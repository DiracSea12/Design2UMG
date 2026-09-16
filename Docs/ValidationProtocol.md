# Design2UMG Validation Protocol

## 当前输入基线（2026-09-15 用户确认）

当前 81-case 基线为 `G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/review-batch-transaction-full-20260915`。当前输入与参考文件已逐项核验，原始 PNG、评分公式、阈值、成绩文件均不改写。后续完整回归显式使用该目录作为 `--reference-root`。

`Automation/_baseline.json` 是小型一次性登记指针，绑定该 run 的 summary/run_scope SHA256 与登记时间；`ReferenceCache/CURRENT.json` 同步指向它。历史重建只纳入该登记基线和登记之后的有效运行。不同源/有效参考身份不强行比较；同身份的既有历史会保留，A→B→A 不得把回来的 A 当全新基线。候选运行不能自己移动登记起点。缺失输入身份、弱参考来源、缺报告或同输入退步仍阻断。

仓库巨型 `design2umg_pinned_baseline.json` 已删除，消费者不再依赖该文件。下文旧阶段与旧 run 的记录只作历史背景，不覆盖本节已明确的当前基线。

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
- `self-hosted build and automation`: packages the plugin and runs focused generation, Figma parser, and lifecycle automation on a maintainer-provided Windows UE runner for maintainer pushes, same-repository PRs, and manual runs, then uploads the packaged artifact.
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
<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe <RuntimeProjectName>Editor Win64 Development "-Project=<MaintainerRuntimeProject.uproject>" -WaitMutex -FromMsBuild
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
pwsh -NoProfile -File <PluginRoot>\Tools\Run-DesignerSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -AssetObjectPath <WidgetBlueprintObjectPath> -DesignerPngPath <PngPath> -ResultJsonPath <ResultJsonPath>
```

`-AssetObjectPath`, `-DesignerPngPath` and `-ResultJsonPath` are required; the wrapper has no sample-specific defaults.

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
pwsh -NoProfile -File <PluginRoot>\Tools\Run-StructureSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -SourcePath <MaintainerDesignRoot>\pencil-lunaris.pen -TargetNodeId <FrameNodeId> -OutputPath <ImportedAssetPath> -SavedPngPath <PngPath> -SourceJsonPath <SourceTreeJsonPath> -SavedJsonPath <SavedTreeJsonPath> -ResultJsonPath <ResultJsonPath>
```

All snapshot-specific paths (`-TargetNodeId`, `-OutputPath`, `-SavedPngPath`, `-SourceJsonPath`, `-SavedJsonPath`, `-ResultJsonPath`) are required; the wrapper has no sample-specific defaults.

Expected output files (whatever you passed for `-SourceJsonPath` / `-SavedJsonPath`):

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

### 7.1 Require a fresh-process reload after saving

A WidgetTree observed inside the generation editor only proves that the in-memory object tree exists. The saved-asset structure result must follow this sequence:

1. Generate the Widget Blueprint and capture the pre-save structure snapshot.
2. Wait for the actual `UnrealEditor.exe` process to exit.
3. Start a new `UnrealEditor` process and load the same `.uasset`.
4. Capture a second structure snapshot and inspect `LoadErrors`, compiler ensures, and the final PNG.

This repository once recorded an A/B reproduction using the same project and automation entry point.
**The following bullets are a superseded historical failure sample, kept only as the negative fixture
model for the reload gate below — they are not a current production-defect claim:**

- Historical sample: the asset containing `Design2UMGConstrained*` widgets was observed reloading as 10 nodes with 168 `LoadErrors`; the errors were concentrated on those custom widgets' external containers.
- Historical sample: the asset containing only native UMG widgets reloaded as 219 nodes with 0 `LoadErrors`.

**Current conclusion (2026-08-25):** a controlled re-run of the original command line (same project,
same driver entry point, same on-disk asset, brand-new editor process) loaded the fusion asset cleanly:
full node count, zero outer-load failures, zero variable-GUID ensures. The historical failure sample above
was captured during a window when plugin binaries were being rebuilt by parallel work; it is evidence of
that environment, not of an inherent save/reload defect in `LayoutMapper::TryFuseSizeConstraints()`.

