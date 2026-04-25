/*
 * hal/arm64/boot.S — AArch64 Bare-Metal Startup
 * =================================================
 * Minimal startup code for QEMU virt machine (or real AArch64 hardware).
 *
 * Execution flow:
 *   _start → set up stack → clear BSS → nexs_hal_init → nexs_main_baremetal
 *
 * Stack is placed immediately after the BSS section (see nexs.ld).
 */

.section .text.boot
.global _start

_start:
    /* Set up stack pointer */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Clear BSS section */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
    mov     x2, #0
.Lbss_loop:
    cmp     x0, x1
    beq     .Lbss_done
    str     x2, [x0], #8
    b       .Lbss_loop
.Lbss_done:

    /* Call HAL init (UART setup, etc.) */
    bl      nexs_hal_init

    /* Call the NEXS baremetal entry point (defined in runtime/main.c) */
    bl      nexs_main_baremetal

    /* Should never return; spin forever if it does */
.Lhang:
    wfe
    b       .Lhang
