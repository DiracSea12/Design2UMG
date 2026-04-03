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

- Common `WidgetStyle / TextData / Brush / Effect / PathData / Render / WidgetState` fields now run typed-first and shadow same-key legacy `Properties`.
- Legacy `Properties` still exists only for widget-specific runtime parameters that are still consumed by class-specific `FUMGPropertyBag` appliers.
- Old UMG JSON compatibility is no longer a product goal; parser/runtime cleanup should continue to shrink legacy-only paths when it does not affect active baselines.
- This round does not delete the legacy bag entirely because doing so would couple a typed migration with wider widget-specific schema work and old JSON breakage.

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

- Re-import does not currently promise patch/update of an existing Widget Blueprint.
- Name collisions currently resolve to creating a new generated asset rather than mutating the old one in place.

## Interaction and Animation

- `Prototype Basic` is a shipped supported capability: page-switching prototype flows, typed trigger/action bindings, lightweight `OVERLAY/BACK/OPEN_LINK`, and basic fade / slide transition approximation are in scope.
- The limitations below describe the intentionally unsupported or approximate parts of prototype export; they do not mean prototype support is purely experimental.
- Top-level Figma prototype page navigation now covers `ON_CLICK/ON_HOVER/ON_PRESS/ON_DRAG/ON_KEY_DOWN/ON_KEY_UP + NAVIGATE/SWAP/OVERLAY/BACK/OPEN_LINK`, and conditional flows run through a typed step program with session-local variable storage.
- `OVERLAY` now uses a dedicated overlay host layered over the base switcher, but it still does not provide a full floating-layer system with anchor, outside-click dismiss, or generalized overlay layout semantics.
- `BACK` currently replays the generated runtime history stack plus overlay stack, not a generalized graph state machine.
- `OPEN_LINK` currently launches the external URL at runtime and does not participate in page switching.
- Prototype variables currently exist only inside the generated runtime contract for trigger/conditional execution; they are not yet a broader cross-format variable/state system.
- Generated prototype transition animations currently fall back to the core-ticker transition path outside real game worlds (for example editor-only preview/automation contexts).
- Prototype transitions now generate minimal two-stage `UWidgetAnimation` assets for fade / slide switcher transitions, but they still do not provide full Smart Animate layer matching or generalized animation authoring.

## Explicit Non-Goals For This Round

- Full `TEXT_PATH` reproduction is still out of scope because the current runtime tree has no text-on-path shaping / geometry rendering path.
- Full semantic blur / inner-shadow parity is still out of scope because the current effect pipeline is built on `RetainerBox + EffectMaterial` / `UBackgroundBlur`, not custom multi-pass Slate rendering.
- Full prototype overlay layer semantics / generalized interaction graph generation is still out of scope because the current runtime contract is centered on `WidgetSwitcher` page transitions plus a lightweight overlay host, not a generic state machine / event graph compiler.

## Verification Boundary

- Claims like "historical best visual fidelity" are not treated as static facts in this document.
- Final parity conclusions must come from the current full-batch baseline protocol and real-project reruns, not from code inspection alone.
- Release validation in this repository uses a maintainer-owned canonical runtime boundary; HostProject fallback is not part of the supported workflow.
- Public documentation should describe that boundary by role, not by maintainer machine path.

## Widget Class Overrides

- Widget class override is a supported release capability for generation-time class substitution.
- Matching is case-insensitive `Name Contains`, and the first matching rule wins.
- It is intentionally a narrow override mechanism, not a full widget-type rule engine.

## Import Diagnostics

- Import and automation now emit a structured `Saved/Design2UMG/ImportReports/*.import_report.json` artifact.
- The report captures source/output binding, skipped designer nodes, degradation and fallback entries, resource failures, effect wrapper usage, and operator recommendations.
- It is the single authority artifact for import troubleshooting, but not yet a full end-to-end provenance system.
