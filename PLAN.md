# NEXS OS — Piano di Trasformazione
## Da scripting runtime a sistema operativo stabile

> Stile: Thompson · Pike · Ritchie · Wozniak
> Filosofia: tutto è file · tutto è messaggio · tutto è registro

---

## Stato Attuale (v0.2.0)

```
core/        buddy + pager + value + dynarray + utils     ✅ stabile
registry/    registro gerarchico + IPC non-bloccante       ✅ stabile
lang/        lexer + parser + fn_table + eval + builtins   ✅ stabile
sys/         sysio (Plan 9) + sysproc                      ✅ stabile
compiler/    codegen + driver + 7 target                   ✅ funzionante
hal/         boot.S + uart.c + nexs.ld (arm64 + amd64)    ⚠️  QEMU-only, 32-bit x86
runtime/     REPL + init + IPC + TYPE_PTR                  ✅ stabile
```

**Gap critici:**
- x86_64 rimane in 32-bit (no long mode, no GDT, no paging)
- Nessuna gestione delle eccezioni/interrupt
- Nessun processo, nessun context switch
- Nessun filesystem persistente
- HAL = stub UART, nulla di più

---

## Architettura Target

```
┌─────────────────────────────────────────────────────┐
│                  NEXS Scripts (.nx)                  │  ← userspace in NEXS
├─────────────────────────────────────────────────────┤
│              Registry Namespace (/sys /proc /dev)    │  ← tutto è registro
├──────────────┬──────────────┬───────────────────────┤
│  IPC Engine  │  Scheduler   │  VFS (9P)             │  ← kernel services
│  (reg_ipc)   │  (seL4-msg)  │  (reg-backed)         │
├──────────────┴──────────────┴───────────────────────┤
│  HAL: MMU · Interrupts · Timer · Block · UART        │  ← C puro
├──────────────────────────────────────────────────────┤
│  Bootloader: Stage1 (MBR/UEFI) + Stage2 (long mode) │  ← asm minimale
└─────────────────────────────────────────────────────┘
```

**Principio IPC seL4-style:**
- Ogni processo è un insieme di path nel registro `/proc/<pid>/`
- NESSUNA memoria condivisa — tutto via `sendmessage`/`receivemessage`
- Il kernel è un server di messaggi: syscall = messaggio a `/sys/kernel`
- Context switch = timer IRQ → salva TCB in registro → carica prossimo TCB
- La policy del scheduler vive in `/sys/sched/` come configurazione NEXS

---

## Struttura Directory Target

