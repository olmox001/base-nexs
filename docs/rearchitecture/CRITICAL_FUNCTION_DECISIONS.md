# Critical function-by-function findings and exact redesign decisions

This is the manually curated priority subset of FUNCTION_CATALOG.csv.
It does NOT represent manual verification of all source-indexed definitions.
The original P0 index contained 4,062; the rolling P1 index currently
contains 4,129. See INVENTORY_MANIFEST.json for authoritative counts.
Findings marked CODE were checked against implementation text in this
checkout; SPEC describes a documented architectural gap. A function's
observed lexical signal is not sufficient alone to establish a defect.

## Base-NEXS: source-confirmed problems

| Function(s) and source | Verified limitation | Canonical redesign | Mandatory falsifiable gate |
|---|---|---|---|
| buddy_alloc_node, buddy_alloc; core/buddy.c | CODE: recursively traverses one global buddy tree over one compile-time memory pool | Per-instance, page-backed allocation with metered reservations; bounded iterative allocator; generation-safe object ownership | Exhaust page provider, fragmented page tests, multi-instance simultaneous allocation |
| buddy_free_node, buddy_free | CODE: caller hands raw pool pointer; lifetime determined by global tree | Versioned page/arena lease, checked ownership and reclaim | Foreign-pointer, stale/double-release and concurrent reclaim tests |
| xmalloc, die | CODE: allocation failure calls fatal exit/halt | Typed OOM result; unwind active transaction; contain failure in requesting task | Memory exhaustion preserves other task and committed root |
| buddy_strdup, val_str, val_ref, val_ptr | CODE: NULL-terminated copies allocated from the global pool | Length-bearing object data spans; mapped buffer with lifetime and COW | Embedded zero, multimegabyte value, COW and lossless serialization |
| buddy_next_pow2 | CODE: unchecked doubling can overflow size_t | Checked reserve/rounding based on provider and available capacity | Boundary at largest size_t and provider rejection |
| page_alloc/page_free; core/pager.c | CODE: page allocator selected through compile-time profile and raw pointer APIs | Canonical page object with generation, owner, map/lease and typed fault | Map/unmap across domains, page sizes, stale lease, OOM and revalidation |
| arr_get; core/dynarray.c | CODE: scans mutable global array pointer table linearly | Per-runtime named-object index, no global array registry | Lookup complexity/latency and cross-runtime isolation |
| arr_get_or_create | CODE: exceeds MAX_ARRAYS then calls die; name truncated to NAME_LEN | Resource-derived array admission; full-length key indexing | Create beyond old ceiling; no cross-name collisions; quota failure |
| arr_ensure_cap | CODE: repeated shifts without checked overflow and fatal allocation | Segmented capacity planning, checked arithmetic and page allocation | Very sparse/high index, overflow and atomic resize failure |
| arr_set, arr_delete, arr_unref, arr_free | CODE: no concurrency protocol for mutable refcount/elements | COW page views, leases, per-array revision and atomic publish | Concurrent mutation/read, generation, delete with outstanding lease |
| val_to_int, val_to_float; core/value.c | CODE: atoll/atof conversions do not report malformed or range failures | Typed parse with bounds/error and exact promotion rules | Invalid, overflow, NaN/Inf, locale and 32/64-bit portability vectors |
| val_equal, val_clone, val_free | CODE: mixed pointer/owned value representations | Generation-aware deep/shallow clone policy, cycle-safe graph traversal | Alias graphs, self-reference, ownership and leak tests |
| make_tok, lexer_next; lang/lexer.c | CODE: Token.text[NAME_LEN], make_tok strncpy, silently truncates valid literals to 63 bytes on default profile | Borrowed immutable source slices and decoded length-bearing string objects | Reproduce 80->63 bug, then exact 80/8 KiB/1 MiB and invalid UTF-8 |
| lexer_next numeric parsing | CODE: atoll/atof after token scan | Checked integer/float semantics in lexer/IR | Numeric boundaries, no partial token acceptance, target parity |
| lexer_next registry path | CODE: path copied via NAME_LEN token even when REG_PATH_MAX is longer | Independent variable-length path-token representation | Long path and distinct-prefix/no alias test |
| parser_advance, parser_expect; lang/parser.c | CODE: lexer error is mapped through fixed string buffer | Structured error(span, code, context), no truncation | Exact source coordinates and exception propagation |
| parse_primary, parse_binop, parse_stmt, parse_program | CODE: recursive parsing, a large parse_stmt implementation, fixed AST fields/limits | Iterative parser/IR builder and bounded parse workspaces | Deep nesting, hostile grammar corpus, bounded-step assurance |
| ast_free, ast_free_safe | CODE: recursion and special ownership transfer of function body | Arena/region lifetime or iterative DAG cleanup with explicit ref policy | Deep AST, duplicate function body, aborted registration |
| eval_node, eval_block; lang/eval.c | CODE: nested recursive host calls and unbounded user loop | Explicit VM frames, per-step fuel, preemption and cancellation | No host stack overflow, bounded instruction slice, parallel tasks |
| eval_file, eval_str, eval_str_lib | CODE: execution also owns import/AST teardown and global namespaces | Versioned source module object and isolated runtime context | Concurrent imports, circular imports, per-principal auth |
| fn_table_init, g_fn_table; lang/fn_table.c | CODE: global flat singleton function array | Object-indexed callable registry and runtime-specific dispatch cache | Parallel instances, hot reload, generation/stale symbol |
| fn_register, fn_lookup, fn_unref | CODE: linear lookup, integer function indexes and manual mutable refcount | Callable object ID, schema/effects, indexed names and safe quiescence | 100k symbols, concurrent bind/unbind, stale index rejection |
| regkey_find_child, reg_lookup; registry/registry.c | CODE: child linked-list linear scan; paths copied into REG_PATH_MAX arrays | NXOLMO indexed parent+name key; immutable verified path spans | Large sibling sets, cycles, long path and collision/rename stress |
| reg_set | CODE: accepts a caller-provided rights byte and assigns k->rights without checking caller's capability | NXEC authority callback, immutable rights ceiling, transactional write | Unauthorized overwrite/rights amplification must fail |
| reg_delete | CODE: linked-parent detach and destruction without caller capability check | Generation-based revoke/unlink/retire with lease fencing | Unauthorized delete, descendant and concurrent reader tests |
| reg_set_ptr | CODE: calls reg_set(path, val_ptr(target), RK_ALL) | Typed reference with rights from actual held capability, not RK_ALL | Unauthorized pointer creation cannot elevate authority |
| reg_get_deref | CODE: 64-hop fixed limit; REG_PATH_MAX copy; path-based pointer deref | Cycle detection by visited object IDs with resumption and rights at each deref | Deep valid chain, cycle and revoke during traversal |
| reg_move, reg_mount, reg_bind, reg_unmount | CODE: path-based mutable graph operations distinct from NXOLMO node identity | Single transactional namespace/mount relationship engine | Cross-namespace permission, old/new root atomicity, rename race |
| pipe_serialize_value, pipe_deserialize_value; registry/reg_ipc.c | CODE: recursive value traversal; home-grown pipe wire and numeric type representation | Canonical object-descriptor wire, verified iterative bounded codec | Cyclic/deep arrays, malformed packet, endian/generation fuzz |
| reg_ipc_enable_pipes | CODE: fork copies registry state, then upgrades all queues to POSIX pipes | Per-endpoint shared-buffer transport independent of fork | Two real processes, owner crash, credit exhaustion, no duplicate IPC |
| nexs_rfork; sys/sysproc.c | CODE: hosted fork path and global pipe conversion | Execution provider with explicit task/process cloning semantics | Parent/child authority, inherited handles and separation tests |
| nexs_exec, nexs_await | CODE: process API tightly coupled to hosted environment | Object executor, futures and status/owner handles | Spawn/exit/restart/cancel, environmental inheritance |
| sysio_init, fd_alloc; sys/sysio.c | CODE: interpreter FD facade is independent from capability object identity | FD as principal-local capability handle for file/console/pipe | Cross-process grant, close/reuse, double close, concurrent IO |
| nexs_runtime_init; runtime/runtime.c | CODE: memset of global buddy_pool/tree, global registry and function table | RuntimeInstance_create with owned provider and module namespace | Two runtimes in same process; destroy one while other works |
| load_directory_recursive | CODE: recursive host directory scanner and fixed buffer probe; autoload executes code | Verified module graph and iterative dependency resolver | Import cycles, unlimited logical tree depth via continuation |
| nexs_repl; runtime/main.c | CODE: one REPL context; terminal adapter platform-specific | Concurrent execution sessions and capability-bound console object | Parallel REPLs, multiple TTYs, no double input |
| os1_registry_get_builtin; hal/os1/os1_driver.c | CODE: uses 128-byte local value for OS1 registry get | Length-delimited, negotiated object read or explicit larger-data protocol | Valid value above 128 bytes and capability denial |
| nexs_os1_tty_read; hal/os1/tty.c | CODE: uses inherited console object and bounded polling; correct OS1 ownership seam | Keep nxttyd authority, move event wakeups to provider-driven async wait | Concurrent shells, foreground switch, no keyboard double-read |
| mmu_virt_to_phys; hal/hal_hosted.c | CODE: hosted stub casts virtual pointer to physical address | Never treat hosted pointer as physical; require typed provider mapping | Hosted negative hardware mapping test |
| nexs_compile_native; compiler/driver.c | CODE: constructs and runs Make shell string via system() | Manifest-driven exec argv with path, permission and output validation | Metacharacter path cannot execute arbitrary shell |
| nexs_compile_file_ex | CODE: shell command builder, predictable /tmp by PID, duplicated source list | Single target source graph, secure temporary objects, checked subprocess status | Concurrent compiler invocations, malicious path and missing sysroot |

