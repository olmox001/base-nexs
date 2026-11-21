# P4: NXEC REG transport and NXEXEC process convergence

**Status: started, not completed.** A real hosted fork/queue defect was reproduced and a narrow prerequisite repaired. The REG wire format, authority, buffer-lease data plane and multi-ISA NXEXEC are not certified.

## Source-backed finding and code change

Originally sys/sysproc.c invoked reg_ipc_enable_pipes in both processes AFTER fork. Because each process had already received a private copy of the RegKey tree, both created unrelated pipe pairs. The actual NEXS language test observed a PREEXIST message but no CHILD_SENT message: PREEXIST, nil, P4_PARENT_DONE.

The existing implementation now prepares the SAME queue pipe descriptors BEFORE fork. Both processes inherit them. The existing RegKey parent/child/next relationships are traversed with a bound derived from the actual tree rather than a fixed DFS stack. reg_ipc_enable_pipes returns failure instead of silently skipping an endpoint. Its pre-fork conversion transfers already queued Values before releasing their nodes, sets the provider writer nonblocking, and on capacity failure closes the tentative pipe while retaining the local queued data; fork is not started.

Actual result: PREEXIST, CHILD_SENT, P4_PARENT_DONE. Four live regression tests include preloaded queues, drained queue with no duplicate, child delivery, and provider capacity rejection without existing-message loss. These test the ONE actual registry and process provider, not a simulated interface. POSIX pipe opening, nonblocking admission and failed-attempt disposal are isolated in hal/hal_hosted.c, called via registry/include/nexs_ipc_transport.h. The REG common source does not call the platform pipe constructor directly. On OS1 and baremetal, hal/common/pipe_transport_unavailable.c explicitly rejects a POSIX pipe transport; only the future actual kernel/USERKIT REG provider can supply those endpoints. The provider is selected through config/sources.mk, not architecture #ifdef in the common implementation. No new syscall. Hosted compile, OS1 dual-architecture source/ABI checks, and baremetal guest tests were exercised for this transport-seam change; they do NOT validate full OS1 NXEXEC execution.

## Important remaining defects (do not certify as REG)

The old pipe_serialize_value and pipe_deserialize_value are recursive, not complete atomic checked frames, and contain unbounded allocations and partial read/writes. Shared pipe direction is not a capability-scoped endpoint. No complete credit accounting exists for the pipe; no typed request/response, negotiated envelope, endpoint session, correlation/generation, revoke, cancellation or secure mapping. Do not treat this legacy pipe serialization as NXEC REG compatibility.

The NXEC source docs/REG_PROTOCOL.md explicitly reports DESIGN / FORMALIZATION REQUIRED and an unfinished wire layout. Never make up version compatibility. Required wire fields: major/minor; header+total lengths; request type/opcode and reserved flags; session/endpoint; request and correlation ID, object ID+generation, scoped capability, required rights, bounded payload or leased-buffer descriptor offset+length; deadline, idempotency and auditing. All offsets/endian widths must be independently verified before commit.

CANCEL and Tflush-style acknowledgment must distinguish CANCELLED before effect publication from TOO_LATE/ALREADY_COMMITTED; timeout alone does not cancel. Shared memory uses the EXISTING NXOLMO MPMC ring/leased buffer model, not a new channel registry or unverified C pointer passed to another address space.

## P4 dependency-ordered phases

| Phase | Work | Acceptance |
| --- | --- | --- |
| P4-A (current) | Repair inherited hosted IPC provider and preserve queued state on pipe admission failure | Actual fork parent/child plus negative/full-provider tests |
| P4-B (blocked by P3) | Port canonical NXOLMO object identity, authority, rights/epoch verification into the ONE registry | Stale/reforge/revoked principal failures; authorization at access and commit |
| P4-C | Finalize NXEC REG wire and independent length-first iterative parser; remove legacy recursive pipe Value encoding | Version negotiation, hostile frames, no pointer/callable transfer, endian vectors |
| P4-D | Reuse NXOLMO buffer/MPMC ring with bounded credit and exact lease/owner lifecycle | Backpressure, reorder, cancellation, owner crash, shared map safety |
| P4-E | Integrate NXEXEC/execsvc and actual OS1 nxttyd/CONSOLE object without duplicating broker or keyboard ownership | argv/env/cwd/ctty/fd inheritance, detached/foreground, job control, restarts |
| P4-F | End-to-end both ISA proof | Clean build, QEMU full image make run, nxadb interactive tests and graphical validation, strict migrated Power12 |

Every stage must have a two-test minimum per trusted function plus race/fault/negative tests where relevant. Reject a protected operation if canonical authority is not yet available; local in-process fast paths do not bypass checks. Existing NEXS language service source lives under nxlib/services; native C extension under extensions; no duplicate VFS, registry or transport.

SMD/SPP is researched separately as a placement policy on existing NXOLMO provider and lifecycle objects. It may use REG messages when authoritative references and buffer lease are in place, but cannot provide its own security source of truth.

## Current exit status

P4-A is a real partial change. P4-B through P4-F remain NOT IMPLEMENTED/UNQUALIFIED; overall P4 exit is BLOCKED. Read-only NXOLMO/NXEC and external NexsOS1 sources have not been altered.

## P4-A final qualification — checked product source

After the latest implementation/source-catalog rebuild:

- make power12-phase0: **PASS 25/25** inventory/AST/ownership tests,
  including source crosscheck and explicit no-false-port classifications.
- make test: **PASS 165/165** Python tests, 33/33 NEXS syntax
  checks, 17/17 executed NEXS examples; separate 8,798 real
  production C registry integrity checks PASS.
- tests/test_p4_prefork_ipc.py: **PASS 4/4** for actual fork,
  inherited queue delivery, duplicate rejection and failed-pipe
  admission without lost messages.
- make os1-check: **PASS AMD64 and ARM64**, both native
  compiler ABI checks and parallel AOT sessions.
- Target baremetal-amd64/arm64 compilation followed by actual
  QEMU execution: **PASS both** and matching verified
  guest UART text-buffer output, no guest faults.
- Host make debug real fork and registry C ASan/UBSan: **PASS**.
- NXOLMO read-only make test-unit: **PASS** MPMC ring, rehash,
  buffer concurrent leases, unit and VM example driver.
- NXEC preboot and kernel make test-host: **PASS** independent
  HCD/admission host reference tests.
- make power12-strict is **expected FAIL** until all 347
  tracked blockers are resolved. It cannot be waived.
- Full NexsOS1 umbrella make run plus nxadb on both ISA is
  still **NOT RUN** due separate read-only umbrella missing
  user/NexsOS1-main/portability/doom/doom.mk.

This is the exact evidence for the narrow P4-A provider repair,
not evidence for the still-unimplemented P4-B through P4-F.
