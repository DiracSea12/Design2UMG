import argparse
import concurrent.futures
import html
import json
import os
import shutil
import shlex
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify
import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_render_rule as render_rule
import design2umg_validate_run as validate_run_tool
from design2umg_verify_lib import batch_discovery
from design2umg_verify_lib import execution_shards
from design2umg_verify_lib import reference_cache
from design2umg_verify_lib.diagnostics import EvidenceReadError
from design2umg_verify_lib.diagnostics import emit_diagnostic
from design2umg_verify_lib.editor_driver_writer import write_editor_driver_script as write_shared_editor_driver_script
from design2umg_verify_lib.run_history import write_historical_best_validation


NO_REGRESSION_EPSILON = 1.25e-4
NO_REGRESSION_METRICS = (
    "stable_changed_ratio",
    "changed_ratio",
    "stable_mean_abs_error",
    "mean_abs_error",
)
# Structure metrics compared against a baseline run for horizontal tracking only
# (baseline average + per-case deviation); never gate pass/fail on them.
STRUCTURE_BASELINE_METRICS = (
    "expansion_ratio",
    "structure_coverage_ratio",
    "carrier_overhead_ratio",
)
STRUCTURE_BASELINE_DELTA_EPSILON = 1e-4
STRUCTURE_BASELINE_LOWER_IS_BETTER = {
    "expansion_ratio": True,
    "structure_coverage_ratio": False,
    "carrier_overhead_ratio": True,
}
EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = 30.0  # seconds to wait for editor results after process exits
EDITOR_STEP_DRIVER_START_ATTEMPTS = 2
AUTOMATION_GAME_OUTPUT_PREFIX = "/Game/Design2UMG/Automation"
FORMAL_DISCOVERED_CASE_COUNT = 71


def parse_args():
    parser = argparse.ArgumentParser(description="Batch Design2UMG verification from a design directory")
    parser.add_argument("--design-root", required=True, help="Directory containing .pen design files")
    parser.add_argument("--figma-export-root", action="append", default=[], help="Optional local Figma export directory containing design.json and a top-level reference PNG. Can be repeated.")
    parser.add_argument("--output-root", default="", help="Directory for batch outputs")
    parser.add_argument("--reference-root", default="", help="Optional directory containing per-case reference PNGs at <reference-root>/<case-name>/reference.png")
    parser.add_argument("--project", default="", help="Runtime project path. If omitted, DESIGN2UMG_RUNTIME_PROJECT is required. HostProject fallback and design-root inference are not supported.")
    parser.add_argument("--editor", default="", help="UnrealEditor executable path. If omitted, use DESIGN2UMG_UNREAL_EDITOR, or UE_ROOT to resolve Engine/Binaries/Win64/UnrealEditor.exe.")
    parser.add_argument("--dry-run-validate-paths", action="store_true", help="Resolve and validate the runtime project/editor paths, then exit before discovering cases or launching Unreal.")
    parser.add_argument("--reference-provider", choices=["pencil"], default="pencil", help="Reference source for discovered pen cases")
    parser.add_argument("--mcp-server", default="", help="Override Pencil MCP server binary path")
    parser.add_argument("--pencil-app", default="", help="Override Pencil app name. Leave empty for auto-detect from ~/.pencil/apps and running IDE processes.")
    parser.add_argument("--pixel-tolerance", type=int, default=6, help="Per-pixel max-channel tolerance")
    parser.add_argument("--game-output", default="", help="Optional UE asset output base path override passed through to all cases")
    parser.add_argument("--idle-timeout-sec", type=int, default=180, help="Kill the editor if no log or result progress is observed for this many seconds")
    parser.add_argument("--startup-timeout-sec", type=int, default=60, help="Kill a shard early if UnrealEditor shows zero progress right after launch for this many seconds")
    parser.add_argument("--parallel-editors", type=int, default=0, help="How many UnrealEditor shards to run in parallel. 0 means auto.")
    parser.add_argument("--editor-isolation", choices=["shard", "case", "step"], default="shard", help="Editor process isolation level. shard keeps the current batched shard behavior; case runs each discovered case in its own UnrealEditor process; step runs each automation spec in its own UnrealEditor process.")
    parser.add_argument("--post-workers", type=int, default=0, help="How many parallel workers to use for post-render compare/report. 0 means auto.")
    parser.add_argument("--native-self-check", choices=["auto", "off"], default="off", help="Whether to run optional native-size self-check passes. 'off' is faster and does not affect formal scoring metrics.")
    parser.add_argument("--reference-cache", choices=["off", "read", "read-write", "refresh"], default="read-write", help="Reference cache policy for pen+pencil cases without --reference-root. read-write is recommended for faster reruns.")
    parser.add_argument("--reference-cache-root", default="", help="Optional cache root for exported references. Default: <output-root-parent>/_reference_cache")
    parser.add_argument("--include", default="", help="Only run discovered case names matching this regex")
    parser.add_argument("--exclude", default="", help="Skip discovered case names matching this regex")
    parser.add_argument("--log-cmds", default="", help="Optional Unreal -LogCmds payload, for example 'LogDesign2UMG Verbose'")
    return parser.parse_args()


def build_case_args(batch_args, case: dict) -> SimpleNamespace:
    effective_game_output_root = getattr(batch_args, "effective_game_output_root", getattr(batch_args, "game_output", ""))
    return SimpleNamespace(
        mode=case["mode"],
        source=case["source"],
        node=case.get("node", ""),
        reference_provider="png" if (batch_args.reference_root or case.get("reference_path")) else batch_args.reference_provider,
        reference=case.get("reference_path", ""),
        figma_file="",
        figma_node="",
        figma_token="",
        mcp_server=batch_args.mcp_server,
        pencil_app=batch_args.pencil_app,
        pixel_tolerance=batch_args.pixel_tolerance,
        game_output=build_case_game_output_root(effective_game_output_root, case.get("name", "")),
        native_self_check=batch_args.native_self_check,
    )


def normalize_game_output_path(value: str) -> str:
    normalized = (value or "").strip().replace("\\", "/").rstrip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return normalized


def build_default_batch_game_output_root(output_root: Path, batch_start_time: float) -> str:
    output_segment = verify.sanitize_case_name(output_root.name)
    stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(batch_start_time))
    return f"{AUTOMATION_GAME_OUTPUT_PREFIX}/{output_segment}-{stamp}-{os.getpid()}"


def resolve_effective_batch_game_output_root(raw_game_output: str, output_root: Path, batch_start_time: float) -> str:
    explicit_root = normalize_game_output_path(raw_game_output)
    if explicit_root:
        return explicit_root
    return build_default_batch_game_output_root(output_root, batch_start_time)


def build_case_game_output_root(effective_game_output_root: str, case_name: str) -> str:
    normalized_root = normalize_game_output_path(effective_game_output_root)
    if not normalized_root:
        return ""
    case_segment = verify.sanitize_case_name(case_name)
    return f"{normalized_root}/{case_segment}"


def automation_game_output_relative_path(game_output_root: str) -> str | None:
    normalized_root = normalize_game_output_path(game_output_root)
    normalized_prefix = AUTOMATION_GAME_OUTPUT_PREFIX.rstrip("/")
    if normalized_root.lower() == normalized_prefix.lower():
        return ""
    prefix_with_slash = f"{normalized_prefix}/"
    if normalized_root.lower().startswith(prefix_with_slash.lower()):
        return normalized_root[len(prefix_with_slash):].strip("/")
    return None


def has_run_unique_marker(segment: str) -> bool:
    value = (segment or "").strip()
    lower = value.lower()
    if lower.startswith(("_", "run_", "run-", "batch_", "batch-")):
        return True
    if lower.startswith("after") and "unique" in lower:
        return True

    consecutive_digits = 0
    for character in value:
        if character.isdigit():
            consecutive_digits += 1
            if consecutive_digits in {8, 13}:
                return True
        else:
            consecutive_digits = 0
    return False


def validate_explicit_batch_game_output_root(game_output_root: str, cases: list[dict]) -> None:
    relative_path = automation_game_output_relative_path(game_output_root)
    if relative_path is None:
        return

    if not relative_path:
        raise RuntimeError(
            f"Unsafe --game-output: {game_output_root}. Use a unique run-level root under "
            f"{AUTOMATION_GAME_OUTPUT_PREFIX}, for example "
            f"{AUTOMATION_GAME_OUTPUT_PREFIX}/Run_YYYYMMDD-HHMMSS."
        )

    first_segment = relative_path.split("/", 1)[0]
    if not has_run_unique_marker(first_segment):
        raise RuntimeError(
            f"Unsafe --game-output: {game_output_root} does not use a unique run-level automation root. "
            f"Use a unique run-level root such as {AUTOMATION_GAME_OUTPUT_PREFIX}/Run_YYYYMMDD-HHMMSS."
        )


