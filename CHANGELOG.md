# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- Release hygiene cleanup for GitHub publishing (docs, CI, ignore rules).
- `FUMGPropertyBag` removed legacy string-map accessors (`Contains/Find/FindRef/GetValueOr/operator[]/AsMap/ToMap`) in favor of typed getters plus `ForEachSerialized()`. External code should migrate to `TryGet*()` APIs.

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
