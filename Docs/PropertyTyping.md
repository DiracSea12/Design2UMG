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
| Brush, image, shape, and gradient data | `FUMGVisualBrushData` |
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

Core owns primitive serialization, image-transform parsing, material-ref derivation, and verbose presentation through this facade. These functions serve explicit interchange, diagnostic, and test boundaries; final consumers read their typed domain instead of performing keyed lookups.

`TryGetWidget*` helpers retained in test headers are not public production APIs. Procedural request construction belongs to the separate `Design2UMGWidgetNodeProceduralResourceAccess` namespace.

## Fixed Resource And Text Payloads

| Domain | Canonical owner | Final consumer |
|--------|-----------------|----------------|
| Gradients | `Brush.Gradient` | Typed `FUMGProceduralResourceRequest::Gradient` |
| Generated shapes, shadows, and placeholders | `Brush.Shape` plus `Brush.MaterialRef` | Typed `FUMGProceduralResourceRequest::Shape` |
| Rich-text style rows | `Text.RichTextStyleRows`, an optional map of `FUMGRichTextStyleRowData` | `UMGTextStyleApplication::ApplyRichTextStyleRows` |
| Image path, mode, and transform | `Brush.Image` | Importer, crop helper, resource binder, and brush applier |
| Prototype trigger keys | `FDesignPrototypeInteraction::TriggerKeyCodes` | Direct copy to `FUMGPrototypeNavigationBinding::TriggerKeyCodes` |

Mappers do not duplicate gradients into `MaterialRef` / `MaterialParams`. The procedural request derives the gradient selector and carries its typed data. `SerializeGradientMaterialParams` remains a serialization helper used by test adapters and external-input parity tests, not an internal resource-generation step.

Shape radii and thickness use normalized fractions; optional fields retain the generator's defaults when absent. A present shape payload, including an explicitly empty one, is authoritative and is not merged with `MaterialParams`. Gradient data has precedence over shape and external parameters. Resource cache identity includes the active typed fields and their presence; successful materialization replaces them with the resolved image path.

The Shape JSON boundary accepts numeric fields only as JSON numbers. Floating-point fields and all four corner-radii components must be finite and fit in a float before narrowing. Integer fields retain the existing int32 range and numeric rounding behavior; colors retain the existing color-string parser. Invalid declared fields produce a warning and remain absent, without discarding valid sibling fields. Invalid corner arrays are rejected as a whole. An invalid Shape object is diagnosed and omitted; an explicit empty object stays present. This is input normalization, not a second validation layer in the renderer.

Prototype key codes are extracted once from the source trigger object in `FigmaParserPrototype.cpp`. The source extension carries the normalized, ordered unique list to the graph assembler, which no longer parses `TriggerJson`. Raw trigger metadata remains available for preservation and reporting, but cannot override or refill the typed key list. A keyboard trigger with an empty typed list retains the existing skip diagnostic.

Rich-text builders produce typed rows directly. The JSON parser and serializer retain the existing `TextData.RichTextStyleRows` encoded-string interchange format at the boundary only. Absent or invalid row input does not replace a style set; a present empty map clears it. Optional size, spacing, and color retain their existing application and inheritance semantics.

Byte-color and established raster-parameter precision are preserved without string conversion. The typed path does not change font measurement, shadow layout, or source unit conventions.

## Input Aliases And Open Parameters

`FUMGJsonParser::ParseBrushPayload` is the only normalization point for the existing image aliases:

- `Brush.Image.TexturePath` wins over the root `Brush.TexturePath`, including an explicit empty canonical value. A conflicting nonempty root value retains its warning.
- `WidgetStyle.ImageMode` keeps its established precedence over `Brush.Image.Mode`.
- A valid `Brush.Image.Transform` wins; an existing `WidgetStyle.ImageTransform` string is used only when no valid canonical transform was supplied. Invalid transforms are diagnosed at input.

The IR and serialized output contain only `Brush.Image`; downstream code does not carry or resolve duplicate owners. These are retained input forms, not permission to add another compatibility path.

`MaterialParams` remains opaque string/JSON input for explicit user parameters and the existing external material-input interface. Its external parser is retained; internal shape and gradient producers do not generate it. Text markup, vector geometry, and interchange/log serialization remain their existing domain formats rather than being replaced by a generic schema or property framework.

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
