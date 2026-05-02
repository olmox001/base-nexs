# NEXS — Stato del Progetto (2026-05-02)

## Panoramica del progetto

NEXS è un **linguaggio di scripting portatile** con runtime Plan 9-inspired. Il file principale è `./nexs` — un interprete single-binary che:
- Ospita un REPL interattivo con line editor, history, comandi speciali (`:fn`, `:ptr`, `:ipc`, `:reg`, `:ls`, ...)
- Esegue script `.nx` direttamente (`./nexs script.nx`)
- Cross-compila script `.nx` in binari nativi per 7 target (`--compile` / `--standalone-program`)
- Gira su macOS, Linux, Plan 9 e bare-metal amd64/arm64

`example/minios/` è una delle applicazioni demo — non è il progetto principale.

---

## Cosa funziona adesso

### Interprete / Runtime

| Feature | Stato |
|---------|-------|
| Build pulita: `make` → `./nexs` | ✅ |
| REPL interattivo con line editor, history, escape ANSI | ✅ |
| Auto-load `modules/stdlib.nx` all'avvio (no `exec`) | ✅ |
| `input()`, `min/max/clamp`, `trim`, `startswith/endswith`, `join` da stdlib | ✅ |
| Tutti i tipi: `int`, `float`, `str`, `bool`, `arr`, `fn`, `ptr`, `nil`, `err` | ✅ |
| Comandi REPL: `:fn`, `:ptr`, `:ipc`, `:reg`, `:ls`, `:cd`, `:debug`, `:ast`, `:version`, `:exit` | ✅ |
| `eval_str()` / `eval_file()` embedding API (`include/nexs.h`) | ✅ |

### Linguaggio — builtins C

| Categoria | Builtins |
|-----------|----------|
| Output | `out` |
| Stringhe | `len`, `substr`, `split`, `replace`, `contains`, `str`, `int`, `float`, `type` |
| Array | `arr_create`, `arr_create_anon`, `arr_ins`, `arr_del`, `arr_join`, `len` |
| Math | `abs`, `min`, `max`, `clamp` |
| Registry | `reg_get`, `reg_set`, `keys`, `reg_delete` |
| IPC | `sendmessage`, `receivemessage`, `msgpending`, `bind` |
| Puntatori | `ptr`, `deref` |
| I/O file | `open`, `create`, `close`, `read`, `write`, `seek`, `dup`, `fd2path`, `remove`, `pipe`, `fstat` |
| TTY raw | `rawon`, `rawoff`, `readbyte`, `chr`, `readkey`, `set_layout` |
| VFS/mount | `chdir`, `mount`, `bind`, `unmount`, `getwd` |
| Processo | `exec`, `exits`, `sleep`, `alarm`, `rfork`, `await`, `getpid` |

### Compilatore (7 target)

| Target | Stato |
|--------|-------|
| `linux-amd64` | ✅ |
| `linux-arm64` | ✅ |
| `macos-amd64` | ✅ |
| `macos-arm64` | ✅ |
| `plan9-amd64` | ✅ |
| `baremetal-amd64` | ✅ |
| `baremetal-arm64` | ✅ |
| Memory profiling automatico (6 profili POOL_4KB→POOL_16MB) | ✅ |
| `--compile` / `--standalone-program` equivalenti | ✅ |
| `--no-dep`, `--dep-only` | ✅ |
| Fix shell injection su `tc->ld_script` (aggiunta virgolette) | ✅ |

### Kernel baremetal

| Feature | Stato |
|---------|-------|
| amd64: GDT, IDT (256 stubs), APIC+timer, MMU identity map, ACPI MADT | ✅ |
| arm64: EL3→EL1, exception vectors, GIC, ARM generic timer, MMU, FDT parser | ✅ |
| `nexs_hal_halt()` amd64: QEMU ACPI shutdown (`outw 0x604, 0x2000`) | ✅ |
| `nexs_hal_halt()` arm64: PSCI `SYSTEM_OFF` via `hvc #0` | ✅ |
| Context switch: `ctx_amd64.S`, `ctx_arm64.S`, trampoline arm64 | ✅ |
| Scheduler round-robin O(1) con `s_tails[8]` e `irq_disable` | ✅ |
| IPC pipe-backed (`reg_ipc_enable_pipes`) per cross-fork IPC | ✅ |

### C runtime / libc_stub (baremetal)

