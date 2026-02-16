# Design2UMG

Convert design files to Unreal Engine 5 UMG Widget Blueprints. Supports [Pencil](https://pencil.evolves.dev/) `.pen` files, Figma (via REST API), and live Pencil MCP connection.

![Light Theme](Docs/Images/step1_light_theme.png)
![Dark Theme](Docs/Images/step2_dark_theme.png)

## Features

- **Multi-Source Input**: Import from `.pen` files, Figma REST API, or live Pencil MCP connection
- **Layout Engine**: CSS flexbox to UMG anchors/offsets with 9-grid anchor selection and hysteresis
- **Layout Fixer**: Auto-fix overflow, alignment snapping, equal spacing before anchor calculation
- **Visual Properties**: Fill, stroke, corner radius, box shadow, background blur, gradients
- **SVG Path Rasterization**: Full SVG path command set (M/L/H/V/C/Q/S/T/A/Z)
- **Component System**: Ref node expansion, design token/variable resolution, theme support
- **Text Styling**: Font size, weight, color, letter spacing, line height, alignment
- **Procedural Shapes**: Circle, ring, rounded rect, gradient textures
- **Drag & Drop**: Import `.pen` files directly from Content Browser
- **Live Sync**: Real-time design updates via MCP protocol (JSON-RPC 2.0 over SSE)
- **Preview Window**: Inspect and edit intermediate JSON before generation
- **Zero Runtime Dependencies**: Pure C++ / UE5 Engine API, editor-only plugin

## Requirements

- Unreal Engine 5.4+
- Pencil desktop app (optional, for live connection)
- Figma Personal Access Token (optional, for Figma import)

## Installation

Clone or copy this plugin to your engine or project plugin directory:

```
# Engine plugin
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/

# Project plugin
<Project>/Plugins/Design2UMG/
```

Enable in Edit > Plugins > "Pen To UMG".

## Quick Start

### Drag & Drop

Drag a `.pen` file into the Content Browser. Select a frame if multiple exist. Done.

### Commandlet

```bash
UnrealEditor.exe "YourProject.uproject" \
  -ExecCmds="Design2UMG.Import path/to/design.pen --node=<NodeId> --no-preview" \
  -log -nosplash -stdout -unattended
```

### Figma Import

```bash
# In UE5 console
Design2UMG.FigmaImport <file_key> <personal_access_token> [node_id]
```

### Live Pencil Connection

1. Open Pencil desktop app with your design
2. In UE5 Editor, open Design2UMG toolbar > "Connect"
3. Select a frame — changes sync in real-time

## Architecture

```
.pen / Figma API / Pencil MCP
    │
    ▼
PenParser / FigmaParser / PenMCPBridge
    │
    ▼
Design2UMGConverter
    ├── PenRefExpander (component refs)
    ├── PenVariableResolver (design tokens)
    ├── PenVisualMapper (fill, stroke, shadow)
    └── PenLayoutMapper (gap, padding, justify)
    │
    ▼
LayoutFixer (overflow, alignment, equal spacing)
    │
    ▼
LayoutCalculator (Bounds → Anchors/Offsets)
    │
    ▼
UMGGenerator → Widget Blueprint
```

Key principle: **Bounds is the single source of truth.** All layout works on pixel-coordinate Bounds, projected to UMG Anchors/Offsets at the end.

| Module | Responsibility |
|--------|---------------|
| `PenParser` | Parse `.pen` JSON into `FPenDocument` AST |
| `FigmaParser` | Parse Figma REST API JSON into `FPenDocument` |
| `Design2UMGConverter` | Transform Pen nodes to `FUMGWidgetNode` tree |
| `LayoutFixer` | Fix bounds overflow, alignment snapping, equal spacing |
| `LayoutCalculator` | 9-grid anchor selection with hysteresis, edge snapping |
| `UMGGenerator` | Create UWidget objects and Widget Blueprint asset |
| `PenMCPBridge` | MCP protocol handler (JSON-RPC 2.0 over SSE) |
| `DesignSourceRegistry` | Extensibility point for design tool backends |

## Settings

Edit > Editor Preferences > Plugins > Pen To UMG

| Setting | Description | Default |
|---------|-------------|---------|
| Design Width | UMG design resolution width | 1920 |
| Design Height | UMG design resolution height | 1080 |
| Enable Remote Image Import | Auto-download remote images | true |
| Remote Image Import Path | Save path for imported images | /Game/Design2UMG/ImportedImages |
| Remote Image Timeout | Download timeout (seconds) | 20 |
| MCP Server Path | MCP server path (blank = auto-discover) | |

## Supported Widget Types

`CanvasPanel` · `Overlay` · `HorizontalBox` · `VerticalBox` · `SizeBox` · `ScaleBox` · `Border` · `Image` · `TextBlock` · `Spacer` · `WidgetSwitcher`

## Known Limitations

- Complex nested component overrides may not fully resolve
- Gradients: basic H/V/Radial with two color stops (multi-stop + arbitrary angle planned)
- Box shadow clipped under ClipToBounds (SDF material fix planned)
- Non-circular ellipses approximate as capsule shape (path rasterization fix planned)
- Animation/interaction properties not yet converted

See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for details and [Docs/ROADMAP.md](Docs/ROADMAP.md) for the development plan.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
