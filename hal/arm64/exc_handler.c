/*
 * hal/arm64/exc_handler.c — AArch64 Exception Handler
 * ======================================================
 * Called from exc_vectors.S with:
 *   x0 = exception kind (group/type encoded as 0xGT)
 *   x1 = ESR_EL1 (exception syndrome)
 *   x2 = FAR_EL1 (fault address)
 */

#include "../../hal/include/nexs_hal.h"
#include <stdint.h>

/* ESR_EL1 EC field (bits [31:26]) */
#define ESR_EC(esr)     (((esr) >> 26) & 0x3F)
#define ESR_ISS(esr)    ((esr) & 0x1FFFFFF)

/* Exception class names for diagnostics */
static const char *ec_name(uint32_t ec) {
    switch (ec) {
    case 0x01: return "WFI/WFE";
    case 0x07: return "SVE/FP/SIMD";
    case 0x0E: return "Illegal execution state";
    case 0x15: return "SVC (AArch64)";
    case 0x20: return "Instruction Abort (lower EL)";
    case 0x21: return "Instruction Abort (current EL)";
    case 0x22: return "PC Alignment Fault";
    case 0x24: return "Data Abort (lower EL)";
    case 0x25: return "Data Abort (current EL)";
    case 0x26: return "SP Alignment Fault";
    case 0x30: return "Breakpoint (lower EL)";
    case 0x31: return "Breakpoint (current EL)";
    case 0x32: return "Software Step (lower EL)";
    case 0x33: return "Software Step (current EL)";
    case 0x34: return "Watchpoint (lower EL)";
    case 0x35: return "Watchpoint (current EL)";
    default:   return "Unknown";
    }
}

static void print_hex(uint64_t v) {
    char buf[20];
    int i = 15;
    buf[16] = '\0';
    while (i >= 0) {
        int nibble = (int)(v & 0xF);
        buf[i--] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        v >>= 4;
    }
    nexs_hal_print("0x");
    nexs_hal_print(buf);
}

void nexs_exc_handler(uint64_t kind, uint64_t esr, uint64_t far) {
    uint32_t ec = ESR_EC((uint32_t)esr);

    nexs_hal_print("\r\n*** NEXS EXCEPTION ***\r\n");
    nexs_hal_print("  Kind : ");
    print_hex(kind);
    nexs_hal_print("\r\n  ESR  : ");
    print_hex(esr);
    nexs_hal_print("  (");
    nexs_hal_print(ec_name(ec));
    nexs_hal_print(")\r\n  FAR  : ");
    print_hex(far);
    nexs_hal_print("\r\n*** HALTED ***\r\n");

    /* Spin — do not attempt recovery */
    while (1) {
        __asm__ volatile("wfe");
    }
}
