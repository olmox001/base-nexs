"""Fail-closed census: every actual kernel syscall verb has fuzz coverage.

This checks static instrumenting scope only; execution evidence comes from
real make test-system-syscall-fuzz and fuzz-system-syscalls runs. It cannot
and does not prove authorization, concurrency, guest execution or 9P2000.L.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DISPATCH = (ROOT / "kernel/syscall.c").read_text()
HARNESS = (ROOT / "tests/fuzz/system_syscall_fuzz.c").read_text()
MAKE = (ROOT / "Makefile").read_text()


class RealSyscallFuzzCoverage(unittest.TestCase):
    def test_every_live_dispatch_verb_is_fuzzed(self):
        table = DISPATCH.split("static const SyscallEntry s_table[] =", 1)[1]
        table = table.split("};", 1)[0]
        defined = re.findall(r'\{\s*"([a-z]+)"\s*,\s*sys_[a-z]+\s*\}', table)
        self.assertGreater(len(defined), 0)
        self.assertEqual(len(defined), len(set(defined)))
        corpus = HARNESS.split("static const char *const verbs[] =", 1)[1]
        corpus = corpus.split("};", 1)[0]
        selected = re.findall(r'"([a-z]+)"', corpus)
        self.assertEqual(set(selected), set(defined))
        self.assertEqual(len(selected), len(defined))

    def test_all_real_call_paths_have_required_entrypoints(self):
        for text in (
            "kmsg_handle(&message)",
            "kmsg_encode(&message)",
            "kmsg_decode(&encoded, &decoded)",
            'reg_ipc_send("/fuzz/kernel_wire", encoded)',
            'reg_ipc_recv("/fuzz/kernel_wire", &received)',
            "reg_ipc_enable_pipes()",
            "p9_server_handle(request, request_length,",
            "nx_state_store_validate(&g_registry.state",
        ):
            with self.subTest(path=text):
                self.assertIn(text, HARNESS)

    def test_all_three_paths_preflight_and_runtime_distinguishes_metrics(self):
        for text in (
            "execute_verb(op, op, 0)",
            "execute_verb(op, op + VERBS, 1)",
            "execute_verb(op, op + 2u * VERBS, 2)",
            "static Metric metric[VERBS + 1u]",
            "static Metric wire_metric[VERBS]",
            "static Metric transport_metric[VERBS]",
            "NX_SYSCALL_FUZZ_SEED",
            "test-system-syscall-fuzz:",
        ):
            with self.subTest(text=text):
                self.assertIn(text, MAKE if text.startswith("NX_") or text.endswith(":") else HARNESS)


if __name__ == "__main__":
    unittest.main()
