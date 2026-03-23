import argparse
import base64
import glob
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageStat


STABLE_VISUAL_FILTER = "closing_max3_min3"


def parse_numeric_dimension(value):
    if isinstance(value, (int, float)):
        return float(value)
    return None


def load_json_document(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


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


def extract_node_numeric_size(node: dict) -> tuple[float | None, float | None]:
    if not isinstance(node, dict):
        return None, None

    width = parse_numeric_dimension(node.get("width"))
    height = parse_numeric_dimension(node.get("height"))
    if width and height and width > 1 and height > 1:
        return width, height

    size = node.get("size")
    if isinstance(size, dict):
        width = width or parse_numeric_dimension(size.get("x"))
        height = height or parse_numeric_dimension(size.get("y"))
        if width and height and width > 1 and height > 1:
            return width, height

    bbox = node.get("absoluteBoundingBox")
    if isinstance(bbox, dict):
        width = width or parse_numeric_dimension(bbox.get("width"))
        height = height or parse_numeric_dimension(bbox.get("height"))
        if width and height and width > 1 and height > 1:
            return width, height

    return width, height


def infer_node_size_from_children(node: dict) -> tuple[int, int] | None:
    children = node.get("children", [])
    if not isinstance(children, list) or not children:
        return None

    max_right = 0.0
    max_bottom = 0.0
    found_child_extent = False
    for child in children:
        if not isinstance(child, dict):
            continue
        child_size = extract_node_size(child)
        if not child_size:
            continue
        child_x = parse_numeric_dimension(child.get("x")) or 0.0
        child_y = parse_numeric_dimension(child.get("y")) or 0.0
        max_right = max(max_right, child_x + child_size[0])
        max_bottom = max(max_bottom, child_y + child_size[1])
        found_child_extent = True

    if not found_child_extent:
        return None
    if max_right <= 1 or max_bottom <= 1:
        return None
    return round(max_right), round(max_bottom)


def extract_node_size(node: dict) -> tuple[int, int] | None:
    if not isinstance(node, dict):
        return None

    width, height = extract_node_numeric_size(node)
    inferred = infer_node_size_from_children(node)
    if inferred:
        inferred_width, inferred_height = inferred
        if not width or width <= 1:
            width = inferred_width
        if not height or height <= 1:
            height = inferred_height

    if width and height and width > 1 and height > 1:
        return round(width), round(height)

    return None


def get_expected_design_size(mode: str, source_path: str, node_id: str) -> tuple[int, int] | None:
    source = Path(source_path)
    if not source.is_file():
        return None
    try:
        document = load_json_document(source)
    except Exception:
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


def get_declared_design_size(mode: str, source_path: str, node_id: str) -> tuple[float | None, float | None]:
    source = Path(source_path)
    if not source.is_file():
        return None, None
    try:
        document = load_json_document(source)
    except Exception:
        return None, None

    if mode == "pen" and source.suffix.lower() == ".pen":
        target_node = find_node_by_id(document.get("children", []), node_id)
        return extract_node_numeric_size(target_node)

    if mode in {"local-figma", "figma-source"} and source.suffix.lower() == ".json":
        normalized_node_id = parse_figma_node_id(node_id)
        root = document.get("document", {})
        target_node = find_node_by_id(root.get("children", []), normalized_node_id)
        return extract_node_numeric_size(target_node)

    return None, None


def validate_reference_against_design(reference_size: tuple[int, int], expected_size: tuple[int, int] | None) -> dict:
    if not expected_size:
        return {
            "available": False,
            "valid": True,
            "reason": "",
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
    valid = aspect_delta <= 0.08
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


def parse_args():
    parser = argparse.ArgumentParser(description="Design2UMG full automation visual verification")
    parser.add_argument("mode", choices=["pen", "local-figma", "figma-source", "asset"], help="Verification mode")
    parser.add_argument("--source", required=True, help="Source file path or Widget Blueprint asset path")
    parser.add_argument("--node", default="", help="Target frame/node id")
    parser.add_argument("--reference-provider", choices=["png", "figma", "pencil", "pen-sim"], required=True)
    parser.add_argument("--reference", default="", help="Existing reference png path when provider=png")
    parser.add_argument("--figma-file", default="", help="Figma file key or URL")
    parser.add_argument("--figma-node", default="", help="Figma node id")
    parser.add_argument("--figma-token", default="", help="Figma personal access token")
    parser.add_argument("--mcp-server", default="", help="Override Pencil MCP server binary path")
    parser.add_argument("--pencil-app", default="", help="Override Pencil app name (default: vscode)")
    parser.add_argument("--output-dir", default="", help="Directory for render, diff and reports")
    parser.add_argument("--game-output", default="", help="UE asset output path, default is a unique automation path")
    parser.add_argument("--project", default="", help="Project path. Leave empty to auto-build a temporary HostProject from the engine plugin source.")
    parser.add_argument("--build-root", default="", help="Temporary BuildPlugin output directory. Leave empty to use the system temp directory.")
    parser.add_argument("--keep-build-root", action="store_true", help="Keep the generated BuildPlugin workspace for debugging.")
    parser.add_argument("--editor", default=r"G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe", help="UnrealEditor.exe path")
    parser.add_argument("--pixel-tolerance", type=int, default=6, help="Per-pixel max-channel tolerance")
    parser.add_argument("--changed-ratio-threshold", type=float, default=0.01, help="Max changed pixel ratio to pass")
    parser.add_argument("--mae-threshold", type=float, default=0.02, help="Max mean absolute error to pass")
    parser.add_argument("--log-cmds", default="", help="Optional Unreal -LogCmds payload, for example 'LogDesign2UMG Verbose'")
    return parser.parse_args()


def plugin_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_output_dir() -> Path:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return plugin_root() / "Saved" / "Design2UMG" / "Automation" / f"{stamp}_{os.getpid()}"


def default_build_root() -> Path:
    return Path(tempfile.gettempdir()) / f"Design2UMG_VerifyBuild_{os.getpid()}"


def resolve_engine_root(editor_path: str) -> Path:
    return Path(editor_path).resolve().parents[2]


def resolve_uat_path(editor_path: str) -> Path:
    return resolve_engine_root(editor_path) / "Build" / "BatchFiles" / "RunUAT.bat"


def contains_pluginbuild_segment(path: Path) -> bool:
    normalized_parts = {part.lower() for part in path.parts}
    return "_pluginbuild" in normalized_parts


def build_temporary_host_project(editor_path: str, build_root: Path) -> tuple[Path, Path]:
    ensure_clean_editor()
    if build_root.exists():
        shutil.rmtree(build_root, ignore_errors=True)
    build_root.mkdir(parents=True, exist_ok=True)

    plugin_file = plugin_root() / "Design2UMG.uplugin"
    uat_path = resolve_uat_path(editor_path)
    command = [
        str(uat_path),
        "BuildPlugin",
        f"-Plugin={plugin_file}",
        f"-Package={build_root}",
        "-TargetPlatforms=Win64",
        "-Rocket",
        "-StrictIncludes",
        "-NoDeleteHostProject",
    ]
    subprocess.run(command, check=True, cwd=resolve_engine_root(editor_path))

    project_path = build_root / "HostProject" / "HostProject.uproject"
    project_plugin_root = build_root / "HostProject" / "Plugins" / "Design2UMG"
    if not project_path.is_file():
        raise RuntimeError(f"BuildPlugin did not produce HostProject: {project_path}")
    if not project_plugin_root.is_dir():
        raise RuntimeError(f"BuildPlugin did not produce project plugin root: {project_plugin_root}")
    return project_path, project_plugin_root


def prepare_runtime_project(args) -> tuple[str, Path, Path | None]:
    requested_project = Path(args.project).resolve() if args.project else None
    if requested_project:
        if contains_pluginbuild_segment(requested_project):
            raise RuntimeError(
                "Refusing to run verification against a repository-local _PluginBuild project. "
                "Use the engine plugin as the single source of truth and let this script auto-build a temporary HostProject."
            )
        requested_plugin_root = requested_project.parent / "Plugins" / "Design2UMG"
        project_plugin_root = requested_plugin_root if requested_plugin_root.is_dir() else plugin_root()
        return str(requested_project), project_plugin_root, None

    build_root = Path(args.build_root).resolve() if args.build_root else default_build_root()
    if contains_pluginbuild_segment(build_root):
        raise RuntimeError(f"Refusing to create verification workspace under _PluginBuild: {build_root}")
    project_path, project_plugin_root = build_temporary_host_project(args.editor, build_root)
    return str(project_path), project_plugin_root, build_root


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
    project_manifest.write_text(json.dumps(project_data, ensure_ascii=False, indent=4), encoding="utf-8-sig")


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
                    workspace_root = project_root.parent
                    for host_fonts_dir in workspace_root.glob("*/HostProject/Content/Design2UMG/Fonts"):
                        candidate_dirs.append(host_fonts_dir)
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
        return sorted(all_matches, reverse=True)[0]
    raise FileNotFoundError("Pencil MCP server binary not found; use --mcp-server to override")


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


def request_json(url: str, payload: dict, headers: dict[str, str]) -> tuple[str, dict]:
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.headers.get("Mcp-Session-Id", ""), response.read().decode("utf-8")


class PencilMcpClient:
    def __init__(self, server_path: str, app_name: str):
        self.server_path = server_path
        self.app_name = app_name or "vscode"
        self.port = find_available_port()
        self.endpoint = f"http://127.0.0.1:{self.port}/mcp"
        self.session_id = ""
        self.process: subprocess.Popen | None = None

    def __enter__(self):
        args = [self.server_path, "-app", self.app_name, "-http", "-http-port", str(self.port)]
        self.process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 10.0
        while time.time() < deadline:
            try:
                self.connect()
                return self
            except Exception:
                time.sleep(0.2)
        raise RuntimeError(f"Failed to connect to Pencil MCP server at {self.endpoint}")

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

    def call_tool_content(self, name: str, arguments: dict) -> list[dict]:
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
        _, body = request_json(self.endpoint, payload, headers)
        data_lines = [line[5:].strip() for line in body.splitlines() if line.startswith("data:")]
        content = data_lines[-1] if data_lines else body
        rpc = json.loads(content)
        if "error" in rpc:
            message = rpc["error"].get("message", f"Tool call failed: {name}")
            raise RuntimeError(message)
        result = rpc.get("result", {})
        parts = result.get("content", [])
        return parts

    def call_tool(self, name: str, arguments: dict):
        parts = self.call_tool_content(name, arguments)
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


def ensure_pencil_document_open(client: PencilMcpClient, source_path: str):
    normalized_target = str(Path(source_path).resolve()).lower().replace("/", "\\")
    active_document = get_active_pencil_document(client).lower().replace("/", "\\")
    if active_document == normalized_target:
        return

    try:
        client.call_tool_content("open_document", {"filePathOrTemplate": source_path})
    except Exception:
        # Some Pencil builds time out even though the document switch still succeeds.
        pass

    deadline = time.time() + 15.0
    while time.time() < deadline:
        active_document = get_active_pencil_document(client).lower().replace("/", "\\")
        if active_document == normalized_target:
            return
        time.sleep(0.5)
    raise RuntimeError(f"Pencil did not switch to document: {source_path}")


def export_pencil_reference_png(client: PencilMcpClient, source_path: str, node_id: str, output_dir: Path) -> Path | None:
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


def compare_image_paths(left_path: Path, right_path: Path, pixel_tolerance: int) -> dict | None:
    if not left_path.is_file() or not right_path.is_file():
        return None
    comparison = build_diff(load_rgba(left_path), load_rgba(right_path), pixel_tolerance)
    return {
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
        with PencilMcpClient(mcp_server, app_name) as client:
            ensure_pencil_document_open(client, source_path)
            parts = client.call_tool_content(
                "batch_get",
                {
                    "filePath": source_path,
                    "patterns": [{"name": f"^{re.escape(search_name)}$"}],
                    "searchDepth": 6,
                    "readDepth": 12,
                    "resolveInstances": True,
                    "resolveVariables": False,
                },
            )
    except Exception:
        return None

    parsed = _parse_tool_text_json(parts)
    if not isinstance(parsed, list):
        return None

    target_node = _find_pencil_node_by_id(parsed, node_id)
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
    except Exception:
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
    }


def validate_structure_consistency(args, ue_result: dict, render_chain: dict, reference_context: dict | None = None) -> dict:
    generation_result = render_chain.get("generation")
    asset_result = render_chain.get("asset_render")
    live_source_snapshot = (reference_context or {}).get("source_structure_live")
    source_snapshot = live_source_snapshot or get_result_structure(generation_result, "source_structure") or get_result_structure(ue_result, "source_structure")
    generated_asset_snapshot = get_result_structure(generation_result, "asset_structure")
    asset_snapshot = get_result_structure(asset_result, "asset_structure") or get_result_structure(ue_result, "asset_structure")

    required = args.mode == "pen"
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
        "generation_vs_asset": compare_structure_snapshots(generated_asset_snapshot, asset_snapshot),
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
    source_texts = normalize_structure_texts(source_snapshot.get("visible_texts"))
    asset_texts = normalize_structure_texts(asset_snapshot.get("visible_texts"))
    source_text_counter = Counter(source_texts)
    asset_text_counter = Counter(asset_texts)

    min_total_required = 1 if source_total <= 1 else max(2, int((source_total * 0.05) + 0.9999))
    min_leaf_required = 1 if source_leaf <= 1 else max(1, int((source_leaf * 0.05) + 0.9999))

    if source_text_counter != asset_text_counter:
        result["reasons"].append("visible_text_mismatch")
    if source_total >= 2 and asset_total < min_total_required:
        result["reasons"].append("asset_total_nodes_collapsed")
    if source_leaf >= 2 and asset_leaf < min_leaf_required:
        result["reasons"].append("asset_leaf_nodes_collapsed")
    if source_container > 0 and asset_container == 0:
        result["reasons"].append("asset_container_missing")
    if source_total >= 8 and asset_total <= 3:
        result["reasons"].append("asset_tree_too_small")
    if source_total >= 8 and not asset_texts and asset_visual <= 1 and asset_total <= 3:
        result["reasons"].append("single_visual_cheat_suspected")
    if source_visual >= 12 and asset_visual <= 1:
        result["reasons"].append("visual_structure_collapsed")

    result["metrics"] = {
        "source_total_nodes": source_total,
        "asset_total_nodes": asset_total,
        "source_leaf_nodes": source_leaf,
        "asset_leaf_nodes": asset_leaf,
        "source_container_nodes": source_container,
        "asset_container_nodes": asset_container,
        "source_visual_nodes": source_visual,
        "asset_visual_nodes": asset_visual,
        "min_total_required": min_total_required,
        "min_leaf_required": min_leaf_required,
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
    }

    mcp_server = discover_mcp_server(server_path)
    authority_dir = output_dir / "reference_authority"
    export_dir = authority_dir / "export_nodes"
    screenshot_path = authority_dir / "get_screenshot.png"
    with PencilMcpClient(mcp_server, app_name) as client:
        try:
            ensure_pencil_document_open(client, source_path)
        except Exception:
            pass
        export_path = export_pencil_reference_png(client, source_path, node_id, export_dir)
        witness_path = None
        try:
            witness_path = screenshot_pencil_reference_png(client, source_path, node_id, screenshot_path)
        except Exception:
            witness_path = None

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
        authority["get_screenshot_path"] = str(witness_path)
        authority["get_screenshot_validation"] = validate_reference_against_design(
            read_image_size(witness_path),
            expected_design_size,
        )

    if not authority["available"]:
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

    return authority


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


def prepare_pen_sim_reference(source_path: str, node_id: str, output_path: Path) -> Path:
    script_path = plugin_root() / "Tools" / "pen2umg_sim.py"
    command = [sys.executable, str(script_path), source_path, "--frame", node_id, "--render"]
    subprocess.run(command, check=True)
    expected = Path(source_path).parent / f"sim_layout_{node_id.replace(' ', '_')}.png"
    if not expected.is_file():
        pngs = sorted(Path(source_path).parent.glob("sim_layout_*.png"), key=lambda item: item.stat().st_mtime, reverse=True)
        if not pngs:
            raise RuntimeError("pen2umg_sim.py did not produce a simulated layout image")
        expected = pngs[0]
    shutil.copyfile(expected, output_path)
    return output_path


def load_rgba(path: Path) -> Image.Image:
    image = Image.open(path)
    if image.mode != "RGBA":
        image = image.convert("RGBA")
    return image


def apply_stable_visual_tolerance(image: Image.Image) -> Image.Image:
    return image.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))


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

    raw_metrics = measure_canvas_diff(reference_canvas, candidate_canvas, pixel_tolerance)
    stable_metrics = measure_canvas_diff(
        apply_stable_visual_tolerance(reference_canvas),
        apply_stable_visual_tolerance(candidate_canvas),
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


def run_editor(editor_path: str, project_path: str, spec_path: Path, log_path: Path, log_cmds: str = "") -> int:
    exec_cmd = f"Design2UMG.AutomationRender {spec_path.as_posix()}"
    command = [
        editor_path,
        project_path,
        "-unattended",
        "-nop4",
        "-nosplash",
        "-stdout",
        "-FullStdOutLogOutput",
        f"-ExecCmds={exec_cmd}",
    ]
    if log_cmds:
        command.append(f"-LogCmds={log_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8-sig") as handle:
        process = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, check=False)
    return process.returncode


