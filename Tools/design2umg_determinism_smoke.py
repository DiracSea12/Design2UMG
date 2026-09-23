from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

import design2umg_auto_verify as verify
import design2umg_render_rule as render_rule
from design2umg_verify_lib.json_io import load_json as helper_load_json
from design2umg_verify_lib.json_io import write_json as helper_write_json


DETERMINISM_SUMMARY_FILENAME = "determinism_smoke_summary.json"
ZERO_DRIFT_EPSILON = 1e-9
STRUCTURE_COUNT_FIELDS = (
    ("left_total_nodes", "right_total_nodes"),
    ("left_leaf_nodes", "right_leaf_nodes"),
    ("left_container_nodes", "right_container_nodes"),
    ("left_text_nodes", "right_text_nodes"),
    ("left_visual_nodes", "right_visual_nodes"),
)
STRUCTURE_BOOLEAN_FIELDS = ("kind_histograms_match", "class_histograms_match")
GENERATION_VS_ASSET_METRICS = (
    "changed_ratio",
    "stable_changed_ratio",
    "mean_abs_error",
    "stable_mean_abs_error",
)


def load_json(path: Path) -> dict:
    return helper_load_json(path)


def write_json(path: Path, payload: dict) -> None:
    helper_write_json(path, payload)


def make_summary_stub(run_root: Path) -> dict:
    return {
        "passed": False,
        "run_root": str(run_root),
        "run_scope_case_names": [],
        "summary_case_names": [],
        "case_results": [],
        "blocked_reasons": [],
        "blocked_reason_details": [],
    }


def append_blocked_reason(summary: dict, reason: str, **details: object) -> None:
    if reason not in summary["blocked_reasons"]:
        summary["blocked_reasons"].append(reason)

    payload = {"reason": reason}
    for key, value in details.items():
        if value is None:
            continue
        payload[key] = value
    summary["blocked_reason_details"].append(payload)


def require_explicit_value(raw_value: str, flag_name: str) -> str:
    value = (raw_value or "").strip()
    if value:
        return value
    raise RuntimeError(
        f"Determinism smoke requires explicit {flag_name}. "
        "Hidden fallback project/editor/reference-root behavior is not supported."
    )


def resolve_reference_root(raw_reference_root: str) -> Path:
    reference_root = Path(require_explicit_value(raw_reference_root, "--reference-root")).expanduser().resolve()
    if not reference_root.is_dir():
        raise RuntimeError(f"Determinism smoke reference root does not exist: {reference_root}")
    return reference_root


def resolve_design_root(raw_design_root: str) -> Path:
    candidate = Path(require_explicit_value(raw_design_root, "--design-root")).expanduser().resolve()
    if not candidate.is_dir():
        raise RuntimeError(f"Determinism smoke design root does not exist: {candidate}")
    return candidate


def resolve_run_root(raw_run_root: str, flag_name: str) -> Path:
    run_root = Path(require_explicit_value(raw_run_root, flag_name)).expanduser().resolve()
    if flag_name == "--run-root" and not run_root.is_dir():
        raise RuntimeError(f"Determinism smoke run root does not exist: {run_root}")
    return run_root


def validate_explicit_smoke_inputs(project: str, editor: str, reference_root: str) -> dict:
    resolved_project = verify.validate_runtime_project_path(
        Path(require_explicit_value(project, "--project")).expanduser(),
        "--project",
    )
    resolved_editor = Path(verify.resolve_unreal_editor_path(require_explicit_value(editor, "--editor"))).resolve()
    resolved_reference_root = resolve_reference_root(reference_root)
    return {
        "project": str(resolved_project),
        "editor": str(resolved_editor),
        "reference_root": str(resolved_reference_root),
    }


def report_path_belongs_to_run_root(report_path: Path, run_root: Path) -> bool:
    try:
        report_path.relative_to(run_root)
        return True
    except ValueError:
        return False


def resolve_expected_report_path(run_root: Path, case_name: str) -> Path:
    return (run_root / case_name / "compare_report.json").resolve()


