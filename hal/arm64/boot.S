/* ============================================================
 * hal/arm64/boot.S — AArch64 Bare-Metal Startup
 * ============================================================
 * Handles EL3/EL2 → EL1 transition, sets up exception vectors,
 * clears BSS, calls MMU init, then launches the NEXS runtime.
 *
 * Targets QEMU 'virt' machine and real AArch64 boards.
 * ============================================================ */

.section .text.boot
.global _start

_start:
    /* ── Only core 0 continues; all others spin ─────────────── */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbnz    x0, .Lspin

    /* ── Determine current EL and drop to EL1 ───────────────── */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #3
    beq     .Lfrom_el3
    cmp     x0, #2
    beq     .Lfrom_el2
    b       .Lel1_entry         /* already EL1 */

/* ── EL3 → EL2 → EL1 ─────────────────────────────────────── */
.Lfrom_el3:
    /* Route exceptions to EL1, enable AArch64 at EL2 */
    mov     x0, #(1 << 10) | (1 << 0)  /* RW=1 (EL2 is AArch64), NS=1 */
    msr     scr_el3, x0
    mov     x0, #0x3C9              /* EL2h, all interrupts masked */
    msr     spsr_el3, x0
    adr     x0, .Lfrom_el2
    msr     elr_el3, x0
    eret

.Lfrom_el2:
    /* Enable AArch64 at EL1, disable VHE */
    mov     x0, #(1 << 31)          /* RW=1 — EL1 is AArch64 */
    msr     hcr_el2, x0
    /* Set up EL1 SCTLR (disabled MMU for now; we enable it in mmu.c) */
    mov     x0, #0x0800             /* little-endian, no MMU, no cache */
    msr     sctlr_el1, x0
    /* Enter EL1 */
    mov     x0, #0x3C5              /* EL1h, all interrupts masked */
    msr     spsr_el2, x0
    adr     x0, .Lel1_entry
    msr     elr_el2, x0
    eret

/* ── EL1 initialisation ───────────────────────────────────── */
.Lel1_entry:
    /* Install exception vectors */
    adr     x0, nexs_vectors
    msr     vbar_el1, x0

    /* Stack pointer for EL1 */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Clear BSS */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
    mov     x2, xzr
.Lbss_loop:
    cmp     x0, x1
    bge     .Lbss_done
    str     x2, [x0], #8
    b       .Lbss_loop
.Lbss_done:

    /* MMU + cache init (identity map) */
    bl      nexs_mmu_init

    /* HAL init (UART, etc.) */
    bl      nexs_hal_init

    /* NEXS baremetal entry */
    bl      nexs_main_baremetal

.Lspin:
    wfe
    b       .Lspin
