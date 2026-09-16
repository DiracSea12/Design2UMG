import os
import hashlib
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_rebuild_admissible_best_selftest as fixtures
import design2umg_validate_run as validate_run


def stamp_run(run_root: Path, timestamp: float) -> None:
    os.utime(run_root / "summary.json", (timestamp, timestamp))


def main() -> None:
    with TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)

        epoch_a = fixtures.make_run(
            automation_root,
            "_formal_epoch_a",
            stable_changed_ratio=0.10,
            changed_ratio=0.10,
            source_hash="source-a",
            reference_hash="reference-a",
        )
        stamp_run(epoch_a, 1000.0)
        manifest_a_path = automation_root / "manifest-a.json"
        fixtures.write_json(manifest_a_path, rebuild_best.rebuild_historical_best_manifest(automation_root))

        epoch_b = fixtures.make_current_validation_run(
            automation_root,
            "_formal_epoch_b_seed",
            0.20,
            source_hash="source-b",
            reference_hash="reference-b",
        )
        stamp_run(epoch_b, 2000.0)
        seeded = validate_run.validate_run(epoch_b, admissible_best_path=manifest_a_path)
        assert not seeded["blocked"], seeded
        assert seeded["historical_best_status"] == "pass_with_new_evidence_epoch", seeded
        assert seeded["historical_best_epoch_seeded_cases"] == ["case_a"], seeded
        fixtures.write_json(epoch_b / "validation_against_historical_best.json", seeded)

        manifest_b = rebuild_best.rebuild_historical_best_manifest(automation_root)
        best_b = manifest_b["cases"]["case_a"]
        assert best_b["stable_changed_ratio"] == 0.20, best_b
        assert best_b["metric_sources"]["stable_changed_ratio"]["source_input"]["source_design_artifact_hash"] == "source-b", best_b

        manifest_b_path = automation_root / "manifest-b.json"
        fixtures.write_json(manifest_b_path, manifest_b)
        worse_b = fixtures.make_current_validation_run(
            automation_root,
            "_formal_epoch_b_worse",
            0.30,
            source_hash="source-b",
            reference_hash="reference-b",
        )
        stamp_run(worse_b, 3000.0)
        regressed = validate_run.validate_run(worse_b, admissible_best_path=manifest_b_path)
        assert regressed["blocked"], regressed
        assert "historical_best_regression" in regressed["blocked_reasons"], regressed

        # Returning to a previously seen identity must reuse that identity's baseline.
        revisit_a = fixtures.make_current_validation_run(
            automation_root, "_formal_epoch_a_revisit", 0.30,
            source_hash="source-a", reference_hash="reference-a",
        )
        stamp_run(revisit_a, 3500.0)
        revisit = validate_run.validate_run(revisit_a, admissible_best_path=manifest_b_path)
        assert revisit["blocked"], revisit
        assert "historical_best_regression" in revisit["blocked_reasons"], revisit

        epoch_c = fixtures.make_current_validation_run(
            automation_root,
            "_formal_epoch_c_seed",
            0.40,
            source_hash="source-c",
            reference_hash="reference-c",
        )
        stamp_run(epoch_c, 4000.0)
        independent = validate_run.validate_run(epoch_c, admissible_best_path=manifest_b_path)
        assert not independent["blocked"], independent
        assert independent["historical_best_epoch_seeded_cases"] == ["case_a"], independent

    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        run = fixtures.make_current_validation_run(root, "_weak_reference", 0.10)
        report_path = run / "case_a" / "compare_report.json"
        report = rebuild_best.load_json(report_path)
        report.setdefault("reference_context", {}).setdefault("reference_authority", {})["verification_level"] = "weak"
        fixtures.write_json(report_path, report)
        manifest_path = root / "empty-manifest.json"
        fixtures.write_json(manifest_path, {"cases": {}})
        weak = validate_run.validate_run(run, admissible_best_path=manifest_path)
        assert weak["blocked"], weak
        assert "historical_best_degraded_authority" in weak["blocked_reasons"], weak
        assert not weak["historical_best_epoch_seeded_cases"], weak

    with TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        old = fixtures.make_run(root, "_old", stable_changed_ratio=0.10, changed_ratio=0.10)
        baseline = fixtures.make_run(root, "_accepted", stable_changed_ratio=0.20, changed_ratio=0.20)
        stamp_run(old, 1000.0)
        stamp_run(baseline, 2000.0)
        fixtures.write_json(root / "_baseline.json", {
            "schema": 1, "run": baseline.name, "accepted_at_unix": 3000.0,
            "summary_sha256": hashlib.sha256((baseline / "summary.json").read_bytes()).hexdigest(),
            "scope_sha256": hashlib.sha256((baseline / "run_scope.json").read_bytes()).hexdigest(),
        })
        manifest = rebuild_best.rebuild_historical_best_manifest(root)
        assert manifest["cases"]["case_a"]["stable_changed_ratio"] == 0.20, manifest
        assert manifest["runs"] == [baseline.name], manifest
        mp = root / "anchored-manifest.json"
        fixtures.write_json(mp, manifest)
        worse = fixtures.make_current_validation_run(root, "_after_acceptance", 0.30)
        stamp_run(worse, 4000.0)
        result = validate_run.validate_run(worse, admissible_best_path=mp)
        assert result["blocked"] and "historical_best_regression" in result["blocked_reasons"], result
        # A candidate does not silently move the accepted start point.
        again = rebuild_best.rebuild_historical_best_manifest(root)
        assert again["cases"]["case_a"]["stable_changed_ratio"] == 0.20, again

    print("evidence epoch selftest passed")


if __name__ == "__main__":
    main()
