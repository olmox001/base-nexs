#!/usr/bin/env python3
"""Audit every C/H/assembly file in all three implementation trees.

Emits an auditable, SHA-pinned per-source graph and explicitly preserves
unresolved interproject dependencies. Does NOT claim manual semantic review,
linkage, test coverage, or a port that was not linked and exercised.
"""
from __future__ import annotations
import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "external/references/nxolmo"
NXEC = REFERENCE / "external/references/nxec"
DEST = ROOT / "docs/rearchitecture/REFERENCE_IMPLEMENTATION_DEPENDENCIES.csv"
KINDS = {".c", ".h", ".S", ".s", ".inc"}
INCLUDES = re.compile(r'^\s*#\s*include\s*["<]([^">]+)')
# Function candidates are not proof of a definition: full AST lives in
# CLANG_AST_CALLS.csv and must be cross-referenced in P0.
DECL = re.compile(r"^\s*(?:static\s+)?(?:inline\s+)?(?:[A-Za-z_]\w*[\s*]+){1,8}([A-Za-z_]\w*)\s*\(")

def files_under(directory: Path):
    return sorted(p for p in directory.rglob("*")
                  if p.is_file() and p.suffix in KINDS)

def assignment(path: Path, origin: str) -> str:
    rel = path.relative_to(ROOT).as_posix()
    if origin == "BASE-NEXS":
        return "PRODUCT"
    if origin == "NXEC":
        return "REFERENCE_ONLY"
    if "/src/imports/" in rel or "/external/" in rel.replace("external/references/nxolmo/", "", 1):
        return "IMPORTED_REFERENCE"
    if "/src/base/" in rel:
        return "ABI_REFERENCE_PORTS_EXIST"
    if "/src/crypto/" in rel:
        return "CRYPTO_PORTED_AND_TESTED"
    return "PENDING_SOURCE_SEMANTICS_AND_INTEGRATION"

def main() -> None:
    product = [p for p in files_under(ROOT) if
               "external" not in p.relative_to(ROOT).parts
               and "build" not in p.relative_to(ROOT).parts
               and ".git" not in p.relative_to(ROOT).parts]
    reference = files_under(REFERENCE / "src") + files_under(REFERENCE / "include")
    nxec = files_under(NXEC / "preboot") + files_under(NXEC / "kernel")
    rows = []
    seen = set()
    for origin, sources in (("BASE-NEXS", product),
                            ("NXOLMO", reference), ("NXEC", nxec)):
        for path in sources:
            if path in seen:
                raise RuntimeError(f"duplicate source record: {path}")
            seen.add(path)
            raw = path.read_bytes()
            text = raw.decode("utf-8", errors="replace")
            lines = text.splitlines()
            includes = sorted(set(INCLUDES.match(l).group(1)
                                  for l in lines if INCLUDES.match(l)))
            functions = sorted(set(DECL.match(l).group(1)
                                   for l in lines if DECL.match(l)))
            rows.append({
                "origin": origin,
                "source": path.relative_to(ROOT).as_posix(),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "lines_read": len(lines),
                "include_dependencies": ";".join(includes),
                "lexical_function_candidates": ";".join(functions),
                "role": path.parts[-2] if len(path.parts)>1 else "",
                "disposition": assignment(path, origin),
                "semantic_review": "PENDING_AST_CFG_LINKER_REVIEW",
            })
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with DEST.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    counts = {name: sum(row["origin"] == name for row in rows)
              for name in ("BASE-NEXS","NXOLMO","NXEC")}
    print("COMPLETE_SOURCE_READ_SHA_INDEX", counts, "TOTAL", len(rows),
          "EXPLICIT_UNREVIEWED_SEMANTICS", len(rows))

if __name__ == "__main__":
    main()
