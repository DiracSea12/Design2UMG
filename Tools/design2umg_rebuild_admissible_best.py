import argparse
import json
import time
from pathlib import Path

import design2umg_auto_verify as verify
import design2umg_render_rule as render_rule
from design2umg_verify_lib.case_metrics import (
    normalize_case_metrics,
    resolve_case_report_path,
    resolve_report_metrics,
    summary_metrics_payload,
)
from design2umg_verify_lib.compatibility import evaluate_report_compatibility
from design2umg_verify_lib.json_io import file_signature
from design2umg_verify_lib.json_io import load_json
from design2umg_verify_lib.json_io import write_json
from design2umg_verify_lib.manifest_paths import (
    default_pinned_baseline_path as helper_default_pinned_baseline_path,
    iter_manifest_case_report_paths as helper_iter_manifest_case_report_paths,
)


METRIC_FIELDS = (
    ("stable_changed_ratio", "stable_changed_ratio"),
    ("changed_ratio", "changed_ratio"),
    ("stable_mean_abs_error", "stable_mae"),
    ("mean_abs_error", "mae"),
)
METRIC_EPSILON = 1e-9
DEFAULT_PIXEL_TOLERANCE = 6
CACHE_FILENAME = "_admissible_best_cache.json"
CACHE_VERSION = 3
PINNED_BASELINE_FILENAME = "design2umg_pinned_baseline.json"


def resolve_admissible_best_manifest(automation_root: Path) -> tuple[Path, dict, str]:
    output_path = automation_root / "_admissible_best_latest.json"
    pinned_path = helper_default_pinned_baseline_path(Path(__file__), PINNED_BASELINE_FILENAME)
    if pinned_path.is_file():
        pinned_manifest = load_json(pinned_path)
        pinned_cases = pinned_manifest.get("cases") or {}
        skipped_by_reason: dict[str, int] = {}
        for case in pinned_cases.values():
            case_skip_reasons: set[str] = set()
            report_paths = helper_iter_manifest_case_report_paths(case, pinned_path)
            if not report_paths:
                case_skip_reasons.add("report_missing")

            for report_path in report_paths:
                compatibility = evaluate_report_compatibility(report_path)
                rule_status = compatibility["render_size"]
                if not rule_status.get("compatible"):
                    case_skip_reasons.add(rule_status.get("reason", "render_size_rule_incompatible"))
                    continue

                pipeline_rule_status = compatibility["pipeline_determinism"]
                if not pipeline_rule_status.get("compatible"):
                    case_skip_reasons.add(pipeline_rule_status.get("reason", "pipeline_determinism_rule_incompatible"))

            for reason in case_skip_reasons:
                skipped_by_reason[reason] = skipped_by_reason.get(reason, 0) + 1

        if not skipped_by_reason:
            pinned_manifest["render_size_rule"] = render_rule.DESIGN_SIZE_RENDER_RULE
            pinned_manifest["pipeline_determinism_rule"] = render_rule.PIPELINE_DETERMINISM_RULE
            write_json(output_path, pinned_manifest)
            return output_path, pinned_manifest, str(pinned_path)

        manifest = rebuild_historical_best_manifest(automation_root)
        manifest["pinned_baseline_source"] = str(pinned_path)
        manifest["pinned_baseline_skipped_by_reason"] = skipped_by_reason
        write_json(output_path, manifest)
        return output_path, manifest, f"{pinned_path} (skipped incompatible pinned entries)"

    manifest = rebuild_historical_best_manifest(automation_root)
    write_json(output_path, manifest)
    return output_path, manifest, str(output_path)


def load_cache(path: Path) -> dict:
    if not path.is_file():
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}}

    try:
        payload = load_json(path)
    except Exception:
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}}

    if not isinstance(payload, dict):
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}}

    entries = payload.get("entries")
    if not isinstance(entries, dict):
        entries = {}

    payload["version"] = CACHE_VERSION
    payload["entries"] = entries
    payload.pop("runs", None)
    payload.setdefault("generated_at", "")
    payload.setdefault("automation_root", "")
    return payload


