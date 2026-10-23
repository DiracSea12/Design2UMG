"""Render histories compare only the same UE minor version and actual RHI."""
import json
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import design2umg_rebuild_admissible_best as rebuild
import design2umg_validate_run as validate
from design2umg_verify_lib import evidence_contract
from design2umg_verify_lib.run_history import write_historical_best_validation


class RenderEvidenceIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def log_report(self, backend, *, mode="generation", confirmed=True, feature_level="SM5"):
        path = self.root / f"{backend}-{mode}-{feature_level}.log"
        text = f"LogRHI: Using Forced RHI: {backend}\n"
        if confirmed:
            text += f"LogRHI: RHI {backend} with Feature Level {feature_level} is supported and will be used.\n"
        path.write_text(text, encoding="utf-8")
        return {"render_chain": {mode: {"log_path": str(path)}}}

    def test_runtime_confirmation_identifies_backend(self):
        for backend in ("D3D11", "D3D12", "Vulkan"):
            report = self.log_report(backend)
            self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), backend)

    def test_runtime_confirmation_identifies_feature_level(self):
        for feature in ("SM5", "SM6"):
            report = self.log_report("D3D12", feature_level=feature)
            self.assertEqual(evidence_contract.rhi_feature_level_from_evidence(report), feature)

    def test_conflicting_render_feature_levels_have_no_identity(self):
        report = self.log_report("D3D12", feature_level="SM5")
        report["render_chain"].update(self.log_report(
            "D3D12", mode="asset_render", feature_level="SM6")["render_chain"])
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")
        self.assertEqual(evidence_contract.rhi_feature_level_from_evidence(report), "")

    def test_adapter_feature_support_is_not_runtime_confirmation(self):
        report = self.log_report("D3D12", confirmed=False)
        path = Path(report["render_chain"]["generation"]["log_path"])
        path.write_text("LogD3D12RHI: Max supported Feature Level 12_2, shader model 6.7\n", encoding="utf-8")
        self.assertEqual(evidence_contract.rhi_feature_level_from_evidence(report), "")

    def test_history_identity_includes_actual_feature_level(self):
        current = self.log_report("D3D12", feature_level="SM6")
        current.update(engine_version="5.8", source_signature={"sha1": "source"},
                       effective_reference_signature={"sha1": "reference"})
        source = self.log_report("D3D12", feature_level="SM5")
        source.update(engine_version="5.8", source_input={
            "source_design_artifact_hash": "source", "reference_image_hash": "reference"})
        best = {"mean_abs_error": 0.1, "metric_sources": {"mean_abs_error": source}}
        result = validate.evaluate_historical_best_identity(current, best)
        self.assertFalse(result["compatible"])
        self.assertEqual([item["field"] for item in result["mismatches"]], ["rhi_feature_level"])

    def test_requested_backend_alone_is_not_confirmation(self):
        report = self.log_report("Vulkan", confirmed=False)
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")

    def test_declared_backend_without_runtime_is_unavailable(self):
        for backend in ("D3D11", "D3D12", "Vulkan"):
            for report in ({"rhi_backend": backend}, {"source_input": {"rhi_backend": backend}}):
                self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")

    def test_external_history_requires_no_publish_history(self):
        with self.assertRaisesRegex(ValueError, "External historical root requires --no-publish-history"):
            write_historical_best_validation(self.root / "run", self.root / "external", None, None, None, {"case"})

    def test_external_history_validates_without_publishing(self):
        import design2umg_rebuild_admissible_best_selftest as fixtures
        self.accepted_anchor()
        current = fixtures.make_current_validation_run(self.root / "ci", "current", .2)
        result = write_historical_best_validation(
            current, self.root, rebuild, validate, fixtures.write_json, {"case_a"},
            publish_global_manifest=False,
        )
        self.assertFalse(result["validation"]["blocked"], result["validation"])
        self.assertEqual(result["manifest_path"], current / "historical_best_manifest.json")
        self.assertTrue(result["validation_path"].is_file())
        self.assertFalse((self.root / "_admissible_best_latest.json").exists())

    def test_next_run_recovers_the_previous_runs_best(self):
        import design2umg_rebuild_admissible_best_selftest as fixtures
        baseline = fixtures.make_run(self.root, "accepted", stable_changed_ratio=.2, changed_ratio=.2)
        pointer = {"schema": 1, "run": baseline.name, "accepted_at_unix": 0,
                   "summary_sha256": hashlib.sha256((baseline / "summary.json").read_bytes()).hexdigest(),
                   "scope_sha256": hashlib.sha256((baseline / "run_scope.json").read_bytes()).hexdigest(),
                   "identity_evidence_sha256": rebuild.baseline_identity_digest(baseline)}
        (self.root / "_baseline.json").write_text(json.dumps(pointer), encoding="utf-8")
        fixtures.make_run(self.root, "first", stable_changed_ratio=.1, changed_ratio=.1, binary_bound=True)
        current = fixtures.make_current_validation_run(self.root, "next", .15)
        path, _, _ = rebuild.resolve_admissible_best_manifest(self.root, expected_case_names={"case_a"})
        result = validate.validate_run(current, admissible_best_path=path, metric_epsilon=0)
        self.assertTrue(result["historical_best_regressions"])

    def accepted_anchor(self):
        import design2umg_rebuild_admissible_best_selftest as fixtures
        baseline = fixtures.make_run(self.root, "accepted", stable_changed_ratio=.2, changed_ratio=.2)
        pointer = {"schema": 1, "run": baseline.name, "accepted_at_unix": 0,
                   "summary_sha256": hashlib.sha256((baseline / "summary.json").read_bytes()).hexdigest(),
                   "scope_sha256": hashlib.sha256((baseline / "run_scope.json").read_bytes()).hexdigest(),
                   "identity_evidence_sha256": rebuild.baseline_identity_digest(baseline)}
        (self.root / "_baseline.json").write_text(json.dumps(pointer), encoding="utf-8")
        self.assertEqual(rebuild.load_baseline_anchor(self.root)[0], baseline)
        return baseline

    def test_anchor_rejects_runtime_log_identity_change(self):
        baseline = self.accepted_anchor()
        report = json.loads((baseline / "case_a/compare_report.json").read_text(encoding="utf-8"))
        log = Path(report["render_chain"]["generation"]["log_path"])
        log.write_text(log.read_text(encoding="utf-8").replace("D3D12", "Vulkan"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity evidence changed"):
            rebuild.load_baseline_anchor(self.root)

    def test_anchor_rejects_report_source_identity_change(self):
        baseline = self.accepted_anchor()
        path = baseline / "case_a/compare_report.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        report["source_signature"] = {"sha1": "different-source"}
        path.write_text(json.dumps(report), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity evidence changed"):
            rebuild.load_baseline_anchor(self.root)

    def test_anchor_without_identity_binding_is_rejected(self):
        self.accepted_anchor()
        path = self.root / "_baseline.json"
        pointer = json.loads(path.read_text(encoding="utf-8"))
        del pointer["identity_evidence_sha256"]
        path.write_text(json.dumps(pointer), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity binding"):
            rebuild.load_baseline_anchor(self.root)

    def test_anchor_scope_does_not_read_outside_the_accepted_run(self):
        baseline = self.accepted_anchor()
        path = baseline / "run_scope.json"
        scope = json.loads(path.read_text(encoding="utf-8"))
        scope["case_names"] = [".."]
        path.write_text(json.dumps(scope), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Invalid accepted baseline case scope"):
            rebuild.baseline_identity_digest(baseline)

    def test_conflicting_render_process_backends_are_unavailable(self):
        report = self.log_report("D3D12")
        report["render_chain"].update(self.log_report("Vulkan", mode="asset_render")["render_chain"])
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")

    def test_backend_creation_log_confirms_runtime_rhi(self):
        report = self.log_report("D3D12")
        path = Path(report["render_chain"]["generation"]["log_path"])
        path.write_text(
            "LogD3D12RHI: Display: Creating D3D12 RHI with Max Feature Level SM5\n",
            encoding="utf-8",
        )
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "D3D12")
        self.assertEqual(evidence_contract.rhi_feature_level_from_evidence(report), "SM5")

    def test_adapter_enumeration_and_requested_flags_are_not_runtime_rhi(self):
        report = self.log_report("D3D12")
        path = Path(report["render_chain"]["generation"]["log_path"])
        path.write_text(
            "LogInit: Command Line: -d3d12\n"
            "LogD3D12RHI: Found D3D12 adapter 0: GPU\n",
            encoding="utf-8",
        )
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")

    def test_declared_backend_conflicting_with_runtime_is_unavailable(self):
        report = self.log_report("Vulkan")
        report["rhi_backend"] = "D3D12"
        self.assertEqual(evidence_contract.rhi_backend_from_evidence(report), "")

    def test_history_epoch_partitions_version_and_backend(self):
        a = {"engine_version": "5.5", "rhi_backend": "D3D12",
             "source_design_artifact_hash": "source", "reference_image_hash": "reference"}
        for difference in ({"engine_version": "5.4"}, {"rhi_backend": "Vulkan"}):
            b = dict(a, **difference)
            self.assertFalse(rebuild.same_evidence_epoch(
                rebuild.evidence_epoch_identity(a), rebuild.evidence_epoch_identity(b)))

    def test_returning_backend_uses_its_previous_history(self):
        def source(backend, score):
            report = self.log_report(backend)
            path = self.root / f"{backend}-report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            return {"run": backend, "report_path": str(path),
                    "engine_version": "5.5", "rhi_backend": backend,
                    "source_input": {"source_design_artifact_hash": "source", "reference_image_hash": "reference"},
                    "stable_changed_ratio": score, "changed_ratio": score,
                    "stable_mean_abs_error": score, "mean_abs_error": score}
        old = source("Vulkan", .2)
        other = source("D3D12", .1)
        best = {"admitted_sources": [old, other], "metric_sources": {"stable_changed_ratio": other},
                "stable_changed_ratio": .1}
        current = {"engine_version": "5.5", "rhi_backend": "Vulkan",
                   "report_path": old["report_path"],
                   "source_signature": {"sha1": "source"}, "effective_reference_signature": {"sha1": "reference"}}
        selected = validate.select_case_evidence_epoch(current, best)
        self.assertEqual(selected["stable_changed_ratio"], .2)
        self.assertEqual([item["run"] for item in selected["admitted_sources"]], ["Vulkan"])

    def test_empty_temporary_history_requires_accepted_anchor(self):
        with self.assertRaisesRegex(ValueError, "accepted baseline"):
            rebuild.resolve_admissible_best_manifest(self.root, expected_case_names={"case"})

    def test_explicit_accepted_control_does_not_require_cross_rhi_parity(self):
        summary = {"total": 81, "blocked_cases": 0, "completeness": {"blocked": False},
                   "no_regression": {"available": 81, "failed": 12}}
        self.assertEqual(rebuild.classify_summary_admissibility(self.root, summary, accepted_baseline=True),
                         (True, "user_accepted_baseline"))

    def test_failed_editor_is_not_admitted_despite_complete_metrics(self):
        (self.root / "validation_against_historical_best.json").write_text(
            json.dumps({"blocked": False, "historical_best_checked": True,
                        "historical_best_regressions": []}), encoding="utf-8")
        summary = {"total": 81, "blocked_cases": 0, "completeness": {"blocked": False},
                   "editor_process": {"passed": 0, "failed": 1},
                   "no_regression": {"available": 81, "failed": 0}}
        for accepted in (False, True):
            with self.subTest(accepted_baseline=accepted):
                self.assertEqual(rebuild.classify_summary_admissibility(
                    self.root, summary, accepted_baseline=accepted),
                    (False, "summary_editor_process_failed"))
        summary["editor_process"] = {"passed": 1, "failed": 0}
        summary["editor_exit_codes"] = [3221225477]
        self.assertEqual(rebuild.classify_summary_admissibility(self.root, summary),
                         (True, "summary_admissible"))


if __name__ == "__main__":
    unittest.main()
