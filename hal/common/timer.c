/*
 * hal/common/timer.c — Shared HAL Timer Logic
 * ============================================
 */

#include "../include/nexs_timer.h"
#include "../include/nexs_mmu.h"
#include <stddef.h>

/* Global monotonic tick counter (1 ms per tick) */
volatile uint64_t g_hal_ticks   = 0;
TimerCallback     g_hal_tick_cb = NULL;

uint64_t hal_timer_ticks(void) {
    return g_hal_ticks;
}

void hal_timer_sleep_ms(uint32_t ms) {
    uint64_t start = g_hal_ticks;
    while (g_hal_ticks - start < (uint64_t)ms) {
        mmu_worker_sync();
        /* Busy wait or yield if we had a scheduler yield here */
#if defined(__x86_64__)
        __asm__ volatile("pause");
#elif defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }
}
