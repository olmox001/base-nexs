"""Real interpreter tests for canonical RegKey path integrity.

Path admission and subtree reclamation are preparatory invariants for
the NXOLMO StateStore transition. They are NOT capability tests.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


class RegistryPathRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(
            dir=ROOT / "build", prefix="registry-integrity-")
        self.addCleanup(self.folder.cleanup)
        self.script = Path(self.folder.name) / "program.nx"

    def run_program(self, source):
        self.script.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(CLI), str(self.script)], cwd=ROOT, capture_output=True,
            text=True, timeout=35)

    def test_root_variable_path_is_canonical(self):
        result = self.run_program(
            'top=73\nout top\nout reg_get("/top")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["73", "73"], result.stdout.splitlines()[-2:])

    def test_nested_registry_path_exact_lookup(self):
        result = self.run_program(
            'reg_set("/tree/child/leaf", 93)\n'
            'out reg_get("/tree/child/leaf")\n'
            'out type(reg_get("/tree/child/other"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["93", "err"], result.stdout.splitlines()[-2:])

    def test_recursive_delete_removes_descendants(self):
        result = self.run_program(
            'reg_set("/tree/child/a", 4)\n'
            'reg_set("/tree/child/b", 5)\n'
            'reg_del("/tree")\n'
            'out type(reg_get("/tree/child/a"))\n'
            'out type(reg_get("/tree/child/b"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["err", "err"], result.stdout.splitlines()[-2:])

    def test_long_multi_segment_path_not_truncated(self):
        path = "/" + "/".join(["segment" + "x" * 40] * 9) + "/end"
        self.assertLess(len(path), 512)
        result = self.run_program(
            f'reg_set("{path}", 841)\n'
            f'out reg_get("{path}")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("841\n"))

    def test_overlong_path_does_not_alias_truncated_prefix(self):
        prefix = "/" + "/".join(["component" + "y" * 38] * 10)
        bad = prefix + "/not_admitted_" + "x" * 40
        self.assertLess(len(prefix), 512)
        self.assertGreaterEqual(len(bad), 512)
        result = self.run_program(
            f'reg_set("{bad}", 17)\n'
            f'out type(reg_get("{bad}"))\n'
            f'out type(reg_get("{prefix}"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["err", "err"], result.stdout.splitlines()[-2:])

    def test_dot_segments_rejected_from_single_registry_namespace(self):
        result = self.run_program(
            'reg_set("/same/path/../alias", 77)\n'
            'reg_set("/same/./alias", 78)\n'
            'out type(reg_get("/same/path/../alias"))\n'
            'out type(reg_get("/same/./alias"))\n'
            'out type(reg_get("/same"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["err", "err", "err"],
                         result.stdout.splitlines()[-3:])

    def test_ls_after_cd_uses_exact_same_namespace_node(self):
        result = self.run_program(
            'reg_set("/same/child", "one")\n'
            'cd same\n'
            'ls\n'
            'out type(reg_get("/same/child"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[REGISTRY] /same", result.stdout)
        self.assertNotIn("Path '/same/' not found", result.stdout)
        self.assertEqual("str", result.stdout.splitlines()[-1])

    def test_overlong_component_never_creates_truncated_alias(self):
        component = "oversized_" + "z" * 90
        prefix = "/comp/" + component[:63]
        result = self.run_program(
            f'reg_set("/comp/{component}", 93)\n'
            f'out type(reg_get("/comp/{component}"))\n'
            f'out type(reg_get("{prefix}"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["err", "err"], result.stdout.splitlines()[-2:])

    def test_noncanonical_empty_segment_rejected_without_side_effect(self):
        result = self.run_program(
            'reg_set("/canonical//bad", 7)\n'
            'out type(reg_get("/canonical//bad"))\n'
            'out type(reg_get("/canonical/bad"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["err", "err"], result.stdout.splitlines()[-2:])

    def test_repeated_function_scopes_remain_usable(self):
        result = self.run_program(
            'fn local_value(a) { local = a ret local }\n'
            'i=0\nloop { if i>=90 {break} out local_value(i) i=i+1 }\n'
            'out i\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("90\n"))
        self.assertFalse("FATAL" in result.stdout + result.stderr)

    def test_callable_namespace_stays_authoritative_after_path_changes(self):
        result = self.run_program(
            'fn sum(a b) { ret a+b }\n'
            'reg_set("/nested/alias", reg_get("/fn/sum"))\n'
            'fn sum(a b) { ret a*b }\n'
            'out sum(6 7)\n'
            'reg_del("/nested")\n'
            'out sum(6 7)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["42", "42"], result.stdout.splitlines()[-2:])


if __name__ == "__main__":
    unittest.main()
