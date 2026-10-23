"""Publisher-only, pinned Win64 font-worker build. End users run the bundled exe."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import venv


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    if sys.version_info[:3] != (3, 13, 7) or sys.platform != "win32":
        raise RuntimeError("Build with the pinned CPython 3.13.7 Windows x64 distribution.")
    if not args.build_dir.is_absolute():
        raise ValueError("Use an absolute, external build directory.")
    plugin = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    if build.is_relative_to(plugin):
        raise ValueError("Keep packaging intermediates outside the plugin.")
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
    output = plugin / "Resources/PencilFontWorker/Win64"
    if (output.resolve().parent != (plugin / "Resources/PencilFontWorker").resolve()
            or not output.resolve().is_relative_to(plugin)):
        raise ValueError("Worker output left its dedicated resource directory.")
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(build / "dist/PencilFontWorker", output)
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
    size = sum(p.stat().st_size for p in output.rglob("*") if p.is_file())
    print(json.dumps({"output": str(output), "bytes": size, "binary_sha256": identity["binary_sha256"]}))


if __name__ == "__main__":
    main()
