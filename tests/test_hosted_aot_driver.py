"""End-to-end tests for the canonical hosted AOT toolchain launcher.

The build must be safe under concurrent invocations and arbitrary user output
names; only private generated paths are passed into the Make source graph.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "nxscript"


class HostedAOTDriverTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(
            dir=ROOT / "build", prefix="nx-aot-test-")
        self.addCleanup(self.scratch.cleanup)
        self.dir = Path(self.scratch.name)
        self.source = self.dir / "source.nx"
        self.source.write_text("out 6 * 7\n", encoding="utf-8")

    def run_compile(self, target, output, source=None, env=None):
        if source is None:
            source = self.source
        return subprocess.run(
            [str(BIN), "--compile", str(source),
             "--target", target, "-o", str(output), "--no-dep"],
            cwd=ROOT, capture_output=True, text=True,
            env=env, timeout=180)

    def test_hosted_binary_matches_script_output_without_service_autoload(self):
        expected = subprocess.run(
            [str(BIN), str(self.source)],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(expected.returncode, 0, expected.stderr)
        output = self.dir / "program"
        compiled = self.run_compile("macos-amd64", output)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        observed = subprocess.run(
            [str(output)], cwd=ROOT,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(observed.returncode, 0, observed.stderr)
        # The CLI explicitly autoloads host services; a standalone AOT
        # executable uses an isolated image. Match program output, keeping
        # the known service-initialization difference visible and documented.
        bootstrap = ("[tty] ", "[auth] ")
        expected_script_lines = [
            line for line in expected.stdout.splitlines()
            if not line.startswith(bootstrap)]
        self.assertTrue(all(
            line.startswith(bootstrap) for line in expected.stdout.splitlines()
            if line not in expected_script_lines))
        self.assertEqual(observed.stdout.splitlines(), expected_script_lines)
        self.assertEqual(observed.stdout, "42\n")

    def test_installed_nxlib_overrides_embedded_only_when_present(self):
        self.source.write_text(
            'exec("nxlib/stdlib.nx")\n'
            'out "AOT_PRECEDENCE_FINISHED"\n', encoding="utf-8")
        executable = self.dir / "runtime"
        compiled = subprocess.run(
            [str(BIN), "--compile", str(self.source),
             "--target", "macos-amd64", "-o", str(executable)],
            cwd=ROOT, capture_output=True, text=True, timeout=180)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        embedded = subprocess.run(
            [str(executable)], cwd=self.dir,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(embedded.returncode, 0, embedded.stderr)
        self.assertIn("AOT_PRECEDENCE_FINISHED", embedded.stdout)
        self.assertNotIn("INSTALLED_RESOURCE_WINS", embedded.stdout)

        installed = self.dir / "nxlib"
        installed.mkdir()
        library = installed / "stdlib.nx"
        library.write_text(
            'library "stdlib"\n'
            'out "INSTALLED_RESOURCE_WINS"\n',
            encoding="utf-8")
        external = subprocess.run(
            [str(executable)], cwd=self.dir,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(external.returncode, 0, external.stderr)
        self.assertIn("INSTALLED_RESOURCE_WINS", external.stdout)
        self.assertIn("AOT_PRECEDENCE_FINISHED", external.stdout)

        library.unlink()
        restored = subprocess.run(
            [str(executable)], cwd=self.dir,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertNotIn("INSTALLED_RESOURCE_WINS", restored.stdout)
        self.assertIn("AOT_PRECEDENCE_FINISHED", restored.stdout)

    def test_metacharacters_in_output_path_never_execute(self):
        marker = self.dir / "INJECTED"
        # Command substitution in a double-quoted Make variable would create
        # INJECTED in the current working directory. The driver must NEVER
        # pass this output name to Make or any shell.
        dangerous = self.dir / "program;touch INJECTED $(echo unsafe) 'quotes'"
        compiled = self.run_compile("macos-amd64", dangerous)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        self.assertTrue(dangerous.is_file())
        self.assertFalse(marker.exists())
        self.assertFalse((ROOT / "INJECTED").exists())
        execution = subprocess.run(
            [str(dangerous)], cwd=ROOT, capture_output=True,
            text=True, timeout=30)
        self.assertEqual(execution.returncode, 0, execution.stderr)
        self.assertIn("42", execution.stdout)

    def test_missing_source_does_not_replace_existing_binary(self):
        destination = self.dir / "previous_program"
        destination.write_bytes(b"valid-existing-binary-placeholder")
        before = destination.read_bytes()
        missing = self.dir / "missing.nx"
        compiled = self.run_compile("macos-amd64", destination, missing)
        self.assertNotEqual(compiled.returncode, 0)
        self.assertEqual(destination.read_bytes(), before)

    def test_invalid_resource_profile_fails_closed(self):
        destination = self.dir / "must-not-appear"
        variables = dict(os.environ)
        variables["NEXS_POOL_PROFILE"] = "64MB;touch INJECTED"
        compiled = self.run_compile(
            "macos-amd64", destination, env=variables)
        self.assertNotEqual(compiled.returncode, 0)
        self.assertFalse(destination.exists())
        self.assertFalse((ROOT / "INJECTED").exists())

    def test_unsupported_host_toolchain_is_not_simulated(self):
        destination = self.dir / "plan9-output"
        compiled = self.run_compile("plan9-amd64", destination)
        self.assertNotEqual(compiled.returncode, 0)
        self.assertFalse(destination.exists())
        self.assertIn("unavailable target", compiled.stderr)

    def test_parallel_builds_do_not_share_generated_source(self):
        first = self.dir / "first"
        second = self.dir / "second"

        def compile_one(output):
            return self.run_compile("macos-amd64", output)

        with ThreadPoolExecutor(max_workers=2) as threads:
            result_a, result_b = list(
                threads.map(compile_one, [first, second]))
        self.assertEqual(result_a.returncode, 0, result_a.stderr)
        self.assertEqual(result_b.returncode, 0, result_b.stderr)
        for output in (first, second):
            executed = subprocess.run(
                [str(output)], cwd=ROOT,
                capture_output=True, text=True, timeout=30)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertIn("42", executed.stdout)
        # Inspect only the staging directories created by these invocations.
        # Other independent AOT builds may run concurrently in this tree.
        import re
        for compiled in (result_a, result_b):
            generated = re.findall(
                r"build/\.nexs-aot-[A-Za-z0-9]+/program\.bin",
                compiled.stdout)
            self.assertEqual(1, len(generated), compiled.stdout[-800:])
            owned = ROOT / Path(generated[0]).parent
            self.assertFalse(owned.exists(),
                             f"Compiler leaked its staging directory: {owned}")

    def test_interactive_empty_program_remains_interactive(self):
        output = self.dir / "repl"
        fixture = ROOT / "tests/fixtures/repl.nx"
        compiled = self.run_compile("macos-amd64", output, fixture)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        executed = subprocess.run(
            [str(output)], cwd=ROOT, input=":exit\n",
            capture_output=True, text=True, timeout=30)
        self.assertEqual(executed.returncode, 0, executed.stderr)
        self.assertIn("Bye.", executed.stdout)

    def test_single_source_graph_and_host_only_compiler(self):
        catalog = (ROOT / "config/sources.mk").read_text()
        self.assertIn("NEXS_AOT_HOSTED_SRCS", catalog)
        self.assertIn("compiler/compile_unavailable.c", catalog)
        self.assertIn("compiler/driver.c", catalog)
        source = (ROOT / "compiler/driver.c").read_text()
        self.assertNotIn("system(", source)
        self.assertNotIn("popen(", source)
        self.assertNotIn("goto ", source)
        self.assertIn("posix_spawnp(", source)
        self.assertIn("mkdtemp(", source)


if __name__ == "__main__":
    unittest.main()
