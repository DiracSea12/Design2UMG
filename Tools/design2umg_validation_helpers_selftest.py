import argparse
import json
import os
import runpy
import subprocess
import sys
import tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify
import design2umg_auto_verify_suite as suite_verify
import design2umg_geometry_compare as geometry_compare
import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_scoring as scoring
import design2umg_single_session_isolation_test as single_session_isolation
import design2umg_validate_run as validate_run
from design2umg_verify_lib import suite_discovery
from design2umg_verify_lib import evidence_contract
from design2umg_verify_lib import execution_shards
from design2umg_verify_lib import editor_driver_writer


PNG_1X1 = (
    b"\x89PNG\r\n\x1a\n"
    b"\x00\x00\x00\rIHDR"
    b"\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"
    b"\x00\x00\x00\rIDATx\x9cc`\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4"
    b"\x00\x00\x00\x00IEND\xaeB`\x82"
)


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def make_rgba_png(path: Path, rgba: tuple[int, int, int, int]) -> None:
    from PIL import Image

    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", (1, 1), rgba).save(path)


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def make_pen_fixture(design_root: Path) -> Path:
    pen_path = design_root / "Sample Screen.pen"
    write_text(
        pen_path,
        """
{
  "children": [
    {"id": "root-a", "type": "frame", "name": "Main Hero", "width": 320, "height": 180},
    {"id": "root-b", "type": "frame", "name": "Main Hero", "width": 320, "height": 180},
    {"id": "tiny", "type": "frame", "name": "Tiny", "width": 4, "height": 4},
    {"id": "leaf", "type": "rectangle", "name": "Leaf", "width": 120, "height": 40},
    {
      "id": "inferred",
      "type": "group",
      "name": "Inferred Root",
      "children": [
        {"id": "child", "type": "text", "content": "Hello", "fontSize": 16}
      ]
    }
  ]
}
""".strip(),
    )
    return pen_path


def make_figma_export_fixture(workspace_root: Path) -> list[Path]:
    container = workspace_root / "FigmaExports"
    roots = []
    for name, large_png_size in (("figma_export", 2), ("figma_export2", 3)):
        export_root = container / name
        write_text(export_root / "design.json", '{"document":{"children":[]}}')
        write_bytes(export_root / "small.png", PNG_1X1)
        write_bytes(export_root / "large.png", PNG_1X1 * large_png_size)
        roots.append(export_root)
    # subdirectory without design.json must be ignored by the container scan
    write_text(container / "_diagnostics" / "notes.txt", "not a case")
    return roots


def test_dynamic_corpus_cardinality_and_scope() -> None:
    for count in (0, 1, 7, 100, 1000):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "cases"
            root.mkdir()
            # An unrelated corpus outside the selected root must never leak in.
            make_figma_export_fixture(Path(directory))
            for index in range(count):
                write_text(root / f"group-{index % 3}" / f"case-{index}.pen", json.dumps({
                    "children": [{"id": "root", "type": "frame", "width": 100, "height": 100}]}))
            command = [sys.executable, str(Path(suite_verify.__file__)), "--design-root", str(root),
                       "--output-root", str(Path(directory) / "output"), "--list-cases"]
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            scope = json.loads(result.stdout)
            assert_equal(scope["discovered_total"], count)
            assert_equal(len(set(scope["case_names"])), count)
            assert_equal(scope["figma_export_roots"], [])
            if count == 0:
                result = subprocess.run(command[:-1], capture_output=True, text=True)
                assert result.returncode == 0, result.stderr
                summary = verify.helper_load_json(Path(directory) / "output/summary.json")
                assert_equal(summary["status"], "empty")
                assert_equal(summary["total"], 0)
                assert_equal(summary["executed"], 0)
                assert summary["passed"] is None


def test_nested_export_discovery_and_unique_names() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for folder in ("a/screen", "b/screen", "c/screen-2"):
            write_text(root / folder / "design.json", '{"document":{"children":[]}}')
            write_bytes(root / folder / "reference.png", PNG_1X1)
        roots = suite_discovery.resolve_default_figma_export_roots(root, [])
        assert_equal(len(roots), 3)
        assert_equal(suite_discovery.resolve_default_figma_export_roots(root, [str(root), roots[0]]), roots)
        cases = suite_discovery.discover_figma_export_cases(roots, "", "")
        assert_equal(len(cases), 6)
        assert_equal(len({case["name"] for case in cases}), 6)
        write_text(root / "incomplete/design.json", '{}')
        # Partly collected data is not a runnable case; diagnostics explain the skipped export.
        assert_equal(len(suite_discovery.discover_figma_export_cases(
            suite_discovery.resolve_default_figma_export_roots(root, []), "", "")), 6)


def test_discovery_and_run_scope_contracts() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        workspace_root = Path(temp_dir)
        design_root = workspace_root / "Designs" / "Nested"
        pen_path = make_pen_fixture(design_root)
        figma_roots = make_figma_export_fixture(design_root)

        assert_equal(
            suite_discovery.sanitize_case_name("  Hello, UE 5.7!  "),
            "hello-ue-5-7",
        )
        assert_equal(suite_discovery.classify_run_scope("", ""), "full")
        assert_equal(suite_discovery.classify_run_scope("hero", ""), "focus")
        assert_equal(suite_discovery.classify_run_scope("", "skip"), "full_with_exclusions")

        default_roots = suite_discovery.resolve_default_figma_export_roots(design_root, [])
        assert_equal(default_roots, [str(root.resolve()) for root in figma_roots])
        explicit_roots = suite_discovery.resolve_default_figma_export_roots(design_root, [str(figma_roots[1]), ""])
        assert_equal(explicit_roots, [str(figma_roots[1].resolve())])
        container_roots = suite_discovery.resolve_default_figma_export_roots(
            design_root, [str(figma_roots[0].parent)])
        assert_equal(container_roots, [str(root.resolve()) for root in figma_roots])

        pen_cases = suite_discovery.discover_pen_cases(design_root, "", "")
        assert_equal(
            [case["name"] for case in pen_cases],
            [
                "pen_sample-screen_main-hero",
                "pen_sample-screen_main-hero-2",
                "pen_sample-screen_inferred-root",
            ],
        )
        assert_equal(suite_discovery.discover_pen_cases(design_root, "inferred", ""), [pen_cases[2]])
        assert_equal(suite_discovery.discover_pen_cases(design_root, "", "-2$"), [pen_cases[0], pen_cases[2]])

        figma_cases = suite_discovery.discover_figma_export_cases(default_roots, "", "")
        assert_equal(
            [case["name"] for case in figma_cases],
            [
                "figma_figma-export_local",
                "figma_figma-export_source",
                "figma_figma-export2_local",
                "figma_figma-export2_source",
            ],
        )
        assert_equal({Path(case["reference_path"]).name for case in figma_cases}, {"large.png"})

        output_root = workspace_root / "Run"
        args = SimpleNamespace(include="figma", exclude="")
        all_cases = [pen_cases[0], figma_cases[0]]
        run_scope_path = suite_discovery.write_run_scope(output_root, args, all_cases, default_roots)
        run_scope = verify.helper_load_json(run_scope_path)
        assert_equal(run_scope["scope"], "focus")
        assert_equal(run_scope["formal_admissible_candidate"], False)
        assert_equal(run_scope["discovered_total"], 2)
        assert_equal(run_scope["figma_export_roots"], default_roots)
        assert_equal(run_scope["case_names"], [case["name"] for case in all_cases])
        assert_equal(run_scope["case_inputs"][0]["source"], str(pen_path.resolve()))
        assert run_scope["case_inputs"][0]["source_signature"]["size"] > 0
        assert run_scope["case_inputs"][1]["provided_reference_signature"]["size"] > 0


