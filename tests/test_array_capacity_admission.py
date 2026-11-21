"""End-to-end overflow, atomicity and bounds tests for Base-NEXS arrays."""
from __future__ import annotations
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "nxscript"


class ArrayAdmissionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            prefix="array-admission-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "input.nx"

    def program(self, source):
        self.source.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(BINARY), str(self.source)], cwd=ROOT,
            capture_output=True, text=True, timeout=20)

    def test_max_index_fails_without_signal(self):
        result = self.program("a[0]=5\na[9223372036854775807]=3\n")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("capacity admission failed", result.stderr)

    def test_invalid_new_array_is_not_published(self):
        result = self.program("newarray[9223372036854775807]=9\n")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("capacity admission failed", result.stderr)

    def test_out_of_bounds_insert_is_error_not_segfault(self):
        result = self.program(
            "a = arr_create_anon()\n"
            "out arr_ins(a, 2147483647, 42)\nout a\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual("[]", result.stdout.splitlines()[-1])
        self.assertIn("index out of range", result.stdout)

    def test_negative_insert_and_delete_do_not_modify_array(self):
        result = self.program(
            'a = arr_create_anon()\n'
            'arr_ins(a, 0, "safe")\n'
            'out arr_ins(a, -1, "bad")\n'
            'out arr_del(a, -1)\nout a[0]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("index out of range", result.stdout)
        self.assertIn("negative index", result.stdout)
        self.assertEqual("safe", result.stdout.splitlines()[-1])

    def test_insert_delete_reinsert_does_not_leave_alias_to_freed_string(self):
        result = self.program(
            'a = arr_create_anon()\n'
            'arr_ins(a, 0, "AAA")\n'
            'arr_ins(a, 1, "BBB")\n'
            'arr_ins(a, 2, "CCC")\n'
            'arr_del(a, 1)\n'
            'arr_ins(a, 1, "DDD")\n'
            'out a[0]\nout a[1]\nout a[2]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["AAA", "DDD", "CCC"],
                         result.stdout.splitlines()[-3:])

    def test_text_buffer_load_atomically_replaces_previous_lines(self):
        result = self.program(
            'a = tb_create()\nout tb_load(a, "old\\nentry")\n'
            'out tb_load(a, "new\\nreplaced")\n'
            'out a[0]\nout a[1]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["2", "2", "new", "replaced"],
                         result.stdout.splitlines()[-4:])

    def test_text_buffer_empty_input_remains_valid(self):
        result = self.program(
            'a = tb_create()\nout tb_load(a, "")\nout a\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["0", "[]"],
                         result.stdout.splitlines()[-2:])

    def test_split_and_lines_of_still_return_owned_arrays(self):
        result = self.program(
            'a = split("AA,BB,CC", ",")\nout a[0]\nout a[2]\n'
            'b = lines_of("first\\nsecond")\nout b[0]\nout b[1]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["AA", "CC", "first", "second"],
                         result.stdout.splitlines()[-4:])

    def test_normal_growth_and_delete_do_not_change_other_arrays(self):
        result = self.program(
            "a[0] = 5\nb[0] = 7\na[24] = 16\n"
            "out a[0]\nout a[24]\nout b[0]\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(["5", "16", "7"],
                         result.stdout.splitlines()[-3:])


if __name__ == "__main__":
    unittest.main()