```
hal/
  include/
    nexs_hal.h          (già presente, da espandere)
    nexs_mmu.h          NUOVO — page table, TLB, MMU API
    nexs_idt.h          NUOVO — IDT/VBAR, exception vectors, IRQ
    nexs_acpi.h         NUOVO — ACPI/RSDP/MADT parsing
    nexs_fdt.h          NUOVO — FDT/device-tree parsing (ARM)
    nexs_timer.h        NUOVO — APIC timer / ARM generic timer
    nexs_smp.h          NUOVO — SMP bringup, AP init
  amd64/
    boot/
      stage1.S          NUOVO — MBR 446-byte + GPT bootstrap
      stage2.S          NUOVO — 32→64 bit transition, load kernel
      stage2.c          NUOVO — FAT16 loader, parse Multiboot2 tags
    gdt.c               NUOVO — GDT + TSS + LDT
    idt.c               NUOVO — IDT 256 entries + exception stubs
    apic.c              NUOVO — LAPIC + IOAPIC init + timer
    mmu.c               NUOVO — PML4 page tables, CR3, TLB
    acpi.c              NUOVO — RSDP → RSDT/XSDT → MADT → SRAT
    boot.S              (già presente, da sostituire con stage2)
    uart.c              (già presente, da migliorare)
    nexs.ld             (da aggiornare per 64-bit + kernel mapping)
  arm64/
    boot/
      stage1.S          NUOVO — EL3/EL2→EL1 transition
      uboot_env.txt     NUOVO — U-Boot environment per boot HW reale
    exc_vectors.S       NUOVO — VBAR_EL1, eccezioni + IRQ handler
    mmu.c               NUOVO — TCR_EL1, TTBR0/1, MAIR, MMU enable
    gic.c               NUOVO — GICv2/GICv3 interrupt controller
    timer.c             NUOVO — ARM generic timer (CNTV_CTL_EL0)
    fdt.c               NUOVO — FDT blob parsing (memory, uart, irq)
    boot.S              (da espandere con EL handling)
    uart.c              (già presente)
    nexs.ld             (da aggiornare per MMU mapping)

kernel/
  include/
    nexs_proc.h         NUOVO — TCB, process state, PCB in registry
    nexs_sched.h        NUOVO — scheduler API, priority, timeslice
    nexs_ctx.h          NUOVO — context save/restore (arch-specific asm)
    nexs_msg.h          NUOVO — kernel IPC protocol (sopra reg_ipc)
    nexs_vfs.h          NUOVO — VFS inode/dentry/mount (9P-style)
    nexs_blk.h          NUOVO — block device abstraction
    nexs_journal.h      NUOVO — WAL journal per crash recovery
  proc.c                NUOVO — process create/destroy/wait
  sched.c               NUOVO — round-robin + priority scheduler
  ctx_amd64.S           NUOVO — save/restore registri x86_64
  ctx_arm64.S           NUOVO — save/restore registri AArch64
  msg.c                 NUOVO — wrapper kernel IPC + capability check
  vfs.c                 NUOVO — VFS layer (inode cache, mount table)
  blk.c                 NUOVO — buffer cache (4KB blocks, write-back)
  journal.c             NUOVO — WAL: log → checkpoint → replay
  syscall.c             NUOVO — tabella syscall → dispatch via IPC

fs/
  include/
    nexs_fs9p.h         NUOVO — 9P2000 protocol types + API
    nexs_fat.h          NUOVO — FAT16/32 per bootloader + init ramdisk
    nexs_regfs.h        NUOVO — registry serializzato su disco
  9p.c                  NUOVO — 9P2000 server (mount, walk, read, write)
  fat.c                 NUOVO — FAT16/32 driver (read-only per ora)
  regfs.c               NUOVO — registry → binary file e viceversa

(esistenti, invariati)
core/   registry/   lang/   sys/   compiler/   runtime/
```

---

## Fasi di Implementazione

### FASE 0 — Preparazione (pre-requisiti, nessun codice)

**Download toolchain:**
```bash
# macOS Intel (host corrente)
brew install x86_64-elf-gcc x86_64-elf-binutils      # cross-compiler hosted AMD64
brew install aarch64-elf-gcc aarch64-elf-binutils     # cross ARM64
brew install qemu                                      # test
brew install nasm                                      # assembler x86
```

**ISO/immagini di riferimento da scaricare:**
- OVMF (UEFI firmware per QEMU): `brew install --cask qemu` lo include
- SeaBIOS: già incluso in QEMU
- Grub2 source (per reference, non dipendenza): https://ftp.gnu.org/gnu/grub/

**Principio:** NON usiamo GRUB come dipendenza. Scriviamo il nostro stage1/stage2.
Motivo: Thompson — se non lo capisci, non lo controlli.

---

### FASE 1 — x86_64 Long Mode Bootstrap

**File:** `hal/amd64/boot/stage1.S` (446 byte MBR)
```
MBR layout:
  [0x000] stage1 code     (446 byte) — carica stage2 da settore 2
  [0x1BE] partition table (64 byte)  — 4 entry da 16 byte
  [0x1FE] 0x55AA boot signature
```

**stage1.S — real mode (16-bit):**
```asm
; Carica 31 settori da LBA 1 a 0x7E00 via BIOS int 0x13/AH=0x42
; Poi jmp 0x0000:0x7E00
```

**File:** `hal/amd64/boot/stage2.S` + `stage2.c`
```
stage2 fa:
  1. A20 line enable (keyboard controller o port 0x92)
  2. Legge E820 memory map via BIOS int 0x15 (salva in 0x8000)
  3. Carica GDT temporanea (32-bit flat)
  4. CR0.PE = 1 → protected mode
  5. Far jmp a codice 32-bit
  6. Imposta page tables (PML4 identity map 0→4GB)
  7. EFER.LME = 1 → long mode enable
  8. CR0.PG = 1 → paging on
  9. Far jmp a codice 64-bit
  10. Carica GDT definitiva + TSS
  11. Chiama nexs_kernel_main(memory_map, boot_info)
```

