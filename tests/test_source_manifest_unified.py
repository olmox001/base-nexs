#!/usr/bin/env python3
"""Real hosted runtime admission and bootstrap authority policy tests.

No mocked registry, filesystem, loader or service. The copied executable
test uses the actual hosted source-root resolver and physical source files.
"""
from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = ROOT / "nxscript"


def execute_script(executable: Path, source: str, root: Path):
    script = root / "probe.nx"
    script.write_text(source, encoding="utf-8")
    return subprocess.run(
        [str(executable), str(script)], cwd=root, capture_output=True,
        text=True, timeout=12, check=False
    )


class RealManifestBootstrapTests(unittest.TestCase):
    def test_installed_files_and_registry_are_identical_names(self):
        with tempfile.TemporaryDirectory() as temp:
            result = execute_script(EXECUTABLE, '''
out type(reg_get("/nxlib/fs.nx"))
out type(reg_get("/nxlib/services/fs/init.nx"))
out type(reg_get("/example/test.nx"))
out type(reg_get("/vfs/files/nxlib/fs.nx"))
out type(reg_get("/sys/vfs/files/nxlib/fs.nx"))
out vfs_exists("/nxlib/fs.nx")
''', Path(temp))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ["str", "str", "str", "err", "err", "1"])

    def test_linking_a_source_never_starts_privileged_services(self):
        with tempfile.TemporaryDirectory() as temp:
            result = execute_script(EXECUTABLE, '''
out type(reg_get("/sys/loader/libs/fs"))
out type(reg_get("/sys/loader/libs/stdlib"))
out type(reg_get("/sys/auth/status"))
out type(reg_get("/sys/tty/status"))
out basename("/alpha/beta")
''', Path(temp))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ["int", "int", "err", "err", "beta"])
        self.assertNotIn("Granted cap=", result.stdout)

    def test_explicit_service_activation_still_uses_same_registry(self):
        with tempfile.TemporaryDirectory() as temp:
            result = execute_script(EXECUTABLE, '''
out type(reg_get("/sys/tty/status"))
import "nxlib/services/tty/init.nx"
out reg_get("/sys/tty/status")
''', Path(temp))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("err", result.stdout.splitlines())
        self.assertEqual("ready", result.stdout.splitlines()[-1])

    def test_manifest_traversal_cannot_bootstrap_unlisted_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "nxlib").mkdir()
            (root / "config").mkdir()
            shutil.copy2(EXECUTABLE, root / "nxscript")
            (root / "config/nx_sources.txt").write_text(
                "nxlib/../unauthorized.nx\n", encoding="utf-8")
            (root / "unauthorized.nx").write_text(
                'library "unauthorized"\nout "UNAUTHORIZED_EXECUTED"\n',
                encoding="utf-8")
            result = execute_script(root / "nxscript",
                                    'out "SCRIPT_EXECUTED"\n', root)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SCRIPT_EXECUTED", result.stdout)
        self.assertNotIn("UNAUTHORIZED_EXECUTED", result.stdout)

    def test_empty_and_dot_components_rejected_by_shared_name_contract(self):
        for malicious in ("nxlib/./unlisted.nx", "nxlib//unlisted.nx"):
            with self.subTest(malicious=malicious), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / "nxlib").mkdir()
                (root / "config").mkdir()
                shutil.copy2(EXECUTABLE, root / "nxscript")
                (root / "config/nx_sources.txt").write_text(
                    malicious + "\n", encoding="utf-8")
                (root / "nxlib/unlisted.nx").write_text(
                    'library "unlisted"\nout "UNAUTHORIZED_EXECUTED"\n',
                    encoding="utf-8")
                result = execute_script(root / "nxscript",
                                        'out "SCRIPT_EXECUTED"\n', root)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("SCRIPT_EXECUTED", result.stdout)
                self.assertNotIn("UNAUTHORIZED_EXECUTED", result.stdout)

    def test_missing_installed_manifest_resource_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "config").mkdir()
            shutil.copy2(EXECUTABLE, root / "nxscript")
            (root / "config/nx_sources.txt").write_text(
                "nxlib/nonexistent.nx\n", encoding="utf-8")
            result = execute_script(root / "nxscript",
                                    'out "SCRIPT_EXECUTED"\n', root)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SCRIPT_EXECUTED", result.stdout)


if __name__ == "__main__":
    unittest.main()
