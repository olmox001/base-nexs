# NEXS Unified Runtime/Kernel Architecture — Target Contract

Status: design proposal, NOT an implemented architecture.
Source baseline: Base-NEXS 15ee3c1, NXOLMO 8b21198, NXEC 713f2c6;
NexsOS1 SPERIMENTAL-LINUX is a read-only engineering reference.
Decision scope: a full Base-NEXS product redesign. No legacy dual-stack.

## 1. Product identity and invariant

NEXS is simultaneously a language, compiler/linker, object runtime, execution
engine, and optionally a freestanding capability microkernel. These are *build
profiles and authority domains of one semantic system*, not independently
invented object/registry/process models.

One object identity and state-transition model is mandatory across all profiles.
Every executable function (including a private runtime callable), compiled
code image, symbol, array, element view, page, memory map, process, thread,
file, directory, mount, endpoint, buffer, transaction, device and resource
has a canonical typed metadata descriptor and a versioned operation contract.
The registry indexes EVERY runtime callable and its public/private visibility,
even when its descriptor is lazily instantiated. Private implementation helper
functions remain non-invocable outside their authorized scope and may be
cataloged in immutable build metadata without per-call object allocation.
Internal temporary scalar values can be unregistered only if they never
acquire observable or transferable identity.

Global registry *means a coherent logical registry*, never a single global
lock, a global pointer tree, a singleton interpreter context, ambient memory
access, or one global authorization token. An object lookup is not authority.
No operation accepts raw cross-address-space pointers as an object reference.

Primary identity: NXOLMO's 128-bit nx_object_id (high, low) plus its 64-bit
generation in nx_object_ref. Generation and schema version have different
purposes. Principal-specific handles/capabilities resolve that reference via
authorized, generation-checked local tables. Do not use path/PID/FD/fid/slot
number as canonical identity. Object IDs are stable within their defined store
and namespace; cross-store import/reconciliation needs an explicit origin map.

The NEXS registry carries metadata, schemas, callable bindings, relationships,
object-state revisions, leases and committed-root digests. Actual high-volume
bytes stay in page/buffer objects with rights-checked references. A function
call must not stringify a path or perform IPC if a verified direct local fast
path exists. Fast paths still revalidate authority at their defined boundary.

## 2. Unified object record and lifecycle

Logical descriptor fields:
- object reference, owner principal, domain, type ID, schema ID/version,
  lifecycle state, generation, state revision/epoch and object relationships;
- public operation and ABI identifiers, capability requirements, scope/rights,
  lease/revocation epoch and policy/version references;
- storage/buffer references or immutable content digest, with explicit
  ownership, lifetime, pin count, coercion and copy-on-write policy;
- workload/accounting descriptor: current budget, memory commitment, bytes,
  pages, requests, execution fuel, cancellation, retry and deadline state.

Allowed transitions are type-specific and versioned: NEW -> ADMITTED ->
ACTIVE -> QUIESCING -> RETIRED -> RECLAIMED. Publication and commit have
separate linearization points. A stale reference can never resolve a recycled
object or authorize a different generation. Lease return, revocation and
quiescence precede reclamation. Parent/child/name/mount relationships cannot
silently create cycles. Relationships may be sharded or indexed; the canonical
logical graph remains unique.

A first-class callable is a code-object reference with a stable symbol ID,
call signature, effect class, ABI version, owning module, executable image/IR
version, authority requirements, and dispatch slot generation. Its local
address is a private, ephemeral cached mapping, never a serialized pointer.
Calls may be synchronous, asynchronous, message-based, or remote; the
authorization and result semantics are identical.

## 3. Memory: page-backed, expandable, NUMA/SMP aware, no global pool

Retire core/buddy.c's global memory_pool/buddy_tree, process-exiting xmalloc,
and global array table. Replace them with a resource-provider ABI that supplies
page-object reservations; per-runtime/per-domain/per-thread allocator contexts;
caller-visible allocation results; explicit ownership; and resource-derived
budgets. The data structures may grow via incremental indexed slabs,
page extents, segmented arenas and mappings. Nothing presupposes a fixed
process/thread/array/stack/table capacity or a 4/16/64 MiB global pool.

