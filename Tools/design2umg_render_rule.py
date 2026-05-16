from pathlib import Path

from design2umg_verify_lib.diagnostics import EvidenceReadError
from design2umg_verify_lib.json_io import load_json as helper_load_json


DESIGN_SIZE_RENDER_RULE = "source_design_size_v1"
DESIGN_SIZE_AUTHORITY_SOURCE = "ue_blueprint_design_time_size"
DESIGN_SIZE_COMPARISON_MODES = {
    "native_size",
    "reference_scaled_to_candidate_for_compare_only",
}
PIPELINE_DETERMINISM_RULE = "source_pipeline_transient_state_v4"
PIPELINE_DETERMINISM_AUTHORITY = "ue_automation_render_reset"
TEXT_MEASUREMENT_CACHE_SCOPE = "per_automation_render"
TEXT_SINGLE_LINE_HEIGHT_PROBE_AUTHORITY = "slate_font_measure_single_line_height_v1"
ASSET_PACKAGE_RELOAD_AUTHORITY = "ue_reload_package_before_asset_render_v1"
SLATE_FONT_CACHE_AUTHORITY = "flush_before_automation_render_v1"
RENDER_CHAIN_MATCH_EPSILON = 1e-9
AUTOMATION_SPEC_FILENAMES = (
    "automation_spec.json",
    "automation_generate_spec.json",
    "automation_asset_spec.json",
    "automation_native_generate_spec.json",
    "automation_native_asset_spec.json",
)
REQUESTED_RENDER_SIZE_FIELDS = ("render_width", "render_height")


def load_json(path: Path) -> dict:
    return helper_load_json(path)


def nested_value(payload: dict, path: tuple[str, ...]):
    current = payload
    for key in path:
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    return current


def parse_positive_int(value) -> int | None:
    try:
        parsed = int(round(float(value)))
    except (TypeError, ValueError):
        return None
    return parsed if parsed > 1 else None


def as_size_pair(width, height) -> list[int] | None:
    parsed_width = parse_positive_int(width)
    parsed_height = parse_positive_int(height)
    if parsed_width is None or parsed_height is None:
        return None
    return [parsed_width, parsed_height]


def list_size(value) -> list[int] | None:
    if not isinstance(value, list) or len(value) != 2:
        return None
    return as_size_pair(value[0], value[1])


def requested_render_size_fields(spec_path: Path) -> list[str]:
    if not spec_path.is_file():
        return []

    try:
        payload = load_json(spec_path)
    except EvidenceReadError:
        return []

    if not isinstance(payload, dict):
        return []
    return [field for field in REQUESTED_RENDER_SIZE_FIELDS if field in payload]


