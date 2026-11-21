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
#include <stdint.h>
#include <string.h>

static Endpoint *s_vfs_ep = NULL;

/* vfs_server_main — The main loop of the VFS server */
void vfs_server_main(void *arg) {
    (void)arg;
    if (s_vfs_ep == NULL) {
        nexs_hal_print("[VFS] endpoint capability not admitted; refusing service\n");
        return;
    }
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
            int64_t requested = val_to_int(&size_val);
            if (requested < 0 || (uint64_t)requested >= SIZE_MAX) {
                val_map_set(&resp, "status", val_int(-1));
            } else {
                size_t bytes = (size_t)requested;
                char *buf = nexs_alloc(bytes + 1U);
                if (buf == NULL) {
                    val_map_set(&resp, "status", val_int(-1));
                } else {
                    int n = vfs_read(fd, buf, bytes);
                    if (n >= 0 && (size_t)n <= bytes) {
                        buf[n] = '\0';
                        val_map_set(&resp, "status", val_int(0));
                        val_map_set(&resp, "data", val_str(buf));
                    } else {
                        val_map_set(&resp, "status", val_int(-1));
                    }
                    nexs_free(buf, bytes + 1U);
                }
            }
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
    if (s_vfs_ep != NULL)
        return;
    /* A RegKey/PID path is NOT a capability. A real root-process CSpace
     * must already exist before any endpoint slot can be advertised. */
    NexsProc *root = proc_by_pid(1U);
    if (root == NULL || root->cspace.caps == NULL) {
        nexs_hal_print("[VFS] no admitted root CSpace; endpoint unavailable\n");
        return;
    }
    Endpoint *candidate = ipc_endpoint_create();
    if (candidate == NULL) {
        nexs_hal_print("[VFS] endpoint allocation refused\n");
        return;
    }
    Cap endpoint = { .type = CAP_ENDPOINT, .obj = candidate,
                     .rights = CAP_READ | CAP_WRITE, .badge = 0U };
    if (cap_insert(root, 0U, endpoint) != 0) {
        (void)ipc_endpoint_destroy(candidate);
        nexs_hal_print("[VFS] root CSpace admission refused\n");
        return;
    }
    if (reg_set("/sys/vfs/endpoint", val_int(0), RK_READ) != 0) {
        (void)cap_remove(root, 0U);
        (void)ipc_endpoint_destroy(candidate);
        nexs_hal_print("[VFS] endpoint discovery rollback\n");
        return;
    }
    s_vfs_ep = candidate;
}

#endif /* NEXS_VFS_SERVER_ENABLED */
