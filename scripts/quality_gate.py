#!/usr/bin/env python3
"""Base-NEXS Power12-inspired fail-closed build gate, not a compliance proof.

Strict: new OS1 adapter, source ownership, recursive dependency prohibition.
Warning: existing imported implementation debt; never promote to PASS.
Both architectures separately compile every native OS1 source with -Werror.
"""
from __future__ import annotations

import argparse
import configparser
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/assurance"
VALID_EXTS = {".c", ".h", ".S", ".s"}
SOURCE_ROOTS = (
    "core", "registry", "state", "authority", "hardware", "buffer", "block", "journal",
    "lang", "sys", "runtime", "compiler", "hal", "kernel", "fs",
    "extensions", "include",
)
# Every relocated NXOLMO product implementation now blocks new lexical
# safety violations. Historical non-migrated source remains warning debt.
STRICT_ROOTS = ("hal/os1/", "state/")
FORBIDDEN_CALLS = ("assert", "__assert_fail", "_Static_assert", "static_assert",
                   "setjmp", "longjmp", "_setjmp", "_longjmp")
RESERVED_INSTRUCTIONS = ("cli", "hlt", "lidt", "lgdt", "wfi", "msr", "outb", "inb")
REQUIRED_GROUPS = {
    "NEXS_SHARED_SRCS", "NEXS_HOSTED_ADAPTER_SRCS",
    "NEXS_OS1_ADAPTER_SRCS", "NEXS_KERNEL_SRCS",
    "NEXS_BAREMETAL_ARM64_SRCS", "NEXS_BAREMETAL_AMD64_SRCS",
    "NEXS_BAREMETAL_RISCV64_SRCS",
    "NEXS_SEL4_PRIVILEGED_SRCS",
}
ALLOWED_SUBMODULES = {
    "external/os1/nx-sdk": {
        "url": "https://github.com/olmox001/NX-SDK.git",
        "branch": "SPERIMENTAL-LINUX", "update": "none", "active": "false",
    },
    "external/references/nxolmo": {
        "url": "https://github.com/olmox001/NXOLMO.git",
        "branch": "main", "update": "none", "active": "false",
    },
}


def mask_comments_and_strings(source: str) -> str:
    """Keep line positions; ignore non-code for lexical diagnostics."""
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda match: re.sub(r"[^\n]", " ", match.group()), source)


def audit_source(path: str, source: str, strict: bool) -> list[dict]:
    clean = mask_comments_and_strings(source)
    issues: list[dict] = []
    checks = [
        ("no_assert", r"\b(?:" + "|".join(FORBIDDEN_CALLS) + r")\s*\("),
        ("no_goto", r"\bgoto\s+[a-zA-Z_]\w*\s*;"),
        ("no_break", r"\bbreak\s*;"),
        ("bounded_loop", r"\bwhile\s*\(\s*1\s*\)|\bfor\s*\(\s*;\s*;"),
        ("no_arch_ifdef", r"(?m)^\s*#\s*(?:ifdef|elif|if)\b[^\n]*(?:NEXS_|ARCH_|__aarch64__|__x86_64__)"),
    ]
    if strict:
        # Strings are deliberately inspected as well for privileged CPU opcodes.
        asm_strings = re.findall(r'__asm__[^;]*;', source, flags=re.S)
        for part in asm_strings:
            for opcode in RESERVED_INSTRUCTIONS:
                if re.search(r"\b" + opcode + r"\b", part):
                    issues.append({"path": path,
                        "line": source[:source.find(part)].count("\n") + 1,
                        "rule": "no_privileged_userland_asm", "severity": "block"})
    for rule, expression in checks:
        for match in re.finditer(expression, clean):
            issues.append({"path": path,
                "line": clean[:match.start()].count("\n") + 1,
                "rule": rule, "severity": "block" if strict else "warning"})
    return issues


