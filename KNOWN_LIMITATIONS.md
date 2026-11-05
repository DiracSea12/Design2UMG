# Known Limitations

This document tracks current limitations that are known and accepted for now.

## Path Rasterization

- Path texture cache uses hashed keys, so hash collision is theoretically possible (very low probability).
- Some non-uniform path scaling scenarios may still show visual discrepancy versus design tools.

## Import Cache Isolation

- Imported image assets are now isolated by source identity plus content version to prevent cross-source reuse and stale URL-only asset reuse.
- This also means identical binaries from different design sources are intentionally not collapsed into one shared persistent asset.
- Local Figma derived images are now scoped by source document identity and source image identity, so stale derived PNG reuse across sibling exports is avoided.

## Box Shadow

- Shadows can be clipped in strict `ClipToBounds` parent containers.
- Multi-layer shadow quality is acceptable for editor previews, but not yet physically accurate.

## Effect Fidelity

- Foreground blur now uses `RetainerBox + EffectMaterial` for runtime approximation, but it is not a full semantic match for every design-tool blur mode.
- Inner shadow now uses the same typed `RetainerBox + EffectMaterial` contract, but it is still a single-pass approximation rather than a full inset-shadow semantic match.
- Radial gradients now preserve multi-stop ramps plus `center` / `size` / `rotation`; `mesh_gradient` remains unsupported.

## Typed Property Boundary

- Common `WidgetStyle / TextData / Brush / Effect / PathData / Render / WidgetState` fields now run through typed ownership or narrow typed request DTOs.
- The legacy `FUMGPropertyBag` bridge has been removed from current code ownership; new runtime or parser work should not reintroduce a broad key/value property store.
- Old UMG JSON compatibility is no longer a product goal; parser/runtime cleanup should continue to shrink legacy-only paths when it does not affect active baselines.

## Build Context And Measurement

- The root handoff derives design size from source root bounds and passes it explicitly through the recursive widget build. Invalid source dimensions fail generation rather than borrowing a process-global size.
- Font resolution and text measurement caches are now synchronized, but text sizing is still heuristic because UE font metrics and design-tool/web font metrics are not identical.

## Ellipse / Vector Fidelity

- Complex ellipse and vector edge cases can still fall back to approximation paths.

## Figma-Specific Fallbacks

