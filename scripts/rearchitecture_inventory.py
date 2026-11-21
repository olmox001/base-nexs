#!/usr/bin/env python3
"""Reproducible, source-indexed C/Python/NEXS definition inventory.

This indexes definitions; it does not establish behavioral correctness or a
complete call graph. The human-reviewed architecture is documented separately.
Requires Universal Ctags 6.x and Python >= 3.10; no source tree is modified.
"""
from __future__ import annotations

import ast
import csv
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
NXOLMO = BASE / "external/references/nxolmo"
NXEC = NXOLMO / "external/references/nxec"
NEXSOS = BASE.parents[2]
OUTPUT = BASE / "docs/rearchitecture"
C_SUFFIX = {".c", ".h", ".cc", ".cpp"}
ASM_SUFFIX = {".s", ".S"}
PROJECTS = [
    ("BASE-NEXS", BASE, ("core/", "registry/", "state/", "lang/", "sys/", "runtime/",
                           "compiler/", "hal/", "kernel/", "fs/", "block/", "journal/", "extensions/", "include/",
                          "tests/", "scripts/", "nxlib/", "nxlib/services/", "example/")),
    ("NXOLMO", NXOLMO, ("src/", "include/", "tests/", "example/", "tools/")),
    ("NXEC", NXEC, ("preboot/", "boot/", "kernel/", "userland/",
                    "tools/", "patch/")),
]
# NexsOS1 is read-only. These are targeted, separately checked-out Git
# submodules, not source files tracked by the umbrella Git index.
PROJECTS.extend((
    ("NEXSOS1-KERNEL-REFERENCE", NEXSOS / "kernel", (
        "core/object.c", "core/object_boot.c", "core/hal_core_adapter.c",
        "fs/vfs.c", "fs/ext4.c", "lib/registry.c", "HAL/core/", "HAL/base/",
        "HAL/common/", "HAL/capability/", "HAL/include/")),
    ("NEXSOS1-SDK-REFERENCE", NEXSOS / "system", (
        "lib/extensions/USERKIT/nxexecd/", "lib/extensions/object/",
        "include/contract/object.h")),
    ("NEXSOS1-USERKIT-REFERENCE", NEXSOS / "user/USERKIT", (
        "services/nxexecd/", "services/nxttyd/")),
))

FIELDS = ("id", "project", "role", "subsystem", "language", "path", "line",
          "end_line", "symbol", "visibility", "target_owner", "target_contract",
          "problem_to_resolve", "required_transformation", "phase",
          "verification", "code_lines", "lexical_signals", "review_status")
GAP_FIELDS = ("project", "path", "line", "symbol", "status")
FILE_FIELDS = ("project", "role", "subsystem", "path", "extension", "bytes",
               "sha256", "function_or_label_count", "review_status")
EDGE_FIELDS = ("project", "caller_id", "caller_path", "caller_symbol", "callee_name",
               "lexical_occurrences", "candidate_target_paths", "resolution")
TEST_FIELDS = ("function_id", "project", "source", "symbol", "phase",
               "proposed_success_test_id", "proposed_fault_test_id",
               "existing_test_mapping_status")
ENTRY_FIELDS = ("project", "path", "kind", "symbol", "notes")
FILES = []
SOURCES: dict[tuple[str, str], list[str]] = {}
SCRIPTS = []
ENTRIES: list[dict] = []
PROTOTYPES: list[dict] = []


def tracked(project: str, root: Path, prefixes: tuple[str, ...]) -> list[str]:
    # gitlink submodules are not recursively searched by git ls-files.
    # P0 must never silently omit new production sources/tests merely
    # because they are not staged yet. Include untracked, non-ignored
    # Base-NEXS worktree files, but keep NXOLMO/NXEC/NexsOS references
    # pinned to their read-only Git-indexed source revisions.
    command = ["git", "-C", str(root), "ls-files", "-z"]
    if project == "BASE-NEXS":
        command.extend(("--cached", "--others", "--exclude-standard"))
    check = subprocess.run(command, capture_output=True, check=True)
    paths = sorted({entry.decode("utf-8", "surrogateescape")
                    for entry in check.stdout.split(b"\0") if entry})
    chosen = []
    for path in paths:
        if not path.startswith(prefixes) and path not in prefixes:
            continue
        if project == "NXOLMO" and path.startswith("src/imports/"):
            # Include snapshots as a distinct class (never product code).
            pass
        if (root / path).is_file() and (root / path).suffix in (
                C_SUFFIX | ASM_SUFFIX | {".nx", ".py"}):
            chosen.append(path)
    return sorted(chosen)