def submodule_issues(links: list[str], data: str | None) -> list[dict]:
    """Allow only reviewed, inactive reference gitlinks and their exact URLs."""
    issues: list[dict] = []

    def fault(rule: str, detail: str) -> None:
        issues.append({"path": ".gitmodules", "line": 0,
                       "rule": rule, "detail": detail, "severity": "block"})

    if len(links) != len(ALLOWED_SUBMODULES) or set(links) != set(ALLOWED_SUBMODULES):
        fault("recursive_submodules", f"Unexpected gitlinks: {links}")
    if data is None:
        fault("gitmodule_missing", ".gitmodules")
        return issues

    config = configparser.ConfigParser(interpolation=None)
    try:
        config.read_string(data)
    except configparser.Error as exc:
        fault("gitmodule_parse", str(exc))
        return issues
    expected = {f'submodule "{path}"' for path in ALLOWED_SUBMODULES}
    if set(config.sections()) != expected:
        fault("submodule_sections", f"Unexpected sections: {config.sections()}")
    for path, attrs in ALLOWED_SUBMODULES.items():
        section = f'submodule "{path}"'
        if not config.has_section(section):
            continue
        actual = dict(config.items(section))
        if actual != {"path": path, **attrs}:
            fault("submodule_config", f"Unexpected config for {path}: {actual}")
    return issues


