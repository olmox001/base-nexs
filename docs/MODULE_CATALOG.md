# Base-NEXS module catalog

The authoritative C source list is config/sources.mk. Installed NEXS library and service sources are enumerated in
config/nx_sources.txt and enforced by the build gate.
External examples are NOT part of this startup manifest.

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

## Runtime libraries and external examples

The installed startup inventory contains the 14 files under nxlib/,
including its language libraries and service source files.
config/nx_sources.txt lists only installed nxlib sources.
config/nx_resources.txt lists only installed runtime resources.
Pure root libraries load at startup; services under nxlib/services/
are available but require explicit activation.

The 20 example/*.nx programs and their .nxtimg assets are external
VFS content, not runtime-internal resources. They are not inserted
into the registry at startup, included in the runtime manifests,
auto-admitted into the startup registry, or linked automatically into an
AOT dependency table. Image staging separately copies all example scripts
and assets to /home/nxscript/examples as ordinary VFS files. Execution
still uses the caller's namespace, mounted providers and permissions.
Syntax and PTY tests use external files as fixtures without changing
the runtime's source catalog.

sys_bundled() enumerates runtime resources only.
The existence of a source file grants no privilege, mount or service
activation.

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
