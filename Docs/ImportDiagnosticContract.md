# Import Diagnostic Report Contract

This document covers the JSON written by `DesignImportDiagnosticReport::Write()`.

It does **not** cover helper stderr lines such as `[DIAGNOSTIC] ...` from the Python validation scripts. Those are a separate tooling contract.

## Machine-stable fields

The current machine-facing contract is built around these grouped sections:

- top-level scalars:
  - `pluginVersion`
  - `documentVersion`
- grouped objects:
  - `source`
  - `output`
  - `outcome` (when a commit result is supplied)
  - `diagnostics`
  - `componentMapping`
  - `reimport` (on reimport reports)
  - `resources`
  - `fontResolution`
  - `effects`

These grouped objects are the fields new tooling should read first.

## Grouped section meaning

`source`
- `origin`: inferred source kind such as `pen_file` or `local_json`
- `documentVersion`: copied from the parsed design document when available
- `filePath`: resolved source file path when the document carries one
- `identity`: stable source identity derived from that file path

`output`
- `requestedOutputPath`
- `requestedRenderPngPath`
- `generatedAssetPath`
- `renderPngPath`

`diagnostics`
- `warnings`
- `skippedDesignerNodes`
- `degradations`

`outcome`
- `status`: `succeeded`, `succeeded_with_degradation`, `not_committed`, `committed_with_metadata_failure`, `rollback_incomplete`, or `conflict`.
- `succeeded`: true only for the two successful statuses. It describes the asset commit, not whether an optional PNG or preview operation succeeded.
- `assetState`, `manifestState`: `unchanged`, `saved`, `restored`, or `indeterminate`.
- `memoryState`: `unchanged`, `modified`, `restored`, or `recovery_failed`.
- `assetPath`: the commit target, present when known; its presence alone does not prove a successful save.

The writer serializes the supplied typed result; it does not infer success from diagnostic kinds or message text. An absent `outcome` means no commit result was supplied, not success. `manifestState=unchanged` also covers an asset-only save that did not request a manifest. File and memory states describe the state at return; `indeterminate` means the final file contents are not guaranteed.

`succeeded` means the requested asset/manifest commit completed without reported degradation. `succeeded_with_degradation` is still a successful commit, but some design behavior or content was skipped or preserved. Examples include unsupported Prototype wiring, skipped declared mapping inputs, and nested subtrees preserved outside the safe-delete scope. Component batches propagate both page and dependency degradation. Informational mapping hits and successful input writes do not imply degradation.

`committed_with_metadata_failure` means the asset was saved but its manifest failed. `not_committed` means there is no successful commit; inspect file and memory states to distinguish untouched state from a completed rollback or remaining memory edits. `rollback_incomplete` reports uncertain files or incomplete recovery. `conflict` reports an operation blocked by an ownership or identity conflict. These failure states take precedence over degradation; a warning cannot turn a failed operation into success.

`componentMapping` and `reimport`
- `entries`: structured `kind` / `message` pairs. `kind` identifies the event; `message` explains it for people and is not a parsing contract.
- `prototype_wiring_skipped` identifies skipped Prototype graph wiring; `mapping_input_skipped` identifies an unapplied declared input; `reimport_partial_sync` identifies deliberately preserved nested design content.
- For successful commits these events must agree with `outcome.status=succeeded_with_degradation`. Their presence during an unsuccessful operation does not replace the failure outcome.

`resources`
- `missingFonts`
- `imageFailures`
- `imageImportSummary`

`fontResolution`
- `results`: compact per-declaration font resolution diagnostics. `resolvedAssetPath` can be an exact project font match, or a diagnostic fallback candidate when `resolutionState` remains `default-font`; it does not by itself prove the generated UMG node was explicitly bound to that fallback asset.

`effects`
- `wrapperCount`
- `wrappers`

## Top-level path aliases

The writer still emits these top-level scalar path aliases because existing import/bake paths read them directly:

- `sourceFilePath` -> alias of `source.filePath`
- `sourceIdentity` -> alias of `source.identity`
- `requestedOutputPath` -> alias of `output.requestedOutputPath`
- `requestedRenderPngPath` -> alias of `output.requestedRenderPngPath`
- `generatedAssetPath` -> alias of `output.generatedAssetPath`
- `renderPngPath` -> alias of `output.renderPngPath`

Diagnostics and resources are only emitted through grouped sections. New tooling should not read top-level `warnings`, `skippedDesignerNodes`, `missingFonts`, or `imageImportReport` fields.

## Human-facing fields

`recommendations` is human-facing guidance. It is intentionally readable prose and should not be treated as a strict machine contract.

## Typed provenance owner

The current typed owner for source-file provenance is `FDesignSourceDocumentData::SourceFilePath`.

`Document.Metadata["design.sourceFilePath"]` is still retained as a compatibility alias because some existing report/bake/test paths still expect the old key.
