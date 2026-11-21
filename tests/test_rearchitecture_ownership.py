"""Fail-closed canonical ownership tests (index != implementation)."""
import csv
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "docs/rearchitecture"


class CanonicalOwnershipTests(unittest.TestCase):
    def rows(self, filename):
        with (REPORT / filename).open(newline="", encoding="utf-8") as f:
            return list(csv.DictReader(f))

    def test_every_indexed_definition_has_one_owner(self):
        source = self.rows("FUNCTION_CATALOG.csv")
        mapped = self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
        self.assertEqual(len(source), len(mapped))
        self.assertEqual({r["id"] for r in source},
                         {r["function_id"] for r in mapped})
        self.assertEqual(len(mapped),
                         len({r["function_id"] for r in mapped}))
        self.assertTrue(all(r["canonical_role"] and r["canonical_module"]
                            and r["target_gate"] for r in mapped))

    def test_external_function_never_falsely_claims_implementation(self):
        rows = self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
        ported = [r for r in rows
                  if r["migration_state"] == "PORT_IMPLEMENTED_TESTED"]
        self.assertEqual(len(ported), 27)
        extensions = [r for r in rows if r["migration_state"] ==
                      "BASE_NEXS_ABI_EXTENSION_TESTED_NOT_NXOLMO_PORT"]
        self.assertEqual({r["source_symbol"] for r in extensions},
                         {"nx_i64_add", "nx_i64_sub", "nx_i64_mul"})
        self.assertEqual({r["source_project"] for r in extensions},
                         {"BASE-NEXS"})
        self.assertEqual({r["source_project"] for r in ported},
                         {"BASE-NEXS"})
        self.assertEqual({r["source_file"] for r in ported},
                         {"core/checked.c", "core/status.c",
                          "core/types.c", "core/result.c",
                          "core/include/nexs_endian.h"})
        self.assertFalse(any(
            r["source_project"] in ("NXOLMO", "NXEC") and
            r["migration_state"] == "PORT_IMPLEMENTED_TESTED"
            for r in rows))

    def test_nxolmo_absent_from_product_build(self):
        text = (ROOT / "Makefile").read_text() + (
            ROOT / "config/sources.mk").read_text()
        for forbidden in (
            "-Iexternal/references/nxolmo/include",
            "external/references/nxolmo/src/",
            "NXOLMO_CHECKED_REF", "NXOLMO_CHECKED_OBJ"):
            self.assertNotIn(forbidden, text)
        self.assertIn("core/checked.c", text)
        self.assertIn("core/status.c", text)
        self.assertIn('#include "include/nexs_checked.h"',
                      (ROOT / "core/checked.c").read_text())
        # The page allocator is not yet accepted as the new memory engine:
        # no dependency on an unfinished pager port belongs in this gate.
        self.assertNotIn('#include "nxolmo/',
                         (ROOT / "core/pager.c").read_text())

    def test_real_nxolmo_registry_ports_are_classified_without_fake_p3_exit(self):
        records = self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
        actual = (ROOT / "config/sources.mk").read_text()
        module_names = ("state_store", "nxreg", "snapshot_pool",
                        "schema", "migration")
        for module in module_names:
            product_path = f"state/registry/{module}.c"
            original_path = f"src/registry/{module}.c"
            product = [r for r in records if
                       r["source_project"] == "BASE-NEXS"
                       and r["source_file"] == product_path]
            original = [r for r in records if
                        r["source_project"] == "NXOLMO"
                        and r["source_file"] == original_path]
            self.assertTrue(product, product_path)
            self.assertTrue(original, original_path)
            self.assertIn(product_path, actual)
            self.assertTrue((ROOT / product_path).is_file())
            self.assertEqual(
                {"NXOLMO_CANONICAL_SOURCE_LINKED_REAL_TESTS_P3_PENDING"},
                {r["migration_state"] for r in product})
            self.assertEqual(
                {"NXOLMO_REFERENCE_SOURCE_PROMOTED_P3_EXIT_PENDING"},
                {r["migration_state"] for r in original})
            self.assertTrue(all("full P3 authority/transaction/MVCC" in
                                r["implementation_evidence"]
                                for r in product + original))
        manifest = json.loads(
            (REPORT / "CANONICAL_OWNERSHIP_MANIFEST.json").read_text())
        self.assertFalse(manifest["entire_architecture_ported"])

    def test_hcd_9p_and_registry_are_not_marked_ported(self):
        rows = self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
        self.assertTrue(any(
            r["source_project"] == "NXEC" and
            r["migration_state"] == "REFERENCE_FUNCTION_NOT_PORTED"
            for r in rows))
        self.assertTrue(any(
            r["source_file"].startswith("src/filesystem/9p/") and
            r["migration_state"] == "IMPORTED_REFERENCE_NOT_PRODUCTION"
            for r in rows))
        self.assertTrue(any(
            r["source_file"] == "registry/registry.c" and
            r["migration_state"] == "CURRENT_LEGACY_IMPLEMENTATION_UNQUALIFIED"
            for r in rows))

    def test_single_callable_namespace_is_not_mislabeled_as_state_store(self):
        rows = self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
        callables = [
            r for r in rows
            if r["source_project"] == "BASE-NEXS"
            and r["source_file"] == "lang/fn_table.c"
        ]
        self.assertTrue(callables)
        self.assertTrue(all(
            r["migration_state"] ==
            "REGISTRY_CALLABLE_OBJECT_TESTED_P3_EXECUTOR_PENDING"
            for r in callables))
        self.assertFalse(any(
            r["migration_state"] == "PORT_IMPLEMENTED_TESTED"
            for r in callables))

    def test_registry_integrity_not_claimed_as_state_store_port(self):
        rows = [
            r for r in self.rows("CANONICAL_FUNCTION_OWNERSHIP.csv")
            if r["source_project"] == "BASE-NEXS" and
            r["source_file"] == "registry/registry.c"
        ]
        hardened = [
            r for r in rows if r["migration_state"] ==
            "REGKEY_PROJECTION_INTEGRITY_TESTED_P3_NAMESPACE_PENDING"
        ]
        self.assertGreaterEqual(len(hardened), 15)
        self.assertTrue(any(
            r["migration_state"] == "CURRENT_LEGACY_IMPLEMENTATION_UNQUALIFIED"
            for r in rows))
        self.assertFalse(any(
            r["migration_state"] == "PORT_IMPLEMENTED_TESTED"
            for r in rows))
        self.assertTrue(all(
            "P3 named namespace transactions and authority pending" in
            r["implementation_evidence"]
            for r in hardened))

    def test_manifest_honesty(self):
        d = json.loads((REPORT / "CANONICAL_OWNERSHIP_MANIFEST.json").read_text())
        self.assertEqual(d["source_definitions"], d["mapped_definitions"])
        self.assertFalse(d["entire_architecture_ported"])
        self.assertFalse(d["build_uses_external_nxolmo"])
        self.assertEqual(d["migration_counts"]["PORT_IMPLEMENTED_TESTED"], 27)


if __name__ == "__main__":
    unittest.main()
