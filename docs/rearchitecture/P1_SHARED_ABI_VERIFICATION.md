# Shared Binary/Object ABI — Complete Foundation Migration

Status: VERIFIED completion of the NXOLMO *base ABI* migration into the
single Base-NEXS product. This does NOT mean the NXOLMO StateStore, NXEC
boot/REG, authority, buffer, journal, VFS, or Base-NEXS VM is complete.

## Actual source/function ownership and original semantics

| Source in NEXS product | Reference algorithm | Functions / semantics |
| --- | --- | --- |
| core/include/nexs_checked.h, core/checked.c | NXOLMO base/checked.c, span.h | 8 checked-size/u64 add/multiply, ranges and span validators; failure clears outputs |
| core/status.c | NXOLMO base/status.c | 25 stable status values and 1 complete name mapping |
| core/include/nexs_types.h, core/types.c | NXOLMO base/types.c | 4 ID/reference comparisons and generation validity; original 128-bit object IDs + 64-bit generations |
| core/include/nexs_result.h, core/result.c | NXOLMO base/result.c | 2 result init/validate functions; schema v1, 7 domains, partial/retryable/unknown-cycles flags |
| core/include/nexs_endian.h | NXOLMO base/endian.h | 12 exact little/big-endian 16/32/64-bit load/store inlines; never native struct wire casts |

All 27 indexed functions have real implementation bodies in the Base-NEXS
tree. Reference copies remain strictly READ-ONLY and are not linked or
called by the production build. One common status/error namespace is
used by all future roles. This is a semantic migration, not a stub or
renaming that discards original functions.

The original endian header's static_assert checks were not copied:
project policy bans assertions in production. The exact-width integer
types are provided by stdint.h and the active platform's octet width is
checked by the real test harness. Byte array parsing is safe only after
the existing nx_const_span_validate/nx_mut_span_validate checks; the
unbounded endian access helpers do not pretend to enforce capabilities.

## Actual API usage (not idle scaffolding)

core/dynarray.c uses the canonical nx_size_add/nx_size_mul functions
for initial Value allocation, array growth admission and existing copy
sizes. Impossible lengths result in failure before publication or
mutation. No separate array-private checked arithmetic library was
introduced. Other subsystems must use this same core ABI as they migrate.

The previous attempt to compile the NXOLMO checked implementation
directly through external/references/nxolmo/src/base/checked.c was
removed from config/sources.mk, the Makefile object graph and include
paths. Both the interpreter and every current AOT target link the
product-owned core/checked.c and core/status.c, plus types.c/result.c.
Direct external reference availability is not required for a product
compile. The comparison sources are retained only for audits.

## Executed tests and negative contracts

- tests/test_checked_contract.c: PASS **57 checks** covering all 9
  checked/status functions, normal arithmetic, MAX overflow, zero,
  null outputs, invalid pointer ranges, disjoint/overlapping ranges,
  truncated spans and all 25 stable status-to-string names.
- tests/test_foundation_abi.c: PASS **39 checks** covering 4 object
  identity functions (including stale generation and zero identity),
  2 result functions (schema version, flags, domain, unknown cycle
  contract) and all 12 endian load/store functions (exact octet
  patterns, round trips, unaligned byte buffers, zero/max boundaries,
  explicit truncated span rejection).
- Both test executables PASS under AddressSanitizer and
  UndefinedBehaviorSanitizer. No mock libraries or external NXOLMO
  source are linked to the test executables.
- make test-array-capacity-core and make test-array-capacity-asan:
  PASS real production Value/buddy/array source; the array tests now
  link against the canonical checked ABI.
- make os1-check: PASS for amd64 and arm64 with two concurrent OS1
  native AOT ELF builds; not an nxadb guest result.
- Real QEMU boot after this change: AMD64 and ARM64 PASS the expected
  NXSCRIPT_BAREMETAL_SMOKE UART marker, without fault markers.
- make power12-phase0: PASS all 19 source-index, AST, ownership and
  provenance tests on the regenerated catalog.
- make test: PASS 85 real Python tests, production C array harness,
  57 checked ABI tests and 39 foundation ABI tests; 34/34 language
  syntax sources parsed, and 17/17 noninteractive examples executed.
- make power12-strict remains intentionally red for unmigrated legacy
  sources. This ABI slice does not waive or hide any remaining blockers.

## Migration evidence, no duplicate feature claims

CANONICAL_FUNCTION_OWNERSHIP.csv covers every selected source
definition with its desired canonical owner, present migration state
and acceptance gate. Exactly 27 Base-NEXS functions have status
PORT_IMPLEMENTED_TESTED. The corresponding 27 NXOLMO reference
symbols have REFERENCE_ALGORITHMS_PORTED_IN_FULL; these do not count
as a second production implementation. All remaining NXOLMO/NXEC
functions are marked reference-only or not-ported, not passed.

The source inventory, AST call candidates and planned two-tests-per-
function list remain traceability tools, NOT proofs that all 4,236
definitions have been semantically audited. Full Power12 strict
remains blocked on pre-existing issues, including the NXOLMO Linux
9P reference snapshot. There are NO rule exemptions.

## Explicitly unresolved (must NOT be called complete)

- NXEC bootloader/HCD/firmware map, independent kernel admission,
  capability HAL and REG envelope/transport.
- NXOLMO canonical StateStore/NXREG, authority and revocation,
  generational object graph, page lease/buffer, resource admission,
  WAL/block, ext4 and complete 9P.
- Base-NEXS per-instance runtime/VM, no fixed stack, concurrency,
  verified IR, full callable object linker and integration with NXEXEC.
- Legacy global buddy/page pools, registry and refcount limitations:
  the rejected global-pager prototype was retained only as an audit
  patch in ignored build/assurance. It was NOT staged as a completed
  memory subsystem.
- Full NexsOS1 make run + nxadb on both architectures remains blocked
  by the read-only umbrella's missing portability/doom/doom.mk.

Source changes are limited to Base-NEXS. NXOLMO/NXEC/NexsOS1 sources
remain untouched. No commit or push.
