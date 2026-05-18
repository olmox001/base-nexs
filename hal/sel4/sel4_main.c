/*
 * hal/sel4/sel4_main.c — seL4 + Microkit Entry Point for NEXS Root PD
 * =====================================================================
 * Implements the Microkit-mandated init() and notified() callbacks.
 *
 * Single-PD mode  (default — NEXS_MULTI_PD not defined):
 *   - Identical to the original behaviour: one PD runs everything.
 *   - notified() dispatches /sys/kernel/inbox via kmsg_handle().
 *
 * Multi-PD mode   (compiled with -DNEXS_MULTI_PD):
 *   - Root PD owns the physical UART.
 *   - Service PDs (fs / pm / tty) send log text via NexsLogMR.
 *   - sel4_uart_daemon_flush() drains all log MRs when CH_LOG fires.
 *   - Cross-PD IPC requests are routed via NexsMR + microkit_notify().
 */

#include "../../core/include/nexs_common.h"
#include "../../runtime/include/nexs_runtime.h"
#include "../../lang/include/nexs_eval.h"
#include "../include/nexs_hal.h"
#include "../../kernel/include/nexs_sched.h"
#include "../../kernel/include/nexs_msg.h"
#include "../../kernel/include/nexs_vfs.h"
#include "../../kernel/include/nexs_proc.h"
#include "../../registry/include/nexs_registry.h"
#include <microkit.h>
#include <string.h>

#ifdef NEXS_SEL4_VERIFY
#include "verify/verify.h"
#endif


/* nexs_embed_deps_table[], nexs_script_src, nexs_embedded_lookup(), and
 * nexs_embed_load_all() are provided by the codegen'd embed file generated
 * via: ./nexs --codegen services/init.nx -o build/sel4-microkit/nexs_embed.c
 * Compile that file alongside this one in the sel4-microkit Makefile target. */
extern const char nexs_script_src[] __attribute__((weak));

/* =========================================================
   MULTI-PD EXTENSIONS
   =========================================================
   All multi-PD code is gated on NEXS_MULTI_PD so that the
   original single-PD build path is completely unaffected.
   ========================================================= */

#ifdef NEXS_MULTI_PD

#include "sel4_ipc_bridge.h"
#include "sel4_uart_daemon.c"

/* ── Root PD MR virtual addresses (must match .system XML) ── */

/* Log MRs — root maps these READ-ONLY, service PDs map READ-WRITE */
#define SEL4_LOG_FS_VADDR   0x30020000UL
#define SEL4_LOG_PM_VADDR   0x30120000UL
#define SEL4_LOG_TTY_VADDR  0x30220000UL

/* Request MRs — root maps READ-WRITE, service PDs map READ-ONLY */
#define SEL4_REQ_FS_VADDR   0x30000000UL
#define SEL4_REQ_PM_VADDR   0x30100000UL
#define SEL4_REQ_TTY_VADDR  0x30200000UL

/* Reply MRs — root maps READ-ONLY, service PDs map READ-WRITE */
#define SEL4_REP_FS_VADDR   0x30010000UL
#define SEL4_REP_PM_VADDR   0x30110000UL
#define SEL4_REP_TTY_VADDR  0x30210000UL

static NexsLogMR *g_log_fs  = (NexsLogMR *)SEL4_LOG_FS_VADDR;
static NexsLogMR *g_log_pm  = (NexsLogMR *)SEL4_LOG_PM_VADDR;
static NexsLogMR *g_log_tty = (NexsLogMR *)SEL4_LOG_TTY_VADDR;

static NexsMR *g_req_fs  = (NexsMR *)SEL4_REQ_FS_VADDR;
static NexsMR *g_req_pm  = (NexsMR *)SEL4_REQ_PM_VADDR;
static NexsMR *g_req_tty = (NexsMR *)SEL4_REQ_TTY_VADDR;

static NexsMR *g_rep_fs  = (NexsMR *)SEL4_REP_FS_VADDR;
static NexsMR *g_rep_pm  = (NexsMR *)SEL4_REP_PM_VADDR;
static NexsMR *g_rep_tty = (NexsMR *)SEL4_REP_TTY_VADDR;

/*
 * route_to_service — forward a KernelMsg to the appropriate service PD.
 * Returns 1 if the message was routed (caller should NOT process locally),
 * returns 0 if the message belongs to the root PD's own dispatch.
 *
 * Routing table:
 *   /services/fs/*  → nexs_fs  (CH_FS)
 *   /services/pm/*  → nexs_pm  (CH_PM)
 *   /services/tty/* → nexs_tty (CH_TTY)
 */
static int route_to_service(const KernelMsg *km, const Value *raw_msg) {
    const char *dst = km->arg0;   /* primary path argument */

    NexsMR         *req_mr = NULL;
    microkit_channel ch    = 0;

    if (strncmp(dst, "/services/fs/",  13) == 0) { req_mr = g_req_fs;  ch = SEL4_CH_FS;  }
    else if (strncmp(dst, "/services/pm/",  13) == 0) { req_mr = g_req_pm;  ch = SEL4_CH_PM;  }
    else if (strncmp(dst, "/services/tty/", 14) == 0) { req_mr = g_req_tty; ch = SEL4_CH_TTY; }
    else return 0;   /* local */

    sel4_bridge_write(req_mr, raw_msg);
    microkit_notify(ch);
    return 1;
}

