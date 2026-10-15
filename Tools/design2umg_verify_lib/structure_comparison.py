"""Pure structure comparison, independent of editor, image and I/O services."""

import math
import re
from collections import Counter


def normalize_structure_text(value: str) -> str:
    if not isinstance(value, str):
        return ""
    normalized = re.sub(r"\s+", " ", value.replace("\r", " ").replace("\n", " ").replace("\t", " ")).strip()
    return normalized


def normalize_structure_texts(values) -> list[str]:
    normalized = []
    for value in values or []:
        text = normalize_structure_text(value)
        if text:
            normalized.append(text)
    normalized.sort()
    return normalized


def get_result_structure(step_result: dict | None, field: str) -> dict | None:
    if not isinstance(step_result, dict):
        return None
    ue_result = step_result.get("ue_result", step_result)
    if not isinstance(ue_result, dict):
        return None
    snapshot = ue_result.get(field)
    return snapshot if isinstance(snapshot, dict) else None


def compare_structure_snapshots(left: dict | None, right: dict | None) -> dict | None:
    if not isinstance(left, dict) or not isinstance(right, dict):
        return None
    left_texts = normalize_structure_texts(left.get("visible_texts"))
    right_texts = normalize_structure_texts(right.get("visible_texts"))
    left_kind_histogram = normalize_structure_histogram(left.get("kind_histogram"))
    right_kind_histogram = normalize_structure_histogram(right.get("kind_histogram"))
    left_class_histogram = normalize_structure_histogram(left.get("class_histogram"))
    right_class_histogram = normalize_structure_histogram(right.get("class_histogram"))
    return {
        "available": True,
        "texts_match": left_texts == right_texts,
        "left_total_nodes": int(left.get("total_nodes") or 0),
        "right_total_nodes": int(right.get("total_nodes") or 0),
        "left_leaf_nodes": int(left.get("leaf_nodes") or 0),
        "right_leaf_nodes": int(right.get("leaf_nodes") or 0),
        "left_container_nodes": int(left.get("container_nodes") or 0),
        "right_container_nodes": int(right.get("container_nodes") or 0),
        "left_text_nodes": int(left.get("text_nodes") or 0),
        "right_text_nodes": int(right.get("text_nodes") or 0),
        "left_visual_nodes": int(left.get("visual_nodes") or 0),
        "right_visual_nodes": int(right.get("visual_nodes") or 0),
        "kind_histograms_match": left_kind_histogram == right_kind_histogram,
        "class_histograms_match": left_class_histogram == right_class_histogram,
    }


def normalize_structure_histogram(value) -> dict[str, int]:
    if not isinstance(value, dict):
        return {}
    normalized = {}
    for key, count in value.items():
        normalized_key = str(key or "")
        if not normalized_key:
            continue
        try:
            normalized[normalized_key] = int(count or 0)
        except (TypeError, ValueError):
            normalized[normalized_key] = 0
    return normalized


def structure_count_floor(source_count: int, ratio: float, minimum: int) -> int:
    if source_count <= 1:
        return 1
    return max(minimum, int(math.ceil(source_count * ratio)))


def structure_comparison_counts_match(comparison: dict | None) -> bool:
    if not isinstance(comparison, dict) or not comparison.get("available"):
        return False
    fields = ("total_nodes", "leaf_nodes", "container_nodes", "text_nodes", "visual_nodes")
    for field in fields:
        if comparison.get(f"left_{field}") != comparison.get(f"right_{field}"):
            return False
    return bool(
        comparison.get("texts_match")
        and comparison.get("kind_histograms_match")
        and comparison.get("class_histograms_match")
    )


def compute_structure_correspondence(source_snapshot, asset_snapshot) -> dict:
    """Match source node names against widget object names: real structure
    coverage (did every source node get a widget?) vs synthetic carrier
    widgets (wrappers UMG needs but the source never declared)."""
    source_names = [name for name in (source_snapshot or {}).get("node_names") or [] if name]
    asset_names = [name for name in (asset_snapshot or {}).get("node_names") or [] if name]
    if not source_names or not asset_names:
        return {}

    asset_counter = Counter(asset_names)
    exact_unmatched = []
    matched_sources = 0
    for name in source_names:
        if asset_counter.get(name, 0) > 0:
            asset_counter[name] -= 1
            matched_sources += 1
        else:
            exact_unmatched.append(name)

    still_unmatched = []
    for name in exact_unmatched:
        # Widget names carry converter suffixes (content, shadow, slot wraps,
        # uniqueness counters); a prefixed widget still corresponds to the node.
        prefix = name + "_"
        match = next((widget for widget in asset_counter if asset_counter[widget] > 0 and widget.startswith(prefix)), None)
        if match is not None:
            asset_counter[match] -= 1
            matched_sources += 1
        else:
            still_unmatched.append(name)

    consumed_widgets = len(asset_names) - sum(asset_counter.values())
    carrier_widgets = len(asset_names) - consumed_widgets
    return {
        "source_named_nodes": len(source_names),
        "matched_source_nodes": matched_sources,
        "coverage_ratio": round(matched_sources / len(source_names), 4),
        "carrier_widgets": carrier_widgets,
        "carrier_overhead_ratio": round(carrier_widgets / len(source_names), 4),
        "unmatched_source_names": still_unmatched[:20],
    }