def test_completeness_contract() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state_a_dir = root / "case-a"
        state_b_dir = root / "case-b"
        write_text(state_a_dir / "compare_report.json", "{}")
        state_b_dir.mkdir()

        states = [
            {"case": {"name": "case-a"}, "output_dir": state_a_dir},
            {"case": {"name": "case-b"}, "output_dir": state_b_dir},
        ]
        completeness = suite_discovery.collect_suite_completeness(
            states,
            [{"name": "case-a", "report_path": str(state_a_dir / "compare_report.json")}],
        )
        assert_equal(completeness["blocked"], True)
        assert_equal(completeness["blocked_reasons"], ["missing_compare_report", "missing_case_summary_entry"])
        assert_equal(completeness["discovered_total"], 2)
        assert_equal(completeness["reported_total"], 1)
        assert_equal(completeness["missing_compare_reports"], ["case-b"])
        assert_equal(completeness["missing_case_results"], ["case-b"])


def test_transparent_rgb_does_not_count_as_visible_change() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        left = root / "left.png"
        right = root / "right.png"
        make_rgba_png(left, (12, 34, 56, 0))
        make_rgba_png(right, (200, 210, 220, 0))

        comparison = verify.build_diff(verify.load_rgba(left), verify.load_rgba(right), 0)
        assert_equal(comparison["changed_ratio"], 0.0)
        assert_equal(comparison["stable_changed_ratio"], 0.0)
        assert_equal(comparison["mean_abs_error"], 0.0)
        assert_equal(comparison["stable_mean_abs_error"], 0.0)
        assert_equal(comparison["diff_bbox"], None)
        assert_equal(comparison["stable_diff_bbox"], None)


def test_opaque_rgb_difference_still_counts() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        left = root / "left.png"
        right = root / "right.png"
        make_rgba_png(left, (12, 34, 56, 255))
        make_rgba_png(right, (200, 210, 220, 255))

        comparison = verify.build_diff(verify.load_rgba(left), verify.load_rgba(right), 0)
        assert comparison["changed_ratio"] > 0.0
        assert comparison["mean_abs_error"] > 0.0


def test_semi_transparent_difference_still_counts() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        left = root / "left.png"
        right = root / "right.png"
        make_rgba_png(left, (12, 34, 56, 64))
        make_rgba_png(right, (200, 210, 220, 64))

        comparison = verify.build_diff(verify.load_rgba(left), verify.load_rgba(right), 0)
        assert comparison["changed_ratio"] > 0.0
        assert comparison["mean_abs_error"] > 0.0


def test_evaluate_no_regression_checks_every_available_metric() -> None:
    original_loader = scoring.load_baseline_case_metrics

    def evaluate(baseline: dict, report: dict) -> dict:
        scoring.load_baseline_case_metrics = lambda _case_dir, _pixel_tolerance: dict(
            baseline, engine_version="5.7", rhi_backend="D3D12", rhi_feature_level="SM6")
        with tempfile.TemporaryDirectory() as temp_dir:
            log = Path(temp_dir) / "runtime.log"
            log.write_text("LogRHI: RHI D3D12 with Feature Level SM6 is supported and will be used.\n",
                           encoding="utf-8")
            return scoring.evaluate_no_regression(dict(
                report, engine_version="5.7", render_chain={"generation": {"log_path": str(log)}}), None, 6)

    complete_baseline = {
        "available": True,
        "stable_changed_ratio": 0.4,
        "changed_ratio": 0.3,
        "stable_mean_abs_error": 4.0,
        "mean_abs_error": 3.0,
    }
    try:
        changed_ratio_regression = evaluate(
            complete_baseline,
            {
                "stable_changed_ratio": 0.2,
                "changed_ratio": 0.301,
                "stable_mean_abs_error": 3.0,
                "mean_abs_error": 2.0,
            },
        )
        assert not changed_ratio_regression["passed"]
        assert changed_ratio_regression["visual_regressed"]
        assert_equal(changed_ratio_regression["decisive_metric"], "changed_ratio")
        assert abs(changed_ratio_regression["decisive_delta"] - 0.001) < 1e-12

        mae_regression = evaluate(
            complete_baseline,
            {
                "stable_changed_ratio": 0.2,
                "changed_ratio": 0.2,
                "stable_mean_abs_error": 3.0,
                "mean_abs_error": 3.001,
            },
        )
        assert not mae_regression["passed"]
        assert mae_regression["visual_regressed"]
        assert_equal(mae_regression["decisive_metric"], "mean_abs_error")
        assert abs(mae_regression["decisive_delta"] - 0.001) < 1e-12

        within_tolerance = evaluate(
            complete_baseline,
            {
                "stable_changed_ratio": 0.4 + scoring.NO_REGRESSION_EPSILON,
                "changed_ratio": 0.3,
                "stable_mean_abs_error": 4.0,
                "mean_abs_error": 3.0,
                "structure_validation": {"required": True, "passed": True},
            },
        )
        assert within_tolerance["passed"]
        assert not within_tolerance["visual_regressed"]
        assert_equal(within_tolerance["decisive_metric"], "stable_changed_ratio")
        assert abs(within_tolerance["decisive_delta"] - scoring.NO_REGRESSION_EPSILON) < 1e-12

        incomplete_baseline = evaluate(
            {
                "available": True,
                "stable_changed_ratio": None,
                "changed_ratio": 0.3,
                "stable_mean_abs_error": None,
                "mean_abs_error": None,
            },
            complete_baseline,
        )
        assert not incomplete_baseline["available"]
        assert not incomplete_baseline["passed"]
        assert_equal(incomplete_baseline["reasons"], ["baseline_metrics_incomplete"])

        incomplete_current = evaluate(
            complete_baseline,
            {
                "stable_changed_ratio": 0.2,
                "changed_ratio": 0.2,
                "stable_mean_abs_error": None,
                "mean_abs_error": 2.0,
            },
        )
        assert not incomplete_current["available"]
        assert not incomplete_current["passed"]
        assert_equal(incomplete_current["reasons"], ["current_metrics_incomplete"])

        unavailable_current = evaluate(complete_baseline, {})
        assert not unavailable_current["available"]
        assert not unavailable_current["passed"]
        assert_equal(unavailable_current["reasons"], ["current_metrics_unavailable"])

        structure_regression = evaluate(
            complete_baseline,
            {
                **complete_baseline,
                "structure_validation": {"required": True, "passed": False},
            },
        )
        assert not structure_regression["passed"]
        assert not structure_regression["visual_regressed"]
        assert not structure_regression["structure_guard_passed"]
        assert_equal(structure_regression["reasons"], ["structure_guard_failed"])

    finally:
        scoring.load_baseline_case_metrics = original_loader


def test_load_baseline_case_metrics_rejects_incomplete_metrics() -> None:
    original_render_rule = scoring.render_rule.evaluate_report_render_size_rule
    try:
        scoring.render_rule.evaluate_report_render_size_rule = lambda _report_path: {"compatible": True}
        with tempfile.TemporaryDirectory() as temp_dir:
            case_dir = Path(temp_dir)
            verify.write_json(
                case_dir / "compare_report.json",
                {
                    "changed_ratio": 0.3,
                    "stable_changed_ratio": None,
                    "mean_abs_error": 3.0,
                    "stable_mean_abs_error": None,
                },
            )

            baseline = scoring.load_baseline_case_metrics(case_dir, 6)

        assert not baseline["available"]
        assert_equal(baseline["reason"], "baseline_metrics_incomplete")
    finally:
        scoring.render_rule.evaluate_report_render_size_rule = original_render_rule