## NXOLMO: algorithms to preserve, extend or reject

| Implementation or functions | Current scope and limit | Canonical convergence | Mandatory gate |
|---|---|---|---|
| nx_object_ref_equal / nx_object_ref_is_valid; src/base/types.c | CODE: 128-bit ID + 64-bit generation is available | Single canonical identity for callable/page/file/task/mount | Wire vectors, stale/recycled object |
| nx_state_store_create/read/update/delete; state_store.c | SPEC+API: tested slot store, index and generations; not full MVCC/name graph | Indexed named root, transactions, version chains and durable update | Linearizable concurrent histories and crash/restart |
| nx_state_store_child_first/next/count | CODE: generation-based child traversal; no parent+name index | Capability-scoped ordered/hash path-component lookup | Millions of siblings under memory budget, stale cursor |
| nxreg_api_validate, nxreg_client_bind; nxreg.c | CODE: versioned read API negotiation exists | Extend same ABI family to all typed object/transaction operations | Negotiation/mixed-version negative tests |
| nxreg_dispatch; nxreg.c | CODE: only object-read/child-first/next/count cases exposed | Complete operation table and canonical REG transport mapping | Unauthorized mutation, unknown opcode, concurrency |
| nx_state_schema_registry_seal; schema.c | CODE: sealed digest state; not signed dynamic module authority | Authenticated, versioned code/schema/module manifest | Signature rollback, unauthorized schema update |
| nx_state_migration_execute; migration.c | CODE: bounded COPY/ZERO/LITERAL H0, not general execution VM | Verified state-transition bytecode, fuel, atomic staged write | Malformed bytecode, repeat deterministic replay |
| nx_state_snapshot_pool_acquire/view/release | CODE: bounded retained detached snapshots and leases | MVCC epoch reclamation, process-shared owner-death handling | Forced reader death and quiescence/reclaim race |
| nx_buffer_manager_*; buffer.c | CODE: caller-owned arenas/slot/lease and CRC/digest, not full page object service | Pin/map/share/COW/revoke cross process/device | Alias, DMA coherency, lease exhaustion and revocation |
| nx_buffer_ring_*; buffer.c | CODE: bounded MPMC transport primitive | REG data-plane endpoint with credits/correlation | Concurrent producers, fairness, cancellation, wraparound |
| nx_operation_budget_*; runtime/budget.c | CODE: per-operation bounded budget H0 | VM/scheduler/registry/storage universal deterministic accounting | Undercharge detection and fuel exhaustion |
| nx_cycle_counter_*; runtime/cycle_counter.c | CODE: x86 RDTSCP only when available | Provider time/per-CPU counter fallback with UNKNOWN | AMD64/AArch64 clock qualification and CPU migration |
| nx_capability_issue / nx_capability_verify; authority/capability.c | CODE: software token H0; missing kernel authority lifecycle | NXEC delegated capability/revocation/nonce/lineage | Forge/replay/revoke races and domain crossing |
| nx_hardware_grant_from_token, nx_hardware_dma_validate | CODE: bounds and token validation without real IOMMU/mapping | Capability-driven page/device grant at real backend | Bad DMA rights, no-IOMMU bounce and teardown |
| nx_block_device_read/write/flush; block/device.c | CODE: checked block provider dispatch | Real device/host-file persistence and completion | Power-loss, torn write and actual fsync |
| nx_journal_begin/write/commit/recover; journal/wal.c | CODE: H0 WAL tested in memory backend | One root atomic publication and restartable durable log | Multiple crashes during replay, idempotent recovery |
| nx_ext4_superblock_decode/validate; ext4/superblock.c | CODE: tested metadata feature checks | Preserve verified parser, extend feature admission | Independent mke2fs/e2fsck vector suite |
| nx_ext4_directory_lookup; ext4/directory.c | CODE: read-side linear/HTree lookup, not complete writable VFS | Preserve optimized read algorithm; add transactional mutation | Huge dirs, rename/fsync/crash and casefold interoperability |
| nx_ext4_inode_checksum_compute; ext4/inode.c | CODE: checksum and metadata read path | Authenticated inode writes + zero-copy buffer cache | Corrupt inode, fsck and crash roundtrip |
| nx_9p_frame_decode/encode; protocol/9p/frame.c | CODE: seven-byte common envelope only | Full native 9P2000.L codec/fid/session/cancel transport | Independent 9P peer, malformed message fuzz |
| src/filesystem/9p/ Linux snapshot | CODE: unported Linux implementation with goto; not linked as native provider | License-reviewed algorithm port, remove unadapted copy from product after test parity | Existing NXOLMO Power-of-Ten gate failure cleared without hiding code |

