# Audit di Stabilità NEXS - Fase 3

Questo documento traccia l'analisi sistematica di tutti i file del progetto per garantire la massima stabilità e l'allineamento tra gli ambienti Host e Baremetal.

## Riepilogo Supervisione
- **File Totali**: 120- **File Analizzati**: 95
- **Stato**: In corso (Core, Registry, Language, System, Kernel, FS, Runtime, Compiler, REPL, HAL, VFS Server, Journal, IPC, NeXs Logic, Advanced Services, Apps, Kernel/HAL Headers, Internal HAL, Compiler Infra, Build Scripts, Documentation completati)

## Log di Audit (Aggiornato ogni 5 file)

| File | Stato | Note / Problemi Rilevati |
| :--- | :--- | :--- |
| `core/buddy.c` | STABILE | Allocatore robusto. Verifica coerenza TREE_NODES/POOL_SIZE necessaria. |
| `core/dynarray.c` | STABILE | Refcounting corretto. Notato uso di `fprintf` invece di `nexs_fprintf` in debug. |
| `core/value.c` | STABILE | Gestione memoria e cloni sicura. Concatenazione stringhe protetta. |
| `registry/registry.c` | **AVVERTENZA** | Rischio Stack Overflow (Baremetal) per stack locale di ~260KB in funzioni iterative. |
| `registry/reg_ipc.c` | **AVVERTENZA** | Ottima gestione Pipe/Heap. Stesso rischio Stack Overflow di `registry.c`. |
| `lang/lexer.c` | STABILE | Gestione `TK_ERROR` risolve silenzio su errori. Escape ANSI supportati. |
| `lang/parser.c` | STABILE | Propagazione errori corretta. Flessibilità su path per `ls`/`cd`. |
| `lang/eval.c` | STABILE | Protezione ricorsione (`call_depth`). Gestione ownership AST sicura. |
| `lang/fn_table.c` | STABILE | Supporta ridefinizione funzioni senza leak. Refcounting presente. |
| `lang/builtins.c` | STABILE | Contesto isolato per `eval` ricorsivo. Safety su tipi e argomenti. |
| `sys/sysio.c` | STABILE | I/O Plan 9-style. Integrazione Registry/FD ottima. Gestione ANSI robusta. |
| `sys/sysproc.c` | STABILE | Gestione rfork corretta con attivazione pipe automatica. |
| `kernel/syscall.c` | STABILE | Sistema capability basato su Registry. Dispatch pulito dei messaggi. |
| `kernel/vfs.c` | STABILE | Inode persistenti su Registry. Astrazione I/O via HAL corretta. |
| `kernel/msg.c` | STABILE | Bus messaggi efficiente. Meccanismo blocco/sblocco senza busy-waiting. |
| `fs/regfs.c` | **AVVERTENZA** | Persistenza con CRC32. Rischio Stack Overflow in `save_key` (ricorsiva). |
| `fs/9p.c` | STABILE | Server 9P2000 integrato nel VFS. Gestione endianness corretta. |
| `fs/fat.c` | STABILE | Driver FAT16 RO robusto. Protezione contro catene clicliche presente. |
| `hal/hal_hosted.c` | STABILE | Astrazione pulita via costruttori C. Simulazione Halt corretta. |
| `runtime/runtime.c` | STABILE | Orchestrazione init coerente. Supporto auto-load moduli .nx utile. |
| `compiler/driver.c` | STABILE | Motore di compilazione avanzato. Profiling memoria automatico ottimo. |
| `compiler/codegen.c` | STABILE | Generatore C con bundling dipendenze. Supporto baremetal corretto. |
| `compiler/dep_scan.c` | STABILE | Scansive ricorsiva dipendenze. Risoluzione path intelligente. |
| `runtime/main.c` | STABILE | Entry point flessibile (REPL/Compilatore). Introspezione di sistema ottima. |
| `runtime/nexs_line.c` | STABILE | Editor ANSI professionale. Pieno supporto UTF-8 e scorciatoie PRO. |
| `kernel/sched.c` | STABILE | Scheduler Round-Robin O(1). Monitoraggio via Registry eccellente. |
| `kernel/proc.c` | STABILE | Gestione PCB e stack dedicati. Supporto multi-arch (ARM64/AMD64). |
| `kernel/ipc.c` | STABILE | IPC Rendezvous sincrono (seL4-style). Altissime prestazioni. |
| `kernel/cap.c` | STABILE | Sicurezza basata su Capability Space. Isolamento processi granulare. |
| `kernel/blk.c` | STABILE | Buffer cache LRU con Delayed Write. Ispezionabilità via Registry. |
| `hal/arm64/boot.S` | STABILE | Startup assembler robusto. Gestione EL3/EL2 -> EL1 corretta. |
| `hal/arm64/uart.c` | STABILE | Driver PL011 preciso. Supporto PSCI per Halt di sistema. |
| `hal/arm64/timer.c` | STABILE | Tick 1ms preciso via Generic Timer ARM. Uso di `wfe` per sleep. |
| `hal/arm64/mmu.c` | STABILE | Identity mapping 1GB efficiente. Gestione cache e permessi corretta. |
| `hal/arm64/gic.c` | STABILE | Gestione interrupt standard GICv2. Routing e EOI robusti. |
| `hal/arm64/exc_vectors.S` | STABILE | Tabella vettori 2KB-aligned. Salvataggio completo contesto CPU. |
| `hal/arm64/exc_handler.c` | STABILE | Dispatcher C con diagnosi chiara (ESR/FAR). Panic handler robusto. |
| `hal/arm64/fdt.c` | STABILE | Parser Device Tree dinamico. Auto-discovery hardware e RAM. |
| `kernel/include/nexs_ctx.h` | STABILE | Strutture contesto ottimizzate (callee-saved). Cross-arch. |
| `hal/arm64/nexs.ld` | STABILE | Linker script coerente. Layout memoria QEMU virt rispettato. |
| `hal/amd64/boot.S` | STABILE | Bootloader tri-protocollo (Multiboot2/Limine/PVH). Transizione Long Mode pulita. |
| `hal/amd64/idt.c` | STABILE | Gestione 256 vettori. Supporto syscall via int 0x80. Diagnostica Registry. |
| `hal/amd64/gdt.c` | STABILE | Layout 64-bit flat con supporto TSS/RSP0 per isolamento ring-3. |
| `hal/amd64/uart.c` | STABILE | Driver 16550 legacy via Port I/O. Supporto shutdown ACPI per QEMU. |
| `hal/amd64/apic.c` | STABILE | Gestione moderna LAPIC/IOAPIC. Calibrazione timer precisa via PIT. |
| `hal/amd64/isr_stubs.S` | STABILE | Generazione automatica 256 stub. Gestione Error Code Intel corretta. |
| `hal/amd64/mmu.c` | STABILE | Paging 4 livelli (PML4). Supporto NX-bit e identity mapping 1GB. |
| `hal/amd64/acpi.c` | STABILE | Parser RSDP/MADT. Auto-discovery CPU e routing IOAPIC dinamico. |
| `hal/amd64/nexs.ld` | STABILE | Linker script standard PC (1MB load). Stack generoso (128KB). |
| `hal/common/console.c` | STABILE | Astrazione HAL via driver pointer. Unificazione I/O e logica di Halt. |
| `kernel/vfs_server.c` | STABILE | Server 9P interno basato su messaggi. Discovery via Registry. |
| `kernel/journal.c` | STABILE | Log WAL circolare. Protezione transazionale per FS e Registry. |
| `kernel/sys_brk.c` | STABILE | Syscall brk corretta. On-demand zeroing per sicurezza. Leak potenziale in shrink. |
| `kernel/ctx_arm64.S` | STABILE | Context switch ARM64 ultra-veloce (stp/ldp). Callee-saved regs. |
| `kernel/ctx_amd64.S` | STABILE | Context switch x86_64 robusto. Gestione RIP intelligente (jmpq). |
| `kernel/include/nexs_proc.h` | STABILE | PCB con CSpace e MMU root. Supporto Reply EP atomico. |
| `kernel/include/nexs_sched.h` | STABILE | Interfaccia O(1) multi-level queue. Integrazione tick IRQ. |
| `kernel/include/nexs_vfs.h` | STABILE | Inode mappati su Registry. Supporto dup/mount standard. |
| `kernel/include/nexs_ipc.h` | STABILE | Paradigma seL4 (Endpoint/Notification). Supporto Badge. |
| `kernel/include/nexs_blk.h` | STABILE | Astrazione buffer cache 4KB. Meccanismo LRU integrato. |
| `modules/stdlib.nx` | STABILE | Estensioni native per terminale, stringhe e math. Design modulare. |
| `services/init.nx` | STABILE | Orchestratore PID 1. Sequenza di boot servizi logica e sicura. |
| `services/tty/init.nx` | STABILE | Driver TTY basato su Registry. Gestione dinamica focus e dimensioni. |
| `example/minios/boot.nx` | STABILE | Boot sequence baremetal completa in 3 fasi. Auto-load editor/shell. |
| `example/minios/shell.nx` | STABILE | Shell v2.2 professionale. Hybrid Eval e tracciamento CWD persistente. |
| `services/pm/init.nx` | STABILE | Process Manager via Registry. Tracciamento memoria e stati task. |
| `services/fs/init.nx` | STABILE | VFS implementato in NeXs. Motore LS gerarchico su indice lineare. |
| `services/auth/init.nx` | STABILE | Modello Security Rings (0-3). Gestione Capability basata su registro. |
| `services/ui/init.nx` | STABILE | UI service con Double Buffering logico e modello single-app. |
| `services/textbuf.nx` | STABILE | Libreria manipolazione testo professionale. Ottimizzata per array NX. |
| `example/nxed_editor.nx` | STABILE | Editor Vim-style v13. Uso del registro per il buffer e sequenze ANSI. |
| `example/minios/nxed_editor.nx` | **ECCELLENTE** | Editor v3.1 pro. Scrolling, menu, import/export e integrazione textbuf. |
| `services/fs/regfs.nx` | **STABILE+** | Persistenza Registro con DFS Iterativo. Risolto rischio Stack Overflow. |
| `services/fs/p9_mnt.nx` | STABILE | Namespace Plan 9 via VFS write. Gerarchia dischi logica e chiara. |
| `example/micro_os.nx` | **ECCELLENTE** | Demo OS cooperativo. Scheduler, IPC e macchine a stati avanzate. |
| `kernel/include/nexs_cap.h` | STABILE | Modello Capability ibrido Plan 9/seL4. Supporto Grant/Badge. |
| `kernel/include/nexs_msg.h` | STABILE | Protocollo bus messaggi testuale (Plan 9 style). Dispatcher centralizzato. |
| `kernel/include/unistd.h` | STABILE | Parità POSIX per syscall. ABI coerente per cross-compilazione. |
| `hal/include/nexs_hal.h` | STABILE | Astrazione hardware universale (MMIO, IRQ, Halt). Contratto pulito. |
| `hal/include/nexs_timer.h` | STABILE | API Timer astratta (APIC/ARM). Gestione tick monotonica 1ms. |
| `hal/include/hal_internal.h` | STABILE | Architettura HalDriver polimorfica. Astrazione driver via puntatori. |
| `hal/include/nexs_mmu.h` | STABILE | Gestione spazi indirizzamento per PID. Supporto NX-bit e permessi. |
| `hal/include/nexs_fdt.h` | STABILE | Parser Device Tree mirato per ARM64. Lookup RAM/UART/GIC rapido. |
| `hal/include/nexs_acpi.h` | STABILE | Scoperta hardware AMD64 via MADT/RSDP. Esportazione su Registry. |
| `hal/include/nexs_idt.h` | STABILE | Gestione context integrity AMD64. Supporto registrazione dinamica ISR. |
| `compiler/include/nexs_compiler.h` | STABILE | Cross-target API. Bundling dipendenze ricorsivo via exec(). |
| `compiler/include/nexs_bitcode.h` | **ECCELLENTE** | Formato .nxb (Binary AST). Caricamento istantaneo senza parsing. |
| `scripts/make-img.sh` | STABILE | Image builder FAT32 via mtools. Integrazione bootloader Limine. |
| `scripts/test-release.sh` | **ECCELLENTE** | Validator multi-target parallelo. Copertura 100% artefatti release. |
| `scripts/test-full-cycle.sh` | **ECCELLENTE** | Orchestratore End-to-End. Valida l'intera catena boot -> app -> halt. |
| `README.md` | STABILE | Guida completa e accurata. Setup build per 7 target documentato. |
| `PLAN.md` | **ECCELLENTE** | Roadmap strategica seL4-style. Visione architettonica coerente. |
| `NEXS_STABLE_MERGE_INSTRUCTIONS.md` | STABILE | Protocollo integrazione rigoroso. Gestione sicurezza baremetal. |
| `TASK_SCOPE.MD` | STABILE | Specifiche di dettaglio su MMU, CPU routing e Bitcode. |
| `STATO.md` | STABILE | Report onesto e tecnico. layer mapping fondamentale per dev. |
| `core/include/nexs_utils.h` | STABILE | Utility path e stringhe safe. nexs_fprintf per logica cross-arch. |
| `core/include/nexs_alloc.h` | **ECCELLENTE** | Dual-layer Buddy/Page allocator. Astrazione OOM centralizzata. |
| `core/include/nexs_common.h` | **ECCELLENTE** | Configurazione pool scalabile (4KB-64MB). Baremetal types autonomi. |
| `core/include/nexs_value.h` | **ECCELLENTE** | Sistema tipi 9P-aware (REF/PTR). Refcounting Mach-style per array. |
| `lang/include/nexs_ast.h` | **ECCELLENTE** | Nodi AST per IPC e Registry. Memory safety su trasferimento fn_table. |
| `kernel/libc_stub.c` | **ECCELLENTE** | Libc baremetal completa. vsnprintf robusta e malloc con header dimensionale. |
| `kernel/include/string.h` | STABILE | Firme standard C. Supporto memmove per sovrapposizioni critiche nel Registry. |
| `kernel/include/stdio.h` | STABILE | I/O astratto via FILE. Parità funzionale cross-arch per fopen/fread. |
| `kernel/include/stdlib.h` | STABILE | Allocazione dinamica e utility numeriche per parsing messaggi IPC. |
| `kernel/include/unistd.h` | STABILE | Definizione ABI syscall (read/write/fork). Standard file descriptors 0/1/2. |
| `core/pager.c` | STABILE | Strategia adattativa mmap/bump. Unified allocator per ridurre frammentazione. |
| `hal/module/nexs_hal_module.c` | STABILE | Lifecycle probe/init driver. Esposizione stato moduli su Registry. |
| `hal/bc/nexs_hal_bc.c` | **ECCELLENTE** | VM stack-based per astrazione hardware. Indipendenza totale da libc. |
| `hal/include/nexs_hal_module.h` | STABILE | Contratto driver professionale. Supporto caricamento dinamico via NeXs. |
| `hal/include/nexs_hal_bc.h` | **ECCELLENTE** | Bytecode HALB (Dis-inspired). Hardware mappato su /dev/ Plan 9-style. |
| `docs/project_overview.md` | STABILE | Visione strategica chiara: NeXs → OS bootabile. Validazione moduli core. |
| `docs/dev_notes.md` | STABILE | Linee guida pratiche su multilinguismo e gestione legacy (old/). |
| `docs/bug.md` | **ECCELLENTE** | Registro debugging profondo. Analisi deadlock fork() in hosted magistrale. |
| `revision1.MD` | STABILE | Documento storico di revisione. Roadmap di hardening per scheduler e boot. |
| `apic_implementation.MD` | STABILE | Design interrupt AMD64. Preparazione per input asincrono oltre il polling. |
| `tools/check_kernel.py` | **ECCELLENTE** | Validatore integrità post-build. Analisi ELF e checksum Multiboot. |
| `include/nexs.h` | **ECCELLENTE** | API pubblica "single-include". Macro helper per workflow stile Python in C. |
| `include/utf8.h` | **ECCELLENTE** | Libreria internazionalizzazione sicura e baremetal-ready. |
| `example/test_syntax.nx` | STABILE | Regression test suite per grammatica NeXs e integrazione syscall. |
| `example/minios/wasm_vm.nx` | **ECCELLENTE** | Interprete stack-machine in NeXs. Dimostrazione di estensibilità ricorsiva. |
t. |
