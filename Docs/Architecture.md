# Architecture

Chinese companion: [Architecture.zh-CN.md](Architecture.zh-CN.md)

Design2UMG converts design-side documents into Unreal Engine 5 UMG Widget Blueprints through a layered, source-agnostic pipeline.

## Pipeline

```mermaid
graph LR
    A["Pencil .pen"] --> C["PenParser"]
    B["Figma JSON"] --> D["FigmaParser"]
    C --> E["FDesignNode IR"]
    D --> E
    E --> F["RefExpander"]
    F --> G["Design2UMGConverter"]
    G --> H["VisualMapper"]
    G --> I["LayoutMapper"]
    H --> J["FUMGWidgetNode IR"]
    I --> J
    J --> L["LayoutCalculator"]
    L --> M["UMGGenerator"]
    M --> N["Widget Blueprint"]
```

## Core Boundaries

- `PenParser` and `FigmaParser` are the only places that may interpret format-specific fields.
- `FDesignNode` is the shared design IR. Everything after parsing must operate on IR, not on source-specific JSON.
- `FUMGWidgetNode` is the generation IR used by editor-side Blueprint construction.
- `Design2UMGConverter` owns source-to-UMG conversion orchestration, while specialized helpers handle grid layout, prototype navigation, source metadata, and text widget construction.

## Design Rules

### Parser responsibility stays in the parser

If a fidelity problem comes from source data semantics, fix the responsible parser instead of adding broad runtime branches in shared layers.

### Bounds are the source of truth

Layout projection is driven by bounds. `LayoutCalculator` turns the final bounds into anchors and offsets.

### Typed data is the primary contract

Generated widget data should flow through typed structs (`TextStyle`, `WidgetStyle`, `Text`, `Brush`, `Effect`, `Path`, `Shadow`, `Render`, `WidgetState`, `ScrollBox`) rather than ad hoc legacy property maps.

### Verification is part of the architecture

The formal release gate is not just "does it render" but "does no admissible case regress against the tracked historical best." Refactors are only acceptable if the full gate remains non-regressing.

## Module Overview

| Module | Responsibility |
| --- | --- |
| `Design2UMGCore` | Editor module for parsing, IR definitions, conversion, layout logic, and fidelity-related shared helpers. It is not a runtime module; the Core name means shared editor/importer ownership, not runtime availability. |
| `Design2UMGEditor` | Import pipeline, Blueprint generation, preview/editor UI, automation rendering, editor-only asset workflows |
| `Tools/` | Suite verification, historical-best validation, snapshot wrappers, release governance helpers |

## Release Boundary

- The public release promise covers `.pen` import and Figma design import through REST or local export JSON.
- Bridge/debug workflows such as MCP JSON import remain available but are not treated as the primary release contract.
- Local maintainer workflow files such as `AGENTS.md`, `CLAUDE.md`, `.claude/`, and `.codex/` should remain untracked and are excluded from packaged release outputs.

## Validation Boundary

- Public docs and CI only promise the layers described in [ValidationProtocol.md](ValidationProtocol.md) and [ValidationGovernance/TestCoverageBoundary.md](ValidationGovernance/TestCoverageBoundary.md): `repository-hygiene` for every PR, contributor-facing smoke/repro commands, self-hosted packaging and automation for maintainer changes, and the separate maintainer formal admissible gate.
- Real-project validation is role-based, not machine-path-based. Public examples should use placeholders such as `<UE5_Root>`, `<PluginRoot>`, and `<MaintainerRuntimeProject.uproject>`.
- HostProject fallback is not an acceptable architecture or validation route. If a workflow needs a runtime project, it must use an explicit real UE project instead of creating or inferring a temporary host project.

For a plain-language summary of coverage layers and who can run them, see [ValidationGovernance/TestCoverageBoundary.md](ValidationGovernance/TestCoverageBoundary.md).

## Public API Boundary

The retained public header surface is intentional, but it is still a plugin API surface, not a runtime support promise.

`Source/Design2UMGCore/Public/` owns the shared parser, converter, settings, schema, font, notification, text measurement, layout, value parsing, and typed widget-node access contracts used across Core, Editor, and tests.

`Source/Design2UMGEditor/Public/` contains four editor-facing headers: `Design2UMGEditorModule.h`, `DesignImageDiagnostics.h`, `DesignPreviewContracts.h`, and `PreviewUserWidget.h`. These cover the module entry point, image diagnostics, and preview contracts; they are not runtime module APIs.

Remote image import, Figma/MCP/Pencil connectors, preview windows, and JSON parsing/serialization live under `Source/Design2UMGEditor/Private/`. They are implementation details, not public extension contracts.

## IR Data Flow

The pipeline uses two intermediate representations:

```
FDesignDocument  →  Design2UMGConverter  →  FUMGWidgetNode tree  →  UWidgetBlueprint
   (design IR)                                  (generation IR)
```

`FDesignDocument` is produced by parsers and holds source-agnostic design data. `FUMGWidgetNode` is produced by the converter and holds UMG-specific generation data. `UMGGenerator` consumes the node tree to produce the final Blueprint asset.

### FDesignNode children vs descendants

