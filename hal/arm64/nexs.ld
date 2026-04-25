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

    /* Align to page boundary, then allocate stack */
    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x4000;        /* 16 KB stack */
    _stack_top = .;
}
