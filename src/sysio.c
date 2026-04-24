/*
 * sysio.c — Plan 9-style File I/O Syscalls
 * ===========================================
 * Implementazione delle syscall di I/O ispirate a Plan 9:
 *   open, create, close, pread, pwrite, seek, dup, fd2path,
 *   remove, pipe, stat, chdir
 *
 * La tabella dei file descriptor (g_fd_table) è inizializzata con:
 *   fd 0 = stdin, fd 1 = stdout, fd 2 = stderr
 */

#include "basereg.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* =========================================================
   DATI GLOBALI
   ========================================================= */

NexsFd g_fd_table[NEXS_MAX_FDS];
static char g_errstr[256] = "";

/* =========================================================
   INIZIALIZZAZIONE
   ========================================================= */

void sysio_init(void) {
  memset(g_fd_table, 0, sizeof(g_fd_table));

  /* fd 0 = stdin */
  g_fd_table[0].fp = stdin;
  strncpy(g_fd_table[0].path, "/dev/stdin", REG_PATH_MAX - 1);
  g_fd_table[0].in_use = 1;
  g_fd_table[0].flags = NEXS_OREAD;

  /* fd 1 = stdout */
  g_fd_table[1].fp = stdout;
  strncpy(g_fd_table[1].path, "/dev/stdout", REG_PATH_MAX - 1);
  g_fd_table[1].in_use = 1;
  g_fd_table[1].flags = NEXS_OWRITE;

  /* fd 2 = stderr */
  g_fd_table[2].fp = stderr;
  strncpy(g_fd_table[2].path, "/dev/stderr", REG_PATH_MAX - 1);
  g_fd_table[2].in_use = 1;
  g_fd_table[2].flags = NEXS_OWRITE;
}

/* =========================================================
   HELPER: trova un fd libero
   ========================================================= */

static int fd_alloc(void) {
  for (int i = 3; i < NEXS_MAX_FDS; i++) {
    if (!g_fd_table[i].in_use)
      return i;
  }
  return -1;
}

static void set_errstr(const char *msg) {
  if (msg)
    strncpy(g_errstr, msg, sizeof(g_errstr) - 1);
  else
    g_errstr[0] = '\0';
  g_errstr[sizeof(g_errstr) - 1] = '\0';
}

/* =========================================================
   SYSCALLS
   ========================================================= */

/*
 * nexs_open — apre un file esistente.
 *
 * mode: NEXS_OREAD (0), NEXS_OWRITE (1), NEXS_ORDWR (2)
 *       può essere combinato con NEXS_OTRUNC (16)
 *
 * Ritorna fd >= 0 in caso di successo, -1 in caso di errore.
 */
int nexs_open(const char *path, int mode) {
  if (!path) {
    set_errstr("open: path NULL");
    return -1;
  }

  int fd = fd_alloc();
  if (fd < 0) {
    set_errstr("open: fd table piena");
    return -1;
  }

  const char *fmode;
  int base = mode & 0x0F;
  int trunc = mode & NEXS_OTRUNC;

  switch (base) {
  case NEXS_OREAD:
    fmode = "r";
    break;
  case NEXS_OWRITE:
    fmode = trunc ? "w" : "a";
    break;
  case NEXS_ORDWR:
    fmode = trunc ? "w+" : "r+";
    break;
  default:
    set_errstr("open: modo non valido");
    return -1;
  }

  FILE *fp = fopen(path, fmode);
  if (!fp) {
    set_errstr("open: impossibile aprire il file");
    return -1;
  }

  g_fd_table[fd].fp = fp;
  strncpy(g_fd_table[fd].path, path, REG_PATH_MAX - 1);
  g_fd_table[fd].path[REG_PATH_MAX - 1] = '\0';
  g_fd_table[fd].in_use = 1;
  g_fd_table[fd].flags = mode;

  /* Registra nel registro /sys/fd/N */
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_set(regpath, val_str(path), RK_READ);

  return fd;
}

/*
 * nexs_create — crea un nuovo file o apre uno esistente per scrittura.
 *
 * In Plan 9, create(2) crea il file se non esiste e lo tronca se esiste.
 * perm è ignorato in questa implementazione (usa umask del sistema).
 */
