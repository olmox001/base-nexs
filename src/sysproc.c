/*
 * sysproc.c — Plan 9-style Process Control Syscalls
 * ====================================================
 * Implementazione delle syscall di controllo processo:
 *   sleep, exec, exits, alarm, rfork, await
 */

#include "basereg.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* =========================================================
   SYSCALL IMPLEMENTATIONS
   ========================================================= */

/*
 * nexs_sleep — dorme per msec millisecondi.
 *
 * In Plan 9 sleep(2) prende millisecondi.
 * Ritorna 0 se completato, -1 se interrotto.
 */
int nexs_sleep(int msec) {
  if (msec <= 0)
    return 0;
  usleep((useconds_t)msec * 1000);
  return 0;
}

/*
 * nexs_exec — esegue un file NEXS (sostituisce il contesto corrente).
 *
 * A differenza di exec(2) di Plan 9 che sostituisce il processo,
 * qui eseguiamo il file nel contesto di valutazione corrente.
 */
int nexs_exec(EvalCtx *ctx, const char *path) {
  if (!ctx || !path)
    return -1;
  EvalResult r = eval_file(ctx, path);
  if (r.sig == CTRL_ERR) {
    val_print(&r.ret_val, ctx->err);
    fprintf(ctx->err, "\n");
    val_free(&r.ret_val);
    return -1;
  }
  val_free(&r.ret_val);
  return 0;
}

/*
 * nexs_exits — termina il processo con un messaggio di status.
 *
 * In Plan 9 exits(2) prende una stringa di status (NULL = success).
 * Converti: "" o NULL → exit(0), altrimenti exit(1).
 */
void nexs_exits(const char *status) {
  if (!status || status[0] == '\0') {
    exit(0);
  }
  fprintf(stderr, "[exits] %s\n", status);
  exit(1);
}

/*
 * nexs_alarm — imposta un timer.
 *
 * In Plan 9 alarm(2) imposta un allarme in millisecondi.
 * Ritorna il tempo rimanente del vecchio allarme.
 * Su POSIX usiamo alarm(2) che lavora in secondi.
 */
int nexs_alarm(int msec) {
  unsigned int secs = (msec > 0) ? (unsigned int)((msec + 999) / 1000) : 0;
  unsigned int old = alarm(secs);
  return (int)(old * 1000);
}

/*
 * nexs_rfork — fork del processo con flags.
 *
 * Flags:
 *   NEXS_RFPROC   — crea un nuovo processo (fork)
 *   NEXS_RFNOWAIT — il figlio non è attendibile
 *   NEXS_RFNAMEG  — nuovo namespace group (non implementabile direttamente)
 *   NEXS_RFMEM    — condividi memoria
 *   NEXS_RFFDG    — copia fd table
 *
 * Ritorna: 0 nel figlio, PID nel padre, -1 in caso di errore.
 */
int nexs_rfork(int flags) {
  if (!(flags & NEXS_RFPROC)) {
    /* Senza RFPROC, modifica solo gli attributi del processo corrente.
     * Per ora non facciamo nulla (namespace isolation richiederebbe
     * un'implementazione più complessa). */
    return 0;
  }

  pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    /* Figlio */
    if (flags & NEXS_RFFDG) {
      /* La fd table è già copiata dal fork */
    }
    return 0;
  }
  /* Padre */
  if (flags & NEXS_RFNOWAIT) {
    /* Double fork per evitare zombie */
    /* In un'implementazione completa useremmo SIGCHLD SIG_IGN */
    signal(SIGCHLD, SIG_IGN);
  }
  return (int)pid;
}

/*
 * nexs_await — attende la terminazione di un processo figlio.
 *
 * In Plan 9 await(2) ritorna una stringa con il PID e lo status.
 * Noi scriviamo in buf: "pid status"
 * Ritorna il numero di byte scritti, o -1 se non ci sono figli.
 */
