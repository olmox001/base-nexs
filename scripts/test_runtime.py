#!/usr/bin/env python3
"""Verify Base-NEXS interpreter results, never count zero exit as proof alone."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKERS = {
    "example/example_lib.nx": "Esecuzione completata",
    "example/exent_bus.nx": "Indice 1:",
    "example/lib.nx": "Libreria matematica",
    "example/micro_os.nx": "OS Spento",
    "example/minios/logo.nx": "logo1.nxtimg",
    "example/minios/termimg.nx": "Uso: termimg",
    "example/test.nx": "OK",
    "example/test_builtins_all.nx": "TESTS COMPLETED",
    "example/test_input.nx": "Ciao",
    "example/test_parse_loop.nx": "45",
    "example/test_syntax.nx": "33",
}
# Acceptance methods run real OS PTY I/O on the selected interpreter.
# This mapping is only the test harness, never a runtime resource catalog.
INTERACTIVE_PTY = {
    "example/druglords/druglords.nx":
        "test_druglords_quit_at_intro_in_real_pty",
    "example/minios/boot.nx":
        "test_boot_shell_help_pwd_exit_in_real_pty",
    "example/nxed_editor.nx":
        "test_nxed_editor_calibrate_edit_menu_quit_in_real_pty",
}
INTERACTIVE = frozenset(INTERACTIVE_PTY)
# Actual functional probes for import-only modules: exercise an exported
# operation instead of declaring their silent top-level load successful.
# These tests do not claim full interactive workflow or UI acceptance.
FEATURE_FIXTURES = {
    "example/minios/druglords.nx": (
        "tests/fixtures/features/druglords.nx", ("FEATURE_DRUGLORDS_MOD_PASS",)),
    "example/minios/logos_config.nx": (
        "tests/fixtures/features/logos.nx", ("FEATURE_LOGO_CONFIG_PASS",)),
    "example/minios/nxed_editor.nx": (
        "tests/fixtures/features/nxed-clamp.nx", ("FEATURE_NXED_CLAMP_PASS",)),
    "example/minios/shell.nx": (
        "tests/fixtures/features/shell-help.nx",
        ("Comandi standard:", "Arresta il sistema")),
    "example/minios/wasm_vm.nx": (
        "tests/fixtures/features/wasm-vm.nx",
        ("[VM-STDOUT] Risultato cima stack: 42",
         "[VM] Halt incontrato. Arresto.")),
    "example/nxed_editor copy.nx": (
        "tests/fixtures/features/editor-copy-render.nx",
        ("> ABC",)),
}
ERROR = re.compile(r"(?i)(?:\[NEXS FATAL ERROR\]|runtime error:|parse error:)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="nxscript")
    parser.add_argument("--all", action="store_true",
                        help="run all examples; interactive require separate PTY proof")
    args = parser.parse_args()
    binary = (ROOT / args.binary).resolve()
    if not binary.is_file():
        print(f"Missing interpreter: {binary}", file=sys.stderr)
        return 2
    examples = sorted((ROOT / "example").rglob("*.nx"))
    if not examples:
        print("No scripts found", file=sys.stderr)
        return 2
    # Every example must have either a direct assertion, a functional
    # component fixture, or a separately reported interactive acceptance gap.
    covered = set(MARKERS) | set(FEATURE_FIXTURES)
    unknown = covered - {path.relative_to(ROOT).as_posix() for path in examples}
    if unknown or (set(MARKERS) & set(FEATURE_FIXTURES)):
        print(f"Invalid test catalog: {unknown}", file=sys.stderr)
        return 2
    tests = examples if args.all else [ROOT / name for name in sorted(covered)]
    results: list[dict] = []
    for source in tests:
        relative = source.relative_to(ROOT).as_posix()
        fixture, expected = FEATURE_FIXTURES.get(relative, (relative, ()))
        if relative in MARKERS:
            expected = (MARKERS[relative],)
        if args.all and relative in INTERACTIVE:
            # No source is auto-admitted. Prove real interactive behavior
            # independently, using this exact tested interpreter binary.
            env = os.environ.copy()
            env["NEXS_TEST_BINARY"] = str(binary)
            try:
                proc = subprocess.run(
                    [sys.executable, "-m", "unittest",
                     "tests.test_interactive_os_examples.InteractiveOSExamples."
                     + INTERACTIVE_PTY[relative], "-v"],
                    cwd=ROOT, env=env, text=True, capture_output=True,
                    timeout=20, check=False)
                passed = (proc.returncode == 0 and
                          "Ran 1 test" in proc.stderr and
                          "\nOK\n" in proc.stderr)
                results.append({
                    "path": relative, "status": "PASS" if passed else "FAIL_PTY",
                    "execution_mode": "real-pty",
                    "assertion": INTERACTIVE_PTY[relative],
                    "return_code": proc.returncode,
                    "stdout_tail": proc.stdout[-900:],
                    "stderr_tail": proc.stderr[-900:]})
            except subprocess.TimeoutExpired:
                results.append({
                    "path": relative, "status": "FAIL_PTY_TIMEOUT",
                    "execution_mode": "real-pty",
                    "assertion": INTERACTIVE_PTY[relative]})
            print(f"[nxtest] {results[-1]['status']:<23} {relative}")
            continue
        try:
            proc = subprocess.run(
                [str(binary), fixture], cwd=ROOT, stdin=subprocess.DEVNULL,
                capture_output=True, timeout=3, check=False)
            output = proc.stdout.decode("utf-8", "replace")
            stderr = proc.stderr.decode("utf-8", "replace")
            has_error = bool(ERROR.search(output + stderr))
            has_failure = "FEATURE_" in output and "_FAIL" in output
            if proc.returncode != 0 or has_error or has_failure:
                status = "FAIL"
            elif expected and not all(marker in output for marker in expected):
                status = "FAIL"
            elif expected:
                status = "PASS"
            else:
                status = "UNVERIFIED_NO_ASSERTION"
            results.append({"path": relative, "status": status,
                            "execution_mode": "exported-function" if
                                relative in FEATURE_FIXTURES else "direct",
                            "fixture": fixture, "return_code": proc.returncode,
                            "expected_markers": expected,
                            "stdout_tail": output[-900:],
                            "stderr_tail": stderr[-500:]})
        except subprocess.TimeoutExpired as ex:
            status = "UNVERIFIED_INTERACTIVE" if relative in INTERACTIVE else "FAIL_TIMEOUT"
            results.append({"path": relative, "status": status,
                            "return_code": None,
                            "output_bytes": len(ex.stdout or b"")})
        print(f"[nxtest] {results[-1]['status']:<23} {relative}")
    report = ROOT / "build/assurance/runtime-examples.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(results, ensure_ascii=False, indent=2)+"\n")
    counts = {status: sum(r["status"] == status for r in results)
              for status in {r["status"] for r in results}}
    print(f"[nxtest] total={len(results)} counts={counts} report={report}")
    failed = [r for r in results if r["status"].startswith("FAIL")]
    incomplete = [r for r in results
                  if r["status"].startswith("UNVERIFIED")]
    if failed:
        return 1
    # "All examples" cannot be a green build with unasserted outputs or
    # interactive workflows missing a real terminal/guest acceptance test.
    if args.all and incomplete:
        print(f"[nxtest] INCOMPLETE: {len(incomplete)} examples require "
              "explicit runtime/interactive assertions")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