def read_case_report(report_path: Path, run_root: Path, case_name: str, summary: dict) -> dict | None:
    if not report_path.is_file():
        append_blocked_reason(
            summary,
            "missing_compare_report",
            case=case_name,
            expected_report_path=str(report_path),
        )
        return None

    if not report_path_belongs_to_run_root(report_path, run_root):
        append_blocked_reason(
            summary,
            "stale_compare_report",
            case=case_name,
            report_path=str(report_path),
            run_root=str(run_root),
        )
        return None

    try:
        report = load_json(report_path)
    except RuntimeError as exc:
        append_blocked_reason(
            summary,
            "unreadable_compare_report",
            case=case_name,
            report_path=str(report_path),
            detail=str(exc),
        )
        return None

    if not isinstance(report, dict):
        append_blocked_reason(
            summary,
            "invalid_compare_report",
            case=case_name,
            report_path=str(report_path),
        )
        return None

    return report


def validate_zero_drift_metrics(case_name: str, render_chain: dict, case_result: dict, summary: dict) -> None:
    generation_vs_asset = render_chain.get("generation_vs_asset")
    case_result["generation_vs_asset"] = generation_vs_asset
    if not isinstance(generation_vs_asset, dict):
        append_blocked_reason(summary, "generation_vs_asset_missing", case=case_name)
        return

    for metric_name in GENERATION_VS_ASSET_METRICS:
        metric_value = generation_vs_asset.get(metric_name)
        case_result[metric_name] = metric_value
        if type(metric_value) not in (int, float) or not math.isfinite(metric_value) or metric_value < 0:
            append_blocked_reason(
                summary,
                "generation_vs_asset_metric_invalid",
                case=case_name,
                metric=metric_name,
            )
            continue
        if abs(float(metric_value)) > ZERO_DRIFT_EPSILON:
            append_blocked_reason(
                summary,
                "generation_vs_asset_nonzero",
                case=case_name,
                metric=metric_name,
                value=metric_value,
                epsilon=ZERO_DRIFT_EPSILON,
            )


def validate_structure_parity(case_name: str, render_chain: dict, case_result: dict, summary: dict) -> None:
    structure = render_chain.get("generation_vs_asset_structure")
    case_result["generation_vs_asset_structure"] = structure
    if not isinstance(structure, dict):
        append_blocked_reason(summary, "generation_vs_asset_structure_missing", case=case_name)
        return

    if structure.get("available") is not True:
        append_blocked_reason(
            summary,
            "generation_vs_asset_structure_unavailable",
            case=case_name,
            available=structure.get("available"),
        )

    if structure.get("texts_match") is not True:
        append_blocked_reason(
            summary,
            "generation_vs_asset_structure_text_mismatch",
            case=case_name,
            texts_match=structure.get("texts_match"),
        )

    for left_key, right_key in STRUCTURE_COUNT_FIELDS:
        if (type(structure.get(left_key)) is not int or type(structure.get(right_key)) is not int
                or structure[left_key] < 0 or structure[left_key] != structure[right_key]):
            append_blocked_reason(
                summary,
                "generation_vs_asset_structure_mismatch",
                case=case_name,
                left_key=left_key,
                left_value=structure.get(left_key),
                right_key=right_key,
                right_value=structure.get(right_key),
            )

    for field_name in STRUCTURE_BOOLEAN_FIELDS:
        if structure.get(field_name) is not True:
            append_blocked_reason(
                summary,
                "generation_vs_asset_structure_mismatch",
                case=case_name,
                field=field_name,
                value=structure.get(field_name),
            )