**File:** `hal/amd64/gdt.c`
```c
/* 5 descriptor: null, kernel code 64-bit, kernel data, user code 64-bit, user data */
/* + TSS per interrupt stack */
typedef struct { uint16_t limit_lo; uint16_t base_lo; ... } GdtEntry;
void gdt_init(void);
void gdt_load(void);  /* lgdt */
void tss_init(void *isr_stack);
```

**File:** `hal/amd64/idt.c`
```c
/* 256 handler stubs generati da macro */
/* Ogni stub pusha error_code (o 0) + vector_num e chiama common_isr_handler */
void idt_init(void);
void idt_set_handler(uint8_t vec, void *handler, uint8_t dpl);
/* common_isr_handler(regs*) → dispatch via kernel/syscall.c */
```

**File:** `hal/amd64/mmu.c`
```c
/* PML4 → PDPT → PD → PT (4KB pagine) */
/* Layout: kernel 0xFFFFFFFF80000000 (kernel high), user 0→128TB */
void mmu_init(void);
void mmu_map_page(uint64_t virt, uint64_t phys, uint32_t flags);
void mmu_unmap_page(uint64_t virt);
void mmu_flush_tlb(uint64_t virt);
uint64_t mmu_virt_to_phys(uint64_t virt);
```

**File:** `hal/amd64/apic.c`
```c
/* LAPIC: rilevamento via CPUID, map MMIO, enable, set timer */
/* IOAPIC: route IRQ0 (timer) → vector 0x20, IRQ1 (kbd) → 0x21 */
void apic_init(void);
void apic_eoi(void);               /* End Of Interrupt */
void apic_timer_set(uint32_t ticks); /* one-shot o periodic */
```

**File:** `hal/amd64/acpi.c`
```c
/* Scan 0xE0000-0xFFFFF per "RSD PTR" signature */
/* RSDP → RSDT/XSDT → FADT, MADT, SRAT */
/* MADT: rileva LAPIC/IOAPIC addresses, CPU count */
int  acpi_init(void);
int  acpi_find_rsdp(void);
void acpi_parse_madt(void);        /* popola /hal/acpi/ nel registro */
int  acpi_cpu_count(void);
```

---

### FASE 2 — ARM64 EL Setup + MMU

**File:** `hal/arm64/boot/stage1.S`
```asm
/* Se entrato a EL3: configura SCR_EL3, passa a EL2 */
/* Se EL2: configura HCR_EL2 (VHE o no), passa a EL1 */
/* EL1: imposta stack_el1, chiama mmu_init() */
/* Vettori eccezioni: VBAR_EL1 = exc_vectors */
```

**File:** `hal/arm64/exc_vectors.S`
```asm
/* Tabella vettori 2KB-allineata per VBAR_EL1 */
/* 16 handler: sync/irq/fiq/serror × (curr_el0/currx/lowerA64/lowerA32) */
/* Ogni entry: 128 byte, salva tutti i registri, chiama C handler */
```

**File:** `hal/arm64/mmu.c`
```c
/* TCR_EL1: T0SZ=16 (48-bit user), T1SZ=16 (48-bit kernel) */
/* MAIR_EL1: normal cacheable, device, strongly-ordered */
/* TTBR0_EL1: user page tables, TTBR1_EL1: kernel page tables */
/* Granule: 4KB, 3 livelli (PGD→PMD→PTE) */
void mmu_init(void);
void mmu_map(uint64_t virt, uint64_t phys, uint64_t size, uint32_t attrs);
```

**File:** `hal/arm64/gic.c`
```c
/* GICv2: GICD (distributor) + GICC (CPU interface) */
/* GICv3: ICC_* system registers */
/* Auto-detect da FDT o ACPI */
void gic_init(void);
void gic_enable_irq(uint32_t irq);
void gic_set_priority(uint32_t irq, uint8_t prio);
void gic_eoi(uint32_t irq);
```

**File:** `hal/arm64/fdt.c`
```c
/* Parse FDT blob passato da U-Boot/QEMU in x1/x2 all'entry */
/* Legge: /memory, /chosen (cmdline), /cpus, uart, gic, timer */
void fdt_init(void *blob);
uint64_t fdt_get_ram_base(void);
uint64_t fdt_get_ram_size(void);
const char *fdt_get_cmdline(void);
uint64_t fdt_get_uart_base(void);
```

---

### FASE 3 — Kernel: Processo + Scheduler (seL4-style IPC)

