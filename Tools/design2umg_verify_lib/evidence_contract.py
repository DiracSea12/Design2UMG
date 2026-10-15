from pathlib import Path
import json
import re


SOURCE_INPUT_COMPARE_FIELDS = (
    "engine_version",
    "rhi_backend",
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


def engine_version_from_evidence(payload: dict) -> str:
    """Resolve one UE minor version from the bound report and its runtime logs."""
    payloads = [payload]
    report_path = payload.get("report_path")
    if report_path:
        try:
            report = json.loads(Path(report_path).read_text(encoding="utf-8-sig"))
            if isinstance(report, dict):
                payloads.append(report)
        except (OSError, ValueError):
            pass
    versions = set()
    for item in payloads:
        for version in (item.get("engine_version"), (item.get("binary_binding") or {}).get("engine_version"),
                        (item.get("source_input") or {}).get("engine_version")):
            if version:
                match = re.fullmatch(r"(\d+\.\d+)(?:\.\d+)?(?:-[^\s]+)?", str(version))
                if not match:
                    return ""
                versions.add(match.group(1))
        chain = item.get("render_chain") or {}
        for mode in ("generation", "asset_render"):
            log_path = (chain.get(mode) or {}).get("log_path")
            if not log_path:
                continue
            try:
                with Path(log_path).open(encoding="utf-8-sig", errors="replace") as log:
                    for line in log:
                        match = re.search(r"LogInit: Engine Version: (\d+\.\d+)\.", line)
                        if match:
                            versions.add(match.group(1))
                            break
            except OSError:
                pass
    return next(iter(versions)) if len(versions) == 1 else ""


def rhi_identity_from_evidence(payload: dict) -> tuple[str, str]:
    """Resolve the backend and feature level confirmed by every bound render process."""
    payloads = [payload]
    if payload.get("report_path"):
        try:
            report = json.loads(Path(payload["report_path"]).read_text(encoding="utf-8-sig"))
            if isinstance(report, dict):
                payloads.append(report)
        except (OSError, ValueError):
            pass
    declared = set()
    declared_features = set()
    identities = set()
    for item in payloads:
        for backend in (item.get("rhi_backend"), (item.get("source_input") or {}).get("rhi_backend")):
            if backend:
                declared.add(str(backend))
        for feature in (item.get("rhi_feature_level"), (item.get("source_input") or {}).get("rhi_feature_level")):
            if feature:
                declared_features.add(str(feature))
        for mode in ("generation", "asset_render"):
            log_path = ((item.get("render_chain") or {}).get(mode) or {}).get("log_path")
            if not log_path:
                continue
            confirmed = False
            try:
                with Path(log_path).open(encoding="utf-8-sig", errors="replace") as log:
                    for line in log:
                        match = re.search(r"LogRHI: RHI (\w+) with Feature Level (\w+) is supported and will be used\.", line)
                        if not match:
                            match = re.search(
                                r"Log(\w+)RHI: (?:Display: )?Creating \1 RHI with Max Feature Level (\w+)", line
                            )
                        if match:
                            identities.add((match.group(1), match.group(2)))
                            confirmed = True
                            break
            except OSError:
                return "", ""
            if not confirmed:
                return "", ""
    if len(identities) == 1:
        backend, feature = next(iter(identities))
        if (not declared or declared == {backend}) and (not declared_features or declared_features == {feature}):
            return backend, feature
    return "", ""


def rhi_backend_from_evidence(payload: dict) -> str:
    return rhi_identity_from_evidence(payload)[0]


def rhi_feature_level_from_evidence(payload: dict) -> str:
    return rhi_identity_from_evidence(payload)[1]


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
    payload["engine_version"] = engine_version_from_evidence(report)
    payload["rhi_backend"] = rhi_backend_from_evidence(report)
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
