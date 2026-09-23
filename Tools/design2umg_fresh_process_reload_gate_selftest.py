import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import Mock, patch

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
                    "force_reload_package_requested": True,
                    "generated_asset_path": "/Game/X.WBP_X",
                    "asset_structure": {"total_nodes": 3},
                }
            ),
            encoding="utf-8",
        )
        started_at = result_path.stat().st_mtime - 10
        failures, summaries = gate.evaluate_results({result_path: "/Game/X.WBP_X"}, min_total_nodes=5, started_at=started_at)
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
            {result_path: "/Game/X.WBP_X"}, min_total_nodes=1, started_at=result_path.stat().st_mtime - 1
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
        failures, _ = gate.evaluate_results({result_path: "/Game/X.WBP_X"}, min_total_nodes=1, started_at=future_start)
        assert any("stale" in failure for failure in failures)


def test_evaluate_results_passes_healthy_result():
    with tempfile.TemporaryDirectory() as tmp:
        result_path = Path(tmp) / "r.json"
        result_path.write_text(
            json.dumps(
                {
                    "success": True,
                    "force_reload_package_requested": True,
                    "generated_asset_path": "/Game/X.WBP_X",
                    "asset_structure": {"total_nodes": 136},
                }
            ),
            encoding="utf-8",
        )
        failures, summaries = gate.evaluate_results(
            {result_path: "/Game/X.WBP_X"}, min_total_nodes=10, started_at=result_path.stat().st_mtime - 5
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
        paths = {}
        for name, nodes in (("a.json", 100), ("b.json", 40)):
            p = Path(tmp) / name
            p.write_text(
                json.dumps({"success": True, "force_reload_package_requested": True, "generated_asset_path": f"/Game/{name}", "asset_structure": {"total_nodes": nodes}}),
                encoding="utf-8",
            )
            paths[p] = f"/Game/{name}"
        started_at = max(p.stat().st_mtime for p in paths) - 10
        failures, summaries = gate.evaluate_results(
            paths, min_total_nodes=1, started_at=started_at, asset_floors={1: 50}
        )
        # asset 0 passes the global floor of 1; asset 1 fails its own floor of 50.
        assert_equal(len(summaries), 1)
        assert_equal(len(failures), 1)
        assert "guarded floor (40 < 50)" in failures[0]


def run_with_result(*, exit_code=0, changes=None, extra_args=()):
    # Only replace process and clock; use the real specs, file reads and log audit.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        editor, project = root / "editor.exe", root / "project.uproject"
        editor.touch()
        project.touch()
        output = root / "gate"
        args = gate.build_parser().parse_args([
            "--editor", str(editor), "--project", str(project),
            "--assets", "/Game/X.WBP_X", "--output-root", str(output),
            "--min-total-nodes", "5", *extra_args,
        ])

        def finish_process(timeout):
            for spec_path in output.glob("*-automation_asset_spec.json"):
                spec = json.loads(spec_path.read_text(encoding="utf-8"))
                payload = {"success": True, "generated_asset_path": spec["source_path"],
                           "force_reload_package_requested": True, "asset_structure": {"total_nodes": 5}}
                payload.update(changes or {})
                Path(spec["result_json_path"]).write_text(json.dumps(payload), encoding="utf-8")
            return exit_code

        console = io.StringIO()
        process = Mock()
        process.wait.side_effect = finish_process
        with (patch.object(gate.subprocess, "Popen", return_value=process) as launch,
              patch.object(gate.time, "time", return_value=1.0), redirect_stdout(console)):
            result = gate.run_gate(args)
        if launch.called:
            process.wait.assert_called_once_with(timeout=args.deadline_sec)
        return result, console.getvalue(), launch.called


def test_run_gate_requires_successful_process_exit():
    for exit_code in (0, 1, 3221225477):
        result, console, launched = run_with_result(exit_code=exit_code)
        assert launched and "total_nodes=5" in console
        assert_equal(result, 0 if exit_code == 0 else 1)
        assert_equal("[PASS]" in console, exit_code == 0)
        if exit_code:
            assert str(exit_code) in console


def test_run_gate_rejects_invalid_result_contract():
    for changes in (
        {"generated_asset_path": "/Game/Other.WBP_Other"},
        {"generated_asset_path": ""},
        {"force_reload_package_requested": False},
        {"force_reload_package_requested": None},
        {"success": "false"}, {"success": 1},
        {"asset_structure": {"total_nodes": True}},
        {"asset_structure": {"total_nodes": 5.0}},
        {"asset_structure": []},
    ):
        code, console, launched = run_with_result(changes=changes)
        assert launched
        assert code == 1 and "[PASS]" not in console, (changes, console)


def test_run_gate_rejects_invalid_floor_before_launch():
    for args in (
        ("--asset-floors", "1=500"), ("--asset-floors=-1=500",),
        ("--asset-floors", "0=500,0=5"), ("--min-total-nodes", "0"),
        ("--min-total-nodes", "-1"),
    ):
        code, console, launched = run_with_result(extra_args=args)
        assert code == 2 and not launched, (args, console)


def test_suite_reload_checks_structure_and_pixels():
    from PIL import Image
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        case = root / "suite/user-screen"
        case.mkdir(parents=True)
        structure = {"total_nodes": 5, "leaf_nodes": 3, "container_nodes": 2, "text_nodes": 1,
                     "visual_nodes": 2, "visible_texts": ["Hello"],
                     "kind_histogram": {"text": 1}, "class_histogram": {"TextBlock": 1}}
        baseline = {"success": True, "generated_asset_path": "/Game/X.WBP_X", "asset_structure": structure}
        (case.parent / "run_scope.json").write_text(json.dumps({"case_names": [case.name]}), encoding="utf-8")
        (case / "ue_render_result.json").write_text(json.dumps(baseline), encoding="utf-8")
        Image.new("RGBA", (2, 2), (50, 100, 150, 255)).save(case / "candidate.png")
        baselines = gate.load_suite_assets(case.parent)
        result_path = root / "reload.json"
        payload = baseline | {"force_reload_package_requested": True}
        result_path.write_text(json.dumps(payload), encoding="utf-8")
        Image.new("RGBA", (2, 2), (50, 100, 150, 255)).save(result_path.with_suffix(".png"))
        results = {result_path: "/Game/X.WBP_X"}
        assert_equal(gate.evaluate_results(results, 1, 0, baselines=baselines)[0], [])
        payload["asset_structure"] = structure | {"visible_texts": ["Changed"]}
        result_path.write_text(json.dumps(payload), encoding="utf-8")
        assert "structure or text" in gate.evaluate_results(results, 1, 0, baselines=baselines)[0][0]
        payload["asset_structure"] = structure
        result_path.write_text(json.dumps(payload), encoding="utf-8")
        Image.new("RGBA", (2, 2), (0, 0, 0, 255)).save(result_path.with_suffix(".png"))
        assert "pixels differ" in gate.evaluate_results(results, 1, 0, baselines=baselines)[0][0]
        result_path.with_suffix(".png").unlink()
        assert "missing or stale" in gate.evaluate_results(results, 1, 0, baselines=baselines)[0][0]
        (case / "ue_render_result.json").unlink()
        try:
            gate.load_suite_assets(case.parent)
        except RuntimeError:
            pass
        else:
            raise AssertionError("A missing suite result must block reload")


def test_suite_reload_rejects_empty_or_invalid_scope():
    for names in ([], ["same", "same"], ["../outside"], [None]):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "run_scope.json").write_text(json.dumps({"case_names": names}), encoding="utf-8")
            try:
                gate.load_suite_assets(root)
            except RuntimeError:
                continue
            raise AssertionError(f"invalid suite scope accepted: {names}")


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print(f"[OK] {test.__name__}")
    print(f"fresh-process reload gate selftest passed ({len(tests)} tests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
