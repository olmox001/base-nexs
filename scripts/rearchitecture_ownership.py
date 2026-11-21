#!/usr/bin/env python3
"""Lossless function-by-function ownership and migration ledger.

This script records the chosen canonical component for every original
function/symbol; it DOES NOT mark functions as ported just because they
have been indexed. Only the actual checked/status contract currently has
an implemented, tested migration evidence gate.
"""
from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "docs/rearchitecture"
FIELDS = ("function_id", "source_project", "source_file", "source_line",
          "source_symbol", "original_role", "canonical_role",
          "canonical_module", "migration_state", "target_gate",
          "implementation_evidence")

# These locations are ownership contracts; they do not imply that files
# were copied or that the final source layout already exists.
def target(project: str, path: str, subsystem: str) -> tuple[str, str]:
    if project.startswith("NEXSOS1-"):
        if "nxexec" in path:
            return "NXEXEC", "execution/execsvc"
        if "object" in path or "capability" in path:
            return "NXOLMO", "state/authority"
        if "HAL/" in path:
            return "NXEC", "hal/platform"
        return "NXOLMO", "state/storage"
    if project == "NXOLMO":
        if path == "include/nxolmo/base/endian.h":
            return "SHARED_ABI", "core"
        for prefix, role, folder in (
            ("src/base/", "SHARED_ABI", "core"),
            ("src/crypto/", "SHARED_ABI", "core/crypto"),
            ("src/registry/", "NXOLMO", "state/registry"),
            ("src/authority/", "NXOLMO", "state/authority"),
            ("src/hardware/", "NXOLMO", "state/authority/hardware"),
            ("src/buffer/", "NXOLMO", "state/memory/buffer"),
            ("src/block/", "NXOLMO", "state/storage/block"),
            ("src/journal/", "NXOLMO", "state/storage/journal"),
            ("src/filesystem/ext4/", "NXOLMO", "state/storage/ext4"),
            ("src/filesystem/9p/", "NXOLMO", "state/storage/9p"),
            ("src/protocol/9p/", "NXOLMO", "state/storage/9p"),
            ("src/runtime/", "SHARED_ABI", "core/budget"),
        ):
            if path.startswith(prefix):
                return role, folder
        return "NXOLMO", "state"
    if project == "NXEC":
        if path.startswith("preboot/common/"):
            return "NXEC", "boot/hcd"
        if path.startswith("preboot/hal/"):
            return "NXEC", "boot/hal"
        if path.startswith("kernel/"):
            return "NXEC", "kernel/boot_admission"
        return "NXEC", "boot"
    if project == "BASE-NEXS":
        for prefix, role, folder in (
            ("core/checked.c", "SHARED_ABI", "core"),
            ("core/status.c", "SHARED_ABI", "core"),
            ("core/include/nexs_checked.h", "SHARED_ABI", "core/include"),
            ("core/pager.c", "NXOLMO", "state/memory/provider"),
            ("core/buddy.c", "NXOLMO", "state/memory/allocator"),
            ("core/dynarray.c", "NXOLMO", "state/memory/array"),
            ("core/value.c", "NXOLMO", "state/object"),
            ("core/", "SHARED_ABI", "core"),
            ("authority/", "NXOLMO", "state/authority"),
            ("hardware/", "NXOLMO", "state/authority/hardware"),
            ("buffer/", "NXOLMO", "state/memory/buffer"),
            ("block/", "NXOLMO", "state/storage/block"),
            ("journal/", "NXOLMO", "state/storage/journal"),
            ("registry/reg_ipc.c", "NXEC", "transport/reg"),
            ("registry/", "NXOLMO", "state/registry"),
            ("lang/fn_table.c", "NXOLMO", "state/registry/callables"),
            ("extensions/", "BASE_NEXS", "extensions"),
            ("lang/", "BASE_NEXS", "language"),
            ("compiler/", "BASE_NEXS", "compiler"),
            ("runtime/", "BASE_NEXS", "execution/runtime"),
            ("sys/sysproc.c", "NXEXEC", "execution/execsvc"),
            ("sys/", "NXOLMO", "state/storage/vfs_client"),
            ("kernel/", "NXEC", "kernel"),
            ("hal/", "NXEC", "hal"),
            ("fs/", "NXOLMO", "state/storage"),
            ("nxlib/services/", "BASE_NEXS", "nxlib/services"),
            ("nxlib/", "BASE_NEXS", "nxlib"),
            ("tests/", "VERIFICATION", "tests"),
            ("example/", "VERIFICATION", "tests/programs"),
            ("scripts/", "VERIFICATION", "tools"),
        ):
            if path.startswith(prefix):
                return role, folder
        return "BASE_NEXS", "language"
    return "VERIFICATION", "tests"

