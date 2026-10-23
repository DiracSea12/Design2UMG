import argparse
import hashlib
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
from design2umg_verify_lib.diagnostics import EvidenceReadError
from design2umg_verify_lib.diagnostics import emit_diagnostic
from design2umg_verify_lib.evidence_contract import collect_binary_binding
from design2umg_verify_lib.evidence_contract import collect_source_input
from design2umg_verify_lib.json_io import file_signature
from design2umg_verify_lib.json_io import load_json
from design2umg_verify_lib.json_io import write_json

METRIC_FIELDS = (
    ("stable_changed_ratio", "stable_changed_ratio"),
    ("changed_ratio", "changed_ratio"),
    ("stable_mean_abs_error", "stable_mae"),
    ("mean_abs_error", "mae"),
)
METRIC_EPSILON = 1e-9
DEFAULT_PIXEL_TOLERANCE = 6
CACHE_FILENAME = "_admissible_best_cache.json"
CACHE_VERSION = 6
EVIDENCE_EPOCH_FIELDS = (
    "engine_version",
    "rhi_backend",
    "source_design_artifact_hash",
    "reference_image_hash",
)


def evidence_epoch_identity(source_input: dict) -> dict:
    return {field: source_input.get(field) or "" for field in EVIDENCE_EPOCH_FIELDS}


def same_evidence_epoch(left: dict, right: dict) -> bool:
    return all(left.get(field) and left.get(field) == right.get(field) for field in EVIDENCE_EPOCH_FIELDS)


def resolve_admissible_best_manifest(
    automation_root: Path,
    *,
    expected_case_names: set[str],
    output_path: Path | None = None,
    cache_path: Path | None = None,
) -> tuple[Path, dict, str]:
    """Build the dynamic history view for the currently discovered case set."""
    expected_names = set(expected_case_names)
    if not expected_names:
        raise ValueError("Historical validation requires a nonempty discovered case set")
    if load_baseline_anchor(automation_root)[0] is None:
        raise ValueError(f"Historical validation requires an accepted baseline anchor: {automation_root}")

    output_path = output_path or (automation_root / "_admissible_best_latest.json")
    manifest = rebuild_historical_best_manifest(automation_root, cache_path=cache_path)
    historical_names = set(manifest.get("cases") or {})
    manifest["scope_case_count"] = len(expected_names)
    manifest["formal_scope_case_count"] = len(expected_names)
    manifest["scope_case_names"] = sorted(expected_names)
    manifest["scope_missing_cases"] = sorted(expected_names - historical_names)
    manifest["scope_extra_historical_cases"] = sorted(historical_names - expected_names)
    manifest["render_size_rule"] = render_rule.DESIGN_SIZE_RENDER_RULE
    manifest["pipeline_determinism_rule"] = render_rule.PIPELINE_DETERMINISM_RULE
    write_json(output_path, manifest)
    return output_path, manifest, "dynamic_history"


