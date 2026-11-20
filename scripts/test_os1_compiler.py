#!/usr/bin/env python3
"""Validate actual OS1 compiler targets and ELF capability ABI link contracts.

A target is successful only after the public nxscript CLI compiles and the
result has correct machine type, SDK symbols and non-executable stack.
Actual OS1 guest execution is independently verified through nxadb.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
SOURCE = "tests/fixtures/baremetal_smoke.nx"
PROFILES = (
    ("amd64", "x86_64-elf-readelf", "x86_64-elf-nm",
     "Advanced Micro Devices X86-64"),
    ("arm64", "aarch64-none-elf-readelf", "aarch64-none-elf-nm",
     "AArch64"),
)
NEEDED = ("OS1_registry_object_get", "OS1_registry_object_set",
          "OS1_report_error", "nexs_platform_register",
          "nexs_embedded_lookup", "main")


def command(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args, cwd=BASE, text=True, capture_output=True, check=False,
        timeout=150)


def main() -> int:
    results: list[dict] = []
    out = BASE / "build/assurance"
    out.mkdir(parents=True, exist_ok=True)
    cli = BASE / "nxscript"
    for arch, readelf, nm, expected_machine in PROFILES:
        target = "os1-" + arch
        binary = out / ("compiler-" + target + ".elf")
        build = command([str(cli), "--compile", SOURCE, "--target",
                         target, "-o", str(binary)])
        issues = []
        if build.returncode != 0 or not binary.is_file():
            issues.append("Compiler failed: " + build.stderr[-600:])
        else:
            header = command([readelf, "-h", "-W", str(binary)])
            program = command([readelf, "-l", "-W", str(binary)])
            symbols = command([nm, "-g", str(binary)])
            if any(proc.returncode != 0
                   for proc in (header, program, symbols)):
                issues.append("ELF inspection failed")
            if expected_machine not in header.stdout:
                issues.append("Wrong architecture")
            stacks = [line for line in program.stdout.splitlines()
                      if "GNU_STACK" in line]
            if len(stacks) != 1 or "RW" not in stacks[0] or (
                    "RWE" in stacks[0]):
                issues.append("Executable/absent or ambiguous ELF stack")
            for symbol in NEEDED:
                if not any(
                    line.split()[-1] == symbol for line in
                    symbols.stdout.splitlines() if line.split()
                ):
                    issues.append("Missing SDK/runtime symbol: " + symbol)
        record = {"target": target, "passed": not issues,
                  "elf": str(binary), "issues": issues,
                  "compiler_tail": build.stdout[-350:]}
        results.append(record)
        print(f"[os1-compiler] {target}: "
              f"{'PASS' if not issues else 'FAIL'} "
              f"{'; '.join(issues[:2])}")
    report = out / "native-os1-compiler.json"
    report.write_text(json.dumps(results, indent=2) + "\n")
    print(f"[os1-compiler] report={report}")
    return 0 if all(item["passed"] for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
