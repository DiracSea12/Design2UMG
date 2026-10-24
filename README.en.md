<div align="center">

# Design2UMG

**Turn Pencil and Figma designs into Unreal Engine 5 UMG Widget Blueprints.**

[![Plugin CI](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml/badge.svg)](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.0--5.8-313131?logo=unrealengine)
![Platform](https://img.shields.io/badge/platform-Win64-0078D6)

[简体中文](README.md) · **English**

</div>

## Overview

Design2UMG is an **editor-only plugin**. It reads structured data exported by design tools and rebuilds the layout, styles, images, text and component references as an editable UMG widget tree, saved as an ordinary Widget Blueprint asset. Generated assets do not depend on the plugin at runtime and packaged games do not need it.

| Design source | Entry point | Support level |
| --- | --- | --- |
| Pencil `.pen` files | Drag into Content Browser, or `Design2UMG.Import` | ✅ Supported |
| Figma files (REST API) | Menu **Design2UMG > Import from Figma...** | ✅ Supported |
| Figma local export JSON | `Design2UMG.LocalFigmaImport`; exporter in [Tools/FigmaExporter](Tools/FigmaExporter/README.md) | ✅ Supported |
| Active Pencil document (live MCP read) | Menu **Design2UMG > Connect to Pencil...** | 🧪 Bridge/debug, not a release promise |
| MCP JSON files | `Design2UMG.PenConvert` | 🧪 Bridge/debug, not a release promise |

> **Pencil stroke limitation:** Conversion of the historical stroke format used by Pencil 2.6–2.11 is currently unsupported. Strokes in older documents may be missing or display incorrectly; this part is not fully supported or validated. Full Pencil support remains the long-term goal.

## Features

- **Layout conversion**: projects design-side flex / auto-layout onto UMG slots, anchors and offsets, with node bounds as the single source of truth.
- **Visual mapping**: fill, stroke, corner radius, shadows, blur approximation, linear/radial gradients, SVG path rasterization.
- **Source-sized exports**: keep the design layout size and calculate PNG bounds separately for shadows and unclipped content. One-pixel axes and tiled exports within the independent 256 × 1024 × 1024 pixel screenshot export budget are supported; reference dimensions never resize generation.
- **Variables and components**: resolves design variables and themes, expands refs and component instances.
- **Preview before generating**: inspect a preview image and editable intermediate JSON before import.
- **Safe Reimport**: refreshes plugin-owned parts in place after a design update, keeping the widgets, bindings and Blueprint logic you added (Figma sources for now).
- **Component blueprints**: Figma components can become standalone child Widget Blueprints that parent pages instance.
- **Component mapping**: replaces Figma component instances with Widget Blueprint classes from your project.
- **Prototype interactions**: supported single-step click and keyboard navigation from Figma prototypes becomes buttons, page switching and native Blueprint wiring.
- **Structured diagnostics**: every import writes a JSON report listing degradations, missing resources and recommendations.

## Generated widgets

The converter picks widgets from the design content:

| Category | Widget | Generated when |
| --- | --- | --- |
| Layout containers | `CanvasPanel` | Nodes without auto-layout, placed by absolute position |
| | `HorizontalBox` / `VerticalBox` | Horizontal / vertical auto-layout |
| | `WrapBox` | Wrap layout |
| | `UniformGridPanel` / `GridPanel` | Grid layout; `GridPanel` when children declare row or column spans |
| | `ScrollBox` | Figma containers with horizontal or vertical scrolling overflow (Pencil has no equivalent field) |
| | `Overlay` | Layered fills, stroke overlays, effect wrappers, prototype overlays |
| Size and spacing | `SizeBox` | Nodes that need fixed width/height constraints |
| | `ScaleBox` | Images filled in Fit mode |
| | `Spacer` | Flexible gaps in space-between layouts |
| Appearance | `Border` | Background fill, stroke, corner radius |
| | `Image` | Images, rasterized vector paths, shapes |
| Effects | `BackgroundBlur` | Background blur |
| | `RetainerBox` | Layer blur, inner shadow and other material-based effects |
| Text | `TextBlock` | Plain text |
| | `RichTextBlock` | Text that mixes several styles |
| Prototype | `Button` | Nodes with interaction triggers |
| | `WidgetSwitcher` | Multi-page prototype flows |
| Components | `UserWidget` subclass + `NamedSlot` | Component blueprints and replaceable per-instance content |

Beyond these, Widget Class override rules, component mapping or edited intermediate JSON can use other UMG widgets, such as `CheckBox`, `Slider`, `SpinBox`, `ProgressBar`, `EditableTextBox`, `ComboBoxString`, `ListView`, `TileView`, `ExpandableArea`, `SafeZone`, `InvalidationBox` and `MenuAnchor`, as well as any `UWidget` subclass in your project.

> These widgets are created, but the design's appearance is not yet written into their widget-specific styles (check marks, slider tracks, progress fills); set those up in UE. Pointing component mapping at an already-styled WBP is currently the easiest route.

## Five-minute start

1. Download the precompiled ZIP matching your UE version from [Releases](https://github.com/DiracSea12/Design2UMG/releases) and put its `Design2UMG` folder in `<Project>/Plugins/`. If no matching precompiled package exists yet, install from source as described below.
2. Start the editor, enable **Design2UMG** in `Edit > Plugins`, and restart.
3. Drag `Examples/SimpleCard.pen` from the plugin folder into Content Browser.
4. Open the generated Widget Blueprint and compare it with the preview PNG.

To try a Figma source, run this in the console (use an absolute path; relative paths resolve against the editor process's working directory, not the project):

```text
Design2UMG.LocalFigmaImport <Project>/Plugins/Design2UMG/Examples/AiHud/design.json
```

It is a "Chinese cyberpunk HUD" export sample. `Examples/` also contains `Dashboard.pen`, `GameHUD.pen` and `LoginForm.pen`.

## Installation

### Compatibility

| Item | Notes |
| --- | --- |
| Platform | Windows (Win64). Other operating systems are not yet verified |
| Unreal Engine | 5.0–5.8 adapted. On 2026-10-10 a full regression ran on 5.0.3 / 5.1.1 / 5.2.1 / 5.3.2 / 5.4.4 / 5.5.4 / 5.6.1 / 5.7.1 / 5.8.2, each completing generation and saved-asset reload for all 81 design cases |
| Local validation / GitHub CI environment | UE 5.7.1 / UE 5.8.3 |
| Pencil (optional) | VS Code extension 0.6.77, for live reads |
| Figma (optional) | Personal Access Token, for online import |

> The multi-version regression is a point-in-time result; it does not mean every later commit was retested on every version.

### Install location

Pick one. Never let UE discover two copies:

```text
<Project>/Plugins/Design2UMG/                          # recommended: this project only
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/      # every project on this engine
```

- **Precompiled package**: use the Release ZIP matching your UE version and platform. Blueprint-only projects (no C++ source) should use it.
- **Source**: install the C++ toolchain for your UE version and build the project's Editor target; see [CONTRIBUTING](CONTRIBUTING.md). GitHub's auto-generated "Source code" archives are not precompiled plugins.
- Close the editor before replacing an installation.
- **Fonts are not bundled.** Map licensed project fonts in the plugin settings and follow the diagnostic report for missing fonts rather than expecting pixel parity.
- **Static TTF/OTF fonts can be previewed and imported directly, without installing the font tool.** Web-compressed fonts (WOFF2) and variable fonts (one file containing multiple weights) need the optional Win64 tool to convert them into font files UE can use. It extracts the weight the design needs from a variable font, for example 600.
- The font tool is **not installed by default and never downloads automatically**. Check its option in the preview, then confirm again to download; cancelling does not install it. Without it, static TTF/OTF fonts still work; fonts that need conversion produce a message that they cannot be converted. **移除工具** (Remove Tool) removes the installed tool and keeps font resources already imported into UE.
- The tool converts existing font files; it **does not find or download missing fonts**. The plugin package excludes the tool and its Python / FontTools / Brotli components. Download and disk-space details appear in the installation confirmation; `Resources/PencilFontWorkerDownload.json` describes the download.
- Enter a storage directory in **安装位置** (Installation Location), or select it with **浏览…** (Browse). UE saves the choice in the current project's user config across panel/editor restarts. The default is `<plugin>/Saved/Tools`; the actual install is `<selected directory>/PencilFontWorker/1.0.0`, shown in the panel and confirmation. Remove the current tool before changing location; installation/conversion blocks changes. No old AppData lookup or migration occurs.
- Removal validates the installation record and package file list, deletes only owned files and empty subdirectories, and preserves extra user files. Existing foreign installation directories are never overwritten. Missing/corrupt records, modified files, and reparse nodes inside the managed subtree prevent removal; approved junctions above the storage directory remain supported.
- Edit font-tool UI, management errors and remedy-dialog text in `Resources/FontToolText.json` (flat string values). Reopen the panel or make the next request to reload it. Keep keys and named placeholders: `{Version}`, `{DownloadUrl}`, `{DownloadMiB}`, `{InstalledMiB}`, `{InstallDirectory}`, `{File}`. Nested values and literal-brace escaping are unsupported. Invalid resources/keys display their path/key, with no compiled Chinese fallback.

Release ZIPs contain the license, documentation, Figma exporter and public examples, but not the contributor validation scripts; those require the source repository.

## Usage

### Pencil `.pen`

Drag the file into Content Browser, or:

```text
Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]
```

Bundled `pencil:<library>.lib.pen` imports read component definitions and variables from the installed extension, preserve import aliases, and leave the source design unchanged.

### Figma files

Use **Design2UMG > Import from Figma...** and enter the token in its password field.

Local scripts can use the command below, but the second argument is the **token value itself** and stays in console history:

```text
Design2UMG.FigmaImport <file_url_or_key> <FigmaAccessToken>
```

> ⚠️ Never paste tokens into screenshots, shared logs or chat transcripts. Expanding one from an environment variable still puts it in the command record.

Import targets ordinary design layers; web programs in Figma code layers cannot be imported directly as native UMG. You can try **Extract designs** first, then export or import the resulting ordinary design layers; see [Figma's help](https://help.figma.com/hc/en-us/articles/44193259600535-Code-layers-in-Figma-Design) for the steps.

### Figma local export JSON

After exporting `design.json` with the [Figma exporter](Tools/FigmaExporter/README.md):

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

### Live Pencil document (debug)

1. Open a `.pen` file in the Pencil design editor in VS Code.
2. In Unreal Editor choose **Design2UMG > Connect to Pencil...**.
3. Select a frame, review the preview, and generate the Widget Blueprint.

The plugin launches Pencil's bundled MCP server over stdio and reads frames and variables read-only through `execute`; the design is never modified. The server path is discovered automatically and can be overridden in settings. After updating the Pencil extension, close and reopen existing design tabs.

### MCP JSON (debug)

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

## Updating existing assets: Safe Reimport

A normal import that hits an existing name **creates a new auto-renamed asset** and leaves the old one untouched. To update an asset in place, use Safe Reimport. Its entry points currently accept Figma local export `design.json` only; `.pen` files are not supported yet.

Entry points:

- Menu **Design2UMG > Reimport > Reimport Design...**
- Console `Design2UMG.Reimport <TargetWBPAssetPath> <design.json> [--resolution-copy]`
- C++ API `DesignImportPipeline::Reimport`

How it works:

- Each generated asset has a `<AssetName>.d2u.json` sidecar recording each instance's `designNodeId` and `widgetName`. Before reimporting, the plugin checks that provider, document scope and these records match the new design.
- Plugin-generated nodes (layout, slot, visual and text properties) are refreshed in place; widgets you added or renamed, event bindings and Blueprint graph logic are preserved.
- Node removals, identity transitions and mapped class changes keep the asset and emit structured diagnostics; add `--resolution-copy` to also save a `_Resolution` copy.
- If the asset-to-design relationship is unclear (scope mismatch, recorded instances not found), the plugin refuses to patch and reports `reimport_full_conflict`.

## Component mapping

Replace Figma component instances (`INSTANCE` nodes) with Widget Blueprint classes from your project. Configure it in `<Project>/Config/Design2UMGComponentMapping.ini`:

```ini
[/Design2UMG/ComponentMapping]
+Mappings=(Key="figma|<fileKey-or-scope>|<mainComponentKey>",WidgetClass="/Game/UI/WBP_PrimaryButton.WBP_PrimaryButton_C",Inputs=((Source="label",Target="Label",Type="Text",ValueKind="ComponentProperty")))
```

| Field | Notes |
| --- | --- |
| `Key` | `provider\|documentScope\|componentKey`, case-insensitive; `documentScope` resolves to the Figma file key when present |
| `WidgetClass` | Full object path ending with the generated `_C` class |
| `Inputs` | Written to instance properties on every import/reimport. `Type` is `Text` / `Bool` / `Name` / `Float`; `ValueKind` is `ComponentProperty` (default, from the instance's `componentProperties`) or `ComponentText` (from the component's text content) |

- Invalid entries are dropped with a `mapping_`-prefixed diagnostic instead of blocking the import.
- Component mapping takes precedence over name-based Widget Class overrides; unmatched components convert normally.
- Prototype interactions on a mapped component are project-owned; the plugin reports `mapping_prototype_inert` and generates no runtime bindings.
- Pencil sources provide candidate identity only and stay out of the mapping main path.

## Component blueprints (Figma)

When a Figma export keeps component dependency data, the plugin generates one standalone Widget Blueprint per component and variant in dependency order (children first), and parent pages reference instances of them:

- A component referenced in several places produces several instances of one child blueprint. Asset identity comes from document scope and component key, not display names.
- Per-instance text, visibility and component swaps are stored in that instance's `NamedSlot` content.
- Reimport starts from the parent page and updates child blueprints with their dependencies; event graphs, variables and hand-added widgets you saved on the parent are preserved.
- Components already mapped to an existing class use the mapping instead of being generated.

Full behavior and failure recovery: [component blueprint generation](Docs/ComponentBlueprintGeneration.zh-CN.md) (in Chinese).

## Prototype interactions (Figma)

Figma prototype connections are converted as follows:

- Trigger nodes are wrapped in a `Button`; multi-page flows get a synthesized `WidgetSwitcher` root.
- Supported single-step mouse actions (navigate, open link, overlay, back) and keyboard actions (navigate, open link) are wired as native Blueprint graph nodes.
- Supported page transitions generate `UWidgetAnimation`s.

Multi-step or conditional flows, drag, and unsupported trigger/action combinations stay unwired and are listed in the diagnostic report. This is not a general interaction-graph compiler; see [Known limitations](KNOWN_LIMITATIONS.md#interaction-and-animation).

## Settings

Location: `Edit > Editor Preferences > Plugins > Design2UMG`

| Setting | Notes |
| --- | --- |
| Design Width / Height | Design canvas size |
| Remote Image Import | On by default; saved user preferences still apply. Insecure or local image URLs are disabled by default |
| Generated Texture Budget (MiB) | Cap on newly generated raster data per generation or preview. Default 512, minimum 1, applies to the next operation. Cache reuse is not charged; regeneration is. Does not change download, decode or single-image limits, and is not a total editor memory cap |
| Icon Font mapping | Maps icon fonts to glyphs |
| Widget Class overrides | Replaces the default widget class when the node name contains the configured text (case-insensitive `Name Contains`); the first matching rule wins |
| MCP Server Path / Pencil App Name | Override the auto-discovered Pencil MCP server |
| Known web font auto import | Experimental, off by default; not recommended for production workflows |

## Import diagnostic report

Every import and automation run writes:

```text
<Project>/Saved/Design2UMG/ImportReports/*.import_report.json
```

This report is the authority for import troubleshooting. It covers source/output binding, degradations, resource diagnostics, effect wrappers and recommendations; see [Docs/ImportDiagnosticContract.md](Docs/ImportDiagnosticContract.md) for the fields.

## Quality and validation

| Layer | What it does | Where it runs |
| --- | --- | --- |
| `repository-hygiene` | Public repo boundary, text hygiene, Python / PowerShell / Node tool tests | GitHub-hosted runner on every push / PR |
| `maintainer build and automation` | Strict plugin compile, UAT packaging, focused UE automation, candidate upload | Maintainer self-hosted Windows runner |
| `public smoke / public validate` | Local build and focused validation for reproducing issues | Contributors and maintainers, locally |
| `maintainer formal admissible gate` | No-regression release gate over every discovered case | Maintainers only |

- The CI badge reflects the whole `Plugin CI` workflow. Fork PRs run hosted checks only; maintainer pushes, same-repository PRs and manual runs also request the self-hosted UE job, which stays queued when no runner is available.
- The badge is **not** evidence of full visual regression or packaged-game runtime. No hosted UE version matrix or coverage badge is published.
- Validation tools scan the case directory you provide and run whatever they discover. The maintainer's private visual corpus is not distributed or required; an empty directory means zero tests, not a pass.

See the [test coverage boundary](Docs/ValidationGovernance/TestCoverageBoundary.md) for each layer's scope, [Tools/README.md](Tools/README.md) for tool usage, and the [validation protocol](Docs/ValidationProtocol.md) for the formal gate.

<details>
<summary><b>Automated visual verification (maintainers)</b></summary>

Visual verification has two layers:

1. `Design2UMG.AutomationRender <spec.json>`: renders a compiled Widget Blueprint to PNG from a manifest.
2. `Tools/design2umg_auto_verify.py`: prepares a design reference image, launches Unreal Editor unattended to render the candidate, and writes a diff image and JSON report.

```text
python Tools/design2umg_auto_verify.py pen --source <DesignRoot>\screen.pen --node <NodeId> --reference-provider png --reference <DesignRoot>\screen.png --project <YourProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

```text
python Tools/design2umg_auto_verify.py local-figma --source <FigmaExportRoot>\design.json --reference-provider figma --figma-file <fileKeyOrUrl> --figma-node 123:456 --figma-token <FigmaAccessToken> --project <YourProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

- `DESIGN2UMG_RUNTIME_PROJECT` can replace `--project`; `DESIGN2UMG_UNREAL_EDITOR` or `UE_ROOT` can replace `--editor`. Without either, the script fails; it never creates or infers a HostProject.
- Prepare font seeds first in a discoverable `Content/Design2UMG/Fonts` directory next to the design source or in the runtime project. The verifier copies them; it does not download fonts or repair broken font assets.
- A valid seed is a UE `UFont` with at least one default typeface entry backed by a valid `FontFace` or equivalent font data. A lone `.ttf`, an empty `UFont` or an entry without font data counts as missing.
- Formal full-fidelity validation runs in a maintainer-owned environment; these paths are not a user installation recipe.

</details>

<details>
<summary><b>CI and release package details (maintainers)</b></summary>

- The self-hosted runner must define `UE_ROOT`, `DESIGN2UMG_RUNTIME_PROJECT` and `DESIGN2UMG_EDITOR_TARGET`.
- The isolated CI project's Editor target must come from `Tools/CI/Design2UMGVerificationEditor.Target.cs` (rename the file and class to match `DESIGN2UMG_EDITOR_TARGET` if needed). Only the two plugin modules disable unity builds; the engine keeps its settings.
- CI first compares descriptor, Source and Config hashes between the installed plugin and the checkout, rebuilds only the plugin modules strictly with `-UsePrecompiled -NoEngineChanges -NoPCH -NoSharedPCH`, runs focused automation, then packages with the normal UAT `BuildPlugin` and checks the package boundary.
- The output is `Design2UMG-<version>-UE<engine>-Win64.zip` with `candidate.json` and `SHA256SUMS.txt`. Actions artifacts expire after 14 days; GitHub Releases is the stable download.
- Packages and public exports must exclude root review reports, `openspec/`, AGENTS / CLAUDE local rules, raw automation output, caches, HostProject fallback artifacts and maintainer absolute paths.

Release steps: [release candidate checklist](Tools/Release/ReleaseCandidateChecklist.md) and [public export workflow](Tools/Release/PublicRepoExportWorkflow.md).

</details>

## Architecture

```text
.pen / Figma / MCP
  → Parser / Bridge
  → Design2UMGConverter
  → LayoutCalculator
  → UMGGenerator
```

Key principle: **bounds are the source of truth** for layout projection. Layout dimensions remain separate from the PNG export bounds, which also contain source-defined outer shadows and unclipped content. Reference PNG dimensions never set either size. One-pixel dimensions are valid; exports above the GPU texture dimension are rendered in tiles, within the independent 256 × 1024 × 1024 pixel screenshot export budget. See [Architecture](Docs/Architecture.md) for module boundaries and rules, and [PropertyMapping](Docs/PropertyMapping.md) for property mapping.

## FAQ

<details>
<summary><b>Does a packaged game need this plugin?</b></summary>

No. The plugin has editor modules only and generates ordinary Widget Blueprints with no runtime dependency on it.

</details>

<details>
<summary><b>Fonts or text widths do not match the design</b></summary>

Fonts are not bundled. Map the design's fonts to licensed UE font assets in your project in the plugin settings, then check the missing-font diagnostics in the import report. Even with correct mapping, UE and design-tool font metrics differ, so text sizing is approximate.

</details>

<details>
<summary><b>The editor errors or asks to rebuild modules after enabling the plugin</b></summary>

- Make sure only one copy exists; never keep both the project `Plugins/` and engine `Marketplace/` copies.
- A precompiled package must match your UE version and platform.
- Blueprint-only projects have no C++ toolchain and need a matching precompiled package.

</details>

<details>
<summary><b>Importing the same design again created a renamed asset</b></summary>

That is expected: a normal import never overwrites existing assets. To update an asset in place, use [Safe Reimport](#updating-existing-assets-safe-reimport).

</details>

<details>
<summary><b>Safe Reimport refuses with <code>reimport_full_conflict</code></b></summary>

The plugin cannot confirm how the asset relates to the new design, for example because the source file changed or recorded instances are gone. To protect your edits, it leaves the asset alone. Check that the source is correct; if it really is a different design, use a normal import.

</details>

<details>
<summary><b>Remote images were not imported</b></summary>

Check that Remote Image Import is enabled. `http://` and local-address images are blocked by default. The import report's resource diagnostics state the exact reason.

If a whole preview or generation is rejected for exceeding a budget, its newly generated images went over the Generated Texture Budget; raise it with your machine's memory in mind.

</details>

<details>
<summary><b>Connect to Pencil cannot find the current design</b></summary>

Open the `.pen` file in the Pencil design editor in VS Code first. After updating the Pencil extension, close and reopen the design tab. If auto-discovery fails, set MCP Server Path in the plugin settings.

</details>

## Getting help

- Usage questions and bugs: open an [issue](https://github.com/DiracSea12/Design2UMG/issues) with your UE version, plugin version and design source. Check import reports for private paths or design content before attaching them.
- Security issues: report privately as described in the [security policy](SECURITY.md), not in a public issue.

## More documentation

- [Known limitations](KNOWN_LIMITATIONS.md)
- [Roadmap](Docs/ROADMAP.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## License

[MIT](LICENSE)
