# Design2UMG

Validation tools accept your own case directory and discover its actual size. The maintainer's private visual corpus is not distributed or required; an empty directory means zero tests, not a passed visual gate. See [the tooling guide](Tools/README.md).

Convert design files into Unreal Engine 5 UMG Widget Blueprints.

[![Plugin CI](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml/badge.svg)](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml)

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
- `Examples/AiHud/` is a ready-to-import Figma export sample ("AI NPC Demo — 中式赛博朋克 HUD"): import its `design.json` with the editor console command `Design2UMG.LocalFigmaImport Examples/AiHud/design.json`.
- The screenshots above show the current preview surfaces shipped with the plugin workflow.
- A fast local smoke path is: enable the plugin, drag one of the example `.pen` files into Content Browser, then inspect the generated Widget Blueprint and preview PNG.

## Validation At A Glance

This repo intentionally separates contributor-facing validation from maintainer release gating:

| Layer | Purpose | Who runs it |
|------|------|------|
| `repository-hygiene` | Public repo boundary and required-file checks | GitHub Actions on push / PR |
| `maintainer build and automation` | Package the plugin, run focused Unreal automation, and upload the artifact | Maintainer-provided self-hosted Windows runner |
| `public smoke / public validate` | Local build plus focused validation / repro commands | Contributors and maintainers |
| `maintainer formal admissible gate` | Full discovered case set release gate | Maintainers only |

The badge reflects the whole `Plugin CI` workflow, not one job. Hosted checks include repository hygiene and tool tests. Maintainer pushes, same-repository PRs and manual runs additionally request the self-hosted UE job; without a runner it stays queued. Fork PRs run hosted checks only. This badge is not full visual or packaged-game evidence.

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

- Unreal Engine: Windows / UE 5.7 is the current maintainer verification environment. Other versions and platforms need their own build and runtime evidence; older 5.4+ wording is not a verified support matrix.
- Pencil extension in VS Code with a `.pen` document open in its design editor (optional, for live Pencil import; currently verified with 0.6.77)
- Figma Personal Access Token (optional, for Figma import)

## Installation

Copy the plugin into either location:

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

Use exactly one installation location; never keep both copies enabled/discoverable.

- **Source download:** install the C++ toolchain required by your UE version and build the project editor target; see [CONTRIBUTING](CONTRIBUTING.md). A Blueprint-only project also needs a compatible precompiled package or a C++ build setup.
- **Precompiled download:** use a release ZIP matching the stated UE version and platform. Extract the `Design2UMG` directory into one of the locations above. Source archives from GitHub are not precompiled plugins.
- Close the editor before replacing an installation. Enable `Design2UMG` in `Edit > Plugins` and restart.
- Import `Examples/SimpleCard.pen` first. Font binaries are not bundled: map your licensed project fonts in the plugin preferences and read missing-font diagnostics rather than assuming pixel parity.

Release ZIPs include the license, installation instructions, Figma exporter and public examples. CI candidate downloads expire; stable downloads belong on GitHub Releases. Contributor validation scripts require the source checkout; the precompiled bundle is not a complete development workspace.

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

Prefer **Design2UMG > Import from Figma...**, which provides a password-style token field. Do not paste tokens into console history, screenshots or shared transcripts.

The console entry below is for explicit local scripting only; it takes a token value, not an environment-variable name:

```text
Design2UMG.FigmaImport <file_url_or_key> <FigmaAccessToken>
```

A secure source does not prevent leakage after a value is expanded into a command. Prefer the dialog for interactive imports; keep scripting inputs and output private.

### 3. Import local Figma export JSON

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

### 4. Import from MCP JSON files

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

This MCP JSON path is a bridge/debug utility. The current release contract does not treat live sync itself as a formal support promise.

### 5. Read the active Pencil document

Open a `.pen` file in the pen.dev design editor in VS Code, then choose **Design2UMG > Connect to Pencil...** in Unreal Editor. The connection uses the installed Pencil 0.6.77 MCP server over stdio and reads frames and variables through its `execute` tool without changing the design. Select a frame, review the preview, and generate the Widget Blueprint.

