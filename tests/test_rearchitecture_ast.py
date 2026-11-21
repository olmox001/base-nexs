"""Assert that architecture inventory never fabricates compiler evidence."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE / "scripts"))
import rearchitecture_clang_ast as astinv


class CompilerASTCatalogTests(unittest.TestCase):
    def test_declaration_without_body_cannot_pass(self):
        node = {
            "kind": "TranslationUnitDecl",
            "inner": [
                {"kind": "FunctionDecl", "name": "test_function",
                 "loc": {"line": 2}, "inner": []}
            ],
        }
        functions, calls = astinv.inspect_ast(
            node, "BASE-NEXS", "host-darwin-c11", "test.c")
        self.assertEqual([], functions)
        self.assertEqual([], calls)

    def test_system_header_declaration_cannot_impersonate_source(self):
        node = {
            "kind": "TranslationUnitDecl",
            "inner": [
                {"kind": "FunctionDecl", "name": "header_function",
                 "loc": {"line": 2, "file": "/usr/include/header.h",
                         "includedFrom": {"file": "test.c"}},
                 "inner": [{"kind": "CompoundStmt"}]}
            ],
        }
        functions, _ = astinv.inspect_ast(
            node, "BASE-NEXS", "host-darwin-c11", "test.c")
        self.assertEqual([], functions)

    def test_ast_direct_call_and_indirect_call_are_separate(self):
        source = r"""