int nexs_await(char *buf, int nbuf) {
  if (!buf || nbuf <= 0)
    return -1;

  int status;
  pid_t pid = waitpid(-1, &status, 0);
  if (pid < 0) {
    strncpy(buf, "", (size_t)nbuf);
    return -1;
  }

  int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  int written = snprintf(buf, (size_t)nbuf, "%d %d", (int)pid, exit_status);
  return written;
}

/* =========================================================
   BUILT-IN REGISTRATION
   ========================================================= */

static Value bi_sleep(Value *args, int n) {
  if (n < 1)
    return val_err(4, "sleep: richiede millisecondi");
  int msec = (int)val_to_int(&args[0]);
  return val_int(nexs_sleep(msec));
}

static Value bi_exec(Value *args, int n) {
  if (n < 1)
    return val_err(4, "exec: richiede un path");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "exec: argomento deve essere una stringa");
  /* Creiamo un contesto temporaneo per l'esecuzione */
  EvalCtx ctx;
  eval_ctx_init(&ctx);
  int result = nexs_exec(&ctx, (char *)args[0].data);
  return val_int(result);
}

static Value bi_exits(Value *args, int n) {
  const char *status = NULL;
  if (n >= 1 && args[0].type == TYPE_STR && args[0].data)
    status = (char *)args[0].data;
  nexs_exits(status);
  /* Non raggiunto */
  return val_nil();
}

static Value bi_alarm(Value *args, int n) {
  if (n < 1)
    return val_err(4, "alarm: richiede millisecondi");
  int msec = (int)val_to_int(&args[0]);
  return val_int(nexs_alarm(msec));
}

static Value bi_rfork(Value *args, int n) {
  int flags = (n >= 1) ? (int)val_to_int(&args[0]) : NEXS_RFPROC;
  return val_int(nexs_rfork(flags));
}

static Value bi_await(Value *args, int n) {
  (void)args;
  (void)n;
  char buf[256];
  int result = nexs_await(buf, sizeof(buf));
  if (result < 0)
    return val_err(4, "await: nessun figlio");
  return val_str(buf);
}

static Value bi_getpid(Value *args, int n) {
  (void)args;
  (void)n;
  return val_int((int64_t)getpid());
}

static Value bi_getwd(Value *args, int n) {
  (void)args;
  (void)n;
  char buf[REG_PATH_MAX];
  if (!getcwd(buf, sizeof(buf)))
    return val_err(4, "getwd: fallito");
  return val_str(buf);
}

void sysproc_register_builtins(void) {
  reg_register_builtin("sleep", bi_sleep);
  reg_register_builtin("exec", bi_exec);
  reg_register_builtin("exits", bi_exits);
  reg_register_builtin("alarm", bi_alarm);
  reg_register_builtin("rfork", bi_rfork);
  reg_register_builtin("await", bi_await);
  reg_register_builtin("getpid", bi_getpid);
  reg_register_builtin("getwd", bi_getwd);

  /* Registra rfork flags nel registro /sys/rfork/ */
  reg_set("/sys/rfork/RFPROC", val_int(NEXS_RFPROC), RK_READ);
  reg_set("/sys/rfork/RFNOWAIT", val_int(NEXS_RFNOWAIT), RK_READ);
  reg_set("/sys/rfork/RFNAMEG", val_int(NEXS_RFNAMEG), RK_READ);
  reg_set("/sys/rfork/RFMEM", val_int(NEXS_RFMEM), RK_READ);
  reg_set("/sys/rfork/RFFDG", val_int(NEXS_RFFDG), RK_READ);

  /* Registra le costanti per open/seek */
  reg_set("/sys/OREAD", val_int(NEXS_OREAD), RK_READ);
  reg_set("/sys/OWRITE", val_int(NEXS_OWRITE), RK_READ);
  reg_set("/sys/ORDWR", val_int(NEXS_ORDWR), RK_READ);
  reg_set("/sys/OTRUNC", val_int(NEXS_OTRUNC), RK_READ);
}