def role(project: str, path: str) -> str:
    if project.startswith("NEXSOS1-"):
        return "external-reference-readonly"
    if path.startswith("src/imports/"):
        return "imported-reference-nonproduct"
    if project == "NXOLMO" and path.startswith("src/filesystem/9p/"):
        return "unadapted-linux-reference-gate-blocker"
    if ("/tests/" in f"/{path}" or path.startswith("tests/") or
            project == "BASE-NEXS" and path.startswith("kernel/test/")):
        return "test"
    if path.startswith(("tools/", "scripts/")):
        return "build-tool"
    if project == "BASE-NEXS" and path.startswith("nxlib/services/"):
        return "language-service"
    if project == "BASE-NEXS" and path.startswith("nxlib/"):
        return "language-library"
    if project == "BASE-NEXS" and path.startswith("example/"):
        return "language-application"
    if path.startswith("example/"):
        return "example"
    return "product"


def subsystem(project: str, path: str) -> str:
    p = path.lower()
    if project.startswith("NEXSOS1-"):
        if "nxexec" in p:
            return "ExecutionBroker"
        if "/hal/" in p:
            return "PlatformHAL"
        if "object" in p:
            return "CapabilityObject"
        if "vfs" in p or "ext4" in p:
            return "VFSProvider"
        return "NexsOSReference"
    if project == "NXEC":
        if "hcd" in p or "memory_plan" in p:
            return "BootDescriptorAndMemory"
        if "preboot/hal/" in p:
            return "FirmwareHAL"
        return "NXECContracts"
    if project == "NXOLMO":
        for prefix, label in (
            ("src/imports/", "ImportedSourceSnapshot"),
            ("registry/", "CanonicalState"),
            ("authority/", "Authority"),
            ("hardware/", "HardwareResources"),
            ("buffer/", "PageBufferLease"),
            ("block/", "BlockBackend"),
            ("journal/", "DurableTransactions"),
            ("filesystem/ext4/", "Ext4Provider"),
            ("filesystem/9p/", "Linux9pSnapshot"),
            ("protocol/9p/", "9PWire"),
            ("runtime/", "FuelAndMetrics"),
            ("crypto/", "Integrity"),
            ("base/", "PortableBinaryABI"),
        ):
            if prefix in p:
                return label
        return "NXOLMOInfrastructure"
    for prefix, label in (
        ("core/buddy", "Allocator"), ("core/pager", "PageMemory"),
        ("core/value", "ValueAndObject"), ("core/dynarray", "ArrayAndBuffer"),
        ("core/", "RuntimeUtilities"), ("registry/reg_ipc", "REGTransport"),
        ("registry/", "LegacyNamespace"), ("state/registry/", "CanonicalStateStore"),
        ("state/authority/", "CanonicalAuthority"),
        ("state/memory/", "CanonicalMemory"),
        ("state/storage/", "CanonicalStorage"), ("lang/lexer", "Lexer"),
        ("lang/parser", "ParserAndAST"), ("lang/eval", "Evaluator"),
        ("lang/fn_table", "CallableRegistry"), ("lang/builtins", "LanguageBindings"),
        ("sys/sysproc", "ProcessExecution"), ("sys/sysio", "VFSAndStreams"),
        ("runtime/nexs_line", "TTYAndInput"), ("runtime/", "RuntimeLifecycle"),
        ("compiler/driver", "BuildOrchestrator"),
        ("compiler/codegen", "NativeCodegen"),
        ("compiler/dep_scan", "ModuleLinker"), ("compiler/", "CompilerABI"),
        ("hal/bc/", "DeviceBytecode"), ("hal/os1/", "OS1Adapter"),
        ("hal/sel4/", "SeL4Adapter"), ("hal/", "PlatformHAL"),
        ("kernel/", "FreestandingCore"), ("fs/", "FilesystemProtocol"),
        ("extensions/", "RuntimeExtension"),
        ("nxlib/services/", "LanguageService"), ("nxlib/", "LanguageLibrary"),
        ("example/", "LanguageExample"),
    ):
        if p.startswith(prefix):
            return label
    return "PublicContractOrTest"


