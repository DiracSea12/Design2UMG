"""Release checks use disposable fixtures, never an Unreal project."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = (
    "LICENSE", "README.md", "README.zh-CN.md", "CHANGELOG.md",
    "KNOWN_LIMITATIONS.md", "Design2UMG.uplugin",
    "Tools/FigmaExporter/manifest.json", "Tools/FigmaExporter/code.js",
    "Tools/FigmaExporter/ui.html", "Tools/FigmaExporter/README.md",
    "Examples/SimpleCard.pen",
)


class ReleaseContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="d2u-release-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_script(self, relative, *args, timeout=30):
        return subprocess.run(
            ["pwsh", "-NoProfile", "-File", str(ROOT / relative), *map(str, args)],
            capture_output=True, encoding="utf-8", errors="replace",
            timeout=timeout,
        )

    def package(self):
        package = self.root / "package"
        for name in REQUIRED:
            path = package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        return package

    def check_package(self, package):
        return self.run_script("Tools/Release/Assert-PublicRepoBoundary.ps1",
                               "-RepoRoot", package, "-Mode", "Package")

    def test_complete_package(self):
        result = self.check_package(self.package())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_required_files(self):
        package = self.package()
        for name in REQUIRED:
            with self.subTest(name=name):
                path = package / name
                original = path.read_bytes()
                path.unlink()
                result = self.check_package(package)
                self.assertNotEqual(result.returncode, 0, name)
                self.assertIn(name, result.stdout + result.stderr)
                path.write_bytes(original)

    def test_uat_generated_headers_preserved(self):
        package = self.package()
        for name in ("UMGSchema.generated.h", "Design2UMGCoreClasses.h"):
            path = package / "Intermediate/Build/Win64/UnrealEditor/Inc/Design2UMGCore/UHT" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// UHT generated header\n", encoding="utf-8")
        library = package / "Intermediate/Build/Win64/x64/UnrealEditorNU/Development/Design2UMGCore/UnrealEditor-Design2UMGCore.lib"
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_bytes(b"UBT import library")
        result = self.check_package(package)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        exported = self.run_script("Tools/Release/Assert-PublicRepoBoundary.ps1",
                                   "-RepoRoot", package, "-Mode", "ExportWorkspace")
        self.assertNotEqual(exported.returncode, 0)
        self.assertIn("Intermediate", exported.stdout + exported.stderr)

    def test_intermediate_pollution_rejected(self):
        package = self.package()
        for name in ("Intermediate/cache.json", "Intermediate/Build/Win64/cache.obj",
                     "Intermediate/Build/Win64/x64/UnrealEditorNU/Development/Design2UMGCore/cache.lib",
                     "Intermediate/Build/Win64/x64/UnrealEditorNU/Development/Other/UnrealEditor-Other.lib",
                     "Intermediate/Build/Win64/UnrealEditor/Inc/Design2UMGCore/UHT/cache.json",
                     "Intermediate/Build/Win64/UnrealEditor/Inc/Design2UMGCore/UHT/notes.h",
                     "Intermediate/Build/Win64/UnrealEditor/Inc/Other/UHT/Other.generated.h",
                     "Extras/Intermediate/Build/Win64/UnrealEditor/Inc/cache.h"):
            with self.subTest(name=name):
                path = package / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("local cache", encoding="utf-8")
                result = self.check_package(package)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(name, result.stdout.replace("\\", "/") + result.stderr)
                path.unlink()

    def test_ci_dirty_workspace_stops_before_build(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        start = workflow.index("          ./Tools/Release/Assert-CleanWorkspace.ps1")
        end = workflow.index("./Tools/CI/Assert-CandidatePlugin.ps1", start)
        end = workflow.rfind("\n", start, end) + 1
        guard = "\n".join(line[10:] for line in workflow[start:end].splitlines())
        check = self.root / "Tools/Release/Assert-CleanWorkspace.ps1"
        check.parent.mkdir(parents=True)
        driver = self.root / "ci-guard.ps1"
        marker = self.root / "build-started.txt"
        driver.write_text(guard + "\npwsh -NoProfile -Command 'exit 0'\n"
                          + "Set-Content build-started.txt started\n", encoding="utf-8")
        for code in (2, 0):
            with self.subTest(clean_exit=code):
                check.write_text(f"exit {code}\n", encoding="utf-8")
                result = subprocess.run(["pwsh", "-NoProfile", "-File", str(driver)],
                                        cwd=self.root, capture_output=True, encoding="utf-8", errors="replace")
                self.assertEqual(marker.exists(), code == 0, result.stdout + result.stderr)
                self.assertEqual(result.returncode == 0, code == 0)

    def test_ci_runs_font_rollback_tests(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn("Design2UMG.Fonts.LocalSource", re.findall(r'Filter\s*=\s*"([^"]+)"', workflow))
        filters = re.findall(r'Filter\s*=\s*"([^"]+)"', workflow)
        self.assertLess(filters.index("Design2UMG.Fonts.Persistence.Save"), filters.index("Design2UMG.Fonts.Persistence.Reload"))

    def test_ci_runs_font_replacement_recovery_in_separate_processes(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        filters = re.findall(r'Filter\s*=\s*"([^"]+)"', workflow)
        self.assertEqual([name for name in filters if name.startswith("Design2UMG.Fonts.")], [
            "Design2UMG.Fonts.LocalSource", "Design2UMG.Fonts.Persistence.Save",
            "Design2UMG.Fonts.Persistence.Reload", "Design2UMG.Fonts.Persistence.RejectedReplacement",
            "Design2UMG.Fonts.Persistence.RetryReplacement", "Design2UMG.Fonts.Persistence.Reload",
        ])
        self.assertIn("$runIndex++", workflow)
        self.assertIn("'{0:D2}-{1}' -f $runIndex", workflow)

    def test_ci_isolates_persistent_test_state_per_run(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn('$automationUserDir = Join-Path $artifactRoot', workflow)
        self.assertIn('[Guid]::NewGuid().ToString("N")', workflow)
        self.assertLess(workflow.index('$automationUserDir ='), workflow.index('foreach ($testRun in $testRuns)'))
        self.assertIn('"-UserDir={0}"', workflow)
        self.assertIn('-f $automationUserDir', workflow)

    def test_ci_runs_engine_plugin_selection_tests(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn('python Tools/Tests/Test-EnginePluginSelection.py', workflow)

    def test_ci_runs_generated_texture_asset_name_budget(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn('Filter = "Design2UMG.Resources.GeneratedTexture.AssetNameBudget"', workflow)

    def test_ci_runs_same_source_reimport_tests(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        filters = re.findall(r'Filter\s*=\s*"([^"]+)"', workflow)
        self.assertIn("Design2UMG.ComponentBlueprints.SameSourceReimport", filters)
        self.assertIn("Design2UMG.SafeReimport.Resources.SameSourceEffects", filters)

    def test_ci_runs_component_restart_reimport_in_separate_processes(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        filters = re.findall(r'Filter\s*=\s*"([^"]+)"', workflow)
        self.assertIn("Design2UMG.ComponentReimportPersistence.Save", filters)
        self.assertIn("Design2UMG.ComponentReimportPersistence.Reimport", filters)
        self.assertLess(filters.index("Design2UMG.ComponentReimportPersistence.Save"),
                        filters.index("Design2UMG.ComponentReimportPersistence.Reimport"))

    def test_local_fonts_rejected(self):
        package = self.package()
        (package / "Fonts").mkdir()
        (package / "Fonts/local.ttf").write_bytes(b"local font")
        self.assertNotEqual(self.check_package(package).returncode, 0)

    def test_package_filter_includes_public_files_not_fonts(self):
        rules = set((ROOT / 'Config/FilterPlugin.ini').read_text(encoding='utf-8').splitlines())
        for name in REQUIRED:
            if name == 'Design2UMG.uplugin':
                continue  # UAT includes the descriptor by default.
            rule = '/Examples/...' if name.startswith('Examples/') else '/' + name
            self.assertIn(rule, rules)
        self.assertNotIn('/Fonts/...', rules)
        self.assertIn('-/Fonts/...', rules)

    def candidate_fixture(self):
        source = self.root / "candidate"
        installed = self.root / "ue/Engine/Plugins/Marketplace/Design2UMG"
        for name in ("Design2UMG.uplugin", "Source/Module/Module.Build.cs", "Config/FilterPlugin.ini"):
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        shutil.copytree(source, installed)
        project = self.root / "project/Game.uproject"
        project.parent.mkdir()
        project.write_text("{}", encoding="utf-8")
        return source, installed, project

    def check_candidate(self, source, project):
        return self.run_script("Tools/CI/Assert-CandidatePlugin.ps1",
                               "-SourceRoot", source, "-EngineRoot", self.root / "ue",
                               "-ProjectFile", project)

    def test_matching_candidate(self):
        source, _, project = self.candidate_fixture()
        result = self.check_candidate(source, project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_missing_and_extra_source(self):
        for mode in ("changed", "missing", "extra"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                old_root, self.root = self.root, Path(temp)
                try:
                    source, installed, project = self.candidate_fixture()
                    path = installed / "Source/Module/Module.Build.cs"
                    if mode == "changed":
                        path.write_text("old version", encoding="utf-8")
                    elif mode == "missing":
                        path.unlink()
                    else:
                        (installed / "Source/extra.cpp").write_text("extra", encoding="utf-8")
                    self.assertNotEqual(self.check_candidate(source, project).returncode, 0)
                finally:
                    self.root = old_root

    def test_duplicate_project_plugin(self):
        source, installed, project = self.candidate_fixture()
        shutil.copytree(installed, project.parent / "Plugins/Design2UMG")
        self.assertNotEqual(self.check_candidate(source, project).returncode, 0)

    def test_missing_installed_plugin(self):
        source, installed, project = self.candidate_fixture()
        (installed / 'Design2UMG.uplugin').unlink()
        self.assertNotEqual(self.check_candidate(source, project).returncode, 0)

    def test_duplicate_in_all_plugin_scan_roots(self):
        source, installed, project = self.candidate_fixture()
        roots = (
            project.parent / "Mods",
            project.parent / "Platforms/Win64/Plugins",
            project.parent / "Restricted/NoRedist/Plugins",
            self.root / "ue/Engine/Platforms/Win64/Plugins",
            self.root / "ue/Engine/Restricted/NoRedist/Plugins",
            self.root / "ue/Enterprise/Plugins",
        )
        for root in roots:
            with self.subTest(root=str(root)):
                duplicate = root / "Vendor/Design2UMG/Design2UMG.uplugin"
                duplicate.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(installed / "Design2UMG.uplugin", duplicate)
                result = self.check_candidate(source, project)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                duplicate.unlink()

    def test_enterprise_only_plugin_is_checked(self):
        source, installed, project = self.candidate_fixture()
        enterprise = self.root / "ue/Enterprise/Plugins/Design2UMG"
        shutil.copytree(installed, enterprise)
        (installed / "Design2UMG.uplugin").unlink()
        result = self.check_candidate(source, project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nested_plugin_contents_not_scanned(self):
        source, installed, project = self.candidate_fixture()
        duplicate = installed / "Resources/Example/Design2UMG.uplugin"
        duplicate.parent.mkdir(parents=True)
        shutil.copy2(installed / "Design2UMG.uplugin", duplicate)
        result = self.check_candidate(source, project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def directory_link(self, link, target):
        link.parent.mkdir(parents=True, exist_ok=True)
        if os.name != "nt":
            link.symlink_to(target, target_is_directory=True)
            return
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-Command",
             f"New-Item -ItemType Junction -Path '{link.as_posix()}' -Target '{target.as_posix()}' | Out-Null"],
            capture_output=True, encoding="utf-8", errors="replace", timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_plugin_scan_directory_link_cycle(self):
        source, installed, project = self.candidate_fixture()
        plugins = installed.parents[1]
        self.directory_link(plugins / "Cycle", plugins)
        result = self.check_candidate(source, project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_non_windows_directory_link_fixture(self):
        link, target = self.root / "link", self.root / "target"
        with patch.object(os, "name", "posix"), patch.object(Path, "symlink_to") as create, patch.object(subprocess, "run") as run:
            self.directory_link(link, target)
        create.assert_called_once_with(target, target_is_directory=True)
        run.assert_not_called()

    def test_linked_duplicate_plugin(self):
        source, installed, project = self.candidate_fixture()
        duplicate = self.root / "external/Design2UMG"
        shutil.copytree(installed, duplicate)
        self.directory_link(project.parent / "Plugins/Linked", duplicate.parent)
        result = self.check_candidate(source, project)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("found 2", result.stdout + result.stderr)

    def test_only_installed_plugin_via_directory_link(self):
        source, installed, project = self.candidate_fixture()
        external = self.root / "external/Design2UMG"
        external.parent.mkdir()
        shutil.move(str(installed), external)
        self.directory_link(installed, external)
        result = self.check_candidate(source, project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ci_rebuild_removes_stale_plugin_outputs(self):
        source, installed, project = self.candidate_fixture()
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        start = workflow.index("./Tools/CI/Assert-CandidatePlugin.ps1")
        start = workflow.rfind("\n", 0, start) + 1
        end = workflow.index("      - name: Build plugin package", start)
        block = "\n".join(line[10:] for line in workflow[start:end].splitlines())
        block = block.replace("Engine/Build/BatchFiles/Build.bat", "Engine/Build/BatchFiles/Build.ps1")
        script = source / "Tools/CI/Assert-CandidatePlugin.ps1"
        script.parent.mkdir(parents=True)
        shutil.copy2(ROOT / "Tools/CI/Assert-CandidatePlugin.ps1", script)
        engine = installed.parents[3]
        build = engine / "Engine/Build/BatchFiles/Build.ps1"
        build.parent.mkdir(parents=True)
        binaries = installed / "Binaries/Win64"
        intermediate = installed / "Intermediate"
        metadata = project.parent / "Intermediate/Build/Win64/x64/FixtureEditor/Development/TargetMetadata.json"
        metadata.parent.mkdir(parents=True)
        metadata.write_bytes(b"fixture plan")
        writer = engine / "Engine/Source/Programs/UnrealBuildTool/Modes/WriteMetadataMode.cs"
        writer.parent.mkdir(parents=True)
        writer.write_text("const int CurrentVersionNumber = 2;\n", encoding="utf-8")
        engine_manifest = engine / "Engine/Binaries/Win64/UnrealEditor.modules"
        engine_manifest.parent.mkdir(parents=True)
        engine_manifest.write_text('{"BuildId":"fixture-engine"}', encoding="utf-8")
        driver = source / "ci-rebuild.ps1"
        driver.write_text(
            "$ErrorActionPreference='Stop'\n"
            f"$env:UE_ROOT='{engine.as_posix()}'\n"
            f"$env:DESIGN2UMG_RUNTIME_PROJECT='{project.as_posix()}'\n"
            "$env:DESIGN2UMG_EDITOR_TARGET='FixtureEditor'\n" + block,
            encoding="utf-8",
        )
        for complete, matching_manifest in ((False, True), (True, True), (True, False)):
            with self.subTest(complete_build=complete, matching_manifest=matching_manifest):
                binaries.mkdir(parents=True, exist_ok=True)
                intermediate.mkdir(parents=True, exist_ok=True)
                (binaries / "stale.dll").write_bytes(b"old")
                (intermediate / "stale.obj").write_bytes(b"old")
                output = ""
                if complete:
                    output = f"[IO.Directory]::CreateDirectory('{binaries.as_posix()}') | Out-Null\n"
                    for module in ("Core", "Editor"):
                        binary = binaries / f"UnrealEditor-Design2UMG{module}.dll"
                        output += f"[IO.File]::WriteAllText('{binary.as_posix()}', 'rebuilt')\n"
                build_id = "fixture-engine" if matching_manifest else "different-engine"
                write_metadata = (
                    "if ($args -contains '-Mode=WriteMetadata') {\n"
                    "  $normalizedArgs = @($args | ForEach-Object { $_.Replace('\\', '/') })\n"
                    f"  if ($normalizedArgs -notcontains '-Input={metadata.as_posix()}' -or $args -notcontains '-Version=2') {{ throw 'Wrong metadata arguments' }}\n"
                    f"  [IO.File]::WriteAllText('{(binaries / 'UnrealEditor.modules').as_posix()}', '{{\"BuildId\":\"{build_id}\"}}')\n"
                    "  $global:LASTEXITCODE=0; return\n}\n"
                )
                build.write_text(write_metadata + output + "$global:LASTEXITCODE=0\n", encoding="utf-8")
                result = subprocess.run(["pwsh", "-NoProfile", "-File", str(driver)],
                                        cwd=source, capture_output=True, encoding="utf-8", errors="replace")
                self.assertFalse((binaries / "stale.dll").exists(), result.stdout + result.stderr)
                self.assertFalse((intermediate / "stale.obj").exists(), result.stdout + result.stderr)
                self.assertEqual(result.returncode == 0, complete and matching_manifest, result.stdout + result.stderr)
                if complete and not matching_manifest:
                    self.assertIn("does not match the engine build", result.stdout + result.stderr)

    def test_additional_plugin_directory(self):
        source, installed, project = self.candidate_fixture()
        shutil.copytree(installed, self.root / 'extra/Design2UMG')
        project.write_text('{"AdditionalPluginDirectories":["../extra"]}', encoding='utf-8')
        self.assertNotEqual(self.check_candidate(source, project).returncode, 0)


if __name__ == "__main__":
    unittest.main()
