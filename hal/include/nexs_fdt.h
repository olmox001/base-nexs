/*
 * hal/include/nexs_fdt.h — FDT/Device-Tree parser API
 */
#ifndef NEXS_FDT_H
#define NEXS_FDT_H
#pragma once
#include <stdint.h>

void        fdt_init(void *blob);
uint64_t    fdt_get_ram_base(void);
uint64_t    fdt_get_ram_size(void);
const char *fdt_get_cmdline(void);
uint64_t    fdt_get_uart_base(void);
uint64_t    fdt_get_gic_dist_base(void);
uint64_t    fdt_get_gic_cpu_base(void);

#endif
