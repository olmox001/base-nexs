"""Real source/asset image staging, exact bytes, and fail-closed behavior."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGER = ROOT / "scripts/stage_nxlib.py"


class NxlibImageStageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            dir=ROOT / "build", prefix="nxlib-stage-")
        self.addCleanup(self.temp.cleanup)
        self.target = Path(self.temp.name) / "image"

    def invoke(self):
        return subprocess.run(
            [sys.executable, str(STAGER), "--root", str(self.target)],
            cwd=ROOT.parents[2], capture_output=True, text=True, timeout=15)

    def test_all_catalog_sources_and_original_binary_assets_stage(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NXLIB_IMAGE_STAGE_PASS", result.stdout)
        report = self.target / "lib/nxscript/catalog/staged_files.json"
        self.assertTrue(report.is_file())
        entries = json.loads(report.read_text(encoding="utf-8"))
        scripts = [
            line for line in (ROOT / "config/nx_sources.txt").read_text().splitlines()
            if line and not line.startswith("#")
        ]
        self.assertEqual(len(scripts), 33)
        self.assertEqual(len(entries), len(scripts) + 4 + 2)
        self.assertEqual(len(entries), len({row["installed"] for row in entries}))
        for row in entries:
            with self.subTest(source=row["logical"]):
                original = (ROOT / row["logical"]).read_bytes()
                installed = (self.target / row["installed"].lstrip("/")).read_bytes()
                self.assertEqual(original, installed)
                self.assertEqual(row["bytes"], len(original))
                self.assertEqual(row["sha256"], hashlib.sha256(installed).hexdigest())
        self.assertTrue((self.target / "lib/nxscript/config/nx_resources.txt").exists())
        self.assertTrue((self.target / "lib/nxscript/nxlib/services/fs/init.nx").exists())
        self.assertTrue((self.target / "home/nxscript/examples/minios/logos/logo1.nxtimg").exists())
        self.assertFalse((self.target / "lib/nxscript/services").exists())

    def test_second_stage_refuses_overwrite_without_touching_first(self):
        first = self.invoke()
        self.assertEqual(first.returncode, 0, first.stderr)
        original = (self.target / "lib/nxscript/nxlib/stdlib.nx").read_bytes()
        second = self.invoke()
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("Refuse to overwrite", second.stderr)
        self.assertEqual((self.target / "lib/nxscript/nxlib/stdlib.nx").read_bytes(),
                         original)

    def test_refuses_existing_nonempty_destination_and_parent(self):
        self.target.mkdir()
        sentinel = self.target / "do-not-touch.txt"
        sentinel.write_text("preserve")
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_text(), "preserve")
        forbidden = subprocess.run(
            [sys.executable, str(STAGER), "--root", str(ROOT)],
            capture_output=True, text=True, timeout=10)
        self.assertNotEqual(forbidden.returncode, 0)


if __name__ == "__main__":
    unittest.main()
