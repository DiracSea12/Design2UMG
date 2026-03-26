# Known Limitations

This document tracks current limitations that are known and accepted for now.

## Path Rasterization

- Path texture cache uses hashed keys, so hash collision is theoretically possible (very low probability).
- Some non-uniform path scaling scenarios may still show visual discrepancy versus design tools.

## Box Shadow

- Shadows can be clipped in strict `ClipToBounds` parent containers.
- Multi-layer shadow quality is acceptable for editor previews, but not yet physically accurate.

## Effect Fidelity

- Foreground blur now uses `RetainerBox + EffectMaterial` for runtime approximation, but it is not a full semantic match for every design-tool blur mode.
- Inner shadow now uses the same typed `RetainerBox + EffectMaterial` contract, but it is still a single-pass approximation rather than a full inset-shadow semantic match.
- Radial gradients now preserve multi-stop ramps plus `center` / `size` / `rotation`; `mesh_gradient` remains unsupported.

## Ellipse / Vector Fidelity

- Complex ellipse and vector edge cases can still fall back to approximation paths.

## Figma-Specific Fallbacks

- `TEXT_PATH` currently degrades to a plain `TextBlock`; text-on-path geometry is not reproduced.
- `SLICE` and FigJam-only node types (`TABLE`, `TABLE_CELL`, `STICKY`, `SHAPE_WITH_TEXT`, `CONNECTOR`, `WASHI_TAPE`) are intentionally skipped from the runtime widget tree.

## Font Assets

- Design metadata does not contain font binaries.
- Plugin can map/import known fonts, but cannot guarantee full automatic font reconstruction for all projects.

## Interaction and Animation

- Top-level Figma prototype page navigation currently covers `ON_CLICK + NAVIGATE/SWAP` into a generated `WidgetSwitcher`; overlay, back-stack, hover, drag, keyboard, and conditional flows are not generated.
- Prototype transitions currently map to runtime fade / slide approximations on the generated switcher; they do not generate full `UWidgetAnimation` assets or full Smart Animate layer matching.
