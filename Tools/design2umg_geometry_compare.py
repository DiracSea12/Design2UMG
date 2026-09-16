"""Design2UMG geometry golden layer (validation layer 2).

Compares the runtime geometry dump (geometry_dump.json, written by the
editor-side Design2UMGAutomationGeometryDump after the compiled render) against
expectations derived from the same run's ir_dump.json plus a frozen golden dump.

  - fidelity: expected geometry is derived per node from IR Bounds/Slot/
    WidgetStyle using the v1 policy table (strict / derived / position_only /
    size_only / derived_size / exempt); runtime geometry is the widget's
    cached geometry anchored on the runtime pair of its IR parent (absolute
    for the root) -- the same frame the expectation is derived in, so a
    wrapper inserted between the IR parent and the node (absolute geometry
    unchanged) is not a fidelity diff. Tolerance 0.75px.
  - stability: the frozen golden geometry_dump.json is compared against the
    current dump on the same comparable components (0.75px).

Usage:
  python design2umg_geometry_compare.py <golden> <run>
      <golden>/<run> may each be a single geometry_dump.json file or a run/case
      root directory. Directory mode compares <root>/<case>/geometry_dump.json.
  Options:
      --max-diffs N     max printed diffs per comparison (default 50)
      --no-reports      do not write geometry_golden_report.json into run case dirs
      --include REGEX   only compare case names matching this regex (dir mode)

Semantics:
  - missing golden dump -> case verdict "unavailable" (fidelity still reported)
  - missing dump / missing ir_dump -> "unavailable" (no expectation source)
  - directory mode enumerates golden ∪ run case dirs, so a case whose dump is
    missing still appears as unavailable instead of vanishing
  - the case verdict is the regression signal: golden runtime vs current runtime
    on the comparable components; fidelity diffs (IR-derived expectation vs
    runtime) are reported as the fidelity record and do not fail the case,
    because a first-frozen golden legitimately contains long-standing
    fidelity drift that this layer exists to expose, not to gate on
  - alignment paired ratio below 60% -> alignment_low_confidence (not a FAIL)
  - alignment pairs IR and runtime nodes via compute_tree_correspondence
    (name + name_N + kind fallback, tolerating wrapper-inserted path
    migration); IR nodes without a runtime counterpart and runtime-only nodes
    (synthetic wrappers) are visible in the alignment block and do not FAIL;
    stability FAILs come only from geometry drift beyond 0.75px and from
    golden nodes absent from the current dump (nodes added after the freeze
    are counted, not failed)
  - stability x/y are anchored on the nearest golden-paired ancestor on both
    sides (not the immediate parent), so a wrapper inserted into the runtime
    tree that leaves absolute geometry unchanged is not drift
  - exempt share above 50% of matched nodes -> low_coverage (not a FAIL)
Exit code: 0 = no failures, 1 = at least one case failed, 2 = usage/config
error (bad paths, no dumps found under the run root, unreadable input files).
"""

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

import design2umg_auto_verify as verify

REPORT_NAME = "geometry_golden_report.json"
DUMP_NAME = "geometry_dump.json"
IR_DUMP_NAME = "ir_dump.json"
TOLERANCE_PX = 0.75
SOURCE_TEXT_TYPES = frozenset({"TextBlock", "RichTextBlock", "IconFont"})
RUNTIME_TEXT_CLASSES = frozenset({"TextBlock", "RichTextBlock", "Design2UMGConstrainedTextBlock"})

# Synthetic carrier wrappers (Design2UMGGeneratedNameSuffixes literals); their
# geometry is generated structure, not source layout.
CARRIER_SUFFIX_RES = (
    re.compile(r"(_fxblur|_geometrymask|_innershadow|_surface|_content|_stroke_top|_stroke_right|_stroke_bottom|_stroke_left)$"),
    re.compile(r"_fill[0-9]+(_img)?$"),
    re.compile(r"_shadow[0-9]+$"),
)

BOX_TYPES = {"HorizontalBox": "w", "VerticalBox": "h"}

CONSTRAINED_TYPE_PREFIX = "Design2UMGConstrained"


def effective_panel_type(widget_class: str) -> str:
    """Fused SizeBox constraints (UDesign2UMGConstrainedHorizontalBox etc.) keep the
    base panel's layout behavior by inheritance; policy dispatch uses the base type."""
    if widget_class.startswith(CONSTRAINED_TYPE_PREFIX):
        return widget_class[len(CONSTRAINED_TYPE_PREFIX):]
    return widget_class

