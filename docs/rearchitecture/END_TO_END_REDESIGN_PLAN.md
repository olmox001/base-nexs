# Base-NEXS complete function-level redesign and execution plan

Status: ACTIVE STRUCTURAL MIGRATION; P0–P8 exit gates remain open.
Inputs: FUNCTION_CATALOG.csv, SOURCE_FILE_CATALOG.csv,
DECLARATION_GAPS.csv, EXECUTION_ENTRYPOINTS.csv,
INTEGRATED_ARCHITECTURE.md and POWER12_ENGINEERING_STANDARD.md.

## Authoritative source-wide cutover register (21 November 2026)

The original P0-P8 criteria below remain **blocking**.
[GLOBAL_SOURCE_AUDIT_AND_CUTOVER_2026_11_21.md](GLOBAL_SOURCE_AUDIT_AND_CUTOVER_2026_11_21.md)
maps every Base-NEXS folder to one implementation owner, inventories
NXOLMO/NXEC reference source families and defines dependency-ordered
cutovers T0-T8. This is a part of THIS plan, not a second roadmap.
During structural migrations, test touched components and their
actual dependents instead of recursively running every global suite.
Regenerate source/AST provenance once per coherent tranche. Full hosted,
baremetal dual-ISA and performance gates are for stable cutovers. Stage
verified changes only; no commits/push. NexsOS1 and both references
remain read-only. No unverified HCD, REG or signed-grant implementation
is considered finished.

## Actual structural source cutover and verification gate — 21 November 2026

This is one **existing P0–P8/T0–T8 plan**. No new registry, allocator,
hardware model or alternative 9P filesystem has been introduced. Original
NXOLMO product implementations and their public interfaces were physically
relocated as whole modules, retaining the existing ABI and implementation.

| Source owner now in product | Moved existing source + interface | Actually tested | Required semantic exit still OPEN |
| --- | --- | --- | --- |
| `state/registry/` | StateStore, NXREG, signed-capability adapter, snapshot pool, schema, migration (6 C + 6 H) | NXREG signed masked child/parent reads; million-operation concurrency; schema/snapshot and 34 RegKey integration assertions | Single indexed name/object graph, transactions/MVCC, typed arrays/register push-pull, trusted issuer, revocation and WAL commit |
| `state/authority/` | Signed capability and hardware capability (2 C + 2 H) | Original signed token/authority and hardware capability tests | NXEC kernel issuer/revoker and true MMIO/DMA mapping |
| `state/memory/buffer/` | Buffer manager and capability adapter (2 C + 2 H) | Original MPMC, concurrent leases, 10,000 reclaim cycles | RuntimeInstance page/lease owner, zero-copy lifecycle, pin/revoke and failure isolation |
| `state/storage/block/` | Block device, memory and memory-device (3 C + 3 H) | 24,577 real request/completion pairs, backpressure, fault injection | Live device authority and actually durable flush |
| `state/storage/journal/` | Existing WAL (1 C + 1 H) | Original WAL crash recovery suite; 100,000 media mutations | Atomic NXREG transaction publish and persistent ext4 crash consistency |
| `state/storage/9p/` | Original frame codec (1 C + 1 H) | Actual 9P frame/request tests | Full Plan 9 Chan/Dev/Pgrp and 9P2000.L peer/FID/mount interoperability |

**15 C files and 15 headers moved, not duplicated.** The unchanged
`registry/registry.c`, `registry/view.c`, `registry/reg_ipc.c`, `sys/fd_owner.c`,
`kernel/vfs.c`, `fs/9p.c` and runtime singleton are still live cutover inputs.
No RegKey clone is allowed as a substitute for Plan 9 name/mount semantics.

One `config/sources.mk` catalog selects the moved modules for hosted, OS1,
baremetal and AOT. Separate seL4 single/multi-PD privileged kernel source
lists were consolidated to one manifest variable. The three seL4
secondary-service codegen fallbacks that silently emitted empty source
files were deleted; all four PD compiler invocations now fail closed.
Signed NXOLMO `nx_capability_verify` was previously missing from a direct
RegKey integration link: the real production authority source is now
in the test dependency closure exactly once, with no mock.

One post-cutover AST/catalog reconciliation classifies all 5,351
definition/label candidates, with 2,282 mapped compiler AST bodies,
45 indirect/unresolved AST call nodes and 33 not-yet-qualified target
translation units. These are explicit unresolved gates, not invented
function/target verification; the 29 dedicated catalog/AST/ownership
tests passed after their provenance was refreshed.

**Targeted proof, not release:** hosted product build PASS; actual
NXOLMO resource, block, WAL, StateStore, RegKey integration PASS;
11 focused structural/ownership/recipe tests PASS. The partial Power12
lexical gate still reports WARN=275, BLOCK=0 across 36 strict source files (all
canonical state providers plus OS1 adapters); this is still only lexical
assurance, not full Power12 certification. Full strict-all/AST/CFG audit, seL4 boot, complete NXEC/OS1
runtime and Plan 9 positive tests are **NOT complete**.

### Next mandatory dependencies and positive acceptance criteria

| Step (original phase) | Required implementation in existing owner | Hard exit gate; no negative-only shortcuts |
| --- | --- | --- |
| T1/P1 | Replace active global Buddy/pager/Value/array ownership through one per-RuntimeInstance memory provider, binary-length spans and page leases; do not remove existing features | Two independent concurrent instances, forced OOM with other instance surviving, stale lease and rollback, wide arrays |
| T2/P2 | Migrate runtime/main/eval/repl/TTY/FD into admitted tasks, scopes, cancellable fuel and segmented VM frames; remove host recursion after caller cutover | Real independent interactive sessions, input ownership, cancellation and old/new execution differential |
| T3/P3 | Evolve existing StateStore/NXREG typed identity and name index; explicit signed scope/mask + hardware register geometry; atomic mutations and parent/link relations | Visible/hidden generation checks, concurrent rename/insert/delete, signed grant+revoke, no parallel name tree |
| T3/P5 | Couple the *same* registry transaction to the existing WAL; no rollback fiction for MMIO side effects | Old-or-new publish after crash, conflict rejection, real durable reopen, commit-time authority proof |
| T4/P5 | Actual Plan 9 Chan/Dev/Pgrp, 9P2000.L sessions/FIDs, bind/mount/union and real file/link/device operations via NXREG, never clones | Independent 9P peer; mounted/open channel and unmount races; ordered union, link identity and real provider I/O |
| T5/P4 | Integrate existing REG IPC with NXEC transport, trusted credential transfer, MPMC buffer credits and NXEXEC lifecycle | Two processes, owner-death/cancel/backpressure, real CTTY/FD handoff and no duplicate TTY |
| T6/P6 | One typed/effect IR, verifier, VM and native AOT; preserve source dependency/resource table and original MiniOS | Interpreter/AOT value, array, exception, I/O/failure equality; invalid IR rejection, independent build targets |
| T7/P7 | Port independently validated NXEC HCD/ISA boot admission and runtime device maps, IRQ/timer/DMA grants, fault reporting | Independent HCD parser negative corpus, firmware variants and dual-ISA QEMU/OS1 nxadb |
| T8/P0/P8 | Close function-by-function callers/effects, two independent positive/negative tests per function and every strict Power12/CFG defect; then global qualification | Full AST/link map, per-function evidence, sanitizers/race/crash/ext4/Plan9 interoperability and both real guests |

