/*
 * hal/amd64/nexs.ld — x86-64 Multiboot2/Limine Linker Script
 * ======================================================
 */

OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_strap_entry)

SECTIONS {
    /* Mappiamo il kernel a 1MB per compatibilità Multiboot */
    . = 1M;

    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    .limine_req : {
        KEEP(*(.limine_req))
    }

    .strap : {
        KEEP(*(.strap))
    }

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : {
        . = ALIGN(16);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    /* Stack statico (aumentato a 512KB per ricorsione profonda) */
    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x80000;
    _stack_top = .;
}
