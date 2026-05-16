# Tools

This directory contains the maintainer and validation utilities for Design2UMG.

## Public-facing helpers

These scripts are intended for contributors who want to run local validation or reproduce a failure:

- `Run-Design2UMGAutomationTests.ps1`
- `Run-DesignerSnapshotValidation.ps1`
- `Run-LunarisStructureSnapshotValidation.ps1`

## Maintainer-only workflows

These files support release governance, historical-best validation, or internal automation flows:

- `design2umg_auto_verify.py`
- `design2umg_auto_verify_batch.py`
- `design2umg_rebuild_admissible_best.py`
- `design2umg_validate_run.py`
- `design2umg_render_rule.py`
- `design2umg_structure_guard_selftest.py`
- `design2umg_pinned_baseline.json`
- `Release/`
- `Invoke-ClaudeWorker.ps1`

## Python dependencies

The maintainer visual-validation scripts use Pillow for image loading and diffing:

```powershell
python -m pip install Pillow
```

No other external Python runtime package is required for the scripts listed above.

## Boundary rule

The public repository and packaged release artifacts must keep maintainer-only files out of the shipped boundary. Use `Tools/Release/Assert-PublicRepoBoundary.ps1` when checking that boundary.