typedef int (*Fn)(int);
static int square(int v) { return v*v; }
int invoke(Fn callback, int arg) {
    return square(arg) + callback(arg);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "sample.c"
            file.write_text(source)
            data, diagnostic = astinv.run_ast(
                Path(directory), "sample.c", ["-std=c11"])
            self.assertIsNotNone(data, diagnostic)
            functions, calls = astinv.inspect_ast(
                data, "BASE-NEXS", "host-darwin-c11", "sample.c")
            self.assertEqual({"square", "invoke"},
                             {f["symbol"] for f in functions})
            found = [call for call in calls if call["caller"] == "invoke"]
            self.assertTrue(any(c["callee"] == "square" for c in found))
            self.assertTrue(any(not c["callee"] for c in found))
            indirect = [c for c in found if not c["callee"]]
            self.assertEqual(len(indirect), 1)
            self.assertEqual(indirect[0]["receiver_symbol"], "callback")
            self.assertEqual(indirect[0]["receiver_decl_kind"], "ParmVarDecl")
            self.assertIn("int", indirect[0]["receiver_abi_type"])
            self.assertEqual(indirect[0]["resolution"],
                             "INDIRECT_OR_MACRO_CALL")
            self.assertEqual(indirect[0]["target_candidates"], "")

    def test_loop_and_goto_are_visible_in_compiler_ast(self):
        source = r"""
int steps(int count) {
    int i = 0;
    while (i < count) {
        if (i == 2) break;
        i++;
    }
    return i;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "sample.c").write_text(source)
            data, diagnostic = astinv.run_ast(
                Path(directory), "sample.c", ["-std=c11"])
            self.assertIsNotNone(data, diagnostic)
            functions, _ = astinv.inspect_ast(
                data, "BASE-NEXS", "host-darwin-c11", "sample.c")
            self.assertEqual(1, len(functions))
            self.assertIn("WhileStmt:1", functions[0]["control_flow_nodes"])
            self.assertIn("BreakStmt:1", functions[0]["control_flow_nodes"])

    def test_clang_failure_is_not_an_empty_valid_program(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "invalid.c").write_text(
                "int broken( { return 0; }\n")
            data, diagnostic = astinv.run_ast(
                Path(directory), "invalid.c", ["-std=c11"])
            self.assertIsNone(data)
            self.assertTrue(diagnostic)

    def test_architectures_have_explicit_qualified_target(self):
        for path, target in (
            ("hal/amd64/uart.c", "x86_64-elf"),
            ("hal/os1/amd64/arch.c", "x86_64-elf"),
            ("hal/os1/arm64/arch.c", "aarch64-none-elf"),
            ("hal/arm64/mmu.c", "aarch64-none-elf"),
            ("hal/riscv64/uart.c", "riscv64-none-elf"),
            ("kernel/proc.c", "x86_64-elf"),
        ):
            with self.subTest(path=path):
                profile = astinv.choose_profile("BASE-NEXS", path)
                self.assertIsNotNone(profile)
                self.assertIn(target, profile[0])
                self.assertIn(target, profile[1])
        self.assertIsNone(astinv.choose_profile(
            "BASE-NEXS", "hal/sel4/hal_sel4.c"))

    def test_value_comparators_have_real_compiler_bodies(self):
        # The former CMP_OP token-pasting macro emitted linked functions
        # that the source index mislabeled as declaration-only. Regress
        # against actual selected Clang AST bodies, not string mentions.
        with (BASE / "docs/rearchitecture/CLANG_AST_FUNCTIONS.csv").open(
                newline="", encoding="utf-8") as fh:
            functions = list(csv.DictReader(fh))
        names = {"val_lt", "val_gt", "val_le", "val_ge"}
        owned = [row for row in functions
                 if row["project"] == "BASE-NEXS"
                 and row["path"] == "core/value.c"
                 and row["symbol"] in names]
        self.assertEqual(names, {row["symbol"] for row in owned})
        self.assertEqual(len(names), len(owned))
        self.assertTrue(all(row["body_status"] ==
                            "COMPILER_AST_BODY_PARSED_NOT_SEMANTICALLY_REVIEWED"
                            for row in owned))
        with (BASE / "docs/rearchitecture/DECLARATION_GAPS.csv").open(
                newline="", encoding="utf-8") as fh:
            gaps = list(csv.DictReader(fh))
        self.assertFalse(any(
            row["project"] == "BASE-NEXS" and row["symbol"] in names
            for row in gaps))
        with (BASE / "docs/rearchitecture/FUNCTION_CATALOG.csv").open(
                newline="", encoding="utf-8") as fh:
            catalog = list(csv.DictReader(fh))
        declared = [row for row in catalog
                    if row["project"] == "BASE-NEXS"
                    and row["path"] == "core/value.c"
                    and row["symbol"] in names]
        self.assertEqual(names, {row["symbol"] for row in declared})
        self.assertEqual(len(names), len(declared))

    def test_dead_api_prototypes_are_not_exported_as_product(self):
        # P0 does not retain exported declarations for imaginary decoders,
        # reallocation or TTY probes. The future P6 codec is still absent.
        for name, symbol in (
            ("compiler/include/nexs_bitcode.h", "nexs_encode_bc"),
            ("compiler/include/nexs_bitcode.h", "nexs_decode_bc"),
            ("compiler/include/nexs_bitcode.h", "nexs_free_bc"),
            ("core/include/nexs_alloc.h", "xrealloc"),
            ("runtime/include/nexs_line.h", "nexs_line_is_tty"),
        ):
            with self.subTest(symbol=symbol):
                text = (BASE / name).read_text()
                self.assertNotIn(symbol + "(", text)
        with (BASE / "docs/rearchitecture/DECLARATION_GAPS.csv").open(
                newline="", encoding="utf-8") as fh:
            gaps = list(csv.DictReader(fh))
        self.assertFalse(any(
            row["project"] == "BASE-NEXS" and
            row["symbol"] in {"nexs_encode_bc", "nexs_decode_bc",
                              "nexs_free_bc", "xrealloc",
                              "nexs_line_is_tty"} for row in gaps))

    def test_exported_manifest_contains_all_statuses(self):
        manifest = BASE / "docs/rearchitecture/CLANG_AST_MANIFEST.json"
        self.assertTrue(manifest.is_file(), "P0 requires the AST inventory")
        report = json.loads(manifest.read_text())
        self.assertIn("sources_by_status", report)
        with (BASE / "docs/rearchitecture/CLANG_AST_TRANSLATION_UNITS.csv").open() as fh:
            rows = list(csv.DictReader(fh))
        self.assertTrue(rows)
        self.assertEqual(
            sum(report["sources_by_status"].values()), len(rows))
        self.assertEqual(
            0, sum(1 for r in rows if not r["result"]))


if __name__ == "__main__":
    unittest.main()
