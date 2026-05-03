/*
 * hal/module/driver.hpp — C++ Driver Base for NEXS
 * ===============================================
 */

#ifndef NEXS_DRIVER_HPP
#define NEXS_DRIVER_HPP

#include <stdint.h>

namespace nexs {

enum class DriverType {
    GENERIC,
    UART,
    BLOCK,
    NETWORK,
    GPU
};

class Driver {
public:
    virtual ~Driver() {}
    virtual const char* getName() const = 0;
    virtual DriverType getType() const = 0;
    
    virtual int init() = 0;
    virtual int handleMessage(void* msg, void* reply) = 0;
};

} // namespace nexs

#endif
