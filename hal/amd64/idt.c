/*
 * hal/amd64/idt.c — x86-64 IDT (256 entries)
 * ==============================================
 * Installs stubs from isr_stubs.S, then calls nexs_isr_dispatch()
 * for every interrupt/exception.
 *
 * Exception names and a /hal/idt/ registry entry per installed handler.
 */

#include "../include/nexs_idt.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>
#include <string.h>

/* ── IDT gate descriptor (16 bytes) ──────────────────────── */
typedef struct {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;        /* bits[2:0] = IST index (0 = use RSP0) */
    uint8_t  type_attr;  /* P | DPL | 0 | type; type=0xE=interrupt gate */
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed)) IdtGate;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) Idtr;

/* ── Storage ──────────────────────────────────────────────── */
static IdtGate s_idt[256];
static Idtr    s_idtr;

/* Defined in isr_stubs.S — array of 256 function pointers */
extern void (*isr_stub_table[256])(void);

/* Exception names (vectors 0-31) */
static const char * const exc_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND Range", "Invalid Opcode", "No Math Co",
    "Double Fault", "Co Seg Overrun", "Invalid TSS", "Seg Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FP", "Alignment Check", "Machine Check", "SIMD FP",
    "Virt Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Inj", "VMM Comm", "Security Exc", "Reserved"
};

void idt_set_handler(uint8_t vec, void *fn, uint8_t dpl) {
    uint64_t addr = (uint64_t)fn;
    IdtGate *g = &s_idt[vec];
    g->offset_lo  = (uint16_t)(addr & 0xFFFF);
    g->selector   = GDT_KERN_CODE;
    g->ist        = 0;
    g->type_attr  = (uint8_t)(0x8E | ((dpl & 3) << 5)); /* P=1,type=E */
    g->offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    g->offset_hi  = (uint32_t)(addr >> 32);
    g->reserved   = 0;
}

void idt_init(void) {
    memset(s_idt, 0, sizeof(s_idt));

    for (int i = 0; i < 256; i++)
        idt_set_handler((uint8_t)i, (void *)isr_stub_table[i], 0);

    /* Vector 0x80: syscall gate (DPL=3 so user mode can call int 0x80) */
    idt_set_handler(0x80, (void *)isr_stub_table[0x80], 3);

    s_idtr.limit = (uint16_t)(sizeof(s_idt) - 1);
    s_idtr.base  = (uint64_t)s_idt;
    __asm__ volatile("lidt %0" :: "m"(s_idtr) : "memory");

    /* Publish exception names to registry */
    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/hal/idt/exc/%d", i);
        reg_set(path, val_str(exc_names[i]), RK_READ);
    }
    reg_set("/hal/idt/status", val_str("ok"), RK_READ);
}

/* ── ISR dispatch ─────────────────────────────────────────── */

/* Handlers registered by other subsystems */
static void (*s_handlers[256])(IsrFrame *) = {0};

void nexs_isr_register(uint8_t vec, void (*fn)(IsrFrame *)) {
    s_handlers[vec] = fn;
}

void nexs_isr_dispatch(IsrFrame *f) {
    uint8_t vec = (uint8_t)f->vec;

    if (s_handlers[vec]) {
        s_handlers[vec](f);
        return;
    }

    /* Unhandled exception: print info and halt */
    if (vec < 32) {
        /* Use HAL print — UART is always available */
        extern void nexs_hal_print(const char *);
        nexs_hal_print("\r\n*** CPU EXCEPTION #");
        char tmp[8];
        tmp[0] = (char)('0' + vec / 10);
        tmp[1] = (char)('0' + vec % 10);
        tmp[2] = ' ';
        tmp[3] = '\0';
        nexs_hal_print(tmp);
        if (vec < 32) nexs_hal_print(exc_names[vec]);
        nexs_hal_print(" ***\r\n");
    }
    /* For IRQs (vec>=32) with no handler: spurious, just return */
}
