#!/usr/bin/env python3
"""Real source-closure ownership and fail-closed seL4 build contracts.

The tests check the exact production catalog and Make recipes. They are
deliberately scoped to the structural transition; no simulated boot passes.
"""
from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULES = ("state_store", "nxreg", "capability_adapter",
           "snapshot_pool", "schema", "migration")
HEADERS = ("nexs_state_store", "nexs_nxreg",
           "nexs_nxreg_capability_adapter", "nexs_snapshot_pool",
           "nexs_schema", "nexs_migration")


def entries(source: str, name: str) -> list[str]:
    match = re.search(
        rf"(?m)^{re.escape(name)}\s*:=\s*((?:[^\n]*\\\n)*[^\n]*)",
        source,
    )
    if match is None:
        raise AssertionError(f"Missing canonical group: {name}")
    return re.findall(r"[a-zA-Z0-9_./-]+\.c", match.group(1))


class CanonicalStatePlacement(unittest.TestCase):
    def test_one_implementation_and_one_public_interface_per_module(self):
        catalog = (ROOT / "config/sources.mk").read_text()
        shared = entries(catalog, "NEXS_SHARED_SRCS")
        self.assertEqual(len(shared), len(set(shared)))
        makefile = (ROOT / "Makefile").read_text()
        for name in MODULES:
            path = f"state/registry/{name}.c"
            with self.subTest(name=name):
                self.assertTrue((ROOT / path).is_file())
                self.assertFalse((ROOT / "registry" / f"{name}.c").exists())
                self.assertEqual(shared.count(path), 1)
                self.assertIn(path, makefile)
        self.assertIn("-Istate/registry/include", makefile)
        for name in HEADERS:
            with self.subTest(header=name):
                path = ROOT / "state/registry/include" / f"{name}.h"
                self.assertTrue(path.is_file())
                self.assertFalse((ROOT / "registry/include" / f"{name}.h").exists())

    def test_signed_authority_is_in_actual_product_link_closure(self):
        makefile = (ROOT / "Makefile").read_text()
        closure = entries(makefile, "NEXS_STATE_PRODUCT_LINK_SRCS")
        self.assertEqual(closure.count("state/authority/capability.c"), 1)
        for name in MODULES:
            self.assertEqual(closure.count(f"state/registry/{name}.c"), 1)
        self.assertEqual(len(closure), len(set(closure)))
        test_rule = makefile.split("test-nxolmo-regkey-integration: $(TARGET)", 1)[1]
        test_rule = test_rule.split("\n.PHONY:", 1)[0]
        self.assertIn("$(NEXS_STATE_PRODUCT_LINK_OBJS)", test_rule)


class SeL4SourceClosure(unittest.TestCase):
    def test_single_canonical_privileged_kernel_source_group(self):
        catalog = (ROOT / "config/sources.mk").read_text()
        sources = entries(catalog, "NEXS_SEL4_PRIVILEGED_SRCS")
        self.assertEqual(
            sources,
            ["kernel/msg.c", "kernel/syscall.c", "kernel/vfs.c",
             "kernel/blk.c", "kernel/journal.c"],
        )
        self.assertEqual(len(sources), len(set(sources)))
        for source in sources:
            self.assertTrue((ROOT / source).is_file())
        makefile = (ROOT / "Makefile").read_text()
        self.assertEqual(makefile.count("$(NEXS_SEL4_PRIVILEGED_SRCS)"), 2)
        sel4_section = makefile.split("sel4-microkit: $(TARGET)", 1)[1]
        sel4_section = sel4_section.split("\nsel4-multikernel-aarch64:", 1)[0]
        for source in sources:
            self.assertNotIn(source + " ", sel4_section)

    def test_no_silent_service_codegen_or_compile_fallback(self):
        makefile = (ROOT / "Makefile").read_text()
        multi = makefile.split("sel4-multikernel: $(TARGET)\n", 1)[1]
        multi = multi.split("\nsel4-multikernel-aarch64:", 1)[0]
        self.assertTrue(multi.startswith("\t@set -e; "))
        self.assertNotIn("no fs script", multi)
        self.assertNotIn("no pm script", multi)
        self.assertNotIn("no tty script", multi)
        for service in ("fs", "pm", "tty"):
            source = f"nxlib/services/{service}/init.nx"
            self.assertTrue((ROOT / source).is_file())
            regex = re.compile(
                r"\./\$\(TARGET\) --codegen " + re.escape(source) +
                r" -o [^;\n]+ \|\| exit 1;"
            )
            self.assertRegex(multi, regex)

    def test_make_rejects_missing_microkit_inputs_before_build(self):
        result = subprocess.run(
            ["make", "-s", "--no-print-directory",
             "MICROKIT_SDK=", "MICROKIT_BOARD=", "MICROKIT_CONFIG=",
             "sel4-multikernel"],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be specified", result.stderr + result.stdout)
        self.assertNotIn("Done ->", result.stdout)


if __name__ == "__main__":
    unittest.main()