`FDesignNode::Children` is an ordered list of direct child nodes. `FDesignNode::Descendants` is a key-indexed lookup table for reusable nodes used during ref expansion — key format is either a slash-separated path (`"parentId/childId"`, resolved by recursive path traversal) or a bare node id (`"nodeId"`, resolved by full-tree id search); `RefExpander::ApplyDescendantOverride` selects the strategy based on whether the key contains `/`. `FDesignDocument::Children` is the root node list — same field name as `FDesignNode::Children` but different semantics. `FDesignDocument::FindReusableNode` traverses `Children` recursively and does not use `Descendants`.

### Parser decision fields in IR

Some fields such as `bPaddingRequiresRuntimeWrap` are set by the parser and consumed by the converter. This is intentional: the parser has access to source-format semantics (e.g. whether padding is already baked into child coordinates) that the converter cannot reconstruct. These fields represent parser decisions that must survive the IR boundary.

### Copy semantics

`FUMGWidgetNode` holds a `TUniquePtr<FUMGSourceNodeData>` and is move-only. It is a `USTRUCT`, and UHT-generated code does not support deep-copying `TUniquePtr` members, so copy construction is deleted. `FDesignNode` also holds a `TUniquePtr<FDesignSourceNodeData>` but implements a deep-copy constructor manually (see `DesignSchema.cpp`). The asymmetry is intentional: `FDesignNode` needs to be copyable for ref expansion, while `FUMGWidgetNode` does not.

## Extension Points

### Adding a new design source format

Implement a parser that produces a `FDesignDocument`. The required root payload is `Children` (the root node list), plus document-level metadata the downstream pipeline uses, such as `Fonts`. The current `Pen`/`Figma` dispatch is `ConvertRegisteredSource` in `Source/Design2UMGEditor/Private/Design2UMGEditorImportOrchestrator.cpp`; there is no source registry API.

Reference signatures:

```cpp
static bool FPenParser::ParseDocument(const FString& JsonString, FDesignDocument& OutDocument);
static bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument);
```

For a new source format:

1. Add a parser entry point in `Source/Design2UMGCore/Public/*.h` and parser implementation in `Source/Design2UMGCore/Private/*.cpp`.
2. Add explicit source dispatch in `Design2UMGEditorImportOrchestrator.cpp`, and update the import command entry points in `Design2UMGEditorCommandOwner.cpp` as needed.
3. Pass the parsed document and current settings to `DesignImportPipeline::AnalyzeImport`, check its error, then return its converter output and import-analysis data through the existing workflow.

### Adding a new Widget type mapping

1. Add a type mapping entry in `FDesignTypeMapper` (`Source/Design2UMGCore/Private/DesignTypeMapper.cpp`).
2. Add a typed-node handler function and register it in `GetPropsApplierMap()` inside the `UMGPropertyApplierInternal` namespace (`Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp`). Signature: `void(UWidget*, const FUMGWidgetNode&)`.
3. Add or reuse the narrow typed payload / request DTO that owns the behavior. Text, brush, render, widget style, widget state, and procedural resource flows already have typed owners.
4. To add new typed payload data, add a field or struct in `UMGSchemaTypedPayloads.h` and hold it from `FUMGWidgetNode` through the existing local typed payload pattern.

Reference signatures:

```cpp
static FString FDesignTypeMapper::MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout);
const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap();
void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);
```

Contributor checklist by scenario:

1. New design node kind -> widget class:
   - file: `Source/Design2UMGCore/Private/DesignTypeMapper.cpp`
   - function: `FDesignTypeMapper::MapNodeTypeToWidget(...)`
2. New widget that consumes current typed payloads:
   - file: `Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp`
   - function: `GetPropsApplierMap()`
3. New widget that needs a new typed owner or DTO:
   - file: `Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp`
   - functions: `GetPropsApplierMap()` and the relevant typed payload / request builder

### Extending the IR without breaking existing conversions

New transient fields in `DesignSchema.h` and adjacent schema bridges should prefer ordinary `TOptional<T>` with a default unset state for set-vs-unset semantics. Follow the current unreflected optional fields rather than assuming `UPROPERTY()` support across all adapted UE versions. The converter checks `IsSet()` before processing optional fields.

Reference pattern:

```cpp
TOptional<float> Width;
TOptional<FMargin> Padding;
TOptional<bool> IsExposedInstance;
```

Do not introduce new `bHas*` + value shadow pairs into `DesignSchema.h`-style transient IR unless there is a documented serializer or typed-contract reason to keep that shape. Prefer `TOptional<T>` when you only need set-vs-unset presence. For existing typed payload structs in `UMGSchemaTypedPayloads.h`, follow the current local contract instead of forcing a blind `TOptional` rewrite.

## Known Issues

### TextMeasurement and FontResolver process-local state

`TextMeasurement` and `FontResolver` currently keep process-local cache/callback state. The shared state is guarded by locks and fits the normal editor import path, where imports are not treated as independent parallel jobs.

This is not a full architecture promise for parallel imports. If the product scope later requires concurrent import workers in the same editor process, this state must be reviewed as part of that design instead of assuming the current cache/callback boundary is already sufficient.

### UMGPropertyApplierInternal module placement

`UMGPropertyApplierInternal.cpp` stays in `Design2UMGEditor` by design. The supported product path is editor-side import and Widget Blueprint generation, so mutating `UWidget` instances belongs with the editor generation pipeline instead of becoming a Core public contract.

Re-evaluate this placement only if the supported product scope explicitly adds a runtime module, or if the `UWidget` mutation pipeline is split from editor-side generation into a separately supported runtime path. Until then, keep Core responsible for typed schema and narrow typed inspection/request helpers, and keep final `UWidget` mutation in the Editor module.