def record_effective_game_output_root(run_scope_path: Path, args) -> None:
    run_scope = verify.load_json_document(run_scope_path)
    run_scope["game_output"] = {
        "explicit": bool(getattr(args, "game_output_explicit", False)),
        "effective_root": getattr(args, "effective_game_output_root", getattr(args, "game_output", "")),
    }
    verify.write_json(run_scope_path, run_scope)


def prepare_external_reference(case_args, case: dict, reference_root: Path, output_dir: Path) -> tuple[Path, dict]:
    case_reference_dir = reference_root / case["name"]
    source_reference_path = case_reference_dir / "reference.png"
    source_structure_path = case_reference_dir / "source_structure.json"
    if not source_reference_path.is_file():
        raise RuntimeError(f"Missing external reference PNG for case {case['name']}: {source_reference_path}")

    reference_path = output_dir / "reference.png"
    verify.shutil.copyfile(source_reference_path, reference_path)

    reference_context = {
        "canonical_source": "pencil_export_nodes_external",
        "provided_reference_path": str(source_reference_path),
        "reference_authority": {
            "available": True,
            "valid": True,
            "reason": "",
            "canonical_source": "pencil_export_nodes_external",
            "canonical_path": str(source_reference_path),
            "export_nodes_path": str(source_reference_path),
            "get_screenshot_path": "",
            "verification_level": "external_export_nodes",
        },
        "provided_vs_canonical": None,
    }
    if source_structure_path.is_file():
        try:
            reference_context["source_structure_live"] = verify.load_json_document(source_structure_path)
            reference_context["source_structure_live_path"] = str(source_structure_path)
        except EvidenceReadError as exc:
            reference_context["source_structure_live_unavailable"] = {
                "reason": exc.reason,
                "path": str(exc.path),
                "detail": exc.detail,
            }
            emit_diagnostic("warning", exc.reason, exc.path, exc.detail)
    return reference_path, reference_context


def prepare_specific_reference(case_args, case: dict, reference_path: Path, output_dir: Path) -> tuple[Path, dict]:
    if not reference_path.is_file():
        raise RuntimeError(f"Missing reference PNG for case {case['name']}: {reference_path}")

    output_reference_path = output_dir / "reference.png"
    verify.shutil.copyfile(reference_path, output_reference_path)
    return output_reference_path, {
        "canonical_source": "figma_export_external_png",
        "provided_reference_path": str(reference_path),
        "reference_authority": {
            "available": True,
            "valid": True,
            "reason": "",
            "canonical_source": "figma_export_external_png",
            "canonical_path": str(reference_path),
            "export_nodes_path": str(reference_path),
            "get_screenshot_path": "",
            "verification_level": "external_export_nodes",
        },
        "provided_vs_canonical": None,
    }


def prepare_case_state(
    case_args,
    case: dict,
    output_root: Path,
    reference_cache_root: Path | None,
    reference_cache_mode: str,
) -> dict:
    output_dir = output_root / case["name"]
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    expected_design_size = verify.get_expected_design_size(case_args.mode, case_args.source, case_args.node)
    restored_from_cache = None
    if reference_cache_mode in {"read", "read-write"}:
        try:
            restored_from_cache = reference_cache.restore_reference_from_cache(case_args, case, output_dir, reference_cache_root)
        except (OSError, RuntimeError, ValueError) as exc:
            emit_diagnostic("warning", "reference_cache_restore_failed", reference_cache_root or "", str(exc))
            restored_from_cache = None

    if restored_from_cache is not None:
        reference_path, reference_context = restored_from_cache
    elif case.get("reference_path"):
        reference_path, reference_context = prepare_specific_reference(case_args, case, Path(case["reference_path"]), output_dir)
    elif case_args.reference:
        reference_path, reference_context = prepare_external_reference(case_args, case, Path(case_args.reference), output_dir)
    else:
        reference_path, reference_context = verify.prepare_reference(case_args, output_dir, expected_design_size)

    if restored_from_cache is None and reference_cache_mode in {"read-write", "refresh"}:
        try:
            reference_cache.save_reference_to_cache(case_args, case, reference_cache_root, reference_path, reference_context)
        except (OSError, RuntimeError, ValueError) as exc:
            emit_diagnostic("warning", "reference_cache_save_failed", reference_cache_root or "", str(exc))

    verify.finalize_reference_context(case_args, reference_path, reference_context)
    # Formal full-runs with an external reference root already carry the authoritative
    # reference image. In that mode, per-case live Pencil MCP structure fetches add a
    # large serial latency and routinely fall back to the local design snapshot anyway.
    # Cache hits are fully local: the .pen parse snapshot matches the live fetch.
    if (
        case_args.mode == "pen"
        and restored_from_cache is None
        and not case_args.reference
        and "source_structure_live" not in reference_context
    ):
        live_source_structure = verify.try_fetch_pencil_source_structure_snapshot(
            case_args.source,
            case_args.node,
            case.get("node_name", ""),
            case_args.mcp_server,
            case_args.pencil_app,
        )
        if live_source_structure:
            reference_context["source_structure_live"] = live_source_structure
    reference_size = verify.read_image_size(reference_path)

    reference_validation = verify.validate_reference_against_design(reference_size, expected_design_size)
    reference_context["reference_validation"] = reference_validation

    candidate_path = output_dir / "candidate.png"
    game_output_root = verify.build_automation_asset_root(output_dir, case_args.game_output, case.get("name", ""))

    state = {
        "case": case,
        "args": case_args,
        "output_dir": output_dir,
        "source_signature": verify.build_file_signature(case_args.source),
        "provided_reference_signature": verify.build_file_signature(case.get("reference_path")),
        "reference_path": reference_path,
        "reference_context": reference_context,
        "reference_validation": reference_validation,
        "expected_design_size": expected_design_size,
        "render_size": expected_design_size,
        "candidate_path": candidate_path,
        "game_output_root": game_output_root,
        "spec_paths": [],
        "steps": {},
        "ue_log_path": output_root / "unreal_batch.log",
    }
    build_case_specs(state)
    return state


def append_spec(state: dict, key: str, filename: str, spec: dict):
    spec_path = state["output_dir"] / filename
    verify.write_json(spec_path, spec)
    state["spec_paths"].append(spec_path)
    state["steps"][key] = {
        "spec_path": spec_path,
        "result_json_path": Path(spec["result_json_path"]),
        "render_png_path": Path(spec["render_png_path"]),
        "log_path": state["ue_log_path"],
        "editor_exit_code": None,
        "fallback_error": "",
    }


def build_native_plan(state: dict):
    expected_design_size = state["expected_design_size"]

    if not expected_design_size:
        return {"available": False, "reason": "expected_design_size_unavailable"}

    primary_width, primary_height = state["render_size"]
    native_width, native_height = expected_design_size
    if native_width <= 0 or native_height <= 0:
        return {
            "available": False,
            "reason": "expected_design_size_invalid",
            "expected_design_size": [native_width, native_height],
        }

    if (native_width, native_height) == (primary_width, primary_height):
        return {
            "available": False,
            "reason": "expected_design_size_matches_primary_render",
            "expected_design_size": [native_width, native_height],
            "primary_render_size": [primary_width, primary_height],
        }

    return {
        "available": True,
        "expected_design_size": [native_width, native_height],
        "primary_render_size": [primary_width, primary_height],
    }


def build_case_specs(state: dict):
    case_args = state["args"]
    candidate_path = state["candidate_path"]
    output_dir = state["output_dir"]
    game_output_root = state["game_output_root"]
    primary_game_output = verify.build_automation_phase_output(game_output_root, "primary")
    native_game_output = verify.build_automation_phase_output(game_output_root, "native")
    native_asset_output = verify.build_automation_phase_output(game_output_root, "native-asset")
    asset_only_output = verify.build_automation_phase_output(game_output_root, "asset")
    mode_map = {
        "pen": "pen",
        "local-figma": "local_figma",
        "figma-source": "figma_source",
        "asset": "asset",
    }

    if case_args.mode == "asset":
        append_spec(
            state,
            "asset_render",
            "automation_asset_spec.json",
            {
                "mode": "asset",
                "source_path": case_args.source,
                "node_id": case_args.node,
                "output_path": asset_only_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "force_reload_package": True,
            },
        )
    else:
        bootstrap_candidate_path = verify.add_name_suffix(candidate_path, ".bootstrap")
        generate_result_path = output_dir / "ue_generate_result.json"
        append_spec(
            state,
            "generation",
            "automation_generate_spec.json",
            {
                "mode": mode_map[case_args.mode],
                "source_path": case_args.source,
                "node_id": case_args.node,
                "output_path": primary_game_output,
                "render_png_path": str(bootstrap_candidate_path),
                "result_json_path": str(generate_result_path),
            },
        )
        append_spec(
            state,
            "asset_render",
            "automation_asset_spec.json",
            {
                "mode": "asset",
                "source_path": f"@generated_asset_from:{generate_result_path}",
                "node_id": "",
                "output_path": primary_game_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "force_reload_package": True,
            },
        )

    if case_args.native_self_check == "off":
        state["native_plan"] = {
            "available": False,
            "reason": "disabled_by_batch_option",
        }
        return

    native_plan = build_native_plan(state)
    state["native_plan"] = native_plan
    if not native_plan["available"]:
        return

    native_width, native_height = native_plan["expected_design_size"]
    native_path = output_dir / "candidate.native.png"
    if case_args.mode == "asset":
        append_spec(
            state,
            "native_asset_render",
            "automation_native_asset_spec.json",
            {
                "mode": "asset",
                "source_path": case_args.source,
                "node_id": "",
                "output_path": native_asset_output,
                "render_png_path": str(native_path),
                "result_json_path": str(output_dir / "ue_native_render_result.json"),
                "force_reload_package": True,
            },
        )
        return

    native_bootstrap_path = verify.add_name_suffix(native_path, ".bootstrap")
    native_generate_result_path = output_dir / "ue_native_generate_result.json"
    append_spec(
        state,
        "native_generation",
        "automation_native_generate_spec.json",
        {
            "mode": mode_map[case_args.mode],
            "source_path": case_args.source,
            "node_id": case_args.node,
            "output_path": native_game_output,
            "render_png_path": str(native_bootstrap_path),
            "result_json_path": str(native_generate_result_path),
        },
    )
    append_spec(
        state,
        "native_asset_render",
        "automation_native_asset_spec.json",
        {
            "mode": "asset",
            "source_path": f"@generated_asset_from:{native_generate_result_path}",
            "node_id": "",
            "output_path": native_asset_output,
            "render_png_path": str(native_path),
            "result_json_path": str(output_dir / "ue_native_render_result.json"),
            "force_reload_package": True,
        },
    )


