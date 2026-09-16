from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

import design2umg_rebuild_admissible_best as history


def main() -> None:
    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        dynamic_manifest = {"cases": {"a": {}, "old": {}}}
        with patch.object(history, "rebuild_historical_best_manifest", return_value=dynamic_manifest):
            output, manifest, source = history.resolve_admissible_best_manifest(
                root, expected_case_names={"a", "new"}
            )

        assert output == root / "_admissible_best_latest.json"
        assert source == "dynamic_history"
        assert manifest["scope_case_names"] == ["a", "new"]
        assert manifest["scope_missing_cases"] == ["new"]
        assert manifest["scope_extra_historical_cases"] == ["old"]
        assert set(manifest["cases"]) == {"a", "old"}

        try:
            history.resolve_admissible_best_manifest(root, expected_case_names=set())
        except ValueError:
            pass
        else:
            raise AssertionError("Empty discovery must not be accepted")

    print("history scope selftest passed")


if __name__ == "__main__":
    main()
