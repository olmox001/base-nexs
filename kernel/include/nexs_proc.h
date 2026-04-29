/*
 * kernel/include/nexs_proc.h — Process/TCB descriptor
 */
#ifndef NEXS_PROC_H
#define NEXS_PROC_H
#pragma once
#include "../../core/include/nexs_common.h"
#include <stdint.h>

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
