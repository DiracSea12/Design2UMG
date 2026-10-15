import tempfile
from pathlib import Path

import design2umg_rebuild_admissible_best as rebuild
import design2umg_render_rule as render_rule
import design2umg_validate_run as validate_run


def write_json(path: Path, payload: dict) -> None:
    if "ue_result" in payload and "engine_version" in payload:
        log = path.parent / "runtime.log"
        log.write_text("LogInit: Engine Version: 5.7.1-0+UE5\n"
                       "LogRHI: RHI D3D12 with Feature Level SM6 is supported and will be used.\n", encoding="utf-8")
        payload = dict(payload, render_chain=dict(payload["render_chain"], generation={"log_path": str(log)}))
    rebuild.write_json(path, payload)


def compatible_report() -> dict:
    size = [100, 80]
    return {
        "comparison_mode": "native_size",
        "engine_version": "5.7",
        "rhi_backend": "D3D12",
        "candidate_size": size,
        "reference_context": {
            "render_size_authority": {
                "source": render_rule.DESIGN_SIZE_AUTHORITY_SOURCE,
                "design_size": size,
            }
        },
        "ue_result": {
            "design_time_width": size[0],
            "design_time_height": size[1],
            "render_width": size[0],
            "render_height": size[1],
            "pipeline_determinism_rule": render_rule.PIPELINE_DETERMINISM_RULE,
            "transient_state_authority": render_rule.PIPELINE_DETERMINISM_AUTHORITY,
            "transient_state_reset_before_render": True,
            "text_measurement_cache_scope": render_rule.TEXT_MEASUREMENT_CACHE_SCOPE,
            "text_single_line_height_probe_authority": render_rule.TEXT_SINGLE_LINE_HEIGHT_PROBE_AUTHORITY,
            "force_reload_package_requested": True,
            "asset_package_reload_authority": render_rule.ASSET_PACKAGE_RELOAD_AUTHORITY,
            "slate_font_cache_authority": render_rule.SLATE_FONT_CACHE_AUTHORITY,
        },
        "render_chain": {
            "final_render_mode": "direct",
        },
    }


def binary_binding_payload() -> dict:
    return {
        "plugin_source_snapshot": "commit:test",
        "design2umg_core_dll": {
            "path": "G:/SampleProject/Binaries/Win64/UnrealEditor-Design2UMGCore.dll",
            "sha256": "c" * 64,
            "size": 123,
            "mtime": "2026-06-08T00:00:00Z",
        },
        "design2umg_editor_dll": {
            "path": "G:/SampleProject/Binaries/Win64/UnrealEditor-Design2UMGEditor.dll",
            "sha256": "e" * 64,
            "size": 456,
            "mtime": "2026-06-08T00:00:00Z",
        },
        "validation_project_path": "G:/SampleProject/SampleProject.uproject",
        "engine_root": "E:/MockEngineRoot",
        "engine_version": "5.7",
    }


def make_run(
    automation_root: Path,
    name: str,
    *,
    include_run_scope: bool = True,
    blocked_cases: int = 0,
    completeness_blocked: bool = False,
    no_regression_available: int = 1,
    no_regression_failed: int = 0,
    historical_validation: dict | None = None,
    stable_changed_ratio: float = 0.1,
    changed_ratio: float = 0.2,
    stable_mae: float = 1.0,
    mae: float = 2.0,
    binary_bound: bool = False,
    source_hash: str = "source-a",
    reference_hash: str = "reference-a",
    include_pattern: str = "case_a",
    missing_source_compare_field: str | None = None,
) -> Path:
    run_root = automation_root / name
    case_root = run_root / "case_a"
    case_root.mkdir(parents=True)
    if include_run_scope:
        write_json(
            run_root / "run_scope.json",
            {
                "formal_admissible_candidate": True,
                "case_names": ["case_a"],
                "include_pattern": include_pattern,
                "validation_project_path": "G:/SampleProject/SampleProject.uproject",
                "case_inputs": [
                    {
                        "name": "case_a",
                        "mode": "figma-source",
                        "source": "G:/UE5/Designs/source.json",
                        "source_signature": {"sha1": source_hash},
                    }
                ],
            },
        )
    report_path = case_root / "compare_report.json"
    report = compatible_report()
    if missing_source_compare_field != "reference_image_hash":
        report["effective_reference_signature"] = {"sha1": reference_hash}
    if missing_source_compare_field != "baseline_compare_report_hash":
        report["baseline_compare_report_hash"] = "baseline-a"
    report["reference_root_case_path"] = "G:/SampleProject/Saved/ReferenceRoot/case_a"
    if binary_bound:
        report["binary_binding"] = binary_binding_payload()
    write_json(report_path, report)
    case_results = [
        {
            "name": "case_a",
            "report_path": str(report_path),
            "stable_changed_ratio": stable_changed_ratio,
            "changed_ratio": changed_ratio,
            "stable_mae": stable_mae,
            "mae": mae,
            "structure_required": True,
            "structure_passed": True,
        }
    ]
    write_json(
        run_root / "summary.json",
        {
            "total": len(case_results),
            "blocked_cases": blocked_cases,
            "completeness": {
                "blocked": completeness_blocked,
                "discovered_total": len(case_results),
                "reported_total": len(case_results),
            },
            "no_regression": {
                "available": no_regression_available,
                "failed": no_regression_failed,
            },
            "cases": case_results,
        },
    )
    write_json(run_root / "validation_against_historical_best.json", historical_validation if historical_validation is not None else {
        "historical_best_checked": True, "blocked": False, "historical_best_regressions": [],
    })
    return run_root


def assert_skipped(manifest: dict, run_name: str, reason: str) -> None:
    assert run_name in manifest["skipped_runs"], f"{run_name} was not skipped"
    assert manifest["skipped_runs_by_reason"].get(reason) == 1, manifest["skipped_runs_by_reason"]


def make_historical_best(
    automation_root: Path,
    best_ratio: float,
    *,
    source_hash: str = "source-a",
    reference_hash: str = "reference-a",
) -> Path:
    best_path = automation_root / "_admissible_best_latest.json"
    report_path = automation_root / "_formal_best" / "case_a" / "compare_report.json"
    report_path.parent.mkdir(parents=True)
    best_report = compatible_report()
    best_report["comparison_candidate_size"] = [100, 80]
    best_report["stable_changed_ratio"] = best_ratio
    best_report["changed_ratio"] = best_ratio
    best_report["stable_mean_abs_error"] = 1.0
    best_report["mean_abs_error"] = 2.0
    write_json(report_path, best_report)
    source_input = {
        "source_design_artifact_hash": source_hash,
        "reference_image_hash": reference_hash,
    }
    metric_sources = {
        metric: {
            "run": "_formal_best",
            "report_path": str(report_path),
            "source_input": source_input,
        }
        for metric in (
            "stable_changed_ratio",
            "changed_ratio",
            "stable_mean_abs_error",
            "mean_abs_error",
        )
    }
    write_json(
        best_path,
        {
            "cases": {
                "case_a": {
                    "report_path": str(report_path),
                    "stable_changed_ratio": best_ratio,
                    "changed_ratio": best_ratio,
                    "stable_mean_abs_error": 1.0,
                    "mean_abs_error": 2.0,
                    "reference_authority": {"verification_level": "strong"},
                    "metric_sources": metric_sources,
                }
            }
        },
    )
    return best_path


def make_current_validation_run(
    automation_root: Path,
    name: str,
    ratio: float,
    *,
    source_hash: str = "source-a",
    reference_hash: str = "reference-a",
    include_pattern: str = "case_a",
    missing_source_compare_field: str | None = None,
) -> Path:
    run_root = automation_root / name
    case_root = run_root / "case_a"
    case_root.mkdir(parents=True)
    report = compatible_report()
    report["comparison_candidate_size"] = [100, 80]
    report["stable_changed_ratio"] = ratio
    report["changed_ratio"] = ratio
    report["stable_mean_abs_error"] = 1.0
    report["mean_abs_error"] = 2.0
    if missing_source_compare_field != "reference_image_hash":
        report["effective_reference_signature"] = {"sha1": reference_hash}
    if missing_source_compare_field != "baseline_compare_report_hash":
        report["baseline_compare_report_hash"] = "baseline-a"
    report["reference_root_case_path"] = "G:/SampleProject/Saved/ReferenceRoot/case_a"
    report_path = case_root / "compare_report.json"
    write_json(report_path, report)
    write_json(
        run_root / "run_scope.json",
        {
            "formal_admissible_candidate": True,
            "case_names": ["case_a"],
            "include_pattern": include_pattern,
            "validation_project_path": "G:/SampleProject/SampleProject.uproject",
            "case_inputs": [
                {
                    "name": "case_a",
                    "mode": "figma-source",
                    "source": "G:/UE5/Designs/source.json",
                    "source_signature": {"sha1": source_hash},
                }
            ],
        },
    )
    case_summary = {
        "name": "case_a",
        "report_path": str(report_path),
        "stable_changed_ratio": ratio,
        "changed_ratio": ratio,
        "stable_mae": 1.0,
        "mae": 2.0,
        "structure_required": True,
        "structure_passed": True,
        "source_signature": {"sha1": source_hash},
    }
    if missing_source_compare_field != "reference_image_hash":
        case_summary["effective_reference_signature"] = {"sha1": reference_hash}
    write_json(
        run_root / "summary.json",
        {
            "total": 1,
            "blocked_cases": 0,
            "completeness": {"blocked": False, "discovered_total": 1, "reported_total": 1},
            "no_regression": {"available": 1, "failed": 0},
            "cases": [case_summary],
        },
    )
    return run_root


def assert_validate_run_pixel_quantum_epsilon() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        pixel_quantum = 1.0 / (100 * 80)
        best_path = make_historical_best(automation_root, 0.1)

        one_pixel_run = make_current_validation_run(automation_root, "_formal_current_one_pixel", 0.1 + pixel_quantum)
        one_pixel_validation = validate_run.validate_run(one_pixel_run, admissible_best_path=best_path)
        assert not one_pixel_validation["blocked"], one_pixel_validation

        two_pixel_run = make_current_validation_run(automation_root, "_formal_current_two_pixels", 0.1 + (2 * pixel_quantum))
        two_pixel_validation = validate_run.validate_run(two_pixel_run, admissible_best_path=best_path)
        assert two_pixel_validation["blocked"], two_pixel_validation
        assert two_pixel_validation["historical_best_regressions"], two_pixel_validation


def assert_validate_run_reason_details() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        run_root = Path(temp_dir) / "_formal_missing_report"
        case_root = run_root / "case_a"
        case_root.mkdir(parents=True)

        write_json(run_root / "run_scope.json", {"formal_admissible_candidate": True, "case_names": ["case_a"]})
        missing_report_path = case_root / "compare_report.json"
        write_json(
            run_root / "summary.json",
            {
                "cases": [
                    {
                        "name": "case_a",
                        "report_path": str(missing_report_path),
                    }
                ]
            },
        )

        validation = validate_run.validate_run(run_root)
        assert validation["blocked"], validation
        assert "missing_compare_report" in validation["blocked_reasons"], validation
        assert validation["missing_compare_report_details"], validation
        assert validation["missing_compare_report_details"][0]["case"] == "case_a", validation
        assert str(missing_report_path.resolve()) in validation["missing_compare_report_details"][0]["checked_paths"], validation

        blocked_detail_by_reason = {
            item["reason"]: item
            for item in validation["blocked_reason_details"]
        }
        missing_report_detail = blocked_detail_by_reason.get("missing_compare_report")
        assert missing_report_detail is not None, validation
        assert missing_report_detail.get("detail_field") == "missing_compare_report_details", validation


def make_quarantine_manifest(
    automation_root: Path,
    *,
    best_binary_bound: bool = False,
    repeatable_best: bool = False,
    missing_source_compare_field: str | None = None,
) -> Path:
    make_run(
        automation_root,
        "_formal_one_off_best",
        stable_changed_ratio=0.09,
        changed_ratio=0.09,
        stable_mae=1.0,
        mae=2.0,
        binary_bound=best_binary_bound,
        missing_source_compare_field=missing_source_compare_field,
    )
    if repeatable_best:
        make_run(
            automation_root,
            "_formal_repeat_best",
            stable_changed_ratio=0.09,
            changed_ratio=0.09,
            stable_mae=1.0,
            mae=2.0,
        )
    make_run(
        automation_root,
        "_formal_next_best",
        stable_changed_ratio=0.10,
        changed_ratio=0.10,
        stable_mae=1.0,
        mae=2.0,
    )
    manifest = rebuild.rebuild_historical_best_manifest(automation_root)
    manifest_path = automation_root / "_admissible_best_latest.json"
    write_json(manifest_path, manifest)
    return manifest_path


def assert_one_off_best_can_be_quarantined() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        manifest_path = make_quarantine_manifest(automation_root)
        current_run = make_current_validation_run(automation_root, "_formal_current", 0.10)

        validation = validate_run.validate_run(current_run, admissible_best_path=manifest_path)
        assert not validation["blocked"], validation
        assert validation["historical_best_status"] == "pass_with_quarantined_one_off_best", validation
        assert "historical_best_regression" not in validation["blocked_reasons"], validation
        quarantined = validation["historical_best_quarantined_one_off_sources"]
        assert quarantined, validation
        assert quarantined[0]["case"] == "case_a", validation
        assert quarantined[0]["quarantined_run"] == "_formal_one_off_best", validation
        assert quarantined[0]["next_best_values"]["stable_changed_ratio"] == 0.10, validation
        assert validation["expected_case_names"] == ["case_a"], validation


def assert_normal_historical_best_blocks_worse_current() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        best_path = make_historical_best(automation_root, 0.1)
        current_run = make_current_validation_run(automation_root, "_formal_current_worse", 0.2)

        validation = validate_run.validate_run(current_run, admissible_best_path=best_path)
        assert validation["blocked"], validation
        assert validation["historical_best_status"] == "blocked", validation
        assert "historical_best_regression" in validation["blocked_reasons"], validation
        assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_repeatable_or_binary_bound_best_blocks_quarantine() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        repeatable_manifest = make_quarantine_manifest(automation_root, repeatable_best=True)
        current_run = make_current_validation_run(automation_root, "_formal_current_repeatable", 0.10)

        repeatable_validation = validate_run.validate_run(current_run, admissible_best_path=repeatable_manifest)
        assert repeatable_validation["blocked"], repeatable_validation
        assert repeatable_validation["historical_best_status"] == "blocked", repeatable_validation
        assert not repeatable_validation["historical_best_quarantined_one_off_sources"], repeatable_validation

    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        binary_bound_manifest = make_quarantine_manifest(automation_root, best_binary_bound=True)
        current_run = make_current_validation_run(automation_root, "_formal_current_binary_bound", 0.10)

        binary_bound_validation = validate_run.validate_run(current_run, admissible_best_path=binary_bound_manifest)
        assert binary_bound_validation["blocked"], binary_bound_validation
        assert binary_bound_validation["historical_best_status"] == "blocked", binary_bound_validation
        assert not binary_bound_validation["historical_best_quarantined_one_off_sources"], binary_bound_validation


def assert_quarantine_denied_when_current_is_worse_than_next_best() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        manifest_path = make_quarantine_manifest(automation_root)
        current_run = make_current_validation_run(automation_root, "_formal_current_worse_than_next", 0.11)

        validation = validate_run.validate_run(current_run, admissible_best_path=manifest_path)
        assert validation["blocked"], validation
        assert validation["historical_best_status"] == "blocked", validation
        assert "historical_best_regression" in validation["blocked_reasons"], validation
        assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_missing_binary_binding_does_not_auto_pass() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        make_run(
            automation_root,
            "_formal_one_off_best",
            stable_changed_ratio=0.09,
            changed_ratio=0.09,
            stable_mae=1.0,
            mae=2.0,
        )
        manifest = rebuild.rebuild_historical_best_manifest(automation_root)
        manifest_path = automation_root / "_admissible_best_latest.json"
        write_json(manifest_path, manifest)
        current_run = make_current_validation_run(automation_root, "_formal_current_no_next_best", 0.10)

        validation = validate_run.validate_run(current_run, admissible_best_path=manifest_path)
        assert validation["blocked"], validation
        assert validation["historical_best_status"] == "blocked", validation
        assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_same_source_input_mismatch_denies_quarantine() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        manifest_path = make_quarantine_manifest(automation_root)
        current_run = make_current_validation_run(
            automation_root,
            "_formal_current_mismatch",
            0.10,
            source_hash="different-source",
        )
        validation = validate_run.validate_run(current_run, admissible_best_path=manifest_path)
        assert not validation["blocked"], validation
        assert validation["historical_best_status"] == "pass_with_new_evidence_epoch", validation
        assert validation["historical_best_epoch_seeded_cases"] == ["case_a"], validation
        assert validation["historical_best_identity_incompatible_cases"], validation
        assert not validation["historical_best_regressions"], validation
        assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_historical_best_identity_contract() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        best_path = make_historical_best(automation_root, 0.1)

        source_mismatch_run = make_current_validation_run(
            automation_root,
            "_formal_current_source_mismatch",
            0.1,
            source_hash="source-b",
        )
        source_mismatch = validate_run.validate_run(source_mismatch_run, admissible_best_path=best_path)
        assert not source_mismatch["blocked"], source_mismatch
        assert source_mismatch["historical_best_epoch_seeded_cases"] == ["case_a"], source_mismatch
        assert {
            item["field"]
            for item in source_mismatch["historical_best_identity_incompatible_cases"][0]["mismatches"]
        } == {"source_signature.sha1"}, source_mismatch

        reference_mismatch_run = make_current_validation_run(
            automation_root,
            "_formal_current_reference_mismatch",
            0.1,
            reference_hash="reference-b",
        )
        reference_mismatch = validate_run.validate_run(reference_mismatch_run, admissible_best_path=best_path)
        assert not reference_mismatch["blocked"], reference_mismatch
        assert reference_mismatch["historical_best_epoch_seeded_cases"] == ["case_a"], reference_mismatch
        assert {
            item["field"]
            for item in reference_mismatch["historical_best_identity_incompatible_cases"][0]["mismatches"]
        } == {"effective_reference_signature.sha1"}, reference_mismatch

        matching_run = make_current_validation_run(automation_root, "_formal_current_matching", 0.1)
        matching = validate_run.validate_run(matching_run, admissible_best_path=best_path)
        assert not matching["blocked"], matching
        assert matching["historical_best_status"] == "normal_pass", matching
        assert not matching["historical_best_epoch_seeded_cases"], matching


