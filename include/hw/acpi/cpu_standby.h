/*
 * Qemu ACPI Utilities for standby CPUs.
 *
 * Copyright (c) 2025 Huawei Technologies R&D (UK) Ltd.
 *
 * Author: Salil Mehta <salil.mehta@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef ACPI_CPU_STANDBY_H
#define ACPI_CPU_STANDBY_H

#include "qapi/qapi-types-acpi.h"
#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/boards.h"

#define ACPI_CPU_STANDBY_REG_LEN 12

typedef struct AcpiCpuStandbyStatus {
    CPUState *cpu;
    uint64_t arch_id;
    bool devchk_pending; /* device-check pending */
    bool ejrqst_pending; /* eject-request pending */
    uint32_t ost_event;
    uint32_t ost_status;
} AcpiCpuStandbyStatus;

typedef struct CPUStandbyState {
    MemoryRegion ctrl_reg;
    uint32_t selector;
    uint8_t command;
    uint32_t dev_count;
    AcpiCpuStandbyStatus *devs;
} CPUStandbyState;

void acpi_cpu_resume_cb(StandbyHandler *handler, CPUStandbyState *cpu_st,
                        DeviceState *dev, Error **errp);

void acpi_cpu_standby_request_cb(StandbyHandler *handler,
                                 CPUStandbyState *cpu_st, DeviceState *dev,
                                 Error **errp);

void acpi_cpu_standby_cb(CPUStandbyState *cpu_st, DeviceState *dev,
                         Error **errp);

void cpu_standby_hw_init(MemoryRegion *as, Object *owner,
                         CPUStandbyState *state, hwaddr base_addr);

void build_cpus_standby_aml(Aml *table, hwaddr base_addr, const char *res_root,
                            const char *event_handler_method);

void acpi_cpu_ospm_standby_status(CPUStandbyState *cpu_st,
                                  ACPIOSTInfoList ***list);

extern const VMStateDescription vmstate_cpu_standby;
#define VMSTATE_CPU_STANDBY(cpusb, state) \
    VMSTATE_STRUCT(cpusb, state, 1, \
                   vmstate_cpu_standby, CPUStandbyState)
#endif
