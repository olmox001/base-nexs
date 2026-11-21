**Canonical source-wide status and transition blockers:**
[Global source audit and cutover register](GLOBAL_SOURCE_AUDIT_AND_CUTOVER_2026_11_21.md).
The [original P0-P8 plan](END_TO_END_REDESIGN_PLAN.md) remains authoritative.

# Complete Base-NEXS redesign study and source-indexed migration catalog

Scope: the Base-NEXS working tree at os1-support; canonical target combines
Base-NEXS language/compiler/executor, NXOLMO state/storage/buffer algorithms,
NXEC capability/REG/boot contracts and NexsOS1 nxexecd/execsvc behavior.
NexsOS1 and all nested external source references remain READ ONLY.
All repository documentation and plans here are in English.

## Start reading here

1. INTEGRATED_ARCHITECTURE.md — one object identity, runtime, callable
   registry, page/array/zero-copy, capability and execution model.
2. CRITICAL_FUNCTION_DECISIONS.md — manually inspected high-risk functions:
   present limits, specific replacement and executable acceptance evidence.
3. END_TO_END_REDESIGN_PLAN.md — phased, dependency-ordered function-level
   implementation and no-regression plan P0–P8.
4. POWER12_ENGINEERING_STANDARD.md — NASA original ten, precise
   project-specific rules 11/12, deviations, blocking gates and tests.
5. FUNCTION_CATALOG.csv — EVERY discovered C/C++/Python/NEXS definition
   and candidate assembly symbol, with owner, source, source line, role,
   target contract, redesign action, phase, testing family, lexical
   candidate findings and review status.
6. SOURCE_FILE_CATALOG.csv — tracked source files with SHA-256, role,
   component, extension and function/label count.
7. CALL_EDGE_CANDIDATES.csv — source-backed lexical call candidate edges;
   names and file candidates only, NOT resolved compiler call graph.
8. DECLARATION_GAPS.csv — prototypes with no matching definition in the
   *selected source inventory*. May be legitimate external symbols, assembly
   or compiler-conditional targets; each requires proof.
9. PROPOSED_TEST_TRACEABILITY.csv — two proposed unique test IDs per
   project-owned production definition (success, fault); ALL labeled
   NOT_REVIEWED_OR_LINKED. These are not test implementations and are NOT
   passed test results.
10. EXECUTION_ENTRYPOINTS.csv — selected C/Python entrypoints plus all
    tracked NEXS-language source entrypoints; top-level service execution
    is NOT established merely by the existence of a file.
11. INVENTORY_MANIFEST.json — exact repository commits, per-class counts,
    exclusions, current index limitations, and reproducible output metadata.
12. CLANG_AST_FUNCTIONS.csv, CLANG_AST_CALLS.csv and
    CLANG_AST_TRANSLATION_UNITS.csv — compiler AST bodies, direct calls,
    indirect calls and fail-closed per-profile target compilation outcomes.
13. FUNCTION_COMPILER_COVERAGE.csv — classification of every source-indexed
    definition; no unavailable toolchain or disabled feature counts as PASS.
14. CLANG_AST_MANIFEST.json — compiler evidence totals and exact exclusions.
15. POWER12_BASELINE_BLOCKERS.csv and POWER12_BASELINE_MANIFEST.json —
    each current strict issue has an owner and planned remediation phase;
    all remain **unresolved and blocking**.
16. P1_COMPILER_SLICE_VERIFICATION.md — actual host AOT process-launch,
    ELF, parallel compilation, QEMU/OS1 results and remaining P1 defects.
17. CANONICAL_ARCHITECTURE_STANDARD.md — normative unified NXEC,
    NXOLMO, Base-NEXS and NXEXEC roles, no duplicate engines.
18. CANONICAL_FUNCTION_OWNERSHIP.csv and
    CANONICAL_OWNERSHIP_MANIFEST.json — exact per-source function
    role, migration status and required gates; NOT completed ports.
19. P1_SHARED_ABI_VERIFICATION.md — 27 actually migrated functions,
    full binary ABI and negative/ASan/OS1/QEMU evidence.
20. P2_CANONICAL_CALLABLE_OWNERSHIP.md — registry-backed callable
    Value ownership, typed resolver, native terminal and text-buffer
    integration tests; not an NXOLMO StateStore port.
21. TEXTBUF_FUNCTION_MIGRATION.csv — every displaced text-buffer
    service function mapped to a single native implementation.
22. P3_REGISTRY_INTEGRITY_PRECONDITION.md — source-level NXOLMO
    comparison, actual RegKey path/deletion/move/mount fixes, wide/deep
    production C and real NEXS behavioral tests. NOT a StateStore port.

23. PRE_P4_HARDENING_CHECKPOINT.md — canonical nxlib with services,
    native C extensions, real PTY/AOT tests, image resource catalog,
    and explicit NXOLMO/NXEC/NXEXEC/9P/Power12 release blockers.

