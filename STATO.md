# NEXS — Stato del Progetto (2026-05-01)

## Cosa funziona adesso

### Host (macOS/Linux)
- Build pulita: `make` → `./nexs` ✅
- REPL interattivo con line editor, history, escape ANSI ✅
- Auto-load `modules/stdlib.nx` → `input()`, `min/max/clamp`, `trim`, `startswith/endswith` ✅
- Compile `.nx` → binario nativo per 7 target (`--compile` / `--standalone-program`) ✅

### minios demo (`example/minios/boot.nx`)
- Boot completo: stdlib → fs → pm → auth → tty → p9 → hw_dt → textbuf → shell ✅
- Shell v2.2 con VFS cwd corretto (tracciato in `/proc/1/vfs_cwd`) ✅
- Navigazione VFS: `cd`, `pwd`, `ls`, `cat`, `touch`, `cp`, `mv`, `rm` ✅
- `ls /reg/path` → lista figli nel registry tramite `keys()` ✅
- `ps` → elenca processi reali da PID 100 in su ✅
- `nxed` → editor full-screen NXED v18 con textbuf ✅
- Auth service: `auth_cap_grant/check/revoke`, ring 0-3 ✅
- `regfs_save/load` iterativo (no stack overflow su alberi profondi) ✅

### Kernel baremetal (amd64 + arm64)
- amd64: GDT, IDT (256 stub), APIC+timer, MMU identity map, ACPI MADT ✅
- arm64: EL3→EL1 bootstrap, exception vectors, GIC, ARM generic timer, MMU, FDT parser ✅
- `nexs_hal_halt()` amd64: scrive porta ACPI QEMU `0x604/0x2000` → QEMU si spegne ✅
- `nexs_hal_halt()` arm64: PSCI `SYSTEM_OFF` via `hvc #0` ✅
- Context switch: `ctx_amd64.S`, `ctx_arm64.S`, `proc_create()` con trampoline arm64 ✅
- Scheduler round-robin O(1) con `s_tails[8]` e `irq_disable` su `sched_pick_next` ✅
- IPC pipe-backed (`reg_ipc_enable_pipes`) per IPC cross-fork ✅

### C runtime / libc_stub
- `strdup()`: implementazione reale (malloc+memcpy) ✅
- `exit()` baremetal: chiama `nexs_hal_halt()` (non più x86-only `cli+hlt`) ✅
- `poll()`: restituisce 0 (timeout) invece di -1 ✅
- `strtol()`, `strtoul()`: implementati ✅
- `libc_stub.c`: includeva `nexs_hal.h` → architettura-agnostico ✅

### VFS kernel (kernel/vfs.c)
- `vfs_init()` pre-alloca fd 0/1/2 (stdin/stdout/stderr) ✅
- `vfs_dup(oldfd, newfd)`: duplica fd, -1 per prossimo disponibile ✅
- `vfs_seek(fd, offset, whence)`: aggiorna `pos` nella fd table ✅
- `sys_dup()` + `sys_seek()` non più stub ✅

### Bug corretti da revision1.MD
| File | Bug | Stato |
|------|-----|-------|
| `services/auth/init.nx` | `auth_cap_grant`: 3 args a `reg_set` invece di 2 | ✅ Corretto |
| `services/pm/init.nx` | `pm_ps` scansione da PID 1 invece di 100 | ✅ Corretto |
| `services/fs/init.nx` | `vfs_resolve` restituiva path con `/` iniziale | ✅ Corretto |
| `services/fs/regfs.nx` | `_regfs_dump_node` ricorsivo → stack overflow | ✅ Iterativo |
| `example/minios/shell.nx` | `cd var` trattato come percorso letterale | ✅ Riscritto |
| `sys/sysproc.c` | `nexs_exits()` non usava `nexs_hal_halt` su baremetal | ✅ Corretto |
| `compiler/driver.c` | `tc->ld_script` non quotato → shell injection | ✅ Corretto |
| `kernel/sched.c` | Race condition su `sched_pick_next` | ✅ irq_disable/enable |
| `kernel/vfs.c` | FD 0/1/2 non pre-allocati | ✅ Già corretto |
| `fs/regfs.c` | CRC non includeva i dati | ✅ Già corretto |
| `fs/fat.c` | Loop infinito su catena FAT ciclica | ✅ Guard counter |
| `hal/bc` | Off-by-one `ip+4 >= prog_len` | ✅ Non è un bug reale |
| `nexs_line.c` | ANSI in prompt_len | ✅ Già corretto |
| `lang/lexer.c` | Closing quote non consumata | ✅ Già corretto |
| `kernel/proc.c` | arm64 `ctx->x20` non settato | ✅ Già corretto |
| `kernel/syscall.c` | `sender_pid == 0` senza check | ✅ Già corretto |

