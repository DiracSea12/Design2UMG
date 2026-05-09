# Design2UMG Release Candidate Checklist

Use this checklist before cutting a GitHub release branch or tag.

## 1. Candidate Snapshot

- [ ] Candidate commit SHA recorded
- [ ] Scope summary prepared (feature/fix/docs/chore)
- [ ] Included file list reviewed
- [ ] Excluded local-only files confirmed
- [ ] Export plan reviewed against `Tools/Release/PublicRepoExportWorkflow.md`

## 2. Workspace Hygiene

- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Release/Assert-CleanWorkspace.ps1` passed
- [ ] No staged/unstaged/untracked mix in release candidate workspace
- [ ] No local cache/screenshot/debug script files in tracked set

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
- [ ] Self-hosted BuildPlugin passed (`RunUAT BuildPlugin -StrictIncludes`) on a maintainer-provided UE runner
- [ ] Focused automation tests passed (`Automation RunTests Design2UMG`)
- [ ] Maintainer formal full admissible validation passed and wrapped in the current `Docs/ValidationGovernance/AdmissibleBaselineManifest_<date>.json`
- [ ] CI artifacts uploaded and downloadable

## 6. Public Consistency

- [ ] Public name uses `Design2UMG` (README, issue/pr templates, plugin metadata)
- [ ] `Design2UMG.uplugin` `VersionName` matches `CHANGELOG.md`
- [ ] Version follows semantic versioning (`MAJOR.MINOR.PATCH`)
- [ ] Public docs describe the maintainer validation boundary without exposing maintainer absolute paths
