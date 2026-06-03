import argparse
import base64
import csv
import glob
import json
import math
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

import design2umg_rebuild_admissible_best as rebuild_best
import design2umg_validate_run as validate_run_tool
from design2umg_verify_lib.json_io import content_file_signature as helper_content_file_signature
from design2umg_verify_lib.diagnostics import EvidenceReadError
from design2umg_verify_lib.diagnostics import emit_diagnostic
from design2umg_verify_lib.editor_driver_writer import write_editor_driver_script
from design2umg_verify_lib.json_io import load_json as helper_load_json
from design2umg_verify_lib.json_io import write_json as helper_write_json
from design2umg_verify_lib.run_history import write_historical_best_validation
from PIL import Image, ImageChops, ImageFilter, ImageStat


STABLE_VISUAL_FILTER = "closing_max3_min3"
REFERENCE_ASPECT_COMPATIBILITY_THRESHOLD = 0.08
RUNTIME_PROJECT_ENV_VAR = "DESIGN2UMG_RUNTIME_PROJECT"
UNREAL_EDITOR_ENV_VAR = "DESIGN2UMG_UNREAL_EDITOR"
UE_ROOT_ENV_VAR = "UE_ROOT"
PENCIL_EXPORT_TOOL_TIMEOUT_SEC = 60.0


def parse_numeric_dimension(value):
    if isinstance(value, (int, float)):
        return float(value)
    return None


def load_json_document(path: Path):
    return helper_load_json(path)


_COMPARE_IMAGE_CACHE: dict[tuple, dict] = {}


def _path_signature(path: Path) -> tuple[str, int, int] | None:
    try:
        stat = path.stat()
    except OSError:
        return None
    return (str(path.resolve()), stat.st_mtime_ns, stat.st_size)


def extract_compare_metrics(report: dict | None) -> dict | None:
    if not isinstance(report, dict):
        return None

    metrics = {
        "size_match": report.get("size_match"),
        "reference_size": report.get("reference_size"),
        "candidate_size": report.get("candidate_size"),
        "changed_ratio": report.get("changed_ratio"),
        "stable_changed_ratio": report.get("stable_changed_ratio"),
        "mean_abs_error": report.get("mean_abs_error"),
        "stable_mean_abs_error": report.get("stable_mean_abs_error"),
        "diff_bbox": report.get("diff_bbox"),
        "stable_diff_bbox": report.get("stable_diff_bbox"),
    }
    if all(metrics.get(metric_name) is None for metric_name in ("changed_ratio", "stable_changed_ratio", "mean_abs_error", "stable_mean_abs_error")):
        return None
    return metrics


def sanitize_case_name(value: str) -> str:
    normalized = (value or "").strip().lower()
    normalized = re.sub(r"[^a-z0-9]+", "-", normalized).strip("-")
    return normalized or "unnamed"


def build_automation_asset_root(output_dir: Path, game_output_base: str = "", case_name: str = "") -> str:
    normalized_base = (game_output_base or "").strip().rstrip("/")
    if normalized_base:
        return normalized_base

    case_segment = sanitize_case_name(case_name or output_dir.name)
    return f"/Game/Design2UMG/Automation/{case_segment}"


def build_automation_phase_output(game_output_root: str, phase_name: str) -> str:
    normalized_root = (game_output_root or "").strip().rstrip("/")
    phase_segment = sanitize_case_name(phase_name)
    if not normalized_root:
        raise ValueError("game_output_root must not be empty")
    if not phase_segment:
        return normalized_root
    return f"{normalized_root}/{phase_segment}"


def find_node_by_id(nodes, target_id):
    if not target_id:
        return None
    queue = list(nodes or [])
    while queue:
        node = queue.pop(0)
        if node.get("id") == target_id:
            return node
        queue[0:0] = node.get("children", [])
    return None


def parse_padding_box(value) -> tuple[float, float, float, float]:
    def as_number(item) -> float:
        parsed = parse_numeric_dimension(item)
        return parsed if parsed is not None else 0.0

    if isinstance(value, list):
        items = [as_number(item) for item in value]
        if len(items) == 1:
            return items[0], items[0], items[0], items[0]
        if len(items) == 2:
            return items[0], items[1], items[0], items[1]
        if len(items) == 3:
            return items[0], items[1], items[2], items[1]
        if len(items) >= 4:
            return items[0], items[1], items[2], items[3]

    padding = as_number(value)
    return padding, padding, padding, padding


def resolve_node_width(node: dict, available_width: float | None = None) -> float | None:
    width = parse_numeric_dimension(node.get("width"))
    if width and width > 1:
        return width

    if isinstance(node.get("width"), str) and node.get("width") == "fill_container" and available_width and available_width > 1:
        return available_width

    size = node.get("size")
    if isinstance(size, dict):
        width = parse_numeric_dimension(size.get("x"))
        if width and width > 1:
            return width

    bbox = node.get("absoluteBoundingBox")
    if isinstance(bbox, dict):
        width = parse_numeric_dimension(bbox.get("width"))
        if width and width > 1:
            return width

    return None


def resolve_node_height(node: dict) -> float | None:
    height = parse_numeric_dimension(node.get("height"))
    if height and height > 1:
        return height

    size = node.get("size")
    if isinstance(size, dict):
        height = parse_numeric_dimension(size.get("y"))
        if height and height > 1:
            return height

    bbox = node.get("absoluteBoundingBox")
    if isinstance(bbox, dict):
        height = parse_numeric_dimension(bbox.get("height"))
        if height and height > 1:
            return height

    return None


def estimate_text_node_size(node: dict, available_width: float | None = None) -> tuple[float, float] | None:
    if node.get("type") not in {"text", "icon_font"}:
        return None

    width = resolve_node_width(node, available_width)
    height = resolve_node_height(node)
    if width and height:
        return width, height

    font_size = parse_numeric_dimension(node.get("fontSize")) or 12.0
    line_height = parse_numeric_dimension(node.get("lineHeight"))
    if line_height is None:
        line_height = 1.5
    if line_height <= 4.0:
        estimated_height = font_size * line_height
    else:
        estimated_height = line_height

    content = str(node.get("content") or node.get("iconFontName") or "")
    estimated_width = max(font_size, len(content) * font_size * 0.56)

    return width or estimated_width, height or estimated_height


def extract_node_numeric_size(node: dict, available_width: float | None = None) -> tuple[float | None, float | None]:
    if not isinstance(node, dict):
        return None, None

    width = resolve_node_width(node, available_width)
    height = resolve_node_height(node)
    if width and height and width > 1 and height > 1:
        return width, height

    return width, height


def infer_node_size_from_children(node: dict, available_width: float | None = None) -> tuple[int, int] | None:
    children = node.get("children", [])
    if not isinstance(children, list) or not children:
        text_size = estimate_text_node_size(node, available_width)
        if text_size:
            return math.ceil(text_size[0]), math.ceil(text_size[1])
        return None

    width, _ = extract_node_numeric_size(node, available_width)
    top, right, bottom, left = parse_padding_box(node.get("padding"))
    inner_width = max(0.0, width - left - right) if width and width > 1 else available_width
    gap = parse_numeric_dimension(node.get("gap")) or 0.0
    layout = str(node.get("layout") or "").lower()

    child_sizes: list[tuple[float, float, float, float]] = []
    for child in children:
        if not isinstance(child, dict):
            continue
        child_size = extract_node_size(child, inner_width)
        if not child_size:
            continue
        child_x = parse_numeric_dimension(child.get("x")) or 0.0
        child_y = parse_numeric_dimension(child.get("y")) or 0.0
        child_sizes.append((child_x, child_y, float(child_size[0]), float(child_size[1])))

    if not child_sizes:
        return None

    if layout in {"vertical", "v", "column"}:
        content_width = max(child_width for _, _, child_width, _ in child_sizes)
        content_height = sum(child_height for _, _, _, child_height in child_sizes)
        content_height += gap * max(0, len(child_sizes) - 1)
        inferred_width = left + content_width + right
        inferred_height = top + content_height + bottom
    elif layout in {"horizontal", "h", "row"}:
        content_width = sum(child_width for _, _, child_width, _ in child_sizes)
        content_width += gap * max(0, len(child_sizes) - 1)
        content_height = max(child_height for _, _, _, child_height in child_sizes)
        inferred_width = left + content_width + right
        inferred_height = top + content_height + bottom
    else:
        max_right = max(child_x + child_width for child_x, _, child_width, _ in child_sizes)
        max_bottom = max(child_y + child_height for _, child_y, _, child_height in child_sizes)
        inferred_width = left + max_right + right
        inferred_height = top + max_bottom + bottom

    if inferred_width <= 1 or inferred_height <= 1:
        return None
    return math.ceil(inferred_width), math.ceil(inferred_height)


def extract_node_size(node: dict, available_width: float | None = None) -> tuple[int, int] | None:
    if not isinstance(node, dict):
        return None

    width, height = extract_node_numeric_size(node, available_width)
    inferred = infer_node_size_from_children(node, available_width)
    if inferred:
        inferred_width, inferred_height = inferred
        if not width or width <= 1:
            width = inferred_width
        if not height or height <= 1:
            height = inferred_height

    if width and height and width > 1 and height > 1:
        return math.ceil(width), math.ceil(height)

    return None


def extract_declared_node_size(node: dict) -> tuple[int, int] | None:
    width, height = extract_node_numeric_size(node)
    if width and height and width > 1 and height > 1:
        return math.ceil(width), math.ceil(height)
    return None


def get_expected_design_size(mode: str, source_path: str, node_id: str) -> tuple[int, int] | None:
    source = Path(source_path)
    if not source.is_file():
        return None
    try:
        document = load_json_document(source)
    except EvidenceReadError as exc:
        emit_diagnostic("warning", "expected_design_size_source_unreadable", exc.path, exc.detail)
        return None

    if mode == "pen" and source.suffix.lower() == ".pen":
        target_node = find_node_by_id(document.get("children", []), node_id)
        return extract_node_size(target_node)

    if mode in {"local-figma", "figma-source"} and source.suffix.lower() == ".json":
        normalized_node_id = parse_figma_node_id(node_id)
        root = document.get("document", {})
        target_node = find_node_by_id(root.get("children", []), normalized_node_id)
        return extract_node_size(target_node)

    return None


def validate_reference_against_design(reference_size: tuple[int, int], expected_size: tuple[int, int] | None) -> dict:
    if not expected_size:
        return {
            "available": False,
            "valid": False,
            "reason": "expected_design_size_unavailable",
            "expected_size": None,
            "reference_size": list(reference_size),
            "scale_x": None,
            "scale_y": None,
            "aspect_delta": None,
        }

    expected_width, expected_height = expected_size
    reference_width, reference_height = reference_size
    if expected_width <= 0 or expected_height <= 0 or reference_width <= 0 or reference_height <= 0:
        return {
            "available": True,
            "valid": False,
            "reason": "invalid_dimensions",
            "expected_size": [expected_width, expected_height],
            "reference_size": [reference_width, reference_height],
            "scale_x": None,
            "scale_y": None,
            "aspect_delta": None,
        }

    scale_x = reference_width / expected_width
    scale_y = reference_height / expected_height
    expected_aspect = expected_width / expected_height
    reference_aspect = reference_width / reference_height
    aspect_delta = abs(expected_aspect - reference_aspect) / max(expected_aspect, reference_aspect)
    valid = aspect_delta <= REFERENCE_ASPECT_COMPATIBILITY_THRESHOLD
    reason = "" if valid else "reference_aspect_mismatch_design"
    return {
        "available": True,
        "valid": valid,
        "reason": reason,
        "expected_size": [expected_width, expected_height],
        "reference_size": [reference_width, reference_height],
        "scale_x": scale_x,
        "scale_y": scale_y,
        "aspect_delta": aspect_delta,
    }


def resolve_authoritative_design_size(ue_result: dict | None) -> tuple[int, int] | None:
    ue_result = ue_result or {}
    width = parse_numeric_dimension(ue_result.get("design_time_width"))
    height = parse_numeric_dimension(ue_result.get("design_time_height"))
    if width and height and width > 1 and height > 1:
        return math.ceil(width), math.ceil(height)

    return None