# Widget object names run through FUMGWidgetUtils::SanitizeWidgetName ->
# engine SlugStringForValidName (delete INVALID_OBJECTNAME_CHARACTERS, then the
# plugin adds a fallback name for empty results and a "W" prefix for digit-led
# names). The IR keeps the raw name, so alignment sanitizes IR names the same way.
INVALID_NAME_CHARS = set("\"' ,/.:|&!~\n\r\t@#(){}[]=;^%$`")


def sanitize_widget_name(raw_name: str, fallback_prefix: str = "") -> str:
    sanitized = "".join(char for char in raw_name if char not in INVALID_NAME_CHARS)
    if not sanitized:
        sanitized = f"{fallback_prefix}_Widget"
    if sanitized and sanitized[0].isdigit():
        sanitized = "W" + sanitized
    return sanitized


def load_json_file(path: Path):
    with open(path, "r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def fnum(value) -> float | None:
    if value is None:
        return None
    return float(value)


def expand_ir_nodes(node: dict, path: str, out: list) -> None:
    raw_name = str(node.get("Name") or "")
    widget_class = str(node.get("Type") or "")
    out.append({
        "path": path,
        "name": raw_name,
        "match_name": sanitize_widget_name(raw_name, widget_class),
        "class": widget_class,
        "node": node,
    })
    for index, child in enumerate(node.get("Children") or []):
        if isinstance(child, dict):
            expand_ir_nodes(child, f"{path}/{index}", out)


def slot_of(entry: dict) -> dict:
    return entry["node"].get("Slot") or {}


def style_of(entry: dict) -> dict:
    return entry["node"].get("WidgetStyle") or {}


def is_carrier(entry: dict) -> bool:
    # RetainerBox widgets are always synthesized effect wrappers even when the
    # name carries no suffix; the suffix list covers the remaining carriers.
    if entry["class"] == "RetainerBox":
        return True
    name = entry["name"]
    return any(pattern.search(name) for pattern in CARRIER_SUFFIX_RES)


def has_size_override(entry: dict, dim: str) -> float | None:
    key = "WidthOverride" if dim == "w" else "HeightOverride"
    return fnum(style_of(entry).get(key))


def slot_padding(slot: dict) -> dict:
    padding = slot.get("Padding") or {}
    return {
        "left": fnum(padding.get("Left")) or 0.0,
        "top": fnum(padding.get("Top")) or 0.0,
        "right": fnum(padding.get("Right")) or 0.0,
        "bottom": fnum(padding.get("Bottom")) or 0.0,
    }


def canvas_policy(entry: dict, parent_geo: dict) -> tuple[str, str, dict]:
    """Canvas children: SConstraintCanvas arrange math from IR anchors/offsets."""
    slot = slot_of(entry)
    anchors = slot.get("Anchors") or {}
    anchor_min = anchors.get("Min") or {}
    anchor_max = anchors.get("Max") or {}
    offsets = slot.get("Offsets") or {}
    alignment = slot.get("Alignment") or {}
    parent_w = fnum(parent_geo.get("w")) or 0.0
    parent_h = fnum(parent_geo.get("h")) or 0.0

    def axis(anchor_letter: str, offset_start_key: str, offset_end_key: str, parent_size: float):
        anchor_min_value = fnum(anchor_min.get(anchor_letter)) or 0.0
        anchor_max_value = fnum(anchor_max.get(anchor_letter)) or 0.0
        offset_start = fnum(offsets.get(offset_start_key)) or 0.0
        offset_end = fnum(offsets.get(offset_end_key)) or 0.0
        if anchor_min_value != anchor_max_value:
            position = anchor_min_value * parent_size + offset_start
            size = anchor_max_value * parent_size - position - offset_end
            return position, size, True
        size = offset_end
        align = fnum(alignment.get(anchor_letter)) or 0.0
        position = anchor_min_value * parent_size + offset_start - size * align
        return position, size, False

    if slot.get("AutoSize"):
        # FitContent: desired size decides W/H; alignment/stretch pull desired
        # into the position formula, which IR cannot derive.
        stretch = fnum(anchor_min.get("X")) != fnum(anchor_max.get("X")) or fnum(anchor_min.get("Y")) != fnum(anchor_max.get("Y"))
        nonzero_align = (fnum(alignment.get("X")) or 0.0) != 0.0 or (fnum(alignment.get("Y")) or 0.0) != 0.0
        if stretch or nonzero_align:
            return "exempt", "canvas_autosize_desired_geometry", {}
        pos_x, _, _ = axis("X", "Left", "Right", parent_w)
        pos_y, _, _ = axis("Y", "Top", "Bottom", parent_h)
        return "position_only", "canvas_fit", {"x": pos_x, "y": pos_y}

    pos_x, width, stretch_x = axis("X", "Left", "Right", parent_w)
    pos_y, height, stretch_y = axis("Y", "Top", "Bottom", parent_h)
    expected = {"x": pos_x, "y": pos_y, "w": width, "h": height}
    if stretch_x or stretch_y:
        return "derived", "canvas_anchor_stretch", expected
    return "strict", "canvas_fixed", expected


def box_policy(entry: dict, parent_entry: dict, parent_geo: dict, siblings: list) -> tuple[str, str, dict]:
    """Horizontal/VerticalBox children: SBoxPanel allocation math from IR slots."""
    slot = slot_of(entry)
    horizontal = effective_panel_type(parent_entry["class"]) == "HorizontalBox"
    main_axis = "x" if horizontal else "y"
    main_dim = "w" if horizontal else "h"
    off_dim = "h" if horizontal else "w"
    align_key = "VerticalAlignment" if horizontal else "HorizontalAlignment"
    box_main_size = fnum(parent_geo.get(main_dim)) or 0.0
    box_off_size = fnum(parent_geo.get(off_dim)) or 0.0

    padding = slot_padding(slot)
    padding_main = padding["left"] + padding["right"] if horizontal else padding["top"] + padding["bottom"]
    padding_off = padding["top"] + padding["bottom"] if horizontal else padding["left"] + padding["right"]

    # Cross axis: Fill alignment stretches to the panel content area, anything
    # else keeps the desired size (derivable only from an explicit override).
    expected = {}
    if slot.get(align_key) == "Fill":
        expected[off_dim] = box_off_size - padding_off
    else:
        override = has_size_override(entry, off_dim)
        if override is not None:
            expected[off_dim] = override

    if slot.get("SizeRule") == "Fill":
        occupied = 0.0
        total_weight = 0.0
        derivable = True
        for sibling in siblings:
            sibling_slot = slot_of(sibling)
            sibling_padding = slot_padding(sibling_slot)
            occupied += sibling_padding["left"] + sibling_padding["right"] if horizontal else sibling_padding["top"] + sibling_padding["bottom"]
            if sibling_slot.get("SizeRule") == "Fill":
                total_weight += fnum(sibling_slot.get("FillWeight")) or 1.0
                continue
            override = has_size_override(sibling, main_dim)
            if override is None:
                # Desired size measured at runtime (text, brush-sized images,
                # content-fit containers): the Fill share is not derivable.
                derivable = False
                break
            occupied += override
        if not derivable:
            return "exempt", "box_fill_sibling_desired_runtime", {}
        weight = fnum(slot.get("FillWeight")) or 1.0
        # SBoxPanel clamps available space at zero when the fixed children already
        # overflow the panel (AllowShrink path); the Fill share never goes negative.
        available = max(0.0, box_main_size - occupied)
        expected[main_dim] = available * weight / total_weight
        return "derived_size", "box_fill_allocation", expected

    override = has_size_override(entry, main_dim)
    if override is None:
        return "exempt", "box_auto_desired_runtime", {}
    expected[main_dim] = override
    if not expected:
        return "exempt", "box_auto_desired_runtime", {}
    return "size_only", "box_auto_override", expected


def uniform_grid_policy(entry: dict, parent_entry: dict, parent_geo: dict, siblings: list) -> tuple[str, str, dict]:
    """UniformGridPanel children: equal cell split (SUniformGridPanel math)."""
    slot = slot_of(entry)
    columns = 1
    rows = 1
    for sibling in siblings:
        sibling_slot = slot_of(sibling)
        columns = max(columns, int(fnum(sibling_slot.get("Column")) or 0) + max(1, int(fnum(sibling_slot.get("ColumnSpan")) or 1)))
        rows = max(rows, int(fnum(sibling_slot.get("Row")) or 0) + max(1, int(fnum(sibling_slot.get("RowSpan")) or 1)))
    cell_w = (fnum(parent_geo.get("w")) or 0.0) / columns
    cell_h = (fnum(parent_geo.get("h")) or 0.0) / rows
    panel_padding = slot_padding({"Padding": style_of(parent_entry).get("SlotPadding")})

    expected = {}
    if slot.get("HorizontalAlignment") == "Fill":
        expected["w"] = cell_w - panel_padding["left"] - panel_padding["right"]
    else:
        override = has_size_override(entry, "w")
        if override is not None:
            expected["w"] = override
    if slot.get("VerticalAlignment") == "Fill":
        expected["h"] = cell_h - panel_padding["top"] - panel_padding["bottom"]
    else:
        override = has_size_override(entry, "h")
        if override is not None:
            expected["h"] = override
    if not expected:
        return "exempt", "uniform_grid_desired_runtime", {}
    return "derived_size", "uniform_grid_cell_split", expected


def compute_policy(entry: dict, parent_entry: dict | None, parent_geo: dict | None, siblings: list) -> tuple[str, str, dict]:
    if parent_entry is None:
        bounds = entry["node"].get("Bounds") or {}
        return "strict", "root_window", {
            "x": 0.0,
            "y": 0.0,
            "w": fnum(bounds.get("W")),
            "h": fnum(bounds.get("H")),
        }
    if is_carrier(entry):
        return "exempt", "synthetic_carrier", {}
    parent_class = effective_panel_type(parent_entry["class"])
    if parent_class == "CanvasPanel":
        return canvas_policy(entry, parent_geo or {})
    if parent_class in BOX_TYPES:
        return box_policy(entry, parent_entry, parent_geo or {}, siblings)
    if parent_class == "UniformGridPanel":
        return uniform_grid_policy(entry, parent_entry, parent_geo or {}, siblings)
    if parent_class == "WrapBox":
        return "exempt", "wrap_flow_runtime", {}
    if parent_class == "GridPanel":
        return "exempt", "grid_panel_column_runtime", {}
    return "exempt", f"uncovered_parent_{parent_class or 'unknown'}", {}


def nearest_paired_anchor(geo_node: dict, geo_by_path: dict, paired_geo_paths: set) -> dict | None:
    """Nearest strict ancestor whose path has a golden pair; a node never
    anchors itself. None means no paired ancestor exists above it, so the
    caller compares in absolute coordinates."""
    path = geo_node["path"].rpartition("/")[0]
    while path:
        if path in paired_geo_paths:
            return geo_by_path[path]
        path = path.rpartition("/")[0]
    return None


def anchored_runtime(geo_node: dict, anchor: dict | None) -> dict:
    """x/y relative to the anchor's absolute origin (absolute when the anchor
    is None); w/h are parent-independent and pass straight through."""
    origin_x = fnum(anchor.get("abs_x")) if anchor is not None else 0.0
    origin_y = fnum(anchor.get("abs_y")) if anchor is not None else 0.0
    abs_x = fnum(geo_node.get("abs_x"))
    abs_y = fnum(geo_node.get("abs_y"))
    return {
        "x": None if abs_x is None or origin_x is None else abs_x - origin_x,
        "y": None if abs_y is None or origin_y is None else abs_y - origin_y,
        "w": fnum(geo_node.get("w")),
        "h": fnum(geo_node.get("h")),
    }


def axis_diffs(name_path: str, policy: str, expected: dict, actual: dict, out: list) -> None:
    for axis in ("x", "y", "w", "h"):
        expected_value = expected.get(axis)
        if expected_value is None:
            continue
        actual_value = actual.get(axis)
        if actual_value is None:
            out.append(f"{name_path}: expected {axis}={expected_value:.6g} runtime {axis}=<null> [policy={policy}]")
            continue
        delta = abs(expected_value - actual_value)
        if delta > TOLERANCE_PX:
            out.append(
                f"{name_path}: expected {axis}={expected_value:.6g} runtime {axis}={actual_value:.6g}"
                f" delta={delta:.6g} [policy={policy}]"
            )


def name_path_for(entry: dict, ir_by_path: dict) -> str:
    segments = []
    path = entry["path"]
    while path:
        node = ir_by_path.get(path)
        if node is None:
            break
        segments.append(node["name"] or node["class"] or path.rpartition("/")[2])
        path = path.rpartition("/")[0]
    return "/".join(reversed(segments))


def evaluate_case(ir_doc: dict, geo_doc: dict, golden_doc: dict | None) -> dict:
    ir_nodes: list[dict] = []
    expand_ir_nodes(ir_doc.get("RootWidget") or {}, "0", ir_nodes)
    ir_by_path = {node["path"]: node for node in ir_nodes}
    ir_children_by_path: dict[str, list] = {}
    for node in ir_nodes:
        parent_path = node["path"].rpartition("/")[0]
        ir_children_by_path.setdefault(parent_path, []).append(node)

    geo_nodes = [node for node in geo_doc.get("nodes") or [] if isinstance(node, dict)]
    # Runtime geometry derivation needs a path (parent-origin lookup), so only
    # pathful nodes are comparable; the correspondence input must be this exact
    # list because its asset indices point back into it.
    geo_matchable = [node for node in geo_nodes if node.get("path")]
    geo_by_path = {node.get("path"): node for node in geo_matchable}

    def geo_structure_inputs(nodes: list) -> dict:
        return {"structure_nodes": [
            {
                "path": node.get("path"),
                "name": str(node.get("name") or ""),
                "kind": "text" if str(node.get("class") or "") in RUNTIME_TEXT_CLASSES else "",
                "class": str(node.get("class") or ""),
            }
            for node in nodes
        ]}

    # Node pairing reuses the structure-snapshot correspondence (name + name_N
    # + kind fallback), so wrapper-inserted path migration keeps nodes paired;
    # unpaired nodes are alignment visibility, not per-node geometry FAILs.
    correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": [
            {
                "path": node["path"],
                "name": node["match_name"],
                "kind": "text" if node["class"] in SOURCE_TEXT_TYPES else "",
                "type": node["class"],
                "class": node["class"],
            }
            for node in ir_nodes
        ]},
        geo_structure_inputs(geo_matchable),
    )
    pairs = correspondence.get("source_to_asset_indices") or []
    matched = [(ir_nodes[source_index], geo_matchable[asset_index]) for source_index, asset_index in pairs]
    geo_by_ir_path = {entry["path"]: geo_node for entry, geo_node in matched}
    matched_ir_indices = {source_index for source_index, _ in pairs}
    matched_geo_indices = {asset_index for _, asset_index in pairs}
    ir_unmatched = [node for index, node in enumerate(ir_nodes) if index not in matched_ir_indices]
    geo_unmatched_paths = sorted(
        str(node.get("path")) for index, node in enumerate(geo_matchable) if index not in matched_geo_indices
    )
    alignment_low_confidence = bool(ir_nodes) and len(matched) < 0.6 * len(ir_nodes)

    fidelity_diffs: list[str] = []
    stability_diffs: list[str] = []
    policy_counts: Counter = Counter()
    uncovered_forms: Counter = Counter()
    comparable_nodes = 0
    golden_uncovered_count = 0

    golden_by_geo_path = {}
    golden_unmatched = []
    if golden_doc is not None:
        golden_nodes = [node for node in golden_doc.get("nodes") or [] if isinstance(node, dict) and node.get("path")]
        # Golden lookup runs the same tree correspondence (golden as source,
        # current dump as asset): a wrapper-migrated node keeps its frozen
        # counterpart instead of path-matching into the wrapper placeholder.
        golden_correspondence = verify.compute_tree_correspondence(
            geo_structure_inputs(golden_nodes),
            geo_structure_inputs(geo_matchable),
        )
        matched_golden_indices = set()
        for source_index, asset_index in golden_correspondence.get("source_to_asset_indices") or []:
            golden_by_geo_path[geo_matchable[asset_index].get("path")] = golden_nodes[source_index]
            matched_golden_indices.add(source_index)
        golden_unmatched = [node for index, node in enumerate(golden_nodes) if index not in matched_golden_indices]

    for entry, geo_node in matched:
        parent_path = entry["path"].rpartition("/")[0]
        parent_entry = ir_by_path.get(parent_path) if parent_path else None
        siblings = ir_children_by_path.get(parent_path, [])
        # Fidelity and the layout policies share one frame: the expectation is
        # derived in the IR parent's coordinates (slot anchors/offsets against
        # the IR parent), so the runtime side must anchor on the IR parent's
        # runtime-paired node. A wrapper the runtime inserts between the IR
        # parent and this node leaves absolute geometry unchanged and is
        # visibility in the alignment block (geo_unmatched), not a fidelity
        # diff. When the IR parent itself did not pair (pathological edge),
        # fall back to the immediate runtime parent (pre-existing behavior);
        # the root node (no IR parent) stays None / absolute.
        parent_geo = geo_by_ir_path.get(parent_path)
        if parent_geo is None:
            geo_parent_path = str(geo_node.get("path")).rpartition("/")[0]
            parent_geo = geo_by_path.get(geo_parent_path) if geo_parent_path else None
        if not bool(geo_node.get("geometry_valid")):
            policy, detail = "exempt", "geometry_invalid"
            expected = {}
        else:
            policy, detail, expected = compute_policy(entry, parent_entry, parent_geo, siblings)
        policy_counts[policy] += 1
        if not expected:
            uncovered_forms[detail] += 1

        display_path = name_path_for(entry, ir_by_path)
        runtime = anchored_runtime(geo_node, parent_geo)
        axis_diffs(display_path, policy, expected, runtime, fidelity_diffs)

        if not expected:
            continue
        comparable_nodes += 1
        if golden_doc is None:
            continue
        golden_node = golden_by_geo_path.get(geo_node.get("path"))
        if golden_node is None:
            # Node added after the freeze: visible via the counter, not a FAIL.
            golden_uncovered_count += 1
            continue
        # Stability anchors both sides on the nearest golden-paired ancestor:
        # a wrapper inserted above the node changes its local parent origin,
        # not its absolute geometry, and must not read as drift. Fidelity
        # above anchors on the IR parent's runtime pair for the same reason
        # (the expectation itself is derived in IR-parent coordinates).
        anchor = nearest_paired_anchor(geo_node, geo_by_path, golden_by_geo_path)
        golden_anchor = golden_by_geo_path[anchor["path"]] if anchor is not None else None
        golden_runtime = anchored_runtime(golden_node, golden_anchor)
        axis_diffs(display_path, policy, expected_for_stability(expected, golden_runtime), anchored_runtime(geo_node, anchor), stability_diffs)

    for node in ir_unmatched:
        fidelity_diffs.append(f"{name_path_for(node, ir_by_path)}: unmatched IR node (no runtime counterpart)")

    if golden_doc is not None:
        for node in golden_unmatched:
            stability_diffs.append(f"{node.get('name') or node.get('path')}: golden node absent from current dump")

    exempt_count = len(matched) - comparable_nodes
    return {
        "fidelity": {
            "compared_nodes": comparable_nodes,
            "failed_nodes": len(fidelity_diffs),
            "diffs": fidelity_diffs,
        },
        "stability": None if golden_doc is None else {
            "compared_nodes": comparable_nodes,
            "failed_nodes": len(stability_diffs),
            "diffs": stability_diffs,
            "golden_uncovered_count": golden_uncovered_count,
        },
        "alignment": {
            "ir_nodes": len(ir_nodes),
            "geo_nodes": len(geo_nodes),
            "matched_nodes": len(matched),
            "matched_ratio": round(len(matched) / len(ir_nodes), 4) if ir_nodes else 0.0,
            "low_confidence": alignment_low_confidence,
            "correspondence_matched": correspondence.get("matched_source_nodes", 0),
            "ir_unmatched_count": len(ir_unmatched),
            "ir_unmatched_samples": [name_path_for(node, ir_by_path) for node in ir_unmatched[:10]],
            "geo_unmatched_count": len(geo_unmatched_paths),
            "geo_unmatched_samples": geo_unmatched_paths[:10],
        },
        "policy_counts": dict(policy_counts),
        "exempt_count": exempt_count,
        "comparable_ratio": round(comparable_nodes / len(matched), 4) if matched else 0.0,
        "low_coverage": bool(matched) and exempt_count > comparable_nodes,
        "uncovered_forms": dict(uncovered_forms),
    }