/*
 * collect_reply — wait for a reply from a service PD (blocking spin).
 * Under Microkit the root PD is the only "caller" so a tight spin
 * is acceptable for the initial version.  A future iteration can use
 * a deferred-reply pattern with a per-pid reply MR.
 */
static Value collect_reply(NexsMR *rep_mr) {
    /* Spin until the service PD writes the reply.
     * Upper bound: 1 M iterations ≈ a few ms at 1 GHz → safe for init. */
    for (volatile int i = 0; i < 1000000; i++) {
        if (rep_mr->magic == SEL4_MR_MAGIC && rep_mr->len > 0) {
            Value reply = val_nil();
            sel4_bridge_read(rep_mr, &reply);
            return reply;
        }
    }
    return val_err(1, "service timeout");
}

#endif /* NEXS_MULTI_PD */

/* =========================================================
   init — called once by Microkit
   ========================================================= */

void init(void) {
    nexs_hal_init();
    nexs_runtime_init();

#ifdef NEXS_SEL4_VERIFY
    nexs_sel4_verify_all();
#endif

    sched_init();
    vfs_init();
    reg_ipc_init_queue("/sys/kernel/inbox", 64);

#ifdef NEXS_MULTI_PD
    /* Register log MRs with the UART daemon */
    sel4_uart_daemon_init(g_log_fs,  "[fs] ",  SEL4_CH_LOG_FS);
    sel4_uart_daemon_init(g_log_pm,  "[pm] ",  SEL4_CH_LOG_PM);
    sel4_uart_daemon_init(g_log_tty, "[tty]", SEL4_CH_LOG_TTY);
    nexs_hal_print("[NEXS] multi-PD root ready — uart daemon active\n");
#endif

    EvalCtx ctx;
    eval_ctx_init(&ctx);
    ctx.out = NULL;

    if (nexs_script_src && nexs_script_src[0]) {
        EvalResult r = eval_str(&ctx, nexs_script_src);
        if (r.sig == CTRL_ERR) {
            nexs_hal_print("\033[1;31m[NEXS AOT EXECUTION ERROR]\033[0m\n");
            if (r.ret_val.type == TYPE_ERR && r.ret_val.err_msg) {
                nexs_hal_print(r.ret_val.err_msg);
                nexs_hal_print("\n");
            }
        }
        val_free(&r.ret_val);
    } else {
        nexs_hal_print("[NEXS] No AOT script provided. Evaluating fallback test script...\n");
        const char *fallback =
            "x = 10\n"
            "y = 32\n"
            "out \"Executing fallback: x + y =\"\n"
            "out (x + y)\n";
        EvalResult r = eval_str(&ctx, fallback);
        val_free(&r.ret_val);

        nexs_hal_print("[NEXS] Starting interactive NEXS shell (REPL)...\n");
        nexs_repl();
    }

    nexs_hal_print("[NEXS] Protection Domain initialization complete. Halting system...\n");
    nexs_hal_halt();
}

/* =========================================================
   notified — IPC / notification handler
   ========================================================= */

void notified(microkit_channel ch) {

#ifdef NEXS_MULTI_PD
    /* ── UART daemon: drain log MRs from all service PDs ── */
    if (ch == SEL4_CH_LOG_FS || ch == SEL4_CH_LOG_PM || ch == SEL4_CH_LOG_TTY) {
        sel4_uart_daemon_flush_channel(ch);
        return;
    }

    /* ── Service PD replies ── */
    if (ch == SEL4_CH_BACK_FS || ch == SEL4_CH_BACK_PM || ch == SEL4_CH_BACK_TTY) {
        /* Reply already collected synchronously in route_to_service path.
         * This notification is a wake-up signal for deferred-reply scenarios;
         * in the current spin-wait model it is a no-op. */
        return;
    }
#endif


    /* ── Standard in-process kernel inbox ── */
    const char *inbox = "/sys/kernel/inbox";
    Value msg = val_nil();
    while (reg_ipc_recv(inbox, &msg) == 0) {
        KernelMsg km;
        if (kmsg_decode(&msg, &km) == 0) {

#ifdef NEXS_MULTI_PD
            /* Try to route cross-PD before local dispatch */
            if (route_to_service(&km, &msg)) {
                NexsMR *rep_mr = NULL;
                if (km.arg0[12] == 'f') rep_mr = g_rep_fs;   /* /services/fs/  */
                else if (km.arg0[12] == 'p') rep_mr = g_rep_pm;   /* /services/pm/  */
                else if (km.arg0[12] == 't') rep_mr = g_rep_tty;  /* /services/tty/ */

                if (rep_mr && km.sender_pid > 0) {
                    Value result = collect_reply(rep_mr);
                    char reply[REG_PATH_MAX];
                    snprintf(reply, sizeof(reply), "/proc/%u/inbox", km.sender_pid);
                    reg_ipc_send(reply, result);
                    proc_unblock_by_msg(reply);
                    val_free(&result);
                }
                val_free(&msg);
                msg = val_nil();
                continue;
            }
#endif
            /* Local dispatch */
            val_free(&msg);
            Value result = kmsg_handle(&km);
            if (km.sender_pid > 0) {
                char reply[REG_PATH_MAX];
                snprintf(reply, sizeof(reply), "/proc/%u/inbox", km.sender_pid);
                reg_ipc_send(reply, result);
                proc_unblock_by_msg(reply);
            }
            val_free(&result);
        } else {
            val_free(&msg);
        }
        msg = val_nil();
    }
    val_free(&msg);
}
