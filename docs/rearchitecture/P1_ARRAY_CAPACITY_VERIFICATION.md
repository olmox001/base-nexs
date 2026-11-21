# P1 Array Capacity Admission and Buffer Mutation Safety

Status: real source implementation of checked array growth, fallible
array item storage, safe insert/delete and transactional text-buffer loading.
This is NOT completion of P1 allocator/provider migration and is not a
claim that a single global 64 MiB buddy pool is acceptable architecture.

## Replaced code paths and ownership

- core/dynarray.c arr_create_anon: obtains both the DynArray and initial
  items from the currently selected allocator using a fallible API and
  cleans up the first allocation on failure.
- core/dynarray.c arr_ensure_cap: returns an explicit failure status
  rather than looping until capacity integer overflow or dying through
  xmalloc. Checks the index, capacity growth and multiplication before
  asking the real provider for memory. A provider refusal retains the
  previous array pointer, size, capacity and contents unchanged.
- core/dynarray.c arr_set: checks admission before mutation, returns
  failure to callers and clones the replacement Value BEFORE freeing the
  old slot. This closes self-assignment use-after-free for strings.
- lang/eval.c: propagates array capacity errors and publishes no new
  registry key until its initial array item is successfully admitted.
- lang/builtins.c: all current producers check arr_create_anon or
  arr_set admission results and propagate errors rather than silently
  claiming an insertion. Owned string temporaries are released.
- lang/builtins.c arr_ins: checks signed index range BEFORE any slot
  operation, performs checked capacity admission and refuses out-of-range
  insertions without corrupting memory.
- lang/builtins.c arr_del: explicitly rejects negative indices and clears
  the stale tail slot after shifting Value ownership, preventing later
  append/self-replacement use-after-free and retention of duplicate refs.
- lang/builtins.c tb_load: stages the complete new text buffer in a
  separate temporary DynArray and only swaps its contents with the old
  array after successful admission, releasing previous Values afterward.
- sys/sysio.c bi_pipe: any failure to produce an array pair releases the
  partially created array and closes both newly issued descriptors.

## Reproduced failures before fix

The old implementation crashed on two real NEXS programs:

    a[0] = 5
    a[9223372036854775807] = 3

and

    a = arr_create_anon()
    arr_ins(a, 2147483647, 42)

The first triggered a fatal guest/host signal through oversized growth;
the second crashed via an out-of-range slot write.

Post-fix, the first yields "array capacity admission failed" and the
second returns a typed "arr_ins: index out of range" error; both
preserve the process and avoid out-of-bounds writes. The second still
leaves the array empty. No hidden global fallback was reintroduced.

## Tests and release constraints

- tests/test_array_capacity_core.c links actual production buddy/dynarray/
  Value code. Checks ordinary growth, impossible indices, physical
  provider refusal without state corruption, self-replacement of
  allocated strings, reference ownership and 4,000 allocate/grow/free
  cycles. Real success marker: ARRAY_CAPACITY_CORE_PASS.
- make test-array-capacity-asan: PASS on host using both AddressSanitizer
  and UndefinedBehaviorSanitizer from actual production source files.
- tests/test_array_capacity_admission.py: PASS 9/9 real NEXS programs
  testing max signed index refusal, invalid new array, negative/huge
  insertion/deletion, insert/delete/reinsert, text-buffer replacement and
  empty buffer, split/lines_of and preservation of independent arrays.
- make test: PASS 80 Python tests and ARRAY_CAPACITY_CORE_PASS in
  the production-object C harness; 34/34 NEXS syntax files and 17/17
  actually executed noninteractive examples also PASS.
- make os1-check: PASS independently for OS1 AMD64 and ARM64 ELFs;
  concurrent dual-program AOT isolation also PASS.
- Public Base-NEXS baremetal compiler and REAL QEMU: PASS AMD64 and ARM64.
  Both guest UART logs contain SECOND and ARRAY_CAPACITY_QEMU_OK; neither
  contains FIRST after array insertion/deletion and neither records a
  fault marker.
- make power12-phase0: PASS all 14 source fingerprint/Clang AST baseline
  tests after code changes, 4,184 definitions across 545 source files.
  The strict-wide release gate still FAILS and is not waived.
- git diff --cached --check: required before stage.

## Unresolved architectural contracts, never waived

The buddy provider is STILL the existing global fixed static pool and
xmalloc/buddy_strdup may still terminate on exhaustion. A full fault-
recoverable allocator requires per-instance page arenas and explicit
ownership plus HAL/NXOLMO/NXEC provider integration. This slice only
makes array *admission and capacity growth* fail safely, not every
upstream Value allocation or every VM operation.

The original RegKey registry also remains globally scoped and lacks
NXREG capability authority, concurrency and snapshots. CPU/task
isolation, dynamic registry components, AST continuation frames,
Power12 strict-wide compliance, guest NexsOS1 nxadb and selected
interactive examples remain unresolved. No code outside Base-NEXS
was modified, and no commit or push was performed.
