# Design2UMG Roadmap

Chinese companion: [ROADMAP.zh-CN.md](ROADMAP.zh-CN.md)

**Created**: 2026-02-15
**Current public baseline**: 2026-05-14
**Status legend**: `[ ]` not started | `[~]` in progress | `[x]` complete

Private planning records are not public release promises. This roadmap describes the public direction and the current release boundary only.

## Strategy

Design2UMG is intended to make design-to-UMG workflows easier for Unreal Engine projects. The repository is being prepared as an open-source plugin first, with possible commercial support, consulting, or advanced add-ons later.

## Current Release Boundary

The current public release scope covers:

- `.pen` import into UMG Widget Blueprints.
- Figma import through REST API.
- Local Figma export JSON import.
- Layout conversion from design-space layout into UMG slots, anchors, and offsets.
- Visual mapping for fills, strokes, radius, shadows, blur approximations, gradients, SVG paths, variables, themes, and component references.
- Content Browser drag-and-drop import and command-line import.
- Public smoke and validation guidance.

The following remain bridge, debug, or future work unless a later release explicitly promotes them:

- MCP JSON bridge import and live Pencil MCP debug chains.
- Full public hosted UE formal validation.
- Full UEdGraph authoring for prototype logic.
- Complete Smart Animate, overlay layer semantics, generalized animation authoring, and design-system sync.

## Validation And Release Gates

The project uses four different validation tiers:

| Tier | Meaning | Public promise |
| --- | --- | --- |
| `repository-hygiene` | Required-file and public-boundary checks in GitHub Actions | Public, lightweight |
| `self-hosted build and automation` | BuildPlugin packaging and Unreal automation on a maintainer Windows UE runner | Maintainer pushes, same-repository PRs, and manual runs |
| `public smoke/public validate` | Contributor-reproducible local build and focused validation | Public repro boundary |
| `maintainer formal admissible gate` | Full discovered admissible case set no-regression release gate | Maintainer release gate |

The repository does not currently publish a public hosted UE matrix, public hosted full formal gate, or coverage badge.

## Competitive Position

| Capability | Current Design2UMG state |
| --- | --- |
| Layout conversion | Ahead of basic references: flex-like layout, anchors, offsets, and fixer rules |
| Visual properties | Fill, stroke, radius, shadow, blur approximation, gradients, and SVG paths |
| Component system | Ref expansion, variables, and themes |
| Prototype flow | Typed navigation data and native Blueprint wiring for supported single-step actions; broader semantics remain future work |
| Multiple input sources | `.pen`, Figma REST, local Figma export JSON, plus bridge/debug utilities |
| Blueprint logic generation | Native Blueprint graph wiring for supported mouse/keyboard actions; not a general-purpose interaction graph compiler |

## v1.0 Public Contents

- Core conversion pipeline from `.pen`, Figma, and local Figma export JSON to UMG Widget Blueprint.
- Layout engine: flex-like layout to anchors/offsets, plus fixer rules.
- Current visual-property path: fill, stroke, radius, shadow, blur approximation, multi-stop gradients with endpoints/rotation, SVG path rasterization, and cached path textures.
- Component reference expansion plus variable and theme parsing.
- Native Blueprint wiring for supported single-step mouse/keyboard actions; detailed limits are in [Known Limitations](../KNOWN_LIMITATIONS.md#interaction-and-animation).
- Incremental reimport of recorded instances, with explicit conflict and unsupported-update diagnostics; see [Known Limitations](../KNOWN_LIMITATIONS.md).
- Content Browser drag-and-drop import and command-line import.
- Public smoke / validation documentation.
- Bridge and debug utilities remain available but are not the current support promise.
- Maintainer-owned formal admissible gate remains the release gate.

## v1.1 Candidates

- Prototype flow expansion beyond the supported single-step native graph bindings: drag, conditional/multi-step flows, additional keyboard actions, and complete overlay layer semantics.

## Later Community-Driven Areas

- Advanced interaction semantics.
- Responsive adaptation and safe-zone support.
- Design token and design-system sync.
- Broader reimport coverage beyond the current recorded-instance update boundary.

## Public API Boundary

The intended public API is the retained `Design2UMGCore/Public` and `Design2UMGEditor/Public` header surface documented in [Architecture](Architecture.md). Headers are retained when they are parser, converter, schema, settings, import, preview, image, or test-facing contracts needed by downstream modules or contributors.

Any future header narrowing must be supported by current include evidence and validation.

## Open-Source Readiness Milestones

| Milestone | Completion standard |
| --- | --- |
| M0: Open-source readiness | Repository hygiene passes, self-hosted BuildPlugin remains available, license and contributor docs are current, public package/export boundaries exclude maintainer-only files |
| M1: v1.0 release | M0 plus current v1.0 scope is clearly documented and does not overclaim deferred material, graph-authoring, or formal-gate work |
| M2: v1.1 expansion | Prototype expansion is implemented and validated without changing the v1.0 support boundary retroactively |
| M3: Community health | External contributors, active issue triage, and roadmap updates driven by real use |
