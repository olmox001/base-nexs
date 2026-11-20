# Portability and Power12 phase-1 policy

## Dependency and Git structure

The only Base-NEXS Git submodule is external/os1/nx-sdk, pointing at
https://github.com/olmox001/NX-SDK.git branch SPERIMENTAL-LINUX, pinned in
.gitmodules and the Git index. The NexsOS1/reference recursive submodule was
removed. scripts/setup-reference.sh no longer clones a project reference.

scripts/resolve-sdk.sh detects whether Base-NEXS is inside NexsOS1 using the
existing superproject module declarations. Within the umbrella, it uses
NexsOS1/system rather than downloading or initializing external/os1/nx-sdk.
Standalone users explicitly enable only the NX-SDK checkout with:
scripts/resolve-sdk.sh --init
scripts/resolve-sdk.sh --check

The optional SDK Git module is deliberately configured with
active=false and update=none, so recursive umbrella updates never clone
a second SDK. The explicit standalone setup overrides these options for
that one checkout. The resolver requires the real SDK object/API headers.

Use scripts/stage-project.sh for staging while embedded. A generic
git add -A removes Git index entries whose optional working-tree checkout is
missing; the script preserves the pinned 160000 SDK gitlink and stages all other
changed sources.

Known integration gap: the current umbrella tools/setup-dependencies.sh --check
treats even intentionally inactive/uninitialized nested gitlinks as errors.
That root bootstrap must be taught to recognize optional links in an
umbrella-scoped change before the Base-NEXS gitlink can be updated and
published. No root/NX-SDK source was altered to disguise this failure.

## Platform build targets

| Platform | Make target | Executable |
|---|---|---|
| Host macOS or Linux | all | nxscript |
| macOS amd64 | macos-amd64 | build/macos-amd64/nxscript |
| macOS arm64 | macos-arm64 | build/macos-arm64/nxscript |
| Linux amd64 | linux-amd64 | build/linux-amd64/nxscript |
| Linux arm64 | linux-arm64 | build/linux-arm64/nxscript |
| NexsOS1 amd64 | os1-amd64 | build/os1-amd64/nxscript.elf |
| NexsOS1 arm64 | os1-arm64 | build/os1-arm64/nxscript.elf |
| Baremetal amd64 | baremetal-amd64 | build/baremetal-amd64/nxscript.elf |
| Baremetal arm64 | baremetal-arm64 | build/baremetal-arm64/nxscript.elf |

The public AOT compiler also supports native OS1 targets using the same
cataloged runtime, SDK startup and capability-backed HAL as the interpreter:

    ./nxscript --compile tests/fixtures/baremetal_smoke.nx --target os1-amd64 -o build/assurance/os1-amd64-program.elf
    ./nxscript --compile tests/fixtures/baremetal_smoke.nx --target os1-arm64 -o build/assurance/os1-arm64-program.elf

Run make os1-check for both architectures' compiler, ELF ABI and non-executable
stack verification. For full guest validation use the separate nxadb tests,
which execute the generated AOT ELF after the boot test suite completes.

All target executables use the nxscript product name. NEXS is still the name
of the language and its internal C API. A failed compile, missing compiler,
missing libc sysroot or missing ELF is a build failure, never a skipped PASS.
The macOS targets use Apple clang, while Linux needs the appropriate GNU
compiler plus complete target libc/sysroot.

The public compiler's baremetal targets use the same canonical Make HAL
source catalog (the previous kernel/ctx_arm64.S link path was invalid):

    ./nxscript --compile tests/fixtures/baremetal_smoke.nx --no-dep --target baremetal-amd64 -o build/assurance/cli-baremetal-amd64.elf
    ./nxscript --compile tests/fixtures/baremetal_smoke.nx --no-dep --target baremetal-arm64 -o build/assurance/cli-baremetal-arm64.elf

These binaries are additionally executed under isolated QEMU with the real
UART output asserted, not just inspected for the ELF header.

Baremetal uses the existing compiler to generate real embedded C from
BAREMETAL_ENTRY_NX (default tests/fixtures/repl.nx, an empty REPL entry).
BAREMETAL_EMBED_DEPS=1 embeds dependency sources for a selected script. The
kernel/physical HAL are linked only for standalone baremetal, not for OS1.

## Staged Power12-inspired checks

make gate is enforced before compilation of C objects. It runs
scripts/quality_gate.py and checks the canonical C source list, the reviewed
34-file .nx manifest, the exact SDK gitlink and no recursive reference link.

Blocking in hal/os1: assertions, goto/break, obviously unbounded loops,
privileged ISA instructions, platform #ifdef branches, missing sources and
uncataloged scripts. OS1 native cross-compilation uses -Werror and requires
both independent ISA backends to link. The architecture-specific fences
are ordinary user-mode memory-ordering instructions.

Existing imported dev-stable violations are warnings, not silent passes:
the initial scan found 254 lexical findings across 130 checked C/header/asm
files. The complete diagnosis is build/assurance/quality.json. To scope all
existing violations as blockers, use:
python3 scripts/quality_gate.py --mode strict-all

This is a lexical phase-1 audit, NOT a NASA Power of Ten/Power12 safety
certification. Loop bounds, time budgets, architecture equivalence and memory
ownership still require compiler AST/CFG analysis, concurrency tests and actual
runtime evidence. No violations are suppressed by counting warnings as passes.

## Test and fault evidence

make test performs the gate, parser/ABI/policy unit tests, full 34-file
syntax-only catalog validation and 11 asserted interpreter cases.
make check-syntax parses all proprietary modules, services and examples with
the canonical lexer/parser but does not execute any service or change system
state. make check-examples attempts all examples, and explicitly exits
incomplete when some lack runtime assertions or require an interactive PTY.

NexsOS1 acceptance uses scripts/test-os1-nxadb.py --arch amd64 or --arch
aarch64 with --display default. The test waits for the asynchronous boot-time
NXTEST suite to finish BEFORE injecting any guest-shell input, executes
nxscript and a real .nx smoke fixture, and captures a verified QMP screenshot.
The existing boot-test-suite result is recorded separately from the nxscript
result, even if an unrelated third-party test fails.

The system diagnostics contract remains NX-SDK OS1_report_error, the standard
notification client and NXPaX; Base-NEXS must not create a second UART fault
transport. Language errors remain typed NEXS val_err values.
