import io
import json
from contextlib import redirect_stderr
from pathlib import Path
from tempfile import TemporaryDirectory

from design2umg_verify_lib.run_history import write_historical_best_validation


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


class FakeRebuildBestModule:
    def __init__(self) -> None:
        self.written_manifests: list[tuple[Path, dict]] = []

    def resolve_admissible_best_manifest(self, automation_root: Path) -> tuple[Path, dict, str]:
        assert_equal(automation_root.name, "automation-root")
        return (
            automation_root / "_admissible_best_latest.json",
            {"cases": [{"name": "fixture"}]},
            "pinned_latest",
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
    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        output_root = root / "run-root"
        automation_root = root / "automation-root"
        output_root.mkdir()
        automation_root.mkdir()

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
                print_paths=True,
            )

        assert_equal(result["source"], "pinned_latest")
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
        assert stderr_lines[0] == "[HISTORICAL_BEST] source=pinned_latest"
        assert stderr_lines[1].startswith("[HISTORICAL_BEST] manifest=")
        assert stderr_lines[2].startswith("[HISTORICAL_BEST] validation=")

    print("run history selftest passed")


if __name__ == "__main__":
    main()
