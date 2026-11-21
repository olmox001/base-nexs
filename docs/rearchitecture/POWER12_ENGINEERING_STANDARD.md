# NEXS Project Power12: Rules, Enforcement and Qualification

Status: target enforcement specification; EXISTING SOURCE DOES NOT PASS.
Do not call this "NASA Power of Twelve": NASA/JPL published "The Power of
Ten: Rules for Developing Safety-Critical Code" (Gerard Holzmann). P11 and P12
are project-specific additions inspired by NXOLMO, not NASA rules. Distinguish
literal compliance, adaptations, stronger alternatives and deviations.

## Execution contexts

TRUSTED: boot, HAL critical handlers, capability access/revocation, NXREG
commit, WAL recovery, verified bytecode interpreter/verifier, lease pinning,
resource admission and execution authorization. All rules blocking.

RESOURCE_CONTROL: explicit page/arena growth, environment discovery, process
spawning. Resources may be admitted dynamically at declared epoch boundaries,
not implicitly allocated during trusted atomic operations.

LANGUAGE_APPLICATION: user script executed as untrusted metered code; a script
can express an indefinite loop, but each VM execution quantum is bounded and
yields, cancels or reports BUDGET_EXHAUSTED without hanging the engine.

EXTERNAL_SNAPSHOT: NXOLMO imports, Linux 9P reference, NXEC, NexsOS1.
Treat as evidence/source for promotion, never assign invented PASS status.

## Original NASA/JPL Ten and NEXS rules

### P01. Simple control flow; no recursion

NASA: no goto/setjmp/longjmp, no direct or indirect recursion.
NEXS also bans break, indirect/computed goto, hidden recursive serializers,
recursive interpreter/AST free, callback cycles and nonlocal jumps in trusted
code. Use iterative state machines, work queues and continuation frames.
AST/CFG plus transitive call graph SCC analysis is the required gate.

### P02. Bounded loops

NASA: every loop must have a verifiable bound.
NEXS: each trusted loop's bound derives from checked spans, object counts,
queue capacities, resource descriptors or operation fuel. Count nested work,
retries and cleanup. Long-running daemon lifetime is implemented as a sequence
of bounded event-loop steps. No unbounded CAS, sleeping wait or while(1)
in a trusted operation. Gate: CFG loop/bound provenance and fuel fault tests.

### P03. Explicit allocation with bounded critical work

NASA: avoid dynamic allocation after initialization.
NEXS: NO implicit heap, global fixed memory pool, fatal xmalloc, or allocation
inside trusted atomic operations. Reserve page objects and caller-owned
workspace before entry. Application/runtime growth is supported by explicit
metered resource-admission transitions, segmented arenas and page providers.
This is a documented adaptation, NOT literal P03 compliance for all dynamic
application components. Failed admission leaves prior committed state intact.
Gate: call graph allocation effects and OOM/rollback fault injection.

### P04. Short, auditable functions

NASA: about 60 physical source lines maximum.
NEXS: at most 50 nonblank physical source lines per production C/C++ function,
single primary behavior and bounded CFG complexity. Generated code must have
provenance and equivalent complexity checks. Gate: compiler AST and CFG.

### P05. Explicit invariants; no production assert traps

NASA literally PRESCRIBES assertions (at least two per function on average).
NEXS project policy explicitly bans assert, _Static_assert, static_assert,
__assert_fail and hidden fatal assertion macros in trusted production.
These policies CONFLICT. Record the deviation; NEVER falsely report literal
NASA Rule 5 compliance.

Preserve the underlying assurance intent through at least two independently
meaningful invariant/edge tests per critical production function; use
checked preconditions, checked postconditions, typed failures, NXPaX/fault
diagnostics and unchanged outputs/committed state on error. Validate ABI
sizes/offsets in independent/generated build tests, not by silently dropping
the checks. Irrecoverable MACHINE corruption escalates via platform fault.
Gate: compiler AST + ABI tests + invariant/test traceability.

### P06. Smallest scope and explicit ownership

NASA: data declared at smallest possible scope.
NEXS: retire g_registry, g_fn_table, g_array_count, global memory_pool and
mutable hidden singletons. RuntimeInstance, Task, ThreadContext, Lease,
Namespace and Transaction own their state. All mutable shared state requires
a documented concurrency policy and principal isolation. Gate: global-state
inventory, TSan and cross-tenant isolation tests.

### P07. Check all results and arguments

NASA: validate parameters and check non-void return values.
NEXS: typed result; no unchecked read, write, flush, mapping, clone, release,
callback or cancellation result. Check spans/overflow/alignment, generations,
rights, authority, epochs and output behavior. Distinguish EOF, WOULD_BLOCK,
CANCELLED, partial completion and error. Gate: must-use annotations, analyzer,
failure path and commit-boundary tests.

### P08. Limited preprocessor; separate platform source ownership

NASA: minimize preprocessor usage.
NEXS: no production #ifdef, architecture-conditioned #if, or architecture-
specific path embedded in neutral runtime files. Select typed ISA/HAL provider
translation units via the target build manifest; generated public ABI wrappers
can be specialized for C/C++/platform. Include guards are allowed. Avoid
function-like macros with hidden side effects. Gate: preprocessor and
compile-command auditing for each target.