int nexs_create(const char *path, int mode, int perm) {
  (void)perm; /* Plan 9 permissions: non implementato su POSIX */

  if (!path) {
    set_errstr("create: path NULL");
    return -1;
  }

  int fd = fd_alloc();
  if (fd < 0) {
    set_errstr("create: fd table piena");
    return -1;
  }

  const char *fmode;
  int base = mode & 0x0F;
  switch (base) {
  case NEXS_OREAD:
    fmode = "w"; /* crea e apre per lettura (Plan 9 semantics) */
    break;
  case NEXS_OWRITE:
    fmode = "w";
    break;
  case NEXS_ORDWR:
    fmode = "w+";
    break;
  default:
    fmode = "w";
  }

  FILE *fp = fopen(path, fmode);
  if (!fp) {
    set_errstr("create: impossibile creare il file");
    return -1;
  }

  g_fd_table[fd].fp = fp;
  strncpy(g_fd_table[fd].path, path, REG_PATH_MAX - 1);
  g_fd_table[fd].path[REG_PATH_MAX - 1] = '\0';
  g_fd_table[fd].in_use = 1;
  g_fd_table[fd].flags = mode;

  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_set(regpath, val_str(path), RK_READ);

  return fd;
}

/*
 * nexs_close — chiude un file descriptor.
 */
int nexs_close(int fd) {
  if (fd < 0 || fd >= NEXS_MAX_FDS) {
    set_errstr("close: fd non valido");
    return -1;
  }
  if (!g_fd_table[fd].in_use) {
    set_errstr("close: fd non aperto");
    return -1;
  }

  /* Non chiudere stdin/stdout/stderr */
  if (fd >= 3 && g_fd_table[fd].fp) {
    fclose(g_fd_table[fd].fp);
  }

  g_fd_table[fd].fp = NULL;
  g_fd_table[fd].path[0] = '\0';
  g_fd_table[fd].in_use = 0;
  g_fd_table[fd].flags = 0;

  /* Rimuovi dal registro */
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_delete(regpath);

  return 0;
}

/*
 * nexs_pread — legge n byte da fd a offset.
 *
 * Se offset < 0, legge dalla posizione corrente (come read(2)).
 * Ritorna il numero di byte letti, o -1 in caso di errore.
 */
int nexs_pread(int fd, char *buf, int n, int64_t offset) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use) {
    set_errstr("pread: fd non valido");
    return -1;
  }
  if (!buf || n <= 0) {
    set_errstr("pread: parametri non validi");
    return -1;
  }

  FILE *fp = g_fd_table[fd].fp;
  if (!fp) {
    set_errstr("pread: fp NULL");
    return -1;
  }

  if (offset >= 0) {
    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
      set_errstr("pread: seek fallito");
      return -1;
    }
  }

  size_t rd = fread(buf, 1, (size_t)n, fp);
  return (int)rd;
}

/*
 * nexs_pwrite — scrive n byte su fd a offset.
 *
 * Se offset < 0, scrive alla posizione corrente (come write(2)).
 * Ritorna il numero di byte scritti, o -1 in caso di errore.
 */
int nexs_pwrite(int fd, const char *buf, int n, int64_t offset) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use) {
    set_errstr("pwrite: fd non valido");
    return -1;
  }
  if (!buf || n <= 0) {
    set_errstr("pwrite: parametri non validi");
    return -1;
  }

  FILE *fp = g_fd_table[fd].fp;
  if (!fp) {
    set_errstr("pwrite: fp NULL");
    return -1;
  }

  if (offset >= 0) {
    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
      set_errstr("pwrite: seek fallito");
      return -1;
    }
  }

  size_t wr = fwrite(buf, 1, (size_t)n, fp);
  fflush(fp);
  return (int)wr;
}

/*
 * nexs_seek — cambia la posizione corrente nel file.
 *
 * whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END
 * Ritorna la nuova posizione, o -1 in caso di errore.
 */
