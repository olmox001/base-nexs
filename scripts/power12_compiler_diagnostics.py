#!/usr/bin/env python3
"""Fail-closed strict compiler audit for actual hosted Base-NEXS sources.

This complements, but does not replace, Power12 AST/CFG and hardware evidence.
"""
from __future__ import annotations

import json
import os
import platform
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_LIST = ROOT / "config/sources.mk"
OUTPUT = ROOT / "build/assurance/power12_compiler_diagnostics.json"
INCLUDE_DIRS = (
    "core/include", "registry/include", "state/registry/include", "state/authority/include",
    "state/authority/hardware/include", "state/memory/buffer/include", "state/storage/block/include",
    "state/storage/journal/include", "fs/include", "state/storage/9p/include", "lang/include",
    "sys/include", "runtime/include", "compiler/include", "hal/include",
)
FLAGS = (
    "-std=c11", "-O2", "-fsyntax-only", "-Wall", "-Wextra", "-Werror",
    "-Wpedantic", "-Wconversion", "-Wsign-conversion", "-Wfloat-conversion",
    "-Wformat=2", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
    "-Wcast-align", "-Wundef", "-Wpointer-arith",
)


def canonical_sources() -> list[str]:
    content = SOURCE_LIST.read_text()
    groups: dict[str, list[str]] = {}
    for match in re.finditer(
        r"(?m)^(NEXS_[A-Z0-9_]+_SRCS)\s*:=\s*((?:[^\n]*\\\n)*[^\n]*)",
        content,
    ):
        groups[match.group(1)] = re.findall(
            r"[\w.-]+(?:/[\w.-]+)+\.c", match.group(2),
        )
    selected = groups.get("NEXS_SHARED_SRCS", []) + groups.get(
        "NEXS_HOSTED_ADAPTER_SRCS", [],
    )
    if platform.machine().lower() in ("x86_64", "amd64"):
        selected.append("runtime/cycle_counter_hosted_amd64.c")
    if not selected or len(selected) != len(set(selected)):
        raise RuntimeError("Missing or duplicate canonical hosted sources")
    for source in selected:
        if not (ROOT / source).is_file():
            raise RuntimeError(f"Missing canonical source {source}")
    return selected


def main() -> int:
    sources = canonical_sources()
    compiler = os.environ.get("NEXS_STRICT_CC", "gcc")
    target = "MACOS" if platform.system() == "Darwin" else "LINUX"
    base = [
        compiler, *FLAGS, "-DPOOL_64MB", "-DNEXS_HOST_TOOL",
        f"-DHOST_OS_{target}",
        *(f"-I{directory}" for directory in INCLUDE_DIRS),
    ]
    failures = []
    details = []
    for source in sources:
        process = subprocess.run(
            [*base, source], cwd=ROOT, capture_output=True,
            text=True, timeout=90, check=False,
        )
        diagnostics = (process.stdout + process.stderr).strip()
        if process.returncode or diagnostics:
            details.append({
                "source": source, "exit_code": process.returncode,
                "diagnostics": diagnostics[:16000],
            })
            failures.append(source)
    result = {
        "profile": "Power12-P10-C11-strict-conversion",
        "qualification": "BLOCKED" if failures else "COMPILER_DIAGNOSTICS_PASS_ONLY",
        "not_claimed": [
            "No functional correctness or full Power12 proof",
            "No OS1 architecture runtime certification",
            "No whole-program lifetime, data-race or CFG proof",
        ],
        "flags": FLAGS,
        "source_count": len(sources),
        "failed_source_count": len(failures),
        "failed_sources": failures,
        "diagnostics": details,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(result, indent=2) + "\n")
    print(f"POWER12_COMPILER {result['qualification']} "
          f"source_count={len(sources)} failed_sources={len(failures)}")
    for source in failures[:14]:
        print(f"  BLOCK {source}")
    print(f"Report: {OUTPUT}")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
