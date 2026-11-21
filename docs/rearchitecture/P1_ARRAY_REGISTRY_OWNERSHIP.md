# P1/P3 Boundary — Remove Duplicate Array Name Authority

Status: implementation complete for registry-owned named arrays and
independent pipe-return arrays. The overarching P1/P3 architecture is
NOT complete. Existing RegKey/NXREG authority and memory provisioning
still require replacement; this work removes redundant array state now.

## Actual source transitions

- core/dynarray.c: REMOVE g_arrays[MAX_ARRAYS], g_array_count, arr_get,
  arr_get_or_create and their global name lookup. Named arrays do not
  have a second ownership/lookup path. Anonymous Value arrays remain.
- core/include/nexs_value.h: DELETE the obsolete global table and
  named-table API. No compatibility function or shadow implementation.
- lang/eval.c AST_INDEX/AST_DEL: lookup only via the current registry
  resolver and validate TYPE_ARR; a scalar can no longer accidentally
  expose an orphaned named array.
- lang/eval.c AST_INDEX_ASSIGN: create a *new anonymous* array only if the
  registry name is absent, then clone its reference into the canonical
  namespace and release the temporary creator reference. Preserve
  regular aliasing by using the existing Value refcount and clone
  contract. Explicitly handle name collisions, registry write failure,
  missing registry owner, and truncated scope paths.
- sys/sysio.c bi_pipe: create a distinct anonymous pair for every
  invocation. The old "__pipe__" singleton alias meant that a second
  pipe overwrote FD contents in the first pipe's return array.
- runtime/runtime.c: REMOVE the reset of a now-deleted array counter.

## Reproduction and actual tests

Pre-change confirmed regression:

    alpha[0] = 123
    alpha = 6
    out alpha[0]

The old implementation incorrectly returned 123 via the global name
table, in contradiction with the namespace's current scalar value.

Post-change the same execution fails explicitly with
"array not found". No old array is resurrected. This is a real
negative result, not a fixture that merely checks exit code.

- tests/test_array_registry_ownership.py: PASS 12/12 real execution
  cases, including orphan invalidation, scalar type collision, pipe
  uniqueness and independence, 2,200 simultaneous named arrays beyond
  the previously hardcoded 2,048 table limit, delete/lookup, array
  reference aliasing, explicit root/worker sharing, proper local scope
  isolation, registry lifetime and returning an owned Value from a function.
- example/exent_bus.nx: The former implicit cross-worker global table
  assumption failed under the correct single-owner semantics. The example
  now explicitly creates its shared ledger in the root namespace before
  worker invocations. A REAL post-change execution completed all jobs and
  returned ledger values 67, 142, 91, 51. The source was changed only in
  Base-NEXS; no hidden global fallback was reintroduced.
- make test: PASS, 71 Python tests, 34/34 parsed NEXS sources and
  17/17 noninteractive runtime examples, including exent_bus.
- make os1-check: PASS, AMD64/ARM64 SDK ELF ABI checks and two simultaneous
  OS1 AOT compiler sessions with independent outputs.
- Baremetal public AOT CLI and QEMU: PASS AMD64 and ARM64, executed
  multiple distinct named arrays in real guests with verified output 30,
  7, 9 and ARRAY_REGISTRY_QEMU_PASS, no fault markers.
- make power12-phase0: PASS 14 AST/inventory/source-hash tests after
  source/fixture updates; indexed 4,166 source symbols in 543 files.
- git diff --cached --check: required before staging; no commit/push.
- No claim that a global registry shared by all RuntimeInstances already
  implements per-instance isolation, NXREG capabilities, transactional
  changes or lock-free concurrent updates.

## Remaining unrelated restrictions

The old core/buddy.c allocator still uses one global fixed pool and
process-fatal xmalloc on OOM; arrays' item storage still has unchecked
growth in arr_ensure_cap and an int refcount without overflow
protection. These must be addressed in a distinct P1 memory and
resource-ownership migration, not hidden by deleting MAX_ARRAYS.

Registry RegKey path names, RegKey authority and registry isolation
remain subject to the full NXOLMO/NXEC migration at P3. No new syscall
was added. Existing NXOLMO/NXEC/NexsOS1 references were not modified.
No commit or push.
