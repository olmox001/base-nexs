/*
 * kernel/include/nexs_ipc.h — Synchronous IPC API
 */

#ifndef NEXS_IPC_H
#define NEXS_IPC_H

#include "../../core/include/nexs_value.h"

typedef struct Endpoint Endpoint;
typedef struct Notification Notification;

/* Create/Destroy Endpoints & Notifications */
Endpoint *ipc_endpoint_create(void);
void      ipc_endpoint_destroy(Endpoint *ep);
Notification *ipc_notification_create(void);
void          ipc_notification_destroy(Notification *ntfn);

/* Core Syscall Logic (Sync) */
int nexs_ipc_send(Endpoint *ep, Value msg, uint32_t badge);
int nexs_ipc_recv(Endpoint *ep, Value *out_msg, uint32_t *out_badge);

/* Async Notifications */
int nexs_ipc_signal(Notification *ntfn, uint32_t badge);
int nexs_ipc_wait(Notification *ntfn, uint32_t *out_badge);

/* Helper for Call/ReplyWait */
int nexs_ipc_call(Endpoint *ep, Value msg, Value *out_reply);
int nexs_ipc_reply(uint32_t target_pid, Value msg);

#endif