def expected_for_stability(expected: dict, golden_runtime: dict) -> dict:
    """Stability compares golden vs current runtime on the fidelity-comparable axes."""
    return {axis: golden_runtime.get(axis) for axis in expected}


def compare_case(golden_root: Path | None, case_name: str, case_dir: Path) -> dict:
    result = {
        "case": case_name,
        "dump_path": str(case_dir / DUMP_NAME),
        "golden_path": str(golden_root / case_name / DUMP_NAME) if golden_root else "",
        "available": False,
        "reason": "",
        "passed": None,
        "fidelity": None,
        "stability": None,
        "alignment": None,
        "policy_counts": {},
        "exempt_count": 0,
        "comparable_ratio": 0.0,
        "low_coverage": False,
        "uncovered_forms": {},
    }
    dump_path = case_dir / DUMP_NAME
    ir_path = case_dir / IR_DUMP_NAME
    if not dump_path.is_file():
        result["reason"] = "dump_missing"
        return result
    if not ir_path.is_file():
        result["reason"] = "ir_dump_missing"
        return result
    golden_path = golden_root / case_name / DUMP_NAME if golden_root else None
    golden_doc = None
    if golden_path is None or not golden_path.is_file():
        result["reason"] = "golden_missing"
    else:
        try:
            golden_doc = load_json_file(golden_path)
        except (OSError, json.JSONDecodeError) as error:
            result["reason"] = f"golden_read_error: {error}"
            return result
    try:
        ir_doc = load_json_file(ir_path)
        geo_doc = load_json_file(dump_path)
    except (OSError, json.JSONDecodeError) as error:
        result["reason"] = f"read_error: {error}"
        return result

    try:
        evaluation = evaluate_case(ir_doc, geo_doc, golden_doc)
    except (KeyError, TypeError, ValueError) as error:
        result["reason"] = f"evaluate_error: {error}"
        return result
    result.update(evaluation)
    if golden_doc is not None:
        # Verdict is the regression signal (golden vs current runtime). Fidelity
        # diffs are the layer's fidelity record: they expose layout translations
        # that have been wrong all along and are reported, but they do not fail
        # the case on their own (that is what the frozen golden is for).
        result["available"] = True
        result["passed"] = evaluation["stability"]["failed_nodes"] == 0
    return result


