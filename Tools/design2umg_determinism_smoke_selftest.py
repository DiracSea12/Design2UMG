from __future__ import annotations

import tempfile
from pathlib import Path

import design2umg_determinism_smoke as smoke
from design2umg_verify_lib.json_io import write_json as helper_write_json


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def assert_contains(items, expected) -> None:
    assert expected in items, f"expected {expected!r} in {items!r}"


def write_json(path: Path, payload: dict) -> None:
    helper_write_json(path, payload)


def make_project(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('{"FileVersion":3}', encoding="utf-8")
    return path


def make_editor(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("stub", encoding="utf-8")
    return path


def make_report_payload() -> dict:
    return {
        "ue_result": {
            "pipeline_determinism_rule": "source_pipeline_transient_state_v4",
            "transient_state_authority": "ue_automation_render_reset",
            "transient_state_reset_before_render": True,
            "text_measurement_cache_scope": "per_automation_render",
            "text_single_line_height_probe_authority": "slate_font_measure_single_line_height_v1",
            "force_reload_package_requested": True,
            "asset_package_reload_authority": "ue_reload_package_before_asset_render_v1",
            "slate_font_cache_authority": "flush_before_automation_render_v1",
        },
        "render_chain": {
            "final_render_mode": "asset_reload",
            "generation_vs_asset": {
                "changed_ratio": 0.0,
                "stable_changed_ratio": 0.0,
                "mean_abs_error": 0.0,
                "stable_mean_abs_error": 0.0,
            },
            "generation_vs_asset_structure": {
                "available": True,
                "texts_match": True,
                "left_total_nodes": 10,
                "right_total_nodes": 10,
                "left_leaf_nodes": 5,
                "right_leaf_nodes": 5,
                "left_container_nodes": 5,
                "right_container_nodes": 5,
                "left_text_nodes": 2,
                "right_text_nodes": 2,
                "left_visual_nodes": 3,
                "right_visual_nodes": 3,
                "kind_histograms_match": True,
                "class_histograms_match": True,
            },
        },
    }


def build_run_root(root: Path, *, mutate_report=None, missing_case: str = "", missing_report: str = "", stale_summary_report: str = "") -> Path:
    run_scope = {
        "case_names": smoke.canonical_sentinel_cases(),
    }
    summary_cases = []
    for case_name in smoke.canonical_sentinel_cases():
        case_dir = root / case_name
        case_dir.mkdir(parents=True, exist_ok=True)
        report_path = case_dir / "compare_report.json"
        if case_name != missing_report:
            payload = make_report_payload()
            if mutate_report is not None:
                payload = mutate_report(case_name, payload)
            write_json(report_path, payload)

        if case_name != missing_case:
            summary_report_path = report_path
            if stale_summary_report == case_name:
                stale_path = root.parent / f"{case_name}-stale-report.json"
                write_json(stale_path, make_report_payload())
                summary_report_path = stale_path
            summary_cases.append(
                {
                    "name": case_name,
                    "report_path": str(summary_report_path.resolve()),
                }
            )

    write_json(root / "run_scope.json", run_scope)
    write_json(root / "summary.json", {"cases": summary_cases})
    return root


def test_canonical_owner_and_regex() -> None:
    assert_equal(
        smoke.canonical_sentinel_cases(),
        [
            "pen_pencil-new_gamehud-1920x1080",
            "pen_pencil-welcome_step-2-frame",
        ],
    )
    assert_equal(
        smoke.canonical_sentinel_regex(),
        r"^(pen_pencil\-new_gamehud\-1920x1080|pen_pencil\-welcome_step\-2\-frame)$",
    )


def test_missing_explicit_inputs_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        project_path = make_project(root / "FPSRoguelike.uproject")
        editor_path = make_editor(root / "UnrealEditor.exe")
        reference_root = root / "reference-root"
        reference_root.mkdir()

        try:
            smoke.validate_explicit_smoke_inputs("", str(editor_path), str(reference_root))
        except RuntimeError as exc:
            assert "--project" in str(exc)
        else:
            raise AssertionError("expected missing --project to fail")

        try:
            smoke.validate_explicit_smoke_inputs(str(project_path), "", str(reference_root))
        except RuntimeError as exc:
            assert "--editor" in str(exc)
        else:
            raise AssertionError("expected missing --editor to fail")

        try:
            smoke.validate_explicit_smoke_inputs(str(project_path), str(editor_path), "")
        except RuntimeError as exc:
            assert "--reference-root" in str(exc)
        else:
            raise AssertionError("expected missing --reference-root to fail")


def test_missing_run_root_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        missing_root = Path(temp_dir) / "missing"
        try:
            smoke.validate_run_root(missing_root)
        except RuntimeError as exc:
            assert "run root does not exist" in str(exc)
        else:
            raise AssertionError("expected missing run root to fail")


def test_validate_run_root_passes_for_canonical_payload() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run")
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], True)
        assert_equal(summary["blocked_reasons"], [])
        assert_equal(len(summary["case_results"]), 2)


