# Pencil font worker

Static TTF/OTF fonts can be previewed and imported directly, without installing this optional Win64 tool. Web-compressed fonts (WOFF2) and variable fonts (one file containing multiple weights) need conversion into font files UE can use. The tool extracts the weight the design needs from a variable font, for example 600.

The tool is not installed by default and never downloads automatically. Check its option in the preview, then confirm again to download; cancelling does not install it. The confirmation lists Python / FontTools / Brotli, the version, GitHub Release source and URL, about 9 MiB to download, about 16.7 MiB installed, the installation path, and verification details. A failed download-package check prevents execution; no separate Python installation is needed.

Without the tool, static TTF/OTF fonts still work; fonts that need conversion produce a message that they cannot be converted. You can remove the tool without deleting font resources already imported into UE. It only converts existing font files and does not find or download missing fonts.

The plugin package includes `Resources/PencilFontWorkerDownload.json` and editable `Resources/FontToolText.json`; it excludes the executable, embedded Python / FontTools / Brotli, and publisher tools. Our worker/build source remains in the source repository. The generated UMG uses native font assets, not this worker or a Python plugin. Keep the executable, `_internal/`, `notices/` and `identity.json` together. Import callers launch it hidden; the binary uses the GUI subsystem and has no console or success dialog.

- Enter a storage directory in **安装位置** (Installation Location), or select it with **浏览…** (Browse). UE saves the choice in the current project's user config across panel/editor restarts. The default is `<plugin>/Saved/Tools`; the actual install is `<selected directory>/PencilFontWorker/1.0.0`, shown in the panel and confirmation. Remove the current tool before changing location; installation/conversion blocks changes. No old AppData lookup or migration occurs.
- Removal validates the installation record and package file list, deletes only owned files and empty subdirectories, and preserves extra user files. Existing foreign installation directories are never overwritten. Missing/corrupt records, modified files, and reparse nodes inside the managed subtree prevent removal; approved junctions above the storage directory remain supported.
- Edit font-tool UI, management errors and remedy-dialog text in `Resources/FontToolText.json` (flat string values). Reopen the panel or make the next request to reload it. Keep keys and named placeholders: `{Version}`, `{DownloadUrl}`, `{DownloadMiB}`, `{InstalledMiB}`, `{InstallDirectory}`, `{File}`. Nested values and literal-brace escaping are unsupported. Invalid resources/keys display their path/key, with no compiled Chinese fallback.

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

The script creates a private environment and installs the exact wheel versions/hashes in `requirements-lock.txt` from PyPI. It freezes only the conversion entry, includes dynamic sfnt table modules, and excludes package-management/UI/TLS modules. Public `hashlib` uses CPython's built-in SHA implementations rather than shipping OpenSSL. End users need no Python installation, pip packages or rebuild.

Build output stays in `<build-dir>/dist/PencilFontWorker`; the script never replaces `Resources`. It also creates `<build-dir>/PencilFontWorker-1.0.0-Win64.zip` and `<build-dir>/PencilFontWorkerDownload.json`. `identity.json` records build-source identity, the executable, dependency versions/notices and each distributed file's SHA-256. The directory carries PSF, FontTools, Brotli, PyInstaller bootloader-exception and other dependency notices; Microsoft runtime terms come from the official [Visual C++ runtime license page](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/). Imported font licenses are separate from the tool's licenses.

To package an existing frozen tool without rebuilding Python:

```text
python Tools/BuildPencilFontWorker.py --build-dir G:/EXTERNAL/FontToolRelease --package-dir G:/EXTERNAL/VerifiedWorker/Win64
```

This verifies the complete file set against `identity.json`, including runtime files and required license notices. ZIP entries are sorted with fixed timestamps and permissions; identical input bytes packaged with the same Python/compression runtime produce the same archive and manifest. The ZIP root directly contains `PencilFontWorker.exe`, `_internal/`, `notices/` and `identity.json`, without a directory prefix. The manifest records `version`, `url`, the whole archive's `archive_sha256` and `archive_bytes`, and the sum of all installed files as `installed_bytes`. Use `--manifest Resources/PencilFontWorkerDownload.json` to update the plugin's download manifest explicitly.

The fixed independent release tag is `pencil-font-worker-1.0.0`, with asset `PencilFontWorker-1.0.0-Win64.zip` and [download URL](https://github.com/DiracSea12/Design2UMG/releases/download/pencil-font-worker-1.0.0/PencilFontWorker-1.0.0-Win64.zip). The script only stages local artifacts; it does not publish a release.

Version 1.0.0 preserves the existing frozen bytes and original `identity.json`. Its source hash describes the original build inputs, not the updated publisher script; no Python rebuild was performed.

`test_worker.py` checks the real executable with external read-only fixtures. Tests remove Python-related environment variables, restrict PATH to System32 and use hidden child processes. No fonts or validation references are shipped with the tool.

## Font tests

Ordinary test runs do not automatically download the tool. `Design2UMG.Fonts.LocalSource.DeclaredWoff2` requires an installed tool and fails explicitly when it is missing, unless `-D2UInstallFontToolForTests` is supplied. CI passes this parameter only for `Design2UMG.Fonts.LocalSource`: the test waits for a real verified installation, executes every WOFF2 import assertion, and removes only the installation it created. Existing user installations are preserved.

The separate online lifecycle validation uses `Design2UMG.Pencil.FontTool.DownloadManifest` with `-D2UValidateFontToolLifecycle`. It refuses an existing installation and validates cancellation, digest rejection, installation, acquisition and removal, ending with the tool uninstalled.
