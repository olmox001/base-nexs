/*
 * kernel/proc.c — Process create/destroy/block
 * ===============================================
 * STEP 07: TCB management. State lives in /proc/<pid>/ registry.
 */

#include "include/nexs_proc.h"
#include "include/nexs_ctx.h"
#include "include/nexs_sched.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_alloc.h"
#include <string.h>
#include <stdio.h>

NexsProc *g_current_proc = NULL;

static uint32_t s_next_pid = 1;

/* Each process gets a small stack (32 KB) */
#define PROC_STACK_SIZE (32 * 1024)

NexsProc *proc_create(const char *name, void (*entry)(void *), void *arg) {
    NexsProc *p = (NexsProc *)nexs_alloc(sizeof(NexsProc));
    if (!p) return NULL;
    memset(p, 0, sizeof(NexsProc));

    p->pid   = s_next_pid++;
    p->state = PROC_READY;
    p->priority = 128;
    p->timeslice_us = 1000; /* 1 ms */
    strncpy(p->name, name ? name : "unnamed", sizeof(p->name) - 1);
    snprintf(p->reg_path,   sizeof(p->reg_path),   "/proc/%u", p->pid);
    snprintf(p->ipc_inbox,  sizeof(p->ipc_inbox),  "/proc/%u/inbox", p->pid);

    /* Allocate context + stack */
#ifdef __aarch64__
    typedef struct { uint64_t x19,x20,x21,x22,x23,x24,x25,x26,x27,x28,x29,x30,sp; } Ctx;
#else
    typedef struct { uint64_t rbx,rbp,r12,r13,r14,r15,rsp,rip; } Ctx;
#endif
    Ctx *ctx = (Ctx *)nexs_alloc(sizeof(Ctx));
    if (!ctx) { nexs_free(p, sizeof(*p)); return NULL; }
    memset(ctx, 0, sizeof(Ctx));

    uint8_t *stack = (uint8_t *)nexs_alloc(PROC_STACK_SIZE);
    if (!stack) { nexs_free(ctx, sizeof(*ctx)); nexs_free(p, sizeof(*p)); return NULL; }

    /* Set up initial stack frame so ctx_switch returns into entry(arg) */
    uint64_t *sp = (uint64_t *)(stack + PROC_STACK_SIZE);
    *--sp = (uint64_t)arg;     /* pushed as if by call convention */
    *--sp = (uint64_t)entry;   /* return address = entry */
#ifdef __aarch64__
    ctx->sp  = (uint64_t)sp;
    ctx->x30 = (uint64_t)entry;
    ctx->x19 = (uint64_t)arg;
#else
    ctx->rsp = (uint64_t)sp;
    ctx->rip = (uint64_t)entry;
#endif
    p->ctx = ctx;

    /* Publish to registry */
    reg_set(p->reg_path,  val_str(name ? name : "unnamed"), RK_READ);
    {
        char buf[REG_PATH_MAX];
        snprintf(buf, sizeof(buf), "%s/state", p->reg_path);
        reg_set(buf, val_str("ready"), RK_READ);
        snprintf(buf, sizeof(buf), "%s/priority", p->reg_path);
        reg_set(buf, val_int(p->priority), RK_READ);
    }
    reg_ipc_init_queue(p->ipc_inbox, 64);

    sched_add(p);
    return p;
}

void proc_destroy(NexsProc *p) {
    if (!p) return;
    sched_remove(p);
    p->state = PROC_ZOMBIE;
    char buf[REG_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/state", p->reg_path);
    reg_set(buf, val_str("zombie"), RK_READ);
    /* Free resources — ctx/stack freed by parent's wait */
}

NexsProc *proc_current(void) { return g_current_proc; }

NexsProc *proc_by_pid(uint32_t pid) {
    /* Fast path: check /proc/<pid>/state in registry */
    char buf[REG_PATH_MAX];
    snprintf(buf, sizeof(buf), "/proc/%u", pid);
    RegKey *k = reg_lookup(buf);
    if (!k) return NULL;
    /* Walk the scheduler queues (slow, only for rare lookups) */
    extern NexsProc *sched_find(uint32_t pid);
    return sched_find(pid);
}

void proc_block(const char *wait_path) {
    NexsProc *p = g_current_proc;
    if (!p) return;
    p->state = PROC_BLOCKED;
    char buf[REG_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/state", p->reg_path);
    reg_set(buf, val_str("blocked"), RK_READ);
    snprintf(buf, sizeof(buf), "%s/wait_path", p->reg_path);
    reg_set(buf, val_str(wait_path), RK_READ);
    sched_yield();
}

void proc_unblock_by_msg(const char *ipc_path) {
    /* Scan registry for /proc/[pid]/wait_path matching ipc_path */
    /* For now: linear scan via sched queues */
    extern void sched_unblock_waiting(const char *path);
    sched_unblock_waiting(ipc_path);
}

void proc_yield(void) {
    sched_yield();
}
