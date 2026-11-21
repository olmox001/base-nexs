#!/usr/bin/env python3
"""Focused source-owner tests for the actual NXOLMO provider cutover."""
from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import rearchitecture_clang_ast as ast_audit
import rearchitecture_crosscheck as source_audit

SOURCE_OWNER = {
    "authority/capability.c": "state/authority/capability.c",
    "hardware/capability.c": "state/authority/hardware/capability.c",
    "buffer/buffer.c": "state/memory/buffer/buffer.c",
    "buffer/capability_adapter.c": "state/memory/buffer/capability_adapter.c",
    "block/device.c": "state/storage/block/device.c",
    "block/memory.c": "state/storage/block/memory.c",
    "block/memory_device.c": "state/storage/block/memory_device.c",
    "journal/wal.c": "state/storage/journal/wal.c",
    "fs/9p_frame.c": "state/storage/9p/9p_frame.c",
}
HEADER_OWNER = {
    "authority/include/nexs_capability.h":
        "state/authority/include/nexs_capability.h",
    "hardware/include/nexs_hardware_capability.h":
        "state/authority/hardware/include/nexs_hardware_capability.h",
    "buffer/include/nexs_buffer.h":
        "state/memory/buffer/include/nexs_buffer.h",
    "buffer/include/nexs_buffer_capability_adapter.h":
        "state/memory/buffer/include/nexs_buffer_capability_adapter.h",
    "block/include/nexs_block_device.h":
        "state/storage/block/include/nexs_block_device.h",
    "block/include/nexs_block_memory.h":
        "state/storage/block/include/nexs_block_memory.h",
    "block/include/nexs_block_memory_device.h":
        "state/storage/block/include/nexs_block_memory_device.h",
    "journal/include/nexs_journal_wal.h":
        "state/storage/journal/include/nexs_journal_wal.h",
    "fs/include/nexs_p9_frame.h":
        "state/storage/9p/include/nexs_p9_frame.h",
}


class CanonicalProviderTests(unittest.TestCase):
    def test_all_production_sources_have_one_canonical_link_owner(self):
        catalog = (ROOT / "config/sources.mk").read_text()
        manifest = re.search(
            r"(?m)^NEXS_SHARED_SRCS\s*:=\s*((?:[^\n]*\\\n)*[^\n]*)",
            catalog,
        )
        self.assertIsNotNone(manifest)
        sources = re.findall(r"[\w./-]+\.c", manifest.group(1))
        self.assertEqual(len(sources), len(set(sources)))
        for old, new in SOURCE_OWNER.items():
            with self.subTest(new=new):
                self.assertEqual(1, sources.count(new))
                self.assertEqual(0, sources.count(old))
                self.assertFalse((ROOT / old).is_file())
                self.assertTrue((ROOT / new).is_file())

    def test_all_public_contracts_have_one_canonical_owner(self):
        makefile = (ROOT / "Makefile").read_text()
        for old, new in HEADER_OWNER.items():
            with self.subTest(new=new):
                self.assertFalse((ROOT / old).is_file())
                self.assertTrue((ROOT / new).is_file())
                self.assertIn(new, makefile)
                self.assertIn("-I" + str(Path(new).parent), makefile)

    def test_reclassified_provider_sources_are_in_actual_p0_audit(self):
        for path in SOURCE_OWNER.values():
            with self.subTest(path=path):
                phase, _classification, owner = source_audit.classify(
                    "BASE-NEXS", path
                )
                self.assertNotEqual("UNASSIGNED", phase)
                self.assertTrue(owner)
                profile = ast_audit.choose_profile("BASE-NEXS", path)
                self.assertIsNotNone(profile)
                self.assertEqual("host-darwin-c11", profile[0])
                for flag in profile[1]:
                    self.assertNotIn(" -I", flag)

    def test_single_signed_capability_in_each_direct_test_link(self):
        makefile = (ROOT / "Makefile").read_text()
        for target in ("test-nxolmo-resource-conformance",
                       "perf-billion-buffer"):
            recipe = makefile.split(target + ":", 1)[1]
            recipe = recipe.split("\n\n", 1)[0]
            with self.subTest(target=target):
                self.assertIn("$(NEXS_STATE_PRODUCT_LINK_OBJS)", recipe)
                self.assertNotIn("state/authority/capability.o", recipe)

    def test_real_9p_driver_includes_migrated_frame_interface(self):
        source = (ROOT / "fs/9p.c").read_text()
        self.assertIn('#include "nexs_p9_frame.h"', source)
        self.assertNotIn('#include "include/nexs_p9_frame.h"', source)


class SeL4FailClosedTests(unittest.TestCase):
    def test_all_privileged_pd_build_failures_propagate(self):
        makefile = (ROOT / "Makefile").read_text()
        multi = makefile.split("sel4-multikernel: $(TARGET)", 1)[1]
        multi = multi.split("\nsel4-multikernel-aarch64:", 1)[0]
        for process in ("root", "fs", "pm", "tty"):
            self.assertIn(
                f"-o $$OUT/nexs_{process}.elf || exit 1;", multi,
            )
        self.assertTrue(multi.startswith("\n\t@set -e; "))


if __name__ == "__main__":
    unittest.main()
