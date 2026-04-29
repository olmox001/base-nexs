/*
 * runtime/nexs_line.c — NEXS ANSI Line Editor Implementation
 * ===========================================================
 * Full readline-style line editor:
 *   - Raw terminal mode via termios
 *   - Complete ANSI/VT100 escape sequence parser
 *   - Arrow keys, Home, End, Delete, Page Up/Down, Shift/Ctrl variants
 *   - Ctrl shortcuts (A/B/C/D/E/F/K/L/N/P/R/T/U/W/Y)
 *   - History ring buffer (256 entries)
 *   - UTF-8 aware cursor movement
 *   - Kill/yank (Ctrl+K, Ctrl+U, Ctrl+Y)
 *   - Fallback to fgets() when stdin is not a tty
 */

#include "include/nexs_line.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* =========================================================
   INTERNAL: TERMIOS STATE
   ========================================================= */

static struct termios s_orig_termios;
static int s_raw_active = 0;

int nexs_line_raw_on(void) {
  if (s_raw_active)
    return 0;
  if (!isatty(STDIN_FILENO))
    return -1;
  if (tcgetattr(STDIN_FILENO, &s_orig_termios) == -1)
    return -1;

  struct termios raw = s_orig_termios;
  /* Input flags: no ICRNL (CR→NL), no IXON (XON/XOFF) */
  raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON | BRKINT | ISTRIP | INPCK);
  /* Output flags: keep OPOST so \n still moves to next line */
  /* Local flags: no echo, no canonical, no signals, no IEXTEN */
  raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cflag |= CS8;
  /* Read returns after 1 byte; no timeout */
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return -1;
  s_raw_active = 1;
  return 0;
}

void nexs_line_raw_off(void) {
  if (!s_raw_active)
    return;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
  s_raw_active = 0;
}

/* =========================================================
   INTERNAL: LOW-LEVEL READ
   ========================================================= */

/* Read one byte, returns -1 on EOF/error */
static int read_byte(void) {
  unsigned char c;
  ssize_t n = read(STDIN_FILENO, &c, 1);
  if (n <= 0)
    return -1;
  return (int)c;
}

/* Read one byte with timeout (~100 ms) for escape sequences */
static int read_byte_timeout(void) {
  struct termios tmp;
  tcgetattr(STDIN_FILENO, &tmp);
  struct termios t2 = tmp;
  t2.c_cc[VMIN] = 0;
  t2.c_cc[VTIME] = 1; /* 0.1 s */
  tcsetattr(STDIN_FILENO, TCSANOW, &t2);
  int c = read_byte();
  tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
  return c;
}

/* =========================================================
   UTF-8 HELPERS
   ========================================================= */

/* Return number of bytes in a UTF-8 sequence from first byte */
static int utf8_seq_len(unsigned char c) {
  if (c < 0x80)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 1; /* continuation byte — treat as 1 */
}

/* Return visual column width of a UTF-8 codepoint (simplified) */
static int utf8_col_width(uint32_t cp) {
  /* CJK wide chars — simplified range check */
  if (cp >= 0x1100 && cp <= 0x9FFF)
    return 2;
  if (cp >= 0xF900 && cp <= 0xFFFF)
    return 2;
  if (cp >= 0x20000)
    return 2;
  return 1;
}

/* Parse UTF-8 starting at buf[pos], return codepoint and advance pos */
static uint32_t utf8_decode(const char *buf, int len, int *pos) {
  unsigned char c = (unsigned char)buf[*pos];
  int seqlen = utf8_seq_len(c);
  if (*pos + seqlen > len) {
    (*pos)++;
    return c;
  }

  uint32_t cp = 0;
  if (seqlen == 1) {
    cp = c;
  } else if (seqlen == 2) {
    cp = (c & 0x1F) << 6;
    cp |= ((unsigned char)buf[*pos + 1]) & 0x3F;
  } else if (seqlen == 3) {
    cp = (c & 0x0F) << 12;
    cp |= (((unsigned char)buf[*pos + 1]) & 0x3F) << 6;
    cp |= ((unsigned char)buf[*pos + 2]) & 0x3F;
  } else {
    cp = (c & 0x07) << 18;
    cp |= (((unsigned char)buf[*pos + 1]) & 0x3F) << 12;
    cp |= (((unsigned char)buf[*pos + 2]) & 0x3F) << 6;
    cp |= ((unsigned char)buf[*pos + 3]) & 0x3F;
  }
  *pos += seqlen;
  return cp;
}