OWNERS = {
    "Allocator": ("NEXS Runtime", "ResourceAllocator/PageProvider", "P1"),
    "PageMemory": ("NEXS Runtime", "PageObjectAndLease", "P1"),
    "ValueAndObject": ("NEXS Runtime", "TypedObjectValueABI", "P1"),
    "ArrayAndBuffer": ("NEXS Runtime", "SharedArrayPageView", "P2"),
    "RuntimeUtilities": ("NEXS Runtime", "PortableCheckedPrimitives", "P1"),
    "LegacyNamespace": ("NEXS Runtime", "NXREGCanonicalNamespace", "P3"),
    "REGTransport": ("NEXS Runtime", "REGEndpointTransport", "P4"),
    "Lexer": ("NEXS Language", "SpanLexer", "P1"),
    "ParserAndAST": ("NEXS Language", "ArenaIR", "P1"),
    "Evaluator": ("NEXS Executor", "FuelMeteredConcurrentVM", "P2"),
    "CallableRegistry": ("NEXS Runtime", "CallableObjectIndex", "P2"),
    "LanguageBindings": ("NEXS Language", "CapabilityBoundIntrinsicABI", "P3"),
    "ProcessExecution": ("NEXS Executor", "CapabilityExecutionBroker", "P4"),
    "VFSAndStreams": ("NEXS Runtime", "NXREGVFSFileObjects", "P5"),
    "TTYAndInput": ("NEXS Runtime", "ScopedConsoleObject", "P4"),
    "RuntimeLifecycle": ("NEXS Runtime", "NEXSRuntimeInstance", "P2"),
    "NativeCodegen": ("NEXS Compiler", "CanonicalIRToNative", "P6"),
    "BuildOrchestrator": ("NEXS Compiler", "ArgvProcessLauncher", "P1"),
    "ModuleLinker": ("NEXS Compiler", "CapabilityModuleLinker", "P6"),
    "CompilerABI": ("NEXS Compiler", "VerifiedExecutableFormat", "P6"),
    "DeviceBytecode": ("NEXS Executor", "VerifiedObjectBytecode", "P6"),
    "OS1Adapter": ("NEXS Platform", "OS1ObjectCapabilityAdapter", "P7"),
    "SeL4Adapter": ("NEXS Platform", "SeL4CapabilityAdapter", "P7"),
    "PlatformHAL": ("NEXS Platform", "ResourceProviderABI", "P7"),
    "FreestandingCore": ("NEXS Kernel", "CapabilityKernelProfile", "P7"),
    "FilesystemProtocol": ("NEXS Runtime", "NXREGVFSAnd9P", "P5"),
    "LanguageLibrary": ("NEXS Language", "VersionedModuleAPI", "P6"),
    "LanguageService": ("NEXS Runtime", "ScopedServiceObject", "P5"),
    "LanguageExample": ("NEXS Language", "ExecutableConformanceFixture", "P8"),
    "CanonicalState": ("NEXS Runtime", "NXREGCanonicalStateTransaction", "P3"),
    "Authority": ("NEXS Runtime", "NXECCompatibleCapabilityEngine", "P3"),
    "HardwareResources": ("NEXS Platform", "DeviceResourceObjects", "P7"),
    "PageBufferLease": ("NEXS Runtime", "SharedPageBufferLease", "P2"),
    "BlockBackend": ("NEXS Runtime", "CapabilityBlockDevice", "P5"),
    "DurableTransactions": ("NEXS Runtime", "MVCCWALCommitEngine", "P5"),
    "Ext4Provider": ("NEXS Runtime", "TransactionalExt4Provider", "P5"),
    "Linux9pSnapshot": ("NEXS Runtime", "PortOrDiscardLinux9PReference", "P5"),
    "9PWire": ("NEXS Runtime", "9P2000LAndREGBridge", "P5"),
    "FuelAndMetrics": ("NEXS Runtime", "PerOperationBudgetAccounting", "P0"),
    "Integrity": ("NEXS Runtime", "ValidatedIntegrityPrimitives", "P1"),
    "PortableBinaryABI": ("NEXS Runtime", "ObjectWireABICodec", "P1"),
    "BootDescriptorAndMemory": ("NEXS Kernel", "ValidatedBootResourceDescriptor", "P7"),
    "FirmwareHAL": ("NEXS Platform", "PerArchitectureFirmwareProvider", "P7"),
    "ExecutionBroker": ("NEXS Executor", "ExecutionProcessAndThreadObject", "P4"),
    "CapabilityObject": ("NEXS Runtime", "CapabilityHandleAdapter", "P4"),
    "VFSProvider": ("NEXS Runtime", "FilesystemProviderConformance", "P5"),
    "NexsOSReference": ("NEXS Runtime", "ReferenceOnlyContractComparison", "P7"),
    "ImportedSourceSnapshot": ("NEXS Runtime", "AuditPortOrExcludeWithProvenance", "P8"),
    "NXECContracts": ("NEXS Kernel", "NXECSecurityAndREGContracts", "P4"),
}


