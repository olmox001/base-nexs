# P2 — Canonical Callable Namespace and AST Lifetime

Status: code-level transition from duplicate fn_table/RegKey registries to
**one RegKey namespace** with owned TYPE_FN Values. This is not a claim that
NXOLMO StateStore/Authority, NXEC REG, or concurrent NEXS VM are complete.

## Replaced source functions, not a parallel subsystem

- lang/fn_table.c no longer defines g_fn_table[MAX_FN_DEFS], g_fn_count,
  fn_lookup_by_idx, fn_ref and fn_unref on a globally indexed slot array.
  It now implements reference-owned callable objects and a single
  registration/lookup path through the existing RegKey registry. Full
  AST bodies are freed by last-reference release, not by a hidden table.
  Builtin replacement creates an independent object; old aliases remain
  valid until their references are released.
- lang/include/nexs_fn.h describes only the registry-owned function
  object; the old fixed MAX_FN_DEFS ceiling is removed from all resource
  profiles in core/include/nexs_common.h.
- core/value.c and core/include/nexs_value.h implement owning/borrowed
  TYPE_FN Values, with checked reference admission and a language-owned
  retain/release/print hook (avoiding a duplicate implementation in core).
  TYPE_FN no longer embeds a pointer index in ival.
- lang/eval.c dispatch resolves ONLY the registry, respecting shadowed,
  deleted and replaced bindings; it retains an executing function across
  self-redefinition and deletion. Function-body ownership transfers
  only after a successful registry admission.
- lang/builtins.c, sys/sysio.c and sys/sysproc.c each register each
  built-in once at /sys/name. All second registration loops that
  republished the global table index were removed.
- runtime/runtime.c installs callable Value lifetime hooks; runtime/repl.c
  :fn introspects registry keys instead of listing a parallel table.
  registry/registry.c no longer has its own legacy direct BuiltinFn
  insertion path. There is ONE authoritative name index for callables.
- registry/reg_ipc.c and hal/sel4/sel4_ipc_bridge.c reject serializing
  local TYPE_FN objects directly. Arbitrary native pointers and historical
  fn_table indices must NEVER cross address spaces as object identities.

## Actual verification and invariants

- tests/test_callable_registry_ownership.py has 15 real interpreter
  integration cases: successful calls, redefinition, deletion, shadowed
  builtins, alias lifetime, AST printing, self-deletion/redefinition,
  separate variable/callable names, complete typed registry lookup and registration
  of 2,100 different named functions (beyond legacy 2,048 limit).
- make power12-phase0: PASS 20 source/Clang/ownership/strict-debt tests
  on the regenerated 4,280-definition inventory.
- make os1-check: PASS for real AMD64/ARM64 SDK object/ELF builds and
  parallel independent compiler sessions after the callable change.
- Real QEMU AMD64 and ARM64 guest: PASS, UART verifies alias=18,
  replacement=99, native textbuf load/split/merge/serialize and the
  CANONICAL_REGISTRY_TEXTBUF_QEMU_OK marker without faults.
- Full make test: PASS **122/122** Python tests, the production C array
  harness, 57 checked and 39 foundational ABI checks, 34/34 syntax
  files and **17/17** actually executed examples. Includes 15 callable,
  15 text-buffer and 6 terminal tests (not merely source scans).
- Clang AddressSanitizer/UndefinedBehaviorSanitizer: PASS real
  self-delete, self-redefinition, callable aliases and 90 text-buffer
  split/merge cycles; no simulator or test stub.
- Source catalog and per-function ownership manifest must classify
  lang/fn_table.c specifically as
  REGKEY_CALLABLE_OWNER_TESTED_STATESTORE_PENDING. It is NOT counted as
  a complete NXOLMO source port.

## Still unresolved and mandatory for subsequent phases

The existing global RegKey still lacks NXOLMO StateStore generation-safe
identities, schemas, MVCC, proper caller-authenticated capabilities,
concurrency and namespace isolation; function pointers remain private
to this process and all cross-process dispatch must later use REG handles.
The evaluator still uses recursive C calls and fixed call-depth/MAX_PARAMS,
and the old allocator still has process-fatal OOM paths. This slice does
not implement safe concurrent function object reference updates; thread
safety requires the common StateStore/epoch/lease migration.

Legacy IPC serialization of arbitrary recursive arrays is still not a
qualified REG protocol. Its incomplete-frame and resource bounds must be
fixed by the single NXEC REG transport integration; only the unsafe
TYPE_FN identity decoding/encoding has been explicitly rejected here.

No code outside Base-NEXS was changed. NXOLMO, NXEC and umbrella are
read-only references. No new syscall and no commit or push.

## Text-buffer duplicate elimination and verified semantics

The callable refactor exposed a previously hidden duplicate implementation:
services/textbuf.nx defined nine functions whose names overlapped with five
native built-ins. The old global fn_table resolved the C names first; once
the registry became authoritative, the service version became callable and
disagreed with the already-tested native tb_load empty-input contract.

