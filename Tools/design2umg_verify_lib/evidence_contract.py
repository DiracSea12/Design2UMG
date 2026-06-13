from pathlib import Path


SOURCE_INPUT_COMPARE_FIELDS = (
    "case",
    "source_authority",
    "source_design_artifact",
    "source_design_artifact_hash",
    "reference_root_case_path",
    "reference_image_hash",
    "baseline_compare_report_hash",
    "validation_project_path",
    "run_scope_case_names",
    "run_scope_include_pattern",
)


def first_dict(*payloads: dict, keys: tuple[str, ...]) -> dict:
    for payload in payloads:
        if not isinstance(payload, dict):
            continue
        for key in keys:
            value = payload.get(key)
            if isinstance(value, dict):
                return value
    return {}


def signature_hash(signature: object) -> str:
    if isinstance(signature, dict):
        return signature.get("sha1") or signature.get("sha256") or signature.get("hash") or ""
    if isinstance(signature, str):
        return signature
    return ""


def find_case_input(run_scope: dict, case_name: str) -> dict:
    case_inputs = run_scope.get("case_inputs") or []
    if not isinstance(case_inputs, list):
        return {}
    for case_input in case_inputs:
        if isinstance(case_input, dict) and case_input.get("name") == case_name:
            return case_input
    return {}


def normalize_dll_binding(raw: dict, *keys: str) -> tuple[dict, list[str]]:
    dll_payload = first_dict(raw, keys=keys)
    normalized = {
        "path": dll_payload.get("path") or dll_payload.get("absolute_path") or "",
        "sha256": dll_payload.get("sha256") or "",
        "size": dll_payload.get("size") if dll_payload.get("size") is not None else dll_payload.get("file_size"),
        "mtime": dll_payload.get("mtime") if dll_payload.get("mtime") is not None else dll_payload.get("modified_time"),
    }
    missing = [key for key, value in normalized.items() if value in (None, "")]
    return normalized, missing


def collect_binary_binding(run_scope: dict, summary: dict, report: dict) -> dict:
    raw = first_dict(
        report,
        summary,
        run_scope,
        keys=(
            "historical_best_binary_binding",
            "binary_binding",
            "plugin_binary_fingerprint",
            "build_fingerprint",
        ),
    )
    core_dll, core_missing = normalize_dll_binding(
        raw,
        "UnrealEditor-Design2UMGCore.dll",
        "design2umg_core_dll",
        "core_dll",
    )
    editor_dll, editor_missing = normalize_dll_binding(
        raw,
        "UnrealEditor-Design2UMGEditor.dll",
        "design2umg_editor_dll",
        "editor_dll",
    )
    normalized = {
        "plugin_source_snapshot": raw.get("plugin_source_snapshot") or raw.get("source_snapshot") or "",
        "design2umg_core_dll": core_dll,
        "design2umg_editor_dll": editor_dll,
        "validation_project_path": raw.get("validation_project_path") or raw.get("project_path") or "",
        "engine_root": raw.get("engine_root") or "",
        "engine_version": raw.get("engine_version") or "",
    }
    missing = []
    if not normalized["plugin_source_snapshot"]:
        missing.append("plugin_source_snapshot")
    missing.extend(f"design2umg_core_dll.{key}" for key in core_missing)
    missing.extend(f"design2umg_editor_dll.{key}" for key in editor_missing)
    if not normalized["validation_project_path"]:
        missing.append("validation_project_path")
    if not normalized["engine_root"] and not normalized["engine_version"]:
        missing.append("engine_root_or_engine_version")
    normalized["binary_bound"] = not missing
    normalized["missing_fields"] = missing
    return normalized


def collect_source_input(
    run_root: Path,
    case_name: str,
    run_scope: dict | None,
    summary: dict | None,
    report: dict,
    report_path: Path,
    *,
    allow_explicit: bool = True,
) -> dict:
    run_scope = run_scope or {}
    summary = summary or {}
    if allow_explicit:
        explicit = first_dict(
            report,
            summary,
            run_scope,
            keys=("historical_best_source_input", "source_input", "same_source_input"),
        )
    else:
        explicit = {}

    if explicit:
        payload = dict(explicit)
    else:
        case_input = find_case_input(run_scope, case_name)
        reference_context = report.get("reference_context") or {}
        source_signature = (
            report.get("source_signature")
            or reference_context.get("source_signature")
            or case_input.get("source_signature")
            or {}
        )
        effective_reference_signature = (
            report.get("effective_reference_signature")
            or reference_context.get("effective_reference_signature")
            or {}
        )
        baseline_compare_signature = (
            report.get("baseline_compare_report_signature")
            or report.get("baseline_compare_report_hash")
            or {}
        )
        payload = {
            "case": case_name,
            "source_authority": case_input.get("mode")
            or report.get("effective_reference_provider")
            or report.get("reference_provider")
            or reference_context.get("canonical_source")
            or "",
            "source_design_artifact": case_input.get("source") or report.get("source_path") or "",
            "source_design_artifact_hash": signature_hash(source_signature),
            "reference_root_case_path": report.get("reference_root_case_path")
            or reference_context.get("reference_root_case_path")
            or "",
            "reference_image_hash": signature_hash(effective_reference_signature),
            "baseline_compare_report_hash": signature_hash(baseline_compare_signature),
            "validation_project_path": run_scope.get("validation_project_path")
            or summary.get("validation_project_path")
            or "",
            "run_scope_case_names": list(run_scope.get("case_names") or []),
            "run_scope_include_pattern": run_scope.get("include_pattern") or "",
        }
    payload.setdefault("case", case_name)
    payload.setdefault("report_path", str(report_path))
    payload.setdefault("run_root", str(run_root))
    return payload


def same_source_input(left: dict, right: dict) -> tuple[bool, list[dict]]:
    mismatches = []
    for field in SOURCE_INPUT_COMPARE_FIELDS:
        left_has = field in left and left.get(field) not in (None, "", [])
        right_has = field in right and right.get(field) not in (None, "", [])
        if not left_has or not right_has or left.get(field) != right.get(field):
            mismatches.append(
                {
                    "field": field,
                    "left": left.get(field),
                    "right": right.get(field),
                }
            )
    return not mismatches, mismatches
