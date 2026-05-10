# Design2UMG Release Candidate Checklist

Use this checklist before cutting a GitHub release branch or tag.

## 1. Candidate Snapshot

- [ ] Candidate commit SHA recorded
- [ ] Scope summary prepared (feature/fix/docs/chore)
- [ ] Included file list reviewed
- [ ] Excluded local-only files confirmed
- [ ] Export plan reviewed against `Tools/Release/PublicRepoExportWorkflow.md`
- [ ] GitHub Release draft targets this exact candidate commit / tag

## 2. Workspace Hygiene

- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Release/Assert-CleanWorkspace.ps1` passed
- [ ] No staged/unstaged/untracked mix in release candidate workspace
- [ ] No local cache/screenshot/debug script files in tracked set
- [ ] Source workspace public boundary check passed: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -Mode SourceWorkspace`

## 3. Encoding and Text Integrity

- [ ] UTF-8 BOM encoding guard passed for tracked `.h/.cpp/.cs/.py/.md/.json/.yml/.yaml`
- [ ] Mojibake markers check passed (garbled-path marker and replacement-character marker not found)
- [ ] Critical files manually spot-checked for readable logs/comments

## 4. Security and Secrets

- [ ] Figma token flow docs prioritize env var / secure input
- [ ] CLI plaintext token path is marked as compatibility only
- [ ] Runtime logs do not print token values
- [ ] Known web font auto import is documented as experimental / optional, not as the primary offline-safe workflow

## 5. Quality Gates

- [ ] Public smoke validation command is documented separately from the release gate
- [ ] Public smoke checks are described as contribution confidence checks, not as the formal admissible gate
- [ ] Self-hosted BuildPlugin passed (`RunUAT BuildPlugin -StrictIncludes`) on a maintainer-provided UE runner
- [ ] Focused automation tests passed (`Automation RunTests Design2UMG`)
- [ ] Maintainer formal full admissible validation passed and wrapped in the current `Docs/ValidationGovernance/AdmissibleBaselineManifest_<date>.json`
- [ ] Any coverage visibility is limited to the actually collected public/self-hosted scope; no percentage badge is published without a real collection job
- [ ] BuildPlugin package artifact is uploaded and downloadable
- [ ] BuildPlugin package boundary check passed before upload: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -RepoRoot <PackageRoot> -Mode Package`
- [ ] Artifact publication evidence records the manual self-hosted workflow run URL or equivalent downloadable artifact proof

## 6. Public Consistency

- [ ] Public name uses `Design2UMG` (README, issue/pr templates, plugin metadata)
- [ ] `Design2UMG.uplugin` `VersionName` matches `CHANGELOG.md`
- [ ] Version follows semantic versioning (`MAJOR.MINOR.PATCH`)
- [ ] Public docs describe the maintainer validation boundary without exposing maintainer absolute paths
- [ ] Public docs do not claim a hosted full UE formal gate, public CI matrix, or coverage badge

## 7. GitHub Release Notes and Assets

- [ ] GitHub generated release notes use `.github/release.yml` categories
- [ ] Generated notes were reviewed and edited for plain-language user impact
- [ ] Release notes clearly separate public smoke evidence, self-hosted BuildPlugin evidence, and maintainer formal admissible evidence
- [ ] Release notes do not claim a hosted full UE gate or a public formal 47-case gate
- [ ] Uploaded artifact is the packaged plugin output from the validated candidate commit, not a raw engine workspace copy
- [ ] Uploaded artifact excludes root review reports, `openspec/`, AGENTS/CLAUDE local rules, raw automation trees, cache folders, generated HostProject fallback artifacts, local paths, and transient debug output
- [ ] Public export workspace boundary check passed: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -Mode ExportWorkspace`
- [ ] Release entry links to the relevant public docs and the current validation governance manifest
