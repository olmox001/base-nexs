#!/usr/bin/env python3
"""Real runtime registry object-view vs explicit payload read.

The registry is the entire NEXS runtime. This checks the existing read and
listing APIs on the same actual StateStore objects, never a mocked FS/view.
"""
from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


def run_nexs(program: str):
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
        source = Path(folder) / "probe.nx"
        source.write_text(program, encoding="utf-8")
        return subprocess.run(
            [str(CLI), str(source)], cwd=ROOT,
            capture_output=True, text=True, timeout=20,
            check=False,
        )


class RegistryResourceViewTests(unittest.TestCase):
    def test_ls_and_recursive_print_expose_identity_not_content(self):
        result = run_nexs("""
reg_set("/resources/document", "HIDDEN_FILE_CONTENT_PROOF")
reg_set("/resources/scalar", 998733)
reg_set("/resources/subdir/file", "HIDDEN_SECOND_CONTENT_PROOF")
ls /resources
print_reg("/resources")
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[REGISTRY] /resources", result.stdout)
        self.assertIn("ref=", result.stdout)
        self.assertIn("gen=", result.stdout)
        self.assertIn("rev=", result.stdout)
        self.assertIn("children=", result.stdout)
        self.assertIn("document", result.stdout)
        self.assertIn("scalar", result.stdout)
        self.assertIn("subdir", result.stdout)
        self.assertIn("file", result.stdout)
        self.assertNotIn("HIDDEN_FILE_CONTENT_PROOF", result.stdout)
        self.assertNotIn("HIDDEN_SECOND_CONTENT_PROOF", result.stdout)
        self.assertNotIn("998733", result.stdout)

    def test_explicit_get_still_reads_exact_same_object(self):
        result = run_nexs("""
reg_set("/resources/content", "EXPLICIT_READ_CONTENT")
ls /resources
out reg_get("/resources/content")
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        prefix, separator, explicit = result.stdout.partition(
            "EXPLICIT_READ_CONTENT")
        self.assertTrue(separator, result.stdout)
        self.assertIn("content", prefix)
        self.assertIn("ref=", prefix)
        self.assertNotIn("EXPLICIT_READ_CONTENT", prefix)
        self.assertEqual("", explicit.strip())

    def test_large_listing_is_sorted_without_mutating_object_order(self):
        names = [f"k_{i:03d}" for i in range(127, -1, -1)]
        script = "\n".join(
            f'reg_set("/sort/{name}", "{name}_PRIVATE_PAYLOAD")'
            for name in names)
        result = run_nexs(script + '\nls /sort\nprint_reg("/sort")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        section = result.stdout.split("[REGISTRY] /sort", 1)[1]
        listed = re.findall(r"(?m)^(k_\d{3})\s+str\s+ref=", section)
        self.assertEqual(listed, sorted(names))
        self.assertIn("children=128", result.stdout)
        self.assertNotIn("_PRIVATE_PAYLOAD", result.stdout)

    def test_live_revision_changes_only_for_mutation(self):
        result = run_nexs("""
reg_set("/revisions/file", "a")
ls /revisions
reg_set("/revisions/file", "b")
ls /revisions
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        refs = re.findall(
            r"file\s+str\s+ref=(\d+:\d+) gen=(\d+) rev=(\d+)",
            result.stdout)
        self.assertEqual(2, len(refs), result.stdout)
        self.assertEqual(refs[0][0:2], refs[1][0:2])
        self.assertEqual(int(refs[0][2]) + 1, int(refs[1][2]))
        self.assertNotIn(" = a", result.stdout)
        self.assertNotIn(" = b", result.stdout)

    def test_registry_array_and_callable_objects_are_still_visible(self):
        result = run_nexs("""
fn private_probe(v) { ret v }
ls /fn
ls /sys
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("private_probe", result.stdout)
        self.assertIn("fn", result.stdout)
        self.assertIn("ref=", result.stdout)
        self.assertNotIn("val=", result.stdout)

    def test_multilevel_object_listing_is_iterative(self):
        result = run_nexs("""
reg_set("/depth/a/b/c/d/e/f/g/value", "NOT_AN_IMPLICIT_PAYLOAD")
print_reg("/depth")
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("value", result.stdout)
        self.assertIn("children=", result.stdout)
        self.assertNotIn("NOT_AN_IMPLICIT_PAYLOAD", result.stdout)


if __name__ == "__main__":
    unittest.main()
