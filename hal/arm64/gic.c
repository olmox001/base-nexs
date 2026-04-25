/*
 * hal/arm64/gic.c — GICv2 interrupt controller (QEMU virt)
 * ===========================================================
 * STEP 05: GIC init + IRQ enable/disable + EOI.
 *
 * QEMU virt machine defaults:
 *   GICD base: 0x08000000
 *   GICC base: 0x08010000
 *   GICv3 redistributor: 0x080A0000
 *
 * We implement GICv2 for simplicity (also works in GICv3 compat mode).
 */

#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>

/* ── GICv2 addresses (QEMU virt) ─────────────────────────── */
#define GICD_BASE  0x08000000UL
#define GICC_BASE  0x08010000UL

/* Distributor registers */
#define GICD_CTLR        (GICD_BASE + 0x000)
#define GICD_TYPER       (GICD_BASE + 0x004)
#define GICD_ISENABLER(n) (GICD_BASE + 0x100 + (n) * 4)
#define GICD_ICENABLER(n) (GICD_BASE + 0x180 + (n) * 4)
#define GICD_IPRIORITYR(n)(GICD_BASE + 0x400 + (n) * 4)
#define GICD_ITARGETSR(n) (GICD_BASE + 0x800 + (n) * 4)
#define GICD_ICFGR(n)    (GICD_BASE + 0xC00 + (n) * 4)

/* CPU interface registers */
#define GICC_CTLR        (GICC_BASE + 0x000)
#define GICC_PMR         (GICC_BASE + 0x004)   /* priority mask */
#define GICC_BPR         (GICC_BASE + 0x008)   /* binary point */
#define GICC_IAR         (GICC_BASE + 0x00C)   /* interrupt ack */
#define GICC_EOIR        (GICC_BASE + 0x010)   /* end of interrupt */

/* ── MMIO helpers ─────────────────────────────────────────── */
static inline uint32_t gic_rd32(uint64_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void gic_wr32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

/* ── IRQ handler table ────────────────────────────────────── */
#define MAX_IRQS 512
static void (*s_handlers[MAX_IRQS])(uint32_t irq) = {0};

/* ── Public API ───────────────────────────────────────────── */

void gic_init(void) {
    /* Disable distributor */
    gic_wr32(GICD_CTLR, 0);

    /* Read number of IRQs */
    uint32_t typer = gic_rd32(GICD_TYPER);
    uint32_t nirqs = 32 * ((typer & 0x1F) + 1);

    /* Disable all IRQs, set all to low priority, route to CPU0 */
    for (uint32_t i = 0; i < nirqs / 32; i++) {
        gic_wr32(GICD_ICENABLER(i), 0xFFFFFFFF);
    }
    for (uint32_t i = 0; i < nirqs / 4; i++) {
        gic_wr32(GICD_IPRIORITYR(i), 0xA0A0A0A0); /* mid priority */
        gic_wr32(GICD_ITARGETSR(i), 0x01010101);  /* CPU 0 */
    }

    /* Enable distributor */
    gic_wr32(GICD_CTLR, 1);

    /* Configure CPU interface: accept all priorities, enable */
    gic_wr32(GICC_PMR,  0xFF);    /* lowest priority threshold */
    gic_wr32(GICC_BPR,  0x00);    /* all priority bits for preemption */
    gic_wr32(GICC_CTLR, 1);       /* enable */

    /* Enable IRQ delivery at CPU level */
    __asm__ volatile("msr daifclr, #0xf");  /* clear DAIF.I (unmask IRQ) */

    /* Publish to registry */
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", nirqs);
    reg_set("/hal/gic/nirqs",  val_str(buf),  RK_READ);
    reg_set("/hal/gic/status", val_str("ok"), RK_READ);
}

void gic_enable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    gic_wr32(GICD_ISENABLER(irq / 32), 1U << (irq % 32));
}

void gic_disable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    gic_wr32(GICD_ICENABLER(irq / 32), 1U << (irq % 32));
}

void gic_set_priority(uint32_t irq, uint8_t prio) {
    if (irq >= MAX_IRQS) return;
    uint64_t reg  = GICD_IPRIORITYR(irq / 4);
    uint32_t shift = (irq % 4) * 8;
    uint32_t val   = gic_rd32(reg);
    val = (val & ~(0xFFU << shift)) | ((uint32_t)prio << shift);
    gic_wr32(reg, val);
}

void gic_eoi(uint32_t irq) {
    gic_wr32(GICC_EOIR, irq);
}

void gic_register_handler(uint32_t irq, void (*fn)(uint32_t)) {
    if (irq < MAX_IRQS) s_handlers[irq] = fn;
}

/* Called from exc_handler.c when IRQ group (kind=0x11) fires */
void gic_handle_irq(void) {
    uint32_t iar = gic_rd32(GICC_IAR);
    uint32_t irq = iar & 0x3FF;
    if (irq == 1023) return; /* spurious */
    if (irq < MAX_IRQS && s_handlers[irq])
        s_handlers[irq](irq);
    gic_wr32(GICC_EOIR, iar);
}