After updating Pencil, close and reopen an existing design tab so the new extension registers the document. Use the latest installed extension; the plugin discovers its server automatically, with the server path and app name settings available as overrides.

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
- Remote Image Import: enabled by default; saved user preferences still apply. Insecure or local image URLs remain disabled by default.
- Generated Texture Budget (MiB): cumulative newly generated raster data per generation or preview; defaults to 512, minimum 1. Changes apply to the next operation. This does not change remote-download, decoded-input or single-image limits, and is not a cap on total editor memory.
- Icon Font mapping
- Widget Class override rules
- MCP Server Path override
- Pencil App Name override
- Known web font auto import is experimental, defaults off, and should not be treated as a required production workflow

## Import Semantics

- Normal import keeps the existing collision behavior: when the target name already exists, a new auto-renamed asset is generated and the old asset is left untouched.
- Safe Reimport (in-place update) is available for assets previously generated by this plugin:
  - Entry points: editor menu `Design2UMG > Reimport > Reimport Design...`, the `DesignImportPipeline::Reimport` API, or the `Design2UMG.Reimport <TargetWBPAssetPath> <design.json> [--resolution-copy]` console command.
  - Each generated asset carries a `<AssetName>.d2u.json` sidecar manifest recording design identities (`designNodeId` + `widgetName` per instance). Reimport matches provider + document scope + recorded instances against the new source before applying any change.
  - D2U-owned nodes (layout, slot, visual, text properties) are refreshed in place; project-side content — widgets you added or renamed, event bindings, blueprint graph logic — is preserved by design.
  - Removals, mapped/generated identity transitions, and mapped class changes use conservative conflict semantics: the asset is kept, the reason is reported as structured diagnostics, and an explicit `_Resolution` copy can be generated on demand.
  - If the relationship between the asset and the new source is unclear (scope mismatch, recorded instances no longer locatable), reimport refuses to patch and reports `reimport_full_conflict`.

## Component Mapping

- Project-level component mapping maps design components (Figma `INSTANCE` nodes) to your own Widget Blueprint classes, replacing the generated subtree with a real instance of your class.
- Configuration lives in `<Project>/Config/Design2UMGComponentMapping.ini`, section `[/Design2UMG/ComponentMapping]`, array key `+Mappings`:
  ```ini
  +Mappings=(Key="figma|<fileKey-or-scope>|<mainComponentKey>",WidgetClass="/Game/UI/WBP_PrimaryButton.WBP_PrimaryButton_C",Inputs=((Source="label",Target="Label",Type="Text",ValueKind="ComponentProperty")))
  ```
  - `Key` is `provider|documentScope|componentKey` (matched case-insensitively; `documentScope` resolves to the Figma file key when present).
  - `WidgetClass` must be a full object path ending with the `_C` generated class.
  - `Inputs` copy declared design values into instance properties on every import/reimport. `Type` is one of `Text/Bool/Name/Float`; `ValueKind` is `ComponentProperty` (value from the instance's `componentProperties`, the default) or `ComponentText` (value from the component's text content). Invalid entries are dropped with a `mapping_` diagnostic instead of blocking the import.
  - Mapping takes precedence over name-based Widget Class overrides; unmatched components keep the normal conversion path.
  - Prototype interactions attached to a mapped component are project-owned by definition: the plugin reports them as inert (`mapping_prototype_inert`) and does not generate runtime bindings for them.
  - Pencil sources stay out of the mapping main path; they provide candidate identity only.

## Verification Boundary

- Formal full-fidelity release validation is performed against a maintainer-owned canonical runtime boundary.
- The public repository must not treat that maintainer boundary as a portable user setup recipe.
- HostProject fallback is not part of the supported verification workflow.

## Repository And Release Checks

- `repository-hygiene`: the public GitHub Actions check that verifies required repo files and blocks tracked maintainer-local paths.
- `maintainer build and automation`: the packaging and Unreal automation job in `.github/workflows/build.yml`; it triggers on maintainer pushes, same-repository PRs, and manual dispatches but runs only on a self-hosted Windows runner (registered with `UE_ROOT`, `DESIGN2UMG_RUNTIME_PROJECT` and `DESIGN2UMG_EDITOR_TARGET`). When no runner is registered the job stays queued; hosted CI then covers repository hygiene only, and packaging and automation are maintainer-local verifications. It validates the package boundary, runs focused generation, Figma parser, and lifecycle tests against the unique installed plugin after descriptor/Source/Config hash comparison with the checkout and an editor-target rebuild, exercises the packaged plugin itself through the UAT `BuildPlugin -StrictIncludes` compile plus the package boundary assertion, and uploads the artifact.
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