def treatment(project: str, subsystem_name: str, name: str, item_role: str):
    owner, target, phase = OWNERS.get(
        subsystem_name, ("NEXS Runtime", "ObjectCapabilityIntegration", "P8"))
    if item_role in ("test", "example", "language-application"):
        action = "REWRITE_AS_ASSERTED_BEHAVIORAL_CONFORMANCE"
        problem = "Existing coverage is not a full cross-backend/parallel/failure contract"
        phase = "P8"
    elif item_role in ("imported-reference-nonproduct",
                       "unadapted-linux-reference-gate-blocker"):
        action = "REVIEW_LICENSE_COMPARE_ALGORITHM_PORT_ONLY_IF_SUPERIOR"
        problem = "Unadapted external snapshot remains a strict gate blocker"
    elif item_role == "external-reference-readonly":
        action = "COMPARE_ADAPT_BEHAVIOR_IN_NEXS_DO_NOT_EDIT_REFERENCE"
        problem = "OS-specific reference ABI or ownership cannot become universal"
    elif item_role == "build-tool":
        action = "REWORK_FOR_REPRODUCIBLE_BUILD_AND_VERIFICATION"
        problem = "Build tooling needs reproducible cross-target evidence"
    elif project == "NXEC":
        action = "INTEGRATE_VERIFIED_ALGORITHM_AND_SECURE_BOOT_CONTRACT"
        problem = "Boot/HCD implementation is not the complete NXEC REG/capability kernel"
    elif project == "NXOLMO":
        action = "ADOPT_REFACTOR_MIGRATE_INTO_ONE_CANONICAL_ENGINE"
        problem = "H0 component lacks complete namespace/commit/execution integration"
    else:
        action = "REPLACE_OR_REFINE_AND_MIGRATE_ALL_CALLERS"
        problem = "Legacy singleton/host-specific/unsafe-state assumptions require redesign"
    lower = name.lower()
    if lower in ("make_tok", "lexer_next", "lexer_init_lib") and project == "BASE-NEXS":
        problem = "Token text[NAME_LEN] truncates literals/paths; missing exact spans"
        action = "REPLACE_WITH_BORROWED_SOURCE_SPANS_AND_EXPLICIT_OWNERSHIP"
        phase = "P1"
    elif lower in ("buddy_alloc", "buddy_free", "xmalloc", "xfree", "buddy_next_pow2"):
        problem = "Global pool, failure-as-exit, hidden ownership and pool ceiling"
        action = "REPLACE_WITH_SCOPED_RESERVATION_PAGE_PROVIDER_AND_RECOVERABLE_OOM"
        phase = "P1"
    elif lower in ("eval_node", "eval_block", "eval", "eval_file", "eval_str", "parse_expr",
                    "parse_binop", "ast_free", "ast_free_safe"):
        problem = "Recursive host stack or unmetered execution; global AST lifetimes"
        action = "ITERATIVE_VM_FRAMES_FUEL_CANCELLATION_AND_ARENA_OWNERSHIP"
        phase = "P2"
    elif lower in ("fn_lookup", "fn_lookup_by_idx", "fn_register", "fn_register_builtin_sig"):
        problem = "Global linear function index, stale indices, no capability or version"
        action = "REGISTER_CALLABLE_AS_GENERATION_CHECKED_OBJECT_WITH_INDEXED_LOOKUP"
        phase = "P2"
    elif lower in ("nexs_runtime_init", "nexs_runtime_autoload"):
        problem = "Global initialization and implicit service loading"
        action = "EXPLICIT_REENTRANT_RUNTIME_CREATE_AND_INSTANCE_SCOPED_MODULE_GRAPH"
        phase = "P2"
    elif lower.startswith(("reg_", "pipe_")) and project == "BASE-NEXS":
        problem = "Pointer/path authority, singleton tree, recursive message wire or POSIX coupling"
        action = "REPLACE_WITH_NXREG_OBJECT_ID_CAPABILITY_AND_BOUNDED_REG_ENDPOINT"
        phase = "P3"
    elif lower.startswith("nxreg_"):
        problem = "Read-only H0 ABI lacks transactional names, mount, create and commit"
        action = "EXTEND_TO_CANONICAL_TYPED_TRANSACTIONAL_NXREG_SERVICE"
        phase = "P3"
    elif lower.startswith("nx_state_store_"):
        problem = "Slot-based H0 state is not complete MVCC, named namespace or durable root"
        action = "EXTEND_PERSISTENCE_NAME_INDEX_MVCC_AND_SAFE_RECLAMATION"
        phase = "P3"
    elif lower.startswith(("nx_buffer_", "nx_ring_", "nx_mpmc_")):
        problem = "Buffer H0 lease lacks full cross-process/page/DMA/revocation integration"
        action = "INTEGRATE_PAGE_OBJECT_LEASE_COW_COHERENCY_BACKPRESSURE"
        phase = "P2"
    elif lower.startswith(("nx_journal_", "nx_block_")):
        problem = "H0 storage/wal needs physical durability and crash restart tests"
        action = "IMPLEMENT_FILE_DEVICE_BACKENDS_DURABLE_ATOMIC_COMMIT"
        phase = "P5"
    elif lower.startswith("execsvc_"):
        problem = "OS-specific spawn/TTY/environment/fd limits and broker ownership"
        action = "GENERALIZE_TO_EXECUTABLE_CAPABILITY_AND_ASYNC_SPAWN_CONTRACT"
        phase = "P4"
    elif lower.startswith(("nexs_compile_", "nexs_codegen_")):
        problem = "C AOT and shell command/temporary-file path lack secure unified IR"
        action = "CANONICAL_IR_EXPLICIT_ARGV_LAUNCH_UNIFIED_NATIVE_LINK"
        phase = "P6"
    elif lower.startswith(("nx_ext4_", "nx_9p_", "nxp9_")):
        problem = "Missing complete provider/wire/lifecycle/conformance integration"
        action = "IMPLEMENT_TRANSACTIONAL_PROVIDER_AND_FULL_PROTOCOL_TESTS"
        phase = "P5"

    tests = {
        "P0": "POWER12_AST_CFG_AND_OPERATION_BUDGET_GATE",
        "P1": "ASAN_UBSAN_OOM_FAULT_STRING_AND_ABI_VECTORS",
        "P2": "TSAN_PARALLEL_VM_FUEL_ZERO_COPY_LEASE_AND_STALE_REF",
        "P3": "MODEL_BASED_NXREG_CAPABILITY_MVCC_LINEARIZABILITY",
        "P4": "PROCESS_DEATH_REVOKE_CANCEL_EXEC_TTY_TESTS",
        "P5": "FSYNC_CRASH_RESTART_EXT4_9P_VFS_CONFORMANCE",
        "P6": "DIFFERENTIAL_AST_VM_AOT_BYTECODE_LINKER",
        "P7": "HOST_MACOS_LINUX_QEMU_AMD64_ARM64_SE_L4_NXADB",
        "P8": "EXISTING_FIXTURES_NO_REGRESSION_AND_PROFILE_EVIDENCE",
    }
    return owner, target, problem, action, phase, tests[phase]