def write_editor_driver_script(output_root: Path, spec_paths: list[Path], tag: str = "batch") -> Path:
    output_root = output_root.resolve()
    normalized_tag = batch_discovery.sanitize_case_name(tag)
    manifest_path = output_root / f"editor_driver_manifest_{normalized_tag}.json"
    driver_log_path = output_root / f"editor_driver_{normalized_tag}.log"
    script_path = output_root / f"editor_driver_{normalized_tag}.py"
    return write_shared_editor_driver_script(
        script_path,
        manifest_path,
        driver_log_path,
        {"spec_paths": [spec_path.as_posix() for spec_path in spec_paths]},
        "spec_paths",
    )


def get_editor_driver_log_path(driver_script_path: Path) -> Path:
    return driver_script_path.with_suffix(".log")


def editor_driver_started(driver_log_path: Path) -> bool:
    if not driver_log_path.is_file():
        return False
    try:
        return "driver:start" in driver_log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False


def extend_deadline(current_deadline: float, extension_sec: float) -> float:
    return max(current_deadline, time.time() + extension_sec)


class EditorDriverStartError(RuntimeError):
    pass


def run_editor_driver(
    editor_path: str,
    project_path: str,
    driver_script_path: Path,
    result_paths: list[Path],
    log_path: Path,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str = "",
) -> int:
    driver_script_path = driver_script_path.resolve()
    driver_log_path = get_editor_driver_log_path(driver_script_path)
    command = [
        editor_path,
        project_path,
        "-nop4",
        "-NoSound",
        "-nosplash",
        "-NoMutex",
        "-stdout",
        "-FullStdOutLogOutput",
        "-unattended",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    extra_editor_args = os.environ.get("DESIGN2UMG_EDITOR_EXTRA_ARGS", "").strip()
    if extra_editor_args:
        command.extend(shlex.split(extra_editor_args))
    if log_cmds:
        command.append(f"-LogCmds={log_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8") as handle:
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
        process_started_at = time.time()
        last_progress_time = time.time()
        last_result_count = 0
        last_log_size = 0
        observed_progress = False

        while True:
            exit_code = process.poll()
            if exit_code is not None:
                break

            current_result_count = sum(1 for path in result_paths if path.is_file())
            if current_result_count > last_result_count:
                last_result_count = current_result_count
                last_progress_time = time.time()
                observed_progress = True
            if current_result_count >= len(result_paths) and verify.log_indicates_clean_editor_shutdown(log_path):
                process.kill()
                return 0

            if editor_driver_started(driver_log_path):
                last_progress_time = time.time()
                observed_progress = True

            try:
                current_log_size = log_path.stat().st_size
            except FileNotFoundError:
                current_log_size = 0
            if current_log_size > last_log_size:
                last_log_size = current_log_size
                last_progress_time = time.time()
                observed_progress = True

            if not observed_progress and (time.time() - process_started_at > startup_timeout_sec):
                process.kill()
                raise TimeoutError(
                    f"Editor startup made no progress for {startup_timeout_sec}s. "
                    f"result_files={last_result_count}/{len(result_paths)} log={log_path}"
                )

            if time.time() - last_progress_time > idle_timeout_sec:
                if current_result_count >= len(result_paths) and verify.log_indicates_clean_editor_shutdown(log_path):
                    process.kill()
                    return 0
                process.kill()
                raise TimeoutError(
                    f"Editor made no progress for {idle_timeout_sec}s. "
                    f"result_files={last_result_count}/{len(result_paths)} log={log_path}"
                )

            time.sleep(2)

    exit_deadline = time.time() + max(EDITOR_GRACEFUL_EXIT_DEADLINE_SEC, float(idle_timeout_sec))
    while time.time() < exit_deadline:
        current_result_count = sum(1 for path in result_paths if path.is_file())
        if current_result_count >= len(result_paths):
            break
        if current_result_count > last_result_count:
            last_result_count = current_result_count
            exit_deadline = extend_deadline(exit_deadline, EDITOR_GRACEFUL_EXIT_DEADLINE_SEC)
        try:
            current_log_size = log_path.stat().st_size
        except FileNotFoundError:
            current_log_size = 0
        if current_log_size > last_log_size:
            last_log_size = current_log_size
            exit_deadline = extend_deadline(exit_deadline, max(EDITOR_GRACEFUL_EXIT_DEADLINE_SEC, 10.0))
        time.sleep(1)

    if not editor_driver_started(driver_log_path):
        raise EditorDriverStartError(
            f"Editor exited before the automation driver started: "
            f"exit_code={exit_code} driver_log={driver_log_path} log={log_path}"
        )

    if exit_code != 0 and all(path.is_file() for path in result_paths):
        if verify.log_indicates_clean_editor_shutdown(log_path):
            return 0

    return exit_code


def run_execution_shard(
    shard: dict,
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
) -> dict:
    shard_tag = f"shard-{shard['id'] + 1:02d}"
    batch_log_path = output_root / f"unreal_batch_{shard_tag}.log"
    driver_script_path = write_editor_driver_script(output_root, shard["spec_paths"], shard_tag)

    for state in shard["states"]:
        state["ue_log_path"] = batch_log_path
        for step in state["steps"].values():
            step["log_path"] = batch_log_path

    exit_code = run_editor_driver(
        editor_path,
        project_path,
        driver_script_path,
        shard["result_paths"],
        batch_log_path,
        idle_timeout_sec,
        startup_timeout_sec,
        log_cmds,
    )

    for state in shard["states"]:
        for step in state["steps"].values():
            step["editor_exit_code"] = exit_code

    return {
        "shard_id": shard["id"],
        "exit_code": exit_code,
        "log_path": str(batch_log_path),
        "spec_count": len(shard["spec_paths"]),
    }


def run_execution_shards(
    shards: list[dict],
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
) -> list[dict]:
    if len(shards) == 1:
        return [
            run_execution_shard(
                shards[0],
                output_root,
                editor_path,
                project_path,
                idle_timeout_sec,
                startup_timeout_sec,
                log_cmds,
            )
        ]

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(shards)) as executor:
        future_to_shard = {
            executor.submit(
                run_execution_shard,
                shard,
                output_root,
                editor_path,
                project_path,
                idle_timeout_sec,
                startup_timeout_sec,
                log_cmds,
            ): shard
            for shard in shards
        }
        for future in concurrent.futures.as_completed(future_to_shard):
            shard = future_to_shard[future]
            try:
                results.append(future.result())
            except Exception as exc:
                results.append(
                    {
                        "shard_id": shard["id"],
                        "exit_code": 1,
                        "log_path": "",
                        "spec_count": len(shard["spec_paths"]),
                        "error": str(exc),
                    }
                )
    return sorted(results, key=lambda item: item["shard_id"])


def run_execution_cases(
    states: list[dict],
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
) -> list[dict]:
    results = []
    for index, state in enumerate(states):
        case_name = state["case"]["name"]
        case_shard = {
            "id": index,
            "states": [state],
            "spec_paths": list(state["spec_paths"]),
            "result_paths": [step["result_json_path"] for step in state["steps"].values()],
        }
        print(f"[EDITOR_CASE] {case_name}", flush=True)
        verify.ensure_clean_editor()
        result = run_execution_shard(
            case_shard,
            output_root,
            editor_path,
            project_path,
            idle_timeout_sec,
            startup_timeout_sec,
            log_cmds,
        )
        result["editor_isolation"] = "case"
        result["case_name"] = case_name
        results.append(result)
    return results


