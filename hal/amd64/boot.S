/*
 * hal/amd64/boot.S — x86-64 Multiboot2 Bare-Metal Startup
 * ===========================================================
 * Provides a Multiboot2 header and a 32-bit protected-mode _start.
 * Assumes a multiboot2-compliant bootloader (GRUB, QEMU -kernel, etc.).
 *
 * Note: this entry point starts in 32-bit mode as required by Multiboot2.
 * Full 64-bit long mode setup would require additional GDT/paging code;
 * for simplicity this stays in 32-bit protected mode.
 */

/* =========================================================
   MULTIBOOT2 HEADER
   ========================================================= */

.set MULTIBOOT2_MAGIC,  0xe85250d6
.set MULTIBOOT2_ARCH,   0x00000000   /* i386 protected mode */
.set HEADER_LEN,        16
.set CHECKSUM,          -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + HEADER_LEN)

.section .multiboot
.align 8
multiboot_header:
    .long  MULTIBOOT2_MAGIC
    .long  MULTIBOOT2_ARCH
    .long  HEADER_LEN
    .long  CHECKSUM & 0xFFFFFFFF
    /* End tag */
    .short 0
    .short 0
    .long  8

/* =========================================================
   32-BIT PROTECTED MODE ENTRY
   ========================================================= */

.section .text
.global _start
.code32

_start:
    /* Set up stack */
    movl    $(_stack_top), %esp

    /* Clear BSS */
    movl    $(_bss_start), %edi
    movl    $(_bss_end),   %ecx
    subl    %edi, %ecx
    xorl    %eax, %eax
    rep stosl

    /* Call HAL init (UART 16550 setup) */
    call    nexs_hal_init

    /* Call NEXS baremetal entry point */
    call    nexs_main_baremetal

.Lhang:
    hlt
    jmp     .Lhang
