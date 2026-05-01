/*
 * kernel/proc.c — Process create/destroy/block
 * ===============================================
 * TCB management. State lives in /proc/<pid>/ registry.
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

#ifdef __aarch64__
/* x19 = arg, x20 = real entry fn — set by proc_create */
__attribute__((naked)) static void proc_trampoline_arm64(void) {
    __asm__ volatile(
        "mov x0, x19\n"
        "br  x20\n"
    );
}
#endif

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
    Ctx_arm64 *ctx = (Ctx_arm64 *)nexs_alloc(sizeof(Ctx_arm64));
    if (!ctx) { nexs_free(p, sizeof(*p)); return NULL; }
    memset(ctx, 0, sizeof(Ctx_arm64));
    uint8_t *stack = (uint8_t *)nexs_alloc(PROC_STACK_SIZE);
    if (!stack) { nexs_free(ctx, sizeof(Ctx_arm64)); nexs_free(p, sizeof(*p)); return NULL; }
    uint64_t *sp = (uint64_t *)(stack + PROC_STACK_SIZE);
    *--sp = (uint64_t)arg;
    *--sp = (uint64_t)entry;
    ctx->sp  = (uint64_t)sp;
    ctx->x30 = (uint64_t)proc_trampoline_arm64;
    ctx->x19 = (uint64_t)arg;
    ctx->x20 = (uint64_t)entry;
#else
    Ctx_amd64 *ctx = (Ctx_amd64 *)nexs_alloc(sizeof(Ctx_amd64));
    if (!ctx) { nexs_free(p, sizeof(*p)); return NULL; }
    memset(ctx, 0, sizeof(Ctx_amd64));
    uint8_t *stack = (uint8_t *)nexs_alloc(PROC_STACK_SIZE);
    if (!stack) { nexs_free(ctx, sizeof(Ctx_amd64)); nexs_free(p, sizeof(*p)); return NULL; }
    uint64_t *sp = (uint64_t *)(stack + PROC_STACK_SIZE);
    *--sp = (uint64_t)arg;
    *--sp = (uint64_t)entry;
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
    return sched_find(pid);
}

void proc_block(const char *wait_path) {
    NexsProc *p = g_current_proc;
    if (!p) return;
    p->state = PROC_BLOCKED;
    strncpy(p->wait_path, wait_path ? wait_path : "", REG_PATH_MAX - 1);
    p->wait_path[REG_PATH_MAX - 1] = '\0';
    char buf[REG_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/state", p->reg_path);
    reg_set(buf, val_str("blocked"), RK_READ);
    sched_yield();
}

void proc_unblock_by_msg(const char *ipc_path) {
    /* Linear scan via sched queues for process waiting on ipc_path */
    sched_unblock_waiting(ipc_path);
}

void proc_yield(void) {
    sched_yield();
}
