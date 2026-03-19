import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify


ROOT_MIN_SIZE = 8
ROOT_CONTAINER_TYPES = {"frame", "group", "component", "ref"}


def parse_args():
    parser = argparse.ArgumentParser(description="Batch Design2UMG verification from a design directory")
    parser.add_argument("--design-root", required=True, help="Directory containing .pen design files")
    parser.add_argument("--output-root", default="", help="Directory for batch outputs")
    parser.add_argument("--reference-root", default="", help="Optional directory containing per-case reference PNGs at <reference-root>/<case-name>/reference.png")
    parser.add_argument("--project", default="", help="Project path. Leave empty to auto-build a temporary HostProject from the engine plugin source.")
    parser.add_argument("--build-root", default="", help="Temporary BuildPlugin output directory. Leave empty to use the system temp directory.")
    parser.add_argument("--keep-build-root", action="store_true", help="Keep the generated BuildPlugin workspace for debugging.")
    parser.add_argument("--editor", default=r"G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe", help="UnrealEditor.exe path")
    parser.add_argument("--reference-provider", choices=["pencil", "pen-sim"], default="pencil", help="Reference source for discovered pen cases")
    parser.add_argument("--mcp-server", default="", help="Override Pencil MCP server binary path")
    parser.add_argument("--pencil-app", default="", help="Override Pencil app name (default: vscode)")
    parser.add_argument("--pixel-tolerance", type=int, default=6, help="Per-pixel max-channel tolerance")
    parser.add_argument("--changed-ratio-threshold", type=float, default=0.01, help="Max changed pixel ratio to pass")
    parser.add_argument("--mae-threshold", type=float, default=0.02, help="Max mean absolute error to pass")
    parser.add_argument("--idle-timeout-sec", type=int, default=180, help="Kill the editor if no log or result progress is observed for this many seconds")
    parser.add_argument("--include", default="", help="Only run discovered case names matching this regex")
    parser.add_argument("--exclude", default="", help="Skip discovered case names matching this regex")
    parser.add_argument("--log-cmds", default="", help="Optional Unreal -LogCmds payload, for example 'LogDesign2UMG Verbose'")
    return parser.parse_args()


def default_output_root() -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return verify.plugin_root() / "Saved" / "Design2UMG" / "Automation" / f"batch_{stamp}_{Path.cwd().name}"


def sanitize_case_name(value: str) -> str:
    normalized = (value or "").strip().lower()
    normalized = re.sub(r"[^a-z0-9]+", "-", normalized).strip("-")
    return normalized or "unnamed"


def discover_pen_cases(design_root: Path, include_pattern: str, exclude_pattern: str) -> list[dict]:
    include_regex = re.compile(include_pattern) if include_pattern else None
    exclude_regex = re.compile(exclude_pattern) if exclude_pattern else None
    discovered = []
    used_names: dict[str, int] = {}

    for pen_path in sorted(design_root.rglob("*.pen")):
        document = verify.load_json_document(pen_path)
        nodes = document.get("children", [])
        if not isinstance(nodes, list):
            continue

        for index, node in enumerate(nodes):
            if not isinstance(node, dict):
                continue
            node_id = node.get("id", "")
            if not node_id:
                continue
            node_type = str(node.get("type", "")).strip().lower()
            has_children = isinstance(node.get("children"), list) and bool(node.get("children"))
            if node_type not in ROOT_CONTAINER_TYPES and not has_children:
                continue
            size = verify.extract_node_size(node)
            declared_width, declared_height = verify.extract_node_numeric_size(node)
            has_declared_extent = (
                (declared_width is not None and declared_width >= ROOT_MIN_SIZE)
                or (declared_height is not None and declared_height >= ROOT_MIN_SIZE)
            )
            if not size and not has_declared_extent:
                continue
            if size and (size[0] < ROOT_MIN_SIZE or size[1] < ROOT_MIN_SIZE):
                continue

            node_name = node.get("name") or node_id
            case_name = f"pen_{sanitize_case_name(pen_path.stem)}_{sanitize_case_name(node_name)}"
            duplicate_index = used_names.get(case_name, 0)
            used_names[case_name] = duplicate_index + 1
            if duplicate_index:
                case_name = f"{case_name}-{duplicate_index + 1}"

            if include_regex and not include_regex.search(case_name):
                continue
            if exclude_regex and exclude_regex.search(case_name):
                continue

            discovered.append(
                {
                    "name": case_name,
                    "mode": "pen",
                    "source": str(pen_path),
                    "node": node_id,
                    "node_name": node_name,
                }
            )

    return discovered


