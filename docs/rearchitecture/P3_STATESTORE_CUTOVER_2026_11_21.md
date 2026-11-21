# P3 canonical NXOLMO StateStore / RegKey cutover evidence

Date: 2026-11-21. Status: PARTIALLY MIGRATED; P3 EXIT IS NOT ACCEPTED.
Governing source: END_TO_END_REDESIGN_PLAN.md (P0-P8), INTEGRATED_ARCHITECTURE.md, CANONICAL_ARCHITECTURE_STANDARD.md.

## Canonical technology and implemented code

Base-NEXS owns language/parser/executable code; NXOLMO owns canonical
object identities, generations, revisions and parent-child relationships.
NXEC REG is transport/authority semantics, not a second registry.
VFS must be a path/mount projection over those same object identities.

Real source-qualified product-owned ports, compiled and linked:
- registry/state_store.c / include/nexs_state_store.h
- registry/nxreg.c / include/nexs_nxreg.h
- registry/snapshot_pool.c / include/nexs_snapshot_pool.h
- registry/schema.c / include/nexs_schema.h
- registry/migration.c / include/nexs_migration.h

The modules use the previously promoted SINGLE product checked/status/
types/crypto/fuel/metrics/endian ABI. Original upstream snapshots remain
read-only and are not linked into production. Reference test-only rehash
failure hooks have not been carried into trusted production; equivalent
fault-injection evidence is still an open verification gate.

## Live RegKey StateStore wiring

registry/registry.c now embeds the ONE NXOLMO StateStore. Published RegKey
objects hold an nx_object_ref, not a pointer-derived global identity.
Creating a path publishes a generation-checked object; replacing a typed
Value advances the revision before pointer publication; move/reparent
advances the canonical parent relation while preserving object ID; subtree
retirement deletes StateStore nodes and rejects dead references.
reg_object_read reads the actual StateStore metadata for a named object.

nx_state_store_grow takes caller-owned replacement arrays, rehashes
actual live objects, and preserves object IDs/generations, revisions,
parent-child links and epoch through an atomic writer-lock swap. Memory
for metadata comes from the page provider rather than the legacy Buddy
language-value allocator; capacity grows on admitted demand.

The still-existing RegKey path strings, child linked lists, rights bytes,
queues and Value pointers are LOCAL PROJECTIONS, NOT an accepted canonical
transactional namespace. Named lookup is still linear and externally
mutable pointers can cause divergence; this is an active P3 blocker.
NXREG currently offers read/child enumeration, not full transactional
writes. The StateStore and named namespace do not yet share a durable
commit, kernel-enforced capability checks, MVCC version retention or a
per-runtime-instance authority domain. Schema and snapshot mechanisms
are built but not yet enforced throughout language mutations.

## Executed evidence, not inventory-only

- tests/test_nxolmo_regkey_state_core.c: 34 actual checks PASS
  (creation, type, parent identity, revision, move, growth,
  integrity, stale reference, mass retire).
- tests/nxolmo_canonical: seven original NXOLMO tests reproduced
  against product source: snapshot pool, schema, migration, NXREG API
  and three concurrent snapshot/schema tests, all PASS.
- Original NXREG test: 8 threads, 1,000,000 direct reads and
  1,000,000 opcode dispatches, with real contention/retries.
- Existing callable/path test selection: 24 tests PASS.
- make power12-phase0 PASS against regenerated source evidence.
- make test, os1-check, baremetal/QEMU are separate evidence steps;
  no full NexsOS1 umbrella make run + nxadb claim is made here.

## Outstanding P3/P5/P6/P8 hard gates

1. Replace linear named child traversal and fixed buffers with indexed
   growable names; make StateStore the sole source for name/mount/bind
   relation publication, not two mutable graphs.
2. Complete authenticated NXREG write/transaction opcodes, sealed schema
   enforcement, MVCC snapshots, write-conflict and rollback, WAL/fsync,
   restart recovery and robust reader reclamation.
3. Kernel/host capability validation with principal, domain, rights,
   attenuation, revocation and commit-boundary revalidation. Path lookup
   itself confers no authority.
4. RuntimeInstance: remove singleton registry and allocator, isolate
   resources/ownership, bind executable code to typed function metadata.
   Every callable must be introspectable with independent invocation
   rights; no second function table.
5. Integrate VFS, 9P, ext4, buffers/leases, process supervision and
   NXEXEC into the same state graph. The current fs/regfs format and
   local RegKey queue are not canonical VFS/transport.
6. Strict full-source Power12, two real tests per function, fault
   injection, multithreaded/multiprocess stress, performance comparison,
   hosted macOS/Linux and actual AMD64/AArch64 OS1 nxadb acceptance.

No commit or push was performed.
