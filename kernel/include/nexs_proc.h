/*
 * kernel/include/nexs_proc.h — Process/TCB descriptor
 */
#ifndef NEXS_PROC_H
#define NEXS_PROC_H
#pragma once
#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"
#include "nexs_cap.h"
#include <stdint.h>

typedef struct Endpoint Endpoint;

typedef enum {
    PROC_RUNNING = 0,
    PROC_READY,
    PROC_BLOCKED,
    PROC_ZOMBIE,
} ProcState;

typedef struct NexsProc {
    uint32_t   pid;
    ProcState  state;
    char       name[64];
    char       reg_path[REG_PATH_MAX];   /* /proc/<pid> */
    char       ipc_inbox[REG_PATH_MAX];  /* /proc/<pid>/inbox */
    void      *ctx;                      /* arch-specific saved context */
    uint8_t    priority;                 /* 0=highest, 255=lowest */
    uint64_t   timeslice_us;
    uint64_t   ticks_used;
    char       wait_path[REG_PATH_MAX];  /* non-empty when PROC_BLOCKED */
    uint64_t   mmu_root;                 /* PML4/TTBR0 physical address */
    uint64_t   heap_start;
    uint64_t   heap_end;
    CNode      cspace;                   /* Capability Space (seL4 style) */
    Value      ipc_msg;                  /* IPC Message payload */
    uint32_t   ipc_badge;                /* Badge from sender */
    Endpoint  *reply_ep;                 /* Private reply endpoint */
    struct NexsProc *next;
} NexsProc;

NexsProc *proc_create(const char *name, void (*entry)(void *), void *arg);
void      proc_destroy(NexsProc *p);
void      proc_yield(void);
void      proc_block(const char *wait_path);
void      proc_unblock_by_msg(const char *ipc_path);
NexsProc *proc_current(void);
NexsProc *proc_by_pid(uint32_t pid);

extern NexsProc *g_current_proc;

#endif