def test_suite_summary_fails_required_unavailable_no_regression() -> None:
    args = SimpleNamespace(
        parallel_editors=1,
        idle_timeout_sec=180,
        startup_timeout_sec=60,
        editor_isolation="shard",
        post_workers=1,
        include="",
        exclude="",
        reference_cache="read-write",
        native_self_check="off",
        ir_golden="",
        geometry_golden="",
    )
    case_results = [
        {
            "name": "baseline-incomplete",
            "blocked": False,
            "no_regression_required": True,
            "no_regression_available": False,
            "no_regression_passed": False,
            "no_regression_reasons": ["baseline_metrics_incomplete"],
        },
        {
            "name": "current-incomplete",
            "blocked": False,
            "no_regression_required": True,
            "no_regression_available": False,
            "no_regression_passed": False,
            "no_regression_reasons": ["current_metrics_incomplete"],
        },
    ]
    summary = suite_verify.build_suite_summary(
        args=args,
        editor_exit_code=0,
        editor_exit_codes=[0],
        shards=[{"shard_id": 0}],
        shard_results=[{"shard_id": 0, "exit_code": 0}],
        post_workers=1,
        design_root=Path("G:/Designs"),
        output_root=Path("G:/Runs/suite_run"),
        run_scope_path=Path("G:/Runs/suite_run/run_scope.json"),
        reference_cache_root=None,
        suite_start_time=10.0,
        prep_completed_at=11.0,
        editor_completed_at=12.0,
        postprocess_completed_at=13.0,
        case_results=case_results,
        completeness={"blocked": False},
    )

    assert_equal(summary["no_regression"], {"available": 0, "passed": 0, "failed": 2})


def test_no_regression_epsilon_rejects_non_finite_and_negative() -> None:
    # epsilon=nan made `delta > nan` always False, so high-delta regressions passed vacuously.
    for bad_epsilon in (float("nan"), float("inf"), float("-inf"), -1.0):
        try:
            scoring.evaluate_no_regression({}, None, 6, epsilon=bad_epsilon)
        except ValueError as exc:
            assert "finite" in str(exc)
        else:
            raise AssertionError(f"expected ValueError for epsilon={bad_epsilon!r}")
    for raw in ("nan", "inf", "-inf", "-1"):
        try:
            suite_verify.parse_no_regression_epsilon(raw)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"expected argparse rejection for epsilon {raw!r}")
    assert suite_verify.parse_no_regression_epsilon("1e-6") == 1e-6


def run_tool_cli(script_name: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(Path(__file__).parent / script_name), *[str(arg) for arg in args]],
        capture_output=True,
        text=True,
        cwd=str(Path(__file__).parent),
    )


GEOMETRY_IR_ROOT_AB = (
    # Real canvas slots: root Bounds 200x100; fixed (non-stretch) anchors with
    # Offsets in position+size form, so canvas_policy derives A at (10, 10)
    # sized 100x50 and B at (80, 10) sized 50x20 -- matching the runtime dumps.
    '{"RootWidget": {"Name": "Root", "Type": "CanvasPanel", "Bounds": {"W": 200.0, "H": 100.0}, "Children": ['
    '{"Name": "A", "Type": "TextBlock", "Slot": {"Anchors": {"Min": {"X": 0.0, "Y": 0.0}, "Max": {"X": 0.0, "Y": 0.0}},'
    ' "Offsets": {"Left": 10.0, "Top": 10.0, "Right": 100.0, "Bottom": 50.0}, "Alignment": {"X": 0.0, "Y": 0.0}}},'
    ' {"Name": "B", "Type": "TextBlock", "Slot": {"Anchors": {"Min": {"X": 0.0, "Y": 0.0}, "Max": {"X": 0.0, "Y": 0.0}},'
    ' "Offsets": {"Left": 80.0, "Top": 10.0, "Right": 50.0, "Bottom": 20.0}, "Alignment": {"X": 0.0, "Y": 0.0}}}]}}'
)


