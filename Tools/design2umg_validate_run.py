import argparse
import json
import sys
from pathlib import Path


BLOCKED_EXIT_CODE = 2


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def collect_case_dirs(output_root: Path) -> set[str]:
    names: set[str] = set()
    for child in output_root.iterdir():
        if not child.is_dir():
            continue
        if child.name.startswith("_"):
            continue
        names.add(child.name)
    return names


def validate_run(output_root: Path) -> dict:
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

    expected_case_set = set(expected_case_names)
    case_dir_set = set(case_dir_names)
    summary_case_set = set(summary_case_names)

    if expected_case_names:
        for case_name in sorted(expected_case_set):
            compare_report_path = output_root / case_name / "compare_report.json"
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
            compare_report_path = output_root / case_name / "compare_report.json"
            if not compare_report_path.is_file():
                result["missing_compare_reports"].append(case_name)

    if result["missing_compare_reports"]:
        result["blocked_reasons"].append("missing_compare_report")
    if result["missing_case_summary_entries"]:
        result["blocked_reasons"].append("missing_case_summary_entry")

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
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_root = Path(args.output_root).resolve()
    if not output_root.is_dir():
        raise RuntimeError(f"Automation output root not found: {output_root}")

    result = validate_run(output_root)
    payload = json.dumps(result, ensure_ascii=False, indent=2)
    print(payload)

    if args.write_json:
        write_path = Path(args.write_json).resolve()
        write_path.parent.mkdir(parents=True, exist_ok=True)
        write_path.write_text(payload, encoding="utf-8-sig")

    sys.exit(BLOCKED_EXIT_CODE if result["blocked"] else 0)


if __name__ == "__main__":
    main()
