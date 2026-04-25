/*
 * runtime/main.c — NEXS Entry Point, REPL, Runtime Init
 * ========================================================
 * Contains:
 *   nexs_runtime_init()   — initialise the full runtime
 *   nexs_print_version()  — print version and credits
 *   nexs_repl()           — interactive REPL
 *   main()                — CLI entry point (file eval + compile mode)
 *   nexs_main_baremetal() — bare-metal entry point (NEXS_BAREMETAL only)
 */

#include "include/nexs_runtime.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../registry/include/nexs_registry.h"
#include "../lang/include/nexs_fn.h"
#include "../lang/include/nexs_eval.h"
#include "../sys/include/nexs_sys.h"
#include "../compiler/include/nexs_compiler.h"
#include "../hal/include/nexs_hal.h"

/* builtins_register_all is declared in lang/builtins.c — forward declare here */
extern void builtins_register_all(void);

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================
   VERSION
   ========================================================= */

/* nexs_print_version and nexs_runtime_init live in runtime/runtime.c */

/* =========================================================
   REPL
   ========================================================= */

static int g_ast_debug = 0; /* toggle with :ast command */

void nexs_repl(void) {
  EvalCtx ctx;
  eval_ctx_init(&ctx);

  nexs_print_version(stdout);
  fprintf(stdout,
          "Special commands: :exit :help :ls [path] :reg [path] :debug :version\n"
          "                  :fn  :ptr /path  :ipc /path  :ast\n"
          "Syntax: x=42 | arr[0]=10 | fn add(a b){ret a+b} | out add(1 2)\n\n");

  char line[1024];
  while (1) {
    fprintf(stdout, "\033[1;32mnexs\033[0m> ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) break;
    nexs_trim(line);
    if (strlen(line) == 0) continue;

    if (strcmp(line, ":exit") == 0 || strcmp(line, ":q") == 0) break;

    if (strcmp(line, ":version") == 0) { nexs_print_version(stdout); continue; }

    if (strcmp(line, ":debug") == 0) {
      ctx.debug = !ctx.debug;
      fprintf(stdout, "Debug: %s\n", ctx.debug ? "ON" : "OFF");
      continue;
    }

    if (strcmp(line, ":ast") == 0) {
      g_ast_debug = !g_ast_debug;
      fprintf(stdout, "AST debug: %s\n", g_ast_debug ? "ON" : "OFF");
      continue;
    }

    if (strcmp(line, ":fn") == 0) {
      fprintf(stdout, "\n[FN TABLE] %d functions registered:\n", g_fn_count);
      for (int i = 0; i < g_fn_count; i++) {
        NexsFnDef *def = &g_fn_table[i];
        if (def->is_builtin) {
          if (def->signature[0])
            fprintf(stdout, "  [%d] %s  ref=%d\n", i, def->signature, def->ref_count);
          else
            fprintf(stdout, "  [%d] <builtin: %s(...)>  ref=%d\n", i, def->name, def->ref_count);
        } else {
          fprintf(stdout, "  [%d] fn %s(", i, def->name);
          for (int j = 0; j < def->n_params; j++)
            fprintf(stdout, "%s%s", j > 0 ? " " : "", def->params[j]);
          fprintf(stdout, ")  ref=%d\n", def->ref_count);
        }
      }
      fprintf(stdout, "\n");
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
        if (!k) { fprintf(stdout, "  -> (not found)\n"); break; }
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
      if (hops >= 32) fprintf(stdout, "  (chain too deep, possible cycle)\n");
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
        fprintf(stdout, "count=%d max=%d\n", k->queue->count, k->queue->max_count);
      }
      fprintf(stdout, "\n");
      continue;
    }

    if (strcmp(line, ":help") == 0 || strcmp(line, "help") == 0) {
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
              "  :ls [path]  :reg [path]  :fn  :debug  :ast\n"
              "  :ptr /path  :ipc /path   :exit\n\n");
      continue;
    }

    if (strncmp(line, ":ls", 3) == 0) {
      const char *path = strlen(line) > 4 ? line + 4 : "/";
      reg_ls(path, stdout);
      continue;
    }

    if (strncmp(line, ":reg", 4) == 0) {
      const char *path = strlen(line) > 5 ? line + 5 : "/";
      reg_ls_recursive(path, stdout, 0);
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

/* =========================================================
   BARE-METAL ENTRY POINT
   ========================================================= */

#ifdef NEXS_BAREMETAL
void nexs_main_baremetal(void) {
  nexs_runtime_init();
  EvalCtx ctx;
  eval_ctx_init(&ctx);
  ctx.out = stdout; /* libc_stub maps stdout to nexs_hal_print */

  nexs_hal_print("NEXS v" NEXS_VERSION_STR " baremetal\n");

  /*
   * In a compiled bare-metal program, nexs_script_src is defined by
   * the code generator (compiler/codegen.c).  Declare it as a weak
   * symbol so that bare-metal REPL builds that do not provide a script
   * still link cleanly.
   */
  extern const char nexs_script_src[] __attribute__((weak));
  if (nexs_script_src && nexs_script_src[0]) {
    EvalResult r = eval_str(&ctx, nexs_script_src);
    (void)r;
  } else {
    nexs_repl();
  }
  /* Spin forever */
  while (1) {}
}
#endif /* NEXS_BAREMETAL */

/* =========================================================
   MAIN
   ========================================================= */

int main(int argc, char *argv[]) {
  nexs_runtime_init();

  if (argc == 1) {
    nexs_repl();
    return 0;
  }

  /* --version / -v */
  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
    nexs_print_version(stdout);
    return 0;
  }

  /* --help / -h */
  if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    fprintf(stdout,
            "Usage: nexs [options] [file.nx]\n"
            "  -v, --version                  Show version\n"
            "  -h, --help                     Show this help\n"
            "  <file.nx>                      Run a NEXS source file\n"
            "  --compile <file.nx>            Compile file to native binary\n"
            "    --target <target>            Compilation target:\n"
            "      linux-amd64  linux-arm64\n"
            "      macos-amd64  macos-arm64  plan9-amd64\n"
            "      baremetal-arm64  baremetal-amd64\n"
            "    -o <output>                  Output binary path\n"
            "    --no-dep                     Skip dependency bundling\n"
            "                                 (exec() calls will use fopen at runtime)\n"
            "    --dep-only                   List exec() dependencies and exit\n"
            "  (no args)                      Start interactive REPL\n");
    return 0;
  }

  /* --compile or --standalone-program <file.nx> [--target <t>] [-o <out>] [--no-dep] [--dep-only] */
  if (strcmp(argv[1], "--compile") == 0 || strcmp(argv[1], "--standalone-program") == 0) {
    if (argc < 3) {
      fprintf(stderr, "nexs: --compile requires a source file\n");
      return 1;
    }
    const char *src_file  = argv[2];
    const char *out_file  = "nexs_out";
    CompileTarget target  = TARGET_LINUX_AMD64;
    int no_dep            = 0;
    int dep_only          = 0;

    for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
        const char *tname = argv[++i];
        int found = 0;
        for (int t = 0; t < TARGET_COUNT; t++) {
          if (strcmp(target_name((CompileTarget)t), tname) == 0) {
            target = (CompileTarget)t;
            found = 1;
            break;
          }
        }
        if (!found) {
          fprintf(stderr, "nexs: unknown target '%s'\n", tname);
          fprintf(stderr, "Available: linux-amd64 linux-arm64 macos-amd64 "
                          "macos-arm64 plan9-amd64 baremetal-arm64 baremetal-amd64\n");
          return 1;
        }
      } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
        out_file = argv[++i];
      } else if (strcmp(argv[i], "--no-dep") == 0) {
        no_dep = 1;
      } else if (strcmp(argv[i], "--dep-only") == 0) {
        dep_only = 1;
      }
    }

    /* --dep-only: scan and print dependencies, then exit */
    if (dep_only) {
      NexsDepEntry deps[NEXS_MAX_DEPS];
      int n = nexs_scan_deps(src_file, deps, NEXS_MAX_DEPS);
      if (n == 0) {
        fprintf(stdout, "(no exec() dependencies found in '%s')\n", src_file);
      } else {
        fprintf(stdout, "Dependencies of '%s' (%d):\n", src_file, n);
        for (int i = 0; i < n; i++)
          fprintf(stdout, "  [%d] %s%s\n", i, deps[i].path,
                  deps[i].src ? "" : "  [UNREADABLE]");
        nexs_free_deps(deps, n);
      }
      return 0;
    }

    fprintf(stdout, "Compiling %s → %s [target: %s%s]\n",
            src_file, out_file, target_name(target),
            no_dep ? ", no-dep" : " + bundled deps");
    int rc = nexs_compile_file_ex(src_file, target, out_file, no_dep);
    if (rc != 0) {
      fprintf(stderr, "nexs: compilation failed\n");
      return 1;
    }
    fprintf(stdout, "Done: %s\n", out_file);
    return 0;
  }

  /* <file.nx> */
  if (argc == 2) {
    EvalCtx ctx;
    eval_ctx_init(&ctx);
    EvalResult r = eval_file(&ctx, argv[1]);
    if (r.sig == CTRL_ERR) {
      fprintf(stderr, "\033[1;31m[ERR]\033[0m ");
      val_print(&r.ret_val, stderr);
      fprintf(stderr, "\n");
      val_free(&r.ret_val);
      return 1;
    }
    val_free(&r.ret_val);
    return 0;
  }

  fprintf(stderr, "Usage: nexs [file.nx]\n");
  return 1;
}
