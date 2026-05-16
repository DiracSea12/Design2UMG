# Contributing to Design2UMG

Chinese companion: [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)

Thanks for contributing.

## Development Setup

Put the plugin in one of the following:

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

Requirements:
- Unreal Engine 5.4+
- Visual Studio 2022 (17.8+ recommended)

Build the plugin via UnrealBuildTool:

```bat
"<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" <ProjectName>Editor Win64 Development "-Project=<ContributorProject.uproject>" -WaitMutex
```

Run automation tests:

```bat
"<UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe" "<ContributorProject.uproject>" -ExecCmds="Automation RunTests Design2UMG" -unattended -nopause -nullrhi -log
```

Use a real UE project you control. HostProject fallback is not a supported contributor or maintainer validation path.

For pipeline extension points and verification expectations, see [Docs/Architecture.md](Docs/Architecture.md) and [Docs/CONTRIBUTING_INTERNALS.md](Docs/CONTRIBUTING_INTERNALS.md).

## Validation Levels

- `repository-hygiene`: public repo checks such as required files and tracked local-only paths.
- `self-hosted build`: maintainer-operated packaging/build workflow on a self-hosted Windows UE runner; package boundary validation and artifact upload happen there.
- `public smoke/public validate`: contributor-reproducible build and focused validation steps; useful for smoke checks and local repro, but not the release gate.
- `maintainer formal admissible gate`: the maintainer-owned full discovered case set release gate described in [Docs/ValidationProtocol.md](Docs/ValidationProtocol.md).

See [Docs/ValidationGovernance/TestCoverageBoundary.md](Docs/ValidationGovernance/TestCoverageBoundary.md) for the plain-language split between private unit tests, editor workflow smoke, focused automation render, and the maintainer formal gate.

Package and public export checks are stricter than source workspace checks. Local ignored planning files do not need to be deleted from a maintainer source workspace, but release packages and public exports must not contain root review reports, `openspec/`, AGENTS/CLAUDE local rules, raw automation trees, cache folders, generated HostProject fallback artifacts, generated fallback host directories, concrete fallback project paths, or maintainer absolute paths.

## Code Style

- Follow Unreal Engine C++ style conventions.
- Keep functions focused and small.
- Avoid unnecessary abstraction.
- Remove dead code instead of commenting it out.

## Before Opening a PR

1. Build the plugin locally.
2. Run the relevant contributor-facing checks you can actually reproduce locally: build, `Automation RunTests Design2UMG`, example import smoke, or a focused automation repro.
3. Update docs for any user-facing behavior change.
4. Keep commits focused and reviewable.
5. State the validation tier honestly: `repository-hygiene`, `self-hosted build`, `public smoke/public validate`, and `maintainer formal admissible gate` are not interchangeable.

## Branch Naming

Use `<type>/<short-description>` where type is one of: `fix`, `feat`, `refactor`, `docs`.

## Pull Request Checklist

- What changed
- Why it changed
- How to verify
- Any known impact or limitations
- Which validation tier was actually run

## Reporting Issues

Include:
- UE version
- OS
- Repro steps
- Expected vs actual behavior
- Minimal design input (`.pen` or Figma sample) if possible

## License

By contributing, you agree your contribution is licensed under MIT.
