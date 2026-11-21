/* Canonical NEXS interactive REPL implementation.
 * Shared by hosted CLI, OS1 userland, and freestanding program entrypoints.
 * TTY ownership belongs to the platform console provider, not this REPL.
 */
#include "include/nexs_line.h"
#include "include/nexs_runtime.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "../lang/include/nexs_eval.h"
#include "../lang/include/nexs_fn.h"
#include "../registry/include/nexs_registry.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
   REPL
   ========================================================= */

static int g_ast_debug = 0; /* toggle with :ast command */

void nexs_repl(void) {
  EvalCtx ctx;
  eval_ctx_init(&ctx);

  nexs_print_version(ctx.out);
  nexs_fprintf(
      ctx.out,
      "Special commands: :exit :help :ls [path] :reg [path] :debug :version\n"
      "                  :fn  :ptr /path  :ipc /path  :ast\n"
      "Syntax: x=42 | arr[0]=10 | fn add(a b){ret a+b} | out add(1 2)\n\n");

  /* Each process owns exactly one interactive REPL. The editor retains a
   * 256 x 4 KiB history ring and must NOT live on the 1 MiB OS1 user stack.
   * Static storage is process-local BSS, reset for each REPL invocation and
   * never shared with another process or graphical TTY session. */
  static NxLineEditor le;
  nexs_line_init(&le);
  char line[1024];
  while (1) {
    if (nexs_line_read(&le, "\033[1;32mnexs\033[0m> ", line, sizeof(line)) < 0)
      break;
    nexs_trim(line);
    if (strlen(line) == 0)
      continue;
    /* Every interactive command receives one fresh NXOLMO work budget.
     * A buggy script loop must return to its TTY instead of monopolizing
     * the bootstrap CPU. This is a policy bound, not a hardware duration. */
    if (eval_ctx_set_fuel_budget(&ctx, UINT64_C(4096)) != 0) {
      nexs_fprintf(ctx.err, "[REPL] execution budget unavailable\n");
      continue;
    }

    nexs_line_add_history(&le, line);
    if (strcmp(line, ":exit") == 0 || strcmp(line, ":q") == 0)
      break;

    if (strcmp(line, ":version") == 0) {
      nexs_print_version(ctx.out);
      continue;
    }

    if (strcmp(line, ":debug") == 0) {
      ctx.debug = !ctx.debug;
      g_nexs_debug = ctx.debug;
      nexs_fprintf(ctx.out, "Debug: %s\n", ctx.debug ? "ON" : "OFF");
      continue;
    }

    if (strcmp(line, ":ast") == 0) {
      g_ast_debug = !g_ast_debug;
      nexs_fprintf(ctx.out, "AST debug: %s\n", g_ast_debug ? "ON" : "OFF");
      continue;
    }

    if (strcmp(line, ":fn") == 0) {
      /* Introspection enumerates registry-owned callable objects;
       * never a duplicate process-wide table. */
      const char *roots[] = {REG_FN, REG_SYS};
      nexs_fprintf(ctx.out, "\n[CALLABLE REGISTRY]\n");
      for (size_t root_idx = 0; root_idx < 2U; ++root_idx) {
        RegKey *root = reg_lookup(roots[root_idx]);
        if (root == NULL)
          continue;
        for (RegKey *key = root->children; key != NULL; key = key->next) {
          if (key->val.type != TYPE_FN || key->val.data == NULL)
            continue;
          NexsFnDef *def = (NexsFnDef *)key->val.data;
          nexs_fprintf(ctx.out, "  %s  ref=%llu\n",
                       key->path, (unsigned long long)def->ref_count);
        }
      }
      nexs_fprintf(ctx.out, "\n");
      continue;
    }

    if (strncmp(line, ":ptr ", 5) == 0) {
      const char *path = line + 5;
      fprintf(stdout, "[PTR CHAIN] %s\n", path);
      /* Follow and print pointer chain */
      char cur[REG_PATH_MAX];
      strncpy(cur, path, REG_PATH_MAX - 1);
      cur[REG_PATH_MAX - 1] = '\0';
      int hops = 0;
      while (hops < 32) {
        RegKey *k = reg_lookup(cur);
        if (!k) {
          fprintf(stdout, "  -> (not found)\n");
          break;
        }
        fprintf(stdout, "  %s [%s]", cur, val_type_name(k->val.type));
        if (k->val.type == TYPE_PTR && k->val.data) {
          fprintf(stdout, " -> %s\n", (char *)k->val.data);
          strncpy(cur, (char *)k->val.data, REG_PATH_MAX - 1);
          cur[REG_PATH_MAX - 1] = '\0';
          hops++;
        } else {
          fprintf(stdout, " = ");
          val_print(&k->val, stdout);
          fprintf(stdout, "\n");
          break;
        }
      }
      if (hops >= 32)
        fprintf(stdout, "  (chain too deep, possible cycle)\n");
      fprintf(stdout, "\n");
      continue;
    }

    if (strncmp(line, ":ipc ", 5) == 0) {
      const char *path = line + 5;
      RegKey *k = reg_lookup(path);
      fprintf(stdout, "[IPC] %s: ", path);
      if (!k) {
        fprintf(stdout, "(key not found)\n");
      } else if (!k->queue) {
        fprintf(stdout, "(no queue)\n");
      } else {
        fprintf(stdout, "count=%d max=%d\n", k->queue->count,
                k->queue->max_count);
      }
      fprintf(stdout, "\n");
      continue;
    }

    if (strcmp(line, ":help") == 0) {
      fprintf(stdout,
              "NEXS — Syntax:\n"
              "  x = 42                # integer variable\n"
              "  x = 3.14              # float\n"
              "  x = \"hello\"           # string\n"
              "  arr[0] = 10           # array\n"
              "  del arr[0]            # delete element\n"
              "  out x                 # print\n"
              "  fn add(a b){ret a+b}  # function declaration\n"
              "  out add(3 4)          # call and print\n"
              "  if x > 0 { out x }    # conditional\n"
              "  loop { ... break }    # loop\n"
              "  reg /env/x = 99       # write registry\n"
              "  reg /env/x            # read registry\n"
              "  ls /fn                # list registry\n"
              "  ptr /a = /b           # create pointer /a -> /b\n"
              "  deref /a              # follow pointer chain at /a\n"
              "  sendmessage /q 42     # send message to IPC queue\n"
              "  receivemessage /q     # receive from IPC queue\n"
              "  msgpending /q         # count pending messages\n"
              "\nPlan 9 Syscalls:\n"
              "  fd = open(\"f\" 0)  read=0 write=1 rdwr=2\n"
              "  s  = read(fd 100)\n"
              "  write(fd \"data\")\n"
              "  close(fd)\n"
              "  sleep(1000)\n"
              "  pid = rfork(1)\n"
              "\nREPL commands:\n"
              "  :ls [path]  :reg [path]  :cd [path]  :fn  :debug  :ast\n"
              "  :ptr /path  :ipc /path   :exit\n\n");
      continue;
    }



    if (strncmp(line, ":ls", 3) == 0 || strncmp(line, ":cd", 3) == 0) {
      /* Route special commands through the canonical language parser. */
      EvalResult r = eval_str(&ctx, line + 1);
      if (r.sig == CTRL_ERR) {
        fprintf(stderr, "\033[1;31m[ERR]\033[0m ");
        val_print(&r.ret_val, stderr);
        fprintf(stderr, "\n");
      } else if (r.sig == CTRL_NONE && r.ret_val.type != TYPE_NIL) {
        fprintf(stdout, "= ");
        val_print(&r.ret_val, stdout);
        fprintf(stdout, "\n");
      }
      val_free(&r.ret_val);
      continue;
    }

    if (strncmp(line, ":reg", 4) == 0) {
      /* Route registry inspection through the ordinary language builtin. */
      const char *path = strlen(line) > 5 ? line + 5 : "";
      char cmd[REG_PATH_MAX + 32];
      if (path[0] == '\0') {
        snprintf(cmd, sizeof(cmd), "print_reg(\"/\")");
      } else if (path[0] != '/') {
        char target[REG_PATH_MAX];
        REG_PATH(target, ctx.scope, path);
        snprintf(cmd, sizeof(cmd), "print_reg(\"%s\")", target);
      } else {
        snprintf(cmd, sizeof(cmd), "print_reg(\"%s\")", path);
      }
      EvalResult r = eval_str(&ctx, cmd);
      if (r.sig == CTRL_ERR) {
        fprintf(stderr, "\033[1;31m[ERR]\033[0m ");
        val_print(&r.ret_val, stderr);
        fprintf(stderr, "\n");
      }
      val_free(&r.ret_val);
      continue;
    }

    /* Evaluate as NEXS source */
    EvalResult r = eval_str(&ctx, line);
    if (r.sig == CTRL_ERR) {
      fprintf(stderr, "\033[1;31m[ERR]\033[0m ");
      val_print(&r.ret_val, stderr);
      fprintf(stderr, "\n");
    } else if (r.sig == CTRL_NONE && r.ret_val.type != TYPE_NIL) {
      fprintf(stdout, "= ");
      val_print(&r.ret_val, stdout);
      fprintf(stdout, "\n");
    }
    val_free(&r.ret_val);
  }
  fprintf(stdout, "\nBye.\n");
}
