import argparse
import json
import time
from pathlib import Path

import design2umg_auto_verify as verify
import design2umg_render_rule as render_rule


METRIC_FIELDS = (
    ("stable_changed_ratio", "stable_changed_ratio"),
    ("changed_ratio", "changed_ratio"),
    ("stable_mean_abs_error", "stable_mae"),
    ("mean_abs_error", "mae"),
)
METRIC_EPSILON = 1e-9
OUTPUT_COLLISION_WARNINGS = (
    "Output path already contained an asset with this name; auto-renamed",
    "输出路径已存在同名资产，自动重命名为",
)
DEFAULT_PIXEL_TOLERANCE = 6
CACHE_FILENAME = "_admissible_best_cache.json"
CACHE_VERSION = 3
PINNED_BASELINE_FILENAME = "design2umg_pinned_baseline.json"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8-sig")


def default_pinned_baseline_path() -> Path:
    return Path(__file__).resolve().with_name(PINNED_BASELINE_FILENAME)


def resolve_admissible_best_manifest(automation_root: Path) -> tuple[Path, dict, str]:
    output_path = automation_root / "_admissible_best_latest.json"
    pinned_path = default_pinned_baseline_path()
    if pinned_path.is_file():
        pinned_manifest = load_json(pinned_path)
        pinned_cases = pinned_manifest.get("cases") or {}
        skipped_by_reason: dict[str, int] = {}
        for case in pinned_cases.values():
            rule_status = render_rule.evaluate_report_render_size_rule(Path(case.get("report_path", "")))
            if not rule_status.get("compatible"):
                reason = rule_status.get("reason", "render_size_rule_incompatible")
                skipped_by_reason[reason] = skipped_by_reason.get(reason, 0) + 1
                continue

            pipeline_rule_status = render_rule.evaluate_report_pipeline_determinism_rule(Path(case.get("report_path", "")))
            if not pipeline_rule_status.get("compatible"):
                reason = pipeline_rule_status.get("reason", "pipeline_determinism_rule_incompatible")
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
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}, "runs": {}}

    try:
        payload = load_json(path)
    except Exception:
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}}

    if not isinstance(payload, dict):
        return {"version": CACHE_VERSION, "generated_at": "", "automation_root": "", "entries": {}, "runs": {}}

    entries = payload.get("entries")
    if not isinstance(entries, dict):
        entries = {}
    runs = payload.get("runs")
    if not isinstance(runs, dict):
        runs = {}

    payload["version"] = CACHE_VERSION
    payload["entries"] = entries
    payload["runs"] = runs
    payload.setdefault("generated_at", "")
    payload.setdefault("automation_root", "")
    return payload


def file_signature(path: Path) -> dict | None:
    try:
        stat = path.stat()
    except OSError:
        return None
    return {
        "path": str(path.resolve()),
        "mtime_ns": stat.st_mtime_ns,
        "size": stat.st_size,
    }


def normalize_case_metrics(metrics: dict | None) -> dict | None:
    if not isinstance(metrics, dict):
        return None

    stable_changed_ratio = metrics.get("stable_changed_ratio")
    changed_ratio = metrics.get("changed_ratio")
    stable_mae = metrics.get("stable_mae")
    if stable_mae is None:
        stable_mae = metrics.get("stable_mean_abs_error")
    mae = metrics.get("mae")
    if mae is None:
        mae = metrics.get("mean_abs_error")

    normalized = {
        "stable_changed_ratio": stable_changed_ratio,
        "changed_ratio": changed_ratio,
        "stable_mae": stable_mae,
        "mae": mae,
    }
    if all(value is None for value in normalized.values()):
        return None
    return normalized


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


def run_cache_lookup(runs_cache: dict, run_root: Path, summary_signature: dict | None, log_signature: dict | None) -> dict | None:
    if summary_signature is None:
        return None

    run_cache = runs_cache.get(run_root.name)
    if not isinstance(run_cache, dict):
        return None
    if run_cache.get("version") != CACHE_VERSION:
        return None
    if run_cache.get("summary_signature") != summary_signature:
        return None
    if run_cache.get("log_signature") != log_signature:
        return None

    collision_cases = run_cache.get("collision_cases")
    if not isinstance(collision_cases, dict):
        return None
    return run_cache