def cache_lookup(
    cache_entries: dict,
    report_path: Path,
    report_signature: dict | None,
    reference_signature: dict | None,
    candidate_signature: dict | None,
    pixel_tolerance: int,
) -> dict | None:
    if report_signature is None or reference_signature is None or candidate_signature is None:
        return None

    cache_entry = cache_entries.get(str(report_path))
    if not isinstance(cache_entry, dict):
        return None
    if cache_entry.get("version") != CACHE_VERSION:
        return None
    if cache_entry.get("pixel_tolerance") != pixel_tolerance:
        return None
    if cache_entry.get("report_signature") != report_signature:
        return None
    if cache_entry.get("reference_signature") != reference_signature:
        return None
    if cache_entry.get("candidate_signature") != candidate_signature:
        return None

    metrics = normalize_case_metrics(cache_entry.get("metrics"))
    return metrics


def cache_store(
    cache_entries: dict,
    report_path: Path,
    report_signature: dict | None,
    reference_signature: dict | None,
    candidate_signature: dict | None,
    pixel_tolerance: int,
    metrics: dict | None,
) -> None:
    normalized_metrics = normalize_case_metrics(metrics)
    if (
        report_signature is None
        or reference_signature is None
        or candidate_signature is None
        or normalized_metrics is None
    ):
        return

    cache_entries[str(report_path)] = {
        "version": CACHE_VERSION,
        "pixel_tolerance": pixel_tolerance,
        "report_signature": report_signature,
        "reference_signature": reference_signature,
        "candidate_signature": candidate_signature,
        "metrics": normalized_metrics,
    }


def load_run_scope(run_root: Path) -> dict | None:
    run_scope_path = run_root / "run_scope.json"
    if not run_scope_path.is_file():
        return None

    try:
        payload = load_json(run_scope_path)
    except Exception:
        return None
    return payload if isinstance(payload, dict) else None


def load_historical_validation(run_root: Path) -> dict | None:
    validation_path = run_root / "validation_against_historical_best.json"
    if not validation_path.is_file():
        return None

    try:
        payload = load_json(validation_path)
    except Exception:
        return None
    return payload if isinstance(payload, dict) else None


def classify_summary_admissibility(run_root: Path, summary: dict) -> tuple[bool, str]:
    blocked_cases = summary.get("blocked_cases")
    if isinstance(blocked_cases, int) and blocked_cases > 0:
        return False, "summary_blocked_cases"

    completeness = summary.get("completeness") or {}
    if isinstance(completeness, dict) and bool(completeness.get("blocked")):
        return False, "summary_completeness_blocked"

    no_regression = summary.get("no_regression") or {}
    if isinstance(no_regression, dict):
        no_regression_available = no_regression.get("available")
        no_regression_failed = no_regression.get("failed")
        if (
            isinstance(no_regression_available, int)
            and no_regression_available > 0
            and isinstance(no_regression_failed, int)
            and no_regression_failed > 0
        ):
            return False, "summary_no_regression_failed"

    historical_validation = load_historical_validation(run_root)
    if historical_validation is not None:
        if bool(historical_validation.get("blocked")):
            return False, "historical_validation_blocked"
        historical_regressions = historical_validation.get("historical_best_regressions") or []
        if isinstance(historical_regressions, list) and historical_regressions:
            return False, "historical_validation_regressed"

    return True, "summary_admissible"


def classify_run_inclusion(run_root: Path, summary: dict) -> tuple[bool, str]:
    run_scope = load_run_scope(run_root)
    if run_scope is not None:
        if bool(run_scope.get("formal_admissible_candidate")):
            summary_admissible, summary_reason = classify_summary_admissibility(run_root, summary)
            if not summary_admissible:
                return False, summary_reason
            return True, "run_scope"
        return False, "run_scope_excluded"

    return False, "run_scope_missing"


def iter_run_roots(automation_root: Path):
    for child in sorted(automation_root.iterdir(), key=lambda item: item.name.lower()):
        if not child.is_dir():
            continue
        if not (child / "summary.json").is_file():
            continue
        yield child


def build_case_entry(case_name: str) -> dict:
    return {
        "run": "",
        "report_path": "",
        "stable_changed_ratio": None,
        "stable_mean_abs_error": None,
        "changed_ratio": None,
        "mean_abs_error": None,
        "structure_required": False,
        "structure_passed": True,
        "metric_sources": {},
        "source_runs": [],
        "updated_at": "",
        "updated_timestamp": 0.0,
        "case": case_name,
    }
