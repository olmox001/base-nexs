#!/usr/bin/env python3
"""Fail-closed source/phase cross-check for all three canonical input trees.

Not a claim of function-by-function semantic review, capability correctness
or imported source completion. No source outside the Base product is edited.
"""
from __future__ import annotations

import csv
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOS = {
    "BASE-NEXS": ROOT,
    "NXOLMO-REFERENCE": ROOT / "external/references/nxolmo",
    "NXEC-REFERENCE": ROOT / "external/references/nxolmo/external/references/nxec",
}
OUT = ROOT / "docs/rearchitecture"


def git_info(root: Path):
    command = ["git", "-C", str(root), "ls-files", "-z"]
    if root == ROOT:
        command.extend(("--cached", "--others", "--exclude-standard"))
    command.extend(("--", "*.c", "*.h", "*.S", "*.s"))
    tracked = subprocess.run(
        command, capture_output=True, check=True).stdout.decode().split("\0")
    revision = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=True).stdout.strip()
    return sorted(x for x in tracked if x), revision


def classify(repository: str, path: str):
    if repository == "NXOLMO-REFERENCE":
        if path.startswith("include/nxolmo/"):
            phase, _, owner = classify(repository, "src/" + path[len("include/nxolmo/"):])
            return phase, "REFERENCE_PUBLIC_ABI_NOT_PORTED", owner
        if path.startswith(("tests/", "examples/", "example/", "tools/")):
            return "P0-P8", "REFERENCE_TEST_EVIDENCE", "Assurance and vectors"
        if path.startswith("src/imports/"):
            return ("P0/P5", "IMPORTED_SNAPSHOT_NOT_PRODUCT",
                    "Unadapted Base-NEXS/NexsOS1 reference snapshot")
        for prefix, phase, owner in (
            ("src/base/", "P1", "Core checked ABI"),
            ("src/crypto/", "P1/P7", "Canonical cryptography and HCD integrity"),
            ("src/runtime/", "P2", "Operation budgets, clock and metrics"),
            ("src/buffer/", "P2/P4", "One buffer lease and MPMC transport"),
            ("src/registry/", "P3", "One StateStore and authenticated NXREG"),
            ("src/authority/", "P3", "Capability issuer and verifier"),
            ("src/hardware/", "P3/P7", "Device/DMA capability provider"),
            ("src/journal/", "P5", "Durable WAL"),
            ("src/block/", "P5", "Block/flush backend"),
            ("src/protocol/", "P5", "Native 9P codec"),
            ("src/filesystem/9p/", "P5", "Unadapted Linux 9P reference"),
            ("src/filesystem/ext4/", "P5", "Full ext4 provider"),
            ("src/filesystem/", "P5", "Filesystem provider contracts"),
        ):
            if path.startswith(prefix):
                return phase, ("LINUX_9P_REFERENCE_NEEDS_LICENSED_PORT"
                               if prefix == "src/filesystem/9p/"
                               else "UPSTREAM_IMPLEMENTATION_NOT_OWNED"), owner
    if repository == "NXEC-REFERENCE":
        if "/tests/" in path or path.startswith(("tools/", "tests/")):
            return "P0/P7", "REFERENCE_TEST_EVIDENCE", "Boot and kernel fixture evidence"
        if path.startswith(("preboot/", "boot/", "kernel/")):
            owner = ("Independent kernel HCD admission" if path.startswith("kernel/")
                     else "NXEC boot/HCD/ISA platform provider")
            return "P7", "UPSTREAM_IMPLEMENTATION_NOT_OWNED", owner
    if repository == "BASE-NEXS":
        if path.startswith("include/"):
            return "P1/P2/P4", "CURRENT_PRODUCT_REQUIRES_FULL_EXIT_GATE", "Public ABI"
        if path.startswith(("tests/", "scripts/", "kernel/test/")):
            return "P0-P8", "PRODUCT_TEST_OR_TOOL", "Product assurance"
        for prefix, phase, owner in (
            ("core/", "P1/P2", "Memory, page and Value source"),
            ("authority/", "P3", "Canonical NXOLMO capability authority"),
            ("hardware/", "P3/P7", "NXOLMO hardware resource contracts"),
            ("buffer/", "P2/P4", "NXOLMO buffer leases and MPMC plane"),
            ("block/", "P5", "NXOLMO block request/completion backend"),
            ("journal/", "P5", "NXOLMO write-ahead transaction journal"),
            ("registry/reg_ipc", "P4", "One REG transport and queue"),
            ("state/registry/", "P3", "Canonical NXOLMO StateStore + NXREG"),
            ("state/authority/", "P3", "NXOLMO signed capability and hardware authority"),
            ("state/memory/", "P2/P4", "NXOLMO buffer leases and MPMC"),
            ("state/storage/", "P5", "NXOLMO block WAL and 9P framing"),
            ("registry/", "P3", "One canonical registry"),
            ("lang/", "P2/P6", "One VM and language IR"),
            ("runtime/", "P2", "Runtime instances and execution"),
            ("sys/", "P4", "Execution/FD/TTY services"),
            ("extensions/", "P2/P6", "Native C userland modules"),
            ("compiler/", "P1/P6", "One compiler and AOT"),
            ("hal/", "P7", "ISA/device providers"),
            ("kernel/", "P7", "Kernel object/authority"),
            ("fs/", "P5", "One VFS and 9P"),
        ):
            if path.startswith(prefix):
                return phase, "CURRENT_PRODUCT_REQUIRES_FULL_EXIT_GATE", owner
    return "UNASSIGNED", "MANUAL_REVIEW_REQUIRED", "Unmapped tracked source"


