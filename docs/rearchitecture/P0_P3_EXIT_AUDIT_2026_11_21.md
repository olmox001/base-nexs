# P0–P3 exit-gate audit — 21 November 2026

**Verdict: P0, P1, P2 and P3 do not satisfy their original full exit criteria.**
Do not equate successful unit/host/guest test slices with the completion of all functions in each architectural phase. This audit is grounded in the actual Base-NEXS source, the original END_TO_END_REDESIGN_PLAN.md, the read-only NXOLMO/NXEC repositories, and the user-supplied SMD/SPP research text.

## Actual accepted baseline

The prior tested slice passed 147 Python test cases, 33 NEXS syntax files, 17 executable NEXS examples, 8,798 registry integrity C checks, OS1 AMD64 and ARM64 compiler checks, two independent AOT checks, real QEMU AMD64/ARM64 baremetal and two real hosted PTYs. This proves those stated behaviors, not the overall architecture.

Current inventory: 4,588 source-indexed definitions from 613 files, 2,000 Base-NEXS product functions and 2,002 compiler AST bodies. The 37 indirect/unresolved calls, inactive/unsupported compiler profiles, lack of complete linkage/authority effects and two tests per every function are still open. The full Power12 strict gate FAILS: 333 reported issues (261 Base-NEXS, 72 reference Linux 9P). No waiver applies.

Full OS1 umbrella make run plus nxadb on both ISA remains NOT EXECUTED, previously blocked by the read-only umbrella's missing user/NexsOS1-main/portability/doom/doom.mk. Reference NXOLMO and NXEC sources are not rewritten by Base-NEXS testing.

## Phase exit matrix

| Phase | Intended exit requirement | Grounded evidence | Verdict |
| --- | --- | --- | --- |
| P0 | Complete AST/CFG/asm/linker and caller/authority/effects graph, exact ABI, per-function traceability | Generated candidate/function/file/AST/provenance indexes with 25/25 inventory/crosscheck tests | PARTIAL; full indirect graph, manually reviewed effects, asm/linker maps, target coverage and OS1 runtime missing |
| P1 | No global allocator, fatal OOM, fixed logical limits; typed owner/budget for every resource | 27 tested NXOLMO base ABI functions, checked lexer/arrays, safe hosted AOT | PARTIAL; core/buddy.c global 64 MiB memory_pool and xmalloc fatal OOM, core/pager.c, Value/AST lifetime, provider admission unresolved |
| P2 | Multiple isolated runtime instances, preemptible fuel VM, concurrent tasks and safe page leases | Canonical /sys callable resolution, native text-buffer and registry-owned arrays | PARTIAL; RuntimeInstance, verified IR, segmented continuations, proper NXOLMO buffer/lease/ring and operation budget absent |
| P3 | ONE canonical capability-secured NXOLMO StateStore and NXREG transaction engine | RegKey integrity/path/subtree mutations verified by 8,798 C checks | PARTIAL; RegKey/g_registry is still the only actual store; no authenticated generation/MVCC/schema/revoke/commit authority |

**P4 may fix independently testable local prerequisites but cannot qualify protected distributed operations while P1–P3 invariants remain absent.** Privileged operations must fail closed until real generation+capability authority exists.

## Function-level preservation map: Base-NEXS / NXOLMO