No physical platform has infinite memory. *No arbitrary architectural ceiling*
means that logical requests are chunkable/growable, representational limits
are explicit, and exhausted resources return NO_MEMORY/QUOTA/WOULD_BLOCK
without corrupting other runtime instances. Memory growth is a checked
resource-admission transition. Reserve before commit; abort unwinds cleanly.
Critical trusted-operation paths use pre-reserved bounded workspaces; optional
growth occurs at explicit, separately metered admission boundaries. This
reconciles safety-critical no-allocation-after-operation-start with a dynamic
runtime without pretending NASA's Rule 3 is literally satisfied by unlimited
heap allocation.

Typed arrays are object-backed segmented collections with optional dense,
sparse, multidimensional, immutable, shared and COW representations chosen
from observed workload and declared semantic requirements. Elements and
slices have checked ranges, lengths, schemas and generation. Small immediate
scalars need no heap object; externally shared arrays and all transferable
buffers have canonical object references.

Zero-copy is a *conditional protocol*, not a blanket promise:
1. request a range lease with rights, generation, mutability and cache mode;
2. validate alignment, page ownership, mapping and principal boundaries;
3. pin physical/logical backing, map the range to the borrower, and publish
   only offset/length/lease IDs in shared rings or serializable descriptors;
4. enforce coherence and write-after-read/COW policy, revalidate revocation
   at mapping/use/commit; release and reclaim after quiescence;
5. copy through a bounded provider when a range cannot safely be shared
   (cross-host, incompatible alignment/coherency, untrusted device/DMA).
Never serialize virtual pointers, expose kernel addresses or grant DMA without
validated IOMMU/domain mapping or checked bounce-buffer fallback.

## 4. Concurrent execution: no one-thread assumption or fixed stack

Every RuntimeInstance is independently creatable, destructible, reentrant
and independently authorized. It owns registry namespace roots/caches,
scheduler context, allocations, module bindings, IO endpoints, configuration,
metrics and structured diagnostics. No implicit process-global runtime state.

Execution uses typed continuations/VM frames stored in growing, accounted
segments (not unbounded C recursion or a fixed giant stack). Every call,
loop, import, iterator, FFI operation and task consumes deterministic work
fuel and supports cancellation/deadline checkpoints. Stack growth is
admitted from pages; exhaustion returns a resumable/typed failure, never an
unbounded C stack overflow. Native code still executes on OS-defined safe
thread stacks with overflow protection; "no fixed stack" means no arbitrary
*NEXS language execution* stack ceiling.

Tasks, threads and processes are first-class registry objects with
parentage, owner, lifecycle, capabilities, owned handles and budget. Executor
scheduling supports multiple worker threads/CPUs, affinity and CPU-hotplug
where provided, bounded per-worker queues, work stealing or other measured
policy, fairness, priority handling, cancellation and supervision.
Thread safety cannot rely on g_registry, g_fn_table or global heaps.

An in-process function call may dispatch through a validated cached slot.
Cross-process execution uses NXEC REG endpoint semantics over NXOLMO
shared-buffer rings or native capability transport. Cross-machine execution
uses explicit transport codecs and copies where true zero-copy is impossible.
Async operations return futures/completions with correlation, idempotency,
commit point and separate CANCELLED versus ALREADY_COMMITTED statuses.
On executor crash: revoke/close authority, resolve outstanding leases,
restart services from committed state, and report non-idempotent outcomes.

## 5. Language, IR, compiler, linker, executor: ONE semantic pipeline

Source (.nx) -> span-preserving UTF-8 lexer -> bounded parser ->
canonical typed/effect-aware IR -> verifier -> optimizers -> one or more
execution backends (portable verified VM/bytecode; native AMD64/AArch64
AOT; optional validated hosted JIT). All backends share one intrinsic/
object/capability ABI and effect semantics. The present C code generator
is migration input, not a permanent second language semantics implementation.

Lexer tokens borrow source ranges with lengths and owner lifetime; decoded
string bytes have an explicit buffer object. Never truncate a valid literal,
name, path or message to NAME_LEN/REG_PATH_MAX. Errors carry exact source
span, expected grammar set, stable code and cause chain. Large programs are
parsed in bounded chunks; recursion is removed from trusted parse, AST free,
evaluation and object traversal. UTF-8 and binary string rules are explicit.

