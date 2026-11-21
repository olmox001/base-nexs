# Pre-P4 hardening checkpoint — NXOLMO / NXEC / Base-NEXS / NXEXEC

**Status: verified source/loader/runtime integration slice; NOT
a completed StateStore, REG transport, Plan 9 VFS or authority port.**

## Reproduced failures and fixes

Running the absolute Base-NEXS interpreter and minios boot.nx script from
the NXSCRIPT-dev parent originally produced eleven missing service-file
errors and then a missing hw_dt_init. Source imports incorrectly depended
on the host process CWD. The hosted platform provider now selects its
installed resource root from the executable path, without chdir and
without changing the OS1 SDK capability-backed FILE interface. CWD
shadow tests and parent-traversal rejection are in the real test suite.
This is NOT an OS sandbox or a per-runtime-instance source provider;
symlink containment and resource provenance remain pending.

The old modules/stdlib.nx and services/stdlib.nx were byte-identical.
They have been merged into a SINGLE nxlib/stdlib.nx, and the actual
eleven .nx services were moved into nxlib/services/, retaining their
fs, ui, tty, auth, pm, hw and logo service subtrees. The native
text-buffer module descriptor is now nxlib/textbuf.nx. No duplicated
service tree, module copy or second registry survives.
Importers, tested fixtures, autoload, seL4 Makefile source entrypoints,
OS1 mapping, compiler, manifest and inventory were updated together.

The existing language module guard already provides a library identity.
The former extra synthetic declaration injected into imported nxlib
sources could stop parsing before a service's exported functions; this
was directly reproduced as function not found: vfs_import. Removing
the duplicate declaration restored the complete exports.

Registry endpoint names such as /services/fs/inbox are OBJECT paths,
not source-file paths, and were preserved unchanged.

## Native C extensions, ownership and module boundaries

extensions/textbuf/textbuf.c now contains all nine original native
text-buffer algorithms, removed from the core language builtins.
They register with the existing fn_register_builtin_sig via the SAME
/sys callable registry; there is no second dispatch table. Shared
Value ownership and array element insertion, removal and assignment
are implemented once in core/dynarray.c and used by both the language
and the extension. The extension is currently statically selected
for hosted, OS1 and baremetal builds. It is not yet independently
loaded/unloaded or authenticated by NXOLMO capabilities.

The canonical roles are: nxlib for all language .nx libraries/services;
extensions for C runtime/userland drivers and native functions;
HAL platform providers for actual architecture/device primitives;
kernel for enforceable hardware/object authority. Future
hal/extensions and kernel/extensions locations must not become
parallel implementations of TTY, VFS, registry or process management.
Do not create stub folders or substitute diagnostic strings for
drivers. Userland services will communicate through the ONE future
NXEC REG transport and NXOLMO object/capability authority.

## Catalog, image and AOT correctness

A single tree scan in scripts/update_nx_catalog.py builds:
- config/nx_sources.txt: exactly 33 .nx script entries;
- config/nx_resources.txt: the same 33 sources plus 4 actual binary
  .nxtimg assets, 37 logical resource identities.

The build quality gate checks BOTH catalogs fail-closed, including no
missing or duplicated sources/assets. sys_bundled() on the hosted
interpreter now enumerates all 37 entries, not just the three top-level
directory names. The generated AOT program publishes its actual
embedded dependency paths through two native read-only accessors,
not a new mutable catalog. Compiler dependency scanning uses the SAME
platform source provider as runtime imports, preserving logical paths.

Before the fix, an external-CWD compilation reported fourteen
unreadable imports. After the fix, the boot dependency scanner reports
18 readable transitive paths; full codegen also includes the single
nxlib module/service tree (20 dependencies for the boot example).
This does not establish that the current AOT emitter stores arbitrary
binary assets losslessly: some generated C content still treats binary
resource data as NUL-terminated strings. Length-aware encoding and
independent byte-fidelity tests remain blocking.

The new product-local scripts/stage_nxlib.py and make nxlib-stage target
create a NEW image staging directory without overwriting any existing
files. The image maps nxlib to /lib/nxscript/nxlib, examples/assets to
/home/nxscript/examples, and the two catalogs to /lib/nxscript/config.
All 39 staged files (33 .nx, 4 .nxtimg, 2 catalog manifests) are
byte-compared and SHA-256 checked. The userland image is PREPARED,
NOT installed into the read-only NexsOS1 umbrella or QEMU OS1 VFS.

## Real interactive terminal acceptance

The old readkey() wrapper checked all host PIDs >=100 against a
process-manager session registry. A host PID is not a proof of
ownership. This arbitrary input-denial heuristic was removed;
the existing TTY provider still owns input and must implement proper
per-session process/foreground capability validation later.

