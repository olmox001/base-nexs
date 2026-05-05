/*
 * kernel/ipc.c — Synchronous Rendezvous IPC (seL4-style)
 * ========================================================
 */

#include "include/nexs_ipc.h"
#include "include/nexs_proc.h"
#include "include/nexs_sched.h"
#include "../core/include/nexs_alloc.h"
#include <string.h>

struct Endpoint {
    NexsProc *waiting_send;
    NexsProc *waiting_recv;
};

/* Internal: Queue management */
static void enqueue_proc(NexsProc **head, NexsProc *p) {
    p->next = NULL;
    if (!*head) {
        *head = p;
    } else {
        NexsProc *curr = *head;
        while (curr->next) curr = curr->next;
        curr->next = p;
    }
}

static NexsProc *dequeue_proc(NexsProc **head) {
    if (!*head) return NULL;
    NexsProc *p = *head;
    *head = p->next;
    p->next = NULL;
    return p;
}

/* =========================================================
   CORE IPC OPERATIONS
   ========================================================= */

int nexs_ipc_send(Endpoint *ep, Value msg, uint32_t badge) {
    if (!ep) return -1;

    NexsProc *receiver = dequeue_proc(&ep->waiting_recv);
    if (receiver) {
        /* RENDEZVOUS: Transfer message directly to receiver's TCB */
        receiver->ipc_msg = val_clone(&msg);
        receiver->ipc_badge = badge;
        
        /* Wake up receiver */
        receiver->state = PROC_READY;
        return 0;
    }

    /* No receiver: block sender */
    NexsProc *curr = proc_current();
    curr->ipc_msg = val_clone(&msg);
    curr->ipc_badge = badge;
    enqueue_proc(&ep->waiting_send, curr);
    curr->state = PROC_BLOCKED;
    proc_yield();
    return 0;
}

int nexs_ipc_recv(Endpoint *ep, Value *out_msg, uint32_t *out_badge) {
    if (!ep || !out_msg) return -1;

    NexsProc *sender = dequeue_proc(&ep->waiting_send);
    if (sender) {
        /* RENDEZVOUS: Receive from waiting sender's TCB */
        *out_msg = sender->ipc_msg; /* Transfer ownership */
        if (out_badge) *out_badge = sender->ipc_badge;
        
        /* Wake up sender */
        sender->state = PROC_READY;
        return 0;
    }

    /* No sender: block receiver */
    NexsProc *curr = proc_current();
    enqueue_proc(&ep->waiting_recv, curr);
    curr->state = PROC_BLOCKED;
    proc_yield();
    
    /* After wake up, the message should be in our TCB */
    *out_msg = curr->ipc_msg;
    if (out_badge) *out_badge = curr->ipc_badge;
    return 0;
}

struct Notification {
    uint32_t badge_mask;
    NexsProc *waiting_recv;
};

/* ... (keep previous functions) ... */

/* =========================================================
   NOTIFICATION OPERATIONS (Async)
   ========================================================= */

int nexs_ipc_signal(Notification *ntfn, uint32_t badge) {
    if (!ntfn) return -1;

    ntfn->badge_mask |= badge;

    NexsProc *receiver = dequeue_proc(&ntfn->waiting_recv);
    if (receiver) {
        receiver->ipc_badge = ntfn->badge_mask;
        ntfn->badge_mask = 0;
        receiver->state = PROC_READY;
    }
    return 0;
}

int nexs_ipc_wait(Notification *ntfn, uint32_t *out_badge) {
    if (!ntfn) return -1;

    if (ntfn->badge_mask != 0) {
        if (out_badge) *out_badge = ntfn->badge_mask;
        ntfn->badge_mask = 0;
        return 0;
    }

    /* Block receiver */
    NexsProc *curr = proc_current();
    enqueue_proc(&ntfn->waiting_recv, curr);
    curr->state = PROC_BLOCKED;
    proc_yield();

    if (out_badge) *out_badge = curr->ipc_badge;
    return 0;
}

/* =========================================================
   HIGH-LEVEL IPC (Call / Reply)
   ========================================================= */

int nexs_ipc_call(Endpoint *ep, Value msg, Value *out_reply) {
    NexsProc *curr = proc_current();
    if (!curr || !ep) return -1;

    /* 1. Send message to target endpoint, using our PID as badge so the server can reply */
    int res = nexs_ipc_send(ep, msg, curr->pid);
    if (res < 0) return res;

    /* 2. Wait for reply on our private reply endpoint */
    uint32_t badge = 0;
    return nexs_ipc_recv(curr->reply_ep, out_reply, &badge);
}

int nexs_ipc_reply(uint32_t target_pid, Value msg) {
    NexsProc *target = proc_by_pid(target_pid);
    if (!target || !target->reply_ep) return -1;

    /* Send message to the target's private reply endpoint */
    /* We use badge=0 for replies for now */
    return nexs_ipc_send(target->reply_ep, msg, 0);
}

/* =========================================================
   PUBLIC INTERFACE
   ========================================================= */

Endpoint *ipc_endpoint_create(void) {
    Endpoint *ep = xmalloc(sizeof(Endpoint));
    ep->waiting_send = NULL;
    ep->waiting_recv = NULL;
    return ep;
}

void ipc_endpoint_destroy(Endpoint *ep) {
    /* TODO: Wake up all waiting processes with error */
    xfree(ep);
}

Notification *ipc_notification_create(void) {
    Notification *ntfn = xmalloc(sizeof(Notification));
    ntfn->badge_mask = 0;
    ntfn->waiting_recv = NULL;
    return ntfn;
}

void ipc_notification_destroy(Notification *ntfn) {
    xfree(ntfn);
}
