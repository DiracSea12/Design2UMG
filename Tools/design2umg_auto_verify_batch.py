import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify
import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_validate_run as validate_run_tool


ROOT_MIN_SIZE = 8
ROOT_CONTAINER_TYPES = {"frame", "group", "component", "ref"}
NO_REGRESSION_EPSILON = 1.25e-4
REFERENCE_CACHE_SCHEMA_VERSION = 1


def parse_args():
    parser = argparse.ArgumentParser(description="Batch Design2UMG verification from a design directory")
    parser.add_argument("--design-root", required=True, help="Directory containing .pen design files")
    parser.add_argument("--figma-export-root", action="append", default=[], help="Optional local Figma export directory containing design.json and a top-level reference PNG. Can be repeated.")
    parser.add_argument("--output-root", default="", help="Directory for batch outputs")
    parser.add_argument("--reference-root", default="", help="Optional directory containing per-case reference PNGs at <reference-root>/<case-name>/reference.png")
    parser.add_argument("--project", default="", help="Runtime project path. HostProject fallback has been removed. If omitted, the script tries DESIGN2UMG_RUNTIME_PROJECT and then infers a .uproject from the design roots.")
    parser.add_argument("--editor", default=r"G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe", help="UnrealEditor executable path")
    parser.add_argument("--reference-provider", choices=["pencil"], default="pencil", help="Reference source for discovered pen cases")
    parser.add_argument("--mcp-server", default="", help="Override Pencil MCP server binary path")
    parser.add_argument("--pencil-app", default="", help="Override Pencil app name. Leave empty for auto-detect from ~/.pencil/apps and running IDE processes.")
    parser.add_argument("--pixel-tolerance", type=int, default=6, help="Per-pixel max-channel tolerance")
    parser.add_argument("--changed-ratio-threshold", type=float, default=0.01, help="Max changed pixel ratio to pass")
    parser.add_argument("--mae-threshold", type=float, default=0.02, help="Max mean absolute error to pass")
    parser.add_argument("--game-output", default="", help="Optional UE asset output base path override passed through to all cases")
    parser.add_argument("--idle-timeout-sec", type=int, default=180, help="Kill the editor if no log or result progress is observed for this many seconds")
    parser.add_argument("--startup-timeout-sec", type=int, default=60, help="Kill a shard early if UnrealEditor shows zero progress right after launch for this many seconds")
    parser.add_argument("--parallel-editors", type=int, default=0, help="How many UnrealEditor shards to run in parallel. 0 means auto.")
    parser.add_argument("--post-workers", type=int, default=0, help="How many parallel workers to use for post-render compare/report. 0 means auto.")
    parser.add_argument("--native-self-check", choices=["auto", "off"], default="off", help="Whether to run optional native-size self-check passes. 'off' is faster and does not affect formal scoring metrics.")
    parser.add_argument("--reference-cache", choices=["off", "read", "read-write", "refresh"], default="read-write", help="Reference cache policy for pen+pencil cases without --reference-root. read-write is recommended for faster reruns.")
    parser.add_argument("--reference-cache-root", default="", help="Optional cache root for exported references. Default: <output-root-parent>/_reference_cache")
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


def classify_run_scope(include_pattern: str, exclude_pattern: str) -> str:
    if include_pattern:
        return "focus"
    if exclude_pattern:
        return "full_with_exclusions"
    return "full"


def write_run_scope(output_root: Path, args, cases: list[dict]) -> Path:
    run_scope = {
        "scope": classify_run_scope(args.include, args.exclude),
        "include_pattern": args.include,
        "exclude_pattern": args.exclude,
        "discovered_total": len(cases),
        "formal_admissible_candidate": not bool(args.include),
        "case_names": [case["name"] for case in cases],
    }
    run_scope_path = output_root / "run_scope.json"
    verify.write_json(run_scope_path, run_scope)
    return run_scope_path


