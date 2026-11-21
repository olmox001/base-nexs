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

#include "include/nexs_line.h"
#include "include/nexs_runtime.h"

#include "../compiler/include/nexs_compiler.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "../hal/include/nexs_hal.h"
#include "../lang/include/nexs_eval.h"
#include "../lang/include/nexs_fn.h"
#include "../lang/include/nexs_lex.h"
#include "../lang/include/nexs_parse.h"
#include "../registry/include/nexs_registry.h"
#include "../sys/include/nexs_sys.h"

/* builtins_register_all is declared in lang/builtins.c — forward declare here
 */
extern void builtins_register_all(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   VERSION
   ========================================================= */

/* nexs_print_version and nexs_runtime_init live in runtime/runtime.c */

/* Validate a source without executing its system/service side effects.
 * The same lexer/parser handles .nx services, libraries and applications.
 * A finite token budget and a nesting bound protect the host test process
 * against unbounded parsing of malformed inputs.
 */
static int nexs_source_tokens_checked(const char *source) {
  Lexer lex;
  lexer_init(&lex, source);
  int nesting = 0;
  for (int i = 0; i < MAX_TOKENS; ++i) {
    const Token token = lexer_next(&lex);
    if (token.kind == TK_ERROR) {
      fprintf(stderr, "nxscript: lexer error at line %d: %s\n",
              token.line, token.text);
      return -1;
    }
    if (token.kind == TK_EOF)
      return 0;
    if (token.kind == TK_LBRACE || token.kind == TK_LPAREN ||
        token.kind == TK_LBRACKET)
      ++nesting;
    if (token.kind == TK_RBRACE || token.kind == TK_RPAREN ||
        token.kind == TK_RBRACKET)
      --nesting;
    if (nesting < 0 || nesting > 128) {
      fprintf(stderr, "nxscript: invalid or excessive nesting\n");
      return -1;
    }
  }
  fprintf(stderr, "nxscript: source exceeds MAX_TOKENS budget\n");
  return -1;
}

static int nexs_syntax_check_file(const char *path) {
  FILE *input = fopen(path, "rb");
  if (input == NULL) {
    fprintf(stderr, "nxscript: cannot open source: %s\n", path);
    return 1;
  }
  long length = -1;
  if (fseek(input, 0, SEEK_END) == 0)
    length = ftell(input);
  if (length < 0 || length > 512L * 1024L ||
      fseek(input, 0, SEEK_SET) != 0) {
    fclose(input);
    fprintf(stderr, "nxscript: invalid source size: %s\n", path);
    return 1;
  }
  char *source = malloc((size_t)length + 1U);
  if (source == NULL) {
    fclose(input);
    fprintf(stderr, "nxscript: cannot allocate source buffer\n");
    return 1;
  }
  size_t nread = fread(source, 1U, (size_t)length, input);
  int closed = fclose(input);
  source[nread] = '\0';
  if (closed != 0 || nread != (size_t)length ||
      nexs_source_tokens_checked(source) != 0) {
    free(source);
    return 1;
  }
  Lexer lexer;
  Parser parser;
  lexer_init(&lexer, source);
  parser_init(&parser, &lexer);
  ASTNode *ast = parse_program(&parser);
  int failed = parser.had_error || ast == NULL;
  if (failed)
    fprintf(stderr, "nxscript: syntax error in %s: %s\n", path,
            parser.error_msg);
  ast_free(ast);
  free(source);
  if (!failed)
    fprintf(stdout, "[syntax] PASS %s\n", path);
  return failed;
}

/* =========================================================
   BARE-METAL ENTRY POINT
   ========================================================= */

#ifdef NEXS_BAREMETAL
__attribute__((weak)) void nexs_main_baremetal(void) {
  if (nexs_runtime_init() != 0) {
    nexs_hal_print("NEXS runtime admission failed\n");
    nexs_hal_halt();
    return;
  }
  EvalCtx ctx;
  eval_ctx_init(&ctx);
  ctx.out = NULL; /* use nexs_hal_print instead of fprintf */

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
    if (r.sig == CTRL_ERR) {
      nexs_hal_print("\033[1;31m[NEXS FATAL ERROR]\033[0m\n");
      if (r.ret_val.type == TYPE_ERR && r.ret_val.err_msg) {
        nexs_hal_print(r.ret_val.err_msg);
        nexs_hal_print("\n");
      }
    }
    val_free(&r.ret_val);
  } else {
    nexs_repl();
  }
  nexs_hal_halt();
}
#endif /* NEXS_BAREMETAL */

/* =========================================================
   MAIN
   ========================================================= */

