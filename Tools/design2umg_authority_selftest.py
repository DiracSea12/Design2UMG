import design2umg_auto_verify as verify
from pathlib import Path


class FakeClient:
    def __init__(self, states, open_exception=None, app_name="cursor"):
        self.states = list(states)
        self.open_exception = open_exception
        self.app_name = app_name
        self.open_calls = []
        self.state_index = 0

    def _current_state(self):
        if not self.states:
            return ""
        index = min(self.state_index, len(self.states) - 1)
        value = self.states[index]
        if self.state_index < len(self.states) - 1:
            self.state_index += 1
        return value

    def call_tool_content(self, name, arguments):
        if name == "open_document":
            self.open_calls.append(arguments)
            if self.open_exception is not None:
                raise self.open_exception
            return [{"type": "text", "text": "Document opened. Use get_editor_state to continue."}]
        if name == "get_editor_state":
            active = self._current_state()
            return [{"type": "text", "text": f"## Currently active editor\n- `{active}`\n"}]
        raise AssertionError(f"unexpected tool: {name}")


def assert_equal(left, right):
    assert left == right, f"expected {right!r}, got {left!r}"


def assert_contains(text, needle):
    assert needle in text, f"expected {needle!r} in {text!r}"


def test_order_pencil_app_candidates():
    ordered = verify.order_pencil_app_candidates(
        {"visual_studio_code", "cursor", "antigravity"},
        {"cursor"},
    )
    assert_equal(ordered[0], "cursor")
    assert_equal(set(ordered), {"visual_studio_code", "cursor", "antigravity"})


def test_discover_pencil_app_candidates_prefers_document_host_then_foreground():
    original_read = verify.read_pencil_registered_apps
    original_observations = verify.describe_pencil_app_runtime_observations
    try:
        verify.read_pencil_registered_apps = lambda: {"visual_studio_code", "cursor", "antigravity"}
        verify.describe_pencil_app_runtime_observations = lambda _path="": {
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
        ordered = verify.discover_pencil_app_candidates("", r"G:\UE5\FPSRoguelike\Designs\pencil-shadcn.pen")
    finally:
        verify.read_pencil_registered_apps = original_read
        verify.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered[0], "antigravity")
    assert_equal(ordered[1], "visual_studio_code")


def test_discover_pencil_app_candidates_prefers_compatible_pen_window_without_document_hint():
    original_read = verify.read_pencil_registered_apps
    original_observations = verify.describe_pencil_app_runtime_observations
    try:
        verify.read_pencil_registered_apps = lambda: {"visual_studio_code", "cursor", "antigravity"}
        verify.describe_pencil_app_runtime_observations = lambda _path="": {
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
        ordered = verify.discover_pencil_app_candidates("")
    finally:
        verify.read_pencil_registered_apps = original_read
        verify.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered[0], "antigravity")
    assert_equal(ordered[1], "visual_studio_code")


def test_discover_pencil_app_candidates_keeps_clean_fallback_without_runtime_hits():
    original_read = verify.read_pencil_registered_apps
    original_observations = verify.describe_pencil_app_runtime_observations
    try:
        verify.read_pencil_registered_apps = lambda: set()
        verify.describe_pencil_app_runtime_observations = lambda _path="": {}
        ordered = verify.discover_pencil_app_candidates("")
    finally:
        verify.read_pencil_registered_apps = original_read
        verify.describe_pencil_app_runtime_observations = original_observations

    assert_equal(ordered, ["visual_studio_code"])


def test_ensure_pencil_document_open_uses_current_contract():
    client = FakeClient([
        "pencil-new.pen",
        r"G:/UE5/FPSRoguelike/Designs/pencil-nitro.pen",
    ])
    verify.ensure_pencil_document_open(
        client,
        r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
        timeout_sec=0.1,
        poll_interval=0.0,
    )
    assert_equal(client.open_calls, [{"path": r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen"}])


def test_ensure_pencil_document_open_tolerates_open_error_only_after_switch():
    client = FakeClient(
        [
            "pencil-new.pen",
            r"/g:/UE5/FPSRoguelike/Designs/pencil-nitro.pen",
        ],
        open_exception=RuntimeError("Timeout waiting for document"),
    )
    verify.ensure_pencil_document_open(
        client,
        r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
        timeout_sec=0.1,
        poll_interval=0.0,
    )


def test_ensure_pencil_document_open_force_reopen_when_already_active():
    client = FakeClient([
        r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
        r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
    ])
    verify.ensure_pencil_document_open(
        client,
        r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
        force_reopen=True,
        timeout_sec=0.1,
        poll_interval=0.0,
    )
    assert_equal(client.open_calls, [{"path": r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen"}])


def test_ensure_pencil_document_open_fails_closed_when_switch_never_happens():
    client = FakeClient(
        ["pencil-new.pen", "pencil-new.pen", "pencil-new.pen"],
        open_exception=RuntimeError("transport timeout"),
        app_name="visual_studio_code",
    )
    try:
        verify.ensure_pencil_document_open(
            client,
            r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
            timeout_sec=0.01,
            poll_interval=0.0,
        )
    except RuntimeError as exc:
        message = str(exc)
        assert_contains(message, "pencil-nitro.pen")
        assert_contains(message, "visual_studio_code")
        assert_contains(message, "transport timeout")
        return
    raise AssertionError("expected ensure_pencil_document_open to fail when the document never switches")


def test_prepare_pen_authority_reference_fails_closed_on_switch_failure():
    original_attempt = verify.run_pen_authority_reference_attempt

    def fail_attempt(*args, **kwargs):
        raise RuntimeError(
            "Pencil did not switch to document: G:\\UE5\\FPSRoguelike\\Designs\\pencil-nitro.pen "
            "(active=<none>, app=cursor, open_document_error=simulated switch failure)"
        )

    try:
        verify.run_pen_authority_reference_attempt = fail_attempt
        authority = verify.prepare_pen_authority_reference(
            source_path=r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
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
            source_path=r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
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
            source_path=r"G:\UE5\FPSRoguelike\Designs\pencil-nitro.pen",
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


def main():
    test_order_pencil_app_candidates()
    test_discover_pencil_app_candidates_prefers_document_host_then_foreground()
    test_discover_pencil_app_candidates_prefers_compatible_pen_window_without_document_hint()
    test_discover_pencil_app_candidates_keeps_clean_fallback_without_runtime_hits()
    test_ensure_pencil_document_open_uses_current_contract()
    test_ensure_pencil_document_open_tolerates_open_error_only_after_switch()
    test_ensure_pencil_document_open_force_reopen_when_already_active()
    test_ensure_pencil_document_open_fails_closed_when_switch_never_happens()
    test_prepare_pen_authority_reference_fails_closed_on_switch_failure()
    test_prepare_pen_authority_reference_retries_with_fresh_attempt_after_export_timeout()
    test_prepare_pen_authority_reference_uses_screenshot_fallback_when_export_times_out()
    print("authority selftest passed")


if __name__ == "__main__":
    main()