def collect_batch_completeness(states: list[dict], case_results: list[dict]) -> dict:
    missing_compare_reports = []
    missing_case_results = []
    for state in states:
        compare_report_path = state["output_dir"] / "compare_report.json"
        if not compare_report_path.is_file():
            missing_compare_reports.append(state["case"]["name"])

    reported_names = {item["name"] for item in case_results}
    for state in states:
        case_name = state["case"]["name"]
        if case_name not in reported_names:
            missing_case_results.append(case_name)

    blocked_reasons = []
    if missing_compare_reports:
        blocked_reasons.append("missing_compare_report")
    if missing_case_results:
        blocked_reasons.append("missing_case_summary_entry")

    return {
        "blocked": bool(blocked_reasons),
        "blocked_reasons": blocked_reasons,
        "discovered_total": len(states),
        "reported_total": len(case_results),
        "missing_compare_reports": missing_compare_reports,
        "missing_case_results": missing_case_results,
    }


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


def discover_figma_export_cases(export_roots: list[str], include_pattern: str, exclude_pattern: str) -> list[dict]:
    include_regex = re.compile(include_pattern) if include_pattern else None
    exclude_regex = re.compile(exclude_pattern) if exclude_pattern else None
    discovered = []

    for raw_root in export_roots:
        if not raw_root:
            continue

        export_root = Path(raw_root).resolve()
        design_json = export_root / "design.json"
        if not design_json.is_file():
            continue

        top_level_pngs = sorted(
            [path for path in export_root.glob("*.png") if path.is_file()],
            key=lambda path: (-path.stat().st_size, path.name.lower()),
        )
        if not top_level_pngs:
            continue

        reference_png = top_level_pngs[0]
        base_name = f"figma_{sanitize_case_name(export_root.name)}"
        for mode, suffix in (("local-figma", "local"), ("figma-source", "source")):
            case_name = f"{base_name}_{suffix}"
            if include_regex and not include_regex.search(case_name):
                continue
            if exclude_regex and exclude_regex.search(case_name):
                continue

            discovered.append(
                {
                    "name": case_name,
                    "mode": mode,
                    "source": str(design_json),
                    "node": "",
                    "node_name": export_root.name,
                    "reference_path": str(reference_png),
                    "export_root": str(export_root),
                }
            )

    return discovered


def resolve_reference_cache_root(args, output_root: Path) -> Path | None:
    if args.reference_cache == "off":
        return None
    if args.reference_cache_root:
        return Path(args.reference_cache_root).resolve()
    return output_root.parent / "_reference_cache"


def build_source_signature(source_path: Path) -> dict:
    resolved = source_path.resolve()
    stat = resolved.stat()
    digest = hashlib.sha1(resolved.read_bytes()).hexdigest()
    return {
        "path": str(resolved),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha1": digest,
    }


def build_reference_cache_key(case_args) -> str:
    payload = {
        "schema": REFERENCE_CACHE_SCHEMA_VERSION,
        "mode": case_args.mode,
        "source": str(Path(case_args.source).resolve()),
        "node": case_args.node or "",
        "reference_provider": case_args.reference_provider,
    }
    raw = json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()


def can_use_reference_cache(case_args, case: dict) -> bool:
    if case_args.mode != "pen":
        return False
    if case.get("reference_path"):
        return False
    if case_args.reference:
        return False
    return case_args.reference_provider == "pencil"


def restore_reference_from_cache(
    case_args,
    case: dict,
    output_dir: Path,
    cache_root: Path | None,
) -> tuple[Path, dict] | None:
    if not cache_root or not can_use_reference_cache(case_args, case):
        return None

    source_path = Path(case_args.source)
    if not source_path.is_file():
        return None

    cache_key = build_reference_cache_key(case_args)
    cache_dir = cache_root / cache_key
    meta_path = cache_dir / "meta.json"
    cached_reference_path = cache_dir / "reference.png"
    if not meta_path.is_file() or not cached_reference_path.is_file():
        return None

    metadata = verify.load_json_document(meta_path)
    if metadata.get("schema") != REFERENCE_CACHE_SCHEMA_VERSION:
        return None
    if metadata.get("mode") != case_args.mode:
        return None
    if metadata.get("node") != (case_args.node or ""):
        return None
    if metadata.get("reference_provider") != case_args.reference_provider:
        return None

    expected_signature = metadata.get("source_signature") or {}
    current_signature = build_source_signature(source_path)
    if expected_signature != current_signature:
        return None

    output_reference_path = output_dir / "reference.png"
    verify.shutil.copyfile(cached_reference_path, output_reference_path)

    reference_context = metadata.get("reference_context") or {}
    cached_structure_path = cache_dir / "source_structure_live.json"
    if cached_structure_path.is_file():
        try:
            reference_context["source_structure_live"] = verify.load_json_document(cached_structure_path)
            reference_context["source_structure_live_path"] = str(cached_structure_path)
        except Exception:
            pass

    cache_meta = {
        "hit": True,
        "cache_dir": str(cache_dir),
        "cache_key": cache_key,
    }
    reference_context["reference_cache"] = cache_meta
    reference_context.setdefault("reference_authority", {})
    return output_reference_path, reference_context


