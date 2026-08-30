import os
import sys
import tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify
import design2umg_auto_verify_batch as batch_verify
import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_scoring as scoring
import design2umg_validate_run as validate_run
from design2umg_verify_lib import batch_discovery
from design2umg_verify_lib import evidence_contract
from design2umg_verify_lib import execution_shards


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
    roots = []
    for name, large_png_size in (("figma_export", 2), ("figma_export2", 3)):
        export_root = workspace_root / name
        write_text(export_root / "design.json", '{"document":{"children":[]}}')
        write_bytes(export_root / "small.png", PNG_1X1)
        write_bytes(export_root / "large.png", PNG_1X1 * large_png_size)
        roots.append(export_root)
    return roots


def test_discovery_and_run_scope_contracts() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        workspace_root = Path(temp_dir)
        design_root = workspace_root / "Designs" / "Nested"
        pen_path = make_pen_fixture(design_root)
        figma_roots = make_figma_export_fixture(workspace_root)

        assert_equal(
            batch_discovery.sanitize_case_name("  Hello, UE 5.7!  "),
            "hello-ue-5-7",
        )
        assert_equal(batch_discovery.classify_run_scope("", ""), "full")
        assert_equal(batch_discovery.classify_run_scope("hero", ""), "focus")
        assert_equal(batch_discovery.classify_run_scope("", "skip"), "full_with_exclusions")

        default_roots = batch_discovery.resolve_default_figma_export_roots(design_root, [])
        assert_equal(default_roots, [str(root.resolve()) for root in figma_roots])
        explicit_roots = batch_discovery.resolve_default_figma_export_roots(design_root, [str(figma_roots[1]), ""])
        assert_equal(explicit_roots, [str(figma_roots[1].resolve())])

        pen_cases = batch_discovery.discover_pen_cases(design_root, "", "")
        assert_equal(
            [case["name"] for case in pen_cases],
            [
                "pen_sample-screen_main-hero",
                "pen_sample-screen_main-hero-2",
                "pen_sample-screen_inferred-root",
            ],
        )
        assert_equal(batch_discovery.discover_pen_cases(design_root, "inferred", ""), [pen_cases[2]])
        assert_equal(batch_discovery.discover_pen_cases(design_root, "", "-2$"), [pen_cases[0], pen_cases[2]])

        figma_cases = batch_discovery.discover_figma_export_cases(default_roots, "", "")
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
        run_scope_path = batch_discovery.write_run_scope(output_root, args, all_cases, default_roots)
        run_scope = verify.load_json_document(run_scope_path)
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
        completeness = batch_discovery.collect_batch_completeness(
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
        scoring.load_baseline_case_metrics = lambda _case_dir, _pixel_tolerance: baseline
        return scoring.evaluate_no_regression(report, None, 6)

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


def test_batch_summary_fails_required_unavailable_no_regression() -> None:
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
    summary = batch_verify.build_batch_summary(
        args=args,
        editor_exit_code=0,
        editor_exit_codes=[0],
        shards=[{"shard_id": 0}],
        shard_results=[{"shard_id": 0, "exit_code": 0}],
        post_workers=1,
        design_root=Path("G:/Designs"),
        output_root=Path("G:/Runs/batch_run"),
        run_scope_path=Path("G:/Runs/batch_run/run_scope.json"),
        reference_cache_root=None,
        batch_start_time=10.0,
        prep_completed_at=11.0,
        editor_completed_at=12.0,
        postprocess_completed_at=13.0,
        case_results=case_results,
        completeness={"blocked": False},
    )

    assert_equal(summary["no_regression"], {"available": 0, "passed": 0, "failed": 2})


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
            "baseline_compare_report_hash": "baseline-hash",
            "reference_root_case_path": "G:/SampleProject/Saved/ReferenceRoot/case-a",
        }

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
            "engine_root": "G:/UE5/UnrealEngine",
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
            "log_path": output_dir / "unreal_batch.log",
            "editor_exit_code": None,
            "fallback_error": "",
        }
    spec_paths.append(asset_spec_path)
    steps["asset_render"] = {
        "spec_path": asset_spec_path,
        "result_json_path": asset_result_path,
        "render_png_path": output_dir / "candidate.png",
        "log_path": output_dir / "unreal_batch.log",
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
        sys.argv = ["design2umg_auto_verify_batch.py", "--design-root", "."]
        assert_equal(batch_verify.parse_args().editor_isolation, "shard")
    finally:
        sys.argv = original_argv


def test_step_editor_isolation_runs_single_specs_in_dependency_order() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root)
        driver_specs: dict[Path, list[Path]] = {}
        calls = []

        original_writer = batch_verify.write_editor_driver_script
        original_runner = batch_verify.run_editor_driver
        original_cleaner = verify.ensure_clean_editor
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "batch") -> Path:
                assert_equal(len(spec_paths), 1)
                driver_path = output_root / f"driver-{tag}.py"
                driver_specs[driver_path] = list(spec_paths)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec_path = driver_specs[driver_script_path][0]
                calls.append(spec_path.name)
                spec = verify.load_json_document(spec_path)
                result_path = Path(spec["result_json_path"])
                if spec["mode"] == "pen":
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                return 0

            batch_verify.write_editor_driver_script = fake_writer
            batch_verify.run_editor_driver = fake_runner
            verify.ensure_clean_editor = lambda: None
            with redirect_stdout(StringIO()):
                results = batch_verify.run_execution_steps(
                    [state],
                    root,
                    "UnrealEditor.exe",
                    "G:/SampleProject/SampleProject.uproject",
                    180,
                    60,
                    "",
                )
        finally:
            batch_verify.write_editor_driver_script = original_writer
            batch_verify.run_editor_driver = original_runner
            verify.ensure_clean_editor = original_cleaner

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

        original_writer = batch_verify.write_editor_driver_script
        original_runner = batch_verify.run_editor_driver
        original_cleaner = verify.ensure_clean_editor
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "batch") -> Path:
                driver_path = output_root / f"editor_driver_{tag}.py"
                driver_paths.append(driver_path)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec = verify.load_json_document(Path(result_paths[0]).with_name("automation_generate_spec.json")) if "generate" in str(result_paths[0]) else None
                if Path(result_paths[0]).name == "ue_generate_result.json":
                    verify.write_json(result_paths[0], {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_paths[0], {"success": True})
                return 0

            batch_verify.write_editor_driver_script = fake_writer
            batch_verify.run_editor_driver = fake_runner
            verify.ensure_clean_editor = lambda: None
            with redirect_stdout(StringIO()):
                batch_verify.run_execution_steps([state], root, "UnrealEditor.exe", "G:/SampleProject/SampleProject.uproject", 180, 60, "")
        finally:
            batch_verify.write_editor_driver_script = original_writer
            batch_verify.run_editor_driver = original_runner
            verify.ensure_clean_editor = original_cleaner

        assert_equal([path.name for path in driver_paths], ["editor_driver_step-001.py", "editor_driver_step-002.py"])
        assert long_case_name not in " ".join(path.name for path in driver_paths)


def test_step_editor_isolation_retries_driver_start_once() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root)
        driver_specs: dict[Path, list[Path]] = {}
        attempts = []

        original_writer = batch_verify.write_editor_driver_script
        original_runner = batch_verify.run_editor_driver
        original_cleaner = verify.ensure_clean_editor
        original_attempts = batch_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS
        try:
            def fake_writer(output_root: Path, spec_paths: list[Path], tag: str = "batch") -> Path:
                driver_path = output_root / f"driver-{tag}.py"
                driver_specs[driver_path] = list(spec_paths)
                return driver_path

            def fake_runner(editor_path, project_path, driver_script_path, result_paths, log_path, idle_timeout_sec, startup_timeout_sec, log_cmds="") -> int:
                spec_path = driver_specs[driver_script_path][0]
                attempts.append(spec_path.name)
                if spec_path.name == "automation_generate_spec.json" and attempts.count(spec_path.name) == 1:
                    raise batch_verify.EditorDriverStartError("driver did not start")
                spec = verify.load_json_document(spec_path)
                result_path = Path(spec["result_json_path"])
                if spec["mode"] == "pen":
                    verify.write_json(result_path, {"success": True, "generated_asset_path": "/Game/Generated.CaseA"})
                else:
                    verify.write_json(result_path, {"success": True})
                return 0

            batch_verify.write_editor_driver_script = fake_writer
            batch_verify.run_editor_driver = fake_runner
            verify.ensure_clean_editor = lambda: None
            batch_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS = 2
            with redirect_stdout(StringIO()):
                batch_verify.run_execution_steps([state], root, "UnrealEditor.exe", "G:/SampleProject/SampleProject.uproject", 180, 60, "")
        finally:
            batch_verify.write_editor_driver_script = original_writer
            batch_verify.run_editor_driver = original_runner
            verify.ensure_clean_editor = original_cleaner
            batch_verify.EDITOR_STEP_DRIVER_START_ATTEMPTS = original_attempts

        assert_equal(attempts, ["automation_generate_spec.json", "automation_generate_spec.json", "automation_asset_spec.json"])