int64_t nexs_seek(int fd, int64_t offset, int whence) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use) {
    set_errstr("seek: fd non valido");
    return -1;
  }
  FILE *fp = g_fd_table[fd].fp;
  if (!fp) {
    set_errstr("seek: fp NULL");
    return -1;
  }

  int w;
  switch (whence) {
  case 0: w = SEEK_SET; break;
  case 1: w = SEEK_CUR; break;
  case 2: w = SEEK_END; break;
  default:
    set_errstr("seek: whence non valido");
    return -1;
  }

  if (fseek(fp, (long)offset, w) != 0) {
    set_errstr("seek: fallito");
    return -1;
  }
  return (int64_t)ftell(fp);
}

/*
 * nexs_dup — duplica un fd.
 *
 * Se newfd >= 0, usa quel fd (lo chiude prima se aperto).
 * Se newfd < 0, alloca automaticamente il primo fd libero.
 * Ritorna il nuovo fd, o -1 in caso di errore.
 */
int nexs_dup(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= NEXS_MAX_FDS || !g_fd_table[oldfd].in_use) {
    set_errstr("dup: oldfd non valido");
    return -1;
  }

  if (newfd < 0)
    newfd = fd_alloc();
  if (newfd < 0 || newfd >= NEXS_MAX_FDS) {
    set_errstr("dup: newfd non valido");
    return -1;
  }

  /* Chiudi il vecchio fd se era aperto */
  if (g_fd_table[newfd].in_use && newfd >= 3)
    nexs_close(newfd);

  /* Copia il fd (condividono lo stesso FILE*) */
  g_fd_table[newfd] = g_fd_table[oldfd];

  /* Registra */
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", newfd);
  reg_set(regpath, val_str(g_fd_table[newfd].path), RK_READ);

  return newfd;
}

/*
 * nexs_fd2path — ottiene il path associato a un fd.
 */
int nexs_fd2path(int fd, char *buf, int nbuf) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use) {
    set_errstr("fd2path: fd non valido");
    return -1;
  }
  if (!buf || nbuf <= 0) {
    set_errstr("fd2path: buf non valido");
    return -1;
  }
  strncpy(buf, g_fd_table[fd].path, (size_t)(nbuf - 1));
  buf[nbuf - 1] = '\0';
  return 0;
}

/*
 * nexs_remove — elimina un file dal filesystem.
 */
int nexs_remove(const char *path) {
  if (!path) {
    set_errstr("remove: path NULL");
    return -1;
  }
  if (remove(path) != 0) {
    set_errstr("remove: fallito");
    return -1;
  }
  return 0;
}

/*
 * nexs_pipe — crea una pipe.
 *
 * fd[0] = read end, fd[1] = write end.
 * Usa popen con pipe(2) POSIX sotto il cofano.
 */
int nexs_pipe(int fd[2]) {
  if (!fd) {
    set_errstr("pipe: fd NULL");
    return -1;
  }

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    set_errstr("pipe: creazione fallita");
    return -1;
  }

  int fd0 = fd_alloc();
  if (fd0 < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    set_errstr("pipe: fd table piena");
    return -1;
  }
  g_fd_table[fd0].fp = fdopen(pipefd[0], "r");
  strncpy(g_fd_table[fd0].path, "/dev/pipe/r", REG_PATH_MAX - 1);
  g_fd_table[fd0].in_use = 1;
  g_fd_table[fd0].flags = NEXS_OREAD;

  int fd1 = fd_alloc();
  if (fd1 < 0) {
    fclose(g_fd_table[fd0].fp);
    g_fd_table[fd0].in_use = 0;
    close(pipefd[1]);
    set_errstr("pipe: fd table piena");
    return -1;
  }
  g_fd_table[fd1].fp = fdopen(pipefd[1], "w");
  strncpy(g_fd_table[fd1].path, "/dev/pipe/w", REG_PATH_MAX - 1);
  g_fd_table[fd1].in_use = 1;
  g_fd_table[fd1].flags = NEXS_OWRITE;

  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

/*
 * nexs_stat — ritorna le informazioni di un file come Value (ad albero).
 *
 * Ritorna un registro con: size, mode, mtime, type.
 * Per semplicità, ritorna una stringa formattata.
 */