def save_reference_to_cache(
    case_args,
    case: dict,
    cache_root: Path | None,
    reference_path: Path,
    reference_context: dict,
) -> None:
    if not cache_root or not can_use_reference_cache(case_args, case):
        return

    source_path = Path(case_args.source)
    if not source_path.is_file():
        return

    cache_key = build_reference_cache_key(case_args)
    cache_dir = cache_root / cache_key
    cache_dir.mkdir(parents=True, exist_ok=True)
    verify.shutil.copyfile(reference_path, cache_dir / "reference.png")

    cached_context = dict(reference_context or {})
    source_structure_live = cached_context.pop("source_structure_live", None)
    cached_context.pop("source_structure_live_path", None)
    if source_structure_live is not None:
        verify.write_json(cache_dir / "source_structure_live.json", source_structure_live)

    metadata = {
        "schema": REFERENCE_CACHE_SCHEMA_VERSION,
        "mode": case_args.mode,
        "source": str(source_path.resolve()),
        "node": case_args.node or "",
        "reference_provider": case_args.reference_provider,
        "source_signature": build_source_signature(source_path),
        "reference_context": cached_context,
    }
    verify.write_json(cache_dir / "meta.json", metadata)


def build_case_args(batch_args, case: dict) -> SimpleNamespace:
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
        changed_ratio_threshold=batch_args.changed_ratio_threshold,
        mae_threshold=batch_args.mae_threshold,
        game_output=batch_args.game_output,
        native_self_check=batch_args.native_self_check,
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
    declared_design_size = verify.get_declared_design_size(case_args.mode, case_args.source, case_args.node)
    restored_from_cache = None
    if reference_cache_mode in {"read", "read-write"}:
        try:
            restored_from_cache = restore_reference_from_cache(case_args, case, output_dir, reference_cache_root)
        except Exception:
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
            save_reference_to_cache(case_args, case, reference_cache_root, reference_path, reference_context)
        except Exception:
            pass

    verify.finalize_reference_context(case_args, reference_path, reference_context)
    # Formal full-runs with an external reference root already carry the authoritative
    # reference image. In that mode, per-case live Pencil MCP structure fetches add a
    # large serial latency and routinely fall back to the local design snapshot anyway.
    if case_args.mode == "pen" and not case_args.reference and "source_structure_live" not in reference_context:
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

    preliminary_reference_validation = verify.validate_reference_against_design(reference_size, expected_design_size)
    declared_width, declared_height = declared_design_size
    has_partial_declared_size = (
        (declared_width and declared_width > 1 and not (declared_height and declared_height > 1))
        or (declared_height and declared_height > 1 and not (declared_width and declared_width > 1))
    )
    if not preliminary_reference_validation["valid"] and has_partial_declared_size:
        inferred_expected_size = verify.infer_expected_design_size_from_reference(declared_design_size, reference_size)
        if inferred_expected_size and inferred_expected_size != expected_design_size:
            inferred_validation = verify.validate_reference_against_design(reference_size, inferred_expected_size)
            if inferred_validation["valid"]:
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
    game_output_root = verify.build_automation_asset_root(output_dir, case_args.game_output)

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
    game_output_root = state["game_output_root"]
    primary_game_output = verify.build_automation_phase_output(game_output_root, "primary")
    primary_asset_output = verify.build_automation_phase_output(game_output_root, "primary-asset")
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
                "output_path": primary_game_output,
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
                "output_path": primary_asset_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "render_width": render_width,
                "render_height": render_height,
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
            "output_path": native_game_output,
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
            "output_path": native_asset_output,
            "render_png_path": str(native_path),
            "result_json_path": str(output_dir / "ue_native_render_result.json"),
            "render_width": native_width,
            "render_height": native_height,
            "force_reload_package": True,
        },
    )


