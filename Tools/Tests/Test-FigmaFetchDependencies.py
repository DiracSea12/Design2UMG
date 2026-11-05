import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("figma_fetch", TOOLS / "design2umg_figma_fetch.py")
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


def node(node_id, kind, children=None, **fields):
    return {"id": node_id, "type": kind, "children": children or [], **fields}


def interaction(target, navigation="CHANGE_TO", **trigger):
    return {"trigger": {"type": "ON_CLICK", **trigger}, "actions": [
        None, {"type": "NODE", "navigation": navigation, "destinationId": target}]}


def source():
    leaf = node("leaf", "COMPONENT", fills=[{"type": "IMAGE", "imageRef": "leaf-image"}])
    parent = node("parent", "COMPONENT", [node("private", "INSTANCE", componentId="leaf")],
                  interactions=[interaction("state")])
    state = node("state", "COMPONENT", interactions=[interaction("parent"), interaction("overlay", "OVERLAY")])
    selected = node("selected", "FRAME", [node("live", "INSTANCE", componentId="parent")],
                    interactions=[{"trigger": None, "actions": [
                        {"type": "NODE", "navigation": "CHANGE_TO", "destinationId": "absent"}]}])
    overlay = node("overlay", "FRAME", [node("external", "INSTANCE", componentId="leaf")],
                   interactions=[interaction("selected", "NAVIGATE", type="AFTER_TIMEOUT", timeout=0.9750000238418579)],
                   fillGeometry=[{"path": "M0 0 L8 8", "windingRule": "NONZERO"}],
                   strokes=[{"type": "IMAGE", "imageRef": "page-image"}])
    return {"document": node("document", "DOCUMENT", [selected, parent, state, leaf, overlay]),
            "version": "source-version", "components": {"parent": {"key": "parent-key", "componentSetId": "set"}},
            "componentSets": {"set": {"key": "set-key"}}}


class FigmaFetchDependenciesTests(unittest.TestCase):
    def test_reachable_closure_preserves_identity_source_fields_and_resources(self):
        data = source()
        before = copy.deepcopy(data)
        result = fetch.prepare_design(data, "selected")
        bundle = result["design2umgComponentDependencies"]
        self.assertEqual({entry["id"] for entry in bundle["definitions"]}, {"parent", "state", "leaf"})
        self.assertEqual(set(bundle["instances"]), {"live", "external"})
        self.assertEqual(bundle["instances"]["live"]["componentSetKey"], "set-key")
        self.assertEqual(bundle["instances"]["external"]["key"], "")
        self.assertNotIn("fileKey", result)
        self.assertEqual(result["version"], data["version"])
        self.assertEqual(result["componentSets"], data["componentSets"])
        self.assertEqual(result["document"]["interactions"], data["document"]["children"][0]["interactions"])
        self.assertEqual(result["design2umgPrototypePages"][0]["interactions"], data["document"]["children"][-1]["interactions"])
        self.assertEqual(result["design2umgPrototypePages"][0]["fillGeometry"], data["document"]["children"][-1]["fillGeometry"])
        refs = set()
        for entry in bundle["definitions"]:
            fetch.collect_image_refs(entry["document"], refs)
        for page in result["design2umgPrototypePages"]:
            fetch.collect_image_refs(page, refs)
        self.assertEqual(refs, {"leaf-image", "page-image"})
        self.assertEqual(data, before)

    def test_selected_component_runtime_instances_remain_distinct_from_definition_copies(self):
        result = fetch.prepare_design(source(), "parent", "requested-key")
        self.assertEqual(result["fileKey"], "requested-key")
        self.assertEqual(set(result["design2umgComponentDependencies"]["instances"]), {"private", "external", "live"})

    def test_missing_remote_wrong_target_and_structural_cycle_fail(self):
        data = source()
        data["components"]["parent"]["remote"] = True
        with self.assertRaisesRegex(ValueError, "missing or inaccessible"):
            fetch.prepare_design(data, "selected")
        data = source()
        data["document"]["children"][2]["interactions"] = [interaction("absent")]
        with self.assertRaisesRegex(ValueError, "absent"):
            fetch.prepare_design(data, "selected")
        data["document"]["children"][2]["interactions"] = [interaction("leaf", "OVERLAY")]
        with self.assertRaisesRegex(ValueError, "not a FRAME"):
            fetch.prepare_design(data, "selected")
        data = source()
        data["document"]["children"][3]["children"].append(node("recursive", "INSTANCE", componentId="leaf"))
        with self.assertRaisesRegex(ValueError, "structure cycle"):
            fetch.prepare_design(data, "selected")
        data = source()
        data["fileKey"] = "different-key"
        with self.assertRaisesRegex(ValueError, "fileKey"):
            fetch.prepare_design(data, "selected", "requested-key")
        data = source()
        del data["document"]["children"][1]["interactions"][0]["actions"][1]["navigation"]
        with self.assertRaisesRegex(ValueError, "missing required navigation"):
            fetch.prepare_design(data, "selected")
        data["document"]["children"][1]["interactions"][0]["actions"][1]["navigation"] = ""
        with self.assertRaisesRegex(ValueError, "missing required navigation"):
            fetch.prepare_design(data, "selected")

    def test_conditionals_are_dependencies_but_empty_actions_are_not(self):
        data = source()
        data["document"]["children"][0]["interactions"] = [{"trigger": {"type": "ON_CLICK"}, "actions": [
            None, {"type": "CONDITIONAL", "conditionalBlocks": [{"actions": interaction("state")["actions"]}]}]}]
        result = fetch.prepare_design(data, "selected")
        self.assertEqual(len(result["design2umgComponentDependencies"]["definitions"]), 3)
        data["document"]["children"][0]["interactions"] = [interaction("unresolved-target", "UNKNOWN_NAVIGATION")]
        result = fetch.prepare_design(data, "selected")
        self.assertEqual(result["document"]["interactions"], data["document"]["children"][0]["interactions"])

    def test_build_requests_full_geometry_source_and_uses_existing_download_flow(self):
        data = {"document": node("selected", "FRAME"), "version": "source-version"}
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(fetch, "get_token", return_value="synthetic-token"), \
                 patch.object(fetch, "http_json", side_effect=[data, {"images": {}}]) as request, \
                 patch.object(fetch, "http_download") as download, \
                 patch.object(sys, "argv", ["fetch", "build", "requested-key", "selected", directory]):
                fetch.main()
            self.assertEqual(request.call_args_list[0].args[0], f"{fetch.API}/files/requested-key?geometry=paths")
            self.assertEqual(request.call_count, 2)
            download.assert_not_called()
            result = json.loads((Path(directory) / "design.json").read_text(encoding="utf-8"))
            self.assertEqual(result["document"]["id"], "selected")
            self.assertEqual(result["fileKey"], "requested-key")


if __name__ == "__main__":
    unittest.main()
