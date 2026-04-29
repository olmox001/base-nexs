/*
 * hal/arm64/timer.c — ARM Generic Timer (EL1 virtual timer)
 * ===========================================================
 * STEP 05: ARM generic timer init + 1ms periodic tick.
 *
 * Uses CNTV_CTL_EL0 (virtual timer control) and CNTV_TVAL_EL0
 * (countdown value). IRQ 27 = EL1 virtual timer PPI (QEMU virt).
 */

#include "../include/nexs_timer.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>

/* ── Virtual timer system registers ──────────────────────── */
static inline uint64_t read_cntfrq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}
static inline uint64_t read_cntvct(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
static inline void write_cntv_tval(uint32_t tval) {
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"((uint64_t)tval));
}
static inline void write_cntv_ctl(uint64_t val) {
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(val));
}

/* ── State ────────────────────────────────────────────────── */
static uint64_t       s_ticks    = 0;
static uint32_t       s_reload   = 0;
static TimerCallback  s_tick_cb  = NULL;

/* ── Timer IRQ handler (IRQ 27, routed via GIC) ─────────── */
static void timer_irq(uint32_t irq) {
    (void)irq;
    s_ticks++;
    if (s_tick_cb) s_tick_cb();
    /* Rearm: write reload value to TVAL (countdown restarts) */
    write_cntv_tval(s_reload);
}

/* ── Public API ───────────────────────────────────────────── */

uint64_t hal_timer_ticks(void) { return s_ticks; }

void hal_timer_sleep_ms(uint32_t ms) {
    uint64_t target = s_ticks + ms;
    while (s_ticks < target)
        __asm__ volatile("wfe");
}

void hal_timer_set_hz(uint32_t hz) {
    uint64_t freq = read_cntfrq();
    s_reload = (uint32_t)(freq / hz);
    write_cntv_tval(s_reload);
}

void hal_timer_init(TimerCallback cb) {
    s_tick_cb = cb;

    uint64_t freq = read_cntfrq();
    if (freq == 0) freq = 62500000; /* QEMU default 62.5 MHz */

    /* 1000 Hz = 1 ms per tick */
    s_reload = (uint32_t)(freq / 1000);

    /* Register GIC handler for PPI 27 */
    extern void gic_register_handler(uint32_t, void (*)(uint32_t));
    extern void gic_enable_irq(uint32_t);
    gic_register_handler(27, timer_irq);
    gic_enable_irq(27);

    /* Enable virtual timer: ENABLE=1, IMASK=0 */
    write_cntv_tval(s_reload);
    write_cntv_ctl(1);

    /* Publish to registry */
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)freq);
    reg_set("/hal/timer/cntfrq",  val_str(buf),  RK_READ);
    reg_set("/hal/timer/hz",      val_int(1000),  RK_READ);
    reg_set("/hal/timer/status",  val_str("ok"),  RK_READ);
}
