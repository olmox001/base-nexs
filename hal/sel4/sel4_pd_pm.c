/*
 * hal/sel4/sel4_pd_pm.c — seL4 Microkit Protection Domain: Process Manager
 * ==========================================================================
 * Runs services/pm/init.nx in isolation.
 * Same pattern as sel4_pd_fs.c — see that file for full rationale.
 *
 * MR virtual addresses (must match nexs_*arch*.system XML):
 *   SEL4_PM_REQ_VADDR  0x6_0010_0000  — root→pm  request MR (r)
 *   SEL4_PM_REP_VADDR  0x6_0011_0000  — pm→root  reply   MR (rw)
 *   SEL4_PM_LOG_VADDR  0x6_0012_0000  — pm→root  log     MR (rw)
 */

#ifdef NEXS_SEL4

#include "sel4_ipc_bridge.h"
#include "sel4_uart_daemon.c"
#include "../../runtime/include/nexs_runtime.h"
#include "../../lang/include/nexs_eval.h"
#include "../../kernel/include/nexs_msg.h"
#include "../../kernel/include/nexs_proc.h"
#include "../../kernel/include/nexs_sched.h"
#include "../../registry/include/nexs_registry.h"
#include "../../hal/include/nexs_hal.h"
#include <microkit.h>
#include <string.h>

#define SEL4_PM_REQ_VADDR  0x30100000UL
#define SEL4_PM_REP_VADDR  0x30110000UL
#define SEL4_PM_LOG_VADDR  0x30120000UL

static NexsMR    *g_req_mr = (NexsMR    *)SEL4_PM_REQ_VADDR;
static NexsMR    *g_rep_mr = (NexsMR    *)SEL4_PM_REP_VADDR;
static NexsLogMR *g_log_mr = (NexsLogMR *)SEL4_PM_LOG_VADDR;

extern const char nexs_pm_script_src[] __attribute__((weak));

/* UART shim — same pattern as sel4_pd_fs.c */
void nexs_hal_print(const char *s) { sel4_uart_daemon_puts(g_log_mr, s, SEL4_CH_LOG_PM); }
void nexs_hal_putc(char c) {
    char tmp[2] = { c, '\0' };
    sel4_uart_daemon_puts(g_log_mr, tmp, SEL4_CH_LOG_PM);
}
int nexs_hal_getc(void) { return -1; }

void init(void) {
    nexs_runtime_init();

    /* PM needs scheduler + proc table */
    sched_init();
    reg_ipc_init_queue("/services/pm/inbox", 64);

    sel4_uart_daemon_puts(g_log_mr, "[nexs_pm] PD initialized\n", SEL4_CH_LOG_PM);

    if (nexs_pm_script_src && nexs_pm_script_src[0]) {
        EvalCtx ctx;
        eval_ctx_init(&ctx);
        ctx.out = NULL;
        EvalResult r = eval_str(&ctx, nexs_pm_script_src);
        if (r.sig == CTRL_ERR)
            sel4_uart_daemon_puts(g_log_mr, "[nexs_pm] script error\n", SEL4_CH_LOG_PM);
        val_free(&r.ret_val);
    }
}

void notified(microkit_channel ch) {
    if (ch == SEL4_CH_PM) {
        Value req = val_nil();
        if (sel4_bridge_read(g_req_mr, &req) != 0) return;

        KernelMsg km;
        Value reply;
        if (kmsg_decode(&req, &km) == 0) {
            reply = kmsg_handle(&km);
        } else {
            reply = val_err(1, "pm: bad kmsg");
        }
        val_free(&req);

        sel4_bridge_write(g_rep_mr, &reply);
        val_free(&reply);
        microkit_notify(SEL4_CH_BACK_PM);
    }
}

#endif /* NEXS_SEL4 */
