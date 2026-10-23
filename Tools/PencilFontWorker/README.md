# Pencil font worker

This is an import-time, self-contained Win64 executable. The generated UMG uses native font assets, not this worker or a Python plugin. Keep the executable and its `_internal` directory together. Import callers launch it hidden; the binary uses the GUI subsystem and has no console or success dialog.

## Protocol 1

```text
PencilFontWorker.exe --request ABS_JSON --response ABS_JSON
```

The request is UTF-8 JSON:

```json
{"input_path":"G:/TEMP/source.font","output_path":"G:/TEMP/converted.ttf","weight":350}
```

An optional `axes` object may specify actual source axis coordinates. Omitted axes use the font's `fvar` defaults, except `wght`, which uses `weight`. Conflicting `wght` values are an error. Coordinates are clamped to the font's own bounds and represented at FreeType's 16.16 precision.

For read-only indexing, send `{"action":"inspect","input_path":"G:/TEMP/source.woff2"}` without output/weight. The response contains `success`, `source_sha256`, `family`, `postscript`, `weight`, `italic`, `variable`, and `weight_min`/`weight_max` when a `wght` axis exists. Inspection only reads name/OS2/fvar; it does not instantiate or write a font. Omitted action preserves conversion behavior; explicit `convert` is also accepted.

Success returns `success`, `source_sha256`, `output_sha256`, actual `weight`, and the complete effective `axes`. Failure returns `success:false` and `error`, with a nonzero exit code. Output and response must be new absolute paths in the caller's own temporary directory; input files are never overwritten. Input/output fonts are limited to 32 MiB. The single policy source is `Source/Design2UMGEditor/Private/DesignFontImportPolicy.h`. The publisher derives `font_policy.py` for the Python source CLI and freezes it into the executable; do not edit that generated module.

Static TTF/OpenType bytes and names are preserved. WOFF2 is decoded into sfnt. Variable fonts are fully instantiated using FontTools. Family names remain unchanged; static subfamily names describe their real axis tuple, with a derived full name/PostScript identity. This also represents non-named weights without rounding them to a named style.

## Publisher build

Build only with CPython 3.13.7 Windows x64:

```text
python Tools/BuildPencilFontWorker.py --build-dir G:/EXTERNAL/PencilFontWorkerBuild
```

The script creates a private environment and installs the exact wheel versions/hashes in `requirements-lock.txt` from PyPI. It freezes only the conversion entry, includes dynamic sfnt table modules, and excludes package-management/UI/TLS modules. Public `hashlib` uses CPython's built-in SHA implementations rather than shipping OpenSSL. No user-machine Python installation, package download, or rebuild occurs during import.

The dedicated `Resources/PencilFontWorker/Win64` output is replaced by the build. `identity.json` records build-source identity, the executable, dependency versions/notices and each distributed file's SHA-256. The directory carries PSF, FontTools, Brotli, PyInstaller bootloader-exception and other dependency notices; Microsoft runtime terms come from the official [Visual C++ runtime license page](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/). Imported font licenses are separate from the tool's licenses.

`test_worker.py` checks the real executable with external read-only fixtures. Tests remove Python-related environment variables, restrict PATH to System32 and use hidden child processes. No fonts or validation references are shipped with the tool.