def write_case_report(case_dir: Path, result: dict) -> Path:
    report_path = case_dir / REPORT_NAME
    case_dir.mkdir(parents=True, exist_ok=True)
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
    return report_path


def annotate_batch_case_result(case_result: dict, golden_root: str, case_name: str, case_dir: Path) -> None:
    """Batch integration hook: compare one case, write its report, attach summary fields."""
    result = compare_case(Path(golden_root), case_name, case_dir)
    write_case_report(case_dir, result)
    case_result["geometry_golden_available"] = result["available"]
    case_result["geometry_golden_passed"] = result["passed"] if result["available"] else None
    case_result["geometry_golden_reason"] = result["reason"]
    case_result["geometry_golden_fidelity_failed_nodes"] = (result["fidelity"] or {}).get("failed_nodes")
    case_result["geometry_golden_stability_failed_nodes"] = (result["stability"] or {}).get("failed_nodes") if result["stability"] else None
    case_result["geometry_golden_comparable_ratio"] = result["comparable_ratio"]
    case_result["geometry_golden_low_coverage"] = result["low_coverage"]


def discover_dump_cases(golden_root: Path, run_root: Path, include_regex: str) -> list[tuple[str, Path]]:
    if (run_root / DUMP_NAME).is_file():
        return [(run_root.name, run_root)]
    # Directory mode enumerates the union of golden and run case dirs: a run case
    # whose dump is missing shows up as unavailable instead of vanishing.
    case_names = sorted(
        {entry.name for entry in golden_root.iterdir() if entry.is_dir()}
        | {entry.name for entry in run_root.iterdir() if entry.is_dir()}
    )
    if include_regex:
        case_names = [name for name in case_names if re.search(include_regex, name)]
    return [(name, run_root / name) for name in case_names]


