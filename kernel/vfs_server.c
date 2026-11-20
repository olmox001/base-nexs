/*
 * kernel/vfs_server.c — Message-based VFS Server (Hybrid seL4/Plan9)
 * ================================================================
 */

#ifdef NEXS_VFS_SERVER_ENABLED

#include "include/nexs_vfs.h"
#include "include/nexs_ipc.h"
#include "include/nexs_cap.h"
#include "include/nexs_proc.h"
#include "../core/include/nexs_value.h"
#include "../registry/include/nexs_registry.h"
#include "../hal/include/nexs_hal.h"
#include <string.h>

static Endpoint *s_vfs_ep = NULL;

/* vfs_server_main — The main loop of the VFS server */
void vfs_server_main(void *arg) {
    (void)arg;
    nexs_hal_print("[VFS] Server started, listening on Endpoint.\n");

    while (1) {
        Value msg;
        uint32_t badge = 0;
        int res = nexs_ipc_recv(s_vfs_ep, &msg, &badge);
        if (res < 0) continue;

        if (msg.type != TYPE_MAP) {
            val_free(&msg);
            continue;
        }

        /* Protocol: { "op": "open", "path": "/...", "flags": ... } */
        Value op = val_map_get(&msg, "op");
        Value resp = val_map_new();

        if (val_eq_str(&op, "open")) {
            Value path = val_map_get(&msg, "path");
            Value flags = val_map_get(&msg, "flags");
            int fd = vfs_open((char *)path.data, (int)val_to_int(&flags));
            val_map_set(&resp, "status", val_int(fd >= 0 ? 0 : -1));
            val_map_set(&resp, "fd", val_int(fd));
        } else if (val_eq_str(&op, "read")) {
            Value fd_val = val_map_get(&msg, "fd");
            Value size_val = val_map_get(&msg, "size");
            int fd = (int)val_to_int(&fd_val);
            size_t size = (size_t)val_to_int(&size_val);
            char *buf = xmalloc(size + 1);
            int n = vfs_read(fd, buf, size);
            if (n >= 0) {
                buf[n] = '\0';
                val_map_set(&resp, "status", val_int(0));
                val_map_set(&resp, "data", val_str(buf));
            } else {
                val_map_set(&resp, "status", val_int(-1));
            }
            xfree(buf);
        } else if (val_eq_str(&op, "write")) {
            Value fd_val = val_map_get(&msg, "fd");
            Value data_val = val_map_get(&msg, "data");
            int fd = (int)val_to_int(&fd_val);
            int n = vfs_write(fd, data_val.data, strlen(data_val.data));
            val_map_set(&resp, "status", val_int(n >= 0 ? 0 : -1));
            val_map_set(&resp, "bytes", val_int(n));
        } else {
            val_map_set(&resp, "status", val_int(-1));
            val_map_set(&resp, "err", val_str("unknown op"));
        }

        /* seL4 fusion: Reply to caller */
        nexs_ipc_reply(badge, resp);

        val_free(&msg);
        val_free(&resp);
    }
}

void vfs_server_init(void) {
    s_vfs_ep = ipc_endpoint_create();
    
    /* Publish VFS capability to registry (Plan 9 style discovery) */
    Cap c;
    c.type = CAP_ENDPOINT;
    c.obj = s_vfs_ep;
    c.rights = CAP_READ | CAP_WRITE;
    c.badge = 0; /* VFS itself has no badge when receiving */

    /* Insert into root process (PID 1) CSpace at slot 0 */
    NexsProc *root = proc_by_pid(1);
    if (root) {
        extern int cap_insert(NexsProc *p, uint32_t slot, Cap c);
        cap_insert(root, 0, c);
    }
    
    reg_set("/sys/vfs/endpoint", val_int(0), RK_READ); /* Slot 0 in root */
}

#endif /* NEXS_VFS_SERVER_ENABLED */