IR must type callable/resource references, effects, ranges, access rights,
source maps, gas/fuel, exception edges, lifecycle transitions and suspension
points. The verifier rejects malformed offsets, illegal opcodes, invalid
stack/control flow, unchecked host imports, privilege escalation, cycles
where forbidden and unbounded trusted work. A function manifest declares
its API and resource dependencies. NXB v1 is NOT adopted as-is: its stated
16-byte header conflicts with the listed field widths, and the declared
nexs_encode_bc/nexs_decode_bc/nexs_free_bc are not defined in current code.

The linker resolves symbols through the verified callable-object registry,
not a global function-name array. It links code, libraries, FFI exports,
schema migrations, capability imports and versioned dependencies as objects.
Native relocations remain native platform artifacts but their authority
binding is mediated through the same validated module descriptor.
Unloading/replacing code requires quiescence and generation changes;
running frames either complete on an old pinned image or switch at a safe
point under a declared upgrade policy. Hot reload must not invalidate an
active function body or stale pointer.

All toolchain execution uses argv-based subprocess APIs with no shell
interpolation, no predictable shared /tmp names, checked exit status,
bounded tool output and reproducible target manifests. Build-time and
runtime compilation use one codegen/IR inventory. Unsupported compilers,
targets, JIT W^X permissions or firmware providers fail explicitly.

## 6. NXOLMO, NXEC and NXEXEC become one implementation contract

Canonical technology selection is *per operation*, not per repository:
- **NXOLMO State Store / NXREG**: adopt ID+generation, checked ABI, schema,
  incremental indexing, snapshots, budget instrumentation, typed read API.
  Complete names, isolation, transactions, durable publication and MVCC.
  There is one canonical state engine; migrate/rewrite Base-NEXS reg_* and
  its callers, then remove the second linked-node registry.
- **NXOLMO buffer, block, WAL, ext4 and 9P**: retain tested semantics and
  progressively integrate through the same object/ref/transaction graph.
  Do not start independent file, page, queue or capability subsystems.
  Linux 9P snapshot must be audited/ported or removed, not silently compiled
  as product code.
- **NXEC REG**: implement its versioned wire/session/endpoint/cancel/lease/
  correlation semantics as a transport over the canonical object runtime.
  REG is the communication protocol; NXREG is the canonical state ABI.
  Do NOT conflate them or add a second registry server.
- **NXEC authority/security domains**: adopt MACHINE/SYSTEM/ROOT/USER/
  SHARED/DRIVER distinctions, attenuation, revocation fences, per-principal
  mapping and kernel-only hardware authority. NXOLMO H0 token algorithms
  are evidence/inputs, not a complete production authority kernel.
- **NXEC preboot/HCD/HAL**: integrate validated hardware descriptors,
  UEFI/Multiboot2 normalization, memory planner and independent admission
  verification into NEXS freestanding profiles. Independent boot/kernel
  trust-boundary validators may intentionally share a *format* but not one
  mutable trust context; test their agreement and rejection behavior.
- **NexsOS1 nxexecd / execsvc**: adopt owner-preserving spawn, argv/env/cwd,
  inherited/attenuated capabilities, redirections, terminal/session rights,
  job supervision, failure replies and service lifecycle. Rebuild as NEXS
  execution objects and adapters rather than importing hardcoded broker
  body/path/argument ceilings as universal rules.
- **NexsOS1 object ABI/TTY/HAL**: consume actual NX-SDK object/capability
  interfaces on OS1 without adding syscalls or duplicating nxttyd ownership,
  and use OS1 kernel/HAL only as a comparative contract. Hosted, seL4 and
  baremetal configurations use different providers for the same semantics.

Each production algorithm has ONE canonical implementation owner in the
Base-NEXS build graph. Upstream NXOLMO/NXEC snapshots remain pinned
read-only until function-level promotion and provenance/license review.
Do not deploy a new implementation alongside an active old one: migrate
all callers in the same bounded work package, compare results and remove the
retired implementation from compilation. Temporary differential test binaries
are acceptable; two competing production state stores are not.

## 7. VFS, registry and persistence are one state machine

NXREG is the source of truth for namespace identity/relationships,
object generations, authority-independent metadata and transactional root
versions. The VFS is the path/mount/file-handle projection. ext4, 9P and
other filesystem providers implement typed operations under the same
transaction context; no duplicate path authority, journal or inode identity.