---

## Cosa manca ancora

### Alta priorità (bloccanti per il prossimo step)

| Componente | Dove | Problema |
|------------|------|---------|
| Syscall dispatch loop | `kernel/msg.c:kmsg_dispatch_loop()` | Struttura presente, mai avviata nel kernel baremetal |
| VFS path lookup | `kernel/vfs.c:path_to_ino()` | O(n) scan lineare su ogni `open()` — serve hash table |
| `fat_readdir()` | `fs/fat.c:196` | Stub `-1`, FAT directory listing non funziona |
| `sys_fork` + `sys_exec` | `kernel/syscall.c` | Funzionali solo come messaggi IPC, non wired al boot |
| `/mem/phys/<pfn>/` | registry | Buddy alloca ma non pubblica le pagine nel registry |
| IOAPIC routing | `hal/amd64/apic.c` | `ioapic_route()` mai chiamata — IRQ tastiera non instradato |
| Ring 0/1 enforcement | `kernel/syscall.c` | Logica descritta, non implementata |
| hal/amd64/boot.S | PAE ordering | PAE abilitato dopo le page table (solo 2MB huge pages) |
| hal/amd64/isr_stubs.S | Stack layout | `addq $16, %rsp` presuppone layout fisso — rompe su interrupt annidati |

### Media priorità

| Componente | Dove | Problema |
|------------|------|---------|
| Bitcode encoder/decoder | `compiler/bitcode.c` (non esiste) | Header `nexs_bitcode.h` completo, zero implementazione |
| `services/ui/init.nx` | Non caricato in `boot.nx` | UI service esiste ma non si avvia |
| `boot.nx` → `services/ui/init.nx` | `example/minios/boot.nx` | Da aggiungere dopo tty |
| `journal_log` ordering | `kernel/journal.c:97` | Legge dati old senza garanzia di ordinamento (flush prima di log) |
| `blk.c` read fail | `kernel/blk.c:105` | Read device fallita → buffer con dati non inizializzati in cache |

### Bassa priorità / futuro

| Componente | Note |
|------------|------|
| Bitcode format `.nxb` | Phase 6 PLAN.md — necessario per dual-partition build |
| Dual-partition boot image | `make minios` → kernel partition + FS partition |
| `state_save()/state_restore()` | `regfs_save` esiste in NEXS, manca il hook automatico al boot |
| 9P server completo | `fs/9p.c` ha la struttura, mancano i message handler |
| SMP bringup | AP init via SIPI non implementato |
| `sys_dup`/`sys_seek` test | Implementati, mai testati end-to-end |

---

## Prossimi step consigliati (in ordine)

1. **Avviare `kmsg_dispatch_loop()`** nel kernel baremetal dopo `sched_init()` — collega syscall IPC al kernel
2. **Caricare `services/ui/init.nx`** in `boot.nx` — completa lo stack di servizi
3. **Implementare `fat_readdir()`** — sblocca FAT directory listing per initrd
4. **Patch `hal/amd64/boot.S`** — PAE ordering fix (abilitare PAE prima di costruire le page table)
5. **Patch `hal/amd64/isr_stubs.S`** — stack frame canonico per interrupt annidati
6. **Implementare bitcode** `compiler/bitcode.c` — Phase 6 del PLAN, prerequisito per dual-partition
7. **IOAPIC routing** — `ioapic_route()` per tastiera/timer sul baremetal

---

## Struttura dei layer (come capirla)

```
C runtime (lang/ sys/ registry/ core/ hal/) 
  ↑ C builtins: out, reg_get/set, keys, vfs_*, read, write, ...
  
services/  ← librerie NEXS caricate da boot.nx (sistema)
  stdlib.nx  → input(), cat(), cp(), ...  (dipende da VFS)
  fs/init.nx → vfs_write/read/ls/resolve (VFS simulato in registry)
  pm/init.nx → pm_spawn/kill/ps
  auth/init.nx → auth_cap_grant/check/revoke
  
modules/   ← auto-caricato dal REPL (no dipendenze da services)
  stdlib.nx → input(), min/max/clamp, trim, startswith/endswith

example/minios/ ← applicazione demo (usa services + builtins)
  boot.nx → sequenza di boot
  shell.nx → shell interattiva
  nxed_editor.nx → editor full-screen
```

**Non confondere:**
- `cd/pwd/ls` come keyword NEXS → operano sull'eval scope (registry scope), NON sul VFS
- `shell_cwd()` in `shell.nx` → legge `/proc/1/vfs_cwd` per il VFS cwd dello user
- `vfs_*` → VFS simulato (indice flat in `/sys/vfs/files/`), non il C `kernel/vfs.c`
- `kernel/vfs.c` → VFS C per il kernel baremetal (fd table reale, inode, mount)