def apply_render_size_authority(
    reference_context: dict,
    reference_path: Path,
    ue_result: dict | None,
    candidate_path: Path | None,
) -> dict:
    design_size = resolve_authoritative_design_size(ue_result)
    reference_validation = validate_reference_against_design(read_image_size(reference_path), design_size)
    candidate_size = read_image_size(candidate_path) if candidate_path and candidate_path.is_file() else None
    candidate_size_valid = bool(design_size and candidate_size and tuple(candidate_size) == tuple(design_size))
    if not candidate_size_valid:
        reference_validation["valid"] = False
        reference_validation["reason"] = "candidate_size_mismatch_design_time_size" if design_size else "design_time_size_unavailable"
    reference_validation["candidate_size"] = list(candidate_size) if candidate_size else None
    reference_validation["candidate_size_matches_design_time"] = candidate_size_valid
    reference_context["reference_validation"] = reference_validation
    reference_context["render_size_authority"] = {
        "source": "ue_blueprint_design_time_size",
        "reason": "source design size defines render output; reference PNG size is compare-only",
        "design_size": list(design_size) if design_size else None,
        "candidate_size": list(candidate_size) if candidate_size else None,
        "candidate_size_matches_design_time": candidate_size_valid,
        "requested_render_size": None,
    }
    return reference_validation


def parse_args():
    parser = argparse.ArgumentParser(description="Design2UMG full automation visual verification")
    parser.add_argument("mode", choices=["pen", "local-figma", "figma-source", "asset"], help="Verification mode")
    parser.add_argument("--source", required=True, help="Source file path or Widget Blueprint asset path")
    parser.add_argument("--node", default="", help="Target frame/node id")
    parser.add_argument("--reference-provider", choices=["png", "figma", "pencil"], required=True)
    parser.add_argument("--reference", default="", help="Existing reference png path when provider=png")
    parser.add_argument("--figma-file", default="", help="Figma file key or URL")
    parser.add_argument("--figma-node", default="", help="Figma node id")
    parser.add_argument("--figma-token", default="", help="Figma personal access token from a secure source")
    parser.add_argument("--mcp-server", default="", help="Override Pencil MCP server binary path")
    parser.add_argument("--pencil-app", default="", help="Override Pencil app name. Leave empty for auto-detect from ~/.pencil/apps and running IDE processes.")
    parser.add_argument("--output-dir", default="", help="Directory for render, diff and reports")
    parser.add_argument("--game-output", default="", help="UE asset output base path, default is a stable per-case automation path")
    parser.add_argument("--project", default="", help=f"Runtime project path. If omitted, {RUNTIME_PROJECT_ENV_VAR} is required. HostProject fallback and source-path inference are not supported.")
    parser.add_argument("--editor", default="", help=f"UnrealEditor executable path. If omitted, use {UNREAL_EDITOR_ENV_VAR}, or {UE_ROOT_ENV_VAR} to resolve Engine/Binaries/Win64/UnrealEditor.exe.")
    parser.add_argument("--dry-run-validate-paths", action="store_true", help="Resolve and validate the runtime project/editor paths, then exit before launching Unreal.")
    parser.add_argument("--pixel-tolerance", type=int, default=6, help="Per-pixel max-channel tolerance")
    parser.add_argument("--idle-timeout-sec", type=int, default=600, help="Kill the editor if no log or result progress is observed for this many seconds")
    parser.add_argument("--startup-timeout-sec", type=int, default=90, help="Kill the editor if UnrealEditor shows zero progress right after launch for this many seconds")
    parser.add_argument("--log-cmds", default="", help="Optional Unreal -LogCmds payload, for example 'LogDesign2UMG Verbose'")
    return parser.parse_args()


def plugin_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_output_dir() -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return plugin_root() / "Saved" / "Design2UMG" / "Automation" / f"{stamp}_{os.getpid()}"


def contains_pluginbuild_segment(path: Path) -> bool:
    normalized_parts = {part.lower() for part in path.parts}
    return "_pluginbuild" in normalized_parts


def is_host_project_path(path: Path) -> bool:
    normalized_parts = {part.lower() for part in path.parts}
    return "hostproject" in normalized_parts or path.name.lower() == ("hostproject" + ".uproject")


def validate_runtime_project_path(project_path: Path, source_label: str) -> Path:
    resolved_project = project_path.resolve()
    if resolved_project.suffix.lower() != ".uproject":
        raise RuntimeError(f"Runtime project from {source_label} is not a .uproject: {resolved_project}")
    if contains_pluginbuild_segment(resolved_project):
        raise RuntimeError(
            "Refusing to run verification against a repository-local _PluginBuild project. "
            f"Use a real runtime project instead: {resolved_project}"
        )
    if is_host_project_path(resolved_project):
        raise RuntimeError(
            "HostProject fallback has been removed. "
            f"Refusing temporary verification project: {resolved_project}"
        )
    if not resolved_project.is_file():
        raise RuntimeError(f"Runtime project from {source_label} does not exist: {resolved_project}")
    return resolved_project


def discover_runtime_project(source_hints: list[str] | None = None) -> Path:
    environment_project = os.environ.get(RUNTIME_PROJECT_ENV_VAR, "").strip()
    if environment_project:
        return validate_runtime_project_path(Path(environment_project), f"{RUNTIME_PROJECT_ENV_VAR} environment variable")

    raise RuntimeError(
        "Runtime project path not provided. "
        f"Pass --project explicitly or set {RUNTIME_PROJECT_ENV_VAR}. "
        "HostProject fallback and source-path inference are not supported."
    )


def prepare_runtime_project(args, source_hints: list[str] | None = None) -> tuple[str, Path]:
    if getattr(args, "build_root", "") or getattr(args, "keep_build_root", False):
        print(
            "[WARN] --build-root/--keep-build-root are ignored because HostProject fallback has been removed.",
            file=sys.stderr,
        )

    requested_project = (
        validate_runtime_project_path(Path(args.project).expanduser(), "--project")
        if getattr(args, "project", "")
        else discover_runtime_project(source_hints)
    )
    requested_plugin_root = requested_project.parent / "Plugins" / "Design2UMG"
    project_plugin_root = requested_plugin_root if requested_plugin_root.is_dir() else plugin_root()
    return str(requested_project), project_plugin_root


