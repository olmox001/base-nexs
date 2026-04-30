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
  NXK_CHAR,        /* printable character (see NxKey.utf8) */
  NXK_ENTER,       /* \r or \n */
  NXK_BACKSPACE,   /* 0x7F or Ctrl+H */
  NXK_DELETE,      /* ESC [ 3 ~ */
  NXK_LEFT,        /* ESC [ D */
  NXK_RIGHT,       /* ESC [ C */
  NXK_UP,          /* ESC [ A */
  NXK_DOWN,        /* ESC [ B */
  NXK_HOME,        /* ESC [ H  or  ESC [ 1 ~ */
  NXK_END,         /* ESC [ F  or  ESC [ 4 ~ */
  NXK_PGUP,        /* ESC [ 5 ~ */
  NXK_PGDN,        /* ESC [ 6 ~ */
  NXK_SHIFT_LEFT,  /* ESC [ 1 ; 2 D */
  NXK_SHIFT_RIGHT, /* ESC [ 1 ; 2 C */
  NXK_CTRL_LEFT,   /* ESC [ 1 ; 5 D */
  NXK_CTRL_RIGHT,  /* ESC [ 1 ; 5 C */
  NXK_CTRL_A,      /* beginning of line */
  NXK_CTRL_B,      /* backward char */
  NXK_CTRL_C,      /* interrupt */
  NXK_CTRL_D,      /* EOF / delete-at-cursor */
  NXK_CTRL_E,      /* end of line */
  NXK_CTRL_F,      /* forward char */
  NXK_CTRL_K,      /* kill to end of line */
  NXK_CTRL_L,      /* clear screen + redraw */
  NXK_CTRL_N,      /* next history */
  NXK_CTRL_P,      /* prev history */
  NXK_CTRL_R,      /* reverse search (future) */
  NXK_CTRL_T,      /* transpose chars */
  NXK_CTRL_U,      /* kill entire line */
  NXK_CTRL_W,      /* kill word backward */
  NXK_CTRL_Y,      /* yank */
  NXK_TAB,         /* tab */
  NXK_ESC,         /* bare ESC */
  NXK_EOF,         /* real EOF (Ctrl+D on empty line) */
} NxKeyCode;

typedef struct {
  NxKeyCode code;
  uint32_t  codepoint;  /* Unicode codepoint for NXK_CHAR */
  char      utf8[5];    /* UTF-8 bytes (NUL-terminated) for NXK_CHAR */
} NxKey;

/* =========================================================
   LINE EDITOR STATE
   ========================================================= */

#define NEXS_LINE_BUFSIZE  4096   /* max line length */
#define NEXS_LINE_HISTORY  256    /* history ring size */
#define NEXS_LINE_KILLSZ   4096   /* kill ring size */

typedef struct {
  /* Current line buffer */
  char  buf[NEXS_LINE_BUFSIZE];
  int   len;                        /* used bytes */
  int   pos;                        /* cursor byte offset */

  /* History ring */
  char  history[NEXS_LINE_HISTORY][NEXS_LINE_BUFSIZE];
  int   hist_head;                  /* index of newest entry */
  int   hist_count;                 /* entries stored */
  int   hist_nav;                   /* -1 = not navigating; 0..count-1 = index */
  char  hist_saved[NEXS_LINE_BUFSIZE]; /* line saved before navigating */

  /* Kill ring (Ctrl+K / Ctrl+U) */
  char  kill_buf[NEXS_LINE_KILLSZ];

  /* State */
  int   raw_mode;          /* 1 if terminal is in raw mode */
  int   need_redraw;       /* 1 if display is dirty */
  int   ctrl_c_seen;       /* 1 if Ctrl+C was pressed */
  int   interrupted;       /* set on Ctrl+C to signal caller */
  const char *prompt;      /* current prompt string */
  int   prompt_len;        /* visible length of prompt (no escape codes) */
} NxLineEditor;

/* =========================================================
   PUBLIC API
   ========================================================= */

NEXS_API void nexs_line_init(NxLineEditor *le);
NEXS_API int nexs_line_read(NxLineEditor *le, const char *prompt,
                              char *out, int outsz);
NEXS_API void nexs_line_add_history(NxLineEditor *le, const char *line);

NEXS_API int  nexs_line_raw_on(void);
NEXS_API void nexs_line_raw_off(void);
NEXS_API int  nexs_line_is_tty(void);

NEXS_API NxKey      nexs_key_read(void);
NEXS_API const char *nexs_key_name(NxKeyCode k);

NEXS_API void nexs_term_write(const char *s, int n);
NEXS_API void nexs_term_puts(const char *s);
NEXS_API void nexs_term_cursor_left(int n);
NEXS_API void nexs_term_cursor_right(int n);
NEXS_API void nexs_term_cursor_col(int col);
NEXS_API void nexs_term_erase_eol(void);
NEXS_API void nexs_term_clear_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_LINE_H */

