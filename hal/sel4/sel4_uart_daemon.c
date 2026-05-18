/*
 * hal/sel4/sel4_uart_daemon.c — UART Root Daemon
 * ================================================
 * Centralises all UART output for the multi-PD configuration.
 *
 * Rationale
 * ---------
 * Under seL4 Microkit each Protection Domain is hardware-isolated.
 * Giving every PD direct access to the UART MR/IO-port would risk
 * character interleaving (two PDs writing concurrently).  Instead:
 *
 *   - Only nexs_root maps the physical UART (PL011 / COM1 / NS16550).
 *   - Service PDs (nexs_fs, nexs_pm, nexs_tty) call sel4_log_write()
 *     into their shared NexsLogMR, then microkit_notify(SEL4_CH_LOG).
 *   - nexs_root's notified() handler calls sel4_uart_daemon_flush()
 *     which reads each log MR and forwards the text to nexs_hal_print().
 *
 * API (called from sel4_main.c / notified() only)
 * ------------------------------------------------
 *   sel4_uart_daemon_init()   — register the three log MRs at startup
 *   sel4_uart_daemon_flush()  — drain all pending log MRs to UART
 *
 * This file is ONLY compiled when NEXS_SEL4 is defined.
 * Hosted and baremetal builds are completely unaffected.
 */

#ifdef NEXS_SEL4

#include "sel4_ipc_bridge.h"
#include "../../hal/include/nexs_hal.h"
#include <stddef.h>

/* =========================================================
   REGISTERED LOG SOURCES
   =========================================================
   The three log MR pointers are set once during init() from the
   virtual addresses declared in the .system XML file.
   ========================================================= */

#define UART_DAEMON_MAX_SOURCES 3

typedef struct {
    NexsLogMR  *mr;
    const char *label;   /* prefix printed before each message */
    microkit_channel ch; /* channel ID mapped to this source */
} LogSource;

static LogSource s_sources[UART_DAEMON_MAX_SOURCES];
static int       s_source_count = 0;

/* =========================================================
   sel4_uart_daemon_init
   =========================================================
   Register a log Memory Region for a service PD.
   label   — short prefix, e.g. "[fs]", "[pm]", "[tty]"
   lmr     — pointer to the service's log MR (already mapped into
              nexs_root's vspace via <map mr="..." vaddr="..."/>).
   ch      — the channel this source notifies root on.
   ========================================================= */

void sel4_uart_daemon_init(NexsLogMR *lmr, const char *label, microkit_channel ch) {
    if (s_source_count >= UART_DAEMON_MAX_SOURCES) return;
    if (!lmr) return;
    s_sources[s_source_count].mr    = lmr;
    s_sources[s_source_count].label = label ? label : "[?]";
    s_sources[s_source_count].ch    = ch;
    s_source_count++;
}

/* =========================================================
   sel4_uart_daemon_flush_channel
   =========================================================
   Called from nexs_root's notified() when a specific log channel fires.
   Drains that specific log MR.
   ========================================================= */

void sel4_uart_daemon_flush_channel(microkit_channel ch) {
    char buf[SEL4_LOG_TEXT_MAX];
    for (int i = 0; i < s_source_count; i++) {
        if (s_sources[i].ch != ch) continue;
        NexsLogMR *lmr = s_sources[i].mr;
        if (!lmr) continue;

        /* Spin over multiple pending frames from the same source. */
        while (sel4_log_read(lmr, buf, (uint32_t)sizeof(buf)) == 0) {
            nexs_hal_print(s_sources[i].label);
            nexs_hal_print(" ");
            nexs_hal_print(buf);
            /* Ensure the line ends with a newline so console is clean */
            if (buf[0] && buf[sizeof(buf)-1] != '\n') {
                char last = '\0';
                for (int j = 0; buf[j]; j++) last = buf[j];
                if (last != '\n') nexs_hal_print("\n");
            }
        }
    }
}

/* =========================================================
   sel4_uart_daemon_puts
   =========================================================
   Used by service PDs that have no UART access: replaces the
   nexs_hal_print() call with a write into the local log MR.
   Implemented as a thin wrapper so service PD code stays clean.

   In service PD context, lmr points to the PD's own outbound
   log MR (mapped read-write in the service PD's vspace, read-only
   in root's vspace via a second <map> stanza).
   ========================================================= */

void sel4_uart_daemon_puts(NexsLogMR *lmr, const char *s, microkit_channel ch) {
    if (!lmr || !s) return;
    sel4_log_write(lmr, s);
    microkit_notify(ch);
}

#endif /* NEXS_SEL4 */
