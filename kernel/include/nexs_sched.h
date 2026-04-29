/*
 * kernel/include/nexs_sched.h — Scheduler API
 */
#ifndef NEXS_SCHED_H
#define NEXS_SCHED_H
#pragma once
#include "nexs_proc.h"

#define SCHED_LEVELS  8     /* priority levels (0=highest) */

void      sched_init(void);
void      sched_add(NexsProc *p);
void      sched_remove(NexsProc *p);
void      sched_tick(void);             /* called from timer IRQ */
NexsProc *sched_pick_next(void);        /* O(1) */
void      sched_yield(void);

#endif
