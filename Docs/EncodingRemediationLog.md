# Encoding Remediation Log (Release Readiness)

## Scope

Requirement source: `openspec/changes/harden-github-release-readiness/specs/text-encoding-integrity/spec.md`

Target rule:
- Text files (`.h/.cpp/.cs/.py/.md/.json/.yml/.yaml`) must be UTF-8 with BOM
- No mojibake in critical paths, logs, rules, and CI files

## Repair Priority

1. CI and release rules (`.github/workflows`, `.gitignore`)
2. Core parser/schema files (`PenParser.cpp`, `UMGSchema.h`)
3. Public docs and templates (`README`, issue/pr templates, CONTRIBUTING)
4. Remaining tracked docs and JSON fixtures

## Fixed in This Change

- Repaired mojibake text in release-critical files:
  - `.gitignore`
  - `.github/workflows/build.yml`
  - `Source/Design2UMGEditor/Public/UMGSchema.h`
  - `Source/Design2UMGEditor/Private/PenParser.cpp`
- Converted remaining non-BOM tracked text files discovered by audit to UTF-8 BOM:
  - `.github/pull_request_template.md`
  - `Docs/Design2UMG_TechnicalDesign.md`
  - `Docs/archive/legacy-plans/DevelopmentProgress.md`
  - `Docs/archive/legacy-plans/FullPipelineReview.md`
  - `Docs/archive/legacy-plans/PRD_EasyImport.md`
  - `Docs/archive/legacy-plans/PRD_PenMCPBridge.md`
  - `Docs/PropertyMapping.md`
  - `Docs/PropertyTyping.md`
  - `Docs/ROADMAP.md`
  - `Docs/SDD_DegradationFixes.md`
  - `KNOWN_LIMITATIONS.md`
  - `Source/Design2UMGEditor/Private/Tests/Fixtures/HorizontalParentRefChildCrossFill.json`
  - `Source/Design2UMGEditor/Private/Tests/Fixtures/ParserNumberStringFlexible.json`
  - `Source/Design2UMGEditor/Private/Tests/Fixtures/RefOfRefDescendantsOverride.json`

## Verification

- Local audit result:
  - Controlled tracked files: BOM check passed
  - UTF-8 decode check passed
  - Mojibake marker check passed
