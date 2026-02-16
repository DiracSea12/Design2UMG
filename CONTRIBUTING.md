# Contributing to Design2UMG

Thanks for your interest in contributing!

## Getting Started

1. Fork the repository
2. Clone to your UE5 engine or project plugin directory
3. Build with UE 5.4+

## Development Setup

```bash
# Engine plugin location
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/

# Or project plugin location
<Project>/Plugins/Design2UMG/
```

## Code Style

- Follow Unreal Engine C++ coding standards
- Max 3 levels of indentation — refactor if deeper
- Functions do one thing and do it well
- Delete dead code, don't comment it out
- No over-engineering — solve the actual problem

## Submitting Changes

1. Create a feature branch from `main`
2. Keep commits focused and atomic
3. Write clear commit messages (English)
4. Ensure the plugin compiles on your UE version
5. Open a Pull Request with:
   - What the change does
   - Why it's needed
   - How to test it

## Reporting Issues

- Use GitHub Issues
- Include: UE version, OS, steps to reproduce, expected vs actual behavior
- Attach the `.pen` or Figma file if relevant (or a minimal reproduction)

## Architecture Overview

```
.pen / Figma / MCP
    → PenParser / FigmaParser / PenMCPBridge
    → Design2UMGConverter
    → LayoutFixer → LayoutCalculator
    → UMGGenerator → Widget Blueprint
```

Key principle: **Bounds is the single source of truth.** All layout calculations work on pixel-coordinate Bounds, then project to UMG Anchors/Offsets at the end.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
