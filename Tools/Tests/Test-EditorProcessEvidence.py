"""Editor validity is produced from terminal logs and expected result files."""
import json
import itertools
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import design2umg_auto_verify_suite as suite


class EditorProcessEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def execute(self, code, *, normal_exit=True, crash=False, driver_complete=True,
                missing_result=False, malformed_result=False):
        script = self.root / "editor_driver_shard-01.py"
        results = [self.root / "generation.json", self.root / "asset-render.json"]
        state = {"steps": {name: {"result_json_path": path, "spec_path": self.root / (name + "-spec.json")}
                           for name, path in zip(("generation", "asset_render"), results)}}
        shard = {"id": 0, "states": [state], "spec_paths": ["generate", "render"],
                 "result_paths": results}

        def driver(*args):
            for path in results:
                path.write_text(json.dumps({"success": True}), encoding="utf-8")
            if missing_result:
                results[-1].unlink()
            if malformed_result:
                results[-1].write_text("{", encoding="utf-8")
            log = Path(args[4])
            text = "LogInit: Engine Version: 5.7.1-0+UE5\n"
            if normal_exit:
                text += "LogExit: Exiting.\n"
            if crash:
                text += "LogWindows: Error: Unhandled Exception: EXCEPTION_ACCESS_VIOLATION\n"
                text += "LogWindows: Could not start crash report client using CrashReportClient.exe\n"
            log.write_text(text, encoding="utf-8")
            suite.get_editor_driver_log_path(script).write_text(
                "2026-10-05T00:00:00Z driver:complete\n" if driver_complete else
                "2026-10-05T00:00:00Z driver:run:generate\n", encoding="utf-8")
            return code

        with patch.object(suite, "write_editor_driver_script", return_value=script), \
                patch.object(suite, "run_editor_driver", side_effect=driver):
            result = suite.run_execution_shard(shard, self.root, "editor", "project", 60, 60, "")
        return result, state

    def summary(self, result, *, codes=None):
        args = SimpleNamespace(parallel_editors=1, idle_timeout_sec=60,
            startup_timeout_sec=60, editor_isolation="shard", post_workers=1,
            include="", exclude="", reference_cache="read-write", native_self_check="off",
            ir_golden="", geometry_golden="")
        return suite.build_suite_summary(args=args, editor_exit_code=result["exit_code"],
            editor_exit_codes=codes if codes is not None else [result["exit_code"]],
            shards=[{"id": 0}], shard_results=[result], post_workers=1,
            design_root=self.root, output_root=self.root, run_scope_path=self.root / "scope.json",
            reference_cache_root=None, suite_start_time=0, prep_completed_at=1,
            editor_completed_at=2, postprocess_completed_at=3, case_results=[],
            completeness={"blocked": False})

    def test_normal_nonzero_exit_produces_valid_summary_and_steps(self):
        result, state = self.execute(3221225477)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 1, "failed": 0})
        for step in state["steps"].values():
            self.assertTrue(step["editor_process_evidence"]["valid"])
        self.assertEqual(result["exit_code"], 3221225477)

    def test_normal_zero_exit_is_valid(self):
        result, _ = self.execute(0)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 1, "failed": 0})

    def test_driver_launch_discards_previous_process_outputs(self):
        script = self.root / "editor_driver_shard-01.py"
        script.write_text("# driver", encoding="utf-8")
        driver_log = suite.get_editor_driver_log_path(script)
        driver_log.write_text("previous driver:start\nprevious driver:complete\n", encoding="utf-8")
        result = self.root / "previous-result.json"
        result.write_text('{"success":true}', encoding="utf-8")
        process = SimpleNamespace(poll=lambda: 0)
        with patch.object(suite.subprocess, "Popen", return_value=process), \
                patch.object(suite.time, "time", side_effect=itertools.count(0, 1000)):
            with self.assertRaises(suite.EditorDriverStartError):
                suite.run_editor_driver("EDITOR", "PROJECT", script, [result],
                                        self.root / "editor.log", 1, 1)
        self.assertFalse(driver_log.exists())
        self.assertFalse(result.exists())

    def test_driver_launch_keeps_fresh_outputs(self):
        script = self.root / "editor_driver_shard-01.py"
        script.write_text("# driver", encoding="utf-8")
        driver_log = suite.get_editor_driver_log_path(script)
        driver_log.write_text("previous driver:start\nprevious driver:complete\n", encoding="utf-8")
        result = self.root / "result.json"
        result.write_text('{"previous":true}', encoding="utf-8")
        log = self.root / "editor.log"

        def launch(*args, **kwargs):
            self.assertFalse(driver_log.exists())
            self.assertFalse(result.exists())
            driver_log.write_text("current driver:start\ncurrent driver:complete\n", encoding="utf-8")
            result.write_text('{"success":true}', encoding="utf-8")
            kwargs["stdout"].write("LogExit: Exiting.\n")
            return SimpleNamespace(poll=lambda: 0)

        with patch.object(suite.subprocess, "Popen", side_effect=launch):
            code = suite.run_editor_driver("EDITOR", "PROJECT", script, [result], log, 1, 1)
        self.assertTrue(suite.collect_editor_process_evidence(code, log, [result], driver_log)["valid"])
        self.assertEqual(json.loads(result.read_text(encoding="utf-8")), {"success": True})

    def test_zero_exit_and_completed_outputs_without_exit_marker_is_valid(self):
        result, _ = self.execute(0, normal_exit=False)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 1, "failed": 0})

    def test_explicit_crash_invalidates_complete_outputs(self):
        result, _ = self.execute(3221225477, normal_exit=False, crash=True)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 0, "failed": 1})
        self.assertTrue(result["editor_process_evidence"]["crash_reported"])

    def test_crash_after_normal_exit_is_still_invalid(self):
        result, _ = self.execute(3221225477, crash=True)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 0, "failed": 1})

    def test_nonfatal_ensure_report_does_not_invalidate_normal_completion(self):
        result, _ = self.execute(0)
        log = Path(result["log_path"])
        log.write_text("LogOutputDevice: Error: Ensure condition failed: invariant\n"
                       "LogWindows: Could not start crash report client using CrashReportClient.exe\n"
                       "LogExit: Exiting.\n", encoding="utf-8")
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 1, "failed": 0})

    def test_nonzero_exit_without_normal_exit_evidence_is_unconfirmed(self):
        result, _ = self.execute(17, normal_exit=False)
        self.assertFalse(result["editor_process_evidence"]["valid"])
        self.assertFalse(result["editor_process_evidence"]["crash_reported"])

    def test_missing_or_malformed_output_is_invalid_even_after_normal_exit(self):
        for option in ("missing_result", "malformed_result"):
            with self.subTest(option=option):
                result, _ = self.execute(0, **{option: True})
                self.assertEqual(self.summary(result)["editor_process"], {"passed": 0, "failed": 1})

    def test_unfinished_driver_is_invalid(self):
        result, _ = self.execute(0, driver_complete=False)
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 0, "failed": 1})

    def test_summary_does_not_trust_a_forged_validity_field(self):
        result, _ = self.execute(3221225477, normal_exit=False, crash=True)
        result["editor_process_evidence"] = {"valid": True}
        self.assertEqual(self.summary(result)["editor_process"], {"passed": 0, "failed": 1})

    def test_summary_rejects_exit_code_binding_mismatch(self):
        result, _ = self.execute(0)
        with self.assertRaisesRegex(ValueError, "exit codes"):
            self.summary(result, codes=[17])

    def test_case_report_uses_produced_validity_not_the_nonzero_exit_code(self):
        for crash in (False, True):
            with self.subTest(crash=crash):
                result, state = self.execute(3221225477, crash=crash)
                state.update(args=SimpleNamespace(mode="asset"), case={"name": "case"},
                    candidate_path=self.root / "missing.png", reference_validation={},
                    reference_context={}, native_plan={"available": False})
                with patch.object(suite, "write_render_failure_report", return_value=self.root / "failure.json"):
                    report = suite.process_case_result(state, result["exit_code"], Path(result["log_path"]))
                self.assertEqual(report["editor_process_failed"], crash)


if __name__ == "__main__":
    unittest.main()
