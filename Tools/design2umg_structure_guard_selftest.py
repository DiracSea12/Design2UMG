from types import SimpleNamespace

import design2umg_auto_verify as verify


def make_snapshot(
    *,
    total: int,
    leaf: int,
    container: int,
    text: int,
    visual: int,
    visible_texts: list[str] | None = None,
    root_kind: str = "container",
    root_child_count: int = 1,
    node_names: list[str] | None = None,
    structure_nodes: list[dict] | None = None,
) -> dict:
    return {
        "snapshot_type": "test",
        "root_kind": root_kind,
        "root_child_count": root_child_count,
        "total_nodes": total,
        "leaf_nodes": leaf,
        "container_nodes": container,
        "text_nodes": text,
        "visual_nodes": visual,
        "visible_texts": visible_texts or [],
        "node_names": node_names or [],
        "structure_nodes": structure_nodes or [],
        "kind_histogram": {
            "container": container,
            "text": text,
            "visual": visual,
        },
        "class_histogram": {
            "CanvasPanel": container,
            "TextBlock": text,
            "Image": visual,
        },
    }


def validate(source: dict | None, generated_asset: dict | None, saved_asset: dict | None, mode: str = "local-figma") -> dict:
    args = SimpleNamespace(mode=mode)
    render_chain = {
        "generation": {
            "ue_result": {
                "source_structure": source,
                "asset_structure": generated_asset,
            }
        },
        "asset_render": {
            "ue_result": {
                "asset_structure": saved_asset,
            }
        },
    }
    return verify.validate_structure_consistency(args, {}, render_chain, {})


def assert_reason(result: dict, reason: str):
    assert reason in result["reasons"], f"missing {reason}: {result}"


def make_tree_snapshot(nodes: list[dict], visible_texts: list[str]) -> dict:
    return make_snapshot(
        total=len(nodes),
        leaf=sum(1 for node in nodes if node["kind"] != "container"),
        container=sum(1 for node in nodes if node["kind"] == "container"),
        text=sum(1 for node in nodes if node["kind"] == "text"),
        visual=sum(1 for node in nodes if node["kind"] == "visual"),
        visible_texts=visible_texts,
        node_names=[node["name"] for node in nodes],
        structure_nodes=nodes,
    )