**Execution order:** continue substantive owner/consumer and ABI cutovers
first; after each coherent implementation do its real positive/negative
targeted tests, targeted compiler/gate checks and stage the verified work.
Rebuild the P0 inventory and traceability once at the stable cutover.
Run expensive global suites only after source placement and functional
migration are no longer moving. No commit or push; NXOLMO/NXEC/NexsOS1
reference trees stay read-only. A source move never closes a phase.

## Coherent structural cutover / completion ledger (current source state)

This is the **same original P0-P8 plan** and T0-T8 register, not another
architecture or an index-only progress claim. Existing upstream NXOLMO/NXEC
algorithms are to be preserved when moved; their complete contracts must be
read and validated, not replaced by smaller invented stubs. All remaining
exits below are BLOCKING, including items with passing component tests.

| Phase, prerequisite | Actual product source / unresolved replacement | Required exit evidence |
| --- | --- | --- |
| T0/P0 first | config/sources.mk, source/AST/CFG ownership; reconcile current staged and unstaged changes; remove repeated Makefile source selection | Every function has owner, callers, ABI, effects, two meaningful normal/negative test IDs, tested profiles and honest NOT RUN; current auto-index alone is insufficient |
| T1/P1 | core/buddy, pager, value, dynarray still contain global pools, fixed Value representation and source-sized limits | Per-instance provider memory, independent OOM, lease/revoke, bounded allocation rollback and no fatal global fallback |
| T2/P2 after P1 | runtime/runtime, repl, lang/eval, parser and fn_table remain single-process or recursive | RuntimeInstance/Task/Scope/Executor ownership, concurrent isolation, cancellation and continuation frames, no unbounded C call stack |
| T3/P3 after P1/P2 | registry/registry RegKey remains mutable pathname/payload projection over NXOLMO StateStore; schema/MVCC/transactions/authority incomplete | ONE indexed NXREG identity/name/payload graph, generation and revision linearization, trusted grant and revocation rechecks, commit atomicity and stale reference rejection |
| T4/P4/P5 after T3 | sys/fd_owner, kernel/vfs, fs/9p, fs/regfs, nxlib/fs and services still implement partial Chan/Dev/FD/FID/mount semantics | Full Plan 9 Chan/Dev/Pgrp, attach/walk/open/read/write/stat/wstat/clunk/flush and bind/union/mount, true independent 9P2000.L interoperability and one capability-checked object identity |
| T4/P5 after Chan | block, journal WAL and NXOLMO ext4 metadata are linked but no complete writable transactional filesystem | ext4 feature/extent/link/rename/xattr/ACL, independent e2fsprogs/fsck and fsync, power loss, complete replay against durable block provider |
| T5/P4 after T2/T3/T4 | registry/reg_ipc, sys/sysproc, kernel IPC and NXEXEC ownership are not a complete REG endpoint | Versioned session/wire protocol, credit, correlation, cancellation/commit, real shared page leases, process/CTTY/FD ownership and permission transfer; no new OS1 syscalls or duplicate TTY |
| T6/P6 after T2/T3 | lang AST evaluator, C codegen, NXB declarations and linker remain different semantic paths | ONE typed/effect IR, verifier, native/portable backends, W^X, signed imports, differential language+failure tests |
| T7/P7 after T1/T3/T5 | HAL/ISA, HCD, hardcoded firmware memory and NXPaX still incomplete | Independent NXEC hardware admission, two firmware maps and two guest ISAs, IRQ/CPU/timer/device grant and nonblocking fault channel |
| T8/P8 after coherent moves | Whole production still has strict Power12 debt and incomplete coverage | No duplicate storage/registry/FD/TTY/compiler; two real tests per function, strict CFG/compilers, sanitizer+race+crash/interop tests and only then full system suites |

### Current safe migration order (no false exit)

1. T3 registry presentation now owns only a read-only view of the EXISTING
   NXOLMO object identity in registry/view.c; list/recursive print are
   iterative, sorted by bounded heapsort, and never expose payload by
   enumeration. Explicit reads still use reg_get. No second registry.
2. T3 registry reference traversal resolves existing canonical object
   references without an arbitrary 64-hop/path-copy limit, detects real
   identity cycles and rejects malformed pointer targets before mutation.
   Script mutation/deletion now report typed failure instead of silent
   success. Full NXEC principal authority is still not present.
3. T4 source staging installs 14 nxlib runtime sources and separately the
   20 external examples plus four image assets in the same OS1 image VFS.
   Only nxlib belongs to the autoload manifest; examples are plain
   filesystem resources at /home/nxscript/examples, never automatically
   loaded into the runtime registry.
4. Next substantive dependency is RuntimeInstance-owned page/Value/array
   state and ONE indexed StateStore namespace. Only after eliminating
   their obsolete consumers can Plan 9 Chan/Dev/Pgrp, REG, ext4/9P,
   hardware grants and verified IR be accepted.
5. Per-tranche verification: inspect real callers/ownership first; compile
   the changed production closure, run focused positive/hostile tests,
   targeted strict Power12/Clang/sanitizers, and cross-ISA compile as
   appropriate. Regenerate inventory/AST/ownership catalogs once when the
   changes settle; stage proven code and docs, but do not commit/push.
   Do NOT repeatedly run recursively all global suites before structural
   moves and their targeted tests are complete.

### T3/T4/T5 acceptance correction: real register machine and Plan 9

The **registry is the NEXS State Node Machine itself**, not a catalogue
layer atop another runtime. NXOLMO's actual hardware-resource registry
and its virtual masked representation must obey the same typed geometry,
rights and lifetime contract. Hardware register extents are discovered
and admitted via NXEC HCD/HAL; no script can declare a physical device
by writing a string. Virtual registers, software objects and resources
use the SAME canonical ID+generation/schema/revision/lifecycle, but
different authority and mapping providers. A virtual mirror is not MMIO.

Each canonical register/resource has typed array and container metadata,
hardware-derived geometry (register width, element encoding, address-space,
offset, length, alignment, supported bit masks/side effects), principal,
domain, namespace, capability lineage/lease/expiry/epoch, operation mask,
resource mask and page/buffer extents. Hardware and protocol field widths
are exact. No arbitrary global logical node/file/register size limits.
Inaccessible objects project zero/absent protected fields without mapping
backing resources or revealing an existence oracle. A zero projection is
never a writable alias of protected memory.

