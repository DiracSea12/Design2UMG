# Contributor Internals

This guide explains where to make changes when extending Design2UMG without breaking the shared conversion pipeline.

## Pipeline ownership

```text
.pen / Figma JSON
  -> PenParser / FigmaParser
  -> FDesignNode IR
  -> RefExpander
  -> Design2UMGConverter
  -> FUMGWidgetNode IR
  -> LayoutFixer / LayoutCalculator
  -> UMGGenerator
```

- Source-specific semantics belong in the matching parser.
- Shared IR semantics belong in `FDesignNode` and the mapper/converter layers.
- Blueprint/editor concerns belong in `Design2UMGEditor`.

## When to change a parser

Change `PenParser` or `FigmaParser` when the issue is caused by source-export semantics:

- a field is interpreted incorrectly
- padding, clip, or bounds data needs different source-side mapping
- a source format exposes metadata that the shared pipeline should consume

Do not add source-specific branches in `Design2UMGConverter`, `LayoutMapper`, or `VisualMapper` when the source parser can express the intent correctly in IR.

## Adding a new node type

1. Extend the relevant enum and schema fields in `DesignSchema.h`.
2. Map source data in the responsible parser.
3. Add conversion handling in `Design2UMGConverter` or its helper modules.
4. Add rendering/layout behavior in `VisualMapper` and `LayoutMapper` only if it is shared IR behavior.
5. Add or update focused tests under `Source/Design2UMGEditor/Private/Tests`.

## Adding a new visual or layout property

1. Add typed schema fields first.
2. Teach parsers how to populate those typed fields.
3. Consume them in `VisualMapper`, `LayoutMapper`, or `UMGPropertyApplier` as appropriate.
4. Prefer typed accessors over ad hoc string properties.

## Reference and component behavior

- `RefExpander` owns component expansion, deep-copying, descendant overrides, and retheming.
- If a change affects component instances or descendant patching, start there before adding post-conversion fixes.

## Validation expectations

For any behavior change:

1. Build the plugin against the real project.
2. Run the smallest relevant automation tests.
3. Re-run any affected snapshot validations.
4. Treat the formal admissible gate as the final regression authority when behavior changes are involved.

Do not accept a change that makes any formal case worse than its tracked historical best.
