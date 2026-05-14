from pathlib import Path

import design2umg_render_rule as render_rule


def evaluate_report_compatibility(report_path: Path) -> dict:
    return {
        "render_size": render_rule.evaluate_report_render_size_rule(report_path),
        "pipeline_determinism": render_rule.evaluate_report_pipeline_determinism_rule(report_path),
    }