Value nexs_stat(const char *path) {
  if (!path)
    return val_err(4, "stat: path NULL");

  struct stat sb;
  if (stat(path, &sb) != 0)
    return val_err(4, "stat: file non trovato");

  char buf[512];
  snprintf(buf, sizeof(buf),
           "path=%s size=%lld mode=%o mtime=%lld type=%s",
           path, (long long)sb.st_size, (unsigned int)(sb.st_mode & 0777),
           (long long)sb.st_mtime,
           S_ISDIR(sb.st_mode) ? "dir" : S_ISREG(sb.st_mode) ? "file" : "other");
  return val_str(buf);
}

/*
 * nexs_chdir — cambia directory corrente.
 */
int nexs_chdir(const char *path) {
  if (!path) {
    set_errstr("chdir: path NULL");
    return -1;
  }
  if (chdir(path) != 0) {
    set_errstr("chdir: fallito");
    return -1;
  }
  return 0;
}

/*
 * nexs_errstr — scambia la stringa di errore (Plan 9 style).
 *
 * In Plan 9 errstr(2) scambia il buffer con il kernel error string.
 * Qui leggiamo/scriviamo da/a g_errstr.
 */
void nexs_errstr(char *buf, int nbuf) {
  if (!buf || nbuf <= 0)
    return;
  /* Scambia: buf ↔ g_errstr */
  char tmp[256];
  strncpy(tmp, g_errstr, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  strncpy(g_errstr, buf, sizeof(g_errstr) - 1);
  g_errstr[sizeof(g_errstr) - 1] = '\0';
  strncpy(buf, tmp, (size_t)(nbuf - 1));
  buf[nbuf - 1] = '\0';
}

/* =========================================================
   BUILT-IN REGISTRATION (espone le syscall come funzioni NEXS)
   ========================================================= */

static Value bi_open(Value *args, int n) {
  if (n < 1) return val_err(4, "open: richiede almeno 1 argomento");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "open: primo argomento deve essere una stringa");
  int mode = (n >= 2) ? (int)val_to_int(&args[1]) : NEXS_OREAD;
  int fd = nexs_open((char *)args[0].data, mode);
  if (fd < 0) return val_err(4, "open: fallito");
  return val_int(fd);
}

static Value bi_create(Value *args, int n) {
  if (n < 1) return val_err(4, "create: richiede almeno 1 argomento");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "create: primo argomento deve essere una stringa");
  int mode = (n >= 2) ? (int)val_to_int(&args[1]) : NEXS_OWRITE;
  int perm = (n >= 3) ? (int)val_to_int(&args[2]) : 0644;
  int fd = nexs_create((char *)args[0].data, mode, perm);
  if (fd < 0) return val_err(4, "create: fallito");
  return val_int(fd);
}

static Value bi_close(Value *args, int n) {
  if (n < 1) return val_err(4, "close: richiede 1 argomento");
  int fd = (int)val_to_int(&args[0]);
  return val_int(nexs_close(fd));
}

static Value bi_read(Value *args, int n) {
  if (n < 2) return val_err(4, "read: richiede fd e nbytes");
  int fd = (int)val_to_int(&args[0]);
  int nbytes = (int)val_to_int(&args[1]);
  if (nbytes <= 0 || nbytes > MAX_STR_LEN - 1) nbytes = MAX_STR_LEN - 1;
  char buf[MAX_STR_LEN];
  int64_t offset = (n >= 3) ? val_to_int(&args[2]) : -1;
  int rd = nexs_pread(fd, buf, nbytes, offset);
  if (rd < 0) return val_err(4, "read: fallito");
  buf[rd] = '\0';
  return val_str(buf);
}

static Value bi_write(Value *args, int n) {
  if (n < 2) return val_err(4, "write: richiede fd e stringa");
  int fd = (int)val_to_int(&args[0]);
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "write: secondo argomento deve essere una stringa");
  const char *data = (const char *)args[1].data;
  int len = (int)strlen(data);
  int64_t offset = (n >= 3) ? val_to_int(&args[2]) : -1;
  int wr = nexs_pwrite(fd, data, len, offset);
  return val_int(wr);
}

static Value bi_seek(Value *args, int n) {
  if (n < 2) return val_err(4, "seek: richiede fd e offset");
  int fd = (int)val_to_int(&args[0]);
  int64_t offset = val_to_int(&args[1]);
  int whence = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  int64_t pos = nexs_seek(fd, offset, whence);
  return val_int(pos);
}

