"""Real hosted terminal interaction for the two NEXS OS-style entrypoints.

These are not mock responses or simulated UI; the executable owns a PTY
and must consume actual input, output functional markers, and exit cleanly.
"""
from __future__ import annotations

import os
import pty
import select
import signal
import subprocess
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"
OUTSIDE = ROOT.parents[2]


def run_pty_script(relative: str, prompt: bytes, commands: bytes):
    master, slave = pty.openpty()
    process = None
    output = bytearray()
    wrote = False
    deadline = time.monotonic() + 15.0
    try:
        process = subprocess.Popen(
            [str(CLI), str(ROOT / relative)],
            cwd=OUTSIDE, stdin=slave, stdout=slave, stderr=slave,
            start_new_session=True)
        os.close(slave)
        slave = -1
        while time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.1)
            if readable:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    data = b""
                if not data:
                    if process.poll() is not None:
                        break
                elif len(output) < 750000:
                    output.extend(data)
            if not wrote and prompt in output:
                os.write(master, commands)
                wrote = True
            if process.poll() is not None and not readable:
                break
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=2)
            raise AssertionError("Interactive guest did not terminate within 15s")
        return process.returncode, wrote, bytes(output)
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=2)
        if slave >= 0:
            os.close(slave)
        os.close(master)


class InteractiveOSExamples(unittest.TestCase):
    def test_boot_shell_help_pwd_exit_in_real_pty(self):
        code, wrote, data = run_pty_script(
            "example/minios/boot.nx",
            b"nexs-os:/> ",
            b"help\npwd\nexit\n")
        self.assertTrue(wrote, "Boot did not reach the shell prompt")
        self.assertEqual(0, code, data[-350:])
        for marker in (
            b"Boot completato",
            b"Comandi standard:",
            b"pwd",
            b"exit",
            b"nexs-os:/> ",
            b"\r\n/\r\n",
        ):
            self.assertIn(marker, data)
        self.assertNotIn(b"cannot open", data)
        self.assertNotIn(b"function not found", data)
        self.assertNotIn(b"[NEXS FATAL ERROR]", data)

    def test_druglords_quit_at_intro_in_real_pty(self):
        code, wrote, data = run_pty_script(
            "example/druglords/druglords.nx",
            b"Press ENTER to start or 'q' to quit...",
            b"q\n")
        self.assertTrue(wrote, "Game did not reach interactive intro")
        self.assertEqual(0, code, data[-350:])
        self.assertIn(b"DRUG LORDS", data)
        self.assertIn(b"q\r\n", data)
        self.assertNotIn(b"cannot open", data)
        self.assertNotIn(b"function not found", data)
        self.assertNotIn(b"[NEXS FATAL ERROR]", data)


if __name__ == "__main__":
    unittest.main()
