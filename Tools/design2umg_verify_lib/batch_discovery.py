import re
import time
from pathlib import Path

import design2umg_auto_verify as verify


ROOT_MIN_SIZE = 8
ROOT_CONTAINER_TYPES = {"frame", "group", "component", "ref"}
DEFAULT_FIGMA_EXPORTS_CONTAINER_NAME = "FigmaExports"


def default_output_root() -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return verify.plugin_root() / "Saved" / "Design2UMG" / "Automation" / f"batch_{stamp}_{Path.cwd().name}"


def expand_figma_export_root(root: Path) -> list[str]:
    """One --figma-export-root may be an export dir itself (has design.json)
    or a container whose direct subdirectories are export dirs (e.g.
    FigmaExports/). Nested helper folders (no design.json, like
    _diagnostics/) are ignored by the direct-children scan."""
    if (root / "design.json").is_file():
        return [str(root)]
    return [
        str(child.resolve())
        for child in sorted(root.iterdir())
        if child.is_dir() and (child / "design.json").is_file()
    ]


def resolve_default_figma_export_roots(design_root: Path, explicit_roots: list[str]) -> list[str]:
    resolved: list[str] = []
    roots: list[Path] = [Path(root) for root in explicit_roots if root]
    if not roots:
        for ancestor in design_root.parents:
            candidate = ancestor / DEFAULT_FIGMA_EXPORTS_CONTAINER_NAME
            if candidate.is_dir():
                roots = [candidate]
                break
    for root in roots:
        resolved.extend(expand_figma_export_root(root.resolve()))
    return resolved


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


def write_run_scope(output_root: Path, args, cases: list[dict], effective_figma_export_roots: list[str]) -> Path:
    run_scope = {
        "scope": classify_run_scope(args.include, args.exclude),
        "include_pattern": args.include,
        "exclude_pattern": args.exclude,
        "discovered_total": len(cases),
        "formal_admissible_candidate": not bool(args.include),
        "figma_export_roots": effective_figma_export_roots,
        "case_names": [case["name"] for case in cases],
        "case_inputs": [
            {
                "name": case["name"],
                "mode": case["mode"],
                "source": str(Path(case["source"]).resolve()),
                "source_signature": verify.build_file_signature(case["source"]),
                "provided_reference_signature": verify.build_file_signature(case.get("reference_path")),
            }
            for case in cases
        ],
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

        for node in nodes:
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