### P09. Restricted pointers and proved lifetime

NASA: restrict pointer complexity and dereferences.
NEXS: never serialize a native pointer, grant authority via object paths, use
unchecked pointer math or share a raw virtual pointer across processes.
Serialized references consist of object ID, generation, lease, offset, span,
schema and capability metadata. W^X, ownership, aliasing, COW and DMA
coherence are enforced at actual map/access/commit boundaries. Gate: AST
pointer inventory, sanitizers and revocation/reuse adversarial tests.

### P10. Strict warnings and independent analysis

NASA: highest compiler warnings and static analysis.
NEXS: -Wall -Wextra -Werror -Wpedantic, Clang analyzer, clang-tidy, cppcheck,
ASan/UBSan/TSan where supported, freestanding ABI builds and two independent
behavioral tests per production function (normal plus negative/edge).
Critical functions additionally require race/crash/replay/fuzz coverage.
No fabricated PASS, blanket suppression, or fixture that checks only prints.
Gate: source-to-test traceability and executed target evidence.

## Project additions, not NASA rules

### P11. Quantifiable operation work and measurements

Inspired by NXOLMO OPERATION_BUDGETS.md. Each material public operation,
trusted internal loop, VM step, buffer lease, object lookup, transaction,
journal step and executor action has a stable operation ID, exact work count
or conservative checked bound, a resource-derived fuel budget, retries,
bytes/items, and cycles when genuinely measurable (UNKNOWN otherwise).
Nested accounting must not overflow. If quantum ends, preserve continuation
or return explicit error WITHOUT visible partial writes. MAX_OBSERVED latency
is not a WCET proof. Gate: operation catalog, forced budget exhaustion,
accounting differential tests and profile overhead benchmarks.

### P12. No arbitrary logical architectural limits

Inspired by NXOLMO ARCHITECTURAL_LIMITS.json. Eliminate NAME_LEN,
REG_PATH_MAX, MAX_FN_DEFS, POOL_SIZE, MAX_ARRAYS, MAX_PARAMS, fixed runtime
stack depth, arbitrary process/thread counts, hardcoded RAM ranges and
unnegotiated command buffers as global product ceilings. Use checked lengths,
resumable traversal, segmented arrays, sparse/ordered indexes, page-backed
growth, scatter-gather, versioned formats and negotiated bounds.
Protocol and hardware normative widths remain exact, and finite resources
produce typed quota/backpressure failures. No physical machine is infinite.
Gate: limits inventory, origin/standard/formula documentation and boundary
tests at/above representational and resource limits.

## Cross-subsystem NEXS invariants (PX)

PX01. Every exposed function, page, array, process, file and mount has one
canonical identity/generation/schema; one authoritative state transition.
PX02. Path/PID/fid/fd/slot is never an independent capability.
PX03. Commit, cancellation, revocation, lease reclaim and transaction
publication have explicit linearization points and hostile-race tests.
PX04. One selected platform backend; no shadow VFS/TTY/registry/kernel.
PX05. Upstream algorithms require license/provenance audit, original tests,
new tests, migrated callers and removal of duplicate production paths.
PX06. Ban silent truncation, hidden downgrade, ambient privilege fallback,
unchecked constructor initialization and success-on-partial-error.

## Practical activation and current blocker state

Base-NEXS make gate today is PARTIAL: selected OS1 files strict, imported
legacy sources warnings. An actual strict-wide invocation was run:

    python3 scripts/quality_gate.py --mode strict-all \
        --report build/assurance/rearchitecture-strict-all.json

Original P0 baseline result: 132 sources, 288 BLOCKING findings, exit 1.
After the P1 compiler slice the source scanner reports 280 Base-NEXS
BLOCKING findings plus 72 unadapted Linux 9P gotos in NXOLMO.
This is the
release-blocking starting baseline, not an exemption or certification.

NXOLMO make check-power-of-ten also fails today due to goto statements
in the unported Linux 9P snapshot src/filesystem/9p/. Resolve through
reviewed, license-compliant algorithm ports, removals or rewritten source;
never hide the snapshot simply to pass an audit.

Activation strategy:
1. In P0, preserve source-level debt and automate AST/CFG, loop, globals,
   pointer, allocation effects and API/ABI inventory. Block every *new*
   violation immediately. Run full strict-release audit as a required,
   intentionally failing gate until the baseline is actually resolved.
2. Each P1–P8 migration transaction must ship the rewritten function,
   impacted call graph, two or more real tests, all-platform compilation and
   strict P01-P12+PX evidence as one acceptance unit. No new warnings/debt.
3. Upgrade lexical scanning to Clang AST/CFG + transitive call graph and
   zero-false-PASS manifest of source/hash/function/test associations.
4. Complete and verify removal of all current Base-NEXS findings plus NXOLMO
   imported-source blocker before declaring full Power12 enabled/green.
5. Record NOT RUN as NOT RUN. Cross-compile, emulator boot, native runtime,
   interactive console and persistence fault testing are different gates.

The two primary executable commands above are real and have FAILED on the
inspected baseline. The specialized Rule 5 conflict and Rule 3 adaptation
must be explicitly disclosed in any future compliance or assurance claim.
