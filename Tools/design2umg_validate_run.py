import argparse
import json
import sys
from pathlib import Path

import design2umg_auto_verify as verify
import design2umg_render_rule as render_rule


BLOCKED_EXIT_CODE = 2
DEFAULT_METRIC_EPSILON = 1e-6
DEFAULT_PIXEL_TOLERANCE = 6
RATIO_METRICS = {"stable_changed_ratio", "changed_ratio"}
METRIC_PAIRS = [
    ("stable_changed_ratio", "stable_changed_ratio"),
    ("changed_ratio", "changed_ratio"),
    ("stable_mae", "stable_mean_abs_error"),
    ("mae", "mean_abs_error"),
]


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def collect_case_dirs(output_root: Path) -> set[str]:
    names: set[str] = set()
    for child in output_root.iterdir():
        if not child.is_dir():
            continue
        if child.name.startswith("_"):
            continue
        if not (child / "compare_report.json").is_file():
            continue
        names.add(child.name)
    return names


def resolve_report_path(output_root: Path, case_name: str, summary_case: dict | None) -> Path:
    summary_case = summary_case or {}
    summary_report_path = summary_case.get("report_path", "")
    if summary_report_path:
        candidate = Path(summary_report_path)
        if candidate.is_file():
            return candidate.resolve()

    direct_case_report_path = output_root / case_name / "compare_report.json"
    if direct_case_report_path.is_file():
        return direct_case_report_path.resolve()

    root_compare_report_path = output_root / "compare_report.json"
    if root_compare_report_path.is_file():
        return root_compare_report_path.resolve()

    return direct_case_report_path.resolve()


def resolve_case_metrics(output_root: Path, case_name: str, summary_case: dict | None) -> dict:
    summary_case = summary_case or {}
    report_path = resolve_report_path(output_root, case_name, summary_case)
    summary_metrics = {
        "stable_changed_ratio": summary_case.get("stable_changed_ratio"),
        "changed_ratio": summary_case.get("changed_ratio"),
        "stable_mae": summary_case.get("stable_mae"),
        "mae": summary_case.get("mae"),
    }
    if any(value is not None for value in summary_metrics.values()):
        return {
            "report_path": str(report_path),
            **summary_metrics,
        }

    report = load_json(report_path) if report_path.is_file() else {}
    pixel_tolerance = report.get("pixel_tolerance", DEFAULT_PIXEL_TOLERANCE)
    try:
        pixel_tolerance = int(pixel_tolerance)
    except (TypeError, ValueError):
        pixel_tolerance = DEFAULT_PIXEL_TOLERANCE

    reference_png_path = Path(report.get("reference_png_path") or (report_path.parent / "reference.png"))
    candidate_png_path = Path(report.get("candidate_png_path") or (report_path.parent / "candidate.png"))
    report_metrics = verify.extract_compare_metrics(report)
    if report_metrics:
        return {
            "report_path": str(report_path),
            "stable_changed_ratio": report_metrics.get("stable_changed_ratio"),
            "changed_ratio": report_metrics.get("changed_ratio"),
            "stable_mae": report_metrics.get("stable_mean_abs_error"),
            "mae": report_metrics.get("mean_abs_error"),
        }

    image_metrics = verify.compare_image_paths(reference_png_path, candidate_png_path, pixel_tolerance)
    if image_metrics:
        return {
            "report_path": str(report_path),
            "stable_changed_ratio": image_metrics.get("stable_changed_ratio"),
            "changed_ratio": image_metrics.get("changed_ratio"),
            "stable_mae": image_metrics.get("stable_mean_abs_error"),
            "mae": image_metrics.get("mean_abs_error"),
        }

    return {
        "report_path": str(report_path),
        "stable_changed_ratio": summary_case.get("stable_changed_ratio"),
        "changed_ratio": summary_case.get("changed_ratio"),
        "stable_mae": summary_case.get("stable_mae"),
        "mae": summary_case.get("mae"),
    }


def resolve_report_pixel_quantum(report: dict) -> float | None:
    for key in ("comparison_candidate_size", "candidate_size", "comparison_reference_size", "reference_size"):
        size = report.get(key)
        if not isinstance(size, list) or len(size) != 2:
            continue
        try:
            width = int(size[0])
            height = int(size[1])
        except (TypeError, ValueError):
            continue
        pixel_count = width * height
        if pixel_count > 0:
            return 1.0 / pixel_count
    return None


def resolve_metric_epsilon(metric_name: str, current_report: dict, default_epsilon: float) -> float:
    if metric_name not in RATIO_METRICS:
        return default_epsilon

    pixel_quantum = resolve_report_pixel_quantum(current_report)
    if pixel_quantum is None:
        return default_epsilon

    return max(default_epsilon, pixel_quantum) + default_epsilon