The hosted raw-mode controller previously used TCSAFLUSH and silently
discarded pending input at mode switches. It now changes termios
using TCSANOW, preserving queued keys without creating another input
provider. Real PTYs run from a parent CWD:
- example/minios/boot.nx boots, displays the shell prompt, accepts
  help, pwd and exit, and terminates with exit status 0;
- example/druglords/druglords.nx reaches its greeting prompt,
  accepts q and exits 0.

The tests use actual PTY I/O and check full output markers and clean
exit, not synthesized return values. Directly launching
example/minios/shell.nx is only module definition, not an executable
bootstrap. A full interactive text-editor session and multi-process
TTY, CTTY focus and NXEXEC supervision remain unverified.

## Architecture blockers before P4 acceptance

| Owner | Code-backed current limitation | Acceptance required |
| --- | --- | --- |
| NXOLMO StateStore/NXREG | Global RegKey tree, local pointers, no authoritative generation-safe object/transaction lifecycle | Replace the ONE store in place; schema, authenticated authority, stale-generation/revocation/MVCC adversarial tests |
| NXEC REG | reg_ipc queues and pipe fallback, not a versioned capability-transfer protocol | Wire envelopes, resource credit/backpressure, correlation, handle authority, cancellation, bounded transfers |
| NXOLMO memory/FD/buffers | Global 64MiB buddy pool, fatal OOM, fixed g_fd_table and legacy pager | Provider-admitted per-instance accounting, checked memory/frame/lease/pin/buffer ownership and process FD isolation |
| NXEC kernel/HAL | kernel/cap.c unchecked init and inconsistent count updates; hal/module uses global fixed-size driver slots | Hardware vs userland ownership, true cap issue/attenuation/revocation, tested platform drivers, no new syscalls |
| NXOLMO Plan 9 VFS | fs/9p.c is an unselected, incomplete 9P2000 sketch: global 64 FIDs, unchecked message fields, missing operations, nonstandard QIDs/stat and read offset | Full 9P message framing/validation and T/R operations, per-session FIDs, real VFS object graph, ext4/WAL, wire interoperability/negative tests |
| NXEXEC / TTY | PID heuristic removed but no validated CTTY authority, process/FD/surface segregation or safe session lifecycle | Per-process I/O ownership, foreground transfer, execsvc contract, supervision, timeouts, recovery |
| Base-NEXS compiler/runtime | CWD loader fixed but source root still process-global; recursive AOT dependency scanner and binary asset length issue remain | Independent runtime instances, verified IR, bounded module dependency graph, binary asset fidelity |
| Quality gates | Strict Power12 still has blocking violations, and full OS1 make run / nxadb is blocked in the read-only external umbrella | Complete bounded trusted code, real guest + nxadb acceptance on both architectures, no waiver |

This is an architecture CHECKPOINT and a detailed blocker register,
not a declaration that the future kernel, VFS or StateStore is complete.
The 9P-shaped NEXS registry service does not prove 9P2000 wire
conformance. No fake authority checks, alternate registry or VFS
implementations are acceptable in P4.

## Verification scope and evidence

Real tests created/updated: test_source_catalog_resolution.py,
test_interactive_os_examples.py, test_nxlib_image_stage.py,
test_canonical_textbuf.py and the existing array/registry suites.
Verified for the pre-P4 slice:
- Reviewed source manifest: 33/33 NEXS scripts parsed without execution,
  37/37 logical assets/sources matched to disk (4 binary images).
- Existing example harness: 17/17 real noninteractive examples executed;
  text-buffer 15/15, capacity admission 9/9, hosted resource/AOT 10/10,
  image-stage 3/3 and real terminal PTY 2/2 acceptance tests.
- Production C registry harness: 8,798 assertions PASS under the real
  allocator/registry; separate ASan and UBSan passes also confirmed.
- make os1-check: PASS on AMD64 and ARM64 plus independent parallel
  native AOT compiler sessions.
- Actual QEMU AMD64 and ARM64: PASS identical text-buffer execution,
  exact guest UART output, no guest fault markers.
- Source-indexed provenance and ownership: 21/21 tests PASS, 4,318
  definitions across 567 files, 1,906 Clang AST function bodies. Existing
  NXOLMO ports remain 27 fully tested ABI functions; newly relocated
  Base-NEXS text-buffer functions are native extension implementations,
  NOT falsely marked as NXOLMO StateStore ports.
- Full strict Power12 is still FAILED: 347 tracked blockers (275 in
  Base-NEXS, 72 unported imported Linux 9P violations), never waived.
- Full make test: PASS 147/147 actual Python test cases after the
  FINAL staged source inventory refresh, including 33/33 syntax files,
  17/17 noninteractive executed example programs and the 8,798-case
  production registry core harness.
- The checkpointed source/module refactor is accepted by these
  verification gates; the next architecture phase remains BLOCKED
  by the explicitly listed StateStore, 9P, REG, per-process TTY,
  buffer/accounting and Power12 issues.

External NXOLMO, NXEC and umbrella source repositories remain READ ONLY.
