import argparse
import json
import sys
from pathlib import Path

import design2umg_auto_verify as verify
from design2umg_verify_lib.case_metrics import (
    resolve_case_report_path as helper_resolve_case_report_path,
    resolve_report_metrics,
    summary_metrics_payload,
)
from design2umg_verify_lib.compatibility import evaluate_report_compatibility
from design2umg_verify_lib.evidence_contract import collect_source_input
from design2umg_verify_lib.evidence_contract import same_source_input
from design2umg_verify_lib.json_io import load_json as helper_load_json


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
    return helper_load_json(path)


def append_blocked_reason(result: dict, reason: str, summary: str, **details: object) -> None:
    if reason not in result["blocked_reasons"]:
        result["blocked_reasons"].append(reason)

    detail_payload = {
        "reason": reason,
        "summary": summary,
    }
    for key, value in details.items():
        if value is None:
            continue
        if isinstance(value, (str, list, dict)) and not value:
            continue
        detail_payload[key] = value
    result["blocked_reason_details"].append(detail_payload)


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
    direct_case_report_path = helper_resolve_case_report_path(output_root, case_name, summary_case)
    if direct_case_report_path.is_file():
        return direct_case_report_path.resolve()

    root_compare_report_path = output_root / "compare_report.json"
    if root_compare_report_path.is_file():
        return root_compare_report_path.resolve()

    return direct_case_report_path.resolve()


def collect_report_path_candidates(output_root: Path, case_name: str, summary_case: dict | None) -> list[str]:
    summary_case = summary_case or {}
    candidates: list[Path] = []

    summary_report_path = summary_case.get("report_path", "")
    if summary_report_path:
        candidates.append(Path(summary_report_path))

    candidates.append(output_root / case_name / "compare_report.json")
    candidates.append(output_root / "compare_report.json")

    unique_candidates: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        candidate_text = str(candidate.resolve())
        if candidate_text in seen:
            continue
        seen.add(candidate_text)
        unique_candidates.append(candidate_text)
    return unique_candidates