24. P0_P3_EXIT_AUDIT_2026_11_21.md — P0–P3 original exit gates,
    actual evidence, per-subsystem missing contracts and honest stage status.
25. P4_REG_NXEXEC_CONVERGENCE.md — real hosted fork IPC bug and narrow
    P4-A repair; REG/NXEXEC wire/authority remains unqualified.
26. SMD_SPP_V01.md — the user-supplied placement/lifecycle proposal,
    hard constraints, falsifiable math and experimental limits.
27. P4_SOURCE_CROSSCHECK.csv and P4_SOURCE_CROSSCHECK_MANIFEST.json —
    exact Git-tracked source/headers across Base, NXOLMO and NXEC,
    phase/owner decisions without false port claims.

28. P1_BUDDY_PAGER_OWNERSHIP_2026_11_21.md — actual Buddy/Pager
    code hardening, fallible allocation, exact page release, verified
    explicit backing arenas and remaining per-RuntimeInstance migration.
29. LIMEN_SM_V02_VALIDATION.md — attached LIMEN-SM model compared
    against SMD/SPP on shared workloads; files/extents, Fisher–Rao,
    reversible reserves, independent holdout and ablations.
30. scripts/research/limen_sm_v02.py — deterministic RESEARCH ONLY
    simulator; never an alternate production allocator or VFS.

## Exact baseline (2026-11-20 / 2026-11-21 local checkout)

| Reference | Commit | Status |
|---|---|---|
| Base-NEXS | 15ee3c1f4855c4c79820acd63779a999c650bf2b | product to redesign, pending changes held separately |
| NXOLMO | 8b211982ac97a51acc2430e6bde018840c34a31d | read-only source reference |
| NXEC | 713f2c605afe6ce18559ed895d6a34cf21051548 | NXOLMO's pinned nested submodule, read only |
| NexsOS1 umbrella | 5a83b40b659614894e93e9699a8fd4e5bddf028b | read-only reference; subset from pinned CORE/SDK/USERKIT |

Source index from Universal Ctags, Python AST and NEXS declaration scans:

| Inventory measure | Count | Interpretation |
|---|---:|---|
| Source definitions / assembly candidates | 4,438 | lexical/source index after compiler rewrite; NOT manually reviewed |
| Tracked source files indexed | 582 | Base, NXOLMO, NXEC, selected NexsOS1 references |
| Project-owned product definitions | 1,921 | source functions; two planned tests per function (NOT implemented) |
| NEXS language function declarations | 287 | functions in proprietary .nx scripts |
| NXOLMO imported non-product snapshot definitions | 295 | explicitly not adopted into product |
| Unadapted Linux 9P definitions | 168 | separately tracked as an active strict-gate blocker |
| External NexsOS1 reference definitions | 770 | selective comparison, never edited |
| Test function definitions | 857 | source-indexed test declarations, NOT count of passed test cases |
| Entrypoint/source records | 144 | includes C/Python main and .nx script entrypoints |
| Candidate intra-repository call edges | 8,497 | lexical names, MUST be verified by compiler AST |
| Declaration-only candidate records | 64 | not proof of unresolved target linkage |
| Planned production function test mappings | 1,921 | requirements only, status NOT_REVIEWED_OR_LINKED |
| Clang AST function bodies | 1,923 | selected translation units; not manually reviewed |
| Clang AST direct calls | 9,512 | compiler-resolved callee names; cross-file target candidates only |
| Clang AST indirect/unresolved calls | 37 | explicitly identified for later call-graph resolution |
| Strict baseline issues assigned | 340 | 268 Base-NEXS plus 72 NXOLMO Linux 9P gotos; **not fixed** |

Important limitations: the source catalog uses ctags C/C++ function tags,
Python AST, NEXS declaration regex and conservative assembly labels.
C function-like macros, generated/untracked files, linker-only symbols,
function pointer edges, runtime-dispatched imports, conditional preprocess
branches and source outside the selected reference paths need further AST,
linker and runtime verification. A source inventory is not a completed
function-by-function security or performance audit.

## Verification performed on the source baseline

