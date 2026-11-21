#!/usr/bin/env python3
"""Compiler AST evidence for Base-NEXS, NXOLMO and NXEC (C product files).

This program does not pretend to resolve indirect calls or certify Power12.
It executes Clang against target-specific build profiles, records *every*
source's compile outcome, and emits checked compiler-based function and
call graphs. Never silently turn unsupported/failed source into PASS.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
NXOLMO = BASE / "external/references/nxolmo"
NXEC = NXOLMO / "external/references/nxec"
OUT = BASE / "docs/rearchitecture"
HEADER = ("project", "profile", "path", "symbol", "line", "end_line",
          "direct_call_names", "indirect_call_count", "control_flow_nodes",
          "variable_globals", "body_status")
CALL_HEADER = ("project", "profile", "caller_path", "caller",
               "callee", "direct_kind", "resolution", "target_candidates",
               "receiver_symbol", "receiver_decl_kind", "receiver_abi_type",
               "call_offset")
TU_HEADER = ("project", "profile", "path", "result", "functions",
             "direct_calls", "indirect_calls", "stderr_excerpt")
COVERAGE_HEADER = ("function_id", "project", "role", "path", "symbol",
                   "line", "compiler_coverage", "reason", "source_status")


def source_files(root: Path, folders: tuple[str, ...]) -> list[str]:
    if not (root / ".git").exists():
        raise RuntimeError(f"Source is not initialized: {root}")
    command = ["git", "-C", str(root), "ls-files", "-z"]
    if root == BASE:
        command.extend(("--cached", "--others", "--exclude-standard"))
    result = subprocess.run(command, check=True, capture_output=True)
    names = {x.decode("utf-8", "surrogateescape")
             for x in result.stdout.split(b"\0") if x}
    return [p for p in sorted(names) if p.endswith(".c") and
            p.startswith(folders) and (root / p).is_file() and
            not (root == BASE and p.startswith("kernel/test/"))]


def choose_profile(project: str, path: str) -> tuple[str, list[str]] | None:
    if project == "BASE-NEXS":
        common = [
            "-std=c11", "-DPOOL_64MB", "-Icore/include",
            "-Iregistry/include", "-Istate/registry/include", "-Istate/authority/include",
            "-Istate/authority/hardware/include", "-Istate/memory/buffer/include", "-Istate/storage/block/include",
            "-Istate/storage/journal/include",
            "-Ifs/include", "-Istate/storage/9p/include",
            "-Ilang/include", "-Isys/include", "-Iruntime/include",
            "-Icompiler/include", "-Ihal/include",
        ]
        if path == "compiler/compile_unavailable.c":
            return ("freestanding-amd64-c11", [
                "-target", "x86_64-elf", "-ffreestanding",
                "-DNEXS_BAREMETAL", "-Ikernel/include", *common,
            ])
        if path.startswith("hal/os1/"):
            target = ("aarch64-none-elf" if path.startswith("hal/os1/arm64/")
                      else "x86_64-elf")
            arch = ("-DARCH_AARCH64" if target.startswith("aarch64")
                    else "-DARCH_AMD64")
            sdk = BASE.parents[2] / "system"
            return ("os1-" + target, [
                "-target", target, "-ffreestanding",
                "-DNEXS_HOST_TOOL", "-DNEXS_OS1", arch,
                "-I" + str(sdk / "include/api"),
                "-I" + str(sdk / "include/contract"),
                "-I" + str(sdk / "lib/extensions/USERKIT/nxntfy_srv/include"),
                *common,
            ])
        if path.startswith("hal/sel4/"):
            return None  # Microkit SDK unavailable in current checkout
        for prefix, target in (
            ("hal/amd64/", "x86_64-elf"),
            ("hal/arm64/", "aarch64-none-elf"),
            ("hal/riscv64/", "riscv64-none-elf"),
            ("kernel/", "x86_64-elf"),
            ("fs/", "x86_64-elf"),
        ):
            if path.startswith(prefix):
                return ("freestanding-" + target, [
                    "-target", target, "-ffreestanding", "-DNEXS_BAREMETAL",
                    "-Ikernel/include", *common,
                ])
        if path.startswith(("hal/", "core/", "registry/", "state/", "lang/",
                            "sys/", "runtime/", "compiler/", "extensions/",
                            "authority/", "hardware/", "buffer/", "block/", "journal/")):
            return ("host-darwin-c11", [
                "-DNEXS_HOST_TOOL", "-DHOST_OS_MACOS", *common,
            ])
        if path.startswith("fs/"):
            return None
    if project == "NXOLMO":
        if path.startswith(("src/filesystem/9p/", "src/imports/")):
            return None
        if path.startswith("src/"):
            return ("host-darwin-c11", ["-std=c11", "-Iinclude"])
    if project == "NXEC":
        if path.startswith("preboot/hal/arm64/"):
            return ("freestanding-aarch64-c17", [
                "-target", "aarch64-none-elf", "-std=c17", "-ffreestanding",
                "-I.", "-Ipreboot",
            ])
        if path.startswith("preboot/hal/amd64/"):
            return ("freestanding-amd64-c17", [
                "-target", "x86_64-elf", "-std=c17", "-ffreestanding",
                "-I.", "-Ipreboot",
            ])
        if path.startswith("preboot/"):
            return ("freestanding-host-c17", [
                "-std=c17", "-ffreestanding", "-I.", "-Ipreboot",
            ])
        if path.startswith("kernel/"):
            return ("freestanding-host-c17", [
                "-std=c17", "-ffreestanding", "-I.", "-Ikernel",
                "-Ikernel/core", "-Ikernel/core/hcd",
            ])
    return None


def loc_for_source(node: dict, source: str) -> tuple[int, int] | None:
    loc = node.get("loc") or {}
    if "spellingLoc" in loc:
        loc = loc["spellingLoc"]
    if "expansionLoc" in loc and "line" not in loc:
        loc = loc["expansionLoc"]
    # The main source usually omits "file"; included headers include a
    # provenance field. Explicitly reject any includedFrom.
    if loc.get("includedFrom"):
        return None
    file = loc.get("file")
    if file and not (Path(file).as_posix() == source or
                     Path(file).name == Path(source).name):
        return None
    line = loc.get("line")
    if line is None:
        return None
    end = (node.get("range") or {}).get("end") or {}
    return int(line), int(end.get("line", line))


def direct_callee(call: dict) -> str | None:
    # Only inspect the callee expression, not argument subtrees.
    inner = call.get("inner") or []
    if not inner:
        return None
    current = inner[0]
    while isinstance(current, dict):
        kind = current.get("kind")
        if kind == "DeclRefExpr":
            referred = current.get("referencedDecl") or {}
            if referred.get("kind") in ("FunctionDecl", "FunctionTemplateDecl"):
                return referred.get("name")
            return None
        if kind == "MemberExpr":
            return None
        children = current.get("inner") or []
        if not children:
            return None
        current = children[0]
    return None


def callable_receiver(call: dict) -> tuple[str, str, str, str]:
    """Retrieve a compiler-typed callee *expression*, never guess its target.

    For an indirect callback we know the receiver and function-pointer ABI,
    but we do NOT know which runtime registration wins. That obligation is
    left open for linking, capability and semantic review.
    """
    callee = (call.get("inner") or [])
    if not callee:
        return "", "", "", ""
    expression = callee[0]
    receiver_info = expression.get("type") or {}
    # Clang's desugared type is the ABI call signature; preserve a typedef
    # alias only if its actual target type is unavailable in this AST.
    receiver_type = (receiver_info.get("desugaredQualType") or
                     receiver_info.get("qualType", ""))
    begin = (call.get("range") or {}).get("begin") or {}
    offset = begin.get("offset")
    call_offset = str(offset) if isinstance(offset, int) else ""
    candidate = expression
    for _ in range(24):
        kind = candidate.get("kind")
        if kind == "MemberExpr":
            return (candidate.get("name") or "",
                    "MemberExpr", receiver_type, call_offset)
        if kind == "DeclRefExpr":
            ref = candidate.get("referencedDecl") or {}
            return (ref.get("name") or "",
                    ref.get("kind") or "DeclRefExpr",
                    receiver_type, call_offset)
        children = candidate.get("inner") or []
        if not children or not isinstance(children[0], dict):
            return "", kind or "", receiver_type, call_offset
        candidate = children[0]
    return "", "NESTED_CALLEE_REQUIRES_REVIEW", receiver_type, call_offset


def inspect_function(node: dict, project: str, profile: str,
                     path: str, line: int, end: int) -> tuple[dict, list[dict]]:
    counter = Counter()
    direct = Counter()
    references = set()
    calls = []

    def inspect(subtree: dict) -> None:
        kind = subtree.get("kind") or ""
        if kind in ("ForStmt", "WhileStmt", "DoStmt", "GotoStmt",
                    "BreakStmt", "ContinueStmt", "SwitchStmt",
                    "ConditionalOperator"):
            counter[kind] += 1
        if kind in ("CallExpr", "CXXMemberCallExpr", "CXXOperatorCallExpr"):
            name = direct_callee(subtree)
            if name:
                direct[name] += 1
                calls.append(dict(project=project, profile=profile,
                                  caller_path=path, caller=node["name"],
                                  callee=name, direct_kind=kind,
                                  resolution="DIRECT_AST_NAME_ONLY",
                                  target_candidates="", receiver_symbol="",
                                  receiver_decl_kind="", receiver_abi_type="",
                                  call_offset=""))
            else:
                counter["IndirectOrUnresolvedCall"] += 1
                receiver, decl, abi, offset = callable_receiver(subtree)
                calls.append(dict(project=project, profile=profile,
                                  caller_path=path, caller=node["name"],
                                  callee="", direct_kind=kind,
                                  resolution="INDIRECT_OR_MACRO_CALL",
                                  target_candidates="",
                                  receiver_symbol=receiver,
                                  receiver_decl_kind=decl,
                                  receiver_abi_type=abi,
                                  call_offset=offset))
        if kind == "DeclRefExpr":
            target = subtree.get("referencedDecl") or {}
            if target.get("kind") == "VarDecl" and target.get("name"):
                references.add(target["name"])
        for child in subtree.get("inner") or []:
            if isinstance(child, dict):
                inspect(child)

    for child in node.get("inner") or []:
        if child.get("kind") == "CompoundStmt":
            inspect(child)
    row = dict(project=project, profile=profile, path=path,
               symbol=node["name"], line=line, end_line=end,
               direct_call_names="|".join(sorted(direct)),
               indirect_call_count=counter["IndirectOrUnresolvedCall"],
               control_flow_nodes="|".join(
                   f"{k}:{v}" for k, v in sorted(counter.items())
                   if k != "IndirectOrUnresolvedCall"),
               variable_globals="|".join(sorted(references)),
               body_status="COMPILER_AST_BODY_PARSED_NOT_SEMANTICALLY_REVIEWED")
    return row, calls


def inspect_ast(data: dict, project: str, profile: str,
                path: str) -> tuple[list[dict], list[dict]]:
    functions = []
    calls = []
    # TranslationUnitDecl outermost children include all imported header
    # declarations, but source functions have distinct loc provenance.
    for node in data.get("inner") or []:
        if node.get("kind") != "FunctionDecl" or not node.get("name"):
            continue
        if not any(x.get("kind") == "CompoundStmt"
                   for x in node.get("inner") or []):
            continue
        loc = loc_for_source(node, path)
        if loc is None:
            continue
        row, found = inspect_function(
            node, project, profile, path, loc[0], loc[1])
        functions.append(row)
        calls.extend(found)
    return functions, calls


def run_ast(root: Path, path: str,
            flags: list[str]) -> tuple[dict | None, str]:
    process = subprocess.run(
        ["clang", *flags, "-fno-color-diagnostics", "-Wno-everything",
         "-fsyntax-only", "-Xclang", "-ast-dump=json", path],
        cwd=root, capture_output=True, timeout=55)
    if process.returncode != 0:
        return None, process.stderr.decode("utf-8", "replace")[-1200:]
    try:
        return json.loads(process.stdout), ""
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return None, f"INVALID_AST_JSON: {exc}"


def write_csv(name: str, fields: tuple[str, ...], items: list[dict]) -> None:
    with (OUT / name).open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(items)


def main() -> int:
    cli = argparse.ArgumentParser()
    cli.add_argument("--max-sources", type=int,
                     help="Diagnostic only; partial run does not qualify P0")
    args = cli.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    definitions = []
    calls = []
    translation_units = []
    projects = [
        ("BASE-NEXS", BASE, ("core/", "registry/", "state/", "lang/", "sys/",
                             "runtime/", "compiler/", "hal/", "kernel/", "fs/", "block/", "journal/",
                             "extensions/")),
        ("NXOLMO", NXOLMO, ("src/",)),
        ("NXEC", NXEC, ("preboot/", "kernel/")),
    ]
    for project, root, dirs in projects:
        files = source_files(root, dirs)
        for path in files:
            selected = choose_profile(project, path)
            if selected is None:
                translation_units.append(dict(
                    project=project, profile="NOT_QUALIFIED",
                    path=path, result="NOT_QUALIFIED_TARGET_OR_IMPORTED",
                    functions=0, direct_calls=0, indirect_calls=0,
                    stderr_excerpt="Requires selected target profile or is external snapshot"))
                continue
            profile, flags = selected
            if args.max_sources is not None and len(translation_units) >= args.max_sources:
                translation_units.append(dict(
                    project=project, profile=profile,
                    path=path, result="NOT_RUN_PARTIAL",
                    functions=0, direct_calls=0, indirect_calls=0,
                    stderr_excerpt="Diagnostic limited invocation"))
                continue
            try:
                data, diagnostic = run_ast(root, path, flags)
            except subprocess.TimeoutExpired:
                data, diagnostic = None, "CLANG_AST_TIMEOUT"
            if data is None:
                translation_units.append(dict(
                    project=project, profile=profile, path=path,
                    result="FAIL", functions=0, direct_calls=0,
                    indirect_calls=0, stderr_excerpt=diagnostic))
                continue
            rows, edges = inspect_ast(data, project, profile, path)
            if not rows:
                # A feature-disabled translation unit is not compiler
                # evidence for its inactive function definitions.
                status = ("NOT_COMPILED_FEATURE_DISABLED"
                          if project == "BASE-NEXS" and
                          path == "kernel/vfs_server.c"
                          else "FAIL_NO_SOURCE_FUNCTIONS")
            else:
                status = "AST_PARSED"
            translation_units.append(dict(
                project=project, profile=profile, path=path,
                result=status, functions=len(rows),
                direct_calls=sum(bool(x["callee"]) for x in edges),
                indirect_calls=sum(not x["callee"] for x in edges),
                stderr_excerpt=""))
            definitions.extend(rows)
            calls.extend(edges)

    by_name: dict[tuple[str, str], set[str]] = defaultdict(set)
    for fn in definitions:
        by_name[(fn["project"], fn["symbol"])].add(fn["path"])
    for edge in calls:
        if not edge["callee"]:
            continue
        targets = by_name.get((edge["project"], edge["callee"]), set())
        edge["target_candidates"] = "|".join(sorted(targets))
        if len(targets) == 1:
            edge["resolution"] = "AST_DIRECT_SYMBOL_SINGLE_SOURCE_CANDIDATE"
        elif len(targets) > 1:
            edge["resolution"] = "AST_DIRECT_SYMBOL_AMBIGUOUS_TARGET"
        else:
            edge["resolution"] = "AST_DIRECT_EXTERNAL_OR_NOT_QUALIFIED"

    definitions.sort(key=lambda x: (x["project"], x["path"], x["line"]))
    calls.sort(key=lambda x: (x["project"], x["caller_path"], x["caller"], x["callee"]))
    source_status = {(row["project"], row["path"]): row["result"]
                     for row in translation_units}
    parsed = {(row["project"], row["path"], row["symbol"])
              for row in definitions}
    coverage = []
    with (OUT / "FUNCTION_CATALOG.csv").open(
            newline="", encoding="utf-8") as stream:
        catalog = list(csv.DictReader(stream))
    for definition in catalog:
        project = definition["project"]
        path = definition["path"]
        symbol = definition["symbol"]
        language = definition["language"]
        status = source_status.get((project, path), "NOT_A_C_TRANSLATION_UNIT")
        if language not in ("C", "C++"):
            result = "NOT_C_FUNCTION"
            reason = "Analyzed by separate language or assembly inventory"
        elif definition["role"] == "external-reference-readonly":
            result = "REFERENCE_READ_ONLY"
            reason = "NexsOS1 comparison source, not a NEXS implementation"
        elif definition["role"] in ("test", "example", "build-tool"):
            result = "TEST_TOOL_AST_NOT_SCANNED"
            reason = "Test/tool/example source intentionally not in product TU analysis"
        elif definition["role"] in (
                "imported-reference-nonproduct",
                "unadapted-linux-reference-gate-blocker"):
            result = "UNADAPTED_IMPORTED_REFERENCE"
            reason = "External snapshot is not compiled as product source"
        elif (project, path, symbol) in parsed:
            result = "AST_BODY_PARSED"
            reason = "Validated Clang AST body in one selected target profile"
        elif path.endswith(".h"):
            result = "HEADER_INLINE_NOT_PROVEN"
            reason = "Header function requires compiler profile selection"
        elif status == "NOT_QUALIFIED_TARGET_OR_IMPORTED":
            result = "UNQUALIFIED_TOOLCHAIN"
            reason = "SDK/target not available or excludes this source"
        elif status == "NOT_COMPILED_FEATURE_DISABLED":
            result = "FEATURE_DISABLED"
            reason = "Function excluded by selected target's preprocessing"
        elif status == "AST_PARSED":
            result = "NOT_IN_TARGET_PREPROCESSOR"
            reason = "Definition not present in this compiler build profile"
        else:
            result = "UNRESOLVED_COMPILER_EVIDENCE"
            reason = "No verified source body; inspect compiler diagnostic"
        coverage.append(dict(
            function_id=definition["id"], project=project,
            role=definition["role"], path=path, symbol=symbol,
            line=definition["line"], compiler_coverage=result,
            reason=reason, source_status=status))
    write_csv("FUNCTION_COMPILER_COVERAGE.csv", COVERAGE_HEADER, coverage)
    write_csv("CLANG_AST_FUNCTIONS.csv", HEADER, definitions)
    write_csv("CLANG_AST_CALLS.csv", CALL_HEADER, calls)
    write_csv("CLANG_AST_TRANSLATION_UNITS.csv", TU_HEADER, translation_units)
    counts = Counter(x["result"] for x in translation_units)
    report = dict(
        tool="clang -Xclang -ast-dump=json; selected hosted, freestanding and OS1 cross profiles",
        status="PARTIAL_ARCHITECTURE_QUALIFICATION",
        sources_by_status=dict(sorted(counts.items())),
        functions_with_compiler_body=len(definitions),
        direct_ast_call_nodes=sum(bool(x["callee"]) for x in calls),
        indirect_or_unresolved_ast_calls=sum(not x["callee"] for x in calls),
        function_compiler_coverage=dict(sorted(
            Counter(row["compiler_coverage"] for row in coverage).items())),
        catalog_rows_classified=len(coverage),
        limitations=[
            "Target-specific source is parsed with selected host, freestanding and OS1 Clang profiles; not a complete build database",
            "seL4/Microkit source cannot be compiled without its Microkit SDK",
            "NEXS/kernel version-conditional variants are not all compiled",
            "Function-pointer and indirect calls require manual tracing",
            "C++ and assembly need separate target-specific compilers/analysis",
            "Definition and AST call evidence does not establish Power12 proof",
            "Target candidate mapping by symbol name may be ambiguous",
            "Imported Linux 9P source is NOT qualified product code",
        ],
    )
    (OUT / "CLANG_AST_MANIFEST.json").write_text(
        json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if counts.get("FAIL", 0) == 0 and \
        counts.get("FAIL_NO_SOURCE_FUNCTIONS", 0) == 0 and \
        counts.get("NOT_RUN_PARTIAL", 0) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