def make_wrapper_migration_dumps(b_abs_x: float) -> tuple[str, str]:
    """Current runtime inserts a SizeBox wrapper above B at (b_abs_x, 10); the
    frozen golden has B directly under Root at the absolute spot (80, 10)."""
    current = json.dumps({"nodes": [
        {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
        {"path": "0/0", "name": "A", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 100.0, "h": 50.0, "geometry_valid": True},
        {"path": "0/1", "name": "wrapper", "class": "SizeBox", "abs_x": b_abs_x, "abs_y": 10.0, "w": 50.0, "h": 20.0, "geometry_valid": True},
        {"path": "0/1/0", "name": "B", "class": "TextBlock", "abs_x": b_abs_x, "abs_y": 10.0, "w": 50.0, "h": 20.0, "geometry_valid": True}]})
    golden = json.dumps({"nodes": [
        {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
        {"path": "0/0", "name": "A", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 100.0, "h": 50.0, "geometry_valid": True},
        {"path": "0/1", "name": "B", "class": "TextBlock", "abs_x": 80.0, "abs_y": 10.0, "w": 50.0, "h": 20.0, "geometry_valid": True}]})
    return current, golden


def test_geometry_wrapper_migration_does_not_fail() -> None:
    # Cross-version wrapper migration: the current runtime inserts a SizeBox
    # the frozen golden never had, but B's absolute geometry is unchanged.
    # Stability anchors on the nearest paired ancestor (Root); fidelity
    # anchors on the IR parent's runtime pair (Root as well), so the IR slot
    # expectation (80, 10) meets the runtime in the same frame and the
    # wrapper insertion must not fail either record.
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        case_dir = root / "case-a"
        write_text(case_dir / "ir_dump.json", GEOMETRY_IR_ROOT_AB)
        current, golden = make_wrapper_migration_dumps(80.0)
        write_text(case_dir / "geometry_dump.json", current)
        golden_file = root / "golden_dump.json"
        write_text(golden_file, golden)

        proc = run_tool_cli("design2umg_geometry_compare.py", golden_file, case_dir)

    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "PASS" in proc.stdout
    assert "unmatched IR node" not in proc.stdout
    assert "FAIL" not in proc.stdout
    stability_lines = [line for line in proc.stdout.splitlines() if "stability:" in line]
    assert stability_lines and "failed=0" in stability_lines[0], proc.stdout
    fidelity_lines = [line for line in proc.stdout.splitlines() if "fidelity:" in line]
    assert fidelity_lines and "failed=0" in fidelity_lines[0], proc.stdout
    assert "Root/B: expected" not in proc.stdout, proc.stdout


def test_geometry_wrapper_insertion_with_real_move_fails() -> None:
    # Same wrapper insertion, but B's absolute x really moved (80 -> 130,
    # delta 50 >> 0.75 tolerance): the anchored comparison must still gate it.
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        case_dir = root / "case-a"
        write_text(case_dir / "ir_dump.json", GEOMETRY_IR_ROOT_AB)
        current, golden = make_wrapper_migration_dumps(130.0)
        write_text(case_dir / "geometry_dump.json", current)
        golden_file = root / "golden_dump.json"
        write_text(golden_file, golden)

        proc = run_tool_cli("design2umg_geometry_compare.py", golden_file, case_dir)

    assert proc.returncode == 1, proc.stdout + proc.stderr
    assert "Root/B: expected x=80 runtime x=130 delta=50" in proc.stdout


def test_geometry_icon_font_typed_projection() -> None:
    # IconFont source names may be replaced by the resolved glyph name in the
    # runtime tree. Geometry must pass the source type and runtime text kind to
    # the shared correspondence helper so the real counterpart remains paired.
    ir_doc = {
        "RootWidget": {
            "Name": "Root",
            "Type": "CanvasPanel",
            "Bounds": {"W": 200.0, "H": 100.0},
            "Children": [{
                "Name": "IconFont_Widget",
                "Type": "IconFont",
                "Slot": {
                    "Anchors": {"Min": {"X": 0.0, "Y": 0.0}, "Max": {"X": 0.0, "Y": 0.0}},
                    "Offsets": {"Left": 10.0, "Top": 10.0, "Right": 20.0, "Bottom": 20.0},
                    "Alignment": {"X": 0.0, "Y": 0.0},
                },
            }],
        },
    }
    geo_doc = {"nodes": [
        {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
        {"path": "0/0", "name": "uJzgY", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 20.0, "h": 20.0, "geometry_valid": True},
    ]}
    report = geometry_compare.evaluate_case(ir_doc, geo_doc, geo_doc)
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        case_dir = root / "case-a"
        write_text(case_dir / "ir_dump.json", json.dumps(ir_doc))
        write_text(case_dir / "geometry_dump.json", json.dumps(geo_doc))
        golden_file = root / "golden_dump.json"
        write_text(golden_file, json.dumps(geo_doc))

        proc = run_tool_cli("design2umg_geometry_compare.py", golden_file, case_dir)

    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert report["alignment"]["matched_nodes"] == 2, report
    assert report["alignment"]["ir_unmatched_count"] == 0, report
    assert report["alignment"]["geo_unmatched_count"] == 0, report
    assert "Root/IconFont_Widget: unmatched IR node" not in proc.stdout, proc.stdout
    fidelity_lines = [line for line in proc.stdout.splitlines() if "fidelity:" in line]
    assert fidelity_lines and "failed=0" in fidelity_lines[0], proc.stdout
    stability_lines = [line for line in proc.stdout.splitlines() if "stability:" in line]
    assert stability_lines and "failed=0" in stability_lines[0], proc.stdout


def test_geometry_disappeared_node_fails() -> None:
    # B vanished from the runtime dump while the frozen golden still has it:
    # fidelity keeps the unmatched IR node visible, stability gates the absent
    # golden node.
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        case_dir = root / "case-a"
        write_text(case_dir / "ir_dump.json", GEOMETRY_IR_ROOT_AB)
        write_text(case_dir / "geometry_dump.json", json.dumps({"nodes": [
            {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
            {"path": "0/0", "name": "A", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 100.0, "h": 50.0, "geometry_valid": True}]}))
        golden_file = root / "golden_dump.json"
        write_text(golden_file, json.dumps({"nodes": [
            {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
            {"path": "0/0", "name": "A", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 100.0, "h": 50.0, "geometry_valid": True},
            {"path": "0/1", "name": "B", "class": "TextBlock", "abs_x": 10.0, "abs_y": 60.0, "w": 100.0, "h": 30.0, "geometry_valid": True}]}))

        proc = run_tool_cli("design2umg_geometry_compare.py", golden_file, case_dir)

    assert proc.returncode == 1, proc.stdout + proc.stderr
    assert "FAIL" in proc.stdout
    assert "Root/B: unmatched IR node (no runtime counterpart)" in proc.stdout
    assert "B: golden node absent from current dump" in proc.stdout


def test_geometry_ir_unmatched_without_golden_drift_passes() -> None:
    # B was already absent when the golden froze: the unmatched IR node stays
    # visible in fidelity but must not gate stability (no golden drift).
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        case_dir = root / "case-a"
        write_text(case_dir / "ir_dump.json", GEOMETRY_IR_ROOT_AB)
        geo_doc = json.dumps({"nodes": [
            {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True},
            {"path": "0/0", "name": "A", "class": "TextBlock", "abs_x": 10.0, "abs_y": 10.0, "w": 100.0, "h": 50.0, "geometry_valid": True}]})
        write_text(case_dir / "geometry_dump.json", geo_doc)
        golden_file = root / "golden_dump.json"
        write_text(golden_file, geo_doc)

        proc = run_tool_cli("design2umg_geometry_compare.py", golden_file, case_dir)

    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "PASS" in proc.stdout
    assert "Root/B: unmatched IR node (no runtime counterpart)" in proc.stdout
    stability_lines = [line for line in proc.stdout.splitlines() if "stability:" in line]
    assert stability_lines and "failed=0" in stability_lines[0], proc.stdout


def test_directory_mode_enumerates_golden_run_union() -> None:
    # Golden has case-a + case-b but the run produced a dump only for case-a:
    # case-b must appear as unavailable (dump_missing), not vanish from summary.
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        ir_golden, ir_run = root / "ir-golden", root / "ir-run"
        for name in ("case-a", "case-b"):
            write_text(ir_golden / name / "ir_dump.json", '{"RootWidget": {"Name": "Root"}}')
        write_text(ir_run / "case-a" / "ir_dump.json", '{"RootWidget": {"Name": "Root"}}')
        geo_doc = json.dumps({"nodes": [
            {"path": "0", "name": "Root", "class": "CanvasPanel", "abs_x": 0.0, "abs_y": 0.0, "w": 200.0, "h": 100.0, "geometry_valid": True}]})
        geo_golden, geo_run = root / "geo-golden", root / "geo-run"
        for name in ("case-a", "case-b"):
            write_text(geo_golden / name / "geometry_dump.json", geo_doc)
        write_text(geo_run / "case-a" / "geometry_dump.json", geo_doc)
        write_text(geo_run / "case-a" / "ir_dump.json", '{"RootWidget": {"Name": "Root", "Type": "CanvasPanel", "Children": []}}')

        procs = (
            run_tool_cli("design2umg_ir_golden.py", ir_golden, ir_run, "--no-reports"),
            run_tool_cli("design2umg_geometry_compare.py", geo_golden, geo_run, "--no-reports"),
        )

    for proc in procs:
        assert proc.returncode == 0, proc.stdout + proc.stderr
        assert "cases=2" in proc.stdout
        assert "unavailable=1" in proc.stdout
        assert "UNAVAILABLE case-b" in proc.stdout
        assert "dump_missing" in proc.stdout


def make_state(name: str, spec_count: int) -> dict:
    spec_paths = [Path(f"{name}-spec-{index}.json") for index in range(spec_count)]
    steps = {
        f"step-{index}": {"result_json_path": Path(f"{name}-result-{index}.json")}
        for index in range(spec_count)
    }
    return {
        "case": {"name": name},
        "spec_paths": spec_paths,
        "steps": steps,
    }


def test_execution_shards_contracts() -> None:
    original_cpu_count = execution_shards.os.cpu_count
    try:
        execution_shards.os.cpu_count = lambda: 16
        assert_equal(execution_shards.resolve_parallel_editor_count(0, 1), 1)
        assert_equal(execution_shards.resolve_parallel_editor_count(0, 7), 1)
        assert_equal(execution_shards.resolve_parallel_editor_count(0, 8), 2)
        assert_equal(execution_shards.resolve_parallel_editor_count(3, 8), 3)
        assert_equal(execution_shards.resolve_post_worker_count(0, 1), 1)
        assert_equal(execution_shards.resolve_post_worker_count(0, 10), 8)
        assert_equal(execution_shards.resolve_post_worker_count(3, 10), 3)
    finally:
        execution_shards.os.cpu_count = original_cpu_count

    states = [
        make_state("heavy", 4),
        make_state("medium", 2),
        make_state("light", 1),
    ]
    shards = execution_shards.build_execution_shards(states, 2)
    assert_equal([shard["id"] for shard in shards], [0, 1])
    assert_equal([state["case"]["name"] for state in shards[0]["states"]], ["heavy"])
    assert_equal([state["case"]["name"] for state in shards[1]["states"]], ["medium", "light"])
    assert_equal(len(shards[0]["spec_paths"]), 4)
    assert_equal(len(shards[1]["spec_paths"]), 3)
    assert_equal(len(shards[0]["result_paths"]), 4)
    assert_equal(len(shards[1]["result_paths"]), 3)

    single_shard = execution_shards.build_execution_shards(states, 99)
    assert_equal(len(single_shard), 3)
    assert_equal(sum(len(shard["states"]) for shard in single_shard), len(states))


def test_evidence_contract_shared_by_manifest_and_run_validation() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        report_path = root / "case-a" / "compare_report.json"
        run_scope = {
            "validation_project_path": "G:/SampleProject/SampleProject.uproject",
            "case_names": ["case-a"],
            "include_pattern": "case-a",
            "case_inputs": [
                {
                    "name": "case-a",
                    "mode": "figma-source",
                    "source": "G:/UE5/Designs/source.json",
                    "source_signature": {"sha1": "source-hash"},
                }
            ],
        }
        summary = {}
        report = {
            "effective_reference_signature": {"sha256": "reference-hash"},
            "engine_version": "5.7",
            "rhi_backend": "D3D12",
            "baseline_compare_report_hash": "baseline-hash",
            "reference_root_case_path": "G:/SampleProject/Saved/ReferenceRoot/case-a",
        }
        log = root / "runtime.log"
        log.write_text("LogRHI: RHI D3D12 with Feature Level SM6 is supported and will be used.\n", encoding="utf-8")
        report["render_chain"] = {"generation": {"log_path": str(log)}}

        manifest_source = rebuild_best.collect_source_input(root, "case-a", run_scope, summary, report, report_path)
        current_source = validate_run.current_source_input("case-a", root, run_scope, summary, report, report_path)

        assert_equal(manifest_source, current_source)
        inputs_match, mismatches = validate_run.same_source_input(manifest_source, current_source)
        assert inputs_match, mismatches

        changed_source = dict(current_source)
        changed_source["source_design_artifact_hash"] = "other-source"
        inputs_match, mismatches = validate_run.same_source_input(manifest_source, changed_source)
        assert not inputs_match
        assert_equal(mismatches[0]["field"], "source_design_artifact_hash")

        assert rebuild_best.collect_source_input is evidence_contract.collect_source_input
        assert validate_run.same_source_input is evidence_contract.same_source_input


def test_binary_binding_contract_shared_by_manifest_production() -> None:
    report = {
        "binary_binding": {
            "plugin_source_snapshot": "commit:test",
            "design2umg_core_dll": {
                "path": "G:/SampleProject/Binaries/Win64/UnrealEditor-Design2UMGCore.dll",
                "sha256": "c" * 64,
                "size": 123,
                "mtime": "2026-06-08T00:00:00Z",
            },
            "design2umg_editor_dll": {
                "path": "G:/SampleProject/Binaries/Win64/UnrealEditor-Design2UMGEditor.dll",
                "sha256": "e" * 64,
                "size": 456,
                "mtime": "2026-06-08T00:00:00Z",
            },
            "validation_project_path": "G:/SampleProject/SampleProject.uproject",
            "engine_root": "E:/MockEngineRoot",
        }
    }

    binding = rebuild_best.collect_binary_binding({}, {}, report)

    assert rebuild_best.collect_binary_binding is evidence_contract.collect_binary_binding
    assert binding["binary_bound"], binding
    assert_equal(binding["missing_fields"], [])
    assert_equal(binding["design2umg_core_dll"]["sha256"], "c" * 64)


def make_step_isolation_state(root: Path, *, include_generation: bool = True) -> dict:
    output_dir = root / "case-a"
    output_dir.mkdir(parents=True, exist_ok=True)
    generate_result_path = output_dir / "ue_generate_result.json"
    asset_result_path = output_dir / "ue_render_result.json"
    generation_spec_path = output_dir / "automation_generate_spec.json"
    asset_spec_path = output_dir / "automation_asset_spec.json"
    verify.write_json(
        generation_spec_path,
        {
            "mode": "pen",
            "source_path": str(root / "source.pen"),
            "node_id": "root",
            "output_path": "/Game/Design2UMG/Automation/Test/primary",
            "render_png_path": str(output_dir / "candidate.bootstrap.png"),
            "result_json_path": str(generate_result_path),
        },
    )
    verify.write_json(
        asset_spec_path,
        {
            "mode": "asset",
            "source_path": f"@generated_asset_from:{generate_result_path}",
            "node_id": "",
            "output_path": "/Game/Design2UMG/Automation/Test/primary",
            "render_png_path": str(output_dir / "candidate.png"),
            "result_json_path": str(asset_result_path),
            "force_reload_package": True,
        },
    )
    steps = {}
    spec_paths = []
    if include_generation:
        spec_paths.append(generation_spec_path)
        steps["generation"] = {
            "spec_path": generation_spec_path,
            "result_json_path": generate_result_path,
            "render_png_path": output_dir / "candidate.bootstrap.png",
            "log_path": output_dir / "unreal_suite.log",
            "editor_exit_code": None,
            "fallback_error": "",
        }
    spec_paths.append(asset_spec_path)
    steps["asset_render"] = {
        "spec_path": asset_spec_path,
        "result_json_path": asset_result_path,
        "render_png_path": output_dir / "candidate.png",
        "log_path": output_dir / "unreal_suite.log",
        "editor_exit_code": None,
        "fallback_error": "",
    }
    return {
        "case": {"name": "case-a"},
        "output_dir": output_dir,
        "spec_paths": spec_paths,
        "steps": steps,
    }


def make_step_isolation_state_with_name(root: Path, case_name: str) -> dict:
    state = make_step_isolation_state(root)
    state["case"]["name"] = case_name
    return state


def test_editor_isolation_defaults_to_shard() -> None:
    original_argv = sys.argv
    try:
        sys.argv = ["design2umg_auto_verify_suite.py", "--design-root", "."]
        assert_equal(suite_verify.parse_args().editor_isolation, "shard")
    finally:
        sys.argv = original_argv


def test_step_editor_isolation_runs_single_specs_in_dependency_order() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root)
        driver_specs: dict[Path, list[Path]] = {}
        calls = []

        original_writer = suite_verify.write_editor_driver_script
        original_runner = suite_verify.run_editor_driver
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "suite") -> Path:
                assert_equal(len(spec_paths), 1)
                driver_path = output_root / f"driver-{tag}.py"
                driver_specs[driver_path] = list(spec_paths)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec_path = driver_specs[driver_script_path][0]
                calls.append(spec_path.name)
                spec = verify.helper_load_json(spec_path)
                result_path = Path(spec["result_json_path"])
                if spec["mode"] == "pen":
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                return 0

            suite_verify.write_editor_driver_script = fake_writer
            suite_verify.run_editor_driver = fake_runner
            with redirect_stdout(StringIO()):
                results = suite_verify.run_execution_steps(
                    [state],
                    root,
                    "UnrealEditor.exe",
                    "G:/SampleProject/SampleProject.uproject",
                    180,
                    60,
                    "",
                )
        finally:
            suite_verify.write_editor_driver_script = original_writer
            suite_verify.run_editor_driver = original_runner

        assert_equal(calls, ["automation_generate_spec.json", "automation_asset_spec.json"])
        assert_equal([item["spec_count"] for item in results], [1, 1])
        assert_equal(state["steps"]["generation"]["editor_exit_code"], 0)
        assert_equal(state["steps"]["asset_render"]["editor_exit_code"], 0)


def test_step_editor_isolation_uses_short_driver_script_names() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        long_case_name = "pen_pencil-lunaris_lunaris-design-system-components"
        state = make_step_isolation_state_with_name(root, long_case_name)
        driver_paths = []

        original_writer = suite_verify.write_editor_driver_script
        original_runner = suite_verify.run_editor_driver
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "suite") -> Path:
                driver_path = output_root / f"editor_driver_{tag}.py"
                driver_paths.append(driver_path)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec = verify.helper_load_json(Path(result_paths[0]).with_name("automation_generate_spec.json")) if "generate" in str(result_paths[0]) else None
                if Path(result_paths[0]).name == "ue_generate_result.json":
                    verify.write_json(result_paths[0], {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_paths[0], {"success": True})
                return 0

            suite_verify.write_editor_driver_script = fake_writer
            suite_verify.run_editor_driver = fake_runner
            with redirect_stdout(StringIO()):
                suite_verify.run_execution_steps([state], root, "UnrealEditor.exe", "G:/SampleProject/SampleProject.uproject", 180, 60, "")
        finally:
            suite_verify.write_editor_driver_script = original_writer
            suite_verify.run_editor_driver = original_runner

        assert_equal([path.name for path in driver_paths], ["editor_driver_step-001.py", "editor_driver_step-002.py"])
        assert long_case_name not in " ".join(path.name for path in driver_paths)


def test_step_editor_isolation_retries_driver_start_once() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root)
        driver_specs: dict[Path, list[Path]] = {}
        attempts = []

        original_writer = suite_verify.write_editor_driver_script
        original_runner = suite_verify.run_editor_driver
        original_attempts = suite_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "suite") -> Path:
                driver_path = output_root / f"driver-{tag}.py"
                driver_specs[driver_path] = list(spec_paths)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec_path = driver_specs[driver_script_path][0]
                attempts.append(spec_path.name)
                if spec_path.name == "automation_generate_spec.json" and attempts.count(spec_path.name) == 1:
                    raise suite_verify.EditorDriverStartError("driver did not start")
                spec = verify.helper_load_json(spec_path)
                result_path = Path(spec["result_json_path"])
                if spec["mode"] == "pen":
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_path, {"success": True})
                return 0

            suite_verify.write_editor_driver_script = fake_writer
            suite_verify.run_editor_driver = fake_runner
            suite_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS = 2
            with redirect_stdout(StringIO()):
                suite_verify.run_execution_steps([state], root, "UnrealEditor.exe", "G:/SampleProject/SampleProject.uproject", 180, 60, "")
        finally:
            suite_verify.write_editor_driver_script = original_writer
            suite_verify.run_editor_driver = original_runner
            suite_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS = original_attempts

        assert_equal(attempts, ["automation_generate_spec.json", "automation_generate_spec.json", "automation_asset_spec.json"])


