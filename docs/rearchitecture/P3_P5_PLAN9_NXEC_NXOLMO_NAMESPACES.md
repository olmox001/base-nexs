# P3–P5 Plan 9 resource namespace over NXEC + NXOLMO

Status: source-contract reconciliation, NOT implemented/qualified end to end.
Date: 2026-11-21. Original governing plan: END_TO_END_REDESIGN_PLAN.md.

## Roles and canonical identity

- NXEC owns domains, trusted capability issuance/delegation/revocation and
  thin HAL hardware resource contracts. A pathname never grants rights.
- NXOLMO owns the canonical generational nx_object_ref, state transitions,
  relationships and revisions, page/lease lifetime and commit semantics.
- Base-NEXS owns typed language, REPL, IR, callable-object presentation and
  bulk operations. It does not own a second object table or memory authority.

Plan 9 Chan/Dev and per-process Pgrp namespaces are the architectural
reference. This is not a claim of complete Plan 9 or 9P2000.L implementation.

| Plan 9 | NEXS role | Identity and authority |
|---|---|---|
| Chan | session-local handle, offset, mode and lease | NXOLMO ref with NXEC resolved grant |
| Dev/devtab | typed operation dispatch for each device | NXOLMO callable descriptor, NXEC HAL |
| Qid | wire-local path/version projection | canonical ref and revision; never an authority |
| Pgrp | per-domain/process namespace and mount view | NXOLMO namespace node, NXEC domain |
| Mount/union | typed versioned namespace relationships | NXOLMO commit, ordered resolution |
| /srv | published service endpoint | capability-bearing object, not host FD string |
| #c/#p/#e/#d/#s | /dev, /proc, /env, /fd, /srv | projections of same graph |
| 9P fid | session-local channel key | maps to NXOLMO ref; not global identity |
| REG endpoint | asynchronous bulk resource operations | NXEC verified envelope + NXOLMO commit |

NexsOS1 object/capability interfaces are the privileged boundary:
NO new kernel syscalls are requested.

## Addressing every bit without materializing a node per bit

One authorized address/view consists of object ref, domain, address-space ID,
extent offset/length, bit offset/mask, rights, expected revision, lease epoch.
Sparse extents represent RAM, virtual regions, MMIO register ranges, DMA,
buffer pages, cache-coherency domains, files and typed arrays. A pointer
borrowed by an active zero-copy lease is valid only in its own local address
space; it is never serialized or transferred as a raw pointer.

Batch operation descriptors represent millions of masked operations. One
StateStore transition governs the commit; count transforms and commits
separately. Access to MMIO can have irreversible effects: validate and record
intent, execute under NXEC authority, record ACK or partial failure, never
pretend that hardware writes can be rolled back by restoring metadata.

Hardware cache and IOMMU coherency must be respected; eliminate redundant
software mirrors, not required caches/barriers. Resource limits should be
runtime-derived and expandable. OOM, contention and stale authority must be
typed recoverable failures, never false successes.

## Actual product source inventory and remaining duplication

Product-linked real NXOLMO modules:
registry/state_store.c, registry/nxreg.c, registry/schema.c,
registry/snapshot_pool.c, registry/migration.c, authority/capability.c,
hardware/capability.c, buffer/buffer.c, buffer/capability_adapter.c,
fs/9p_frame.c. Original upstream unit/concurrency tests run on those files.

