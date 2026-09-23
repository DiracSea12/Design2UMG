import sys
from pathlib import Path


def write_historical_best_validation(
    output_root: Path,
    automation_root: Path,
    rebuild_best_module,
    validate_run_module,
    write_json_fn,
    expected_case_names: set[str],
    *,
    print_paths: bool = False,
    publish_global_manifest: bool = True,
) -> dict:
    """Write historical best manifest and run validation against it.

    **Contract**: This function writes two files and emits optional diagnostics:
      - ``output_root / "historical_best_manifest.json"`` — the manifest snapshot
      - ``output_root / "validation_against_historical_best.json"`` — comparison result

    The three ``[HISTORICAL_BEST]`` print lines (source, manifest, validation) are
    **diagnostic output only**; they are routed to ``sys.stderr`` so they do not
    corrupt stdout which carries the formal contract lines (``[OK]``, ``[SUMMARY]``,
    etc.) from the suite driver.

    Args:
        output_root: Directory to write output files.
        automation_root: Root scanned for admissible run directories.
        rebuild_best_module: Module providing ``resolve_admissible_best_manifest()``.
        validate_run_module: Module providing ``validate_run()``.
        write_json_fn: JSON serialization function.
        print_paths: If True, emit manifest/validation paths to stderr.
        publish_global_manifest: Full runs publish their discovered scope globally;
            single-case runs write only their own manifest, using the same history.

    Returns:
        dict with keys: manifest_path, run_manifest_path, validation_path,
        validation, source.
    """
    run_manifest_path = output_root / "historical_best_manifest.json"
    # A single-case check still scans all history, but only publishes its own scope.
    destination = {} if publish_global_manifest else {"output_path": run_manifest_path}
    manifest_path, manifest, manifest_source = rebuild_best_module.resolve_admissible_best_manifest(
        automation_root, expected_case_names=expected_case_names, **destination
    )
    if manifest_path != run_manifest_path:
        rebuild_best_module.write_json(run_manifest_path, manifest)

    validation = validate_run_module.validate_run(
        output_root,
        admissible_best_path=manifest_path,
    )
    validation_path = output_root / "validation_against_historical_best.json"
    write_json_fn(validation_path, validation)
    print(f"[HISTORICAL_BEST] source={manifest_source}", file=sys.stderr)
    if print_paths:
        print(f"[HISTORICAL_BEST] manifest={manifest_path}", file=sys.stderr)
        print(f"[HISTORICAL_BEST] validation={validation_path}", file=sys.stderr)

    return {
        "manifest_path": manifest_path,
        "run_manifest_path": run_manifest_path,
        "validation_path": validation_path,
        "validation": validation,
        "source": manifest_source,
    }
