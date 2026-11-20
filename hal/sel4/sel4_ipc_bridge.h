/*
 * hal/sel4/sel4_ipc_bridge.h — Inter-PD IPC Bridge (seL4 Microkit)
 * ==================================================================
 * Defines the shared memory region (MR) wire format and channel
 * assignments used to pass NEXS Values between Protection Domains.
 *
 * Transport:
 *   Each inter-PD link uses one MR (4 KB) as a ring-free message slot
 *   plus one Microkit channel number for notification.
 *
 * Wire format inside MR:
 *   [uint32  magic  ]  0xNE580001 — sanity check
 *   [uint32  seq    ]  monotonic sequence number (detects stale reads)
 *   [uint32  len    ]  payload byte count (0 = empty slot)
 *   [uint32  pad    ]  reserved / alignment
 *   [uint8[] payload]  binary-serialised NEXS Value (reg_ipc wire format)
 *                      max SEL4_MR_PAYLOAD bytes
 *
 * Usage flow (root → fs example):
 *   root side:   sel4_bridge_write(mr_root_to_fs, &val);
 *                microkit_notify(SEL4_CH_FS);
 *   fs notified: sel4_bridge_read(mr_root_to_fs, &out_val);
 *                [process] ... then reply:
 *                sel4_bridge_write(mr_fs_to_root, &reply);
 *                microkit_notify(SEL4_CH_BACK_FS);
 *
 * Compilation guard: only included when NEXS_SEL4 is defined.
 * Hosted and baremetal builds are completely unaffected.
 */

#ifndef SEL4_IPC_BRIDGE_H
#define SEL4_IPC_BRIDGE_H

#ifdef NEXS_SEL4

#include <microkit.h>
#include <stdint.h>
#include "../../core/include/nexs_value.h"

/* =========================================================
   CHANNEL ASSIGNMENTS
   =========================================================
   These must match the <channel> elements in the .system XML files.
   Channels are one-directional: root notifies services on CH_*,
   services reply to root on CH_BACK_*.
   ========================================================= */

#define SEL4_CH_FS       1   /* root → nexs_fs  (request) */
#define SEL4_CH_PM       2   /* root → nexs_pm  (request) */
#define SEL4_CH_TTY      3   /* root → nexs_tty (request) */
#define SEL4_CH_BACK_FS  4   /* nexs_fs  → root (reply)   */
#define SEL4_CH_BACK_PM  5   /* nexs_pm  → root (reply)   */
#define SEL4_CH_BACK_TTY 6   /* nexs_tty → root (reply)   */
#define SEL4_CH_LOG_FS   7   /* nexs_fs  → root (log-only) */
#define SEL4_CH_LOG_PM   8   /* nexs_pm  → root (log-only) */
#define SEL4_CH_LOG_TTY  9   /* nexs_tty → root (log-only) */

/* =========================================================
   MR LAYOUT
   ========================================================= */

#define SEL4_MR_MAGIC    0x4E450001U  /* "NE" + version 1 */
#define SEL4_MR_HDR_SIZE 16           /* magic+seq+len+pad (4×4 bytes)   */
#define SEL4_MR_PAGE_SIZE 4096
#define SEL4_MR_PAYLOAD  (SEL4_MR_PAGE_SIZE - SEL4_MR_HDR_SIZE) /* 4080 B */

/*
 * NexsMR — overlay struct for a single 4KB Microkit Memory Region.
 * The linker places the MR at a fixed virtual address declared in the
 * .system XML file; the PD maps it via a <map mr="..." vaddr="..."/> stanza.
 * Cast the mapped vaddr to (NexsMR *) to use this API.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                   /* SEL4_MR_MAGIC when slot contains data */
    uint32_t seq;                     /* monotonic counter — reader checks != last */
    uint32_t len;                     /* payload bytes written; 0 = empty         */
    uint32_t _pad;
    uint8_t  payload[SEL4_MR_PAYLOAD];
} NexsMR;

/* =========================================================
   LOG SLOT — compact text-only MR for daemon logging
   ========================================================= */

#define SEL4_LOG_TEXT_MAX (SEL4_MR_PAGE_SIZE - 8)

typedef struct __attribute__((packed)) {
    uint32_t magic;                   /* SEL4_MR_MAGIC */
    uint32_t len;                     /* text byte count, 0 = empty */
    char     text[SEL4_LOG_TEXT_MAX];
} NexsLogMR;

/* =========================================================
   BRIDGE API
   =========================================================
   These functions are implemented in sel4_ipc_bridge.c and are
   shared by sel4_main.c, sel4_pd_*.c, and sel4_uart_daemon.c.
   ========================================================= */

/*
 * sel4_bridge_write — serialise *val into mr->payload and set mr->magic/seq/len.
 * Returns 0 on success, -1 if the serialised form exceeds SEL4_MR_PAYLOAD.
 * Caller must call microkit_notify(ch) after this to wake the peer PD.
 */
int sel4_bridge_write(NexsMR *mr, const Value *val);

/*
 * sel4_bridge_read — deserialise mr->payload into *out.
 * Returns 0 on success, -1 if mr is empty (len==0) or magic mismatch.
 * Clears mr->len to 0 after a successful read (slot recycled).
 * Caller owns *out and must call val_free(out) when done.
 */
int sel4_bridge_read(NexsMR *mr, Value *out);

/*
 * sel4_log_write — write a NUL-terminated string into a NexsLogMR.
 * Truncates silently if s > SEL4_LOG_TEXT_MAX-1.
 * Caller notifies root via microkit_notify(SEL4_CH_LOG).
 */
void sel4_log_write(NexsLogMR *lmr, const char *s);

/*
 * sel4_log_read — read text from lmr into buf (max n bytes, NUL-terminated).
 * Clears lmr->len to 0.  Returns 0 on success, -1 if empty.
 */
int sel4_log_read(NexsLogMR *lmr, char *buf, uint32_t n);

#endif /* NEXS_SEL4 */
#endif /* SEL4_IPC_BRIDGE_H */
