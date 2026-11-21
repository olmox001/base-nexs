"""AOT ABI regression: the generated C must include the canonical types."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CodegenContractTests(unittest.TestCase):
    def test_empty_program_still_emits_complete_dependency_table(self):
        script = ROOT / "tests/fixtures/repl.nx"
        output = ROOT / "build/assurance/test-aot-empty.c"
        output.parent.mkdir(parents=True, exist_ok=True)
        result = subprocess.run([
            str(ROOT / "nxscript"), "--codegen", str(script),
            "--no-dep", "-o", str(output),
        ], cwd=ROOT, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = output.read_text()
        self.assertIn('#include "nexs_eval.h"', data)
        self.assertIn("const NexsEmbedDep nexs_embed_deps_table[]", data)
        self.assertIn("{ (void*)0, (void*)0, 0u, 0u }", data)
        self.assertNotIn("char  scope[256]", data)

    def test_embed_lookup_is_built_with_same_runtime_abi(self):
        output = ROOT / "build/assurance/test-aot-empty.c"
        if not output.is_file():
            self.test_empty_program_still_emits_complete_dependency_table()
        result = subprocess.run([
            "clang", "-std=c11", "-DPOOL_64MB", "-DNEXS_HOST_TOOL",
            "-Ilang/include", "-Icore/include", "-Iruntime/include",
            "-Ihal/include", "-Isys/include", "-Iregistry/include", "-Istate/registry/include",
            "-fsyntax-only", str(output),
        ], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
