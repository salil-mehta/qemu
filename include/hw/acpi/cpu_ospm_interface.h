/*
 * ACPI CPU OSPM Interface Handling.
 *
 * Copyright (c) 2025 Huawei Technologies R&D (UK) Ltd.
 *
 * Author: Salil Mehta <salil.mehta@huawei.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the ree Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef CPU_OSPM_INTERFACE_H
#define CPU_OSPM_INTERFACE_H

#include "qapi/qapi-types-acpi.h"
#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/boards.h"

/**
 * Total size (in bytes) of the ACPI CPU OSPM Interface MMIO region.
 *
 * This region contains control and status fields such as CPU selector,
 * flags, command register, and data register. It must exactly match the
 * layout defined in the AML code and the memory region implementation.
 *
 * Any mismatch between this definition and the AML layout may result in
 * runtime errors or build-time assertion failures (e.g., _Static_assert),
 * breaking correct device emulation and guest OS coordination.
 */
#define ACPI_CPU_OSPM_IF_REG_LEN 16

typedef struct  {
    CPUState *cpu;
    uint64_t arch_id;
    bool devchk_pending; /* device-check pending */
    bool ejrqst_pending; /* eject-request pending */
    uint32_t ost_event;
    uint32_t ost_status;
} AcpiCpuOspmStateStatus;

typedef struct AcpiCpuOspmState {
    DeviceState *acpi_dev;
    MemoryRegion ctrl_reg;
    uint32_t selector;
    uint8_t command;
    uint32_t dev_count;
    AcpiCpuOspmStateStatus *devs;
} AcpiCpuOspmState;

void acpi_cpu_device_check_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                              uint32_t event_st, Error **errp);

void acpi_cpu_eject_request_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                               uint32_t event_st, Error **errp);

void acpi_cpu_eject_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                       Error **errp);

void acpi_cpu_ospm_state_interface_init(MemoryRegion *as, Object *owner,
                                        AcpiCpuOspmState *state,
                                        hwaddr base_addr);

void acpi_build_cpus_aml(Aml *table, hwaddr base_addr, const char *root,
                         const char *event_handler_method);

void acpi_cpus_ospm_status(AcpiCpuOspmState *cpu_st,
                           ACPIOSTInfoList ***list);

extern const VMStateDescription vmstate_cpu_ospm_state;
#define VMSTATE_CPU_OSPM_STATE(cpuospm, state) \
    VMSTATE_STRUCT(cpuospm, state, 1, \
                   vmstate_cpu_ospm_state, AcpiCpuOspmState)
#endif  /* CPU_OSPM_INTERFACE_H */
