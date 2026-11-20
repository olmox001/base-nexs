# Base-NEXS validation checkpoint — 2026-11-20

This report distinguishes completed tests from unavailable/unfinished ones.
It is not an assertion of full Power12 compliance or all-platform acceptance.

## Source and Git ownership

- Target branch: os1-support; dev-stable has been merged into the working
  index with no conflicts. The merge is NOT committed/pushed yet, and the
  NexsOS1 umbrella gitlink remains pinned to the earlier revision.
- Old recursive NexsOS1 reference Git submodule removed.
- Exactly one optional Base-NEXS module: external/os1/nx-sdk, pinned to
  f98069bd149caba81c59441a7c57447d847617b4 (SPERIMENTAL-LINUX).
- Embedded build resolves the umbrella system/ SDK; standalone setup uses
  scripts/resolve-sdk.sh --init. No second SDK working tree is created in the
  umbrella. An isolated Git test proved --recursive does not initialize the
  optional module, whereas explicit setup does.
- NX-SDK source and root umbrella source remain unchanged in this task.
  The umbrella build and rootfs were rebuilt for the runtime tests.

## Verified outcomes

| Scope | Verdict | Evidence |
|---|---|---|
| make gate | PASS (stage-1 policy only) | 130 scanned C/header/assembly sources; 4 new OS1 adapter files strict; 0 blocking findings, 258 imported warnings |
| strict-all analysis | NOT COMPLIANT | 284 blockers across all existing source roots; full JSON in build/assurance/strict-all-diagnostic.json |
| Python unit tests | PASS | 17 tests, including negative syntax/error and canonical compiler AOT ABI |
| NEXS-language catalog syntax | PASS | All 34 services, modules and examples parsed with real lexer/parser without execution |
| 11 asserted hosted script tests | PASS | 11/11 verified outputs |
| All 20 examples (functional) | INCOMPLETE | 11 verified, 6 lack assertions, 3 require PTY interaction; make check-examples fails closed |
| macOS amd64 interpreter and AOT | PASS | Mach-O x86_64, compiled and AOT script executed |
| macOS arm64 interpreter and AOT | BUILD PASS ONLY | Mach-O arm64 generated; no native arm64 macOS execution on this host |
| Baremetal amd64 default REPL | PASS | Real QEMU 512 MiB boot and prompt, no fault |
| Baremetal arm64 default REPL | PASS | Real QEMU 512 MiB boot and prompt after MMU/FP correction, no fault |
| Public baremetal compiler AOT amd64/arm64 | PASS | Both generated ELF images ran in QEMU and printed NXSCRIPT_BAREMETAL_SMOKE |
| Native NexsOS1 interpreter amd64 | PASS | nxadb GUI boot, version + interpreted script + screenshot 1280x800 |
| Native NexsOS1 interpreter arm64 | PASS | nxadb GUI boot, version + interpreted script + screenshot 1280x800 |
| Native NexsOS1 compiler AOT amd64/arm64 | PASS | Public --compile CLI, ELF symbols/stack checks, both AOT ELF programs executed in nxadb guest |
| Address/undefined sanitizer (macOS amd64) | PASS | nxscript_dbg executed example/test.nx without sanitizer output |
| Linux amd64 | BLOCKED | Linux cross compiler + libc sysroot unavailable on host |
| Linux arm64 | BLOCKED | Cross compiler present, libc target sysroot/headers unavailable |
| seL4/Microkit | NOT RUN | Microkit SDK/board configuration unavailable |

## Independent system blocker

Both NexsOS1 guest NXTEST suites reported 20/21 with
04nxtest_coreutils status=256. This is an existing system-level failure,
not a failed nxscript interpreter/AOT test. Native nxscript acceptance and the
full system acceptance are reported as separate values in
build/nxadb-os1/{amd64,aarch64}/nxadb-verification.json.

## Work still required before final merge/push

1. Complete real interactive coverage for the 3 PTY/GUI examples and add
   explicit assertions for the 6 other unverified examples.
2. Fix inherited Power12 violations. The strict-all scan's 284 blockers are
   diagnostic, not waived, even though the new OS1 adapter scope is gated.
3. Verify native Linux AMD64/ARM64 with complete target compiler/sysroots,
   and native macOS ARM64 runtime where available.
4. Update the umbrella dependency check in a separate authorized task: its
   current tools/setup-dependencies.sh --check treats deliberately inactive
   nested gitlinks as uninitialized failures. It will additionally report the
   modified Base-NEXS HEAD until its pin is intentionally updated.
5. Before calling the baremetal HAL architecture-independent, replace the
   current QEMU virt fixed RAM/MMIO mapping assumptions with FDT-detected
   ranges, and validate variable memory layouts beyond 512 MiB.
6. Audit the older seL4/Microkit compiler and service path with the matching
   SDK/board, including source catalog completeness and real domain IPC.

No commit/push and no umbrella gitlink update should be presented as
completed while these acceptance gates remain unresolved.
