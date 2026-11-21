#!/usr/bin/env python3
"""Stage the reviewed NEXS libraries AND external VFS example files.

Only nxlib is a startup source catalog. Example programs/assets are installed
as ordinary filesystem files under /home/nxscript/examples, never admitted
into the startup registry or auto-executed. No kernel/SDK modification.
An existing staging directory is never overwritten or deleted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "config/nx_resources.txt"


def reviewed_sources():
    names = [
        line.strip() for line in CATALOG.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    actual = sorted(
        file.relative_to(ROOT).as_posix()
        for directory in ("nxlib",)
        for file in (ROOT / directory).rglob("*")
        if file.is_file() and file.suffix in (".nx", ".nxtimg")
    )
    sources = [
        item for item in names if item.endswith(".nx")
    ]
    reviewed = [
        line.strip() for line in
        (ROOT / "config/nx_sources.txt").read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if (sorted(names) != actual or sorted(sources) != sorted(reviewed) or
            len(names) != len(set(names))):
        raise ValueError("Refuse to stage an incomplete or duplicated catalog")
    return names


def external_examples() -> list[str]:
    tree = ROOT / "example"
    # Every deployed example is an ordinary VFS file, not a runtime source.
    # Check *all* entries, not only suffix-filtered files, to reject hidden
    # symlinks, unexpected binaries and new unreviewed assets.
    entries = sorted(tree.rglob("*"))
    if not entries or any(entry.is_symlink() for entry in entries):
        raise ValueError("Missing examples or unsafe symbolic link")
    files = [entry for entry in entries if entry.is_file()]
    if not files or any(file.suffix not in (".nx", ".nxtimg")
                        for file in files):
        raise ValueError("Unreviewed external example file type")
    return [file.relative_to(ROOT).as_posix() for file in files]


def image_path(logical: str) -> Path:
    if logical.startswith("nxlib/"):
        return Path("lib/nxscript") / logical
    if logical.startswith("example/"):
        return Path("home/nxscript/examples") / logical.removeprefix("example/")
    if logical in ("config/nx_sources.txt", "config/nx_resources.txt"):
        return Path("lib/nxscript/config") / Path(logical).name
    raise ValueError(f"Uncatalogued image mapping: {logical}")


def stage(destination: Path) -> int:
    destination = destination.resolve()
    if destination == ROOT or destination in ROOT.parents:
        raise ValueError("Refuse to use a source or ancestor as image root")
    if destination.exists():
        raise ValueError("Refuse to overwrite an existing staging directory")
    # Installation and startup are separate contracts. The image contains
    # external examples but the two runtime catalogs contain nxlib ONLY.
    names = reviewed_sources()
    names += ["config/nx_sources.txt", "config/nx_resources.txt"]
    names += external_examples()
    manifest = []
    # Complete validation before writing the first destination object.
    for name in names:
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Invalid catalog path: {name}")
        source = ROOT / relative
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"Untrusted or missing catalog source: {name}")
        target = image_path(name)
        if target.is_absolute() or ".." in target.parts:
            raise ValueError(f"Invalid image destination: {target}")
        manifest.append((source, target, name))
    destination.mkdir(parents=True)
    records = []
    for source, target, logical in manifest:
        output = destination / target
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output, follow_symlinks=False)
        source_bytes = source.read_bytes()
        copied = output.read_bytes()
        if source_bytes != copied:
            raise OSError(f"Byte mismatch after staging: {logical}")
        records.append({
            "logical": logical,
            "installed": "/" + target.as_posix(),
            "source_class": ("external-vfs" if logical.startswith("example/")
                             else "installed-runtime"),
            "bytes": len(source_bytes),
            "sha256": hashlib.sha256(source_bytes).hexdigest(),
        })
    report = destination / "lib/nxscript/catalog/staged_files.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    return len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="New empty image root; never overwrites files")
    arguments = parser.parse_args()
    try:
        count = stage(arguments.root)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"nxlib-stage: {exc}\n")
    print(f"NXLIB_IMAGE_STAGE_PASS files={count} root={arguments.root}")


if __name__ == "__main__":
    main()
