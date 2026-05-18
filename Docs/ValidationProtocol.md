# Design2UMG Validation Protocol

Chinese companion: [ValidationProtocol.zh-CN.md](ValidationProtocol.zh-CN.md)

Last verified: 2026-05-18

## Purpose

This document defines the required validation method for Design2UMG changes.

Goal:
- keep validation repeatable
- avoid mixing incompatible evidence
- prevent false positives caused by stale PNGs or mismatched render paths

Public-note:
- Public-facing copies of this document should use logical placeholders such as `<UE5_Root>`, `<PluginRoot>`, and `<MaintainerRuntimeProject.uproject>`.
- The maintainer-owned canonical runtime boundary is an implementation role, not a public machine-path requirement.

## Validation Layers

This repository has four different validation layers. Do not collapse them into one label:

- `repository-hygiene`: public repo checks such as required-file presence and tracked maintainer-local path detection.
- `self-hosted build`: the manual BuildPlugin packaging workflow that runs on a maintainer-provided self-hosted Windows UE runner, validates the package boundary, and uploads the packaged plugin artifact.
- `public smoke/public validate`: contributor-reproducible local build plus focused validation. This is the public validation boundary.
- `maintainer formal admissible gate`: the maintainer-owned full discovered admissible case set release gate.

Unless a section explicitly says `maintainer formal admissible gate`, treat the step-by-step commands below as public smoke/public validate guidance, not as the release gate on their own.

Package/export boundary validation is a release hygiene layer, not a substitute for UE behavior validation. Source workspace checks may ignore local planning files that are intentionally outside the public artifact, while package/export checks must reject root review reports, `openspec/` including verification evidence, AGENTS/CLAUDE local rules, raw automation trees, cache folders, generated HostProject fallback artifacts, generated fallback host directories, concrete fallback project paths, and maintainer absolute paths.

For a shorter coverage-boundary summary of what contributors can run versus what stays maintainer-only, see [ValidationGovernance/TestCoverageBoundary.md](ValidationGovernance/TestCoverageBoundary.md).

## Core Rule

Do not treat any single signal as sufficient on its own.

A Design2UMG visual fix is only considered verified when all of the following agree:
- automation tests pass
- structural snapshot comparison passes when saved-asset structure parity is in scope
- the target `.uasset` is newly regenerated
- the post-save rendered PNG is newly regenerated
- the editor-side Designer screenshot is newly regenerated when Designer parity is in scope
- the result matches the intended source design in the specific area being fixed

## Automation Font Isolation Rules

Automation font imports are transient per-case evidence, not shared truth.

Maintainer rules:

- Do not persist case-local automation icon font paths into shared settings or any cross-case snapshot. Paths such as `/Game/Design2UMG/Automation/<case>/ImportedFonts/...` belong to one formal case only.
- During automation, the active case must resolve its own imported font object path. If the current case-local font asset is missing, repair or recreate the current case-local asset first.
- Do not silently let a case-local run reuse `/Game/Design2UMG/Automation/ImportedFonts/...` as if it were the current case's font asset. That path can carry stale typography state from an older run and create fake regressions.
- If a case-local font import cannot be downloaded again, prefer rebuilding the current case-local asset from an already loadable font asset over borrowing the old global automation object path directly.
- When diagnosing a font regression, inspect both the generation-stage and compiled-instance font object paths in the Unreal log. If the family resolves to another case's `ImportedFonts` path or to the global automation path during a case-local run, treat that as contamination first and only then judge the score delta.

These rules were reconfirmed during phase 21 batch 2: the real regression was caused by cross-case automation font reuse, not by the converter boundary cleanup itself.

## Required Validation Order

### 1. Build first

```text
<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe FPSRoguelikeEditor Win64 Development "-Project=<MaintainerRuntimeProject.uproject>" -WaitMutex -FromMsBuild
```

### 2. Run focused automation tests

Use the smallest relevant test set first, then broaden if needed.

