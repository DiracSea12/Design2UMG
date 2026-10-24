"""Build or package the optional Win64 font worker outside the plugin."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import venv
import zipfile


VERSION = "1.0.0"
RELEASE_TAG = f"pencil-font-worker-{VERSION}"
ASSET_NAME = f"PencilFontWorker-{VERSION}-Win64.zip"
DOWNLOAD_URL = f"https://github.com/DiracSea12/Design2UMG/releases/download/{RELEASE_TAG}/{ASSET_NAME}"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_worker_sources(plugin):
    policy = plugin / "Source/Design2UMGEditor/Private/DesignFontImportPolicy.h"
    values = re.findall(r"\bconstexpr\s+int32\s+MaxFontBytes\s*=\s*(\d+)\s*;",
                        policy.read_text(encoding="utf-8"))
    if len(values) != 1:
        raise RuntimeError("Expected one integer font byte limit in DesignFontImportPolicy.h.")
    generated = plugin / "Tools/PencilFontWorker/font_policy.py"
    content = ('"""Generated from DesignFontImportPolicy.h. Do not edit."""\n'
               f"MAX_FONT_BYTES = {int(values[0])}\n").encode("utf-8")
    if not generated.exists() or generated.read_bytes() != content:
        generated.write_bytes(content)
    return [Path(__file__).resolve(), plugin / "Tools/PencilFontWorker/worker.py",
            plugin / "Tools/PencilFontWorker/requirements-lock.txt", policy, generated]


def freeze_worker(build, python, sources):
    subprocess.run([str(python), "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir",
                    "--windowed", "--disable-windowed-traceback", "--noupx", "--name", "PencilFontWorker",
                    "--distpath", str(build / "dist"), "--workpath", str(build / "work"),
                    "--specpath", str(build / "spec"), "--collect-submodules", "fontTools.ttLib.tables",
                    "--exclude-module", "tkinter", "--exclude-module", "unittest", "--exclude-module", "pip",
                    "--exclude-module", "setuptools", "--exclude-module", "pkg_resources",
                    "--exclude-module", "ssl", "--exclude-module", "_ssl",
                    "--exclude-module", "_hashlib", str(sources[1])], check=True, creationflags=subprocess.CREATE_NO_WINDOW)


def build_source_identity(plugin, sources):
    source_hash = hashlib.sha256()
    license_sources = sorted((plugin / "Tools/PencilFontWorker/licenses").iterdir())
    for path in sorted(sources + license_sources):
        source_hash.update(path.relative_to(plugin).as_posix().encode("utf-8") + b"\n")
        data = path.read_bytes()
        if path.suffix in (".py", ".txt"):
            data = data.replace(b"\r\n", b"\n")
        source_hash.update(data + b"\n")
    return source_hash.hexdigest()


def build_identity(plugin, output, sources, packages):
    return {
        "protocol": 1,
        "python": sys.version.split()[0],
        "source_sha256": build_source_identity(plugin, sources),
        "binary_sha256": digest(output / "PencilFontWorker.exe"),
        "dependencies": packages,
        "files_sha256": {p.relative_to(output).as_posix(): digest(p)
                         for p in sorted(output.rglob("*"))
                         if p.is_file() and p != output / "identity.json"},
    }


def verify_worker(output):
    identity = json.loads((output / "identity.json").read_text(encoding="utf-8"))
    files = {p.relative_to(output).as_posix(): p for p in output.rglob("*") if p.is_file()}
    expected = identity["files_sha256"]
    if set(files) != set(expected) | {"identity.json"}:
        raise ValueError("Worker file set differs from identity.json.")
    required = {
        "PencilFontWorker.exe", "_internal/python313.dll",
        "notices/Python-LICENSE.txt", "notices/fonttools-0-LICENSE",
        "notices/brotli-0-LICENSE", "notices/pyinstaller-0-COPYING.txt",
        "notices/Microsoft-Visual-C-Runtime-2015-2022.docx",
    }
    if not required.issubset(expected):
        raise ValueError("Worker executable, runtime or license notices are missing.")
    for name, checksum in expected.items():
        if digest(files[name]) != checksum:
            raise ValueError(f"Worker SHA-256 mismatch: {name}")
    if identity["binary_sha256"] != expected["PencilFontWorker.exe"]:
        raise ValueError("Worker binary identity disagrees with file identity.")
    return files


def package_worker(output, build, manifest_path):
    files = verify_worker(output)
    archive = build / ASSET_NAME
    build.mkdir(parents=True, exist_ok=True)
    # Fixed order, timestamps and permissions make repackaging the same bytes repeatable.
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as package:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            package.writestr(entry, path.read_bytes(), compresslevel=9)
    with zipfile.ZipFile(archive) as package:
        for name, path in files.items():
            if hashlib.sha256(package.read(name)).hexdigest() != digest(path):
                raise ValueError(f"Archive SHA-256 mismatch: {name}")
    manifest = {
        "version": VERSION,
        "url": DOWNLOAD_URL,
        "archive_sha256": digest(archive),
        "archive_bytes": archive.stat().st_size,
        "installed_bytes": sum(p.stat().st_size for p in files.values()),
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"archive": str(archive), "manifest": str(manifest_path), **manifest}


def build_worker(plugin, build):
    if sys.version_info[:3] != (3, 13, 7) or sys.platform != "win32":
        raise RuntimeError("Build with the pinned CPython 3.13.7 Windows x64 distribution.")
    env = build / "env"
    if not (env / "Scripts/python.exe").exists():
        venv.EnvBuilder(with_pip=True).create(env)
    python = env / "Scripts/python.exe"
    sources = prepare_worker_sources(plugin)
    license_sources = sorted((plugin / "Tools/PencilFontWorker/licenses").iterdir())
    subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check",
                    "--index-url", "https://pypi.org/simple", "--only-binary=:all:",
                    "--require-hashes", "-r", str(sources[2])], check=True)
    freeze_worker(build, python, sources)
    output = build / "dist/PencilFontWorker"
    notices = output / "notices"
    notices.mkdir()
    shutil.copyfile(Path(sys.base_prefix) / "LICENSE.txt", notices / "Python-LICENSE.txt")
    for path in license_sources:
        shutil.copyfile(path, notices / path.name)
    # Inspect only the pinned build environment, never packages from the publisher's global site.
    metadata_script = """
