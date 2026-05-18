#include <stdint.h>

#define NS16550_BASE  0x10000000UL
#define UART_THR      ((volatile uint8_t *)(NS16550_BASE + 0))
#define UART_RBR      ((volatile uint8_t *)(NS16550_BASE + 0))
#define UART_LSR      ((volatile uint8_t *)(NS16550_BASE + 5))
#define UART_LSR_THRE (1U << 5)
#define UART_LSR_DR   (1U << 0)

void nexs_uart_init(void) {
    /* QEMU virt NS16550 is pre-initialized; nothing to do */
}

void nexs_uart_putc(char c) {
    while (!(*UART_LSR & UART_LSR_THRE))
        ;
    *UART_THR = (uint8_t)c;
}

int nexs_uart_getc(void) {
    if (!(*UART_LSR & UART_LSR_DR)) {
        return -1;
    }
    return (int)*UART_RBR;
}
