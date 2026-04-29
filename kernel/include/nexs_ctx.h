/*
 * kernel/include/nexs_ctx.h — CPU context save/restore
 */
#ifndef NEXS_CTX_H
#define NEXS_CTX_H
#pragma once
#include <stdint.h>

/* x86-64 callee-saved context (saved by ctx_switch) */
typedef struct {
    uint64_t rbx, rbp, r12, r13, r14, r15;
    uint64_t rsp;   /* stack pointer at time of switch */
    uint64_t rip;   /* return address (pushed by call) */
} Ctx_amd64;

/* AArch64 callee-saved context */
typedef struct {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;   /* frame pointer */
    uint64_t x30;   /* link register (return address) */
    uint64_t sp;
} Ctx_arm64;

/*
 * ctx_switch(from, to) — defined in ctx_amd64.S / ctx_arm64.S
 * Saves callee-saved regs into *from, restores from *to, returns in to's context.
 */
void ctx_switch(void *from_ctx, void *to_ctx);

#endif
