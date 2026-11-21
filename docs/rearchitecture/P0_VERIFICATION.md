# P0 Source Discovery and Baseline Qualification — Executed Evidence

The counts in the following historical snapshot were recorded before the
P1 compiler rewrite; the reproducible current counts are always in
INVENTORY_MANIFEST.json, CLANG_AST_MANIFEST.json and POWER12_BASELINE_MANIFEST.json.
The rolling generator currently indexes 4,438 definitions/582 files,
1,923 compiler AST bodies and 340 currently unresolved strict issues after
removal of some unsafe compiler-shell operations. The original 360 remain
historical evidence, not proof of 360 unresolved issues today.

This closes the **source-discovery and baseline-qualification work package**,
NOT the complete multi-year Base-NEXS/NXOLMO/NXEC architecture redesign.
Every known result below is a real observed command status. The integrated
runtime, manual semantic review of each function and full Power12 conformity
are NOT complete and are never represented as complete.

## Proven, reproducible P0 outputs

- All 532 selected tracked source files fingerprinted by SHA-256.
- All 4,062 source definitions and assembly candidates assigned source,
  subsystem, role, target contract, planned transformation and phase.
- Project-owned product population: 1,835 definitions; each has two planned
  unique test IDs. These IDs are **requirements**, not implemented tests.
- NXOLMO unadapted Linux 9P: 168 source definitions, explicitly tagged as
  a reference and retained under strict audit.
- Clang AST selected target evaluation: 161 C translation units in scope:
  127 with parsed bodies, 1 feature-disabled, 33 unqualified or external.
- 1,850 actual compiler AST function bodies and 9,284 direct AST call nodes.
  36 indirect/unresolved call nodes retained explicitly.
- Every lexical function now has a row in FUNCTION_COMPILER_COVERAGE.csv;
  compiler tests, imported snapshots, inactive branches and missing toolchains
  are never counted as verified body coverage.
- Power12 release-blocking issues tracked individually, never waived:
  288 Base-NEXS source findings and 72 Linux 9P goto instances.

## Executed verification

| Command | Result | Notes |
|---|---|---|
| make power12-phase0 | PASS | 14 independent Python structural/AST tests |
| make test | PASS | 33 Python tests total; 34/34 NEXS syntax files; 17/17 real example programs |
| make os1-check | PASS | AMD64 and ARM64 OS1 compiler smoke |
| make power12-strict | FAIL, expected | 288 Base + NXOLMO Linux 9P goto blockers; exit code 2 |
| make -C NXOLMO test-unit | PASS | native macOS C11 unit/concurrency/VM tests |
| make -C NXEC/preboot test-host | PASS | firmware/boot descriptor host contract tests |
| make -C NXEC/kernel test-host | PASS | independent kernel admission host tests |
| Python py_compile and git diff --check | PASS | changed P0 tools and tracked modifications |
| NexsOS1 amd64 nxadb ISO start | BLOCKED | Umbrella Makefile cannot include missing doom.mk; no QEMU started |
| NexsOS1 aarch64 QEMU run | NOT RUN | Same makefile dependency stops project compilation |

NexsOS1 umbrella failure is separate from Base-NEXS P0 and was observed
before building or running the guest. Precise requirement:
user/NexsOS1-main/portability/doom/doom.mk is referenced from umbrella
Makefile, but absent in current read-only checkout. Do not work around it
by suppressing DOOM or falsify a successful nxadb test. A separate authorized
umbrella-provider repair is required before guest testing P7.

## Ownership of remaining evidence and remediation

- P1: eliminate fixed pools, fatal allocation, string truncation, unsound
  arithmetic and shell-based compiler tool invocation.
- P2: concurrent instances, safe continuations, dynamic page/array ownership,
  verified VM and explicit language effects.
- P3: canonical NXREG authority and full transactional state engine; remove
  unsafe Base-NEXS RegKey registry and caller-supplied rights.
- P4: NXEC REG messaging and NXEXEC ownership/cancellation/inheritance.
- P5: durable WAL, native VFS/9P/ext4 and imported Linux source audit.
- P6: one IR/bytecode/native compiler and versioned object/function linker.
- P7: seL4 Microkit toolchain qualification, real QEMU AMD64/AArch64 boots,
  nxadb interaction, timing and HAL/device tests; umbrella doom.mk blocker
  is tracked as an external test dependency, not silently waived.
- P8: full system safety/performance qualification and strict-wide Power12
  green. Unresolved indirect calls, header-only functions, inactive code
  variants and all 360 original blockers remain explicitly tracked.

## Source boundaries and staged-change policy

Only the Base-NEXS tree was changed for this phase. NXOLMO, NXEC,
NexsOS1 kernel, SDK, USERKIT and umbrella source code remain untouched.
Base-NEXS pre-existing staged changes (.gitmodules gitlink and gate
maintenance) are preserved. The P0 program/architecture reports are not
a replacement for actual runtime code or evidence from a booted guest.

No committed/pushed source is created by the P0 work package.
