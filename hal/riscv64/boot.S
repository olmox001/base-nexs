.section .text.boot
.global _start

_start:
    /* Only hart 0 continues; others spin */
    csrr    a0, mhartid
    bnez    a0, .Lspin

    /* Set up stack pointer from linker-defined symbol */
    la      sp, _stack_top

    /* Clear BSS */
    la      a0, _bss_start
    la      a1, _bss_end
.Lbss_loop:
    bgeu    a0, a1, .Lbss_done
    sd      zero, 0(a0)
    addi    a0, a0, 8
    j       .Lbss_loop
.Lbss_done:

    /* Call into NEXS baremetal entry */
    call    nexs_main_baremetal

.Lspin:
    wfi
    j       .Lspin