def build_case_args(batch_args, case: dict) -> SimpleNamespace:
    return SimpleNamespace(
        mode=case["mode"],
        source=case["source"],
        node=case.get("node", ""),
        reference_provider="png" if batch_args.reference_root else batch_args.reference_provider,
        reference="",
        figma_file="",
        figma_node="",
        figma_token="",
        mcp_server=batch_args.mcp_server,
        pencil_app=batch_args.pencil_app,
        pixel_tolerance=batch_args.pixel_tolerance,
        changed_ratio_threshold=batch_args.changed_ratio_threshold,
        mae_threshold=batch_args.mae_threshold,
        game_output="",
    )


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
        except Exception:
            pass
    return reference_path, reference_context


def prepare_case_state(case_args, case: dict, output_root: Path) -> dict:
    output_dir = output_root / case["name"]
    output_dir.mkdir(parents=True, exist_ok=True)

    expected_design_size = verify.get_expected_design_size(case_args.mode, case_args.source, case_args.node)
    declared_design_size = verify.get_declared_design_size(case_args.mode, case_args.source, case_args.node)
    if case_args.reference:
        reference_path, reference_context = prepare_external_reference(case_args, case, Path(case_args.reference), output_dir)
    else:
        reference_path, reference_context = verify.prepare_reference(case_args, output_dir, expected_design_size)
    verify.finalize_reference_context(case_args, reference_path, reference_context)
    if case_args.mode == "pen":
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

    if not expected_design_size:
        inferred_expected_size = verify.infer_expected_design_size_from_reference(declared_design_size, reference_size)
        if inferred_expected_size:
            expected_design_size = inferred_expected_size
            reference_context["inferred_expected_design_size"] = list(inferred_expected_size)

    reference_validation = verify.validate_reference_against_design(reference_size, expected_design_size)
    reference_context["reference_validation"] = reference_validation
    if reference_validation["valid"]:
        render_width, render_height = reference_size
    elif expected_design_size:
        render_width, render_height = expected_design_size
    else:
        render_width, render_height = reference_size

    candidate_filename = "candidate.png" if reference_validation["valid"] else "candidate.native.png"
    candidate_path = output_dir / candidate_filename
    game_output = f"/Game/Design2UMG/Automation/{output_dir.name}"

    state = {
        "case": case,
        "args": case_args,
        "output_dir": output_dir,
        "reference_path": reference_path,
        "reference_context": reference_context,
        "reference_validation": reference_validation,
        "expected_design_size": expected_design_size,
        "render_size": (render_width, render_height),
        "candidate_path": candidate_path,
        "game_output": game_output,
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
    }


def build_native_plan(state: dict):
    expected_design_size = state["expected_design_size"]
    render_width, render_height = state["render_size"]

    if not expected_design_size:
        return {"available": False, "reason": "expected_design_size_unavailable"}

    native_width, native_height = expected_design_size
    if native_width <= 0 or native_height <= 0:
        return {
            "available": False,
            "reason": "expected_design_size_invalid",
            "expected_design_size": [native_width, native_height],
        }

    if (native_width, native_height) == (render_width, render_height):
        return {
            "available": False,
            "reason": "expected_design_size_matches_primary_render",
            "expected_design_size": [native_width, native_height],
            "primary_render_size": [render_width, render_height],
        }

    return {
        "available": True,
        "expected_design_size": [native_width, native_height],
        "primary_render_size": [render_width, render_height],
    }


def build_case_specs(state: dict):
    case_args = state["args"]
    render_width, render_height = state["render_size"]
    candidate_path = state["candidate_path"]
    output_dir = state["output_dir"]
    game_output = state["game_output"]
    mode_map = {
        "pen": "pen",
        "local-figma": "local_figma",
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
                "output_path": game_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "render_width": render_width,
                "render_height": render_height,
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
                "output_path": game_output,
                "render_png_path": str(bootstrap_candidate_path),
                "result_json_path": str(generate_result_path),
                "render_width": render_width,
                "render_height": render_height,
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
                "output_path": game_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "render_width": render_width,
                "render_height": render_height,
                "force_reload_package": True,
            },
        )

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
                "output_path": game_output,
                "render_png_path": str(native_path),
                "result_json_path": str(output_dir / "ue_native_render_result.json"),
                "render_width": native_width,
                "render_height": native_height,
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
            "output_path": game_output,
            "render_png_path": str(native_bootstrap_path),
            "result_json_path": str(native_generate_result_path),
            "render_width": native_width,
            "render_height": native_height,
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
            "output_path": game_output,
            "render_png_path": str(native_path),
            "result_json_path": str(output_dir / "ue_native_render_result.json"),
            "render_width": native_width,
            "render_height": native_height,
            "force_reload_package": True,
        },
    )


