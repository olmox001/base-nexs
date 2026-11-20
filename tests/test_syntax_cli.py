"""Regression tests for non-executing parser API, error propagation."""
import subprocess
import unittest
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
CLI = str(BASE / "nxscript")


class SyntaxCLITests(unittest.TestCase):
    def run_syntax(self, relative: str) -> subprocess.CompletedProcess:
        return subprocess.run([CLI, "--check-syntax", relative],
                              cwd=BASE, capture_output=True, text=True,
                              timeout=8, check=False)

    def test_valid_nx_script(self):
        process = self.run_syntax("example/test.nx")
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertIn("[syntax] PASS", process.stdout)

    def test_file_not_found_is_failure(self):
        process = self.run_syntax("tests/fixtures/nonexistent.nx")
        self.assertNotEqual(0, process.returncode)

    def test_invalid_nx_syntax_is_failure(self):
        process = self.run_syntax("tests/fixtures/invalid-syntax.nx")
        self.assertNotEqual(0, process.returncode)
        self.assertIn("syntax error", process.stderr)

    def test_parse_only_does_not_execute_program(self):
        process = self.run_syntax("example/test.nx")
        self.assertNotIn("Hello World", process.stdout)


if __name__ == "__main__":
    unittest.main()
