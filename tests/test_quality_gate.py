"""Regression checks for the Base-NEXS compile gate and module catalog."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import quality_gate


class CatalogSafetyTests(unittest.TestCase):
    def test_canonical_catalog_is_complete(self):
        self.assertEqual([], quality_gate.catalog_issues())

    def test_strict_os1_sources_are_present(self):
        for path in (
            "hal/os1/os1_driver.c",
            "hal/os1/arm64/arch.c",
            "hal/os1/amd64/arch.c",
        ):
            with self.subTest(path=path):
                self.assertTrue((ROOT / path).is_file())

    def test_old_recursive_reference_is_not_tracked(self):
        self.assertFalse((ROOT / "reference/.git").exists())

    def test_all_proprietary_nx_files_are_cataloged(self):
        entries = [
            line for line in (ROOT / "config/nx_sources.txt").read_text().splitlines()
            if line and not line.startswith("#")
        ]
        self.assertEqual(34, len(entries))
        self.assertTrue(all((ROOT / entry).is_file() for entry in entries))


class LexicalAuditTests(unittest.TestCase):
    def test_strings_and_comments_are_not_executable_violations(self):
        text = "// goto here\nconst char *text = \"assert(1)\\n\";\n"
        self.assertEqual([], quality_gate.audit_source("sample.c", text, True))

    def test_goto_and_break_block_new_adapters(self):
        result = quality_gate.audit_source(
            "hal/os1/example.c",
            "void f(void) { goto err; err: break; }", True)
        self.assertEqual({"no_goto", "no_break"},
                         {issue["rule"] for issue in result})
        self.assertTrue(all(issue["severity"] == "block" for issue in result))

    def test_same_issues_are_warnings_in_imported_code(self):
        result = quality_gate.audit_source(
            "core/legacy.c", "void f(void) { goto err; err: return; }",
            False)
        self.assertTrue(result)
        self.assertTrue(all(issue["severity"] == "warning" for issue in result))

    def test_runtime_assertions_are_blocked(self):
        result = quality_gate.audit_source(
            "hal/os1/example.c", "void f(void) { assert(1); }", True)
        self.assertEqual("no_assert", result[0]["rule"])

    def test_privileged_assembly_is_blocked_on_os1(self):
        result = quality_gate.audit_source(
            "hal/os1/example.c",
            'void f(void) { __asm__ volatile("cli" ::: "memory"); }', True)
        self.assertIn("no_privileged_userland_asm",
                      {issue["rule"] for issue in result})

    def test_memory_fences_are_allowed_on_os1(self):
        result = quality_gate.audit_source(
            "hal/os1/example.c",
            'void f(void) { __asm__ volatile("mfence" ::: "memory"); }',
            True)
        self.assertEqual([], result)

    def test_unbounded_loop_is_a_violation(self):
        result = quality_gate.audit_source(
            "hal/os1/example.c", "void f(void) { while (1) {} }",
            True)
        self.assertEqual("bounded_loop", result[0]["rule"])


if __name__ == "__main__":
    unittest.main()
