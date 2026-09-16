import design2umg_auto_verify as verify
from design2umg_verify_lib import pencil_mcp
import io
import json
import os
import queue
import tempfile
from pathlib import Path


class FakeClient:
    def __init__(self, states, app_name="cursor"):
        self.states = list(states)
        self.app_name = app_name
        self.state_index = 0

    def has_tool(self, name):
        return name == "get_editor_state"

    def _current_state(self):
        if not self.states:
            return ""
        index = min(self.state_index, len(self.states) - 1)
        value = self.states[index]
        if self.state_index < len(self.states) - 1:
            self.state_index += 1
        return value

    def call_tool_content(self, name, arguments):
        if name == "get_editor_state":
            active = self._current_state()
            return [{"type": "text", "text": f"## Currently active editor\n- `{active}`\n"}]
        raise AssertionError(f"unexpected tool: {name}")


def assert_equal(left, right):
    assert left == right, f"expected {right!r}, got {left!r}"


def assert_contains(text, needle):
    assert needle in text, f"expected {needle!r} in {text!r}"


def test_current_pencil_icon_contract():
    current_icon = {"type": "icon", "icon": "search", "library": "lucide", "fontSize": 20}
    missing_library = {"type": "icon", "icon": "search", "fontSize": 20}
    legacy_icon = {
        "type": "icon_font",
        "iconFontName": "search",
        "iconFontFamily": "lucide",
        "fontSize": 20,
    }

    assert verify.estimate_text_node_size(current_icon) is not None
    assert_equal(verify._classify_pencil_node_kind(current_icon), "text")
    assert verify.estimate_text_node_size(missing_library) is None
    assert_equal(verify._classify_pencil_node_kind(missing_library), "other")
    assert verify.estimate_text_node_size(legacy_icon) is None
    assert_equal(verify._classify_pencil_node_kind(legacy_icon), "other")


class FakeMcpOutput:
    def __init__(self):
        self.lines = queue.Queue()

    def push(self, payload):
        self.lines.put(json.dumps(payload) + "\n")

    def close(self):
        self.lines.put("")

    def readline(self):
        return self.lines.get(timeout=1.0)


class FakeMcpInput:
    def __init__(self, output):
        self.output = output

    def write(self, line):
        payload = json.loads(line)
        if payload["method"] == "initialize":
            self.output.push({"jsonrpc": "2.0", "id": payload["id"], "result": {}})
        elif payload["method"] == "tools/call":
            self.output.push({
                "jsonrpc": "2.0",
                "id": payload["id"],
                "result": {"content": [{"type": "text", "text": "tool result"}]},
            })
        return len(line)

    def flush(self):
        pass

    def close(self):
        pass


class FakeMcpProcess:
    def __init__(self, *, returncode=None, stderr=""):
        self.returncode = returncode
        self.stdout = FakeMcpOutput()
        self.stderr = io.StringIO(stderr)
        self.stdin = FakeMcpInput(self.stdout)

    def poll(self):
        return self.returncode

    def terminate(self):
        self.returncode = 0
        self.stdout.close()

    def wait(self, timeout=None):
        return self.returncode

    def kill(self):
        self.terminate()


def test_discover_mcp_server_searches_editor_extension_dirs():
    original_userprofile = os.environ.get("USERPROFILE")
    try:
        with tempfile.TemporaryDirectory() as temp_dir:
            old_extension_dir = Path(temp_dir) / ".old-editor" / "extensions" / "highagency.pencildev-0.6.35-universal" / "out"
            old_extension_dir.mkdir(parents=True)
            (old_extension_dir / "mcp-server-windows-x64.exe").touch()
            extension_dir = Path(temp_dir) / ".editor" / "extensions" / "highagency.pencildev-0.6.61-universal" / "out"
            extension_dir.mkdir(parents=True)
            server_path = extension_dir / "mcp-server-windows-x64.exe"
            server_path.touch()
            os.environ["USERPROFILE"] = temp_dir
            assert_equal(Path(pencil_mcp.discover_mcp_server("")), server_path)
    finally:
        if original_userprofile is None:
            os.environ.pop("USERPROFILE", None)
        else:
            os.environ["USERPROFILE"] = original_userprofile


