"""One compiled NEXS runtime, original language sources and byte-exact assets.

Executes the actual compiler and guest QEMU, not an emulated dependency table.
This is the regressions gate for MiniOS as a LANGUAGE test program.
"""
from __future__ import annotations

import ast
import os
import re
import select
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "nxscript"
ENTRY = ROOT / "example/minios/boot.nx"
LOGOS = tuple((ROOT / "example/minios/logos").glob("*.nxtimg"))


class CompiledRuntimeResourcesTests(unittest.TestCase):
    def test_existing_dependency_scanner_and_exact_generated_bytes(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build/assurance") as temp:
            generated = Path(temp) / "runtime.c"
            compiled = subprocess.run(
                [str(RUNTIME), "--codegen", str(ENTRY),
                 "--hosted", "-o", str(generated)],
                cwd=ROOT, capture_output=True, text=True, timeout=35)
            self.assertEqual(0, compiled.returncode, compiled.stderr)
            text = generated.read_text()
            self.assertIn("example/minios/shell.nx", text)
            self.assertIn("example/minios/logo.nx", text)
            for asset in LOGOS:
                expected = asset.relative_to(ROOT).as_posix()
                match = re.search(
                    r'\{\s*"' + re.escape(expected) +
                    r'",\s*nexs_dep_\d+_src,\s*(\d+),\s*0u\s*\}', text)
                self.assertIsNotNone(match, f"Missing binary {expected}")
                self.assertEqual(asset.stat().st_size, int(match.group(1)))
            self.assertIn("size_t *bytes", text)
            self.assertNotIn("unresolved AOT dependency", compiled.stderr)

    def test_existing_scanner_preserves_opaque_bytes_in_generated_image(self):
        # The AOT linker must not use strlen: C NULs and adjacent octal
        # digits must survive as one immutable payload in the ONE runtime.
        payload = bytes([0x00, 0x37, 0xFF, 0x7F, 0x1B, 0x00, 0x38, 0x80])
        with tempfile.TemporaryDirectory(dir=ROOT / "build/assurance") as temp:
            directory = Path(temp)
            asset = directory / "opaque.nxtimg"
            asset.write_bytes(payload)
            entry = directory / "entry.nx"
            entry.write_text(
                f'vfs_import("{asset}" "sd02/assets/opaque.nxtimg")\n',
                encoding="utf-8")
            generated = directory / "runtime.c"
            built = subprocess.run(
                [str(RUNTIME), "--codegen", str(entry),
                 "--hosted", "-o", str(generated)],
                cwd=ROOT, capture_output=True, text=True, timeout=35)
            self.assertEqual(0, built.returncode, built.stderr)
            source = generated.read_text()
            match = re.search(
                r'static const char nexs_dep_0_src\[\] =\n(".*");',
                source)
            self.assertIsNotNone(match)
            embedded = ast.literal_eval("b" + match.group(1))
            self.assertEqual(payload, embedded)
            self.assertRegex(
                source, r'nexs_dep_0_src,\s*8,\s*0u')
            self.assertIn(r"\0007", source)
            self.assertIn(r"\0008", source)

    def test_arm64_real_guest_displays_the_exact_original_logo(self):
        if shutil.which("qemu-system-aarch64") is None:
            self.skipTest("Real QEMU AArch64 is unavailable")
        with tempfile.TemporaryDirectory(dir=ROOT / "build/assurance") as temp:
            elf = Path(temp) / "runtime.elf"
            built = subprocess.run(
                [str(RUNTIME), "--compile", str(ENTRY), "--target",
                 "baremetal-arm64", "-o", str(elf)],
                cwd=ROOT, capture_output=True, text=True, timeout=110)
            self.assertEqual(0, built.returncode, built.stderr[-3000:])
            self.assertTrue(elf.is_file(), "The one runtime ELF is missing")
            self.assertEqual(["runtime.elf"], sorted(
                path.name for path in Path(temp).iterdir()))
            self.assertIn("example/minios/logos/logo1.nxtimg", built.stdout)
            self.assertIn("example/minios/shell.nx", built.stdout)
            program = [
                "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57",
                "-m", "512M", "-kernel", str(elf), "-serial", "stdio",
                "-display", "none", "-no-reboot"
            ]
            process = subprocess.Popen(
                program, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, cwd=ROOT)
            actual = bytearray()
            sent_exit = False
            try:
                deadline = time.monotonic() + 18.0
                while time.monotonic() < deadline:
                    ready, _, _ = select.select(
                        [process.stdout], [], [], 0.1)
                    if ready:
                        data = os.read(process.stdout.fileno(), 65536)
                        if not data:
                            break
                        actual.extend(data)
                    if not sent_exit and b"nexs-os:/> " in actual:
                        process.stdin.write(b"exit\n")
                        process.stdin.flush()
                        sent_exit = True
                    if process.poll() is not None:
                        break
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)
            output = bytes(actual).replace(b"\r\n", b"\n")
            self.assertTrue(sent_exit, output[-400:].decode(
                errors="replace"))
            self.assertEqual(0, process.returncode)
            self.assertIn(
                (ROOT / "example/minios/logos/logo1.nxtimg").read_bytes(),
                output, "The original logo must appear byte-for-byte")
            for name in ("logo1", "logo2", "druglords"):
                resource = ROOT / f"example/minios/logos/{name}.nxtimg"
                self.assertIn(
                    f"{name}.nxtimg ({resource.stat().st_size} bytes)".encode(),
                    output)
            self.assertIn(b"NEXS Shell v0.1.8", output)
            self.assertNotIn(b"[NEXS FATAL ERROR]", output)
            self.assertNotIn(b"cannot open 'example/minios/", output)


if __name__ == "__main__":
    unittest.main()
