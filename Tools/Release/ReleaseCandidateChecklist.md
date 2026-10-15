# Design2UMG Release Candidate Checklist

Use this checklist before cutting a GitHub release branch or tag.

## 1. Candidate Snapshot

- [ ] Candidate commit SHA recorded
- [ ] Scope summary prepared (feature/fix/docs/chore)
- [ ] Included file list reviewed
- [ ] Package includes LICENSE, both READMEs, changelog, limitations, the Figma exporter and public examples
- [ ] Package contains no local Fonts directory; third-party redistribution permission is recorded before adding any bundled font
- [ ] Excluded local-only files confirmed
- [ ] Export plan reviewed against `Tools/Release/PublicRepoExportWorkflow.md`
- [ ] GitHub Release draft targets this exact candidate commit / tag

## 2. Workspace Hygiene

- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Release/Assert-CleanWorkspace.ps1` passed
- [ ] No staged/unstaged/untracked mix in release candidate workspace
- [ ] No local cache/screenshot/debug script files in tracked set
- [ ] Source workspace public boundary check passed: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -Mode SourceWorkspace`

## 3. Encoding and Text Integrity

- [ ] UTF-8 without BOM encoding guard passed for tracked `.h/.cpp/.cs/.py/.md/.json/.yml/.yaml`
- [ ] Mojibake markers check passed (garbled-path marker and replacement-character marker not found)
- [ ] Critical files manually spot-checked for readable logs/comments

## 4. Security and Secrets

- [ ] Public token examples use placeholders such as `<FigmaAccessToken>`
- [ ] Figma token flow docs prioritize env var / secure input
- [ ] CLI plaintext token path is marked as compatibility only
- [ ] Real tokens are absent from public logs, command transcripts, screenshots, and release artifacts
- [ ] Runtime logs do not print token values
- [ ] Reachable Git history and release archives checked for credentials and private design material; findings resolved before publishing, without rewriting history implicitly
- [ ] Example designs, screenshots and embedded images have confirmed publication rights; root MIT licensing is not used to assume rights to third-party assets
- [ ] Known web font auto import is documented as experimental / optional, not as the primary offline-safe workflow

## 5. Quality Gates

- [ ] Public smoke validation command is documented separately from the release gate
- [ ] Public smoke checks are described as contribution confidence checks, not as the formal admissible gate
- [ ] Self-hosted BuildPlugin passed (`RunUAT BuildPlugin -StrictIncludes`) on a maintainer-provided UE runner
- [ ] Installed plugin source matches the candidate, no duplicate plugin is present, and the real project's editor target was rebuilt before automation
- [ ] Actual packaged plugin installed as the only copy and tested in the existing real project; source-matched installed-copy automation alone does not satisfy this item
- [ ] Generated assets saved and reloaded, then exercised in a cooked game with the plugin's editor modules absent; record the map, Widget asset, build configuration and results
- [ ] Focused automation tests passed (`Automation RunTests Design2UMG`)
- [ ] The complete discovered user/maintainer corpus was validated against its own frozen baseline; private sources, references and raw reports remain private
- [ ] Any coverage visibility is limited to the actually collected public/self-hosted scope; no percentage badge is published without a real collection job
- [ ] BuildPlugin package artifact is uploaded and downloadable
- [ ] BuildPlugin package boundary check passed before upload: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -RepoRoot <PackageRoot> -Mode Package`
- [ ] Artifact publication evidence records the self-hosted workflow run URL or equivalent downloadable artifact proof

## 6. Public Consistency

- [ ] Public name uses `Design2UMG` (README, issue/pr templates, plugin metadata)
- [ ] `Design2UMG.uplugin` `VersionName` matches `CHANGELOG.md`
- [ ] Version follows semantic versioning (`MAJOR.MINOR.PATCH`)
- [ ] Public docs describe the maintainer validation boundary without exposing maintainer absolute paths
- [ ] Public docs do not claim a hosted full UE formal gate, public CI matrix, or coverage badge
- [ ] Badge explanation describes the entire workflow (hosted checks plus eligible self-hosted jobs), not an individual job or a full visual/game-runtime gate

## 7. GitHub Release Notes and Assets

- [ ] GitHub generated release notes use `.github/release.yml` categories
- [ ] No standalone checked-in release-notes template is assumed for this release; generated notes are reviewed and edited instead
- [ ] Generated notes were reviewed and edited for plain-language user impact
- [ ] Release notes clearly separate public smoke evidence, self-hosted BuildPlugin evidence, and maintainer formal admissible evidence
- [ ] Release notes do not claim a hosted full UE gate or a public formal admissible gate
- [ ] Uploaded artifact is the packaged plugin output from the validated candidate commit, not a raw engine workspace copy
- [ ] Version/platform-specific ZIP, candidate.json and SHA256SUMS.txt attached to the GitHub Release; Actions artifacts expire after 14 days
- [ ] SHA-256 of the downloaded release ZIP matches the candidate checksum; do not rebuild between validation and publication
- [ ] Release notes distinguish versions actually tested from unverified engine/platform combinations
- [ ] Uploaded artifact excludes root review reports, `openspec/`, AGENTS/CLAUDE local rules, raw automation trees, cache folders, generated HostProject fallback artifacts, local paths, and transient debug output
- [ ] Public export workspace boundary check passed: `pwsh -NoProfile -File Tools/Release/Assert-PublicRepoBoundary.ps1 -Mode ExportWorkspace`
- [ ] Release entry links to public tooling docs and states the actual validation scope without distributing private cases or raw reports