def test_discover_mcp_server_prefers_latest_suffixed_version():
    original_glob = pencil_mcp.glob.glob
    old_server = r"C:\Users\test\.editor-a\extensions\highagency.pencildev-0.6.35-universal\out\mcp-server-windows-x64.exe"
    current_server = r"C:\Users\test\.editor-b\extensions\highagency.pencildev-0.6.61-universal\out\mcp-server-windows-x64.exe"
    try:
        pencil_mcp.glob.glob = lambda _pattern: [old_server, current_server]
        assert_equal(pencil_mcp.discover_mcp_server(""), current_server)
    finally:
        pencil_mcp.glob.glob = original_glob


def test_synchronize_pencil_vscode_alias_uses_temporary_home():
    saved_environment = {name: os.environ.get(name) for name in ("HOME", "USERPROFILE")}
    try:
        with tempfile.TemporaryDirectory() as temp_dir:
            os.environ["HOME"] = temp_dir
            os.environ["USERPROFILE"] = temp_dir
            apps_dir = Path(temp_dir) / ".pencil" / "apps"
            apps_dir.mkdir(parents=True)
            source_path = apps_dir / "visual_studio_code"
            alias_path = apps_dir / "vscode"

            source_path.write_text("first registration", encoding="utf-8")
            assert_equal(pencil_mcp.synchronize_pencil_vscode_alias(), True)
            assert_equal(alias_path.read_text(encoding="utf-8"), "first registration")

            source_path.write_text("updated registration", encoding="utf-8")
            assert_equal(pencil_mcp.synchronize_pencil_vscode_alias(), True)
            assert_equal(alias_path.read_text(encoding="utf-8"), "updated registration")

        with tempfile.TemporaryDirectory() as temp_dir:
            os.environ["HOME"] = temp_dir
            os.environ["USERPROFILE"] = temp_dir
            alias_path = Path(temp_dir) / ".pencil" / "apps" / "vscode"
            assert_equal(pencil_mcp.synchronize_pencil_vscode_alias(), False)
            assert_equal(alias_path.exists(), False)
    finally:
        for name, value in saved_environment.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def test_pencil_mcp_client_uses_stdio_transport():
    original_popen = pencil_mcp.subprocess.Popen
    original_discover = pencil_mcp.discover_pencil_app_candidates
    original_sync_alias = pencil_mcp.synchronize_pencil_vscode_alias
    captured_args = []
    startup_events = []
    process = FakeMcpProcess(stderr="[MCP] Starting server in stdio mode\n")

    def fake_popen(args, **kwargs):
        startup_events.append("process")
        captured_args.append((args, kwargs))
        return process

    try:
        pencil_mcp.subprocess.Popen = fake_popen
        pencil_mcp.discover_pencil_app_candidates = lambda _app, _path="": ["cursor"]
        pencil_mcp.synchronize_pencil_vscode_alias = lambda: startup_events.append("alias") or True
        with pencil_mcp.PencilMcpClient("mcp-server.exe", "cursor") as client:
            assert_equal(client.call_tool("get_editor_state", {}), "tool result")
    finally:
        pencil_mcp.subprocess.Popen = original_popen
        pencil_mcp.discover_pencil_app_candidates = original_discover
        pencil_mcp.synchronize_pencil_vscode_alias = original_sync_alias

    assert_equal(startup_events, ["alias", "process"])
    assert_equal(captured_args[0][0], ["mcp-server.exe", "-app", "cursor"])
    assert_equal(captured_args[0][1]["stdin"], pencil_mcp.subprocess.PIPE)
    assert_equal(captured_args[0][1]["stdout"], pencil_mcp.subprocess.PIPE)
    assert_equal(captured_args[0][1]["stderr"], pencil_mcp.subprocess.PIPE)


