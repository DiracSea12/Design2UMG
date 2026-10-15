# Validation tools

The tools are public; design corpora and visual baselines are user-owned inputs. No private maintainer corpus is bundled or required. A local run's case count is evidence about that run, not a product limit or a required minimum.

## Collect and discover your cases

Put supported inputs under one directory of your choice. The suite tool recursively discovers `.pen` files and Figma export directories containing `design.json` plus a top-level reference PNG. It never searches parent directories or a bundled private corpus. Optional repeated `--figma-export-root` arguments explicitly select separate Figma roots instead of the default scan within `--design-root`.

```text
<YourCases>/
  screens.pen
  exports/
    dashboard/
      design.json
      reference.png
```

A Pen case is an eligible root frame/group/component/ref, not necessarily a whole file. A Figma export contributes the existing `local` and `source` verification paths. Unsupported files are not cases. A directory with design.json but no reference PNG is not yet a runnable Figma case; discovery reports a warning and does not count it. A reference that disappears after discovery blocks execution.

```powershell
python Tools/design2umg_auto_verify_suite.py --design-root <YourCases> --output-root <RunRoot> --list-cases
```

This command requires no Unreal installation. It prints the discovered set and writes `run_scope.json`. Counts follow the input: 0, 1, 100, 1000 or any other discovered total; there is no fixed quota or built-in case-name list. Include/exclude expressions filter this set, and duplicate display names receive deterministic distinct case names.

An empty normal suite writes `summary.json` with `status=empty`, `total=0`, `executed=0`, `passed=null`, starts no editor and exits zero. This means discovery completed without tests, **not** that rendering or regression validation passed. A missing root or malformed input fails instead; an incomplete Figma directory is reported as skipped, not passed.

## Run a suite

```powershell
python Tools/design2umg_auto_verify_suite.py --design-root <YourCases> --output-root <NewRunRoot> --project <YourProject.uproject> --editor <UE5_Root>/Engine/Binaries/Win64/UnrealEditor.exe --reference-root <YourFrozenReferenceRoot> --no-publish-history
```

Use an existing real project. Runtime execution requires `--project` or `DESIGN2UMG_RUNTIME_PROJECT`; no generated host project is substituted. Omit `--reference-root` when using each Figma export's PNG or the configured Pencil reference provider. Reference cache mode remains `read-write`; external frozen references are copied for comparison, not regenerated.

Each external baseline case directory needs both `reference.png` and `compare_report.json` for available per-case `no_regression`. Keep input/reference identities and baseline pointers frozen during a code comparison. A filtered run is not a full-corpus result. Compare each metric per case, never just an average.

`--no-publish-history` retains the ordinary history check and its run-local reports without replacing the global historical-best manifest. It does not waive regressions or publish anything to a remote service. Keep private cases, PNGs and run logs outside public exports.

## Determinism and fresh-process reload

```powershell
python Tools/design2umg_determinism_smoke.py validate-run-root --run-root <CompletedRunRoot>
python Tools/design2umg_fresh_process_reload_gate.py --project <YourProject.uproject> --editor <UE5_Root>/Engine/Binaries/Win64/UnrealEditor.exe --run-root <CompletedRunRoot> --output-root <NewReloadRoot>
```

Determinism checks every case recorded in `run_scope.json`, including exact set completeness, finite non-negative error metrics, zero pixel drift and structure/text parity. Empty or incomplete evidence cannot prove determinism. To generate a focused replay, use the helper's `run` command (or `Run-Design2UMGDeterminismSmoke.ps1`) with explicit project/editor/design/reference/output roots and an optional `--include` regex (`-Include` in PowerShell). No named maintainer examples are mandatory.

The reload gate's `--run-root` mode derives assets from that same manifest, launches a fresh editor, requires zero process exit, matches exact asset identities and explicit success/reload flags, then checks node counts, text, type statistics and candidate pixels. It does not require a long command line enumerating every asset. Alternatively, `--assets` takes explicit object paths; `--asset-floors` uses zero-based indices, rejects duplicate/out-of-range indices and requires positive floors. Manual asset mode checks those floors, not parity against an absent suite baseline.

## Low-cost checks and CI

```powershell
python -m pip install -r Tools/requirements-validation.txt
python Tools/run_validation_selftests.py
pwsh -NoProfile -File Tools/Tests/Test-ValidationProcessIsolation.ps1
python Tools/Tests/Test-ReleaseContracts.py
npm --prefix Tools/FigmaExporter ci
npm --prefix Tools/FigmaExporter test
```

The Python runner compiles the tooling and runs every `design2umg_*_selftest.py`, failing on the first failure. The exporter uses its committed lockfile, compiles TypeScript and runs its Node tests. Public CI uses these same entry points. These tests generate small synthetic fixtures; they do not need or upload any user's design corpus.

UE automation remains available through `Run-Design2UMGAutomationTests.ps1`. Full private visual regression is separate from GitHub-hosted checks and from the self-hosted package compilation and installed-plugin tests; neither a badge nor a zero-case scan is full visual evidence.

## Snapshot tools and release boundaries

`Run-DesignerSnapshotValidation.ps1` and `Run-StructureSnapshotValidation.ps1` reject an already-running editor by default. They never terminate unrelated editor sessions. Save and close sessions first, or explicitly use `-KeepExistingEditor` for intentional debugging.

`Release/Assert-PublicRepoBoundary.ps1` checks source/export/package boundaries. Source workspace files, private run reports, source designs and comparison references are not release assets. See [ValidationProtocol](../Docs/ValidationProtocol.md) for the detailed evidence contract.
