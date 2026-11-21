#!/usr/bin/env python3
"""Require two real simultaneous OS1-native AOT builds to remain isolated.

The runtime object graph and output parser are shared between invocations;
generated source, intermediate program objects and published ELFs must not be.
This is a host ELF/link qualification, NOT a substitute for guest nxadb.
"""
from __future__ import annotations

import json
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "nxscript"
MARKERS = ("NX_AOT_PARALLEL_ALPHA", "NX_AOT_PARALLEL_BETA")


def build_one(directory: Path, marker: str) -> dict:
    source = directory / (marker + ".nx")
    binary = directory / (marker + ".elf")
    source.write_text(f'out "{marker}"\n', encoding="utf-8")
    result = subprocess.run(
        [str(CLI), "--compile", str(source), "--target", "os1-amd64",
         "-o", str(binary), "--no-dep"],
        cwd=ROOT, text=True, capture_output=True, timeout=180,
        check=False)
    verified = False
    forbidden = next(value for value in MARKERS if value != marker)
    if result.returncode == 0 and binary.is_file():
        objdump = subprocess.run(
            ["x86_64-elf-objdump", "-s", "-j", ".rodata", str(binary)],
            cwd=ROOT, text=True, capture_output=True, timeout=30,
            check=False)
        # ELF data is encoded into hex bytes and not necessarily contiguous
        # in the objdump formatted output; use a byte scan instead.
        raw = binary.read_bytes()
        verified = (objdump.returncode == 0 and
                    marker.encode() in raw and forbidden.encode() not in raw)
    return {
        "marker": marker,
        "passed": verified,
        "compiler_exit": result.returncode,
        "stderr_tail": result.stderr[-900:],
        "output": str(binary),
    }


def main() -> int:
    (ROOT / "build/assurance").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="os1-parallel-aot-", dir=ROOT / "build") as folder:
        temp = Path(folder)
        with ThreadPoolExecutor(max_workers=2) as executor:
            results = list(executor.map(
                lambda marker: build_one(temp, marker), MARKERS))
        report = ROOT / "build/assurance/os1-parallel-aot.json"
        report.write_text(json.dumps(results, indent=2) + "\n")
        ok = all(result["passed"] for result in results)
        print("OS1_PARALLEL_AOT=" + ("PASS" if ok else "FAIL"))
        for item in results:
            print(item["marker"], item["passed"],
                  item["stderr_tail"][:200] if not item["passed"] else "")
        return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
