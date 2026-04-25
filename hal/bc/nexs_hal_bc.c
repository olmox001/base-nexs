/*
 * hal/bc/nexs_hal_bc.c — NEXS HAL Bytecode Executor
 * ====================================================
 * Minimal stack-based VM for HALB (HAL Bytecode) programs.
 * Runs bare-metal (no libc) and in hosted mode.
 *
 * UTF-8 operand encoding:
 *   String:  [len:1-2 bytes][utf8 data:len bytes]
 *            len < 128  → 1 byte
 *            len ≥ 128  → 0x80|(len>>7) then (len & 0x7F)
 *   Integer: [0x80][value:4 bytes LE]
 *   Register:[reg:1]
 */

#include "../include/nexs_hal_bc.h"
#include "../include/nexs_hal.h"

#ifdef NEXS_BAREMETAL
/* No libc — use HAL print for diagnostics */
#  define HALBC_PUTS(s)  nexs_hal_print(s)
#else
#  include <string.h>
#  include <stdio.h>
#  define HALBC_PUTS(s)  do { } while (0)   /* silent in hosted mode */
static void *halbc_memset(void *s, int c, size_t n) { return memset(s, c, n); }
static size_t halbc_strlen(const char *s) { return strlen(s); }
static void halbc_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
static int halbc_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
#endif

#ifdef NEXS_BAREMETAL
/* Bare-metal replacements */
static void *halbc_memset(void *s, int c, size_t n) {
  unsigned char *p = s;
  while (n--) *p++ = (unsigned char)c;
  return s;
}
static void halbc_memcpy(void *d, const void *s, size_t n) {
  unsigned char *dd = d; const unsigned char *ss = s;
  while (n--) *dd++ = *ss++;
}
static size_t halbc_strlen(const char *s) {
  size_t n = 0; while (s[n]) n++; return n;
}
static int halbc_memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *aa = a, *bb = b;
  while (n--) { if (*aa != *bb) return *aa - *bb; aa++; bb++; }
  return 0;
}
#endif

/* =========================================================
   DEVICE FUNCTION REGISTRY
   ========================================================= */

HalDevEntry g_hal_devfns[HALBC_MAX_DEVFNS];
int         g_hal_devfn_count = 0;

void halbc_register_devfn(const char *name, HalDevFn fn) {
  if (g_hal_devfn_count >= HALBC_MAX_DEVFNS) return;
  g_hal_devfns[g_hal_devfn_count].name = name;
  g_hal_devfns[g_hal_devfn_count].fn   = fn;
  g_hal_devfn_count++;
}

static HalDevFn halbc_find_devfn(int idx) {
  if (idx < 0 || idx >= g_hal_devfn_count) return NULL;
  return g_hal_devfns[idx].fn;
}

/* =========================================================
   OPERAND DECODERS (read from bytecode stream)
   ========================================================= */

/* Read a UTF-8 length-prefixed string from the bytecode.
 * Writes up to bufsz-1 bytes into out (always NUL-terminated).
 * Returns bytes consumed from prog, or -1 on error. */
static int read_str(const uint8_t *prog, uint32_t prog_len, uint32_t ip,
                    char *out, int bufsz) {
  if (ip >= prog_len) return -1;
  uint32_t len;
  int header = 1;
  uint8_t b0 = prog[ip];
  if (b0 & 0x80) {
    /* 2-byte length */
    if (ip + 1 >= prog_len) return -1;
    uint8_t b1 = prog[ip + 1];
    len = ((uint32_t)(b0 & 0x7F) << 7) | (b1 & 0x7F);
    header = 2;
  } else {
    len = b0;
  }
  if (ip + header + len > prog_len) return -1;
  uint32_t copy = (uint32_t)(bufsz - 1) < len ? (uint32_t)(bufsz - 1) : len;
  halbc_memcpy(out, prog + ip + header, copy);
  out[copy] = '\0';
  return header + (int)len;
}

/* Read a 4-byte LE integer (preceded by 0x80 marker).
 * Returns bytes consumed (5), or -1 on error. */
static int read_int(const uint8_t *prog, uint32_t prog_len, uint32_t ip,
                    int64_t *out) {
  if (ip + 4 >= prog_len) return -1;
  if (prog[ip] != 0x80) return -1;
  uint32_t v = (uint32_t)prog[ip+1]
             | ((uint32_t)prog[ip+2] << 8)
             | ((uint32_t)prog[ip+3] << 16)
             | ((uint32_t)prog[ip+4] << 24);
  *out = (int64_t)(int32_t)v;
  return 5;
}

