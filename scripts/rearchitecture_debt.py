#!/usr/bin/env python3
"""Translate current Base-NEXS and NXOLMO audit blockers into owned work IDs.

This gate proves traceability of known failures, not that they were fixed.
The release still fails until the original strict gates actually pass.
"""
from __future__ import annotations

import csv
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/rearchitecture"
NXOLMO = ROOT / "external/references/nxolmo"
sys.path.insert(0, str(ROOT / "scripts"))
import quality_gate

FIELDS = ("issue_id", "project", "path", "line", "rule", "current_severity",
          "assigned_phase", "owner", "disposition", "test_gate",
          "review_state")
PHASE = {
    "core/": "P1", "lang/": "P2", "registry/": "P3",
    "runtime/": "P2", "sys/": "P4", "compiler/": "P6",
    "hal/": "P7", "kernel/": "P7", "fs/": "P5",
}


def baseline_blockers() -> list[dict]:
    report = ROOT / "build/assurance/rearchitecture-strict-all.json"
    subprocess.run([
        sys.executable, str(ROOT / "scripts/quality_gate.py"),
        "--mode", "strict-all", "--report",
        "build/assurance/rearchitecture-strict-all.json",
    ], cwd=ROOT, capture_output=True, check=False)
    if not report.is_file():
        raise RuntimeError("Strict Base-NEXS report missing")
    data = json.loads(report.read_text())
    if data["mode"] != "strict-all" or data["strict_files"] != data["scanned_files"]:
        raise RuntimeError("Strict Base-NEXS report is not comprehensive")
    return data["blocking"]


def linux_reference_gotos() -> list[dict]:
    items = []
    folder = NXOLMO / "src/filesystem/9p"
    if not folder.is_dir():
        raise RuntimeError("NXOLMO Linux 9P folder not initialized")
    for path in sorted(folder.rglob("*.c")):
        relative = path.relative_to(NXOLMO).as_posix()
        original = path.read_text(encoding="utf-8", errors="replace")
        clean = quality_gate.mask_comments_and_strings(original)
        for match in re.finditer(r"\bgoto\s+[A-Za-z_]\w*\s*;", clean):
            items.append(dict(
                path=relative,
                line=clean[:match.start()].count("\n") + 1,
                rule="no_goto_unadapted_linux_9p"))
    return items


def append(issues: list[dict], project: str, original: dict) -> None:
    path = original.get("path", "")
    rawline = original.get("line", 0)
    line = int(rawline)
    rule = original.get("rule", "")
    phase = ("P5" if project == "NXOLMO" else
             next((value for prefix, value in PHASE.items()
                   if path.startswith(prefix)), "P0"))
    if phase == "P0" and project == "BASE-NEXS":
        owner = "Project build/assurance owner"
    else:
        owner = ("NEXS VFS / NXOLMO migration" if phase == "P5" else
                 "NEXS " + phase + " canonical implementation")
    stable = f"{project}:{path}:{line}:{rule}"
    issues.append({
        "issue_id": "P12-" + hashlib.sha256(stable.encode()).hexdigest()[:14],
        "project": project,
        "path": path, "line": line, "rule": rule,
        "current_severity": "BLOCK",
        "assigned_phase": phase, "owner": owner,
        "disposition": "REPLACE_OR_REFACTOR_WITH_STRICT_CONFORMANCE",
        "test_gate": "AST_CFG_REVIEW_PLUS_TWO_REAL_TESTS_PER_FUNCTION",
        "review_state": "UNRESOLVED_TRACKED_NEVER_WAIVED",
    })


def main() -> int:
    if not (OUT / "FUNCTION_CATALOG.csv").is_file():
        raise RuntimeError("Regenerate function catalog first")
    issues: list[dict] = []
    for item in baseline_blockers():
        append(issues, "BASE-NEXS", item)
    for item in linux_reference_gotos():
        append(issues, "NXOLMO", item)
    if not issues:
        print("No blockers to classify: verify strict scanner independently")
    if len(set(x["issue_id"] for x in issues)) != len(issues):
        # Can happen if multiple scanner rules report identical position/rule;
        # require deduplication by source evidence, not silently drop evidence.
        raise RuntimeError("Duplicate blocker identities")
    issues.sort(key=lambda x: (x["project"], x["path"], x["line"], x["rule"]))
    with (OUT / "POWER12_BASELINE_BLOCKERS.csv").open(
            "w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(issues)
    base_count = sum(x["project"] == "BASE-NEXS" for x in issues)
    nx_count = sum(x["project"] == "NXOLMO" for x in issues)
    report = {
        "status": "BASELINE_ASSIGNED_NOT_REMEDIATED",
        "base_nexs_blockers": base_count,
        "nxolmo_linux_9p_goto_blockers": nx_count,
        "all_issues_assigned": len(issues),
        "fully_power12_compliant": False,
        "baseline_is_not_a_waiver": True,
    }
    (OUT / "POWER12_BASELINE_MANIFEST.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