def build_rows():
    rows = []
    revisions = {}
    for repository, path in REPOS.items():
        sources, revision = git_info(path)
        revisions[repository] = revision
        for source in sources:
            if repository == "BASE-NEXS" and source.startswith("external/"):
                continue
            full = path / source
            if not full.is_file():
                raise RuntimeError(f"missing tracked implementation: {repository}/{source}")
            phase, status, owner = classify(repository, source)
            rows.append({
                "repository": repository, "revision": revision,
                "source": source, "sha256": hashlib.sha256(full.read_bytes()).hexdigest(),
                "phase": phase, "classification": status,
                "intended_owner": owner,
                "proof": "REFERENCE_ONLY" if repository != "BASE-NEXS"
                else "PER_FUNCTION_VERIFICATION_REQUIRED",
            })
    if any(row["phase"] == "UNASSIGNED" for row in rows):
        unmapped = [f"{row['repository']}:{row['source']}"
                    for row in rows if row["phase"] == "UNASSIGNED"]
        raise RuntimeError(f"unclassified source files: {unmapped[:20]}")
    return sorted(rows, key=lambda x: (x["repository"], x["source"])), revisions


def main():
    rows, revisions = build_rows()
    OUT.mkdir(parents=True, exist_ok=True)
    csv_path = OUT / "P4_SOURCE_CROSSCHECK.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=list(rows[0]),
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    report = {
        "status": "SOURCE_CROSSCHECK_ONLY_NO_PORT_OR_PROOF_CLAIMS",
        "revisions": revisions,
        # Keep the historical count key for existing consumers. Its scope
        # now ALSO includes Base-NEXS untracked worktree source files.
        "tracked_files": len(rows),
        "source_scope": "BASE_PRODUCT_INDEX_PLUS_UNTRACKED_WORKTREE_AND_PINNED_REFERENCES",
        "by_repository": dict(Counter(x["repository"] for x in rows)),
        "classifications": dict(Counter(x["classification"] for x in rows)),
        "unclassified": 0,
        "non_product_reference_trees_mutated": False,
    }
    (OUT / "P4_SOURCE_CROSSCHECK_MANIFEST.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
