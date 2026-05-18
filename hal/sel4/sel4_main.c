/*
 * hal/sel4/sel4_main.c — seL4 + Microkit Entry Point for NEXS PD
 * ===============================================================
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

/* nexs_embed_deps_table[], nexs_script_src, nexs_embedded_lookup(), and
 * nexs_embed_load_all() are provided by the codegen'd embed file generated
 * via: ./nexs --codegen services/init.nx -o build/sel4-microkit/nexs_embed.c
 * Compile that file alongside this one in the sel4-microkit Makefile target. */
extern const char nexs_script_src[] __attribute__((weak));

void init(void) {
    nexs_hal_init();
    nexs_runtime_init();

    sched_init();
    vfs_init();
    reg_ipc_init_queue("/sys/kernel/inbox", 64);

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

    nexs_hal_print("[NEXS] Protection Domain initialization complete.\n");
}

void notified(microkit_channel ch) {
    (void)ch;
    const char *inbox = "/sys/kernel/inbox";
    Value msg = val_nil();
    while (reg_ipc_recv(inbox, &msg) == 0) {
        KernelMsg km;
        if (kmsg_decode(&msg, &km) == 0) {
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
