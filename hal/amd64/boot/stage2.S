; ============================================================
; hal/amd64/boot/stage2.S — NEXS Stage2: Real→Protected→Long Mode
; ============================================================
; Loaded by stage1 at 0x7E00, runs in 16-bit real mode.
; Assembled into a flat binary (nasm -f bin) and padded to
; exactly 62 sectors (31,744 bytes) to match stage1's load count.
;
; Memory map after stage2 sets up:
;   0x0500–0x7BFF  stack (grows down from 0x7C00)
;   0x7E00–0xFFFF  stage2 code/data
;   0x8000–0x87FF  E820 memory map (up to 128 entries × 24 bytes)
;   0x9000         PML4 table         (4 KB, 512 × 8-byte entries)
;   0xA000         PDPT (identity)    (4 KB)
;   0xB000         PD   (identity)    (4 KB, 512 × 2MB pages → 1 GB)
;   0x100000       kernel ELF (loaded by stage2 if FAT/disk present;
;                  for QEMU -kernel this code is not used)
;
; Transition sequence:
;   1. A20 line enable (BIOS method + port 0x92 fallback)
;   2. E820 memory map via int 0x15/EAX=0xE820
;   3. Load GDT32 → enter 32-bit protected mode
;   4. Build PML4 identity-map page tables (0 → 512 GB with 2MB pages)
;   5. Enable PAE (CR4.PAE)
;   6. Load PML4 address into CR3
;   7. Set EFER.LME via MSR 0xC0000080
;   8. Enable paging (CR0.PG=1)
;   9. Long-jump to 64-bit code segment
;  10. Load GDT64, set segment registers, setup 64-bit stack
;  11. Call nexs_hal_init() and nexs_main_baremetal()
; ============================================================

BITS 16
ORG  0x7E00

;; ── CONSTANTS ────────────────────────────────────────────────
PML4_ADDR   equ 0x1000
PDPT_ADDR   equ 0x2000
PD_ADDR     equ 0x3000
E820_ADDR   equ 0x4000
KERNEL_STACK equ 0x200000    ; 2 MB virtual stack top (64-bit)

stage2_entry:
    cli
    mov  [boot_drive], dl
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti

    ; ── 0. Load kernel binary from disk (512 sectors total, read in 64-sector chunks) ──
    mov  word [dap_kernel + 2], 64     ; read 64 sectors at a time
    mov  word [dap_kernel + 4], 0x0000 ; dest offset
    mov  word [dap_kernel + 6], 0x2000 ; dest segment
    mov  dword [dap_kernel + 8], 63     ; LBA start
    mov  dword [dap_kernel + 12], 0    ; LBA high

.read_chunk_loop:
    mov  si, dap_kernel
    mov  ah, 0x42
    mov  dl, [boot_drive]
    int  0x13
    jnc  .chunk_ok

    ; Disk error fallback
    mov  si, msg_disk_err
.print_loop:
    lodsb
    test al, al
    jz   .hang
    mov  ah, 0x0E
    int  0x10
    jmp  .print_loop
.hang:
    hlt
    jmp  .hang

.chunk_ok:
    ; Advance LBA by 64
    add  dword [dap_kernel + 8], 64
    ; Advance segment by 0x0800 (64 sectors * 512 bytes = 32 KB = 0x8000 absolute, segment +0x800)
    add  word [dap_kernel + 6], 0x0800

    ; Check if we have read 512 sectors
    cmp  dword [dap_kernel + 8], 63 + 512
    jl   .read_chunk_loop

.disk_ok:
    ; ── 1. Enable A20 ─────────────────────────────────────────
    ; Try BIOS method first
    mov  ax, 0x2401
    int  0x15

    ; Fast A20 via port 0x92
    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE        ; don't trigger reset bit
    out  0x92, al

    ; ── 2. E820 memory map ────────────────────────────────────
    call collect_e820

    ; ── 3. Load 32-bit GDT + enter protected mode ─────────────
    cli
    lgdt [gdt32_ptr]

    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    jmp  dword 0x08:pm32_entry   ; far jump → flush pipeline, CS=0x08

;; ────────────────────────────────────────────────────────────
;; E820 collection
;; ────────────────────────────────────────────────────────────
collect_e820:
    push es
    mov  ax, 0
    mov  es, ax
    xor  ebx, ebx
    mov  di, E820_ADDR
    mov  dword [e820_count], 0

.e820_loop:
    mov  eax, 0xE820
    mov  ecx, 24
    mov  edx, 0x534D4150   ; 'SMAP'
    int  0x15
    jc   .e820_done
    cmp  eax, 0x534D4150
    jne  .e820_done

    ; Store entry at ES:DI
    add  di, 24
    inc  dword [e820_count]

    test ebx, ebx
    jz   .e820_done
    jmp  .e820_loop

.e820_done:
    pop  es
    ret