def validate_case_report(
    *,
    run_root: Path,
    case_name: str,
    summary_case: dict | None,
    summary: dict,
) -> None:
    previous_errors = len(summary["blocked_reason_details"])
    expected_report_path = resolve_expected_report_path(run_root, case_name)
    case_result = {
        "case": case_name,
        "expected_report_path": str(expected_report_path),
        "summary_report_path": "",
        "passed": False,
    }
    summary["case_results"].append(case_result)

    if not (run_root / case_name).is_dir():
        append_blocked_reason(summary, "missing_case_directory", case=case_name, case_dir=str(run_root / case_name))

    summary_case = summary_case or {}
    if not summary_case:
        append_blocked_reason(summary, "missing_summary_case", case=case_name)
    else:
        summary_report_path = summary_case.get("report_path", "")
        case_result["summary_report_path"] = summary_report_path
        if not summary_report_path:
            append_blocked_reason(summary, "missing_summary_report_path", case=case_name)
        else:
            resolved_summary_report_path = Path(summary_report_path).resolve()
            if resolved_summary_report_path != expected_report_path:
                append_blocked_reason(
                    summary,
                    "stale_compare_report",
                    case=case_name,
                    summary_report_path=str(resolved_summary_report_path),
                    expected_report_path=str(expected_report_path),
                )

    report = read_case_report(expected_report_path, run_root, case_name, summary)
    if report is None:
        return

    render_chain = report.get("render_chain")
    if not isinstance(render_chain, dict):
        append_blocked_reason(summary, "render_chain_missing", case=case_name)
        return

    final_render_mode = render_chain.get("final_render_mode")
    case_result["final_render_mode"] = final_render_mode
    if final_render_mode != "asset_reload":
        append_blocked_reason(
            summary,
            "final_render_mode_mismatch",
            case=case_name,
            final_render_mode=final_render_mode,
        )

    validate_zero_drift_metrics(case_name, render_chain, case_result, summary)
    validate_structure_parity(case_name, render_chain, case_result, summary)

    compatibility = render_rule.evaluate_report_pipeline_determinism_rule(expected_report_path)
    case_result["pipeline_determinism_rule"] = compatibility
    if not compatibility.get("compatible"):
        append_blocked_reason(
            summary,
            "pipeline_determinism_rule_incompatible",
            case=case_name,
            detail=compatibility.get("reason", ""),
        )
        return

    case_result["passed"] = len(summary["blocked_reason_details"]) == previous_errors


def validate_run_root(run_root: Path) -> dict:
    resolved_run_root = run_root.resolve()
    if not resolved_run_root.is_dir():
        raise RuntimeError(f"Determinism smoke run root does not exist: {resolved_run_root}")

    summary = make_summary_stub(resolved_run_root)
    run_scope_path = resolved_run_root / "run_scope.json"
    suite_summary_path = resolved_run_root / "summary.json"

    try:
        run_scope = load_json(run_scope_path)
    except RuntimeError as exc:
        append_blocked_reason(summary, "missing_run_scope", path=str(run_scope_path), detail=str(exc))
        run_scope = {}

    try:
        suite_summary = load_json(suite_summary_path)
    except RuntimeError as exc:
        append_blocked_reason(summary, "missing_summary", path=str(suite_summary_path), detail=str(exc))
        suite_summary = {}

    if not isinstance(run_scope, dict) or not isinstance(suite_summary, dict):
        append_blocked_reason(summary, "invalid_run_metadata")
        return summary
    expected = run_scope.get("case_names")
    if (not isinstance(expected, list) or any(not isinstance(name, str) or not name for name in expected)
            or len(expected) != len(set(expected))):
        append_blocked_reason(summary, "invalid_run_scope")
        return summary
    summary["run_scope_case_names"] = expected
    if not expected:
        append_blocked_reason(summary, "no_cases")
        return summary
    suite_cases = suite_summary.get("cases")
    if not isinstance(suite_cases, list):
        append_blocked_reason(summary, "invalid_summary_cases")
        return summary
    names = [item.get("name") for item in suite_cases if isinstance(item, dict)]
    summary["summary_case_names"] = names
    if (len(names) != len(suite_cases) or any(not isinstance(name, str) for name in names)
            or len(names) != len(set(names)) or set(names) != set(expected)):
        append_blocked_reason(summary, "summary_case_mismatch", expected=expected, actual=names)
    cases_by_name = {item["name"]: item for item in suite_cases
                     if isinstance(item, dict) and isinstance(item.get("name"), str)}
    for case_name in expected:
        validate_case_report(run_root=resolved_run_root, case_name=case_name,
                             summary_case=cases_by_name.get(case_name), summary=summary)

    summary["passed"] = len(summary["blocked_reasons"]) == 0
    return summary


