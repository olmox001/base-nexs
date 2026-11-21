"""Single text-buffer implementation: no service-vs-builtin fork.

Real program execution tests preserve all original textbuf.nx operations
and assert both valid behavior and fault/negative contracts.
"""
from __future__ import annotations
import subprocess
import re
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BIN=ROOT/"nxscript"


class CanonicalTextBufferTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="canonical-textbuf-",
                                              dir=ROOT/"build")
        self.addCleanup(self.temp.cleanup)
        self.script=Path(self.temp.name)/"test.nx"

    def execute(self, code: str):
        self.script.write_text(code, encoding="utf-8")
        return subprocess.run([str(BIN),str(self.script)],cwd=ROOT,
                              capture_output=True,text=True,timeout=35)

    def test_module_load_preserves_single_builtin_functions(self):
        r=self.execute('exec("nxlib/textbuf.nx")\n'
                       'out /sys/tb_load\nout /sys/tb_split_line\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertTrue(all("<builtin:" in x for x in
                            r.stdout.splitlines()[-2:]))

    def test_create_full_text_buffer(self):
        r=self.execute('a=tb_create()\nout len(a)\nout a[0]\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["1",""],r.stdout.splitlines()[-2:])

    def test_get_line_valid_and_invalid(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"hello")\n'
                       'out tb_get_line(a,0)\nout tb_get_line(a,999)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["hello",""],r.stdout.splitlines()[-2:])

    def test_get_line_converts_nontext_with_existing_value_semantics(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,27)\n'
                       'out tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertTrue(r.stdout.endswith("27\n"))

    def test_set_line_updates_existing_and_preserves_other(self):
        r=self.execute('a=tb_create()\nout tb_load(a,"left\\nright")\n'
                       'tb_set_line(a,1,"updated")\n'
                       'out tb_get_line(a,0)\nout tb_get_line(a,1)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["2","left","updated"],r.stdout.splitlines()[-3:])

    def test_set_line_negative_is_typed_error_not_memory_corruption(self):
        r=self.execute('a=tb_create()\nout tb_set_line(a,-1,"bad")\n'
                       'out tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("negative line index",r.stdout)
        self.assertEqual("",r.stdout.splitlines()[-1])

    def test_split_line_middle_preserves_both_substrings(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"abcd")\n'
                       'tb_split_line(a,0,2)\nout len(a)\n'
                       'out tb_get_line(a,0)\nout tb_get_line(a,1)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["2","ab","cd"],r.stdout.splitlines()[-3:])

    def test_split_at_start_and_end(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"abcdef")\n'
                       'tb_split_line(a,0,0)\n'
                       'out tb_get_line(a,0)\nout tb_get_line(a,1)\n'
                       'tb_split_line(a,1,999)\n'
                       'out tb_get_line(a,1)\nout tb_get_line(a,2)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["","abcdef","abcdef",""],
                         r.stdout.splitlines()[-4:])

    def test_split_invalid_line_does_not_modify_existing_content(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"safe")\n'
                       'out tb_split_line(a,99,0)\nout a\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("index out of range",r.stdout)
        self.assertEqual("[safe]",r.stdout.splitlines()[-1])

    def test_merge_previous_and_current_and_returns_cursor(self):
        r=self.execute('a=tb_create()\nout tb_load(a,"first\\nsecond")\n'
                       'out tb_merge_line(a,1)\nout len(a)\nout tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["2","5","1","firstsecond"],
                         r.stdout.splitlines()[-4:])

    def test_merge_at_first_line_is_noop(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"safe")\n'
                       'out tb_merge_line(a,0)\nout tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["0","safe"],r.stdout.splitlines()[-2:])

    def test_merge_invalid_line_does_not_delete_existing(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"safe")\n'
                       'out tb_merge_line(a,8)\nout tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("index out of range",r.stdout)
        self.assertEqual("safe",r.stdout.splitlines()[-1])

    def test_split_and_merge_preserve_long_text_exactly(self):
        text=("abC1239z_"*55)
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"'+text+'")\n'
                       'tb_split_line(a,0,200)\n'
                       'out tb_merge_line(a,1)\nout tb_get_line(a,0)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["200",text],r.stdout.splitlines()[-2:])

    def test_to_str_after_split_merge_set(self):
        r=self.execute('a=tb_create()\ntb_set_line(a,0,"alpha")\n'
                       'tb_split_line(a,0,2)\ntb_set_line(a,1,"BCD")\n'
                       'out tb_to_str(a)\n')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(["al","BCD"],r.stdout.splitlines()[-2:])

    def test_editor_uses_no_duplicate_script_implementation(self):
        module=(ROOT/"nxlib/textbuf.nx").read_text()
        self.assertIn('library "textbuf"',module)
        self.assertNotIn("\nfn tb_",module)
        native=(ROOT/"extensions/textbuf/textbuf.c").read_text()
        legacy=(ROOT/"lang/builtins.c").read_text()
        self.assertNotIn("static Value builtin_tb_", legacy)
        for symbol in ("tb_create","tb_get_line","tb_set_line",
                       "tb_insert_char","tb_delete_char","tb_split_line",
                       "tb_merge_line","tb_load","tb_to_str"):
            self.assertRegex(native, r'register_builtin_sig\s*\(\s*"' +
                             re.escape(symbol) + r'"')


if __name__=="__main__":
    unittest.main()