def test_step_editor_isolation_blocks_asset_before_generation_result() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root, include_generation=False)
        calls = []

        original_runner = suite_verify.run_editor_driver
        try:
            def fake_runner(*args, **kwargs) -> int:
                calls.append(args)
                return 0

            suite_verify.run_editor_driver = fake_runner
            try:
                suite_verify.run_execution_steps(
                    [state],
                    root,
                    "UnrealEditor.exe",
                    "G:/SampleProject/SampleProject.uproject",
                    180,
                    60,
                    "",
                )
            except RuntimeError as exc:
                assert "missing generated asset dependency result" in str(exc)
            else:
                raise AssertionError("expected asset step to be blocked before editor launch")
        finally:
            suite_verify.run_editor_driver = original_runner

        assert_equal(calls, [])


def test_step_editor_isolation_reports_driver_never_started() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        driver_path = root / "editor_driver_missing.py"
        driver_path.write_text("", encoding="utf-8")
        result_path = root / "missing_result.json"
        log_path = root / "unreal.log"

        class FakeProcess:
            pid = 12345

            def poll(self):
                return 1

        original_popen = suite_verify.subprocess.Popen
        original_deadline = suite_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC
        try:
            def fake_popen(command, stdout=None, stderr=None):
                if stdout is not None:
                    stdout.write("editor exited before python driver\n")
                    stdout.flush()
                return FakeProcess()

            suite_verify.subprocess.Popen = fake_popen
            suite_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = 0.0
            try:
                suite_verify.run_editor_driver(
                    "UnrealEditor.exe",
                    "G:/SampleProject/SampleProject.uproject",
                    driver_path,
                    [result_path],
                    log_path,
                    0,
                    0,
                    "",
                )
            except RuntimeError as exc:
                assert "automation driver started" in str(exc)
                assert str(driver_path.with_suffix(".log")) in str(exc)
            else:
                raise AssertionError("expected missing driver start to fail explicitly")
        finally:
            suite_verify.subprocess.Popen = original_popen
            suite_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = original_deadline