def write_determinism_summary(run_root: Path, summary: dict) -> Path:
    summary_path = run_root / DETERMINISM_SUMMARY_FILENAME
    write_json(summary_path, summary)
    return summary_path


def build_suite_command(args: argparse.Namespace, resolved_inputs: dict, design_root: Path, output_root: Path) -> list[str]:
    suite_script = Path(__file__).with_name("design2umg_auto_verify_suite.py")
    command = [
        sys.executable,
        str(suite_script),
        "--design-root",
        str(design_root),
        "--output-root",
        str(output_root),
        "--project",
        resolved_inputs["project"],
        "--editor",
        resolved_inputs["editor"],
        "--reference-root",
        resolved_inputs["reference_root"],
        "--parallel-editors",
        "1",
        "--post-workers",
        "1",
        "--reference-cache",
        "read-write",
    ]
    if args.include:
        command.extend(["--include", args.include])
    for root in args.figma_export_root:
        command.extend(["--figma-export-root", root])
    if args.no_publish_history:
        command.append("--no-publish-history")
    if args.log_cmds:
        command.extend(["--log-cmds", args.log_cmds])
    return command


def command_validate_inputs(args: argparse.Namespace) -> int:
    resolved_inputs = validate_explicit_smoke_inputs(args.project, args.editor, args.reference_root)
    payload = {
        **resolved_inputs,
        "design_root": str(resolve_design_root(args.design_root)),
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def command_validate_run_root(args: argparse.Namespace) -> int:
    run_root = resolve_run_root(args.run_root, "--run-root")
    summary = validate_run_root(run_root)
    summary_path = write_determinism_summary(run_root, summary)
    summary["summary_path"] = str(summary_path)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1


def command_run(args: argparse.Namespace) -> int:
    resolved_inputs = validate_explicit_smoke_inputs(args.project, args.editor, args.reference_root)
    design_root = resolve_design_root(args.design_root)
    output_root = resolve_run_root(args.output_root, "--output-root")
    output_root.mkdir(parents=True, exist_ok=True)

    command = build_suite_command(args, resolved_inputs, design_root, output_root)
    print("Determinism smoke command:")
    print(" ".join(f'"{part}"' if " " in part else part for part in command))
    suite_process = subprocess.run(command, check=False)

    summary = validate_run_root(output_root)
    summary["suite_exit_code"] = suite_process.returncode
    summary["command"] = command
    if suite_process.returncode != 0:
        append_blocked_reason(
            summary,
            "suite_exit_code_nonzero",
            exit_code=suite_process.returncode,
        )
        summary["passed"] = False

    summary_path = write_determinism_summary(output_root, summary)
    summary["summary_path"] = str(summary_path)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Focused fail-closed Design2UMG determinism smoke")
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_inputs_parser = subparsers.add_parser("validate-inputs", help="Fail closed if project/editor/reference-root is missing")
    validate_inputs_parser.add_argument("--project", default="")
    validate_inputs_parser.add_argument("--editor", default="")
    validate_inputs_parser.add_argument("--reference-root", default="")
    validate_inputs_parser.add_argument("--design-root", default="")
    validate_inputs_parser.set_defaults(func=command_validate_inputs)

    validate_run_parser = subparsers.add_parser("validate-run-root", help="Validate a determinism smoke output root")
    validate_run_parser.add_argument("--run-root", default="")
    validate_run_parser.set_defaults(func=command_validate_run_root)

    run_parser = subparsers.add_parser("run", help="Run the focused determinism smoke and validate the output")
    run_parser.add_argument("--project", default="")
    run_parser.add_argument("--editor", default="")
    run_parser.add_argument("--reference-root", default="")
    run_parser.add_argument("--output-root", default="")
    run_parser.add_argument("--design-root", default="")
    run_parser.add_argument("--include", default="", help="Optional case-name regex; default validates the entire discovered corpus.")
    run_parser.add_argument("--figma-export-root", action="append", default=[])
    run_parser.add_argument("--no-publish-history", action="store_true")
    run_parser.add_argument("--log-cmds", default="")
    run_parser.set_defaults(func=command_run)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
