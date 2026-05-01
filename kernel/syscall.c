/*
 * kernel/syscall.c — Syscall dispatch table (verb → handler)
 *
 * Handlers receive a KernelMsg and the sender's PID.
 * Ring check: /sys/ mutations require RK_ADMIN in /proc/<pid>/caps.
 */

#include "include/nexs_msg.h"
#include "include/nexs_proc.h"
#include "include/nexs_sched.h"
#include "include/nexs_vfs.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../lang/include/nexs_eval.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================
   RING CHECK
   ========================================================= */

static int caller_has_admin(uint32_t pid) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%u/caps", pid);
    Value v = reg_get(path);
    int ok = (v.type == TYPE_INT && (v.ival & RK_ADMIN));
    val_free(&v);
    return ok;
}

static Value err_eperm(void) {
    return val_err(1, "EPERM");
}

/* =========================================================
   SYSCALL HANDLERS
   ========================================================= */

static Value sys_open(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int flags = (int)m->n;
    int fd = vfs_open(m->arg0, flags);
    return val_int(fd);
}

static Value sys_read(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int fd = (int)m->n;
    char buf[1024];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    if (n < 0) return val_err(1, "read failed");
    buf[n] = '\0';
    return val_str(buf);
}

static Value sys_write(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int fd = (int)m->n;
    int n = vfs_write(fd, m->arg1, strlen(m->arg1));
    return val_int(n);
}

static Value sys_close(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int fd = (int)m->n;
    return val_int(vfs_close(fd));
}

static Value sys_stat(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    VfsInode ino;
    int rc = vfs_stat(m->arg0, &ino);
    if (rc < 0) return val_err(1, "stat failed");
    char buf[64];
    snprintf(buf, sizeof(buf), "ino:%llu size:%llu",
             (unsigned long long)ino.ino,
             (unsigned long long)ino.size);
    return val_str(buf);
}

static Value sys_mkdir(const KernelMsg *m, uint32_t pid) {
    if (!caller_has_admin(pid)) return err_eperm();
    int rc = vfs_mkdir(m->arg0, (uint32_t)m->n);
    return val_int(rc);
}

static Value sys_mount(const KernelMsg *m, uint32_t pid) {
    if (!caller_has_admin(pid)) return err_eperm();
    int rc = vfs_mount(m->arg0, m->arg1, "regfs");
    return val_int(rc);
}

static Value sys_fork(const KernelMsg *m, uint32_t sender_pid) {
    (void)m;
    /* Copy /proc/<parent>/ subtree to /proc/<child>/ */
    NexsProc *child = proc_create("fork_child", NULL, NULL);
    if (!child) return val_err(1, "fork failed");

    /* Copy parent caps to child */
    char parent_caps[REG_PATH_MAX], child_caps[REG_PATH_MAX];
    snprintf(parent_caps, sizeof(parent_caps), "/proc/%u/caps", sender_pid);
    snprintf(child_caps,  sizeof(child_caps),  "/proc/%u/caps", child->pid);
    Value caps = reg_get(parent_caps);
    reg_set(child_caps, caps, RK_READ | RK_WRITE);
    val_free(&caps);

    /* Return child PID to parent; child gets 0 via its own reply */
    return val_int((int64_t)child->pid);
}

static Value sys_exec(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    /* Load and evaluate a .nx script from VFS path */
    EvalCtx ctx;
    eval_ctx_init(&ctx);
    EvalResult r = eval_file(&ctx, m->arg0);
    Value ret = r.ret_val;
    /* Don't free ret — caller takes ownership */
    return ret;
}

static Value sys_getpid(const KernelMsg *m, uint32_t pid) {
    (void)m;
    return val_int((int64_t)pid);
}

static Value sys_exit(const KernelMsg *m, uint32_t pid) {
    (void)m;
    NexsProc *p = sched_find(pid);
    if (p) proc_destroy(p);
    return val_int(0);
}

