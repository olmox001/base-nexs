# NEXS

NEXS is a Plan 9-inspired scripting language and micro-OS kernel. It has a tree-walking interpreter, a hierarchical registry (the OS backbone), IPC message queues, and a cross-compilation pipeline targeting macOS, Linux, and baremetal amd64/arm64.

---

## Quick Start

```sh
make          # host build (gcc by default)
./nexs        # interactive REPL
./nexs file.nx
```

Boot the demo mini-OS:

```sh
./nexs example/minios/boot.nx
```

This boots a full Plan 9-style userspace: VFS, process manager, auth, TTY, P9 mounts, and a shell with `ls`, `cd`, `cat`, `nxed`, `ps`, and more.

---

## Building

### Host

```sh
make
./nexs
```

### Cross-compile targets

```sh
make linux-amd64     # GCC x86_64 Linux ELF
make linux-arm64     # aarch64-linux-gnu-gcc
make macos-arm64     # clang -arch arm64 (Apple Silicon)
make macos-amd64     # clang -arch x86_64
make plan9-amd64     # Plan 9 style (GCC)
make baremetal-arm64 # aarch64-none-elf-gcc, QEMU virt
make baremetal-amd64 # x86_64-elf-gcc, Multiboot2 / Limine
```

### Standalone binary from a .nx script

```sh
./nexs --compile program.nx --target linux-amd64 -o out/program
./nexs --standalone-program example/minios/boot.nx --target macos-amd64 -o minios.out
./nexs --compile program.nx --target baremetal-arm64 -o out/kernel.elf
```

### Baremetal QEMU

```sh
make baremetal-amd64
bash scripts/qemu-amd64.sh   # boots NEXS REPL via UART

make baremetal-arm64
bash scripts/qemu-arm64.sh
```

---

## Architecture

```
┌────────────────────────────────────────────────────┐
│         NEXS Scripts (.nx) — userspace             │
│  example/minios/  services/  modules/              │
├────────────────────────────────────────────────────┤
│         Registry Namespace (/sys /proc /dev)       │  ← everything is a path
├──────────────┬─────────────┬───────────────────────┤
│  IPC Engine  │  Scheduler  │  VFS (9P-style)       │
│  (reg_ipc)   │  (sched.c)  │  (kernel/vfs.c)       │
├──────────────┴─────────────┴───────────────────────┤
│  HAL: MMU · IDT/VBAR · APIC/GIC · Timer · UART    │
└────────────────────────────────────────────────────┘
```

**Key principle:** every resource is a registry path. Processes live at `/proc/<pid>/`, syscalls are IPC messages to `/sys/kernel/inbox`, hardware state is reflected in `/hal/acpi/` and `/hal/fdt/`.

### Directory map

| Path | What it is |
|------|-----------|
| `core/` | Buddy allocator, pager, value types, dynarray |
| `registry/` | Hierarchical registry + IPC queues |
| `lang/` | Lexer, parser, evaluator, builtins |
| `sys/` | Plan 9 syscall wrappers (sysio, sysproc) |
| `runtime/` | REPL, line editor, standalone init |
| `compiler/` | Codegen + cross-compiler driver (7 targets) |
| `hal/amd64/` | GDT, IDT, APIC, MMU, ACPI, UART |
| `hal/arm64/` | EL setup, exception vectors, GIC, timer, FDT, UART |
| `kernel/` | Process, scheduler, VFS, blk cache, WAL journal, syscall dispatch |
| `fs/` | regfs (C binary format), FAT16, 9P server |
| `services/` | NEXS-language system services (loaded at boot) |
| `modules/` | Auto-loaded stdlib (available in REPL without exec) |
| `example/minios/` | Demo mini-OS: boot, shell, nxed editor |

---

## Language Reference

### Variables

```nexs
x = 42
y = 3.14
s = "hello"
b = true
```

### Arrays

```nexs
arr[0] = 10
arr[1] = 20
out arr[0]
del arr[1]
```

### Output

```nexs
out x
out "hello world"
```

### Arithmetic

```nexs
z = x + y
z = x * 2 - 1
z = x / y
z = x % 3
```

### Conditionals

```nexs
if x > 10 {
  out "big"
} else {
  out "small"
}
```

### Loops

```nexs
i = 0
loop {
  if i >= 5 { break }
  out i
  i = i + 1
}
```

### Functions

```nexs
fn add(a b) {
  ret a + b
}
out add(3 4)
```

### Strings

```nexs
s = "hello"
t = s + " world"
out len(s)
out substr(s 1 3)
out split(s "l")
out replace(s "l" "r")
```

### String builtins