**Principio chiave:**
```
TUTTO il cross-process communication passa per reg_ipc.
Il kernel è un "server" raggiungibile a /sys/kernel/ipc.
Ogni syscall = messaggio strutturato.
Il scheduler è triggerato dal timer IRQ.
```

**File:** `kernel/include/nexs_proc.h`
```c
typedef enum {
  PROC_RUNNING, PROC_READY, PROC_BLOCKED, PROC_ZOMBIE
} ProcState;

typedef struct NexsProc {
  uint32_t   pid;
  ProcState  state;
  char       reg_path[REG_PATH_MAX];  /* /proc/<pid> nel registro */
  char       ipc_inbox[REG_PATH_MAX]; /* /proc/<pid>/inbox */
  void      *ctx;          /* puntatore al contesto salvato (arch-specific) */
  uint8_t    priority;     /* 0-255 */
  uint64_t   timeslice_us; /* quanto di tempo */
  uint64_t   ticks_used;
  struct NexsProc *next;   /* lista ready queue */
} NexsProc;

/* API — tutto vive nel registro /proc/ */
NexsProc *proc_create(const char *name, void (*entry)(void*), void *arg);
void      proc_destroy(NexsProc *p);
void      proc_yield(void);
void      proc_block(const char *wait_path); /* bloccato su IPC path */
void      proc_unblock_by_msg(const char *ipc_path);
```

**File:** `kernel/include/nexs_ctx.h`
```c
/* x86_64 context */
typedef struct { uint64_t rax,rbx,rcx,rdx,rsi,rdi,rbp,rsp,
                          r8,r9,r10,r11,r12,r13,r14,r15,rip,rflags,
                          cs,ss,ds,es,fs,gs; } Ctx_amd64;

/* arm64 context */
typedef struct { uint64_t x[31]; uint64_t sp; uint64_t pc; uint64_t pstate; } Ctx_arm64;

/* arch-agnostic wrapper */
void ctx_save(void *ctx);   /* salva CPU → struct */
void ctx_load(void *ctx);   /* carica struct → CPU */
void ctx_switch(void *from_ctx, void *to_ctx); /* atomico */
```

**File:** `kernel/ctx_amd64.S`
```asm
/* ctx_switch(from, to):
   push tutti i registri callee-saved in from->*
   pop tutti i registri callee-saved da to->*
   ret (ritorna nel contesto di to) */
```

**File:** `kernel/sched.c`
```c
/* Round-robin con priority queue (8 livelli) */
/* Ogni livello è una lista circolare di NexsProc */
/* Timer IRQ (ogni 1ms) chiama sched_tick() */

void sched_init(void);
void sched_add(NexsProc *p);
void sched_remove(NexsProc *p);
void sched_tick(void);        /* chiamato da timer IRQ */
NexsProc *sched_pick_next(void); /* O(1) — prende dalla testa del livello più alto */

/* Tutto il registro sched vive in /sys/sched/:
   /sys/sched/policy = "rr"
   /sys/sched/quantum_us = 1000
   /sys/sched/ncpu = 1
   /sys/sched/runqueue_depth = <count> */
```

**File:** `kernel/msg.c` — kernel IPC protocol (sopra reg_ipc)
```c
/* Formato messaggio kernel: Value TYPE_STR con JSON-like encoding
   "syscall:open:/path/to/file:flags" → Value str
   Risposta: "ok:fd:3" o "err:2:no such file" */

typedef struct {
  char   verb[32];      /* "open", "read", "write", "fork", "wait", ... */
  char   arg0[256];
  char   arg1[256];
  int64_t n;            /* argomento numerico */
  uint32_t sender_pid;
} KernelMsg;

/* Codifica/decodifica su Value TYPE_STR */
Value  kmsg_encode(const KernelMsg *m);
int    kmsg_decode(const Value *v, KernelMsg *out);

/* Dispatch: legge da /sys/kernel/inbox e chiama il giusto handler */
void   kmsg_dispatch_loop(void); /* il kernel "main loop" */
```

