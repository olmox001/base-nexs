"""Real terminal wrapper -> raw HAL provider resolution, no second driver."""
from __future__ import annotations

import fcntl
import os
import select
import struct
import subprocess
import tempfile
import termios
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


class CanonicalTerminalAliasTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            prefix="canonical-terminal-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.script = Path(self.temp.name) / "script.nx"

    def run_script(self, code):
        self.script.write_text(code, encoding="utf-8")
        return subprocess.run(
            [str(CLI), str(self.script)], cwd=ROOT,
            capture_output=True, text=True, timeout=35)

    def test_all_raw_terminal_endpoints_are_registry_owned_builtins(self):
        names = ("_readkey", "_term_at", "_term_cls", "_term_flush",
                 "_term_cursor_move", "_term_cursor_show", "_term_size")
        result = self.run_script(
            "".join(f"out /sys/{name}\n" for name in names))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(7, sum(line.startswith("<builtin:")
                                for line in result.stdout.splitlines()))

    def test_terminal_shadow_buffer_and_raw_output_use_one_driver(self):
        result = self.run_script(
            'v = arr_create_anon()\n'
            'reg_set("/sys/0/0/vterm", v)\n'
            'term_at(1,1,"CANONICAL_SHADOWED_LINE")\n'
            'w = reg_get("/sys/0/0/vterm")\n'
            'out w[0]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CANONICAL_SHADOWED_LINE", result.stdout)

    def test_raw_provider_invalid_args_are_typed_errors(self):
        result = self.run_script(
            "out _term_cursor_move(1)\nout _term_cursor_show()\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("term_cursor_move: requires y, x", result.stdout)
        self.assertIn("term_cursor_show: requires bool", result.stdout)

    def test_terminal_clear_and_flush_wrappers_resolve_without_recursion(self):
        result = self.run_script(
            'term_cls()\nterm_flush()\nout "CANONICAL_TERMINAL_WRAPPER_OK"\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("CANONICAL_TERMINAL_WRAPPER_OK\n"))

    def test_runtime_terminal_dimensions_not_hardcoded_when_pty_available(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 41, 123, 0, 0))
        self.script.write_text("out term_size()\nout _term_size()\n",
                               encoding="utf-8")
        result = subprocess.run(
            [str(CLI), str(self.script)], cwd=ROOT,
            stdin=subprocess.DEVNULL, stdout=slave,
            stderr=subprocess.PIPE, timeout=35)
        self.assertEqual(result.returncode, 0,
                         result.stderr.decode(errors="replace"))
        # Keep the parent slave FD open on macOS: closing the last slave
        # descriptor can discard queued PTY output before the master reads.
        chunks = []
        while select.select([master], [], [], 0.25)[0]:
            chunk = os.read(master, 65536)
            if not chunk:
                break
            chunks.append(chunk)
        text = b"".join(chunks).decode(errors="replace")
        self.assertGreaterEqual(text.count("123x41"), 2, repr(text[-450:]))

    def test_no_duplicate_hal_driver_function_bodies(self):
        source = (ROOT / "sys/sysio.c").read_text()
        for name in ("bi_term_at", "bi_term_cls", "bi_term_flush",
                     "bi_term_cursor_move", "bi_term_cursor_show",
                     "bi_term_size", "bi_readkey"):
            self.assertEqual(1, source.count("static Value " + name + "("))
        # Public and raw terminal endpoints bind the exact same C callback.
        self.assertIn('fn_register_builtin_sig("_term_at", bi_term_at,', source)
        self.assertIn('fn_register_builtin_sig("term_at",   bi_term_at,', source)
        for script in ("nxlib/stdlib.nx",):
            code = (ROOT / script).read_text()
            self.assertIn("fn term_size() { ret _term_size() }", code)


if __name__ == "__main__":
    unittest.main()
