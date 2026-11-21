#!/usr/bin/env python3
"""Syntax-only gate for installed nxlib sources plus external example fixtures."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
MANIFEST = BASE / "config/nx_sources.txt"
COMMAND = BASE / "nxscript"


def main() -> int:
    if not COMMAND.is_file() or not MANIFEST.is_file():
        print("[nx-syntax] Missing interpreter or reviewed manifest",
              file=sys.stderr)
        return 2
    installed = [line for line in MANIFEST.read_text().splitlines()
                 if line and not line.startswith("#")]
    # External examples remain independently testable without autoload or
    # admission to the runtime's library/service resource catalog.
    examples = sorted(p.relative_to(BASE).as_posix()
                      for p in (BASE / "example").rglob("*.nx"))
    source_paths = installed + examples
    if not installed or len(source_paths) != len(set(source_paths)):
        print("[nx-syntax] Empty or duplicated source manifest",
              file=sys.stderr)
        return 2
    results = []
    for rel in source_paths:
        path = BASE / rel
        if not path.is_file():
            results.append({"source": rel, "passed": False,
                            "error": "Missing source"})
            continue
        try:
            proc = subprocess.run(
                [str(COMMAND), "--check-syntax", rel], cwd=BASE,
                text=True, capture_output=True, check=False, timeout=8)
            passed = proc.returncode == 0
            results.append({
                "source": rel, "passed": passed, "rc": proc.returncode,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "error": proc.stderr[-512:],
            })
        except subprocess.TimeoutExpired:
            results.append({"source": rel, "passed": False, "rc": 124,
                            "error": "Parser timeout; possible unbounded loop"})
        print(f"[nx-syntax] {'PASS' if results[-1]['passed'] else 'FAIL'} {rel}")
    out = BASE / "build/assurance/nx-source-syntax.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2) + "\n")
    passed = sum(bool(row["passed"]) for row in results)
    print(f"[nx-syntax] {passed}/{len(results)} parsed without execution "
          f"(services remain inactive); report={out}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