def read_image_size(path: Path) -> tuple[int, int]:
    image = Image.open(path)
    return image.width, image.height


def write_json(path: Path, payload: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8-sig") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)


def load_ue_result(path: Path) -> dict:
    with open(path, "r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def add_name_suffix(path: Path, suffix: str) -> Path:
    return path.with_name(f"{path.stem}{suffix}{path.suffix}")


def execute_automation_render(args, spec_path: Path, log_path: Path, spec: dict):
    result_path = Path(spec["result_json_path"])
    write_json(spec_path, spec)
    sync_project_plugin_build_id(args.editor, Path(args.project_plugin_root))
    ensure_clean_editor()
    editor_exit_code = run_editor(args.editor, args.project, spec_path, log_path, args.log_cmds)
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
    game_output: str,
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
            "output_path": game_output,
            "render_png_path": str(native_path),
            "result_json_path": str(output_dir / "ue_native_render_result.json"),
            "render_width": native_width,
            "render_height": native_height,
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
        "output_path": game_output,
        "render_png_path": str(native_bootstrap_path),
        "result_json_path": str(output_dir / "ue_native_generate_result.json"),
        "render_width": native_width,
        "render_height": native_height,
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
        "output_path": game_output,
        "render_png_path": str(native_path),
        "result_json_path": str(output_dir / "ue_native_render_result.json"),
        "render_width": native_width,
        "render_height": native_height,
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
        canonical_path = authority.get("canonical_path")
        if not canonical_path:
            raise RuntimeError("Pencil authority reference did not produce a usable PNG")
        shutil.copyfile(canonical_path, reference_png)
        context["canonical_source"] = authority.get("canonical_source")
        return reference_png, context
    if args.reference_provider == "pen-sim":
        prepare_pen_sim_reference(args.source, args.node, reference_png)
        context["canonical_source"] = "pen_sim"
        return reference_png, context
    raise ValueError(f"Unsupported reference provider: {args.reference_provider}")


def finalize_reference_context(args, reference_path: Path, reference_context: dict):
    effective_provider = reference_context.get("canonical_source") or args.reference_provider
    reference_context["requested_reference_provider"] = args.reference_provider
    reference_context["effective_reference_provider"] = effective_provider
    reference_context["effective_reference_path"] = str(reference_path)
    reference_context["provided_reference_dirty"] = assess_provided_reference_dirty(
        reference_context.get("provided_vs_canonical")
    )


def infer_expected_design_size_from_reference(
    declared_size: tuple[float | None, float | None],
    reference_size: tuple[int, int],
) -> tuple[int, int] | None:
    declared_width, declared_height = declared_size
    reference_width, reference_height = reference_size
    if reference_width <= 0 or reference_height <= 0:
        return None

    if declared_width and declared_width > 1 and declared_height and declared_height > 1:
        return round(declared_width), round(declared_height)

    inferred_width = round(declared_width) if declared_width and declared_width > 1 else None
    inferred_height = round(declared_height) if declared_height and declared_height > 1 else None

    if inferred_width and not inferred_height:
        scale = reference_width / inferred_width
        if scale > 0:
            inferred_height = round(reference_height / scale)
    elif inferred_height and not inferred_width:
        scale = reference_height / inferred_height
        if scale > 0:
            inferred_width = round(reference_width / scale)

    if inferred_width and inferred_height and inferred_width > 1 and inferred_height > 1:
        return inferred_width, inferred_height
    return None


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
    comparison = build_diff(reference_image, candidate_image, args.pixel_tolerance)
    structure_validation = validate_structure_consistency(args, ue_result, render_chain, reference_context)

    diff_preview_path = output_dir / "diff_overlay.png"
    comparison["preview"].save(diff_preview_path)

    visual_passed = (
        comparison["size_match"]
        and comparison["changed_ratio"] <= args.changed_ratio_threshold
        and comparison["mean_abs_error"] <= args.mae_threshold
    )
    structure_passed = structure_validation["passed"] if structure_validation.get("required") else True
    passed = visual_passed and structure_passed
    verdict = "passed"
    if not visual_passed and not structure_passed:
        verdict = "visual_and_structure_mismatch"
    elif not visual_passed:
        verdict = "visual_mismatch"
    elif not structure_passed:
        verdict = "structure_mismatch"

    report = {
        "passed": passed,
        "visual_passed": visual_passed,
        "verdict": verdict,
        "editor_exit_code": editor_exit_code,
        "reference_provider": args.reference_provider,
        "effective_reference_provider": reference_context.get("effective_reference_provider"),
        "effective_reference_path": reference_context.get("effective_reference_path"),
        "reference_png_path": str(reference_path),
        "candidate_png_path": str(candidate_path),
        "diff_overlay_path": str(diff_preview_path),
        "pixel_tolerance": args.pixel_tolerance,
        "changed_ratio_threshold": args.changed_ratio_threshold,
        "mae_threshold": args.mae_threshold,
        "stable_visual_filter": comparison["stable_visual_filter"],
        "changed_ratio": comparison["changed_ratio"],
        "stable_changed_ratio": comparison["stable_changed_ratio"],
        "mean_abs_error": comparison["mean_abs_error"],
        "stable_mean_abs_error": comparison["stable_mean_abs_error"],
        "size_match": comparison["size_match"],
        "reference_size": comparison["reference_size"],
        "candidate_size": comparison["candidate_size"],
        "diff_bbox": comparison["diff_bbox"],
        "stable_diff_bbox": comparison["stable_diff_bbox"],
        "ue_result": ue_result,
        "reference_context": reference_context,
        "render_chain": render_chain,
        "structure_validation": structure_validation,
    }
    report_path = output_dir / "compare_report.json"
    write_json(report_path, report)
    return passed, report_path


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
        "passed": False,
        "verdict": verdict,
        "compare_skipped": True,
        "editor_exit_code": editor_exit_code,
        "reference_provider": args.reference_provider,
        "effective_reference_provider": reference_context.get("effective_reference_provider"),
        "effective_reference_path": reference_context.get("effective_reference_path"),
        "reference_png_path": str(reference_path),
        "candidate_png_path": str(candidate_path),
        "pixel_tolerance": args.pixel_tolerance,
        "changed_ratio_threshold": args.changed_ratio_threshold,
        "mae_threshold": args.mae_threshold,
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
    output_dir = Path(args.output_dir) if args.output_dir else default_output_dir()
    output_dir.mkdir(parents=True, exist_ok=True)
    runtime_build_root = None

    try:
        args.project, project_plugin_root, runtime_build_root = prepare_runtime_project(args)
        args.project_plugin_root = str(project_plugin_root)
        seed_runtime_font_assets(args.project, [args.source])
        expected_design_size = get_expected_design_size(args.mode, args.source, args.node)
        declared_design_size = get_declared_design_size(args.mode, args.source, args.node)
        reference_path, reference_context = prepare_reference(args, output_dir, expected_design_size)
        finalize_reference_context(args, reference_path, reference_context)
        if args.mode == "pen":
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
        if not expected_design_size:
            inferred_expected_size = infer_expected_design_size_from_reference(declared_design_size, reference_size)
            if inferred_expected_size:
                expected_design_size = inferred_expected_size
                reference_context["inferred_expected_design_size"] = list(inferred_expected_size)
        reference_validation = validate_reference_against_design(reference_size, expected_design_size)
        reference_context["reference_validation"] = reference_validation
        if reference_validation["valid"]:
            render_width, render_height = reference_size
        elif expected_design_size:
            render_width, render_height = expected_design_size
        else:
            render_width, render_height = reference_size

        candidate_filename = "candidate.png" if reference_validation["valid"] else "candidate.native.png"
        candidate_path = output_dir / candidate_filename
        game_output = args.game_output or f"/Game/Design2UMG/Automation/{output_dir.name}"
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
                "output_path": game_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "render_width": render_width,
                "render_height": render_height,
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
                "output_path": game_output,
                "render_png_path": str(bootstrap_candidate_path),
                "result_json_path": str(output_dir / "ue_generate_result.json"),
                "render_width": render_width,
                "render_height": render_height,
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
                "output_path": game_output,
                "render_png_path": str(candidate_path),
                "result_json_path": str(output_dir / "ue_render_result.json"),
                "render_width": render_width,
                "render_height": render_height,
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
            game_output,
            expected_design_size,
            (render_width, render_height),
            args.source,
            args.node,
            generated_asset_path,
        )

        reference_authority = reference_context.get("reference_authority") or {}
        if args.mode == "pen" and reference_validation["valid"] and reference_authority.get("available"):
            if reference_authority.get("verification_level") != "strong":
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
            print(f"[INVALID_REFERENCE] report={report_path}")
            print(f"reference={reference_path}")
            print(f"candidate={candidate_path}")
            print(f"log={ue_log_path}")
            sys.exit(2)

        passed, report_path = compare_and_report(
            args,
            output_dir,
            reference_path,
            candidate_path,
            ue_result,
            editor_exit_code,
            reference_context,
            render_chain,
        )

        status = "PASS" if passed else "FAIL"
        print(f"[{status}] report={report_path}")
        print(f"reference={reference_path}")
        print(f"candidate={candidate_path}")
        print(f"log={ue_log_path}")
        sys.exit(0 if passed else 1)
    finally:
        if runtime_build_root and not args.keep_build_root:
            shutil.rmtree(runtime_build_root, ignore_errors=True)


if __name__ == "__main__":
    main()