A table-priority compatibility fallback is architecturally forbidden. All
nine text-buffer algorithms now exist in ONE published callable namespace
(/sys/tb_*), including four formerly service-only operations:
tb_get_line, tb_set_line, tb_split_line and tb_merge_line. Existing
tb_create/tb_insert_char/tb_delete_char/tb_load/tb_to_str operations are
still present. Splitting pre-admits capacity and both new strings before
altering old data; merging checks sizes and releases old storage correctly;
tb_to_str checks concatenation arithmetic, allocates fallibly and returns
one owned Value without a second copy.

services/textbuf.nx remains an importable language module descriptor, but
does NOT shadow any system callable or duplicate their implementations.
The exact old source line, migrated canonical function, error policy and
test evidence for each of the nine functions appear in
TEXTBUF_FUNCTION_MIGRATION.csv. No language module was silently dropped.

- tests/test_canonical_textbuf.py: PASS 15/15 real programs including
  module import, all formerly unique operations, valid/invalid indexes,
  long strings, split/merge and exact text outputs.
- tests/test_array_capacity_admission.py: PASS 9/9 with original
  tb_load(buf, "") = 0 semantics restored.
- Real host program exec("services/textbuf.nx") displays native functions
  and has no contradictory function registration in the service.
- The ABI still uses the legacy RegKey implementation; zero-copy and
  cross-process callable capability transfer are NOT implemented here.

## Typed name resolution, raw terminal drivers and compatibility

The authoritative RegKey resolver now shares ONE traversal for ordinary
values and type-filtered callable resolution (reg_resolve_callable).
Variable assignment no longer overwrites a callable stored in /fn or
/sys merely because the variable has the same text name. Variable access
and function calls retain their original separate language semantics;
explicit reg_set("/sys/name", scalar) still replaces that callable and
subsequent calls correctly fail. Six previously failing examples exposed
this accidental shadowing; all 17 examples now execute successfully
after repairing the resolution and HAL aliases.

services/stdlib.nx and modules/stdlib.nx expose a terminal wrapper
that updates a per-session virtual framebuffer, then calls the native
raw _term_at/_term_cls/_term_flush etc. Those raw names were missing and
were previously hidden by the obsolete global builtin table. The single
native sys/sysio.c HAL callbacks now expose both the public and raw
names through the SAME registry callback implementation; no second
terminal driver was added. _readkey exposes the raw key translator for
the language wrapper. Both stdlib term_size wrappers delegate to the
same _term_size runtime provider instead of hardcoding "80x24" at the
language level. The freestanding HAL fallback for real display size
still requires a separate hardware discovery migration.

tests/test_canonical_terminal_alias.py: PASS 6/6 real host tests,
including PTY ioctls proving an actual 123x41 terminal size, virtual
terminal shadow-buffer updates, and invalid raw argument responses.
The existing double copy of the legacy stdlib source under modules and
services is not claimed to be solved; it needs canonical module-loading
and idempotent import semantics before one path can be removed without
breaking existing images. This limitation is visible and never waived.


## Power12 work on the common typed resolver

registry/registry.c uses the same bounded search for untyped and
TYPE_FN-selected lookups. Ancestor traversal is bounded by the
existing REG_PATH_MAX path ABI, with explicit progress and no
break/goto; module siblings are bounded by g_registry.total_keys
(the actual admitted registry objects, not a new fixed table limit).
Both invalid and callable resolution cases are verified by the
function-namespace integration tests. This removes two actual
strict-baseline blockers from this slice: total outstanding findings
are now 349 (277 Base-NEXS, 72 reference Linux 9P).
This is not full Power12 compliance of RegKey or the rest of NEXS.


### Pre-P4 native extension relocation

The nine original native text-buffer bodies have moved from
lang/builtins.c to extensions/textbuf/textbuf.c, with unique array
Value ownership operations in core/dynarray.c. They still use the
one canonical /sys callable registry. Language text-buffer metadata
is at nxlib/textbuf.nx; every script service is under
nxlib/services/. This is module source separation, not a duplicate
registry or independent unloadable kernel driver.


## Current NXOLMO operation-runtime migration

Operation budget, metrics and the canonical operation catalog are now
ported into the ONE Base runtime with no second status/result/ID ABI.
EvalCtx executes AST units via nx_operation_budget_consume instead of its
own independent fuel counter. Original NXOLMO budget/metrics/catalog tests,
their ASan/UBSan builds, real hosted clock, Base runtime budget tests,
both OS1 target builds and AOT passed. N-worker task scheduling,
mandatory per-runtime admission, segmented verified IR/VM continuations,
cancellation and per-task buffer leases are NOT implemented. P2 OPEN.


### Guest acceptance after runtime budget migration

Full cross-ISA guest test evidence: make baremetal-amd64 and
make baremetal-arm64 each compile and link the shared runtime modules;
scripts/test_baremetal_qemu.py passes for both actual QEMU ISA guests
with an interactive REPL prompt. This verifies boot/link survival,
not N-worker scheduling or RuntimeInstance isolation.
