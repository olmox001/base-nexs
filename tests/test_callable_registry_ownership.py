"""Real NEXS runtime tests: the registry is the sole callable name owner.

The existing RegKey is still a legacy namespace, not a capability-secure
NXOLMO StateStore; these tests only qualify the duplicate-table removal.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


class CallableRegistryOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(
            prefix="canonical-callable-", dir=ROOT / "build")
        self.addCleanup(self.folder.cleanup)
        self.source = Path(self.folder.name) / "scenario.nx"

    def execute(self, program, timeout=50):
        self.source.write_text(program, encoding="utf-8")
        return subprocess.run([str(CLI), str(self.source)], cwd=ROOT,
                              capture_output=True, text=True, timeout=timeout)

    def test_function_executes_only_from_registry(self):
        actual = self.execute("fn add(a b) { ret a+b }\nout add(11 31)\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertTrue(actual.stdout.endswith("42\n"))

    def test_renaming_replaces_function_body_at_canonical_path(self):
        actual = self.execute("fn f() { ret 1 }\nout f()\n"
                              "fn f() { ret 2 }\nout f()\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(["1", "2"], actual.stdout.splitlines()[-2:])

    def test_deleted_function_does_not_resurrect_from_old_table(self):
        actual = self.execute('fn f() { ret 73 }\nout f()\n'
                              'reg_del("/fn/f")\nout f()\n')
        self.assertEqual(actual.returncode, 1)
        self.assertTrue(actual.stdout.endswith("73\n"))
        self.assertIn("function not found", actual.stderr)

    def test_registry_scalar_shadows_function_definition(self):
        actual = self.execute("fn f() { ret 73 }\n"
                              'reg_set("/fn/f", 99)\nout f()\n')
        self.assertEqual(actual.returncode, 1)
        self.assertIn("function not found", actual.stderr)

    def test_builtin_can_be_rebound_in_authoritative_registry(self):
        actual = self.execute('out len("abc")\n'
                              'reg_set("/sys/len", 17)\nout len("abc")\n')
        self.assertEqual(actual.returncode, 1)
        self.assertTrue(actual.stdout.endswith("3\n"))
        self.assertIn("function not found", actual.stderr)

    def test_callable_value_is_aliased_without_copying_body(self):
        actual = self.execute('fn old() { ret 18 }\n'
                              'reg_set("/sys/alias_fn", reg_get("/fn/old"))\n'
                              'fn old() { ret 99 }\n'
                              'out alias_fn()\nout old()\n')
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(["18", "99"], actual.stdout.splitlines()[-2:])

    def test_function_in_registry_is_printed_as_a_callable(self):
        actual = self.execute("fn add(a b) { ret a+b }\nout /fn/add\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertTrue(actual.stdout.endswith("<fn add(a b)>\n"))

    def test_builtin_registry_value_prints_original_signature(self):
        actual = self.execute("out /sys/len\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertIn("<builtin:", actual.stdout.splitlines()[-1])

    def test_callable_removal_within_body_does_not_free_active_ast(self):
        actual = self.execute('fn f() { reg_del("/fn/f") ret 42 }\n'
                              "out f()\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertTrue(actual.stdout.endswith("42\n"))

    def test_callable_redefinition_within_body_retains_current_ast(self):
        actual = self.execute('fn f() { fn f() { ret 99 } ret 7 }\n'
                              'out f()\nout f()\n')
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(["7", "99"], actual.stdout.splitlines()[-2:])

    def test_variable_and_callable_namespaces_preserve_language_semantics(self):
        actual = self.execute("fn square(a) { ret a*a }\n"
                              "square=9\nout square\nout square(5)\n")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(["9", "25"], actual.stdout.splitlines()[-2:])

    def test_builtin_type_stays_callable_when_variable_type_exists(self):
        actual = self.execute('type = "shadow"\nout type\n'
                              'out type(17)\n')
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(["shadow", "int"],
                         actual.stdout.splitlines()[-2:])

    def test_function_registration_not_limited_by_legacy_max_fn_defs(self):
        script = "".join(
            f"fn declared_{i}() {{ ret {i} }}\n" for i in range(2100)
        ) + "out declared_0()\nout declared_2099()\n"
        actual = self.execute(script, timeout=90)
        self.assertEqual(actual.returncode, 0, actual.stderr[-900:])
        self.assertEqual(["0", "2099"], actual.stdout.splitlines()[-2:])

    def test_no_global_function_index_or_legacy_builtin_table(self):
        source = "".join((ROOT / p).read_text() for p in (
            "lang/fn_table.c", "lang/include/nexs_fn.h",
            "lang/eval.c", "runtime/runtime.c", "runtime/repl.c"))
        for forbidden in ("g_fn_table", "g_fn_count",
                          "val_fn_idx(", "fn_lookup_by_idx(",
                          "#define MAX_FN_DEFS "):
            self.assertNotIn(forbidden, source)
        self.assertIn("fn_lookup_in_scope(", source)
        self.assertIn("fn_value_hooks_init();", source)

    def test_local_callable_pointer_cannot_be_deserialized_from_ipc(self):
        ipc = (ROOT / "registry/reg_ipc.c").read_text()
        bridge = (ROOT / "hal/sel4/sel4_ipc_bridge.c").read_text()
        self.assertIn("function object requires capability transfer", ipc)
        self.assertIn("callable requires capability", bridge)
        self.assertNotIn("val_fn_idx(", ipc + bridge)


if __name__ == "__main__":
    unittest.main()