def validate_run(output_root: Path, admissible_best_path: Path | None = None, metric_epsilon: float = DEFAULT_METRIC_EPSILON) -> dict:
    result = {
        "output_root": str(output_root),
        "blocked": False,
        "blocked_reasons": [],
        "run_scope_path": str(output_root / "run_scope.json"),
        "summary_path": str(output_root / "summary.json"),
        "expected_case_names": [],
        "summary_case_names": [],
        "case_dir_names": [],
        "missing_summary": False,
        "missing_run_scope": False,
        "missing_compare_reports": [],
        "missing_case_summary_entries": [],
        "unexpected_summary_cases": [],
        "unexpected_case_dirs": [],
        "admissible_best_path": str(admissible_best_path) if admissible_best_path else "",
        "metric_epsilon": metric_epsilon,
        "historical_best_checked": False,
        "historical_best_missing_cases": [],
        "historical_best_incompatible_cases": [],
        "historical_best_degraded_authority_cases": [],
        "historical_best_regressions": [],
        "render_size_rule_checked": False,
        "render_size_rule_incompatible_cases": [],
        "pipeline_determinism_rule_checked": False,
        "pipeline_determinism_rule_incompatible_cases": [],
        "historical_best_pipeline_determinism_incompatible_cases": [],
    }

    run_scope_path = output_root / "run_scope.json"
    summary_path = output_root / "summary.json"

    run_scope = load_json(run_scope_path) if run_scope_path.is_file() else None
    summary = load_json(summary_path) if summary_path.is_file() else None

    if run_scope is None:
        result["missing_run_scope"] = True
        result["blocked_reasons"].append("missing_run_scope")

    if summary is None:
        result["missing_summary"] = True
        result["blocked_reasons"].append("missing_summary")

    expected_case_names = list((run_scope or {}).get("case_names") or [])
    result["expected_case_names"] = expected_case_names

    case_dir_names = sorted(collect_case_dirs(output_root))
    result["case_dir_names"] = case_dir_names

    summary_case_names = [item.get("name", "") for item in ((summary or {}).get("cases") or []) if item.get("name")]
    result["summary_case_names"] = summary_case_names
    summary_cases_by_name = {
        item.get("name"): item
        for item in ((summary or {}).get("cases") or [])
        if item.get("name")
    }

    expected_case_set = set(expected_case_names)
    case_dir_set = set(case_dir_names)
    summary_case_set = set(summary_case_names)

    if expected_case_names:
        for case_name in sorted(expected_case_set):
            summary_case = summary_cases_by_name.get(case_name) or {}
            compare_report_path = resolve_report_path(output_root, case_name, summary_case)
            if not compare_report_path.is_file():
                result["missing_compare_reports"].append(case_name)
            if case_name not in summary_case_set:
                result["missing_case_summary_entries"].append(case_name)

        result["unexpected_summary_cases"] = sorted(summary_case_set - expected_case_set)
        result["unexpected_case_dirs"] = sorted(case_dir_set - expected_case_set)
    elif summary is not None:
        for item in (summary.get("cases") or []):
            case_name = item.get("name")
            if not case_name:
                continue
            compare_report_path = resolve_report_path(output_root, case_name, item)
            if not compare_report_path.is_file():
                result["missing_compare_reports"].append(case_name)

    if result["missing_compare_reports"]:
        result["blocked_reasons"].append("missing_compare_report")
    if result["missing_case_summary_entries"]:
        result["blocked_reasons"].append("missing_case_summary_entry")

    current_rule_case_names = expected_case_names or summary_case_names or case_dir_names
    for case_name in current_rule_case_names:
        summary_case = summary_cases_by_name.get(case_name) or {}
        compare_report_path = resolve_report_path(output_root, case_name, summary_case)
        if not compare_report_path.is_file():
            continue

        result["render_size_rule_checked"] = True
        current_rule = render_rule.evaluate_report_render_size_rule(compare_report_path)
        if not current_rule.get("compatible"):
            result["render_size_rule_incompatible_cases"].append(
                {
                    "case": case_name,
                    "current": current_rule,
                }
            )

        result["pipeline_determinism_rule_checked"] = True
        current_pipeline_rule = render_rule.evaluate_report_pipeline_determinism_rule(compare_report_path)
        if not current_pipeline_rule.get("compatible"):
            result["pipeline_determinism_rule_incompatible_cases"].append(
                {
                    "case": case_name,
                    "current": current_pipeline_rule,
                }
            )

    if result["render_size_rule_incompatible_cases"]:
        result["blocked_reasons"].append("render_size_rule_incompatible")
    if result["pipeline_determinism_rule_incompatible_cases"]:
        result["blocked_reasons"].append("pipeline_determinism_rule_incompatible")

    if admissible_best_path is not None:
        admissible_best = load_json(admissible_best_path) if admissible_best_path.is_file() else None
        if admissible_best is None:
            result["blocked_reasons"].append("missing_admissible_best")
        else:
            result["historical_best_checked"] = True
            best_cases = (admissible_best or {}).get("cases") or {}
            case_names_to_check = expected_case_names or list(summary_cases_by_name.keys())
            for case_name in case_names_to_check:
                best_case = best_cases.get(case_name)
                current_case = summary_cases_by_name.get(case_name)
                if best_case is None or current_case is None:
                    result["historical_best_missing_cases"].append(case_name)
                    continue

                resolved_current_metrics = resolve_case_metrics(output_root, case_name, current_case)
                current_report_path = Path(resolved_current_metrics.get("report_path", ""))
                current_report = load_json(current_report_path) if current_report_path.is_file() else {}
                current_reference_authority = (
                    ((current_report.get("reference_context") or {}).get("reference_authority") or {})
                )
                current_verification_level = str(current_reference_authority.get("verification_level") or "")
                if current_verification_level and current_verification_level not in {"strong", "external_export_nodes"}:
                    result["historical_best_degraded_authority_cases"].append(
                        {
                            "case": case_name,
                            "verification_level": current_verification_level,
                            "canonical_source": current_reference_authority.get("canonical_source", ""),
                            "reason": current_reference_authority.get("reason", ""),
                        }
                    )
                    continue

                current_rule = render_rule.evaluate_report_render_size_rule(current_report_path)
                best_rule = render_rule.evaluate_report_render_size_rule(Path(best_case.get("report_path", "")))
                if not current_rule.get("compatible") or not best_rule.get("compatible"):
                    result["historical_best_incompatible_cases"].append(
                        {
                            "case": case_name,
                            "current": current_rule,
                            "historical_best": best_rule,
                        }
                    )
                    continue

                current_pipeline_rule = render_rule.evaluate_report_pipeline_determinism_rule(
                    Path(resolved_current_metrics.get("report_path", ""))
                )
                best_pipeline_rule = render_rule.evaluate_report_pipeline_determinism_rule(Path(best_case.get("report_path", "")))
                if not current_pipeline_rule.get("compatible") or not best_pipeline_rule.get("compatible"):
                    result["historical_best_pipeline_determinism_incompatible_cases"].append(
                        {
                            "case": case_name,
                            "current": current_pipeline_rule,
                            "historical_best": best_pipeline_rule,
                        }
                    )
                    continue

                regressions = []
                for current_metric, best_metric in METRIC_PAIRS:
                    current_value = resolved_current_metrics.get(current_metric)
                    best_value = best_case.get(best_metric)
                    if current_value is None or best_value is None:
                        continue
                    delta = current_value - best_value
                    effective_epsilon = resolve_metric_epsilon(current_metric, current_report, metric_epsilon)
                    if delta > effective_epsilon:
                        regressions.append(
                            {
                                "metric": current_metric,
                                "current": current_value,
                                "historical_best": best_value,
                                "delta": delta,
                                "epsilon": effective_epsilon,
                            }
                        )

                if regressions:
                    result["historical_best_regressions"].append(
                        {
                            "case": case_name,
                            "regressions": regressions,
                        }
                    )

            if result["historical_best_missing_cases"]:
                result["blocked_reasons"].append("historical_best_missing_case")
            if result["historical_best_incompatible_cases"]:
                result["blocked_reasons"].append("historical_best_render_size_rule_incompatible")
            if result["historical_best_pipeline_determinism_incompatible_cases"]:
                result["blocked_reasons"].append("historical_best_pipeline_determinism_rule_incompatible")
            if result["historical_best_degraded_authority_cases"]:
                result["blocked_reasons"].append("historical_best_degraded_authority")
            if result["historical_best_regressions"]:
                result["blocked_reasons"].append("historical_best_regression")

    result["blocked"] = bool(result["blocked_reasons"])
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate a Design2UMG automation run root for baseline completeness."
    )
    parser.add_argument(
        "--output-root",
        required=True,
        help="Automation run root containing run_scope.json and summary.json",
    )
    parser.add_argument(
        "--write-json",
        default="",
        help="Optional path to write the validation result JSON",
    )
    parser.add_argument(
        "--admissible-best-json",
        default="",
        help="Optional admissible-best JSON used to enforce per-case historical-best parity",
    )
    parser.add_argument(
        "--metric-epsilon",
        type=float,
        default=DEFAULT_METRIC_EPSILON,
        help="Tolerance used when comparing current metrics against admissible-best metrics",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_root = Path(args.output_root).resolve()
    if not output_root.is_dir():
        raise RuntimeError(f"Automation output root not found: {output_root}")

    admissible_best_path = Path(args.admissible_best_json).resolve() if args.admissible_best_json else None
    result = validate_run(output_root, admissible_best_path=admissible_best_path, metric_epsilon=args.metric_epsilon)
    payload = json.dumps(result, ensure_ascii=False, indent=2)
    print(payload)

    if args.write_json:
        write_path = Path(args.write_json).resolve()
        write_path.parent.mkdir(parents=True, exist_ok=True)
        write_path.write_text(payload, encoding="utf-8-sig")

    sys.exit(BLOCKED_EXIT_CODE if result["blocked"] else 0)


if __name__ == "__main__":
    main()
