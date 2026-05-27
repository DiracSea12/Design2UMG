from pathlib import Path

from design2umg_verify_lib.json_io import write_json


def write_editor_driver_script(
    script_path: Path,
    manifest_path: Path,
    driver_log_path: Path,
    manifest_fields: dict,
    spec_manifest_key: str,
) -> Path:
    manifest_payload = dict(manifest_fields)
    manifest_payload["driver_log_path"] = str(driver_log_path)
    write_json(manifest_path, manifest_payload)

    script_content = f"""import json
import traceback
from datetime import datetime
import unreal

MANIFEST_PATH = r\"{manifest_path}\"
SPEC_MANIFEST_KEY = \"{spec_manifest_key}\"


def _write_line(path, message):
    timestamp = datetime.utcnow().isoformat(timespec=\"milliseconds\") + \"Z\"
    with open(path, \"a\", encoding=\"utf-8\") as handle:
        handle.write(f\"{{timestamp}} {{message}}\\n\")


def _get_world():
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def _iter_spec_paths(manifest):
    if SPEC_MANIFEST_KEY == \"spec_path\":
        return [manifest.get(\"spec_path\", \"\")]
    return manifest.get(\"spec_paths\", [])


def _run():
    driver_log_path = \"\"
    try:
        with open(MANIFEST_PATH, \"r\", encoding=\"utf-8-sig\") as handle:
            manifest = json.load(handle)
        driver_log_path = manifest.get(\"driver_log_path\", \"\")
        if driver_log_path:
            _write_line(driver_log_path, \"driver:start\")
        spec_paths = _iter_spec_paths(manifest)
        if SPEC_MANIFEST_KEY == \"spec_path\" and driver_log_path:
            _write_line(driver_log_path, f\"driver:run:{{spec_paths[0]}}\")
        world = _get_world()
        for spec_path in spec_paths:
            if driver_log_path:
                if SPEC_MANIFEST_KEY != \"spec_path\":
                    _write_line(driver_log_path, f\"driver:run:{{spec_path}}\")
            unreal.SystemLibrary.execute_console_command(world, f\"Design2UMG.AutomationRender {{spec_path}} --no-quit\")
        if driver_log_path:
            _write_line(driver_log_path, \"driver:quit\")
        unreal.SystemLibrary.execute_console_command(world, \"QUIT_EDITOR\")
    except Exception:
        if driver_log_path:
            _write_line(driver_log_path, traceback.format_exc())
        raise


_run()
"""
    script_path.write_text(script_content, encoding="utf-8")
    return script_path