def load_step_spec(step: dict) -> dict:
    return verify.load_json_document(step["spec_path"])


def ensure_step_dependencies_ready(case_name: str, step_key: str, step: dict) -> None:
    spec = load_step_spec(step)
    source_path = str(spec.get("source_path", ""))
    if not source_path.startswith("@generated_asset_from:"):
        return

    dependency_path = Path(source_path.removeprefix("@generated_asset_from:"))
    if not dependency_path.is_file():
        raise RuntimeError(
            f"Step isolation blocked dependent asset render before generation result existed: "
            f"{case_name}:{step_key} missing generated asset dependency result {dependency_path}"
        )

    dependency_result = verify.load_json_document(dependency_path)
    generated_asset_path = str(dependency_result.get("generated_asset_path", "")).strip()
    if not dependency_result.get("success") or not generated_asset_path:
        raise RuntimeError(
            f"Step isolation blocked dependent asset render before generation produced a generated asset path: "
            f"{case_name}:{step_key} dependency={dependency_path}"
        )


def run_execution_steps(
    states: list[dict],
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
) -> list[dict]:
    results = []
    step_index = 0
    for state in states:
        case_name = state["case"]["name"]
        for step_key, step in state["steps"].items():
            ensure_step_dependencies_ready(case_name, step_key, step)

            step_tag = f"step-{step_index + 1:03d}"
            log_tag = f"{step_tag}-{case_name}-{step_key}"
            log_path = output_root / f"unreal_batch_{batch_discovery.sanitize_case_name(log_tag)}.log"
            driver_script_path = write_editor_driver_script(output_root, [step["spec_path"]], step_tag)
            step["log_path"] = log_path
            state["ue_log_path"] = log_path
            print(f"[EDITOR_STEP] {case_name}:{step_key} log={log_path}", flush=True)
            exit_code = 0
            for attempt in range(1, EDITOR_STEP_DRIVER_START_ATTEMPTS + 1):
                verify.ensure_clean_editor()
                try:
                    exit_code = run_editor_driver(
                        editor_path,
                        project_path,
                        driver_script_path,
                        [step["result_json_path"]],
                        log_path,
                        idle_timeout_sec,
                        startup_timeout_sec,
                        log_cmds,
                    )
                    break
                except EditorDriverStartError:
                    if attempt >= EDITOR_STEP_DRIVER_START_ATTEMPTS:
                        raise
                    print(f"[EDITOR_STEP_RETRY] {case_name}:{step_key} driver did not start; retrying", flush=True)
                    time.sleep(5)
            step["editor_exit_code"] = exit_code
            if not step["result_json_path"].is_file():
                raise RuntimeError(
                    f"Step isolation editor did not produce result json: "
                    f"{case_name}:{step_key} exit_code={exit_code} "
                    f"result={step['result_json_path']} log={log_path}"
                )
            results.append(
                {
                    "shard_id": step_index,
                    "exit_code": exit_code,
                    "log_path": str(log_path),
                    "spec_count": 1,
                    "editor_isolation": "step",
                    "case_name": case_name,
                    "step": step_key,
                }
            )
            step_index += 1
    return results


def build_missing_step_states(states: list[dict]) -> list[dict]:
    missing_states = []
    for state in states:
        missing_steps = {
            key: step
            for key, step in state["steps"].items()
            if not step["result_json_path"].is_file()
        }
        if missing_steps:
            missing_states.append(
                {
                    "case": state["case"],
                    "output_dir": state["output_dir"],
                    "spec_paths": [step["spec_path"] for step in missing_steps.values()],
                    "steps": missing_steps,
                }
            )
    return missing_states


def build_missing_case_states(states: list[dict]) -> list[dict]:
    return [
        state
        for state in states
        if any(not step["result_json_path"].is_file() for step in state["steps"].values())
    ]


def build_isolation_summary_shards(states: list[dict], editor_isolation: str) -> list[dict]:
    return [
        {
            "id": 0,
            "states": states,
            "spec_paths": [step["spec_path"] for state in states for step in state["steps"].values()],
            "result_paths": [step["result_json_path"] for state in states for step in state["steps"].values()],
            "editor_isolation": editor_isolation,
        }
    ]


def rerun_missing_steps(
    states: list[dict],
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
    editor_isolation: str = "shard",
) -> dict | None:
    missing_steps = []
    for state in states:
        for step in state["steps"].values():
            if not step["result_json_path"].is_file():
                missing_steps.append(step)

    if not missing_steps:
        return None

    if editor_isolation == "case":
        case_results = run_execution_cases(
            build_missing_case_states(states),
            output_root,
            editor_path,
            project_path,
            idle_timeout_sec,
            startup_timeout_sec,
            log_cmds,
        )
        return {
            "shard_id": -1,
            "exit_code": next((item["exit_code"] for item in case_results if item["exit_code"] != 0), 0),
            "log_path": "",
            "spec_count": sum(int(item.get("spec_count", 0)) for item in case_results),
            "retry": True,
            "editor_isolation": "case",
            "cases": case_results,
        }

    if editor_isolation == "step":
        step_results = run_execution_steps(
            build_missing_step_states(states),
            output_root,
            editor_path,
            project_path,
            idle_timeout_sec,
            startup_timeout_sec,
            log_cmds,
        )
        return {
            "shard_id": -1,
            "exit_code": next((item["exit_code"] for item in step_results if item["exit_code"] != 0), 0),
            "log_path": "",
            "spec_count": len(missing_steps),
            "retry": True,
            "editor_isolation": "step",
            "steps": step_results,
        }

    spec_paths = [step["spec_path"] for step in missing_steps]
    result_paths = [step["result_json_path"] for step in missing_steps]
    retry_tag = "retry-missing"
    batch_log_path = output_root / "unreal_batch_retry_missing.log"
    driver_script_path = write_editor_driver_script(output_root, spec_paths, retry_tag)
    verify.ensure_clean_editor()
    exit_code = run_editor_driver(
        editor_path,
        project_path,
        driver_script_path,
        result_paths,
        batch_log_path,
        idle_timeout_sec,
        startup_timeout_sec,
        log_cmds,
    )
    for step in missing_steps:
        step["editor_exit_code"] = exit_code
        step["log_path"] = batch_log_path

    return {
        "shard_id": -1,
        "exit_code": exit_code,
        "log_path": str(batch_log_path),
        "spec_count": len(spec_paths),
        "retry": True,
    }


def load_step_result(step: dict, batch_exit_code: int, batch_log_path: Path):
    if step.get("result"):
        return step["result"]
    result_path = step["result_json_path"]
    if not result_path.is_file():
        return None
    return {
        "editor_exit_code": step["editor_exit_code"],
        "log_path": str(step["log_path"]),
        "spec_path": str(step["spec_path"]),
        "ue_result": verify.load_ue_result(result_path),
    }


def ensure_driver_completed_all_steps(states: list[dict], batch_log_path: Path):
    missing_steps = []
    for state in states:
        for key, step in state["steps"].items():
            if not step["result_json_path"].is_file():
                missing_steps.append(f"{state['case']['name']}:{key}")

    if not missing_steps:
        return

    missing_preview = ", ".join(missing_steps[:8])
    if len(missing_steps) > 8:
        missing_preview += f", ... total={len(missing_steps)}"

    raise RuntimeError(
        "Editor driver did not finish all verification steps in a single session. "
        f"Missing results: {missing_preview}. "
        f"Batch log: {batch_log_path}"
    )


def write_render_failure_report(state: dict, ue_result: dict | None, render_chain: dict, verdict: str, message: str) -> Path:
    output_dir = state["output_dir"]
    report_path = output_dir / "compare_report.json"
    candidate_path = state["candidate_path"]
    report = {
        "verdict": verdict,
        "compare_skipped": True,
        "message": message,
        "reference_provider": state["args"].reference_provider,
        "effective_reference_provider": state["reference_context"].get("effective_reference_provider"),
        "effective_reference_path": state["reference_context"].get("effective_reference_path"),
        "reference_png_path": str(state["reference_path"]),
        "candidate_png_path": str(candidate_path),
        "pixel_tolerance": state["args"].pixel_tolerance,
        "changed_ratio": None,
        "stable_changed_ratio": None,
        "mean_abs_error": None,
        "stable_mean_abs_error": None,
        "size_match": False,
        "reference_size": list(verify.read_image_size(state["reference_path"])),
        "candidate_size": list(verify.read_image_size(candidate_path)) if candidate_path.is_file() else None,
        "diff_bbox": None,
        "stable_diff_bbox": None,
        "source_signature": state.get("source_signature"),
        "provided_reference_signature": state["reference_context"].get("provided_reference_signature") or state.get("provided_reference_signature"),
        "effective_reference_signature": state["reference_context"].get("effective_reference_signature"),
        "ue_result": ue_result,
        "reference_context": state["reference_context"],
        "reference_validation": state["reference_validation"],
        "render_chain": render_chain,
    }
    verify.write_json(report_path, report)
    return report_path


