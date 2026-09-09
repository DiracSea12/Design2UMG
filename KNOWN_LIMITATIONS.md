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

- `EffectiveDesignSize` is snapshotted once per recursive widget build so one build pass no longer observes mid-recursion drift from the process-global value.
- The top-level build entry still receives design size through the legacy global handoff, so cross-run callers must continue to set it correctly before conversion.
- Font resolution and text measurement caches are now synchronized, but text sizing is still heuristic because UE font metrics and design-tool/web font metrics are not identical.

## Ellipse / Vector Fidelity

- Complex ellipse and vector edge cases can still fall back to approximation paths.

## Figma-Specific Fallbacks

- `TEXT_PATH` currently degrades to a plain `TextBlock`; text-on-path geometry is not reproduced.
- `SLICE` and FigJam-only node types (`TABLE`, `TABLE_CELL`, `STICKY`, `SHAPE_WITH_TEXT`, `CONNECTOR`, `WASHI_TAPE`) are intentionally skipped from the runtime widget tree.

## Font Assets

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

- Prototype export still produces typed navigation IR: synthesized switcher roots for multi-page flows, button-wrapped trigger nodes, typed trigger/action bindings, and conditional step programs (`ON_CLICK/ON_HOVER/ON_PRESS/ON_DRAG/ON_KEY_DOWN/ON_KEY_UP + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK`).
- Generated widget blueprints bake no runtime navigation: the former opt-in `RuntimeExtension` / `TagHelper` generation modes are gone, generated assets structurally contain only standard UMG classes, and no transition `UWidgetAnimation` assets are generated.
- Wiring click navigation, overlay show/hide, history/back, open-link, and transition playback in an actual game world is intentionally left to the project side.

## Explicit Non-Goals For This Round

- Full `TEXT_PATH` reproduction is still out of scope because the current runtime tree has no text-on-path shaping / geometry rendering path.
- Full semantic blur / inner-shadow parity is still out of scope because the current effect pipeline is built on `RetainerBox + EffectMaterial` / `UBackgroundBlur`, not custom multi-pass Slate rendering.
- Full prototype overlay layer semantics / generalized interaction graph generation is still out of scope because the current export stops at typed navigation IR over `WidgetSwitcher` page structure plus a lightweight overlay host, not a generic state machine / event graph compiler.

## Verification Boundary

- Claims like "historical best visual fidelity" are not treated as static facts in this document.
- Final parity conclusions must come from the current full-batch baseline protocol and real-project reruns, not from code inspection alone.
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
