"""Actual NXOLMO-backed registry pointer traversal and failure gates."""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


def evaluate(program: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
        source = Path(folder) / "pointers.nx"
        source.write_text(program, encoding="utf-8")
        return subprocess.run([str(CLI), str(source)], cwd=ROOT,
                              capture_output=True, text=True, timeout=15)


class RegistryPointerChainTests(unittest.TestCase):
    def test_more_than_legacy_64_hops_preserves_exact_value(self):
        steps = [f'reg_set("/chain/{i}", mkptr("/chain/{i + 1}"))'
                 for i in range(128)]
        steps += ['reg_set("/chain/128", "TARGET_128_EXACT")',
                  'out eval("deref /chain/0")',
                  'out reg_get("/chain/128")']
        result = evaluate("\n".join(steps) + "\n")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["TARGET_128_EXACT", "TARGET_128_EXACT"],
                         result.stdout.splitlines()[-2:])

    def test_self_and_multi_object_cycle_fail_without_registry_mutation(self):
        result = evaluate("""
reg_set("/cycle/a", mkptr("/cycle/b"))
reg_set("/cycle/b", mkptr("/cycle/c"))
reg_set("/cycle/c", mkptr("/cycle/a"))
reg_set("/cycle/self", mkptr("/cycle/self"))
out type(eval("deref /cycle/a"))
out type(eval("deref /cycle/self"))
out type(reg_get("/cycle/c"))
""")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["err", "err", "ptr"],
                         result.stdout.splitlines()[-3:])

    def test_pointer_alias_syntax_rejected_before_existing_value_changes(self):
        result = evaluate("""
reg_set("/admission/target", "UNCHANGED_321")
reg_set("/admission/link", mkptr("/admission/target"))
out type(reg_set("/admission/link", mkptr("/admission/../escape")))
out eval("deref /admission/link")
out type(reg_get("/admission/escape"))
""")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["err", "UNCHANGED_321", "err"],
                         result.stdout.splitlines()[-3:])

    def test_unlink_failure_propagates_typed_error_without_removing_root(self):
        result = evaluate("""
out type(reg_del("/"))
out type(reg_del("/nonexistent"))
out type(reg_get("/sys"))
""")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["err", "err", "nil"],
                         result.stdout.splitlines()[-3:])

    def test_missing_pointer_target_is_typed_error_not_alias(self):
        result = evaluate("""
reg_set("/pointer/missing", mkptr("/pointer/nonexistent"))
reg_set("/pointer/existing", 271)
out type(eval("deref /pointer/missing"))
out eval("deref /pointer/existing")
out type(reg_get("/pointer/nonexistent"))
""")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["err", "271", "err"],
                         result.stdout.splitlines()[-3:])


if __name__ == "__main__":
    unittest.main()
