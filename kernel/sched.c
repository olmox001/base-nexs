/*
 * kernel/sched.c — Round-robin scheduler with 8 priority levels
 * ===============================================================
 * sched_tick() called from timer IRQ → context switch.
 *
 * Data structure: 8 singly-linked lists with O(1) tail tracking.
 * sched_pick_next(): O(1) — picks head of highest non-empty level.
 * All scheduler state published to /sys/sched/ in the registry.
 */

#include "include/nexs_sched.h"
#include "include/nexs_proc.h"
#include "include/nexs_ctx.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../hal/include/nexs_hal.h"
#include <string.h>
#include <stdio.h>

/* ── Priority queues (singly-linked with O(1) tail tracking) ─ */
static NexsProc *s_queues[SCHED_LEVELS];  /* head of each level */
static NexsProc *s_tails[SCHED_LEVELS];   /* tail of each level */
static int       s_counts[SCHED_LEVELS];
static uint64_t  s_tick = 0;
static int       s_total = 0;

static int prio_level(NexsProc *p) {
    return (int)(p->priority * SCHED_LEVELS / 256);
}

/* ── Registry update (throttled: every 64 ticks) ─────────── */
static void publish_state(void) {
    if (s_tick % 64 != 0) return;
    reg_set("/sys/sched/tick",          val_int((int64_t)s_tick), RK_READ);
    reg_set("/sys/sched/runqueue_depth",val_int(s_total),         RK_READ);
    if (g_current_proc) {
        reg_set("/sys/sched/current_pid",
                val_int(g_current_proc->pid), RK_READ);
    }
}

/* ── API ──────────────────────────────────────────────────── */

void sched_init(void) {
    memset(s_queues, 0, sizeof(s_queues));
    memset(s_tails,  0, sizeof(s_tails));
    memset(s_counts, 0, sizeof(s_counts));
    s_tick  = 0;
    s_total = 0;

    reg_set("/sys/sched/policy",        val_str("rr"),    RK_READ);
    reg_set("/sys/sched/quantum_us",    val_int(1000),    RK_READ);
    reg_set("/sys/sched/ncpu",          val_int(1),       RK_READ);
    reg_set("/sys/sched/status",        val_str("idle"),  RK_READ);
}

void sched_add(NexsProc *p) {
    if (!p) return;
    int lvl = prio_level(p);
    p->next = NULL;
    if (s_tails[lvl]) s_tails[lvl]->next = p;
    else s_queues[lvl] = p;
    s_tails[lvl] = p;
    s_counts[lvl]++;
    s_total++;
    p->state = PROC_READY;
}

void sched_remove(NexsProc *p) {
    if (!p) return;
    int lvl = prio_level(p);
    NexsProc *prev = NULL;
    NexsProc *cur  = s_queues[lvl];
    while (cur) {
        if (cur == p) {
            if (prev) prev->next = p->next;
            else s_queues[lvl] = p->next;
            if (s_tails[lvl] == p) s_tails[lvl] = prev;
            p->next = NULL;
            s_counts[lvl]--;
            s_total--;
            return;
        }
        prev = cur;
        cur  = cur->next;
    }
}

NexsProc *sched_pick_next(void) {
    nexs_hal_irq_disable();
    NexsProc *picked = NULL;
    for (int lvl = 0; lvl < SCHED_LEVELS; lvl++) {
        if (s_queues[lvl] && s_queues[lvl]->state == PROC_READY) {
            NexsProc *p = s_queues[lvl];
            s_queues[lvl] = p->next;
            if (!s_queues[lvl]) s_tails[lvl] = NULL;
            p->next = NULL;
            if (s_tails[lvl]) s_tails[lvl]->next = p;
            else s_queues[lvl] = p;
            s_tails[lvl] = p;
            picked = p;
            break;
        }
    }
    nexs_hal_irq_enable();
    return picked;
}

void sched_tick(void) {
    s_tick++;
    publish_state();

    NexsProc *next = sched_pick_next();
    if (!next || next == g_current_proc) return;

    NexsProc *prev = g_current_proc;
    g_current_proc = next;
    next->state = PROC_RUNNING;
    if (prev) prev->state = PROC_READY;

    /* Context switch */
    if (prev)
        ctx_switch(prev->ctx, next->ctx);
    /* If no prev (first process), just load next context */
}

void sched_yield(void) {
    sched_tick(); /* Trigger a switch now */
}

/* Find a proc by pid (linear scan — only for rare lookups) */
NexsProc *sched_find(uint32_t pid) {
    for (int lvl = 0; lvl < SCHED_LEVELS; lvl++) {
        NexsProc *p = s_queues[lvl];
        while (p) {
            if (p->pid == pid) return p;
            p = p->next;
        }
    }
    return NULL;
}

/* Unblock any process waiting on ipc_path */
void sched_unblock_waiting(const char *path) {
    for (int lvl = 0; lvl < SCHED_LEVELS; lvl++) {
        NexsProc *p = s_queues[lvl];
        while (p) {
            if (p->state == PROC_BLOCKED &&
                strcmp(p->wait_path, path) == 0) {
                p->state = PROC_READY;
                p->wait_path[0] = '\0';
            }
            p = p->next;
        }
    }
}