def print_result(result: dict, max_diffs: int) -> str:
    if result["available"]:
        status = "PASS" if result["passed"] else "FAIL"
    else:
        status = "UNAVAILABLE"
    lines = [f"[GEOMETRY-GOLDEN] {status} {result['case']}"]
    if not result["available"]:
        lines.append(f"    reason={result['reason']}")
    fidelity = result.get("fidelity")
    if fidelity:
        lines.append(
            f"    fidelity: compared={fidelity['compared_nodes']} failed={fidelity['failed_nodes']}"
            f" comparable_ratio={result['comparable_ratio']}"
            f" low_coverage={result['low_coverage']}"
        )
        for line in fidelity["diffs"][:max_diffs]:
            lines.append(f"    {line}")
        if fidelity["failed_nodes"] > max_diffs:
            lines.append(f"    ... {fidelity['failed_nodes'] - max_diffs} more fidelity diffs suppressed")
    stability = result.get("stability")
    if stability:
        lines.append(f"    stability: compared={stability['compared_nodes']} failed={stability['failed_nodes']}")
        for line in stability["diffs"][:max_diffs]:
            lines.append(f"    {line}")
        if stability["failed_nodes"] > max_diffs:
            lines.append(f"    ... {stability['failed_nodes'] - max_diffs} more stability diffs suppressed")
    return "\n".join(lines)


