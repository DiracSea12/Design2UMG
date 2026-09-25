"""Pencil application discovery and JSON-RPC subprocess communication."""

import csv
import glob
import json
import os
import queue
import re
import shutil
import subprocess
import threading
import time
from collections import deque
from pathlib import Path


def discover_mcp_server(override_path: str) -> str:
    if override_path:
        return override_path
    user_home = Path(os.environ.get("USERPROFILE", ""))
    pattern = str(user_home / ".*" / "extensions" / "highagency.pencildev-*" / "out" / "mcp-server-windows-x64.exe")
    all_matches = glob.glob(pattern)
    if all_matches:
        def version_key(path_str: str):
            # Parse the version from the path text so Windows and POSIX separators
            # both work; Path(parents) indexing depends on the host platform.
            match = re.search(r"highagency\.pencildev-(\d+(?:\.\d+)*)", path_str.replace("\\", "/"))
            if not match:
                return ()
            return tuple(int(token) for token in match.group(1).split("."))

        return max(all_matches, key=version_key)
    raise FileNotFoundError("Pencil MCP server binary not found; use --mcp-server to override")


PENCIL_APP_PROCESS_CANDIDATES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("visual_studio_code", ("Code.exe", "code")),
    ("vscode", ("Code.exe", "code")),
    ("cursor", ("Cursor.exe", "cursor")),
    ("antigravity", ("Antigravity.exe", "antigravity")),
    ("codium", ("VSCodium.exe", "codium")),
)

PENCIL_APP_EDITOR_COMMANDS = {
    "visual_studio_code": "code",
    "vscode": "code",
    "cursor": "cursor",
    "antigravity": "antigravity",
    "codium": "codium",
}


def read_pencil_registered_apps() -> set[str]:
    user_home = Path(os.environ.get("USERPROFILE", ""))
    apps_dir = user_home / ".pencil" / "apps"
    if not apps_dir.is_dir():
        return set()
    return {path.name.lower() for path in apps_dir.iterdir() if path.is_file()}


def synchronize_pencil_vscode_alias() -> bool:
    user_home = Path(os.environ.get("USERPROFILE") or os.environ.get("HOME", ""))
    apps_dir = user_home / ".pencil" / "apps"
    source_path = apps_dir / "visual_studio_code"
    if not source_path.is_file():
        return False

    alias_path = apps_dir / "vscode"
    source_bytes = source_path.read_bytes()
    if alias_path.is_file() and alias_path.read_bytes() == source_bytes:
        return True

    shutil.copy2(source_path, alias_path)
    return True


def is_windows_process_running(process_names: tuple[str, ...]) -> bool:
    for process_name in process_names:
        try:
            result = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {process_name}"],
                check=False,
                capture_output=True,
                text=True,
                errors="replace",
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
            errors="replace",
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


