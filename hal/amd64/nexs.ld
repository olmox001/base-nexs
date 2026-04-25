/*
 * hal/amd64/nexs.ld — x86-64 Multiboot2 Linker Script
 * ======================================================
 * Load address 1 MB (0x100000) — standard for Multiboot kernels.
 */

ENTRY(_start)

SECTIONS {
    . = 1M;

    .trampoline : {
        *(.trampoline)
    }

    .multiboot : {
        *(.multiboot)
    }

    .note.pvh : {
        *(.note.pvh)
    }

    .text : {
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

    /* Stack area */
    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x4000;       /* 16 KB stack */
    _stack_top = .;
}