def compute_tree_correspondence(source_snapshot: dict | None, asset_snapshot: dict | None) -> dict:
    """Compare source/asset named trees while ignoring synthetic carrier nodes.

    The snapshots keep every traversed node in ``structure_nodes`` with a
    numeric path. Source nodes are matched in source-tree order. Exact names
    are preferred, then the converter's numeric uniqueness suffix (for example
    ``Card`` -> ``Card_1``). A candidate must live below the already matched
    source parent, while synthetic carriers are skipped when walking upward.
    This prevents repeated names such as ``Base``/``Line`` from being attached
    to a different branch by a global name-only greedy match.

    ``source_to_asset_indices`` exposes the per-node pairing as
    ``[[source_index, asset_index], ...]`` in ascending source order; the
    indices point into the caller-supplied (dict-only) ``structure_nodes``
    lists, so callers that need the node-level mapping use it.
    """
    source_nodes = [node for node in (source_snapshot or {}).get("structure_nodes") or [] if isinstance(node, dict)]
    asset_nodes = [node for node in (asset_snapshot or {}).get("structure_nodes") or [] if isinstance(node, dict)]
    result = {
        "available": bool(source_nodes and asset_nodes),
        "source_nodes": len(source_nodes),
        "asset_nodes": len(asset_nodes),
        "matched_source_nodes": 0,
        "unmatched_source_names": [],
        "named_parent_path_matches": 0,
        "named_parent_path_compared": 0,
        "sibling_order_matches": 0,
        "sibling_order_compared": 0,
        "kind_matches": 0,
        "kind_compared": 0,
        "kind_mismatch_pairs": {},
        "identity_matches": 0,
        "identity_compared": 0,
        "identity_ratio": 0.0,
        "semantic_kind_matches": 0,
        "semantic_kind_compared": 0,
        "semantic_kind_ratio": 0.0,
        "kind_mismatch_classes": {},
        "kind_mismatch_details": [],
    }
    if not result["available"]:
        return result

    def path_parts(node: dict) -> tuple[int, ...]:
        raw_path = str(node.get("path") or "")
        parts = []
        for part in raw_path.split("/"):
            try:
                parts.append(int(part))
            except (TypeError, ValueError):
                return ()
        return tuple(parts)

    def parent_path(path: tuple[int, ...]) -> tuple[int, ...] | None:
        return path[:-1] if path else None

    def node_name(node: dict) -> str:
        return str(node.get("name") or "")

    def node_kind(node: dict) -> str:
        return str(node.get("kind") or "")

    def node_id(node: dict) -> str:
        return str(node.get("id") or "")

    source_paths = [path_parts(node) for node in source_nodes]
    asset_paths = [path_parts(node) for node in asset_nodes]
    asset_index_by_path = {path: index for index, path in enumerate(asset_paths) if path}
    asset_by_name: dict[str, list[int]] = {}
    asset_by_id: dict[str, list[int]] = {}
    for index, node in enumerate(asset_nodes):
        name = node_name(node)
        if name:
            asset_by_name.setdefault(name, []).append(index)
        identity = node_id(node)
        if identity:
            asset_by_id.setdefault(identity, []).append(index)

    def node_class(node: dict) -> str:
        return str(node.get("class") or "")

    def nearest_matched_asset_ancestor(asset_index: int, asset_to_source: dict[int, int]) -> int | None:
        path = asset_paths[asset_index]
        while path:
            path = parent_path(path)
            if path is None:
                return None
            candidate_index = asset_index_by_path.get(path)
            if candidate_index is not None and candidate_index in asset_to_source:
                return candidate_index
        return None

    def generated_name_candidates(source_name: str) -> list[int]:
        if not source_name:
            return []
        generated_name_pattern = re.compile(rf"^{re.escape(source_name)}_[0-9]+$")
        return [
            index for index, asset_node in enumerate(asset_nodes)
            if generated_name_pattern.fullmatch(node_name(asset_node))
        ]

    def fallback_name_candidates(source_node: dict, used_indices: set[int]) -> list[int]:
        # Unreal's widget-tree builder treats the literal Figma placeholder
        # name ``None`` as an empty name and allocates a type-based name. Keep
        # this normalization in the audit instead of counting valid generated
        # names (TextBlock_0/CanvasPanel_0) as missing source nodes.
        if node_name(source_node) not in {"", "None"}:
            return []
        kind = node_kind(source_node)
        if kind == "text":
            prefixes = ["TextBlock"]
        elif kind == "container":
            prefixes = ["CanvasPanel", "HorizontalBox", "VerticalBox", "WrapBox", "Border", "Overlay"]
        elif kind == "visual":
            prefixes = ["Image", "Border"]
        else:
            prefixes = []
        result: list[int] = []
        for prefix in prefixes:
            result.extend(index for index in asset_by_name.get(prefix, []) if index not in used_indices)
            result.extend(index for index in generated_name_candidates(prefix) if index not in used_indices)
        return result

    def typed_projection_candidates(source_node: dict, used_indices: set[int]) -> list[int]:
        # Icon-font nodes retain their source role as text, but the runtime
        # widget name can come from the resolved glyph mapping (for example
        # ``IconFont_Widget`` -> ``uJzgY``). This fallback supports structure
        # auditing only; type and path alone do not establish node identity.
        if str(source_node.get("type") or "") != "IconFont":
            return []
        return [
            index for index, asset_node in enumerate(asset_nodes)
            if node_kind(asset_node) == "text" and index not in used_indices
        ]

    used_asset_indices: set[int] = set()
    source_to_asset: dict[int, int] = {}
    source_match_methods: dict[int, str] = {}
    unmatched_names: list[str] = []
    source_index_by_path = {path: index for index, path in enumerate(source_paths) if path}
    for source_index, source_node in enumerate(source_nodes):
        source_name = node_name(source_node)
        source_parent_path = parent_path(source_paths[source_index])
        source_parent_index = source_index_by_path.get(source_parent_path) if source_parent_path is not None else None
        expected_parent_asset = source_to_asset.get(source_parent_index) if source_parent_index is not None else None

        source_identity = node_id(source_node)
        identity_candidates = [
            index for index in asset_by_id.get(source_identity, [])
            if index not in used_asset_indices
        ] if source_identity else []
        name_candidates = [
            index for index in asset_by_name.get(source_name, [])
            if index not in used_asset_indices
        ]
        generated_candidates = [
            index for index in generated_name_candidates(source_name)
            if index not in used_asset_indices
        ]
        fallback_candidates = [
            index for index in fallback_name_candidates(source_node, used_asset_indices)
            if node_kind(asset_nodes[index]) == node_kind(source_node)
        ]
        typed_candidates = typed_projection_candidates(source_node, used_asset_indices)
        candidate_groups = [
            ("id", identity_candidates),
            ("name", name_candidates),
            ("generated_name", generated_candidates),
            ("fallback_name", fallback_candidates),
            ("typed_projection", typed_candidates),
        ]

        candidates: list[int] = []
        candidate_methods: dict[int, str] = {}
        for method, group in candidate_groups:
            for index in group:
                if index not in candidates:
                    candidates.append(index)
                    candidate_methods[index] = method

        if expected_parent_asset is not None and not identity_candidates:
            parent_candidates = [
                index for index in candidates
                if nearest_matched_asset_ancestor(
                    index,
                    {asset_index: source for source, asset_index in source_to_asset.items()},
                ) == expected_parent_asset
            ]
            if parent_candidates:
                candidates = parent_candidates

        if not candidates:
            if source_name:
                unmatched_names.append(source_name)
            continue

        source_kind = node_kind(source_node)
        same_kind = [index for index in candidates if node_kind(asset_nodes[index]) == source_kind]
        chosen = (same_kind or candidates)[0]
        used_asset_indices.add(chosen)
        source_to_asset[source_index] = chosen
        source_match_methods[source_index] = candidate_methods[chosen]

    result["matched_source_nodes"] = len(source_to_asset)
    result["unmatched_source_names"] = unmatched_names[:20]

    asset_to_source = {asset_index: source_index for source_index, asset_index in source_to_asset.items()}
    source_to_asset_matches = []
    for source_index, asset_index in source_to_asset.items():
        source_parent_path = parent_path(source_paths[source_index])
        source_parent_index = source_index_by_path.get(source_parent_path) if source_parent_path is not None else None
        asset_parent_index = nearest_matched_asset_ancestor(asset_index, asset_to_source)
        asset_parent_source_index = asset_to_source.get(asset_parent_index) if asset_parent_index is not None else None
        source_to_asset_matches.append({
            "source_index": source_index,
            "asset_index": asset_index,
            "method": source_match_methods[source_index],
            "parent_constrained": source_parent_index is not None
            and source_parent_index == asset_parent_source_index,
        })
        result["named_parent_path_compared"] += 1
        if source_parent_index == asset_parent_source_index:
            result["named_parent_path_matches"] += 1
        result["kind_compared"] += 1
        if node_kind(source_nodes[source_index]) == node_kind(asset_nodes[asset_index]):
            result["kind_matches"] += 1
        else:
            mismatch_key = f"{node_kind(source_nodes[source_index])}->{node_kind(asset_nodes[asset_index])}"
            result["kind_mismatch_pairs"][mismatch_key] = result["kind_mismatch_pairs"].get(mismatch_key, 0) + 1

        source_identity = node_id(source_nodes[source_index])
        asset_identity = node_id(asset_nodes[asset_index])
        if source_identity and asset_identity:
            result["identity_compared"] += 1
            if source_identity == asset_identity:
                result["identity_matches"] += 1

        source_kind = node_kind(source_nodes[source_index])
        asset_kind = node_kind(asset_nodes[asset_index])
        asset_widget_class = node_class(asset_nodes[asset_index])
        mismatch_class = "exact"
        if source_kind != asset_kind:
            if source_kind == "placeholder":
                mismatch_class = "placeholder_surface_projection"
            elif source_kind == "visual" and asset_kind == "container" and "overlay" in asset_widget_class.lower():
                mismatch_class = "visual_composite_carrier"
            elif source_kind == "visual" and asset_kind == "container" and asset_widget_class.lower() in {
                "canvaspanel",
                "retainerbox",
                "border",
                "design2umgconstrainedborder",
            }:
                mismatch_class = "visual_surface_carrier"
            elif source_kind == "container" and asset_kind == "visual" and asset_widget_class.lower() in {
                "border",
                "design2umgconstrainedborder",
                "image",
            }:
                mismatch_class = "container_surface_projection"
            else:
                mismatch_class = "unclassified"
        result["semantic_kind_compared"] += 1
        result["kind_mismatch_classes"][mismatch_class] = result["kind_mismatch_classes"].get(mismatch_class, 0) + 1
        if mismatch_class != "exact" and len(result["kind_mismatch_details"]) < 32:
            result["kind_mismatch_details"].append({
                "source_path": source_nodes[source_index].get("path"),
                "source_name": source_nodes[source_index].get("name"),
                "source_kind": source_kind,
                "source_type": source_nodes[source_index].get("type"),
                "asset_path": asset_nodes[asset_index].get("path"),
                "asset_name": asset_nodes[asset_index].get("name"),
                "asset_kind": asset_kind,
                "asset_class": asset_widget_class,
                "class": mismatch_class,
            })
        if mismatch_class != "unclassified":
            result["semantic_kind_matches"] += 1

    def named_siblings(source_parent_index: int | None, mapping: dict[int, int]) -> list[int]:
        siblings = []
        for source_index in mapping:
            parent_path_value = parent_path(source_paths[source_index])
            expected_parent_path = source_paths[source_parent_index] if source_parent_index is not None else None
            if parent_path_value == expected_parent_path:
                siblings.append(source_index)
        return sorted(siblings, key=lambda index: source_paths[index])

    source_parent_groups: dict[tuple[int, ...] | None, list[int]] = {}
    for source_index in source_to_asset:
        source_parent_groups.setdefault(parent_path(source_paths[source_index]), []).append(source_index)
    for source_parent_path, source_children in source_parent_groups.items():
        source_children.sort(key=lambda index: source_paths[index])
        matched_children = [source_index for source_index in source_children if source_index in source_to_asset]
        if len(matched_children) <= 1:
            continue

        expected_order = [source_to_asset[source_index] for source_index in matched_children]
        asset_children = sorted(expected_order, key=lambda index: asset_paths[index])
        for expected_asset_index, actual_asset_index in zip(expected_order, asset_children):
            result["sibling_order_compared"] += 1
            if expected_asset_index == actual_asset_index:
                result["sibling_order_matches"] += 1

    result["named_parent_path_ratio"] = round(
        result["named_parent_path_matches"] / result["named_parent_path_compared"], 4
    ) if result["named_parent_path_compared"] else 0.0
    result["sibling_order_ratio"] = round(
        result["sibling_order_matches"] / result["sibling_order_compared"], 4
    ) if result["sibling_order_compared"] else 1.0
    result["kind_ratio"] = round(result["kind_matches"] / result["kind_compared"], 4) if result["kind_compared"] else 0.0
    result["identity_ratio"] = round(
        result["identity_matches"] / result["identity_compared"], 4
    ) if result["identity_compared"] else 0.0
    result["semantic_kind_ratio"] = round(
        result["semantic_kind_matches"] / result["semantic_kind_compared"], 4
    ) if result["semantic_kind_compared"] else 0.0
    result["source_to_asset_indices"] = [
        [source_index, asset_index] for source_index, asset_index in sorted(source_to_asset.items())
    ]
    result["source_to_asset_matches"] = source_to_asset_matches
    return result