def baseline_identity_digest(root: Path) -> str:
    """Bind case identities and the runtime logs used to interpret them."""
    scope = load_json(root / "run_scope.json")
    names = scope.get("case_names")
    if not isinstance(names, list) or not names or any(
        not isinstance(name, str) or not name or name in (".", "..") or Path(name).name != name for name in names
    ) or len(set(names)) != len(names):
        raise ValueError(f"Invalid accepted baseline case scope: {root}")
    reports = {}
    logs = {}

    def digest(path: Path) -> str:
        result = hashlib.sha256()
        try:
            with path.open("rb") as file:
                for block in iter(lambda: file.read(1024 * 1024), b""):
                    result.update(block)
        except OSError as exc:
            raise ValueError(f"Accepted baseline identity evidence unreadable: {path}") from exc
        return result.hexdigest()

    for name in sorted(names):
        path = root / name / "compare_report.json"
        reports[name] = digest(path)
        report = load_json(path)
        chain = report.get("render_chain") or {}
        paths = [(chain.get(mode) or {}).get("log_path") for mode in ("generation", "asset_render")]
        paths = [Path(path).resolve() for path in paths if path]
        if not paths:
            raise ValueError(f"Accepted baseline runtime identity evidence missing: {path}")
        for log in paths:
            key = log.as_posix()
            if key not in logs:
                logs[key] = digest(log)
    payload = json.dumps({"case_reports": reports, "runtime_logs": logs}, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def load_baseline_anchor(automation_root: Path) -> tuple[Path | None, float]:
    """An explicit user acceptance starts a new history window, not a moving baseline."""
    pointer_path = automation_root / "_baseline.json"
    if not pointer_path.is_file():
        return None, 0.0
    pointer = load_json(pointer_path)
    name = pointer.get("run")
    if pointer.get("schema") != 1 or not isinstance(name, str) or not name or name in (".", "..") or Path(name).name != name:
        raise ValueError(f"Invalid baseline pointer: {pointer_path}")
    root = automation_root / name
    for filename, field in (("summary.json", "summary_sha256"), ("run_scope.json", "scope_sha256")):
        if hashlib.sha256((root / filename).read_bytes()).hexdigest() != pointer.get(field):
            raise ValueError(f"Accepted baseline evidence changed: {root / filename}")
    identity_digest = pointer.get("identity_evidence_sha256")
    if not isinstance(identity_digest, str) or len(identity_digest) != 64:
        raise ValueError(f"Accepted baseline identity binding missing: {pointer_path}")
    if baseline_identity_digest(root) != identity_digest:
        raise ValueError(f"Accepted baseline identity evidence changed: {root}")
    return root, float(pointer["accepted_at_unix"])


def load_cache(path: Path) -> dict:
    if not path.is_file():
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}}

    try:
        payload = load_json(path)
    except EvidenceReadError as exc:
        emit_diagnostic("warning", "admissible_best_cache_unreadable", exc.path, exc.detail)
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
    except EvidenceReadError as exc:
        emit_diagnostic("warning", "run_scope_unreadable", exc.path, exc.detail)
        return None
    return payload if isinstance(payload, dict) else None


def load_historical_validation(run_root: Path) -> dict | None:
    validation_path = run_root / "validation_against_historical_best.json"
    if not validation_path.is_file():
        return None

    try:
        payload = load_json(validation_path)
    except EvidenceReadError as exc:
        emit_diagnostic("warning", "historical_validation_unreadable", exc.path, exc.detail)
        return None
    return payload if isinstance(payload, dict) else None


def classify_summary_admissibility(run_root: Path, summary: dict, *, accepted_baseline: bool = False) -> tuple[bool, str]:
    blocked_cases = summary.get("blocked_cases")
    if isinstance(blocked_cases, int) and blocked_cases > 0:
        return False, "summary_blocked_cases"

    completeness = summary.get("completeness") or {}
    if isinstance(completeness, dict) and bool(completeness.get("blocked")):
        return False, "summary_completeness_blocked"

    editor_process = summary.get("editor_process") or {}
    if isinstance(editor_process, dict):
        failed = editor_process.get("failed")
        if isinstance(failed, int) and failed > 0:
            return False, "summary_editor_process_failed"

    total = summary.get("total")
    if accepted_baseline and isinstance(total, int) and total > 0:
        return True, "user_accepted_baseline"

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
    if (not isinstance(total, int) or total <= 0 or not isinstance(no_regression, dict)
            or no_regression.get("available") != total or no_regression.get("failed") != 0):
        return False, "summary_no_regression_unavailable"
    if historical_validation is None:
        return False, "historical_validation_unavailable"
    if historical_validation is not None:
        if bool(historical_validation.get("blocked")):
            return False, "historical_validation_blocked"
        historical_regressions = historical_validation.get("historical_best_regressions") or []
        if isinstance(historical_regressions, list) and historical_regressions:
            return False, "historical_validation_regressed"
        if historical_validation.get("historical_best_checked") is not True:
            return False, "historical_validation_unavailable"

    return True, "summary_admissible"


def classify_run_inclusion(run_root: Path, summary: dict, *, accepted_baseline: bool = False) -> tuple[bool, str]:
    run_scope = load_run_scope(run_root)
    if run_scope is not None:
        if bool(run_scope.get("formal_admissible_candidate")):
            summary_admissible, summary_reason = classify_summary_admissibility(run_root, summary, accepted_baseline=accepted_baseline)
            if not summary_admissible:
                return False, summary_reason
            return True, "run_scope"
        return False, "run_scope_excluded"

    return False, "run_scope_missing"


def iter_run_roots(automation_root: Path):
    def run_order(item: Path) -> tuple[float, str]:
        summary_path = item / "summary.json"
        return (summary_path.stat().st_mtime if summary_path.is_file() else 0.0, item.name.lower())

    for child in sorted(automation_root.iterdir(), key=run_order):
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
        "admitted_sources": [],
        "evidence_epoch_identity": {},
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


