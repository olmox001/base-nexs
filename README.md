# NEXS

NEXS is a portable scripting language. Write once, run everywhere: the same `.nx` program runs interactively in a REPL, as a native binary on macOS/Linux/Plan 9, or as the init process on bare-metal amd64/arm64 hardware.

The core artifact is the `./nexs` interpreter. Everything else — the registry, IPC, scheduler, VFS, HAL — is infrastructure for the language.

```sh
make && ./nexs          # build and start the REPL
./nexs script.nx        # run a script
```

---

## Language

### Variables and types

```nexs
x    = 42
y    = 3.14
name = "NEXS"
flag = true
```

Types: `int`, `float`, `str`, `bool`, `arr`, `fn`, `ptr`, `nil`, `err`.

### Functions

```nexs
fn add(a b) {
  ret a + b
}
out add(3 4)       # prints 7
```

### Control flow

```nexs
if x > 10 {
  out "big"
} else {
  out "small"
}

i = 0
loop {
  if i >= 5 { break }
  out i
  i = i + 1
}
```

### Arrays

```nexs
arr[0] = 100
arr[1] = 200
out arr[0] + arr[1]

a = arr_create_anon(4)
arr_ins(a 0 "hello")
arr_del(a 0)
out len(a)
```

### Strings

```nexs
s = "hello"
out len(s)
out substr(s 1 3)           # "ell"
out split("a,b,c" ",")
out replace(s "l" "r")
out contains(s "ell")       # 1
out str(42)                 # "42"
out int("42")               # 42
out type(3.14)              # "float"
```

### Registry — the global namespace

Every value, process, and device is a path in the registry:

```nexs
reg /env/app = "NEXS"      # set a registry key
out reg /env/app            # read it

reg_set("/proc/1/state" "running")
v = reg_get("/proc/1/state")
children = keys("/proc/1")  # list children as array
```

The NEXS keywords `cd`, `pwd`, `ls` navigate the **registry tree** (not the filesystem):

```nexs
cd /env          # change eval scope root
pwd              # print current scope
ls /env          # list children of /env
```

### Registry pointers (zero-copy)

```nexs
p = ptr "/sys/bigdata"      # TYPE_PTR: a path reference
v = deref p                 # follow up to 64 hops, cycle-safe
```

### IPC message queues

```nexs
bind "/sys/myservice/inbox"            # create a queue

sendmessage "/sys/myservice/inbox" "hello"
msg = receivemessage "/sys/myservice/inbox"   # blocking
n   = msgpending "/sys/myservice/inbox"       # non-blocking count
```

Syscalls work the same way: the kernel reads `/sys/kernel/inbox` and replies to `/proc/<pid>/inbox`.

### I/O

```nexs
fd = open("/path/file" 0)       # 0=read, 1=write, 0100=creat
n  = read(fd buf 1024)
write(fd "hello\n")
close(fd)
remove("/tmp/tmpfile")
```

### Raw TTY / input

```nexs
rawon()
ch = readkey()      # "ENTER", "BACKSPACE", "a", …
b  = readbyte()     # raw byte integer
rawoff()
```

### Process builtins

```nexs
pid = getpid()
cwd = getwd()
exec("other.nx")        # load and run another .nx file
exits("done")           # exit with status string
sleep(500)              # milliseconds
child = rfork(1)        # fork — returns child pid to parent, 0 to child
```

### Math

```nexs
out abs(-5)
out min(3 7)
out max(3 7)
out clamp(15 0 10)   # 10
```

---

## Auto-loaded stdlib (`modules/stdlib.nx`)

Loaded at startup without any `exec` — always available in the REPL:

```nexs
name = input("Your name: ")   # line editor with backspace support
out min(3 7)                  # 3
out max(3 7)                  # 7
out clamp(15 0 10)            # 10
out trim("  hi  ")            # "hi"
out startswith("hello" "he")  # true
out endswith("hello" "lo")    # true
```

---

## REPL

```sh
./nexs
```

Any `.nx` expression or statement evaluated live. Special commands:

| Command | Description |
|---------|-------------|
| `:help` | Syntax reference and command list |
| `:fn` | List all defined functions with signatures |
| `:ls [path]` | List registry children at path |
| `:reg [path]` | Recursive registry dump |
| `:ptr /path` | Follow and print a pointer chain |
| `:ipc /path` | Show IPC queue at path |
| `:cd path` | Change REPL registry root |
| `:debug` | Toggle debug trace |
| `:ast` | Toggle AST dump on each eval |
| `:version` | Print version |
| `:exit` / `:q` | Quit |

---

## Running scripts

```sh
./nexs example/test.nx            # core language test suite
./nexs example/test_builtins_all.nx
./nexs example/micro_os.nx        # minimal embedded OS skeleton
./nexs example/exent_bus.nx       # IPC event bus demo
./nexs example/nxed_editor.nx     # full-screen terminal editor
```

---

## Compilation

Compile any `.nx` script to a self-contained native binary. The compiler bundles all dependencies, auto-detects memory footprint, and links with the runtime — no separate install needed on the target.

```sh
./nexs --compile program.nx --target linux-amd64 -o out/program
./nexs --standalone-program program.nx --target macos-arm64 -o program.out
./nexs --compile program.nx --target baremetal-amd64 -o kernel.elf
```

Flags:
- `--compile` / `--standalone-program` — equivalent; compile to binary
- `--target <name>` — target platform (see table)
- `-o <path>` — output path
- `--no-dep` — skip dependency bundling
- `--dep-only` — print dependency list and exit

### Targets