def catalog_issues() -> list[dict]:
    issues: list[dict] = []
    def fault(rule: str, detail: str) -> None:
        issues.append({"path": "config/sources.mk", "line": 0,
                       "rule": rule, "detail": detail, "severity": "block"})
    catalog = ROOT / "config/sources.mk"
    if not catalog.exists():
        fault("catalog_missing", str(catalog))
        return issues
    raw = catalog.read_text()
    groups = {}
    for match in re.finditer(r"(?m)^(NEXS_[A-Z0-9_]+_SRCS)\s*:=\s*"
                             r"((?:[^\n]*\\\n)*[^\n]*)", raw):
        name = match.group(1)
        tokens = re.findall(r"(?<![\w./-])(?:[\w-]+/)+[\w.-]+\.[cSsHh]",
                            match.group(2))
        groups[name] = tokens
    if REQUIRED_GROUPS - set(groups):
        fault("catalog_groups", "Missing: " +
              ", ".join(sorted(REQUIRED_GROUPS - set(groups))))
    for name, files in groups.items():
        if not files and name != "NEXS_OS1_SRCS":
            fault("catalog_empty_group", name)
        if len(files) != len(set(files)):
            fault("catalog_duplicates", name)
        for src in files:
            if not (ROOT / src).is_file():
                fault("catalog_missing_source", name + ": " + src)
    # No source list should provide two implementations of the same system ABI.
    shared = set(groups.get("NEXS_SHARED_SRCS", []))
    if shared.intersection(groups.get("NEXS_HOSTED_ADAPTER_SRCS", [])):
        fault("catalog_shared_host_collision", "shared and hosted overlap")
    if shared.intersection(groups.get("NEXS_OS1_ADAPTER_SRCS", [])):
        fault("catalog_shared_os1_collision", "shared and os1 overlap")

    git = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "--stage", "-z"],
        capture_output=True, check=False)
    if git.returncode:
        fault("git_index_unavailable", git.stderr.decode(errors="replace"))
        return issues
    links = []
    for entry in git.stdout.split(b"\0"):
        if entry:
            mode, _blob, _stage_name = entry.split(b" ", 2)
            filename = entry.partition(b"\t")[2]
            if mode == b"160000":
                links.append(filename.decode(errors="replace"))
    module_file = ROOT / ".gitmodules"
    module_text = module_file.read_text() if module_file.is_file() else None
    issues.extend(submodule_issues(links, module_text))
    if (ROOT / "reference/.git").exists():
        fault("recursive_reference_worktree", "reference/.git")

    # The NXTIMG container must be byte-preserving Git binary content.
    attributes = ROOT / ".gitattributes"
    if not attributes.is_file() or "*.nxtimg binary" not in attributes.read_text():
        fault("nxtimg_binary_attribute_missing",
              "NXTIMG files must not be rewritten by text normalization")

    # .nx is a first-class proprietary library/service file, not an opaque
    # untracked data asset. The reviewed manifest covers only installed
    # runtime libraries/services. Examples remain on external VFS sources.
    nx_manifest = ROOT / "config/nx_sources.txt"
    if not nx_manifest.is_file():
        fault("nx_script_manifest_missing", str(nx_manifest))
    else:
        registered = [
            entry.strip() for entry in nx_manifest.read_text().splitlines()
            if entry.strip() and not entry.lstrip().startswith("#")
        ]
        on_disk = sorted(
            p.relative_to(ROOT).as_posix()
            for directory in ("nxlib",)
            for p in (ROOT / directory).rglob("*.nx")
        )
        if sorted(set(registered)) != on_disk or len(set(registered)) != len(registered):
            missing = sorted(set(on_disk) - set(registered))
            stale = sorted(set(registered) - set(on_disk))
            fault("nx_script_catalog_mismatch",
                  f"unregistered={missing[:10]} stale={stale[:10]}")

    resource_manifest = ROOT / "config/nx_resources.txt"
    if not resource_manifest.is_file():
        fault("nx_resource_manifest_missing", str(resource_manifest))
    else:
        expected_resources = sorted(
            p.relative_to(ROOT).as_posix()
            for directory in ("nxlib",)
            for p in (ROOT / directory).rglob("*")
            if p.is_file() and p.suffix in (".nx", ".nxtimg")
        )
        actual_resources = [
            entry.strip() for entry in resource_manifest.read_text().splitlines()
            if entry.strip() and not entry.lstrip().startswith("#")
        ]
        if (sorted(actual_resources) != expected_resources or
                len(actual_resources) != len(set(actual_resources))):
            fault("nx_resource_catalog_mismatch",
                  f"expected={len(expected_resources)} actual={len(actual_resources)}")
        if sorted(p for p in actual_resources if p.endswith(".nx")) != sorted(registered):
            fault("nx_resource_source_subset_mismatch",
                  "source and resource manifests disagree")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("default", "strict-all"),
                        default="default")
    parser.add_argument("--report", default="build/assurance/quality.json")
    args = parser.parse_args()
    issues = catalog_issues()
    scanned = 0
    strict_scanned = 0
    for root in SOURCE_ROOTS:
        for file in (ROOT / root).rglob("*"):
            if file.suffix not in VALID_EXTS or not file.is_file():
                continue
            if "/build/" in file.as_posix():
                continue
            relative = file.relative_to(ROOT).as_posix()
            strict = (args.mode == "strict-all" or
                      any(relative.startswith(prefix) for prefix in STRICT_ROOTS))
            issues.extend(audit_source(relative, file.read_text(errors="replace"),
                                       strict))
            scanned += 1
            if strict:
                strict_scanned += 1
    if scanned == 0 or strict_scanned == 0:
        issues.append({"path": ".", "line": 0, "rule": "empty_audit",
                       "severity": "block"})
    blocked = [x for x in issues if x["severity"] == "block"]
    warnings = [x for x in issues if x["severity"] == "warning"]
    report = {
        "profile": "base-nexs-power12-stage1",
        "certified": False,
        "mode": args.mode, "scanned_files": scanned,
        "strict_files": strict_scanned,
        "blocking": blocked, "warnings": warnings,
        "limitations": [
            "Lexical audit is not whole-program AST or model-checker proof",
            "Existing dev-stable sources carry warning debt",
            "Timing, memory and cross-target semantics require runtime evidence",
        ],
    }
    out = ROOT / args.report
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"[power12] scanned={scanned} strict={strict_scanned} "
          f"BLOCK={len(blocked)} WARN={len(warnings)}")
    for item in blocked[:20]:
        print(f"[power12][BLOCK] {item['path']}:{item['line']} "
              f"{item['rule']} {item.get('detail','')}")
    for item in warnings[:8]:
        print(f"[power12][WARN] {item['path']}:{item['line']} {item['rule']}")
    print(f"[power12] full report: {out}")
    return 1 if blocked else 0


if __name__ == "__main__":
    raise SystemExit(main())
