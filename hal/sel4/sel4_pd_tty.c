/*
 * hal/sel4/sel4_pd_tty.c — seL4 Microkit Protection Domain: TTY
 * ==============================================================
 * Runs services/tty/init.nx in isolation.
 * Same pattern as sel4_pd_fs.c — see that file for full rationale.
 *
 * The TTY PD handles console I/O requests from user processes.
 * It has no physical UART; read requests to the root PD are
 * forwarded via a dedicated request channel.
 *
 * MR virtual addresses (must match nexs_*arch*.system XML):
 *   SEL4_TTY_REQ_VADDR  0x6_0020_0000  — root→tty request MR (r)
 *   SEL4_TTY_REP_VADDR  0x6_0021_0000  — tty→root reply   MR (rw)
 *   SEL4_TTY_LOG_VADDR  0x6_0022_0000  — tty→root log     MR (rw)
 */

#ifdef NEXS_SEL4

#include "sel4_ipc_bridge.h"
#include "sel4_uart_daemon.c"
#include "../../runtime/include/nexs_runtime.h"
#include "../../lang/include/nexs_eval.h"
#include "../../kernel/include/nexs_msg.h"
#include "../../registry/include/nexs_registry.h"
#include "../../hal/include/nexs_hal.h"
#include <microkit.h>
#include <string.h>

#define SEL4_TTY_REQ_VADDR  0x30200000UL
#define SEL4_TTY_REP_VADDR  0x30210000UL
#define SEL4_TTY_LOG_VADDR  0x30220000UL

static NexsMR    *g_req_mr = (NexsMR    *)SEL4_TTY_REQ_VADDR;
static NexsMR    *g_rep_mr = (NexsMR    *)SEL4_TTY_REP_VADDR;
static NexsLogMR *g_log_mr = (NexsLogMR *)SEL4_TTY_LOG_VADDR;

extern const char nexs_tty_script_src[] __attribute__((weak));

/* UART shim */
void nexs_hal_print(const char *s) { sel4_uart_daemon_puts(g_log_mr, s, SEL4_CH_LOG_TTY); }
void nexs_hal_putc(char c) {
    char tmp[2] = { c, '\0' };
    sel4_uart_daemon_puts(g_log_mr, tmp, SEL4_CH_LOG_TTY);
}
int nexs_hal_getc(void) { return -1; } /* TTY reads handled via IPC from root */

void init(void) {
    nexs_runtime_init();

    reg_ipc_init_queue("/services/tty/inbox", 64);

    sel4_uart_daemon_puts(g_log_mr, "[nexs_tty] PD initialized\n", SEL4_CH_LOG_TTY);

    if (nexs_tty_script_src && nexs_tty_script_src[0]) {
        EvalCtx ctx;
        eval_ctx_init(&ctx);
        ctx.out = NULL;
        EvalResult r = eval_str(&ctx, nexs_tty_script_src);
        if (r.sig == CTRL_ERR)
            sel4_uart_daemon_puts(g_log_mr, "[nexs_tty] script error\n", SEL4_CH_LOG_TTY);
        val_free(&r.ret_val);
    }
}

void notified(microkit_channel ch) {
    if (ch == SEL4_CH_TTY) {
        Value req = val_nil();
        if (sel4_bridge_read(g_req_mr, &req) != 0) return;

        KernelMsg km;
        Value reply;
        if (kmsg_decode(&req, &km) == 0) {
            /* TTY-specific: intercept "write" to stdout as print via daemon */
            if (km.verb[0] == 'w' && km.verb[1] == 'r') {   /* "write" */
                sel4_uart_daemon_puts(g_log_mr, km.arg1, SEL4_CH_LOG_TTY);
                reply = val_int((int64_t)strlen(km.arg1));
            } else {
                reply = kmsg_handle(&km);
            }
        } else {
            reply = val_err(1, "tty: bad kmsg");
        }
        val_free(&req);

        sel4_bridge_write(g_rep_mr, &reply);
        val_free(&reply);
        microkit_notify(SEL4_CH_BACK_TTY);
    }
}

#endif /* NEXS_SEL4 */