Example:

```text
<UE5_Root>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <MaintainerRuntimeProject.uproject> -unattended -nop4 -nosplash -NullRHI "-ExecCmds=Automation RunTests Design2UMG.WidgetTree.BorderSpacerStrokePadding+Design2UMG.Rasterizer.PersistentPathTexture+Design2UMG.WidgetTree.DesignSizeExpansion; Quit" "-TestExit=Automation Test Queue Empty" -log
```

### 3. Regenerate the real asset

For visual verification, import the target frame into the final output path.

Example:

```text
<UE5_Root>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <MaintainerRuntimeProject.uproject> -unattended -nop4 -nosplash "-ExecCmds=Design2UMG.Import <MaintainerDesignRoot>\pencil-welcome.pen --node=frame-1761929672442 --out=/Game/Design2UMG/PenConvert_Output --no-preview; Quit" -log
```

### 4. Confirm the asset and PNG were both regenerated

Both timestamps must be newer than the current validation start time:
- `<MaintainerRuntimeProjectRoot>\Content\Design2UMG\PenConvert_Output\WBP_lunaris__design_system_components.uasset`
- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.png`

If the asset updated but the PNG did not, that validation run is incomplete.

### 5. Check the log for the correct render path

The log must contain both:
- `Saved Widget Blueprint at ...`
- `Rendered PNG saved: ...`

If the log contains:
- `Widget PNG render failed`

then do not use that run for visual conclusions.

### 6. Run the Designer screenshot validation when Designer parity matters

Use the real editor process, not `UnrealEditor-Cmd.exe`, and do not use `-NullRHI`.
The current wrapper goes through `Design2UMG.AutomationRender` in `asset` mode and renders the saved Widget Blueprint with designer flags enabled.

Preferred command:

```text
pwsh -NoProfile -File <PluginRoot>\Tools\Run-DesignerSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

The wrapper now kills any existing `UnrealEditor.exe` / `UnrealEditor-Cmd.exe` process by default before launching the validation editor.
Use `-KeepExistingEditor` only when you are intentionally debugging against an already-open editor session.

Example:

```text
<UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe <MaintainerRuntimeProject.uproject> -NoSplash -NoSound -nop4 "-ExecCmds=Design2UMG.AutomationRender <designer-spec.json>; Quit" -log
```

Expected output file:

- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.designer.png`

This file is editor evidence, not asset-render evidence.

Important:
- do not treat the PowerShell command returning as proof that validation finished
- `UnrealEditor.exe` can detach and keep running after the shell returns
- wait for the actual `UnrealEditor.exe` process to exit, or use the wrapper script above
- reject any run where `designer.png` is older than the validation start time

### 7. Run the structure snapshot validation when saved asset parity matters

Use this to compare:
- the source design conversion tree for the target frame
- the generated saved `UWidgetBlueprint` widget tree from the same automation run

Example:

```text
<UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe <MaintainerRuntimeProject.uproject> -NoSplash -NoSound -nop4 "-ExecCmds=Design2UMG.AutomationRender <structure-spec.json>; Quit" -log
```

Preferred wrapper:

```text
pwsh -NoProfile -File <PluginRoot>\Tools\Run-LunarisStructureSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -SourcePath <MaintainerDesignRoot>\pencil-lunaris.pen
```

Expected output files:

- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.source-tree.json`
- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.saved-tree.json`

The wrapper also writes a companion result json and reports the generated asset path because import semantics may suffix the asset name when the target already exists.

These files are structure evidence, not visual evidence.

The snapshot test intentionally compares stable semantics rather than exact full-tree equality:
- source and saved-asset structure must both be present for non-asset conversion runs
- visible text multiset and text node count must match
- total, leaf, container, and visual node counts must not collapse below the guarded floor
- generated widget-tree structure must still match the saved/reloaded widget-tree structure
- image-only render cheats are rejected when the source has real text/visual structure

Do not tighten this into exact full-tree equality unless the generator guarantees zero wrapper normalization drift after save.

### 8. Run determinism smoke before the final formal gate when automation-render parity changed

Use determinism smoke after touching any of these surfaces:

- automation render orchestration
- package reload preparation
- transient reset
- automation font refresh
- bootstrap render preparation

This is a focused maintainer pre-gate, not release closeout.

The canonical sentinel owner is `Tools/design2umg_determinism_smoke.py:CANONICAL_SENTINEL_CASES`.
Do not maintain a second handwritten sentinel list in wrapper logic or selftests. Inspect the current pair with:

```text
python Tools/design2umg_determinism_smoke.py print-sentinels
```

Focused smoke command:

```text
pwsh -NoProfile -File <PluginRoot>\Tools\Run-Design2UMGDeterminismSmoke.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -ReferenceRoot <TrustedReferenceRoot> -OutputRoot <MaintainerRuntimeProjectRoot>\Saved\Design2UMG\Automation\_determinism_smoke_<stamp>
```

Smoke rules:

- `-ProjectPath`, `-EditorPath`, `-ReferenceRoot`, and `-OutputRoot` must be explicit.
- The smoke helper must fail closed if the run root, required sentinel case, compare report, `generation_vs_asset`, or `generation_vs_asset_structure` evidence is missing, stale, unreadable, or mismatched.
- A passing smoke must prove `generation_vs_asset.changed_ratio = 0.0` and `generation_vs_asset.stable_changed_ratio = 0.0` for every sentinel.
- A passing smoke must also prove `generation_vs_asset_structure.available = true` plus structure/text parity for every sentinel.
- Passing this smoke only means the bootstrap-vs-asset parity path still looks trustworthy enough to continue to the formal gate.

Sentinel maintenance rule:

- If an isolated formal replay proves a new bootstrap-vs-asset drift case that is not covered by the current sentinel pair, the next determinism-focused batch must explicitly revisit the sentinel set instead of silently growing a second list elsewhere.

## Current Formal Gate

The current formal admissible gate is the full discovered admissible case set.

That means:
- do not run the formal full gate with an exclude pattern
- formal batch reports do not use raw visual-threshold `passed/failed` totals as a score
- formal historical-best parity should be checked with:
  `python Tools/design2umg_validate_run.py --output-root <run_root> --admissible-best-json <PluginRoot>\Saved\Design2UMG\Automation\_admissible_best_latest.json`
- the Python helper contracts are checked separately with these UE-free selftests:
  - `python Tools/design2umg_validation_helpers_selftest.py`
  - `python Tools/design2umg_compatibility_selftest.py`
  - `python Tools/design2umg_diagnostics_selftest.py`
  - `python Tools/design2umg_run_history_selftest.py`
  - `python Tools/design2umg_batch_report_contract_selftest.py`
  - `python Tools/design2umg_determinism_smoke_selftest.py`
- together they cover discovery filtering, default Figma export root resolution, run-scope writing, shard sizing, completeness aggregation, compatibility-rule routing, diagnostic stderr shape, historical-best helper ownership, batch summary shape, and batch timing-history persistence
- import diagnostic report JSON is a separate contract from those helper stderr lines; see `Docs/ImportDiagnosticContract.md`

If the active render-size or pipeline-determinism rules changed, rebuild the admissible best manifest first:
`python Tools/design2umg_rebuild_admissible_best.py`

`Tools/design2umg_pinned_baseline.json` is an old pinned baseline for the pre-Phase-10 evidence rule. It must not be used as the Phase 10 release gate. If the current validator reports `historical_best_render_size_rule_incompatible` against that file, treat it as incompatible evidence, not as proof that the new code regressed.

The older `_admissible_best_20260407_47.json` example is obsolete and must not be used for current formal gate validation.

Reference sourcing for the formal gate:
- `pen_*` cases may use Pencil MCP / export-nodes capture as the authoritative source reference when no checked-in external reference root is provided
- `figma_*` cases use the top-level export PNG found under each local Figma export root as the authoritative source reference
- if you package a checked-in external reference root for a formal full run, every formal case directory must contain both `reference.png` and `compare_report.json`
- `Tools/design2umg_auto_verify_batch.py` now auto-discovers the default local Figma export roots from the `design-root` workspace ancestry when `--figma-export-root` is omitted, so the maintainer formal gate does not silently drop the Figma members of the full discovered case set

The batch verifier requires an explicit runtime project through `--project` or `DESIGN2UMG_RUNTIME_PROJECT`. Missing project input is a hard error with no design-root inference and no HostProject creation.

Batch-level timing drift is recorded at `Saved/Design2UMG/Automation/batch_timing_history.jsonl`. This history is for contributor inspection only; it does not replace fidelity or no-regression as the release gate.

Default user flow is fail-closed. If live Pencil `export_nodes` is unavailable, the verifier may still keep a `get_screenshot` witness PNG so the run can preserve diagnostic and comparison artifacts, but that witness fallback does not prove live Pencil export health and it does not relax formal or historical-best gates.

Maintainers may also run a local regression-only comparison against a curated external `--reference-root`, but that is a manual local workflow. It must not be treated as proof that the live Pencil export path is healthy, and it must not be confused with the default fail-closed user flow above.

If `reference.png` exists but the baseline `compare_report.json` is missing, the run can still render and compare, but the run root's own `summary.no_regression` becomes unavailable. That is not acceptable for the formal archived gate.

Operational reminder from phase 21 Batch 6:

- If the maintainer goal is the archived `summary.no_regression` proof, do not forget `--reference-root`.
- Omitting `--reference-root` can still produce a technically useful live Pencil run, but many Pencil cases may then downgrade to `unverified_reference`, `comparison_mode_mismatch`, or degraded reference authority, which blocks the archived no-regression gate even when the scripts themselves are behaving correctly.
- So treat these as two different questions:
  - "Is live Pencil export healthy right now?"
  - "Did this formal tooling change preserve the archived no-regression evidence chain?"
- The second question requires the curated external reference root.

Formal gate reading order:

- `summary.json` answers whether the current run regressed against the chosen baseline for this run (`summary.no_regression`).
- `validation_against_historical_best.json` answers whether any admissible case is still worse than the traceable historical best.
- Do not collapse these into one statement. A run may pass `no_regression` while still failing historical-best parity, and it may also fail for pipeline-determinism reasons without being a historical-best regression.

Formal gate execution discipline:

- Run the full admissible case set formal gate alone. Do not run focused automation, other editor validations, or another formal batch in parallel against the same runtime project or automation asset roots.
- If a new pipeline-determinism incompatibility appears right after a concurrent validation run, rerun the formal gate in isolation before changing code. Parallel validation can manufacture false determinism noise.

## Pencil MCP App Alias Compatibility

The old Pencil `visual_studio_code` / `vscode` app alias compatibility has been removed. If Pencil cannot connect because the editor app id is not registered as expected, record the real blocker in the validation evidence and stop; do not reintroduce alias-file repair, HostProject fallback, or any other compatibility patch to keep the run going.

## Regression-Sensitive Areas

These areas have already produced real score regressions and should be treated as high-risk:

- Text heuristics:
  do not assume design `fontSize` should be copied directly into UMG runtime font size.
  Pixel fidelity is judged against rendered output, not source JSON numbers.

- Fixed single-line text:
  do not add a blanket heuristic that preserves the declared source font size for narrow single-line text boxes.
  This regressed `pen_pencil-welcome_fyi` / `pen_pencil-welcome_fyi-2`.

- Dynamic measured text fallback scale:
  do not enable runtime-measured fallback scaling by default unless a full formal run proves zero regression.
  This changed compiled text sizes across multiple fonts and regressed several previously matched cases.

- Figma text bounds:
  do not blindly force `TEXT` / `TEXT_PATH` nodes to prefer layout bounds over render bounds.
  That changes the text box fed into downstream fitting and regressed Figma export cases.

## PNG vs UMG Designer Parity

## Short answer

No. A PNG is not inherently guaranteed to be strictly identical to what the UMG Designer shows.

## What the PNG does represent

When generated by `DesignImportUtils::GenerateBlueprintAndPng`, the PNG is rendered from the saved, compiled widget asset, not from the pre-save import tree.

That makes it a high-confidence signal for:
- widget hierarchy after save
- post-compile brush/path persistence
- layout after widget tree reflow
- design-time size expansion

## What can still differ from the Designer

The UMG Designer is an editor preview surface, not the same thing as a flat screenshot. Differences can still come from:
- stale open asset tabs in the editor
- selection outlines, focus highlights, and designer-only chrome
- preview zoom / viewport framing
- editor font availability and fallback state
- RHI differences between headless and rendered runs
- DPI and preview environment state
- editor-only widgets or preview-time transforms

## Therefore

PNG is a strong but not absolute proxy.

Treat it as:
- sufficient for regression detection
- insufficient for final parity claims if the Designer still shows a mismatch

## Designer Screenshot Policy

The Designer screenshot is the direct validation path for what the UMG Designer is actually showing.

Before capture, the automation step must clear editor-only overlays that are not part of the design content:
- disable dashed outlines
- clear hover state
- clear widget selection

Use it when:
- the user reports a mismatch inside the UMG Designer
- the saved-asset PNG and the Designer appear to disagree
- the fix changes editor-preview-only behavior, tab framing, or Designer layout state

Treat it as:
- the highest-priority evidence for Designer parity
- separate from the saved-asset PNG, not a replacement for it

## What to do when PNG and Designer disagree

Use this exact escalation order:

1. Reject any run where PNG generation failed.
2. Confirm the `.uasset` and `.png` timestamps are both fresh.
3. Close and reopen the target widget in the UMG Designer.
4. Re-run the import without `-NullRHI`.
5. Run the Designer screenshot automation step and regenerate the Designer PNG.
6. Compare the saved asset path, not preview-only output.
7. If disagreement remains, trust the Designer screenshot plus reopened asset over an older saved-asset PNG.
8. Add a focused regression test or log probe for the disputed node type.

## Rules for Headless Validation

`-NullRHI` is allowed for:
- parser tests
- layout math tests
- widget tree regression tests
- non-visual automation

`-NullRHI` must not be used as the final source of truth for visual parity.

Reason:
- the asset may save successfully
- the PNG may fail to render
- a stale PNG can then be mistaken for the latest result

## Required Evidence in Fix Reports

Any Design2UMG visual fix report must include:
- build command
- automation test command
- structure snapshot test command when saved-asset structure parity was checked
- import command
- asset path
- PNG path
- Designer PNG path when Designer parity was checked
- structure JSON paths when structure parity was checked
- log lines proving the PNG was freshly rendered
- a note stating whether the result was validated against saved asset render, Designer view, saved-asset structure, or a combination of those

## Required Failure Language

When validation is incomplete, the report must say so explicitly.

Valid examples:
- `Asset regenerated, but PNG render failed; visual verification is incomplete.`
- `PNG is fresh, but Designer parity was not rechecked yet.`
- `This result is based on saved asset render, not Designer inspection.`

Invalid examples:
- `Fixed.` when only the asset saved
- `Verified.` when the PNG is stale
- `Matches Designer.` without checking the Designer

## Current Policy

For Design2UMG visual bugs, the minimum acceptable bar is:
- passing focused automation tests
- fresh saved asset
- fresh post-save PNG rendered without `Widget PNG render failed`

If the user reports a remaining mismatch in the UMG Designer, that report overrides any earlier PNG-only conclusion until the discrepancy is reconciled.