def append(project: str, path: str, line: int, end: int, name: str,
           language: str, visibility: str) -> None:
    r = role(project, path)
    s = subsystem(project, path)
    owner, target, problem, action, phase, tests = treatment(project, s, name, r)
    lines = SOURCES.get((project, path), [])
    start = max(line - 1, 0)
    stop = max(end, line) if end else line
    body = "\n".join(lines[start:stop])
    checks = (
        ("ASSERT", r"\b(?:assert|_Static_assert|static_assert)\s*\("),
        ("GOTO", r"\bgoto\s+[A-Za-z_]\w*\s*;"),
        ("BREAK", r"\bbreak\s*;"),
        ("UNBOUNDED_LOOP", r"\bwhile\s*\(\s*1\s*\)|\bfor\s*\(\s*;\s*;"),
        ("DYNAMIC_ALLOC", r"\b(?:xmalloc|buddy_alloc|malloc|calloc|realloc)\s*\("),
        ("FATAL_ON_FAILURE", r"\b(?:die|abort|exit|_Exit|__builtin_trap)\s*\("),
        ("FORMAT_OR_TRUNCATION", r"\b(?:strncpy|strncat|sprintf|snprintf)\s*\("),
        ("SHELL_PROCESS", r"\b(?:system|popen|fork|execv|posix_spawn)\s*\("),
        ("GLOBAL_SINGLETON", r"\bg_[a-zA-Z0-9_]+\b"),
        ("PLATFORM_PREPROCESSOR", r"(?m)^\s*#\s*(?:if|ifdef|elif)\b"),
        ("INLINE_ASM", r"\b(?:__asm__|asm)\b"),
    )
    signals = "|".join(label for label, pattern in checks
                       if re.search(pattern, body))
    if end and end >= line and re.search(
            r"\b" + re.escape(name.split(".")[-1]) + r"\s*\(", "\n".join(lines[start+1:stop])):
        signals += ("|" if signals else "") + "POTENTIAL_SELF_CALL"
    stable = f"{project}|{path}|{line}|{name}|{language}"
    entry = dict(zip(FIELDS, (
        hashlib.sha256(stable.encode()).hexdigest()[:12], project, r, s,
        language, path, line, end if end else "", name, visibility,
        owner, target, problem, action, phase, tests,
        max(end - line + 1, 1) if end else 0, signals,
        "AUTO_INDEXED_NOT_INDIVIDUALLY_VERIFIED",
    )))
    ENTRIES.append(entry)


