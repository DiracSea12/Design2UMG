"""Checks the extracted modules without importing the verification entry point."""

import copy
import json
import sys

from design2umg_verify_lib import pencil_mcp, structure_comparison


def test_modules_do_not_import_the_orchestrator():
    assert "design2umg_auto_verify" not in sys.modules
    assert "PIL" not in sys.modules


def test_structure_comparison_is_pure_and_skips_carriers():
    source = {"structure_nodes": [
        {"id": "root", "name": "Root", "kind": "container", "path": "0"},
        {"id": "a", "name": "Label", "kind": "text", "path": "0/0"},
        {"id": "b", "name": "Label", "kind": "text", "path": "0/1"},
    ]}
    asset = {"structure_nodes": [
        {"id": "root", "name": "Root", "kind": "container", "path": "0"},
        {"name": "Carrier", "kind": "container", "path": "0/0"},
        {"id": "a", "name": "Label", "kind": "text", "path": "0/0/0"},
        {"id": "b", "name": "Label_1", "kind": "text", "path": "0/0/1"},
    ]}
    originals = copy.deepcopy((source, asset))
    result = structure_comparison.compute_tree_correspondence(source, asset)
    assert result["source_to_asset_indices"] == [[0, 0], [1, 2], [2, 3]]
    assert result["identity_ratio"] == 1.0
    assert result["named_parent_path_ratio"] == 1.0
    assert result["sibling_order_ratio"] == 1.0
    assert (source, asset) == originals
    assert not structure_comparison.compute_tree_correspondence(None, None)["available"]


def test_transport_dispatch_and_failure_without_starting_a_process():
    client = pencil_mcp.PencilMcpClient("unused", "test-app")
    sent = []
    client._send = sent.append
    client._stdout_messages.put(json.dumps({"method": "notification"}))
    client._stdout_messages.put(json.dumps({"id": 1, "result": {"value": 42}}))
    assert client._request("test", {}) == {"value": 42}
    assert sent[0]["id"] == 1
    client._stdout_messages.put(None)
    try:
        client._request("test", {})
    except RuntimeError as error:
        assert "closed stdout" in str(error)
    else:
        raise AssertionError("EOF must not spin waiting for a response")
    try:
        client._request("test", {}, timeout_sec=0)
    except RuntimeError as error:
        assert "Timed out" in str(error)
    else:
        raise AssertionError("A request deadline must be enforced")


def main():
    test_modules_do_not_import_the_orchestrator()
    test_structure_comparison_is_pure_and_skips_carriers()
    test_transport_dispatch_and_failure_without_starting_a_process()
    print("module boundaries selftest passed")


if __name__ == "__main__":
    main()