| Canonical product callsites | Reference algorithm owner | Missing functional integration |
| --- | --- | --- |
| core/buddy.c, core/pager.c, core/dynarray.c | NXOLMO src/base/checked.c; src/buffer/buffer.c and capability_adapter.c; src/runtime/budget.c | Explicit per-instance resource provider, leases, refcount, ring, checked page and extent admission; forced OOM and stale lease |
| registry/registry.c, core/value.c | NXOLMO src/base/types.c; src/registry/state_store.c, nxreg.c, schema.c, migration.c, snapshot_pool.c | One indexed identity/generation store, schema, MVCC, authoritative principal/rights, atomic commit, revocation |
| registry/reg_ipc.c, kernel/ipc.c, kernel/msg.c | NXOLMO src/buffer/buffer.c MPMC; src/authority/capability.c; operation_catalog.c | NXEC REG wire, session, bounded credits, typed correlation, cancellation and capability-checked buffer references |
| sys/sysproc.c, sys/sysio.c, runtime/runtime.c | NXOLMO runtime clock/cycle_counter/metrics/budget | NXEXEC-owned process/session, CTTY/FD/cwd/env/argv inheritance, cancellation, no global PID shortcut |
| fs/9p.c, fs/regfs.c, kernel/vfs.c | NXOLMO src/protocol/9p/frame.c and unported Linux 9P source | Full 9P2000.L codec/FID/session/Tflush, VFS authority and interoperability, no second VFS |
| fs/fat.c, kernel/blk.c | NXOLMO ext4 source tree, block device, journal WAL | Transactional VFS, correct writable ext4, flush, crash replay and independent fsck tests |
| hal/*, kernel/cap.c | NXOLMO hardware/capability.c and NXEC HCD / boot / ISA providers | Actual kernel authority, firmware memory map and IOMMU/DMA control, dual-ISA validation |
| compiler/*, lang/*, extensions/*, nxlib/* | NXOLMO registry migration and NXEC module/execution contracts | Verified IR/VM/AOT, consistent module capability/versioning, no independent linker semantics |

## NXEC, SDK and USERKIT sources never to omit

NXEC preboot/common: HCD wire, CRC64, parser, builder, semantics, section catalog, memory-plan algorithms. NXEC kernel/core/hcd_gate and independent parser must verify at a separate trust boundary. NXEC preboot/hal: UEFI and Multiboot2 memory maps, AMD64/ARM64 ISA CPU/timers and admission. REG_PROTOCOL.md is explicitly DESIGN / FORMALIZATION REQUIRED: its wire layout is NOT yet fixed, so compatibility cannot be invented. SECURITY_DOMAINS.md specifies conceptual domain delegation/revocation, not an already proven kernel implementation.

NexsOS1 USERKIT/SDK reference: nxexecd execsvc service; execsvc client/foreground/owned/detached, argv/env/cwd/ctty/FD and failure replies; nxttyd with CONSOLE object. Source comparison does not authorize a second process broker, TTY manager or new syscall.

NXOLMO read-only references include SHA256, CRC32C/64, block memory/device, WAL, Ext4 metadata/HTree and Linux 9P code snapshots. Each reference C source has a specific disposition in P4_SOURCE_CROSSCHECK.csv. The manifest marks unported references as UNPORTED/REFERENCE, not falsely PORT_IMPLEMENTED_TESTED.

## P4 and SMD entry rules

NXREG holds the single canonical name/object identity. The kernel/provider validates authority, principal, rights and revocation at protected access AND commit. A RegKey path, PID, FD, software rights byte or unverified cached snapshot is never authorization.

SMD/SPP in SMD_SPP_V01.md is an experimental resource placement/lifecycle policy above existing providers, not a parallel VFS, registry or RAM allocator. It cannot migrate pinned/DMA/active pages or publish state without valid provider and capability proof. Simulation results are NOT a kernel validation.

Actual evidence categories: PASS_TARGET, PASS_UNIT, UNQUALIFIED_TARGET, FAILED_GATE, NOT_IMPLEMENTED, REFERENCE_ONLY. Neither a manifest entry nor a passing neighboring function implies implementation.

## Independent read-only source tests executed during P4 audit

The NXOLMO test-unit suite returned PASS, including actual concurrent
buffer-ring MPMC (four producers × 2,000 items, four consumers),
registry rehash readers (eight × 4,000), concurrent buffer leases
(eight × 1,000), and the virtual-memory example driver.
NXEC preboot test-host returned PASS for HCD/UEFI/Multiboot2/physical
memory-plan host tests, and NXEC kernel test-host returned PASS for
independent kernel HCD validation and parser adversarial cases.

These are REFERENCE tests only. The Base-NEXS product has not thereby
ported or secured NXOLMO StateStore/buffer/capability or NXEC HCD/REG.


## Source inventory update after P4-A

The regenerated current source index contains **4,438**
functions/labels across **582** files, **1,921** source-indexed
product functions and **1,923** compiler AST bodies. Indirect
call resolution and two independent verified tests for every
production function remain open. The separate C/H/assembly
Git-tracked cross-reference enumerates **388** files
(158 Base-NEXS, 170 NXOLMO, 60 NXEC), with zero
unclassified entries; this is a classification, not a manual
semantic verification of every source.

## Revised P1/P2 dependency status after first owned-domain implementation

The ONE product Buddy algorithm now has caller-supplied
NexsBuddyArena backing and tree geometry. Tests establish
independent ownership of two arenas, cross-arena free refusal,
actual four-pthread execution over four independently owned arenas
(5,000 allocation/release cycles per worker), and 65 exact
isolated-arena assertions. The old global memory_pool/buddy_tree
is still used through a default adapter by nexs_runtime_init and
the language/VM; this is NOT migration of the runtime to independent
instances. P1 remains OPEN until this adapter, fatal legacy xmalloc
and process-global pager slots are replaced by admitted owners.
P2 remains OPEN until the VM, task, arrays and all service objects
actually run under isolated multi-instance schedulers with fuel,
leases, cancellation and concurrent mutations. P3 remains OPEN
until NXOLMO StateStore and authority own real object identity
and every protected operation. P0 remains OPEN until all function
semantics, indirect call targets, ABI and test traceability are
checked, including full OS1 nxadb guest qualification.

LIMEN-SM model and holdout measurements are tracked in
LIMEN_SM_V02_VALIDATION.md; they are not a way to waive these
architecture prerequisites. The source reference NXOLMO/NXEC
remains read-only.

## Verified incremental P1 work after this exit audit

The final regenerated inventory is **4,438 definitions from 582 files**
(1,921 Base-NEXS product definitions, 1,923 Clang AST bodies; 37
indirect/unresolved call nodes still require review). The file loader in
`lang/eval.c` now uses fallible source admission with exact release and
rejects non-seekable/short-read inputs; hosted `core/pager.c` no longer
forges physical frame records from virtual `mmap` pointers. Real large
source, FIFO, and hosted physical-identity regression tests were added.

Final `make test` PASS 184/184 Python cases and 17/17 executed examples;
P0 inventory PASS 25/25; hosted pager 1,224, Buddy 14,692, registry
8,798 and recoverable allocation 38 checks PASS; selected ASan/UBSan
PASS; both baremetal AMD64 and ARM64 QEMU UART runs PASS; OS1 dual ISA
compiler PASS. Strict Power12 still FAILS with 340 assigned issues.
This narrows P1 defects but changes none of the P0/P1/P2/P3 phase-exit
verdicts above. Full NexsOS1 umbrella make-run plus nxadb remains
unqualified, and no NXOLMO/NXEC protected ownership has been ported.


## New verified code and source-closure checkpoint (21 Nov 2026)

The generated P0 run now measures 4,537 source/assembly candidates
from 597 files, 1,961 product definitions and 1,963 compiler AST bodies.
The strict-all baseline still has 333 unresolved assigned issues
(261 Base-NEXS; 72 NXOLMO imported Linux 9P). All 37 Clang-reported
indirect/unresolved calls remain open until reviewed against callsite type,
possible targets, linker map and independent ABI tests. No automatic
candidate attribution is considered evidence of a resolved target.

All C, header, assembly and include-source files in the scanned Base-NEXS
product tree, NXOLMO src/include, and NXEC preboot/kernel source closures
were opened as bytes and SHA256 indexed by
scripts/rearchitecture_complete_reference_scan.py. The reproducible
REFERENCE_IMPLEMENTATION_DEPENDENCIES.csv currently lists 173 Base-NEXS,
119 NXOLMO and 62 NXEC source/header/assembly records (354 total);
each records included headers, textual candidates and pending semantic
review. It is NOT a claim that Clang covered every target, that every
source has been semantically verified, or that excluded firmware SDK/user
references were ported. The original P0 AST/linker/function-by-function
and two-independent-real-tests-per-function gates remain OPEN.

A real NXOLMO cryptographic subsystem dependency closure was migrated into
the SINGLE Base-NEXS core:
- core/crypto/crc32c.c, crc64.c + crc64_table.inc, sha256.c;
- core/include/nexs_crypto.h uses the product's existing
  nexs_checked.h and nexs_types.h ABI; no second span/status/digest system;
- all hosted, AOT, OS1 and baremetal source lists include these same
  production files via config/sources.mk;
- tests/test_crypto_canonical_core.c includes the original NXOLMO SHA256
  streaming/lifecycle/alias, HMAC, CRC64 test vectors and additional CRC32C
  valid/invalid-boundary checks;
- the portable software implementation avoids a separate macOS
  CommonCrypto dependency, and the compiled real source passed normal
  and ASan/UBSan tests.

This is a product source port of cryptographic primitives, NOT a migrated
NXOLMO capability authority, NXOLMO StateStore, NXEC firmware HCD gate or
authenticated transactional registry. Retain the original P0–P3 exit
criteria without narrowing them. The existing global Buddy, pager
page-table, RegKey, recursive evaluator and runtime singleton still
require full ownership/authority and VM migration. No source in the
read-only reference repositories was modified.


## NXOLMO operation-runtime port and Clang-typed indirect callback audit

Latest regenerated inventory: **4,588 definitions / 613 files /
2,000 product functions / 2,002 Clang compiler bodies**.
Strict-wide: **333 open blockers** (261 Base-NEXS; 72 imported Linux 9P).
The **37 indirect callback sites** are compiler-typed and their
receivers/source byte offsets recorded in CLANG_AST_CALLS.csv: 24 Base-NEXS,
10 reference NXOLMO, 3 reference NXEC. These are dynamically invoked
driver/module/fn-lifetime/REG/firmware callbacks. Determining a receiver
and its desugared function-pointer C type DOES NOT establish its runtime
target, registration integrity, authorization or complete linker ABI;
all these gates are OPEN.

The second real migrated NXOLMO subsystem is runtime/operation_budget.c,
runtime/operation_metrics.c and runtime/operation_catalog.c with canonical
nexs_budget.h, nexs_operation_metrics.h, nexs_operation_catalog.h and
nexs_operation_ids.h. The evaluator's one fuel owner is now
EvalCtx.execution_budget (nx_operation_budget); the previous duplicate
fuel counters were removed, with real exhaustion tests. Clock and counter
platform providers explicitly return NX_STATUS_NOT_SUPPORTED when unavailable
(no invented CPU cycles). Hosted monotonic clock and target AMD64 RDTSCP
providers exist; generic OS1/baremetal do not claim hardware measurements.
Normal NXOLMO reference tests and actual Base-NEXS sanitizers passed. The
full Base make test (including 17 executed examples), hosted build, source
Power12 phase0, OS1 AMD64/ARM64 compiler builds and parallel AOT passed.
Reference NXOLMO/NXEC remain unmodified. These successful slices do not
implement multi-instance concurrent verified VM, P1 owned page buffers or
P3 authenticated StateStore.

scripts/rearchitecture_complete_reference_scan.py opens each scanned
implementation source bytewise and records source hash/dependency and
unqualified source semantics in REFERENCE_IMPLEMENTATION_DEPENDENCIES.csv.
Full manual review and 2 real tests for every function remain P0 blockers.


## 21 November actual baremetal guest regression after NXOLMO runtime port

The recompiled canonical Base-NEXS executable (including the new NXOLMO
budget, metrics and operation catalog) successfully **linked and booted
in real QEMU AMD64 and ARM64 guests**, using the unmodified
scripts/test_baremetal_qemu.py UART assertions.

- build/assurance/runtime-operation-amd64-baremetal-build.log;
  runtime-operation-amd64-qemu.log: BAREMETAL_amd64_QEMU=PASS.
- build/assurance/runtime-operation-arm64-baremetal-build.log;
  runtime-operation-arm64-qemu.log: BAREMETAL_arm64_QEMU=PASS.

Both reported a running interpreter, its command list and the live
nexs> prompt, with no configured fault marker. QEMU termination on
the bounded test timeout is intentional. The captured UART reports
are build/assurance/baremetal-{amd64,arm64}-qemu.json.
This is a real Base-NEXS standalone guest test; it is NOT the
separate NexsOS1 umbrella make run/nxadb, full userland graphical,
or concurrent multi-instance P2/P3 acceptance proof.