/* =========================================================
   STACK HELPERS
   ========================================================= */

static void stack_push_int(HalBcState *st, int64_t v) {
  if (st->sp >= HALBC_STACK_DEPTH) { st->error = 1; return; }
  st->stack[st->sp].is_str = 0;
  st->stack[st->sp].ival   = v;
  st->sp++;
}

static void stack_push_str(HalBcState *st, const char *s) {
  if (st->sp >= HALBC_STACK_DEPTH) { st->error = 1; return; }
  st->stack[st->sp].is_str = 1;
  size_t n = halbc_strlen(s);
  if (n >= sizeof(st->stack[st->sp].sval))
    n = sizeof(st->stack[st->sp].sval) - 1;
  halbc_memcpy(st->stack[st->sp].sval, s, n);
  st->stack[st->sp].sval[n] = '\0';
  st->sp++;
}

static HalStackVal stack_pop(HalBcState *st) {
  HalStackVal zero;
  halbc_memset(&zero, 0, sizeof(zero));
  if (st->sp <= 0) { st->error = 1; return zero; }
  return st->stack[--st->sp];
}

/* =========================================================
   INIT
   ========================================================= */

int halbc_init(HalBcState *st, const uint8_t *prog, uint32_t len) {
  halbc_memset(st, 0, sizeof(*st));
  /* Check magic */
  if (len < HALBC_MAGIC_LEN + 4) return -1;
  if (halbc_memcmp(prog, HALBC_MAGIC, HALBC_MAGIC_LEN) != 0) return -1;
  if (prog[HALBC_MAGIC_LEN] != HALBC_VERSION) return -1;
  /* Store program, skip header (magic:4 + version:1 + flags:1 + n_instrs:2) */
  st->prog     = prog;
  st->prog_len = len;
  st->ip       = HALBC_MAGIC_LEN + 4;   /* start after 8-byte header */
  return 0;
}

/* =========================================================
   VIRTUAL DEVICE OPERATIONS
   Implementations are platform-specific (arm64/uart.c, amd64/uart.c).
   The HAL layer provides nexs_hal_putc / nexs_hal_print / nexs_hal_getc.
   ========================================================= */

static void hal_dev_write(HalBcState *st, int reg, const char *data) {
  (void)reg;   /* for /dev/cons, always write to UART */
  const char *p = data;
  while (*p) {
    nexs_hal_putc((unsigned char)*p);
    p++;
  }
}

static void hal_dev_read(HalBcState *st, int reg, int32_t len) {
  (void)reg;
  char buf[128];
  int  i = 0;
  while (i < len && i < (int)sizeof(buf) - 1) {
    int c = nexs_hal_getc();
    if (c < 0) break;
    buf[i++] = (char)c;
    if (c == '\n') break;
  }
  buf[i] = '\0';
  stack_push_str(st, buf);
}

static void hal_dev_ctl(HalBcState *st, int reg, const char *cmd) {
  (void)st; (void)reg;
  /* Control commands are device-specific.
   * Currently only "flush" is handled universally. */
  if (halbc_memcmp(cmd, "flush", 5) == 0)
    nexs_hal_print("");  /* flush — no-op on UART */
}

/* =========================================================
   STEP — execute one instruction
   ========================================================= */

