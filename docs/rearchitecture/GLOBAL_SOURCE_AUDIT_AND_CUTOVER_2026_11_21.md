# Base-NEXS: global hardening, source audit and authoritative cutover register

**OPEN: 21 November 2026.** This document expands the existing
[END_TO_END_REDESIGN_PLAN.md](END_TO_END_REDESIGN_PLAN.md), not a second
implementation or a competing plan. **Only Base-NEXS may be modified.**
NXOLMO and NXEC are read-only reference repositories. NexsOS1 is not
the target of code changes or testing. Stage verified work, no commit/push.

## Scope, actual evidence and non-claims

The existing source/function catalog indexes 5,096 definitions/labels
across the product and reference projects, including 2,245 Base-NEXS
functions and 2,115 Clang-verified compiler bodies across selected
profiles. All 5,096 still have status
AUTO_INDEXED_NOT_INDIVIDUALLY_VERIFIED. This does NOT establish complete
semantic/CFG, alias, lock, call-effect or ABI verification, nor two tests
per function. C/H/assembly source crosscheck: **472 files**, comprising
242 Base-NEXS, 170 NXOLMO and 60 NXEC. See FUNCTION_CATALOG.csv,
FUNCTION_COMPILER_COVERAGE.csv, CLANG_AST_MANIFEST.json,
P4_SOURCE_CROSSCHECK.csv and CANONICAL_FUNCTION_OWNERSHIP.csv.
Unqualified compiler targets and indirect callbacks remain OPEN.

Observed reference HEADs: NXOLMO
8b211982ac97a51acc2430e6bde018840c34a31d; NXEC
713f2c605afe6ce18559ed895d6a34cf21051548.
The reference trees were not edited.
Previous strict-all baseline FAIL: 332 blockers. Following the FD T1/T4
source cutover, an actual strict-all rerun still FAILS with **301 blocking
diagnostics**, 197 scanned/strict C source units, zero warnings; the
new sys/fd_owner.c has no blocking violations. This is not certification.
The earlier hosted strict compiler profile reported
FAIL: 11/56 units: core/buddy.c, core/utils.c, lang/eval.c,
lang/builtins.c, extensions/textbuf/textbuf.c, sys/sysio.c,
sys/sysproc.c, runtime/runtime.c, runtime/nexs_line.c,
hal/bc/nexs_hal_bc.c, hal/hal_hosted.c. The previously verified
block/ and journal/ port has zero additional blockers in those results.
No Power12/global P8 completion is claimed.

## Complete Base-NEXS tree owner and removal matrix

