import io
import json
from unittest.mock import patch
from contextlib import redirect_stderr
from pathlib import Path
from tempfile import TemporaryDirectory

from design2umg_verify_lib.run_history import write_historical_best_validation


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


class FakeRebuildBestModule:
    def __init__(self) -> None:
        self.written_manifests: list[tuple[Path, dict]] = []

    def resolve_admissible_best_manifest(self, automation_root: Path, *, expected_case_names: set[str]) -> tuple[Path, dict, str]:
        assert_equal(automation_root.name, "automation-root")
        assert_equal(expected_case_names, {"discovered-case"})
        return (
            automation_root / "_admissible_best_latest.json",
            {"cases": [{"name": "fixture"}]},
            "dynamic_history",
        )

    def write_json(self, path: Path, payload: dict) -> None:
        self.written_manifests.append((path, payload))
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


class FakeValidateRunModule:
    def __init__(self) -> None:
        self.calls: list[tuple[Path, Path]] = []

    def validate_run(self, output_root: Path, *, admissible_best_path: Path) -> dict:
        self.calls.append((output_root, admissible_best_path))
        return {"blocked": False, "historical_best_regressions": []}


def write_json(path: Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> None:
    assert_external_history_is_read_only()
    assert_single_run_preserves_global_manifest()
    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        automation_root = root / "automation-root"
        automation_root.mkdir()
        output_root = automation_root / "run-root"
        output_root.mkdir()

        rebuild_best = FakeRebuildBestModule()
        validate_run = FakeValidateRunModule()

        stderr = io.StringIO()
        with redirect_stderr(stderr):
            result = write_historical_best_validation(
                output_root,
                automation_root,
                rebuild_best,
                validate_run,
                write_json,
                expected_case_names={"discovered-case"},
                print_paths=True,
            )

        assert_equal(result["source"], "dynamic_history")
        assert_equal(result["manifest_path"], automation_root / "_admissible_best_latest.json")
        assert_equal(result["run_manifest_path"], output_root / "historical_best_manifest.json")
        assert_equal(result["validation_path"], output_root / "validation_against_historical_best.json")
        assert_equal(result["validation"], {"blocked": False, "historical_best_regressions": []})
        assert_equal(len(rebuild_best.written_manifests), 1)
        assert_equal(validate_run.calls, [(output_root, automation_root / "_admissible_best_latest.json")])
        assert (output_root / "historical_best_manifest.json").is_file()
        assert (output_root / "validation_against_historical_best.json").is_file()

        stderr_lines = stderr.getvalue().strip().splitlines()
        assert_equal(len(stderr_lines), 3)
        assert stderr_lines[0] == "[HISTORICAL_BEST] source=dynamic_history"
        assert stderr_lines[1].startswith("[HISTORICAL_BEST] manifest=")
        assert stderr_lines[2].startswith("[HISTORICAL_BEST] validation=")

    print("run history selftest passed")


def assert_single_run_preserves_global_manifest() -> None:
    import design2umg_rebuild_admissible_best as rebuild_best

    with TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        output_root = automation_root / "single-run"
        output_root.mkdir()
        global_manifest = automation_root / "_admissible_best_latest.json"
        original = b'{"scope_case_names":["first","second"]}'
        global_manifest.write_bytes(original)
        validator = FakeValidateRunModule()
        with patch.object(rebuild_best, "rebuild_historical_best_manifest", return_value={"cases": {}}) as scan, \
                patch.object(rebuild_best, "load_baseline_anchor", return_value=(automation_root / "baseline", 0)):
            result = write_historical_best_validation(
                output_root, automation_root, rebuild_best, validator, write_json,
                expected_case_names={"first"}, publish_global_manifest=False,
            )
        scan.assert_called_once_with(automation_root, cache_path=None)
        assert_equal(global_manifest.read_bytes(), original)
        local_manifest = output_root / "historical_best_manifest.json"
        assert_equal(result["manifest_path"], local_manifest)
        assert_equal(validator.calls, [(output_root, local_manifest)])
        assert_equal(json.loads(local_manifest.read_text(encoding="utf-8"))["scope_case_names"], ["first"])



def assert_external_history_is_read_only() -> None:
    import design2umg_rebuild_admissible_best as rebuild_best

    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        history_root = root / "automation-root"
        baseline = history_root / "accepted-run"
        baseline.mkdir(parents=True)
        for name in ("_baseline.json", "_admissible_best_latest.json", rebuild_best.CACHE_FILENAME):
            (history_root / name).write_text('{"unchanged":true}', encoding="utf-8")
        (baseline / "input.json").write_text('{"accepted":true}', encoding="utf-8")
        before = {p.relative_to(history_root): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in history_root.rglob("*") if p.is_file()}
        output_root = root / "ci-automation" / "new-run"
        output_root.mkdir(parents=True)
        validator = FakeValidateRunModule()
        with patch.object(rebuild_best, "load_baseline_anchor", return_value=(baseline, 0)), \
                patch.object(rebuild_best, "collect_best_cases", return_value=({}, [], [], {}, {}, 0, {})):
            result = write_historical_best_validation(
                output_root, history_root, rebuild_best, validator, write_json,
                expected_case_names={"first"}, publish_global_manifest=False,
            )
        after = {p.relative_to(history_root): (p.read_bytes(), p.stat().st_mtime_ns)
                 for p in history_root.rglob("*") if p.is_file()}
        assert_equal(after, before)
        assert_equal(result["manifest_path"], output_root / "historical_best_manifest.json")
        assert_equal(validator.calls, [(output_root, result["manifest_path"])])
        assert (output_root / rebuild_best.CACHE_FILENAME).is_file()
        assert (output_root / "validation_against_historical_best.json").is_file()
        try:
            write_historical_best_validation(
                output_root, history_root, rebuild_best, validator, write_json,
                expected_case_names={"first"}, publish_global_manifest=True,
            )
        except ValueError as error:
            assert "--no-publish-history" in str(error), str(error)
        else:
            raise AssertionError("External history must reject global publishing")


if __name__ == "__main__":
    main()
