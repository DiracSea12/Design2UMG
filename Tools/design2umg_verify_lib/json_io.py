import json
import hashlib
from pathlib import Path

from design2umg_verify_lib.diagnostics import diagnose_json_load_error


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise diagnose_json_load_error(path, exc) from exc


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def file_signature(path: Path) -> dict | None:
    try:
        stat = path.stat()
    except OSError:
        return None
    return {
        "path": str(path.resolve()),
        "mtime_ns": stat.st_mtime_ns,
        "size": stat.st_size,
    }


def content_file_signature(path_like: str | Path) -> dict | None:
    try:
        resolved = Path(path_like).resolve()
    except (OSError, RuntimeError, ValueError, TypeError):
        return None

    if not resolved.is_file():
        return None

    stat = resolved.stat()
    digest = hashlib.sha1(resolved.read_bytes()).hexdigest()
    return {
        "path": str(resolved),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha1": digest,
    }
