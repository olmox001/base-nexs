#!/usr/bin/env python3
"""Regenerate the reviewed runtime library/service inventory.

Examples are external VFS files, never startup/catalog resources.
Installed libraries and services still use the canonical nxlib source tree.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
roots = (ROOT / "nxlib",)
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
    "# Canonical NEXS installed runtime library/service resources; examples remain external.\n"
    + "\n".join(sorted(sources + assets)) + "\n", encoding="utf-8")
print(f"Cataloged {len(sources)} source files and {len(assets)} binary assets")
