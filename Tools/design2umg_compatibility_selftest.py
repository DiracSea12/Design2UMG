from pathlib import Path

from design2umg_verify_lib import compatibility


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def main() -> None:
    original_render_size = compatibility.render_rule.evaluate_report_render_size_rule
    original_pipeline = compatibility.render_rule.evaluate_report_pipeline_determinism_rule
    calls: list[tuple[str, Path]] = []

    def fake_render_size(report_path: Path) -> dict:
        calls.append(("render_size", report_path))
        return {"status": "pass", "rule": "render_size"}

    def fake_pipeline(report_path: Path) -> dict:
        calls.append(("pipeline_determinism", report_path))
        return {"status": "blocked", "rule": "pipeline_determinism"}

    compatibility.render_rule.evaluate_report_render_size_rule = fake_render_size
    compatibility.render_rule.evaluate_report_pipeline_determinism_rule = fake_pipeline
    try:
        report_path = Path("fake-report.json")
        result = compatibility.evaluate_report_compatibility(report_path)
    finally:
        compatibility.render_rule.evaluate_report_render_size_rule = original_render_size
        compatibility.render_rule.evaluate_report_pipeline_determinism_rule = original_pipeline

    assert_equal(
        result,
        {
            "render_size": {"status": "pass", "rule": "render_size"},
            "pipeline_determinism": {"status": "blocked", "rule": "pipeline_determinism"},
        },
    )
    assert_equal(
        calls,
        [
            ("render_size", report_path),
            ("pipeline_determinism", report_path),
        ],
    )
    print("compatibility selftest passed")


if __name__ == "__main__":
    main()
