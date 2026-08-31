"""Design2UMG IR golden layer (validation layer 1).

Compares the fidelity IR dump (ir_dump.json, %.9g / sorted-key / byte-stable)
produced by the editor-side Design2UMGAutomationIrDump against a frozen golden.

Usage:
  python design2umg_ir_golden.py <golden> <dump>
      <golden>/<dump> may each be a single ir_dump.json file or a run/case root
      directory. Directory mode compares <root>/<case>/ir_dump.json per case.
  Options:
      --max-diffs N     max printed diffs per case (default 50)
      --no-reports      do not write ir_golden_report.json into run case dirs
      --include REGEX   only compare case names matching this regex (dir mode)

Semantics:
  - missing golden file  -> case verdict "unavailable" (not a failure)
  - missing dump file    -> case verdict "unavailable" (asset-only renders have no IR)
  - directory mode enumerates golden ∪ run case dirs, so a case whose dump is
    missing still appears as unavailable instead of vanishing
  - any field difference -> case verdict failed (strict zero drift, no epsilon)
  - a run root with no dumps at all is a config error (cvar never enabled), not
    a silent pass; an unreadable single-file input is reported the same way.
Exit code: 0 = no regressions, 1 = at least one case failed, 2 = usage/config
error (bad paths, no dumps found under the run root, unreadable input files).
"""

import argparse
import json
import re
import sys
from pathlib import Path

REPORT_NAME = "ir_golden_report.json"
DUMP_NAME = "ir_dump.json"


