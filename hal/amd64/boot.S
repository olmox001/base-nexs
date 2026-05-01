/* ============================================================
 * hal/amd64/boot.S — NEXS Bootstrapper (Limine + PVH + Multiboot2)
 * ============================================================
 */

/* ── 0. Multiboot2 Header ─────────────────────────────────── */
.section .multiboot2, "a"
.balign 8
multiboot2_header_start:
    .long 0xe85250d6                /* magic */
    .long 0                         /* architecture: i386 (0) */
    .long multiboot2_header_end - multiboot2_header_start
    .long -(0xe85250d6 + 0 + (multiboot2_header_end - multiboot2_header_start))

    /* Tags can be added here if needed */

    /* End tag */
    .short 0, 0
    .long 8
multiboot2_header_end:

/* ── 1. Limine Requests (Per Limine ISO) ──────────────────── */
.section .limine_req, "aw"
.balign 8
limine_base_revision:
    .quad 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 3

limine_entry_point:
    .quad 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b   /* common magic */
    .quad 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a   /* request ID */
    .quad 0                                          /* revision */
    .quad 0                                          /* response (filled by Limine) */
    .quad _start64                                   /* entry function */

/* ── 2. PVH ELF Note (Per QEMU -kernel) ───────────────────── */
.section .note.Xen, "a", @note
.balign 4
pvh_note:
    .long 4                          /* Namesz */
    .long 4                          /* Descsz */
    .long 18                         /* Type (XEN_ELFNOTE_PHYS32_ENTRY) */
    .ascii "Xen\0"                   /* Name (4 bytes) */
    .long _strap_entry               /* Desc (4 bytes) */
    .long 0                          /* Padding */

/* ── Code Section ─────────────────────────────────────────── */
.section .text.boot, "ax"

/* =========================================================
   32-BIT ENTRY (QEMU PVH)
   ========================================================= */
.code32
.global _strap_entry
.global _start
_strap_entry:
_start:
    cli
    movl $(_stack_top), %esp
    
    /* Debug: Stampa 'P' su COM1 
    movw $0x3F8, %dx
    movb $'P', %al
    outb %al, %dx   */

    /* Costruisce le page tables identity a 0x1000 */
    movl $0x1000, %edi
    movl $3072, %ecx
    xorl %eax, %eax
    rep stosl

    movl $(0x2000 | 0x3), 0x1000
    movl $(0x3000 | 0x3), 0x2000

    movl $0x3000, %edi
    movl $0x83, %eax
    movl $512, %ecx
.fill_pd_32:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop .fill_pd_32

    /* PAE */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* CR3 = 0x1000 */
    movl $0x1000, %eax
    movl %eax, %cr3

    /* EFER.LME (Enable Long Mode) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* Enable Paging */
    movl %cr0, %eax
    orl $0x80010000, %eax
    movl %eax, %cr0

    /* Carica GDT a 32-bit e salta a 64-bit */
    lgdtl (gdt64_ptr32)
    ljmpl $0x08, $_start64_common

/* =========================================================
   64-BIT ENTRY (Limine ISO)
   ========================================================= */
.code64
.global _start64
_start64:
    cli
    movq $_stack_top, %rsp
    andq $~0xF, %rsp

    /* Debug: Stampa 'L' su COM1 */
    movw $0x3F8, %dx
    movb $'L', %al
    outb %al, %dx

    /* Ricostruisce le page tables identity a 0x1000 */
    movl $0x1000, %edi
    movl $3072, %ecx
    xorl %eax, %eax
    rep stosl

    movl $(0x2000 | 0x3), 0x1000
    movl $(0x3000 | 0x3), 0x2000

    movl $0x3000, %edi
    movl $0x83, %eax
    movl $512, %ecx
.fill_pd_64:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop .fill_pd_64

    /* Swap CR3 to our custom page tables */
    movq $0x1000, %rax
    movq %rax, %cr3

    /* Carica GDT a 64-bit e ricarica CS */
    lgdt (gdt64_ptr64)
    pushq $0x08
    lea .Lreload_cs(%rip), %rax
    pushq %rax
    lretq
.Lreload_cs:
    jmp _start64_common

/* =========================================================
   COMMON 64-BIT PATH
   ========================================================= */
_start64_common:
    /* Ricarica segmenti dati */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    /* Setup stack per C */
    movq $_stack_top, %rsp
    andq $~0xF, %rsp

    /* Enable SSE */
    movq %cr0, %rax
    andw $0xFFFB, %ax
    orw  $0x0002, %ax
    movq %rax, %cr0

    movq %cr4, %rax
    orl  $0x00000600, %eax
    movq %rax, %cr4

    /* Pulisce BSS */
    movl $(_bss_start), %edi
    movl $(_bss_end), %ecx
    subl %edi, %ecx
    shrl $2, %ecx
    xorl %eax, %eax
    rep stosl

    /* Pulisce flags */
    pushq $0
    popfq

    /* Cede il controllo al kernel in C */
    call nexs_hal_init
    call nexs_main_baremetal
.Lhang:
    hlt
    jmp .Lhang

/* ── GDT e Pointers ───────────────────────────────────────── */
.balign 8
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF   /* Code */
    .quad 0x00CF92000000FFFF   /* Data */
gdt64_end:

/* LGDT in 32-bit legge 6 bytes */
.balign 8
gdt64_ptr32:
    .short (gdt64_end - gdt64 - 1)
    .long gdt64

/* LGDT in 64-bit legge 10 bytes */
.balign 8
gdt64_ptr64:
    .short (gdt64_end - gdt64 - 1)
    .quad gdt64