def assert_missing_required_source_input_field_denies_quarantine(field_name: str) -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        manifest_path = make_quarantine_manifest(
            automation_root,
            missing_source_compare_field=field_name,
        )
        current_run = make_current_validation_run(
            automation_root,
            "_formal_current_missing_source_input",
            0.10,
            missing_source_compare_field=field_name,
        )
        validation = validate_run.validate_run(current_run, admissible_best_path=manifest_path)
        if field_name == "reference_image_hash":
            # Missing evidence is not a new, fully identified input epoch.
            assert validation["blocked"], validation
            assert "current_evidence_identity_missing" in validation["blocked_reasons"], validation
            assert not validation["historical_best_epoch_seeded_cases"], validation
        else:
            assert validation["blocked"], validation
            assert validation["historical_best_status"] == "blocked", validation
            assert "historical_best_regression" in validation["blocked_reasons"], validation
            assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_historical_best_status_normal_pass() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        best_path = make_historical_best(automation_root, 0.1)
        current_run = make_current_validation_run(automation_root, "_formal_current_equal", 0.1)

        validation = validate_run.validate_run(current_run, admissible_best_path=best_path)
        assert not validation["blocked"], validation
        assert validation["historical_best_status"] == "normal_pass", validation
        assert not validation["historical_best_quarantined_one_off_sources"], validation


def assert_collect_best_cases_uses_coherent_case_source() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        make_run(
            automation_root,
            "_formal_source_a",
            stable_changed_ratio=0.09,
            changed_ratio=0.20,
            stable_mae=1.5,
            mae=2.5,
        )
        make_run(
            automation_root,
            "_formal_source_b",
            stable_changed_ratio=0.10,
            changed_ratio=0.18,
            stable_mae=1.0,
            mae=2.0,
        )
        manifest = rebuild.rebuild_historical_best_manifest(automation_root)

    case_entry = manifest["cases"]["case_a"]
    expected_report_path = str((automation_root / "_formal_source_a" / "case_a" / "compare_report.json").resolve())
    assert case_entry["run"] == "_formal_source_a", case_entry
    assert case_entry["report_path"] == expected_report_path, case_entry
    assert case_entry["stable_changed_ratio"] == 0.09, case_entry
    assert case_entry["changed_ratio"] == 0.20, case_entry
    assert case_entry["stable_mean_abs_error"] == 1.5, case_entry
    assert case_entry["mean_abs_error"] == 2.5, case_entry
    expected_metric_values = {
        "stable_changed_ratio": 0.09,
        "changed_ratio": 0.20,
        "stable_mean_abs_error": 1.5,
        "mean_abs_error": 2.5,
    }
    assert set(case_entry["metric_sources"]) == set(expected_metric_values), case_entry
    assert {source["run"] for source in case_entry["metric_sources"].values()} == {"_formal_source_a"}, case_entry
    assert {source["report_path"] for source in case_entry["metric_sources"].values()} == {case_entry["report_path"]}, case_entry
    for metric_name, expected_value in expected_metric_values.items():
        metric_source = case_entry["metric_sources"][metric_name]
        assert metric_source["run"] == "_formal_source_a", case_entry
        assert metric_source["report_path"] == expected_report_path, case_entry
        assert metric_source["value"] == expected_value, case_entry



