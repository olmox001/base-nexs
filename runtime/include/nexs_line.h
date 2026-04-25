/*
 * runtime/include/nexs_line.h — NEXS Terminal Library
 * =====================================================
 * Low-level terminal primitives:
 *   - Raw mode (termios)
 *   - Complete ANSI/VT100 key parser
 *   - Cursor + screen output helpers
 *   - File load/save helpers for text editors
 *
 * Higher-level line editing and the REPL are implemented in .nx.
 */

#ifndef NEXS_LINE_H
#define NEXS_LINE_H
#pragma once

#include "../../core/include/nexs_common.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   KEY CODES
   ========================================================= */

typedef enum {
  NXK_NONE = 0,
  NXK_CHAR,
  NXK_ENTER,
  NXK_BACKSPACE,
  NXK_DELETE,
  NXK_LEFT,  NXK_RIGHT, NXK_UP, NXK_DOWN,
  NXK_HOME,  NXK_END,   NXK_PGUP, NXK_PGDN,
  NXK_SHIFT_LEFT, NXK_SHIFT_RIGHT,
  NXK_CTRL_LEFT,  NXK_CTRL_RIGHT,
  NXK_CTRL_A, NXK_CTRL_B, NXK_CTRL_C, NXK_CTRL_D,
  NXK_CTRL_E, NXK_CTRL_F, NXK_CTRL_K, NXK_CTRL_L,
  NXK_CTRL_N, NXK_CTRL_P, NXK_CTRL_R, NXK_CTRL_T,
  NXK_CTRL_U, NXK_CTRL_W, NXK_CTRL_Y,
  NXK_TAB, NXK_ESC, NXK_EOF,
} NxKeyCode;

typedef struct {
  NxKeyCode code;
  uint32_t  codepoint;
  char      utf8[5];
} NxKey;

/* =========================================================
   PUBLIC API
   ========================================================= */

/* Raw terminal mode */
NEXS_API int  nexs_line_raw_on(void);
NEXS_API void nexs_line_raw_off(void);
NEXS_API int  nexs_line_is_tty(void);

/* Key input — blocks until a complete key event is available */
NEXS_API NxKey      nexs_key_read(void);
NEXS_API int        nexs_key_read_byte(void);        /* raw byte, -1 on EOF */
NEXS_API int        nexs_key_read_byte_timeout(void);/* 100ms timeout, -1 if none */
NEXS_API const char *nexs_key_name(NxKeyCode k);

/* Cursor / screen output (writes to STDOUT_FILENO) */
NEXS_API void nexs_term_write(const char *s, int n); /* write n bytes */
NEXS_API void nexs_term_puts(const char *s);
NEXS_API void nexs_term_cursor_left(int n);
NEXS_API void nexs_term_cursor_right(int n);
NEXS_API void nexs_term_cursor_col(int col);     /* move to absolute column */
NEXS_API void nexs_term_erase_eol(void);         /* ESC[K */
NEXS_API void nexs_term_clear_screen(void);      /* ESC[2J ESC[H */

/* File helpers (used by editor.nx via syscall wrappers) */
NEXS_API char *nexs_file_load(const char *path, int *out_len); /* nexs_alloc'd */
NEXS_API int   nexs_file_save(const char *path, const char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_LINE_H */
