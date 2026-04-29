/*
 * hal/amd64/apic.c — LAPIC + IOAPIC + APIC timer
 * =================================================
 * STEP 02: APIC init + timer IRQ (vector 0x20, 1 ms tick)
 *
 * LAPIC base: detected via CPUID/MSR IA32_APIC_BASE (0x1B).
 * IOAPIC base: 0xFEC00000 (standard; overridden by ACPI MADT).
 * Timer vector: 0x20. LAPIC spurious: 0xFF.
 */

#include "../include/nexs_idt.h"
#include "../include/nexs_timer.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>

/* ── LAPIC MMIO registers (offsets from lapic_base) ──────── */
#define LAPIC_ID        0x020
#define LAPIC_VERSION   0x030
#define LAPIC_TPR       0x080   /* task priority */
#define LAPIC_EOI       0x0B0
#define LAPIC_SPURIOUS  0x0F0
#define LAPIC_ICR_LO    0x300
#define LAPIC_ICR_HI    0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_ICR 0x380   /* initial count */
#define LAPIC_TIMER_CCR 0x390   /* current count */
#define LAPIC_TIMER_DCR 0x3E0   /* divide config */

#define LAPIC_SPURIOUS_ENABLE  (1U << 8)
#define LAPIC_TIMER_PERIODIC   (1U << 17)
#define LAPIC_TIMER_VECTOR     0x20

/* ── IOAPIC registers ─────────────────────────────────────── */
#define IOAPIC_BASE_DEFAULT  0xFEC00000UL
#define IOAPIC_REGSEL        0x00
#define IOAPIC_REGWIN        0x10
#define IOAPIC_VER           0x01
#define IOAPIC_REDTBL(n)     (0x10 + (n) * 2)

/* ── State ────────────────────────────────────────────────── */
static volatile uint32_t *s_lapic  = NULL;
static volatile uint32_t *s_ioapic = (volatile uint32_t *)IOAPIC_BASE_DEFAULT;
static uint64_t           s_ticks  = 0;
static TimerCallback      s_tick_cb = NULL;

/* ── LAPIC MMIO helpers ───────────────────────────────────── */
static inline uint32_t lapic_read(uint32_t off) {
    return *(volatile uint32_t *)((uint8_t *)s_lapic + off);
}
static inline void lapic_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)((uint8_t *)s_lapic + off) = val;
}

/* ── IOAPIC helpers ───────────────────────────────────────── */
static inline uint32_t ioapic_read(uint8_t reg) {
    s_ioapic[IOAPIC_REGSEL / 4] = reg;
    return s_ioapic[IOAPIC_REGWIN / 4];
}
static inline void ioapic_write(uint8_t reg, uint32_t val) {
    s_ioapic[IOAPIC_REGSEL / 4] = reg;
    s_ioapic[IOAPIC_REGWIN / 4] = val;
}

/* Route IOAPIC IRQ → LAPIC vector.
 * dest_apic_id=0 → CPU 0. Delivery=fixed. Active low, edge. */
static void ioapic_route(uint8_t irq, uint8_t vec, uint8_t apic_id) {
    uint32_t lo = (uint32_t)vec;            /* fixed delivery, edge, active high */
    uint32_t hi = ((uint32_t)apic_id) << 24;
    ioapic_write((uint8_t)(IOAPIC_REDTBL(irq)),     lo);
    ioapic_write((uint8_t)(IOAPIC_REDTBL(irq) + 1), hi);
}

/* ── Disable legacy PIC (8259) ────────────────────────────── */
static void pic_disable(void) {
    /* Mask all IRQs on both PICs */
    __asm__ volatile(
        "outb %%al, $0x21\n\t"
        "outb %%al, $0xA1\n\t"
        :
        : "a"((uint8_t)0xFF)
    );
}

/* ── Calibrate LAPIC timer against PIT channel 2 ─────────── */
#define PIT_HZ        1193182UL
#define CALIB_MS      10

