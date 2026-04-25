/* ============================================================
 * hal/amd64/boot.S — x86-64 Multiboot2 Entry + Long Mode Transition
 * ============================================================
 * Provides a valid Multiboot2 header and a 32-bit protected-mode
 * _start (as required by the Multiboot2 spec).
 * Immediately transitions to 64-bit long mode before calling C code.
 *
 * Used for:
 *   - QEMU:    qemu-system-x86_64 -kernel nexs.elf
 *   - SeaBIOS: any Multiboot2-compliant loader
 *
 * For raw disk boot (real hardware) use hal/amd64/boot/stage1+stage2.
 *
 * Memory after setup:
 *   PML4 at 0x1000 (4 KB)
 *   PDPT at 0x2000 (4 KB)
 *   PD   at 0x3000 (4 KB)  → 512 × 2MB pages = 1 GB identity map
 *   Stack at _stack_top (defined in nexs.ld)
 * ============================================================ */

.set MULTIBOOT2_MAGIC,  0xe85250d6
.set MULTIBOOT2_ARCH,   0x00000000
.set HEADER_LEN,        16
.set CHECKSUM,          (-(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + HEADER_LEN))

.set MULTIBOOT1_MAGIC,  0x1BADB002
.set MULTIBOOT1_FLAGS,  0x00000003 /* ALIGN + MEMINFO */
.set MULTIBOOT1_CHECKSUM, -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

/* ── Multiboot1 & Multiboot2 Headers ──────────────────────── */
.section .multiboot
.align 4
multiboot1_header:
    .long  MULTIBOOT1_MAGIC
    .long  MULTIBOOT1_FLAGS
    .long  (MULTIBOOT1_CHECKSUM & 0xFFFFFFFF)
.align 8
multiboot_header:
    .long  MULTIBOOT2_MAGIC
    .long  MULTIBOOT2_ARCH
    .long  HEADER_LEN
    .long  (CHECKSUM & 0xFFFFFFFF)
    .short 0          /* end tag type */
    .short 0
    .long  8

/* ── PVH ELF Note for QEMU -kernel (64-bit ELF) ───────────── */
.section .note.pvh, "a", @note
.align 4
    .long 4               /* namesz */
    .long 4               /* descsz */
    .long 18              /* type: XEN_ELFNOTE_PHYS32_ENTRY */
    .asciz "Xen"          /* name */
    .long _start          /* desc */

/* ── 32-bit Protected Mode Entry ──────────────────────────── */
.section .text
.global _start
.code32

_start:
    cli
    movl  $(_stack_top), %esp

    /* Clear BSS */
    movl  $(_bss_start), %edi
    movl  $(_bss_end),   %ecx
    subl  %edi, %ecx
    shrl  $2, %ecx
    xorl  %eax, %eax
    rep stosl

    /* ── Build identity-map page tables at fixed physical addresses ──
     * PML4 @ 0x1000, PDPT @ 0x2000, PD @ 0x3000
     * Maps 0 → 1 GB using 512 × 2MB huge pages. */

    /* Zero all three tables (3 × 4096 bytes) */
    movl  $0x1000,    %edi
    movl  $3072, %ecx
    xorl  %eax, %eax
    rep stosl

    /* PML4[0] → PDPT @ 0x2000 */
    movl  $(0x2000 | 0x3), 0x1000      /* present + writable */
    movl  $0,              0x1004

    /* PDPT[0] → PD @ 0x3000 */
    movl  $(0x3000 | 0x3), 0x2000
    movl  $0,              0x2004

    /* PD: 512 entries × 2MB huge pages starting at phys 0 */
    movl  $0x3000, %edi
    movl  $0x83,   %eax      /* present + writable + PS (huge) */
    movl  $512,    %ecx
.fill_pd:
    movl  %eax, (%edi)
    movl  $0,  4(%edi)
    addl  $0x200000, %eax    /* +2 MB */
    addl  $8,  %edi
    loop  .fill_pd

    /* ── Enable PAE (CR4.PAE) ─────────────────────────────── */
    movl  %cr4,  %eax
    orl   $(1<<5), %eax
    movl  %eax,  %cr4

    /* ── Load PML4 into CR3 ──────────────────────────────── */
    movl  $0x1000, %eax
    movl  %eax,   %cr3

    /* ── Set EFER.LME ────────────────────────────────────── */
    movl  $0xC0000080, %ecx
    rdmsr
    orl   $(1<<8), %eax
    wrmsr

    /* ── Enable Paging (CR0.PG | CR0.WP) ────────────────── */
    movl  %cr0, %eax
    orl   $0x80010000, %eax
    movl  %eax, %cr0

    /* ── Load 64-bit GDT ─────────────────────────────────── */
    lgdtl (gdt64_ptr)

    /* ── Far jump to 64-bit CS=0x08 ─────────────────────── */
    ljmpl $0x08, $_start64

/* ── GDT64 (must be in .text/.data, addressable in 32-bit) ── */
.align 8
gdt64:
    .quad 0                          /* null */
    .quad 0x00AF9A000000FFFF         /* kernel code 64-bit (CS=0x08) */
    .quad 0x00CF92000000FFFF         /* kernel data      (DS=0x10) */
gdt64_end:

gdt64_ptr:
    .short (gdt64_end - gdt64 - 1)
    .long  gdt64

/* ── 64-bit Long Mode ────────────────────────────────────── */
.code64
_start64:
    movw  $0x10, %ax
    movw  %ax,  %ds
    movw  %ax,  %es
    movw  %ax,  %fs
    movw  %ax,  %gs
    movw  %ax,  %ss

    /* Enable SSE */
    movq  %cr0, %rax
    andw  $0xFFFB, %ax  /* clear CR0.EM */
    orw   $0x0002, %ax  /* set CR0.MP */
    movq  %rax, %cr0
    movq  %cr4, %rax
    orl   $0x00000600, %eax /* set CR4.OSFXSR and CR4.OSXMMEXCPT */
    movq  %rax, %cr4

    /* Reload stack pointer */
    movq  $(_stack_top), %rsp
    andq  $~0xF,        %rsp

    /* Clear RFLAGS */
    pushq $0
    popfq

    /* Call C HAL init (uart.c) then NEXS bare-metal entry */
    call  nexs_hal_init
    call  nexs_main_baremetal

.Lhang:
    hlt
    jmp   .Lhang

/* ── Stack in BSS ────────────────────────────────────────── */
.section .bss
.align 16
.global _bss_start
_bss_start:

.section .bss
.align 4096
_stack_mem:
    .space 0x8000          /* 32 KB stack */
.global _stack_top
_stack_top:
