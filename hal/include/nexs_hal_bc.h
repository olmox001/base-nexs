/*
 * hal/include/nexs_hal_bc.h — NEXS HAL Bytecode (HALB)
 * ======================================================
 * A lightweight, UTF-8-encoded bytecode for abstracting hardware resources
 * as Plan 9-style virtual files.  Inspired by Inferno's Dis VM.
 *
 * Design principles:
 *   • All string operands are UTF-8 encoded (length-prefixed).
 *   • Hardware resources are exposed as virtual device files:
 *       /dev/cons   — console / UART
 *       /dev/null   — null sink
 *       /dev/mem    — physical memory (offset encoded in seek)
 *       /dev/irq    — interrupt controller
 *       /dev/timer  — system timer
 *       /dev/random — entropy source
 *       /dev/cpu    — CPU info and control
 *   • The executor is intentionally minimal (<300 lines) — runs bare-metal.
 *   • Programs are self-describing: magic "HALB" + version + UTF-8 flag.
 *
 * Binary format:
 *   [magic:4 "HALB"][version:1][flags:1 HALBC_UTF8=0x01][n_instrs:2 LE]
 *   [instructions...]
 *
 * Instruction encoding:
 *   [opcode:1][n_ops:1][operands...]
 *
 * Operand encoding:
 *   Integer:  0x80 marker + 4 bytes LE  (total 5 bytes)
 *   String:   length byte(s) + UTF-8 bytes
 *             length < 128  → single byte length
 *             length ≥ 128  → 0x80|(len>>7) then (len & 0x7F)  (2 bytes)
 *   Register: 1 byte (0x00–0x0F, HAL file-handle register)
 */

#ifndef NEXS_HAL_BC_H
#define NEXS_HAL_BC_H
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   MAGIC + VERSION
   ========================================================= */

#define HALBC_MAGIC     "HALB"
#define HALBC_MAGIC_LEN 4
#define HALBC_VERSION   1
#define HALBC_FLAG_UTF8 0x01   /* all strings are UTF-8 */

/* =========================================================
   OPCODES
   ========================================================= */

typedef enum {
  HALO_NOP    = 0x00,  /* no-op                                        */
  HALO_OPEN   = 0x01,  /* OPEN   <path:str> <mode:u8>   → reg           */
  HALO_CLOSE  = 0x02,  /* CLOSE  <reg:u8>                               */
  HALO_READ   = 0x03,  /* READ   <reg:u8>  <len:int>    → pushes result */
  HALO_WRITE  = 0x04,  /* WRITE  <reg:u8>  <data:str>                   */
  HALO_SEEK   = 0x05,  /* SEEK   <reg:u8>  <off:int>  <whence:u8>       */
  HALO_STAT   = 0x06,  /* STAT   <path:str>             → pushes result */
  HALO_MOUNT  = 0x07,  /* MOUNT  <dev:str> <mnt:str>  <flags:u8>        */
  HALO_UMOUNT = 0x08,  /* UMOUNT <mnt:str>                              */
  HALO_CTL    = 0x09,  /* CTL    <reg:u8>  <cmd:str>                    */
  HALO_PUSH   = 0x0A,  /* PUSH   <value:int>            push immediate  */
  HALO_POP    = 0x0B,  /* POP                           discard top     */
  HALO_CALL   = 0x0C,  /* CALL   <fn_idx:u16 LE>        call device fn  */
  HALO_JMP    = 0x0D,  /* JMP    <off:i16 LE>           relative jump   */
  HALO_JZ     = 0x0E,  /* JZ     <off:i16 LE>           jump if TOS==0  */
  HALO_HALT   = 0xFF,  /* HALT                          stop execution  */
} HalOpcode;

/* =========================================================
   OPEN MODES
   ========================================================= */

#define HALBC_OREAD  0x00
#define HALBC_OWRITE 0x01
#define HALBC_ORDWR  0x02
#define HALBC_OTRUNC 0x10

/* =========================================================
   SEEK WHENCE
   ========================================================= */

#define HALBC_SEEK_SET 0
#define HALBC_SEEK_CUR 1
#define HALBC_SEEK_END 2

/* =========================================================
   MOUNT FLAGS
   ========================================================= */

#define HALBC_MOUNT_REPLACE 0
#define HALBC_MOUNT_BEFORE  1
#define HALBC_MOUNT_AFTER   2

/* =========================================================
   EXECUTOR STATE
   ========================================================= */

#define HALBC_MAX_REGS   16   /* file-handle registers (R0–R15)  */
#define HALBC_STACK_DEPTH 64  /* value stack depth               */
#define HALBC_MAX_PROG   4096 /* max bytecode bytes              */

/* Virtual device handle — returned by HALO_OPEN */
typedef struct {
  int      in_use;
  char     path[64];   /* UTF-8 device path */
  uint32_t offset;     /* current read/write offset */
} HalReg;

/* Stack value — integer or short string */
typedef struct {
  int      is_str;
  int64_t  ival;
  char     sval[128];  /* UTF-8 string result */
} HalStackVal;

typedef struct {
  const uint8_t *prog;       /* bytecode buffer                    */
  uint32_t       prog_len;   /* byte length of bytecode            */
  uint32_t       ip;         /* instruction pointer (byte offset)  */

  HalReg         regs[HALBC_MAX_REGS];
  HalStackVal    stack[HALBC_STACK_DEPTH];
  int            sp;         /* stack pointer (grows up)           */

  int            halted;
  int            error;
  char           errmsg[128];
} HalBcState;

/* =========================================================
   DEVICE FUNCTION TABLE
   ========================================================= */

/*
 * Device functions called by HALO_CALL.
 * The concrete implementations live in the platform-specific
 * hal/arm64/uart.c and hal/amd64/uart.c files.
 */
typedef void (*HalDevFn)(HalBcState *st);

#define HALBC_MAX_DEVFNS 32

typedef struct {
  const char *name;
  HalDevFn    fn;
} HalDevEntry;

extern HalDevEntry g_hal_devfns[HALBC_MAX_DEVFNS];
extern int         g_hal_devfn_count;

/* Register a device function (called during HAL init) */
void halbc_register_devfn(const char *name, HalDevFn fn);

/* =========================================================
   PUBLIC API
   ========================================================= */

/* Initialise the executor state and parse header.
 * Returns 0 on success, -1 if magic/version mismatch. */
int halbc_init(HalBcState *st, const uint8_t *prog, uint32_t len);

/* Execute one instruction.  Returns 1 if still running, 0 if halted. */
int halbc_step(HalBcState *st);

/* Run until HALT or error.  Returns 0 on clean HALT, -1 on error. */
int halbc_run(HalBcState *st);

/* Encode a string operand into buf (UTF-8 length-prefixed).
 * Returns bytes written, or -1 on overflow. */
int halbc_encode_str(uint8_t *buf, int bufsz, const char *utf8);

/* Encode an integer operand into buf.
 * Returns bytes written (always 5), or -1 on overflow. */
int halbc_encode_int(uint8_t *buf, int bufsz, int64_t val);

/* =========================================================
   ASSEMBLER HELPERS (build programs at runtime)
   ========================================================= */

typedef struct {
  uint8_t  buf[HALBC_MAX_PROG];
  uint32_t pos;
  int      error;
} HalBcAssembler;

void halbc_asm_init(HalBcAssembler *a);
void halbc_asm_open(HalBcAssembler *a, const char *path, uint8_t mode);
void halbc_asm_close(HalBcAssembler *a, uint8_t reg);
void halbc_asm_write(HalBcAssembler *a, uint8_t reg, const char *data);
void halbc_asm_read(HalBcAssembler *a, uint8_t reg, int32_t len);
void halbc_asm_ctl(HalBcAssembler *a, uint8_t reg, const char *cmd);
void halbc_asm_halt(HalBcAssembler *a);

/* Finalise: write magic+header and return total length */
uint32_t halbc_asm_finish(HalBcAssembler *a);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_HAL_BC_H */
