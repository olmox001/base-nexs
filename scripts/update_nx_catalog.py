#!/usr/bin/env python3
"""Regenerate the two views of ONE reviewed NEXS resource inventory.

Sources are a subset of resources. Both are derived from the actual tree;
the full inventory also includes exact binary assets for image staging.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
roots = (ROOT / "example", ROOT / "nxlib")
sources = sorted(
    p.relative_to(ROOT).as_posix() for base in roots
    for p in base.rglob("*.nx") if p.is_file()
)
assets = sorted(
    p.relative_to(ROOT).as_posix() for base in roots
    for p in base.rglob("*.nxtimg") if p.is_file()
)
if not sources or len(set(sources + assets)) != len(sources) + len(assets):
    raise SystemExit("Refusing inconsistent/empty source or asset inventory")
(ROOT / "config/nx_sources.txt").write_text(
    "# Reviewed NEXS source subset; see nx_resources.txt for assets.\n"
    + "\n".join(sources) + "\n", encoding="utf-8")
(ROOT / "config/nx_resources.txt").write_text(
    "# One canonical NEXS resource index: library/service/application and assets.\n"
    + "\n".join(sorted(sources + assets)) + "\n", encoding="utf-8")
print(f"Cataloged {len(sources)} source files and {len(assets)} binary assets")