def ctags_index(project: str, root: Path, paths: list[str]) -> None:
    sources = [p for p in paths if Path(p).suffix in C_SUFFIX]
    if not sources:
        return
    command = [
        "ctags", "--output-format=json", "--fields=+nKes",
        "--languages=C,C++", "--kinds-C=+p", "--kinds-C++=+p",
        "-f", "-", *sources,
    ]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"ctags error for {project}: {result.stderr}")
    for raw in result.stdout.splitlines():
        item = json.loads(raw)
        if item.get("_type") != "tag":
            continue
        kind = item.get("kind")
        path = item.get("path", "")
        name = item.get("name", "")
        if kind not in ("function", "prototype") or not name:
            continue
        if kind == "prototype":
            PROTOTYPES.append({"project": project, "path": path,
                               "line": item.get("line", 0), "symbol": name})
        else:
            append(project, path, int(item.get("line", 0)),
                   int(item.get("end", 0)), name,
                   "C++" if Path(path).suffix in {".cpp", ".cc"} else "C",
                   "file-local" if item.get("file") else "exported-or-inline")


def python_index(project: str, root: Path, paths: list[str]) -> None:
    for path in paths:
        if not path.endswith(".py"):
            continue
        data = (root / path).read_text(encoding="utf-8", errors="replace")
        try:
            tree = ast.parse(data, filename=path)
        except SyntaxError:
            SCRIPTS.append((project, path, "PYTHON_SYNTAX_ERROR"))
            continue
        def descend(node, parents):
            for child in ast.iter_child_nodes(node):
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    symbol = ".".join([*parents, child.name])
                    append(project, path, child.lineno, child.end_lineno,
                           symbol, "Python", "tool-or-test")
                    descend(child, [*parents, child.name])
                elif isinstance(child, ast.ClassDef):
                    descend(child, [*parents, child.name])
                else:
                    descend(child, parents)
        descend(tree, [])