def load_baseline_case_metrics(case_dir: Path | None, pixel_tolerance: int) -> dict:
    if not case_dir or not case_dir.is_dir():
        return {
            "available": False,
            "reason": "baseline_case_dir_missing",
        }

    report_path = case_dir / "compare_report.json"
    report = verify.load_json_document(report_path) if report_path.is_file() else {}
    baseline_rule = render_rule.evaluate_report_render_size_rule(report_path)
    if not baseline_rule.get("compatible"):
        return {
            "available": False,
            "reason": "baseline_render_size_rule_incompatible",
            "render_size_rule": baseline_rule,
            "case_dir": str(case_dir),
            "report_path": str(report_path) if report_path.is_file() else "",
        }

    reference_png_path = Path(report.get("reference_png_path") or (case_dir / "reference.png"))
    candidate_png_path = Path(report.get("candidate_png_path") or (case_dir / "candidate.png"))
    report_metrics = verify.extract_compare_metrics(report)
    if report_metrics:
        changed_ratio = report_metrics.get("changed_ratio")
        stable_changed_ratio = report_metrics.get("stable_changed_ratio")
        mean_abs_error = report_metrics.get("mean_abs_error")
        stable_mean_abs_error = report_metrics.get("stable_mean_abs_error")
    else:
        image_metrics = verify.compare_image_paths(reference_png_path, candidate_png_path, pixel_tolerance)
        if image_metrics:
            changed_ratio = image_metrics.get("changed_ratio")
            stable_changed_ratio = image_metrics.get("stable_changed_ratio")
            mean_abs_error = image_metrics.get("mean_abs_error")
            stable_mean_abs_error = image_metrics.get("stable_mean_abs_error")
        else:
            changed_ratio = report.get("changed_ratio")
            stable_changed_ratio = report.get("stable_changed_ratio")
            mean_abs_error = report.get("mean_abs_error")
            stable_mean_abs_error = report.get("stable_mean_abs_error")

    metric_values = {
        "changed_ratio": changed_ratio,
        "stable_changed_ratio": stable_changed_ratio,
        "mean_abs_error": mean_abs_error,
        "stable_mean_abs_error": stable_mean_abs_error,
    }
    missing_metrics = [metric_name for metric_name in NO_REGRESSION_METRICS if metric_values[metric_name] is None]
    available = not missing_metrics
    if available:
        reason = ""
    elif len(missing_metrics) == len(NO_REGRESSION_METRICS):
        reason = "baseline_metrics_unavailable"
    else:
        reason = "baseline_metrics_incomplete"
    return {
        "available": available,
        "reason": reason,
        "case_dir": str(case_dir),
        "report_path": str(report_path) if report_path.is_file() else "",
        "reference_png_path": str(reference_png_path) if reference_png_path.is_file() else "",
        "candidate_png_path": str(candidate_png_path) if candidate_png_path.is_file() else "",
        "changed_ratio": changed_ratio,
        "stable_changed_ratio": stable_changed_ratio,
        "mean_abs_error": mean_abs_error,
        "stable_mean_abs_error": stable_mean_abs_error,
    }


def load_baseline_structure_metrics(case_dir: Path | None) -> dict:
    if not case_dir or not case_dir.is_dir():
        return {"available": False, "reason": "baseline_case_dir_missing"}

    report_path = case_dir / "compare_report.json"
    report = verify.load_json_document(report_path) if report_path.is_file() else {}
    metrics = ((report.get("structure_validation") or {}).get("metrics")) or {}
    values = {metric_name: metrics.get(metric_name) for metric_name in STRUCTURE_BASELINE_METRICS}
    missing = [metric_name for metric_name, value in values.items() if value is None]
    result = {"available": not missing, **values}
    result["reason"] = "" if not missing else "baseline_structure_metrics_unavailable"
    return result


def evaluate_no_regression(report: dict, case_dir: Path | None, pixel_tolerance: int) -> dict:
    baseline = load_baseline_case_metrics(case_dir, pixel_tolerance)
    baseline_missing_metrics = [metric_name for metric_name in NO_REGRESSION_METRICS if baseline.get(metric_name) is None]
    baseline_available = bool(baseline.get("available")) and not baseline_missing_metrics
    result = {
        "available": baseline_available,
        "passed": False,
        "baseline": baseline,
        "current": {
            "changed_ratio": report.get("changed_ratio"),
            "stable_changed_ratio": report.get("stable_changed_ratio"),
            "mean_abs_error": report.get("mean_abs_error"),
            "stable_mean_abs_error": report.get("stable_mean_abs_error"),
        },
        "decisive_metric": "",
        "decisive_delta": None,
        "visual_regressed": False,
        "structure_guard_passed": True,
        "reasons": [],
    }
    if not baseline_available:
        if baseline.get("available"):
            reason = "baseline_metrics_unavailable" if len(baseline_missing_metrics) == len(NO_REGRESSION_METRICS) else "baseline_metrics_incomplete"
        else:
            reason = baseline.get("reason", "baseline_unavailable")
        result["reasons"].append(reason)
        return result

    current_missing_metrics = [metric_name for metric_name in NO_REGRESSION_METRICS if result["current"].get(metric_name) is None]
    if current_missing_metrics:
        result["available"] = False
        reason = "current_metrics_unavailable" if len(current_missing_metrics) == len(NO_REGRESSION_METRICS) else "current_metrics_incomplete"
        result["reasons"].append(reason)
        return result

    first_metric = ""
    first_delta = None
    regressed_metric = ""
    regressed_delta = None
    for metric_name in NO_REGRESSION_METRICS:
        current_value = result["current"].get(metric_name)
        baseline_value = baseline.get(metric_name)
        if current_value is None or baseline_value is None:
            continue
        delta = current_value - baseline_value
        if not first_metric:
            first_metric = metric_name
            first_delta = delta
        if delta > NO_REGRESSION_EPSILON and not regressed_metric:
            regressed_metric = metric_name
            regressed_delta = delta
    if not first_metric:
        result["reasons"].append("comparison_metric_unavailable")
        return result

    no_regression_metric_passed = not regressed_metric
    decisive_metric = regressed_metric or first_metric
    decisive_delta = regressed_delta if regressed_metric else first_delta

    structure_validation = report.get("structure_validation") or {}
    structure_guard_passed = structure_validation.get("passed") if structure_validation.get("required") else True
    result["passed"] = no_regression_metric_passed and structure_guard_passed
    result["decisive_metric"] = decisive_metric
    result["decisive_delta"] = decisive_delta
    result["visual_regressed"] = not no_regression_metric_passed
    result["structure_guard_passed"] = structure_guard_passed
    if not no_regression_metric_passed:
        result["reasons"].append(f"visual_regressed:{decisive_metric}")
    if not structure_guard_passed:
        result["reasons"].append("structure_guard_failed")
    return result


def finalize_native_plan(state: dict, batch_exit_code: int, batch_log_path: Path):
    native_plan = dict(state["native_plan"])
    if not native_plan.get("available"):
        return native_plan

    if state["args"].mode == "asset":
        asset_result = load_step_result(state["steps"]["native_asset_render"], batch_exit_code, batch_log_path)
        native_plan["asset_render"] = asset_result
        native_plan["generation_vs_asset"] = None
        native_plan["reason"] = "asset_mode_no_generation_pass"
        return native_plan

    native_generation = load_step_result(state["steps"]["native_generation"], batch_exit_code, batch_log_path)
    native_asset = load_step_result(state["steps"]["native_asset_render"], batch_exit_code, batch_log_path)
    native_plan["generation"] = native_generation
    native_plan["asset_render"] = native_asset
    native_bootstrap_path = state["steps"]["native_generation"]["render_png_path"]
    native_path = state["steps"]["native_asset_render"]["render_png_path"]
    native_plan["generation_vs_asset"] = verify.compare_image_paths(
        native_bootstrap_path,
        native_path,
        state["args"].pixel_tolerance,
    )
    return native_plan


