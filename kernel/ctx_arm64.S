/* ============================================================
 * kernel/ctx_arm64.S — AArch64 Context Switch
 * ============================================================
 * void ctx_switch(void *from_ctx, void *to_ctx)
 *   x0 = from_ctx (Ctx_arm64 *)
 *   x1 = to_ctx   (Ctx_arm64 *)
 *
 * Ctx_arm64 layout (offsets in bytes):
 *   0   x19
 *   8   x20
 *   16  x21
 *   24  x22
 *   32  x23
 *   40  x24
 *   48  x25
 *   56  x26
 *   64  x27
 *   72  x28
 *   80  x29 (frame pointer)
 *   88  x30 (link register / return address)
 *   96  sp
 * ============================================================ */

.section .text
.global ctx_switch
ctx_switch:
    /* Save callee-saved registers into from_ctx (x0) */
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]
    mov  x2, sp
    str  x2,       [x0, #96]

    /* Restore callee-saved registers from to_ctx (x1) */
    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #80]
    ldr  x2,       [x1, #96]
    mov  sp, x2

    /* Return into next process (x30 = saved LR = return address) */
    ret
