/*
 * hal/include/nexs_timer.h — HAL timer API (APIC / ARM generic)
 */
#ifndef NEXS_TIMER_H
#define NEXS_TIMER_H
#pragma once
#include <stdint.h>

/* Callback type: called every timer tick (1 ms by default) */
typedef void (*TimerCallback)(void);

void     hal_timer_init(TimerCallback cb);
void     hal_timer_set_hz(uint32_t hz);
uint64_t hal_timer_ticks(void);       /* monotonic tick counter */
void     hal_timer_sleep_ms(uint32_t ms);

#endif
