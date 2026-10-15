"""Verify that testing one engine never rewrites another engine's plugin metadata."""
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import design2umg_auto_verify as verify
import design2umg_auto_verify_suite as suite


class EnginePluginSelectionTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows directory junction behavior")
    def test_junction_cycle_is_pruned_before_repeated_descent(self):
        plugins = self.engine / "Plugins"
        junction = plugins / "Cycle"
        subprocess.run(["cmd", "/c", "mklink", "/J", str(junction), str(plugins)],
                       capture_output=True, check=True)
        script = f"""import sys
from types import SimpleNamespace
sys.path.insert(0, {str(Path(__file__).resolve().parents[1])!r})
import design2umg_auto_verify as verify
walk = verify.os.walk
directories = []
def observe(root):
    for entry in walk(root):
        directories.append(entry[0])
        yield entry
verify.os.walk = observe
verify.prepare_runtime_project(SimpleNamespace(project={str(self.project)!r}, editor={str(self.editor)!r}))
assert len(directories) <= 4, f'Repeated directory traversal: {{len(directories)}}'
"""
        result = subprocess.run([sys.executable, "-c", script], capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace")[-1000:])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = self.root / "SelectedEngine" / "Engine"
        self.editor = self.engine / "Binaries" / "Win64" / "UnrealEditor.exe"
        self.editor.parent.mkdir(parents=True)
        self.editor.touch()
        self.project = self.root / "Verification" / "Verification.uproject"
        self.project.parent.mkdir()
        self.project.write_text('{"FileVersion":3}', encoding="utf-8")
        self.plugin = self.engine / "Plugins" / "Marketplace" / "Design2UMG"
        self.plugin.mkdir(parents=True)
        (self.plugin / "Design2UMG.uplugin").write_text('{"FileVersion":3}', encoding="utf-8")

    def args(self):
        return SimpleNamespace(project=str(self.project), editor=str(self.editor))

    def write_manifests(self, plugin_id):
        engine = self.editor.parent / "UnrealEditor.modules"
        plugin = self.plugin / "Binaries" / "Win64" / "UnrealEditor.modules"
        plugin.parent.mkdir(parents=True)
        engine.write_text(json.dumps({"BuildId":"engine-id"}), encoding="utf-8")
        modules = {name: f"UnrealEditor-{name}.dll" for name in ("Design2UMGCore", "Design2UMGEditor")}
        plugin.write_text(json.dumps({"BuildId":plugin_id, "Modules":modules}), encoding="utf-8")
        for filename in modules.values():
            (plugin.parent / filename).write_bytes(b"fixture DLL")
        return engine, plugin

    def test_selects_plugin_from_requested_engine(self):
        _, selected = verify.prepare_runtime_project(self.args())
        self.assertEqual(selected, self.plugin)

    def test_project_plugin_is_selected_when_engine_has_no_copy(self):
        local = self.project.parent / "Plugins" / "Design2UMG"
        local.mkdir(parents=True)
        (local / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        (self.plugin / "Design2UMG.uplugin").unlink()
        _, selected = verify.prepare_runtime_project(self.args())
        self.assertEqual(selected, local)

    def test_duplicate_engine_and_project_plugins_are_reported(self):
        local = self.project.parent / "Plugins" / "Design2UMG"
        local.mkdir(parents=True)
        descriptor = local / "Design2UMG.uplugin"
        descriptor.write_text('{}', encoding="utf-8")
        with self.assertRaises(RuntimeError):
            verify.prepare_runtime_project(self.args())
        self.assertEqual(descriptor.read_text(encoding="utf-8"), '{}')

    def test_nested_engine_duplicate_is_reported(self):
        nested = self.engine / "Plugins" / "Other" / "Design2UMG"
        nested.mkdir(parents=True)
        (nested / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        with self.assertRaises(RuntimeError):
            verify.prepare_runtime_project(self.args())

    def test_additional_plugin_directory_duplicate_is_reported(self):
        extra = self.root / "ExtraPlugins"
        extra.mkdir()
        (extra / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        self.project.write_text(json.dumps({"FileVersion": 3, "AdditionalPluginDirectories": ["../ExtraPlugins"]}), encoding="utf-8")
        with self.assertRaises(RuntimeError):
            verify.prepare_runtime_project(self.args())

    def test_enterprise_plugin_duplicate_is_reported(self):
        enterprise = self.engine.parent / "Enterprise" / "Plugins" / "Design2UMG"
        enterprise.mkdir(parents=True)
        (enterprise / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        with self.assertRaises(RuntimeError):
            verify.prepare_runtime_project(self.args())

    def test_enterprise_only_plugin_is_selected(self):
        (self.plugin / "Design2UMG.uplugin").unlink()
        enterprise = self.engine.parent / "Enterprise" / "Plugins" / "Design2UMG"
        enterprise.mkdir(parents=True)
        (enterprise / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        _, selected = verify.prepare_runtime_project(self.args())
        self.assertEqual(selected, enterprise)

    def test_plugin_contents_are_not_scanned_as_more_plugins(self):
        nested = self.plugin / "Source" / "Fixture"
        nested.mkdir(parents=True)
        (nested / "Design2UMG.uplugin").write_text('{}', encoding="utf-8")
        _, selected = verify.prepare_runtime_project(self.args())
        self.assertEqual(selected, self.plugin)

    def test_missing_target_plugin_is_reported(self):
        (self.plugin / "Design2UMG.uplugin").unlink()
        with self.assertRaises(RuntimeError):
            verify.prepare_runtime_project(self.args())

    def test_matching_build_id_is_read_only(self):
        paths = self.write_manifests("engine-id")
        before = [p.read_bytes() for p in paths]
        verify.validate_plugin_module_build_id(str(self.editor), self.plugin)
        self.assertEqual(before, [p.read_bytes() for p in paths])

    def test_matching_build_id_rejects_missing_module_binary(self):
        self.write_manifests("engine-id")
        missing = self.plugin / "Binaries" / "Win64" / "UnrealEditor-Design2UMGEditor.dll"
        missing.unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing plugin module"):
            verify.validate_plugin_module_build_id(str(self.editor), self.plugin)
        self.assertFalse(missing.exists())

    def test_matching_build_id_rejects_missing_module_entry(self):
        _, plugin = self.write_manifests("engine-id")
        data = json.loads(plugin.read_text(encoding="utf-8"))
        del data["Modules"]["Design2UMGEditor"]
        plugin.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Missing plugin module"):
            verify.validate_plugin_module_build_id(str(self.editor), self.plugin)

    def test_mismatching_build_id_is_rejected_without_rewrite(self):
        paths = self.write_manifests("other-engine-id")
        before = [p.read_bytes() for p in paths]
        with self.assertRaises(RuntimeError):
            verify.validate_plugin_module_build_id(str(self.editor), self.plugin)
        self.assertEqual(before, [p.read_bytes() for p in paths])

    def test_missing_manifest_is_reported_without_writing(self):
        engine, plugin = self.write_manifests("engine-id")
        plugin.unlink()
        before = engine.read_bytes()
        with self.assertRaises(RuntimeError):
            verify.validate_plugin_module_build_id(str(self.editor), self.plugin)
        self.assertFalse(plugin.exists())
        self.assertEqual(before, engine.read_bytes())

    def test_empty_build_id_is_reported_without_rewrite(self):
        paths = self.write_manifests("")
        before = [p.read_bytes() for p in paths]
        with self.assertRaises(RuntimeError):
            verify.validate_plugin_module_build_id(str(self.editor), self.plugin)
        self.assertEqual(before, [p.read_bytes() for p in paths])

    def test_font_asset_seed_can_be_disabled_for_an_independent_suite(self):
        with patch.object(sys, "argv", ["suite", "--design-root", str(self.root), "--no-font-asset-seed"]):
            self.assertTrue(suite.parse_args().no_font_asset_seed)

    def test_font_asset_seed_can_be_disabled_for_an_independent_single_case(self):
        with patch.object(sys, "argv", ["verify", "pen", "--source", "sample.pen", "--reference-provider", "png", "--no-font-asset-seed"]):
            self.assertTrue(verify.parse_args().no_font_asset_seed)

    def test_single_case_font_seed_control_flow(self):
        self.write_manifests("engine-id")
        font = self.project.parent / "Content" / "Design2UMG" / "Fonts" / "Existing.uasset"
        font.parent.mkdir(parents=True)
        font.write_bytes(b"declared project font input")
        for disabled in (False, True):
            with self.subTest(disabled=disabled):
                argv = ["verify", "pen", "--source", "sample.pen", "--reference-provider", "png",
                        "--output-dir", str(self.root / "single")]
                if disabled:
                    argv.append("--no-font-asset-seed")
                with patch.object(sys, "argv", argv), \
                     patch.object(verify, "resolve_unreal_editor_path", return_value=str(self.editor)), \
                     patch.object(verify, "prepare_runtime_project", return_value=(str(self.project), self.plugin)), \
                     patch.object(verify, "seed_runtime_font_assets") as seed, \
                     patch.object(verify, "get_expected_design_size", side_effect=StopIteration("after font seed")):
                    with self.assertRaises(StopIteration):
                        verify.main()
                    if disabled:
                        seed.assert_not_called()
                        self.assertEqual(font.read_bytes(), b"declared project font input")
                    else:
                        seed.assert_called_once_with(str(self.project), ["sample.pen"])

    def test_single_case_rejects_mismatched_binary_before_font_write(self):
        self.write_manifests("other-engine-id")
        font = self.project.parent / "Content" / "Design2UMG" / "Fonts" / "Existing.uasset"
        font.parent.mkdir(parents=True)
        font.write_bytes(b"original font input")
        output = self.root / "single-mismatched"
        argv = ["verify", "pen", "--source", "sample.pen", "--reference-provider", "png",
                "--output-dir", str(output)]
        with patch.object(sys, "argv", argv), \
             patch.object(verify, "resolve_unreal_editor_path", return_value=str(self.editor)), \
             patch.object(verify, "prepare_runtime_project", return_value=(str(self.project), self.plugin)), \
             patch.object(verify, "seed_runtime_font_assets") as seed, \
             patch.object(verify, "get_expected_design_size", side_effect=StopIteration("after font seed")):
            with self.assertRaises(RuntimeError):
                verify.main()
            seed.assert_not_called()
        self.assertEqual(font.read_bytes(), b"original font input")
        self.assertFalse(output.exists())

    def test_suite_font_seed_control_flow(self):
        font = self.project.parent / "Content" / "Design2UMG" / "Fonts" / "Existing.uasset"
        font.parent.mkdir(parents=True)
        font.write_bytes(b"declared project font input")
        for disabled in (False, True):
            with self.subTest(disabled=disabled):
                argv = ["suite", "--design-root", str(self.root), "--output-root", str(self.root / "suite")]
                if disabled:
                    argv.append("--no-font-asset-seed")
                with patch.object(sys, "argv", argv), \
                     patch.object(suite.rebuild_best, "load_baseline_anchor", return_value=(self.root / "baseline", 0)), \
                     patch.object(suite.suite_discovery, "resolve_default_figma_export_roots", return_value=[]), \
                     patch.object(suite.suite_discovery, "discover_pen_cases", return_value=[{"source": "sample.pen"}]), \
                     patch.object(suite.suite_discovery, "discover_figma_export_cases", return_value=[]), \
                     patch.object(suite.suite_discovery, "write_run_scope", return_value=self.root / "scope.json"), \
                     patch.object(verify, "resolve_unreal_editor_path", return_value=str(self.editor)), \
                     patch.object(verify, "prepare_runtime_project", return_value=(str(self.project), self.plugin)), \
                     patch.object(verify, "validate_plugin_module_build_id"), \
                     patch.object(verify, "seed_runtime_font_assets") as seed, \
                     patch.object(suite, "record_effective_game_output_root", side_effect=StopIteration("after font seed")):
                    with self.assertRaises(StopIteration):
                        suite.main()
                    if disabled:
                        seed.assert_not_called()
                        self.assertEqual(font.read_bytes(), b"declared project font input")
                    else:
                        seed.assert_called_once_with(str(self.project), ["sample.pen"])


if __name__ == "__main__":
    unittest.main()