def test_step_editor_isolation_blocks_asset_before_generation_result() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        state = make_step_isolation_state(root, include_generation=False)
        calls = []

        original_runner = batch_verify.run_editor_driver
        try:
            def fake_runner(*args, **kwargs) -> int:
                calls.append(args)
                return 0

            batch_verify.run_editor_driver = fake_runner
            try:
                batch_verify.run_execution_steps(
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
            batch_verify.run_editor_driver = original_runner

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

        original_popen = batch_verify.subprocess.Popen
        original_deadline = batch_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC
        try:
            def fake_popen(command, stdout=None, stderr=None):
                if stdout is not None:
                    stdout.write("editor exited before python driver\n")
                    stdout.flush()
                return FakeProcess()

            batch_verify.subprocess.Popen = fake_popen
            batch_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = 0.0
            try:
                batch_verify.run_editor_driver(
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
            batch_verify.subprocess.Popen = original_popen
            batch_verify.EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = original_deadline


def test_run_editor_driver_returns_after_clean_shutdown_result() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        driver_path = root / "editor_driver_step-001.py"
        driver_path.write_text("", encoding="utf-8")
        driver_log_path = driver_path.with_suffix(".log")
        result_path = root / "ue_render_result.json"
        log_path = root / "unreal.log"
        verify.write_json(result_path, {"success": True})
        write_text(driver_log_path, "2026-06-12T00:00:00.000Z driver:start\n")

        class FakeProcess:
            pid = 12345

            def __init__(self):
                self.killed = False

            def poll(self):
                return None

            def kill(self):
                self.killed = True

        fake_process = FakeProcess()
        original_popen = batch_verify.subprocess.Popen
        try:
            def fake_popen(command, stdout=None, stderr=None):
                if stdout is not None:
                    stdout.write("LogExit: Exiting.\n")
                    stdout.flush()
                return fake_process

            batch_verify.subprocess.Popen = fake_popen
            exit_code = batch_verify.run_editor_driver(
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
            batch_verify.subprocess.Popen = original_popen

        assert_equal(exit_code, 0)
        assert fake_process.killed


def test_extend_deadline_never_shortens_existing_wait() -> None:
    current_deadline = batch_verify.time.time() + 180.0
    extended = batch_verify.extend_deadline(current_deadline, 30.0)
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
    test_discovery_and_run_scope_contracts()
    test_completeness_contract()
    test_transparent_rgb_does_not_count_as_visible_change()
    test_opaque_rgb_difference_still_counts()
    test_semi_transparent_difference_still_counts()
    test_evaluate_no_regression_checks_every_available_metric()
    test_load_baseline_case_metrics_rejects_incomplete_metrics()
    test_batch_summary_fails_required_unavailable_no_regression()
    test_execution_shards_contracts()
    test_evidence_contract_shared_by_manifest_and_run_validation()
    test_binary_binding_contract_shared_by_manifest_production()
    test_editor_isolation_defaults_to_shard()
    test_step_editor_isolation_runs_single_specs_in_dependency_order()
    test_step_editor_isolation_uses_short_driver_script_names()
    test_step_editor_isolation_retries_driver_start_once()
    test_step_editor_isolation_blocks_asset_before_generation_result()
    test_step_editor_isolation_reports_driver_never_started()
    test_run_editor_driver_returns_after_clean_shutdown_result()
    test_extend_deadline_never_shortens_existing_wait()
    test_runtime_project_fails_closed_without_explicit_input()
    print("validation helpers selftest passed")


if __name__ == "__main__":
    main()