def main():
    source_tree = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "Label", "kind": "text", "type": "TEXT"},
        {"path": "0/1", "name": "Icon", "kind": "visual", "type": "RECTANGLE"},
    ]
    asset_tree = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "Root_sz", "kind": "other", "class": "SizeBox"},
        {"path": "0/0/0", "name": "Label", "kind": "text", "class": "Design2UMGConstrainedTextBlock"},
        {"path": "0/1", "name": "Icon", "kind": "visual", "class": "Image"},
    ]
    correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": source_tree},
        {"structure_nodes": asset_tree},
    )
    assert correspondence["matched_source_nodes"] == 3, correspondence
    assert correspondence["named_parent_path_matches"] == 3, correspondence
    assert correspondence["sibling_order_matches"] == 2, correspondence
    assert correspondence["kind_matches"] == 3, correspondence

    reordered_asset_tree = [
        asset_tree[0],
        asset_tree[1],
        {**asset_tree[3], "path": "0/1"},
        {**asset_tree[2], "path": "0/2/0"},
    ]
    reordered = verify.compute_tree_correspondence(
        {"structure_nodes": source_tree},
        {"structure_nodes": reordered_asset_tree},
    )
    assert reordered["sibling_order_matches"] == 0, reordered

    duplicate_source = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "Item", "kind": "container", "type": "FRAME"},
        {"path": "0/1", "name": "Item", "kind": "container", "type": "FRAME"},
    ]
    duplicate_asset = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "Item", "kind": "container", "class": "Border"},
        {"path": "0/1", "name": "Item_inner", "kind": "container", "class": "HorizontalBox"},
    ]
    duplicate_correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": duplicate_source},
        {"structure_nodes": duplicate_asset},
    )
    assert duplicate_correspondence["matched_source_nodes"] == 2, duplicate_correspondence
    assert duplicate_correspondence["unmatched_source_names"] == ["Item"], duplicate_correspondence

    fallback_source = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "None", "kind": "text", "type": "TEXT"},
    ]
    fallback_asset = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "TextBlock_0", "kind": "text", "class": "TextBlock"},
    ]
    fallback_correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": fallback_source},
        {"structure_nodes": fallback_asset},
    )
    assert fallback_correspondence["matched_source_nodes"] == 2, fallback_correspondence
    assert fallback_correspondence["named_parent_path_ratio"] == 1.0, fallback_correspondence

    semantic_correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": [{"path": "0", "name": "Badge", "kind": "visual", "type": "RECTANGLE"}]},
        {"structure_nodes": [{"path": "0", "name": "Badge", "kind": "container", "class": "Overlay"}]},
    )
    assert semantic_correspondence["kind_ratio"] == 0.0, semantic_correspondence
    assert semantic_correspondence["semantic_kind_ratio"] == 1.0, semantic_correspondence
    assert semantic_correspondence["kind_mismatch_classes"] == {"visual_composite_carrier": 1}, semantic_correspondence

    icon_source = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "Trigger", "kind": "container", "type": "FRAME"},
        {"path": "0/0/0", "name": "Label", "kind": "text", "type": "Text"},
        {"path": "0/0/1", "name": "IconFont_Widget", "kind": "text", "type": "IconFont"},
    ]
    icon_asset = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "Trigger", "kind": "container", "class": "Border"},
        {"path": "0/0/0", "name": "Label", "kind": "text", "class": "TextBlock"},
        {"path": "0/0/1", "name": "uJzgY", "kind": "text", "class": "TextBlock"},
    ]
    icon_correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": icon_source},
        {"structure_nodes": icon_asset},
    )
    assert icon_correspondence["matched_source_nodes"] == 4, icon_correspondence
    assert icon_correspondence["unmatched_source_names"] == [], icon_correspondence

    identity_source = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME", "id": "root-id"},
        {"path": "0/0", "name": "Object", "kind": "visual", "type": "RECTANGLE", "id": "object-id"},
    ]
    identity_asset = [
        {"path": "0", "name": "Root", "kind": "container", "class": "Border", "id": "root-id"},
        {"path": "0/0", "name": "Root_inner", "kind": "container", "class": "CanvasPanel", "id": "root-id_content"},
        {"path": "0/0/0", "name": "Object_1", "kind": "visual", "class": "Border", "id": "object-id"},
    ]
    identity_correspondence = verify.compute_tree_correspondence(
        {"structure_nodes": identity_source},
        {"structure_nodes": identity_asset},
    )
    assert identity_correspondence["matched_source_nodes"] == 2, identity_correspondence
    assert identity_correspondence["identity_ratio"] == 1.0, identity_correspondence
    assert identity_correspondence["named_parent_path_ratio"] == 1.0, identity_correspondence

    source = make_snapshot(total=20, leaf=10, container=6, text=2, visual=12, visible_texts=["A", "B"])
    asset = make_snapshot(total=24, leaf=12, container=10, text=2, visual=12, visible_texts=["A", "B"])

    ok = validate(source, asset, asset)
    assert ok["required"] is True
    assert ok["passed"] is True, ok

    empty_visual_leaf = make_snapshot(
        total=1,
        leaf=1,
        container=0,
        text=0,
        visual=1,
        root_kind="visual",
        root_child_count=0,
        node_names=["empty-frame"],
    )
    empty_container_leaf = validate(
        make_snapshot(
            total=1,
            leaf=1,
            container=1,
            text=0,
            visual=0,
            root_child_count=0,
            node_names=["empty-frame"],
        ),
        empty_visual_leaf,
        empty_visual_leaf,
    )
    assert empty_container_leaf["passed"] is True, empty_container_leaf

    mismatched_empty_visual_leaf = dict(empty_visual_leaf)
    mismatched_empty_visual_leaf["node_names"] = ["different-node"]
    mismatched_empty_container_leaf = validate(
        make_snapshot(
            total=1,
            leaf=1,
            container=1,
            text=0,
            visual=0,
            root_child_count=0,
            node_names=["empty-frame"],
        ),
        mismatched_empty_visual_leaf,
        mismatched_empty_visual_leaf,
    )
    assert mismatched_empty_container_leaf["passed"] is False, mismatched_empty_container_leaf
    assert_reason(mismatched_empty_container_leaf, "asset_container_missing")

    missing = validate(source, None, None)
    assert missing["passed"] is False
    assert_reason(missing, "structure_snapshot_unavailable")

    image_only = validate(
        source,
        make_snapshot(total=2, leaf=1, container=1, text=0, visual=1),
        make_snapshot(total=2, leaf=1, container=1, text=0, visual=1),
    )
    assert image_only["passed"] is False
    assert_reason(image_only, "image_only_render_cheat_suspected")
    assert_reason(image_only, "visible_text_mismatch")

    reload_drift = validate(
        source,
        asset,
        make_snapshot(total=23, leaf=11, container=10, text=2, visual=12, visible_texts=["A", "B"]),
    )
    assert reload_drift["passed"] is False
    assert_reason(reload_drift, "generated_asset_structure_changed_after_reload")

    asset_mode = validate(None, None, None, mode="asset")
    assert asset_mode["required"] is False
    assert asset_mode["passed"] is True

    source_role_nodes = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "dashboard", "kind": "text", "type": "IconFont"},
        {"path": "0/1", "name": "dashboard", "kind": "text", "type": "Text"},
    ]
    asset_role_nodes = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "dashboard", "kind": "text", "class": "TextBlock", "text": "dashboard"},
        {"path": "0/1", "name": "dashboard_1", "kind": "text", "class": "TextBlock", "text": "dashboard"},
    ]
    role_result = validate(
        make_tree_snapshot(source_role_nodes, ["dashboard"]),
        make_tree_snapshot(asset_role_nodes, ["dashboard", "dashboard"]),
        make_tree_snapshot(asset_role_nodes, ["dashboard", "dashboard"]),
    )
    assert role_result["passed"] is True, role_result
    assert role_result["metrics"]["icon_role_texts_excluded"] == ["dashboard"], role_result

    icon_values = ["settings", "Icon", "\ue123"]
    icon_source_nodes = [source_role_nodes[0]] + [
        {"path": f"0/{index}", "name": f"Icon{index}", "kind": "text", "type": "IconFont"}
        for index in range(3)
    ]
    icon_asset_nodes = [asset_role_nodes[0]] + [
        {"path": f"0/{index}", "name": f"Icon{index}", "kind": "text", "class": "TextBlock", "text": value}
        for index, value in enumerate(icon_values)
    ]
    icon_values_result = validate(
        make_tree_snapshot(icon_source_nodes, []),
        make_tree_snapshot(icon_asset_nodes, icon_values),
        make_tree_snapshot(icon_asset_nodes, icon_values),
    )
    assert icon_values_result["passed"] is True, icon_values_result
    assert sorted(icon_values_result["metrics"]["icon_role_texts_excluded"]) == sorted(icon_values), icon_values_result

    missing_icon_source_nodes = [
        source_role_nodes[0],
        {"path": "0/0", "name": "expected_icon", "kind": "text", "type": "IconFont"},
        {"path": "0/1", "name": "dashboard", "kind": "text", "type": "Text"},
    ]
    missing_icon_asset_nodes = [
        asset_role_nodes[0],
        {"path": "0/0", "name": "dashboard", "kind": "text", "class": "TextBlock", "text": "dashboard"},
    ]
    missing_icon_result = validate(
        make_tree_snapshot(missing_icon_source_nodes, ["dashboard"]),
        make_tree_snapshot(missing_icon_asset_nodes, ["dashboard"]),
        make_tree_snapshot(missing_icon_asset_nodes, ["dashboard"]),
    )
    assert missing_icon_result["passed"] is False, missing_icon_result
    assert_reason(missing_icon_result, "text_node_count_mismatch")
    assert missing_icon_result["metrics"]["icon_role_texts_excluded"] == [], missing_icon_result

    missing_body_result = validate(
        make_tree_snapshot(source_role_nodes, ["dashboard"]),
        make_tree_snapshot(asset_role_nodes[:2], ["dashboard"]),
        make_tree_snapshot(asset_role_nodes[:2], ["dashboard"]),
    )
    assert missing_body_result["passed"] is False, missing_body_result
    assert_reason(missing_body_result, "visible_text_mismatch")
    assert missing_body_result["metrics"]["missing_texts"] == ["dashboard"], missing_body_result
    assert missing_body_result["metrics"]["icon_role_texts_excluded"] == ["dashboard"], missing_body_result

    repeated_source_nodes = [
        {"path": "0", "name": "Root", "kind": "container", "type": "FRAME", "id": "root"},
        {"path": "0/0", "name": "Item", "kind": "container", "type": "FRAME", "id": "instance"},
        {"path": "0/0/0", "name": "IconFont_Widget", "kind": "text", "type": "IconFont", "id": "icon"},
        {"path": "0/1", "name": "Item", "kind": "container", "type": "FRAME", "id": "instance"},
        {"path": "0/1/0", "name": "IconFont_Widget", "kind": "text", "type": "IconFont", "id": "icon"},
    ]
    repeated_asset_nodes = [
        {"path": "0", "name": "Root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "Item", "kind": "container", "class": "Border"},
        {"path": "0/0/0", "name": "IconFont_Widget_1", "kind": "text", "class": "TextBlock", "text": "one"},
        {"path": "0/1", "name": "Item_1", "kind": "container", "class": "Border"},
        {"path": "0/1/0", "name": "IconFont_Widget_2", "kind": "text", "class": "TextBlock", "text": "two"},
    ]
    repeated_result = validate(
        make_tree_snapshot(repeated_source_nodes, []),
        make_tree_snapshot(repeated_asset_nodes, ["one", "two"]),
        make_tree_snapshot(repeated_asset_nodes, ["one", "two"]),
    )
    assert repeated_result["passed"] is True, repeated_result
    assert repeated_result["metrics"]["icon_role_texts_excluded"] == ["one", "two"], repeated_result

    missing_node_text = [dict(node) for node in asset_role_nodes]
    missing_node_text[1].pop("text")
    missing_text_result = validate(
        make_tree_snapshot(source_role_nodes[:2], []),
        make_tree_snapshot(missing_node_text[:2], ["dashboard"]),
        make_tree_snapshot(missing_node_text[:2], ["dashboard"]),
    )
    assert missing_text_result["passed"] is False, missing_text_result
    assert_reason(missing_text_result, "visible_text_mismatch")
    assert missing_text_result["metrics"]["icon_role_texts_excluded"] == [], missing_text_result

    missing_pair_result = validate(
        make_tree_snapshot(source_role_nodes[:2], []),
        make_tree_snapshot([], ["dashboard"]),
        make_tree_snapshot([], ["dashboard"]),
    )
    assert missing_pair_result["passed"] is False, missing_pair_result
    assert_reason(missing_pair_result, "visible_text_mismatch")
    assert missing_pair_result["metrics"]["icon_role_texts_excluded"] == [], missing_pair_result

    low_confidence_source_nodes = [
        {"path": "0", "name": "root", "kind": "container", "type": "FRAME"},
        {"path": "0/0", "name": "expected_icon", "kind": "text", "type": "IconFont"},
    ]
    low_confidence_asset_nodes = [
        {"path": "0", "name": "root", "kind": "container", "class": "CanvasPanel"},
        {"path": "0/0", "name": "UnexpectedBody", "kind": "text", "class": "TextBlock", "text": "unexpected body text"},
    ]
    low_confidence_result = validate(
        make_tree_snapshot(low_confidence_source_nodes, []),
        make_tree_snapshot(low_confidence_asset_nodes, ["unexpected body text"]),
        make_tree_snapshot(low_confidence_asset_nodes, ["unexpected body text"]),
    )
    assert low_confidence_result["passed"] is False, low_confidence_result
    assert_reason(low_confidence_result, "visible_text_mismatch")
    assert low_confidence_result["metrics"]["icon_role_texts_excluded"] == [], low_confidence_result

    fallback_source_nodes = [low_confidence_source_nodes[0], {**low_confidence_source_nodes[1], "name": "None"}]
    fallback_asset_nodes = [
        low_confidence_asset_nodes[0],
        {"path": "0/0", "name": "TextBlock_0", "kind": "text", "class": "TextBlock", "text": "ordinary text"},
    ]
    fallback_result = validate(
        make_tree_snapshot(fallback_source_nodes, []),
        make_tree_snapshot(fallback_asset_nodes, ["ordinary text"]),
        make_tree_snapshot(fallback_asset_nodes, ["ordinary text"]),
    )
    assert fallback_result["passed"] is False, fallback_result
    assert_reason(fallback_result, "visible_text_mismatch")
    assert fallback_result["metrics"]["icon_role_texts_excluded"] == [], fallback_result

    print("structure guard selftest passed")


if __name__ == "__main__":
    main()