def resolve_unreal_editor_path(raw_editor_path: str) -> str:
    editor_source = "--editor"
    candidate = (raw_editor_path or "").strip()
    if not candidate:
        candidate = os.environ.get(UNREAL_EDITOR_ENV_VAR, "").strip()
        editor_source = f"{UNREAL_EDITOR_ENV_VAR} environment variable"

    if not candidate:
        ue_root = os.environ.get(UE_ROOT_ENV_VAR, "").strip()
        if ue_root:
            candidate = str(Path(ue_root) / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe")
            editor_source = f"{UE_ROOT_ENV_VAR} environment variable"

    if not candidate:
        raise RuntimeError(
            "UnrealEditor path not provided. "
            f"Pass --editor explicitly, set {UNREAL_EDITOR_ENV_VAR}, or set {UE_ROOT_ENV_VAR}."
        )

    editor = Path(candidate).expanduser().resolve()
    if not editor.is_file():
        raise RuntimeError(f"UnrealEditor from {editor_source} does not exist: {editor}")
    return str(editor)


def sync_project_plugin_build_id(editor_path: str, project_plugin_root: Path):
    editor = Path(editor_path).resolve()
    engine_root = editor.parents[2]
    project_manifest = project_plugin_root / "Binaries" / "Win64" / "UnrealEditor.modules"
    engine_manifest = engine_root / "Plugins" / "Marketplace" / "Design2UMG" / "Binaries" / "Win64" / "UnrealEditor.modules"
    if not project_manifest.is_file() or not engine_manifest.is_file():
        return
    project_data = json.loads(project_manifest.read_text(encoding="utf-8-sig"))
    engine_data = json.loads(engine_manifest.read_text(encoding="utf-8-sig"))
    engine_build_id = engine_data.get("BuildId", "")
    if not engine_build_id:
        return
    if project_data.get("BuildId") == engine_build_id:
        return
    project_data["BuildId"] = engine_build_id
    project_manifest.write_text(json.dumps(project_data, ensure_ascii=False, indent=4), encoding="utf-8")


def discover_local_font_seed_dirs(source_paths: list[str], destination_dir: Path | None = None) -> list[Path]:
    discovered: list[Path] = []
    seen: set[Path] = set()
    resolved_destination = destination_dir.resolve() if destination_dir else None

    for raw_source in source_paths:
        if not raw_source:
            continue

        source_path = Path(raw_source)
        if not source_path.is_absolute():
            continue
        if not source_path.exists():
            continue

        candidate_dirs: list[Path] = []
        if source_path.is_file():
            for parent in source_path.parents:
                candidate_dirs.append(parent / "Content" / "Design2UMG" / "Fonts")
                if parent.name.lower() == "designs":
                    project_root = parent.parent
                    candidate_dirs.append(project_root / "Content" / "Design2UMG" / "Fonts")
                    break
        else:
            candidate_dirs.append(source_path / "Content" / "Design2UMG" / "Fonts")

        for candidate_dir in candidate_dirs:
            if not candidate_dir.is_dir():
                continue
            resolved_dir = candidate_dir.resolve()
            if resolved_destination and resolved_dir == resolved_destination:
                continue
            if resolved_dir in seen:
                continue
            seen.add(resolved_dir)
            discovered.append(resolved_dir)

    return discovered


def seed_runtime_font_assets(project_path: str, source_paths: list[str]) -> dict:
    runtime_project = Path(project_path).resolve()
    destination_dir = runtime_project.parent / "Content" / "Design2UMG" / "Fonts"
    source_dirs = discover_local_font_seed_dirs(source_paths, destination_dir)
    result = {
        "destination_dir": str(destination_dir),
        "source_dirs": [str(path) for path in source_dirs],
        "selected_source_dir": "",
        "copied": [],
    }

    if not source_dirs:
        return result

    def score_source_dir(path: Path) -> tuple[int, int]:
        files = [item for item in path.iterdir() if item.is_file()]
        return len(files), sum(item.stat().st_size for item in files)

    selected_source_dir = max(source_dirs, key=score_source_dir)
    result["selected_source_dir"] = str(selected_source_dir)
    destination_dir.mkdir(parents=True, exist_ok=True)
    for existing_path in destination_dir.iterdir():
        if existing_path.is_file():
            existing_path.unlink()

    for asset_path in sorted(item for item in selected_source_dir.iterdir() if item.is_file()):
        destination_path = destination_dir / asset_path.name
        shutil.copy2(asset_path, destination_path)
        if destination_path.is_file():
            result["copied"].append(asset_path.name)

    return result


def ensure_clean_editor():
    for image_name in ("UnrealEditor.exe", "UnrealEditor-Cmd.exe"):
        subprocess.run(["taskkill", "/F", "/IM", image_name], check=False, capture_output=True, text=True)


def normalize_node_id(node_id: str) -> str:
    return node_id.replace("-", ":")


def parse_figma_file_key(value: str) -> str:
    if not value:
        return value
    match = re.search(r"figma\.com/(?:design|file)/([^/?]+)/", value)
    return match.group(1) if match else value


def parse_figma_node_id(value: str) -> str:
    if not value:
        return value
    match = re.search(r"node-id=([0-9]+)[-:]([0-9]+)", value)
    if not match:
        return value.replace("-", ":")
    return f"{match.group(1)}:{match.group(2)}"


def http_json(url: str, headers: dict[str, str]) -> dict:
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def download_file(url: str, output_path: Path, headers: dict[str, str] | None = None):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(request, timeout=60) as response, open(output_path, "wb") as handle:
        shutil.copyfileobj(response, handle)


def prepare_png_reference(reference_path: str, output_path: Path) -> Path:
    src = Path(reference_path)
    if not src.is_file():
        raise FileNotFoundError(f"Reference PNG not found: {reference_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, output_path)
    return output_path


def prepare_figma_reference(file_value: str, node_value: str, token: str, output_path: Path) -> Path:
    file_key = parse_figma_file_key(file_value)
    node_id = parse_figma_node_id(node_value)
    if not file_key or not node_id or not token:
        raise ValueError("Figma reference export requires --figma-file, --figma-node and --figma-token")
    query = urllib.parse.urlencode({"ids": node_id, "format": "png", "scale": "2"})
    meta = http_json(
        f"https://api.figma.com/v1/images/{file_key}?{query}",
        {"X-Figma-Token": token},
    )
    image_url = meta.get("images", {}).get(node_id)
    if not image_url:
        raise RuntimeError(f"Figma did not return an export URL for node {node_id}")
    download_file(image_url, output_path)
    return output_path


def discover_mcp_server(override_path: str) -> str:
    if override_path:
        return override_path
    user_home = Path(os.environ.get("USERPROFILE", ""))
    search_dirs = [
        user_home / ".vscode" / "extensions",
        user_home / ".antigravity" / "extensions",
    ]
    all_matches = []
    for base_dir in search_dirs:
        pattern = str(base_dir / "highagency.pencildev-*" / "out" / "mcp-server-windows-x64.exe")
        all_matches.extend(glob.glob(pattern))
    if all_matches:
        def version_key(path_str: str):
            version_text = Path(path_str).parents[1].name.removeprefix("highagency.pencildev-")
            parts = []
            for token in version_text.split("."):
                try:
                    parts.append(int(token))
                except ValueError:
                    parts.append(-1)
            return tuple(parts)

        return max(all_matches, key=version_key)
    raise FileNotFoundError("Pencil MCP server binary not found; use --mcp-server to override")


PENCIL_APP_PROCESS_CANDIDATES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("visual_studio_code", ("Code.exe", "code")),
    ("vscode", ("Code.exe", "code")),
    ("cursor", ("Cursor.exe", "cursor")),
    ("antigravity", ("Antigravity.exe", "antigravity")),
    ("codium", ("VSCodium.exe", "codium")),
)


def read_pencil_registered_apps() -> set[str]:
    user_home = Path(os.environ.get("USERPROFILE", ""))
    apps_dir = user_home / ".pencil" / "apps"
    if not apps_dir.is_dir():
        return set()
    return {path.name.lower() for path in apps_dir.iterdir() if path.is_file()}


def is_windows_process_running(process_names: tuple[str, ...]) -> bool:
    for process_name in process_names:
        try:
            result = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {process_name}"],
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError:
            continue
        if process_name.lower() in result.stdout.lower():
            return True
    return False


def list_windows_task_rows(filter_expression: str, *, verbose: bool) -> list[dict[str, str]]:
    if os.name != "nt":
        return []
    args = ["tasklist", "/fo", "csv"]
    if verbose:
        args.append("/v")
    args.extend(["/fi", filter_expression])
    try:
        result = subprocess.run(
            args,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return []
    if result.returncode != 0 or not result.stdout.strip():
        return []
    return list(csv.DictReader(result.stdout.splitlines()))


def get_foreground_window_process_name() -> str:
    if os.name != "nt":
        return ""
    try:
        import ctypes

        hwnd = ctypes.windll.user32.GetForegroundWindow()
        if not hwnd:
            return ""
        process_id = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
        if not process_id.value:
            return ""
        rows = list_windows_task_rows(f"PID eq {process_id.value}", verbose=False)
        if not rows:
            return ""
        return ((rows[0].get("Image Name", "") or "").strip().lower())
    except Exception:
        return ""


def title_looks_like_pencil_document_window(title: str, preferred_document_path: str = "") -> bool:
    lowered_title = (title or "").strip().lower()
    if not lowered_title:
        return False

    preferred_document_name = Path(preferred_document_path).name.strip().lower()
    if preferred_document_name and preferred_document_name in lowered_title:
        return True

    return ".pen" in lowered_title


def describe_pencil_app_runtime_observations(preferred_document_path: str = "") -> dict[str, dict]:
    target_document_name = Path(preferred_document_path).name.strip().lower()
    foreground_process_name = get_foreground_window_process_name()
    rows_by_process_name: dict[str, list[dict[str, str]]] = {}
    for _, process_names in PENCIL_APP_PROCESS_CANDIDATES:
        for process_name in process_names:
            normalized_process_name = process_name.lower()
            if normalized_process_name not in rows_by_process_name:
                rows_by_process_name[normalized_process_name] = [
                    row
                    for row in list_windows_task_rows(f"IMAGENAME eq {process_name}", verbose=True)
                    if (row.get("Image Name", "") or "").strip().lower() == normalized_process_name
                ]

    observations: dict[str, dict] = {}
    for app_name, process_names in PENCIL_APP_PROCESS_CANDIDATES:
        rows: list[dict[str, str]] = []
        for process_name in process_names:
            rows.extend(rows_by_process_name.get(process_name.lower(), []))
        window_titles = []
        for row in rows:
            title = (row.get("Window Title", "") or "").strip()
            if not title or title.upper() == "N/A" or title == "OleMainThreadWndName":
                continue
            window_titles.append(title)
        lowered_titles = [title.lower() for title in window_titles]
        observations[app_name] = {
            "running": bool(rows),
            "foreground": any(process_name.lower() == foreground_process_name for process_name in process_names),
            "document_match": bool(target_document_name and any(target_document_name in title for title in lowered_titles)),
            "compatible_window": any(
                title_looks_like_pencil_document_window(title, preferred_document_path)
                for title in window_titles
            ),
            "window_titles": window_titles,
        }
    return observations


def order_pencil_app_candidates(
    available_apps: set[str],
    running_apps: set[str],
    *,
    preferred_apps: list[str] | None = None,
    foreground_compatible_apps: list[str] | None = None,
    compatible_window_apps: list[str] | None = None,
    foreground_apps: list[str] | None = None,
    windowed_apps: list[str] | None = None,
) -> list[str]:
    ordered: list[str] = []
    seen: set[str] = set()

    def append_candidates(candidates: list[str]):
        for app_name in candidates:
            if app_name not in seen:
                ordered.append(app_name)
                seen.add(app_name)

    append_candidates(preferred_apps or [])
    append_candidates(foreground_compatible_apps or [])
    append_candidates(compatible_window_apps or [])
    append_candidates(foreground_apps or [])
    append_candidates(windowed_apps or [])

    for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES:
        if app_name in running_apps and app_name not in seen:
            ordered.append(app_name)
            seen.add(app_name)

    for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES:
        if app_name in available_apps and app_name not in seen:
            ordered.append(app_name)
            seen.add(app_name)

    for app_name in sorted(available_apps):
        if app_name not in seen:
            ordered.append(app_name)
            seen.add(app_name)

    if not ordered:
        ordered.append("visual_studio_code")

    return ordered


def discover_pencil_app_candidates(override_app_name: str, preferred_document_path: str = "") -> list[str]:
    if override_app_name:
        return [override_app_name]

    available_apps = read_pencil_registered_apps()
    observations = describe_pencil_app_runtime_observations(preferred_document_path)
    running_apps = {
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("running")
    }
    preferred_apps = [
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("document_match")
    ]
    foreground_compatible_apps = [
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("foreground")
        and observations.get(app_name, {}).get("compatible_window")
    ]
    compatible_window_apps = [
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("compatible_window")
    ]
    foreground_apps = [
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("foreground")
    ]
    windowed_apps = [
        app_name
        for app_name, _ in PENCIL_APP_PROCESS_CANDIDATES
        if observations.get(app_name, {}).get("window_titles")
    ]
    return order_pencil_app_candidates(
        available_apps,
        running_apps,
        preferred_apps=preferred_apps,
        foreground_compatible_apps=foreground_compatible_apps,
        compatible_window_apps=compatible_window_apps,
        foreground_apps=foreground_apps,
        windowed_apps=windowed_apps,
    )


def find_available_port(start_port: int = 18900) -> int:
    for port in range(start_port, start_port + 100):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
            return port
        except OSError:
            continue
        finally:
            sock.close()
    return start_port


def request_json(url: str, payload: dict, headers: dict[str, str], timeout_sec: float = 30.0) -> tuple[str, dict]:
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=timeout_sec) as response:
        return response.headers.get("Mcp-Session-Id", ""), response.read().decode("utf-8")


class PencilMcpClient:
    def __init__(self, server_path: str, app_name: str, preferred_document_path: str = ""):
        self.server_path = server_path
        self.app_candidates = discover_pencil_app_candidates(app_name, preferred_document_path)
        self.app_name = self.app_candidates[0]
        self.port = find_available_port()
        self.endpoint = f"http://127.0.0.1:{self.port}/mcp"
        self.session_id = ""
        self.process: subprocess.Popen | None = None

    def __enter__(self):
        errors: list[str] = []
        for candidate in self.app_candidates:
            self.app_name = candidate
            args = [self.server_path, "-app", self.app_name, "-http", "-http-port", str(self.port)]
            self.process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.time() + 10.0
            last_error = ""
            while time.time() < deadline:
                try:
                    self.connect()
                    return self
                except Exception as exc:
                    last_error = str(exc)
                    time.sleep(0.2)
            self.__exit__(None, None, None)
            errors.append(f"{candidate}: {last_error or 'failed to connect'}")
        raise RuntimeError(f"Failed to connect to Pencil MCP server at {self.endpoint} ({'; '.join(errors)})")

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def connect(self):
        headers = {
            "Content-Type": "application/json",
            "Accept": "text/event-stream",
        }
        payload = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "clientInfo": {"name": "Design2UMGAutomation", "version": "1.0.0"},
                "protocolVersion": "2024-11-05",
                "capabilities": {},
            },
        }
        session_id, _ = request_json(self.endpoint, payload, headers)
        if not session_id:
            raise RuntimeError("MCP initialize did not return Mcp-Session-Id")
        self.session_id = session_id
        headers["Mcp-Session-Id"] = self.session_id
        request_json(
            self.endpoint,
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            headers,
        )

    def call_tool_content(self, name: str, arguments: dict, timeout_sec: float = 30.0) -> list[dict]:
        headers = {
            "Content-Type": "application/json",
            "Accept": "text/event-stream",
            "Mcp-Session-Id": self.session_id,
        }
        payload = {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments},
        }
        _, body = request_json(self.endpoint, payload, headers, timeout_sec=timeout_sec)
        data_lines = [line[5:].strip() for line in body.splitlines() if line.startswith("data:")]
        content = data_lines[-1] if data_lines else body
        rpc = json.loads(content)
        if "error" in rpc:
            message = rpc["error"].get("message", f"Tool call failed: {name}")
            raise RuntimeError(message)
        result = rpc.get("result", {})
        parts = result.get("content", [])
        return parts

    def call_tool(self, name: str, arguments: dict, timeout_sec: float = 30.0):
        parts = self.call_tool_content(name, arguments, timeout_sec=timeout_sec)
        if not parts:
            return ""
        return parts[0].get("text", "")


def extract_png_paths_from_tool_result(tool_result: str, output_dir: Path) -> list[Path]:
    candidate_paths = []
    try:
        parsed = json.loads(tool_result)
        if isinstance(parsed, list):
            candidate_paths = [Path(p) for p in parsed]
        elif isinstance(parsed, dict):
            candidate_paths = [Path(p) for p in parsed.get("files", [])]
    except json.JSONDecodeError:
        candidate_paths = [Path(match) for match in re.findall(r"[A-Za-z]:\\[^\\n\\r\"]+?\\.png", tool_result)]
    if not candidate_paths:
        candidate_paths = sorted(output_dir.glob("*.png"))
    return candidate_paths


def get_active_pencil_document(client: PencilMcpClient) -> str:
    try:
        parts = client.call_tool_content("get_editor_state", {"include_schema": False})
    except Exception:
        return ""
    if not parts:
        return ""
    text = parts[0].get("text", "")
    match = re.search(r"Currently active editor\s*-\s*`([^`]+)`", text, re.IGNORECASE | re.MULTILINE)
    return match.group(1) if match else ""


def normalize_local_document_path(value: str) -> str:
    normalized = (value or "").strip().replace("/", "\\")
    if re.match(r"^\\+[A-Za-z]:\\", normalized):
        normalized = normalized.lstrip("\\")
    return os.path.normcase(os.path.normpath(normalized))


