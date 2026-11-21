"""Fail-closed structural checks on the source-level redesign inventory."""
import csv
import hashlib
import json
import subprocess
import sys
import unittest
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORTS = ROOT / "docs/rearchitecture"


def read_csv(name):
    with (REPORTS / name).open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


class SourceCatalogTests(unittest.TestCase):
    def test_every_definition_has_a_unique_id_and_review_status(self):
        definitions = read_csv("FUNCTION_CATALOG.csv")
        self.assertGreater(len(definitions), 4000)
        ids = [row["id"] for row in definitions]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(row["target_contract"] and
                            row["required_transformation"] and
                            row["verification"] and
                            row["review_status"].startswith("AUTO_")
                            for row in definitions))
        self.assertTrue(any(row["project"] == "NXEC" and
                            row["symbol"] == "preboot_uefi_normalize_memory_map"
                            for row in definitions))
        self.assertTrue(any(row["project"] == "NXOLMO" and
                            row["symbol"] == "nxreg_dispatch"
                            for row in definitions))

    def test_file_hashes_protect_source_provenance(self):
        files = read_csv("SOURCE_FILE_CATALOG.csv")
        revisions = json.loads(
            (REPORTS / "INVENTORY_MANIFEST.json").read_text()
        )["source_revisions"]
        self.assertEqual(len(revisions), 6)
        project_roots = {
            "BASE-NEXS": ROOT,
            "NXOLMO": ROOT / "external/references/nxolmo",
            "NXEC": ROOT / "external/references/nxolmo/external/references/nxec",
            "NEXSOS1-KERNEL-REFERENCE": ROOT.parents[2] / "kernel",
            "NEXSOS1-SDK-REFERENCE": ROOT.parents[2] / "system",
            "NEXSOS1-USERKIT-REFERENCE": ROOT.parents[2] / "user/USERKIT",
        }
        self.assertEqual(set(project_roots), set(revisions))
        for record in files:
            with self.subTest(path=record["path"], project=record["project"]):
                path = project_roots[record["project"]] / record["path"]
                self.assertTrue(path.is_file())
                self.assertEqual(record["sha256"],
                                 hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertTrue(any(
            r["role"] == "unadapted-linux-reference-gate-blocker"
            for r in files))

    def test_current_untracked_kernel_tests_are_not_silently_excluded(self):
        source = read_csv("SOURCE_FILE_CATALOG.csv")
        owned = {(row["project"], row["path"]): row for row in source}
        for path in (
            "kernel/test/test_cap_cspace_integrity.c",
            "kernel/test/test_cap_cspace_oom.c",
            "kernel/test/test_ipc_alloc_core.c",
        ):
            with self.subTest(path=path):
                self.assertIn(("BASE-NEXS", path), owned)
                self.assertEqual("test", owned[("BASE-NEXS", path)]["role"])
                self.assertEqual(
                    hashlib.sha256((ROOT / path).read_bytes()).hexdigest(),
                    owned[("BASE-NEXS", path)]["sha256"])
        definitions = read_csv("FUNCTION_CATALOG.csv")
        tests = [row for row in definitions
                 if row["project"] == "BASE-NEXS" and
                 row["path"].startswith("kernel/test/")]
        self.assertTrue(tests)
        self.assertEqual({"test"}, {row["role"] for row in tests})

    def test_test_traceability_is_explicitly_unverified(self):
        definitions = read_csv("FUNCTION_CATALOG.csv")
        plans = read_csv("PROPOSED_TEST_TRACEABILITY.csv")
        owned = {r["id"] for r in definitions if r["role"] == "product"}
        self.assertEqual(owned, {r["function_id"] for r in plans})
        self.assertTrue(all(
            row["existing_test_mapping_status"] == "NOT_REVIEWED_OR_LINKED"
            and row["proposed_success_test_id"]
            and row["proposed_fault_test_id"]
            for row in plans))

    def test_lexical_edges_not_reported_as_semantically_verified(self):
        edges = read_csv("CALL_EDGE_CANDIDATES.csv")
        self.assertTrue(edges)
        self.assertEqual(
            {"LEXICAL_NAME_ONLY_NOT_VALIDATED_CALL_EDGE"},
            {row["resolution"] for row in edges})

    def test_strict_baseline_assignments_remain_blocking(self):
        issues = read_csv("POWER12_BASELINE_BLOCKERS.csv")
        report = json.loads(
            (REPORTS / "POWER12_BASELINE_MANIFEST.json").read_text())
        self.assertEqual(report["all_issues_assigned"], len(issues))
        self.assertTrue(issues)
        self.assertTrue(all(
            row["assigned_phase"] and row["owner"] and
            row["current_severity"] == "BLOCK" and
            row["review_state"] == "UNRESOLVED_TRACKED_NEVER_WAIVED"
            for row in issues))
        self.assertEqual(
            len(issues), len({row["issue_id"] for row in issues}))
        self.assertFalse(report["fully_power12_compliant"])

    def test_function_compiler_coverage_is_classified_without_false_pass(self):
        source = read_csv("FUNCTION_CATALOG.csv")
        rows = read_csv("FUNCTION_COMPILER_COVERAGE.csv")
        self.assertEqual(len(source), len(rows))
        self.assertEqual(
            {row["id"] for row in source},
            {row["function_id"] for row in rows})
        self.assertFalse(any(
            row["compiler_coverage"] == "UNRESOLVED_COMPILER_EVIDENCE"
            for row in rows))
        self.assertTrue(any(
            row["compiler_coverage"] == "UNQUALIFIED_TOOLCHAIN"
            for row in rows))

    def test_compiler_ast_has_explicit_results_and_real_bodies(self):
        manifest = json.loads(
            (REPORTS / "CLANG_AST_MANIFEST.json").read_text())
        targets = read_csv("CLANG_AST_TRANSLATION_UNITS.csv")
        funcs = read_csv("CLANG_AST_FUNCTIONS.csv")
        edges = read_csv("CLANG_AST_CALLS.csv")
        self.assertEqual(sum(manifest["sources_by_status"].values()),
                         len(targets))
        self.assertEqual(manifest["functions_with_compiler_body"],
                         len(funcs))
        self.assertEqual(manifest["direct_ast_call_nodes"] +
                         manifest["indirect_or_unresolved_ast_calls"],
                         len(edges))
        self.assertFalse(any(row["result"].startswith("FAIL")
                             for row in targets))
        self.assertTrue(all(
            f["body_status"] == "COMPILER_AST_BODY_PARSED_NOT_SEMANTICALLY_REVIEWED"
            for f in funcs))
        self.assertTrue(any(
            row["result"] == "NOT_QUALIFIED_TARGET_OR_IMPORTED"
            for row in targets))


if __name__ == "__main__":
    unittest.main()
