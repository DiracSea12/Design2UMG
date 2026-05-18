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
  - `diagnostics`
  - `resources`
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

`resources`
- `missingFonts`
- `imageFailures`
- `imageImportSummary`

`effects`
- `wrapperCount`
- `wrappers`

## Top-level compatibility aliases

The writer still emits these top-level aliases because they are already shipped and tested:

- `sourceFilePath` -> alias of `source.filePath`
- `sourceIdentity` -> alias of `source.identity`
- `requestedOutputPath` -> alias of `output.requestedOutputPath`
- `requestedRenderPngPath` -> alias of `output.requestedRenderPngPath`
- `generatedAssetPath` -> alias of `output.generatedAssetPath`
- `renderPngPath` -> alias of `output.renderPngPath`
- `warnings` -> alias of `diagnostics.warnings`
- `skippedDesignerNodes` -> alias of `diagnostics.skippedDesignerNodes`
- `missingFonts` -> alias of `resources.missingFonts`
- `imageImportReport` -> alias of `resources.imageImportSummary`

These aliases are compatibility-retained fields, not the preferred long-term read path.

## Human-facing fields

`recommendations` is human-facing guidance. It is intentionally readable prose and should not be treated as a strict machine contract.

## Typed provenance owner

The current typed owner for source-file provenance is `FDesignSourceDocumentData::SourceFilePath`.

`Document.Metadata["design.sourceFilePath"]` is still retained as a compatibility alias because some existing report/bake/test paths still expect the old key.
