/*
 * hal/include/nexs_idt.h — IDT / GDT / interrupt API
 */
#ifndef NEXS_IDT_H
#define NEXS_IDT_H
#pragma once
#include <stdint.h>

/* Saved registers pushed by isr_stubs.S */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vec, err;
    /* CPU-pushed frame */
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) IsrFrame;

/* GDT selectors */
#define GDT_NULL        0x00
#define GDT_KERN_CODE   0x08
#define GDT_KERN_DATA   0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS_LOW     0x28

void gdt_init(void);
void idt_init(void);
void idt_set_handler(uint8_t vec, void *fn, uint8_t dpl);

/* Called from common_isr_handler (isr_stubs.S) */
void nexs_isr_dispatch(IsrFrame *f);

#endif