def test_pencil_mcp_client_reports_process_startup_error():
    original_popen = pencil_mcp.subprocess.Popen
    original_discover = pencil_mcp.discover_pencil_app_candidates
    original_sync_alias = pencil_mcp.synchronize_pencil_vscode_alias
    process = FakeMcpProcess(returncode=2, stderr="flag provided but not defined: -invalid\n")
    try:
        pencil_mcp.subprocess.Popen = lambda _args, **_kwargs: process
        pencil_mcp.discover_pencil_app_candidates = lambda _app, _path="": ["cursor"]
        pencil_mcp.synchronize_pencil_vscode_alias = lambda: False
        try:
            with pencil_mcp.PencilMcpClient("mcp-server.exe", "cursor"):
                pass
        except RuntimeError as exc:
            assert_contains(str(exc), "flag provided but not defined: -invalid")
            return
    finally:
        pencil_mcp.subprocess.Popen = original_popen
        pencil_mcp.discover_pencil_app_candidates = original_discover
        pencil_mcp.synchronize_pencil_vscode_alias = original_sync_alias
    raise AssertionError("expected Pencil MCP startup failure")


def test_order_pencil_app_candidates():
    ordered = pencil_mcp.order_pencil_app_candidates(
        {"visual_studio_code", "cursor", "antigravity"},
        {"cursor"},
    )
    assert_equal(ordered[0], "cursor")
    assert_equal(set(ordered), {"visual_studio_code", "cursor", "antigravity"})


def test_discover_pencil_app_candidates_prefers_document_host_then_foreground():
    original_read = pencil_mcp.read_pencil_registered_apps
    original_observations = pencil_mcp.describe_pencil_app_runtime_observations
    try:
        pencil_mcp.read_pencil_registered_apps = lambda: {"visual_studio_code", "cursor", "antigravity"}
        pencil_mcp.describe_pencil_app_runtime_observations = lambda _path="": {
            "visual_studio_code": {
                "running": True,
                "foreground": True,
                "document_match": False,
                "compatible_window": False,
                "window_titles": ["Design2UMG - Visual Studio Code"],
            },
            "vscode": {
                "running": False,
                "foreground": False,
                "document_match": False,
                "compatible_window": False,
                "window_titles": [],
            },
            "cursor": {
                "running": True,
                "foreground": False,
                "document_match": False,
                "compatible_window": False,
                "window_titles": ["Design2UMG - Cursor"],
            },
            "antigravity": {
                "running": True,
                "foreground": False,
                "document_match": True,
                "compatible_window": True,
                "window_titles": ["Design2UMG - Antigravity - pencil-shadcn.pen"],
            },
            "codium": {
                "running": False,
                "foreground": False,
                "document_match": False,
                "compatible_window": False,
                "window_titles": [],
            },
        }
        ordered = pencil_mcp.discover_pencil_app_candidates("", r"G:\SampleProject\Designs\pencil-shadcn.pen")
    finally:
        pencil_mcp.read_pencil_registered_apps = original_read
        pencil_mcp.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered[0], "antigravity")
    assert_equal(ordered[1], "visual_studio_code")


def test_discover_pencil_app_candidates_prefers_compatible_pen_window_without_document_hint():
    original_read = pencil_mcp.read_pencil_registered_apps
    original_observations = pencil_mcp.describe_pencil_app_runtime_observations
    try:
        pencil_mcp.read_pencil_registered_apps = lambda: {"visual_studio_code", "cursor", "antigravity"}
        pencil_mcp.describe_pencil_app_runtime_observations = lambda _path="": {
            "visual_studio_code": {
                "running": True,
                "foreground": True,
                "document_match": False,
                "compatible_window": False,
                "window_titles": ["UMGValueParser.cpp - Design2UMG - Visual Studio Code"],
            },
            "vscode": {
                "running": True,
                "foreground": True,
                "document_match": False,
                "compatible_window": False,
                "window_titles": ["UMGValueParser.cpp - Design2UMG - Visual Studio Code"],
            },
            "cursor": {
                "running": True,
                "foreground": False,
                "document_match": False,
                "compatible_window": False,
                "window_titles": ["stop-hook.sh - Design2UMG - Cursor"],
            },
            "antigravity": {
                "running": True,
                "foreground": False,
                "document_match": False,
                "compatible_window": True,
                "window_titles": ["Design2UMG - Antigravity - pencil-shadcn.pen"],
            },
            "codium": {
                "running": False,
                "foreground": False,
                "document_match": False,
                "compatible_window": False,
                "window_titles": [],
            },
        }
        ordered = pencil_mcp.discover_pencil_app_candidates("")
    finally:
        pencil_mcp.read_pencil_registered_apps = original_read
        pencil_mcp.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered[0], "antigravity")
    assert_equal(ordered[1], "visual_studio_code")


