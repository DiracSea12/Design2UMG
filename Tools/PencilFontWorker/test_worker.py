"""Focused checks for the real packaged CLI; fixtures are read-only external fonts."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from fontTools.ttLib import TTFont


class FontWorkerTests(unittest.TestCase):
    def invoke(self, filename, weight, same_path=False):
        source = self.inputs / filename
        before = source.read_bytes()
        with tempfile.TemporaryDirectory(prefix="D2U字体-") as directory:
            root = Path(directory)
            output = source if same_path else root / "output.ttf"
            request = root / "request.json"
            response = root / "response.json"
            request.write_text(json.dumps({"input_path": str(source), "output_path": str(output), "weight": weight}), encoding="utf-8")
            result = subprocess.run([str(self.worker), "--request", str(request), "--response", str(response)],
                                    creationflags=subprocess.CREATE_NO_WINDOW, timeout=60,
                                    env={"SYSTEMROOT": os.environ["SYSTEMROOT"], "WINDIR": os.environ["SYSTEMROOT"],
                                         "PATH": str(Path(os.environ["SYSTEMROOT"]) / "System32"),
                                         "TEMP": str(root), "TMP": str(root)})
            self.assertEqual(before, source.read_bytes())
            reply = json.loads(response.read_text(encoding="utf-8"))
            if same_path:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(reply["success"])
                return
            self.assertEqual(result.returncode, 0, reply)
            self.assertTrue(reply["success"], reply)
            data = output.read_bytes()
            self.assertEqual(reply["source_sha256"], hashlib.sha256(before).hexdigest())
            self.assertEqual(reply["output_sha256"], hashlib.sha256(data).hexdigest())
            font = TTFont(output)
            self.assertNotIn("fvar", font)
            self.assertIsNone(font.flavor)
            self.assertEqual(set(font.getBestCmap()), set(TTFont(source).getBestCmap()))
            if filename == "static.ttf":
                self.assertEqual(data, before)
                self.assertEqual(reply["weight"], 400)
            if filename == "variable.ttf":
                self.assertEqual(reply["weight"], weight)
                self.assertEqual(reply["axes"], {"FILL": 0, "GRAD": 0, "opsz": 24, "wght": weight})
                self.assertEqual(font["name"].getDebugName(16) or font["name"].getDebugName(1), "Material Symbols Outlined")
                self.assertIn(f"wght={weight}", font["name"].getDebugName(17))

    def test_static_bytes(self):
        self.invoke("static.ttf", 200)

    def test_woff2_decode(self):
        self.invoke("icons.woff2", 200)

    def test_named_and_non_named_variable_weights(self):
        self.invoke("variable.ttf", 200)
        self.invoke("variable.ttf", 350)

    def test_inspect_is_read_only(self):
        for filename, variable in (("icons.woff2", False), ("variable.ttf", True)):
            source = self.inputs / filename
            before = source.read_bytes()
            with tempfile.TemporaryDirectory(prefix="D2U字体-") as directory:
                root = Path(directory)
                request, response = root / "request.json", root / "response.json"
                request.write_text(json.dumps({"action": "inspect", "input_path": str(source)}), encoding="utf-8")
                process = subprocess.run([str(self.worker), "--request", str(request), "--response", str(response)],
                                         creationflags=subprocess.CREATE_NO_WINDOW, timeout=60,
                                         env={"SYSTEMROOT": os.environ["SYSTEMROOT"], "PATH": str(Path(os.environ["SYSTEMROOT"]) / "System32"),
                                              "TEMP": str(root), "TMP": str(root)})
                reply = json.loads(response.read_text(encoding="utf-8"))
                self.assertEqual(process.returncode, 0, reply)
                self.assertTrue(reply["success"], reply)
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual(reply["source_sha256"], hashlib.sha256(before).hexdigest())
                self.assertEqual(reply["variable"], variable)
                font = TTFont(source)
                self.assertEqual(reply["family"], font["name"].getDebugName(16) or font["name"].getDebugName(1))
                self.assertEqual(reply["postscript"], font["name"].getDebugName(6))
                self.assertEqual(reply["weight"], font["OS/2"].usWeightClass)
                self.assertEqual(reply["italic"], bool(font["OS/2"].fsSelection & 1))
                self.assertEqual({p.name for p in root.iterdir()}, {"request.json", "response.json"})
                if variable:
                    self.assertEqual((reply["weight_min"], reply["weight_max"]), (100, 700))

    def test_source_cannot_be_output(self):
        self.invoke("static.ttf", 200, same_path=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--inputs", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    FontWorkerTests.worker = args.worker.resolve()
    FontWorkerTests.inputs = args.inputs.resolve()
    unittest.main(argv=[__file__, *remaining])