**File:** `kernel/syscall.c`
```c
/* Tabella: verb → handler function */
/* Ogni handler è una C function ma REGISTRATA nel registro /sys/kernel/handlers/<verb> */
/* Lo standard NEXS: out sys_open("/file", 0) chiama via registro */

static SyscallHandler syscall_table[] = {
  {"open",  sys_open},
  {"read",  sys_read},
  {"write", sys_write},
  {"close", sys_close},
  {"fork",  sys_fork},
  {"exec",  sys_exec},
  {"wait",  sys_wait},
  {"exit",  sys_exit},
  {"mount", sys_mount},
  {"bind",  sys_bind},
  {"stat",  sys_stat},
  {NULL, NULL}
};

/* sys_fork: crea nuovo NexsProc, copia registro /local/ del padre
             crea /proc/<newpid>/ nel registro
             ritorna pid figlio al padre via IPC reply */
```

---

### FASE 4 — VFS (Virtual File System) 9P-style

**Filosofia:** Il filesystem è il registro. Ogni file è un path.
Il VFS traduce operazioni POSIX → operazioni sul registro.
Il disco è un caso speciale di "registro persistente".

**File:** `kernel/vfs.c`
```c
/* Inode = RegKey con metadati aggiuntivi in /vfs/inode/<ino>/ */
/* Dentry = path nel registro */
/* Mount = reg_mount() + /vfs/mount/<id>/ */

typedef struct {
  uint64_t ino;
  uint32_t mode;      /* S_IFREG, S_IFDIR, S_IFLNK */
  uint32_t uid, gid;
  uint64_t size;
  uint64_t atime, mtime, ctime;
  char     reg_data_path[REG_PATH_MAX]; /* dove vivono i dati */
} VfsInode;

int  vfs_open(const char *path, int flags);
int  vfs_read(int fd, void *buf, size_t n);
int  vfs_write(int fd, const void *buf, size_t n);
int  vfs_close(int fd);
int  vfs_stat(const char *path, VfsInode *out);
int  vfs_mkdir(const char *path, uint32_t mode);
int  vfs_mount(const char *src, const char *dst, const char *fstype);
```

**File:** `kernel/blk.c` — buffer cache
```c
/* Buffer: 4KB blocchi, LRU cache di 256 blocchi in RAM */
/* Ogni blocco: /dev/blk/<device>/<lba> nel registro quando "dirty" */
/* Writeback: ogni 5 secondi o su sync() */

typedef struct BlkBuf {
  uint64_t  lba;
  uint8_t   data[4096];
  int       dirty;
  int       valid;
  uint32_t  dev_id;
} BlkBuf;

int  blk_read(uint32_t dev, uint64_t lba, void *buf);
int  blk_write(uint32_t dev, uint64_t lba, const void *buf);
void blk_sync(void);
```

**File:** `kernel/journal.c` — Write-Ahead Log
```c
/* Journal: log circolare di 512 record × 4KB su settori dedicati */
/* Prima di scrivere su disco: scrivi nel journal */
/* Dopo crash: replay dal journal fino all'ultimo checkpoint */
/* Checkpoint: ogni 64 transazioni o ogni 30 secondi */

int  journal_init(uint32_t dev, uint64_t journal_lba, uint32_t n_blocks);
int  journal_begin(void);                     /* apre transazione */
int  journal_log(uint32_t dev, uint64_t lba); /* log blocco prima di write */
int  journal_commit(void);                    /* commit transazione */
int  journal_replay(void);                    /* replay dopo crash */
```

---

### FASE 5 — Registry su Disco (nexs_regfs)

**Il registro è anche il filesystem.** Quando monto `/reg` su disco,
ogni scrittura nel registro viene flushed su file binario.

**Formato binario del registro (`.nexsreg`):**
```
[Header 32 byte]
  magic:     "NEXSREG1"
  version:   uint32
  n_keys:    uint32
  root_off:  uint64 (offset del nodo radice)
  crc32:     uint32

[Array di RegRecord]
  path:      256 byte
  type:      uint8  (ValueType)
  rights:    uint8
  data_len:  uint32
  data:      variabile (string/int/float/nil)
  n_children: uint32
  children:  array di offsets
```

**API:**
```c
int regfs_save(const char *path_in_reg, const char *file_path);
int regfs_load(const char *file_path, const char *mount_point);
int regfs_sync(void);       /* flush dirty keys to disk */
void regfs_watch(const char *reg_path, const char *notify_ipc_path);
  /* quando reg_path cambia → manda messaggio a notify_ipc_path */
```

---

### FASE 6 — Plan 9 ABI Completa

