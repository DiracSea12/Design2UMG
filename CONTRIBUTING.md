# Contributing to Design2UMG

Thanks for contributing.

## Development Setup

Put the plugin in one of the following:

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

Build with UE 5.4+.

For pipeline extension points and verification expectations, see [Docs/CONTRIBUTING_INTERNALS.md](Docs/CONTRIBUTING_INTERNALS.md).

## Validation Levels

- `repository-hygiene`: public repo checks such as required files and tracked local-only paths.
- `self-hosted build`: maintainer-operated packaging/build workflow on a self-hosted Windows UE runner.
- `public smoke/public validate`: contributor-reproducible build and focused validation steps; useful for smoke checks and local repro, but not the release gate.
- `maintainer formal admissible gate`: the maintainer-owned full discovered 47-case release gate described in [Docs/ValidationProtocol.md](Docs/ValidationProtocol.md).

## Code Style

- Follow Unreal Engine C++ style conventions.
- Keep functions focused and small.
- Avoid unnecessary abstraction.
- Remove dead code instead of commenting it out.

## Before Opening a PR

1. Build the plugin locally.
2. Run relevant automation tests.
3. Update docs for any user-facing behavior change.
4. Keep commits focused and reviewable.
5. State the validation tier honestly: `repository-hygiene`, `self-hosted build`, `public smoke/public validate`, and `maintainer formal admissible gate` are not interchangeable.

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