| Builtin | Description |
|---------|-------------|
| `len(s)` | Length |
| `substr(s off n)` | Substring |
| `split(s sep)` | Split → array |
| `replace(s from to)` | Replace all occurrences |
| `contains(s sub)` | 1 if sub found |
| `str(v)` | Convert any value to string |
| `int(s)` | Parse string to int |
| `float(s)` | Parse string to float |
| `type(v)` | Type name string |

### Array builtins

| Builtin | Description |
|---------|-------------|
| `arr_create_anon(n)` | Create anonymous array of size n |
| `arr_ins(arr i v)` | Insert v at index i |
| `arr_del(arr i)` | Delete element at index i |
| `arr_join(arr sep)` | Join elements with separator |
| `len(arr)` | Number of elements |

### Math

```nexs
out abs(-5)
out min(3 7)
out max(3 7)
out clamp(15 0 10)
```

---

## Registry

The registry is a hierarchical key-value store. Every OS resource is a path.

```nexs
reg /env/x = 99
out reg /env/x

# List children
ls /env

# Delete
del /env/x

# Read from C-managed paths
out reg /sys/pm/next_pid
```

### Registry vs variables

Variables (`x = 42`) are local to the eval context. Registry keys (`reg /env/x`) persist across the runtime and are shared between all exec'd scripts.

### Registry navigation (NEXS keywords)

```nexs
cd /env        # change eval scope to /env
pwd            # print current scope
ls /env        # list children of /env
```

---

## Registry Pointers

```nexs
reg /env/x = 42
ptr /alias = /env/x
out deref /alias       # 42
```

Chains up to 32 hops, cycle detection built in.

---

## IPC Message Queues

```nexs
sendmessage /jobs 42
v = receivemessage /jobs
n = msgpending /jobs
```

Used by the kernel for syscall dispatch: NEXS programs send messages to `/sys/kernel/inbox`, the kernel replies to `/proc/<pid>/inbox`.

---

## Plan 9 Syscalls

```nexs
fd = open("file.txt" 0)
s  = read(fd 256)
write(fd "data")
close(fd)
pid = rfork(1)
```

---

## Services (auto-loaded at minios boot)

Services are NEXS libraries loaded by `example/minios/boot.nx`:

| Service | Path | What it provides |
|---------|------|-----------------|
| Stdlib | `services/stdlib.nx` | `input()`, `cat()`, `cp()`, etc. |
| VFS | `services/fs/init.nx` | `vfs_read/write/ls/resolve`, `fs_mount` |
| Process Manager | `services/pm/init.nx` | `pm_spawn/kill/ps` |
| Auth | `services/auth/init.nx` | `auth_cap_grant/check`, ring levels |
| TTY | `services/tty/init.nx` | TTY session setup |
| P9 Mounts | `services/fs/p9_mnt.nx` | Namespace bindings via 9P |
| HW Device Tree | `services/hw_dt.nx` | CPU/memory/bus detection |
| Text Buffer | `services/textbuf.nx` | Line buffer for nxed editor |
| UI | `services/ui/init.nx` | Framebuffer/app manager |

Modules in `modules/` are auto-loaded into the REPL without explicit `exec`:

| Module | What it provides |
|--------|-----------------|
| `modules/stdlib.nx` | `input()`, `min/max/clamp`, `trim`, `startswith/endswith`, `join` |

---

## minios Shell

Running `./nexs example/minios/boot.nx` gives:

```
nexs-os:/>
```

| Command | Description |
|---------|-------------|
| `ls [dir]` | List VFS directory |
| `ls /reg/path` | List registry children |
| `cd [dir]` | Change VFS directory |
| `pwd` | Print VFS working directory |
| `cat file` | Print file content |
| `touch file` | Create empty file |
| `cp src dst` | Copy file |
| `mv src dst` | Move file |
| `rm file` | Remove file |
| `ps` | List active processes |
| `devtree` | Dump hardware device tree |
| `fm [dir]` | Plan 9 file manager |
| `source file` | Execute .nx script |
| `nxed [file]` | Full-screen text editor |
| `exit` | Halt system |
| _any NEXS expression_ | Evaluated live |

---

## REPL Commands

| Command | Description |
|---------|-------------|
| `:exit` / `:q` | Quit |
| `:help` | Show syntax reference |
| `:version` | Print version |
| `:debug` | Toggle debug trace |
| `:ast` | Toggle AST dump |
| `:fn` | List all registered functions |
| `:ls [path]` | List registry at path (default `/`) |
| `:reg [path]` | Recursive registry dump |
| `:ptr /path` | Follow and print pointer chain |
| `:ipc /path` | Show IPC queue status |

---

## References

- Plan 9 from Bell Labs: https://9p.io/plan9/
- seL4 IPC model: https://sel4.systems/
- utf8.h: https://github.com/sheredom/utf8.h