<details><summary>Superseded historical failure model (kept verbatim for the reload-gate negative fixtures)</summary>

The claim recorded at the time was that `LayoutMapper::TryFuseSizeConstraints()` fuses `SizeBox + child` into `Design2UMGConstrained*`, that these editor-module widgets work during the in-memory render but fail during fresh-process reload after they are placed in the WBP asset, truncating the WidgetTree, and that the `WidgetBlueprintCompiler.cpp:828` "deleted variable still has a GUID" message is a downstream symptom of that reload failure. This chain is not a current production-defect claim.

</details>

### 7.1.1 Run the fresh-process reload gate

`Tools/design2umg_fresh_process_reload_gate.py` is the executable form of this section. It launches one fresh
editor process, reloads each target asset through `Design2UMG.AutomationRender` with `force_reload_package`,
waits for the real process exit, then fails closed unless every result reports success, every widget tree stays
at or above the guarded node floor, and the fresh log contains none of the two corruption signatures (outer-load
failures and variable-GUID ensures). Its offline selftest replays the historical failure log as negative fixture:

```text
python Tools/design2umg_fresh_process_reload_gate_selftest.py
pwsh -NoProfile -Command "python Tools/design2umg_fresh_process_reload_gate.py --project <MaintainerRuntimeProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe --assets /Game/.../WBP_X.WBP_X[,...] --output-root <RuntimeProjectRoot>\Saved\Design2UMG\FreshReloadGate_<stamp> --min-total-nodes <floor>"
```

For formal multi-asset verification, pin each asset's expected node count with per-asset floors so a local
truncation cannot hide behind another asset's larger tree:

```text
--asset-floors "0=136,1=219"
```

Run it after any change to widget classes, the converter output shape, or the save/compile path, before trusting
a structure snapshot from the generating process.

This conclusion is scoped to the current `Design2UMGConstrained*` implementation and its save/reload path. Other custom widgets require their own fresh-process reload evidence. An in-memory snapshot without a fresh-process reload is not final saved-asset parity evidence.

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

The current formal admissible gate is the 81-case discovered admissible set: 39 Pencil cases plus 21 Figma export roots represented by `local` and `source` paths (42 Figma cases). The locked scope is recorded by the latest full-run `run_scope.json`; a run is not a formal full run when it discovers fewer than 81 cases. The sealed baseline for this scope is `d4-seal81-20260906_01`.

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
- `python Tools/design2umg_evidence_epoch_selftest.py`
  - `python Tools/design2umg_batch_report_contract_selftest.py`
  - `python Tools/design2umg_determinism_smoke_selftest.py`
- together they cover discovery filtering, default Figma export root resolution, run-scope writing, shard sizing, completeness aggregation, compatibility-rule routing, diagnostic stderr shape, historical-best helper ownership, batch summary shape, and batch timing-history persistence
- import diagnostic report JSON is a separate contract from those helper stderr lines; see `Docs/ImportDiagnosticContract.md`

If the active render-size or pipeline-determinism rules changed, rebuild the admissible best manifest first:
`python Tools/design2umg_rebuild_admissible_best.py --automation-root <run-history-root> --design-root <design-corpus-root> --figma-export-root <figma-export-corpus>`

Historical coverage uses the discovered case names, not a fixed count. Full batch history checks discover the entire configured corpus even when the current execution is filtered. A single-source invocation checks its requested case. Complete pinned coverage requires the requested names exactly; matching counts alone are insufficient. When the corpus changes, verified pinned entries remain available and missing newly discovered cases are reported explicitly. Historical entries from other scopes remain preserved and are listed separately from missing current cases. Discovery does not manufacture missing scores or waive source-identity checks.