def test_discover_pencil_app_candidates_keeps_clean_fallback_without_runtime_hits():
    original_read = pencil_mcp.read_pencil_registered_apps
    original_observations = pencil_mcp.describe_pencil_app_runtime_observations
    try:
        pencil_mcp.read_pencil_registered_apps = lambda: set()
        pencil_mcp.describe_pencil_app_runtime_observations = lambda _path="": {}
        ordered = pencil_mcp.discover_pencil_app_candidates("")
    finally:
        pencil_mcp.read_pencil_registered_apps = original_read
        pencil_mcp.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered, ["visual_studio_code"])


def test_ensure_pencil_document_open_uses_current_contract():
    original_activate = verify.activate_pencil_document
    client = FakeClient([
        "pencil-new.pen",
        r"G:/SampleProject/Designs/pencil-nitro.pen",
    ])
    activation_calls = []
    try:
        verify.activate_pencil_document = lambda app, path: activation_calls.append((app, path))
        verify.ensure_pencil_document_open(
            client,
            r"G:\SampleProject\Designs\pencil-nitro.pen",
            timeout_sec=0.1,
            poll_interval=0.0,
        )
    finally:
        verify.activate_pencil_document = original_activate
    assert_equal(activation_calls, [("cursor", r"G:\SampleProject\Designs\pencil-nitro.pen")])


def test_activate_pencil_document_uses_current_editor_command():
    original_which = verify.shutil.which
    original_run = verify.subprocess.run
    commands = []

    class SuccessfulResult:
        returncode = 0
        stdout = b""
        stderr = b""

    try:
        verify.shutil.which = lambda name: r"C:\Tools\cursor.cmd" if name == "cursor" else None

        def record_run(command, **kwargs):
            commands.append((command, kwargs))
            return SuccessfulResult()

        verify.subprocess.run = record_run
        verify.activate_pencil_document("cursor", r"G:\Designs\pencil-demo.pen")
    finally:
        verify.shutil.which = original_which
        verify.subprocess.run = original_run

    command, kwargs = commands[0]
    assert_equal(command[-3:], [r"C:\Tools\cursor.cmd", "--reuse-window", r"G:\Designs\pencil-demo.pen"])
    assert_equal(kwargs["capture_output"], True)
    assert_equal(kwargs["check"], False)


def test_ensure_pencil_document_open_tolerates_activation_error_only_after_switch():
    original_activate = verify.activate_pencil_document
    client = FakeClient([
        "pencil-new.pen",
        r"/g:/SampleProject/Designs/pencil-nitro.pen",
    ])
    try:
        verify.activate_pencil_document = lambda _app, _path: (_ for _ in ()).throw(
            RuntimeError("editor command timed out")
        )
        verify.ensure_pencil_document_open(
            client,
            r"G:\SampleProject\Designs\pencil-nitro.pen",
            timeout_sec=0.1,
            poll_interval=0.0,
        )
    finally:
        verify.activate_pencil_document = original_activate


def test_ensure_pencil_document_open_force_reopen_when_already_active():
    original_activate = verify.activate_pencil_document
    client = FakeClient([
        r"G:\SampleProject\Designs\pencil-nitro.pen",
        r"G:\SampleProject\Designs\pencil-nitro.pen",
    ])
    activation_calls = []
    try:
        verify.activate_pencil_document = lambda app, path: activation_calls.append((app, path))
        verify.ensure_pencil_document_open(
            client,
            r"G:\SampleProject\Designs\pencil-nitro.pen",
            force_reopen=True,
            timeout_sec=0.1,
            poll_interval=0.0,
        )
    finally:
        verify.activate_pencil_document = original_activate
    assert_equal(activation_calls, [("cursor", r"G:\SampleProject\Designs\pencil-nitro.pen")])


