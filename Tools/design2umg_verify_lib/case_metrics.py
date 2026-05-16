from pathlib import Path
from typing import Callable


DEFAULT_PIXEL_TOLERANCE = 6


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


def summary_metrics_payload(case_summary: dict | None) -> dict:
    case_summary = case_summary or {}
    return {
        "stable_changed_ratio": case_summary.get("stable_changed_ratio"),
        "changed_ratio": case_summary.get("changed_ratio"),
        "stable_mae": case_summary.get("stable_mae"),
        "mae": case_summary.get("mae"),
    }


def resolve_case_report_path(
    root: Path,
    case_name: str,
    case_summary: dict | None,
    report_filename: str = "compare_report.json",
) -> Path:
    case_summary = case_summary or {}
    summary_report_path = case_summary.get("report_path", "")
    if summary_report_path:
        candidate = Path(summary_report_path)
        if candidate.is_file():
            return candidate.resolve()

    return (root / case_name / report_filename).resolve()


def _coerce_pixel_tolerance(report: dict, default_pixel_tolerance: int) -> int:
    pixel_tolerance = report.get("pixel_tolerance", default_pixel_tolerance)
    try:
        return int(pixel_tolerance)
    except (TypeError, ValueError):
        return default_pixel_tolerance


def resolve_report_metrics(
    report_path: Path,
    *,
    load_json_fn: Callable[[Path], dict],
    extract_compare_metrics_fn: Callable[[dict], dict | None],
    compare_image_paths_fn: Callable[[Path, Path, int], dict | None],
    default_pixel_tolerance: int = DEFAULT_PIXEL_TOLERANCE,
    file_signature_fn: Callable[[Path], dict | None] | None = None,
    cache_entries: dict | None = None,
    cache_lookup_fn: Callable[[dict, Path, dict | None, dict | None, dict | None, int], dict | None] | None = None,
    cache_store_fn: Callable[[dict, Path, dict | None, dict | None, dict | None, int, dict | None], None] | None = None,
) -> tuple[dict | None, dict]:
    report = load_json_fn(report_path) if report_path.is_file() else {}
    pixel_tolerance = _coerce_pixel_tolerance(report, default_pixel_tolerance)
    reference_png_path = Path(report.get("reference_png_path") or (report_path.parent / "reference.png"))
    candidate_png_path = Path(report.get("candidate_png_path") or (report_path.parent / "candidate.png"))

    report_signature = file_signature_fn(report_path) if file_signature_fn else None
    reference_signature = file_signature_fn(reference_png_path) if file_signature_fn else None
    candidate_signature = file_signature_fn(candidate_png_path) if file_signature_fn else None

    if cache_entries is not None and cache_lookup_fn is not None:
        cached_metrics = cache_lookup_fn(
            cache_entries,
            report_path,
            report_signature,
            reference_signature,
            candidate_signature,
            pixel_tolerance,
        )
        if cached_metrics:
            return cached_metrics, report

    report_metrics = normalize_case_metrics(extract_compare_metrics_fn(report))
    if report_metrics:
        if cache_entries is not None and cache_store_fn is not None:
            cache_store_fn(
                cache_entries,
                report_path,
                report_signature,
                reference_signature,
                candidate_signature,
                pixel_tolerance,
                report_metrics,
            )
        return report_metrics, report

    image_metrics = compare_image_paths_fn(reference_png_path, candidate_png_path, pixel_tolerance)
    normalized_image_metrics = normalize_case_metrics(
        {
            "stable_changed_ratio": image_metrics.get("stable_changed_ratio") if image_metrics else None,
            "changed_ratio": image_metrics.get("changed_ratio") if image_metrics else None,
            "stable_mae": image_metrics.get("stable_mean_abs_error") if image_metrics else None,
            "mae": image_metrics.get("mean_abs_error") if image_metrics else None,
        }
    )
    if normalized_image_metrics:
        if cache_entries is not None and cache_store_fn is not None:
            cache_store_fn(
                cache_entries,
                report_path,
                report_signature,
                reference_signature,
                candidate_signature,
                pixel_tolerance,
                normalized_image_metrics,
            )
        return normalized_image_metrics, report

    return None, report
