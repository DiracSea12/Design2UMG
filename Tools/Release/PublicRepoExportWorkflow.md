# Design2UMG Public Repo Export Workflow

Use this workflow when freezing a validated plugin state into a standalone GitHub repository.

## Goal

- publish the exact plugin state that already passed the maintainer full admissible gate
- exclude machine-local paths, raw automation trees, and transient investigation debris
- keep public validation, documentation, and contribution entrypoints consistent

## Inputs

- candidate commit SHA in the maintainer workspace
- current public workflow badge expectation: only `repository-hygiene` from `.github/workflows/build.yml` is badge-backed today
- successful self-hosted plugin build (`RunUAT BuildPlugin -StrictIncludes`) on a maintainer-provided UE runner
- successful `Automation RunTests Design2UMG`
- successful full admissible summary:
  `Saved/Design2UMG/Automation/_final_admissible_full_<date>/summary.json`
- current curated governance records under `Docs/ValidationGovernance/`
- GitHub Release draft or tag plan for the same candidate SHA

## Export Rule

The public repository is an export of the plugin product, not a mirror of the full engine workspace.

Required public set:

- `Source/`
- `Config/`
- `Docs/` except intentionally excluded archive/history folders
- `Tools/Release/`
- `README.md`
- `KNOWN_LIMITATIONS.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `LICENSE`
- `CHANGELOG.md`
- `Design2UMG.uplugin`
- canonical governance manifests in `Docs/ValidationGovernance/`

Excluded from public export unless they are first rewritten into generic reusable tooling:

- `Saved/`
- `Intermediate/`
- `Binaries/`
- cache folders such as `.cache`, `.pytest_cache`, `DerivedDataCache/`, and `DDC/`
- `.vs/`
- `.claude/`, `.codex/`, `.cowork-temp/`, `AGENTS.md`, and `CLAUDE.md`
- `openspec/`, including `openspec/**/verification/`
- root review reports such as `*REVIEW*.md`, `*Review*.md`, and `code_review_report*.md`
- engine-workspace only helper files
- raw automation output trees under `Saved/Design2UMG/Automation/`
- local probe scripts and one-off diagnostics under `Saved/`
- generated HostProject fallback artifacts, generated fallback host directories, and concrete fallback project paths
- archive/history notes that are not part of the final public support story
- maintainer-local planning / history working sets that are not part of the shipped support story
- any script or doc whose default values expose maintainer-only absolute paths

## Boundary Rules

- Public docs must describe a `maintainer-owned canonical runtime boundary`, not a maintainer machine path.
- Public badge/release wording must match today's workflow truth: `repository-hygiene` is the public badge, `build-plugin` is a manual self-hosted artifact job, and the maintainer formal gate is separate release evidence.
- Public `validate` commands must be labeled as smoke/public validation unless they truly reproduce the maintainer full admissible gate.
- The maintainer full admissible gate may stay private as long as the public repo makes that boundary explicit.
- Public docs may say that HostProject fallback is unsupported; generated fallback projects and concrete fallback paths must not ship.
- Source workspace checks are intentionally separate from package/export workspace checks. Local ignored planning files may remain in the maintainer source workspace, but package and export workspaces must not contain them.
- This repo currently relies on GitHub generated release notes from `.github/release.yml`; phase22.b does not add a separate checked-in release-notes template.

## Freeze Procedure

1. Record the candidate commit SHA and the successful validation artifact paths.
2. Create the release package with `RunUAT BuildPlugin -StrictIncludes` from that candidate, using a maintainer-provided UE runner and an explicit plugin path.
3. Run `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -RepoRoot <PackageRoot> -Mode Package` before uploading the package.
4. Copy the plugin subtree into a clean export workspace.
5. Apply the public include/exclude rules above, removing maintainer-only files before publication.
6. Re-check text encoding, secrets, and path leakage in the export workspace.
7. Verify the exported tree still contains the same plugin version and release notes as the validated source tree.
8. Run `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -Mode ExportWorkspace` inside the export workspace.
9. Review GitHub generated release notes from `.github/release.yml`, edit them for user-facing clarity, and keep gate claims aligned with actual evidence.
10. Upload the BuildPlugin package as the release artifact, not the raw maintainer workspace.

## Publication Checks

Before publishing the exported repository:

- `RunUAT BuildPlugin -StrictIncludes` succeeds in the export workspace
- public smoke validation commands use explicit `--project` / `--editor` values or documented environment variables
- public docs are internally consistent
- `Docs/ValidationGovernance/` points to the latest curated manifests
- no raw automation tree is present
- no root review report, `openspec/`, AGENTS/CLAUDE local rule file, cache folder, generated HostProject fallback artifact, generated fallback host directory, or concrete fallback project path is present
- no maintainer absolute path is present in public-facing docs
- no public doc, release note, screenshot, or transcript contains a real Figma token; placeholders such as `<FigmaAccessToken>` are used instead
- `Assert-PublicRepoBoundary.ps1` passes
- any badge or release blurb only describes the current public workflow truth; it must not imply a hosted BuildPlugin gate or hosted formal admissible gate
- release notes identify the evidence level for each gate: public smoke, self-hosted BuildPlugin, focused automation, and maintainer formal admissible validation
- release assets contain the packaged plugin artifact and do not contain maintainer-only planning, local caches, or raw formal-run output

## Final Note

The public repository should make it easy for users to build, smoke-test, and contribute to the plugin.
It should not expose internal workspace clutter or pretend that public smoke validation is identical to the maintainer full admissible release gate.