def compare_run(golden_root: Path, run_root: Path, max_diffs: int, write_reports: bool, include_regex: str = "") -> int:
    cases = discover_dump_cases(golden_root, run_root, include_regex)
    has_run_dump = (run_root / DUMP_NAME).is_file() or any(
        (entry / DUMP_NAME).is_file() for entry in run_root.iterdir() if entry.is_dir()
    )
    if not has_run_dump or not cases:
        print(
            f"[GEOMETRY-GOLDEN] config error: no {DUMP_NAME} found under {run_root}"
            " -- the geometry golden layer produced nothing for the whole run"
            " (cvar design2umgAutomation.GeometryDump not enabled, or --include matched no case)",
            file=sys.stderr,
        )
        return 2
    failed = 0
    available = 0
    for case_name, case_dir in cases:
        result = compare_case(golden_root, case_name, case_dir)
        if result["available"]:
            available += 1
            if not result["passed"]:
                failed += 1
        print(print_result(result, max_diffs))
        # Unavailable cases keep alignment=None (dict.get's default only fires on a
        # missing key, not a None value), so coerce before reading the block.
        alignment = result.get("alignment") or {}
        if alignment.get("low_confidence"):
            print(f"    alignment_low_confidence: matched {alignment['matched_nodes']}/{alignment['ir_nodes']} IR nodes")
        if write_reports:
            print(f"    report={write_case_report(case_dir, result)}")
    print(
        f"[GEOMETRY-GOLDEN] summary: cases={len(cases)} available={available}"
        f" passed={available - failed} failed={failed} unavailable={len(cases) - available}"
    )
    return 1 if failed else 0