def write_editor_driver_script(output_root: Path, spec_paths: list[Path]) -> Path:
    manifest_path = output_root / "editor_driver_manifest.json"
    driver_log_path = output_root / "editor_driver.log"
    verify.write_json(
        manifest_path,
        {
            "spec_paths": [spec_path.as_posix() for spec_path in spec_paths],
            "driver_log_path": str(driver_log_path),
        },
    )

    script_path = output_root / "editor_driver.py"
    script_content = f"""import json
import traceback
import unreal

MANIFEST_PATH = r\"{manifest_path}\"


def _write_line(path, message):
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(message + "\\n")


def _get_world():
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def _run():
    driver_log_path = ""
    try:
        with open(MANIFEST_PATH, "r", encoding="utf-8-sig") as handle:
            manifest = json.load(handle)
        driver_log_path = manifest.get("driver_log_path", "")
        if driver_log_path:
            _write_line(driver_log_path, "driver:start")
        world = _get_world()
        for spec_path in manifest.get("spec_paths", []):
            if driver_log_path:
                _write_line(driver_log_path, f"driver:run:{{spec_path}}")
            unreal.SystemLibrary.execute_console_command(world, f"Design2UMG.AutomationRender {{spec_path}}")
        if driver_log_path:
            _write_line(driver_log_path, "driver:quit")
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
    except Exception:
        if driver_log_path:
            _write_line(driver_log_path, traceback.format_exc())
        raise


_run()
"""
    script_path.write_text(script_content, encoding="utf-8-sig")
    return script_path


