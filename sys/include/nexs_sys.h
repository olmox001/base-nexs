/*
 * nexs_sys.h — NEXS System I/O and Process API
 * ===============================================
 * Plan 9-inspired file-descriptor table and process control syscalls.
 */

#ifndef NEXS_SYS_H
#define NEXS_SYS_H
#pragma once

#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"
#include "../../lang/include/nexs_eval.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   FILE DESCRIPTOR TABLE
   ========================================================= */

typedef struct {
  FILE *fp;
  char  path[REG_PATH_MAX];
  int   in_use;
  int   flags;
} NexsFd;

extern NexsFd g_fd_table[NEXS_MAX_FDS];

/* =========================================================
   SYSIO API
   ========================================================= */

NEXS_API void    sysio_init(void);
NEXS_API int     nexs_open(const char *path, int mode);
NEXS_API int     nexs_create(const char *path, int mode, int perm);
NEXS_API int     nexs_close(int fd);
NEXS_API int     nexs_pread(int fd, char *buf, int n, int64_t offset);
NEXS_API int     nexs_pwrite(int fd, const char *buf, int n, int64_t offset);
NEXS_API int64_t nexs_seek(int fd, int64_t offset, int whence);
NEXS_API int     nexs_dup(int oldfd, int newfd);
NEXS_API int     nexs_fd2path(int fd, char *buf, int nbuf);
NEXS_API int     nexs_remove(const char *path);
NEXS_API int     nexs_pipe(int fd[2]);
NEXS_API Value   nexs_stat(const char *path);
NEXS_API int     nexs_chdir(const char *path);
NEXS_API void    nexs_errstr(char *buf, int nbuf);

NEXS_API void sysio_register_builtins(void);

/* =========================================================
   SYSPROC API
   ========================================================= */

NEXS_API int  nexs_sleep(int msec);
NEXS_API int  nexs_exec(EvalCtx *ctx, const char *path);
NEXS_API void nexs_exits(const char *status);
NEXS_API int  nexs_alarm(int msec);
NEXS_API int  nexs_rfork(int flags);
NEXS_API int  nexs_await(char *buf, int nbuf);

NEXS_API void sysproc_register_builtins(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_SYS_H */
