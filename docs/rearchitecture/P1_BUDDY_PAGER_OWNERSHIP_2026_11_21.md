# P1: production Buddy and Pager ownership hardening — 21 November 2026

**Exit gate: OPEN.** These are real, tested changes in the ONE existing
Base-NEXS allocator and pager; they do not replace NXOLMO buffer ownership,
remove process-global pools, or establish per-RuntimeInstance allocation.
No shadow allocator or mock state store was introduced.

## Source-level faults found and corrected

| Actual product path | Initial failure mode | Implemented source change |
| --- | --- | --- |
| core/buddy.c, buddy_alloc_node | Recursive descent into tree; Power12 no-recursion violation | Explicit stackless bounded preorder using actual heap-tree parents/siblings; skips fully USED subtrees; no extra global queue |
| core/buddy.c, buddy_free_node | Recursive coalescence, no iterative ownership walk | Bounded pointer-to-block descent, exact block-base check; bottom-up sibling coalescence; interior/unknown pointers cannot free ownership |
| core/buddy.c, buddy_next_pow2 | Unbounded left-shift for direct oversized request may wrap to zero and fail to terminate | Capacity and checked-overflow guard; uses architecture word-width loop bound; returns zero outside provider limit |
| core/buddy.c, static assertions | Four production _Static_assert expressions violate the project's explicit no-production-assert P05 rule | Fail-closed runtime geometry check + tests/test_buddy_geometry_build.c independently checks exact power-of-two and node count equations before every normal make build |
| core/pager.c, hosted page_free | The caller's arbitrary page count was used for munmap **before** checking the slot table; partial unmap/dangling lease possible | page_free_checked validates exact live base+page count, unmaps only recorded span, retires slot only on successful unmap; void wrapper preserves existing ABI |
| core/pager.c, freestanding page_free | Caller count/bounds could clear bitmap bits outside actual ownership; duplicate/incorrect releases | Same exact slot admission, checked alignment and span, full bitmap validation before freeing any bit |
| core/pager.c, page_alloc/nexs_alloc | Unchecked multiplication/roundup, fatal die for large allocations and fatal xmalloc path for small ones | Overflow rejection with NULL and fallible nexs_alloc for BOTH existing page and Buddy providers, zero-initialized on success; existing xmalloc unchanged |
| core/pager.c, freestanding bitmap search | Legacy break in trusted page scan | Bounded scan and provider-derived skip without break |

## Actual independently executed tests

- tests/test_buddy_iteration_core.c compiled with the real 4 KiB Buddy
  provider: **532 checks PASS**, including every leaf in a full pool,
  double-sized fragmentation rejection, full coalescence, no interior
  pointer release and oversized rounding rejection.
- tests/test_recoverable_alloc_core.c, real 4 KiB provider: **38 PASS**,
  including actual allocation exhaustion, NULL instead of process
  fatal termination, zero-fill, recovered admission after release,
  and size overflow. This tests the user-facing nexs_alloc API.
- tests/test_pager_exact_ownership_core.c, actual hosted mmap,
  registry, Value and buddy: **1,218 PASS**, including 600 successive
  allocate/free cycles, mismatch retains mapped bytes, double
  release and unowned pointer rejection.
- Each test also ran with real AddressSanitizer/UBSan and PASS.
- The independent build-only Buddy geometry test executes from the
  existing make gate. It is not copied into the product or faked as a
  runtime proof.
- Full language tests and both OS1/QEMU ISAs require re-execution
  after the final source edits; collect fresh logs rather than
  claiming previous P4 evidence proves the new Buddy search.

## Important remaining P1 work — not optional

1. Retire process-global memory_pool, buddy_tree, page_table and
   hardcoded LARGE_POOL_PAGES/MAX_PAGE_ALLOCS; derive capacity from an
   admitted resource provider/HCD and explicit RuntimeInstance owner.
2. Migrate legacy fatal xmalloc, xrealloc and buddy_strdup callsites
   to typed fallible reservation/rollback. **Do not just globally
   change xmalloc to NULL** while its consumers still dereference
   unchecked returns; update each caller function and tests first.
3. Replace per-target #ifdef and hardcoded freestanding addresses in
   the compiled allocator source with selected provider translation
   units, using actual NXEC preboot maps and NXOLMO lease contracts.
4. Eliminate raw synthetic pointer-as-physical-address registry
   records and process-0 assumptions without breaking kernel MMU
   callers; use authoritative object IDs, generations and rights.
