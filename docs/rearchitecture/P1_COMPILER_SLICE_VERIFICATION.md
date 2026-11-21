# P1 Compiler/Execution Entry Slice — Source and Executed Evidence

Status: implementation and validation of the **host compilation launcher**
and shared REPL link contract. This is a completed, independently testable
P1 work package, NOT completion of the entire P1 memory/value/lexer phase.
The language's existing fixed global buddy pool, array ceiling, recursive
evaluation, lexer truncation and registry authority defects remain OPEN.
Strict Power12 release certification remains FAIL until those are fixed.

## Function-level replacement, ownership and integration

| Old function / source | New behavior and location | Verification |
|---|---|---|
| compiler/driver.c nexs_compile_file_ex | One host-only, per-invocation private staging, canonical Make source graph, checked output publication and typed target dispatch | Tests below |
| compiler/driver.c nexs_compile_file | Delegates to checked hosted target with existing public signature | Existing public compiler tests |
| driver path filtering and command construction | No shell interpolation; output/source no longer passed to Make; child invoked using posix_spawnp with argv | Metacharacter path and malformed profile tests |
| legacy per-profile C transpile shell loops | Removed from active compiler; no predictable PID /tmp and no generated duplicate compile source list | Concurrent AOT and staging cleanup tests |
| runtime/main.c nexs_repl | Moved without duplicate semantics to runtime/repl.c, selected by the canonical shared source graph | Interactive compiled empty-script test |
| config/sources.mk | Hosted driver only on supported host; compile_unavailable.c on OS1/freestanding; runtime/repl.c in shared graph | Both ISA cross builds, static source-graph test |
| Makefile aot-hosted | One canonical NEXS_AOT_HOSTED_SRCS list for native hosted AOT | Real macOS x86_64 and AArch64 executables |
| Makefile os1-aot-build | External private codegen input and request-specific program.o; no shared generated .c/.o between invocations | Real OS1 x86_64/AArch64 ELFs, two parallel isolated builds |
| Makefile baremetal-amd64/arm64 | External private codegen C input without repeated codegen; preserved existing target and linker scripts | Real ELF + QEMU boot/UART for both architectures |

## Verified success and negative tests

- Python unittest discovery restricted to test_hosted_aot_driver.py:
  PASS 8/8; compiled, linked, executed, preserved REPL, rejected missing
  inputs, refused invalid profile and unsupported target, tested dangerous
  output metacharacters without command execution, compiled concurrently,
  and preserved an old destination on failure.
- make os1-check PASS: exact x86_64 and AArch64 OS1 ELF machine types,
  linked SDK capability ABI symbols, non-executable stack, and separate
  OS1_PARALLEL_AOT=PASS with distinct compiled string markers.
- Public compiler CLI with target macos-amd64: PASS, generated Mach-O
  x86_64 executed successfully. Standalone script output was exactly 42.
  The original CLI autoloads TTY/auth service bootstrap messages while the
  standalone AOT starts isolated. This difference is recorded: full
  application/service side-effect equivalence is NOT claimed.
- Public compiler CLI target macos-arm64: PASS, Mach-O ARM64 produced,
  not executed on the Intel Mac.
- Public compiler CLI targets os1-amd64 and os1-arm64: PASS; two
  correctly linked ELF binaries produced. nxadb guest execution remains
  separately required.
- Public compiler CLI targets baremetal-amd64 and baremetal-arm64:
  PASS, two ELF binaries. Both booted in REAL QEMU via
  scripts/test_baremetal_qemu.py; each printed exact UART marker
  NXSCRIPT_BAREMETAL_SMOKE with no detected fault.
- Linux ARM64 hosted compile is BLOCKED by missing Linux C-library/sysroot:
  the current aarch64-linux-gnu-gcc resolves to a baremetal toolchain
  without stdio.h. Linux AMD64 compiler x86_64-linux-gnu-gcc is absent.
  No fake cross-host PASS is reported.

## Remaining blockers and planned remediation

- P1 allocator: core/buddy.c uses a global 64 MiB pool; xmalloc/die
  terminates on allocation failure. core/pager.c has a global page table
  with hardcoded slot/page assumptions.
- P1 values/arrays: global g_arrays; arrays limited to MAX_ARRAYS,
  unbounded index growth arithmetic and no per-runtime page ownership.
- P1 lexer/AST: Token.text[NAME_LEN] causes silent literal truncation and
  path/name corruption; parser uses fixed buffers and recursive AST.
- P2: REPL static line editor and function table remain global; concurrency
  of separate compiler processes is proven, NOT concurrent interpretation
  of multiple tasks in one process.
- P3: registry reg_set()/reg_set_ptr() caller-provided rights and
  reg_delete() no capability authorization remain unsafe and must be
  replaced with NXREG/NXEC canonical authority.
- P5: NXOLMO native writable ext4/VFS/9P and WAL durability incomplete.
- P7 OS1 guest: umbrella Makefile requires missing
  user/NexsOS1-main/portability/doom/doom.mk before QEMU can boot; this
  is a read-only NexsOS1 dependency, not repaired or suppressed.
- P0/P8: strict Power12 remains red; all upgraded function-catalog
  candidates require compiler-interprocedural proof before release.

## Staging and source boundaries

Only Base-NEXS code, tests and docs were changed. Already staged NXOLMO
gitlink and quality-gate adjustments remain untouched.
NexsOS1 kernel/SDK/USERKIT, NXOLMO, NXEC and umbrella source stay read-only.
No commit/push. Compiler slice is staged after complete Base regression
suite and refresh of source fingerprint catalogs.