static uint32_t lapic_calibrate_ticks_per_ms(void) {
    /* Use PIT channel 2 in one-shot mode for ~10 ms */
    uint32_t count = (uint32_t)(PIT_HZ * CALIB_MS / 1000);

    /* Set PIT ch2 gate high, speaker off */
    uint8_t port61;
    __asm__ volatile("inb $0x61, %0" : "=a"(port61));
    port61 = (uint8_t)((port61 & ~0x02) | 0x01);
    __asm__ volatile("outb %0, $0x61" :: "a"(port61));

    /* Mode 0 (one-shot) on channel 2 */
    __asm__ volatile("outb %0, $0x43" :: "a"((uint8_t)0xB0));
    __asm__ volatile("outb %0, $0x42" :: "a"((uint8_t)(count & 0xFF)));
    __asm__ volatile("outb %0, $0x42" :: "a"((uint8_t)(count >> 8)));

    /* Start LAPIC timer with large initial count, divide-by-16 */
    lapic_write(LAPIC_TIMER_DCR, 0x3);   /* divide by 16 */
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    /* Wait for PIT to expire (OUT2 goes high) */
    uint8_t p;
    do { __asm__ volatile("inb $0x61, %0" : "=a"(p)); } while (!(p & 0x20));

    uint32_t remain = lapic_read(LAPIC_TIMER_CCR);
    lapic_write(LAPIC_TIMER_ICR, 0); /* stop */

    uint32_t elapsed = 0xFFFFFFFF - remain;
    return elapsed / CALIB_MS; /* ticks per millisecond */
}

/* ── Timer ISR ────────────────────────────────────────────── */
static void apic_timer_isr(IsrFrame *f) {
    (void)f;
    s_ticks++;
    if (s_tick_cb) s_tick_cb();
    lapic_write(LAPIC_EOI, 0);
}

/* ── Public API ───────────────────────────────────────────── */

uint64_t hal_timer_ticks(void) { return s_ticks; }

void hal_timer_sleep_ms(uint32_t ms) {
    uint64_t target = s_ticks + ms;
    while (s_ticks < target) __asm__ volatile("pause");
}

void hal_timer_set_hz(uint32_t hz) {
    /* Re-programs the LAPIC timer — requires apic_init() called first */
    if (!s_lapic) return;
    uint32_t tpm = lapic_calibrate_ticks_per_ms();
    lapic_write(LAPIC_TIMER_ICR, tpm * 1000 / hz);
}

void apic_eoi(void) {
    if (s_lapic) lapic_write(LAPIC_EOI, 0);
}

/* Set IOAPIC base (called by ACPI parser with real address) */
void apic_set_ioapic_base(uint64_t base) {
    s_ioapic = (volatile uint32_t *)base;
}

void apic_init(void) {
    /* Read LAPIC base from IA32_APIC_BASE MSR */
    uint32_t lo, hi;
    __asm__ volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(0x1B)   /* IA32_APIC_BASE */
    );
    uint64_t lapic_phys = ((uint64_t)hi << 32) | (lo & ~0xFFFULL);
    s_lapic = (volatile uint32_t *)lapic_phys;

    /* Disable legacy PIC */
    pic_disable();

    /* Enable LAPIC (set spurious vector + enable bit) */
    lapic_write(LAPIC_SPURIOUS, 0xFF | LAPIC_SPURIOUS_ENABLE);
    lapic_write(LAPIC_TPR, 0);

    /* Route IOAPIC IRQ0 (PIT legacy) → vector 0x20, but we use LAPIC timer */
    /* So we mask IRQ0 on IOAPIC */
    ioapic_write((uint8_t)IOAPIC_REDTBL(0), (1U << 16));  /* masked */

    /* Register timer ISR */
    extern void nexs_isr_register(uint8_t, void (*)(IsrFrame *));
    nexs_isr_register(LAPIC_TIMER_VECTOR, apic_timer_isr);

    /* Calibrate and configure periodic LAPIC timer at 1000 Hz (1 ms) */
    uint32_t tpm = lapic_calibrate_ticks_per_ms();
    lapic_write(LAPIC_TIMER_DCR, 0x3);    /* divide by 16 */
    lapic_write(LAPIC_LVT_TIMER,
        LAPIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, tpm);    /* 1 ms per tick */

    /* Enable interrupts */
    __asm__ volatile("sti");

    /* Publish to registry */
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)lapic_phys);
    reg_set("/hal/apic/lapic_base", val_str(buf), RK_READ);
    reg_set("/hal/apic/timer_hz",   val_int(1000), RK_READ);
    reg_set("/hal/apic/status",     val_str("ok"), RK_READ);
}

void hal_timer_init(TimerCallback cb) {
    s_tick_cb = cb;
    apic_init();
}