static Value bi_dup(Value *args, int n) {
  if (n < 1) return val_err(4, "dup: richiede almeno 1 argomento");
  int oldfd = (int)val_to_int(&args[0]);
  int newfd = (n >= 2) ? (int)val_to_int(&args[1]) : -1;
  int result = nexs_dup(oldfd, newfd);
  return val_int(result);
}

static Value bi_fd2path(Value *args, int n) {
  if (n < 1) return val_err(4, "fd2path: richiede 1 argomento");
  int fd = (int)val_to_int(&args[0]);
  char buf[REG_PATH_MAX];
  if (nexs_fd2path(fd, buf, sizeof(buf)) < 0)
    return val_err(4, "fd2path: fallito");
  return val_str(buf);
}

static Value bi_remove(Value *args, int n) {
  if (n < 1) return val_err(4, "remove: richiede 1 argomento");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "remove: argomento deve essere una stringa");
  return val_int(nexs_remove((char *)args[0].data));
}

static Value bi_pipe(Value *args, int n) {
  (void)args; (void)n;
  int fds[2];
  if (nexs_pipe(fds) < 0)
    return val_err(4, "pipe: fallito");
  /* Ritorna fd[0] (read end). fd[1] = fd[0] + 1 per convenzione */
  /* Meglio: crea un array con i due fd */
  DynArray *arr = arr_get_or_create("__pipe__");
  arr_set(arr, 0, val_int(fds[0]));
  arr_set(arr, 1, val_int(fds[1]));
  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

static Value bi_fstat(Value *args, int n) {
  if (n < 1) return val_err(4, "stat: richiede 1 argomento");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "stat: argomento deve essere una stringa");
  return nexs_stat((char *)args[0].data);
}

static Value bi_chdir(Value *args, int n) {
  if (n < 1) return val_err(4, "chdir: richiede 1 argomento");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "chdir: argomento deve essere una stringa");
  return val_int(nexs_chdir((char *)args[0].data));
}

/* Mount/bind/unmount come built-in */
static Value bi_mount(Value *args, int n) {
  if (n < 2) return val_err(4, "mount: richiede src e dst");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "mount: src deve essere una stringa");
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "mount: dst deve essere una stringa");
  int before = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  return val_int(reg_mount((char *)args[0].data, (char *)args[1].data, before));
}

static Value bi_bind(Value *args, int n) {
  if (n < 2) return val_err(4, "bind: richiede src e dst");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "bind: src deve essere una stringa");
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "bind: dst deve essere una stringa");
  int flag = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  return val_int(reg_bind((char *)args[0].data, (char *)args[1].data, flag));
}

static Value bi_unmount(Value *args, int n) {
  if (n < 1) return val_err(4, "unmount: richiede almeno dst");
  const char *src = NULL;
  const char *dst = NULL;
  if (n >= 2) {
    if (args[0].type == TYPE_STR && args[0].data) src = (char *)args[0].data;
    if (args[1].type == TYPE_STR && args[1].data) dst = (char *)args[1].data;
  } else {
    if (args[0].type == TYPE_STR && args[0].data) dst = (char *)args[0].data;
  }
  if (!dst) return val_err(4, "unmount: dst non valido");
  return val_int(reg_unmount(src, dst));
}

void sysio_register_builtins(void) {
  reg_register_builtin("open", bi_open);
  reg_register_builtin("create", bi_create);
  reg_register_builtin("close", bi_close);
  reg_register_builtin("read", bi_read);
  reg_register_builtin("write", bi_write);
  reg_register_builtin("seek", bi_seek);
  reg_register_builtin("dup", bi_dup);
  reg_register_builtin("fd2path", bi_fd2path);
  reg_register_builtin("remove", bi_remove);
  reg_register_builtin("pipe", bi_pipe);
  reg_register_builtin("fstat", bi_fstat);
  reg_register_builtin("chdir", bi_chdir);
  reg_register_builtin("mount", bi_mount);
  reg_register_builtin("bind", bi_bind);
  reg_register_builtin("unmount", bi_unmount);

  /* Registra la directory /sys/fd nel registro */
  reg_mkpath("/sys/fd", RK_READ);
}
