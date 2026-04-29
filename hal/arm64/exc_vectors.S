/* ============================================================
 * hal/arm64/exc_vectors.S — AArch64 Exception Vector Table
 * ============================================================
 * AArch64 requires the vector table to be 2KB-aligned.
 * Each slot is 128 bytes (32 instructions max).
 *
 * Layout (4 groups × 4 entries):
 *   Group 0: Current EL, SP0
 *   Group 1: Current EL, SPx
 *   Group 2: Lower EL, AArch64
 *   Group 3: Lower EL, AArch32
 * ============================================================ */

.extern nexs_exc_handler

/* Save all GP regs + ELR + SPSR to the stack */
.macro SAVE_REGS
    sub     sp, sp, #(34 * 8)
    stp     x0,  x1,  [sp, #(0  * 8)]
    stp     x2,  x3,  [sp, #(2  * 8)]
    stp     x4,  x5,  [sp, #(4  * 8)]
    stp     x6,  x7,  [sp, #(6  * 8)]
    stp     x8,  x9,  [sp, #(8  * 8)]
    stp     x10, x11, [sp, #(10 * 8)]
    stp     x12, x13, [sp, #(12 * 8)]
    stp     x14, x15, [sp, #(14 * 8)]
    stp     x16, x17, [sp, #(16 * 8)]
    stp     x18, x19, [sp, #(18 * 8)]
    stp     x20, x21, [sp, #(20 * 8)]
    stp     x22, x23, [sp, #(22 * 8)]
    stp     x24, x25, [sp, #(24 * 8)]
    stp     x26, x27, [sp, #(26 * 8)]
    stp     x28, x29, [sp, #(28 * 8)]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x30, x0,  [sp, #(30 * 8)]
    str     x1,       [sp, #(32 * 8)]
.endm

.macro RESTORE_REGS
    ldr     x1,       [sp, #(32 * 8)]
    ldp     x30, x0,  [sp, #(30 * 8)]
    msr     elr_el1,  x0
    msr     spsr_el1, x1
    ldp     x28, x29, [sp, #(28 * 8)]
    ldp     x26, x27, [sp, #(26 * 8)]
    ldp     x24, x25, [sp, #(24 * 8)]
    ldp     x22, x23, [sp, #(22 * 8)]
    ldp     x20, x21, [sp, #(20 * 8)]
    ldp     x18, x19, [sp, #(18 * 8)]
    ldp     x16, x17, [sp, #(16 * 8)]
    ldp     x14, x15, [sp, #(14 * 8)]
    ldp     x12, x13, [sp, #(12 * 8)]
    ldp     x10, x11, [sp, #(10 * 8)]
    ldp     x8,  x9,  [sp, #(8  * 8)]
    ldp     x6,  x7,  [sp, #(6  * 8)]
    ldp     x4,  x5,  [sp, #(4  * 8)]
    ldp     x2,  x3,  [sp, #(2  * 8)]
    ldp     x0,  x1,  [sp, #(0  * 8)]
    add     sp, sp, #(34 * 8)
.endm

/* Generic entry: save regs, call handler(kind), restore, eret */
.macro ENTRY_HANDLER kind
    SAVE_REGS
    mov     x0, #\kind
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      nexs_exc_handler
    RESTORE_REGS
    eret
.endm

/* ── Vector table — must be 2KB (0x800) aligned ─────────── */
.section .text.vectors
.global nexs_vectors
.balign 0x800
nexs_vectors:

/* ── Group 0: Current EL, SP_EL0 ─────────────────────────── */
.balign 0x80
    ENTRY_HANDLER 0x00      /* Synchronous */
.balign 0x80
    ENTRY_HANDLER 0x01      /* IRQ */
.balign 0x80
    ENTRY_HANDLER 0x02      /* FIQ */
.balign 0x80
    ENTRY_HANDLER 0x03      /* SError */

/* ── Group 1: Current EL, SP_ELx ─────────────────────────── */
.balign 0x80
    ENTRY_HANDLER 0x10      /* Synchronous */
.balign 0x80
    ENTRY_HANDLER 0x11      /* IRQ */
.balign 0x80
    ENTRY_HANDLER 0x12      /* FIQ */
.balign 0x80
    ENTRY_HANDLER 0x13      /* SError */

/* ── Group 2: Lower EL, AArch64 ──────────────────────────── */
.balign 0x80
    ENTRY_HANDLER 0x20
.balign 0x80
    ENTRY_HANDLER 0x21
.balign 0x80
    ENTRY_HANDLER 0x22
.balign 0x80
    ENTRY_HANDLER 0x23

/* ── Group 3: Lower EL, AArch32 ──────────────────────────── */
.balign 0x80
    ENTRY_HANDLER 0x30
.balign 0x80
    ENTRY_HANDLER 0x31
.balign 0x80
    ENTRY_HANDLER 0x32
.balign 0x80
    ENTRY_HANDLER 0x33