def migration(row: dict) -> tuple[str, str, str]:
    project, path = row["project"], row["path"]
    if row["role"] in ("test", "example", "build-tool",
                        "language-application", "language-service",
                        "language-library"):
        return ("SOURCE_TEST_OR_APPLICATION",
                "TEST_REGRESSION_AS_ACTUAL_PROGRAM", "")
    if project.startswith("NEXSOS1-"):
        return ("READ_ONLY_OS1_PLATFORM_REFERENCE",
                "OS1_CROSS_ARCH_QEMU_NXADB", "")
    if "src/imports/" in path or "src/filesystem/9p/" in path:
        return ("IMPORTED_REFERENCE_NOT_PRODUCTION",
                "LICENSE_REVIEW_PORT_AND_STRICT_POWER12", "")
    # Three checked signed-integer helpers were implemented in Base-NEXS
    # after the original NXOLMO base ABI import. They have independent
    # real-source/negative tests but are NOT NXOLMO reference ports.
    if (project == "BASE-NEXS" and path == "core/checked.c" and
            row["symbol"] in {"nx_i64_add", "nx_i64_sub", "nx_i64_mul"}):
        return ("BASE_NEXS_ABI_EXTENSION_TESTED_NOT_NXOLMO_PORT",
                "REAL_SIGNED_ARITHMETIC_AND_SANITIZER_TESTS",
                "core/checked.c; tests/test_checked_contract.c; "
                "tests/test_value_checked_core.c; not in original NXOLMO ABI")
    if project == "BASE-NEXS" and path in (
            "core/checked.c", "core/status.c", "core/types.c",
            "core/result.c", "core/include/nexs_endian.h"):
        return ("PORT_IMPLEMENTED_TESTED",
                "CHECKED_AND_FOUNDATION_TESTS_SANITIZERS",
                "core/checked.c+status.c+types.c+result.c+include/nexs_endian.h; tests/test_checked_contract.c+test_foundation_abi.c")
    if project == "NXOLMO" and path in (
            "src/base/checked.c", "src/base/status.c", "src/base/types.c",
            "src/base/result.c", "include/nxolmo/base/endian.h"):
        return ("REFERENCE_ALGORITHMS_PORTED_IN_FULL",
                "CHECKED_AND_FOUNDATION_TESTS_SANITIZERS",
                "core/checked.c+status.c+types.c+result.c+include/nexs_endian.h; tests/test_checked_contract.c+test_foundation_abi.c")
    # Real compiled implementation ports; P0 per-function/ABI and P1-P3
    # system acceptance remain explicitly unqualified despite unit vectors.
    if project == "BASE-NEXS" and path.startswith("core/crypto/"):
        return ("CANONICAL_CRYPTO_SOURCE_PORT_TESTED_P0_TRACE_OPEN",
                "REAL_CRYPTO_VECTORS_ASAN_AND_PRODUCT_DUAL_ARCH_COMPILATION",
                "core/crypto; tests/test_crypto_canonical_core.c; "
                "NXOLMO reference SHA/HMAC/CRC vectors; 2 tests/function not certified")
    if project == "NXOLMO" and path.startswith("src/crypto/"):
        return ("REFERENCE_CRYPTO_SOURCE_PORTED_IN_BASE_P0_TRACE_OPEN",
                "REAL_CRYPTO_VECTORS_ASAN_AND_PRODUCT_DUAL_ARCH_COMPILATION",
                "core/crypto; tests/test_crypto_canonical_core.c; "
                "referenced upstream source remains unmodified")
    actual_runtime_ports = {"runtime/operation_budget.c",
                            "runtime/operation_metrics.c",
                            "runtime/operation_catalog.c"}
    if project == "BASE-NEXS" and path in actual_runtime_ports:
        return ("CANONICAL_OPERATION_RUNTIME_PORT_TESTED_VM_PENDING",
                "UPSTREAM_RUNTIME_VECTOR_SANITIZERS_AND_REAL_EVALUATOR",
                "runtime/operation_*.c; tests/test_operation_runtime_canonical.c; "
                "tests/test_operation_catalog_canonical.c; verified VM pending")
    actual_runtime_references = {"src/runtime/budget.c",
                                 "src/runtime/metrics.c",
                                 "src/runtime/operation_catalog.c"}
    if project == "NXOLMO" and path in actual_runtime_references:
        return ("REFERENCE_OPERATION_RUNTIME_PORTED_VM_PENDING",
                "UPSTREAM_RUNTIME_VECTOR_SANITIZERS_AND_REAL_EVALUATOR",
                "runtime/operation_*.c; NXOLMO source unchanged; VM pending")
    if project == "BASE-NEXS" and path in (
            "runtime/clock_hosted.c", "runtime/clock_unavailable.c",
            "runtime/cycle_counter_hosted_amd64.c",
            "runtime/cycle_counter_fallback.c"):
        return ("RUNTIME_PLATFORM_PROVIDER_TESTED_NOT_FULL_TARGET",
                "HOST_CLOCK_REAL_AND_AMD64_COUNTER_TARGET_CHECKS",
                "runtime/clock_*.c, cycle_counter_*.c; "
                "tests/test_clock_provider_hosted.c; OS1 cycle reporting unknown")
    # Real, product-linked NXOLMO authority, hardware resource, buffer/lease
    # and 9P frame providers. Six upstream suites and 2 billion-op profiles
    # are evidence of these paths ONLY, not an NXEC contract/VFS cutover.
    canonical_resource_ports = {
        "authority/capability.c": "src/authority/capability.c",
        "hardware/capability.c": "src/hardware/capability.c",
        "buffer/buffer.c": "src/buffer/buffer.c",
        "buffer/capability_adapter.c": "src/buffer/capability_adapter.c",
        "fs/9p_frame.c": "src/protocol/9p/frame.c",
    }
    resource_evidence = (
        "product authority/hardware/buffer/9P sources; "
        "six original NXOLMO unit/concurrency suites; "
        "test_p9_request_validation; "
        "1e9 9P frame decode/validation calls; "
        "1e9 lease-authenticated in-place bit updates with 1 commit; "
        "full NXEC HAL/NXREG VFS 9P namespace cutover P3-P5 pending"
    )
    if project == "BASE-NEXS" and path in canonical_resource_ports:
        return ("REAL_NXOLMO_RESOURCE_PROVIDER_TESTED_FULL_INTEGRATION_PENDING",
                "RESOURCE_CONFORMANCE_AND_P3_P4_P5_EXIT", resource_evidence)
    if project == "NXOLMO" and path in canonical_resource_ports.values():
        return ("NXOLMO_REFERENCE_RESOURCE_PROMOTED_FULL_INTEGRATION_PENDING",
                "RESOURCE_CONFORMANCE_AND_P3_P4_P5_EXIT", resource_evidence)

    # The actual NXOLMO StateStore/NXREG/schema/migration/snapshot-pool
    # algorithms now live in the Base product build, not only in a catalog.
    # Only the source/real regression portion is proven; P3 transactional
    # namespace, full capability and per-function assurance gates remain.
    state_ports = {"state_store", "nxreg", "snapshot_pool", "schema", "migration"}
    base_state_sources = {"registry/" + item + ".c" for item in state_ports}
    reference_state_sources = {
        "src/registry/" + item + ".c" for item in state_ports}
    evidence = ("registry/state_store.c+nxreg.c+schema.c+snapshot_pool.c+"
                "migration.c; tests/nxolmo_canonical (7 original suites); "
                "tests/test_nxolmo_regkey_state_core.c (34 checks); "
                "full P3 authority/transaction/MVCC acceptance pending")
    if project == "BASE-NEXS" and path in base_state_sources:
        return ("NXOLMO_CANONICAL_SOURCE_LINKED_REAL_TESTS_P3_PENDING",
                "ORIGINAL_NXOLMO_SOURCE_CONFORMANCE_AND_P3_EXIT", evidence)
    if project == "NXOLMO" and path in reference_state_sources:
        return ("NXOLMO_REFERENCE_SOURCE_PROMOTED_P3_EXIT_PENDING",
                "ORIGINAL_NXOLMO_SOURCE_CONFORMANCE_AND_P3_EXIT", evidence)
    if project == "BASE-NEXS" and path == "registry/registry.c" and (
            row["symbol"].startswith("regkey_state_")
            or row["symbol"] == "reg_object_read"):
        return ("REAL_STATESTORE_REGKEY_BOUNDARY_TESTED_P3_PENDING",
                "REGKEY_IDENTITY_REAL_TESTS_AND_FULL_P3_EXIT", evidence)

    # Verified integrity prerequisite on the existing registry, NOT a
    # completed NXOLMO StateStore port or an authority implementation.
    # Only functions reached by actual wide/deep/mount/move/IPC tests.
    registry_integrity_functions = {
        "regkey_find_child", "regkey_add_child_tail",
        "regkey_detach_child", "regkey_free_recursive",
        "regkey_release_owned", "regkey_next_preorder",
        "regkey_subtree_relocation_fits", "regkey_update_paths",
        "regkey_path_valid", "reg_mkpath", "reg_lookup",
        "reg_delete", "reg_move", "reg_mount", "reg_unmount",
        "reg_bind", "regkey_paths_overlap", "regkey_mount_path",
        "regkey_mount_one",
    }
    if (project == "BASE-NEXS" and path == "registry/registry.c"
            and row["symbol"] in registry_integrity_functions):
        return ("REGKEY_PROJECTION_INTEGRITY_TESTED_P3_NAMESPACE_PENDING",
                "ACTUAL_REGKEY_CORE_AND_ASAN_AND_CROSS_ARCH",
                "registry/registry.c; tests/test_registry_integrity_core.c; "
                "tests/test_registry_path_runtime.py; StateStore source linked; "
                "P3 named namespace transactions and authority pending")
    if project == "BASE-NEXS" and path == "lang/fn_table.c":
        return ("REGISTRY_CALLABLE_OBJECT_TESTED_P3_EXECUTOR_PENDING",
                "SINGLE_REGISTRY_CALLABLE_TESTS_AND_BOTH_ARCH",
                "lang/fn_table.c, core/value.c, registry/registry.c; "
                "tests/test_callable_registry_ownership.py; StateStore linked; "
                "callable schema/capability executor cutover pending")
    if project == "BASE-NEXS":
        return ("CURRENT_LEGACY_IMPLEMENTATION_UNQUALIFIED",
                "FUNCTION_SEMANTICS_2_TESTS_AND_BOTH_ARCH", "")
    return ("REFERENCE_FUNCTION_NOT_PORTED",
            "FUNCTION_SEMANTICS_2_TESTS_AND_BOTH_ARCH", "")