/* Visual column count from byte offset 0..end in buf */
static int visual_cols(const char *buf, int end) {
  int cols = 0, i = 0;
  while (i < end) {
    uint32_t cp = utf8_decode(buf, end, &i);
    cols += utf8_col_width(cp);
  }
  return cols;
}

/* Move byte position forward by one UTF-8 character */
static int utf8_next(const char *buf, int len, int pos) {
  if (pos >= len)
    return pos;
  return pos + utf8_seq_len((unsigned char)buf[pos]);
}

/* Move byte position backward by one UTF-8 character */
static int utf8_prev(const char *buf, int pos) {
  if (pos <= 0)
    return 0;
  int p = pos - 1;
  while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80)
    p--;
  return p;
}

/* =========================================================
   KEY READER
   ========================================================= */

NxKey nexs_key_read(void) {
  NxKey k;
  memset(&k, 0, sizeof(k));
  k.code = NXK_NONE;

  int c = read_byte();
  if (c == -1) {
    k.code = NXK_EOF;
    return k;
  }

  /* ── Escape sequences ──────────────────────────────────── */
  if (c == 0x1B) {
    int c2 = read_byte_timeout();
    if (c2 == -1) {
      k.code = NXK_ESC;
      return k;
    }

    if (c2 == '[') {
      /* CSI sequence: ESC [ ... */
      int c3 = read_byte_timeout();
      if (c3 == -1) {
        k.code = NXK_ESC;
        return k;
      }

      /* ESC [ A/B/C/D — bare arrows */
      if (c3 == 'A') {
        k.code = NXK_UP;
        return k;
      }
      if (c3 == 'B') {
        k.code = NXK_DOWN;
        return k;
      }
      if (c3 == 'C') {
        k.code = NXK_RIGHT;
        return k;
      }
      if (c3 == 'D') {
        k.code = NXK_LEFT;
        return k;
      }
      if (c3 == 'H') {
        k.code = NXK_HOME;
        return k;
      }
      if (c3 == 'F') {
        k.code = NXK_END;
        return k;
      }

      /* ESC [ 1 ; mod X — modified keys */
      if (c3 == '1') {
        int c4 = read_byte_timeout();
        if (c4 == ';') {
          int mod = read_byte_timeout(); /* 2=shift, 5=ctrl */
          int dir = read_byte_timeout();
          if (mod == '2') {
            if (dir == 'D') {
              k.code = NXK_SHIFT_LEFT;
              return k;
            }
            if (dir == 'C') {
              k.code = NXK_SHIFT_RIGHT;
              return k;
            }
            if (dir == 'A') {
              k.code = NXK_UP;
              return k;
            }
            if (dir == 'B') {
              k.code = NXK_DOWN;
              return k;
            }
          }
          if (mod == '5') {
            if (dir == 'D') {
              k.code = NXK_CTRL_LEFT;
              return k;
            }
            if (dir == 'C') {
              k.code = NXK_CTRL_RIGHT;
              return k;
            }
          }
        } else if (c4 == '~') {
          k.code = NXK_HOME;
          return k; /* ESC [ 1 ~ */
        }
        /* drain any extra bytes */
        return k;
      }

      /* ESC [ N ~ — tilde sequences */
      if (c3 >= '2' && c3 <= '6') {
        int c4 = read_byte_timeout();
        if (c4 == '~') {
          if (c3 == '3') {
            k.code = NXK_DELETE;
            return k;
          }
          if (c3 == '4') {
            k.code = NXK_END;
            return k;
          }
          if (c3 == '5') {
            k.code = NXK_PGUP;
            return k;
          }
          if (c3 == '6') {
            k.code = NXK_PGDN;
            return k;
          }
        }
      }

      /* Unknown CSI — absorb remainder (stop at alpha or ~) */
      while (1) {
        int ch = read_byte_timeout();
        if (ch == -1 || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            ch == '~')
          break;
      }
      return k;
    }

    if (c2 == 'O') {
      /* SS3 — some terminals send these for arrows / F keys */
      int c3 = read_byte_timeout();
      if (c3 == 'A') {
        k.code = NXK_UP;
        return k;
      }
      if (c3 == 'B') {
        k.code = NXK_DOWN;
        return k;
      }
      if (c3 == 'C') {
        k.code = NXK_RIGHT;
        return k;
      }
      if (c3 == 'D') {
        k.code = NXK_LEFT;
        return k;
      }
      if (c3 == 'H') {
        k.code = NXK_HOME;
        return k;
      }
      if (c3 == 'F') {
        k.code = NXK_END;
        return k;
      }
      return k;
    }

    /* Alt+key — treat as bare ESC for now */
    k.code = NXK_ESC;
    return k;
  }

  /* ── Control characters ────────────────────────────────── */
  switch (c) {
  case '\r':
  case '\n':
    k.code = NXK_ENTER;
    return k;
  case 0x7F:
    k.code = NXK_BACKSPACE;
    return k;
  case 0x08:
    k.code = NXK_BACKSPACE;
    return k;
  case 0x09:
    k.code = NXK_TAB;
    return k;
  case 0x01:
    k.code = NXK_CTRL_A;
    return k;
  case 0x02:
    k.code = NXK_CTRL_B;
    return k;
  case 0x03:
    k.code = NXK_CTRL_C;
    return k;
  case 0x04:
    k.code = NXK_CTRL_D;
    return k;
  case 0x05:
    k.code = NXK_CTRL_E;
    return k;
  case 0x06:
    k.code = NXK_CTRL_F;
    return k;
  case 0x0B:
    k.code = NXK_CTRL_K;
    return k;
  case 0x0C:
    k.code = NXK_CTRL_L;
    return k;
  case 0x0E:
    k.code = NXK_CTRL_N;
    return k;
  case 0x10:
    k.code = NXK_CTRL_P;
    return k;
  case 0x12:
    k.code = NXK_CTRL_R;
    return k;
  case 0x14:
    k.code = NXK_CTRL_T;
    return k;
  case 0x15:
    k.code = NXK_CTRL_U;
    return k;
  case 0x17:
    k.code = NXK_CTRL_W;
    return k;
  case 0x19:
    k.code = NXK_CTRL_Y;
    return k;
  }

  /* ── Printable / UTF-8 ─────────────────────────────────── */
  k.code = NXK_CHAR;
  k.utf8[0] = (char)c;
  int seqlen = utf8_seq_len((unsigned char)c);
  for (int i = 1; i < seqlen && i < 4; i++) {
    int cc = read_byte();
    if (cc == -1)
      break;
    k.utf8[i] = (char)cc;
  }
  k.utf8[seqlen] = '\0';

  /* decode codepoint */
  int pos = 0;
  k.codepoint = utf8_decode(k.utf8, seqlen, &pos);

  return k;
}