boot_drive: db 0
msg_disk_err: db "Stage2: kernel disk error", 0x0D, 0x0A, 0
dap_kernel:
    dw 0x0010      ; packet size
    dw 512         ; sectors to read (256 KB)
    dw 0x0000      ; dest offset
    dw 0x2000      ; dest segment (0x2000:0x0000 = absolute 0x20000)
    dd 63          ; LBA start
    dd 0           ; LBA high

;; ────────────────────────────────────────────────────────────
;; GDT32 — temporary flat 32-bit GDT
;; ────────────────────────────────────────────────────────────
align 8
gdt32:
    ; null
    dq 0
    ; kernel code 32-bit (CS=0x08): base=0, limit=4GB, exec/read, DPL=0
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF
    ; kernel data 32-bit (DS=0x10): base=0, limit=4GB, read/write, DPL=0
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF
gdt32_end:

gdt32_ptr:
    dw gdt32_end - gdt32 - 1
    dd gdt32

;; ────────────────────────────────────────────────────────────
;; 32-BIT PROTECTED MODE
;; ────────────────────────────────────────────────────────────
BITS 32
pm32_entry:
    ; Set data segments to 0x10
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x7C00

    ; ── 3b. Copy kernel from low buffer (0x20000) to 1MB (0x100000) ──
    mov  esi, 0x20000
    mov  edi, 0x100000
    mov  ecx, (512 * 512) / 4   ; 256 KB total buffer size in dwords
    rep  movsd

    ; ── 4. Build page tables (identity map 0 → 512 GB via 2MB pages) ──
    ; Clear tables
    mov  edi, PML4_ADDR
    mov  ecx, (3 * 4096) / 4   ; 3 tables × 4 KB each
    xor  eax, eax
    rep  stosd

    ; PML4[0] → PDPT
    mov  dword [PML4_ADDR],     (PDPT_ADDR | 0x03)
    mov  dword [PML4_ADDR + 4], 0

    ; PDPT[0] → PD
    mov  dword [PDPT_ADDR],     (PD_ADDR | 0x03)
    mov  dword [PDPT_ADDR + 4], 0

    ; PD: 512 entries × 2MB each = 1 GB identity mapped
    mov  edi, PD_ADDR
    mov  eax, 0x83             ; present + writable + PS (2MB page)
    mov  ecx, 512
.pd_loop:
    mov  dword [edi],     eax
    mov  dword [edi + 4], 0
    add  eax, 0x200000         ; next 2MB
    add  edi, 8
    loop .pd_loop

    ; ── 5. Enable PAE ─────────────────────────────────────────
    mov  eax, cr4
    or   eax, (1 << 5)         ; CR4.PAE
    mov  cr4, eax

    ; ── 6. Load PML4 into CR3 ─────────────────────────────────
    mov  eax, PML4_ADDR
    mov  cr3, eax

    ; ── 7. Set EFER.LME ───────────────────────────────────────
    mov  ecx, 0xC0000080       ; EFER MSR
    rdmsr
    or   eax, (1 << 8)         ; LME
    wrmsr

    ; ── 8. Enable paging (CR0.PG + WP) ───────────────────────
    mov  eax, cr0
    or   eax, 0x80010000       ; PG + WP
    mov  cr0, eax

    ; ── 9. Far jump to 64-bit code ────────────────────────────
    ; We need a 64-bit GDT loaded before the jump.
    ; GDT64 is at gdt64 (below), in lower memory.
    lgdt [gdt64_ptr]
    jmp  dword 0x08:lm64_entry

;; ────────────────────────────────────────────────────────────
;; GDT64 — permanent 64-bit GDT
;; ────────────────────────────────────────────────────────────
align 8
gdt64:
    ; null
    dq 0
    ; kernel code 64-bit (CS=0x08): L=1 D=0 P=1 S=1 Type=0xA
    dq 0x00AF9A000000FFFF
    ; kernel data 64-bit (DS=0x10): P=1 S=1 Type=0x2
    dq 0x00CF92000000FFFF
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64

;; ────────────────────────────────────────────────────────────
;; 64-BIT LONG MODE
;; ────────────────────────────────────────────────────────────
BITS 64
lm64_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Set up a proper 64-bit stack below the kernel
    mov  rsp, KERNEL_STACK

    ; Clear EFLAGS
    push 0
    popfq

    ; ── 10. Jump into C kernel ────────────────────────────────
    ; In raw hardware boot, we load the kernel ELF from disk and jump
    ; directly to its entry point (typically 0x100000 or defined in ELF).
    mov rax, 0x100000
    jmp rax

.hang64:
    hlt
    jmp  .hang64

;; ────────────────────────────────────────────────────────────
;; BSS/data
;; ────────────────────────────────────────────────────────────
BITS 32
e820_count:  dd 0

;; ── Pad stage2 to exactly 62 sectors (31744 bytes) ───────────
    times 62 * 512 - ($ - stage2_entry) db 0
