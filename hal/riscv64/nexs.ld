/*
 * hal/riscv64/nexs.ld — RISC-V 64-bit Bare-Metal Linker Script
 * Load address 0x80000000 (QEMU virt machine RAM base).
 */

ENTRY(_start)

SECTIONS {
    . = 0x80000000;

    .text : {
        *(.text.boot)
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

    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x400000;       /* 4 MB stack */
    _stack_top = .;
}