**Tabella syscall Plan 9 → nostro IPC:**

| P9 Syscall | Nostro messaggio a /sys/kernel/inbox |
|-----------|--------------------------------------|
| `open(fd, path, mode)` | `"open:<path>:<mode>"` |
| `read(fd, buf, n)` | `"read:<fd>:<n>"` |
| `write(fd, buf, n)` | `"write:<fd>:<data>"` |
| `close(fd)` | `"close:<fd>"` |
| `stat(path, buf)` | `"stat:<path>"` |
| `wstat(path, buf)` | `"wstat:<path>:<fields>"` |
| `fstat(fd, buf)` | `"fstat:<fd>"` |
| `create(path, mode, perm)` | `"create:<path>:<mode>:<perm>"` |
| `remove(path)` | `"remove:<path>"` |
| `bind(new, old, flags)` | `"bind:<new>:<old>:<flags>"` |
| `mount(fd, afd, old, flags, aname)` | `"mount:<old>:<flags>"` |
| `unmount(name, old)` | `"unmount:<old>"` |
| `fork()` | `"fork"` |
| `rfork(flags)` | `"rfork:<flags>"` |
| `exec(path, argv)` | `"exec:<path>:<argv_json>"` |
| `await(msg)` | `receivemessage /proc/<pid>/wait` |
| `exits(msg)` | `"exit:<msg>"` |
| `chdir(path)` | `"chdir:<path>"` |
| `getpid()` | legge `/proc/self/pid` |
| `sleep(ms)` | `"sleep:<ms>"` |
| `alarm(ms)` | `"alarm:<ms>"` |
| `pipe(p)` | `"pipe"` → risposta 2 fd |
| `dup(oldfd, newfd)` | `"dup:<old>:<new>"` |
| `seek(fd, n, type)` | `"seek:<fd>:<n>:<type>"` |

**Tutte le opzioni di base diventano funzioni NEXS in `/sys/`:**
```nexs
# Scritto direttamente in NEXS, caricato all'avvio da /boot/stdlib.nx
fn cat(path) {
  fd = open(path 0)
  if fd < 0 { out "error: cannot open " + path ret }
  loop {
    data = read(fd 512)
    if len(data) == 0 { break }
    out data
  }
  close(fd)
}

fn ls(path) {
  dir = open(path 0)
  loop {
    entry = read(dir 256)
    if len(entry) == 0 { break }
    out entry
  }
  close(dir)
}
```

---

### FASE 7 — Multiprocessing seL4-style in NEXS

**Il modello:**
```
Ogni processo è descritto nel registro:
  /proc/<pid>/name     = "init"
  /proc/<pid>/state    = "running"
  /proc/<pid>/priority = 128
  /proc/<pid>/inbox    = [IPC queue — riceve messaggi]
  /proc/<pid>/outbox   = [IPC queue — risponde]
  /proc/<pid>/caps     = "read:write:exec"  (capability bitmask)
  /proc/<pid>/fds/0    = /dev/stdin
  /proc/<pid>/fds/1    = /dev/stdout
  /proc/<pid>/fds/2    = /dev/stderr

Un processo manda syscall così:
  sendmessage /sys/kernel/inbox "open:/etc/motd:0"
  risposta = receivemessage /proc/self/inbox

Il kernel esegue la syscall e risponde.
Non c'è modo di "chiamare" il kernel direttamente — solo messaggi.
```

**File:** `/boot/init.nx` — il primo processo in NEXS
```nexs
# Init: registra i servizi base nel registro
reg /sys/init/status = "booting"

# Avvia il server filesystem
exec "fs/9pserver.nx"

# Avvia la shell
exec "bin/sh.nx"

reg /sys/init/status = "running"
loop {
  msg = receivemessage /proc/1/inbox
  if msg == "shutdown" { exits "shutdown" }
}
```

---

### FASE 8 — Paging con Supervisione Registro

**Il page allocator diventa "supervisore":**
Ogni pagina fisica ha un entry nel registro:

```
/mem/phys/<page_pfn>/state    = "free" | "used" | "kernel" | "dma"
/mem/phys/<page_pfn>/owner    = <pid>
/mem/phys/<page_pfn>/virt     = <virtual address>
/mem/virt/<pid>/<va>/phys     = <physical address>
/mem/virt/<pid>/<va>/flags    = "rw" | "ro" | "x" | "cow"
```