def compare_files(golden_file: Path, dump_dir: Path, max_diffs: int) -> int:
    case_name = dump_dir.name
    if not (dump_dir / DUMP_NAME).is_file() or not (dump_dir / IR_DUMP_NAME).is_file():
        print(
            f"[GEOMETRY-GOLDEN] config error: missing {DUMP_NAME} or {IR_DUMP_NAME} under {dump_dir}",
            file=sys.stderr,
        )
        return 2
    try:
        golden_doc = load_json_file(golden_file)
    except (OSError, json.JSONDecodeError) as error:
        print(f"[GEOMETRY-GOLDEN] config error: unreadable golden ({error})", file=sys.stderr)
        return 2
    result = compare_case(None, case_name, dump_dir)
    if result["reason"] not in ("", "golden_missing"):
        print(f"[GEOMETRY-GOLDEN] config error: {result['reason']}", file=sys.stderr)
        return 2
    try:
        ir_doc = load_json_file(dump_dir / IR_DUMP_NAME)
        geo_doc = load_json_file(dump_dir / DUMP_NAME)
        evaluation = evaluate_case(ir_doc, geo_doc, golden_doc)
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
        print(f"[GEOMETRY-GOLDEN] config error: read/evaluate failed ({error})", file=sys.stderr)
        return 2
    result.update(evaluation)
    result["available"] = True
    result["passed"] = evaluation["stability"]["failed_nodes"] == 0
    print(print_result(result, max_diffs))
    return 0 if result["passed"] else 1


