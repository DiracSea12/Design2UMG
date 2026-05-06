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
) -> dict:
    return {
        "snapshot_type": "test",
        "root_kind": "container",
        "root_child_count": 1,
        "total_nodes": total,
        "leaf_nodes": leaf,
        "container_nodes": container,
        "text_nodes": text,
        "visual_nodes": visual,
        "visible_texts": visible_texts or [],
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


def main():
    source = make_snapshot(total=20, leaf=10, container=6, text=2, visual=12, visible_texts=["A", "B"])
    asset = make_snapshot(total=24, leaf=12, container=10, text=2, visual=12, visible_texts=["A", "B"])

    ok = validate(source, asset, asset)
    assert ok["required"] is True
    assert ok["passed"] is True, ok

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

    print("structure guard selftest passed")


if __name__ == "__main__":
    main()