| Feature | Stato |
|---------|-------|
| `strdup()`: malloc+memcpy reale | ✅ |
| `exit()` baremetal: chiama `nexs_hal_halt()` | ✅ |
| `poll()`: restituisce 0 (timeout) invece di -1 | ✅ |
| `strtol()`, `strtoul()` implementati | ✅ |
| `vsnprintf`: case `%u` aggiunto | ✅ |

### VFS kernel (`kernel/vfs.c`)

| Feature | Stato |
|---------|-------|
| `vfs_init()` pre-alloca fd 0/1/2 (stdin/stdout/stderr) | ✅ |
| `vfs_dup(oldfd, newfd)` | ✅ |
| `vfs_seek(fd, offset, whence)` | ✅ |
| `sys_dup()` + `sys_seek()` nel dispatch table | ✅ |

### FAT16 (`fs/fat.c`)

| Feature | Stato |
|---------|-------|
| `fat_init()` — legge BPB, calcola layout | ✅ |
| `fat_open()` — apre file per nome 8.3 | ✅ |
| `fat_read()` — legge con chain traversal + guard ciclico | ✅ |
| `fat_readdir()` — directory listing completo (root + subdirectory) | ✅ |

### Applicazione demo: minios

| Feature | Stato |
|---------|-------|
| Boot completo: stdlib → fs → pm → auth → tty → ui → p9 → hw_dt → textbuf → shell | ✅ |
| Shell v2.2 con VFS cwd in `/proc/1/vfs_cwd` (fix `cd var` letterale) | ✅ |
| Navigazione VFS: `cd`, `pwd`, `ls`, `cat`, `touch`, `cp`, `mv`, `rm` | ✅ |
| `ls /reg/path` → lista figli registry via `keys()` | ✅ |
| `ps` → processi reali da PID 100 in su | ✅ |
| `nxed` → editor full-screen NXED v18 | ✅ |
| Auth service: `auth_cap_grant/check/revoke`, ring 0-3 | ✅ |
| `regfs_save/load` iterativo (no stack overflow) | ✅ |

### Bug risolti (da revision1.MD)

| File | Bug | Stato |
|------|-----|-------|
| `services/auth/init.nx` | `auth_cap_grant`: 3 args a `reg_set` invece di 2 | ✅ |
| `services/pm/init.nx` | `pm_ps` scansione da PID 1 invece di 100 | ✅ |
| `services/fs/init.nx` | `vfs_resolve` restituiva path con `/` iniziale | ✅ |
| `services/fs/regfs.nx` | `_regfs_dump_node` ricorsivo → stack overflow | ✅ iterativo |
| `example/minios/shell.nx` | `cd var` trattato come percorso letterale | ✅ |
| `sys/sysproc.c` | `nexs_exits()` usava `cli+hlt` x86-only su baremetal | ✅ |
| `compiler/driver.c` | `tc->ld_script` non quotato → shell injection | ✅ |
| `kernel/sched.c` | Race condition su `sched_pick_next` | ✅ irq_disable |
| `kernel/vfs.c` | FD 0/1/2 non pre-allocati | ✅ (già risolto) |
| `fs/regfs.c` | CRC non includeva i dati | ✅ (già risolto) |
| `fs/fat.c` | Loop infinito su catena FAT ciclica | ✅ guard counter |
| `fs/fat.c` | `fat_readdir()` era uno stub -1 | ✅ implementato |
| `kernel/syscall.c` | `sys_dup()` e `sys_seek()` erano stub | ✅ implementati |
| `nexs_line.c` | ANSI in prompt_len | ✅ (già risolto) |
| `lang/lexer.c` | Closing quote non consumata | ✅ (già risolto) |
| `kernel/proc.c` | arm64 `ctx->x20` non settato | ✅ (già risolto) |
| `kernel/syscall.c` | `sender_pid == 0` senza check | ✅ (già risolto) |
| `hal/bc` | Off-by-one `ip+4 >= prog_len` | ✅ non è un bug |

---

## Cosa manca ancora

### Alta priorità — bloccanti per baremetal funzionante