def main() -> int:
    with (REPORT / "FUNCTION_CATALOG.csv").open(
            encoding="utf-8", newline="") as stream:
        catalog = list(csv.DictReader(stream))
    records = []
    for item in catalog:
        owner, module = target(
            item["project"], item["path"], item["subsystem"])
        status, gate, evidence = migration(item)
        records.append(dict(
            function_id=item["id"], source_project=item["project"],
            source_file=item["path"], source_line=item["line"],
            source_symbol=item["symbol"], original_role=item["role"],
            canonical_role=owner, canonical_module=module,
            migration_state=status, target_gate=gate,
            implementation_evidence=evidence))
    if len(set(r["function_id"] for r in records)) != len(records):
        raise RuntimeError("Duplicate function identity in source ledger")
    with (REPORT / "CANONICAL_FUNCTION_OWNERSHIP.csv").open(
            "w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(records)
    manifest = dict(
        source_definitions=len(catalog),
        mapped_definitions=len(records),
        owner_counts=dict(sorted(Counter(
            x["canonical_role"] for x in records).items())),
        migration_counts=dict(sorted(Counter(
            x["migration_state"] for x in records).items())),
        build_uses_external_nxolmo=False,
        entire_architecture_ported=False,
        canonical_abi_port_tested=True,
        source_read_only_references_retained=True,
        note="Target ownership assigned; NOT semantic source review or full port.")
    (REPORT / "CANONICAL_OWNERSHIP_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