def run_editor_driver(
    editor_path: str,
    project_path: str,
    driver_script_path: Path,
    result_paths: list[Path],
    log_path: Path,
    idle_timeout_sec: int,
    log_cmds: str = "",
) -> int:
    command = [
        editor_path,
        project_path,
        "-nop4",
        "-nosplash",
        "-stdout",
        "-FullStdOutLogOutput",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    if log_cmds:
        command.append(f"-LogCmds={log_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8-sig") as handle:
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
        last_progress_time = time.time()
        last_result_count = 0
        last_log_size = 0

        while True:
            exit_code = process.poll()
            if exit_code is not None:
                return exit_code

            current_result_count = sum(1 for path in result_paths if path.is_file())
            if current_result_count > last_result_count:
                last_result_count = current_result_count
                last_progress_time = time.time()

            try:
                current_log_size = log_path.stat().st_size
            except FileNotFoundError:
                current_log_size = 0
            if current_log_size > last_log_size:
                last_log_size = current_log_size
                last_progress_time = time.time()

            if time.time() - last_progress_time > idle_timeout_sec:
                process.kill()
                raise TimeoutError(
                    f"Editor made no progress for {idle_timeout_sec}s. "
                    f"result_files={last_result_count}/{len(result_paths)} log={log_path}"
                )

            time.sleep(2)


def load_step_result(step: dict, batch_exit_code: int, batch_log_path: Path):
    result_path = step["result_json_path"]
    if not result_path.is_file():
        return None
    return {
        "editor_exit_code": batch_exit_code,
        "log_path": str(batch_log_path),
        "spec_path": str(step["spec_path"]),
        "ue_result": verify.load_ue_result(result_path),
    }


def write_render_failure_report(state: dict, ue_result: dict | None, render_chain: dict, verdict: str, message: str) -> Path:
    output_dir = state["output_dir"]
    report_path = output_dir / "compare_report.json"
    candidate_path = state["candidate_path"]
    report = {
        "passed": False,
        "verdict": verdict,
        "compare_skipped": True,
        "message": message,
        "reference_provider": state["args"].reference_provider,
        "effective_reference_provider": state["reference_context"].get("effective_reference_provider"),
        "effective_reference_path": state["reference_context"].get("effective_reference_path"),
        "reference_png_path": str(state["reference_path"]),
        "candidate_png_path": str(candidate_path),
        "pixel_tolerance": state["args"].pixel_tolerance,
        "changed_ratio_threshold": state["args"].changed_ratio_threshold,
        "mae_threshold": state["args"].mae_threshold,
        "changed_ratio": None,
        "mean_abs_error": None,
        "size_match": False,
        "reference_size": list(verify.read_image_size(state["reference_path"])),
        "candidate_size": list(verify.read_image_size(candidate_path)) if candidate_path.is_file() else None,
        "diff_bbox": None,
        "ue_result": ue_result,
        "reference_context": state["reference_context"],
        "reference_validation": state["reference_validation"],
        "render_chain": render_chain,
    }
    verify.write_json(report_path, report)
    return report_path


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
        report_path = write_render_failure_report(
            state,
            ue_result,
            render_chain,
            "render_failed",
            "Automation render did not produce a valid candidate image",
        )
        return {
            "name": state["case"]["name"],
            "passed": False,
            "report_path": str(report_path),
            "verdict": "render_failed",
        }

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
                "passed": False,
                "report_path": str(report_path),
                "verdict": "unverified_reference",
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
            "passed": False,
            "report_path": str(report_path),
            "verdict": "invalid_reference",
        }

    passed, report_path = verify.compare_and_report(
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
    return {
        "name": state["case"]["name"],
        "passed": passed,
        "report_path": str(report_path),
        "changed_ratio": report.get("changed_ratio"),
        "mae": report.get("mean_abs_error"),
        "verification_level": (reference_context.get("reference_authority") or {}).get("verification_level"),
        "generation_vs_asset_changed_ratio": (render_chain.get("generation_vs_asset") or {}).get("changed_ratio"),
        "structure_passed": (report.get("structure_validation") or {}).get("passed"),
        "structure_required": (report.get("structure_validation") or {}).get("required"),
        "structure_available": (report.get("structure_validation") or {}).get("available"),
        "structure_reasons": (report.get("structure_validation") or {}).get("reasons"),
    }


def main():
    args = parse_args()
    design_root = Path(args.design_root).resolve()
    if not design_root.is_dir():
        raise RuntimeError(f"Design root not found: {design_root}")

    output_root = Path(args.output_root).resolve() if args.output_root else default_output_root()
    output_root.mkdir(parents=True, exist_ok=True)

    runtime_build_root = None
    runtime_args = SimpleNamespace(
        project=args.project,
        build_root=args.build_root,
        keep_build_root=args.keep_build_root,
        editor=args.editor,
    )

    try:
        runtime_args.project, project_plugin_root, runtime_build_root = verify.prepare_runtime_project(runtime_args)
        verify.sync_project_plugin_build_id(args.editor, project_plugin_root)

        cases = discover_pen_cases(design_root, args.include, args.exclude)
        if not cases:
            raise RuntimeError(f"No eligible .pen root nodes discovered under: {design_root}")
        verify.seed_runtime_font_assets(runtime_args.project, [case["source"] for case in cases])

        states = []
        all_spec_paths = []
        all_result_paths = []
        for case in cases:
            case_args = build_case_args(args, case)
            if args.reference_root:
                case_args.reference = str(Path(args.reference_root).resolve())
            state = prepare_case_state(case_args, case, output_root)
            states.append(state)
            all_spec_paths.extend(state["spec_paths"])
            all_result_paths.extend(step["result_json_path"] for step in state["steps"].values())

        verify.ensure_clean_editor()
        batch_log_path = output_root / "unreal_batch.log"
        driver_script_path = write_editor_driver_script(output_root, all_spec_paths)
        editor_exit_code = run_editor_driver(
            args.editor,
            runtime_args.project,
            driver_script_path,
            all_result_paths,
            batch_log_path,
            args.idle_timeout_sec,
            args.log_cmds,
        )

        case_results = []
        for state in states:
            result = process_case_result(state, editor_exit_code, batch_log_path)
            case_results.append(result)
            status = "PASS" if result["passed"] else "FAIL"
            print(f"[{status}] {result['name']} report={result['report_path']}")

        passed = sum(1 for item in case_results if item["passed"])
        summary = {
            "total": len(case_results),
            "passed": passed,
            "failed": len(case_results) - passed,
            "editor_exit_code": editor_exit_code,
            "design_root": str(design_root),
            "output_root": str(output_root),
            "cases": case_results,
        }
        summary_path = output_root / "summary.json"
        verify.write_json(summary_path, summary)
        print(f"[SUMMARY] report={summary_path}")
        sys.exit(0 if passed == len(case_results) else 1)
    finally:
        if runtime_build_root and not args.keep_build_root:
            shutil.rmtree(runtime_build_root, ignore_errors=True)


if __name__ == "__main__":
    main()

