import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

import design2umg_auto_verify_suite as suite_verify


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def assert_true(condition, message: str) -> None:
    assert condition, message


def assert_raises_runtime_error(func, expected_text: str) -> None:
    try:
        func()
    except RuntimeError as exc:
        assert_true(expected_text in str(exc), f"expected {expected_text!r} in {str(exc)!r}")
        return
    raise AssertionError(f"expected RuntimeError containing {expected_text!r}")


def main() -> None:
    args = SimpleNamespace(
        parallel_editors=3,
        idle_timeout_sec=180,
        startup_timeout_sec=60,
        editor_isolation="shard",
        post_workers=4,
        include="hero",
        exclude="",
        reference_cache="read-write",
        native_self_check="off",
        ir_golden="",
        geometry_golden="",
    )
    case_results = [
        {
            "name": "case-a",
            "blocked": False,
            "no_regression_available": True,
            "no_regression_passed": True,
            "tree_correspondence": {
                "available": True,
                "named_parent_path_ratio": 1.0,
                "sibling_order_ratio": 0.5,
                "kind_ratio": 1.0,
                "semantic_kind_ratio": 1.0,
                "kind_mismatch_classes": {"exact": 3},
            },
        },
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

    summary = suite_verify.build_suite_summary(
        args=args,
        editor_exit_code=0,
        editor_exit_codes=[0, 0],
        shards=[{"id": 0}, {"id": 1}],
        shard_results=[{"id": 0, "exit_code": 0, "cases": ["case-a"]},
                       {"id": 1, "exit_code": 0, "cases": ["case-b"]}],
        post_workers=2,
        design_root=Path("G:/Designs"),
        output_root=Path("G:/Runs/suite_run"),
        run_scope_path=Path("G:/Runs/suite_run/run_scope.json"),
        reference_cache_root=Path("G:/Runs/_reference_cache"),
        suite_start_time=10.0,
        prep_completed_at=12.0,
        editor_completed_at=22.0,
        postprocess_completed_at=25.5,
        case_results=case_results,
        completeness=completeness,
    )

    assert_equal(summary["run_scope"], "focus")
    assert_equal(summary["blocked_cases"], 1)
    assert_equal(summary["no_regression"], {"available": 1, "passed": 1, "failed": 0})
    assert_equal(summary["tree_correspondence"]["available_cases"], 1)
    assert_equal(summary["tree_correspondence"]["metrics"]["sibling_order_ratio"]["average"], 0.5)
    assert_equal(summary["tree_correspondence"]["metrics"]["semantic_kind_ratio"]["average"], 1.0)
    assert_equal(summary["tree_correspondence"]["kind_mismatch_classes"], {"exact": 3})
    assert_equal(summary["stage_durations_sec"]["prepare_cases"], 2.0)
    assert_equal(summary["stage_durations_sec"]["editor_execution"], 10.0)
    assert_equal(summary["stage_durations_sec"]["postprocess_reports"], 3.5)

    entry = suite_verify.build_suite_timing_history_entry(
        Path("G:/Runs/suite_run"),
        summary,
        recorded_at_utc="2026-05-18T12:34:56Z",
    )
    assert_equal(
        entry,
        {
            "recorded_at_utc": "2026-05-18T12:34:56Z",
            "run_root": str(Path("G:/Runs/suite_run")),
            "run_scope": "focus",
            "total_cases": 2,
            "blocked_cases": 1,
            "stage_durations_sec": summary["stage_durations_sec"],
            "no_regression": summary["no_regression"],
        },
    )

    with TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        history_path = suite_verify.append_suite_timing_history(automation_root, entry)
        assert_equal(history_path, automation_root / "suite_timing_history.jsonl")
        lines = history_path.read_text(encoding="utf-8").strip().splitlines()
        assert_equal(len(lines), 1)
        assert_equal(json.loads(lines[0]), entry)

        visual_case_dir = automation_root / "case-a"
        visual_case_dir.mkdir()
        for image_name in ("reference.png", "candidate.png", "diff_overlay.png"):
            (visual_case_dir / image_name).write_bytes(b"png")
        visual_review_path = suite_verify.write_visual_review_index(
            automation_root,
            [{
                "name": "case-a<&",
                "report_path": str(visual_case_dir / "compare_report.json"),
                "stable_changed_ratio": 0.125,
                "stable_mae": 0.03125,
                "blocked": False,
            }],
        )
        visual_review = visual_review_path.read_text(encoding="utf-8")
        assert_equal(visual_review_path, automation_root / "visual_review.html")
        assert_true("case-a&lt;&amp;" in visual_review, "case name should be HTML escaped")
        assert_true('src="case-a/reference.png"' in visual_review, "reference image should be linked")
        assert_true('src="case-a/candidate.png"' in visual_review, "candidate image should be linked")
        assert_true('src="case-a/diff_overlay.png"' in visual_review, "diff overlay should be linked")

        effective_root = suite_verify.resolve_effective_suite_game_output_root("", 1780838795.0)
        assert_true(
            effective_root.startswith("/Game/Design2UMG/Automation/run-"),
            f"unexpected default game-output root: {effective_root}",
        )
        case_args = suite_verify.build_case_args(
            SimpleNamespace(
                reference_root="",
                reference_provider="pencil",
                mcp_server="",
                pencil_app="",
                pixel_tolerance=6,
                no_regression_epsilon=1.25e-4,
                game_output="",
                effective_game_output_root=effective_root,
                native_self_check="off",
                ir_golden="",
                geometry_golden="",
            ),
            {"mode": "pen", "source": "design.pen", "name": "pen_sample_card"},
        )
        assert_equal(case_args.game_output, f"{effective_root}/pen-sample-card")
        assert_true(
            case_args.game_output != "/Game/Design2UMG/Automation/pen-sample-card",
            "omitted game-output fell back to the old stable per-case root",
        )

        cases = [{"name": "pen_sample_card"}, {"name": "figma_export_source"}]
        assert_raises_runtime_error(
            lambda: suite_verify.validate_explicit_suite_game_output_root(
                "/Game/Design2UMG/Automation/pen_sample_card",
                cases,
            ),
            "unique run-level automation root",
        )
        assert_raises_runtime_error(
            lambda: suite_verify.validate_explicit_suite_game_output_root(
                "/Game/Design2UMG/Automation/foo",
                cases,
            ),
            "unique run-level automation root",
        )
        assert_raises_runtime_error(
            lambda: suite_verify.validate_explicit_suite_game_output_root(
                "/Game/Design2UMG/Automation/foo1",
                cases,
            ),
            "unique run-level automation root",
        )
        assert_raises_runtime_error(
            lambda: suite_verify.validate_explicit_suite_game_output_root(
                "/Game/Design2UMG/Automation/AfterReloadUnique",
                cases,
            ),
            "unique run-level automation root",
        )
        allowed_root = "/Game/Design2UMG/Automation/run-20260607-132635-4242"
        suite_verify.validate_explicit_suite_game_output_root(allowed_root, cases)
        suite_verify.validate_explicit_suite_game_output_root(
            "/Game/Design2UMG/Automation/Run_20260607-132635",
            cases,
        )
        allowed_case_args = suite_verify.build_case_args(
            SimpleNamespace(
                reference_root="",
                reference_provider="pencil",
                mcp_server="",
                pencil_app="",
                pixel_tolerance=6,
                no_regression_epsilon=1.25e-4,
                game_output=allowed_root,
                effective_game_output_root=allowed_root,
                native_self_check="off",
                ir_golden="",
                geometry_golden="",
            ),
            {"mode": "figma-source", "source": "design.json", "name": "figma_export_source"},
        )
        assert_equal(allowed_case_args.game_output, f"{allowed_root}/figma-export-source")

        run_scope_path = automation_root / "run_scope.json"
        run_scope_path.write_text(json.dumps({"scope": "focus"}), encoding="utf-8")
        suite_verify.record_effective_game_output_root(
            run_scope_path,
            SimpleNamespace(
                game_output="",
                game_output_explicit=False,
                effective_game_output_root=effective_root,
            ),
        )
        run_scope = json.loads(run_scope_path.read_text(encoding="utf-8"))
        assert_equal(
            run_scope["game_output"],
            {"explicit": False, "effective_root": effective_root},
        )

        case_a_result = automation_root / "case-a-result.json"
        case_a_result.write_text("{}", encoding="utf-8")
        states = [
            {
                "case": {"name": "case-a"},
                "spec_paths": [automation_root / "case-a-generate.json", automation_root / "case-a-asset.json"],
                "steps": {
                    "generation": {
                        "spec_path": automation_root / "case-a-generate.json",
                        "result_json_path": case_a_result,
                    },
                    "asset_render": {
                        "spec_path": automation_root / "case-a-asset.json",
                        "result_json_path": automation_root / "case-a-asset-result.json",
                    },
                },
            },
            {
                "case": {"name": "case-b"},
                "spec_paths": [automation_root / "case-b-generate.json"],
                "steps": {
                    "generation": {
                        "spec_path": automation_root / "case-b-generate.json",
                        "result_json_path": case_a_result,
                    },
                },
            },
        ]
        case_summary_shards = suite_verify.build_isolation_summary_shards(states, "case")
        assert_equal(len(case_summary_shards), 1)
        assert_equal(case_summary_shards[0]["editor_isolation"], "case")
        assert_equal(len(case_summary_shards[0]["spec_paths"]), 3)
        missing_case_states = suite_verify.build_missing_case_states(states)
        assert_equal([state["case"]["name"] for state in missing_case_states], ["case-a"])

    with TemporaryDirectory() as tmp:
        root = Path(tmp)
        driver = root / "driver.py"
        result = root / "result.json"
        log = root / "editor.log"
        driver_log = suite_verify.get_editor_driver_log_path(driver.resolve())
        elapsed = [0.0]

        def sleep(seconds):
            elapsed[0] += seconds

        def exited_process(*args, **kwargs):
            return SimpleNamespace(poll=lambda: 3)

        with patch.object(suite_verify.subprocess, "Popen", side_effect=exited_process), \
                patch.object(suite_verify.time, "time", side_effect=lambda: elapsed[0]), \
                patch.object(suite_verify.time, "sleep", side_effect=sleep):
            try:
                suite_verify.run_editor_driver(
                    "EDITOR", "PROJECT", driver, [result], log, 1200, 1200)
            except suite_verify.EditorDriverStartError:
                pass
            else:
                raise AssertionError("Expected editor startup failure")
        assert_equal(elapsed[0], 0.0)

        def partial_process(*args, **kwargs):
            driver_log.write_text("driver:start\n", encoding="utf-8")
            return SimpleNamespace(poll=lambda: 3)

        with patch.object(suite_verify.subprocess, "Popen", side_effect=partial_process), \
                patch.object(suite_verify.time, "time", side_effect=lambda: elapsed[0]), \
                patch.object(suite_verify.time, "sleep", side_effect=sleep):
            assert_equal(suite_verify.run_editor_driver(
                "EDITOR", "PROJECT", driver, [result], log, 1200, 1200), 3)
        assert_equal(elapsed[0], float(suite_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC))
        elapsed[0] = 0.0

        for exit_code in [0, 3221225477]:
            def completed_process(*args, **kwargs):
                driver_log.write_text("driver:start\n", encoding="utf-8")
                result.write_text("{}", encoding="utf-8")
                return SimpleNamespace(poll=lambda: exit_code)

            with patch.object(suite_verify.subprocess, "Popen", side_effect=completed_process), \
                    patch.object(suite_verify.time, "time", side_effect=lambda: elapsed[0]), \
                    patch.object(suite_verify.time, "sleep", side_effect=sleep):
                assert_equal(suite_verify.run_editor_driver(
                    "EDITOR", "PROJECT", driver, [result], log, 1200, 1200), exit_code)
            assert_equal(elapsed[0], 0.0)

    print("suite report contract selftest passed")


if __name__ == "__main__":
    main()