static Value sys_sleep(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    /* m->n = milliseconds; block until timer unblocks us */
    char wake_path[REG_PATH_MAX];
    snprintf(wake_path, sizeof(wake_path), "/proc/%u/wake", pid);
    reg_set(wake_path, val_int(m->n), RK_READ | RK_WRITE);
    proc_block(wake_path);
    return val_int(0);
}

static Value sys_bind(const KernelMsg *m, uint32_t pid) {
    if (!caller_has_admin(pid)) return err_eperm();
    reg_ipc_init_queue(m->arg0, 64);
    return val_int(0);
}

static Value sys_chdir(const KernelMsg *m, uint32_t pid) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%u/cwd", pid);
    reg_set(path, val_str(m->arg0), RK_READ | RK_WRITE);
    return val_int(0);
}

static Value sys_create(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int fd = vfs_open(m->arg0, 1 | 0100); /* O_WRONLY | O_CREAT */
    return val_int(fd);
}

static Value sys_remove(const KernelMsg *m, uint32_t pid) {
    if (!caller_has_admin(pid)) return err_eperm();
    /* Remove registry key */
    reg_delete(m->arg0);
    return val_int(0);
}

static Value sys_pipe(const KernelMsg *m, uint32_t pid) {
    (void)m;
    Value seq_v = reg_get("/sys/pipe_seq");
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%u/pipe/%lld", pid, (long long)seq_v.ival);
    val_free(&seq_v);
    reg_ipc_init_queue(path, 64);
    return val_str(path);
}

static Value sys_dup(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int oldfd = (int)m->n;
    int newfd = m->arg1[0] ? (int)atoi(m->arg1) : -1;
    return val_int(vfs_dup(oldfd, newfd));
}

static Value sys_seek(const KernelMsg *m, uint32_t pid) {
    (void)pid;
    int fd     = (int)m->n;
    int64_t off = m->arg0[0] ? (int64_t)atoi(m->arg0) : 0;
    int whence = m->arg1[0] ? (int)atoi(m->arg1) : 0;
    return val_int(vfs_seek(fd, off, whence));
}

static Value sys_fstat(const KernelMsg *m, uint32_t pid) {
    return sys_stat(m, pid);
}

static Value sys_wait(const KernelMsg *m, uint32_t pid) {
    (void)m;
    char wait_path[REG_PATH_MAX];
    snprintf(wait_path, sizeof(wait_path), "/proc/%u/wait_child", pid);
    proc_block(wait_path);
    return val_int(0);
}

/* =========================================================
   DISPATCH TABLE
   ========================================================= */

typedef Value (*SyscallHandler)(const KernelMsg *, uint32_t sender_pid);

typedef struct {
    const char     *verb;
    SyscallHandler  fn;
} SyscallEntry;

static const SyscallEntry s_table[] = {
    { "open",    sys_open    },
    { "read",    sys_read    },
    { "write",   sys_write   },
    { "close",   sys_close   },
    { "stat",    sys_stat    },
    { "mkdir",   sys_mkdir   },
    { "create",  sys_create  },
    { "remove",  sys_remove  },
    { "bind",    sys_bind    },
    { "mount",   sys_mount   },
    { "fork",    sys_fork    },
    { "exec",    sys_exec    },
    { "wait",    sys_wait    },
    { "exit",    sys_exit    },
    { "chdir",   sys_chdir   },
    { "getpid",  sys_getpid  },
    { "sleep",   sys_sleep   },
    { "pipe",    sys_pipe    },
    { "dup",     sys_dup     },
    { "seek",    sys_seek    },
    { "fstat",   sys_fstat   },
};

#define SYSCALL_COUNT ((int)(sizeof(s_table) / sizeof(s_table[0])))

Value kmsg_handle(const KernelMsg *m) {
    if (m->sender_pid == 0) return val_err(1, "ESRCH");
    for (int i = 0; i < SYSCALL_COUNT; i++) {
        if (strcmp(s_table[i].verb, m->verb) == 0)
            return s_table[i].fn(m, m->sender_pid);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "unknown syscall: %s", m->verb);
    return val_err(1, buf);
}
