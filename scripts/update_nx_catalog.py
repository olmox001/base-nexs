#!/usr/bin/env python3
"""Rebuild the reviewed .nx source catalog after adding or removing scripts."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
paths = sorted(p.relative_to(ROOT).as_posix()
               for module in ("example", "modules", "services")
               for p in (ROOT / module).rglob("*.nx"))
if not paths:
    raise SystemExit("Refusing to write an empty NEXS source catalog")
dest = ROOT / "config/nx_sources.txt"
dest.write_text(
    "# Reviewed NEXS language source catalog: services, libraries, examples.\n"
    + "\n".join(paths) + "\n")
print(f"Cataloged {len(paths)} NEXS scripts: {dest}")