def evaluate_report_render_size_rule(report_path: Path) -> dict:
    result = {
        "rule": DESIGN_SIZE_RENDER_RULE,
        "compatible": False,
        "reason": "",
        "report_path": str(report_path),
        "authority_source": "",
        "comparison_mode": "",
        "design_time_size": None,
        "authority_design_size": None,
        "candidate_size": None,
        "render_size": None,
        "requested_render_size_specs": [],
    }

    if not report_path.is_file():
        result["reason"] = "report_missing"
        return result

    try:
        report = load_json(report_path)
    except EvidenceReadError as exc:
        result["reason"] = exc.reason
        result["diagnostic_path"] = str(exc.path)
        result["diagnostic_detail"] = exc.detail
        return result

    if not isinstance(report, dict):
        result["reason"] = "report_invalid"
        return result

    case_dir = report_path.parent
    requested_specs = []
    for filename in AUTOMATION_SPEC_FILENAMES:
        spec_path = case_dir / filename
        fields = requested_render_size_fields(spec_path)
        if fields:
            requested_specs.append({
                "path": str(spec_path),
                "fields": fields,
            })

    authority_source = nested_value(report, ("reference_context", "render_size_authority", "source"))
    comparison_mode = report.get("comparison_mode", "")
    design_time_size = as_size_pair(
        nested_value(report, ("ue_result", "design_time_width")),
        nested_value(report, ("ue_result", "design_time_height")),
    )
    authority_design_size = list_size(nested_value(report, ("reference_context", "render_size_authority", "design_size")))
    candidate_size = (
        list_size(report.get("candidate_size"))
        or list_size(report.get("actual_candidate_size"))
        or list_size(nested_value(report, ("reference_context", "render_size_authority", "candidate_size")))
    )
    render_size = as_size_pair(
        nested_value(report, ("ue_result", "render_width")),
        nested_value(report, ("ue_result", "render_height")),
    )
    result["authority_source"] = authority_source or ""
    result["comparison_mode"] = comparison_mode or ""
    result["design_time_size"] = design_time_size
    result["authority_design_size"] = authority_design_size
    result["candidate_size"] = candidate_size
    result["render_size"] = render_size
    result["requested_render_size_specs"] = requested_specs

    if requested_specs:
        result["reason"] = "legacy_requested_render_size"
        return result

    if authority_source != DESIGN_SIZE_AUTHORITY_SOURCE:
        result["reason"] = "render_size_authority_missing_or_mismatch"
        return result

    if design_time_size is None:
        result["reason"] = "design_time_size_missing"
        return result

    if authority_design_size != design_time_size:
        result["reason"] = "authority_design_size_mismatch"
        return result

    if candidate_size != design_time_size:
        result["reason"] = "candidate_size_mismatch_design_time_size"
        return result

    if render_size != design_time_size:
        result["reason"] = "render_size_mismatch_design_time_size"
        return result

    if comparison_mode not in DESIGN_SIZE_COMPARISON_MODES:
        result["reason"] = "comparison_mode_mismatch"
        return result

    result["compatible"] = True
    return result


