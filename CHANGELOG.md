# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- Release hygiene cleanup for GitHub publishing (docs, CI, ignore rules).
- Figma import command now supports environment-variable token flow and warns on plaintext CLI token usage.
- CI now gates release with hygiene checks, plugin build, automation tests, and uploaded artifacts.
- Public naming/version consistency updates (`Design2UMG`, `VersionName: 1.0.0`).
- Critical mojibake cleanup in parser/schema comments and release rules.

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