def process_case_result(state: dict, batch_exit_code: int, batch_log_path: Path) -> dict:
    args = state["args"]
    reference_validation = state["reference_validation"]
    reference_context = state["reference_context"]
    candidate_path = state["candidate_path"]
    fallback_errors = [step.get("fallback_error", "") for step in state["steps"].values() if step.get("fallback_error")]

    if args.mode == "asset":
        asset_result = load_step_result(state["steps"]["asset_render"], batch_exit_code, batch_log_path)
        ue_result = asset_result["ue_result"] if asset_result else None
        render_chain = {
            "final_render_mode": "asset",
            "asset_render": asset_result,
            "generation_vs_asset": None,
            "native_self_check": finalize_native_plan(state, batch_exit_code, batch_log_path),
        }
    else:
        generation_result = load_step_result(state["steps"]["generation"], batch_exit_code, batch_log_path)
        asset_result = load_step_result(state["steps"]["asset_render"], batch_exit_code, batch_log_path)
        ue_result = asset_result["ue_result"] if asset_result else None
        bootstrap_candidate_path = state["steps"]["generation"]["render_png_path"]
        render_chain = {
            "final_render_mode": "asset_reload",
            "generation": generation_result,
            "asset_render": asset_result,
            "generation_vs_asset": verify.compare_image_paths(
                bootstrap_candidate_path,
                candidate_path,
                args.pixel_tolerance,
            ),
            "generation_vs_asset_structure": verify.compare_structure_snapshots(
                verify.get_result_structure(generation_result, "asset_structure"),
                verify.get_result_structure(asset_result, "asset_structure"),
            ),
            "native_self_check": finalize_native_plan(state, batch_exit_code, batch_log_path),
        }

    if not ue_result or not ue_result.get("success") or not candidate_path.is_file():
        failure_message = "Automation render did not produce a valid candidate image"
        if fallback_errors:
            failure_message += f" | fallback_errors={' ; '.join(fallback_errors)}"
        report_path = write_render_failure_report(
            state,
            ue_result,
            render_chain,
            "render_failed",
            failure_message,
        )
        return {
            "name": state["case"]["name"],
            "blocked": True,
            "report_path": str(report_path),
            "verdict": "render_failed",
        }

    reference_validation = verify.apply_render_size_authority(
        reference_context,
        state["reference_path"],
        ue_result,
        candidate_path,
        args.pixel_tolerance,
    )
    state["reference_validation"] = reference_validation

    reference_authority = reference_context.get("reference_authority") or {}
    if args.mode == "pen" and reference_validation["valid"] and reference_authority.get("available"):
        if reference_authority.get("verification_level") not in {"strong", "external_export_nodes"}:
            report_path = verify.write_invalid_reference_report(
                state["output_dir"],
                args,
                state["reference_path"],
                candidate_path,
                ue_result,
                batch_exit_code,
                reference_validation,
                reference_context,
                render_chain,
                verdict="unverified_reference",
            )
            return {
                "name": state["case"]["name"],
                "blocked": True,
                "report_path": str(report_path),
                "verdict": "unverified_reference",
                "verification_level": reference_authority.get("verification_level"),
                "canonical_source": reference_authority.get("canonical_source") or reference_context.get("canonical_source"),
                "reason": reference_authority.get("reason") or reference_validation.get("reason") or "unverified_reference",
            }

    if not reference_validation["valid"]:
        report_path = verify.write_invalid_reference_report(
            state["output_dir"],
            args,
            state["reference_path"],
            candidate_path,
            ue_result,
            batch_exit_code,
            reference_validation,
            reference_context,
            render_chain,
        )
        return {
            "name": state["case"]["name"],
            "blocked": True,
            "report_path": str(report_path),
            "verdict": "invalid_reference",
            "verification_level": reference_authority.get("verification_level"),
            "canonical_source": reference_authority.get("canonical_source") or reference_context.get("canonical_source"),
            "reason": reference_validation.get("reason") or reference_authority.get("reason") or "invalid_reference",
        }

    report_path = verify.compare_and_report(
        args,
        state["output_dir"],
        state["reference_path"],
        candidate_path,
        ue_result,
        batch_exit_code,
        reference_context,
        render_chain,
    )
    report = verify.load_json_document(report_path)
    no_regression = evaluate_no_regression(report, state.get("baseline_case_dir"), args.pixel_tolerance)
    report["no_regression"] = no_regression
    structure_current = dict.fromkeys(STRUCTURE_BASELINE_METRICS)
    structure_metrics = ((report.get("structure_validation") or {}).get("metrics")) or {}
    for metric_name in STRUCTURE_BASELINE_METRICS:
        structure_current[metric_name] = structure_metrics.get(metric_name)
    structure_baseline = load_baseline_structure_metrics(state.get("baseline_case_dir"))
    structure_delta = {
        metric_name: (
            None
            if structure_current[metric_name] is None or structure_baseline.get(metric_name) is None
            else round(structure_current[metric_name] - structure_baseline[metric_name], 6)
        )
        for metric_name in STRUCTURE_BASELINE_METRICS
    }
    report["structure_baseline"] = {
        "available": structure_baseline.get("available"),
        "reason": structure_baseline.get("reason"),
        "current": structure_current,
        "baseline": {metric_name: structure_baseline.get(metric_name) for metric_name in STRUCTURE_BASELINE_METRICS},
        "delta": structure_delta,
    }
    verify.write_json(Path(report_path), report)
    return {
        "name": state["case"]["name"],
        "blocked": False,
        "report_path": str(report_path),
        "changed_ratio": report.get("changed_ratio"),
        "stable_changed_ratio": report.get("stable_changed_ratio"),
        "mae": report.get("mean_abs_error"),
        "stable_mae": report.get("stable_mean_abs_error"),
        "verification_level": (reference_context.get("reference_authority") or {}).get("verification_level"),
        "generation_vs_asset_changed_ratio": (render_chain.get("generation_vs_asset") or {}).get("changed_ratio"),
        "structure_passed": (report.get("structure_validation") or {}).get("passed"),
        "structure_required": (report.get("structure_validation") or {}).get("required"),
        "structure_available": (report.get("structure_validation") or {}).get("available"),
        "structure_reasons": (report.get("structure_validation") or {}).get("reasons"),
        "tree_correspondence": (report.get("structure_validation") or {}).get("tree_correspondence"),
        "no_regression_required": state.get("baseline_case_dir") is not None,
        "no_regression_available": no_regression.get("available"),
        "no_regression_passed": no_regression.get("passed"),
        "no_regression_reasons": no_regression.get("reasons"),
        "baseline_changed_ratio": (no_regression.get("baseline") or {}).get("changed_ratio"),
        "baseline_stable_changed_ratio": (no_regression.get("baseline") or {}).get("stable_changed_ratio"),
        "baseline_mae": (no_regression.get("baseline") or {}).get("mean_abs_error"),
        "baseline_stable_mae": (no_regression.get("baseline") or {}).get("stable_mean_abs_error"),
        "changed_ratio_delta": (
            None
            if report.get("changed_ratio") is None or (no_regression.get("baseline") or {}).get("changed_ratio") is None
            else report.get("changed_ratio") - (no_regression.get("baseline") or {}).get("changed_ratio")
        ),
        "stable_changed_ratio_delta": (
            None
            if report.get("stable_changed_ratio") is None or (no_regression.get("baseline") or {}).get("stable_changed_ratio") is None
            else report.get("stable_changed_ratio") - (no_regression.get("baseline") or {}).get("stable_changed_ratio")
        ),
        "mae_delta": (
            None
            if report.get("mean_abs_error") is None or (no_regression.get("baseline") or {}).get("mean_abs_error") is None
            else report.get("mean_abs_error") - (no_regression.get("baseline") or {}).get("mean_abs_error")
        ),
        "stable_mae_delta": (
            None
            if report.get("stable_mean_abs_error") is None or (no_regression.get("baseline") or {}).get("stable_mean_abs_error") is None
            else report.get("stable_mean_abs_error") - (no_regression.get("baseline") or {}).get("stable_mean_abs_error")
        ),
        "no_regression_decisive_metric": no_regression.get("decisive_metric"),
        "no_regression_decisive_delta": no_regression.get("decisive_delta"),
        "structure_baseline": {
            "available": structure_baseline.get("available"),
            "current": structure_current,
            "baseline": {metric_name: structure_baseline.get(metric_name) for metric_name in STRUCTURE_BASELINE_METRICS},
            "delta": structure_delta,
        },
        "structure_baseline_available": structure_baseline.get("available"),
        "structure_baseline_reason": structure_baseline.get("reason"),
        "structure_expansion_ratio": structure_current["expansion_ratio"],
        "structure_coverage_ratio": structure_current["structure_coverage_ratio"],
        "structure_carrier_overhead_ratio": structure_current["carrier_overhead_ratio"],
        "structure_expansion_ratio_delta": structure_delta["expansion_ratio"],
        "structure_coverage_ratio_delta": structure_delta["structure_coverage_ratio"],
        "structure_carrier_overhead_ratio_delta": structure_delta["carrier_overhead_ratio"],
        "source_signature": report.get("source_signature"),
        "provided_reference_signature": report.get("provided_reference_signature"),
        "effective_reference_signature": report.get("effective_reference_signature"),
    }


def process_case_results(
    states: list[dict],
    batch_exit_code: int,
    batch_log_path: Path,
    worker_count: int,
) -> list[dict]:
    if not states:
        return []
    if worker_count <= 1:
        return [process_case_result(state, batch_exit_code, batch_log_path) for state in states]

    ordered_results = [None] * len(states)
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as executor:
        future_to_index = {
            executor.submit(process_case_result, state, batch_exit_code, batch_log_path): index
            for index, state in enumerate(states)
        }
        for future in concurrent.futures.as_completed(future_to_index):
            index = future_to_index[future]
            ordered_results[index] = future.result()

    return [item for item in ordered_results if item is not None]


