/*
 * hal/arm64/nexs.ld — AArch64 Bare-Metal Linker Script
 * =======================================================
 * Load address 0x40000000 (QEMU virt DRAM base).
 */

ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text : {
        *(.text.boot)   /* boot.S _start must come first */
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    /* Match the existing AMD64 512 KiB bootstrap reservation. The earlier
     * 16 KiB ARM64 stack cannot safely accommodate nested interpreter
     * evaluation and formatting frames. Per-task guarded stacks and peak
     * stack qualification remain separate acceptance requirements. */
    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x80000;
    _stack_top = .;
}
