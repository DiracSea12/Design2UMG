# Test Coverage Boundary

Chinese companion: [TestCoverageBoundary.zh-CN.md](TestCoverageBoundary.zh-CN.md)

This document explains what the current test layers actually cover, who can run them, and what this repo does not claim.

It is intentionally about boundaries, not scorekeeping:

- it does not publish a coverage percentage
- it does not treat public CI as a substitute for Unreal validation
- it does not treat maintainer-only release gates as contributor PR checks

## Public CI Truth

The tracked GitHub Actions workflow is `.github/workflows/build.yml`.

Today it has two different jobs with different trust levels:

| Job | Trigger | What it really checks | Public PR default? |
| --- | --- | --- | --- |
| `repository-hygiene` | `push`, `pull_request`, `workflow_dispatch` | required files, tracked maintainer-local path rejection, public repo boundary script | Yes |
| `build-plugin` | maintainer push, same-repository PR, `workflow_dispatch` | Unreal `BuildPlugin`, packaged-plugin focused automation, boundary strip/verify, artifact upload | Same-repository PRs only |

`build-plugin` requires a maintainer-provided self-hosted Windows UE runner, `UE_ROOT`, and `DESIGN2UMG_RUNTIME_PROJECT`. External PRs do not run code on that runner.

The full discovered admissible case set formal visual regression gate is not in public GitHub-hosted PR automation.

## Coverage Layers

| Layer | What it covers | Typical entry point | Who can realistically run it | What it does not prove |
| --- | --- | --- | --- | --- |
| `private unit tests` | parser/converter contracts, typed JSON behavior, generator invariants, workflow helpers, structure guards, import-report behavior, regression-focused editor automation tests under `Source/Design2UMGEditor/Private/Tests/` | `Tools/Run-Design2UMGAutomationTests.ps1 -Tests Design2UMG` or direct `Automation RunTests Design2UMG...` | Contributors and maintainers with a local UE setup and a real project | It does not prove packaged-plugin health or formal admissible case set no-regression |
| `editor workflow smoke` | basic import path, example content flow, generated Widget Blueprint exists, preview PNG exists, obvious editor-facing breakage | enable plugin, import an example `.pen`, inspect the generated widget and preview PNG | Contributors and maintainers | It does not prove automation parity, release packaging, or formal visual quality |
| `automation render` | focused asset render, designer snapshot, structure snapshot, import diagnostics, local repro for a specific case or bug | `Design2UMG.AutomationRender`, `Tools/design2umg_auto_verify.py`, `Run-DesignerSnapshotValidation.ps1`, `Run-StructureSnapshotValidation.ps1` | Contributors can run focused local repro with explicit `--project` / `--editor`; maintainers use the same tools for deeper evidence | It does not automatically become the maintainer release gate just because one case passed locally |
| `formal visual regression gate` | full discovered admissible case set release gate, archived `summary.no_regression`, historical-best comparison, curated reference-root evidence chain | maintainer-run batch validation described in [ValidationProtocol.md](../ValidationProtocol.md) | Maintainers only | It is not a public PR check, not a coverage badge, and not something contributors should describe as already covered by GitHub Actions |

## What Contributors Can Run

These checks are reasonable for contributors:

1. `repository-hygiene` as part of normal push / PR automation.
2. Local build with UnrealBuildTool against a real project they control.
3. `Automation RunTests Design2UMG` or a focused subset.
4. Example import smoke runs.
5. Focused `Design2UMG.AutomationRender` or `design2umg_auto_verify.py` repro with explicit project and editor paths.

Contributor-facing examples should use placeholders such as:

- `<UE5_Root>`
- `<PluginRoot>`
- `<ContributorProject.uproject>`

## What Requires Maintainer Environment

These checks require maintainer-owned infrastructure or evidence:

1. The `build-plugin` workflow for maintainer pushes, same-repository PRs, and manual runs in GitHub Actions.
2. The canonical packaged-plugin artifact that comes from the self-hosted UE runner.
3. The archived formal admissible case set release gate and its `summary.no_regression` evidence chain.
4. Historical-best parity checks tied to the maintainer admissible baseline.

Public docs should describe that boundary by role, not by a maintainer machine path.

For the architecture-level context around these boundaries, see [Architecture.md](../Architecture.md).

## Hard Boundaries

- Do not describe the formal admissible gate as a public PR default check.
- Do not publish a made-up coverage percentage.
- Do not use HostProject fallback as proof that contributor validation is good enough.
- Do not replace placeholders with maintainer absolute paths in public-facing docs.