const char *nexs_key_name(NxKeyCode kc) {
  switch (kc) {
  case NXK_NONE:
    return "NONE";
  case NXK_CHAR:
    return "CHAR";
  case NXK_ENTER:
    return "ENTER";
  case NXK_BACKSPACE:
    return "BACKSPACE";
  case NXK_DELETE:
    return "DELETE";
  case NXK_LEFT:
    return "LEFT";
  case NXK_RIGHT:
    return "RIGHT";
  case NXK_UP:
    return "UP";
  case NXK_DOWN:
    return "DOWN";
  case NXK_HOME:
    return "HOME";
  case NXK_END:
    return "END";
  case NXK_PGUP:
    return "PGUP";
  case NXK_PGDN:
    return "PGDN";
  case NXK_SHIFT_LEFT:
    return "SHIFT_LEFT";
  case NXK_SHIFT_RIGHT:
    return "SHIFT_RIGHT";
  case NXK_CTRL_LEFT:
    return "CTRL_LEFT";
  case NXK_CTRL_RIGHT:
    return "CTRL_RIGHT";
  case NXK_CTRL_A:
    return "CTRL_A";
  case NXK_CTRL_B:
    return "CTRL_B";
  case NXK_CTRL_C:
    return "CTRL_C";
  case NXK_CTRL_D:
    return "CTRL_D";
  case NXK_CTRL_E:
    return "CTRL_E";
  case NXK_CTRL_F:
    return "CTRL_F";
  case NXK_CTRL_K:
    return "CTRL_K";
  case NXK_CTRL_L:
    return "CTRL_L";
  case NXK_CTRL_N:
    return "CTRL_N";
  case NXK_CTRL_P:
    return "CTRL_P";
  case NXK_CTRL_R:
    return "CTRL_R";
  case NXK_CTRL_T:
    return "CTRL_T";
  case NXK_CTRL_U:
    return "CTRL_U";
  case NXK_CTRL_W:
    return "CTRL_W";
  case NXK_CTRL_Y:
    return "CTRL_Y";
  case NXK_TAB:
    return "TAB";
  case NXK_ESC:
    return "ESC";
  case NXK_EOF:
    return "EOF";
  default:
    return "UNKNOWN";
  }
}