def test_ensure_pencil_document_open_fails_closed_when_switch_never_happens():
    original_activate = verify.activate_pencil_document
    client = FakeClient(
        ["pencil-new.pen", "pencil-new.pen", "pencil-new.pen"],
        app_name="visual_studio_code",
    )
    try:
        verify.activate_pencil_document = lambda _app, _path: (_ for _ in ()).throw(
            RuntimeError("editor launch failed")
        )
        try:
            verify.ensure_pencil_document_open(
                client,
                r"G:\SampleProject\Designs\pencil-nitro.pen",
                timeout_sec=0.01,
                poll_interval=0.0,
            )
        except RuntimeError as exc:
            message = str(exc)
            assert_contains(message, "pencil-nitro.pen")
            assert_contains(message, "visual_studio_code")
            assert_contains(message, "editor launch failed")
            return
        raise AssertionError("expected ensure_pencil_document_open to fail when the document never switches")
    finally:
        verify.activate_pencil_document = original_activate


def test_prepare_pen_authority_reference_fails_closed_on_switch_failure():
    original_attempt = verify.run_pen_authority_reference_attempt

    def fail_attempt(*args, **kwargs):
        raise RuntimeError(
            "Pencil did not switch to document: G:\\SampleProject\\Designs\\pencil-nitro.pen "
            "(active=<none>, app=cursor, activation_error=simulated switch failure)"
        )

    try:
        verify.run_pen_authority_reference_attempt = fail_attempt
        authority = verify.prepare_pen_authority_reference(
            source_path=r"G:\SampleProject\Designs\pencil-nitro.pen",
            node_id="rgGjc",
            output_dir=Path("Saved/Design2UMG/Automation/_authority_selftest"),
            server_path="",
            app_name="",
            expected_design_size=None,
            pixel_tolerance=6,
        )
    finally:
        verify.run_pen_authority_reference_attempt = original_attempt

    assert_equal(authority["available"], False)
    assert_contains(authority["reason"], "pencil_document_switch_failed")
    assert_contains(authority["reason"], "simulated switch failure")


def test_export_pencil_reference_png_requests_scale_one():
    calls = []

    class RecordingClient:
        def has_tool(self, name):
            return name == "export_nodes"

        def call_tool(self, name, arguments, *, timeout_sec):
            calls.append((name, arguments, timeout_sec))
            return ""

    with tempfile.TemporaryDirectory() as temp_dir:
        result = verify.export_pencil_reference_png(
            RecordingClient(), "design.pen", "node-id", Path(temp_dir)
        )

    assert_equal(result, None)
    assert_equal(calls[0][0], "export_nodes")
    assert_equal(calls[0][1]["scale"], 1)