def execute_generated_editor_driver(
    root: Path,
    spec_paths: list[Path],
    fail_at_command: int | None = None,
) -> tuple[list[str], str, Exception | None]:
    script_path = root / "generated_editor_driver.py"
    manifest_path = root / "generated_editor_driver_manifest.json"
    driver_log_path = root / "generated_editor_driver.log"
    manifest_key = "spec_path" if len(spec_paths) == 1 else "spec_paths"
    manifest_fields = {
        manifest_key: str(spec_paths[0]) if len(spec_paths) == 1 else [str(path) for path in spec_paths]
    }
    editor_driver_writer.write_editor_driver_script(
        script_path,
        manifest_path,
        driver_log_path,
        manifest_fields,
        manifest_key,
    )
    compile(script_path.read_text(encoding="utf-8"), str(script_path), "exec")

    commands: list[str] = []

    class FakeEditorLevelLibrary:
        @staticmethod
        def get_editor_world():
            return object()

    class FakeSystemLibrary:
        @staticmethod
        def execute_console_command(world, command):
            del world
            commands.append(command)
            if fail_at_command is not None and len(commands) == fail_at_command:
                raise RuntimeError("synthetic render failure")

    fake_unreal = SimpleNamespace(
        EditorLevelLibrary=FakeEditorLevelLibrary,
        SystemLibrary=FakeSystemLibrary,
    )
    original_unreal = sys.modules.get("unreal")
    caught: Exception | None = None
    try:
        sys.modules["unreal"] = fake_unreal
        runpy.run_path(str(script_path), run_name="__design2umg_driver_selftest__")
    except Exception as exc:
        caught = exc
    finally:
        if original_unreal is None:
            sys.modules.pop("unreal", None)
        else:
            sys.modules["unreal"] = original_unreal
    return commands, driver_log_path.read_text(encoding="utf-8"), caught


def test_generated_editor_driver_single_spec_returns_normally() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        spec_path = Path(temp_dir) / "single.json"
        commands, driver_log, caught = execute_generated_editor_driver(Path(temp_dir), [spec_path])
    assert caught is None
    assert_equal(commands, [f"Design2UMG.AutomationRender {spec_path} --no-quit"])
    assert "driver:complete" in driver_log
    assert "driver:quit" not in driver_log


def test_generated_editor_driver_multiple_specs_return_normally() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        spec_paths = [root / "first.json", root / "second.json"]
        commands, driver_log, caught = execute_generated_editor_driver(root, spec_paths)
    assert caught is None
    assert_equal(commands, [f"Design2UMG.AutomationRender {path} --no-quit" for path in spec_paths])
    assert_equal(driver_log.count("driver:run:"), 2)
    assert "driver:complete" in driver_log
    assert "driver:quit" not in driver_log


def test_generated_editor_driver_error_propagates_without_forcing_exit() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        spec_paths = [root / "first.json", root / "second.json"]
        commands, driver_log, caught = execute_generated_editor_driver(root, spec_paths, fail_at_command=1)
    assert isinstance(caught, RuntimeError)
    assert_equal(commands, [f"Design2UMG.AutomationRender {spec_paths[0]} --no-quit"])
    assert "synthetic render failure" in driver_log
    assert "driver:quit" not in driver_log