def main() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        make_run(automation_root, "_formal_good")
        make_run(automation_root, "_formal_blocked_cases", blocked_cases=1)
        make_run(automation_root, "_formal_completeness_blocked", completeness_blocked=True)
        make_run(automation_root, "_formal_no_regression_failed", no_regression_available=1, no_regression_failed=1)
        make_run(
            automation_root,
            "_formal_historical_blocked",
            historical_validation={
                "blocked": True,
                "blocked_reasons": ["historical_best_regression"],
                "historical_best_regressions": [{"case": "case_a"}],
            },
        )
        make_run(
            automation_root,
            "_formal_historical_regressed",
            historical_validation={
                "blocked": False,
                "blocked_reasons": [],
                "historical_best_regressions": [{"case": "case_a"}],
            },
        )
        make_run(automation_root, "_formal_legacy_name_without_scope", include_run_scope=False)
        write_json(
            automation_root / "_admissible_best_cache.json",
            {
                "version": rebuild.CACHE_VERSION,
                "generated_at": "",
                "automation_root": "",
                "entries": {},
                "runs": {
                    "_formal_good": {
                        "collision_cases": {
                            "case_a": True,
                        },
                    },
                },
            },
        )

        manifest = rebuild.rebuild_historical_best_manifest(automation_root)
        cache_payload = rebuild.load_json(automation_root / "_admissible_best_cache.json")

    assert manifest["runs"] == ["_formal_good"], manifest["runs"]
    assert manifest["admissible_run_count"] == 1
    assert manifest["case_count"] == 1
    assert_skipped(manifest, "_formal_blocked_cases", "summary_blocked_cases")
    assert_skipped(manifest, "_formal_completeness_blocked", "summary_completeness_blocked")
    assert_skipped(manifest, "_formal_no_regression_failed", "summary_no_regression_failed")
    assert_skipped(manifest, "_formal_historical_blocked", "historical_validation_blocked")
    assert_skipped(manifest, "_formal_historical_regressed", "historical_validation_regressed")
    assert_skipped(manifest, "_formal_legacy_name_without_scope", "run_scope_missing")
    assert "runs" not in cache_payload, cache_payload
    assert "output_collision" not in manifest["skipped_case_results_by_reason"], manifest["skipped_case_results_by_reason"]
    assert_collect_best_cases_uses_coherent_case_source()
    assert_validate_run_pixel_quantum_epsilon()
    assert_validate_run_reason_details()
    assert_normal_historical_best_blocks_worse_current()
    assert_one_off_best_can_be_quarantined()
    assert_repeatable_or_binary_bound_best_blocks_quarantine()
    assert_quarantine_denied_when_current_is_worse_than_next_best()
    assert_missing_binary_binding_does_not_auto_pass()
    assert_same_source_input_mismatch_denies_quarantine()
    assert_historical_best_identity_contract()
    assert_missing_required_source_input_field_denies_quarantine("reference_image_hash")
    assert_missing_required_source_input_field_denies_quarantine("baseline_compare_report_hash")
    assert_historical_best_status_normal_pass()
    print("admissible best rebuild selftest passed")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temp:
        run = make_run(Path(temp), "missing_evidence")
        summary = rebuild.load_json(run / "summary.json")
        summary["no_regression"] = {"available": 0, "failed": 0}
        assert not rebuild.classify_summary_admissibility(run, summary)[0], "Zero evidence is not a pass"
        summary.pop("no_regression")
        assert not rebuild.classify_summary_admissibility(run, summary)[0], "Missing evidence is not a pass"
        summary["no_regression"] = {"available": 1, "failed": 0}
        (run / "validation_against_historical_best.json").unlink(missing_ok=True)
        assert not rebuild.classify_summary_admissibility(run, summary)[0], "Missing historical validation is not a pass"
    main()