def resolve_case_metrics(run_root: Path, case_name: str, case_summary: dict, cache_entries: dict) -> tuple[dict, Path]:
    report_path = resolve_case_report_path(run_root, case_name, case_summary)
    summary_metrics = normalize_case_metrics(summary_metrics_payload(case_summary))
    if summary_metrics:
        return summary_metrics, report_path

    report_metrics, _ = resolve_report_metrics(
        report_path,
        load_json_fn=load_json,
        extract_compare_metrics_fn=verify.extract_compare_metrics,
        compare_image_paths_fn=verify.compare_image_paths,
        default_pixel_tolerance=DEFAULT_PIXEL_TOLERANCE,
        file_signature_fn=file_signature,
        cache_entries=cache_entries,
        cache_lookup_fn=cache_lookup,
        cache_store_fn=cache_store,
    )
    if report_metrics:
        return report_metrics, report_path

    return {
        "stable_changed_ratio": None,
        "changed_ratio": None,
        "stable_mae": None,
        "mae": None,
    }, report_path


def update_metric(entry: dict, metric_name: str, value, run_root: Path, report_path: str, timestamp: float) -> None:
    if value is None:
        return
    current = entry.get(metric_name)
    if current is None or value < current - METRIC_EPSILON:
        entry[metric_name] = value
        entry["metric_sources"][metric_name] = {
            "run": run_root.name,
            "report_path": report_path,
            "value": value,
            "timestamp": timestamp,
        }


def collect_best_cases(automation_root: Path, cache_payload: dict) -> tuple[
    dict[str, dict],
    list[str],
    list[str],
    dict[str, int],
    dict[str, int],
    list[str],
    dict[str, int],
]:
    metric_cache = cache_payload.setdefault("entries", {})
    best_cases: dict[str, dict] = {}
    source_runs: list[str] = []
    skipped_runs: list[str] = []
    included_runs_by_reason: dict[str, int] = {"run_scope": 0}
    skipped_runs_by_reason: dict[str, int] = {
        "run_scope_missing": 0,
        "run_scope_excluded": 0,
        "summary_blocked_cases": 0,
        "summary_completeness_blocked": 0,
        "summary_no_regression_failed": 0,
        "historical_validation_blocked": 0,
        "historical_validation_regressed": 0,
    }
    skipped_case_results: list[str] = []
    skipped_case_results_by_reason: dict[str, int] = {
        "render_size_rule_incompatible": 0,
        "pipeline_determinism_rule_incompatible": 0,
    }

    for run_root in iter_run_roots(automation_root):
        summary_path = run_root / "summary.json"
        try:
            summary = load_json(summary_path)
        except Exception:
            continue

        cases = summary.get("cases") or []
        if not isinstance(cases, list) or not cases:
            continue

        include_run, inclusion_reason = classify_run_inclusion(run_root, summary)
        if not include_run:
            skipped_runs.append(run_root.name)
            skipped_runs_by_reason[inclusion_reason] = skipped_runs_by_reason.get(inclusion_reason, 0) + 1
            continue

        source_runs.append(run_root.name)
        included_runs_by_reason[inclusion_reason] = included_runs_by_reason.get(inclusion_reason, 0) + 1
        run_timestamp = summary_path.stat().st_mtime
        run_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(run_timestamp))
        for case in cases:
            case_name = case.get("name")
            if not case_name:
                continue

            report_path = resolve_case_report_path(run_root, case_name, case)
            compatibility = evaluate_report_compatibility(report_path)
            rule_status = compatibility["render_size"]
            if not rule_status.get("compatible"):
                skipped_case_results.append(f"{run_root.name}:{case_name}:render_size_rule_incompatible:{rule_status.get('reason', '')}")
                skipped_case_results_by_reason["render_size_rule_incompatible"] = skipped_case_results_by_reason.get("render_size_rule_incompatible", 0) + 1
                continue

            pipeline_rule_status = compatibility["pipeline_determinism"]
            if not pipeline_rule_status.get("compatible"):
                skipped_case_results.append(f"{run_root.name}:{case_name}:pipeline_determinism_rule_incompatible:{pipeline_rule_status.get('reason', '')}")
                skipped_case_results_by_reason["pipeline_determinism_rule_incompatible"] = skipped_case_results_by_reason.get("pipeline_determinism_rule_incompatible", 0) + 1
                continue

            resolved_metrics, report_path = resolve_case_metrics(run_root, case_name, case, metric_cache)
            entry = best_cases.setdefault(case_name, build_case_entry(case_name))
            entry["render_size_rule"] = render_rule.DESIGN_SIZE_RENDER_RULE
            entry["pipeline_determinism_rule"] = render_rule.PIPELINE_DETERMINISM_RULE

            for output_metric, _summary_metric in METRIC_FIELDS:
                update_metric(
                    entry,
                    output_metric,
                    resolved_metrics.get(output_metric),
                    run_root,
                    str(report_path),
                    run_timestamp,
                )

            entry["structure_required"] = bool(entry.get("structure_required")) or bool(case.get("structure_required"))
            if case.get("structure_required"):
                entry["structure_passed"] = bool(entry.get("structure_passed", True)) and bool(case.get("structure_passed"))
            if run_root.name not in entry["source_runs"]:
                entry["source_runs"].append(run_root.name)

            newest_metric_timestamp = max(
                (source.get("timestamp", 0.0) for source in entry["metric_sources"].values()),
                default=0.0,
            )
            if newest_metric_timestamp >= entry.get("updated_timestamp", 0.0):
                primary_source = (
                    entry["metric_sources"].get("stable_changed_ratio")
                    or entry["metric_sources"].get("changed_ratio")
                    or entry["metric_sources"].get("stable_mean_abs_error")
                    or entry["metric_sources"].get("mean_abs_error")
                )
                if primary_source:
                    entry["run"] = primary_source.get("run", "")
                    entry["report_path"] = primary_source.get("report_path", "")
                entry["updated_timestamp"] = newest_metric_timestamp
                entry["updated_at"] = run_iso

    for entry in best_cases.values():
        entry.pop("updated_timestamp", None)

    return (
        best_cases,
        sorted(source_runs),
        sorted(skipped_runs),
        included_runs_by_reason,
        skipped_runs_by_reason,
        skipped_case_results,
        skipped_case_results_by_reason,
    )