def test_prepare_pen_authority_reference_retries_with_fresh_attempt_after_export_timeout():
    original_attempt = verify.run_pen_authority_reference_attempt
    original_validate = verify.validate_reference_against_design
    original_read_size = verify.read_image_size
    attempts = []
    png_bytes = (
        b"\x89PNG\r\n\x1a\n"
        b"\x00\x00\x00\rIHDR"
        b"\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"
        b"\x00\x00\x00\rIDATx\x9cc`\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4"
        b"\x00\x00\x00\x00IEND\xaeB`\x82"
    )

    def flaky_attempt(source_path, node_id, server_path, app_name, export_dir, screenshot_path, **kwargs):
        attempts.append(
            {
                "source_path": source_path,
                "node_id": node_id,
                "app_name": app_name,
                "force_reopen": bool(kwargs.get("force_reopen")),
            }
        )
        if len(attempts) == 1:
            screenshot_path.parent.mkdir(parents=True, exist_ok=True)
            screenshot_path.write_bytes(png_bytes)
            return None, screenshot_path, "Pencil export_nodes did not produce any PNG: timed out"
        export_dir.mkdir(parents=True, exist_ok=True)
        png_path = export_dir / "node.png"
        png_path.write_bytes(png_bytes)
        return png_path, None, ""

    try:
        verify.run_pen_authority_reference_attempt = flaky_attempt
        verify.validate_reference_against_design = lambda _size, _expected: {
            "available": True,
            "valid": True,
            "reason": "",
            "expected_size": _expected,
            "reference_size": [1, 1],
        }
        verify.read_image_size = lambda _path: (1, 1)
        authority = verify.prepare_pen_authority_reference(
            source_path=r"G:\SampleProject\Designs\pencil-nitro.pen",
            node_id="rgGjc",
            output_dir=Path("Saved/Design2UMG/Automation/_authority_selftest_retry"),
            server_path="",
            app_name="antigravity",
            expected_design_size=None,
            pixel_tolerance=6,
        )
    finally:
        verify.run_pen_authority_reference_attempt = original_attempt
        verify.validate_reference_against_design = original_validate
        verify.read_image_size = original_read_size

    assert_equal(len(attempts), 2)
    assert_equal(attempts[0]["force_reopen"], False)
    assert_equal(attempts[1]["force_reopen"], True)
    assert_equal(authority["available"], True)
    assert_equal(authority["valid"], True)
    assert_equal(authority["verification_level"], "weak")
    assert_equal(authority["attempts"][0]["status"], "retry")
    assert_equal(authority["attempts"][1]["status"], "ok")


def test_prepare_pen_authority_reference_uses_screenshot_fallback_when_export_times_out():
    original_attempt = verify.run_pen_authority_reference_attempt
    original_validate = verify.validate_reference_against_design
    original_read_size = verify.read_image_size
    png_bytes = (
        b"\x89PNG\r\n\x1a\n"
        b"\x00\x00\x00\rIHDR"
        b"\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"
        b"\x00\x00\x00\rIDATx\x9cc`\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4"
        b"\x00\x00\x00\x00IEND\xaeB`\x82"
    )

    def screenshot_only_attempt(source_path, node_id, server_path, app_name, export_dir, screenshot_path, **kwargs):
        screenshot_path.parent.mkdir(parents=True, exist_ok=True)
        screenshot_path.write_bytes(png_bytes)
        return None, screenshot_path, "Pencil export_nodes did not produce any PNG: timed out"

    try:
        verify.run_pen_authority_reference_attempt = screenshot_only_attempt
        verify.validate_reference_against_design = lambda _size, _expected: {
            "available": True,
            "valid": True,
            "reason": "",
            "expected_size": _expected,
            "reference_size": [1, 1],
        }
        verify.read_image_size = lambda _path: (1, 1)
        authority = verify.prepare_pen_authority_reference(
            source_path=r"G:\SampleProject\Designs\pencil-nitro.pen",
            node_id="rgGjc",
            output_dir=Path("Saved/Design2UMG/Automation/_authority_selftest_witness"),
            server_path="",
            app_name="antigravity",
            expected_design_size=None,
            pixel_tolerance=6,
        )
    finally:
        verify.run_pen_authority_reference_attempt = original_attempt
        verify.validate_reference_against_design = original_validate
        verify.read_image_size = original_read_size

    assert_equal(authority["available"], True)
    assert_equal(authority["valid"], True)
    assert_equal(authority["canonical_source"], "pencil_get_screenshot")
    assert_equal(authority["verification_level"], "witness_only")
    assert_contains(authority["reason"], "pencil_export_nodes_unavailable")


