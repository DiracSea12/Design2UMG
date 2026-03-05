# Known Limitations

This document tracks current limitations that are known and accepted for now.

## Path Rasterization

- Path texture cache uses hashed keys, so hash collision is theoretically possible (very low probability).
- Some non-uniform path scaling scenarios may still show visual discrepancy versus design tools.

## Box Shadow

- Shadows can be clipped in strict `ClipToBounds` parent containers.
- Multi-layer shadow quality is acceptable for editor previews, but not yet physically accurate.

## Ellipse / Vector Fidelity

- Complex ellipse and vector edge cases can still fall back to approximation paths.

## Font Assets

- Design metadata does not contain font binaries.
- Plugin can map/import known fonts, but cannot guarantee full automatic font reconstruction for all projects.

## Interaction and Animation

- Interaction/prototype behavior is not converted to runtime UMG logic.
- Animation timelines are not generated automatically.
