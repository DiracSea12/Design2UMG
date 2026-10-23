import io
import json
from contextlib import redirect_stderr
from pathlib import Path
from tempfile import TemporaryDirectory

from design2umg_verify_lib import json_io

from design2umg_verify_lib import diagnostics


def assert_equal(actual, expected) -> None:
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def test_error_mapping() -> None:
    parse_error = json.JSONDecodeError("bad json", "{", 1)
    mapped_parse_error = diagnostics.diagnose_json_load_error(Path("bad.json"), parse_error)
    assert_equal(mapped_parse_error.reason, "json_parse_failed")
    assert_equal(mapped_parse_error.path, Path("bad.json"))
    assert "bad json" in mapped_parse_error.detail

    mapped_read_error = diagnostics.diagnose_json_load_error(Path("missing.json"), RuntimeError("disk offline"))
    assert_equal(mapped_read_error.reason, "json_read_failed")
    assert_equal(mapped_read_error.path, Path("missing.json"))
    assert "disk offline" in mapped_read_error.detail


def test_emit_diagnostic_contract() -> None:
    stderr = io.StringIO()
    with redirect_stderr(stderr):
        diagnostics.emit_diagnostic("warning", "summary_unreadable", Path("summary.json"), "bad trailing comma")

    lines = stderr.getvalue().strip().splitlines()
    assert_equal(len(lines), 1)
    assert lines[0].startswith("[DIAGNOSTIC] "), lines[0]
    payload = json.loads(lines[0][len("[DIAGNOSTIC] "):])
    assert_equal(
        payload,
        {
            "detail": "bad trailing comma",
            "level": "warning",
            "path": "summary.json",
            "reason": "summary_unreadable",
        },
    )


def test_pencil_json_comments() -> None:
    plain = r'{"text":"https://host/a//b/*c*/\"quoted\\tail","children":[]}'
    commented = "// header\n" + plain.replace('"children"', '/* nodes */ "children"')
    with TemporaryDirectory() as directory:
        path = Path(directory) / "comments.pen"
        path.write_text(commented, encoding="utf-8")
        assert_equal(json_io.load_pencil_json(path), json.loads(plain))
        try:
            json_io.load_json(path)
        except diagnostics.EvidenceReadError:
            pass
        else:
            raise AssertionError("ordinary JSON must stay strict")
        path.write_text('{/* nodes */ "children":[],}', encoding="utf-8")
        try:
            json_io.load_pencil_json(path)
        except diagnostics.EvidenceReadError:
            pass
        else:
            raise AssertionError("Pencil trailing commas remain invalid")


def main() -> None:
    test_pencil_json_comments()
    test_error_mapping()
    test_emit_diagnostic_contract()
    print("diagnostics selftest passed")


if __name__ == "__main__":
    main()
