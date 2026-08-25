import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import design2umg_fresh_process_reload_gate as gate


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def test_outer_load_failure_signature_detected():
    log_lines = [
        "[2026.08.24-20.47.52:897][  3]LogStreaming: Display: FlushAsyncLoading(): 0 QueuedPackages, 0 AsyncPackages",
        '[2026.08.24-20.47.52:897][  3]LoadErrors: Warning: 创建导出：资产“BorderSlot_0”的外部容器加载失败：'
        "Design2UMGConstrainedBorder /Game/X.WBP_X:WidgetTree.Frame1",
        '[2026.08.24-20.47.52:897][  3]LoadErrors: Warning: CreateExport: Failed to load Outer for resource '
        "'WidgetTree.Frame2': /Game/X.WBP_X:WidgetTree.Frame2",
        "[2026.08.24-20.47.53:000][  3]LogExit: Exiting.",
    ]
    hits = gate.audit_log_for_corruption_lines("\n".join(log_lines))
    assert_equal(len(hits), 2)
    assert all("Design2UMGConstrained" in hit or "Frame2" in hit for hit in hits)


def test_guid_ensure_signature_detected():
    log_lines = [
        "[2026.08.24-20.47.49:710][  2]LogOutputDevice: Error: Variable [Frame3277] was deleted but still has a GUID "
        "referenced by WidgetBlueprint [WBP_Course_Builder]",
        "[2026.08.24-20.47.50:000][  2]LogOutputDevice: Error: Widget [NewOne] was added but did not get a GUID",
        "[2026.08.24-20.47.51:000][  2]LogOutputDevice: Error: [Callstack] UnrealEditor-UMGEditor.dll!ValidateAndFixUpVariableGuids",
    ]
    hits = gate.audit_log_for_corruption_lines("\n".join(log_lines))
    assert_equal(len(hits), 2)


def test_clean_log_has_no_hits():
    log_lines = [
        "LogDesign2UMG: Design2UMG.AutomationRender succeeded: asset=/Game/X.WBP_X",
        "LogExit: Exiting.",
    ]
    assert_equal(gate.audit_log_for_corruption_lines("\n".join(log_lines)), [])


def test_evaluate_results_flags_collapse():
    with tempfile.TemporaryDirectory() as tmp:
        result_path = Path(tmp) / "r.json"
        result_path.write_text(
            json.dumps(
                {
                    "success": True,
                    "generated_asset_path": "/Game/X.WBP_X",
                    "asset_structure": {"total_nodes": 3},
                }
            ),
            encoding="utf-8",
        )
        started_at = result_path.stat().st_mtime - 10
        failures, summaries = gate.evaluate_results([result_path], min_total_nodes=5, started_at=started_at)
        assert not summaries
        assert len(failures) == 1
        assert "collapsed below the guarded floor" in failures[0]


def test_evaluate_results_flags_failure_payload():
    with tempfile.TemporaryDirectory() as tmp:
        result_path = Path(tmp) / "r.json"
        result_path.write_text(
            json.dumps({"success": False, "error_message": "boom"}),
            encoding="utf-8",
        )
        failures, summaries = gate.evaluate_results(
            [result_path], min_total_nodes=1, started_at=result_path.stat().st_mtime - 1
        )
        assert not summaries
        assert any("boom" in failure for failure in failures)


def test_evaluate_results_flags_stale_result():
    with tempfile.TemporaryDirectory() as tmp:
        result_path = Path(tmp) / "r.json"
        result_path.write_text(
            json.dumps({"success": True, "asset_structure": {"total_nodes": 9}}),
            encoding="utf-8",
        )
        future_start = result_path.stat().st_mtime + 100
        failures, _ = gate.evaluate_results([result_path], min_total_nodes=1, started_at=future_start)
        assert any("stale" in failure for failure in failures)


def test_evaluate_results_passes_healthy_result():
    with tempfile.TemporaryDirectory() as tmp:
        result_path = Path(tmp) / "r.json"
        result_path.write_text(
            json.dumps(
                {
                    "success": True,
                    "generated_asset_path": "/Game/X.WBP_X",
                    "asset_structure": {"total_nodes": 136},
                }
            ),
            encoding="utf-8",
        )
        failures, summaries = gate.evaluate_results(
            [result_path], min_total_nodes=10, started_at=result_path.stat().st_mtime - 5
        )
        assert_equal(failures, [])
        assert summaries == ["/Game/X.WBP_X: total_nodes=136"]


def test_build_asset_spec_shape():
    spec = gate.build_asset_spec(
        "/Game/A.B",
        Path(r"G:\out\tag-0-B.png"),
        Path(r"G:\out\tag-0-B.json"),
    )
    assert_equal(spec["mode"], "asset")
    assert_equal(spec["source_path"], "/Game/A.B")
    assert spec["force_reload_package"] is True
    assert spec["render_png_path"].endswith("tag-0-B.png")
    assert spec["result_json_path"].endswith("tag-0-B.json")


def test_normalize_object_path_rejects_missing_object_name():
    try:
        gate.normalize_object_path("/Game/Folder/Asset")
    except SystemExit as exc:
        assert "object name" in str(exc)
    else:
        raise AssertionError("expected SystemExit for object path without asset name")


def test_parse_asset_floors_parses_and_overrides():
    assert_equal(gate.parse_asset_floors("0=136,1=219"), {0: 136, 1: 219})
    assert_equal(gate.parse_asset_floors("2=50; 0 =10"), {2: 50, 0: 10})
    assert_equal(gate.parse_asset_floors(""), {})


def test_parse_asset_floors_rejects_bad_input():
    for bad in ("0=", "a=5", "0=-3", "0=abc"):
        try:
            gate.parse_asset_floors(bad)
        except SystemExit:
            continue
        raise AssertionError(f"expected SystemExit for {bad!r}")


def test_evaluate_results_uses_per_asset_floor():
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for name, nodes in (("a.json", 100), ("b.json", 40)):
            p = Path(tmp) / name
            p.write_text(
                json.dumps({"success": True, "generated_asset_path": f"/Game/{name}", "asset_structure": {"total_nodes": nodes}}),
                encoding="utf-8",
            )
            paths.append(p)
        started_at = max(p.stat().st_mtime for p in paths) - 10
        failures, summaries = gate.evaluate_results(
            paths, min_total_nodes=1, started_at=started_at, asset_floors={1: 50}
        )
        # asset 0 passes the global floor of 1; asset 1 fails its own floor of 50.
        assert_equal(len(summaries), 1)
        assert_equal(len(failures), 1)
        assert "guarded floor (40 < 50)" in failures[0]


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print(f"[OK] {test.__name__}")
    print(f"fresh-process reload gate selftest passed ({len(tests)} tests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