| Check | Observed result | Scope |
|---|---|---|
| Base-NEXS baseline regression | PASS | 19 preexisting Python tests, 34/34 .nx syntax-only, 17/17 executed examples |
| New function/AST catalog tests | PASS | 14 additional Python tests on Clang/inventory/blocker tracking |
| OS1 compiler smoke | PASS | AOT object build for AMD64/ARM64, plus concurrent distinct OS1 ELF builds |
| NXOLMO make test-unit | PASS | local macOS x86_64 unit/concurrency/VM samples; not full test-all |
| NXEC make -C preboot test-host | PASS | HCD, firmware and memory planning host unit tests |
| NXEC make -C kernel test-host | PASS | independent kernel HCD host admission/edge/adversarial tests |
| Base-NEXS quality_gate strict-all | FAIL | 275 blocking findings after compiler rewrite (see current report) |
| NXOLMO check-power-of-ten | FAIL | 72 goto instances in unported Linux 9P source |
| Compiler AST catalog | PASS for selected TUs | 129 selected TUs parsed, 1 feature-disabled, 33 unqualified/imported |
| All-language build graph, indirect calls, Microkit | NOT QUALIFIED | no compiler-level assertion of full program proof |
| NexsOS1 amd64 ISO nxadb start | BLOCKED BEFORE BUILD | Umbrella Makefile requires missing user/NexsOS1-main/portability/doom/doom.mk; no QEMU was started |
| NexsOS1 aarch64 ISO nxadb | NOT RUN | Same umbrella Makefile dependency must be repaired in its own project first |
| macOS/OS1/Linux/QEMU full new architecture | NOT RUN | integrated runtime not implemented; Base-NEXS compiled baremetal QEMU smoke passes both architectures |
| P1 host compiler integration | PASS for qualified targets | Eight dedicated tests; macOS x86_64 native execution, macOS AArch64 Mach-O, OS1 cross ELFs and two real Base-NEXS baremetal QEMU boots |
| Linux AOT target | BLOCKED | Linux AMD64 cross compiler missing; Linux ARM64 C sysroot absent; no fake PASS |
| Power12 AST/CFG/full formal enforcement | PARTIAL | Clang AST source evidence exists; full bounds, indirect-call proofs and semantic review incomplete |

The existing ordinary Base-NEXS gate passes with legacy
warnings in the current selected profile. Its result is NOT full Power12 compliance.

## Reproduction and fail-closed commands

From the Base-NEXS repository root:

    # Only NXEC pinned submodule, not recursive Base-NEXS:
    git -C external/references/nxolmo submodule update --init external/references/nxec

    make power12-inventory       # source-indexed catalog; requires ctags
    make power12-ast             # selected Clang AST target-profile inventory
    make power12-debt            # assign owner and phase to known blockers
    make power12-ownership       # every source function assigned a role (not a port)
    make power12-phase0          # combined inventory, compiler and test verification
    make power12-strict          # intentionally FAILS until full compliance
    make os1-check               # compile OS1 AMD64 and ARM64 AOT smoke programs
    make test                    # existing Base-NEXS regression suite

The strict command intentionally fails until the baseline is actually
repaired. It does not prevent running the current interpreter regression
suite during the gradual rewrite. Once a coherent production slice migrates,
its dependency closure MUST pass strict checks. A future full release gate
must also enforce P01–P12 via real compiler AST/CFG and test traceability.

## Important source findings requiring immediate attention

- Base-NEXS registry/registry.c: reg_set assigns caller-provided rights,
  reg_delete checks no calling capability, reg_set_ptr requests RK_ALL.
  Do not expose this registry as a protected cross-process authority API.
- Core buddy/page provider: fixed singleton memory remains a release blocker.
  The earlier MAX_ARRAYS duplicate name table has already been removed.
- Lexer: original 63-byte string and path truncation has been fixed and
  tested, but the registry still limits component names via RegKey.
- Evaluator/AST/IPC: recursive C execution and serialization, no global
  resource/fuel accountability.
- Compiler driver: original shell-based AOT and predictable temp paths
  have been replaced with an argv-driven isolated hosted compiler.
- NXOLMO: H0 typed State Store, read-only NXREG, buffer manager and WAL
  are useful implementation inputs; full transactional namespace, MVCC
  persistence, writable VFS/ext4 and 9P are incomplete.
- NXEC: working HCD/preboot/kernel admission code; REG/kernel authority/
  scheduler architecture largely specification-only at pinned revision.
- NexsOS1 nxexecd: real broker semantics to preserve selectively, not
  an acceptable universal command/argv/path capacity definition.

## Git and edit scope

Design artifacts and generator are added under Base-NEXS ONLY.
A non-disruptive Makefile target activates observable fail-closed strict
baseline checks; it does not replace the existing make gate or make test.
NXOLMO, NXEC, NexsOS1 CORE/SDK/USERKIT and umbrella tracked sources were
not modified or committed. Previous staged Base-NEXS submodule/quality-gate
changes are preserved. No commit, push, umbrella gitlink adjustment, new
kernel syscall, or claimed tested target implementation is part of this study.

### Reproducible reference dependency inventory

The full source/header/assembly SHA and include index is
REFERENCE_IMPLEMENTATION_DEPENDENCIES.csv, regenerated by
scripts/rearchitecture_complete_reference_scan.py. Its lexical entries
are NOT final call-graph or individual function proof. The original
END_TO_END_REDESIGN_PLAN.md gates remain authoritative.

- [P3 live NXOLMO StateStore cutover](P3_STATESTORE_CUTOVER_2026_11_21.md)