/* =========================================================
   LINE EDITOR INIT
   ========================================================= */

void nexs_line_init(NxLineEditor *le) {
  memset(le, 0, sizeof(*le));
  le->hist_head = 0;
  le->hist_count = 0;
  le->hist_nav = -1;
  le->raw_mode = 0;
}

/* =========================================================
   INTERNAL: WRITE HELPERS
   ========================================================= */

static void write_str(const char *s) {
  size_t n = strlen(s);
  (void)write(STDOUT_FILENO, s, n);
}

static void write_bytes(const char *s, int n) {
  (void)write(STDOUT_FILENO, s, (size_t)n);
}

/* Move cursor left by N visual columns */
static void cursor_left(int n) {
  if (n <= 0)
    return;
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "\033[%dD", n);
  write_bytes(buf, len);
}

/* Move cursor right by N visual columns */
static void cursor_right(int n) {
  if (n <= 0)
    return;
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "\033[%dC", n);
  write_bytes(buf, len);
}

/* =========================================================
   RENDER
   ========================================================= */

/*
 * Render the current line:
 *   \r  → go to column 0
 *   print prompt
 *   print buffer
 *   ESC[K → erase to end of line
 *   reposition cursor
 */
void nexs_line_render(NxLineEditor *le) {
  /* \r — return to start of line */
  write_str("\r");
  /* prompt */
  if (le->prompt)
    write_str(le->prompt);
  /* buffer content */
  write_bytes(le->buf, le->len);
  /* erase remainder of line */
  write_str("\033[K");
  /* reposition: go back from end to cursor */
  int cols_after = visual_cols(le->buf + le->pos, le->len - le->pos);
  if (cols_after > 0)
    cursor_left(cols_after);
}

void nexs_line_clear_screen(NxLineEditor *le) {
  write_str("\033[2J\033[H");
  nexs_line_render(le);
}

/* =========================================================
   HISTORY
   ========================================================= */

void nexs_line_add_history(NxLineEditor *le, const char *line) {
  if (!line || !line[0])
    return;
  /* Avoid duplicate of last entry */
  if (le->hist_count > 0) {
    int last = (le->hist_head - 1 + NEXS_LINE_HISTORY) % NEXS_LINE_HISTORY;
    if (strcmp(le->history[last], line) == 0)
      return;
  }
  strncpy(le->history[le->hist_head], line, NEXS_LINE_BUFSIZE - 1);
  le->history[le->hist_head][NEXS_LINE_BUFSIZE - 1] = '\0';
  le->hist_head = (le->hist_head + 1) % NEXS_LINE_HISTORY;
  if (le->hist_count < NEXS_LINE_HISTORY)
    le->hist_count++;
}