| Componente | File | Problema |
|------------|------|---------|
| Avvio `kmsg_dispatch_loop()` | `kernel/msg.c` | Struttura presente, mai chiamata nel boot baremetal — syscall IPC non funzionano |
| VFS path lookup O(n) | `kernel/vfs.c:path_to_ino()` | Scan lineare per ogni `open()` — serve hash table o trie |
| `sys_fork` + `sys_exec` wired al boot | `kernel/syscall.c` | Funzionali come messaggi IPC, non avviati nel kernel init |
| `/mem/phys/<pfn>/` nel registry | registry | Buddy alloca ma non pubblica le pagine — nessuna visibilità on memoria fisica |
| IOAPIC routing | `hal/amd64/apic.c` | `ioapic_route()` mai chiamata — IRQ tastiera non instradato |
| Ring 0/1 enforcement reale | `kernel/syscall.c` | Solo `caller_has_admin()` per Ring 0 — Ring 1 non distinto da Ring 2/3 |
| PAE ordering | `hal/amd64/boot.S` | PAE abilitato dopo le page table — solo 2MB huge pages funzionano |
| Stack frame interrupt annidati | `hal/amd64/isr_stubs.S` | `addq $16, %rsp` presuppone layout fisso — rompe su interrupt annidati |

### Media priorità

| Componente | File | Problema |
|------------|------|---------|
| Bitcode encoder/decoder | `compiler/bitcode.c` (non esiste) | Header `nexs_bitcode.h` definito, nessuna implementazione |
| `journal_log` ordering | `kernel/journal.c:97` | Legge blocco old senza flush prima — ordinamento non garantito |
| `blk.c` read fail | `kernel/blk.c:105` | Read fallita → buffer non inizializzato in cache |
| 9P message handler | `fs/9p.c` | Struttura presente, tutti gli handler sono stub |
| `sys_dup` / `sys_seek` test E2E | test mancanti | Implementati, mai testati end-to-end via IPC |

### Bassa priorità / futuro

| Componente | Note |
|------------|------|
| Bitcode format `.nxb` | Phase 6 PLAN.md — prerequisito per dual-partition build |
| Dual-partition boot image | `make minios` → kernel partition FAT16 + FS partition regfs |
| `state_save()/state_restore()` | `regfs_save` esiste in NEXS, manca l'hook automatico al boot |
| SMP bringup | AP init via SIPI non implementato |
| NEXS test suite automatizzata | `example/test.nx` e `test_builtins_all.nx` esistono, nessuna CI |

---

## Layer del progetto (gerarchia)

```
C runtime (lang/ sys/ registry/ core/ hal/)
  → C builtins: out, open/read/write/close, reg_get/set, keys,
                sendmessage/receivemessage/msgpending, ptr/deref,
                rawon/rawoff/readkey/readbyte, exec, rfork, ...

modules/   → auto-caricato all'avvio dal REPL
  stdlib.nx → input(), min/max/clamp, trim, startswith/endswith, join

services/  → librerie NEXS caricate da boot.nx di minios
  stdlib.nx  → input(), cat(), cp(), ...  (usa VFS)
  fs/init.nx → vfs_write/read/ls/resolve (VFS simulato in registry)
  pm/init.nx → pm_spawn/kill/ps
  auth/init.nx → auth_cap_grant/check/revoke
  tty/init.nx → sessione TTY
  ui/init.nx  → framebuffer/app manager
  textbuf.nx  → buffer per nxed editor
  hw_dt.nx    → hardware device tree
  fs/p9_mnt.nx → namespace mounts 9P

example/   → applicazioni dimostrative
  test.nx, test_builtins_all.nx → test suite linguaggio
  micro_os.nx → OS skeleton minimale
  exent_bus.nx → event bus IPC
  nxed_editor.nx → editor full-screen
  minios/boot.nx → OS userspace completo (demo)
```

**Distinzioni importanti:**
- `cd`/`pwd`/`ls` come keyword NEXS → operano sull'eval scope (registry tree), **non sul VFS**
- `shell_cwd()` in `shell.nx` → legge `/proc/1/vfs_cwd` per il VFS cwd dell'utente
- `services/fs/` → VFS simulato in registry (flat index in `/sys/vfs/files/`)
- `kernel/vfs.c` → VFS C per kernel baremetal (fd table reale, inode, mount)

---

## Prossimi step (in ordine di priorità)

1. **Avviare `kmsg_dispatch_loop()`** nel boot baremetal dopo `sched_init()` — collega IPC al kernel
2. **Patch `hal/amd64/boot.S`** — PAE ordering fix
3. **Patch `hal/amd64/isr_stubs.S`** — stack frame canonico per interrupt annidati
4. **IOAPIC routing** — `ioapic_route()` per tastiera/timer su amd64 baremetal
5. **Implementare bitcode** `compiler/bitcode.c` — Phase 6 PLAN, prerequisito dual-partition
6. **VFS hash table** in `kernel/vfs.c` — O(1) path lookup per `open()` ad alta frequenza
7. **9P message handler** in `fs/9p.c` — Tversion, Tattach, Twalk, Topen, Tread, Twrite