def write_editor_driver_script(output_root: Path, spec_paths: list[Path], tag: str = "batch") -> Path:
    normalized_tag = sanitize_case_name(tag)
    manifest_path = output_root / f"editor_driver_manifest_{normalized_tag}.json"
    driver_log_path = output_root / f"editor_driver_{normalized_tag}.log"
    verify.write_json(
        manifest_path,
        {
            "spec_paths": [spec_path.as_posix() for spec_path in spec_paths],
            "driver_log_path": str(driver_log_path),
        },
    )

    script_path = output_root / f"editor_driver_{normalized_tag}.py"
    script_content = f"""import json
import traceback
from datetime import datetime
import unreal

MANIFEST_PATH = r\"{manifest_path}\"


def _write_line(path, message):
    timestamp = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"{{timestamp}} {{message}}\\n")


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
    startup_timeout_sec: int,
    log_cmds: str = "",
) -> int:
    command = [
        editor_path,
        project_path,
        "-nop4",
        "-nosplash",
        "-NoMutex",
        "-stdout",
        "-FullStdOutLogOutput",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    if log_cmds:
        command.append(f"-LogCmds={log_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8-sig") as handle:
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
                process.kill()
                raise TimeoutError(
                    f"Editor made no progress for {idle_timeout_sec}s. "
                    f"result_files={last_result_count}/{len(result_paths)} log={log_path}"
                )

            time.sleep(2)

    exit_deadline = time.time() + 30.0
    while time.time() < exit_deadline:
        current_result_count = sum(1 for path in result_paths if path.is_file())
        if current_result_count >= len(result_paths):
            break
        if current_result_count > last_result_count:
            last_result_count = current_result_count
            exit_deadline = time.time() + 30.0
        time.sleep(1)

    if exit_code != 0 and all(path.is_file() for path in result_paths):
        if verify.log_indicates_clean_editor_shutdown(log_path):
            return 0

    return exit_code


def resolve_parallel_editor_count(requested: int, case_count: int) -> int:
    if case_count <= 1:
        return 1
    if requested > 0:
        return max(1, requested)

    cpu_count = os.cpu_count() or 1
    if case_count < 8:
        return 1
    # Keep auto-sharding conservative on shared projects; users can still force
    # higher values with --parallel-editors when the machine is stable.
    if cpu_count >= 12 and case_count >= 8:
        return 2
    return 1


def resolve_post_worker_count(requested: int, case_count: int) -> int:
    if case_count <= 1:
        return 1
    if requested > 0:
        return max(1, requested)

    cpu_count = os.cpu_count() or 1
    baseline = max(1, cpu_count // 2)
    return max(1, min(8, baseline, case_count))


def build_execution_shards(states: list[dict], shard_count: int) -> list[dict]:
    normalized_shard_count = max(1, min(shard_count, len(states)))
    shards = [
        {
            "id": index,
            "states": [],
            "spec_paths": [],
            "result_paths": [],
        }
        for index in range(normalized_shard_count)
    ]

    sorted_states = sorted(states, key=lambda item: len(item["spec_paths"]), reverse=True)
    for state in sorted_states:
        shard = min(shards, key=lambda item: len(item["spec_paths"]))
        shard["states"].append(state)
        shard["spec_paths"].extend(state["spec_paths"])
        shard["result_paths"].extend(step["result_json_path"] for step in state["steps"].values())

    return [shard for shard in shards if shard["states"]]


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


def rerun_missing_steps(
    states: list[dict],
    output_root: Path,
    editor_path: str,
    project_path: str,
    idle_timeout_sec: int,
    startup_timeout_sec: int,
    log_cmds: str,
) -> dict | None:
    missing_steps = []
    for state in states:
        for step in state["steps"].values():
            if not step["result_json_path"].is_file():
                missing_steps.append(step)

    if not missing_steps:
        return None

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
        "stable_changed_ratio": None,
        "mean_abs_error": None,
        "stable_mean_abs_error": None,
        "size_match": False,
        "reference_size": list(verify.read_image_size(state["reference_path"])),
        "candidate_size": list(verify.read_image_size(candidate_path)) if candidate_path.is_file() else None,
        "diff_bbox": None,
        "stable_diff_bbox": None,
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

    available = any(
        value is not None
        for value in (
            changed_ratio,
            stable_changed_ratio,
            mean_abs_error,
            stable_mean_abs_error,
        )
    )
    return {
        "available": available,
        "reason": "" if available else "baseline_metrics_unavailable",
        "case_dir": str(case_dir),
        "report_path": str(report_path) if report_path.is_file() else "",
        "reference_png_path": str(reference_png_path) if reference_png_path.is_file() else "",
        "candidate_png_path": str(candidate_png_path) if candidate_png_path.is_file() else "",
        "changed_ratio": changed_ratio,
        "stable_changed_ratio": stable_changed_ratio,
        "mean_abs_error": mean_abs_error,
        "stable_mean_abs_error": stable_mean_abs_error,
    }


def evaluate_no_regression(report: dict, case_dir: Path | None, pixel_tolerance: int) -> dict:
    baseline = load_baseline_case_metrics(case_dir, pixel_tolerance)
    result = {
        "available": baseline.get("available", False),
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
    if not baseline.get("available"):
        result["reasons"].append(baseline.get("reason", "baseline_unavailable"))
        return result

    decisive_metric = "unavailable"
    decisive_delta = None
    visual_passed = False
    for metric_name in ("stable_changed_ratio", "changed_ratio", "stable_mean_abs_error", "mean_abs_error"):
        current_value = result["current"].get(metric_name)
        baseline_value = baseline.get(metric_name)
        if current_value is None or baseline_value is None:
            continue
        decisive_metric = metric_name
        decisive_delta = current_value - baseline_value
        visual_passed = decisive_delta <= NO_REGRESSION_EPSILON
        break
    if decisive_metric == "unavailable":
        result["reasons"].append("comparison_metric_unavailable")
        return result

    structure_validation = report.get("structure_validation") or {}
    structure_guard_passed = structure_validation.get("passed") if structure_validation.get("required") else True
    result["passed"] = visual_passed and structure_guard_passed
    result["decisive_metric"] = decisive_metric
    result["decisive_delta"] = decisive_delta
    result["visual_regressed"] = not visual_passed
    result["structure_guard_passed"] = structure_guard_passed
    if not visual_passed:
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
    no_regression = evaluate_no_regression(report, state.get("baseline_case_dir"), args.pixel_tolerance)
    report["no_regression"] = no_regression
    verify.write_json(Path(report_path), report)
    return {
        "name": state["case"]["name"],
        "passed": passed,
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


def main():
    batch_start_time = time.time()
    args = parse_args()
    design_root = Path(args.design_root).resolve()
    if not design_root.is_dir():
        raise RuntimeError(f"Design root not found: {design_root}")

    output_root = Path(args.output_root).resolve() if args.output_root else default_output_root()
    output_root.mkdir(parents=True, exist_ok=True)

    runtime_args = SimpleNamespace(
        project=args.project,
        editor=args.editor,
    )

    runtime_args.project, project_plugin_root = verify.prepare_runtime_project(
        runtime_args,
        [str(design_root), *args.figma_export_root],
    )
    runtime_args.project_plugin_root = project_plugin_root
    runtime_args.log_cmds = args.log_cmds
    verify.sync_project_plugin_build_id(args.editor, project_plugin_root)

    cases = discover_pen_cases(design_root, args.include, args.exclude)
    cases.extend(discover_figma_export_cases(args.figma_export_root, args.include, args.exclude))
    if not cases:
        raise RuntimeError(f"No eligible pen or figma export cases discovered under: {design_root}")
    verify.seed_runtime_font_assets(runtime_args.project, [case["source"] for case in cases])
    run_scope_path = write_run_scope(output_root, args, cases)
    reference_cache_root = resolve_reference_cache_root(args, output_root)
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

    parallel_editors = resolve_parallel_editor_count(args.parallel_editors, len(states))
    shards = build_execution_shards(states, parallel_editors)
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
    retry_result = rerun_missing_steps(
        states,
        output_root,
        args.editor,
        runtime_args.project,
        args.idle_timeout_sec,
        args.startup_timeout_sec,
        args.log_cmds,
    )
    if retry_result is not None:
        shard_results.append(retry_result)
    ensure_driver_completed_all_steps(states, output_root / "unreal_batch_incomplete.log")
    editor_exit_codes = [item["exit_code"] for item in shard_results]
    editor_exit_code = next((code for code in editor_exit_codes if code != 0), 0)
    batch_log_path = output_root / "unreal_batch.log"
    editor_completed_at = time.time()

    post_workers = resolve_post_worker_count(args.post_workers, len(states))
    case_results = process_case_results(states, editor_exit_code, batch_log_path, post_workers)
    for result in case_results:
        status = "PASS" if result["passed"] else "FAIL"
        print(f"[{status}] {result['name']} report={result['report_path']}")
    postprocess_completed_at = time.time()

    passed = sum(1 for item in case_results if item["passed"])
    no_regression_cases = [item for item in case_results if item.get("no_regression_available")]
    no_regression_passed = sum(1 for item in no_regression_cases if item.get("no_regression_passed"))
    completeness = collect_batch_completeness(states, case_results)
    summary = {
        "total": len(case_results),
        "passed": passed,
        "failed": len(case_results) - passed,
        "editor_exit_code": editor_exit_code,
        "editor_exit_codes": editor_exit_codes,
        "parallel_editors_requested": args.parallel_editors,
        "parallel_editors_used": len(shards),
        "idle_timeout_sec": args.idle_timeout_sec,
        "startup_timeout_sec": args.startup_timeout_sec,
        "execution_shards": shard_results,
        "post_workers_requested": args.post_workers,
        "post_workers_used": post_workers,
        "design_root": str(design_root),
        "output_root": str(output_root),
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
        "no_regression": {
            "available": len(no_regression_cases),
            "passed": no_regression_passed,
            "failed": len(no_regression_cases) - no_regression_passed,
        },
        "cases": case_results,
    }
    summary_path = output_root / "summary.json"
    verify.write_json(summary_path, summary)
    print(f"[SUMMARY] report={summary_path}")
    automation_root = output_root.parent
    historical_best_manifest = rebuild_best.rebuild_historical_best_manifest(automation_root)
    historical_best_manifest_path = automation_root / "_admissible_best_latest.json"
    rebuild_best.write_json(historical_best_manifest_path, historical_best_manifest)
    run_manifest_path = output_root / "historical_best_manifest.json"
    rebuild_best.write_json(run_manifest_path, historical_best_manifest)

    historical_validation = validate_run_tool.validate_run(
        output_root,
        admissible_best_path=historical_best_manifest_path,
    )
    historical_validation_path = output_root / "validation_against_historical_best.json"
    verify.write_json(historical_validation_path, historical_validation)
    historical_completed_at = time.time()
    print(f"[HISTORICAL_BEST] manifest={historical_best_manifest_path}")
    print(f"[HISTORICAL_BEST] validation={historical_validation_path}")

    summary["phase_durations_sec"]["historical_manifest_and_validation"] = max(
        0.0,
        historical_completed_at - postprocess_completed_at,
    )
    summary["phase_durations_sec"]["total_with_historical_validation"] = max(
        0.0,
        historical_completed_at - batch_start_time,
    )
    verify.write_json(summary_path, summary)

    formal_gate_candidate = not bool(args.include or args.exclude)
    if completeness["blocked"] or (formal_gate_candidate and historical_validation["blocked"]):
        sys.exit(2)
    sys.exit(0 if passed == len(case_results) else 1)


if __name__ == "__main__":
    main()

