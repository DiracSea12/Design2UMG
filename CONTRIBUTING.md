# Contributing to Design2UMG

Thanks for contributing.

## Development Setup

Put the plugin in one of the following:

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

Build with UE 5.4+.

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
5. Treat public `validate` steps as smoke/public validation unless you are explicitly reproducing the maintainer full admissible gate.

## Pull Request Checklist

- What changed
- Why it changed
- How to verify
- Any known impact or limitations
- Whether the change was checked only with public/smoke validation or also against the maintainer full admissible gate

## Reporting Issues

Include:
- UE version
- OS
- Repro steps
- Expected vs actual behavior
- Minimal design input (`.pen` or Figma sample) if possible

## License

By contributing, you agree your contribution is licensed under MIT.
