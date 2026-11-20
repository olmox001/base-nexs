/*
 * kernel/msg.c — Kernel IPC message encode/decode + dispatch loop
 */

#include "include/nexs_msg.h"
#include "include/nexs_proc.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#ifdef NEXS_BAREMETAL
#  include "include/stdlib.h"
#else
#  include <stdlib.h>
#endif
#include <string.h>
#include <stdio.h>

/* =========================================================
   ENCODE / DECODE
   ========================================================= */

Value kmsg_encode(const KernelMsg *m) {
    char buf[640];
    snprintf(buf, sizeof(buf), "%s\x1f%s\x1f%s\x1f%lld\x1f%u",
             m->verb, m->arg0, m->arg1,
             (long long)m->n, (unsigned)m->sender_pid);
    return val_str(buf);
}

int kmsg_decode(const Value *v, KernelMsg *out) {
    if (!v || !out || v->type != TYPE_STR || !v->data) return -1;

    char tmp[640];
    strncpy(tmp, (const char *)v->data, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    memset(out, 0, sizeof(*out));

    char *p = tmp;
    char *tok;

    tok = strtok(p, "\x1f"); if (!tok) return -1;
    strncpy(out->verb, tok, sizeof(out->verb) - 1);

    tok = strtok(NULL, "\x1f"); if (!tok) return -1;
    strncpy(out->arg0, tok, sizeof(out->arg0) - 1);

    tok = strtok(NULL, "\x1f"); if (!tok) return -1;
    strncpy(out->arg1, tok, sizeof(out->arg1) - 1);

    tok = strtok(NULL, "\x1f"); if (!tok) return -1;
    out->n = (int64_t)atoll(tok);

    tok = strtok(NULL, "\x1f"); if (!tok) return -1;
    out->sender_pid = (uint32_t)atoi(tok);

    return 0;
}

/* =========================================================
   DISPATCH LOOP
   ========================================================= */

/* Forward declaration — defined in kernel/syscall.c */
Value kmsg_handle(const KernelMsg *m);

void kmsg_dispatch_loop(void) {
    const char *inbox = "/sys/kernel/inbox";
    reg_ipc_init_queue(inbox, 64);

    while (1) {
        Value msg = val_nil();
        if (reg_ipc_recv(inbox, &msg) != 0 || msg.type == TYPE_NIL) {
            val_free(&msg);
            if (g_current_proc) proc_block(inbox);
            continue;
        }

        KernelMsg km;
        if (kmsg_decode(&msg, &km) != 0) {
            val_free(&msg);
            continue;
        }
        val_free(&msg);

        Value result = kmsg_handle(&km);

        /* Reply to sender's inbox */
        if (km.sender_pid > 0) {
            char reply_path[REG_PATH_MAX];
            snprintf(reply_path, sizeof(reply_path),
                     "/proc/%u/inbox", km.sender_pid);
            reg_ipc_send(reply_path, result);
            proc_unblock_by_msg(reply_path);
        }
        val_free(&result);
    }
}