def evaluate_report_pipeline_determinism_rule(report_path: Path) -> dict:
    result = {
        "rule": PIPELINE_DETERMINISM_RULE,
        "compatible": False,
        "reason": "",
        "report_path": str(report_path),
        "pipeline_determinism_rule": "",
        "transient_state_authority": "",
        "transient_state_reset_before_render": None,
        "text_measurement_cache_scope": "",
        "text_single_line_height_probe_authority": "",
        "force_reload_package_requested": None,
        "asset_package_reload_authority": "",
        "slate_font_cache_authority": "",
        "final_render_mode": "",
        "generation_vs_asset_changed_ratio": None,
        "generation_vs_asset_stable_changed_ratio": None,
        "generation_vs_asset_mean_abs_error": None,
        "generation_vs_asset_stable_mean_abs_error": None,
        "generation_vs_asset_structure_available": None,
        "generation_vs_asset_structure_texts_match": None,
        "generation_vs_asset_structure_counts_match": None,
    }

    if not report_path.is_file():
        result["reason"] = "report_missing"
        return result

    try:
        report = load_json(report_path)
    except EvidenceReadError as exc:
        result["reason"] = exc.reason
        result["diagnostic_path"] = str(exc.path)
        result["diagnostic_detail"] = exc.detail
        return result

    if not isinstance(report, dict):
        result["reason"] = "report_invalid"
        return result

    ue_result = report.get("ue_result") or {}
    if not isinstance(ue_result, dict):
        result["reason"] = "ue_result_missing"
        return result

    result["pipeline_determinism_rule"] = ue_result.get("pipeline_determinism_rule", "")
    result["transient_state_authority"] = ue_result.get("transient_state_authority", "")
    result["transient_state_reset_before_render"] = ue_result.get("transient_state_reset_before_render")
    result["text_measurement_cache_scope"] = ue_result.get("text_measurement_cache_scope", "")
    result["text_single_line_height_probe_authority"] = ue_result.get("text_single_line_height_probe_authority", "")
    result["force_reload_package_requested"] = ue_result.get("force_reload_package_requested")
    result["asset_package_reload_authority"] = ue_result.get("asset_package_reload_authority", "")
    result["slate_font_cache_authority"] = ue_result.get("slate_font_cache_authority", "")

    if result["pipeline_determinism_rule"] != PIPELINE_DETERMINISM_RULE:
        result["reason"] = "pipeline_determinism_rule_missing_or_mismatch"
        return result

    if result["transient_state_authority"] != PIPELINE_DETERMINISM_AUTHORITY:
        result["reason"] = "transient_state_authority_missing_or_mismatch"
        return result

    if result["transient_state_reset_before_render"] is not True:
        result["reason"] = "transient_state_reset_missing"
        return result

    if result["text_measurement_cache_scope"] != TEXT_MEASUREMENT_CACHE_SCOPE:
        result["reason"] = "text_measurement_cache_scope_missing_or_mismatch"
        return result

    if result["text_single_line_height_probe_authority"] != TEXT_SINGLE_LINE_HEIGHT_PROBE_AUTHORITY:
        result["reason"] = "text_single_line_height_probe_authority_missing_or_mismatch"
        return result

    if result["force_reload_package_requested"] is not True:
        result["reason"] = "force_reload_package_requested_missing"
        return result

    if result["asset_package_reload_authority"] != ASSET_PACKAGE_RELOAD_AUTHORITY:
        result["reason"] = "asset_package_reload_authority_missing_or_mismatch"
        return result

    if result["slate_font_cache_authority"] != SLATE_FONT_CACHE_AUTHORITY:
        result["reason"] = "slate_font_cache_authority_missing_or_mismatch"
        return result

    render_chain = report.get("render_chain") or {}
    if not isinstance(render_chain, dict):
        result["reason"] = "render_chain_missing"
        return result

    result["final_render_mode"] = render_chain.get("final_render_mode", "")
    generation_vs_asset = render_chain.get("generation_vs_asset")
    if result["final_render_mode"] == "asset_reload":
        if not isinstance(generation_vs_asset, dict):
            result["reason"] = "generation_vs_asset_metrics_missing"
            return result

        metric_fields = (
            ("generation_vs_asset_changed_ratio", "changed_ratio"),
            ("generation_vs_asset_stable_changed_ratio", "stable_changed_ratio"),
            ("generation_vs_asset_mean_abs_error", "mean_abs_error"),
            ("generation_vs_asset_stable_mean_abs_error", "stable_mean_abs_error"),
        )
        for result_key, metric_key in metric_fields:
            metric_value = generation_vs_asset.get(metric_key)
            result[result_key] = metric_value
            if metric_value is None or metric_value > RENDER_CHAIN_MATCH_EPSILON:
                result["reason"] = f"{metric_key}_mismatch"
                return result

        generation_vs_asset_structure = render_chain.get("generation_vs_asset_structure") or {}
        if not isinstance(generation_vs_asset_structure, dict):
            result["reason"] = "generation_vs_asset_structure_missing"
            return result

        result["generation_vs_asset_structure_available"] = generation_vs_asset_structure.get("available")
        result["generation_vs_asset_structure_texts_match"] = generation_vs_asset_structure.get("texts_match")
        counts_match = (
            generation_vs_asset_structure.get("left_total_nodes") == generation_vs_asset_structure.get("right_total_nodes")
            and generation_vs_asset_structure.get("left_leaf_nodes") == generation_vs_asset_structure.get("right_leaf_nodes")
            and generation_vs_asset_structure.get("left_container_nodes") == generation_vs_asset_structure.get("right_container_nodes")
            and generation_vs_asset_structure.get("left_text_nodes") == generation_vs_asset_structure.get("right_text_nodes")
            and generation_vs_asset_structure.get("left_visual_nodes") == generation_vs_asset_structure.get("right_visual_nodes")
            and generation_vs_asset_structure.get("kind_histograms_match") is True
            and generation_vs_asset_structure.get("class_histograms_match") is True
        )
        result["generation_vs_asset_structure_counts_match"] = counts_match
        if result["generation_vs_asset_structure_available"] is not True:
            result["reason"] = "generation_vs_asset_structure_unavailable"
            return result
        if result["generation_vs_asset_structure_texts_match"] is not True or not counts_match:
            result["reason"] = "generation_vs_asset_structure_mismatch"
            return result

    result["compatible"] = True
    return result