import importlib.metadata as m, json
result=[]
for name in ['fonttools','brotli','pyinstaller','pyinstaller-hooks-contrib','altgraph','packaging','pefile','pywin32-ctypes','setuptools']:
 d=m.distribution(name)
 files=[str(d.locate_file(f)) for f in d.files if 'dist-info' in str(f) and any(x in str(f).lower() for x in ['license','copying']) and d.locate_file(f).is_file()]
 result.append({'name':name,'version':d.version,'license':d.metadata.get('License-Expression') or d.metadata.get('License'),'notices':files})
print(json.dumps(result))
"""
    packages = json.loads(subprocess.check_output([str(python), "-c", metadata_script], encoding="utf-8"))
    for package in packages:
        for index, filename in enumerate(package.pop("notices")):
            shutil.copyfile(filename, notices / f"{package['name']}-{index}-{Path(filename).name}")
    identity = build_identity(plugin, output, sources, packages)
    (output / "identity.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--package-dir", type=Path,
                        help="Package an existing identity-verified worker without rebuilding Python.")
    parser.add_argument("--manifest", type=Path,
                        help="Download manifest destination; defaults to the external build directory.")
    args = parser.parse_args()
    plugin = Path(__file__).resolve().parents[1]
    if not args.build_dir.is_absolute() or args.build_dir.resolve().is_relative_to(plugin):
        raise ValueError("Use an absolute build directory outside the plugin.")
    build = args.build_dir.resolve()
    output = args.package_dir.resolve() if args.package_dir else build_worker(plugin, build)
    manifest = args.manifest or build / "PencilFontWorkerDownload.json"
    print(json.dumps(package_worker(output, build, manifest)))


if __name__ == "__main__":
    main()