def test_render_size_authority_revalidates_live_pen_evidence_after_ue_generation():
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        export_path = root / "export.png"
        witness_path = root / "witness.png"
        candidate_path = root / "candidate.png"
        verify.Image.new("RGBA", (200, 100), (20, 40, 60, 255)).save(export_path)
        verify.Image.new("RGBA", (100, 50), (20, 40, 60, 255)).save(witness_path)
        verify.Image.new("RGBA", (100, 50), (20, 40, 60, 255)).save(candidate_path)
        reference_context = {
            "reference_authority": {
                "available": True,
                "valid": False,
                "reason": "expected_design_size_unavailable",
                "canonical_source": "pencil_export_nodes",
                "canonical_path": str(export_path),
                "export_nodes_path": str(export_path),
                "get_screenshot_path": str(witness_path),
                "export_nodes_validation": None,
                "get_screenshot_validation": None,
                "witness_aspect_delta": None,
                "witness_content_validation": None,
                "verification_level": "none",
            }
        }

        result = verify.apply_render_size_authority(
            reference_context,
            export_path,
            {"design_time_width": 100, "design_time_height": 50},
            candidate_path,
            pixel_tolerance=6,
        )

        authority = reference_context["reference_authority"]
        assert_equal(result["valid"], True)
        assert_equal(authority["valid"], True)
        assert_equal(authority["reason"], "")
        assert_equal(authority["verification_level"], "strong")


def test_pen_authority_uses_stable_witness_metrics():
    original_compare = verify.compare_scaled_image_paths
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        export_path = root / "export.png"
        witness_path = root / "witness.png"
        verify.Image.new("RGBA", (200, 100), (20, 40, 60, 255)).save(export_path)
        verify.Image.new("RGBA", (100, 50), (20, 40, 60, 255)).save(witness_path)
        authority = {
            "export_nodes_path": str(export_path),
            "get_screenshot_path": str(witness_path),
        }

        try:
            verify.compare_scaled_image_paths = lambda *_args, **_kwargs: {
                "changed_ratio": 0.20,
                "mean_abs_error": 0.03,
                "stable_changed_ratio": 0.16,
                "stable_mean_abs_error": 0.01,
            }
            verify.finalize_pen_reference_authority(authority, (200, 100), 6)
            assert_equal(authority["valid"], True)
            assert_equal(authority["verification_level"], "strong")

            verify.compare_scaled_image_paths = lambda *_args, **_kwargs: {
                "changed_ratio": 0.10,
                "mean_abs_error": 0.01,
                "stable_changed_ratio": 0.20,
                "stable_mean_abs_error": 0.03,
            }
            verify.finalize_pen_reference_authority(authority, (200, 100), 6)
            assert_equal(authority["valid"], False)
            assert_equal(authority["reason"], "pencil_witness_content_mismatch")
        finally:
            verify.compare_scaled_image_paths = original_compare


def main():
    test_current_pencil_icon_contract()
    test_discover_mcp_server_searches_editor_extension_dirs()
    test_discover_mcp_server_prefers_latest_suffixed_version()
    test_synchronize_pencil_vscode_alias_uses_temporary_home()
    test_pencil_mcp_client_uses_stdio_transport()
    test_pencil_mcp_client_reports_process_startup_error()
    test_order_pencil_app_candidates()
    test_discover_pencil_app_candidates_prefers_document_host_then_foreground()
    test_discover_pencil_app_candidates_prefers_compatible_pen_window_without_document_hint()
    test_discover_pencil_app_candidates_keeps_clean_fallback_without_runtime_hits()
    test_ensure_pencil_document_open_uses_current_contract()
    test_activate_pencil_document_uses_current_editor_command()
    test_ensure_pencil_document_open_tolerates_activation_error_only_after_switch()
    test_ensure_pencil_document_open_force_reopen_when_already_active()
    test_ensure_pencil_document_open_fails_closed_when_switch_never_happens()
    test_prepare_pen_authority_reference_fails_closed_on_switch_failure()
    test_export_pencil_reference_png_requests_scale_one()
    test_prepare_pen_authority_reference_retries_with_fresh_attempt_after_export_timeout()
    test_prepare_pen_authority_reference_uses_screenshot_fallback_when_export_times_out()
    test_render_size_authority_revalidates_live_pen_evidence_after_ue_generation()
    test_pen_authority_uses_stable_witness_metrics()
    print("authority selftest passed")


if __name__ == "__main__":
    main()