def test_single_session_generated_drivers_compile_without_forced_exit() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        step_driver = root / "step.py"
        suite_driver = root / "suite.py"
        single_session_isolation.write_step_driver(
            step_driver,
            root / "spec.json",
            root / "step.log",
        )
        single_session_isolation.write_single_session_driver(
            suite_driver,
            [str(root / "first.json"), str(root / "second.json")],
            root / "suite.log",
        )
        for driver_path in (step_driver, suite_driver):
            source = driver_path.read_text(encoding="utf-8")
            compile(source, str(driver_path), "exec")
            assert "QUIT_EDITOR" not in source
            assert "driver:complete" in source


def test_run_editor_driver_waits_for_natural_exit_after_clean_shutdown() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        driver_path = root / "editor_driver_step-001.py"
        driver_path.write_text("", encoding="utf-8")
        driver_log_path = driver_path.with_suffix(".log")
        result_path = root / "ue_render_result.json"
        log_path = root / "unreal.log"

        class FakeProcess:
            pid = 12345

            def __init__(self):
                self.killed = False
                self.poll_count = 0

            def poll(self):
                self.poll_count += 1
                return None if self.poll_count == 1 else 3221225477

            def kill(self):
                self.killed = True

            def wait(self, timeout=None):
                return 0

        fake_process = FakeProcess()
        original_popen = suite_verify.subprocess.Popen
        original_sleep = suite_verify.time.sleep
        try:
            def fake_popen(command, stdout=None, stderr=None):
                verify.write_json(result_path, {"success": True})
                write_text(driver_log_path, "2026-06-12T00:00:00.000Z driver:start\n")
                if stdout is not None:
                    stdout.write("LogExit: Exiting.\n")
                    stdout.flush()
                return fake_process

            suite_verify.subprocess.Popen = fake_popen
            suite_verify.time.sleep = lambda _seconds: None
            exit_code = suite_verify.run_editor_driver(
                "UnrealEditor.exe",
                "G:/SampleProject/SampleProject.uproject",
                driver_path,
                [result_path],
                log_path,
                180,
                60,
                "",
            )
        finally:
            suite_verify.subprocess.Popen = original_popen
            suite_verify.time.sleep = original_sleep

        assert_equal(exit_code, 3221225477)
        assert not fake_process.killed


def test_run_editor_driver_idle_timeout_is_not_refreshed_by_existing_start_marker() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        driver_path = root / "editor_driver_step-001.py"
        driver_path.write_text("", encoding="utf-8")
        result_path = root / "missing_result.json"
        log_path = root / "unreal.log"

        class FakeProcess:
            pid = 12345

            def __init__(self):
                self.killed = False
                self.poll_count = 0

            def poll(self):
                self.poll_count += 1
                return None if self.poll_count < 20 else 9

            def kill(self):
                self.killed = True

            def wait(self, timeout=None):
                return 0

        fake_process = FakeProcess()
        clock = {"now": 0.0}

        def fake_time():
            clock["now"] += 1.0
            return clock["now"]

        original_popen = suite_verify.subprocess.Popen
        original_time = suite_verify.time.time
        original_sleep = suite_verify.time.sleep
        try:
            def fake_popen(*args, **kwargs):
                write_text(driver_path.with_suffix(".log"), "2026-06-12T00:00:00.000Z driver:start\n")
                return fake_process

            suite_verify.subprocess.Popen = fake_popen
            suite_verify.time.time = fake_time
            suite_verify.time.sleep = lambda _seconds: None
            try:
                suite_verify.run_editor_driver(
                    "UnrealEditor.exe",
                    "G:/SampleProject/SampleProject.uproject",
                    driver_path,
                    [result_path],
                    log_path,
                    3,
                    100,
                    "",
                )
            except TimeoutError as exc:
                assert "made no progress" in str(exc)
            else:
                raise AssertionError("expected idle timeout despite a persistent driver:start marker")
        finally:
            suite_verify.subprocess.Popen = original_popen
            suite_verify.time.time = original_time
            suite_verify.time.sleep = original_sleep
        assert fake_process.killed


def test_execute_automation_render_preserves_nonzero_editor_exit_code() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        spec_path = root / "automation_spec.json"
        result_path = root / "ue_render_result.json"
        log_path = root / "unreal.log"
        verify.write_json(result_path, {"success": True})
        write_text(log_path, "LogExit: Exiting.\n")
        spec = {"result_json_path": str(result_path)}
        args = SimpleNamespace(
            editor="UnrealEditor.exe",
            project="G:/SampleProject/SampleProject.uproject",
            project_plugin_root=str(root),
            log_cmds="",
            idle_timeout_sec=180,
            startup_timeout_sec=60,
        )

        original_writer = verify.write_single_editor_driver_script
        original_validator = verify.validate_plugin_module_build_id
        original_runner = verify.run_editor
        try:
            verify.write_single_editor_driver_script = lambda output_dir, path: root / "driver.py"
            verify.validate_plugin_module_build_id = lambda editor, plugin_root: None
            verify.run_editor = lambda *runner_args, **runner_kwargs: 3221225477
            result = verify.execute_automation_render(args, spec_path, log_path, spec)
        finally:
            verify.write_single_editor_driver_script = original_writer
            verify.validate_plugin_module_build_id = original_validator
            verify.run_editor = original_runner
        assert_equal(result["editor_exit_code"], 3221225477)


def test_single_run_final_exit_fails_when_generation_process_was_nonzero() -> None:
    generation_result = {
        "editor_exit_code": 3221225477,
        "log_path": "unreal_generate.log",
    }
    asset_result = {
        "editor_exit_code": 0,
        "log_path": "unreal_asset_render.log",
    }
    native_generation_result = {
        "editor_exit_code": 19,
        "log_path": "unreal_native_generate.log",
    }
    render_chain = {
        "generation": generation_result,
        "asset_render": asset_result,
        "native_self_check": {
            "generation": native_generation_result,
            "asset_render": {"editor_exit_code": 0, "log_path": "unreal_native_asset.log"},
        },
    }
    editor_exit_code, editor_log_path = verify.select_render_chain_editor_process_outcome(render_chain)
    assert_equal(editor_exit_code, 3221225477)
    assert_equal(editor_log_path, Path("unreal_generate.log"))
    assert_equal(verify.determine_single_run_exit_code(True, editor_exit_code), 1)
    generation_result["editor_exit_code"] = 0
    editor_exit_code, editor_log_path = verify.select_render_chain_editor_process_outcome(render_chain)
    assert_equal(editor_exit_code, 19)
    assert_equal(editor_log_path, Path("unreal_native_generate.log"))
    assert_equal(verify.determine_single_run_exit_code(True, 0), 0)
    assert_equal(verify.determine_single_run_exit_code(False, 0), 1)


def test_process_case_result_records_invalid_editor_evidence() -> None:
    state = {
        "args": SimpleNamespace(mode="asset"),
        "case": {"name": "case-a"},
        "candidate_path": Path("missing-candidate.png"),
        "reference_validation": {},
        "reference_context": {},
        "native_plan": {"available": False},
        "steps": {
            "asset_render": {
                "editor_process_evidence": suite_verify.collect_editor_process_evidence(
                    17, Path("case-a.log"), [], Path("driver.log")),
                "result": {
                    "editor_exit_code": 17,
                    "log_path": "case-a.log",
                    "ue_result": {"success": True},
                },
                "fallback_error": "",
            },
        },
    }
    original_failure_writer = suite_verify.write_render_failure_report
    try:
        suite_verify.write_render_failure_report = lambda *args, **kwargs: Path("render_failure.json")
        result = suite_verify.process_case_result(state, 17, Path("case-a.log"))
    finally:
        suite_verify.write_render_failure_report = original_failure_writer
    assert_equal(result["editor_exit_code"], 17)
    assert result["editor_process_failed"]