def run_cache_store(
    runs_cache: dict,
    run_root: Path,
    summary_signature: dict | None,
    log_signature: dict | None,
    collision_cases: dict[str, bool],
) -> None:
    if summary_signature is None:
        return

    runs_cache[run_root.name] = {
        "version": CACHE_VERSION,
        "summary_signature": summary_signature,
        "log_signature": log_signature,
        "collision_cases": collision_cases,
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


def infer_legacy_formal_candidate(run_root: Path, summary: dict) -> bool:
    run_name = run_root.name.lower()
    if run_name.startswith(("_probe_", "_focus_", "_codex_focus_")):
        return False

    if run_name.startswith(("_formal_", "_admissible_", "_full47_", "_final_")):
        return True

    cases = summary.get("cases") or []
    completeness = summary.get("completeness") or {}
    total = summary.get("total")
    discovered_total = completeness.get("discovered_total")
    reported_total = completeness.get("reported_total")
    if isinstance(total, int) and total >= 40:
        if isinstance(discovered_total, int) and discovered_total < 40:
            return False
        if isinstance(reported_total, int) and reported_total < 40:
            return False
        if isinstance(cases, list) and len(cases) >= 40:
            return True

    return False


def classify_run_inclusion(run_root: Path, summary: dict) -> tuple[bool, str]:
    run_scope = load_run_scope(run_root)
    if run_scope is not None:
        if bool(run_scope.get("formal_admissible_candidate")):
            return True, "run_scope"
        return False, "run_scope_excluded"

    if infer_legacy_formal_candidate(run_root, summary):
        return True, "legacy_inferred"
    return False, "legacy_excluded"


def detect_case_output_collisions(run_root: Path, cases: list[dict]) -> dict[str, bool]:
    log_path = run_root / "unreal_batch.log"
    if not log_path.is_file():
        return {}

    try:
        log_text = log_path.read_text(encoding="utf-8-sig", errors="ignore")
    except Exception:
        return {}

    case_markers: list[tuple[int, str]] = []
    for case in cases:
        case_name = case.get("name")
        if not case_name:
            continue

        positions = []
        for marker_name in (
            "automation_generate_spec.json",
            "automation_asset_spec.json",
            "automation_native_generate_spec.json",
            "automation_native_asset_spec.json",
        ):
            marker = f"{case_name}/{marker_name}"
            position = log_text.find(marker)
            if position >= 0:
                positions.append(position)

        if positions:
            case_markers.append((min(positions), case_name))

    case_markers.sort()
    collisions: dict[str, bool] = {}
    for index, (start, case_name) in enumerate(case_markers):
        end = case_markers[index + 1][0] if index + 1 < len(case_markers) else len(log_text)
        case_chunk = log_text[start:end]
        collisions[case_name] = any(warning in case_chunk for warning in OUTPUT_COLLISION_WARNINGS)

    return collisions


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


def resolve_report_path(run_root: Path, case_name: str, case_summary: dict) -> Path:
    summary_report_path = case_summary.get("report_path", "")
    if summary_report_path:
        candidate = Path(summary_report_path)
        if candidate.is_file():
            return candidate.resolve()

    return (run_root / case_name / "compare_report.json").resolve()


def resolve_case_metrics(run_root: Path, case_name: str, case_summary: dict, cache_entries: dict) -> tuple[dict, Path]:
    report_path = resolve_report_path(run_root, case_name, case_summary)
    summary_metrics = normalize_case_metrics(
        {
            "stable_changed_ratio": case_summary.get("stable_changed_ratio"),
            "changed_ratio": case_summary.get("changed_ratio"),
            "stable_mae": case_summary.get("stable_mae"),
            "mae": case_summary.get("mae"),
        }
    )
    if summary_metrics:
        return summary_metrics, report_path

    report = load_json(report_path) if report_path.is_file() else {}
    pixel_tolerance = report.get("pixel_tolerance", DEFAULT_PIXEL_TOLERANCE)
    try:
        pixel_tolerance = int(pixel_tolerance)
    except (TypeError, ValueError):
        pixel_tolerance = DEFAULT_PIXEL_TOLERANCE

    reference_png_path = Path(report.get("reference_png_path") or (report_path.parent / "reference.png"))
    candidate_png_path = Path(report.get("candidate_png_path") or (report_path.parent / "candidate.png"))
    report_signature = file_signature(report_path)
    reference_signature = file_signature(reference_png_path)
    candidate_signature = file_signature(candidate_png_path)
    cached_metrics = cache_lookup(
        cache_entries,
        report_path,
        report_signature,
        reference_signature,
        candidate_signature,
        pixel_tolerance,
    )
    if cached_metrics:
        return cached_metrics, report_path

    report_metrics = normalize_case_metrics(verify.extract_compare_metrics(report))
    if report_metrics:
        cache_store(
            cache_entries,
            report_path,
            report_signature,
            reference_signature,
            candidate_signature,
            pixel_tolerance,
            report_metrics,
        )
        return report_metrics, report_path

    image_metrics = verify.compare_image_paths(reference_png_path, candidate_png_path, pixel_tolerance)
    normalized_image_metrics = normalize_case_metrics(
        {
            "stable_changed_ratio": image_metrics.get("stable_changed_ratio") if image_metrics else None,
            "changed_ratio": image_metrics.get("changed_ratio") if image_metrics else None,
            "stable_mae": image_metrics.get("stable_mean_abs_error") if image_metrics else None,
            "mae": image_metrics.get("mean_abs_error") if image_metrics else None,
        }
    )
    if normalized_image_metrics:
        cache_store(
            cache_entries,
            report_path,
            report_signature,
            reference_signature,
            candidate_signature,
            pixel_tolerance,
            normalized_image_metrics,
        )
        return normalized_image_metrics, report_path

    return normalize_case_metrics({
        "stable_changed_ratio": case_summary.get("stable_changed_ratio"),
        "changed_ratio": case_summary.get("changed_ratio"),
        "stable_mae": case_summary.get("stable_mae"),
        "mae": case_summary.get("mae"),
    }) or {
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
    runs_cache = cache_payload.setdefault("runs", {})
    best_cases: dict[str, dict] = {}
    source_runs: list[str] = []
    skipped_runs: list[str] = []
    included_runs_by_reason: dict[str, int] = {"run_scope": 0, "legacy_inferred": 0}
    skipped_runs_by_reason: dict[str, int] = {"run_scope_excluded": 0, "legacy_excluded": 0}
    skipped_case_results: list[str] = []
    skipped_case_results_by_reason: dict[str, int] = {
        "output_collision": 0,
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
        summary_signature = file_signature(summary_path)
        log_path = run_root / "unreal_batch.log"
        log_signature = file_signature(log_path)
        run_cache = run_cache_lookup(runs_cache, run_root, summary_signature, log_signature)
        run_timestamp = summary_path.stat().st_mtime
        run_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(run_timestamp))
        if run_cache is not None:
            collision_cases = run_cache.get("collision_cases") or {}
        else:
            collision_cases = detect_case_output_collisions(run_root, cases)
            run_cache_store(runs_cache, run_root, summary_signature, log_signature, collision_cases)
        for case in cases:
            case_name = case.get("name")
            if not case_name:
                continue

            if collision_cases.get(case_name, False):
                skipped_case_results.append(f"{run_root.name}:{case_name}:output_collision")
                skipped_case_results_by_reason["output_collision"] = skipped_case_results_by_reason.get("output_collision", 0) + 1
                continue

            report_path = resolve_report_path(run_root, case_name, case)
            rule_status = render_rule.evaluate_report_render_size_rule(report_path)
            if not rule_status.get("compatible"):
                skipped_case_results.append(f"{run_root.name}:{case_name}:render_size_rule_incompatible:{rule_status.get('reason', '')}")
                skipped_case_results_by_reason["render_size_rule_incompatible"] = skipped_case_results_by_reason.get("render_size_rule_incompatible", 0) + 1
                continue

            pipeline_rule_status = render_rule.evaluate_report_pipeline_determinism_rule(report_path)
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