def parse_args():
    parser = argparse.ArgumentParser(description="Design2UMG geometry golden comparison (validation layer 2)")
    parser.add_argument("golden", help="Golden geometry_dump.json file, or golden case root directory")
    parser.add_argument("dump", help="Current geometry_dump.json case directory, or run root directory")
    parser.add_argument("--max-diffs", type=int, default=50, help="Max printed diffs per comparison (default 50)")
    parser.add_argument("--no-reports", action="store_true", help="Do not write geometry_golden_report.json into run case dirs")
    parser.add_argument("--include", default="", help="Only compare case names matching this regex (directory mode)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    golden = Path(args.golden)
    dump = Path(args.dump)
    if not golden.exists():
        print(f"[GEOMETRY-GOLDEN] golden path not found: {golden}", file=sys.stderr)
        return 2
    if not dump.exists():
        print(f"[GEOMETRY-GOLDEN] dump path not found: {dump}", file=sys.stderr)
        return 2
    if golden.is_file():
        return compare_files(golden, dump if dump.is_dir() else dump.parent, args.max_diffs)
    if golden.is_dir() and dump.is_dir():
        return compare_run(golden, dump, args.max_diffs, not args.no_reports, args.include)
    print("[GEOMETRY-GOLDEN] golden and dump must both be files/directories in a compatible combination", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