5. Implement per-instance isolation, safe concurrent/free behavior,
   pinning, generation, revoke and page-count accounting, then
   qualify allocator under true multiple-process/thread execution.
6. Complete ownership/lifetime analysis and two independent tests for
   each critical function plus faults, sanitizer and full OS1 nxadb.
7. Run Power12 strict-wide and do not waive remaining warnings
   just because this one allocator slice passes.

The correctness improvement is a prerequisite for LIMEN-SM placement
experiments; it does NOT justify installing SMD as a replacement allocator.

## Second production slice: ONE Buddy engine, explicit admitted arenas

The same functions that power the legacy default Buddy arena are now
parameterized by NexsBuddyArena in the canonical core/buddy.c.
nexs_buddy_arena_init(arena, backing, capacity, tree, tree_bytes)
does NOT allocate hidden heap or invent a global table. It validates
power-of-two geometry, backing alignment, checked pointer arithmetic
and absence of overlap between backing data and its metadata; only
then does it initialize that caller's tree. An invalid request leaves
the proposed owner unmodified. nexs_buddy_arena_alloc and
nexs_buddy_arena_free share EXACTLY the original bounded allocation
and coalescence algorithms, now with explicit per-owner state.

buddy_alloc and buddy_free remain compatibility dispatchers to
the single legacy default arena, using the original exported memory
and tree. THIS IS NOT YET THE P1 EXIT: nexs_runtime_init() still
owns process-global memory_pool/buddy_tree, and the scheduler/VM does
not yet select a different arena for each RuntimeInstance. At least
the memory algorithm itself no longer requires a process-global
allocator, and distinct caller-admitted arenas can be independently
exercised without any duplicate core implementation.

Verified production tests:
- tests/test_buddy_iteration_core.c now adds 1,200 reproducible
  randomized live-extent/canary operations; **14,692 assertions PASS**,
  including ASan/UBSan. Minimum alignment is explicit _Alignas.
- tests/test_buddy_arena_isolation_core.c: **65 PASS** against the
  real single Buddy engine across 4 KiB and 8 KiB caller-owned pools,
  with cross-owner release denial, stale/double free, wrong geometry
  and separate metadata validation; ASan/UBSan PASS.
- tests/test_buddy_arena_thread_isolation.c: **four genuine pthread
  workers × 5,000 allocate/release cycles**, each with independent
  backing/tree, PASS with normal toolchain and ASan/UBSan. This does
  NOT establish safe simultaneous modifications to the SAME arena.

Mandatory next step: migrate the actual VM/Task/Process owners to these
explicit arenas and NXOLMO provider-backed resource admission, instead
of leaving the sole runtime on the default legacy arena. Do not
reclassify P1/P2 as completed based on the separate arena tests.

The full Base-NEXS language suite and both OS1/baremetal AMD64+ARM64
build/QEMU checks were re-run after the first pager/Buddy hardening.
They must be re-run again against the final arena-parameterized source
before accepting the current staging checkpoint.

## Arena-owner overlap and fail-closed admission update

The init path now validates that the DESTINATION NexsBuddyArena control
structure does not overlap the caller's data or metadata span.
A negative test caught the subtle difference between checking a local
temporary candidate and checking the actual destination address; both
data and metadata overlap are rejected before copying control state.
Two additional regression checks pass (65/65 total). This is still
single-owner local memory admission, NOT an unforgeable capability:
other actors need NXOLMO authority to prevent double-admission of the
SAME provider resource to two independent arena owners.

## Final isolated source-level assurance evidence

After converting the single production Buddy implementation to accept
explicit backing arenas, rerunning the source provenance/strict baseline
inventory gives PASS 25/25 P0 checks, 4,435 source definitions
from 582 files, 1,923 compiler AST bodies and 388 tracked
Base-NEXS/NXOLMO/NXEC C/H/assembly sources classified.
Strict Power12 remains FAILED with 340 blockers
(268 Base-NEXS, 72 imported Linux-9P reference), not waived.
The six sanitizer-enabled C suites pass:
Buddy 14,692 assertions; arena owner 65; four worker threads
× 5,000 operations; recoverable OOM 38; pager ownership 1,218;
and legacy registry integrity 8,798. They do not claim security
under concurrent accesses to the SAME arena, authoritative object
revocation or multi-VM runtime separation.