Historical-best evidence is rebuilt from admitted run history. Source artifact and effective-reference hashes define an evidence epoch: an identity change seeds the current result as the new epoch baseline, while later runs with the same identity are compared normally. Results from different identities are retained as diagnostics but are never compared.

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

### Shard-mode score variance

`--editor-isolation shard` is suitable for batch execution and artifact collection, but multiple shard/editor lifecycles can share or reset Slate font caches, transient assets, and render state differently, which can make visual scores fluctuate. Treat a single shard result as screening evidence only; rerun every anomalous case exclusively and serially with `--editor-isolation case` before making a regression judgment. Record both the shard run and case-recheck paths in the report. A one-off shard fluctuation does not itself establish visual regression or improvement.

## Layered Evidence Chain (IR golden, geometry, and pixel layers)

Pixel comparison is one layer, not the whole referee. The pixel score measures similarity against the Figma-rasterized reference PNG, so renderer-specific differences (antialiasing, texture resampling, font rasterization) are always mixed into the number. The layered chain separates what each layer can actually prove:

- **IR golden layer** (`Tools/design2umg_ir_golden.py` + `--ir-golden <golden-root>` on the batch verifier): dumps the converted `FUMGWidgetData` typed tree per case with lossless `%.9g` floats and compares it against a frozen golden, byte-stable and zero-tolerance. It proves "conversion behavior did not change" and attributes any numeric drift directly to a field (`RootWidget...Brush.BrushColor.R: golden=0.108 dump=0.1098`), with no image experiment required. Its first golden (`ir-golden-20260830_01`) is bootstrapped from `ir-golden-bootstrap-full71-20260830_01`; see that directory's `BOOTSTRAP.md` for the code-state and baseline-pointer relationship. Golden is a no-regression oracle, not a correctness oracle. A case with no golden records `unavailable` and does not fail.
- **Pixel layer**: unchanged, end-to-end similarity, managed by tolerance.
- **Geometry layer** (`Tools/design2umg_geometry_compare.py` + `--geometry-golden <golden-root>` on the batch verifier): per case the editor dumps runtime widget geometry (cached geometry after the compiled render) and the tool compares it against a frozen golden geometry dump on the same comparable nodes, paired through the shared tree correspondence (tolerating wrapper-inserted path migration). The case verdict is stability -- frozen golden versus current runtime, 0.75px tolerance, x/y anchored on the nearest paired ancestor, so an inserted wrapper that leaves absolute geometry unchanged is not drift; fidelity (IR-derived expectation versus runtime, measured in the IR parent's paired frame) is reported per node but does not fail the case. A case with no golden records `unavailable` and does not fail.

Scoring for the pixel layer lives in `Tools/design2umg_scoring.py` (single home; the batch verifier only calls it). The no-regression tolerance is now a parameter:

- `--no-regression-epsilon` defaults to `1.25e-4` and leaving it unset reproduces historical behavior exactly.
- For a stricter seal-basis pass, pass `1e-6` explicitly. Two preconditions apply: the Pencil reference cache must be frozen (read-only or sha1-checked, because live re-rendered references drift at the `1e-4` scale and would be misreported as regression), and shard-mode runs must not use the tightened tolerance (shard state noise also lives at `1e-4`; small tolerances are for `--editor-isolation case` formal verdicts only).
- Worked example already in evidence: against the sealed `cmsr-strictfix-full71-20260828_01` baseline, the `stringround-colorlock-full71-20260830_01` run passes 71/71 at the default tolerance, and at `1e-6` exactly six cases flip to FAIL (figma-export2 / bubbo-gameplay / team09-start, `local`+`source` each, deltas 2.3e-6~7.5e-6 — the quantization-removal drift, attributed field-by-field by the IR layer).

IR-golden failures currently report in `summary.ir_golden` and per-case `ir_golden_report.json`; they do not gate the batch exit code yet. When a full run produces zero dumps, `design2umg_ir_golden.py` exits 2 as a configuration error (for example the console variable was not enabled), distinct from per-case `unavailable`.

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