def rebuild_historical_best_manifest(automation_root: Path) -> dict:
    cache_path = automation_root / CACHE_FILENAME
    cache_payload = load_cache(cache_path)
    (
        best_cases,
        source_runs,
        skipped_runs,
        included_runs_by_reason,
        skipped_runs_by_reason,
        skipped_case_results,
        skipped_case_results_by_reason,
    ) = collect_best_cases(automation_root, cache_payload)
    manifest = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "automation_root": str(automation_root),
        "admissible_run_count": len(source_runs),
        "case_count": len(best_cases),
        "runs": source_runs,
        "skipped_runs": skipped_runs,
        "included_runs_by_reason": included_runs_by_reason,
        "skipped_runs_by_reason": skipped_runs_by_reason,
        "skipped_case_results": skipped_case_results,
        "skipped_case_results_by_reason": skipped_case_results_by_reason,
        "render_size_rule": render_rule.DESIGN_SIZE_RENDER_RULE,
        "pipeline_determinism_rule": render_rule.PIPELINE_DETERMINISM_RULE,
        "cases": best_cases,
    }
    cache_payload["generated_at"] = manifest["generated_at"]
    cache_payload["automation_root"] = str(automation_root)
    cache_payload["admissible_run_count"] = len(source_runs)
    cache_payload["case_count"] = len(best_cases)
    write_json(cache_path, cache_payload)
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Rebuild the tracked historical-best manifest from automation run summaries."
    )
    parser.add_argument(
        "--automation-root",
        default="",
        help="Automation root containing per-run subdirectories. Defaults to <plugin>/Saved/Design2UMG/Automation",
    )
    parser.add_argument(
        "--output-json",
        default="",
        help="Output JSON path. Defaults to <automation-root>/_admissible_best_latest.json",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    automation_root = (
        Path(args.automation_root).resolve()
        if args.automation_root
        else Path(__file__).resolve().parents[1] / "Saved" / "Design2UMG" / "Automation"
    )
    if not automation_root.is_dir():
        raise RuntimeError(f"Automation root not found: {automation_root}")

    manifest = rebuild_historical_best_manifest(automation_root)
    output_path = (
        Path(args.output_json).resolve()
        if args.output_json
        else automation_root / "_admissible_best_latest.json"
    )
    write_json(output_path, manifest)
    print(json.dumps({"output_json": str(output_path), "case_count": manifest["case_count"], "run_count": manifest["admissible_run_count"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