def select_case_source(candidate: dict, current: dict | None) -> bool:
    if current is None:
        return True

    candidate_key = (
        candidate.get("stable_changed_ratio") is None,
        candidate.get("stable_changed_ratio") if candidate.get("stable_changed_ratio") is not None else 0.0,
        candidate.get("changed_ratio") is None,
        candidate.get("changed_ratio") if candidate.get("changed_ratio") is not None else 0.0,
        -candidate.get("timestamp", 0.0),
        candidate.get("run", ""),
        candidate.get("report_path", ""),
    )
    current_key = (
        current.get("stable_changed_ratio") is None,
        current.get("stable_changed_ratio") if current.get("stable_changed_ratio") is not None else 0.0,
        current.get("changed_ratio") is None,
        current.get("changed_ratio") if current.get("changed_ratio") is not None else 0.0,
        -current.get("timestamp", 0.0),
        current.get("run", ""),
        current.get("report_path", ""),
    )
    return candidate_key < current_key


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
    epochs_by_case: dict[str, dict[tuple[str, str], dict]] = {}
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

    accepted_root, accepted_at = load_baseline_anchor(automation_root)
    for run_root in iter_run_roots(automation_root):
        summary_path = run_root / "summary.json"
        is_accepted_baseline = run_root == accepted_root
        if accepted_root is not None and not is_accepted_baseline and summary_path.stat().st_mtime <= accepted_at:
            continue
        try:
            summary = load_json(summary_path)
        except EvidenceReadError as exc:
            emit_diagnostic("warning", "summary_unreadable", exc.path, exc.detail)
            continue

        cases = summary.get("cases") or []
        if not isinstance(cases, list) or not cases:
            continue

        include_run, inclusion_reason = classify_run_inclusion(run_root, summary, accepted_baseline=is_accepted_baseline)
        if not include_run:
            skipped_runs.append(run_root.name)
            skipped_runs_by_reason[inclusion_reason] = skipped_runs_by_reason.get(inclusion_reason, 0) + 1
            continue

        run_scope = load_run_scope(run_root) or {}
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
            report = load_json(report_path) if report_path.is_file() else {}
            candidate_source = {
                "run": run_root.name,
                "report_path": str(report_path),
                "timestamp": run_timestamp,
                "stable_changed_ratio": resolved_metrics.get("stable_changed_ratio"),
                "changed_ratio": resolved_metrics.get("changed_ratio"),
                "stable_mae": resolved_metrics.get("stable_mae"),
                "mae": resolved_metrics.get("mae"),
                "structure_required": bool(case.get("structure_required")),
                "structure_passed": bool(case.get("structure_passed", True)),
                "render_size_rule": render_rule.DESIGN_SIZE_RENDER_RULE,
                "pipeline_determinism_rule": render_rule.PIPELINE_DETERMINISM_RULE,
                "binary_binding": collect_binary_binding(run_scope, summary, report),
                "source_input": collect_source_input(run_root, case_name, run_scope, summary, report, report_path),
            }

            candidate_epoch = evidence_epoch_identity(candidate_source["source_input"])
            # Keep each input's selected source while visiting interleaved runs.
            # The public view defaults to the latest epoch; admitted sources retain
            # the other epochs so a returning input is never silently reseeded.
            epoch_key = tuple(candidate_epoch[field] for field in EVIDENCE_EPOCH_FIELDS)
            epochs = epochs_by_case.setdefault(case_name, {})
            entry = epochs.get(epoch_key)
            if entry is None:
                entry = build_case_entry(case_name)
                entry["evidence_epoch_identity"] = candidate_epoch
                epochs[epoch_key] = entry
            best_cases[case_name] = entry
            entry["render_size_rule"] = render_rule.DESIGN_SIZE_RENDER_RULE
            entry["pipeline_determinism_rule"] = render_rule.PIPELINE_DETERMINISM_RULE
            entry["structure_required"] = bool(entry.get("structure_required")) or candidate_source["structure_required"]
            if candidate_source["structure_required"]:
                entry["structure_passed"] = bool(entry.get("structure_passed", True)) and candidate_source["structure_passed"]
            entry["admitted_sources"].append(
                {
                    "run": candidate_source["run"],
                    "report_path": candidate_source["report_path"],
                    "timestamp": candidate_source["timestamp"],
                    "stable_changed_ratio": candidate_source["stable_changed_ratio"],
                    "changed_ratio": candidate_source["changed_ratio"],
                    "stable_mean_abs_error": candidate_source["stable_mae"],
                    "mean_abs_error": candidate_source["mae"],
                    "binary_binding": candidate_source["binary_binding"],
                    "source_input": candidate_source["source_input"],
                }
            )

            if not select_case_source(candidate_source, entry):
                continue

            entry["run"] = candidate_source["run"]
            entry["report_path"] = candidate_source["report_path"]
            entry["updated_timestamp"] = candidate_source["timestamp"]
            entry["updated_at"] = run_iso
            entry["source_runs"] = sorted(set(entry["source_runs"] + [run_root.name]))
            entry["metric_sources"] = {
                metric_name: {
                    "run": candidate_source["run"],
                    "report_path": candidate_source["report_path"],
                    "value": candidate_source[summary_metric],
                    "timestamp": candidate_source["timestamp"],
                    "binary_binding": candidate_source["binary_binding"],
                    "source_input": candidate_source["source_input"],
                }
                for metric_name, summary_metric in METRIC_FIELDS
                if candidate_source.get(summary_metric) is not None
            }
            entry["stable_changed_ratio"] = candidate_source["stable_changed_ratio"]
            entry["changed_ratio"] = candidate_source["changed_ratio"]
            entry["stable_mean_abs_error"] = candidate_source["stable_mae"]
            entry["mean_abs_error"] = candidate_source["mae"]

    for case_name, entry in best_cases.items():
        entry.pop("updated_timestamp", None)
        entry["admitted_sources"] = sorted(
            [source for epoch in epochs_by_case[case_name].values()
             for source in epoch["admitted_sources"]],
            key=lambda source: (
                source.get("stable_changed_ratio") is None,
                source.get("stable_changed_ratio") if source.get("stable_changed_ratio") is not None else 0.0,
                source.get("changed_ratio") is None,
                source.get("changed_ratio") if source.get("changed_ratio") is not None else 0.0,
                source.get("run", ""),
                source.get("report_path", ""),
            ),
        )

    return (
        best_cases,
        sorted(source_runs),
        sorted(skipped_runs),
        included_runs_by_reason,
        skipped_runs_by_reason,
        skipped_case_results,
        skipped_case_results_by_reason,
    )


