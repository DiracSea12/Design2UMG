# Design2UMG Typed Property Contract

This document records the current typed-property contract in the codebase. It is not a migration proposal.

## Current Main Path

Design2UMG now keeps import and generation responsibilities split as follows:

```text
.pen / Figma export
  -> parser / mapper
  -> FUMGWidgetNode typed payloads
  -> direct typed consumers or narrow typed request DTOs
  -> UMG property appliers / resource binders
```

`FUMGWidgetNode` is not a generic string map. Its main properties live in domain payloads:

| Domain | Current payload |
|--------|-----------------|
| Text style | `FUMGTextStyleData` |
| Widget style | `FUMGWidgetStyleData` |
| Text content and behavior | `FUMGTextData` |
| Brush, image, and gradient data | `FUMGVisualBrushData` |
| Effects | `FUMGEffectData` |
| Path and vector data | `FUMGPathData` |
| Shadow data | `FUMGShadowData` |
| Render style | `FUMGRenderStyleData` |
| Widget state | `FUMGWidgetStateData` |
| ScrollBox configuration | `FUMGScrollBoxData` |

Final UMG consumers no longer rebuild a generic property bag. They consume `FUMGWidgetNode` typed ownership directly, or use a domain DTO such as `FUMGProceduralResourceRequest` when a procedural/material/image path needs a compact request object.

## Key Boundaries

### `FUMGWidgetNode`

`FUMGWidgetNode` is the imported intermediate node. It carries layout, slot data, children, source/prototype extensions, and typed payloads.

Maintenance rules:

- Add new node properties to the appropriate typed payload first.
- Do not reintroduce a generic string property table on `FUMGWidgetNode`.
- Do not dual-write the same property to typed payloads and a string map.
- Keep source/prototype data in explicit opt-in extensions instead of moving it into the default aggregate surface.

### Final typed consumers

Final consumers read the typed owner that actually owns the behavior:

- Widget style and common state: `Node.WidgetStyle`, `Node.Render`, `Node.WidgetState`, `Node.ScrollBox`.
- Text content, font, layout, and shadow: `Node.Text`, `Node.TextStyle`, `Node.Shadow`.
- Brush, image, path, effect, and crop behavior: `Node.Brush`, `Node.Path`, `Node.Effect`, plus typed crop/request helpers where needed.
- Procedural resources: `Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(...)`.

The old generic bag bridge is not a supported current contract. New final-consumer code must not create another broad key/value store under a different name.

### `Design2UMGWidgetNodeAccess`

`Design2UMGWidgetNodeAccess` remains the cross-module typed inspection facade owned by Core. It is useful for focused tests, diagnostics, and internal typed-domain helpers that need stable serialized key behavior.

Common retained entries:

| Entry | Use |
|-------|-----|
| `TryGetWidget*` | Typed inspection helper for Core-internal mapping and focused tests; not the preferred final-consumer production path |
| `TryGet*TypedWidgetString` | Domain string serialization for tests and cross-module typed contract checks |
| `SerializeWidget*` | Shared primitive serialization helpers |
| `TryBuildProceduralResourceRequest` | Narrow procedural/material/image DTO construction |

## Gradients And Material Parameters

Gradients already have a current typed contract:

- typed data structure: `FUMGVisualBrushData::FGradientData`
- material ref derivation: `Design2UMGWidgetNodeAccess::GetGradientMaterialRef`
- material params serialization: `Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams`
- final procedural request: `Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest`

Do not treat `MaterialParams` as an open untyped backlog item when maintaining gradients. The main path is: parser / mapper writes `Brush.Gradient`, Core serializes the specific material request fields, and Editor consumers pass the typed request DTO to the procedural resource resolver.

## Adding A Property

Use this order:

1. Decide which typed domain owns the property.
2. Add a field to that payload and define unset vs explicit value semantics.
3. Write the typed field from parser / mapper code.
4. Add a narrow typed inspection or request helper only if a cross-module consumer needs it.
5. Apply the value in the relevant UMG applier / binder from the typed owner or DTO.
6. Add coverage for typed payload parsing and the final UMG application result.

If a property is only a final UMG application detail and has no independent source-domain meaning, keep it in the narrow final consumer path. Do not turn the parser model back into a generic string map.

## Do Not Do This

| Pattern | Reason |
|---------|--------|
| Add a generic `Properties` string table again | It weakens the typed data model |
| Dual-write typed payloads and a string map | It creates two sources of truth |
| Make Editor depend on Core private implementation details | It breaks the cross-module boundary |
| Replace typed payloads with a broad bag-like object | It loses domain and unset semantics |
| Add case-, node-, file-, or sample-specific branches | It becomes overfit and does not satisfy the formal gate |

## Related Code

| File | Purpose |
|------|---------|
| `Source/Design2UMGCore/Public/UMGSchema.h` | `FUMGWidgetNode` typed payload aggregate |
| `Source/Design2UMGCore/Public/UMGSchemaTypedPayloads.h` | typed payload definitions |
| `Source/Design2UMGCore/Public/UMGSchemaFoundation.h` | primitive typed-property value support |
| `Source/Design2UMGCore/Public/UMGWidgetNodeAccess.h` | typed node inspection contract |
| `Source/Design2UMGCore/Public/UMGWidgetNodeProceduralResourceAccess.h` | procedural resource request contract |
| `Source/Design2UMGCore/Private/UMGWidgetNodeAccess.cpp` | typed inspection and serialization helpers |
| `Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp` | typed widget property application registry |
| `Source/Design2UMGEditor/Private/UMGBrushPropertyApplier.cpp` | typed brush / image / border application |
| `Source/Design2UMGEditor/Private/UMGTextPropertyApplier.cpp` | typed text property application |