def ensure_pencil_document_open(
    client: PencilMcpClient,
    source_path: str,
    *,
    force_reopen: bool = False,
    timeout_sec: float = 15.0,
    poll_interval: float = 0.5,
):
    normalized_target = normalize_local_document_path(str(Path(source_path).resolve()))
    active_document = normalize_local_document_path(get_active_pencil_document(client))
    if active_document == normalized_target and not force_reopen:
        return

    open_error: Exception | None = None
    try:
        client.call_tool_content("open_document", {"path": source_path})
    except Exception as exc:
        open_error = exc

    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        active_document = normalize_local_document_path(get_active_pencil_document(client))
        if active_document == normalized_target:
            return
        time.sleep(poll_interval)

    if open_error:
        raise RuntimeError(
            f"Pencil did not switch to document: {source_path} "
            f"(active={active_document or '<none>'}, app={client.app_name}, open_document_error={open_error})"
        ) from open_error
    raise RuntimeError(
        f"Pencil did not switch to document: {source_path} "
        f"(active={active_document or '<none>'}, app={client.app_name})"
    )


def export_pencil_reference_png(
    client: PencilMcpClient,
    source_path: str,
    node_id: str,
    output_dir: Path,
    *,
    timeout_sec: float = PENCIL_EXPORT_TOOL_TIMEOUT_SEC,
) -> Path | None:
    tool_result = ""
    export_error = None
    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        tool_result = client.call_tool(
            "export_nodes",
            {
                "filePath": source_path,
                "nodeIds": [node_id],
                "outputDir": str(output_dir),
                "format": "png",
            },
            timeout_sec=timeout_sec,
        )
    except Exception as exc:
        export_error = exc

    candidate_paths = extract_png_paths_from_tool_result(tool_result, output_dir)
    if candidate_paths:
        return candidate_paths[0]
    if export_error:
        raise RuntimeError(f"Pencil export_nodes did not produce any PNG: {export_error}") from export_error
    return None


def screenshot_pencil_reference_png(client: PencilMcpClient, source_path: str, node_id: str, output_path: Path) -> Path | None:
    screenshot_parts = client.call_tool_content(
        "get_screenshot",
        {
            "filePath": source_path,
            "nodeId": node_id,
        },
    )
    for part in screenshot_parts:
        if part.get("type") == "image" and part.get("data"):
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_bytes(base64.b64decode(part["data"]))
            return output_path
    return None


def reset_pen_authority_attempt_outputs(export_dir: Path, screenshot_path: Path) -> None:
    shutil.rmtree(export_dir, ignore_errors=True)
    try:
        screenshot_path.unlink()
    except FileNotFoundError:
        pass


def run_pen_authority_reference_attempt(
    source_path: str,
    node_id: str,
    server_path: str,
    app_name: str,
    export_dir: Path,
    screenshot_path: Path,
    *,
    force_reopen: bool = False,
) -> tuple[Path | None, Path | None, str]:
    mcp_server = discover_mcp_server(server_path)
    reset_pen_authority_attempt_outputs(export_dir, screenshot_path)

    export_error = ""
    with PencilMcpClient(mcp_server, app_name, source_path) as client:
        ensure_pencil_document_open(client, source_path, force_reopen=force_reopen)
        export_path = None
        try:
            export_path = export_pencil_reference_png(client, source_path, node_id, export_dir)
        except Exception as exc:
            export_error = str(exc)
        try:
            witness_path = screenshot_pencil_reference_png(client, source_path, node_id, screenshot_path)
        except Exception:
            witness_path = None

    return export_path, witness_path, export_error


def compare_image_paths(left_path: Path, right_path: Path, pixel_tolerance: int) -> dict | None:
    if not left_path.is_file() or not right_path.is_file():
        return None
    left_signature = _path_signature(left_path)
    right_signature = _path_signature(right_path)
    if left_signature is None or right_signature is None:
        return None
    cache_key = (left_signature, right_signature, pixel_tolerance)
    cached = _COMPARE_IMAGE_CACHE.get(cache_key)
    if cached is not None:
        return dict(cached)
    comparison = build_diff(load_rgba(left_path), load_rgba(right_path), pixel_tolerance)
    result = {
        "size_match": comparison["size_match"],
        "left_size": comparison["reference_size"],
        "right_size": comparison["candidate_size"],
        "changed_ratio": comparison["changed_ratio"],
        "stable_changed_ratio": comparison["stable_changed_ratio"],
        "mean_abs_error": comparison["mean_abs_error"],
        "stable_mean_abs_error": comparison["stable_mean_abs_error"],
        "diff_bbox": comparison["diff_bbox"],
        "stable_diff_bbox": comparison["stable_diff_bbox"],
    }
    _COMPARE_IMAGE_CACHE[cache_key] = result
    return dict(result)


def compare_scaled_image_paths(source_path: Path, witness_path: Path, pixel_tolerance: int) -> dict | None:
    if not source_path.is_file() or not witness_path.is_file():
        return None
    source_image = load_rgba(source_path)
    witness_image = load_rgba(witness_path)
    scaled_source = source_image.resize(witness_image.size, Image.Resampling.LANCZOS)
    comparison = build_diff(scaled_source, witness_image, pixel_tolerance)
    return {
        "size_match": comparison["size_match"],
        "scaled_source_size": comparison["reference_size"],
        "witness_size": comparison["candidate_size"],
        "changed_ratio": comparison["changed_ratio"],
        "stable_changed_ratio": comparison["stable_changed_ratio"],
        "mean_abs_error": comparison["mean_abs_error"],
        "stable_mean_abs_error": comparison["stable_mean_abs_error"],
        "diff_bbox": comparison["diff_bbox"],
        "stable_diff_bbox": comparison["stable_diff_bbox"],
    }


def _parse_tool_text_json(parts: list[dict]):
    if not parts:
        return None
    text = parts[0].get("text", "")
    if not text:
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def _find_pencil_node_by_id(nodes, target_id: str):
    queue = list(nodes or [])
    while queue:
        node = queue.pop(0)
        if not isinstance(node, dict):
            continue
        if node.get("id") == target_id:
            return node
        children = node.get("children")
        if isinstance(children, list):
            queue[0:0] = children
    return None


def _classify_pencil_node_kind(node: dict) -> str:
    node_type = str(node.get("type", "")).strip().lower()
    if node.get("placeholder") is True:
        return "placeholder"
    if node_type in {"frame", "group", "component", "ref"}:
        return "container"
    if node_type in {"text", "icon_font", "iconfont"}:
        return "text"
    if node_type in {"rectangle", "ellipse", "line", "polygon", "path"}:
        return "visual"
    return "other"


def _build_pencil_structure_snapshot(node: dict):
    if not isinstance(node, dict):
        return None

    stats = {
        "snapshot_type": "pencil_semantic_live",
        "root_kind": _classify_pencil_node_kind(node),
        "root_child_count": 0,
        "total_nodes": 0,
        "leaf_nodes": 0,
        "container_nodes": 0,
        "text_nodes": 0,
        "visual_nodes": 0,
        "button_nodes": 0,
        "placeholder_nodes": 0,
        "max_depth": 0,
        "visible_texts": [],
        "kind_histogram": {},
        "truncated": False,
    }

    def visit(current: dict, depth: int):
        if not isinstance(current, dict):
            return
        if current.get("enabled") is False:
            return

        node_type = str(current.get("type", "")).strip().lower()
        if node_type in {"prompt", "note", "connection", "context"}:
            return

        stats["total_nodes"] += 1
        stats["max_depth"] = max(stats["max_depth"], depth)

        kind = _classify_pencil_node_kind(current)
        stats["kind_histogram"][kind] = int(stats["kind_histogram"].get(kind, 0)) + 1
        if kind == "container":
            stats["container_nodes"] += 1
        elif kind == "text":
            stats["text_nodes"] += 1
            text = normalize_structure_text(current.get("content", "") or current.get("iconFontName", ""))
            if text:
                stats["visible_texts"].append(text)
        elif kind == "visual":
            stats["visual_nodes"] += 1
        elif kind == "placeholder":
            stats["placeholder_nodes"] += 1
            stats["visual_nodes"] += 1

        children = current.get("children")
        if depth == 0 and isinstance(children, list):
            stats["root_child_count"] = len(children)
        if children == "...":
            stats["truncated"] = True
            return
        if not isinstance(children, list) or not children:
            stats["leaf_nodes"] += 1
            return

        for child in children:
            visit(child, depth + 1)

    visit(node, 0)
    stats["visible_texts"].sort()
    return stats


def try_fetch_pencil_source_structure_snapshot(
    source_path: str,
    node_id: str,
    node_name: str,
    server_path: str,
    app_name: str,
) -> dict | None:
    if not source_path or not node_id:
        return None

    search_name = node_name or node_id
    try:
        mcp_server = discover_mcp_server(server_path)
    except Exception:
        return None

    try:
        with PencilMcpClient(mcp_server, app_name, source_path) as client:
            ensure_pencil_document_open(client, source_path)
            def fetch_parts(patterns: list[dict]) -> list[dict]:
                return client.call_tool_content(
                    "batch_get",
                    {
                        "filePath": source_path,
                        "patterns": patterns,
                        "searchDepth": 6,
                        "readDepth": 12,
                        "resolveInstances": True,
                        "resolveVariables": False,
                    },
                )

            parts = fetch_parts([{"name": f"^{re.escape(search_name)}$"}])
            parsed = _parse_tool_text_json(parts)
            target_node = _find_pencil_node_by_id(parsed, node_id) if isinstance(parsed, list) else None
            if not target_node:
                parts = fetch_parts([])
                parsed = _parse_tool_text_json(parts)
                target_node = _find_pencil_node_by_id(parsed, node_id) if isinstance(parsed, list) else None
    except Exception:
        return None

    if not isinstance(parsed, list):
        return None

    if not target_node and len(parsed) == 1 and isinstance(parsed[0], dict):
        target_node = parsed[0]
    snapshot = _build_pencil_structure_snapshot(target_node)
    if snapshot:
        snapshot["source"] = "pencil_live"
        snapshot["node_id"] = node_id
        snapshot["node_name"] = node_name
    return snapshot


def try_get_pen_node_name(source_path: str, node_id: str) -> str:
    try:
        document = load_json_document(Path(source_path))
    except EvidenceReadError as exc:
        emit_diagnostic("warning", "pen_node_name_source_unreadable", exc.path, exc.detail)
        return ""
    node = find_node_by_id(document.get("children", []), node_id)
    if not isinstance(node, dict):
        return ""
    return str(node.get("name") or "")


def normalize_structure_text(value: str) -> str:
    if not isinstance(value, str):
        return ""
    normalized = re.sub(r"\s+", " ", value.replace("\r", " ").replace("\n", " ").replace("\t", " ")).strip()
    return normalized


def normalize_structure_texts(values) -> list[str]:
    normalized = []
    for value in values or []:
        text = normalize_structure_text(value)
        if text:
            normalized.append(text)
    normalized.sort()
    return normalized


def get_result_structure(step_result: dict | None, field: str) -> dict | None:
    if not isinstance(step_result, dict):
        return None
    ue_result = step_result.get("ue_result", step_result)
    if not isinstance(ue_result, dict):
        return None
    snapshot = ue_result.get(field)
    return snapshot if isinstance(snapshot, dict) else None


def compare_structure_snapshots(left: dict | None, right: dict | None) -> dict | None:
    if not isinstance(left, dict) or not isinstance(right, dict):
        return None
    left_texts = normalize_structure_texts(left.get("visible_texts"))
    right_texts = normalize_structure_texts(right.get("visible_texts"))
    left_kind_histogram = normalize_structure_histogram(left.get("kind_histogram"))
    right_kind_histogram = normalize_structure_histogram(right.get("kind_histogram"))
    left_class_histogram = normalize_structure_histogram(left.get("class_histogram"))
    right_class_histogram = normalize_structure_histogram(right.get("class_histogram"))
    return {
        "available": True,
        "texts_match": left_texts == right_texts,
        "left_total_nodes": int(left.get("total_nodes") or 0),
        "right_total_nodes": int(right.get("total_nodes") or 0),
        "left_leaf_nodes": int(left.get("leaf_nodes") or 0),
        "right_leaf_nodes": int(right.get("leaf_nodes") or 0),
        "left_container_nodes": int(left.get("container_nodes") or 0),
        "right_container_nodes": int(right.get("container_nodes") or 0),
        "left_text_nodes": int(left.get("text_nodes") or 0),
        "right_text_nodes": int(right.get("text_nodes") or 0),
        "left_visual_nodes": int(left.get("visual_nodes") or 0),
        "right_visual_nodes": int(right.get("visual_nodes") or 0),
        "kind_histograms_match": left_kind_histogram == right_kind_histogram,
        "class_histograms_match": left_class_histogram == right_class_histogram,
    }


