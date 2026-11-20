/* ============================================================
 * hal/amd64/ctx_amd64.S — x86-64 Context Switch
 * ============================================================
 */

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
    
    /* Save RSP (includes the return address pushed by the call) */
    movq  %rsp, 48(%rdi)
    
    /* Save RIP: read the return address directly from the top of the stack */
    movq  (%rsp), %rax
    movq  %rax, 56(%rdi)

    /* Restore callee-saved registers of next process from to_ctx */
    movq  0(%rsi),  %rbx
    movq  8(%rsi),  %rbp
    movq  16(%rsi), %r12
    movq  24(%rsi), %r13
    movq  32(%rsi), %r14
    movq  40(%rsi), %r15
    
    /* Restore RSP */
    movq  48(%rsi), %rsp
    
    /* Load the saved RIP into RAX */
    movq  56(%rsi), %rax
    
    /* CORREZIONE: Riallinea lo stack simulando il pop dell'indirizzo di ritorno.
       Questo ripristina %rsp al valore esatto precedente all'istruzione 'call'. */
    addq  $8, %rsp
    
    /* Esegue il salto alla destinazione */
    jmpq  *%rax