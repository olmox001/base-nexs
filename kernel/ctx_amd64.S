/* ============================================================
 * kernel/ctx_amd64.S — x86-64 Context Switch
 * ============================================================
 * void ctx_switch(void *from_ctx, void *to_ctx)
 *   from_ctx (rdi) = pointer to Ctx_amd64 of current process
 *   to_ctx   (rsi) = pointer to Ctx_amd64 of next process
 *
 * Ctx_amd64 layout (offsets in bytes):
 *   0   rbx
 *   8   rbp
 *   16  r12
 *   24  r13
 *   32  r14
 *   40  r15
 *   48  rsp
 *   56  rip  (saved via push/pop trick)
 * ============================================================ */

.section .text
.global ctx_switch
ctx_switch:
    /* Save callee-saved registers of current process into from_ctx */
    movq  %rbx,  0(%rdi)
    movq  %rbp,  8(%rdi)
    movq  %r12, 16(%rdi)
    movq  %r13, 24(%rdi)
    movq  %r14, 32(%rdi)
    movq  %r15, 40(%rdi)
    /* Save RSP (points to return address pushed by caller) */
    movq  %rsp, 48(%rdi)
    /* Save RIP: read return address from stack */
    movq  (%rsp), %rax
    movq  %rax, 56(%rdi)

    /* Restore callee-saved registers of next process from to_ctx */
    movq  0(%rsi),  %rbx
    movq  8(%rsi),  %rbp
    movq  16(%rsi), %r12
    movq  24(%rsi), %r13
    movq  32(%rsi), %r14
    movq  40(%rsi), %r15
    movq  48(%rsi), %rsp
    /* Jump to saved RIP (simulates returning from ctx_switch in next context) */
    movq  56(%rsi), %rax
    jmpq  *%rax