def nexs_index(project: str, root: Path, paths: list[str]) -> None:
    pattern = re.compile(r"^\s*fn\s+([A-Za-z_][A-Za-z_0-9]*)\s*\(")
    for path in paths:
        if not path.endswith(".nx"):
            continue
        data = (root / path).read_text(encoding="utf-8", errors="replace")
        lines = data.splitlines()
        for n, text in enumerate(lines, 1):
            match = pattern.search(text)
            if match:
                append(project, path, n, 0, match.group(1), "NEXS",
                       "language-callable")


def asm_index(project: str, root: Path, paths: list[str]) -> None:
    # Conservative global/nonlocal assembly label discovery; only syntax
    # patterns, NOT a proof that each label defines a callable procedure.
    label = re.compile(r"^\s*([A-Za-z_][A-Za-z_0-9.]*)\s*:\s*(?:[#;].*)?$")
    for path in paths:
        if Path(path).suffix not in ASM_SUFFIX:
            continue
        data = (root / path).read_text(encoding="utf-8", errors="replace")
        for n, text in enumerate(data.splitlines(), 1):
            match = label.match(text)
            if match and not match.group(1).startswith(("L", ".L")):
                append(project, path, n, 0, match.group(1),
                       "AssemblyCandidate", "LABEL_UNVERIFIED")