#ifdef NEXS_HOST_TOOL
__attribute__((weak)) int main(int argc, char *argv[]) {
  nexs_host_source_root_init(argc > 0 ? argv[0] : NULL);
  if (nexs_runtime_init() != 0) {
    fprintf(stderr, "nxscript: runtime admission failed\n");
    return 1;
  }

  if (argc == 1) {
    if (nexs_runtime_autoload() != 0) {
      fprintf(stderr, "nxscript: installed source admission failed\n");
      return 1;
    }
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
    fprintf(
        stdout,
        "Usage: nxscript [options] [file.nx]\n"
        "  -v, --version                  Show version\n"
        "  -h, --help                     Show this help\n"
        "  <file.nx>                      Run a NEXS source file\n"        "  --check-syntax <file.nx>       Parse without executing services\n"
        "  --compile <file.nx>            Compile file to native binary\n"
        "  --standalone-program <file.nx> Alias for --compile (bundled deps on "
        "by default)\n"
        "    --target <target>            Compilation target:\n"
        "      linux-amd64  linux-arm64\n"
        "      macos-amd64  macos-arm64  plan9-amd64\n"
        "      baremetal-arm64  baremetal-amd64\n"
        "      os1-amd64  os1-arm64\n"
        "    -o <output>                  Output binary path\n"
        "    --no-dep                     Skip dependency bundling\n"
        "                                 (exec() calls will use fopen at "
        "runtime)\n"
        "    --dep-only                   List exec() dependencies and exit\n"
        "  --codegen <file.nx> -o <out.c> Generate C embed file (table + script src)\n"
        "    --no-dep                     Skip dependency bundling\n"
        "    --hosted                     Emit main() instead of nexs_main_baremetal()\n"
        "  (no args)                      Start interactive REPL\n");
    return 0;
  }

  /* Static .nx verification uses the same parser, no eval() or exec(). */
  if (strcmp(argv[1], "--check-syntax") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Usage: nxscript --check-syntax file.nx\n");
      return 2;
    }
    return nexs_syntax_check_file(argv[2]);
  }

  /* --codegen <file.nx> -o <out.c> [--no-dep] [--hosted]
   * Generates a C embed file (embed table + nexs_script_src) without invoking
   * the full compiler.  Used by the sel4-microkit and baremetal Makefile targets
   * to populate nexs_embed_deps_table[] the same way --compile does. */
  if (strcmp(argv[1], "--codegen") == 0) {
    if (argc < 3) {
      fprintf(stderr, "nxscript: --codegen requires a source file\n");
      return 1;
    }
    const char *src_file = argv[2];
    const char *out_file = NULL;
    int no_dep   = 0;
    int is_bare  = 1; /* emit nexs_main_baremetal(), not main() */
    for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        out_file = argv[++i];
      else if (strcmp(argv[i], "--no-dep") == 0)
        no_dep = 1;
      else if (strcmp(argv[i], "--hosted") == 0)
        is_bare = 0;
    }
    if (!out_file) {
      fprintf(stderr, "nxscript: --codegen requires -o <output.c>\n");
      return 1;
    }
    fprintf(stdout, "Generating embed C: %s -> %s\n", src_file, out_file);
    int rc = nexs_codegen_ex(src_file, out_file, no_dep, is_bare);
    if (rc != 0) {
      fprintf(stderr, "nxscript: codegen failed for '%s'\n", src_file);
      return 1;
    }
    fprintf(stdout, "Done: %s\n", out_file);
    return 0;
  }

  /* --compile / --standalone-program <file.nx> [--target <t>] [-o <out>]
   * [--no-dep] [--dep-only] */
  if (strcmp(argv[1], "--compile") == 0 ||
      strcmp(argv[1], "--standalone-program") == 0) {
    if (argc < 3) {
      fprintf(stderr, "nxscript: --compile requires a source file\n");
      return 1;
    }
    const char *src_file = argv[2];
    const char *out_file = "nexs_out";
#if defined(HOST_OS_MACOS)
    CompileTarget target = TARGET_MACOS_AMD64;
#else
    CompileTarget target = TARGET_LINUX_AMD64;
#endif
    int no_dep = 0;
    int dep_only = 0;

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
          fprintf(stderr, "nxscript: unknown target '%s'\n", tname);
          fprintf(stderr,
                  "Available: linux-amd64 linux-arm64 macos-amd64 "
                  "macos-arm64 plan9-amd64 baremetal-arm64 baremetal-amd64 "
                  "os1-amd64 os1-arm64\n");
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

    fprintf(stdout, "Compiling %s → %s [target: %s%s]\n", src_file, out_file,
            target_name(target), no_dep ? ", no-dep" : " + bundled deps");
    int rc = nexs_compile_file_ex(src_file, target, out_file, no_dep);
    if (rc != 0) {
      fprintf(stderr, "nxscript: compilation failed\n");
      return 1;
    }
    fprintf(stdout, "Done: %s\n", out_file);
    return 0;
  }

  /* <file.nx> */
  if (argc == 2) {
    if (nexs_runtime_autoload() != 0) {
      fprintf(stderr, "nxscript: installed source admission failed\n");
      return 1;
    }
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

  fprintf(stderr, "Usage: nxscript [file.nx]\n");
  return 1;
}
#endif /* NEXS_HOST_TOOL */
