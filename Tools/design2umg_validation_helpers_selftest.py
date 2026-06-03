import os
import tempfile
from pathlib import Path
from types import SimpleNamespace

import design2umg_auto_verify as verify
from design2umg_verify_lib import batch_discovery
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
    test_execution_shards_contracts()
    test_runtime_project_fails_closed_without_explicit_input()
    print("validation helpers selftest passed")


if __name__ == "__main__":
    main()
