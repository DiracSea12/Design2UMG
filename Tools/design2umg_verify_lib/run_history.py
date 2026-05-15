from pathlib import Path


def write_historical_best_validation(
    output_root: Path,
    automation_root: Path,
    rebuild_best_module,
    validate_run_module,
    write_json_fn,
    *,
    print_paths: bool = False,
) -> dict:
    manifest_path, manifest, manifest_source = rebuild_best_module.resolve_admissible_best_manifest(automation_root)
    run_manifest_path = output_root / "historical_best_manifest.json"
    rebuild_best_module.write_json(run_manifest_path, manifest)

    validation = validate_run_module.validate_run(
        output_root,
        admissible_best_path=manifest_path,
    )
    validation_path = output_root / "validation_against_historical_best.json"
    write_json_fn(validation_path, validation)
    print(f"[HISTORICAL_BEST] source={manifest_source}")
    if print_paths:
        print(f"[HISTORICAL_BEST] manifest={manifest_path}")
        print(f"[HISTORICAL_BEST] validation={validation_path}")

    return {
        "manifest_path": manifest_path,
        "run_manifest_path": run_manifest_path,
        "validation_path": validation_path,
        "validation": validation,
        "source": manifest_source,
    }
