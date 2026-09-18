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

Classify a test by the boundary it executes, not its historical name. These L1–L4 tiers describe test dependencies and cost; they do not replace the visual metric layers or change the formal gate. Core and Editor both contain private automation tests. A `Unit` or `BatchD` prefix alone does not guarantee a pure test.

| Tier | Contract and representative owners | Execution boundary |
| --- | --- | --- |
| L1 — semantic | Parser presence, Pen overrides, variable resolution, reference expansion, layout decisions and typed payload conversion. Examples: Core `PenOverrideTests.cpp`, `DesignTextLetterSpacingPresenceTests.cpp`, `Design2UMGCoreHelperContractTests.cpp`; Editor `Design2UMGNumericParsingTests.cpp` and JSON contract cases. | Exercise input-to-output semantics without creating assets or rendering. Run focused cases through UE automation; an engine-independent test executable is not claimed. |
| L2 — UE object / service integration | WidgetTree generation, slots and properties, mapped instances, prototype graph wiring and resource binding. Examples: `Design2UMGPrototypeGraphWiringTests.cpp`, object-generation cases in `Design2UMGComponentBlueprintTests.cpp`, and `Design2UMGHttpTests.cpp`. | Use actual UE objects or request delegates. HTTP tests replace network transport only; they exercise real request construction, response validation, parsing, cancellation and session lifecycle, not live service availability. |
| L3 — persistence and failure | Compile/save/manifest failures, read-only files, Undo ownership, replacement construction, unavailable mappings and nested partial synchronization. Owners: `Design2UMGSaveSemanticsTests.cpp`, `Design2UMGCommitResultTests.cpp`, `Design2UMGReimportQA.*.cpp`, batch rollback cases in `Design2UMGComponentBlueprintTests.cpp`. | Inject a failure at the relevant boundary and assert the returned outcome **and** preserved disk/object state. Some L2 owners also contain L3 cases; select the case, not the whole file as a purported single tier. |
| L4 — end-to-end golden / visual | Pen/Figma import, candidate/reference images, tree correspondence, deterministic output and fresh-process asset reload. Entry points: `Design2UMG.AutomationRender`, `design2umg_auto_verify.py`, designer/structure snapshot tools and the maintainer batch in [ValidationProtocol.md](../ValidationProtocol.md). | Preserve input/reference identities and archive the complete admissible case set and historical comparison. A focused render or same-process reload does not establish the full gate or fresh-process persistence. |
| Architecture guards — separate from L1–L4 | Source visibility and dependency ownership. `Design2UMGArchitectureGuardTests.cpp` owns `Design2UMG.Architecture.TextMeasurementPrivateBoundary`. | Check the declared architecture, not runtime text measurement or visual correctness. File-location guards require a source checkout. |

An interactive editor workflow smoke test remains useful for example import, generated asset and preview inspection, but is not a substitute for any complete automated tier. Public CI and maintainer execution boundaries above still apply.

### Test value and fixture policy

A test that only assigns a standalone `TOptional`, reads `IsSet` / `GetValue`, then resets it is a low-value container test: prefer an actual parser, override, serialization or property-application contract. Do not remove a presence assertion merely because the implementation uses `TOptional`. Explicit zero, false and empty values must survive their real consumer boundaries.

**Marked low-value case:** `Design2UMG.PublicSurface.PostReviewFixesGuard` in `DesignNodeSemanticsTests.cpp` only assigns layout optionals, reads them and resets the row field. Its historical selector is retained as a storage smoke check, but it is not evidence for grid positioning, parser presence or architecture boundaries. Do not extend it with more container-only assertions; actual semantic coverage comes from parser/converter cases. This classification does not inflate the business coverage claimed below.

The PR-09 review retained `TextLetterSpacingPresenceExplicitZeroVsUnset`, `RefOverrideExplicitZeroLetterSpacingPresence`, typed empty-text/property tests and JSON round trips. `SourceExtensionsRemainOptIn` and `UMGSourceExtensionsRemainOptIn` also stay: they verify plugin-owned read-without-allocation and explicit-mutation behavior, not an engine container. No behavior case was deleted merely to reduce the count.

Shared helpers should remove demonstrated duplication, not model the application a second time. `Design2UMGHttpTestTransport.h` provides a scoped transport and raw `IHttpResponse`; it removes a pending request before invoking its real completion delegate, so callbacks can enqueue another request safely. Construct the transport before the client, and keep response status/session/body logic in production. It does not provide network, timeout scheduling or concurrency coverage.

### Fault-injection evidence map

| Boundary | Concrete test / injection | Required observation |
| --- | --- | --- |
| Blueprint compilation | `Design2UMG.Generation.CompileFailurePreservesDisk`: a live graph call references a missing function; the real Kismet compiler runs. | Compiler error, unsuccessful commit, unchanged existing bytes and no newly saved failed asset. |
| Save / manifest / read-only | `SaveFailureRollsBackAndKeepsManifest`, `ManifestWriteFailurePropagates`, `SaveWritabilityPrecheckBlocksHalfUpdate` under `Design2UMG.SafeReimportQA`; one-shot save/sidecar injection and actual file read-only flags. | Original asset/manifest preservation or an explicit partial-commit outcome; flags restored at cleanup. |
| Undo / finalization | `Design2UMG.CommitResult.ReimportUndoFailure` and `.ManifestFinalizeFailure`; one-shot Undo/finalization failures. | Recovery failure is distinct from success; prior user transactions are not silently consumed. |
| Replacement / missing mapping | `Design2UMG.SafeReimportQA.PrepareCommit.KindFailurePreservesSubtree`, `.AutomaticOverrideFailurePreservesAllSlots`, and `Design2UMG.SafeReimportQA.MappingDiagnosticsSurfaced`; invalid widget types or unloadable classes. | Original subtree/slots/manifest retained; diagnostics identify the actual failed boundary. |
| Nested partial synchronization | `Design2UMG.SafeReimportQA.NestedAddAndRemovalBoundary`, `.NestedRemoveConflictNamesNode` and `.NestedMoveConflictCreatesNoCopy`; real nested owned/project content conflicts. | No duplicate instance, unsafe removal or success-shaped partial update; unsupported work remains explicit. |
| HTTP | `Design2UMG.Http.Figma.ResponseContract`, `.MCP.ResponseContract`, `.MCP.HandshakeFailures` plus the retained five `Design2UMG.BatchD` HTTP lifecycle cases. | Exact callback delivery, real status/body/session processing, stale-response suppression, launch-failure cleanup and session release. |

This is a navigation map to executable evidence, not a coverage percentage or a claim that each run exercised all tiers. Record the actual test identifiers, logs and result counts for a change.

### PR-09 ownership and selector migration

The five `Design2UMG.BatchD` HTTP tests moved from `Design2UMGTests.BatchDStructure.cpp` to `Design2UMGHttpTests.cpp` without renaming. The two component REST dependency tests use the same transport fixture and remain in their integration owner. The only relocated selector is `Design2UMG.TextMeasurement.PublicHeaderRemoved` → `Design2UMG.Architecture.TextMeasurementPrivateBoundary`; actual text-measurement behavior selectors are unchanged.

Use `Automation RunTests Design2UMG` for the full plugin suite, `Automation RunTests Design2UMG.Architecture` for the new architecture group, and `Automation RunTests Design2UMG.Http+Design2UMG.BatchD` for the response and retained lifecycle cases. The latter intentionally also includes the existing non-HTTP BatchD tests; use individual identifiers when strict isolation matters.

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