def load_json_file(path: Path):
    with open(path, "r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def format_value(value) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        return f'"{value}"'
    return repr(value)


def types_compatible(left, right) -> bool:
    if isinstance(left, bool) != isinstance(right, bool):
        return False
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return True
    return type(left) is type(right)


def walk_diffs(golden, dump, path: str, out: list[str]) -> None:
    """Appends field-addressed diff lines such as 'RootWidget.Children[3].Brush.BrushColor.R: golden=0.108 dump=0.1098'."""
    if types_compatible(golden, dump) and not isinstance(golden, (dict, list)):
        if golden != dump:
            out.append(f"{path}: golden={format_value(golden)} dump={format_value(dump)}")
        return
    if isinstance(golden, dict) and isinstance(dump, dict):
        for key in sorted(set(golden) | set(dump)):
            child_path = f"{path}.{key}" if path else key
            if key not in golden:
                out.append(f"{child_path}: golden=<missing> dump={format_value(dump[key])}")
            elif key not in dump:
                out.append(f"{child_path}: golden={format_value(golden[key])} dump=<missing>")
            else:
                walk_diffs(golden[key], dump[key], child_path, out)
        return
    if isinstance(golden, list) and isinstance(dump, list):
        for index in range(max(len(golden), len(dump))):
            child_path = f"{path}[{index}]"
            if index >= len(golden):
                out.append(f"{child_path}: golden=<missing> dump={format_value(dump[index])}")
            elif index >= len(dump):
                out.append(f"{child_path}: golden={format_value(golden[index])} dump=<missing>")
            else:
                walk_diffs(golden[index], dump[index], child_path, out)
        return
    out.append(f"{path}: golden={format_value(golden)} dump={format_value(dump)}")


def compare_case(golden_root: Path | None, case_name: str, dump_path: Path) -> dict:
    """Evaluates one case. Never raises on missing files; missing -> unavailable."""
    result = {
        "case": case_name,
        "dump_path": str(dump_path),
        "available": False,
        "reason": "",
        "passed": None,
        "diff_count": 0,
        "diffs": [],
    }
    if not dump_path.is_file():
        result["reason"] = "dump_missing"
        return result
    golden_path = golden_root / case_name / DUMP_NAME if golden_root else None
    if golden_path is None or not golden_path.is_file():
        result["reason"] = "golden_missing"
        return result
    return compare_dump_files(golden_path, dump_path)


def compare_dump_files(golden_path: Path, dump_path: Path) -> dict:
    result = {
        "case": dump_path.parent.name,
        "dump_path": str(dump_path),
        "golden_path": str(golden_path),
        "available": False,
        "reason": "",
        "passed": None,
        "diff_count": 0,
        "diffs": [],
    }
    try:
        golden_doc = load_json_file(golden_path)
        dump_doc = load_json_file(dump_path)
    except (OSError, json.JSONDecodeError) as error:
        result["reason"] = f"read_error: {error}"
        return result
    diffs: list[str] = []
    walk_diffs(golden_doc, dump_doc, "", diffs)
    result["available"] = True
    result["passed"] = not diffs
    result["diff_count"] = len(diffs)
    result["diffs"] = diffs
    return result


def write_case_report(case_dir: Path, result: dict) -> Path:
    report_path = case_dir / REPORT_NAME
    case_dir.mkdir(parents=True, exist_ok=True)
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
    return report_path


def annotate_batch_case_result(case_result: dict, golden_root: str, case_name: str, case_dir: Path) -> None:
    """Batch integration hook: compare one case dump, write its report, and attach summary fields.

    Zero-op semantics for the batch when the IR golden layer is disabled; missing
    golden/dump mark the case unavailable (never a failure).
    """
    result = compare_case(Path(golden_root), case_name, case_dir / DUMP_NAME)
    write_case_report(case_dir, result)
    case_result["ir_golden_available"] = result["available"]
    case_result["ir_golden_passed"] = result["passed"] if result["available"] else None
    case_result["ir_golden_reason"] = result["reason"]
    case_result["ir_golden_diff_count"] = result["diff_count"]


def discover_dump_cases(golden_root: Path, run_root: Path, include_regex: str) -> list[tuple[str, Path]]:
    if (run_root / DUMP_NAME).is_file():
        return [(run_root.name, run_root / DUMP_NAME)]
    # Directory mode enumerates the union of golden and run case dirs: a run case
    # whose dump is missing shows up as unavailable instead of vanishing.
    case_names = sorted(
        {entry.name for entry in golden_root.iterdir() if entry.is_dir()}
        | {entry.name for entry in run_root.iterdir() if entry.is_dir()}
    )
    if include_regex:
        case_names = [name for name in case_names if re.search(include_regex, name)]
    return [(name, run_root / name / DUMP_NAME) for name in case_names]


def compare_run(golden_root: Path, run_root: Path, max_diffs: int, write_reports: bool, include_regex: str = "") -> int:
    cases = discover_dump_cases(golden_root, run_root, include_regex)
    has_run_dump = (run_root / DUMP_NAME).is_file() or any(
        (entry / DUMP_NAME).is_file() for entry in run_root.iterdir() if entry.is_dir()
    )
    if not has_run_dump or not cases:
        print(
            f"[IR-GOLDEN] config error: no {DUMP_NAME} found under {run_root}"
            " -- the IR golden layer produced nothing for the whole run"
            " (cvar design2umgAutomation.IrDump not enabled, or --include matched no case)",
            file=sys.stderr,
        )
        return 2
    failed = 0
    available = 0
    unavailable = 0
    for case_name, dump_path in cases:
        result = compare_case(golden_root, case_name, dump_path)
        if result["available"]:
            available += 1
            if result["passed"]:
                print(f"[IR-GOLDEN] PASS {case_name}")
            else:
                failed += 1
                print(f"[IR-GOLDEN] FAIL {case_name} ({result['diff_count']} field diffs)")
                for line in result["diffs"][:max_diffs]:
                    print(f"    {line}")
                if result["diff_count"] > max_diffs:
                    print(f"    ... {result['diff_count'] - max_diffs} more diffs suppressed")
        else:
            unavailable += 1
            print(f"[IR-GOLDEN] UNAVAILABLE {case_name} ({result['reason']})")
        if write_reports:
            report_path = write_case_report(dump_path.parent, result)
            print(f"    report={report_path}")
    print(
        f"[IR-GOLDEN] summary: cases={len(cases)} available={available} "
        f"passed={available - failed} failed={failed} unavailable={unavailable}"
    )
    return 1 if failed else 0


def compare_files(golden_file: Path, dump_file: Path, max_diffs: int) -> int:
    result = compare_dump_files(golden_file, dump_file)
    if result["available"]:
        for line in result["diffs"][:max_diffs]:
            print(line)
        if result["diff_count"] > max_diffs:
            print(f"... {result['diff_count'] - max_diffs} more diffs suppressed")
        print(f"[IR-GOLDEN] {result['diff_count']} field diffs")
        return 0 if result["passed"] else 1
    print(f"[IR-GOLDEN] config error: unreadable input ({result['reason']})", file=sys.stderr)
    return 2


def parse_args():
    parser = argparse.ArgumentParser(description="Design2UMG IR golden comparison (validation layer 1)")
    parser.add_argument("golden", help="Golden ir_dump.json file, or golden case root directory")
    parser.add_argument("dump", help="Current ir_dump.json file, or run root directory")
    parser.add_argument("--max-diffs", type=int, default=50, help="Max printed diffs per case (default 50)")
    parser.add_argument("--no-reports", action="store_true", help="Do not write ir_golden_report.json into run case dirs")
    parser.add_argument("--include", default="", help="Only compare case names matching this regex (directory mode)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    golden = Path(args.golden)
    dump = Path(args.dump)
    if not golden.exists():
        print(f"[IR-GOLDEN] golden path not found: {golden}", file=sys.stderr)
        return 2
    if not dump.exists():
        print(f"[IR-GOLDEN] dump path not found: {dump}", file=sys.stderr)
        return 2
    if golden.is_file() and dump.is_file():
        return compare_files(golden, dump, args.max_diffs)
    if golden.is_dir() and dump.is_dir():
        return compare_run(golden, dump, args.max_diffs, not args.no_reports, args.include)
    print("[IR-GOLDEN] golden and dump must both be files or both be directories", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
