from pathlib import Path


def default_pinned_baseline_path(script_path: Path, pinned_baseline_filename: str) -> Path:
    return script_path.resolve().with_name(pinned_baseline_filename)


def resolve_manifest_report_path(report_path: str, manifest_path: Path) -> Path:
    candidate = Path(report_path)
    if candidate.is_absolute():
        return candidate

    manifest_dir = manifest_path.resolve().parent
    plugin_root = manifest_dir.parent
    for base in (plugin_root, manifest_dir):
        resolved = (base / candidate).resolve()
        if resolved.is_file():
            return resolved

    return (plugin_root / candidate).resolve()


def iter_manifest_case_report_paths(case: dict, manifest_path: Path) -> list[Path]:
    report_paths: list[Path] = []
    seen: set[str] = set()

    def add_report_path(raw_path: str) -> None:
        if not raw_path:
            return
        resolved = resolve_manifest_report_path(raw_path, manifest_path)
        normalized = str(resolved)
        if normalized not in seen:
            seen.add(normalized)
            report_paths.append(resolved)

    add_report_path(case.get("report_path", ""))
    metric_sources = case.get("metric_sources") or {}
    if isinstance(metric_sources, dict):
        for source in metric_sources.values():
            if isinstance(source, dict):
                add_report_path(source.get("report_path", ""))

    return report_paths