**File:** `hal/include/nexs_mmu.h`
```c
/* Page Frame Number (PFN) = phys_addr / PAGE_SIZE */
typedef uint64_t pfn_t;
typedef uint64_t vaddr_t;

int  mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags);
int  mm_free_page(uint32_t pid, vaddr_t virt);
int  mm_map_range(uint32_t pid, vaddr_t virt, pfn_t phys, size_t pages, uint32_t flags);
void mm_page_fault_handler(vaddr_t fault_addr, uint32_t error_code);
/* Ogni operazione aggiorna il registro /mem/phys/<pfn>/ */
```

---

## Ordine di Implementazione (Incrementale)

```
STEP 01  hal/amd64/: GDT + IDT stub + long mode transition
STEP 02  hal/amd64/: APIC init + timer IRQ (1ms tick)
STEP 03  hal/amd64/: MMU init (identity map kernel)
STEP 04  hal/amd64/: ACPI RSDP/MADT parse → /hal/acpi/ nel registro
STEP 05  hal/arm64/: EL2→EL1 + exception vectors + GIC + ARM timer
STEP 06  hal/arm64/: MMU + FDT parse → /hal/fdt/
STEP 07  kernel/:    TCB + context switch amd64 + sched round-robin
STEP 08  kernel/:    context switch arm64
STEP 09  kernel/:    syscall dispatch via IPC
STEP 10  kernel/:    sys_fork + sys_exec (semplificati)
STEP 11  kernel/:    buffer cache (blk.c)
STEP 12  kernel/:    journal WAL
STEP 13  fs/:        regfs (registro su disco)
STEP 14  fs/:        FAT16 read-only (per initrd)
STEP 15  fs/:        VFS layer + sys_open/read/write/close
STEP 16  fs/:        9P server in NEXS (9pserver.nx)
STEP 17  hal/:       paging supervisor → /mem/phys/ nel registro
STEP 18  runtime/:   /boot/stdlib.nx (cat, ls, cp, mv in NEXS)
STEP 19  runtime/:   /boot/init.nx (primo processo)
STEP 20  testing:    QEMU amd64 + arm64 boot to shell
```

---

## Regole di Stile (da rispettare in ogni file)

1. **Nessuna ricorsione non limitata** — usare stack esplicito o iterativo
2. **Ogni nuova struttura ha un path nel registro** — statefulness = registry
3. **Cross-process = solo IPC** — nessun puntatore condiviso tra processi
4. **Ogni file C fa UNA cosa** — gdt.c sa solo di GDT, idt.c solo di IDT
5. **Nessuna dipendenza esterna oltre libc** (hosted) e nessuna su bare metal
6. **Funzioni C max 60 righe** — se è più lunga, spezza
7. **Il NEXS scripting è il "userspace"** — nessuna feature di sistema in .nx che non sia già in /sys/
8. **Ogni errore ritorna al registro** — `/sys/errors/<timestamp>/msg = "..."``
9. **Test QEMU prima, HW reale dopo**

---

## Come Testare Ogni Step

```bash
# Step 1-4: x86_64 long mode
qemu-system-x86_64 -kernel build/baremetal-amd64/nexs.elf \
  -serial stdio -display none -m 128M

# Step 5-6: ARM64
qemu-system-aarch64 -M virt -cpu cortex-a57 \
  -kernel build/baremetal-arm64/nexs.elf \
  -serial stdio -display none -m 128M

# Step 7+: con disco virtuale
qemu-system-x86_64 -kernel build/baremetal-amd64/nexs.elf \
  -drive file=disk.img,format=raw \
  -serial stdio -display none -m 256M

# Creare disco di test
dd if=/dev/zero of=disk.img bs=1M count=64
mkfs.fat -F 16 disk.img
```

---

## Note Finali

Questo piano produce un sistema che:
- Boota da bare metal su x86_64 e ARM64
- Gestisce i processi SOLO via messaggi (seL4-style)
- Ha un filesystem basato sul registro NEXS (Plan 9-style)
- È programmabile in NEXS a partire da `/boot/init.nx`
- È completamente ispezionabile via `:reg /` dal REPL
- Può compilare programmi NEXS in binari standalone per 7 target

La struttura rimane fedele al progetto:
**il registro è il kernel** — tutto il resto è un client del registro.
