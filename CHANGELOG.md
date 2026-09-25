# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- `DesignImportUtils` (grab-bag of generation orchestration, render preparation, and URL helpers) split into `DesignImportGeneration` and `DesignBlueprintRenderPreparation`; the shared `IsHttpUrl` predicate moved next to its only semantic owner, `FDesignRemoteImageImporter`.
- `FUMGWidgetUtils` and `UMGRenderUtils` split into `FUMGWidgetTreeBuilder` (tree construction and widget naming) and `FUMGWidgetRenderer` (rendering, unpremultiply, font-state dump); `UMGRenderUtils.cpp` removed.
- Import pipeline generation-settings resolution (request, analysis, CDO fallback) collapsed into a single rule function in `DesignImportPipeline`.
- `FDesignNodePatch` now documents its field-synchronization contract: every patch field must be handled by both `MergePatch` and `ApplyPatch` in `RefExpander`.
- Source-wide blank-line formatting normalized (no consecutive blank lines, no trailing blank lines at EOF); no behavior change.
- Public release docs now spell out the current validation boundary: the README badge reflects `repository-hygiene`, Unreal packaging and automation are maintainer-local verifications outside hosted CI, and the maintainer formal admissible gate is a separate release check.
- Release/export docs now align with the current package boundary scripts: public packages must exclude maintainer-local files and folders such as `AGENTS.md`, `CLAUDE.md`, `.claude/`, `.codex/`, `.cowork-temp/`, `openspec/`, raw automation trees, caches, and generated HostProject fallback artifacts.
- Figma token examples in release-facing docs now use placeholders and explicitly say real tokens must not be committed or copied into public logs, command transcripts, screenshots, or release artifacts.

### Removed
- `FUMGJsonParser` no longer reads the legacy image aliases (root-level `Brush.TexturePath`, `WidgetStyle.ImageMode`, `WidgetStyle.ImageTransform`). Only canonical `Brush.Image.*` keys are read; the serializer has emitted exclusively canonical keys since the typed-payload migration. UMG IR JSON produced by pre-migration plugin versions must be re-exported; alias-only inputs now leave the image unset.
- `LayoutFixer` (test-only orphan, no production callers since the bounds-driven layout pipeline landed) removed together with its automation test; README and architecture docs now show `LayoutCalculator` as the only layout projection stage. Historical 1.0.0 entries keep the original mention.
- `FUMGPropertyBag` removed legacy string-map accessors (`Contains/Find/FindRef/GetValueOr/operator[]/AsMap/ToMap`) in favor of typed getters plus `ForEachSerialized()`. External code should migrate to `TryGet*()` APIs.

### Security
- Validation tools enforce a remote-HTTPS boundary at the transport layer (`design2umg_verify_lib/net_guard.py`): cleartext HTTP is refused and loopback/private/link-local/resolved-non-public targets are rejected, redirects included. Downloads are contained to an explicit output directory with a validated file name, and report/log writes go through `pathlib` handles.
- FigmaExporter tests now load the compiled bundle through a CommonJS export block in `code.ts` instead of dynamically evaluating the bundle source.

## [1.0.0] - 2026-02-15

### Added
- Core conversion pipeline: `.pen` to UMG Widget Blueprint.
- Figma REST import (`FigmaParser`, `FigmaHttpClient`, `FigmaImageExporter`).
- Pencil MCP live import (`PencilConnector`, `MCPHttpClient`, `PenMCPBridge`).
- Layout mapping and fixing (`LayoutMapper`, `LayoutFixer`, `LayoutCalculator`).
- SVG path rasterization and procedural shape generation.
- Ref expansion and variable resolution.
- Preview window and drag-and-drop import flow.
- Automated tests for parser/converter/layout/figma paths.

### Fixed
- Stroke-only path rendering regression.
- Smooth SVG curve command handling.
- Nonzero fill scanline performance bottleneck.
