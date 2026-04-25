/*
 * kernel/include/nexs_msg.h — Kernel IPC message protocol
 */
#ifndef NEXS_MSG_H
#define NEXS_MSG_H
#pragma once
#include "../../core/include/nexs_value.h"

/* Kernel message: "verb:arg0:arg1:n" encoded as TYPE_STR */
typedef struct {
    char     verb[32];
    char     arg0[256];
    char     arg1[256];
    int64_t  n;
    uint32_t sender_pid;
} KernelMsg;

Value kmsg_encode(const KernelMsg *m);
int   kmsg_decode(const Value *v, KernelMsg *out);
void  kmsg_dispatch_loop(void);

#endif
