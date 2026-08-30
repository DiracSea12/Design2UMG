# Tools

This directory contains the maintainer and validation utilities for Design2UMG.

## Public-facing helpers

These scripts are intended for contributors who want to run local validation or reproduce a failure:

- `Run-Design2UMGAutomationTests.ps1`
- `Run-Design2UMGDeterminismSmoke.ps1`
- `Run-DesignerSnapshotValidation.ps1`
- `Run-LunarisStructureSnapshotValidation.ps1`

## Maintainer-only workflows

These files support release governance, historical-best validation, or internal automation flows:

- `design2umg_auto_verify.py`
- `design2umg_auto_verify_batch.py`
- `design2umg_determinism_smoke.py`
- `design2umg_determinism_smoke_selftest.py`
- `design2umg_fresh_process_reload_gate.py`
- `design2umg_fresh_process_reload_gate_selftest.py`
- `design2umg_rebuild_admissible_best.py`
- `design2umg_validate_run.py`
- `design2umg_render_rule.py`
- `design2umg_compatibility_selftest.py`
- `design2umg_diagnostics_selftest.py`
- `design2umg_run_history_selftest.py`
- `design2umg_batch_report_contract_selftest.py`
- `design2umg_structure_guard_selftest.py`
- `design2umg_validation_helpers_selftest.py`
- `design2umg_pinned_baseline.json`
- `Release/`

## Python dependencies

The maintainer visual-validation scripts use Pillow for image loading and diffing:

```powershell
python -m pip install Pillow
```

No other external Python runtime package is required for the scripts listed above.

The helper-level contract selftest is intentionally UE-free and can run on a plain GitHub runner:

```powershell
python Tools/design2umg_validation_helpers_selftest.py
python Tools/design2umg_compatibility_selftest.py
python Tools/design2umg_diagnostics_selftest.py
python Tools/design2umg_run_history_selftest.py
python Tools/design2umg_batch_report_contract_selftest.py
python Tools/design2umg_determinism_smoke_selftest.py
```

It covers batch case discovery, default Figma export root resolution, run-scope writing, execution sharding, worker-count resolution, and completeness aggregation. Runtime validation still requires an explicit real project path; the batch verifier fails closed when `--project` or `DESIGN2UMG_RUNTIME_PROJECT` is missing and never creates a HostProject fallback.

`design2umg_auto_verify.py` and `design2umg_auto_verify_batch.py` also seed runtime fonts from a discoverable `Content/Design2UMG/Fonts` directory. They copy the prepared UE assets into the runtime project; they do not download fonts or repair invalid font packages. A valid seed font must be a loadable `UFont` whose default typeface contains at least one entry backed by a valid `FontFace` or equivalent font data. Empty composite fonts and orphaned font-face files are invalid evidence for visual validation.

## Determinism smoke

`Run-Design2UMGDeterminismSmoke.ps1` is the focused pre-gate for automation-render parity work. Use it after touching automation render, package reload preparation, transient reset, automation font refresh, or bootstrap render preparation, and before spending a full 71-case formal run.

The canonical sentinel owner lives in `Tools/design2umg_determinism_smoke.py:CANONICAL_SENTINEL_CASES`. Do not hand-maintain a second list in the wrapper or selftest; inspect the current pair with:

```powershell
python Tools/design2umg_determinism_smoke.py print-sentinels
```

The smoke wrapper is fail-closed:

- `--project`, `--editor`, `--reference-root`, and the wrapper `-OutputRoot` must be explicit.
- It does not fall back to `DESIGN2UMG_RUNTIME_PROJECT`, `DESIGN2UMG_UNREAL_EDITOR`, `UE_ROOT`, a default reference root, or any HostProject.
- It validates that the current run root really contains the canonical sentinel cases, current compare reports, zero-drift `generation_vs_asset`, and passing `generation_vs_asset_structure` parity before it reports success.

Example:

```powershell
pwsh -NoProfile -File Tools/Run-Design2UMGDeterminismSmoke.ps1 `
  -ProjectPath <MaintainerRuntimeProject.uproject> `
  -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe `
  -ReferenceRoot <TrustedReferenceRoot> `
  -OutputRoot <OutputRoot>
```

This smoke is focused evidence only. It is not a replacement for the full formal 71-case no-regression closeout.

The batch verifier also appends batch-level timing drift history to `Saved/Design2UMG/Automation/batch_timing_history.jsonl`. This is an inspection aid for maintainers, not a release gate by itself.

## Boundary rule

The public repository and packaged release artifacts must keep maintainer-only files out of the shipped boundary. Use `Tools/Release/Assert-PublicRepoBoundary.ps1` when checking that boundary.
