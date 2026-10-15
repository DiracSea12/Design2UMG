"""Each UE minor version retains its own visual baseline and history."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import design2umg_scoring as scoring
import design2umg_validate_run as validate
from design2umg_verify_lib import evidence_contract


class EngineEvidenceIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.metrics = {name: 0.1 for name in scoring.NO_REGRESSION_METRICS}

    def compare(self, current_version, baseline_version, current_rhi="D3D12", baseline_rhi="D3D12",
                current_feature="SM6", baseline_feature="SM6"):
        def runtime(version, rhi, name, feature):
            log = self.root / name
            text = f"LogInit: Engine Version: {version}\n"
            if rhi:
                text += f"LogRHI: RHI {rhi} with Feature Level {feature} is supported and will be used.\n"
            log.write_text(text, encoding="utf-8")
            return {"generation": {"log_path": str(log)}}

        baseline = {**self.metrics, "binary_binding": {"engine_version": baseline_version},
                    "render_chain": runtime(baseline_version, baseline_rhi, "baseline-runtime.log", baseline_feature)}
        (self.root / "compare_report.json").write_text(json.dumps(baseline), encoding="utf-8")
        current = {**self.metrics, "binary_binding": {"engine_version": current_version},
                   "render_chain": runtime(current_version, current_rhi, "current-runtime.log", current_feature)}
        with patch.object(scoring.render_rule, "evaluate_report_render_size_rule", return_value={"compatible": True}):
            return scoring.evaluate_no_regression(current, self.root, 6, 0)

    def test_cross_version_comparison_is_unavailable(self):
        result = self.compare("5.4.4", "5.7.1")
        self.assertFalse(result["available"])
        self.assertFalse(result["passed"])
        self.assertIn("engine_identity_mismatch", result["reasons"])

    def test_cross_feature_level_comparison_is_unavailable(self):
        result = self.compare("5.8.2", "5.8.2", current_feature="SM5", baseline_feature="SM6")
        self.assertFalse(result["available"])
        self.assertFalse(result["passed"])
        self.assertIn("rhi_feature_level_mismatch", result["reasons"])

    def test_same_feature_level_comparison_is_available(self):
        result = self.compare("5.8.2", "5.8.2", current_feature="SM5", baseline_feature="SM5")
        self.assertTrue(result["available"])
        self.assertTrue(result["passed"])

    def test_same_minor_version_accepts_different_patch_builds(self):
        result = self.compare("5.4.4-0+UE5", "5.4.2-12345+UE5")
        self.assertTrue(result["available"])
        self.assertTrue(result["passed"])

    def test_missing_engine_identity_does_not_pass(self):
        result = self.compare("5.4.4", "")
        self.assertFalse(result["available"])
        self.assertIn("engine_identity_unavailable", result["reasons"])

    def test_cross_rhi_comparison_is_unavailable(self):
        for current, baseline in (("D3D11", "D3D12"), ("D3D12", "Vulkan")):
            with self.subTest(current=current, baseline=baseline):
                result = self.compare("5.7.1", "5.7.1", current, baseline)
                self.assertFalse(result["available"])
                self.assertFalse(result["passed"])
                self.assertIn("rhi_identity_mismatch", result["reasons"])

    def test_missing_runtime_rhi_does_not_pass(self):
        for current, baseline in ((None, "D3D12"), ("D3D12", None)):
            with self.subTest(current=current, baseline=baseline):
                result = self.compare("5.7.1", "5.7.1", current, baseline)
                self.assertFalse(result["available"])
                self.assertIn("rhi_identity_unavailable", result["reasons"])

    def test_runtime_log_proves_version_without_rewriting_report(self):
        log = self.root / "unreal.log"
        log.write_text("LogInit: Engine Version: 5.7.1-0+UE5\n", encoding="utf-8")
        report = {"render_chain": {"generation": {"log_path": str(log)}}}
        before = log.read_bytes()
        self.assertEqual(evidence_contract.engine_version_from_evidence(report), "5.7")
        self.assertEqual(log.read_bytes(), before)

    def test_conflicting_log_and_binary_binding_is_unavailable(self):
        log = self.root / "unreal.log"
        log.write_text("LogInit: Engine Version: 5.7.1-0+UE5\n", encoding="utf-8")
        report = {"binary_binding": {"engine_version": "5.4"},
                  "render_chain": {"generation": {"log_path": str(log)}}}
        self.assertEqual(evidence_contract.engine_version_from_evidence(report), "")

    def history(self, version, value):
        log = self.root / f"{version}-runtime.log"
        log.write_text(f"LogInit: Engine Version: {version}.1-0+UE5\n"
                       "LogRHI: RHI D3D12 with Feature Level SM6 is supported and will be used.\n", encoding="utf-8")
        report = self.root / f"{version}-report.json"
        report.write_text(json.dumps({"engine_version": version, "render_chain": {"generation": {"log_path": str(log)}}}), encoding="utf-8")
        return {"run": version, "report_path": str(report), "timestamp": 1, "rhi_backend": "D3D12",
                "stable_changed_ratio": value, "changed_ratio": value,
                "stable_mean_abs_error": value, "mean_abs_error": value,
                "source_input": {"source_design_artifact_hash": "source", "reference_image_hash": "reference",
                                 "engine_version": version},
                "binary_binding": {"engine_version": version}}

    def test_same_input_history_is_partitioned_by_engine(self):
        current = {"source_signature": {"sha1": "source"},
                   "rhi_backend": "D3D12",
                   "effective_reference_signature": {"sha1": "reference"},
                   "binary_binding": {"engine_version": "5.4"}}
        other = self.history("5.7", 0.01)
        own = self.history("5.4", 0.20)
        current["report_path"] = own["report_path"]
        best = {"stable_changed_ratio": 0.01, "metric_sources": {"stable_changed_ratio": other},
                "admitted_sources": [other, own]}
        self.assertFalse(validate.evaluate_historical_best_identity(current, best)["compatible"])
        selected = validate.select_case_evidence_epoch(current, best)
        self.assertEqual(selected["run"], "5.4")
        self.assertEqual(selected["stable_changed_ratio"], 0.20)
        self.assertEqual(selected["admitted_sources"], [own])

    def test_source_input_comparison_rejects_other_engine(self):
        a = dict.fromkeys(evidence_contract.SOURCE_INPUT_COMPARE_FIELDS, "same")
        b = dict(a)
        a["engine_version"], b["engine_version"] = "5.4", "5.7"
        compatible, mismatches = evidence_contract.same_source_input(a, b)
        self.assertFalse(compatible)
        self.assertEqual([item["field"] for item in mismatches], ["engine_version"])


if __name__ == "__main__":
    unittest.main()