def normalize_structure_histogram(value) -> dict[str, int]:
    if not isinstance(value, dict):
        return {}
    normalized = {}
    for key, count in value.items():
        normalized_key = str(key or "")
        if not normalized_key:
            continue
        try:
            normalized[normalized_key] = int(count or 0)
        except (TypeError, ValueError):
            normalized[normalized_key] = 0
    return normalized


def structure_count_floor(source_count: int, ratio: float, minimum: int) -> int:
    if source_count <= 1:
        return 1
    return max(minimum, int(math.ceil(source_count * ratio)))


def structure_comparison_counts_match(comparison: dict | None) -> bool:
    if not isinstance(comparison, dict) or not comparison.get("available"):
        return False
    fields = ("total_nodes", "leaf_nodes", "container_nodes", "text_nodes", "visual_nodes")
    for field in fields:
        if comparison.get(f"left_{field}") != comparison.get(f"right_{field}"):
            return False
    return bool(
        comparison.get("texts_match")
        and comparison.get("kind_histograms_match")
        and comparison.get("class_histograms_match")
    )


def validate_structure_consistency(args, ue_result: dict, render_chain: dict, reference_context: dict | None = None) -> dict:
    generation_result = render_chain.get("generation")
    asset_result = render_chain.get("asset_render")
    live_source_snapshot = (reference_context or {}).get("source_structure_live")
    source_snapshot = live_source_snapshot or get_result_structure(generation_result, "source_structure") or get_result_structure(ue_result, "source_structure")
    generated_asset_snapshot = get_result_structure(generation_result, "asset_structure")
    asset_snapshot = get_result_structure(asset_result, "asset_structure") or get_result_structure(ue_result, "asset_structure")

    required = args.mode != "asset"
    generation_vs_asset = compare_structure_snapshots(generated_asset_snapshot, asset_snapshot)
    result = {
        "required": required,
        "available": False,
        "passed": not required,
        "source_origin": "pencil_live" if live_source_snapshot else "local_pen_fallback",
        "source_snapshot_type": source_snapshot.get("snapshot_type") if isinstance(source_snapshot, dict) else None,
        "asset_snapshot_type": asset_snapshot.get("snapshot_type") if isinstance(asset_snapshot, dict) else None,
        "reasons": [],
        "source": source_snapshot,
        "asset": asset_snapshot,
        "generation_vs_asset": generation_vs_asset,
    }

    if not isinstance(source_snapshot, dict) or not isinstance(asset_snapshot, dict):
        if required:
            result["passed"] = False
            result["reasons"].append("structure_snapshot_unavailable")
        return result

    result["available"] = True

    source_total = int(source_snapshot.get("total_nodes") or 0)
    asset_total = int(asset_snapshot.get("total_nodes") or 0)
    source_leaf = int(source_snapshot.get("leaf_nodes") or 0)
    asset_leaf = int(asset_snapshot.get("leaf_nodes") or 0)
    source_container = int(source_snapshot.get("container_nodes") or 0)
    asset_container = int(asset_snapshot.get("container_nodes") or 0)
    source_visual = int(source_snapshot.get("visual_nodes") or 0)
    asset_visual = int(asset_snapshot.get("visual_nodes") or 0)
    source_text_nodes = int(source_snapshot.get("text_nodes") or 0)
    asset_text_nodes = int(asset_snapshot.get("text_nodes") or 0)
    source_texts = normalize_structure_texts(source_snapshot.get("visible_texts"))
    asset_texts = normalize_structure_texts(asset_snapshot.get("visible_texts"))
    source_text_counter = Counter(source_texts)
    asset_text_counter = Counter(asset_texts)

    min_total_required = structure_count_floor(source_total, 0.50, 2)
    min_leaf_required = structure_count_floor(source_leaf, 0.50, 1)
    min_visual_required = structure_count_floor(source_visual, 0.35, 1)

    if source_text_counter != asset_text_counter:
        result["reasons"].append("visible_text_mismatch")
    if source_text_nodes > 0 and asset_text_nodes != source_text_nodes:
        result["reasons"].append("text_node_count_mismatch")
    if source_total >= 2 and asset_total < min_total_required:
        result["reasons"].append("asset_total_nodes_collapsed")
    if source_leaf >= 2 and asset_leaf < min_leaf_required:
        result["reasons"].append("asset_leaf_nodes_collapsed")
    if source_container > 0 and asset_container == 0:
        result["reasons"].append("asset_container_missing")
    if source_total >= 8 and asset_total < min_total_required:
        result["reasons"].append("asset_tree_too_small")
    if (
        source_total >= 4
        and (source_text_nodes > 0 or source_visual >= 3)
        and asset_text_nodes == 0
        and asset_visual <= 1
        and asset_total <= 3
    ):
        result["reasons"].append("image_only_render_cheat_suspected")
    if source_visual >= 12 and asset_visual < min_visual_required:
        result["reasons"].append("visual_structure_collapsed")
    if required and not isinstance(generation_vs_asset, dict):
        result["reasons"].append("generation_asset_structure_unavailable")
    elif required and not structure_comparison_counts_match(generation_vs_asset):
        result["reasons"].append("generated_asset_structure_changed_after_reload")

    result["metrics"] = {
        "source_total_nodes": source_total,
        "asset_total_nodes": asset_total,
        "source_leaf_nodes": source_leaf,
        "asset_leaf_nodes": asset_leaf,
        "source_container_nodes": source_container,
        "asset_container_nodes": asset_container,
        "source_visual_nodes": source_visual,
        "asset_visual_nodes": asset_visual,
        "source_text_nodes": source_text_nodes,
        "asset_text_nodes": asset_text_nodes,
        "min_total_required": min_total_required,
        "min_leaf_required": min_leaf_required,
        "min_visual_required": min_visual_required,
        "source_root_kind": source_snapshot.get("root_kind"),
        "asset_root_kind": asset_snapshot.get("root_kind"),
        "source_root_child_count": source_snapshot.get("root_child_count"),
        "asset_root_child_count": asset_snapshot.get("root_child_count"),
        "visible_texts_match": source_text_counter == asset_text_counter,
        "missing_texts": sorted((source_text_counter - asset_text_counter).elements()),
        "extra_texts": sorted((asset_text_counter - source_text_counter).elements()),
    }
    result["passed"] = len(result["reasons"]) == 0
    return result


def assess_provided_reference_dirty(provided_vs_canonical: dict | None) -> dict:
    if not provided_vs_canonical:
        return {
            "available": False,
            "dirty": False,
            "reason": "",
            "changed_ratio_threshold": 0.05,
            "mae_threshold": 0.02,
        }

    size_match = bool(provided_vs_canonical.get("size_match"))
    changed_ratio = provided_vs_canonical.get("changed_ratio")
    mean_abs_error = provided_vs_canonical.get("mean_abs_error")
    dirty = (
        not size_match
        or changed_ratio is None
        or mean_abs_error is None
        or changed_ratio > 0.05
        or mean_abs_error > 0.02
    )
    reasons = []
    if not size_match:
        reasons.append("size_mismatch")
    if changed_ratio is None or mean_abs_error is None:
        reasons.append("comparison_unavailable")
    else:
        if changed_ratio > 0.05:
            reasons.append("changed_ratio_exceeds_threshold")
        if mean_abs_error > 0.02:
            reasons.append("mae_exceeds_threshold")
    return {
        "available": True,
        "dirty": dirty,
        "reason": ",".join(reasons),
        "changed_ratio_threshold": 0.05,
        "mae_threshold": 0.02,
        "comparison": provided_vs_canonical,
    }


def prepare_pen_authority_reference(
    source_path: str,
    node_id: str,
    output_dir: Path,
    server_path: str,
    app_name: str,
    expected_design_size: tuple[int, int] | None,
    pixel_tolerance: int,
):
    if not node_id:
        raise ValueError("Pencil authority reference export requires --node")

    authority = {
        "available": False,
        "valid": False,
        "reason": "",
        "canonical_source": None,
        "canonical_path": None,
        "export_nodes_path": None,
        "get_screenshot_path": None,
        "export_nodes_validation": None,
        "get_screenshot_validation": None,
        "witness_aspect_delta": None,
        "witness_content_validation": None,
        "verification_level": "none",
        "attempts": [],
    }

    authority_dir = output_dir / "reference_authority"
    export_dir = authority_dir / "export_nodes"
    screenshot_path = authority_dir / "get_screenshot.png"
    export_path = None
    witness_path = None
    fallback_witness_path = None

    max_attempts = 3
    last_error = ""
    for attempt_index in range(max_attempts):
        attempt_number = attempt_index + 1
        try:
            export_path, witness_path, export_error = run_pen_authority_reference_attempt(
                source_path,
                node_id,
                server_path,
                app_name,
                export_dir,
                screenshot_path,
                force_reopen=(attempt_number > 1),
            )
            attempt_result = {
                "attempt": attempt_number,
                "status": "ok",
                "export_available": bool(export_path and export_path.is_file()),
                "screenshot_available": bool(witness_path and witness_path.is_file()),
            }
            if witness_path and witness_path.is_file():
                fallback_witness_path = witness_path
            if export_path and export_path.is_file():
                if export_error:
                    attempt_result["reason"] = f"pencil_export_nodes_unavailable: {export_error}"
                authority["attempts"].append(attempt_result)
                break

            last_error = export_error or "Pencil authority reference did not produce a usable PNG"
            attempt_result["status"] = "retry" if attempt_number < max_attempts else "failed"
            attempt_result["reason"] = last_error
            authority["attempts"].append(attempt_result)
        except Exception as exc:
            last_error = str(exc)
            reason_prefix = "pencil_document_switch_failed" if "did not switch to document" in last_error.lower() else "pencil_authority_attempt_failed"
            authority["attempts"].append(
                {
                    "attempt": attempt_number,
                    "status": "retry" if attempt_number < max_attempts else "failed",
                    "reason": f"{reason_prefix}: {last_error}",
                    "export_available": False,
                    "screenshot_available": False,
                }
            )
        if export_path and export_path.is_file():
            break
        if attempt_number < max_attempts:
            time.sleep(float(attempt_number))

    if not (export_path and export_path.is_file()) and fallback_witness_path and fallback_witness_path.is_file():
        witness_path = fallback_witness_path

    if export_path and export_path.is_file():
        authority["available"] = True
        authority["canonical_source"] = "pencil_export_nodes"
        authority["canonical_path"] = str(export_path)
        authority["export_nodes_path"] = str(export_path)
        authority["export_nodes_validation"] = validate_reference_against_design(
            read_image_size(export_path),
            expected_design_size,
        )

    if witness_path and witness_path.is_file():
        authority["available"] = True
        if not authority["canonical_source"]:
            authority["canonical_source"] = "pencil_get_screenshot"
            authority["canonical_path"] = str(witness_path)
        authority["get_screenshot_path"] = str(witness_path)
        authority["get_screenshot_validation"] = validate_reference_against_design(
            read_image_size(witness_path),
            expected_design_size,
        )

    if not authority["available"]:
        if last_error:
            if "did not switch to document" in last_error.lower():
                authority["reason"] = f"pencil_document_switch_failed: {last_error}"
            else:
                authority["reason"] = f"pencil_authority_not_available: {last_error}"
        else:
            authority["reason"] = "pencil_authority_not_available"
        return authority

    canonical_validation = authority["export_nodes_validation"] or authority["get_screenshot_validation"]
    authority["valid"] = bool(canonical_validation and canonical_validation.get("valid"))
    if not authority["valid"] and canonical_validation:
        authority["reason"] = canonical_validation.get("reason", "")

    if authority["export_nodes_validation"] and authority["get_screenshot_validation"]:
        export_size = authority["export_nodes_validation"]["reference_size"]
        witness_size = authority["get_screenshot_validation"]["reference_size"]
        export_aspect = export_size[0] / export_size[1]
        witness_aspect = witness_size[0] / witness_size[1]
        witness_aspect_delta = abs(export_aspect - witness_aspect) / max(export_aspect, witness_aspect)
        authority["witness_aspect_delta"] = witness_aspect_delta
        if witness_aspect_delta > 0.02:
            authority["valid"] = False
            authority["reason"] = "pencil_witness_aspect_mismatch"
        elif not authority["get_screenshot_validation"]["valid"]:
            authority["valid"] = False
            authority["reason"] = authority["get_screenshot_validation"].get("reason", "")
        else:
            authority["witness_content_validation"] = compare_scaled_image_paths(export_path, witness_path, pixel_tolerance)
            witness_content = authority["witness_content_validation"]
            if not witness_content:
                authority["valid"] = False
                authority["reason"] = "pencil_witness_content_unavailable"
            else:
                witness_content_ok = (
                    witness_content["changed_ratio"] <= 0.15
                    and witness_content["mean_abs_error"] <= 0.02
                )
                if not witness_content_ok:
                    authority["valid"] = False
                    authority["reason"] = "pencil_witness_content_mismatch"
                else:
                    authority["verification_level"] = "strong"
    elif authority["export_nodes_validation"] and authority["valid"]:
        authority["reason"] = authority["reason"] or "pencil_get_screenshot_unavailable"
        authority["verification_level"] = "weak"
    elif authority["get_screenshot_validation"] and authority["valid"]:
        authority["reason"] = authority["reason"] or "pencil_export_nodes_unavailable"
        authority["verification_level"] = "witness_only"

    return authority


