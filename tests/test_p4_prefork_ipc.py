"""Real hosted fork IPC regression tests, not REG/capability certification.

Exercise the existing Base-NEXS registry queue and sysproc process provider
through actual NEXS programs and a real Unix fork. No fake IPC provider.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "nxscript"


class PreforkIpcRuntimeTests(unittest.TestCase):
    def run_nexs(self, source: str, timeout: int = 20):
        with tempfile.TemporaryDirectory(dir=ROOT / "build",
                                         prefix="p4-prefork-") as temp:
            entry = Path(temp) / "probe.nx"
            entry.write_text(source, encoding="utf-8")
            return subprocess.run(
                [str(BIN), str(entry)], cwd=ROOT.parents[2],
                capture_output=True, text=True, timeout=timeout)

    def test_prefork_queue_and_child_message_share_inherited_provider(self):
        result = self.run_nexs(
            'sendmessage("/p4/bridge", "PREEXIST")\n'
            'child = rfork(reg /sys/rfork/RFPROC)\n'
            'if child == 0 {\n'
            ' sendmessage("/p4/bridge", "CHILD_SENT")\n'
            ' exits("")\n'
            '} else {\n'
            ' sleep(220)\n'
            ' out receivemessage("/p4/bridge")\n'
            ' out receivemessage("/p4/bridge")\n'
            ' out "P4_PARENT_DONE"\n'
            '}\n')
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["PREEXIST", "CHILD_SENT", "P4_PARENT_DONE"],
                         result.stdout.splitlines()[-3:])

    def test_drained_local_queue_does_not_duplicate_after_fork(self):
        result = self.run_nexs(
            'sendmessage("/p4/empty", 17)\n'
            'out receivemessage("/p4/empty")\n'
            'child = rfork(reg /sys/rfork/RFPROC)\n'
            'if child == 0 {\n'
            ' sendmessage("/p4/empty", 43)\n'
            ' exits("")\n'
            '} else {\n'
            ' sleep(180)\n'
            ' out receivemessage("/p4/empty")\n'
            ' out type(receivemessage("/p4/empty"))\n'
            ' out "P4_NO_DUPLICATE"\n'
            '}\n')
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["17", "43", "nil", "P4_NO_DUPLICATE"],
                         result.stdout.splitlines()[-4:])

    def test_preflight_pipe_capacity_failure_retains_original_message(self):
        result = self.run_nexs(
            'data = "z"\n'
            'i = 0\n'
            'loop {\n'
            ' if i >= 13 { break }\n'
            ' data = data + data\n'
            ' i = i + 1\n'
            '}\n'
            'i = 0\n'
            'loop {\n'
            ' if i >= 32 { break }\n'
            ' sendmessage("/p4/large", data)\n'
            ' i = i + 1\n'
            '}\n'
            'child = rfork(reg /sys/rfork/RFPROC)\n'
            'if child == 0 {\n'
            ' exits("")\n'
            '} else {\n'
            ' out child\n'
            ' received = receivemessage("/p4/large")\n'
            ' out len(received)\n'
            ' out msgpending("/p4/large")\n'
            ' out "P4_NO_MESSAGE_LOSS"\n'
            '}\n', timeout=24)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["-1", "8192", "31", "P4_NO_MESSAGE_LOSS"],
                         result.stdout.splitlines()[-4:])

    def test_pipe_conversion_occurs_before_fork_in_actual_source(self):
        ipc = (ROOT / "registry/reg_ipc.c").read_text()
        process = (ROOT / "sys/sysproc.c").read_text()
        body = process[process.index("int nexs_rfork("):
                       process.index("int nexs_await(")]
        self.assertLess(body.index("reg_ipc_enable_pipes()"),
                        body.index("fork()"))
        self.assertNotIn("PIPE_STACK_MAX", ipc)
        hosted = (ROOT / "hal/hal_hosted.c").read_text()
        freestanding = (ROOT / "hal/common/pipe_transport_unavailable.c").read_text()
        self.assertIn("O_NONBLOCK", hosted)
        self.assertIn("int nexs_ipc_pipe_open(", hosted)
        self.assertIn("void nexs_ipc_pipe_abort(", hosted)
        self.assertNotIn("fcntl.h", ipc)
        self.assertNotIn("if (pipe(pair)", ipc)
        self.assertIn("nexs_ipc_pipe_open(pair)", ipc)
        self.assertIn("return -1;", freestanding)
        self.assertIn("return walk_and_enable_pipes", ipc)


if __name__ == "__main__":
    unittest.main()
