/*
 * hal/module/uart_cpp.cpp — C++ UART Driver Implementation
 * ========================================================
 */

#include "driver.hpp"
#include "../include/nexs_hal.h"
#include <string.h>

namespace nexs {

class UartDriver : public Driver {
public:
    const char* getName() const override { return "UART0"; }
    DriverType getType() const override { return DriverType::UART; }

    int init() override {
        // Actual hardware init (PL011 or 16550)
        // For now, we wrap the existing HAL C functions
        return 0;
    }

    int handleMessage(void* msg, void* reply) override {
        // Protocol: { "op": "write", "data": "..." }
        // (Simplified for C++ demonstration)
        return 0;
    }

    void putc(char c) {
        nexs_hal_putc(c);
    }
};

} // namespace nexs

// C wrapper for the kernel to call the driver
extern "C" void uart_cpp_test() {
    static nexs::UartDriver s_uart;
    s_uart.putc('C');
    s_uart.putc('+');
    s_uart.putc('+');
    s_uart.putc('\n');
}