## Cross-target and independent compiler evidence on final arena source

Before the subsequent header format-contract refinement, an independent
source-level qualification finished:
- make test: PASS 182/182 Python tests, 33/33 syntax files,
  17/17 actually executed NEXS examples, and all six actual C
  suites including 14,692 Buddy, 65 arena isolation, 1,218 pager,
  38 fallible allocation and 8,798 registry checks.
- make os1-check: PASS both AMD64 and ARM64 compiler targets and
  parallel AOT checks.
- Both real AMD64 and ARM64 QEMU baremetal guests compiled and
  executed the same NEXS program and verified UART completion.
- Independent strict-Clang source analysis with -Wall/-Wextra/-Werror/
  -Wpedantic/-Wconversion/-Wsign-conversion/-Wshadow/
  -Wstrict-prototypes/-Wmissing-prototypes/-Wformat=2/-Wundef
  PASS for both core/buddy.c and core/pager.c after adding
  compile-time printf-format verification to nexs_warn.
- Real C ASan/UBSan runs PASS for each isolated and shared-owner
  test suite. Strict Power12-wide still FAIL with 340 open blockers.

The header annotation has no binary memory layout change, but
the complete source provenance and dual-arch tests must still be
re-run after it before a stage is declared verified.

## Subsequent P1 source admission and hosted virtual-memory correction

Verified on 21 November 2026 against the current Base-NEXS product sources.
This is a genuine P1 sub-slice, **not** the P1 exit gate.

- `lang/eval.c::eval_file` no longer invokes fatal `xmalloc` to read a
  source. It validates seek/size/rewind, admits bytes through the existing
  recoverable `nexs_alloc` provider, rejects a short read and releases the
  exact allocation through `nexs_free`. `fread` count is used instead of
  `ferror`, which the selected freestanding FILE ABI does not implement.
- `core/pager.c` hosted `mmap` no longer publishes virtual pointers as
  synthetic `/mem/virt/0/.../phys` or MMU-rights records. The existing
  exact `page_table` extent tracking remains; no additional manager,
  namespace, fake physical address or kernel syscall was introduced.
- `tests/test_source_catalog_resolution.py` exercises the actual interpreter
  with a 65 MiB comment-containing source (larger than the default Buddy
  pool) and with a real non-seekable FIFO. Both execute or fail closed
  without terminating the interpreter. The pager C suite verifies that a
  hosted mmap does not forge registry physical-frame identity.

Final reproducible logs are in `build/assurance/prep4-verified-final-*`
plus `prep4-portable-{amd64,arm64}-{build,qemu}.log`. Measured gates:
`make power12-phase0` PASS (25/25); `make test` PASS (184 Python
cases, 17/17 actually executed NEXS examples); registry 8,798 checks;
Buddy 14,692; isolated Buddy 65; page ownership 1,224; recoverable
allocation 38. Both AMD64 and ARM64 standalone baremetal QEMU guests
compiled and executed, reporting `PRE_P4_NXLIB_EXTENSION_QEMU_PASS`.
`make os1-check` PASS (both architectures and parallel AOT).
Real pager/OOM C suites under ASan/UBSan PASS.
`git diff --check` PASS. Full `make power12-strict` still FAILS
with 340 tracked issues (268 Base-NEXS, 72 Linux 9P reference).

This does NOT establish a per-instance provider, revoke-safe page leases,
concurrent mutation protection, a nonfatal Value/AST OOM path, or a
protected NXREG authority. The global runtime Buddy adapter, fixed page
slots, fatal legacy allocations, P2 isolated VM/fuel/tasks, P3
StateStore/MVCC/revocation, and full umbrella `make run` / `nxadb` on
both ISA remain mandatory and OPEN. External NXOLMO/NXEC/umbrella trees
were not edited; no commit or push was performed.


## Original provider retained; duplicated pool prototype rejected

An attempted independent NexsPoolGroup/Block/Ref prototype was reverted
before acceptance: it duplicated existing NXOLMO nx_buffer_manager/slot/
lease/view and nx_state_store owner contracts. The previously staged
canonical Buddy/Pager fixes were not rolled back. The proper final indexed
pool syntax must reuse the canonical indexed array and object namespace,
with NXOLMO resource admission, provider ownership, generation, lease,
pinning and revocation. No new parallel named pool registry is authorized.
Legacy global memory_pool, buddy_tree and page_table remain P1 blockers.
