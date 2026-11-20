# Base-NEXS module catalog

The authoritative C source list is config/sources.mk. NEXS-language sources
are enumerated in config/nx_sources.txt and enforced by the build gate.

## Module ownership and runtime interface

| Component | Sources | Role | Platform |
|---|---|---|---|
| Core | core/ | Fixed pool, values and dynamic arrays | All |
| Registry | registry/ | Local interpreter namespace and IPC | All |
| Language | lang/ | AST parser, evaluator, builtins | All |
| Compiler | compiler/ | AOT generator and platform target driver | All |
| Process/file API | sys/ | NEXS I/O/process facade | All |
| Runtime | runtime/ | REPL, script execution, startup | All |
| Hosted HAL | hal/hal_hosted.c, hal/common/ | macOS/Linux libc dispatch | macOS/Linux |
| NexsOS1 HAL | hal/os1/ | OS1 SDK capabilities, registry, per-ISA fence | OS1 userland amd64/arm64 |
| Physical HAL | hal/amd64/, hal/arm64/, hal/riscv64/ | Boot, IRQ, MMU, UART | Baremetal |
| seL4 HAL | hal/sel4/ | Microkit domains, IPC, PD | seL4 |
| VM driver | hal/bc/, hal/module/ | Bounded bytecode and device modules | Selected platforms |
| Standalone kernel | kernel/ | Process, scheduler, capability and VFS | Baremetal/seL4 |
| Standalone filesystem | fs/ | 9P/regfs | Standalone only |
| OS1 SDK dependency | external/os1/nx-sdk | SDK Git dependency | Standalone only (umbrella uses system/) |

The OS1 adapter does not install a second kernel or replace NX-SDK's object
and capability transport. Platform HAL drivers are mutually exclusive.
The first-party code remains independent of the external OS1 SDK on macOS,
Linux, baremetal and seL4 targets.

## Proprietary .nx module and service catalog

These are executable NEXS-language modules, not passive assets. Execution
can mutate the private language registry, open resources, spawn child processes
or wait for input. Do not treat process exit 0 as a functional assertion.

### Automatically loaded language libraries (1)

- modules/stdlib.nx

### System-like language services (.nx) (13)

- services/auth/init.nx
- services/fs/init.nx
- services/fs/p9_mnt.nx
- services/fs/regfs.nx
- services/hw_dt.nx
- services/init.nx
- services/logos/init.nx
- services/pm/init.nx
- services/stdlib.nx
- services/textbuf.nx
- services/tty/init.nx
- services/ui/img.nx
- services/ui/init.nx

### Tests, demos and interactive programs (20)

- example/druglords/druglords.nx
- example/example_lib.nx
- example/exent_bus.nx
- example/lib.nx
- example/micro_os.nx
- example/minios/boot.nx
- example/minios/druglords.nx
- example/minios/logo.nx
- example/minios/logos_config.nx
- example/minios/nxed_editor.nx
- example/minios/shell.nx
- example/minios/termimg.nx
- example/minios/wasm_vm.nx
- example/nxed_editor copy.nx
- example/nxed_editor.nx
- example/test.nx
- example/test_builtins_all.nx
- example/test_input.nx
- example/test_parse_loop.nx
- example/test_syntax.nx

## Registry namespaces and authority

NEXS' internal /env, /sys and /local paths are private interpreter state.
They are not identical to NX-CORE's system registry. The explicit functions
os1_registry_get and os1_registry_set use the existing NX-SDK
OS1_registry_object_get/set APIs and their capability permissions.
No implicit privilege escalation or ambient global registry fallback is
permitted. Baremetal drivers cannot be reused as OS1 user processes.

The compiler includes canonical runtime headers rather than copying the
Value/EvalCtx layouts into generated C. The table of embedded .nx dependencies
has a sentinel even when the selected program has no dependencies.
