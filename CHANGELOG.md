# Changelog

All notable changes to Design2UMG will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Planned
- Path rasterization cache (performance)
- BoxShadow SDF material replacement
- Gradient multi-stop + arbitrary angle
- Ellipse via path rasterization
- Prototype Flow (page navigation logic)

## [1.0.0] - 2026-02-15

### Added
- Core conversion pipeline: `.pen` → UMG Widget Blueprint
- Figma REST API support (`FigmaParser`, `FigmaHttpClient`, `FigmaImageExporter`)
- Pencil MCP live connection (`PencilConnector`, `MCPHttpClient`, `PenMCPBridge`)
- Layout engine: CSS flexbox → UMG anchors/offsets
  - 9-grid anchor selection with hysteresis (anti-jitter)
  - Edge snapping and stretch anchor detection
- LayoutFixer: bounds overflow correction, alignment snapping, equal spacing detection
- Visual properties: fill, stroke, corner radius, box shadow, background blur
- SVG path rasterization (M/L/H/V/C/Q/S/T/A/Z commands)
- Procedural shape generation (circle, ring, rounded rect, gradients)
- Component ref expansion (`PenRefExpander`)
- Design token/variable resolution with theme support (`PenVariableResolver`)
- Text styling: font size, weight, color, letter spacing, line height, alignment
- Content Browser drag & drop import
- Commandlet for headless batch import
- JSON preview/edit window
- Remote image download and import
- Widget class override rules via editor settings
- Automated tests for Parser, Converter, LayoutCalculator, FigmaParser

### Fixed
- fill:none + stroke-only paths no longer filled white
- SVG S/s (smooth cubic) and T/t (smooth quad) commands now supported
- nonzero scanline fill performance (was O(n³), now O(n) per scanline)
