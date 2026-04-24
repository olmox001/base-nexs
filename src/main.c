/*
 * main.c — NEXS Entry Point, REPL, Runtime Init
 * ================================================
 * Contiene:
 *   - nexs_runtime_init()  — inizializza tutto il runtime
 *   - nexs_print_version() — stampa versione e crediti
 *   - nexs_repl()          — REPL interattivo
 *   - main()               — entry point
 */

#include "basereg.h"

#include <string.h>

/* =========================================================
   VERSIONE
   ========================================================= */

void nexs_print_version(FILE *out) {
  fprintf(out,
          "\033[1;36mNEXS\033[0m v%d.%d.%d — Runtime Buddy/Registry + Plan 9 Syscalls\n"
          "  Pool: %dKB  MinBlock: %dB  Registry: gerarchico (regedit-style)\n"
          "  Syscalls: open create close read write seek stat pipe rfork exec\n"
          "  Ispirato a: Thompson · Ritchie · Pike · Rashid\n\n",
          NEXS_VERSION_MAJOR, NEXS_VERSION_MINOR, NEXS_VERSION_PATCH,
          POOL_SIZE / 1024, MIN_BLOCK);
}

/* =========================================================
   RUNTIME INIT
   ========================================================= */

void nexs_runtime_init(void) {
  /* 1. Azzera il pool e l'albero buddy */
  memset(memory_pool, 0, sizeof(memory_pool));
  memset(buddy_tree, 0, sizeof(buddy_tree));
  g_array_count = 0;

  /* 2. Inizializza il registro gerarchico */
  reg_init();

  /* 3. Inizializza la fd table (stdin/stdout/stderr) */
  sysio_init();

  /* 4. Registra tutti i built-in standard */
  builtins_register_all();

  /* 5. Registra i built-in Plan 9 I/O */
  sysio_register_builtins();

  /* 6. Registra i built-in Plan 9 process */
  sysproc_register_builtins();
}

/* =========================================================
   REPL
   ========================================================= */

void nexs_repl(void) {
  EvalCtx ctx;
  eval_ctx_init(&ctx);

  nexs_print_version(stdout);
  fprintf(
      stdout,
      "Comandi speciali: :exit :help :ls [path] :reg [path] :debug :version\n"
      "Sintassi: x = 42 | arr[0] = 10 | fn add(a b) { ret a+b } | out add(1 "
      "2)\n\n");

  char line[1024];
  while (1) {
    fprintf(stdout, "\033[1;32mnexs\033[0m> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
      break;
    nexs_trim(line);
    if (strlen(line) == 0)
      continue;

    /* Comandi speciali del REPL */
    if (strcmp(line, ":exit") == 0 || strcmp(line, ":q") == 0)
      break;
    if (strcmp(line, ":version") == 0) {
      nexs_print_version(stdout);
      continue;
    }
    if (strcmp(line, ":debug") == 0) {
      ctx.debug = !ctx.debug;
      fprintf(stdout, "Debug: %s\n", ctx.debug ? "ON" : "OFF");
      continue;
    }
    if (strcmp(line, ":help") == 0) {
      fprintf(
          stdout,
          "NEXS — Sintassi:\n"
          "  x = 42              # variabile intera\n"
          "  x = 3.14            # float\n"
          "  x = \"ciao\"          # stringa\n"
          "  arr[0] = 10         # array\n"
          "  del arr[0]          # cancella elemento\n"
          "  out x               # stampa\n"
          "  fn add(a b){ret a+b}# funzione\n"
          "  out add(3 4)        # chiama e stampa\n"
          "  if x > 0 { out x }  # condizionale\n"
          "  loop { ... break }  # ciclo\n"
          "  reg /env/x = 99     # scrivi registro\n"
          "  reg /env/x          # leggi registro\n"
          "  ls /fn              # lista registro\n"
          "\nPlan 9 Syscalls:\n"
          "  fd = open(\"file\" 0) # apri file (0=read,1=write,2=rdwr)\n"
          "  fd = create(\"f\" 1)  # crea file\n"
          "  s = read(fd 100)    # leggi 100 byte\n"
          "  write(fd \"data\")    # scrivi\n"
          "  close(fd)           # chiudi fd\n"
          "  seek(fd 0 0)        # posiziona\n"
          "  fstat(\"file\")       # metadata\n"
          "  remove(\"file\")      # cancella file\n"
          "  chdir(\"/tmp\")       # cambia directory\n"
          "  sleep(1000)         # dormi 1 secondo\n"
          "  exec(\"file.nx\")     # esegui file NEXS\n"
          "  pid = rfork(1)      # fork processo\n"
          "  mount(\"/a\" \"/b\" 0)  # monta registro\n"
          "  bind(\"/a\" \"/b\" 0)   # bind registro\n"
          "  unmount(\"/b\")       # smonta\n"
          "\nComandi REPL:\n"
          "  :ls [path]          # ls del registro\n"
          "  :reg [path]         # albero registro\n"
          "  :debug              # toggle debug\n"
          "  :exit               # esci\n\n"
          "Built-in: str(v) int(v) float(v) len(v) type(v) buddy_stats()\n"
          "          getpid() getwd() errstr()\n\n");
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

    /* Valuta la riga come sorgente NEXS */
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
   MAIN
   ========================================================= */

int main(int argc, char *argv[]) {
  nexs_runtime_init();

  if (argc == 1) {
    nexs_repl();
    return 0;
  }

  if (argc == 2) {
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
      nexs_print_version(stdout);
      return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
      fprintf(stdout, "Uso: nexs [opzioni] [file.nx]\n"
                       "  -v, --version   Mostra la versione\n"
                       "  -h, --help      Mostra questo aiuto\n"
                       "  <file.nx>       Esegui un file sorgente NEXS\n"
                       "  (nessun arg)    Avvia il REPL interattivo\n");
      return 0;
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

  fprintf(stderr, "Uso: nexs [file.nx]\n");
  return 1;
}