/* Navigate history: dir = -1 (older), +1 (newer) */
static void history_nav(NxLineEditor *le, int dir) {
  if (le->hist_count == 0)
    return;

  if (le->hist_nav == -1) {
    /* Save current line before entering history */
    strncpy(le->hist_saved, le->buf, NEXS_LINE_BUFSIZE - 1);
    le->hist_saved[NEXS_LINE_BUFSIZE - 1] = '\0';
  }

  int new_nav = le->hist_nav + dir;
  if (new_nav < 0) {
    /* Restore saved line */
    strncpy(le->buf, le->hist_saved, NEXS_LINE_BUFSIZE - 1);
    le->buf[NEXS_LINE_BUFSIZE - 1] = '\0';
    le->len = (int)strlen(le->buf);
    le->pos = le->len;
    le->hist_nav = -1;
    nexs_line_render(le);
    return;
  }
  if (new_nav >= le->hist_count)
    return; /* can't go further forward */

  le->hist_nav = new_nav;
  /* hist_nav=0 → newest, hist_nav=count-1 → oldest */
  int idx = (le->hist_head - 1 - le->hist_nav + NEXS_LINE_HISTORY * 2) %
            NEXS_LINE_HISTORY;
  strncpy(le->buf, le->history[idx], NEXS_LINE_BUFSIZE - 1);
  le->buf[NEXS_LINE_BUFSIZE - 1] = '\0';
  le->len = (int)strlen(le->buf);
  le->pos = le->len;
  nexs_line_render(le);
}

/* =========================================================
   EDITING OPERATIONS
   ========================================================= */

/* Insert UTF-8 bytes at current cursor position */
static void insert_chars(NxLineEditor *le, const char *s, int slen) {
  if (le->len + slen >= NEXS_LINE_BUFSIZE)
    return;
  memmove(le->buf + le->pos + slen, le->buf + le->pos,
          (size_t)(le->len - le->pos));
  memcpy(le->buf + le->pos, s, (size_t)slen);
  le->len += slen;
  le->pos += slen;
  le->buf[le->len] = '\0';
}

/* Delete one UTF-8 character before cursor (backspace) */
static void delete_char_before(NxLineEditor *le) {
  if (le->pos == 0)
    return;
  int prev = utf8_prev(le->buf, le->pos);
  int dlen = le->pos - prev;
  memmove(le->buf + prev, le->buf + le->pos, (size_t)(le->len - le->pos));
  le->len -= dlen;
  le->pos = prev;
  le->buf[le->len] = '\0';
}

/* Delete one UTF-8 character at cursor (delete key / Ctrl+D) */
static void delete_char_at(NxLineEditor *le) {
  if (le->pos >= le->len)
    return;
  int next = utf8_next(le->buf, le->len, le->pos);
  int dlen = next - le->pos;
  memmove(le->buf + le->pos, le->buf + next, (size_t)(le->len - next));
  le->len -= dlen;
  le->buf[le->len] = '\0';
}

/* Kill from cursor to end of line → kill_buf */
static void kill_to_end(NxLineEditor *le) {
  if (le->pos >= le->len)
    return;
  int killed = le->len - le->pos;
  memcpy(le->kill_buf, le->buf + le->pos, (size_t)killed);
  le->kill_buf[killed] = '\0';
  le->len = le->pos;
  le->buf[le->len] = '\0';
}

/* Kill entire line → kill_buf */
static void kill_all(NxLineEditor *le) {
  memcpy(le->kill_buf, le->buf, (size_t)le->len);
  le->kill_buf[le->len] = '\0';
  le->len = 0;
  le->pos = 0;
  le->buf[0] = '\0';
}