| Current tree / implementation | Concrete gap | Single desired owner and required migration |
|---|---|---|
| core/checked, status, types, result, crypto | Imported portable NXOLMO ABI; individual contract and failure-effect traceability unfinished | ONE product-owned checked nx_status/nx_result/crypto ABI |
| core/buddy, pager, alloc, dynarray, value | Caller-owned arena paths coexist with 64 MiB default global pool, hidden page slots and mutable Value lifetime | RuntimeInstance-owned provider, domains, leases, checked OOM and rollback, no fatal global allocator |
| registry/{registry,state_store,nxreg,schema,migration,snapshot_pool} | NXOLMO StateStore imported but RegKey/path/value updates not all schema/capability-committed | ONE NXREG StateStore identity, transactional schema, indexed namespace, epoch and MVCC |
| registry/reg_ipc | Path-based Value queues and hosted pipe serialization | NXEC versioned REG sessions, credits, signed capability references and NXOLMO MPMC buffer leases |
| authority/capability + hardware/capability | Signed token/resource validators exist, no actual trusted kernel issuer, nonce replay/revoker, or hardware mapping | NXEC MACHINE authoritative principal, generation, rights, lease epoch and policy at admission and commit |
| buffer/* | NXOLMO leases/atomic lock ported, not bound to execution/REG/IO lifecycle | ONE buffer/pinned page authority with per-endpoint queues, cleanup and revoke |
| block/* + journal/* | Real ported NXOLMO emulator and WAL, crash/fuzz tests on RAM only | ONE block ABI and WAL, device grant on I/O, real flush/power-loss and cross-domain durability |
| fs/* + kernel/{vfs,vfs_server,blk} | VFS now accesses the same exact RegKey pathname directly, without /vfs/files indirection; live per-domain Chan, FD, 9P and ext4 drivers are still incomplete | ONE Chan/Dev/Pgrp view of the existing NXREG objects, never a second object tree; real 9P2000.L and usermode ext4 require independent interoperability |
| sys/fd_owner.c + sys/sysio.c | Hosted bootstrap owner now uses one dynamic FD slot array and shared refcounted open descriptions; old duplicate nexs_fd.h and synthetic 4096-byte RAMFS removed. Still globally bound; kernel/vfs.c owns another transitional VFS FD provider, and NXEC per-domain Chan transfer is not implemented | Per-domain Chan/open-description owner with cancellation, true VFS identity/generation and no parallel hosted/VFS authority |
| sys/sysproc + kernel/{proc,sched,ipc,cap,msg} | POSIX-process assumptions and baremetal raw-pointer CSpace, not NXEC authority | NXEXEC process/task ownership, transferable capabilities, CTTY/session/FD/argv/env and cancellation |
| runtime/{runtime,repl,main,nexs_line} | Singleton pool, global REPL history, registry-oriented :ls, no isolated per-task budget | RuntimeInstance/Task with fuel/continuations and CLI using the same VFS as syscalls |
| lang/{lexer,parser,eval,builtins,fn_table} | Recursion, static token limits and unchecked registry effects remain | ONE checked IR interpreter with budgeted state and transactional effects |
| compiler/* | Actual hosted AOT, distinct C codegen semantics, target/ELF authority unfinished | ONE typed IR, verified linker/loader and W^X/capability-aware ELF/Mach-O imports |
| extensions/*, nxlib/* and nxlib/services/* | Pure FS APIs now resolve actual /path RegKey objects; privileged services are explicit activation, not auto-execution from an image. Legacy p9_mnt boot fabricates SD/driver objects and remains an OPEN correctness issue | One installed/bundled source namespace; retire fictional P9 device announcements, bind access/mount to actual NXEC authority and supported physical providers |
| hal/module/* | Former global 64-module limit, script probe directly invoked hardware callbacks | Caller-owned module nodes, short atomic publish CAS, per-device probe CAS, read-only script metadata; NXEC driver grant still OPEN |
| hal/bc, hal/common | Legacy unbounded run and raw function-pointer callbacks removed in T1; bounded compute verified but full hardware ABI/ISA and Power12 remain open | Existing NXOLMO fuel budget, privileged opcodes fail closed until real signed NXEC endpoints are present |
| hal/{amd64,arm64,riscv64,sel4,os1} | Fixed QEMU memory maps, ARM FDT handoff/timer gaps, multiple target assumptions | NXEC HCD producer and independent kernel gate, detected CPU/memory/IRQ/DMA, never guessed hardware |
| hal/include, kernel/include, include | Legacy and target headers with mixed pointer/status types | One module-owned ABI with typed error, ownership and platform provider selection |
| kernel/test, tests, scripts | Real targeted coverage; 5,096 functions not individually proven; expensive global tests repeated early | Per-migration hostile tests, AST ownership and source gate; full suites only at stable cutovers |
| docs/rearchitecture | Stale milestone claims and append-only logs | This register + original END_TO_END_REDESIGN_PLAN are the sole transition authority; each PASS names actual binary and oracle |

## T3/T4/T6 tranche — filesystem IS the registry, native source boot (2026-11-21)

**One object and one path:** an executable /nxlib/fs.nx or a data file at
/dir/file is exactly the NXOLMO StateStore-backed RegKey for that path. The
native kernel/vfs.c now validates and accesses the direct RegKey name, not a
parallel /vfs/files tree; nxlib/fs.nx delegates vfs_read/write/ls/exists,
directory creation, copy, move and removal to the existing reg_get/set/del
and keys operations. No /sys/vfs/idx, /sys/vfs/files, BUNDLED marker or
chunk-shard value tree may be resurrected. Ordinary ls and cd resolve the
same scope; empty relative path components no longer manufacture /sys/.
The single exported reg_path_valid is now called by RegKey mutations, native
VFS pathname admission, and source bundle admission. This removed both
competing path validators. The API is syntax validation, never an NXEC
authorization token. Hostile tests exercise exact short and overlong
names, dot/parent components, repeated slash, trailing slash, relative
names and source catalog traversal before any file object publication.
RegKey preflight rejects dot and parent traversal aliases. The 9P codec and
native VFS use the same real StateStore object identity/revision in their
targeted roundtrip test. This is **NOT** a completed domain-authorized
Chan/Dev, ext4, 9P server or NXEC-validated mount facility.

Bundled baremetal images now contain 14 reviewed nxlib source resources
instead of an empty dependency table. Runtime publishes their actual
immutable contents as RegKey strings under /nxlib; generated loader is a
strong ELF symbol and does not silently resolve to the weak no-image stub.
The hosted platform provider consumes the canonical config/nx_sources.txt
manifest in two bounded passes: validate/read/admit real installed sources
into the *same* RegKey tree, then execute **only pure nxlib root libraries**.
The AOT compiler-generated loader invokes the **same product classifier**.
Scripts under nxlib/services are resources requiring explicit activation;
simply linking them does not issue privileges, start auth, or manufacture a
service-ready node. Installed-source fopen/provider failures reject startup
rather than silently replacing a real source with embedded bytes. The
manifest path validator rejects traversal and unlisted startup content.
Read-only RK flags alone are not NXEC trusted authority; hardening remains.

**Diagnosed actual guest defect:** freestanding kernel/libc_stub.c strncpy
wrote a byte past the requested count when NUL-padding a short source.
That corrupted language/state structures and broke nested callable
evaluation. The product libc stub now copies/pads exactly count bytes;
kernel/test/test_freestanding_strncpy.c checks short strings (two separate
canaries), truncation and count-zero, linked with the *same* production libc
and executed on real ARM64 and AMD64 QEMU UART. Both PASS. No host libc
mock is used. ARM64 bootstrap stack is aligned to the existing AMD64
512 KiB boot reservation; guarded per-task stacks and proven worst-case
depth are OPEN. Temporary UART/call traces were removed.

**Executed gates:** real QEMU interactive UART --namespace 13/13 PASS on
ARM64 and AMD64 with compiler-generated shared loader, including nested
language functions, fs existence, cd/ls, exact resources and explicit
non-activation of auth; product C guest libc sentinel 4 independent cases
PASS on both ISAs; hosted manifest/unit tests 5/5 PASS, including real
explicit TTY activation, traversal rejection and missing-file failure;
registry path suite 11/11 PASS. The shared validator refactor
additionally passed hosted manifest 6/6 (including dot/empty path
rejections), real 9P/VFS hostile roundtrip, dual ISA build and real QEMU
13/13 on each architecture. Full native VFS/9P roundtrip and source/AST
phase0 gates were rerun in this tranche, with evidence in ignored
build/assurance logs. No global all-source Power12 PASS, actual NXEC
principal, read/write VFS grant, atomic manifest transaction, safe symlink
openat provider or multi-RuntimeInstance ownership is claimed. These gates
are focused, not a product release qualification.

## T1/T4 tranche — concrete FD ownership cutover (2026-11-21)

The actual sys/sysio.c was separated at the runtime API boundary. The
single hosted descriptor implementation now lives in sys/fd_owner.c;
sys/sysio.c retains script builtins and console/TTY input. The
sys/include/nexs_sys.h public API no longer exposes FILE* or a global
descriptor array; the unused duplicate core/include/nexs_fd.h was
removed, together with its fixed 64-descriptor constant. config/sources.mk
uses the new module, and runtime initialization checks a failed owner
admission instead of silently proceeding.

Descriptor slots dynamically grow using the existing resource provider.
Each slot references a single open description with an atomic borrow
reference and an independent nonwaiting I/O lock. dup/dup2 shares the
cursor and stream. Replacing a target releases its previous description;
close does not fclose/free a still-borrowed stream; the last reference
releases the resource only once. dup(fd,fd) preserves its live source.
Read-only embedded source descriptors share their cursor without a
writable RAMFS shadow. /sys/fd is no longer written as a second
registry/index; fd2path requires a complete destination buffer.

Hosted write-only opens without OTRUNC now use nontruncating r+,
not a mode with forced append, so an explicit-offset write cannot
silently append. The test validates both explicit and sequential writes
against an actual on-disk file. The former fallback which created a
private zeroed 4096-byte 'file' per descriptor on real fopen failure
is removed; unavailable filesystem providers now report failure.

New real production-linked make targets:
test-sysio-fd-owner, test-sysio-fd-owner-sanitized. The C test opens
actual filesystem and pipe resources; checks dup2 replacement, duplicate
alias lifetime and sequential offset sharing, explicit-position read,
real pipe close/read, dynamic 144-FD admission, exact fd2path behavior,
write-only no-append semantics, invalid descriptors/modes, physical
create and persistent readback. Ninety-six pthread read-vs-close
trials accept the two legitimate linearization orders, not mocked
concurrency. ASan+UBSan were executed with macOS leak detection off.
The downstream hosted source catalog, real syscall fuzz and NXOLMO
VFS/9P roundtrip targets also passed after the file move, including
2,400 named resources, 192 VFS FDs/FIDs and 2,048 hostile messages.
The product owner translation unit passed strict Clang -Werror,
-Wconversion, -Wsign-conversion, -Wformat=2 and -Wshadow checks.
Both baremetal AMD64 and ARM64 compile-only gates passed after
replacing unavailable strnlen with a bounded local scan and adding
real ferror status to the freestanding libc's unsupported FILE
provider. **These are build checks, not guest boot or NXEC hardware
security tests.** The original 274-item staged baseline was kept
through the tranche; no commit or push was performed.

**Not yet qualified**: NxEC domain-owned session and per-task current
session selection; actual NXOLMO VFS/Chan as sole pathname authority;
read/write permission and per-domain rights on real resource
admission/commit; create perm and host umask policy; lifecycle teardown
and deferred lease retirement under true concurrent allocator activity;
global bootstrap error string/TTY state migration; buffer/page leases,
stale generation and cross-domain transfer checks; independent guest
boot and power-loss fault injection; complete Power12 strict audit and
two independent tests for every function. A compile-only build does
not qualify these missing features.

## NXOLMO and NXEC: all reference families considered

NXOLMO: src/base and crypto are shared portable ABI/algorithms;
registry/state_store,nxreg,schema,migration,snapshot_pool are imported
but namespace/transaction/MVCC not fully cut over; authority/capability
and hardware/capability validate signed/resource structures but are NOT
a live kernel authority; buffer and runtime budget/metrics/cycle counter
are partially ported, not on every hotpath; block and journal are ported
to Base-NEXS, tested against caller-owned RAM media only; protocol/9p
frame is not a complete 9P2000.L; filesystem/ext4 metadata needs real
writable driver; filesystem/9p and src/imports/Linux snapshots need
license/behavior audit and an explicit clean Power12 port, not direct
linkage. Use original NXOLMO tests as an oracle, not fake product evidence.

NXEC: preboot/common HCD wire, builder/parser/semantics/CRC64,
preboot/hal/UEFI/Multiboot2/ISA and memory-plan logic are producer-side
discovery, NOT a runtime trusted hardware grant. kernel/core/hcd_gate
independently validates immutable admission but must be adapted to
Base-NEXS lifetime and Power12 without importing asserts.
Remaining tracked preboot/, boot/, kernel/, userland/, tools/ and patch/
references are inventoried. SECURITY_DOMAINS.md and REG_PROTOCOL.md
describe future principals/capability delegation/revocation and REG
wire/ring semantics; they are NOT executable finished kernel/REG engines.
Do not invent their completion by interpreting headers as code.

NXPaX from NexsOS1 is a **read-only behavioral reference**. Base-NEXS
has no complete matching system. Port independently validatable, bounded
nonblocking fault/event semantics into Base-NEXS hal/kernel/runtime/sys;
do not link or edit NexsOS1 HAL, and do not log faults by directly
calling a UART driver or writing the registry in exception context.
Separate DIAGNOSTIC_WARNING/ERROR, recoverable fault and MACHINE_FATAL,
with CPU/principal/object generation, operation id, drops and provenance.
Producers never allocate, take sleeping locks, recurse or wait.
NXPaX must never authorize a capability or interrupt handling.

## Trust/lock discipline

MACHINE: only kernel/HAL may hold physical mappings, IRQ, DMA/IOMMU,
root policy and keys. SYSTEM/DRIVER receives explicit scoped grant
only. Name, PID, UID, pointer, registry entry and stale snapshot are
never authorization. Validate token through the live authority resolver
before access and AGAIN at commit; protect revocation linearization
with epoch, quiescence and safe reclaim. Cancellation is not revoke.

Use NXOLMO existing nonwaiting StateStore/buffer manager CAS/lease
operations. HAL module descriptors publish with acquire/release and
per-device state CAS; the catalog publication CAS is never held during
probe/init, blocking IO, fault or user callback. IRQ/fault path uses
atomic nonblocking event reservation, not a global mutex. Runtime,
FD, VFS, REG and journal need domain/object/transaction/endpoint
ownership, not a global lock. BUSY/RETRY must not mutate committed state.
An untrusted module cannot register C function pointers or map memory
by using a script builtin. Per-device "ACTIVE" is idempotent.
Do not allocate inside trusted commit/IRQ boundaries. Caller-supplied
storage has a lifetime contract; nodes cannot be reused before quiescence.

## Cutover execution order (T0–T8)

This order closes dependencies; it does not waive any P0–P8 exit.

T0. Freeze existing staging and reference pins. Classify all 5,096
definitions/callbacks/effects and ABI ownership. Establish one error,
fault and per-function contract. Source indexing alone is not review.

T1. Close unsafe entrypoints (HAL script probe, unverified HALBC CALL,
raw CSpace pointer as grant, unauthenticated privileged RegKey write,
FD shallow dup). For each entrypoint include a genuine negative test.
Do not expose new driver operations until NXEC trusted authorization exists.

T2. Migrate global allocator/pager/Value/AST and VM state to caller-
admitted RuntimeInstance and per-task budgets; remove default singleton
only after all consumers. No unbounded interpreter recursion/hotpath alloc.

T3. ONE indexed NXREG object graph, typed mutation/MVCC/schema, domain
namespace and signed authority; then couple WAL to canonical commit.
Check stale refs, epoch revocation, ABA, OOM and concurrent rollback.

T4. ONE Chan/Dev/FD/TTY VFS with shared open descriptions, no 64-FD
fixed global and no /sys/vfs/files vs /vfs/files split. CLI :ls/cat,
syscalls, nxlib services and exec cwd agree. Then real ext4 usermode
and 9P2000.L providers with host interoperability and power-loss tests.

T5. NXEC REG session/wire, bounded credits and per-endpoint shared-
buffer completion, cancellation and NXEXEC process/CTTY lifecycle.
No duplicate scheduling or new NexsOS1 syscalls.

T6. ONE type/effect IR, verified VM/AOT, signed module/ELF manifest,
W^X, relocation validation and installed-vs-embedded source policy.

T7. Base-NEXS HAL/kernel validated NXEC HCD and firmware topology,
CPU/IRQ/DMA grant, real timer and NXPaX nonblocking diagnostics. Validate
physical mapping on AMD64 and ARM64; OS1 stays explicitly unbuilt.

T8. Two meaningful independent tests per production function and all
13 strict Power12 project rules: AST/CFG/alias/lock/lifetime/compiler
diagnostics plus fuzz/sanitizers. After structural cutovers, run final
full suites, 1/2/4/8/16/32 concurrency scaling, real QEMU dual-ISA,
ext4/9P/WAL interoperability and reproducible performance. Do NOT
repeat full global tests after each file move or claim partial PASS
as P8 certification.

## Per-tranche acceptance, staging and failure semantics

Required: exact source+reference provenance, unique owner, typed ABI,
checked inputs and output-on-error, resource/epoch/lock effects,
two independent positive+hostile tests per high-risk function,
real production link, no duplicate surviving provider, targeted strict
compiler and Power12 tests. Source inventory/AST regenerated once
per cohesive tranche. Stage only the verified code/docs, with clean
index and unstaged diff checks; do not commit/push without permission.
Target-specific QEMU and full suite results must remain NOT RUN until
their scheduled cutover; do not invent PASS. Any unproved hardware
grant, identity binding or runtime instance boundary is BLOCKED.

## Current T1 change: Base-NEXS HAL catalog (NOT P7 completion)

The HAL module metadata catalog now uses externally admitted,
caller-owned HalModuleEntry nodes; a nonwaiting atomic publication CAS,
per-module probe state CAS and immutable node chains; no arbitrary
64-device count limit. Invalid/duplicate/path-injecting names and
overlapping nodes are rejected. The interpreter registers only
READ-ONLY hal_module_list; HAL device probe/init cannot be invoked
as an untrusted script builtin. Actual hosted production-linked
tests cover 96 modules, idempotent init, BUSY on nested or threaded
same-device probe, concurrent independent registration and malformed
arguments. This protects a **transition boundary**, not yet a trusted
NXEC kernel device issuer, a complete usermode loader or safe module
unload/revocation. The temporary singleton catalog must be relocated
to RuntimeInstance before P2/P7 exit; node lifetime is owned by caller.


## T1 HALBC bounded execution (NOT a driver-authority exit)

The old public halbc_step and unbounded halbc_run entrypoints have
become ONE externally reachable halbc_run(state, NXOLMO budget).
A caller-admitted operation budget limits each execution quantum;
BUDGET_EXHAUSTED preserves resumable IP/stack. Invalid jumps, missing
HALT and malformed header/payload lengths fail with typed status.
Unverified HALO_OPEN, READ, WRITE, CTL, CALL, MOUNT and other device
opcodes return ACCESS_DENIED before side effects or fuel consumption.
The old process-global device-function pointer registry and direct
UART dispatch code have been removed rather than left as alternative
bypass paths. An actual product-linked targeted test exercises
finite PUSH/HALT, deliberately infinite branches with 137+13 fuel,
continuations, bad offsets/header, missing HALT and denied raw
hardware instructions with unchanged state. This is **not** a
functioning NXEC-authorized HAL bytecode driver: grant and live epoch,
runtime device isolation, hardware MMIO/IRQ/DMA and trusted loader
remain OPEN. No permissive fallback can be reintroduced.


### Latest T1 HALBC owned-memory and wire-contract refinement
The HALBC stack, program and assembler output are now **caller-owned
checked spans**, with checked disjointness, alignment, integer overflow
and a format-declared unsigned 16-bit payload length (not a guessed
4096-byte runtime quota). No target-specific preprocessor branches or
unmetered public execution entrypoints are left in its product module.
Control flow is a bounded fuel quantum with explicit continuation;
all decoding and jumps are bounds-checked before publication. The old
global device-function pointer table and direct UART path are absent.
A real product-linked test verifies assembled signed 32-bit PUSH,
negative boundary values, stack ownership, invalid headers and jumps,
caller buffer shortage without partial output, and disallowed raw
hardware opcodes without fuel use. Additional targeted Clang strict
and ASan/UBSan tests must pass before staged verification.
HAL module scans are bounded by the atomically published admission
count, not by a new arbitrary hardware ceiling. On concurrent
registration, a listing may return a typed BUSY rather than claim
an inconsistent snapshot. Kernel-grade authority issuance and
module retirement/RCU are still OPEN.