def resolve_case_metrics(output_root: Path, case_name: str, summary_case: dict | None) -> dict:
    summary_case = summary_case or {}
    report_path = resolve_report_path(output_root, case_name, summary_case)
    summary_metrics = summary_metrics_payload(summary_case)
    if any(value is not None for value in summary_metrics.values()):
        return {
            "report_path": str(report_path),
            **summary_metrics,
        }

    report_metrics, _ = resolve_report_metrics(
        report_path,
        load_json_fn=load_json,
        extract_compare_metrics_fn=verify.extract_compare_metrics,
        compare_image_paths_fn=verify.compare_image_paths,
        default_pixel_tolerance=DEFAULT_PIXEL_TOLERANCE,
    )
    if report_metrics:
        return {
            "report_path": str(report_path),
            **report_metrics,
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


def source_metric_value(source: dict, metric_name: str) -> object:
    return source.get(metric_name)


def best_metric_source(best_case: dict, best_metric: str) -> dict:
    metric_sources = best_case.get("metric_sources") or {}
    source = metric_sources.get(best_metric)
    return source if isinstance(source, dict) else {}


def source_matches_metric_source(source: dict, metric_source: dict) -> bool:
    if not metric_source:
        return False
    return (
        source.get("run") == metric_source.get("run")
        and source.get("report_path") == metric_source.get("report_path")
    )


def current_source_input(case_name: str, output_root: Path, run_scope: dict | None, summary: dict | None, report: dict, current_report_path: Path) -> dict:
    return collect_source_input(
        output_root,
        case_name,
        run_scope,
        summary,
        report,
        current_report_path,
        allow_explicit=False,
    )


def find_next_admissible_source(
    admitted_sources: list[dict],
    suspect_source: dict,
    best_metric: str,
) -> dict | None:
    candidates = []
    for source in admitted_sources:
        if not isinstance(source, dict):
            continue
        if source_matches_metric_source(source, suspect_source):
            continue
        value = source_metric_value(source, best_metric)
        if value is None:
            continue
        candidates.append(source)
    if not candidates:
        return None
    return sorted(
        candidates,
        key=lambda source: (
            source_metric_value(source, best_metric),
            source.get("run", ""),
            source.get("report_path", ""),
        ),
    )[0]


def count_best_repetitions(admitted_sources: list[dict], best_metric: str, best_value: object, epsilon: float) -> int:
    count = 0
    for source in admitted_sources:
        value = source_metric_value(source, best_metric)
        if value is None or best_value is None:
            continue
        try:
            if abs(float(value) - float(best_value)) <= epsilon:
                count += 1
        except (TypeError, ValueError):
            continue
    return count


def evaluate_one_off_quarantine(
    *,
    case_name: str,
    regressions: list[dict],
    best_case: dict,
    current_metrics: dict,
    current_source: dict,
    current_report: dict,
    metric_epsilon: float,
) -> dict | None:
    admitted_sources = best_case.get("admitted_sources") or []
    if not isinstance(admitted_sources, list) or not admitted_sources:
        return None

    quarantined_metrics = []
    quarantined_run = ""
    quarantined_report_path = ""
    reasons = set()
    for regression in regressions:
        current_metric = regression["metric"]
        best_metric = next((pair_best for pair_current, pair_best in METRIC_PAIRS if pair_current == current_metric), "")
        if not best_metric:
            return None
        best_source = best_metric_source(best_case, best_metric)
        if not best_source:
            best_source = {
                "run": best_case.get("run", ""),
                "report_path": best_case.get("report_path", ""),
                "value": best_case.get(best_metric),
            }
        source_match = next(
            (source for source in admitted_sources if source_matches_metric_source(source, best_source)),
            None,
        )
        if source_match is None:
            return None
        if quarantined_run and (
            quarantined_run != source_match.get("run", "")
            or quarantined_report_path != source_match.get("report_path", "")
        ):
            return None
        quarantined_run = source_match.get("run", "")
        quarantined_report_path = source_match.get("report_path", "")
        best_binding = (best_source.get("binary_binding") or source_match.get("binary_binding") or {})
        if best_binding.get("binary_bound"):
            return None
        reasons.add("best_source_lacks_binary_binding")

        effective_epsilon = resolve_metric_epsilon(current_metric, current_report, metric_epsilon)
        best_value = best_source.get("value", best_case.get(best_metric))
        if count_best_repetitions(admitted_sources, best_metric, best_value, effective_epsilon) != 1:
            return None
        reasons.add("best_metric_source_single_admitted_run")

        best_source_input = best_source.get("source_input") or source_match.get("source_input") or {}
        inputs_match, mismatches = same_source_input(best_source_input, current_source)
        if not inputs_match:
            return None
        reasons.add("same_source_input_rerun_did_not_reproduce_best")

        next_source = find_next_admissible_source(admitted_sources, source_match, best_metric)
        if next_source is None:
            return None
        next_value = next_source.get(best_metric)
        current_value = current_metrics.get(current_metric)
        if current_value is None or next_value is None:
            return None
        delta_to_next = current_value - next_value
        if delta_to_next > effective_epsilon:
            return None
        reasons.add("current_matches_next_admissible_best")
        quarantined_metrics.append(
            {
                "metric": current_metric,
                "historical_best_metric": best_metric,
                "quarantined_value": best_value,
                "current_value": current_value,
                "next_best_value": next_value,
                "delta_to_next_best": delta_to_next,
                "effective_epsilon": effective_epsilon,
                "next_best_run": next_source.get("run", ""),
                "next_best_report_path": next_source.get("report_path", ""),
                "same_source_input_mismatches": mismatches,
            }
        )

    return {
        "case": case_name,
        "metrics": [item["metric"] for item in quarantined_metrics],
        "quarantined_run": quarantined_run,
        "quarantined_report_path": quarantined_report_path,
        "reasons": sorted(reasons),
        "next_best_replacement": quarantined_metrics,
        "current_values": {item["metric"]: item["current_value"] for item in quarantined_metrics},
        "next_best_values": {item["metric"]: item["next_best_value"] for item in quarantined_metrics},
        "effective_epsilon": {item["metric"]: item["effective_epsilon"] for item in quarantined_metrics},
    }


def validate_run(output_root: Path, admissible_best_path: Path | None = None, metric_epsilon: float = DEFAULT_METRIC_EPSILON) -> dict:
    result = {
        "output_root": str(output_root),
        "blocked": False,
        "blocked_reasons": [],
        "blocked_reason_details": [],
        "run_scope_path": str(output_root / "run_scope.json"),
        "summary_path": str(output_root / "summary.json"),
        "expected_case_names": [],
        "summary_case_names": [],
        "case_dir_names": [],
        "missing_summary": False,
        "missing_run_scope": False,
        "missing_compare_reports": [],
        "missing_compare_report_details": [],
        "missing_case_summary_entries": [],
        "missing_case_summary_entry_details": [],
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
        "historical_best_status": "normal_pass",
        "historical_best_quarantined_one_off_sources": [],
    }

    run_scope_path = output_root / "run_scope.json"
    summary_path = output_root / "summary.json"

    run_scope = load_json(run_scope_path) if run_scope_path.is_file() else None
    summary = load_json(summary_path) if summary_path.is_file() else None

    if run_scope is None:
        result["missing_run_scope"] = True
        append_blocked_reason(
            result,
            "missing_run_scope",
            f"run_scope.json is missing: {run_scope_path}",
            path=str(run_scope_path),
            action="Confirm the automation run reached the stage that writes run_scope.json.",
        )

    if summary is None:
        result["missing_summary"] = True
        append_blocked_reason(
            result,
            "missing_summary",
            f"summary.json is missing: {summary_path}",
            path=str(summary_path),
            action="Confirm the automation run completed summary writing before validating the output root.",
        )

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
                result["missing_compare_report_details"].append(
                    {
                        "case": case_name,
                        "checked_paths": collect_report_path_candidates(output_root, case_name, summary_case),
                    }
                )
            if case_name not in summary_case_set:
                result["missing_case_summary_entries"].append(case_name)
                result["missing_case_summary_entry_details"].append(
                    {
                        "case": case_name,
                        "summary_path": str(summary_path),
                    }
                )

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
                result["missing_compare_report_details"].append(
                    {
                        "case": case_name,
                        "checked_paths": collect_report_path_candidates(output_root, case_name, item),
                    }
                )

    if result["missing_compare_reports"]:
        append_blocked_reason(
            result,
            "missing_compare_report",
            f"{len(result['missing_compare_reports'])} case(s) are missing compare_report.json.",
            count=len(result["missing_compare_reports"]),
            detail_field="missing_compare_report_details",
            action="Check the reported paths or rerun automation for the affected cases so each case writes compare_report.json.",
        )
    if result["missing_case_summary_entries"]:
        append_blocked_reason(
            result,
            "missing_case_summary_entry",
            f"{len(result['missing_case_summary_entries'])} expected case(s) are missing from summary.json.",
            count=len(result["missing_case_summary_entries"]),
            detail_field="missing_case_summary_entry_details",
            action="Regenerate summary.json and confirm every run-scope case is emitted into the summary cases list.",
        )

    current_rule_case_names = expected_case_names or summary_case_names or case_dir_names
    for case_name in current_rule_case_names:
        summary_case = summary_cases_by_name.get(case_name) or {}
        compare_report_path = resolve_report_path(output_root, case_name, summary_case)
        if not compare_report_path.is_file():
            continue

        compatibility = evaluate_report_compatibility(compare_report_path)
        result["render_size_rule_checked"] = True
        current_rule = compatibility["render_size"]
        if not current_rule.get("compatible"):
            result["render_size_rule_incompatible_cases"].append(
                {
                    "case": case_name,
                    "current": current_rule,
                }
            )

        result["pipeline_determinism_rule_checked"] = True
        current_pipeline_rule = compatibility["pipeline_determinism"]
        if not current_pipeline_rule.get("compatible"):
            result["pipeline_determinism_rule_incompatible_cases"].append(
                {
                    "case": case_name,
                    "current": current_pipeline_rule,
                }
            )

    if result["render_size_rule_incompatible_cases"]:
        append_blocked_reason(
            result,
            "render_size_rule_incompatible",
            f"{len(result['render_size_rule_incompatible_cases'])} case(s) violate the render-size rule.",
            count=len(result["render_size_rule_incompatible_cases"]),
            detail_field="render_size_rule_incompatible_cases",
            action="Inspect the listed render-size payloads and compare them with the report's reference authority and UE result dimensions.",
        )
    if result["pipeline_determinism_rule_incompatible_cases"]:
        append_blocked_reason(
            result,
            "pipeline_determinism_rule_incompatible",
            f"{len(result['pipeline_determinism_rule_incompatible_cases'])} case(s) violate the pipeline-determinism rule.",
            count=len(result["pipeline_determinism_rule_incompatible_cases"]),
            detail_field="pipeline_determinism_rule_incompatible_cases",
            action="Inspect the listed determinism payloads and confirm transient-state reset, package reload, and font-cache authorities are present.",
        )

    if admissible_best_path is not None:
        admissible_best = load_json(admissible_best_path) if admissible_best_path.is_file() else None
        if admissible_best is None:
            append_blocked_reason(
                result,
                "missing_admissible_best",
                f"Admissible-best manifest is missing: {admissible_best_path}",
                path=str(admissible_best_path),
                action="Rebuild or point --admissible-best-json at a valid admissible-best manifest before validating historical-best parity.",
            )
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

                current_compatibility = evaluate_report_compatibility(current_report_path)
                best_compatibility = evaluate_report_compatibility(Path(best_case.get("report_path", "")))
                current_rule = current_compatibility["render_size"]
                best_rule = best_compatibility["render_size"]
                if not current_rule.get("compatible") or not best_rule.get("compatible"):
                    result["historical_best_incompatible_cases"].append(
                        {
                            "case": case_name,
                            "current": current_rule,
                            "historical_best": best_rule,
                        }
                    )
                    continue

                current_pipeline_rule = current_compatibility["pipeline_determinism"]
                best_pipeline_rule = best_compatibility["pipeline_determinism"]
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
                    current_source = current_source_input(
                        case_name,
                        output_root,
                        run_scope,
                        summary,
                        current_report,
                        current_report_path,
                    )
                    quarantine = evaluate_one_off_quarantine(
                        case_name=case_name,
                        regressions=regressions,
                        best_case=best_case,
                        current_metrics=resolved_current_metrics,
                        current_source=current_source,
                        current_report=current_report,
                        metric_epsilon=metric_epsilon,
                    )
                    if quarantine is None:
                        result["historical_best_regressions"].append(
                            {
                                "case": case_name,
                                "regressions": regressions,
                            }
                        )
                    else:
                        result["historical_best_quarantined_one_off_sources"].append(quarantine)

            if result["historical_best_missing_cases"]:
                append_blocked_reason(
                    result,
                    "historical_best_missing_case",
                    f"{len(result['historical_best_missing_cases'])} case(s) cannot be matched between the current run and admissible-best manifest.",
                    count=len(result["historical_best_missing_cases"]),
                    detail_field="historical_best_missing_cases",
                    action="Check case discovery and manifest contents so the same case names exist in both the current run and admissible-best JSON.",
                )
            if result["historical_best_incompatible_cases"]:
                append_blocked_reason(
                    result,
                    "historical_best_render_size_rule_incompatible",
                    f"{len(result['historical_best_incompatible_cases'])} case(s) are not comparable because the current or historical-best report violates the render-size rule.",
                    count=len(result["historical_best_incompatible_cases"]),
                    detail_field="historical_best_incompatible_cases",
                    action="Inspect both current and historical-best compare reports for the listed cases before judging regressions.",
                )
            if result["historical_best_pipeline_determinism_incompatible_cases"]:
                append_blocked_reason(
                    result,
                    "historical_best_pipeline_determinism_rule_incompatible",
                    f"{len(result['historical_best_pipeline_determinism_incompatible_cases'])} case(s) are not comparable because the current or historical-best report violates the pipeline-determinism rule.",
                    count=len(result["historical_best_pipeline_determinism_incompatible_cases"]),
                    detail_field="historical_best_pipeline_determinism_incompatible_cases",
                    action="Inspect both current and historical-best compare reports for the listed determinism metadata before judging regressions.",
                )
            if result["historical_best_degraded_authority_cases"]:
                append_blocked_reason(
                    result,
                    "historical_best_degraded_authority",
                    f"{len(result['historical_best_degraded_authority_cases'])} case(s) use degraded reference authority and cannot be admitted as historical-best parity evidence.",
                    count=len(result["historical_best_degraded_authority_cases"]),
                    detail_field="historical_best_degraded_authority_cases",
                    action="Re-run those cases with strong or external_export_nodes reference authority before using them for historical-best validation.",
                )
            if result["historical_best_regressions"]:
                append_blocked_reason(
                    result,
                    "historical_best_regression",
                    f"{len(result['historical_best_regressions'])} case(s) regressed against admissible-best metrics.",
                    count=len(result["historical_best_regressions"]),
                    detail_field="historical_best_regressions",
                    action="Inspect the per-metric regression deltas and rerun or fix the current output before claiming no regression.",
                )

    if result["historical_best_checked"]:
        if result["blocked_reasons"] or result["historical_best_regressions"]:
            result["historical_best_status"] = "blocked"
        elif result["historical_best_quarantined_one_off_sources"]:
            result["historical_best_status"] = "pass_with_quarantined_one_off_best"
        else:
            result["historical_best_status"] = "normal_pass"
    elif result["blocked_reasons"]:
        result["historical_best_status"] = "blocked"

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
        write_path.write_text(payload, encoding="utf-8")

    sys.exit(BLOCKED_EXIT_CODE if result["blocked"] else 0)


if __name__ == "__main__":
    main()
