import hashlib
import json
from pathlib import Path

import design2umg_auto_verify as verify
from design2umg_verify_lib.diagnostics import EvidenceReadError
from design2umg_verify_lib.diagnostics import emit_diagnostic


REFERENCE_CACHE_SCHEMA_VERSION = 1


def resolve_reference_cache_root(args, output_root: Path) -> Path | None:
    if args.reference_cache == "off":
        return None
    if args.reference_cache_root:
        return Path(args.reference_cache_root).resolve()
    return output_root.parent / "_reference_cache"


def build_source_signature(source_path: Path) -> dict:
    resolved = source_path.resolve()
    stat = resolved.stat()
    digest = hashlib.sha1(resolved.read_bytes()).hexdigest()
    return {
        "path": str(resolved),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha1": digest,
    }


def build_reference_cache_key(case_args) -> str:
    payload = {
        "schema": REFERENCE_CACHE_SCHEMA_VERSION,
        "mode": case_args.mode,
        "source": str(Path(case_args.source).resolve()),
        "node": case_args.node or "",
        "reference_provider": case_args.reference_provider,
    }
    raw = json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()


def can_use_reference_cache(case_args, case: dict) -> bool:
    if case_args.mode != "pen":
        return False
    if case.get("reference_path"):
        return False
    if case_args.reference:
        return False
    return case_args.reference_provider == "pencil"


def restore_reference_from_cache(
    case_args,
    case: dict,
    output_dir: Path,
    cache_root: Path | None,
) -> tuple[Path, dict] | None:
    if not cache_root or not can_use_reference_cache(case_args, case):
        return None

    source_path = Path(case_args.source)
    if not source_path.is_file():
        return None

    cache_key = build_reference_cache_key(case_args)
    cache_dir = cache_root / cache_key
    meta_path = cache_dir / "meta.json"
    cached_reference_path = cache_dir / "reference.png"
    if not meta_path.is_file() or not cached_reference_path.is_file():
        return None

    metadata = verify.helper_load_json(meta_path)
    if metadata.get("schema") != REFERENCE_CACHE_SCHEMA_VERSION:
        return None
    if metadata.get("mode") != case_args.mode:
        return None
    if metadata.get("node") != (case_args.node or ""):
        return None
    if metadata.get("reference_provider") != case_args.reference_provider:
        return None

    expected_signature = metadata.get("source_signature") or {}
    current_signature = build_source_signature(source_path)
    # Content hash is the source identity; size/mtime can drift through file
    # syncs that never alter bytes, and the cache key already pins the path.
    if expected_signature.get("sha1") != current_signature.get("sha1"):
        return None

    output_reference_path = output_dir / "reference.png"
    verify.shutil.copyfile(cached_reference_path, output_reference_path)

    reference_context = metadata.get("reference_context") or {}
    cached_structure_path = cache_dir / "source_structure_live.json"
    if cached_structure_path.is_file():
        try:
            reference_context["source_structure_live"] = verify.helper_load_json(cached_structure_path)
            reference_context["source_structure_live_path"] = str(cached_structure_path)
        except EvidenceReadError as exc:
            reference_context["source_structure_live_unavailable"] = {
                "reason": exc.reason,
                "path": str(exc.path),
                "detail": exc.detail,
            }
            emit_diagnostic("warning", exc.reason, exc.path, exc.detail)

    cache_meta = {
        "hit": True,
        "cache_dir": str(cache_dir),
        "cache_key": cache_key,
    }
    reference_context["reference_cache"] = cache_meta
    reference_context.setdefault("reference_authority", {})
    return output_reference_path, reference_context


def save_reference_to_cache(
    case_args,
    case: dict,
    cache_root: Path | None,
    reference_path: Path,
    reference_context: dict,
) -> None:
    if not cache_root or not can_use_reference_cache(case_args, case):
        return

    source_path = Path(case_args.source)
    if not source_path.is_file():
        return

    cache_key = build_reference_cache_key(case_args)
    cache_dir = cache_root / cache_key
    cache_dir.mkdir(parents=True, exist_ok=True)
    verify.shutil.copyfile(reference_path, cache_dir / "reference.png")

    cached_context = dict(reference_context or {})
    source_structure_live = cached_context.pop("source_structure_live", None)
    cached_context.pop("source_structure_live_path", None)
    if source_structure_live is not None:
        verify.write_json(cache_dir / "source_structure_live.json", source_structure_live)

    metadata = {
        "schema": REFERENCE_CACHE_SCHEMA_VERSION,
        "mode": case_args.mode,
        "source": str(source_path.resolve()),
        "node": case_args.node or "",
        "reference_provider": case_args.reference_provider,
        "source_signature": build_source_signature(source_path),
        "reference_context": cached_context,
    }
    verify.write_json(cache_dir / "meta.json", metadata)