def test_missing_case_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", missing_case=smoke.CANONICAL_SENTINEL_CASES[0])
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "summary_case_mismatch")
        assert_contains(summary["blocked_reasons"], "missing_summary_case")


def test_missing_compare_report_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", missing_report=smoke.CANONICAL_SENTINEL_CASES[1])
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "missing_compare_report")


def test_missing_generation_vs_asset_fails_closed() -> None:
    def mutate_report(case_name: str, payload: dict) -> dict:
        if case_name == smoke.CANONICAL_SENTINEL_CASES[0]:
            payload["render_chain"].pop("generation_vs_asset")
        return payload

    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", mutate_report=mutate_report)
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "generation_vs_asset_missing")


def test_missing_generation_vs_asset_structure_fails_closed() -> None:
    def mutate_report(case_name: str, payload: dict) -> dict:
        if case_name == smoke.CANONICAL_SENTINEL_CASES[0]:
            payload["render_chain"].pop("generation_vs_asset_structure")
        return payload

    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", mutate_report=mutate_report)
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "generation_vs_asset_structure_missing")


def test_structure_mismatch_fails_closed() -> None:
    def mutate_report(case_name: str, payload: dict) -> dict:
        if case_name == smoke.CANONICAL_SENTINEL_CASES[1]:
            payload["render_chain"]["generation_vs_asset_structure"]["right_total_nodes"] = 999
        return payload

    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", mutate_report=mutate_report)
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "generation_vs_asset_structure_mismatch")


def test_nonzero_drift_fails_closed() -> None:
    def mutate_report(case_name: str, payload: dict) -> dict:
        if case_name == smoke.CANONICAL_SENTINEL_CASES[1]:
            payload["render_chain"]["generation_vs_asset"]["changed_ratio"] = 0.25
        return payload

    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", mutate_report=mutate_report)
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "generation_vs_asset_nonzero")


def test_nonzero_stable_or_error_drift_fails_closed() -> None:
    def mutate_report(case_name: str, payload: dict) -> dict:
        if case_name == smoke.CANONICAL_SENTINEL_CASES[0]:
            payload["render_chain"]["generation_vs_asset"].update(
                {
                    "stable_changed_ratio": 0.0001,
                    "mean_abs_error": 0.0001,
                    "stable_mean_abs_error": 0.0001,
                }
            )
        return payload

    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", mutate_report=mutate_report)
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "generation_vs_asset_nonzero")


def test_stale_summary_report_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = build_run_root(Path(temp_dir) / "run", stale_summary_report=smoke.CANONICAL_SENTINEL_CASES[0])
        summary = smoke.validate_run_root(run_root)
        assert_equal(summary["passed"], False)
        assert_contains(summary["blocked_reasons"], "stale_compare_report")


def main() -> None:
    test_canonical_owner_and_regex()
    test_missing_explicit_inputs_fail_closed()
    test_missing_run_root_fails_closed()
    test_validate_run_root_passes_for_canonical_payload()
    test_missing_case_fails_closed()
    test_missing_compare_report_fails_closed()
    test_missing_generation_vs_asset_fails_closed()
    test_missing_generation_vs_asset_structure_fails_closed()
    test_structure_mismatch_fails_closed()
    test_nonzero_drift_fails_closed()
    test_nonzero_stable_or_error_drift_fails_closed()
    test_stale_summary_report_fails_closed()
    print("determinism smoke selftest passed")


if __name__ == "__main__":
    main()
