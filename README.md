# Design2UMG

Convert design files into Unreal Engine 5 UMG Widget Blueprints.

[![Repository Hygiene](https://github.com/DiracSea12/ImageToUMG/actions/workflows/build.yml/badge.svg)](https://github.com/DiracSea12/ImageToUMG/actions/workflows/build.yml)

Chinese companion: [README.zh-CN.md](README.zh-CN.md)

Project roadmap: [Docs/ROADMAP.md](Docs/ROADMAP.md)

Formal release scope:
- Pencil `.pen` files
- Figma Design import through REST API or local export JSON

Bridge / debug utilities that remain available but are not formal release promises:
- MCP JSON bridge import
- Live Pencil MCP connection for local tooling/debug workflows

![Light Theme](Docs/Images/step1_light_theme.png)
![Dark Theme](Docs/Images/step2_dark_theme.png)

## Demo And Examples

- `Examples/` includes ready-to-import `.pen` samples such as `Dashboard.pen`, `GameHUD.pen`, `LoginForm.pen`, and `SimpleCard.pen`.
- The screenshots above show the current preview surfaces shipped with the plugin workflow.
- A fast local smoke path is: enable the plugin, drag one of the example `.pen` files into Content Browser, then inspect the generated Widget Blueprint and preview PNG.

## Validation At A Glance

This repo intentionally separates contributor-facing validation from maintainer release gating:

| Layer | Purpose | Who runs it |
|------|------|------|
| `repository-hygiene` | Public repo boundary and required-file checks | GitHub Actions on push / PR |
| `self-hosted build and automation` | Package the plugin, run focused Unreal automation, and upload the artifact | Maintainer pushes, same-repository PRs, and manual runs |
| `public smoke / public validate` | Local build plus focused validation / repro commands | Contributors and maintainers |
| `maintainer formal admissible gate` | Full discovered case set release gate | Maintainers only |

The workflow badge above reflects the tracked `Plugin CI` workflow. Unreal packaging and automation run on the maintainer self-hosted runner for pushes, same-repository PRs, and manual runs; external PRs run only repository hygiene. This is not presented as a public hosted UE formal gate.

For a plain-language coverage boundary and a contributor-versus-maintainer split, see [Docs/ValidationGovernance/TestCoverageBoundary.md](Docs/ValidationGovernance/TestCoverageBoundary.md).

## Features

- Multi-source import pipeline (`.pen`, Figma, MCP bridge utilities)
- Layout conversion (flex-like design layout to UMG slots/anchors)
- Visual mapping (fill, stroke, radius, shadow, gradients)
- SVG path rasterization
- Ref expansion and design variable resolution
- Preview and editable intermediate JSON
- Drag-and-drop import in Content Browser
- Editor-only plugin, no runtime dependency

## Requirements

- Unreal Engine 5.4+
- Pencil desktop app (optional, for MCP bridge/debug tooling)
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

```text
Design2UMG.FigmaImport <file_url_or_key> <FigmaAccessToken>
```

Use a token from a secure source such as local user settings or environment provisioning.
Do not treat command-line token literals as a recommended team workflow.

### 3. Import local Figma export JSON

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

### 4. Import from MCP JSON files

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

This MCP JSON path is a bridge/debug utility. The release candidate does not treat live sync itself as a formal support promise.

## Automation

Full automation visual verification now has two layers:

1. `Design2UMG.AutomationRender <spec.json>`
   Renders the final compiled Widget Blueprint to a PNG from a manifest file.
2. `Tools/design2umg_auto_verify.py`
   Prepares a design reference image, launches Unreal Editor unattended, renders the candidate PNG, then writes diff and JSON reports.

Imports and automation runs now also emit a structured import diagnostic report under:

```text
<Project>/Saved/Design2UMG/ImportReports/*.import_report.json
```

The report is the single authority artifact for import troubleshooting and includes source/output binding, degradations, resource diagnostics, effect wrappers, and operator recommendations.

Example:

```text
python Tools/design2umg_auto_verify.py pen --source <DesignRoot>\screen.pen --node sbChi --reference-provider png --reference <DesignRoot>\screen.png --project <MaintainerRuntimeProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

Figma reference export is also supported:

```text
python Tools/design2umg_auto_verify.py local-figma --source <FigmaExportRoot>\design.json --reference-provider figma --figma-file <fileKeyOrUrl> --figma-node 123:456 --figma-token <FigmaAccessToken> --project <MaintainerRuntimeProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

`DESIGN2UMG_RUNTIME_PROJECT` can replace `--project`. `DESIGN2UMG_UNREAL_EDITOR` or `UE_ROOT` can replace `--editor`.
The script fails if neither the project nor editor path is provided; it does not create or infer a HostProject.
These `design2umg_auto_verify.py` examples are maintainer-oriented focused automation, not the public PR gate.

Maintainer visual validation expects local font seed assets to be prepared before the run. Place them in a discoverable `Content/Design2UMG/Fonts` directory next to the design source or in the explicit runtime project. The verifier copies those assets; it does not download fonts or repair broken UE font assets. A valid seed is a UE `UFont` with at least one default typeface entry backed by a valid `FontFace` or equivalent font data. A lone `.ttf` file, an empty `UFont`, or a typeface entry with no font data is treated as missing/invalid.

Contributor-facing helpers versus maintainer-only automation flows are listed in [Tools/README.md](Tools/README.md).
Coverage boundaries for unit tests, smoke checks, focused automation render, and the maintainer formal gate are summarized in [Docs/ValidationGovernance/TestCoverageBoundary.md](Docs/ValidationGovernance/TestCoverageBoundary.md).

## Settings

`Edit > Editor Preferences > Plugins > Design2UMG`

Key options:
- Design Width / Height
- Remote Image Import
- Icon Font mapping
- Widget Class override rules
- MCP Server Path override
- Pencil App Name override
- Known web font auto import is experimental, defaults off, and should not be treated as a required production workflow

## Import Semantics

- Re-import currently generates a new asset when the target name already exists.
- The plugin does **not** currently promise in-place patch/reimport of an existing Widget Blueprint.

## Verification Boundary

- Formal full-fidelity release validation is performed against a maintainer-owned canonical runtime boundary.
- The public repository must not treat that maintainer boundary as a portable user setup recipe.
- HostProject fallback is not part of the supported verification workflow.

## Repository And Release Checks

- `repository-hygiene`: the public GitHub Actions check that verifies required repo files and blocks tracked maintainer-local paths.
- `self-hosted build and automation`: the packaging and Unreal automation job in `.github/workflows/build.yml`; it runs for maintainer pushes, same-repository PRs, and manual dispatches, requires a Windows runner with `UE_ROOT` and `DESIGN2UMG_RUNTIME_PROJECT`, validates the package boundary, runs focused generation, Figma parser, and lifecycle tests from the packaged plugin, and uploads the artifact.
- `public smoke/public validate`: contributor-facing build and validation steps used for smoke checks and local repro, using explicit `--project` / `--editor` values or the matching environment variables; do not present them as the release gate.
- `maintainer formal admissible gate`: the maintainer-owned full discovered admissible case set release gate documented in `Docs/ValidationProtocol.md`.
- `Tools/README.md`: the short index for contributor-facing helpers versus maintainer-only workflows.
- Public packages and repo exports must exclude root review reports, `openspec/`, AGENTS/CLAUDE local rules, raw automation trees, cache folders, generated HostProject fallback artifacts, generated fallback host directories, concrete fallback project paths, and maintainer absolute paths.
- No public hosted UE formal gate, public UE matrix, or coverage badge is published in this repo today.

## Widget Class Overrides

- `Edit > Editor Preferences > Plugins > Design2UMG` exposes class override rules that replace the default generated widget class when the node name matches a configured pattern.
- Matching is case-insensitive `Name Contains`.
- The first matching rule wins.
- Verification path: generate a Blueprint, inspect the target widget in the Widget Tree, and confirm the constructed widget class matches the override rule.

## Architecture

See [Docs/Architecture.md](Docs/Architecture.md) for the full pipeline, module boundaries, and release-facing architecture rules.

```text
.pen / Figma / MCP
  -> Parser / Bridge
  -> Design2UMGConverter
  -> LayoutCalculator
  -> UMGGenerator
```

Key principle: **Bounds is the source of truth** for layout projection.

## Supported Widget Types

`CanvasPanel`, `Overlay`, `HorizontalBox`, `VerticalBox`, `SizeBox`, `ScaleBox`, `Border`, `Image`, `TextBlock`, `Spacer`, `WidgetSwitcher`

## Known Limitations

See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for the current list and mitigation notes.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