def dump_csv(path: Path, keys: tuple, rows: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=keys, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    revisions = {}
    for project, root, prefixes in PROJECTS:
        if not (root / ".git").exists():
            raise RuntimeError(f"Uninitialized source reference: {project} ({root})")
        rev = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                             text=True, capture_output=True, check=True)
        revisions[project] = rev.stdout.strip()
        paths = tracked(project, root, prefixes)
        for path in paths:
            raw = (root / path).read_bytes()
            SOURCES[(project, path)] = raw.decode("utf-8", errors="replace").splitlines()
            FILES.append({
                "project": project, "role": role(project, path),
                "subsystem": subsystem(project, path), "path": path,
                "extension": Path(path).suffix, "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "function_or_label_count": 0,
                "review_status": "AUTO_INDEXED_NOT_INDIVIDUALLY_VERIFIED",
            })
        ctags_index(project, root, paths)
        python_index(project, root, paths)
        nexs_index(project, root, paths)
        asm_index(project, root, paths)
    ENTRIES.sort(key=lambda x: (x["project"], x["path"], int(x["line"]), x["symbol"]))
    frequency = Counter((x["project"], x["path"]) for x in ENTRIES)
    for record in FILES:
        record["function_or_label_count"] = frequency[
            (record["project"], record["path"])]
    dump_csv(OUTPUT / "FUNCTION_CATALOG.csv", FIELDS, ENTRIES)
    dump_csv(OUTPUT / "SOURCE_FILE_CATALOG.csv", FILE_FIELDS, FILES)
    test_traceability = []
    for function in ENTRIES:
        if function["role"] == "product":
            test_traceability.append({
                "function_id": function["id"], "project": function["project"],
                "source": function["path"], "symbol": function["symbol"],
                "phase": function["phase"],
                "proposed_success_test_id": "NXF-" + function["id"] + "-OK",
                "proposed_fault_test_id": "NXF-" + function["id"] + "-FAULT",
                "existing_test_mapping_status": "NOT_REVIEWED_OR_LINKED",
            })
    dump_csv(OUTPUT / "PROPOSED_TEST_TRACEABILITY.csv",
             TEST_FIELDS, test_traceability)

    names = {}
    for function in ENTRIES:
        if function["language"] in ("C", "C++"):
            key = (function["project"], function["symbol"])
            names.setdefault(key, []).append(function["path"])
    call_pattern = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(")
    edge_candidates = []
    for function in ENTRIES:
        if function["language"] not in ("C", "C++"):
            continue
        lines = SOURCES.get((function["project"], function["path"]), [])
        a = max(int(function["line"]) - 1, 0)
        b = int(function["end_line"] or function["line"])
        if b <= a:
            continue
        # Name matches are intentionally heuristic: preprocessor and macros
        # can shadow them; indirect calls require a real compiler call graph.
        body = "\n".join(lines[a+1:b])
        counts = Counter(name for name in call_pattern.findall(body)
                         if (function["project"], name) in names)
        for callee, count in counts.items():
            edge_candidates.append({
                "project": function["project"],
                "caller_id": function["id"], "caller_path": function["path"],
                "caller_symbol": function["symbol"], "callee_name": callee,
                "lexical_occurrences": count,
                "candidate_target_paths": "|".join(sorted(set(
                    names[(function["project"], callee)]))),
                "resolution": "LEXICAL_NAME_ONLY_NOT_VALIDATED_CALL_EDGE",
            })
    edge_candidates.sort(
        key=lambda x: (x["project"], x["caller_path"],
                       x["caller_symbol"], x["callee_name"]))
    dump_csv(OUTPUT / "CALL_EDGE_CANDIDATES.csv", EDGE_FIELDS, edge_candidates)
    entries = [
        {"project": row["project"], "path": row["path"], "kind": "C_OR_PYTHON_MAIN",
         "symbol": row["symbol"], "notes": "Executable entrypoint, verify build profile"}
        for row in ENTRIES if row["symbol"] in ("main", "nexs_main_baremetal")
    ]
    entries.extend(
        {"project": row["project"], "path": row["path"],
         "kind": "NEXS_SOURCE_ENTRYPOINT", "symbol": "(top-level script)",
         "notes": "Syntax catalog only; execution side effects need asserted tests"}
        for row in FILES if row["extension"] == ".nx"
    )
    entries.sort(key=lambda x: (x["project"], x["path"], x["kind"]))
    dump_csv(OUTPUT / "EXECUTION_ENTRYPOINTS.csv", ENTRY_FIELDS, entries)
    defined = {(x["project"], x["symbol"]) for x in ENTRIES
               if x["language"] in ("C", "C++")}
    gaps = []
    for proto in PROTOTYPES:
        if proto["symbol"] in {"_Alignof", "offsetof", "DEFINE_SPINLOCK"}:
            continue  # C11/operator/macro false positives from lexical indexing
        if proto["path"].startswith("src/imports/") or proto["path"].startswith("src/filesystem/9p/"):
            continue  # imported Linux snapshots, not public header contracts
        if (proto["project"], proto["symbol"]) not in defined:
            gaps.append({**proto, "status": "DECLARATION_NOT_DEFINED_IN_SELECTED_SOURCES"})
    gaps.sort(key=lambda x: (x["project"], x["path"], x["line"], x["symbol"]))
    dump_csv(OUTPUT / "DECLARATION_GAPS.csv", GAP_FIELDS, gaps)
    by_project = Counter(x["project"] for x in ENTRIES)
    by_kind = Counter(x["language"] for x in ENTRIES)
    by_phase = Counter(x["phase"] for x in ENTRIES)
    by_role = Counter(x["role"] for x in ENTRIES)
    manifest = {
        "warning": "AUTO-INDEXED functions; architecture/semantics require code review.",
        "source_revisions": revisions,
        "total_definitions_or_assembly_candidates": len(ENTRIES),
        "source_files_indexed": len(FILES),
        "entrypoints_or_nexs_sources": len(entries),
        "proposed_test_mapping_rows": len(test_traceability),
        "lexical_call_edge_candidates": len(edge_candidates),
        "prototypes_without_selected_source_definition": len(gaps),
        "by_project": dict(sorted(by_project.items())),
        "by_language": dict(sorted(by_kind.items())),
        "by_phase": dict(sorted(by_phase.items())),
        "by_role": dict(sorted(by_role.items())),
        "python_parse_failures": SCRIPTS,
        "exclusions": [
            "NexsOS1: ONLY targeted read-only NXEXEC/TTY/object/HAL/VFS reference submodules",
            "External Git submodules inside NXOLMO except the separately indexed NXEC",
            "NXOLMO imported reference snapshots are labeled non-product",
            "Linux 9P files remain a strict audit blocker, but are not linked as NXOLMO-native 9P",
            "C function-like macros and prototypes are not function definitions",
            "Assembly labels are lexical candidates, not established callable routines",
            "NEXS script top-level statements are counted in source-file inventory, not functions",
        ],
    }
    (OUTPUT / "INVENTORY_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    print("Catalog and declaration gaps stored under docs/rearchitecture/")


if __name__ == "__main__":
    main()