def summarize_structure_baseline(case_results: list[dict]) -> dict:
    per_metric = {}
    for metric_name in STRUCTURE_BASELINE_METRICS:
        baseline_values: list[float] = []
        current_values: list[float] = []
        deltas: list[float] = []
        for item in case_results:
            block = item.get("structure_baseline") or {}
            baseline = (block.get("baseline") or {}).get(metric_name)
            current = (block.get("current") or {}).get(metric_name)
            if baseline is None or current is None:
                continue
            baseline_values.append(baseline)
            current_values.append(current)
            deltas.append(current - baseline)

        stats: dict = {"compared": len(deltas)}
        if deltas:
            lower_is_better = STRUCTURE_BASELINE_LOWER_IS_BETTER[metric_name]
            improved = 0
            regressed = 0
            for delta in deltas:
                if abs(delta) <= STRUCTURE_BASELINE_DELTA_EPSILON:
                    continue
                got_better = delta < 0 if lower_is_better else delta > 0
                improved += 1 if got_better else 0
                regressed += 0 if got_better else 1
            stats.update(
                {
                    "baseline_avg": round(sum(baseline_values) / len(baseline_values), 4),
                    "current_avg": round(sum(current_values) / len(current_values), 4),
                    "avg_delta": round(sum(deltas) / len(deltas), 6),
                    "improved": improved,
                    "regressed": regressed,
                }
            )
        per_metric[metric_name] = stats
    return {
        "delta_epsilon": STRUCTURE_BASELINE_DELTA_EPSILON,
        "compared_cases": sum(
            1
            for item in case_results
            if (item.get("structure_baseline") or {}).get("available")
        ),
        "metrics": per_metric,
    }


def summarize_tree_correspondence(case_results: list[dict]) -> dict:
    metric_names = (
        "identity_ratio",
        "named_parent_path_ratio",
        "sibling_order_ratio",
        "kind_ratio",
        "semantic_kind_ratio",
    )
    values_by_metric = {metric_name: [] for metric_name in metric_names}
    available_cases = 0
    mismatch_classes: dict[str, int] = {}
    for item in case_results:
        correspondence = item.get("tree_correspondence") or {}
        if not correspondence.get("available"):
            continue
        available_cases += 1
        for metric_name in metric_names:
            value = correspondence.get(metric_name)
            if value is not None:
                values_by_metric[metric_name].append(float(value))
        for class_name, count in (correspondence.get("kind_mismatch_classes") or {}).items():
            mismatch_classes[class_name] = mismatch_classes.get(class_name, 0) + int(count)

    metrics = {}
    for metric_name, values in values_by_metric.items():
        metrics[metric_name] = {
            "compared": len(values),
            "average": round(sum(values) / len(values), 4) if values else None,
            "minimum": round(min(values), 4) if values else None,
            "below_full_match": sum(value < 1.0 for value in values),
        }
    return {
        "available_cases": available_cases,
        "metrics": metrics,
        "kind_mismatch_classes": mismatch_classes,
    }


def build_batch_summary(
    *,
    args,
    editor_exit_code: int,
    editor_exit_codes: list[int],
    shards: list[dict],
    shard_results: list[dict],
    post_workers: int,
    design_root: Path,
    output_root: Path,
    run_scope_path: Path,
    reference_cache_root: Path | None,
    batch_start_time: float,
    prep_completed_at: float,
    editor_completed_at: float,
    postprocess_completed_at: float,
    case_results: list[dict],
    completeness: dict,
) -> dict:
    no_regression_cases = [item for item in case_results if item.get("no_regression_available")]
    no_regression_passed = sum(1 for item in no_regression_cases if item.get("no_regression_passed"))
    no_regression_required_unavailable = sum(
        1
        for item in case_results
        if item.get("no_regression_required") and not item.get("no_regression_available")
    )
    blocked_cases = [item for item in case_results if item.get("blocked")]
    return {
        "total": len(case_results),
        "editor_exit_code": editor_exit_code,
        "editor_exit_codes": editor_exit_codes,
        "parallel_editors_requested": args.parallel_editors,
        "parallel_editors_used": len(shards),
        "editor_isolation": args.editor_isolation,
        "idle_timeout_sec": args.idle_timeout_sec,
        "startup_timeout_sec": args.startup_timeout_sec,
        "execution_shards": shard_results,
        "post_workers_requested": args.post_workers,
        "post_workers_used": post_workers,
        "design_root": str(design_root),
        "output_root": str(output_root),
        "game_output": {
            "explicit": bool(getattr(args, "game_output_explicit", False)),
            "effective_root": getattr(args, "effective_game_output_root", getattr(args, "game_output", "")),
        },
        "run_scope": batch_discovery.classify_run_scope(args.include, args.exclude),
        "run_scope_path": str(run_scope_path),
        "reference_cache": {
            "mode": args.reference_cache,
            "root": str(reference_cache_root) if reference_cache_root else "",
        },
        "native_self_check_mode": args.native_self_check,
        "phase_durations_sec": {
            "total_until_summary": max(0.0, postprocess_completed_at - batch_start_time),
            "prepare_cases": max(0.0, prep_completed_at - batch_start_time),
            "editor_execution": max(0.0, editor_completed_at - prep_completed_at),
            "postprocess_reports": max(0.0, postprocess_completed_at - editor_completed_at),
        },
        "completeness": completeness,
        "blocked_cases": len(blocked_cases),
        "no_regression": {
            "available": len(no_regression_cases),
            "passed": no_regression_passed,
            "failed": len(no_regression_cases) - no_regression_passed + no_regression_required_unavailable,
        },
        "structure_metrics": summarize_structure_baseline(case_results),
        "tree_correspondence": summarize_tree_correspondence(case_results),
        "cases": case_results,
    }


def build_batch_timing_history_entry(output_root: Path, summary: dict, *, recorded_at_utc: str | None = None) -> dict:
    return {
        "recorded_at_utc": recorded_at_utc or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "run_root": str(output_root),
        "run_scope": summary.get("run_scope", ""),
        "total_cases": summary.get("total", 0),
        "blocked_cases": summary.get("blocked_cases", 0),
        "phase_durations_sec": dict(summary.get("phase_durations_sec", {})),
        "no_regression": dict(summary.get("no_regression", {})),
    }


def append_batch_timing_history(automation_root: Path, entry: dict) -> Path:
    automation_root.mkdir(parents=True, exist_ok=True)
    history_path = automation_root / "batch_timing_history.jsonl"
    with open(history_path, "a", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(entry, ensure_ascii=False, sort_keys=True))
        handle.write("\n")
    return history_path


def write_visual_review_index(output_root: Path, case_results: list[dict]) -> Path:
    output_root = output_root.resolve()

    def image_href(path: Path) -> str:
        if not path.is_file():
            return ""
        try:
            return path.resolve().relative_to(output_root).as_posix()
        except ValueError:
            return path.resolve().as_uri()

    sections = []
    for result in case_results:
        report_path = Path(result["report_path"])
        case_dir = report_path.parent
        image_columns = (
            ("Design reference", case_dir / "reference.png"),
            ("UMG render", case_dir / "candidate.png"),
            ("Difference overlay", case_dir / "diff_overlay.png"),
        )
        figures = []
        for label, image_path in image_columns:
            href = image_href(image_path)
            if href:
                content = f'<a href="{html.escape(href)}"><img src="{html.escape(href)}" loading="lazy" alt="{html.escape(label)}"></a>'
            else:
                content = '<div class="missing">Not available</div>'
            figures.append(f'<figure><figcaption>{html.escape(label)}</figcaption>{content}</figure>')

        stable_ratio = result.get("stable_changed_ratio")
        stable_mae = result.get("stable_mae")
        metrics = []
        if stable_ratio is not None:
            metrics.append(f"stable changed: {stable_ratio:.4%}")
        if stable_mae is not None:
            metrics.append(f"stable error: {stable_mae:.6f}")
        if result.get("blocked"):
            metrics.append(f"blocked: {result.get('reason') or 'unknown'}")

        sections.append(
            '<section class="case">'
            f'<h2>{html.escape(result["name"])}</h2>'
            f'<p>{html.escape(" | ".join(metrics))}</p>'
            f'<div class="images">{"".join(figures)}</div>'
            '</section>'
        )

    review_path = output_root / "visual_review.html"
    document = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Design2UMG Visual Review</title>
