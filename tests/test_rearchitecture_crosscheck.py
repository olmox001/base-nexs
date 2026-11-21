"""Cross-reference all Git-indexed Base, NXOLMO and NXEC source/headers."""
from __future__ import annotations
import csv
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import rearchitecture_crosscheck as audit


class CrosscheckTests(unittest.TestCase):
    def test_every_relevant_worktree_and_pinned_git_source_is_mapped_once(self):
        rows, revisions = audit.build_rows()
        seen = set()
        for row in rows:
            key = row["repository"], row["source"]
            self.assertNotIn(key, seen)
            seen.add(key)
            source = audit.REPOS[row["repository"]] / row["source"]
            self.assertEqual(row["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertNotEqual("UNASSIGNED", row["phase"])
        self.assertEqual(set(revisions), set(audit.REPOS))
        self.assertGreater(len(rows), 300)

    def test_reference_implementations_never_claim_product_port(self):
        rows, _ = audit.build_rows()
        for row in rows:
            if row["repository"] == "BASE-NEXS":
                self.assertEqual("PER_FUNCTION_VERIFICATION_REQUIRED", row["proof"])
            else:
                self.assertEqual("REFERENCE_ONLY", row["proof"])
                self.assertNotEqual("PORT_IMPLEMENTED_TESTED", row["classification"])

    def test_high_value_implementations_have_explicit_phase(self):
        expectations = {
            ("NXOLMO-REFERENCE", "src/registry/state_store.c"): "P3",
            ("NXOLMO-REFERENCE", "src/registry/nxreg.c"): "P3",
            ("NXOLMO-REFERENCE", "src/buffer/buffer.c"): "P2/P4",
            ("NXOLMO-REFERENCE", "src/authority/capability.c"): "P3",
            ("NXOLMO-REFERENCE", "src/journal/wal.c"): "P5",
            ("NXOLMO-REFERENCE", "src/protocol/9p/frame.c"): "P5",
            ("NXEC-REFERENCE", "kernel/core/hcd_gate.c"): "P7",
            ("BASE-NEXS", "registry/reg_ipc.c"): "P4",
        }
        rows, _ = audit.build_rows()
        indexed = {(r["repository"], r["source"]): r["phase"] for r in rows}
        for name, expected in expectations.items():
            self.assertEqual(expected, indexed.get(name))

    def test_manifest_matches_exact_current_worktree_inventory(self):
        with (ROOT / "docs/rearchitecture/P4_SOURCE_CROSSCHECK.csv").open(
                newline="", encoding="utf-8") as source:
            indexed = list(csv.DictReader(source))
        manifest = json.loads((ROOT / "docs/rearchitecture/P4_SOURCE_CROSSCHECK_MANIFEST.json").read_text())
        rows, versions = audit.build_rows()
        self.assertEqual(rows, indexed)
        self.assertEqual(manifest["tracked_files"], len(indexed))
        self.assertEqual(
            manifest["source_scope"],
            "BASE_PRODUCT_INDEX_PLUS_UNTRACKED_WORKTREE_AND_PINNED_REFERENCES")
        self.assertEqual(manifest["revisions"], versions)
        for source in (
            "kernel/test/test_cap_cspace_integrity.c",
            "kernel/test/test_cap_cspace_oom.c",
            "kernel/test/test_ipc_alloc_core.c",
        ):
            with self.subTest(source=source):
                self.assertIn(
                    ("BASE-NEXS", source),
                    {(row["repository"], row["source"]) for row in indexed})
                self.assertEqual(
                    "PRODUCT_TEST_OR_TOOL",
                    next(row["classification"] for row in indexed
                         if row["repository"] == "BASE-NEXS" and
                         row["source"] == source))
        self.assertEqual(manifest["unclassified"], 0)
        self.assertIn("NO_PORT_OR_PROOF", manifest["status"])


if __name__ == "__main__":
    unittest.main()
