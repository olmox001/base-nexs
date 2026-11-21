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
import termios
import tty
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("NEXS_TEST_BINARY", str(ROOT / "nxscript")))
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
    def test_repl_pasted_commands_survive_raw_mode_transitions(self):
        master, slave = pty.openpty()
        process = None
        received = bytearray()
        submitted = False
        deadline = time.monotonic() + 10.0
        try:
            process = subprocess.Popen(
                [str(CLI)], cwd=OUTSIDE, stdin=slave,
                stdout=slave, stderr=slave, start_new_session=True)
            os.close(slave)
            slave = -1
            while time.monotonic() < deadline:
                readable, _, _ = select.select([master], [], [], 0.1)
                if readable:
                    try:
                        data = os.read(master, 32768)
                    except OSError:
                        data = b""
                    received.extend(data)
                if not submitted and b"> \x1b[K" in received:
                    os.write(master,
                             b'ls\nout "PASTE_FIRST"\nout "PASTE_SECOND"\n:exit\n')
                    submitted = True
                if process.poll() is not None:
                    break
            self.assertTrue(submitted, received[-500:])
            self.assertEqual(process.poll(), 0, received[-900:])
            self.assertIn(b"PASTE_FIRST\r\n", received)
            self.assertIn(b"PASTE_SECOND\r\n", received)
            self.assertIn(b"\r\n[REGISTRY] /\r\n", received)
            self.assertIn(b"\r\nnxlib ", received)
            self.assertNotIn(b"\r\nexample ", received)
        finally:
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=2)
            if slave >= 0:
                os.close(slave)
            os.close(master)

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

    def test_nxed_editor_calibrate_edit_menu_quit_in_real_pty(self):
        # The editor uses the existing Plan 9 read(fd, 1) API, not the
        # REPL line editor. Its terminal must be raw for real key events.
        master, slave = pty.openpty()
        process = None
        output = bytearray()
        calibrated = False
        edited = False
        deadline = time.monotonic() + 10.0
        try:
            tty.setraw(slave, termios.TCSANOW)
            process = subprocess.Popen(
                [str(CLI), str(ROOT / "example/nxed_editor.nx")],
                cwd=OUTSIDE, stdin=slave, stdout=slave, stderr=slave,
                start_new_session=True)
            os.close(slave)
            slave = -1
            while time.monotonic() < deadline:
                readable, _, _ = select.select([master], [], [], 0.1)
                if readable:
                    try:
                        data = os.read(master, 32768)
                    except OSError:
                        data = b""
                    output.extend(data)
                if not calibrated and b"CTRL+X" in output:
                    os.write(master, b"\r\x1b\x7f\x18")
                    calibrated = True
                if calibrated and not edited and b"NXED v13" in output:
                    os.write(master, b"A\x18q")
                    edited = True
                if process.poll() is not None:
                    break
            self.assertTrue(calibrated and edited, output[-900:])
            self.assertEqual(0, process.poll(), output[-900:])
            self.assertIn(b"> A", output)
            self.assertIn(b"--- MENU DI SISTEMA NEXS ---", output)
            self.assertIn(b"q : Esci senza salvare", output)
            self.assertIn(b"\x1b[?25h", output)
            self.assertNotIn(b"[NEXS FATAL ERROR]", output)
        finally:
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=2)
            if slave >= 0:
                os.close(slave)
            os.close(master)

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
