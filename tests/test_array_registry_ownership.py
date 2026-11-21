"""Named arrays are registry-owned; pipes are independent, never global.

These integration tests execute NEXS programs, including negative contracts.
They do not claim that the legacy registry already has full NXREG authority.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
CLI = BASE / "nxscript"


class ArrayRegistryOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            prefix="array-registry-", dir=BASE / "build")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.program = self.directory / "program.nx"

    def execute(self, source: str):
        self.program.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(CLI), str(self.program)], cwd=BASE, text=True,
            capture_output=True, timeout=50)

    def test_replaced_array_cannot_reappear_from_hidden_name_table(self):
        result = self.execute("alpha[0] = 123\nalpha = 6\nout alpha[0]\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("array not found", result.stderr)
        self.assertNotIn("\n123\n", result.stdout)

    def test_scalar_cannot_be_implicitly_converted_to_array(self):
        result = self.execute("alpha = 6\nalpha[0] = 123\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not an array", result.stderr)

    def test_each_pipe_owns_unique_array_and_fds(self):
        result = self.execute(
            "first = pipe()\nsecond = pipe()\n"
            "out first[0]\nout second[0]\nout first[1]\nout second[1]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        fds = [int(line) for line in result.stdout.splitlines()
               if line.isdigit()]
        self.assertEqual(4, len(fds))
        self.assertEqual(4, len(set(fds)))

    def test_pipe_arrays_do_not_alias_on_mutation(self):
        result = self.execute(
            "first = pipe()\nsecond = pipe()\nfirst[0] = 123456\n"
            "out first[0]\nout second[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual("123456", lines[-2])
        self.assertNotEqual("123456", lines[-1])

    def test_registry_owns_many_arrays_beyond_legacy_global_limit(self):
        script = "".join(
            f"array_{i}[0] = {i}\n" for i in range(2200)
        ) + "out array_0[0]\nout array_2199[0]\n"
        result = self.execute(script)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["0", "2199"], result.stdout.splitlines()[-2:])

    def test_ordinary_arrays_and_deletion_preserved(self):
        result = self.execute(
            "first[0] = 11\nsecond[0] = 22\n"
            "out first[0]\nout second[0]\ndel first[0]\nout second[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["11", "22", "22"],
                         result.stdout.splitlines()[-3:])

    def test_array_value_aliases_share_same_owned_instance(self):
        result = self.execute(
            "first[0] = 11\nsecond = first\nsecond[0] = 22\n"
            "out first[0]\nout second[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["22", "22"], result.stdout.splitlines()[-2:])

    def test_registry_outlives_temporary_value(self):
        result = self.execute(
            "test[0] = 42\nout test[0]\nout test[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["42", "42"], result.stdout.splitlines()[-2:])

    def test_no_global_array_table_or_resurrection_path(self):
        header = (BASE / "core/include/nexs_value.h").read_text()
        implementation = (BASE / "core/dynarray.c").read_text()
        evaluator = (BASE / "lang/eval.c").read_text()
        for source in (header, implementation, evaluator):
            self.assertNotIn("g_arrays", source)
            self.assertNotIn("g_array_count", source)
            self.assertNotIn("arr_get_or_create", source)
        self.assertIn("arr_create_anon()", evaluator)

    def test_shared_array_is_explicitly_owned_by_root_registry(self):
        result = self.execute(
            "ledger = arr_create_anon()\n"
            "fn worker_write(value) { ledger[0] = value }\n"
            "worker_write(91)\nout ledger[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("91\n"),
                        result.stdout[-240:])

    def test_undeclared_worker_array_does_not_leak_into_root(self):
        result = self.execute(
            "fn worker_create() { localarr[0] = 91 }\n"
            "worker_create()\nout localarr[0]\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("array not found", result.stderr)

    def test_array_reference_ownership_survives_function_return(self):
        result = self.execute(
            "fn return_local_array() { temp[0] = 91 ret temp }\n"
            "result = return_local_array()\nout result[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("91\n"),
                        result.stdout[-240:])


if __name__ == "__main__":
    unittest.main()
