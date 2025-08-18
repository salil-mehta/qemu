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
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/acpi/cpu_ospm_interface.h"

void acpi_cpu_device_check_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                              uint32_t event_st, Error **errp)
{
}

void acpi_cpu_eject_request_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                               uint32_t event_st, Error **errp)
{
}

void acpi_cpu_eject_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev, Error **errp)
{
}

void acpi_cpu_ospm_state_interface_init(MemoryRegion *as, Object *owner,
                                        AcpiCpuOspmState *state,
                                        hwaddr base_addr)
{
}

void acpi_cpus_ospm_status(AcpiCpuOspmState *cpu_st, ACPIOSTInfoList ***list)
{
}