def summarize_pen_authority_stdout(authority: dict | None, *, fail_closed: bool = False) -> None:
    if not isinstance(authority, dict):
        return

    canonical_source = authority.get("canonical_source") or ""
    verification_level = authority.get("verification_level") or ""
    reason = authority.get("reason") or ""

    if canonical_source == "pencil_get_screenshot":
        print(f"[AUTHORITY] live Pencil export_nodes unavailable: {reason or 'pencil_export_nodes_unavailable'}")
        print(
            "[AUTHORITY] fell back to get_screenshot witness: "
            f"{authority.get('get_screenshot_path') or authority.get('canonical_path') or '<missing>'}"
        )
        if fail_closed or verification_level == "witness_only":
            print(
                "[AUTHORITY] witness fallback can keep compare artifacts flowing, "
                "but formal/historical-best still fail-closed until live Pencil export_nodes succeeds"
            )
        return

    if not authority.get("available"):
        print("[AUTHORITY] export_nodes fallback attempted and get_screenshot witness was also attempted")
        print("[AUTHORITY] no usable Pencil authority PNG was produced")
        print(f"[AUTHORITY] reason={reason or 'pencil_authority_not_available'}")


def prepare_pencil_reference(source_path: str, node_id: str, output_dir: Path, output_path: Path, server_path: str, app_name: str) -> Path:
    if not node_id:
        raise ValueError("Pencil reference export requires --node")
    authority = prepare_pen_authority_reference(source_path, node_id, output_dir, server_path, app_name, None, 6)
    canonical_path = authority.get("canonical_path")
    if not canonical_path:
        raise RuntimeError("Pencil MCP did not produce any canonical reference PNG")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(canonical_path, output_path)
    return output_path


def load_rgba(path: Path) -> Image.Image:
    image = Image.open(path)
    if image.mode != "RGBA":
        image = image.convert("RGBA")
    return image


def apply_stable_visual_tolerance(image: Image.Image) -> Image.Image:
    return image.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))


def normalize_fully_transparent_rgb(reference_canvas: Image.Image, candidate_canvas: Image.Image) -> tuple[Image.Image, Image.Image]:
    reference = reference_canvas.copy()
    candidate = candidate_canvas.copy()
    reference_pixels = reference.load()
    candidate_pixels = candidate.load()

    for y in range(reference.height):
        for x in range(reference.width):
            if reference_pixels[x, y][3] == 0 and candidate_pixels[x, y][3] == 0:
                reference_pixels[x, y] = (0, 0, 0, 0)
                candidate_pixels[x, y] = (0, 0, 0, 0)

    return reference, candidate


def measure_canvas_diff(reference_canvas: Image.Image, candidate_canvas: Image.Image, pixel_tolerance: int):
    diff = ImageChops.difference(reference_canvas, candidate_canvas)
    stat = ImageStat.Stat(diff)
    channel_means = stat.mean
    mean_abs_error = sum(channel_means) / (255.0 * len(channel_means))

    r, g, b, a = diff.split()
    max_rgb = ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, a))
    hist = max_rgb.histogram()
    changed_pixels = sum(hist[pixel_tolerance + 1 :])
    total_pixels = reference_canvas.width * reference_canvas.height
    changed_ratio = 0.0 if total_pixels == 0 else changed_pixels / total_pixels
    bbox = max_rgb.getbbox()
    return {
        "mean_abs_error": mean_abs_error,
        "changed_ratio": changed_ratio,
        "diff_bbox": list(bbox) if bbox else None,
        "max_rgb": max_rgb,
    }


def build_diff(reference: Image.Image, candidate: Image.Image, pixel_tolerance: int):
    canvas_width = max(reference.width, candidate.width)
    canvas_height = max(reference.height, candidate.height)
    reference_canvas = Image.new("RGBA", (canvas_width, canvas_height), (0, 0, 0, 0))
    candidate_canvas = Image.new("RGBA", (canvas_width, canvas_height), (0, 0, 0, 0))
    reference_canvas.paste(reference, (0, 0))
    candidate_canvas.paste(candidate, (0, 0))

    reference_canvas, candidate_canvas = normalize_fully_transparent_rgb(reference_canvas, candidate_canvas)

    raw_metrics = measure_canvas_diff(reference_canvas, candidate_canvas, pixel_tolerance)
    stable_metrics = measure_canvas_diff(
        *normalize_fully_transparent_rgb(
            apply_stable_visual_tolerance(reference_canvas),
            apply_stable_visual_tolerance(candidate_canvas),
        ),
        pixel_tolerance,
    )

    amplified = raw_metrics["max_rgb"].point(lambda value: min(255, value * 4))
    diff_overlay = Image.merge(
        "RGBA",
        (
            amplified,
            Image.new("L", amplified.size, 0),
            Image.new("L", amplified.size, 0),
            amplified,
        ),
    )
    preview = Image.alpha_composite(candidate_canvas, diff_overlay)

    return {
        "reference_canvas": reference_canvas,
        "candidate_canvas": candidate_canvas,
        "preview": preview,
        "mean_abs_error": raw_metrics["mean_abs_error"],
        "changed_ratio": raw_metrics["changed_ratio"],
        "diff_bbox": raw_metrics["diff_bbox"],
        "stable_visual_filter": STABLE_VISUAL_FILTER,
        "stable_mean_abs_error": stable_metrics["mean_abs_error"],
        "stable_changed_ratio": stable_metrics["changed_ratio"],
        "stable_diff_bbox": stable_metrics["diff_bbox"],
        "size_match": reference.size == candidate.size,
        "reference_size": list(reference.size),
        "candidate_size": list(candidate.size),
    }


def build_visual_comparison(reference: Image.Image, candidate: Image.Image, pixel_tolerance: int):
    actual_reference_size = list(reference.size)
    actual_candidate_size = list(candidate.size)
    if reference.size == candidate.size:
        comparison = build_diff(reference, candidate, pixel_tolerance)
        comparison["comparison_mode"] = "native_size"
        comparison["actual_reference_size"] = actual_reference_size
        comparison["actual_candidate_size"] = actual_candidate_size
        comparison["comparison_reference_size"] = actual_reference_size
        comparison["comparison_candidate_size"] = actual_candidate_size
        return comparison

    reference_aspect = reference.width / reference.height if reference.height else 0.0
    candidate_aspect = candidate.width / candidate.height if candidate.height else 0.0
    aspect_denominator = max(reference_aspect, candidate_aspect)
    aspect_delta = 1.0 if aspect_denominator <= 0.0 else abs(reference_aspect - candidate_aspect) / aspect_denominator
    if aspect_delta > REFERENCE_ASPECT_COMPATIBILITY_THRESHOLD:
        comparison = build_diff(reference, candidate, pixel_tolerance)
        comparison["comparison_mode"] = "size_mismatch_no_scale"
        comparison["actual_reference_size"] = actual_reference_size
        comparison["actual_candidate_size"] = actual_candidate_size
        comparison["comparison_reference_size"] = actual_reference_size
        comparison["comparison_candidate_size"] = actual_candidate_size
        comparison["aspect_delta"] = aspect_delta
        return comparison

    scaled_reference = reference.resize(candidate.size, Image.Resampling.BILINEAR)
    comparison = build_diff(scaled_reference, candidate, pixel_tolerance)
    comparison["comparison_mode"] = "reference_scaled_to_candidate_for_compare_only"
    comparison["actual_reference_size"] = actual_reference_size
    comparison["actual_candidate_size"] = actual_candidate_size
    comparison["comparison_reference_size"] = list(scaled_reference.size)
    comparison["comparison_candidate_size"] = actual_candidate_size
    comparison["reference_size"] = actual_reference_size
    comparison["candidate_size"] = actual_candidate_size
    comparison["size_match"] = False
    comparison["aspect_delta"] = aspect_delta
    return comparison


def terminate_editor_process(process: subprocess.Popen) -> None:
    process.kill()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/PID", str(process.pid)], check=False, capture_output=True, text=True)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass


def run_editor(
    editor_path: str,
    project_path: str,
    driver_script_path: Path,
    log_path: Path,
    log_cmds: str = "",
    result_path: Path | None = None,
    idle_timeout_sec: int = 600,
    startup_timeout_sec: int = 90,
) -> int:
    driver_script_path = driver_script_path.resolve()
    command = [
        editor_path,
        project_path,
        "-nop4",
        "-NoSound",
        "-nosplash",
        "-NoMutex",
        "-stdout",
        "-FullStdOutLogOutput",
        "-unattended",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    extra_editor_args = os.environ.get("DESIGN2UMG_EDITOR_EXTRA_ARGS", "").strip()
    if extra_editor_args:
        command.extend(shlex.split(extra_editor_args))
    if log_cmds:
        command.append(f"-LogCmds={log_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8") as handle:
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
        process_started_at = time.time()
        last_progress_time = time.time()
        last_log_size = 0
        result_seen = False
        observed_progress = False

        while True:
            exit_code = process.poll()
            if exit_code is not None:
                return exit_code

            current_result_seen = bool(result_path and result_path.is_file())
            if current_result_seen and not result_seen:
                result_seen = True
                last_progress_time = time.time()
                observed_progress = True

            try:
                current_log_size = log_path.stat().st_size
            except FileNotFoundError:
                current_log_size = 0
            if current_log_size > last_log_size:
                last_log_size = current_log_size
                last_progress_time = time.time()
                observed_progress = True

            if not observed_progress and (time.time() - process_started_at > startup_timeout_sec):
                terminate_editor_process(process)
                raise TimeoutError(
                    f"Editor startup made no progress for {startup_timeout_sec}s. "
                    f"result_file={'1/1' if result_seen else '0/1'} log={log_path}"
                )

            if time.time() - last_progress_time > idle_timeout_sec:
                if result_seen and log_indicates_clean_editor_shutdown(log_path):
                    terminate_editor_process(process)
                    return 0
                terminate_editor_process(process)
                raise TimeoutError(
                    f"Editor made no progress for {idle_timeout_sec}s. "
                    f"result_file={'1/1' if result_seen else '0/1'} log={log_path}"
                )

            time.sleep(2)


def read_image_size(path: Path) -> tuple[int, int]:
    image = Image.open(path)
    return image.width, image.height


def write_json(path: Path, payload: dict):
    helper_write_json(path, payload)


def write_single_editor_driver_script(output_dir: Path, spec_path: Path) -> Path:
    output_dir = output_dir.resolve()
    manifest_path = output_dir / "editor_driver_manifest.json"
    driver_log_path = output_dir / "editor_driver.log"
    script_path = output_dir / "editor_driver.py"
    return write_editor_driver_script(
        script_path,
        manifest_path,
        driver_log_path,
        {"spec_path": spec_path.resolve().as_posix()},
        "spec_path",
    )


def build_file_signature(path_like: str | Path) -> dict | None:
    return helper_content_file_signature(path_like)


def infer_single_case_name(args, output_dir: Path) -> str:
    if args.mode == "pen":
        node_name = try_get_pen_node_name(args.source, args.node) or args.node or Path(args.source).stem
        return f"pen_{sanitize_case_name(Path(args.source).stem)}_{sanitize_case_name(node_name)}"
    if args.mode in {"local-figma", "figma-source"}:
        export_root = Path(args.source).resolve().parent
        suffix = "local" if args.mode == "local-figma" else "source"
        return f"figma_{sanitize_case_name(export_root.name)}_{suffix}"
    if args.mode == "asset":
        return f"asset_{sanitize_case_name(Path(args.source).stem)}"
    return sanitize_case_name(output_dir.name)


def build_single_case_summary_entry(case_name: str, report_path: Path) -> dict:
    report = load_json_document(report_path)
    structure_validation = report.get("structure_validation") or {}
    generation_vs_asset = ((report.get("render_chain") or {}).get("generation_vs_asset") or {})
    reference_authority = ((report.get("reference_context") or {}).get("reference_authority") or {})
    return {
        "name": case_name,
        "report_path": str(report_path),
        "changed_ratio": report.get("changed_ratio"),
        "stable_changed_ratio": report.get("stable_changed_ratio"),
        "mae": report.get("mean_abs_error"),
        "stable_mae": report.get("stable_mean_abs_error"),
        "verification_level": reference_authority.get("verification_level", ""),
        "generation_vs_asset_changed_ratio": generation_vs_asset.get("changed_ratio"),
        "structure_passed": structure_validation.get("passed", True) if structure_validation else True,
        "structure_required": bool(structure_validation.get("required")) if structure_validation else False,
        "structure_available": bool(structure_validation),
        "structure_reasons": structure_validation.get("reasons", []) if structure_validation else [],
        "no_regression_available": False,
        "no_regression_passed": True,
        "no_regression_reasons": [],
        "baseline_changed_ratio": None,
        "baseline_stable_changed_ratio": None,
        "baseline_mae": None,
        "baseline_stable_mae": None,
        "changed_ratio_delta": None,
        "stable_changed_ratio_delta": None,
        "mae_delta": None,
        "stable_mae_delta": None,
        "no_regression_decisive_metric": "",
        "no_regression_decisive_delta": None,
        "source_signature": report.get("source_signature"),
        "provided_reference_signature": report.get("provided_reference_signature"),
        "effective_reference_signature": report.get("effective_reference_signature"),
    }


def finalize_single_run_history(output_dir: Path, args, report_path: Path) -> None:
    case_name = infer_single_case_name(args, output_dir)
    source_signature = build_file_signature(args.source)
    run_scope = {
        "scope": "focus",
        "include_pattern": case_name,
        "exclude_pattern": "",
        "discovered_total": 1,
        "formal_admissible_candidate": False,
        "case_names": [case_name],
        "case_inputs": [
            {
                "name": case_name,
                "mode": args.mode,
                "source": str(Path(args.source).resolve()),
                "source_signature": source_signature,
            }
        ],
    }
    run_scope_path = output_dir / "run_scope.json"
    write_json(run_scope_path, run_scope)

    summary_case = build_single_case_summary_entry(case_name, report_path)
    summary = {
        "total": 1,
        "editor_exit_code": load_json_document(report_path).get("editor_exit_code"),
        "design_root": str(Path(args.source).resolve().parent),
        "output_root": str(output_dir.resolve()),
        "run_scope_path": str(run_scope_path),
        "source_signature": source_signature,
        "completeness": {
            "blocked": False,
            "blocked_reasons": [],
            "discovered_total": 1,
            "reported_total": 1,
            "missing_compare_reports": [],
            "missing_case_results": [],
        },
        "no_regression": {
            "available": 0,
            "passed": 0,
            "failed": 0,
        },
        "cases": [summary_case],
    }
    write_json(output_dir / "summary.json", summary)

    write_historical_best_validation(
        output_dir,
        output_dir.parent,
        rebuild_best,
        validate_run_tool,
        write_json,
    )


def load_ue_result(path: Path) -> dict:
    with open(path, "r", encoding="utf-8-sig") as handle:
        result = json.load(handle)

    render_png_path = Path(result.get("render_png_path", "")).resolve() if result.get("render_png_path") else None
    if render_png_path:
        wait_for_file_stable(render_png_path)
    return result


def wait_for_file_stable(path: Path, timeout_sec: float = 30.0, stable_window_sec: float = 2.0, poll_interval_sec: float = 0.25):
    deadline = time.time() + timeout_sec
    last_signature = None
    stable_since = None

    while time.time() < deadline:
        signature = _path_signature(path)
        if signature is None:
            stable_since = None
            time.sleep(poll_interval_sec)
            continue

        if signature == last_signature:
            if stable_since is None:
                stable_since = time.time()
            elif time.time() - stable_since >= stable_window_sec:
                return
        else:
            last_signature = signature
            stable_since = time.time()

        time.sleep(poll_interval_sec)

    raise RuntimeError(f"Timed out waiting for stable file output: {path}")


def add_name_suffix(path: Path, suffix: str) -> Path:
    return path.with_name(f"{path.stem}{suffix}{path.suffix}")


def log_indicates_clean_editor_shutdown(log_path: Path) -> bool:
    if not log_path.is_file():
        return False

    try:
        with open(log_path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            tail_size = min(handle.tell(), 128 * 1024)
            handle.seek(-tail_size, os.SEEK_END)
            tail_text = handle.read().decode("utf-8-sig", errors="ignore")
    except OSError:
        return False

    fatal_markers = (
        "Fatal error",
        "Unhandled Exception",
        "Access violation",
        "Assertion failed",
    )
    if any(marker in tail_text for marker in fatal_markers):
        return False

    return "LogExit: Exiting." in tail_text or "LogExit: Editor shut down" in tail_text


def execute_automation_render(args, spec_path: Path, log_path: Path, spec: dict):
    result_path = Path(spec["result_json_path"])
    write_json(spec_path, spec)
    driver_script_path = write_single_editor_driver_script(spec_path.parent, spec_path)
    sync_project_plugin_build_id(args.editor, Path(args.project_plugin_root))
    ensure_clean_editor()
    editor_exit_code = run_editor(
        args.editor,
        args.project,
        driver_script_path,
        log_path,
        args.log_cmds,
        result_path,
        args.idle_timeout_sec,
        args.startup_timeout_sec,
    )
    if editor_exit_code != 0:
        print(f"Unreal editor exited with code {editor_exit_code}", file=sys.stderr)
    if not result_path.is_file():
        deadline = time.time() + 600.0
        while time.time() < deadline:
            if result_path.is_file():
                break
            time.sleep(1.0)
    if not result_path.is_file():
        raise RuntimeError(f"UE result json not found: {result_path}")
    ue_result = load_ue_result(result_path)
    if editor_exit_code != 0 and log_indicates_clean_editor_shutdown(log_path):
        editor_exit_code = 0
    return {
        "editor_exit_code": editor_exit_code,
        "log_path": str(log_path),
        "spec_path": str(spec_path),
        "ue_result": ue_result,
    }


def run_native_self_check(
    args,
    output_dir: Path,
    mode_map: dict,
    game_output_root: str,
    expected_design_size: tuple[int, int] | None,
    primary_render_size: tuple[int, int],
    source_path: str,
    node_id: str,
    generated_asset_path: str | None,
):
    if not expected_design_size:
        return {
            "available": False,
            "reason": "expected_design_size_unavailable",
        }

    native_width, native_height = expected_design_size
    if native_width <= 0 or native_height <= 0:
        return {
            "available": False,
            "reason": "expected_design_size_invalid",
            "expected_design_size": [native_width, native_height],
        }

    if (native_width, native_height) == primary_render_size:
        return {
            "available": False,
            "reason": "expected_design_size_matches_primary_render",
            "expected_design_size": [native_width, native_height],
            "primary_render_size": list(primary_render_size),
        }

    native_path = output_dir / "candidate.native.png"
    native_bootstrap_path = add_name_suffix(native_path, ".bootstrap")
    native_generate_output = build_automation_phase_output(game_output_root, "native")
    native_asset_output = build_automation_phase_output(game_output_root, "native-asset")
    result = {
        "available": True,
        "reason": "",
        "expected_design_size": [native_width, native_height],
        "primary_render_size": list(primary_render_size),
    }

    if args.mode == "asset":
        native_asset_spec = {
            "mode": "asset",
            "source_path": source_path,
            "node_id": "",
            "output_path": native_asset_output,
            "render_png_path": str(native_path),
            "result_json_path": str(output_dir / "ue_native_render_result.json"),
            "force_reload_package": True,
        }
        native_asset_result = execute_automation_render(
            args,
            output_dir / "automation_native_asset_spec.json",
            output_dir / "unreal_native_asset_render.log",
            native_asset_spec,
        )
        result["asset_render"] = native_asset_result
        result["generation_vs_asset"] = None
        result["reason"] = "asset_mode_no_generation_pass"
        return result

    native_generate_spec = {
        "mode": mode_map[args.mode],
        "source_path": source_path,
        "node_id": node_id,
        "output_path": native_generate_output,
        "render_png_path": str(native_bootstrap_path),
        "result_json_path": str(output_dir / "ue_native_generate_result.json"),
    }
    native_generate_result = execute_automation_render(
        args,
        output_dir / "automation_native_generate_spec.json",
        output_dir / "unreal_native_generate.log",
        native_generate_spec,
    )
    native_generated_asset_path = native_generate_result["ue_result"].get("generated_asset_path") or generated_asset_path
    if not native_generated_asset_path:
        raise RuntimeError("Native self-check generation pass did not produce a generated_asset_path")

    native_asset_spec = {
        "mode": "asset",
        "source_path": native_generated_asset_path,
        "node_id": "",
        "output_path": native_asset_output,
        "render_png_path": str(native_path),
        "result_json_path": str(output_dir / "ue_native_render_result.json"),
        "force_reload_package": True,
    }
    native_asset_result = execute_automation_render(
        args,
        output_dir / "automation_native_asset_spec.json",
        output_dir / "unreal_native_asset_render.log",
        native_asset_spec,
    )
    result["generation"] = native_generate_result
    result["asset_render"] = native_asset_result
    result["generation_vs_asset"] = compare_image_paths(native_bootstrap_path, native_path, args.pixel_tolerance)
    return result


def prepare_reference(args, output_dir: Path, expected_design_size: tuple[int, int] | None) -> tuple[Path, dict]:
    reference_png = output_dir / "reference.png"
    context = {
        "canonical_source": args.reference_provider,
        "provided_reference_path": None,
        "reference_authority": None,
        "provided_vs_canonical": None,
    }
    if args.reference_provider == "png":
        provided_path = output_dir / "reference.provided.png"
        prepare_png_reference(args.reference, provided_path)
        context["provided_reference_path"] = str(provided_path)
        if args.mode == "pen":
            authority = prepare_pen_authority_reference(
                args.source,
                args.node,
                output_dir,
                args.mcp_server,
                args.pencil_app,
                expected_design_size,
                args.pixel_tolerance,
            )
            context["reference_authority"] = authority
            summarize_pen_authority_stdout(authority)
            canonical_path = authority.get("canonical_path")
            if canonical_path:
                shutil.copyfile(canonical_path, reference_png)
                context["canonical_source"] = authority.get("canonical_source")
                context["provided_vs_canonical"] = compare_image_paths(provided_path, reference_png, args.pixel_tolerance)
                return reference_png, context
        shutil.copyfile(provided_path, reference_png)
        return reference_png, context
    if args.reference_provider == "figma":
        prepare_figma_reference(args.figma_file, args.figma_node, args.figma_token, reference_png)
        context["canonical_source"] = "figma_api"
        return reference_png, context
    if args.reference_provider == "pencil":
        authority = prepare_pen_authority_reference(
            args.source,
            args.node,
            output_dir,
            args.mcp_server,
            args.pencil_app,
            expected_design_size,
            args.pixel_tolerance,
        )
        context["reference_authority"] = authority
        summarize_pen_authority_stdout(authority)
        canonical_path = authority.get("canonical_path")
        if not canonical_path:
            reason = authority.get("reason") or "pencil_authority_not_available"
            raise RuntimeError(f"Pencil authority reference did not produce a usable PNG ({reason})")
        shutil.copyfile(canonical_path, reference_png)
        context["canonical_source"] = authority.get("canonical_source")
        return reference_png, context
    raise ValueError(f"Unsupported reference provider: {args.reference_provider}")


def finalize_reference_context(args, reference_path: Path, reference_context: dict):
    effective_provider = reference_context.get("canonical_source") or args.reference_provider
    reference_context["requested_reference_provider"] = args.reference_provider
    reference_context["effective_reference_provider"] = effective_provider
    reference_context["effective_reference_path"] = str(reference_path)
    reference_context["source_signature"] = build_file_signature(args.source)
    reference_context["provided_reference_signature"] = build_file_signature(reference_context.get("provided_reference_path"))
    reference_context["effective_reference_signature"] = build_file_signature(reference_path)
    reference_context["provided_reference_dirty"] = assess_provided_reference_dirty(
        reference_context.get("provided_vs_canonical")
    )


def compare_and_report(
    args,
    output_dir: Path,
    reference_path: Path,
    candidate_path: Path,
    ue_result: dict,
    editor_exit_code: int,
    reference_context: dict,
    render_chain: dict,
):
    reference_image = load_rgba(reference_path)
    candidate_image = load_rgba(candidate_path)
    comparison = build_visual_comparison(reference_image, candidate_image, args.pixel_tolerance)
    structure_validation = validate_structure_consistency(args, ue_result, render_chain, reference_context)

    diff_preview_path = output_dir / "diff_overlay.png"
    comparison["preview"].save(diff_preview_path)

    comparison_size_compatible = comparison["comparison_mode"] in {
        "native_size",
        "reference_scaled_to_candidate_for_compare_only",
    }
    report = {
        "editor_exit_code": editor_exit_code,
        "reference_provider": args.reference_provider,
        "effective_reference_provider": reference_context.get("effective_reference_provider"),
        "effective_reference_path": reference_context.get("effective_reference_path"),
        "reference_png_path": str(reference_path),
        "candidate_png_path": str(candidate_path),
        "diff_overlay_path": str(diff_preview_path),
        "pixel_tolerance": args.pixel_tolerance,
        "stable_visual_filter": comparison["stable_visual_filter"],
        "changed_ratio": comparison["changed_ratio"],
        "stable_changed_ratio": comparison["stable_changed_ratio"],
        "mean_abs_error": comparison["mean_abs_error"],
        "stable_mean_abs_error": comparison["stable_mean_abs_error"],
        "size_match": comparison["size_match"],
        "comparison_size_compatible": comparison_size_compatible,
        "reference_size": comparison["reference_size"],
        "candidate_size": comparison["candidate_size"],
        "comparison_mode": comparison["comparison_mode"],
        "actual_reference_size": comparison["actual_reference_size"],
        "actual_candidate_size": comparison["actual_candidate_size"],
        "comparison_reference_size": comparison["comparison_reference_size"],
        "comparison_candidate_size": comparison["comparison_candidate_size"],
        "comparison_aspect_delta": comparison.get("aspect_delta"),
        "diff_bbox": comparison["diff_bbox"],
        "stable_diff_bbox": comparison["stable_diff_bbox"],
        "source_signature": reference_context.get("source_signature"),
        "provided_reference_signature": reference_context.get("provided_reference_signature"),
        "effective_reference_signature": reference_context.get("effective_reference_signature"),
        "ue_result": ue_result,
        "reference_context": reference_context,
        "render_chain": render_chain,
        "structure_validation": structure_validation,
    }
    report_path = output_dir / "compare_report.json"
    write_json(report_path, report)
    return report_path


def write_invalid_reference_report(
    output_dir: Path,
    args,
    reference_path: Path,
    candidate_path: Path,
    ue_result: dict,
    editor_exit_code: int,
    reference_validation: dict,
    reference_context: dict,
    render_chain: dict,
    verdict: str = "invalid_reference",
):
    report = {
        "verdict": verdict,
        "compare_skipped": True,
        "editor_exit_code": editor_exit_code,
        "reference_provider": args.reference_provider,
        "effective_reference_provider": reference_context.get("effective_reference_provider"),
        "effective_reference_path": reference_context.get("effective_reference_path"),
        "reference_png_path": str(reference_path),
        "candidate_png_path": str(candidate_path),
        "pixel_tolerance": args.pixel_tolerance,
        "stable_visual_filter": STABLE_VISUAL_FILTER,
        "changed_ratio": None,
        "stable_changed_ratio": None,
        "mean_abs_error": None,
        "stable_mean_abs_error": None,
        "size_match": False,
        "reference_size": reference_validation.get("reference_size"),
        "candidate_size": list(read_image_size(candidate_path)) if candidate_path.is_file() else None,
        "diff_bbox": None,
        "stable_diff_bbox": None,
        "source_signature": reference_context.get("source_signature"),
        "provided_reference_signature": reference_context.get("provided_reference_signature"),
        "effective_reference_signature": reference_context.get("effective_reference_signature"),
        "ue_result": ue_result,
        "reference_validation": reference_validation,
        "reference_context": reference_context,
        "render_chain": render_chain,
    }
    report_path = output_dir / "compare_report.json"
    write_json(report_path, report)
    return report_path


def main():
    args = parse_args()
    args.editor = resolve_unreal_editor_path(args.editor)
    args.project, project_plugin_root = prepare_runtime_project(args, [args.source])
    args.project_plugin_root = str(project_plugin_root)
    if args.dry_run_validate_paths:
        print("[OK] Design2UMG auto verify paths resolved.")
        print(f"project={args.project}")
        print(f"editor={args.editor}")
        print(f"project_plugin_root={args.project_plugin_root}")
        sys.exit(0)

    output_dir = (Path(args.output_dir) if args.output_dir else default_output_dir()).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    seed_runtime_font_assets(args.project, [args.source])
    expected_design_size = get_expected_design_size(args.mode, args.source, args.node)

    reference_path, reference_context = prepare_reference(args, output_dir, expected_design_size)
    finalize_reference_context(args, reference_path, reference_context)
    # When an external reference PNG is already provided, structure validation will
    # fall back to the generated local design snapshot if no live Pencil snapshot is
    # present. Avoid a redundant MCP round-trip on that path.
    if args.mode == "pen" and not args.reference:
        live_node_name = try_get_pen_node_name(args.source, args.node)
        live_source_structure = try_fetch_pencil_source_structure_snapshot(
            args.source,
            args.node,
            live_node_name,
            args.mcp_server,
            args.pencil_app,
        )
        if live_source_structure:
            reference_context["source_structure_live"] = live_source_structure
    reference_size = read_image_size(reference_path)
    reference_validation = validate_reference_against_design(reference_size, expected_design_size)
    reference_context["reference_validation"] = reference_validation

    candidate_path = output_dir / "candidate.png"
    game_output_root = build_automation_asset_root(output_dir, args.game_output, infer_single_case_name(args, output_dir))
    primary_game_output = build_automation_phase_output(game_output_root, "primary")
    asset_only_output = build_automation_phase_output(game_output_root, "asset")
    mode_map = {
        "pen": "pen",
        "local-figma": "local_figma",
        "figma-source": "figma_source",
        "asset": "asset",
    }
    render_chain = {}
    generated_asset_path = None
    if args.mode == "asset":
        spec = {
            "mode": mode_map[args.mode],
            "source_path": args.source,
            "node_id": args.node,
            "output_path": asset_only_output,
            "render_png_path": str(candidate_path),
            "result_json_path": str(output_dir / "ue_render_result.json"),
            "force_reload_package": True,
        }
        render_result = execute_automation_render(
            args,
            output_dir / "automation_spec.json",
            output_dir / "unreal_automation.log",
            spec,
        )
        ue_result = render_result["ue_result"]
        editor_exit_code = render_result["editor_exit_code"]
        ue_log_path = Path(render_result["log_path"])
        render_chain = {
            "final_render_mode": "asset",
            "asset_render": render_result,
        }
    else:
        bootstrap_candidate_path = add_name_suffix(candidate_path, ".bootstrap")
        generate_spec = {
            "mode": mode_map[args.mode],
            "source_path": args.source,
            "node_id": args.node,
            "output_path": primary_game_output,
            "render_png_path": str(bootstrap_candidate_path),
            "result_json_path": str(output_dir / "ue_generate_result.json"),
        }
        generate_result = execute_automation_render(
            args,
            output_dir / "automation_generate_spec.json",
            output_dir / "unreal_generate.log",
            generate_spec,
        )
        generated_asset_path = generate_result["ue_result"].get("generated_asset_path", "")
        if not generated_asset_path:
            raise RuntimeError("Generation pass did not produce a generated_asset_path")

        asset_spec = {
            "mode": "asset",
            "source_path": generated_asset_path,
            "node_id": "",
            "output_path": primary_game_output,
            "render_png_path": str(candidate_path),
            "result_json_path": str(output_dir / "ue_render_result.json"),
            "force_reload_package": True,
        }
        asset_result = execute_automation_render(
            args,
            output_dir / "automation_asset_spec.json",
            output_dir / "unreal_asset_render.log",
            asset_spec,
        )
        ue_result = asset_result["ue_result"]
        editor_exit_code = asset_result["editor_exit_code"]
        ue_log_path = Path(asset_result["log_path"])
        render_chain = {
            "final_render_mode": "asset_reload",
            "generation": generate_result,
            "asset_render": asset_result,
        }

    if not candidate_path.is_file():
        raise RuntimeError(f"Candidate render png not found: {candidate_path}")
    bootstrap_candidate_path = add_name_suffix(candidate_path, ".bootstrap")
    render_chain["generation_vs_asset"] = compare_image_paths(bootstrap_candidate_path, candidate_path, args.pixel_tolerance)
    render_chain["generation_vs_asset_structure"] = compare_structure_snapshots(
        get_result_structure(render_chain.get("generation"), "asset_structure"),
        get_result_structure(render_chain.get("asset_render"), "asset_structure"),
    )
    render_chain["native_self_check"] = run_native_self_check(
        args,
        output_dir,
        mode_map,
        game_output_root,
        expected_design_size,
        expected_design_size,
        args.source,
        args.node,
        generated_asset_path,
    )

    reference_validation = apply_render_size_authority(reference_context, reference_path, ue_result, candidate_path)
    reference_authority = reference_context.get("reference_authority") or {}
    if args.mode == "pen" and reference_validation["valid"] and reference_authority.get("available"):
        if reference_authority.get("verification_level") not in {"strong", "external_export_nodes"}:
            report_path = write_invalid_reference_report(
                output_dir,
                args,
                reference_path,
                candidate_path,
                ue_result,
                editor_exit_code,
                reference_validation,
                reference_context,
                render_chain,
                verdict="unverified_reference",
            )
            finalize_single_run_history(output_dir, args, report_path)
            print(f"[UNVERIFIED_REFERENCE] report={report_path}")
            print(f"reference={reference_path}")
            print(f"candidate={candidate_path}")
            print(f"log={ue_log_path}")
            sys.exit(2)

    if not reference_validation["valid"]:
        report_path = write_invalid_reference_report(
            output_dir,
            args,
            reference_path,
            candidate_path,
            ue_result,
            editor_exit_code,
            reference_validation,
            reference_context,
            render_chain,
        )
        finalize_single_run_history(output_dir, args, report_path)
        print(f"[INVALID_REFERENCE] report={report_path}")
        print(f"reference={reference_path}")
        print(f"candidate={candidate_path}")
        print(f"log={ue_log_path}")
        sys.exit(2)

    report_path = compare_and_report(
        args,
        output_dir,
        reference_path,
        candidate_path,
        ue_result,
        editor_exit_code,
        reference_context,
        render_chain,
    )
    finalize_single_run_history(output_dir, args, report_path)

    report = load_json_document(report_path)
    structure_validation = report.get("structure_validation") or {}
    structure_passed = structure_validation.get("passed") if structure_validation.get("required") else True
    status = "STRUCTURE_OK" if structure_passed else "STRUCTURE_MISMATCH"
    print(f"[{status}] report={report_path}")
    print(f"reference={reference_path}")
    print(f"candidate={candidate_path}")
    print(f"log={ue_log_path}")
    sys.exit(0 if structure_passed else 1)


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        sys.exit(2)