## NXEC and NXEXEC: exact role, not speculative implementation

| Source/contract | Verified reality | Integration decision | Mandatory gate |
|---|---|---|---|
| NXEC preboot/common/hcd_parser.c and hcd_semantics.c | CODE: implemented typed firmware descriptor validation | Feed canonical NEXS boot ResourceProvider; preserve independent kernel-side verification | Host hostile HCD corpus and both ISAs |
| NXEC preboot/common/hcd_builder.c, memory_plan.c | CODE: HCD builder and memory-plan algorithms | Platform memory discovery and page-object admission; no static RAM ceiling | UEFI/Multiboot2 variable maps and CRC mismatch |
| NXEC preboot/hal/{uefi_memory_map,multiboot2_memory_map}.c | CODE: normalizes firmware maps | Select one preboot provider per boot target | Overlaps, reserved memory, malformed tags |
| NXEC preboot/hal/amd64/isa.c, arm64/isa.c | CODE: ISA-specific discovery/capture | Typed CPU topology/timer/memory contracts; no ISA #ifdef in neutral code | Host/cross compilation plus real QEMU firmware |
| NXEC kernel/core/hcd_gate.c | CODE: independent kernel admission | Maintain trust boundary and compare two HCD parsers on same hostile bytes | Independent accept/reject and state-reset tests |
| NXEC docs/REG_PROTOCOL.md | SPEC ONLY: message/fid/lease/cancel envelope is unimplemented as shared production runtime | Implement REG over canonical NXOLMO buffer ring and NXREG operation service | Multi-process request/cancel/revoke/commit races |
| NXEC docs/SECURITY_DOMAINS.md | SPEC ONLY: domain inheritance/revocation requirements | MACHINE/SYSTEM/ROOT/USER/SHARED/DRIVER enforcement policy | Domain isolation and forged handle tests |
| NexsOS1 nxexecd service execsvc_parse, execsvc_run_spawn | CODE: real owner-preserving broker and bounded payload/args | Execution object operations with dynamic resource-derived request segments | Owner, cwd/env/ctty/argv, crash/retry and privilege tests |
| NexsOS1 SDK execsvc_spawn, execsvc_launch_* | CODE: client/owned/foreground/detached contracts | Platform execution provider, not an independent second NEXS broker | Local versus brokered semantics differential |
| NexsOS1 nxttyd and OS1 CONSOLE ABI | CODE: OS1 authority lives in TTY provider/kernel handles | NEXS TTY adapter delegates, never claims keyboard input globally | Multiple window/shell focus and input ownership |
| NexsOS1 kernel/HAL, object, VFS, ext4 | CODE: useful device, object and provider examples, separate project | Selective semantic comparison only; NEXS canonical implementation owned in Base-NEXS | Build/execute both arch with unchanged NexsOS1 |

