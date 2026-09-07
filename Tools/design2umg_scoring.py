"""No-regression scoring for Design2UMG batch verification.

Behavior-equivalent extraction from design2umg_auto_verify_batch.py (OpenSpec
change add-layered-validation, Phase 1). Pixel-level metric computation
(compare_image_paths / extract_compare_metrics) stays in design2umg_auto_verify.py;
this module only calls it.
"""

import math
from pathlib import Path

import design2umg_auto_verify as verify
import design2umg_render_rule as render_rule


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


def load_baseline_case_metrics(case_dir: Path | None, pixel_tolerance: int) -> dict:
    if not case_dir or not case_dir.is_dir():
        return {
            "available": False,
            "reason": "baseline_case_dir_missing",
        }

    report_path = case_dir / "compare_report.json"
    report = verify.helper_load_json(report_path) if report_path.is_file() else {}
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
    report = verify.helper_load_json(report_path) if report_path.is_file() else {}
    metrics = ((report.get("structure_validation") or {}).get("metrics")) or {}
    values = {metric_name: metrics.get(metric_name) for metric_name in STRUCTURE_BASELINE_METRICS}
    missing = [metric_name for metric_name, value in values.items() if value is None]
    result = {"available": not missing, **values}
    result["reason"] = "" if not missing else "baseline_structure_metrics_unavailable"
    return result


def evaluate_no_regression(report: dict, case_dir: Path | None, pixel_tolerance: int, epsilon: float = NO_REGRESSION_EPSILON) -> dict:
    # A non-finite or negative epsilon silently disables the regression gate
    # (delta > nan is always False, delta > -1 is always True), so reject it
    # loudly instead of letting every case pass vacuously.
    if not math.isfinite(epsilon) or epsilon < 0:
        raise ValueError(f"epsilon must be a finite non-negative number, got {epsilon!r}")
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
        if delta > epsilon and not regressed_metric:
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


def assemble_structure_baseline(baseline_case_dir: Path | None, report: dict) -> tuple[dict, dict]:
    """Assemble the structure-guard tracking blocks for one case.

    Returns (report_block, result_fields): report_block is the value written to
    compare_report.json["structure_baseline"]; result_fields are the per-case
    summary columns derived from the same structure current/baseline/delta.
    """
    structure_current = dict.fromkeys(STRUCTURE_BASELINE_METRICS)
    structure_metrics = ((report.get("structure_validation") or {}).get("metrics")) or {}
    for metric_name in STRUCTURE_BASELINE_METRICS:
        structure_current[metric_name] = structure_metrics.get(metric_name)
    structure_baseline = load_baseline_structure_metrics(baseline_case_dir)
    structure_delta = {
        metric_name: (
            None
            if structure_current[metric_name] is None or structure_baseline.get(metric_name) is None
            else round(structure_current[metric_name] - structure_baseline[metric_name], 6)
        )
        for metric_name in STRUCTURE_BASELINE_METRICS
    }
    report_block = {
        "available": structure_baseline.get("available"),
        "reason": structure_baseline.get("reason"),
        "current": structure_current,
        "baseline": {metric_name: structure_baseline.get(metric_name) for metric_name in STRUCTURE_BASELINE_METRICS},
        "delta": structure_delta,
    }
    result_fields = {
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
    }
    return report_block, result_fields