Path lookup produces an object reference only. Authority is then checked
against the caller's namespace, principal, capability, rights, revision and
revocation epoch. Mount/bind/symlink boundaries recheck policy. 9P fids and
Qids map to session-scoped refs and remain non-authoritative. Reads can use
immutable snapshots and page leases; writes stage private dirty pages and
metadata, validate expected revisions and capabilities, durably append/flush
WAL, atomically publish the new root, and only then expose completion.
A cancelled post-commit operation reports ALREADY_COMMITTED.

Do not mistake current NXOLMO ext4 metadata readers for a writable ext4
filesystem. Complete path resolution, hard/symlink semantics, directories,
ACL/xattr, extents mutation, crash recovery, fsync, feature gates and actual
host-tool round trips. 9P currently has only a native frame envelope and
unadapted Linux filesystem files; full 9P2000.L negotiation and fid/transport
semantics must be implemented and tested. Persistent backends require real
restart and interrupted-write fault tests, not only in-memory simulated WAL.

## 8. HAL / kernel / userland portability without duplicated semantics

Define one typed ResourceProvider and ExecutionProvider ABI with versioned
functions for page reserve/map/unmap, clock/timer, task/thread process,
events, shared buffers, files, durable block IO, hardware resources,
diagnostics and capability enforcement. Select platform provider at build
composition / boot registration; no platform-selection #ifdef inside
architecture-independent production logic. Hardware-specific ISA code
stays in ISA HAL targets.

Profiles: macOS AMD64/ARM64, Linux AMD64/ARM64, NexsOS1 AMD64/AArch64,
freestanding AMD64/AArch64 (and RISC-V when qualified), seL4/Microkit.
OS1 runs NEXS in userland through existing kernel-issued object handles.
The freestanding NEXS kernel profile implements the minimal privileged
page/IRQ/scheduling/object authority layer, with VFS, shell, registry
state services and drivers running at explicitly selected privilege.
No host libc assumption enters platform-neutral files.

Kernel/kernel-only build, full image boot, interactive console, VFS import
and compiler AOT are distinct acceptance targets. Physical driver access is
never silently emulated by hosted HAL functions. Emulator and native
hardware evidence are reported separately.

## 9. Safety, failure, isolation and performance

All operations return typed results: OK, NO_MEMORY, NO_SPACE, QUOTA,
WOULD_BLOCK, BUSY, STALE_GENERATION, ACCESS_DENIED, REVOKED, CONFLICT,
INVALID_FORMAT, UNSUPPORTED, CANCELLED and ALREADY_COMMITTED where applicable.
No fatal process exit on ordinary user allocation/parser/IO failure.
An irrecoverable MACHINE invariant fault is separately classified via
the platform fault/NXPaX contract.

Security and durability boundaries are explicit: parsing does not confer
execution permission; namespace lookup never creates authority; local
zero-copy mappings never survive lease/revocation; snapshots never override
capability checks; daemon death cannot leave resources indefinitely pinned;
CPU starvation and lock ordering are measured; service restart cannot
unconditionally replay an irreversible request.

Performance must be demonstrated, never promised. Per operation record
exact/count-based work, cycles when reliable (UNKNOWN otherwise), p50/p95/
p99 and throughput, allocations, lock contention, cache misses, bytes copied
versus leased, page pin lifetime, journal amplification and end-to-end cost.
Compare against current Base-NEXS and NXOLMO baselines with identical
fixtures. A proposed optimization is accepted only if semantics, isolation
and fault gates pass AND measured regressions are understood. Targets for
latency, throughput, memory and startup must be benchmark-derived, not guessed.

## 10. Non-negotiable release invariant

No release claim until: all production functions mapped/reviewed; all
standard and project-specific Power12 gates blocking and green; language
AST/VM/AOT differential conformance; multiprocess/multithread stress;
revocation/lease/owner-death/zero-copy proof; real persistent WAL recovery;
ext4/9P/VFS conformance; full native Linux/macOS targets; QEMU AMD64/AArch64
interactive boot; NexsOS1 nxadb on both architectures; no regression in
previously passing suites. Existing NexsOS1 failures must remain separately
reported. A pass in syntax-only or build-only tests is NOT runtime acceptance.