def test_suite_final_exit_fails_on_invalid_editor_evidence() -> None:
    args = SimpleNamespace(
        parallel_editors=1,
        idle_timeout_sec=180,
        startup_timeout_sec=60,
        editor_isolation="shard",
        post_workers=1,
        include="case-a",
        exclude="",
        reference_cache="read-write",
        native_self_check="off",
        ir_golden="",
        geometry_golden="",
    )
    summary = suite_verify.build_suite_summary(
        args=args,
        editor_exit_code=17,
        editor_exit_codes=[17],
        shards=[{"shard_id": 0}],
        shard_results=[{"shard_id": 0, "exit_code": 17}],
        post_workers=1,
        design_root=Path("G:/Designs"),
        output_root=Path("G:/Runs/suite_run"),
        run_scope_path=Path("G:/Runs/suite_run/run_scope.json"),
        reference_cache_root=None,
        suite_start_time=10.0,
        prep_completed_at=11.0,
        editor_completed_at=12.0,
        postprocess_completed_at=13.0,
        case_results=[{"name": "case-a", "blocked": False}],
        completeness={"blocked": False},
    )
    assert_equal(summary["editor_process"], {"passed": 0, "failed": 1})
    historical_validation = {"blocked": False, "historical_best_regressions": []}
    assert_equal(suite_verify.determine_suite_exit_code(
        formal_gate_candidate=False,
        completeness={"blocked": False},
        historical_validation=historical_validation,
        summary=summary,
    ), 1)
    summary["editor_process"] = {"passed": 1, "failed": 0}
    assert_equal(suite_verify.determine_suite_exit_code(
        formal_gate_candidate=False,
        completeness={"blocked": False},
        historical_validation=historical_validation,
        summary=summary,
    ), 0)


def test_process_case_results_uses_each_cases_editor_outcome() -> None:
    args = SimpleNamespace(ir_golden_root="", geometry_golden_root="")
    states = [
        {"name": "case-a", "args": args, "steps": {"render": {"editor_exit_code": 0, "log_path": Path("case-a.log")}}},
        {"name": "case-b", "args": args, "steps": {"render": {"editor_exit_code": 17, "log_path": Path("case-b.log")}}},
    ]
    observed: list[tuple[str, int, Path]] = []
    original_processor = suite_verify.process_case_result
    try:
        def fake_processor(state, exit_code, log_path):
            observed.append((state["name"], exit_code, log_path))
            return {"name": state["name"]}

        suite_verify.process_case_result = fake_processor
        suite_verify.process_case_results(states, 17, Path("unreal_suite.log"), 1)
    finally:
        suite_verify.process_case_result = original_processor
    assert_equal(observed, [
        ("case-a", 0, Path("case-a.log")),
        ("case-b", 17, Path("case-b.log")),
    ])


def test_terminate_owned_editor_process_targets_only_passed_process() -> None:
    class FakeProcess:
        def __init__(self, pid: int, wait_times_out: bool):
            self.pid = pid
            self.wait_times_out = wait_times_out
            self.killed = False
            self.wait_calls = 0

        def kill(self):
            self.killed = True

        def wait(self, timeout=None):
            self.wait_calls += 1
            if self.wait_times_out and self.wait_calls == 1:
                raise subprocess.TimeoutExpired("UnrealEditor.exe", timeout)
            return 0

    owned = FakeProcess(12345, wait_times_out=True)
    unrelated = FakeProcess(54321, wait_times_out=False)
    commands = []
    original_run = verify.subprocess.run
    try:
        def fake_run(command, **kwargs):
            commands.append(command)
            return subprocess.CompletedProcess(command, 0, "", "")

        verify.subprocess.run = fake_run
        verify.terminate_owned_editor_process(owned)
    finally:
        verify.subprocess.run = original_run

    assert owned.killed
    assert_equal(owned.wait_calls, 2)
    assert not unrelated.killed
    assert_equal(unrelated.wait_calls, 0)
    assert_equal(commands, [["taskkill", "/F", "/PID", "12345"]])


def test_extend_deadline_never_shortens_existing_wait() -> None:
    current_deadline = suite_verify.time.time() + 180.0
    extended = suite_verify.extend_deadline(current_deadline, 30.0)
    assert extended >= current_deadline


def test_runtime_project_fails_closed_without_explicit_input() -> None:
    original_project = os.environ.pop(verify.RUNTIME_PROJECT_ENV_VAR, None)
    try:
        try:
            verify.prepare_runtime_project(SimpleNamespace(project=""), [])
        except RuntimeError as exc:
            message = str(exc)
            assert "Runtime project path not provided" in message
            assert "HostProject fallback and source-path inference are not supported" in message
            return
        raise AssertionError("expected missing runtime project to fail closed")
    finally:
        if original_project is not None:
            os.environ[verify.RUNTIME_PROJECT_ENV_VAR] = original_project


def main() -> None:
    # Rich text remains subject to text geometry correspondence on both sides.
    assert "RichTextBlock" in geometry_compare.SOURCE_TEXT_TYPES
    assert "RichTextBlock" in geometry_compare.RUNTIME_TEXT_CLASSES
    test_dynamic_corpus_cardinality_and_scope()
    test_nested_export_discovery_and_unique_names()
    test_discovery_and_run_scope_contracts()
    test_completeness_contract()
    test_transparent_rgb_does_not_count_as_visible_change()
    test_opaque_rgb_difference_still_counts()
    test_semi_transparent_difference_still_counts()
    test_evaluate_no_regression_checks_every_available_metric()
    test_load_baseline_case_metrics_rejects_incomplete_metrics()
    test_no_regression_epsilon_rejects_non_finite_and_negative()
    test_geometry_wrapper_migration_does_not_fail()
    test_geometry_wrapper_insertion_with_real_move_fails()
    test_geometry_icon_font_typed_projection()
    test_geometry_disappeared_node_fails()
    test_geometry_ir_unmatched_without_golden_drift_passes()
    test_directory_mode_enumerates_golden_run_union()
    test_suite_summary_fails_required_unavailable_no_regression()
    test_execution_shards_contracts()
    test_evidence_contract_shared_by_manifest_and_run_validation()
    test_binary_binding_contract_shared_by_manifest_production()
    test_editor_isolation_defaults_to_shard()
    test_step_editor_isolation_runs_single_specs_in_dependency_order()
    test_step_editor_isolation_uses_short_driver_script_names()
    test_step_editor_isolation_retries_driver_start_once()
    test_step_editor_isolation_blocks_asset_before_generation_result()
    test_step_editor_isolation_reports_driver_never_started()
    test_generated_editor_driver_single_spec_returns_normally()
    test_generated_editor_driver_multiple_specs_return_normally()
    test_generated_editor_driver_error_propagates_without_forcing_exit()
    test_single_session_generated_drivers_compile_without_forced_exit()
    test_run_editor_driver_waits_for_natural_exit_after_clean_shutdown()
    test_run_editor_driver_idle_timeout_is_not_refreshed_by_existing_start_marker()
    test_execute_automation_render_preserves_nonzero_editor_exit_code()
    test_single_run_final_exit_fails_when_generation_process_was_nonzero()
    test_process_case_result_records_invalid_editor_evidence()
    test_suite_final_exit_fails_on_invalid_editor_evidence()
    test_process_case_results_uses_each_cases_editor_outcome()
    test_terminate_owned_editor_process_targets_only_passed_process()
    test_extend_deadline_never_shortens_existing_wait()
    test_runtime_project_fails_closed_without_explicit_input()
    print("validation helpers selftest passed")


if __name__ == "__main__":
    main()