int halbc_step(HalBcState *st) {
  if (st->halted || st->error) return 0;
  if (st->ip >= st->prog_len)  { st->halted = 1; return 0; }

  uint8_t op = st->prog[st->ip++];

  switch ((HalOpcode)op) {

  case HALO_NOP:
    break;

  case HALO_HALT:
    st->halted = 1;
    return 0;

  case HALO_OPEN: {
    char path[64];
    int consumed = read_str(st->prog, st->prog_len, st->ip, path, sizeof(path));
    if (consumed < 0) { st->error = 1; return 0; }
    st->ip += (uint32_t)consumed;
    if (st->ip >= st->prog_len) { st->error = 1; return 0; }
    uint8_t mode = st->prog[st->ip++];

    /* Find a free register */
    int reg = -1;
    for (int i = 0; i < HALBC_MAX_REGS; i++) {
      if (!st->regs[i].in_use) { reg = i; break; }
    }
    if (reg < 0) { stack_push_int(st, -1); break; }

    st->regs[reg].in_use  = 1;
    st->regs[reg].offset  = 0;
    size_t plen = halbc_strlen(path);
    if (plen >= sizeof(st->regs[reg].path))
      plen = sizeof(st->regs[reg].path) - 1;
    halbc_memcpy(st->regs[reg].path, path, plen);
    st->regs[reg].path[plen] = '\0';
    (void)mode;
    stack_push_int(st, reg);
    break;
  }

  case HALO_CLOSE: {
    if (st->ip >= st->prog_len) { st->error = 1; return 0; }
    uint8_t reg = st->prog[st->ip++];
    if (reg < HALBC_MAX_REGS) {
      halbc_memset(&st->regs[reg], 0, sizeof(st->regs[reg]));
    }
    break;
  }

  case HALO_READ: {
    if (st->ip >= st->prog_len) { st->error = 1; return 0; }
    uint8_t reg = st->prog[st->ip++];
    int64_t len = 64;
    if (st->ip < st->prog_len && st->prog[st->ip] == 0x80) {
      int c = read_int(st->prog, st->prog_len, st->ip, &len);
      if (c > 0) st->ip += (uint32_t)c;
    }
    hal_dev_read(st, reg, (int32_t)len);
    break;
  }

  case HALO_WRITE: {
    if (st->ip >= st->prog_len) { st->error = 1; return 0; }
    uint8_t reg = st->prog[st->ip++];
    char data[256];
    int consumed = read_str(st->prog, st->prog_len, st->ip, data, sizeof(data));
    if (consumed < 0) { st->error = 1; return 0; }
    st->ip += (uint32_t)consumed;
    hal_dev_write(st, reg, data);
    break;
  }

  case HALO_CTL: {
    if (st->ip >= st->prog_len) { st->error = 1; return 0; }
    uint8_t reg = st->prog[st->ip++];
    char cmd[64];
    int consumed = read_str(st->prog, st->prog_len, st->ip, cmd, sizeof(cmd));
    if (consumed < 0) { st->error = 1; return 0; }
    st->ip += (uint32_t)consumed;
    hal_dev_ctl(st, reg, cmd);
    break;
  }

  case HALO_PUSH: {
    int64_t val;
    int c = read_int(st->prog, st->prog_len, st->ip, &val);
    if (c < 0) { st->error = 1; return 0; }
    st->ip += (uint32_t)c;
    stack_push_int(st, val);
    break;
  }

  case HALO_POP:
    stack_pop(st);
    break;

  case HALO_CALL: {
    if (st->ip + 1 >= st->prog_len) { st->error = 1; return 0; }
    uint16_t idx = (uint16_t)st->prog[st->ip]
                 | ((uint16_t)st->prog[st->ip+1] << 8);
    st->ip += 2;
    HalDevFn fn = halbc_find_devfn(idx);
    if (fn) fn(st);
    break;
  }

  case HALO_JMP: {
    if (st->ip + 1 >= st->prog_len) { st->error = 1; return 0; }
    int16_t off = (int16_t)((uint16_t)st->prog[st->ip]
                           | ((uint16_t)st->prog[st->ip+1] << 8));
    st->ip += 2;
    st->ip = (uint32_t)((int32_t)st->ip + off);
    break;
  }

  case HALO_JZ: {
    if (st->ip + 1 >= st->prog_len) { st->error = 1; return 0; }
    int16_t off = (int16_t)((uint16_t)st->prog[st->ip]
                           | ((uint16_t)st->prog[st->ip+1] << 8));
    st->ip += 2;
    HalStackVal top = stack_pop(st);
    if (!top.is_str && top.ival == 0)
      st->ip = (uint32_t)((int32_t)st->ip + off);
    break;
  }

  case HALO_STAT:
  case HALO_SEEK:
  case HALO_MOUNT:
  case HALO_UMOUNT:
    /* Not yet implemented — skip operands gracefully */
    st->error = 1;
    break;

  default:
    st->error = 1;
    return 0;
  }

  return !st->halted && !st->error;
}

/* =========================================================
   RUN
   ========================================================= */

int halbc_run(HalBcState *st) {
  while (halbc_step(st)) {}
  return st->error ? -1 : 0;
}