/* Kill word backward (Ctrl+W) */
static void kill_word_back(NxLineEditor *le) {
  int end = le->pos;
  /* skip trailing spaces */
  int p = end;
  while (p > 0 && le->buf[p - 1] == ' ')
    p--;
  /* skip non-spaces */
  while (p > 0 && le->buf[p - 1] != ' ')
    p--;
  if (p == end)
    return;
  int killed = end - p;
  memcpy(le->kill_buf, le->buf + p, (size_t)killed);
  le->kill_buf[killed] = '\0';
  memmove(le->buf + p, le->buf + end, (size_t)(le->len - end));
  le->len -= killed;
  le->pos = p;
  le->buf[le->len] = '\0';
}

/* Yank kill_buf at cursor */
static void yank(NxLineEditor *le) {
  int klen = (int)strlen(le->kill_buf);
  if (klen == 0)
    return;
  insert_chars(le, le->kill_buf, klen);
}

/* Transpose two characters before cursor */
static void transpose_chars(NxLineEditor *le) {
  if (le->pos < 2)
    return;
  int p2 = le->pos;
  int p1 = utf8_prev(le->buf, p2);
  int p0 = utf8_prev(le->buf, p1);
  int len1 = p1 - p0;
  int len2 = p2 - p1;
  char tmp[8];
  memcpy(tmp, le->buf + p0, (size_t)len1);
  memmove(le->buf + p0, le->buf + p1, (size_t)len2);
  memcpy(le->buf + p0 + len2, tmp, (size_t)len1);
}

/* =========================================================
   MAIN LINE READ LOOP
   ========================================================= */