The complete operation path is:
NXREG object ref -> trusted principal + capability/mask admission ->
generation/schema/budget checks -> buffer/array lease -> bounded typed
read or staged write (push/pull, scatter/gather, masked bit operation) ->
conflict/authority/revocation recheck -> WAL transaction commit ->
publish revision/root -> release/scrub. Cross-domain push/pull sends
descriptors and authenticated proposals, never raw pointers or bearer
capability strings. Mask handling MUST honor register-specific reserved
bits, read-clear and write-one-clear behavior; MMIO effects are not
rollback-safe by merely undoing descriptive metadata.

Plan 9 must implement its actual Pgrp/Chan/Dev/devtab, Qid, fid, device
namespaces, /dev, /proc, /env, /fd, /srv, ordered mount/bind/union,
attach/walk/open/create/read/write/stat/wstat/clunk/flush, hardlinks,
symlinks, rename and removal. A directory or file is a *namespace view*
of an NXREG object, not its canonical identity or physical memory.
Mounts/links are versioned namespace relationships to SAME NXOLMO refs,
not cloned Value trees. A link preserves identity/link-count/lifetime;
a mounted Channel owns live provider/namespace rights until unmount
drains and revokes leases. 9P2000.L and the VFS are separate NXREG
clients, not a second filesystem database. Device names such as sd00,
sd01, sd02 and sd99 only exist after real provider discovery and mount.

The current OS1 SDK has capability-scoped FILE operations, but a complete
authenticated NEXS namespace mount API was not found. Until NXEC-issued
namespace grants and an NXREG transaction/commit API exist, user mount,
bind and unmount MUST fail closed, not modify arbitrary RegKeys or
pretend that mounting succeeded. Examples remain external VFS files;
nxlib libraries/services remain installed, not automatically privileged.

#### Concrete source cutover / owner matrix (all exits remain open)