<style>
body {{ margin: 0; background: #f4f5f7; color: #17191d; font: 14px/1.5 Arial, sans-serif; }}
header {{ padding: 20px 24px; background: #17191d; color: white; }}
h1, h2, p {{ margin: 0; }}
h1 {{ font-size: 22px; }}
.case {{ padding: 20px 24px 28px; border-bottom: 1px solid #c9cdd3; }}
.case h2 {{ font-size: 17px; overflow-wrap: anywhere; }}
.case p {{ min-height: 21px; margin: 4px 0 12px; color: #555b65; }}
.images {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; }}
figure {{ min-width: 0; margin: 0; }}
figcaption {{ margin-bottom: 6px; font-weight: 700; }}
img, .missing {{ display: block; width: 100%; aspect-ratio: 16 / 9; object-fit: contain; border: 1px solid #aeb4bd; background: white; box-sizing: border-box; }}
.missing {{ display: grid; place-items: center; color: #6b7280; }}
@media (max-width: 900px) {{ .images {{ grid-template-columns: 1fr; }} }}
</style>
</head>
<body>
<header><h1>Design2UMG Visual Review</h1></header>
<main>{"".join(sections)}</main>
</body>
</html>
"""
    review_path.write_text(document, encoding="utf-8", newline="\n")
    return review_path


def main():
    batch_start_time = time.time()
    args = parse_args()
    args.editor = verify.resolve_unreal_editor_path(args.editor)
    design_root = Path(args.design_root).resolve()
    if not design_root.is_dir():
        raise RuntimeError(f"Design root not found: {design_root}")
    effective_figma_export_roots = batch_discovery.resolve_default_figma_export_roots(design_root, args.figma_export_root)

    runtime_args = SimpleNamespace(
        project=args.project,
        editor=args.editor,
    )

    runtime_args.project, project_plugin_root = verify.prepare_runtime_project(
        runtime_args,
        [str(design_root), *effective_figma_export_roots],
    )
    runtime_args.project_plugin_root = project_plugin_root
    runtime_args.log_cmds = args.log_cmds
    if args.dry_run_validate_paths:
        print("[OK] Design2UMG batch verify paths resolved.")
        print(f"project={runtime_args.project}")
        print(f"editor={args.editor}")
        print(f"project_plugin_root={project_plugin_root}")
        print(f"design_root={design_root}")
        print(f"figma_export_roots={effective_figma_export_roots}")
        sys.exit(0)

    output_root = Path(args.output_root).resolve() if args.output_root else batch_discovery.default_output_root()
    output_root.mkdir(parents=True, exist_ok=True)
    args.game_output_explicit = bool(normalize_game_output_path(args.game_output))
    args.effective_game_output_root = resolve_effective_batch_game_output_root(
        args.game_output,
        output_root,
        batch_start_time,
    )

    cases = batch_discovery.discover_pen_cases(design_root, args.include, args.exclude)
    cases.extend(batch_discovery.discover_figma_export_cases(effective_figma_export_roots, args.include, args.exclude))
    if not cases:
        raise RuntimeError(f"No eligible pen or figma export cases discovered under: {design_root}")
    if not args.include and not args.exclude and len(cases) != FORMAL_DISCOVERED_CASE_COUNT:
        raise RuntimeError(
            f"Formal full discovery must produce {FORMAL_DISCOVERED_CASE_COUNT} cases; found {len(cases)}. "
            "Check the Pencil design root and all configured Figma export roots."
        )
    if args.game_output_explicit:
        validate_explicit_batch_game_output_root(args.effective_game_output_root, cases)
    verify.sync_project_plugin_build_id(args.editor, project_plugin_root)
    verify.seed_runtime_font_assets(runtime_args.project, [case["source"] for case in cases])
    run_scope_path = batch_discovery.write_run_scope(output_root, args, cases, effective_figma_export_roots)
    record_effective_game_output_root(run_scope_path, args)
    reference_cache_root = reference_cache.resolve_reference_cache_root(args, output_root)
    if reference_cache_root:
        reference_cache_root.mkdir(parents=True, exist_ok=True)

    states = []
    for case in cases:
        case_args = build_case_args(args, case)
        if args.reference_root:
            case_args.reference = str(Path(args.reference_root).resolve())
        state = prepare_case_state(
            case_args,
            case,
            output_root,
            reference_cache_root,
            args.reference_cache,
        )
        state["baseline_case_dir"] = (Path(args.reference_root).resolve() / case["name"]) if args.reference_root else None
        states.append(state)
    prep_completed_at = time.time()

    if args.editor_isolation == "case":
        shard_results = run_execution_cases(
            states,
            output_root,
            args.editor,
            runtime_args.project,
            args.idle_timeout_sec,
            args.startup_timeout_sec,
            args.log_cmds,
        )
        summary_shards = build_isolation_summary_shards(states, "case")
    elif args.editor_isolation == "step":
        shard_results = run_execution_steps(
            states,
            output_root,
            args.editor,
            runtime_args.project,
            args.idle_timeout_sec,
            args.startup_timeout_sec,
            args.log_cmds,
        )
        summary_shards = build_isolation_summary_shards(states, "step")
    else:
        parallel_editors = execution_shards.resolve_parallel_editor_count(args.parallel_editors, len(states))
        shards = execution_shards.build_execution_shards(states, parallel_editors)
        verify.ensure_clean_editor()
        shard_results = run_execution_shards(
            shards,
            output_root,
            args.editor,
            runtime_args.project,
            args.idle_timeout_sec,
            args.startup_timeout_sec,
            args.log_cmds,
        )
        summary_shards = shards
    retry_result = rerun_missing_steps(
        states,
        output_root,
        args.editor,
        runtime_args.project,
        args.idle_timeout_sec,
        args.startup_timeout_sec,
        args.log_cmds,
        args.editor_isolation,
    )
    if retry_result is not None:
        shard_results.append(retry_result)
    ensure_driver_completed_all_steps(states, output_root / "unreal_batch_incomplete.log")
    editor_exit_codes = [item["exit_code"] for item in shard_results]
    editor_exit_code = next((code for code in editor_exit_codes if code != 0), 0)
    batch_log_path = output_root / "unreal_batch.log"
    editor_completed_at = time.time()

    post_workers = execution_shards.resolve_post_worker_count(args.post_workers, len(states))
    case_results = process_case_results(states, editor_exit_code, batch_log_path, post_workers)
    for result in case_results:
        status = "BLOCKED" if result.get("blocked") else "CASE"
        line = f"[{status}] {result['name']} report={result['report_path']}"
        if result.get("blocked"):
            if result.get("verdict"):
                line += f" verdict={result['verdict']}"
            if result.get("verification_level"):
                line += f" verification_level={result['verification_level']}"
            if result.get("canonical_source"):
                line += f" canonical_source={result['canonical_source']}"
            if result.get("reason"):
                line += f" reason={result['reason']}"
        print(line)
    postprocess_completed_at = time.time()

    completeness = batch_discovery.collect_batch_completeness(states, case_results)
    summary = build_batch_summary(
        args=args,
        editor_exit_code=editor_exit_code,
        editor_exit_codes=editor_exit_codes,
        shards=summary_shards,
        shard_results=shard_results,
        post_workers=post_workers,
        design_root=design_root,
        output_root=output_root,
        run_scope_path=run_scope_path,
        reference_cache_root=reference_cache_root,
        batch_start_time=batch_start_time,
        prep_completed_at=prep_completed_at,
        editor_completed_at=editor_completed_at,
        postprocess_completed_at=postprocess_completed_at,
        case_results=case_results,
        completeness=completeness,
    )
    visual_review_path = write_visual_review_index(output_root, case_results)
    summary["visual_review_path"] = str(visual_review_path)
    summary_path = output_root / "summary.json"
    verify.write_json(summary_path, summary)
    print(f"[SUMMARY] report={summary_path}")
    print(f"[VISUAL_REVIEW] report={visual_review_path}")
    history_result = write_historical_best_validation(
        output_root,
        output_root.parent,
        rebuild_best,
        validate_run_tool,
        verify.write_json,
        print_paths=True,
    )
    historical_validation = history_result["validation"]
    historical_completed_at = time.time()

    summary["phase_durations_sec"]["historical_manifest_and_validation"] = max(
        0.0,
        historical_completed_at - postprocess_completed_at,
    )
    summary["phase_durations_sec"]["total_with_historical_validation"] = max(
        0.0,
        historical_completed_at - batch_start_time,
    )
    timing_history_path = append_batch_timing_history(
        output_root.parent,
        build_batch_timing_history_entry(output_root, summary),
    )
    summary["timing_history_path"] = str(timing_history_path)
    verify.write_json(summary_path, summary)

    formal_gate_candidate = not bool(args.include or args.exclude)
    if (
        completeness["blocked"]
        or (formal_gate_candidate and historical_validation["blocked"])
    ):
        sys.exit(2)
    blocked_cases = summary["blocked_cases"]
    historical_regressions = historical_validation.get("historical_best_regressions") or []
    no_regression_failed = summary["no_regression"]["failed"]
    if blocked_cases or historical_regressions or no_regression_failed > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        sys.exit(2)
    except Exception as error:
        print(f"[ERROR] {type(error).__name__}: {error}", file=sys.stderr)
        sys.exit(2)

