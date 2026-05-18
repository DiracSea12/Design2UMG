import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace

import design2umg_auto_verify_batch as batch_verify


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def main() -> None:
    args = SimpleNamespace(
        parallel_editors=3,
        idle_timeout_sec=180,
        startup_timeout_sec=60,
        post_workers=4,
        include="hero",
        exclude="",
        reference_cache="read-write",
        native_self_check="off",
    )
    case_results = [
        {"name": "case-a", "blocked": False, "no_regression_available": True, "no_regression_passed": True},
        {"name": "case-b", "blocked": True, "no_regression_available": False, "no_regression_passed": True},
    ]
    completeness = {
        "blocked": False,
        "blocked_reasons": [],
        "discovered_total": 2,
        "reported_total": 2,
        "missing_compare_reports": [],
        "missing_case_results": [],
    }

    summary = batch_verify.build_batch_summary(
        args=args,
        editor_exit_code=0,
        editor_exit_codes=[0, 0],
        shards=[{"id": 0}, {"id": 1}],
        shard_results=[{"id": 0, "cases": ["case-a"]}, {"id": 1, "cases": ["case-b"]}],
        post_workers=2,
        design_root=Path("G:/Designs"),
        output_root=Path("G:/Runs/batch_run"),
        run_scope_path=Path("G:/Runs/batch_run/run_scope.json"),
        reference_cache_root=Path("G:/Runs/_reference_cache"),
        batch_start_time=10.0,
        prep_completed_at=12.0,
        editor_completed_at=22.0,
        postprocess_completed_at=25.5,
        case_results=case_results,
        completeness=completeness,
    )

    assert_equal(summary["run_scope"], "focus")
    assert_equal(summary["blocked_cases"], 1)
    assert_equal(summary["no_regression"], {"available": 1, "passed": 1, "failed": 0})
    assert_equal(summary["phase_durations_sec"]["prepare_cases"], 2.0)
    assert_equal(summary["phase_durations_sec"]["editor_execution"], 10.0)
    assert_equal(summary["phase_durations_sec"]["postprocess_reports"], 3.5)

    entry = batch_verify.build_batch_timing_history_entry(
        Path("G:/Runs/batch_run"),
        summary,
        recorded_at_utc="2026-05-18T12:34:56Z",
    )
    assert_equal(
        entry,
        {
            "recorded_at_utc": "2026-05-18T12:34:56Z",
            "run_root": "G:\\Runs\\batch_run",
            "run_scope": "focus",
            "total_cases": 2,
            "blocked_cases": 1,
            "phase_durations_sec": summary["phase_durations_sec"],
            "no_regression": summary["no_regression"],
        },
    )

    with TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        history_path = batch_verify.append_batch_timing_history(automation_root, entry)
        assert_equal(history_path, automation_root / "batch_timing_history.jsonl")
        lines = history_path.read_text(encoding="utf-8").strip().splitlines()
        assert_equal(len(lines), 1)
        assert_equal(json.loads(lines[0]), entry)

    print("batch report contract selftest passed")


if __name__ == "__main__":
    main()
