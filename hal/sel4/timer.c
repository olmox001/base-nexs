/*
 * hal/sel4/timer.c — seL4-specific HAL Timer Logic using hardware registers
 * =========================================================================
 */

#include "../include/nexs_timer.h"
#include "../include/nexs_mmu.h"
#include <stddef.h>

/* Global monotonic tick counter (1 ms per tick) - unused but defined for compatibility */
volatile uint64_t g_hal_ticks   = 0;
TimerCallback     g_hal_tick_cb = NULL;

#if defined(__riscv)
static inline uint64_t read_hardware_ticks(void) {
  uint64_t val;
  __asm__ volatile("rdtime %0" : "=r"(val));
  return val;
}

#elif defined(__aarch64__)
static inline uint64_t read_hardware_ticks(void) {
  uint64_t val;
  /* Read physical count register cntpct_el0 to avoid VM trapping faults under seL4 EL2 */
  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
  return val;
}

#elif defined(__x86_64__)
static inline uint64_t read_hardware_ticks(void) {
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}
#endif

#if defined(__riscv) || defined(__aarch64__) || defined(__x86_64__)
static uint64_t s_sel4_timer_start = 0;
static int s_sel4_timer_init = 0;
static uint64_t g_timer_ticks_per_ms = 0;

static inline void init_timer_frequency(void) {
#if defined(__aarch64__)
  /* Read AArch64 physical timer frequency register dynamically to avoid hardcoding */
  uint64_t freq;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  g_timer_ticks_per_ms = freq / 1000;
  if (g_timer_ticks_per_ms == 0) {
    g_timer_ticks_per_ms = 62500; /* Fallback if freq is uninitialized/zero */
  }
#elif defined(__riscv)
  /* RISC-V timebase frequency is typically 10 MHz (10,000 ticks per ms) on QEMU Virt */
  g_timer_ticks_per_ms = 10000;
#elif defined(__x86_64__)
  /* x86_64 TSC runs at approx 2.0 GHz (2,000,000 ticks per ms) under QEMU */
  g_timer_ticks_per_ms = 2000000;
#else
  g_timer_ticks_per_ms = 1000;
#endif
}
#endif

uint64_t hal_timer_ticks(void) {
#if defined(__riscv) || defined(__aarch64__) || defined(__x86_64__)
  if (!s_sel4_timer_init) {
    init_timer_frequency();
    s_sel4_timer_start = read_hardware_ticks();
    s_sel4_timer_init = 1;
  }
  uint64_t now = read_hardware_ticks();
  if (now >= s_sel4_timer_start) {
    uint64_t diff = now - s_sel4_timer_start;
    return diff / g_timer_ticks_per_ms;
  }
  return 0;
#else
  return g_hal_ticks;
#endif
}

void hal_timer_sleep_ms(uint32_t ms) {
    uint64_t start = hal_timer_ticks();
    while (hal_timer_ticks() - start < (uint64_t)ms) {
        mmu_worker_sync();
        /* Busy wait or yield if we had a scheduler yield here */
#if defined(__x86_64__)
        __asm__ volatile("pause");
#elif defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }
}
