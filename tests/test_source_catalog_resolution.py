"""Canonical nxlib catalog: hosted source roots, services and C extension.

Tests execute the actual linked runtime with a CWD outside the product.
They do not infer correctness from existence of a manifest or a zero exit.
"""
from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "nxscript"


class SourceCatalogResolutionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="nxlib-catalog-", dir=ROOT / "build")
        self.addCleanup(self.temporary.cleanup)
        self.cwd = Path(self.temporary.name)
        self.entry = self.cwd / "entry.nx"

    def execute(self, program: str, location: Path | None = None):
        self.entry.write_text(program, encoding="utf-8")
        return subprocess.run(
            [str(BINARY), str(self.entry)],
            cwd=location or self.cwd, capture_output=True,
            text=True, timeout=15)

    def test_installed_library_and_service_source_root_excludes_examples(self):
        catalog = [
            line for line in (ROOT / "config/nx_sources.txt").read_text().splitlines()
            if line and not line.startswith("#")
        ]
        on_disk = sorted(
            file.relative_to(ROOT).as_posix()
            for folder in ("nxlib",)
            for file in (ROOT / folder).rglob("*.nx")
        )
        self.assertEqual(on_disk, sorted(catalog))
        self.assertEqual(len(catalog), len(set(catalog)))
        self.assertIn("nxlib/stdlib.nx", catalog)
        self.assertIn("nxlib/textbuf.nx", catalog)
        self.assertIn("nxlib/services/fs/init.nx", catalog)
        self.assertFalse(list((ROOT / "services").rglob("*.nx")))
        self.assertFalse((ROOT / "modules/stdlib.nx").exists())

    def test_external_cwd_resolves_canonical_service_exports(self):
        proc = self.execute(
            'exec("nxlib/services/fs/init.nx")\n'
            'out type(reg_get("/fn/vfs_import"))\n'
            'out "EXTERNAL_CWD_FS_SERVICE_OK"\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(["fn", "EXTERNAL_CWD_FS_SERVICE_OK"],
                         proc.stdout.splitlines()[-2:])
        self.assertNotIn("cannot open", proc.stderr)

    def test_external_cwd_resolves_canonical_textbuffer_extension(self):
        proc = self.execute(
            'exec("nxlib/textbuf.nx")\n'
            'a=tb_create()\n'
            'out tb_load(a,"gamma\\ndelta")\n'
            'out tb_get_line(a,1)\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(["2", "delta"], proc.stdout.splitlines()[-2:])

    def test_autoload_from_external_cwd_uses_installed_catalog(self):
        proc = self.execute(
            'out type(reg_get("/fn/vfs_import"))\n'
            'out type(reg_get("/fn/term_size"))\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(["fn", "fn"], proc.stdout.splitlines()[-2:])

    def test_shadow_nxlib_in_foreign_cwd_cannot_hijack_source(self):
        forged = self.cwd / "nxlib"
        forged.mkdir()
        (forged / "stdlib.nx").write_text(
            'out "CWD_CATALOG_HIJACKED"\n'
            'library "stdlib"\n', encoding="utf-8")
        proc = self.execute(
            'exec("nxlib/stdlib.nx")\n'
            'out type(reg_get("/fn/term_size"))\n'
            'out "CATALOG_ROOT_IDENTITY_OK"\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("CWD_CATALOG_HIJACKED", proc.stdout)
        self.assertEqual(["fn", "CATALOG_ROOT_IDENTITY_OK"],
                         proc.stdout.splitlines()[-2:])

    def test_source_larger_than_legacy_buddy_pool_executes_without_fatal_oom(self):
        # Exercise the ACTUAL interpreter's file reader with an input bigger
        # than the default 64 MiB Buddy arena. Stream the comment to avoid
        # allocating the large fixture in Python memory.
        large_source = self.cwd / "large_source.nx"
        with large_source.open("wb") as output:
            output.write(b"#")
            for _ in range(65):
                output.write(b" " * (1024 * 1024))
            output.write(b'\nout "RECOVERABLE_SOURCE_ALLOC_OK"\n')
        proc = subprocess.run(
            [str(BINARY), str(large_source)], cwd=self.cwd,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual("RECOVERABLE_SOURCE_ALLOC_OK",
                         proc.stdout.splitlines()[-1])
        self.assertNotIn("NEXS FATAL", proc.stderr)

    def test_nonseekable_source_returns_error_without_killing_runtime(self):
        # The real hosted loader must reject a FIFO before allocating and
        # keep the caller running; the process must not interpret partial IO.
        fifo = self.cwd / "nonseekable_source.fifo"
        os.mkfifo(fifo)
        handle = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
        try:
            proc = self.execute(
                f'out exec("{fifo}")\n'
                'out "NONSEEKABLE_SOURCE_REJECTED"\n')
        finally:
            os.close(handle)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("cannot seek source file", proc.stderr)
        self.assertEqual(["-1", "NONSEEKABLE_SOURCE_REJECTED"],
                         proc.stdout.splitlines()[-2:])

    def test_source_path_rejects_parent_directory_escape(self):
        proc = self.execute(
            'out exec("nxlib/../../../../../etc/passwd")\n'
            'out "DENIED_WITHOUT_EXECUTION"\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("invalid or oversized source path", proc.stderr)
        self.assertEqual(["-1", "DENIED_WITHOUT_EXECUTION"],
                         proc.stdout.splitlines()[-2:])

    def test_aot_dep_scanner_resolves_transitive_nxlib_sources_from_outside(self):
        proc = subprocess.run(
            [str(BINARY), "--compile", str(ROOT / "example/minios/boot.nx"),
             "--dep-only"], cwd=self.cwd,
            capture_output=True, text=True, timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("[UNREADABLE]", proc.stdout)
        self.assertIn("nxlib/services/fs/init.nx", proc.stdout)
        self.assertIn("nxlib/services/ui/img.nx", proc.stdout)
        self.assertIn("nxlib/textbuf.nx", proc.stdout)
        self.assertIn("example/minios/logos/logo1.nxtimg", proc.stdout)

    def test_sys_bundled_matches_every_reviewed_source_and_asset(self):
        lines = [
            row for row in (ROOT / "config/nx_resources.txt").read_text().splitlines()
            if row and not row.startswith("#")
        ]
        proc = self.execute('out sys_bundled()\n')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        output = proc.stdout.strip().splitlines()[-1]
        self.assertTrue(output.startswith("[") and output.endswith("]"))
        observed = output[1:-1].split(", ")
        self.assertEqual(sorted(lines), sorted(observed))
        self.assertEqual(14, len(observed))
        self.assertTrue(all(name.startswith("nxlib/") for name in observed))
        self.assertIn("nxlib/services/fs/init.nx", observed)
        self.assertNotIn("services", observed)

    def test_generated_aot_resource_catalog_has_single_registry_accessor(self):
        generated = self.cwd / "standalone.c"
        proc = subprocess.run(
            [str(BINARY), "--codegen", str(ROOT / "example/minios/boot.nx"),
             "--hosted", "-o", str(generated)],
            cwd=self.cwd, capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("UNREADABLE", proc.stdout)
        code = generated.read_text()
        for name in ("nexs_embedded_resource_count",
                     "nexs_embedded_resource_at"):
            self.assertEqual(1, code.count(name + "("))
        self.assertIn("nxlib/services/fs/init.nx", code)
        # Keep the original complete program resource graph, not just nxlib.
        self.assertIn('example/minios/shell.nx', code)
        self.assertIn('example/minios/logo.nx', code)
        self.assertIn('example/minios/logos/logo1.nxtimg', code)
        self.assertIn('example/minios/logos/logo2.nxtimg', code)
        self.assertIn('example/minios/logos/druglords.nxtimg', code)
        self.assertNotIn("nexs_dep_0_src[] =\n\"example", code)
        self.assertNotIn('{"services/', code)

    def test_native_extension_owned_by_selected_runtime_image(self):
        shared = (ROOT / "config/sources.mk").read_text()
        native = (ROOT / "extensions/textbuf/textbuf.c").read_text()
        core = (ROOT / "lang/builtins.c").read_text()
        self.assertIn("extensions/textbuf/textbuf.c", shared)
        self.assertNotIn("static Value builtin_tb_", core)
        self.assertEqual(9, native.count("static Value builtin_tb_"))
        self.assertEqual(9, native.count("fn_register_builtin_sig("))
        self.assertIn("nexs_textbuf_register();",
                      (ROOT / "runtime/runtime.c").read_text())


if __name__ == "__main__":
    unittest.main()
