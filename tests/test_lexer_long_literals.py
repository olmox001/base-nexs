"""Real language-level contracts for lossless bounded string/path tokens."""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"


def path_for(prefix: str, count: int = 12) -> str:
    return "/" + "/".join(f"{prefix}{n}abcdefghi" for n in range(count))


class LexerLongLiteralTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=ROOT / "build",
                                                  prefix="lexer-real-")
        self.addCleanup(self.folder.cleanup)
        self.dir = Path(self.folder.name)
        self.script = self.dir / "long_literals.nx"

    def run_nexs(self, script: str, *, syntax: bool = False):
        self.script.write_text(script, encoding="utf-8")
        argv = [str(CLI)]
        if syntax:
            argv.append("--check-syntax")
        argv.append(str(self.script))
        return subprocess.run(argv, cwd=ROOT, capture_output=True,
                              text=True, timeout=30)

    def test_63_64_256_512_8191_characters_survive(self):
        for length in (63, 64, 128, 256, 512, 8191):
            with self.subTest(length=length):
                chars = ("aBcDeFGhI9" * (length // 10 + 1))[:length]
                result = self.run_nexs('out "' + chars + '"\n')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.endswith(chars + "\n"),
                                f"literal {length} was truncated")

    def test_string_above_language_limit_is_rejected(self):
        result = self.run_nexs('out "' + "x" * 8192 + '"\n', syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MAX_STR_LEN", result.stderr)

    def test_unterminated_string_is_rejected(self):
        result = self.run_nexs('out "unterminated\n', syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unterminated", result.stderr)

    def test_escape_decoding_survives_beyond_inline_preview(self):
        content = "ABCDEFGHIJKLMN" * 8
        result = self.run_nexs(r'out "' + content + r'\n\"ab\"tail"' + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith(content + '\n"ab"tail\n'))

    def test_octal_escape_preserves_terminal_control_sequences(self):
        result = self.run_nexs(r'out "\033[0m"' + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("\x1b[0m\n"),
                        result.stdout[-130:])
        self.assertEqual(
            0, self.run_nexs(r'out "\033[0m"' + "\n",
                             syntax=True).returncode)

    def test_embedded_nul_is_rejected_instead_of_truncated(self):
        result = self.run_nexs(r'out "abcdef\0hidden"' + "\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("contains NUL", result.stderr)

    def test_integer_overflow_is_rejected_instead_of_wrapping(self):
        result = self.run_nexs("out 9223372036854775808\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("out-of-range", result.stderr)

    def test_malformed_float_is_rejected_instead_of_partial_conversion(self):
        result = self.run_nexs("out 1.23.456\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid", result.stderr)

    def test_signed_integer_retains_value(self):
        for value in (-42, -9223372036854775808, 9223372036854775807):
            with self.subTest(value=value):
                result = self.run_nexs(f"out {value}\n")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.endswith(f"{value}\n"))

    def test_negative_integer_underflow_is_rejected(self):
        result = self.run_nexs("out -9223372036854775809\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("out-of-range", result.stderr)

    def test_valid_decimal_float_is_not_partial(self):
        result = self.run_nexs("out 0.125\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("0.125\n"), result.stdout[-90:])

    def test_identifier_longer_than_name_abi_is_rejected(self):
        result = self.run_nexs("i" * 65 + " = 42\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NAME_LEN", result.stderr)

    def test_long_registry_path_multiple_components_is_lossless(self):
        path = path_for("segment", 14)
        self.assertGreater(len(path), 63)
        result = self.run_nexs(f"reg {path} = 31\nout {path}\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("31\n"), result.stdout[-200:])

    def test_unsupported_registry_component_cannot_be_silently_truncated(self):
        path = "/" + "k" * 65
        result = self.run_nexs(f"reg {path} = 31\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("component exceeds NAME_LEN", result.stderr)

    def test_registry_path_exceeding_path_abi_is_rejected(self):
        path = path_for("abcdefgh", 40)
        self.assertGreater(len(path), 511)
        result = self.run_nexs(f"reg {path} = 5\n", syntax=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("REG_PATH_MAX", result.stderr)

    def test_long_pointer_targets_remain_lossless(self):
        target = path_for("target", 12)
        alias = path_for("alias", 12)
        self.assertGreater(len(target), 63)
        result = self.run_nexs(
            f"reg {target} = 73\nptr {alias} = {target}\nout deref {alias}\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("73\n"), result.stdout[-200:])

    def test_import_quoted_path_longer_than_63_characters(self):
        imported = self.dir / ("longmodule_" + "q" * 72 + ".nx")
        imported.write_text('out "IMPORTED_LONG_PATH"\n', encoding="utf-8")
        result = self.run_nexs(f'import "{imported}"\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("IMPORTED_LONG_PATH\n"),
                        (result.stdout[-240:], result.stderr[-240:]))

    def test_compiled_aot_preserves_literal_and_escapes(self):
        content = "qwertyZXCVBNM" * 20
        script = 'out "' + content + r'\nlast"' + "\n"
        self.script.write_text(script, encoding="utf-8")
        output = self.dir / "compiled"
        compiled = subprocess.run(
            [str(CLI), "--compile", str(self.script), "--no-dep",
             "--target", "macos-amd64", "-o", str(output)],
            cwd=ROOT, capture_output=True, text=True, timeout=180)
        self.assertEqual(compiled.returncode, 0, compiled.stderr[-1200:])
        run = subprocess.run([str(output)], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, content + "\nlast\n")


if __name__ == "__main__":
    unittest.main()
