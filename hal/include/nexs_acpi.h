/*
 * hal/include/nexs_acpi.h — ACPI RSDP/MADT parser API
 */
#ifndef NEXS_ACPI_H
#define NEXS_ACPI_H
#pragma once
#include <stdint.h>

int  acpi_init(void);          /* scan + parse → /hal/acpi/ */
int  acpi_cpu_count(void);     /* from MADT */
uint64_t acpi_ioapic_base(void);

#endif