| Target | Toolchain |
|--------|-----------|
| `linux-amd64` | `gcc` x86\_64 ELF |
| `linux-arm64` | `aarch64-linux-gnu-gcc` |
| `macos-amd64` | `clang -arch x86_64` |
| `macos-arm64` | `clang -arch arm64` (Apple Silicon) |
| `plan9-amd64` | Plan 9 GCC |
| `baremetal-amd64` | `x86_64-elf-gcc`, Multiboot2 / Limine |
| `baremetal-arm64` | `aarch64-none-elf-gcc`, QEMU virt |

### Memory profiles

The compiler profiles the script and picks the smallest pool that fits:

`POOL_4KB` → `POOL_16KB` → `POOL_32KB` → `POOL_512KB` → `POOL_4MB` → `POOL_16MB`

---

## Cross-compile the `nexs` interpreter itself

```sh
make linux-amd64
make linux-arm64
make macos-arm64
make macos-amd64
make plan9-amd64
make baremetal-amd64
make baremetal-arm64
```

---

## Baremetal / QEMU

```sh
make baremetal-amd64 && bash scripts/qemu-amd64.sh
make baremetal-arm64 && bash scripts/qemu-arm64.sh
```

Both boot to the NEXS REPL over UART. Implemented:

- **amd64**: GDT, IDT (256 vectors), APIC + timer, MMU identity map, ACPI MADT, QEMU ACPI shutdown (`0x604/0x2000`)
- **arm64**: EL3→EL1 bootstrap, exception vectors, GIC, ARM generic timer, MMU, FDT parser, PSCI `SYSTEM_OFF` via `hvc #0`
- Round-robin O(1) scheduler, 8 priority levels
- IPC pipe-backed transport across `rfork()` boundaries
- FAT16 read-only filesystem for initrd (`fs/fat.c`)
- Buffer-cache block layer (`kernel/blk.c`) + WAL journal (`kernel/journal.c`)

---

## Embedding API

Embed the NEXS runtime in any C project — single include, no external dependencies beyond libc:

```c
#include "nexs.h"

nexs_runtime_init();

EvalCtx ctx;
eval_ctx_init(&ctx);

// Evaluate a string
EvalResult r = eval_str(&ctx, "x = 42\nout x");
val_free(&r.ret_val);

// Evaluate a file
EvalResult r2 = eval_file(&ctx, "script.nx");
val_free(&r2.ret_val);

// Read registry values
int64_t n = nexs_reg_int("/env/count");
char buf[64];
nexs_reg_str("/env/name", buf, sizeof(buf));

// Evaluate inline; returns 0 on success, -1 on error
int ok = nexs_eval_cstr(&ctx, "out 42");
```

Build flags:
```sh
-Icore/include -Iregistry/include -Ilang/include \
-Isys/include  -Iruntime/include  -Icompiler/include -Ihal/include
```

---

## Architecture

```
┌────────────────────────────────────────────────────────┐
│  .nx Scripts                                           │
│  modules/  example/  services/  your programs          │
├────────────────────────────────────────────────────────┤
│  Registry Namespace                                    │
│  /sys  /proc  /env  /mod  /fn  /hal  /mem  /dev       │
├──────────────┬────────────────┬───────────────────────┤
│  IPC Engine  │  Scheduler     │  VFS (9P-style)       │
│  (reg_ipc)   │  (kernel/sched)│  (kernel/vfs.c)       │
├──────────────┴────────────────┴───────────────────────┤
│  HAL: MMU · IDT/VBAR · APIC/GIC · Timer · UART       │
└────────────────────────────────────────────────────────┘
```

### Directory layout

| Path | Contents |
|------|----------|
| `core/` | Buddy allocator, pager, `Value` type, `DynArray`, utils |
| `registry/` | Registry tree + IPC queues (`reg_ipc`) |
| `lang/` | Lexer, parser, evaluator, fn\_table, 40+ builtins |
| `sys/` | Plan 9 syscall builtins (`sysio.c`, `sysproc.c`) |
| `runtime/` | REPL, `nexs_runtime_init`, line editor |
| `compiler/` | `codegen.c`, `driver.c`, `dep_scan.c` |
| `hal/amd64/` | GDT, IDT, APIC, MMU, ACPI, UART, HALB bytecode VM |
| `hal/arm64/` | EL setup, exception vectors, GIC, timer, FDT, UART |
| `kernel/` | proc, sched, ctx switch, VFS, blk cache, WAL, syscall dispatch |
| `fs/` | regfs (binary format), FAT16, 9P stubs |
| `include/nexs.h` | Public embedding API |
| `modules/` | Auto-loaded stdlib (available in REPL without `exec`) |
| `services/` | NEXS-language system services (loaded by boot scripts) |
| `example/` | Programs, test suites, minios demo |

---

## Example: minios

`example/minios/boot.nx` is a complete layered OS userspace built entirely in NEXS, running on top of the interpreter:

```sh
./nexs example/minios/boot.nx
```

Boot sequence: stdlib → fs → pm → auth → TTY → UI → 9P mounts → hardware device tree → shell.

The shell understands: `ls`, `cd`, `pwd`, `cat`, `touch`, `cp`, `mv`, `rm`, `ps`, `source`, `nxed`, `devtree`, `fm`, `exit` — and any NEXS expression typed directly.

minios is one example. Other examples in `example/`:
- `micro_os.nx` — minimal OS skeleton (100 lines)
- `exent_bus.nx` — IPC event bus
- `nxed_editor.nx` — full-screen terminal editor
- `test.nx`, `test_builtins_all.nx` — language test suites
- `example_lib.nx`, `lib.nx` — reusable library patterns

---

## References

- Plan 9 from Bell Labs: https://9p.io/plan9/
- seL4 IPC model: https://sel4.systems/
- utf8.h: https://github.com/sheredom/utf8.h