int nexs_line_read(NxLineEditor *le, const char *prompt, char *out, int outsz) {
  /* Non-tty fallback */
  if (!isatty(STDIN_FILENO)) {
    if (prompt) {
      fputs(prompt, stdout);
      fflush(stdout);
    }
    if (!fgets(out, outsz, stdin))
      return -1;
    int n = (int)strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
      out[--n] = '\0';
    return n;
  }

  /* Initialise editor state for this line */
  le->buf[0] = '\0';
  le->len = 0;
  le->pos = 0;
  le->hist_nav = -1;
  le->prompt = prompt;
  le->prompt_len = prompt ? (int)strlen(prompt) : 0;
  /* TODO: strip ANSI codes from prompt for accurate prompt_len */
  le->ctrl_c_seen = 0;
  le->interrupted = 0;

  if (nexs_line_raw_on() == -1) {
    /* raw mode failed — fallback */
    if (prompt) {
      fputs(prompt, stdout);
      fflush(stdout);
    }
    if (!fgets(out, outsz, stdin))
      return -1;
    int n = (int)strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
      out[--n] = '\0';
    return n;
  }

  nexs_line_render(le);

  for (;;) {
    NxKey k = nexs_key_read();

    switch (k.code) {

    /* ── Submission ─────────────────────────────────────── */
    case NXK_ENTER:
      write_str("\r\n");
      nexs_line_raw_off();
      /* Copy to output */
      {
        int n = le->len < outsz - 1 ? le->len : outsz - 1;
        memcpy(out, le->buf, (size_t)n);
        out[n] = '\0';
        return n;
      }

    case NXK_EOF:
      if (le->len == 0) {
        write_str("\r\n");
        nexs_line_raw_off();
        return -1;
      }
      /* Ctrl+D on non-empty line → delete char at cursor */
      delete_char_at(le);
      nexs_line_render(le);
      break;

    case NXK_CTRL_C:
      le->interrupted = 1;
      write_str("^C\r\n");
      nexs_line_raw_off();
      out[0] = '\0';
      return -1;

    /* ── Movement ───────────────────────────────────────── */
    case NXK_LEFT:
    case NXK_CTRL_B:
      if (le->pos > 0) {
        int prev = utf8_prev(le->buf, le->pos);
        int w = visual_cols(le->buf + prev, le->pos - prev);
        le->pos = prev;
        cursor_left(w);
      }
      break;

    case NXK_RIGHT:
    case NXK_CTRL_F:
      if (le->pos < le->len) {
        int next = utf8_next(le->buf, le->len, le->pos);
        int w = visual_cols(le->buf + le->pos, next - le->pos);
        le->pos = next;
        cursor_right(w);
      }
      break;

    case NXK_HOME:
    case NXK_CTRL_A:
      le->pos = 0;
      nexs_line_render(le);
      break;

    case NXK_END:
    case NXK_CTRL_E:
      le->pos = le->len;
      nexs_line_render(le);
      break;

    case NXK_CTRL_LEFT:
    case NXK_SHIFT_LEFT:
      /* Word backward */
      while (le->pos > 0 && le->buf[le->pos - 1] == ' ')
        le->pos = utf8_prev(le->buf, le->pos);
      while (le->pos > 0 && le->buf[le->pos - 1] != ' ')
        le->pos = utf8_prev(le->buf, le->pos);
      nexs_line_render(le);
      break;

    case NXK_CTRL_RIGHT:
    case NXK_SHIFT_RIGHT:
      /* Word forward */
      while (le->pos < le->len && le->buf[le->pos] == ' ')
        le->pos = utf8_next(le->buf, le->len, le->pos);
      while (le->pos < le->len && le->buf[le->pos] != ' ')
        le->pos = utf8_next(le->buf, le->len, le->pos);
      nexs_line_render(le);
      break;

    /* ── History ────────────────────────────────────────── */
    case NXK_UP:
    case NXK_CTRL_P:
      history_nav(le, 1);
      break;

    case NXK_DOWN:
    case NXK_CTRL_N:
      history_nav(le, -1);
      break;

    /* ── Deletion ───────────────────────────────────────── */
    case NXK_BACKSPACE:
      delete_char_before(le);
      nexs_line_render(le);
      break;

    case NXK_DELETE:
      delete_char_at(le);
      nexs_line_render(le);
      break;

    case NXK_CTRL_K:
      kill_to_end(le);
      nexs_line_render(le);
      break;

    case NXK_CTRL_U:
      kill_all(le);
      nexs_line_render(le);
      break;

    case NXK_CTRL_W:
      kill_word_back(le);
      nexs_line_render(le);
      break;

    /* ── Yank / Transpose ───────────────────────────────── */
    case NXK_CTRL_Y:
      yank(le);
      nexs_line_render(le);
      break;

    case NXK_CTRL_T:
      transpose_chars(le);
      nexs_line_render(le);
      break;

    /* ── Screen ─────────────────────────────────────────── */
    case NXK_CTRL_L:
      nexs_line_clear_screen(le);
      break;

    /* ── Tab (reserved for completion) ──────────────────── */
    case NXK_TAB:
      /* no-op for now; completion hook can be wired here */
      break;

    /* ── Page Up/Down (scroll history page) ─────────────── */
    case NXK_PGUP:
      /* jump to oldest history entry */
      if (le->hist_count > 0) {
        if (le->hist_nav == -1) {
          strncpy(le->hist_saved, le->buf, NEXS_LINE_BUFSIZE - 1);
          le->hist_saved[NEXS_LINE_BUFSIZE - 1] = '\0';
        }
        le->hist_nav = le->hist_count - 1;
        int idx = (le->hist_head - 1 - le->hist_nav + NEXS_LINE_HISTORY * 2) %
                  NEXS_LINE_HISTORY;
        strncpy(le->buf, le->history[idx], NEXS_LINE_BUFSIZE - 1);
        le->buf[NEXS_LINE_BUFSIZE - 1] = '\0';
        le->len = (int)strlen(le->buf);
        le->pos = le->len;
        nexs_line_render(le);
      }
      break;

    case NXK_PGDN:
      history_nav(le, -1);
      break;

    /* ── Printable characters ───────────────────────────── */
    case NXK_CHAR: {
      int slen = (int)strlen(k.utf8);
      insert_chars(le, k.utf8, slen);
      nexs_line_render(le);
      break;
    }

    case NXK_ESC:
    case NXK_NONE:
    default:
      break;
    }
  }
}