- Web programs in Figma code layers cannot be imported directly as native UMG. You can try **Extract designs** first, then export or import the resulting ordinary design layers; see [Figma's help](https://help.figma.com/hc/en-us/articles/44193259600535-Code-layers-in-Figma-Design).
- `TEXT_PATH` currently degrades to a plain `TextBlock`; text-on-path geometry is not reproduced.
- `SLICE` and FigJam-only node types (`TABLE`, `TABLE_CELL`, `STICKY`, `SHAPE_WITH_TEXT`, `CONNECTOR`, `WASHI_TAPE`) are intentionally skipped from the runtime widget tree.

## Font Assets

- Static TTF/OTF fonts can be previewed and imported directly without the optional Win64 tool. Web-compressed fonts (WOFF2) and variable fonts (one file containing multiple weights) need conversion into font files UE can use, with the design's required weight extracted from a variable font, for example 600.
- The tool is not installed by default and never downloads automatically. Check its option in the preview, then confirm again to download; cancelling does not install it. Without it, static TTF/OTF fonts still work, while fonts that need conversion produce a message that they cannot be converted. Removing the tool keeps font resources already imported into UE.
- The tool only converts existing font files; it does not find or download missing fonts. The plugin package contains its download manifest, not the tool binaries or Python / FontTools / Brotli.
- Enter a storage directory in **安装位置** (Installation Location), or select it with **浏览…** (Browse). UE saves the choice in the current project's user config across panel/editor restarts. The default is `<plugin>/Saved/Tools`; the actual install is `<selected directory>/PencilFontWorker/1.0.0`, shown in the panel and confirmation. Remove the current tool before changing location; installation/conversion blocks changes. No old AppData lookup or migration occurs.
- Removal validates the installation record and package file list, deletes only owned files and empty subdirectories, and preserves extra user files. Existing foreign installation directories are never overwritten. Missing/corrupt records, modified files, and reparse nodes inside the managed subtree prevent removal; approved junctions above the storage directory remain supported.
- Edit font-tool UI, management errors and remedy-dialog text in `Resources/FontToolText.json` (flat string values). Reopen the panel or make the next request to reload it. Keep keys and named placeholders: `{Version}`, `{DownloadUrl}`, `{DownloadMiB}`, `{InstalledMiB}`, `{InstallDirectory}`, `{File}`. Nested values and literal-brace escaping are unsupported. Invalid resources/keys display their path/key, with no compiled Chinese fallback.
- Design metadata does not contain font binaries.
- Plugin can map/import known fonts, but cannot guarantee full automatic font reconstruction for all projects.
- Automatic known-web-font download is best treated as an experimental convenience path, not a guaranteed offline-safe production workflow.
- The formal supported path remains local font mapping plus explicit missing-font diagnostics.

## Reimport Semantics

- Normal import (same target name) still creates a new auto-renamed asset; it never mutates the old one in place.
- Safe Reimport is a shipped capability (attribute-level in-place update with conservative conflict semantics); the known limitations below are the intentional boundaries of that capability:
  - Conservative refusal, not data repair: whenever ownership or identity is unclear (scope mismatch, recorded instances no longer locatable, mapped class switched, subtree containing project content marked for removal), reimport keeps the asset as-is and reports a structured diagnostic instead of guessing.
  - Multi-page prototype hosts with an overlay wrapper root (`<switcher id>_root`) keep the conservative full-conflict behavior across page-set changes (pages added/removed). Host continuation is currently implemented for the plain switcher root form only; the overlay wrapper's synthetic children would attach at wrong levels if diffed naively.
  - Deleting a design node also removes the widget's blueprint variable nodes when nothing project-side references them (bindings, blueprint graph variable references, non-D2U children). Nodes with any project reference are preserved and reported as remove conflicts.
  - The v1 delete scope is the recorded-instance level: nested widgets below surviving instances are never auto-deleted when their design nodes disappear (nested subtrees with project-side content get a conflict diagnostic; purely D2U nested subtrees are kept as-is).
  - A mapped instance's own visual attributes (visibility, render opacity) are written at import but never refreshed by reimport; divergence from the design-declared values is reported as a diagnostic instead of being applied.
- Name collisions during normal import still resolve to creating a new generated asset rather than mutating the old one in place.

## Interaction and Animation

- Existing generation wires supported single-step click, hover and press actions (navigate, open link, overlay and back) and keyboard actions (navigate and open link) through native Blueprint graphs. Keyboard input still needs widget focus. Supported page transitions generate `UWidgetAnimation` subobjects within the Widget Blueprint, not a plugin runtime class extension.
- Exported intermediate data can preserve more actions than the generator can execute. Variable writes, conditions, multi-step programs, drag and unresolved targets remain unwired with diagnostics, rather than becoming working Blueprint logic merely because they appear in the data. Extra pages included in a source package are not proof that navigation or animation to them is supported. Rejected animations do not create execution buttons or add their target pages to the generated navigation flow; other supported actions in the design remain eligible.
- The current implementation generates editable engine-native `UWidgetAnimation` tracks for position, rotation, opacity and a single solid fill color. Size changes use native render scale only where it is equivalent: a solid rectangle with no children, rounded corners, strokes or effects. This does not animate layout reflow or arbitrary widget resizing.
- Property animation requires real start/end trees with matching child names, hierarchy and paint order, explicit geometry, and the current action's own positive duration. It supports `LINEAR`, `EASE_IN`, `EASE_OUT`, `EASE_IN_AND_OUT` and precisely representable custom cubic Bézier curves. Unsupported curves, missing targets/values, ambiguous pairing and unsupported property changes produce diagnostics and refuse the action; they do not become instant switches or sampled approximations.
- `ON_CLICK` and `MOUSE_ENTER` play forward without automatic restoration; `MOUSE_ENTER` requires explicit zero delay and no deprecated behavior. `ON_HOVER` reverses on mouse exit for supported `CHANGE_TO` property animation; hover page navigation is refused because it cannot preserve that reversal. Page-entry `AFTER_TIMEOUT` uses its explicit nonnegative delay. These are native Blueprint playback bindings, not a general state machine.
- New Plugin exports normalize reaction `duration` and `timeout` from seconds to milliseconds and carry `design2umgPrototypeDurationUnit=MILLISECONDS`. HTTP/REST animation timing has not been verified for this new property-animation path, so it is refused without verified producer units. Existing static HTTP/REST import remains available.
- `SMART_ANIMATE` overlay actions are unsupported and must be diagnosed and refused. Ordinary supported overlay actions without animation remain available; full overlay placement/dismissal semantics are not implemented.
- **Focused functional validation passed on UE 5.8.3 with D3D12/SM6.** Constructed cases cover native click, mouse entry, hover reversal, automatic playback, basic property evaluation, lifecycle cleanup and user-content conflict preservation; saved assets also passed playback after reopening in a fresh process.
- One genuine Toggle recovery package passed fresh-process playback of a saved native animation: one click, approximately 0.3 seconds, two targets, and intermediate/final movement and solid-color values. Its geometry comes from an existing HTTP cache, with reaction-time witnesses from the same file and nodes through the live Plugin API. This is neither a fresh complete Plugin export nor proof of all HTTP animation units or a full Toggle state machine. Four animation designs now have fresh complete Plugin export packages and have passed real static generation, saved-asset reload and rendering. Loading additionally passed native `AFTER_TIMEOUT` playback. Credit `CUSTOM_SPRING`, SmartHome layer reordering, and animation of Bloodborne’s newly added focus layer and cross-state instance text overrides remain unimplemented; the four static checks are not four animation passes.
- The earlier build/lifecycle tests retained five editor asset-cleanup warnings; fresh-process Toggle reload and playback completed without warnings. Full visual and `no_regression` conclusions require per-case regression evidence against the same inputs and authoritative references. Focused functional validation does not establish full acceptance, CI or large-scale coverage.
- Pencil's existing QuickJS script worker expands supported script-generated design nodes during import on Windows. It is separate from runtime prototype logic and is not removed or declared unsupported by this animation scope.

## Explicit Non-Goals

- Full `TEXT_PATH` reproduction is still out of scope because the current runtime tree has no text-on-path shaping / geometry rendering path.
- Full semantic blur / inner-shadow parity is still out of scope because the current effect pipeline is built on `RetainerBox + EffectMaterial` / `UBackgroundBlur`, not custom multi-pass Slate rendering.
- Full prototype overlay-layer parity and generalized interaction graphs remain out of scope. The generated `WidgetSwitcher` / overlay host and supported native graph bindings do not implement every exported interaction program.
- Previously abandoned directions are **not shipped capabilities and are not being restored**: general prototype-logic compilation (variable calculations, conditions and complex multi-step state), a complete Minesweeper game, and a standalone automated interaction score. Parsing design variables/themes and expanding Pencil script nodes are separate existing capabilities, not a promise to execute prototype business logic.
- Direct conversion of arbitrary Figma or Pencil code-layer programs into UMG remains out of scope; importing ordinary design layers and Pencil's supported QuickJS node expansion are separate paths.
- The basic animation work does not promise arbitrary Smart Animate layer matching, layout reflow animation, image or gradient animation, or complete Figma prototype reproduction. Existing static image/gradient import and supported navigation/transition behavior are not withdrawn.

## Verification Boundary

- Claims like "historical best visual fidelity" are not treated as static facts in this document.
- Final parity conclusions must come from the current full-suite baseline protocol and real-project reruns, not from code inspection alone.
- Release validation in this repository uses a maintainer-owned canonical runtime boundary; HostProject fallback is not part of the supported workflow.
- Public documentation should describe that boundary by role, not by maintainer machine path.

## Repository Workflow Boundary

- Maintainer-local helper files such as `AGENTS.md`, `CLAUDE.md`, `.claude/`, `.codex/`, `.cowork-temp/`, and `openspec/` remain workspace-local aids rather than public package promises or release artifacts.
- Repo-safe collaboration rules that future contributors still need must be duplicated into tracked documents like this file or `CONTRIBUTING.md`; hygiene passes must not rely on deleting tracked guidance without moving the durable parts first.
- Historical planning and change-history working sets may stay in the maintainer source workspace, but BuildPlugin packages and public repo exports must exclude them.

## Public Secret Handling Boundary

- Figma token examples in public docs and release notes should use placeholders such as `<FigmaAccessToken>`.
- Real tokens should come from maintainer-local env vars or secure input, not from committed scripts or checked-in command snippets.
- Real tokens must not appear in public logs, command transcripts, screenshots, or release artifacts.

## Widget Class Overrides

- Widget class override is a supported release capability for generation-time class substitution.
- Matching is case-insensitive `Name Contains`, and the first matching rule wins.
- It is intentionally a narrow override mechanism, not a full widget-type rule engine.

## Import Diagnostics

- Import and automation now emit a structured `Saved/Design2UMG/ImportReports/*.import_report.json` artifact.
- The report captures source/output binding, skipped designer nodes, degradation and fallback entries, resource failures, effect wrapper usage, and operator recommendations.
- It is the single authority artifact for import troubleshooting, but not yet a full end-to-end provenance system.
