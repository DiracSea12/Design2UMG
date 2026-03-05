# Design2UMG

Convert design files into Unreal Engine 5 UMG Widget Blueprints.

Supported inputs:
- Pencil `.pen` files
- Figma REST API
- Live Pencil MCP connection

![Light Theme](Docs/Images/step1_light_theme.png)
![Dark Theme](Docs/Images/step2_dark_theme.png)

## Features

- Multi-source import pipeline (`.pen`, Figma, MCP)
- Layout conversion (flex-like design layout to UMG slots/anchors)
- Visual mapping (fill, stroke, radius, shadow, gradients)
- SVG path rasterization
- Ref expansion and design variable resolution
- Preview and editable intermediate JSON
- Drag-and-drop import in Content Browser
- Editor-only plugin, no runtime dependency

## Requirements

- Unreal Engine 5.4+
- Pencil desktop app (optional, for MCP live sync)
- Figma Personal Access Token (optional, for Figma import)

## Installation

Copy the plugin into either location:

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

Enable `Design2UMG` in `Edit > Plugins`.

## Quick Start

### 1. Import `.pen` file

Drag a `.pen` file into Content Browser, or use console command:

```text
Design2UMG.Import <absolute_or_relative_path_to.pen>
```

Optional flags:

```text
Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]
```

### 2. Import from Figma

Recommended (safer): use environment variable `FIGMA_TOKEN` and pass only file key/url.

```text
Design2UMG.FigmaImport <file_url_or_key>
```

Compatibility mode (not recommended, token may leak in shell history/logs):

```text
Design2UMG.FigmaImport <file_url_or_key> <personal_access_token>
```

Optional explicit env key:

```text
Design2UMG.FigmaImport <file_url_or_key> --token-env=FIGMA_TOKEN
```

### 3. Import local Figma export JSON

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

### 4. Import from MCP JSON files

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

## Settings

`Edit > Editor Preferences > Plugins > Design2UMG`

Key options:
- Design Width / Height
- Remote Image Import
- Icon Font mapping
- MCP Server Path override
- Incremental update

## Architecture

```text
.pen / Figma / MCP
  -> Parser / Bridge
  -> Design2UMGConverter
  -> LayoutFixer + LayoutCalculator
  -> UMGGenerator
```

Key principle: **Bounds is the source of truth** for layout projection.

## Supported Widget Types

`CanvasPanel`, `Overlay`, `HorizontalBox`, `VerticalBox`, `SizeBox`, `ScaleBox`, `Border`, `Image`, `TextBlock`, `Spacer`, `WidgetSwitcher`

## Known Limitations

See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for the current list and mitigation notes.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Versioning

Design2UMG uses semantic versioning (`MAJOR.MINOR.PATCH`).
Each release must keep `Design2UMG.uplugin -> VersionName` and `CHANGELOG.md` aligned to the same version string.

## License

[MIT](LICENSE)