class PencilMcpClient:
    def __init__(self, server_path: str, app_name: str, preferred_document_path: str = ""):
        self.server_path = server_path
        self.app_candidates = discover_pencil_app_candidates(app_name, preferred_document_path)
        self.app_name = self.app_candidates[0]
        self.process: subprocess.Popen | None = None
        self._stdout_messages: queue.Queue[str | None] = queue.Queue()
        self._stderr_lines: deque[str] = deque(maxlen=50)
        self._stderr_thread: threading.Thread | None = None
        self._request_id = 0
        self._tool_names: set[str] | None = None

    def __enter__(self):
        errors: list[str] = []
        for candidate in self.app_candidates:
            self.app_name = candidate
            try:
                self._start_process()
                self.connect()
                return self
            except Exception as exc:
                errors.append(f"{candidate}: {exc}")
                self.__exit__(None, None, None)
        raise RuntimeError(f"Failed to connect to Pencil MCP server ({'; '.join(errors)})")

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        if self.process:
            for pipe_name in ("stdin", "stdout", "stderr"):
                pipe = getattr(self.process, pipe_name, None)
                if pipe:
                    try:
                        pipe.close()
                    except (OSError, ValueError):
                        pass
        self.process = None

    def _start_process(self):
        synchronize_pencil_vscode_alias()
        self._stdout_messages = queue.Queue()
        self._stderr_lines.clear()
        self.process = subprocess.Popen(
            [self.server_path, "-app", self.app_name],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        threading.Thread(
            target=self._read_stdout,
            args=(self.process.stdout, self._stdout_messages),
            daemon=True,
        ).start()
        self._stderr_thread = threading.Thread(
            target=self._read_stderr,
            args=(self.process.stderr, self._stderr_lines),
            daemon=True,
        )
        self._stderr_thread.start()

    @staticmethod
    def _read_stdout(pipe, messages: queue.Queue[str | None]):
        try:
            for line in iter(pipe.readline, ""):
                messages.put(line)
        finally:
            messages.put(None)

    @staticmethod
    def _read_stderr(pipe, stderr_lines: deque[str]):
        for line in iter(pipe.readline, ""):
            stripped = line.strip()
            if stripped:
                stderr_lines.append(stripped)

    def _process_details(self) -> str:
        if self.process and self.process.poll() is not None and self._stderr_thread:
            self._stderr_thread.join(timeout=0.2)
        details = []
        if self.process and self.process.poll() is not None:
            details.append(f"exit_code={self.process.returncode}")
        if self._stderr_lines:
            details.append(f"stderr={' | '.join(self._stderr_lines)}")
        return ", ".join(details)

    def _send(self, payload: dict):
        if not self.process or not self.process.stdin:
            raise RuntimeError("Pencil MCP process is not running")
        if self.process.poll() is not None:
            raise RuntimeError(f"Pencil MCP process exited before request ({self._process_details()})")
        try:
            self.process.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError) as exc:
            details = self._process_details()
            raise RuntimeError(f"Failed to write Pencil MCP request{f' ({details})' if details else ''}") from exc

    def _request(self, method: str, params: dict, timeout_sec: float = 30.0) -> dict:
        self._request_id += 1
        request_id = self._request_id
        self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout_sec
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                details = self._process_details()
                raise RuntimeError(
                    f"Timed out waiting for Pencil MCP response to {method}"
                    f"{f' ({details})' if details else ''}"
                )
            try:
                line = self._stdout_messages.get(timeout=remaining)
            except queue.Empty as exc:
                details = self._process_details()
                raise RuntimeError(
                    f"Timed out waiting for Pencil MCP response to {method}"
                    f"{f' ({details})' if details else ''}"
                ) from exc
            if line is None:
                details = self._process_details()
                raise RuntimeError(
                    f"Pencil MCP process closed stdout while waiting for {method}"
                    f"{f' ({details})' if details else ''}"
                )
            try:
                response = json.loads(line)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Pencil MCP returned invalid JSON: {line.strip()}") from exc
            if response.get("id") != request_id:
                continue
            if "error" in response:
                error = response["error"]
                message = error.get("message", str(error)) if isinstance(error, dict) else str(error)
                raise RuntimeError(f"Pencil MCP {method} failed: {message}")
            result = response.get("result")
            if not isinstance(result, dict):
                raise RuntimeError(f"Pencil MCP {method} returned no result object")
            return result

    def connect(self):
        self._request(
            "initialize",
            {
                "clientInfo": {"name": "Design2UMGAutomation", "version": "1.0.0"},
                "protocolVersion": "2024-11-05",
                "capabilities": {},
            },
            timeout_sec=10.0,
        )
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def call_tool_content(self, name: str, arguments: dict, timeout_sec: float = 30.0) -> list[dict]:
        result = self._request(
            "tools/call",
            {"name": name, "arguments": arguments},
            timeout_sec=timeout_sec,
        )
        if result.get("isError"):
            messages = [part.get("text", "") for part in result.get("content", []) if part.get("type") == "text"]
            raise RuntimeError("; ".join(filter(None, messages)) or f"Pencil tool failed: {name}")
        parts = result.get("content", [])
        return parts if isinstance(parts, list) else []

    def call_tool(self, name: str, arguments: dict, timeout_sec: float = 30.0):
        parts = self.call_tool_content(name, arguments, timeout_sec=timeout_sec)
        if not parts:
            return ""
        return parts[0].get("text", "")

    def available_tools(self) -> set[str]:
        if self._tool_names is None:
            result = self._request("tools/list", {}, timeout_sec=15.0)
            self._tool_names = {str(tool.get("name")) for tool in result.get("tools", []) if tool.get("name")}
        return self._tool_names

    def has_tool(self, name: str) -> bool:
        return name in self.available_tools()
