# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- Public release docs now spell out the current validation boundary: the README badge reflects `repository-hygiene`, the packaged plugin artifact comes from the manual self-hosted `build-plugin` job, and the maintainer formal admissible gate is a separate release check.
- Release/export docs now align with the current package boundary scripts: public packages must exclude maintainer-local files and folders such as `AGENTS.md`, `CLAUDE.md`, `.claude/`, `.codex/`, `.cowork-temp/`, `openspec/`, raw automation trees, caches, and generated HostProject fallback artifacts.
- Figma token examples in release-facing docs now use placeholders and explicitly say real tokens must not be committed or copied into public logs, command transcripts, screenshots, or release artifacts.

### Removed
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