registry/registry.c currently retains RegKey name/Value projection over
NXOLMO StateStore. fs/9p.c uses canonical 9P frame decoding but still has
a singleton FID table and incomplete 9P semantics. kernel/vfs.c,
sys/sysio.c, fs/regfs.c and nxlib/services/fs/*.nx retain independent
file, FD, mount and string-indexed VFS systems. They must become views
on the same NXOLMO object graph, not additional sources of truth.

The final connection is:
NEXS callable/bulk op -> NXEC authority -> NXOLMO typed op ->
domain Chan namespace -> local Dev or 9P/REG -> page/device lease ->
published StateStore revision.

Direct same-domain execution can skip wire serialization but not
authorization, schema, generation, lease and lifetime enforcement.

## Required hostile tests

Hostile test generators MAY violate production Power12 coding conventions
on purpose; production sources remain subject to blocking Power12.

- Every real VFS/9P attach/walk/open/read/write/close, stale FID and
  nonexistent paths; every accepted write changes the same NXOLMO identity.
- Every hosted kernel syscall verb, with separate direct, KernelMsg
  wire and REG IPC paths. Per-operation latency histograms, per-provider
  allocation/page deltas and repro seeds must be reported.
- Actual NEXS interpreter/parser fuzz with independent valid-program
  oracles; mutations, byte injection, truncation and timeout/crash checks.
- NXEC scope attenuation, revocation, stale lease and nonce replay on
  real authority providers; no mock kernel authority accepted as exit proof.
- Random mixed threaded reads/writes/commits and forced allocator faults;
  linearizable histories, cancellation and no lost/double commits.
- Actual 9P2000.L interoperability, ext4 host tools and SSD-backed WAL
  crash consistency; real guest NXEC/OS1 targets when build unblocked.
- Benchmark not just message codecs but end-to-end syscall, path lookup,
  capability revalidation, task switching, byte copies, queue delay and
  commits; 1/2/4/8/16/32 workers and percentile tail latencies.
- TFLOPS must be tested separately using counted numeric operations.

Verified named VFS migration detail:
P3_NAMED_VFS_CUTOVER_2026_11_21.md. The standalone numeric inode
lookup has been deleted and /vfs/files now binds a file directly to
its NXOLMO object ref, payload and revision. Growable FD/FID session
slots reject stale refs, but their owner remains global and their
path/name lookups remain linear. This is NOT a full Plan 9 namespace,
a true NXEC capability handoff or zero-copy file storage.

P3, P4, P5, P7 and P8 remain OPEN despite passing component tests.
The NexsOS1 build remains blocked by explicit user instruction.


## P5 block backend port checkpoint (not a namespace exit)
The Base-NEXS product now contains the NXOLMO block device ABI and its
hosted caller-owned request/completion ring, volatile/durable memory emulator,
fault injection, crash recovery and synchronous adapter for existing contracts,
ported under block/. The initial test is the pinned NXOLMO block conformance
suite against product objects. The single-owner backend does NOT grant
hardware access, implement usermode ext4, offer concurrent submitters, promise
zero-copy payloads, persist to SSD, or bypass NXEC authorization. Future
per-endpoint ownership/lease and nonblocking worker scheduling require
explicit NXEC admission, typed completion and revocation tests.


## T3/T4 register-machine, masked arrays and genuine Plan 9 gap correction

The registry IS the entire runtime State Node Machine, including the masked
virtual representation of actual hardware registers/resources. This is not
a POSIX tree plus a separate application catalogue. Register geometry comes
from HCD-authorized real resources, not device names such as sd00 or from
script reg_set calls. Virtual arrays, file contents, devices, executable
symbols and mounts are NXOLMO typed objects with ONE ID, generation,
revision, schema, operation budget and transactional publication.

Protected read/write/map/execute views require intersection of principal,
domain, object, namespace, operation, hardware range, page, lease and
revocation masks. Masked registers return a REDACTED zero/absent projection,
not a mapped zero page; hidden IDs and relationships may not be listed.
Actual array/buffer views carry exact geometry/element width/encoding,
offset/length, bit masks, alignment, lease, cache policy and rights.
Push/pull uses existing NXOLMO buffer leases/rings and authenticated
versioned change-set transactions. No raw pointers or caller-invented
device grants. MMIO side effects cannot be undone by restoring metadata.

Plan 9 Chan/Dev/Pgrp, ordered union mounts, attach/walk/open/create,
read/write/stat/wstat/remove/clunk/flush, real hard/symbolic links, /srv,
device namespace and 9P2000.L peer compatibility remain mandatory.
The original registry/registry.c mount/bind/unmount clones and removes
subtrees and is NOT a mount/link implementation. The kernel/vfs.c
fixed 16-slot s_mounts catalogue did not participate in ANY path lookup;
it has been removed. Neither implementation is allowed to masquerade
as a capability-aware NXREG namespace edge.

Current security tranche: unauthenticated script mount/bind/unmount
and privileged kernel namespace verbs return denial/unavailability and
publish no copies. Kernel fork no longer copies forged /proc/PID/caps into
child grants. The Plan 9 script no longer invents sd00/sd01/sd02/sd99
devices; the minios demo boots with storage unavailable until a real
provider is admitted. AST_IMPORT also now contains a child library's
CTRL_RET so an already-loaded library cannot interrupt the importer.

This does NOT mean mounts/links work. Build the parent+name index,
object link-edge and namespace-root ABI, mandatory authenticated NXREG
transaction/WAL writer, per-principal mount view and Chan/Dev providers,
then 9P2000.L and ext4 independent tests. A positive test must prove:
same object ref across links, ordered mount selection and coherent fd/fid
reads, revocation/unmount with active leases, no alias to unauthorized
memory, masked visibility, old-or-new transaction root on errors.
Preserve full Plan 9 behavior; do not add a private fake namespace.