/* =========================================================
   OPERAND ENCODERS
   ========================================================= */

int halbc_encode_str(uint8_t *buf, int bufsz, const char *utf8) {
  size_t len = halbc_strlen(utf8);
  int header;
  if (len < 128) {
    if (bufsz < 1 + (int)len) return -1;
    buf[0] = (uint8_t)len;
    header = 1;
  } else {
    if (bufsz < 2 + (int)len) return -1;
    buf[0] = 0x80 | (uint8_t)(len >> 7);
    buf[1] = (uint8_t)(len & 0x7F);
    header = 2;
  }
  halbc_memcpy(buf + header, utf8, len);
  return header + (int)len;
}

int halbc_encode_int(uint8_t *buf, int bufsz, int64_t val) {
  if (bufsz < 5) return -1;
  uint32_t v = (uint32_t)(int32_t)val;
  buf[0] = 0x80;
  buf[1] = (uint8_t)(v & 0xFF);
  buf[2] = (uint8_t)((v >> 8)  & 0xFF);
  buf[3] = (uint8_t)((v >> 16) & 0xFF);
  buf[4] = (uint8_t)((v >> 24) & 0xFF);
  return 5;
}

/* =========================================================
   ASSEMBLER
   ========================================================= */

void halbc_asm_init(HalBcAssembler *a) {
  halbc_memset(a, 0, sizeof(*a));
  /* Reserve 8 bytes for header */
  a->pos = 8;
}

static void asm_emit(HalBcAssembler *a, uint8_t b) {
  if (a->error || a->pos >= HALBC_MAX_PROG) { a->error = 1; return; }
  a->buf[a->pos++] = b;
}

static void asm_emit_str(HalBcAssembler *a, const char *s) {
  int n = halbc_encode_str(a->buf + a->pos,
                            HALBC_MAX_PROG - (int)a->pos, s);
  if (n < 0) { a->error = 1; return; }
  a->pos += (uint32_t)n;
}

static void asm_emit_int(HalBcAssembler *a, int64_t v) {
  int n = halbc_encode_int(a->buf + a->pos,
                            HALBC_MAX_PROG - (int)a->pos, v);
  if (n < 0) { a->error = 1; return; }
  a->pos += (uint32_t)n;
}

void halbc_asm_open(HalBcAssembler *a, const char *path, uint8_t mode) {
  asm_emit(a, HALO_OPEN);
  asm_emit_str(a, path);
  asm_emit(a, mode);
}

void halbc_asm_close(HalBcAssembler *a, uint8_t reg) {
  asm_emit(a, HALO_CLOSE);
  asm_emit(a, reg);
}

void halbc_asm_write(HalBcAssembler *a, uint8_t reg, const char *data) {
  asm_emit(a, HALO_WRITE);
  asm_emit(a, reg);
  asm_emit_str(a, data);
}

void halbc_asm_read(HalBcAssembler *a, uint8_t reg, int32_t len) {
  asm_emit(a, HALO_READ);
  asm_emit(a, reg);
  asm_emit_int(a, len);
}

void halbc_asm_ctl(HalBcAssembler *a, uint8_t reg, const char *cmd) {
  asm_emit(a, HALO_CTL);
  asm_emit(a, reg);
  asm_emit_str(a, cmd);
}

void halbc_asm_halt(HalBcAssembler *a) {
  asm_emit(a, HALO_HALT);
}

uint32_t halbc_asm_finish(HalBcAssembler *a) {
  if (a->error) return 0;
  uint32_t total = a->pos;
  /* Write 8-byte header at offset 0 */
  a->buf[0] = (uint8_t)HALBC_MAGIC[0];
  a->buf[1] = (uint8_t)HALBC_MAGIC[1];
  a->buf[2] = (uint8_t)HALBC_MAGIC[2];
  a->buf[3] = (uint8_t)HALBC_MAGIC[3];
  a->buf[4] = HALBC_VERSION;
  a->buf[5] = HALBC_FLAG_UTF8;
  /* n_instrs — approximate (byte count, not instruction count) */
  uint16_t body = (uint16_t)(total - 8);
  a->buf[6] = (uint8_t)(body & 0xFF);
  a->buf[7] = (uint8_t)((body >> 8) & 0xFF);
  return total;
}
