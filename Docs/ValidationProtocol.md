# Design2UMG Validation Protocol

Last verified: 2026-04-19

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
- `self-hosted build`: the manual BuildPlugin packaging workflow that runs on a maintainer-provided self-hosted Windows UE runner.
- `public smoke/public validate`: contributor-reproducible local build plus focused validation. This is the public validation boundary.
- `maintainer formal admissible gate`: the maintainer-owned full discovered 47-case release gate.

Unless a section explicitly says `maintainer formal admissible gate`, treat the step-by-step commands below as public smoke/public validate guidance, not as the release gate on their own.

## Core Rule

Do not treat any single signal as sufficient on its own.

A Design2UMG visual fix is only considered verified when all of the following agree:
- automation tests pass
- structural snapshot comparison passes when saved-asset structure parity is in scope
- the target `.uasset` is newly regenerated
- the post-save rendered PNG is newly regenerated
- the editor-side Designer screenshot is newly regenerated when Designer parity is in scope
- the result matches the intended source design in the specific area being fixed

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
pwsh -NoProfile -File <MaintainerPluginRoot>\Tools\Run-DesignerSnapshotValidation.ps1
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
pwsh -NoProfile -File <MaintainerPluginRoot>\Tools\Run-LunarisStructureSnapshotValidation.ps1
```

Expected output files:

- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.source-tree.json`
- `<MaintainerRuntimeProjectRoot>\Saved\Design2UMG\WBP_lunaris__design_system_components.saved-tree.json`

The wrapper also writes a companion result json and reports the generated asset path because import semantics may suffix the asset name when the target already exists.

These files are structure evidence, not visual evidence.

The current snapshot test intentionally compares only stable semantics:
- root widget type
- root child count
- visible text multiset
- `TextBlock` count
- total node count staying within a bounded drift window

Do not tighten this into exact full-tree equality unless the generator guarantees zero wrapper normalization drift after save.

## Current Formal Gate

The current formal admissible gate is the full discovered 47-case set.

That means:
- do not run the formal full gate with an exclude pattern
- `figma_figma-export_source` and `pen_pencil-welcome_step-2-frame` are part of the release gate again
- formal historical-best parity should be checked with:
  `python Tools/design2umg_validate_run.py --output-root <run_root> --admissible-best-json Tools/design2umg_pinned_baseline.json`

The older `_admissible_best_20260407_47.json` example is obsolete and must not be used for current formal gate validation.

Reference sourcing for the formal gate:
- `pen_*` cases may use Pencil MCP / export-nodes capture as the authoritative source reference when no checked-in external reference root is provided
- `figma_*` cases use the top-level export PNG found under each local Figma export root as the authoritative source reference
- if you package a checked-in external reference root for a formal full run, every formal case directory must contain both `reference.png` and `compare_report.json`

If `reference.png` exists but the baseline `compare_report.json` is missing, the run can still render and compare, but the run root's own `summary.no_regression` becomes unavailable. That is not acceptable for the formal archived gate.

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
