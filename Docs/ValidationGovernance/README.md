# Validation Governance

Last verified: 2026-04-04

## Purpose

This directory keeps the checked-in governance records that remain meaningful after raw automation trees are trimmed.

These files are not a replacement for the full raw outputs under `Saved/Design2UMG/Automation`.
They are the canonical wrapper records that tell later maintainers:

- which admissible baseline artifacts are formal
- which historical cases were explicitly removed as non-reproducible
- which remaining blocker cases are still in scope
- how no-old-asset admissions must be recorded
- which blocked runs and crash signatures must survive release preparation

Cleanup batch scope, module boundaries, and the admissible evidence they rely on are tracked separately in `Docs/IncrementalCodeSimplificationBatches.md`.

## Canonical Records

- `AdmissibleBaselineManifest_20260404.json`
  Canonical wrapper around the current formal admissible baseline artifacts.

- `FidelityBlockerManifest_20260404.json`
  Canonical blocker-state wrapper plus the explicit admission schema for supported-scope problems that do not have a reproducible old asset.

- `BlockedRuns_20260331.json`
  Canonical blocked-run record that preserves the `g3` crash signature and the replacement official baseline lineage.

## Required Maintainer Flow

1. Refresh the history audit from `Saved/Design2UMG/Automation/_history_audit_<date>.json`.
2. Re-render surviving old assets under the real maintainer runtime boundary and update `_removed_nonreproducible_cases_<date>.json`.
3. Update `FidelityBlockerManifest_<date>.json` so the active blocker list contains only:
   - cases retained in the reproducible-focus set
   - or cases explicitly admitted without an old asset under the schema in that manifest
4. Run focused guard/probe batches for candidate fixes and keep the guard-set evidence with the blocker manifest.
5. Run the full admissible regression and wrap the formal result in `AdmissibleBaselineManifest_<date>.json`.
   Optionally validate the run root with `python Tools/design2umg_validate_run.py --output-root <run_root>`.
6. Preserve any blocked promotion or crash signature in `BlockedRuns_<date>.json`.

## Current 2026-04-04 State

- Formal full admissible run:
  `Saved/Design2UMG/Automation/_final_admissible_full_20260403_final/summary.json`
- Formal admissible best wrapper:
  `Saved/Design2UMG/Automation/_admissible_best_20260331.json`
- Explicitly removed non-reproducible cases:
  `Saved/Design2UMG/Automation/_removed_nonreproducible_cases_20260331.json`
- History audit:
  `Saved/Design2UMG/Automation/_history_audit_20260331.json`
- Current reproducible blocker state:
  `Docs/ValidationGovernance/FidelityBlockerManifest_20260404.json`

The current dated manifests mean:

- the formal 45-case admissible gate is `_final_admissible_full_20260403_final`
- all active reproducible fidelity blockers from the 2026-03-31 queue have been matched or surpassed in the current formal run
- the removed non-reproducible list remains the canonical explanation for why the workspace no longer uses the old 47-case discovered set as the formal release gate