## Cross-cutting implementation decisions

- **NO FIXED LOGICAL STACK:** frame/continuation segments are dynamically
  admitted page objects. Each scheduled quantum remains bounded. Native
  host stack stays OS-managed and protected.
- **NO GLOBAL POOL OR GLOBAL LOCK:** per-runtime allocators, sharded NXREG
  indexes, snapshot readers and per-endpoint queues; measured lock pressure.
- **GLOBAL REGISTRY FOR ALL CALLABLES:** register typed immutable callable
  metadata for every runtime-exposed/private function; local invocation
  resolves a generation-checked dispatch cache, not a string path per call.
- **SERIALIZED POINTERS ARE NOT ADDRESSES:** object ref + generation + range
  + lease/capability, with rights/epoch checked at the mapping boundary.
- **ONE EXECUTION PIPELINE:** source/IR/verifier/native AOT/VM/linker share
  op IDs, types and effects; no shadow C transpiler semantics or HALBC bypass.
- **NO PARALLEL VFS OR CAPABILITY STACK:** NXOLMO metadata/WAL, NXEC
  authority/REG, NXEXEC execution contracts and NEXS language combine under
  one object model, one platform selection and one transaction path.
- **POWER12 AND NASA DISCLOSURE:** original NASA P05 asserts requirement
  conflicts with the project's assertion ban. Never claim literal compliance
  while that deviation exists; independently checked invariants are the
  project assurance strategy.
