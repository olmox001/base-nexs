# P3 — RegKey integrity precondition for NXOLMO StateStore

**Status: implementation and actual tests complete for the current RegKey
mutation/path invariants; the canonical NXOLMO StateStore replacement
has NOT been ported or integrated.** This step is an explicit
prerequisite to the object-store migration, not a parallel data store,
not a compatibility shim and not an authority/security claim.

## Reference source investigation and architectural decision

Read-only NXOLMO reference:
external/references/nxolmo/include/nxolmo/registry/state_store.h,
src/registry/state_store.c, include/nxolmo/registry/nxreg.h,
src/registry/nxreg.c. NXOLMO StateStore uses nx_object_ref with
generation-safe identity, parent/child relationships, atomic revisions,
explicit lifecycle, bounded operation fuel, a keyed hash table with
incremental rehash and caller-provided slot/index/rehash backing
memory. Its nxreg_state_store_adapter_init currently publishes a
read-only API table negotiated by version/features and authorization
callback. It is NOT a generic replacement for RegKey's directly
pointer-returning value registry and IPC queues.

Full migration requires adopting the complete StateStore as the one
backing data model, with runtime-owned and generation-safe payload/
Value refs, allocator/provider admission, checked object transactions,
durable/recoverable namespace topology, and authenticated NXREG
authority at trusted boundaries. Calling the NXOLMO read-only adapter
while retaining a separate mutable RegKey table would introduce two
sources of truth and is explicitly forbidden. No code in NXOLMO, NXEC,
or NexsOS1 was edited.

## Existing registry defects reproduced before repair

An independent compiled C contract test uses the ACTUAL production
registry.c, reg_ipc.c, value.c, dynarray.c, buddy.c and checked.c.
The initial existing implementation failed **18 assertions** including:

- Full paths above REG_PATH_MAX, or name components above NAME_LEN, were
  truncated into reachable shorter aliases or partially admitted keys.
- Deleting "/" freed the registry root and left a dangling global.
- The heap DFS release stack held at most 1,024 nodes, dropping child
  subtrees, object counts and queue ownership on wider deletions.
- reg_move detached its source before destination validation, could
  overwrite a preexisting destination or lose the subtree on rejection.
  Path rewrite also silently skipped nodes beyond the fixed DFS stack.
- Selective unmount detached a node and decremented the object count
  twice when freeing it.

The initial failing baseline is preserved as an ignored local
build/assurance/p3-registry-baseline.log verification artifact.
Tests were not weakened to make their results pass.

## Actual source changes (no new registry implementation)

registry/registry.c now preflights exact path and component sizes
before mutation. reg_mkpath and reg_lookup parse bounded components
without process-global strtok or silent truncation. reg_delete rejects
root deletion, then reclaims the whole detached subtree.

Subtree release is post-order using existing parent/child/sibling links;
it needs **O(1) auxiliary memory**, no fixed 1,024-node stack and
no recursive call. Each object, Value and queue is released once.
The move path prevalidates source, destination, collisions, ancestry,
all descendant lengths and the new parent before unlinking its source.
Its path rewrite visits every existing child exactly once without
a second index. Source/destination failure leaves source reachable.
A move keeps g_registry.total_keys unchanged.

reg_mount now copies the same objects iteratively (no recursion)
and reuses the same canonical RegKey backing tree. Each source
descendant maps to one exact destination path, respecting the existing
"before" value overwrite / "after" preserve policies. reg_unmount
and reg_bind use the same validated paths and accounting. Overlap
that would mutate a tree while traversing itself is explicitly
rejected until NXOLMO transactional snapshots can support it.
Self-bind remains a no-op.

Global RegKey remains legacy and is **not** thread-safe, MVCC,
generation-checked, per-instance, authenticated or transactional.
Its per-key rights field is not a capability. This phase makes
no such claims, changes no syscall or kernel object ABI, and does
not introduce a second registry, allocator, VFS or IPC provider.
Full allocator OOM isolation, dynamic names and paths, transport
resource budgets, complete failure atomicity on OOM and parallel
writers remain outstanding.

## Verification evidence and acceptance boundary

- make test-registry-integrity-core — real production objects,
  **8,798 assertions PASS**, tests wide (2,400) and deep subtrees,
  moving 1,700 descendants, mounting 1,400 descendants, deletion,
  exact path errors, destination collision/rollback, root immunity,
  value preference, bind, selective unmount and IPC message lifetime.
- make test-registry-integrity-asan — the SAME actual source paths
  recompiled with Clang AddressSanitizer and UBSan, same 8,798
  assertions PASS; not mocks, stubs or a fake success marker.
- tests/test_registry_path_runtime.py — **9/9** real NEXS scripts:
  root scope normalization, exact/multi-segment paths, overly long
  path/component rejection, recursive deletion, 90 function scopes and
  function alias lifetime.
- Full make test: PASS 132/132 Python tests, real production C/ABI
  harnesses (including 8,798 registry assertions), 34/34 syntax files
  and 17/17 actually executed noninteractive examples after the fix.
- make power12-phase0: PASS 21/21 source/Clang/ownership tests
  **after staging the two new test files**. Refreshed catalog has
  4,318 indexed definitions in 562 files, 1,895 product source
  functions, 1,898 Clang AST bodies and 19 registry mutation routines
  classified as integrity-tested but NOT StateStore-ported.
- make os1-check: PASS both OS1 AMD64 and ARM64 cross-binary build
  acceptance plus two parallel independent AOT compilation sessions.
- Actual QEMU AMD64 and ARM64 guest boots: PASS, verified exact UART
  values 7, 42, err, err, 73, 19 and P3_REGISTRY_GUEST_OK. This
  exercises global lexical scope, nested registry access, deletion,
  rejected oversized name, long multi-segment path and function
  execution on BOTH actual guest ISAs.
- Strict Power12 baseline remains FAILED: 348 assigned issues
  (276 in Base-NEXS, 72 unported NXOLMO Linux-9P gotos).
  These are NOT waived or reported as release-ready.
- scripts/rearchitecture_ownership.py marks ONLY the actually
  affected registry functions as
  REGKEY_MUTATION_INTEGRITY_TESTED_STATESTORE_PENDING. The rest of
  the registry and the entirety of NXOLMO StateStore remain explicitly
  unqualified/unported, never mislabeled PORT_IMPLEMENTED_TESTED.

## Next required integration

The next *architectural* step is a source-level port of the NXOLMO
state-store memory, object identity, revision and parent/child contracts
with a well-defined Value/AST payload owner and NXREG authenticated
backend. The migration must replace RegKey storage in place, not create
an internal index that shadows it. Caller-supplied buffer capacity is a
provider-admitted bound; do not replace it with an undocumented global
MAX_OBJECTS constant. Implement proper stale generation rejection,
transaction conflicts and authority before claiming shared runtime use.

The 72 unported NXOLMO Linux-9P goto blockers and the remaining
Base-NEXS strict Power12 violations remain release blocking.
No Git commit, push or umbrella change is authorized by this step.