def rebuild_historical_best_manifest(automation_root: Path, *, cache_path: Path | None = None) -> dict:
    """Rebuild historical-best data using the existing admissible-run rules.

    Admissible runs have formal_admissible_candidate in run_scope.json and pass
    classify_summary_admissibility, including no blocked cases, no-regression
    failures or historical-best regressions.

    automation_root supplies the accepted baseline and per-run evidence. The
    scan cache is read and written at cache_path when supplied, otherwise at
    automation_root / CACHE_FILENAME. The caller writes the returned manifest
    to its selected destination.
    """
    cache_path = cache_path if cache_path is not None else automation_root / CACHE_FILENAME
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
        "baseline_anchor": load_json(automation_root / "_baseline.json") if (automation_root / "_baseline.json").is_file() else None,
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
    parser.add_argument("--design-root", required=True, help="Design corpus directory used for full case discovery.")
    parser.add_argument("--figma-export-root", action="append", default=[], help="Figma export corpus (repeatable).")
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

    from design2umg_verify_lib import suite_discovery

    design_root = Path(args.design_root).resolve()
    if not design_root.is_dir():
        raise RuntimeError(f"Design root not found: {design_root}")
    export_roots = suite_discovery.resolve_default_figma_export_roots(design_root, args.figma_export_root)
    discovered = suite_discovery.discover_pen_cases(design_root, "", "")
    discovered.extend(suite_discovery.discover_figma_export_cases(export_roots, "", ""))
    _, manifest, _ = resolve_admissible_best_manifest(
        automation_root, expected_case_names={case["name"] for case in discovered}
    )
    output_path = (
        Path(args.output_json).resolve()
        if args.output_json
        else automation_root / "_admissible_best_latest.json"
    )
    write_json(output_path, manifest)
    print(json.dumps({"output_json": str(output_path), "case_count": manifest["case_count"], "run_count": manifest["admissible_run_count"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
