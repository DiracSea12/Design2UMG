import tempfile
from pathlib import Path

import design2umg_rebuild_admissible_best as rebuild
import design2umg_render_rule as render_rule
import design2umg_validate_run as validate_run


def write_json(path: Path, payload: dict) -> None:
    rebuild.write_json(path, payload)


def compatible_report() -> dict:
    size = [100, 80]
    return {
        "comparison_mode": "native_size",
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
) -> None:
    run_root = automation_root / name
    case_root = run_root / "case_a"
    case_root.mkdir(parents=True)
    if include_run_scope:
        write_json(run_root / "run_scope.json", {"formal_admissible_candidate": True, "case_names": ["case_a"]})
    report_path = case_root / "compare_report.json"
    write_json(report_path, compatible_report())
    write_json(
        run_root / "summary.json",
        {
            "total": 47,
            "blocked_cases": blocked_cases,
            "completeness": {
                "blocked": completeness_blocked,
                "discovered_total": 47,
                "reported_total": 47,
            },
            "no_regression": {
                "available": no_regression_available,
                "failed": no_regression_failed,
            },
            "cases": [
                {
                    "name": "case_a",
                    "report_path": str(report_path),
                    "stable_changed_ratio": 0.1,
                    "changed_ratio": 0.2,
                    "stable_mae": 1.0,
                    "mae": 2.0,
                    "structure_required": True,
                    "structure_passed": True,
                }
            ],
        },
    )
    if historical_validation is not None:
        write_json(run_root / "validation_against_historical_best.json", historical_validation)


def assert_skipped(manifest: dict, run_name: str, reason: str) -> None:
    assert run_name in manifest["skipped_runs"], f"{run_name} was not skipped"
    assert manifest["skipped_runs_by_reason"].get(reason) == 1, manifest["skipped_runs_by_reason"]


def make_historical_best(automation_root: Path, best_ratio: float) -> Path:
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
                }
            }
        },
    )
    return best_path


def make_current_validation_run(automation_root: Path, name: str, ratio: float) -> Path:
    run_root = automation_root / name
    case_root = run_root / "case_a"
    case_root.mkdir(parents=True)
    report = compatible_report()
    report["comparison_candidate_size"] = [100, 80]
    report["stable_changed_ratio"] = ratio
    report["changed_ratio"] = ratio
    report["stable_mean_abs_error"] = 1.0
    report["mean_abs_error"] = 2.0
    report_path = case_root / "compare_report.json"
    write_json(report_path, report)
    write_json(run_root / "run_scope.json", {"formal_admissible_candidate": True, "case_names": ["case_a"]})
    write_json(
        run_root / "summary.json",
        {
            "total": 1,
            "blocked_cases": 0,
            "completeness": {"blocked": False, "discovered_total": 1, "reported_total": 1},
            "no_regression": {"available": 1, "failed": 0},
            "cases": [
                {
                    "name": "case_a",
                    "report_path": str(report_path),
                    "stable_changed_ratio": ratio,
                    "changed_ratio": ratio,
                    "stable_mae": 1.0,
                    "mae": 2.0,
                    "structure_required": True,
                    "structure_passed": True,
                }
            ],
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


def main() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        automation_root = Path(temp_dir)
        make_run(automation_root, "_formal_good")
        make_run(automation_root, "_formal_blocked_cases", blocked_cases=1)
        make_run(automation_root, "_formal_completeness_blocked", completeness_blocked=True)
        make_run(automation_root, "_formal_no_regression_failed", no_regression_available=47, no_regression_failed=1)
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
    assert_validate_run_pixel_quantum_epsilon()
    assert_validate_run_reason_details()
    print("admissible best rebuild selftest passed")


if __name__ == "__main__":
    main()