| Product source and entrypoints | Verified gap and mandatory successor |
| --- | --- |
| kernel/syscall.c kmsg_handle, sys_mkdir/sys_bind/sys_mount/sys_remove/sys_fork | Untrusted sender_pid and /proc/PID/caps cannot authorize operations or transfer child grants. Route to authenticated NXEC endpoint and NXEXEC object-capability transfer, no new OS1 syscalls |
| kernel/vfs.c vfs_mount/open/read/write/remove | Former 16-entry mount catalogue did not redirect paths; remove false success. Move to per-domain Plan 9 Chan/Pgrp and typed NXREG access/commit, no independent path/inode authority |
| registry/registry.c reg_mount/reg_bind/reg_unmount/reg_set/reg_get | Old subtree copy/erase and raw writes remain callable from C and still require removal, with all consumers migrated to NXREG name/link/mount/transaction ops before retirement |
| state/registry/state_store.c + state/registry/nxreg.c | H0 supports only object ID and child traversal reads. Implement parent+name index, links, namespace roots, mounted relations, capability-checked mutations, WAL/MVCC and safe retire/reuse |
| registry/view.c + lang/builtins.c + lang/eval.c | Raw registry view and script reg_set/reg_del remain insufficiently masked. Migrate to per-principal NXREG client and redacted file/directory views, including private callables and hardware IDs |
| sys/sysio.c + sys/fd_owner.c | Hosted FILE sessions/global owner; namespace builtins previously cloned RegKeys. Implement Channel/FD ownership, bounded authenticated mount/bind/unmount, shared ref leases and per-task sessions |
| fs/9p.c + state/storage/9p/9p_frame.c + fs/regfs.c | Incomplete 9P2000.L protocol/fids/cancellation, not a real independently interoperable Plan 9 filesystem. Consume NXREG operations rather than duplicate the tree |
| nxlib/fs.nx + nxlib/services/fs/init.nx + nxlib/services/fs/p9_mnt.nx + example/minios/boot.nx | Scripts fabricated sd00-sd99 contents and /sys/fs/mounts. Remove fictional devices, route real operations to provider and refuse mount without authority; examples remain ordinary VFS resources |
| state/authority/hardware/capability.c + state/memory/buffer/* + state/storage/block/* | Real H0 signed range/grant and buffer lease validation, but no trusted kernel issuer, mapped hardware-register geometry, per-page mask or actual push/pull commit. Extend existing providers, not a duplicate software register array |
| hal/* + kernel/cap.c + NXEC HCD | Physical geometry, IRQ/DMA and revocation must derive from independently verified HCD and live kernel authority, never script paths or registry flags |

Required exit tests: unseen/masked non-existence and zero projection;
hardware/HCD geometry vectors; forged PID/path/rights rejection;
link identity and stale generations; ordered mount/union/replace and
unmount with open Channels under race; transaction old-or-new publication,
lease/revoke, signed push/pull and WAL crash recovery; exact Plan 9 and
9P2000.L peer interoperability; OS1 AMD64/AArch64 nxadb guest control,
all strict Power12 rules and two substantive tests per production function.
A negative provider-unavailable test is NOT positive mount/link evidence.
Run targeted product-linked tests after semantic cutovers; full recursive
qualification remains deferred until structural migration is complete.

## Definition of completion: FUNCTION-BASED, NOT FILE-COUNT BASED

Each function-level work item uses its exact catalog ID, source SHA, original
contract, callers, consumed/produced objects, memory/lease ownership,
authority/side effects, operation budget, concurrency model, platform profile,
replacement function, test IDs and disposition (PROMOTED / REPLACED /
DELETED / TEST_ONLY / PROVEN_NOT_NEEDED). All callers and consumers must be
enumerated using AST call graph, linker symbols and runtime object-operation
traces. Generated inventory tags are NOT equivalent to manually audited work.

A slice is accepted only when (1) it makes a coherent subsystem state transition,
(2) old behavior and new behavior were compared on real input including failure,
(3) old production implementation is no longer linked,
(4) the two minimum tests per function plus required concurrency/fault tests run,
(5) strict Power12 is clean in the migrated dependency closure, and (6) target
build/execute evidence exists. No permanent legacy compatibility shim.

## P0. Freeze evidence, find every contract and activate strict gates

Current rolling catalog (21 Nov 2026, source/AST rerun): 4,588 definitions in 613 source files (2,000 project-owned product definitions; 2,002 Clang AST bodies). The immutable original baseline follows.

Original P0 inventory baseline (before compiler rewrite): 4,062 function definitions/assembly candidates across
532 tracked code files, plus 127 source/main entrypoint records (see manifest
for exact counts and exclusions). The catalog includes 1,835 project-owned
production definitions and separate tests, imported snapshots, 168 unadapted
Linux 9P reference definitions, and read-only NexsOS1 comparison code. A textual list is not a verified complete call
graph or semantic audit.

1. Preserve pinned Base-NEXS/NXOLMO/NXEC source revisions and source hashes.
2. Export compile_commands.json per toolchain/profile; parse ALL selected
   implementation and public-header TUs with Clang AST/CFG. Include asm
   symbol boundaries, linker maps, exported symbols and .nx parser AST.
3. Cross-reference every function ID to its callers, direct/indirect calls,
   globals, memory ownership, object reads/writes, exceptions and status
   propagation; manually review high-risk and unresolved callbacks.
4. Build a namespace/schema/operation table for each current API, including
   all standard library and NEXS-language builtins.
5. Add proof-oriented Power12 checks and source-test traceability. Block
   new/reworked violations immediately. Run strict-wide release audit and
   keep all current strict blockers visible until fixed (280 Base-NEXS
   findings and 72 unadapted NXOLMO Linux 9P gotos after the P1 compiler slice).
6. Run baseline language fixture, AOT, REPL, boot, nxadb, NXOLMO and NXEC
   tests and record real outcomes, including NOT RUN and existing failures.
7. Audit NXOLMO imports and NXEC/NexsOS1 reused algorithms for licensing,
   copyright, provenance, appropriate independent tests and incompatible ABI.

Exit: complete source/AST/linker inventory; no unclassified production
function; observable baseline; versioned target ABI; defined project
Power12 profile; no source/ABI contract is silently omitted.

## P1. Replace unsafe data foundations, not only a memory allocator

Base-NEXS core/buddy.c: buddy_next_pow2, buddy_alloc_node, buddy_alloc,
buddy_free_node, buddy_free, xmalloc, xfree, buddy_strdup and die -> replace
global pool and process-fatal allocation with typed page/arena reservation,
per-instance ownership, checked add/rounding and recoverable OOM. Each
allocation has an object owner and generation; resource quotas are derived
from actual provider capabilities, not profile constants.

Base-NEXS core/pager.c: page_alloc/page_free, is_page_ptr, nexs_alloc,
nexs_free -> unify with NXOLMO block/buffer/page leases and NXEC physical/
virtual memory descriptions. No mapping from raw hosted virtual pointer
to unvalidated physical address. Fix hardcoded page/physical assumptions.

Base-NEXS core/value.c: val_str, val_ref, val_ptr, val_clone, val_free,
val_to_int, val_to_float and other value operations -> typed tagged values,
length-bearing strings, explicit ownership, checked conversion/overflow,
capability-aware object references and shared array/buffer views.

Base-NEXS lang/lexer.c: make_tok, lexer_next, lexer_peek, lexer_init_lib;
lang/parser.c: parse_primary, parse_binop, parse_stmt, parse_program,
ast_alloc, ast_free, ast_free_safe. Replace text[NAME_LEN] and recursive
deallocation with source span/token ownership and bounded iterative IR
construction; preserve exact text, line/column and error spans.
Reproduce the current 80-character -> 63-character corruption before fix.

NXOLMO base/types, checked, endian, crypto and runtime/budget -> promote
verified binary ABI, checked arithmetic, digest and deterministic work
accounting into the single NEXS implementation; do not create a second
parallel utility/clock library.

Base-NEXS compiler/driver.c: os1_path_safe, nexs_compile_native,
nexs_compile_file_ex -> eliminate system() shell command construction,
predictable /tmp intermediates, unchecked string append and hardcoded
sources. Use an argv-based launcher, secure temp staging and canonical
source manifest, with proof of source/output path safety across platforms.

Exit: arbitrarily long valid strings/identifiers/inputs are preserved up
to negotiated resources; OOM/overflow never terminates unrelated runtimes;
hostile compiler paths cannot cause shell execution; strict migrated
functions, at least two tests each and independent signed/overflow vectors.

### P1 source-qualified incremental progress, 21 November 2026

The ONE product core/buddy.c now shares its bounded iterative alloc/free
algorithm between the legacy default and explicit caller-backed
NexsBuddyArena instances. The new admission API handles independent
metadata/backing and returns recoverable OOM. Production core/pager.c
rejects wrong-sized or unowned page release BEFORE munmap/bitmap freeing.
Existing nexs_alloc now returns NULL instead of globally halting on
Buddy/page exhaustion; legacy xmalloc call sites still require conversion.
No product _Static_assert remains in Buddy; build-only geometry is checked
in a separate compile test. Tests on 4KiB/8KiB real providers include
14,692 live-extents/canary checks, 1,218 page checks, 38 OOM checks,
63 isolated arena checks and four threaded 5,000-cycle arena workers.
The new owner-backed algorithm is **not yet wired into the actual
RuntimeInstance or kernel HCD provider**. Full P1 and P2 exits remain
blocked by the original requirements above; no reset of those gates.

## P2. New runtime instance, concurrent VM and shared page arrays

Base-NEXS runtime/runtime.c: nexs_runtime_init, nexs_runtime_autoload,
nexs_embed_load_all and platform registration; lang/fn_table.c:
fn_table_init, fn_register, fn_lookup, fn_unref; lang/eval.c: eval_node,
eval_block, eval, eval_str, eval_file, all value and call sites.

Replace singleton context/array/function table with RuntimeInstance,
Executor, ThreadContext, Task, Scope and owned module graph. Replace
recursive C expression/function execution and AST ownership with one
fuel-metered verified IR executor and segmented continuation frames.
All NEXS loops yield at explicit quanta; no global VM lock. Stable symbol
IDs, generational function handles and typed effect/capability metadata
replace integer slot addresses. Nested imports have cycle and budget checks.

Base-NEXS core/dynarray.c: arr_get_or_create, arr_create_anon, arr_ref,
arr_unref, arr_ensure_cap, arr_set, arr_get_at, arr_delete and arr_free ->
versioned object-backed indexed arrays and page-backed chunked views.
NXOLMO src/buffer/buffer.c and capability_adapter.c -> lease/refcount,
map/unmap, checked offset/length, concurrent reader/writer permissions,
COW and safe zero-on-reuse. Preserve buffer manager's existing tested
lifecycle and ring algorithms, then extend to cross-process ownership,
revocation fencing and architecture cache coherency.

NXOLMO runtime operation_catalog, budget, metrics, clock and cycle_counter
-> embed deterministic fuel and measured work into every VM step,
executable call, page operation and data movement. No claim of CPU-cycle
telemetry on unsupported targets.

Exit: multiple isolated runtime instances in one process; real parallel
NEXS tasks on N workers; forced OOM/cancellation/stack growth; no C stack
overflow on deeply nested valid programs; shared arrays/pointers are
generation-safe and zero-copy only when the page lease is valid. Test stale
lease, concurrent mutation, tenant isolation, slow consumer, owner death,
two-minute+ live workload and memory reclaim without crashes.

Power12 strict follow-up for the changed product modules:
kernel/vfs.c and fs/9p.c have zero strict lint blockers after
removing 13 production switch-break statements and verifying real
SEEK_END overflow/underflow. Whole Base-NEXS strict still FAILS with
242 blockers; NXOLMO Linux 9P reference check still FAILS on goto.
No partial lint pass qualifies as the P8 exit.

Verified single-object VFS cutover (P3/P5 still open):
P3_NAMED_VFS_CUTOVER_2026_11_21.md records removal of the global
numeric inode scan/copy, direct NXOLMO file identity/payload revision,
dynamic VFS FD and 9P FID capacities and generation-safe hostile tests.
RegKey sibling lookup, true NXOLMO page data, per-domain handles, mount,
9P2000.L and NXEC authority are NOT resolved.

Plan 9 Chan/Dev, per-domain namespace and NXEC/NXOLMO bit-range contract
crosscheck: P3_P5_PLAN9_NXEC_NXOLMO_NAMESPACES.md. This document
explicitly inventories the still-competing VFS/FD/FID implementations and
cannot be used to claim an implemented P3/P4/P5 cutover.

**Current P3/P4/T6 checkpoint (2026-11-21):** filesystem and registry
are not independent services to be bridged: they are one NXOLMO
StateStore/RegKey object namespace. VFS and nxlib/fs.nx now address the
same direct pathname. The same public reg_path_valid admits paths in
registry, VFS and source bundles, with no alternative path parser.
Hosted and generated AOT loaders admit source
objects there before executing pure root libraries; service scripts
require explicit activation. Direct native 9P/FD and hosted FD owners
still lack common per-domain Chan rights and transactional authority.
No release qualification follows from the dual-ISA 13/13 UART and
real libc boundary tests. The exact regression evidence, prerequisites
and open gates are in GLOBAL_SOURCE_AUDIT_AND_CUTOVER_2026_11_21.md.

## P3. ONE canonical registry + capability-secured object graph

Base-NEXS registry/registry.c: reg_mkpath, reg_lookup, reg_resolve,
reg_set, reg_get, reg_delete, reg_move, reg_mount, reg_bind,
reg_get_deref and the regkey_* helpers -> retire RegKey pointer tree and
redundant path authority after porting all semantics into NXOLMO NXREG.
Replace linked child linear scans with indexed, versioned named children.
Namespace roots are principal/domain scoped; move/bind/mount and deref
protect cycles and revocation. No registry-global mutex hot path.

NXOLMO state_store.c: nx_state_store_create/read/update/delete,
child_first/next/count, snapshot and rehash helpers -> complete named
namespace, invariant-preserving parent/child linking, type/schema policy,
MVCC revisions, version chains, safe reader reclamation, durable commit
and sharded snapshot correctness. H0 read-only nxreg.c and its operation
dispatch -> complete capability-aware create/update/delete/transaction
service with stable opcode and ABI evolution.

NXOLMO schema.c, migration.c, snapshot_pool.c -> versioned schema/manifest,
deterministic migration verifier with fuel, borrowed snapshot leases and
safe retire/replace. No schema validation bypass through legacy write API.

NXOLMO authority/capability.c, hardware/capability.c + NXEC
docs/SECURITY_DOMAINS.md, REG_PROTOCOL.md -> one principal, delegation,
attenuation, revocation, nonce/replay, scope/range and commit revalidation
contract. Do not mistake signed/hashed capability tokens for a kernel-
authoritative capability service. Verification and issuance have separate
trust boundaries; registry lookup never creates rights.

Exit: every NEXS exposed object/function is discoverable via an explicit
schema and reference; all access requires authority; no double registry;
stale snapshots cannot authorize revoked writes; model-checked concurrent
create/rename/reparent/delete, linearizable reference semantics, MVCC
read-at-epoch, migration rollback and generation reuse.

### P3 NXOLMO source implementation progress (2026-11-21; NOT EXIT)

The five real StateStore/NXREG/schema/migration/snapshot-pool source modules
are now compiled and linked into the product build, with an embedded live
StateStore behind RegKey identity, revision, parent and lifecycle operations.
A provider-backed grow API preserves generational references. Seven original
NXOLMO conformance suites plus a new 34-check StateStore/RegKey integration
suite pass. This is NOT P3 completion: named lookup, atomic namespace writes,
schema-gated commits, capabilities, full MVCC, VFS authority and per-instance
state remain open. Full evidence and gaps are in
P3_STATESTORE_CUTOVER_2026_11_21.md.

## P4. NXEC REG transport + NXEXEC unified executor/process tree

Base-NEXS reg_ipc.c: pipe_serialize_value, pipe_deserialize_value,
reg_ipc_enable_pipes, reg_ipc_send, reg_ipc_recv and pending; kernel/ipc.c,
kernel/msg.c, sys/sysproc.c -> retire recursive value serialization,
POSIX-pipe global conversion, process-PID authority and fork assumptions.
Implement NXEC REG envelope, session/version, endian-coded length/offset,
request ID+generation, correlation, priorities, typed result, capability
transfer, bounded credits/backpressure, cancellation and in-flight revoke.

NXOLMO buffer MPMC descriptors -> shared page-lease transport data plane.
NXOLMO NXREG is called by REG endpoint handlers, not replaced by a new
communication registry. Same-process fast calls are direct but enforce
generational capability binding; remote calls serialize descriptors, never
a pointer. Use per-endpoint/per-CPU queues and measured fair scheduling.

NexsOS1 nxexecd service/main.c + SDK execsvc/client.c and execsvc.h ->
adopt owner-preserving spawn, capability transfer, cwd/env/argv, detached
and foreground sessions, stdout/stderr/ctty, FD redirection, terminal
hosting, failure replies, supervision and job control as typed executable/
task/process objects. Do not copy universal EXECSVC_BODY_MAX=4096,
EXECSVC_ARG_MAX=32 or PATH_MAX=128 ceilings into the NEXS runtime.
At OS1, use existing execsvc and nxttyd/CONSOLE object; no new syscalls,
no second TTY controller or keyboard focus claim.

Exit: two independent NEXS programs concurrently exchange large shared
buffers and function objects with correct capability enforcement, both
threads and processes; process crash/restart does not leak pinned pages,
capabilities or child ownership; idempotent request retries preserve
commit results; full PTY/TTY and multi-window input isolation.

## P5. Durable state, unified VFS, ext4 and 9P

NXOLMO journal/wal.c, block/memory.c, block/device.c -> persistent
file/device backends, checked flush and crash consistency, repeat/idempotent
replay, atomic root publication, lifetime-safe page cache, staging/batching
and bounded write amplification. Memory-only fault injection is NOT
power-loss proof.

NXOLMO filesystem/ext4/{superblock,group_desc,bitmap,inode,block_map,
directory,hash}.c -> preserve validated metadata/HTree tests, implement
complete per-feature writable inode/extent/dir operations, write transactions,
rename/link/unlink/truncate/xattr/ACL/quota/inline/casefold/encryption admission
and independent host-tool interoperability. Complete VFS mount namespace,
path traversal, symlink cycle checks, leases, cache invalidation and fsync.
Use NexsOS1 kernel/fs/vfs.c, kernel/fs/ext4.c ONLY as behavior references,
not a second VFS or privileged filesystem implementation.

NXOLMO protocol/9p/frame.c: nx_9p_frame_decode/encode -> full 9P2000.L,
version negotiation, fid lifecycle, Tflush/cancellation, session ACL and
capability mapping, mount/transport and client/server conformance. The
Linux src/filesystem/9p code is currently unadapted and causes the
Power-of-Ten no-goto gate failure; review licensing and port useful algorithms
to clean NXOLMO-native implementation. Base-NEXS fs/9p.c, fs/regfs.c and
kernel/vfs.c -> migrate all consumers to the same NXREG object operations.

Exit: file and registry data survive process kill, unexpected shutdown and
simulated power interruption at every storage boundary; real ext4 images
round-trip with independent e2fsprogs and fsck; 9P interop against independent
client/server; consistent namespace and capability semantics for local,
hosted, OS1 and remote access.

### P3/P4/P5 NXOLMO resource/data-plane source progress (2026-11-21; NOT EXIT)

The actual NXOLMO authority/capability, hardware/capability,
buffer/buffer, buffer/capability_adapter and protocol/9p/frame C providers
are now product-linked, not merely catalogued references. Six original
unit/concurrency suites pass on these product files, including 10,000
create/commit/verify/destroy cycles, 8 concurrent buffer readers and a
4-producer/4-consumer MPMC queue. The Base-NEXS 9P legacy server now uses the
canonical 9P frame decoder and per-message structural validation before
reading any body fields. Tests cover truncation and malformed 9P fields.

Real timed workloads reached 1,000,000,000 validated 9P decoder operations
and 1,000,000,000 in-place bit-mask transforms under a genuine
signed-capability NXOLMO write lease, verified via one commit and independent
result checking. These are **different operation types**: the latter is ONE
commit, not 1e9 commits. Measured rates and reproducibility details:
P8_MEASURED_PERFORMANCE_2026_11_21.md.

**P3/P4/P5 remain OPEN.** The legacy RegKey pointer/name projection, VFS
fd/inode scans and 9P FID table are not replaced by a single capability-aware
NXREG+9P namespace yet; not all device register bit ranges are mapped.
No full memory topology, VFS persistence, WAL, ext4, 9P2000.L or NXEC REG
service is complete. Signed tokens in a test do not establish a trusted kernel
issuance/revocation service. The performance targets are not exit evidence.

## P6. Compiler, linker, module loader, verified VM and self-hosting

Base-NEXS compiler/codegen.c, dep_scan.c, driver.c, targets.h, bitcode.h;
hal/bc/*; lang/eval.c, runtime/main.c. Define one typed IR with precise
source maps, effect system, SSA/region optimization where proven, typed
binary/numeric semantics, host imports and module ABI. Implement a verified
versioned bytecode codec and interpreter that shares the semantics of native
AOT. NXB v1 has a malformed stated header and lacks encoder/decoder.
Do not retain C text generation as an independent semantic backend.

Linking uses the canonical registry's typed function and module objects,
symbol versions, dependency graph, capabilities and relocation metadata.
Code images are executable objects with code-signature/integrity, W^X,
quiescence and revocation policies. AOT targets Mac Mach-O/ELF Linux,
NexsOS1 ELF and standalone freestanding ELF as provided by actual target
toolchains. Host JIT is optional, gated by W^X and platform permission;
JIT support is never assumed for freestanding/OS1.

Self-hosting goals are staged separately: ability to compile scripts on
a hosted OS; link/load verified NEXS modules; execute signed/authorized
object graphs; then optional native on-device compiler if a toolchain and
exec-memory policy genuinely exist. No dependency on shell-based gcc
commands or arbitrary host paths.

Exit: AST evaluator, verified VM, native AOT and linker return identical
typed results/effects for a shared deterministic corpus, including
exceptions, overflow, arrays, threads, registry transactions and IO.
Unsupported targets produce exact diagnostics. Negative bytecode fuzz,
hostile linker relocation and concurrent hot-reload tests pass.

## P7. Unified HAL/firmware/kernel profiles and two-architecture qualification

NXEC preboot/common hcd_builder/hcd_parser/hcd_semantics/hcd_crc64,
preboot/hal/uefi_*, multiboot2_*, isa.* -> validated boot descriptors,
firmware-to-memory normalization, CPU topology, memory plan and ISA resource
admission. Keep independently verifying kernel HCD at trust boundary.
Cross-check accepted/rejected descriptors and fuzz both decoders.

NexsOS1 kernel/HAL/* supplies reference semantics for device enumeration,
timer/interrupt/CPU, buffer and object providers; Base-NEXS hal/amd64,
hal/arm64, hal/riscv64, hal/sel4 and kernel/proc, sched, cap, blk, syscall
must be refactored under one capability resource provider ABI. No
architecturally fixed physical RAM map, static timer frequency or
hardcoded QEMU UART/device path in neutral code.

At OS1 invoke existing NX-SDK object/capability, TTY, VFS, timer and
execution interfaces without kernel modifications. Baremetal profile alone
owns privileged IRQ/MMU/driver capabilities. Hosted macOS/Linux use native
sandbox/process/page providers; seL4 uses actual Microkit capabilities.

Exit: clean host builds, native execution macOS AMD64/ARM64, Linux AMD64/
ARM64, QEMU AMD64/AArch64 kernel-only and full user image, interactively
typed commands, verified NXREG/array/VFS/exec/capability tests via nxadb
both architectures, screenshots/TTY focus checks, resource-enumeration
variations and at least two memory layouts. Separate build-only from run.

### P7 baremetal verification refresh (2026-11-21; NOT EXIT)

Base-NEXS freestanding AMD64 and ARM64 now both compile and boot to a
real QEMU UART REPL. Both guests have been driven INTERACTIVELY over the
actual serial device with checked arithmetic, real registry set/get,
function definition/call and string operation outputs (5, 731, 21, 9).
QEMU interactive reproducibility: test-baremetal-qemu-interactive.
These results do not establish a working NXEC HCD, hardware memory map,
per-CPU timer, full VFS, SMP or the explicitly blocked NexsOS1 OS1 builds.
Actual HAL memory_map providers still use architecture-specific fixed
RAM descriptions. NXEC preboot read-only hardware access policy must
not be misused as the runtime capability issuer or MMIO permission.

## P8. Final integration: performance, safety and removal of duplicate code

1. Verify every generated FUNCTION_CATALOG.csv item has a reviewed final
   disposition and every canonical production function has >=2 real
   independently asserted tests. Parse and execute all 34 .nx files as
   appropriate; no "success" from parse-only tests.
2. Complete strict-wide P01–P12+PX compliance and resolve all original
   Power12/Power-of-Ten failures (or document a genuinely nonproduct
   reference's provenance without claiming it is tested product code).
3. Run ASan/UBSan/TSan, concurrency/linearizability histories, deterministic
   fuzz + coverage fuzz, thousands of forced resource failures,
   service owner death, repeated warm/cold restart, WAL atomicity and ext4
   host-tool round-trip. No fake/skipped checks.
4. Compare baseline and redesigned runtime using identical realistic
   workloads: parse/compile throughput, function dispatch latency, allocator
   scalability, registry lookup/transaction, zero-copy share and COW,
   9P round-trips, ext4 read/write/fsync, concurrent task saturation,
   page/object reclamation, startup, memory peak and worst observed latencies.
5. Ensure no second registry/allocator/TTY/fd/VFS/IPC/compiler runtime is
   linked. Remove superseded code in coherent batches with dependent tests.
6. Final full cold-clean builds, make run and nxadb interactive both targets;
   every script/module and high-risk operation must be exercised. Existing
   unrelated NexsOS1 coreutils failures remain separately reported.
7. After user review, commit/stage/push ONLY the authorized Base-NEXS tree
   with documented gitlink update order; NexsOS1 is not edited by this plan.

Exit: reproducible qualification bundle and per-function traceability;
no remaining known blocker or skipped mandatory target; measured performance
and correctness evidence. "Indestructible" is an engineering target of
fault containment, recoverability and bounded failure—not an absolute claim.

### Adversarial assurance, not Power12-constrained fixture code

Enforce full Power12 in the *production* tree and build. Hostile tests and
fuzz generators may intentionally violate production patterns and must be
treated as untrusted input/adversarial workloads; do not weaken product rules.
Fuzz all 21 current hosted syscall dispatch verbs through direct, real
KernelMsg wire and real REG IPC, plus independent real 9P/VFS commits and
NEXS interpreter execution with explicit success oracles. Record
per-operation latency, histogram, resource deltas, failure seeds and exact
reproduction; no fake operations or simulated success. See
P8_REAL_SYSCALL_FUZZ_2026_11_21.md. This is NOT a claim of whole-kernel guest
execution, full language grammar coverage or all system calls everywhere.

### P8 measured stress in progress (2026-11-21; NOT EXIT)

Reproducible long-running test targets: `make perf-billion-p9` and
`make perf-billion-buffer`; independent structural test:
`make test-p9-request-conformance`. The default fast suite still runs the six
original NXOLMO resource conformance tests. Results, test boundaries,
thread-scaling caveats and pending qualification:
P8_MEASURED_PERFORMANCE_2026_11_21.md. Real syscall fuzz coverage\n(21 verbs, three real call paths, per-op allocator and latency stats,\nplus VFS/9P end-to-end) is tracked in\nP8_REAL_SYSCALL_FUZZ_2026_11_21.md. The OS1 build is explicitly blocked
by user instruction; no OS1 result is inferred. Numeric TFLOPS is NOT
measured by registry/9P or bitmask workloads: these report real operations
per second. Power12 whole-tree, TSan, memory providers and all targets remain
mandatory, not waived.

## Function inventory interpretation

FUNCTION_CATALOG.csv assigns every discovered function/assembly candidate
a stable ID, source/line, project, role, target contract, specific or
subsystem-level required transformation, phase, test gate and lexical signals.
Lexical signals are suspicious patterns, NOT proof of unsafe behavior.
DECLARATION_GAPS.csv flags unresolved prototypes for manual symbol/ABI audit:
current Base-NEXS examples include NXB encode/decode/free, xrealloc, value
comparison helpers and ctx_switch. Some may be optional, assembly-defined or
belong to omitted target units; no conclusion of a linker failure follows
from a declaration-only scan.

Before changing each item, replace AUTO_INDEXED_NOT_INDIVIDUALLY_VERIFIED
with an actual review result. No generated "reviewed" status is accepted.
Systematic AST verification, tests and human review remain mandatory for the
entire product, not merely the curated critical functions in this plan.

## P3/P8 verified execution checkpoint (2026-11-21)

NOT RELEASE READY. NXOLMO StateStore still uses a RegKey legacy name
projection; NXEC trusted hardware grants, full Plan9 9P2000.L driver
namespaces, usermode filesystems, typed returns for every function and
a completely asynchronous runtime are NOT integrated yet. CLI filesystem
enumeration and unified kernel/sysio descriptor ownership remain open.

Verified changes: external installed .nx sources precede bundled AOT source;
fallback only for NX_STATUS_NOT_FOUND and NX_STATUS_NOT_SUPPORTED, never for
denied I/O. Missing freestanding FILE provider explicitly reports ENOSYS.
Generated AOT loaders now use the same eval_file resolver. A live compiled
binary passed three fallback/override scenarios; 22 existing AOT/catalog/
interactive hosted tests and the typed source-catalog test passed.

200 million actual codec calls or bit masks are now the default benchmark
profile. One billion is explicit stress, not a routine test. The product
links the AMD64 invariant TSC provider selected by the real build manifest;
batch samples check CPU migration and do not impersonate per-op latency
percentiles or a billion state commits. Current smoke test (1 million):
9P codec 134.120068 measured cycles/request over two workers;
masked buffer 4.283690 measured cycles/mask; one buffer commit only.
Those are batch averages, not guaranteed device or ISA performance.

Both standalone Base-NEXS AMD64 and ARM64 ELF files crosscompiled; real
QEMU guest UART REPL probes passed 4/4 on each ISA. NexsOS1 build remains
explicitly blocked, and those guest probes do not test NXEC security.

The full strict Power12 check and an independent C compiler warning/conversion
audit are blocking dependencies of make check and make quality-check.
Ordinary make all is a DEVELOPMENT build, NOT a qualification pass. The
compiler audit is limited to hosted manifest TUs; freestanding and OS1
warnings are not implicitly waived. Use make power12-audit-all for evidence.

Verified artifacts:
- build/assurance/p3_source_precedence_regressions.log
- build/assurance/p3_installed_embedded_precedence_regression.log
- build/assurance/p3_baremetal_crosscompile_after_errno_fix.log
- build/assurance/p3_amd64_interactive_after_errno.log
- build/assurance/p3_arm64_interactive_after_errno.log
- build/assurance/p8_p9_cycles_smoke.log
- build/assurance/p8_mask_cycles_smoke.log
- build/assurance/power12_compiler_diagnostics.json

P3/P4/P5/P6/P7/P8 REMAIN OPEN: per-domain mount/Chan/Dev/Pgrp names,
NXEC capability state transitions, HAL physical mappings, usermode ext4
and tmpfs, zero-copy I/O extents, bounded async event loops, whole-language
fuzzing, production Power12 violations and per-function test traceability.


## P5 block source port — Base-NEXS only (qualification pending)
Ported pinned NXOLMO sources src/block/{device,memory,memory_device}.c and
their original unit test into Base-NEXS block/. The implementation uses
caller-owned request and completion queues, selected at runtime by caller
allocation and device capacity, and preserves NX_STATUS_* outcomes. The
original single-owner contract is retained: concurrency and NXEC hardware
authorization are not implied. Replaced the upstream break in bounded pump
with a checked while condition. No NexsOS1 HAL/kernel or references edited.
Product source manifest, P0 catalog and Power12 roots must include block/.
Completion evidence and regression gates must be recorded before P5 exit.


### P5 NXOLMO block provider verification and NXEC trust boundary
Pinned NXOLMO block/{device,memory,memory_device}.c has been ported
to Base-NEXS block/, with no reference object linked. The original 5-part
NXOLMO conformance suite PASS and the independent 8192-cycle adversarial
model PASS (24577 actual requests/completions, 8192 backpressure conditions,
265 crash recoveries, typed fault injection). Block callbacks remain
caller-owned; the sync adapter is a compatibility interface and is not
misrepresented as an async hardware transport. No global lock or second
registry was introduced. The submit boundary must still be wrapped by
NXEC trusted grants (generation, rights, epoch, revoke) and a per-endpoint
single-writer admission lease before it becomes a usermode driver protocol.
Power12 lexical scanner reports no new blockers in block/ sources. The
existing NXOLMO reference remains read-only.
NXEC source inventory now includes boot/ and patch/ in addition to
preboot/, kernel/, userland/ and tools/. That expands discovery, not a
claim to have verified every implementation. Base-NEXS is the only target.


### P5 block port qualification evidence, 2026-11-21
- Original NXOLMO block suite and independent 8,192-round data oracle passed.
  24,577 actual request/completion pairs, 8,192 full-ring backpressure
  events, 265 recoveries, error-injection and no cross-device buffer alias.
- Full hosted make test PASSED after P0 catalogs/AST were regenerated.
  192 Python test cases, 33/33 .nx syntax checks, 17/17 executable examples.
- Standalone Base-NEXS baremetal AMD64 and ARM64 rebuilt and each QEMU
  UART REPL passed four independent input/output probes. No NexsOS1 build.
- Explicit standard performance profile: 200,000,000 real 9P codec
  validations, 65.378 million/sec (8 worker batch, 225.202724
  measured CPU cycles per request averaged across workers); 200,000,000
  signed NXOLMO buffer bit updates, 352.048 million/sec, 6.817044
  measured CPU cycles per mask, ONE successful buffer commit. These
  are NOT 200M independent commits, zero-latency results or TFLOPS.
- Block/ remains single-owner and the namespace, NXEC hardware-capability
  admission, per-operation revocation, usermode driver loading and WAL
  integration are still OPEN. Old Power12/codegen warning debt remains
  BLOCKING: a functional pass is not a safety certification.
- Logs: build/assurance/p5_nxolmo_block_original_suite.log,
  p5_nxolmo_block_adversarial_model.log, p5_block_port_inventory_ast_fixed.log,
  p5_block_full_hosted_suite.log, p5_block_baremetal_both_arch_qemu.log,
  p5_block_perf_standard_200m.log.


### P5 NXOLMO WAL product port — acceptance checkpoints open
The pinned 1,251-line NXOLMO WAL implementation is ported into
Base-NEXS journal/wal.c and journal/include/nexs_journal_wal.h. No
reference object is linked and no second block abstraction was added.
It links the product block/{device,memory,memory_device}.c, shared
SHA-256 and CRC64. The unchanged original eight scenario WAL unit tests
PASS (root integrity, transaction commit/mount/abort, protected journal
region, replay after home write/flush errors, crash-point matrix, superblock
replica recovery, corrupt-log preflight atomicity and overlapping writes).
The original deterministic WAL mutation harness PASS on 100,000 inputs:
39,881 recoverable and 60,119 rejected, without forged success. The
mutation harness tests media corruption, not all adversarial concurrency.
Every source and test is subject to P0 inventory, AST and Power12 before
staging. WAL requires a single owner; protected NXEC grant/revoke and
per-domain lifecycle remain OPEN. Durable semantics were tested against
a RAM block-device emulator, NOT real host ext4/SSD.


### T1 Base-NEXS HAL boundary (structural tranche; P7 STILL OPEN)
HAL module registration now publishes externally admitted, caller-owned
stable entries with a nonwaiting CAS; independent per-device CAS protects
probe/init and avoids a global callback lock. Scripts get metadata only.
Real product-linked targeted tests cover 96 entries, forbidden names,
duplicates, idempotence, nested BUSY and pthread concurrent independent
registration. HALBC now has a single NXOLMO-budgeted runner, no direct
device function pointer table, no unbounded public stepping path, and
denies privileged opcodes before touching hardware. Targeted positive
and negative bytecode tests cover malicious loops and resume.
The kernel NXEC authority, RuntimeInstance ownership, module unload,
HCD, full device access and NXPaX remain open. No blanket P7 exit.


### T1 structural ABI refinement: no hidden legacy HALBC provider
The HALBC stack, program and assembler are now caller-owned checked
spans. A legacy static HALBC_MAX_PROG/STACK_DEPTH limit has been
removed, but the v1 wire *payload length* remains a checked 16-bit
format restriction. All public source callers have been migrated
to typed HALBC init/run/assembly (previous raw entrypoints had no
external product callsites). HALBC has no C-level ISA #ifdef, break,
goto or unbounded loop; HAL module scans are bounded by admitted
atomic entries. A test of this tranche does NOT establish usermode
hardware driver support; only NXEC trusted grant issuance can allow
the currently denied HALBC I/O opcodes.

### MiniOS AOT compiler regression: one runtime binary with real assets

The existing nexs_scan_deps already discovered all the original explicitly
referenced MiniOS dependencies, including three .nxtimg images. The actual
regression was in compiler/codegen.c, which discarded every example/ resource
after scanning. That source-removal logic has been corrected: one executable
contains the entire runtime, original boot script, recursively referenced
script files and the original image data. No new scanner, second executable,
source splicing or runtime service auto-activation was introduced.

The existing embedded resource table and lookup now preserve exact payload
length; source eval and file I/O use their original runtime paths. The
MiniOS boot example was restored rather than disabled. Its example-local
registry storage is distinct from a genuine Plan 9 mount authority, which
remains unimplemented.

Evidence: existing dependency scanner 19 resources, 21 source/data entries
linked in one ARM64 runtime (including otherwise installed runtime libraries);
9,484-byte logo1, 3,580-byte logo2 and 2,450-byte Druglords image all
loaded. A real QEMU ARM64 run emitted logo1 byte-for-byte (normalizing UART
LF/CRLF), reached interactive shell, accepted exit and showed no fatal or
external resource errors. Test: tests/test_aot_compiled_runtime_resources.py.
22 targeted hosted interpreter/source/catalog tests pass. OS1 AMD64/ARM64
compile checks pass; NXREG signed-capability and concurrent tests pass.

Still open: trusted NXEC issuer, Plan 9 namespace mount/link, canonical
transaction commit/WAL, arbitrarily NUL-containing language Values and
real NXEC/OS1 guest boot. Do not treat this example as the VFS implementation.
