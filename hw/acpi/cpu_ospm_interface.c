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
#include "migration/vmstate.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "trace.h"
#include "qapi/qapi-events-acpi.h"
#include "hw/acpi/cpu_ospm_interface.h"

/* CPU identifier and resource device */
#define CPU_NAME_FMT      "C%.03X" /* CPU name format (e.g., C001) */
#define CPU_RES_DEVICE    "CPUR" /* CPU resource device name */
#define CPU_DEVICE        "CPUS" /* CPUs device name */
#define CPU_LOCK          "CPLK" /* CPU lock object */
/* ACPI method(_STA, _EJ0, etc.) handlers */
#define CPU_STS_METHOD    "CSTA" /* CPU status method (_STA.Enabled) */
#define CPU_SCAN_METHOD   "CSCN" /* CPU scan method for enumeration */
#define CPU_NOTIFY_METHOD "CTFY" /* Notify method for CPU events */
#define CPU_EJECT_METHOD  "CEJ0" /* CPU eject method (_EJ0) */
#define CPU_OST_METHOD    "COST" /* OSPM status reporting (_OST) */
/* CPU MMIO region fields (in PRST region) */
#define CPU_SELECTOR      "CSEL" /* CPU selector index (WO) */
#define CPU_ENABLED_F     "CPEN" /* Flag: CPU enabled status(_STA) (RO) */
#define CPU_DEVCHK_F      "CDCK" /* Flag: Device-check event (RW) */
#define CPU_EJECTRQ_F     "CEJR" /* Flag: Eject-request event (RW)*/
#define CPU_EJECT_F       "CEJ0" /* Flag: Ejection trigger (WO) */
#define CPU_COMMAND       "CCMD" /* Command register (RW) */
#define CPU_DATA          "CDAT" /* Data register (RW) */

 /*
 * CPU OSPM Interface MMIO Layout (Total: 16 bytes)
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |  0x00  |  0x01  |  0x02  |  0x03  |  0x04  |  0x05  |  0x06  |  0x07  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |       Selector (DWord, write-only)         | Flags  |Command |Reserved|
 * |                                            | (RO/RW)|  (WO)  |(2B pad)|
 * |        4 bytes (32 bits)                   | 1B     |   1B   | 2B     |
 * +-----------------------------------------------------------------------+
 * |  0x08  |  0x09  |  0x0A  |  0x0B  |  0x0C  |  0x0D  |  0x0E  |  0x0F  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |                        Data (QWord, read/write)                       |
 * |               Used by CPU scan and _OST methods (64 bits)             |
 * +-----------------------------------------------------------------------+
 *
 * Field Overview:
 *
 * - Selector: 4 bytes @0x00 (DWord, WO)
 *               - Selects target CPU index for the current operation.
 * - Flags:    1 byte  @0x04 (RO/RW)
 *               - Bit 0: ENABLED  – CPU is powered on (RO)
 *               - Bit 1: DEVCHK   – Device-check completed (RW)
 *               - Bit 2: EJECTRQ  – Guest requests CPU eject (RW)
 *               - Bit 3: EJECT    – Trigger CPU ejection (WO)
 *               - Bits 4–7: Reserved (write 0)
 * - Command:  1 byte  @0x05 (WO)
 *               - Specifies control operation (e.g., scan, _OST, eject).
 * - Reserved: 2 bytes @0x06–0x07
 *               - Alignment padding; must be zero on write.
 * - Data:     8 bytes @0x08 (QWord, RW)
 *               - Input/output for command-specific data.
 *               - Used by CPU scan or _OST.
 */

/*
 * Macros defining the CPU MMIO region layout. Change field sizes here to
 * alter the overall MMIO region size.
 */
/* Sub-Field sizes (in bytes) */
#define ACPI_CPU_MR_SELECTOR_SIZE  4 /* Write-only (DWord access) */
#define ACPI_CPU_MR_FLAGS_SIZE     1 /* Read-write (Byte access) */
#define ACPI_CPU_MR_RES_FLAGS_SIZE 0 /* Reserved padding */
#define ACPI_CPU_MR_CMD_SIZE       1 /* Write-only (Byte access) */
#define ACPI_CPU_MR_RES_CMD_SIZE   2 /* Reserved padding */
#define ACPI_CPU_MR_CMD_DATA_SIZE  8 /* Read-write (QWord access) */

#define ACPI_CPU_OSPM_IF_MAX_FIELD_SIZE \
    MAX_CONST(ACPI_CPU_MR_CMD_DATA_SIZE, \
    MAX_CONST(ACPI_CPU_MR_SELECTOR_SIZE, \
    MAX_CONST(ACPI_CPU_MR_CMD_SIZE, ACPI_CPU_MR_FLAGS_SIZE)))

/* Validate layout against exported total length */
_Static_assert(ACPI_CPU_OSPM_IF_REG_LEN ==
               (ACPI_CPU_MR_SELECTOR_SIZE +
                ACPI_CPU_MR_FLAGS_SIZE +
                ACPI_CPU_MR_RES_FLAGS_SIZE +
                ACPI_CPU_MR_CMD_SIZE +
                ACPI_CPU_MR_RES_CMD_SIZE +
                ACPI_CPU_MR_CMD_DATA_SIZE),
               "ACPI_CPU_OSPM_IF_REG_LEN mismatch with internal MMIO layout");

/* Sub-Field sizes (in bits) */
#define ACPI_CPU_MR_SELECTOR_SIZE_BITS \
    (ACPI_CPU_MR_SELECTOR_SIZE * BITS_PER_BYTE)  /* Write-only (DWord Acc) */
#define ACPI_CPU_MR_FLAGS_SIZE_BITS \
    (ACPI_CPU_MR_FLAGS_SIZE * BITS_PER_BYTE)     /* Read-write (Byte Acc) */
#define ACPI_CPU_MR_RES_FLAGS_SIZE_BITS \
    (ACPI_CPU_MR_RES_FLAGS_SIZE * BITS_PER_BYTE) /* Reserved padding */
#define ACPI_CPU_MR_CMD_SIZE_BITS \
    (ACPI_CPU_MR_CMD_SIZE * BITS_PER_BYTE)       /* Write-only (Byte Acc) */
#define ACPI_CPU_MR_RES_CMD_SIZE_BITS \
    (ACPI_CPU_MR_RES_CMD_SIZE * BITS_PER_BYTE)   /* Reserved padding */
#define ACPI_CPU_MR_CMD_DATA_SIZE_BITS \
    (ACPI_CPU_MR_CMD_DATA_SIZE * BITS_PER_BYTE)  /* Read-write (QWord Acc) */

/* Field offsets (in bytes) */
#define ACPI_CPU_MR_SELECTOR_OFFSET_WO  0
#define ACPI_CPU_MR_FLAGS_OFFSET_RW \
    (ACPI_CPU_MR_SELECTOR_OFFSET_WO + \
     ACPI_CPU_MR_SELECTOR_SIZE)
#define ACPI_CPU_MR_CMD_OFFSET_WO \
    (ACPI_CPU_MR_FLAGS_OFFSET_RW + \
     ACPI_CPU_MR_FLAGS_SIZE + \
     ACPI_CPU_MR_RES_FLAGS_SIZE)
#define ACPI_CPU_MR_CMD_DATA_OFFSET_RW \
    (ACPI_CPU_MR_CMD_OFFSET_WO + \
     ACPI_CPU_MR_CMD_SIZE + \
     ACPI_CPU_MR_RES_CMD_SIZE)

/* ensure all offsets are at their natural size alignment boundaries */
#define STATIC_ASSERT_FIELD_ALIGNMENT(offset, type, field_name)               \
    _Static_assert((offset) % sizeof(type) == 0,                              \
                   field_name " is not aligned to its natural boundary")

STATIC_ASSERT_FIELD_ALIGNMENT(ACPI_CPU_MR_SELECTOR_OFFSET_WO,
                              uint32_t, "Selector");
STATIC_ASSERT_FIELD_ALIGNMENT(ACPI_CPU_MR_FLAGS_OFFSET_RW,
                              uint8_t, "Flags");
STATIC_ASSERT_FIELD_ALIGNMENT(ACPI_CPU_MR_CMD_OFFSET_WO,
                              uint8_t, "Command");
STATIC_ASSERT_FIELD_ALIGNMENT(ACPI_CPU_MR_CMD_DATA_OFFSET_RW,
                              uint64_t, "Command Data");

/* Flag bit positions (used within 'flags' subfield) */
#define ACPI_CPU_FLAGS_USED_BITS 4
#define ACPI_CPU_MR_FLAGS_BIT_ENABLED BIT(0)
#define ACPI_CPU_MR_FLAGS_BIT_DEVCHK  BIT(1)
#define ACPI_CPU_MR_FLAGS_BIT_EJECTRQ BIT(2)
#define ACPI_CPU_MR_FLAGS_BIT_EJECT   BIT(ACPI_CPU_FLAGS_USED_BITS - 1)

#define ACPI_CPU_MR_RES_FLAG_BITS (BITS_PER_BYTE - ACPI_CPU_FLAGS_USED_BITS)

enum {
    ACPI_GET_NEXT_CPU_WITH_EVENT_CMD = 0,
    ACPI_OST_EVENT_CMD = 1,
    ACPI_OST_STATUS_CMD = 2,
    ACPI_CMD_MAX
};

#define AML_APPEND_MR_RESVD_FIELD(mr_field, size_bits)       \
    do {                                                        \
        if ((size_bits) != 0) {                                 \
            aml_append((mr_field), aml_reserved_field(size_bits)); \
        }                                                       \
    } while (0)

#define AML_APPEND_MR_NAMED_FIELD(mr_field, name, size_bits)    \
    do {                                                        \
        if ((size_bits) != 0) {                                 \
            aml_append((mr_field), aml_named_field((name), (size_bits))); \
        }                                                       \
    } while (0)

#define AML_CPU_RES_DEV(base, field) \
        aml_name("%s.%s.%s", (base), CPU_RES_DEVICE, (field))

static ACPIOSTInfo *
acpi_cpu_ospm_ost_status(int idx, AcpiCpuOspmStateStatus *cdev)
{
    ACPIOSTInfo *info = g_new0(ACPIOSTInfo, 1);

    info->source = cdev->ost_event;
    info->status = cdev->ost_status;
    if (cdev->cpu) {
        DeviceState *dev = DEVICE(cdev->cpu);
        if (dev->id) {
            info->device = g_strdup(dev->id);
        }
    }
    return info;
}

void acpi_cpus_ospm_status(AcpiCpuOspmState *cpu_st, ACPIOSTInfoList ***list)
{
    ACPIOSTInfoList ***tail = list;
    int i;

    for (i = 0; i < cpu_st->dev_count; i++) {
        QAPI_LIST_APPEND(*tail, acpi_cpu_ospm_ost_status(i, &cpu_st->devs[i]));
    }
}

static uint64_t
acpi_cpu_ospm_intf_mr_read(void *opaque, hwaddr addr, unsigned size)
{
    AcpiCpuOspmState *cpu_st = opaque;
    AcpiCpuOspmStateStatus *cdev;
    uint64_t val = 0;

    if (cpu_st->selector >= cpu_st->dev_count) {
        return val;
    }
    cdev = &cpu_st->devs[cpu_st->selector];
    switch (addr) {
    case ACPI_CPU_MR_FLAGS_OFFSET_RW:
        val |= qdev_check_enabled(DEVICE(cdev->cpu)) ?
                                  ACPI_CPU_MR_FLAGS_BIT_ENABLED : 0;
        val |= cdev->devchk_pending ? ACPI_CPU_MR_FLAGS_BIT_DEVCHK : 0;
        val |= cdev->ejrqst_pending ? ACPI_CPU_MR_FLAGS_BIT_EJECTRQ : 0;
        trace_acpi_cpuos_if_read_flags(cpu_st->selector, val);
        break;
    case ACPI_CPU_MR_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case ACPI_GET_NEXT_CPU_WITH_EVENT_CMD:
           val = cpu_st->selector;
           break;
        default:
           trace_acpi_cpuos_if_read_invalid_cmd_data(cpu_st->selector,
                                                     cpu_st->command);
           break;
        }
        trace_acpi_cpuos_if_read_cmd_data(cpu_st->selector, val);
        break;
    default:
        break;
    }
    return val;
}

static void
acpi_cpu_ospm_intf_mr_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned int size)
{
    AcpiCpuOspmState *cpu_st = opaque;
    AcpiCpuOspmStateStatus *cdev;
    ACPIOSTInfo *info;

    assert(cpu_st->dev_count);
    if (addr) {
        if (cpu_st->selector >= cpu_st->dev_count) {
            trace_acpi_cpuos_if_invalid_idx_selected(cpu_st->selector);
            return;
        }
    }

    switch (addr) {
    case ACPI_CPU_MR_SELECTOR_OFFSET_WO: /* current CPU selector */
        cpu_st->selector = data;
        trace_acpi_cpuos_if_write_idx(cpu_st->selector);
        break;
    case ACPI_CPU_MR_FLAGS_OFFSET_RW: /* set is_* fields  */
        cdev = &cpu_st->devs[cpu_st->selector];
        if (data & ACPI_CPU_MR_FLAGS_BIT_DEVCHK) {
            /* clear device-check pending event */
            cdev->devchk_pending = false;
            trace_acpi_cpuos_if_clear_devchk_evt(cpu_st->selector);
        } else if (data & ACPI_CPU_MR_FLAGS_BIT_EJECTRQ) {
            /* clear eject-request pending event */
            cdev->ejrqst_pending = false;
            trace_acpi_cpuos_if_clear_ejrqst_evt(cpu_st->selector);
        } else if (data & ACPI_CPU_MR_FLAGS_BIT_EJECT) {
            DeviceState *dev = NULL;
            if (!cdev->cpu || cdev->cpu == first_cpu) {
                trace_acpi_cpuos_if_ejecting_invalid_cpu(cpu_st->selector);
                break;
            }
            /*
             * OSPM has returned with eject. Hence, it is now safe to put the
             * cpu device on powered-off state.
             */
            trace_acpi_cpuos_if_ejecting_cpu(cpu_st->selector);
            dev = DEVICE(cdev->cpu);
            qdev_sync_disable(dev, &error_fatal);
        }
        break;
    case ACPI_CPU_MR_CMD_OFFSET_WO:
        trace_acpi_cpuos_if_write_cmd(cpu_st->selector, data);
        if (data < ACPI_CMD_MAX) {
            cpu_st->command = data;
            if (cpu_st->command == ACPI_GET_NEXT_CPU_WITH_EVENT_CMD) {
                uint32_t iter = cpu_st->selector;

                do {
                    cdev = &cpu_st->devs[iter];
                    if (cdev->devchk_pending || cdev->ejrqst_pending) {
                        cpu_st->selector = iter;
                        trace_acpi_cpuos_if_cpu_has_events(cpu_st->selector,
                            cdev->devchk_pending, cdev->ejrqst_pending);
                        break;
                    }
                    iter = iter + 1 < cpu_st->dev_count ? iter + 1 : 0;
                } while (iter != cpu_st->selector);
            }
        }
        break;
    case ACPI_CPU_MR_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case ACPI_OST_EVENT_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_event = data;
           trace_acpi_cpuos_if_write_ost_ev(cpu_st->selector, cdev->ost_event);
           break;
        }
        case ACPI_OST_STATUS_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_status = data;
           info = acpi_cpu_ospm_ost_status(cpu_st->selector, cdev);
           qapi_event_send_acpi_device_ost(info);
           qapi_free_ACPIOSTInfo(info);
           trace_acpi_cpuos_if_write_ost_status(cpu_st->selector,
                                                cdev->ost_status);
           break;
        }
        default:
           trace_acpi_cpuos_if_write_invalid_cmd(cpu_st->selector,
                                                 cpu_st->command);
           break;
        }
        break;
    default:
        trace_acpi_cpuos_if_write_invalid_offset(cpu_st->selector, addr);
        break;
    }
}

static const MemoryRegionOps cpu_common_mr_ops = {
    .read = acpi_cpu_ospm_intf_mr_read,
    .write = acpi_cpu_ospm_intf_mr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = ACPI_CPU_OSPM_IF_MAX_FIELD_SIZE,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = ACPI_CPU_OSPM_IF_MAX_FIELD_SIZE,
        .unaligned = false,
    },
};

void acpi_cpu_ospm_state_interface_init(MemoryRegion *as, Object *owner,
                                        AcpiCpuOspmState *state,
                                        hwaddr base_addr)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *id_list;
    int i;

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    state->dev_count = id_list->len;
    state->devs = g_new0(typeof(*state->devs), state->dev_count);
    for (i = 0; i < id_list->len; i++) {
        state->devs[i].cpu =  CPU(id_list->cpus[i].cpu);
        state->devs[i].arch_id = id_list->cpus[i].arch_id;
    }
    memory_region_init_io(&state->ctrl_reg, owner, &cpu_common_mr_ops, state,
                          "ACPI CPU OSPM State Interface Memory Region",
                          ACPI_CPU_OSPM_IF_REG_LEN);
    memory_region_add_subregion(as, base_addr, &state->ctrl_reg);
}

static AcpiCpuOspmStateStatus *
acpi_get_cpu_status(AcpiCpuOspmState *cpu_st, DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    uint64_t cpu_arch_id = k->get_arch_id(CPU(dev));
    int i;

    for (i = 0; i < cpu_st->dev_count; i++) {
        if (cpu_arch_id == cpu_st->devs[i].arch_id) {
            return &cpu_st->devs[i];
        }
    }
    return NULL;
}

void acpi_cpu_device_check_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                              uint32_t event_st, Error **errp)
{
    AcpiCpuOspmStateStatus *cdev;
    cdev = acpi_get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }
    assert(cdev->cpu);

    /*
     * Tell OSPM via GED IRQ(GSI) that a powered-off cpu is being powered-on.
     * Also, mark 'device-check' event pending for this cpu. This will
     * eventually result in OSPM evaluating the ACPI _EVT method and scan of
     * cpus
     */
    cdev->devchk_pending = true;
    acpi_send_event(cpu_st->acpi_dev, event_st);
}

void acpi_cpu_eject_request_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev,
                              uint32_t event_st, Error **errp)
{
    AcpiCpuOspmStateStatus *cdev;
    cdev = acpi_get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }
    assert(cdev->cpu);

    /*
     * Tell OSPM via GED IRQ(GSI) that a cpu wants to power-off or go on standby
     * Also,mark 'eject-request' event pending for this cpu. (graceful shutdown)
     */
    cdev->ejrqst_pending = true;
    acpi_send_event(cpu_st->acpi_dev, event_st);
}

void
acpi_cpu_eject_cb(AcpiCpuOspmState *cpu_st, DeviceState *dev, Error **errp)
{
    /* TODO: possible handling here */
}

static const VMStateDescription vmstate_cpu_ospm_state_sts = {
    .name = "CPU OSPM state status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(devchk_pending, AcpiCpuOspmStateStatus),
        VMSTATE_BOOL(ejrqst_pending, AcpiCpuOspmStateStatus),
        VMSTATE_UINT32(ost_event, AcpiCpuOspmStateStatus),
        VMSTATE_UINT32(ost_status, AcpiCpuOspmStateStatus),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cpu_ospm_state = {
    .name = "CPU OSPM state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(selector, AcpiCpuOspmState),
        VMSTATE_UINT8(command, AcpiCpuOspmState),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(devs, AcpiCpuOspmState,
                                             dev_count,
                                             vmstate_cpu_ospm_state_sts,
                                             AcpiCpuOspmStateStatus),
        VMSTATE_END_OF_LIST()
    }
};

void acpi_build_cpus_aml(Aml *table, hwaddr base_addr, const char *root,
                         const char *event_handler_method)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
    Aml *sb_scope = aml_scope("_SB"); /* System Bus Scope */
    Aml *ifctx, *field, *method, *cpu_res_dev, *cpus_dev;
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);

    cpu_res_dev = aml_device("%s.%s", root, CPU_RES_DEVICE);
    {
        Aml *crs;

        aml_append(cpu_res_dev,
            aml_name_decl("_HID", aml_eisaid("PNP0A06")));
        aml_append(cpu_res_dev,
            aml_name_decl("_UID", aml_string("CPU OSPM Interface resources")));
        aml_append(cpu_res_dev, aml_mutex(CPU_LOCK, 0));

        crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(base_addr, ACPI_CPU_OSPM_IF_REG_LEN,
                   AML_READ_WRITE));

        aml_append(cpu_res_dev, aml_name_decl("_CRS", crs));

        /* declare CPU OSPM Interface MMIO region related access fields */
        aml_append(cpu_res_dev,
                   aml_operation_region("PRST", AML_SYSTEM_MEMORY,
                                        aml_int(base_addr),
                                        ACPI_CPU_OSPM_IF_REG_LEN));

        /*
         * define named fields within PRST region with 'Byte' access widths
         * and reserve fields with other access width
         */
        field = aml_field("PRST", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
        /* reserve CPU 'selector' field (size in bits) */
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_SELECTOR_SIZE_BITS);
        /* Flag::Enabled Bit(RO) - Read '1' if enabled */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_ENABLED_F, 1);
        /* Flag::Devchk Bit(RW) - Read '1', has a event. Write '1', to clear */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_DEVCHK_F, 1);
        /* Flag::Ejectrq Bit(RW) - Read 1, has event. Write 1 to clear */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_EJECTRQ_F, 1);
        /* Flag::Eject Bit(WO) - OSPM evals _EJx, initiates CPU Eject in Qemu*/
        AML_APPEND_MR_NAMED_FIELD(field, CPU_EJECT_F, 1);
        /* Flag::Bit(ACPI_CPU_FLAGS_USED_BITS)-Bit(7) - Reserve left over bits*/
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_RES_FLAG_BITS);
        /* Reserved space: padding after flags */
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_RES_FLAGS_SIZE_BITS);
        /* Command field written by OSPM */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_COMMAND,
                                  ACPI_CPU_MR_CMD_SIZE_BITS);
        /* Reserved space: padding after command field */
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_RES_CMD_SIZE_BITS);
        /* Command data: 64-bit payload associated with command */
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_CMD_DATA_SIZE_BITS);
        aml_append(cpu_res_dev, field);

        /*
         * define named fields with 'Dword' access widths and reserve fields
         * with other access width
         */
        field = aml_field("PRST", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
        /* CPU selector, write only */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_SELECTOR,
                                  ACPI_CPU_MR_SELECTOR_SIZE_BITS);
        aml_append(cpu_res_dev, field);

        /*
         * define named fields with 'Qword' access widths and reserve fields
         * with other access width
         */
        field = aml_field("PRST", AML_QWORD_ACC, AML_NOLOCK, AML_PRESERVE);
        /*
         * Reserve space: selector, flags, reserved flags, command, reserved
         * command for Qword alignment.
         */
        AML_APPEND_MR_RESVD_FIELD(field, ACPI_CPU_MR_SELECTOR_SIZE_BITS +
                                            ACPI_CPU_MR_FLAGS_SIZE_BITS +
                                            ACPI_CPU_MR_RES_FLAGS_SIZE_BITS +
                                            ACPI_CPU_MR_CMD_SIZE_BITS +
                                            ACPI_CPU_MR_RES_CMD_SIZE_BITS);
        /* Command data accessible via Qword */
        AML_APPEND_MR_NAMED_FIELD(field, CPU_DATA,
                                  ACPI_CPU_MR_CMD_DATA_SIZE_BITS);
        aml_append(cpu_res_dev, field);
    }
    aml_append(sb_scope, cpu_res_dev);

    cpus_dev = aml_device("%s.%s", root, CPU_DEVICE);
    {
        Aml *ctrl_lock = AML_CPU_RES_DEV(root, CPU_LOCK);
        Aml *cpu_selector = AML_CPU_RES_DEV(root, CPU_SELECTOR);
        Aml *is_enabled = AML_CPU_RES_DEV(root, CPU_ENABLED_F);
        Aml *dvchk_evt = AML_CPU_RES_DEV(root, CPU_DEVCHK_F);
        Aml *ejrq_evt = AML_CPU_RES_DEV(root, CPU_EJECTRQ_F);
        Aml *ej_evt = AML_CPU_RES_DEV(root, CPU_EJECT_F);
        Aml *cpu_cmd = AML_CPU_RES_DEV(root, CPU_COMMAND);
        Aml *cpu_data = AML_CPU_RES_DEV(root, CPU_DATA);
        int i;

        aml_append(cpus_dev, aml_name_decl("_HID", aml_string("ACPI0010")));
        aml_append(cpus_dev, aml_name_decl("_CID", aml_eisaid("PNP0A05")));

        method = aml_method(CPU_NOTIFY_METHOD, 2, AML_NOTSERIALIZED);
        for (i = 0; i < arch_ids->len; i++) {
            Aml *cpu = aml_name(CPU_NAME_FMT, i);
            Aml *uid = aml_arg(0);
            Aml *event = aml_arg(1);

            ifctx = aml_if(aml_equal(uid, aml_int(i)));
            {
                aml_append(ifctx, aml_notify(cpu, event));
            }
            aml_append(method, ifctx);
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_STS_METHOD, 1, AML_SERIALIZED);
        {
            Aml *idx = aml_arg(0);
            Aml *sta = aml_local(0);
            Aml *else_ctx;

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(idx, cpu_selector));
            aml_append(method, aml_store(zero, sta));
            ifctx = aml_if(aml_equal(is_enabled, one));
            {
                /* cpu is present and enabled */
                aml_append(ifctx, aml_store(aml_int(0xF), sta));
            }
            aml_append(method, ifctx);
            else_ctx = aml_else();
            {
                /* cpu is present but disabled */
                aml_append(else_ctx, aml_store(aml_int(0xD), sta));
            }
            aml_append(method, else_ctx);
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(sta));
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_EJECT_METHOD, 1, AML_SERIALIZED);
        {
            Aml *idx = aml_arg(0);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(idx, cpu_selector));
            aml_append(method, aml_store(one, ej_evt));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_SCAN_METHOD, 0, AML_SERIALIZED);
        {
            Aml *has_event = aml_local(0); /* Local0: Loop control flag */
            Aml *uid = aml_local(1); /* Local1: Current CPU UID */
            /* Constants */
            Aml *dev_chk = aml_int(1); /* Notify: device check to enable */
            Aml *eject_req = aml_int(3); /* Notify: eject for removal */
            Aml *next_cpu_cmd = aml_int(ACPI_GET_NEXT_CPU_WITH_EVENT_CMD);

            /* Acquire CPU lock */
            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));

            /* Initialize loop */
            aml_append(method, aml_store(zero, uid));
            aml_append(method, aml_store(one, has_event));

            Aml *while_ctx = aml_while(aml_land(
                aml_equal(has_event, one),
                aml_lless(uid, aml_int(arch_ids->len))
            ));
            {
                aml_append(while_ctx, aml_store(zero, has_event));
                /*
                 * Issue scan cmd: QEMU will return next CPU with event in
                 * cpu_data
                 */
                aml_append(while_ctx, aml_store(uid, cpu_selector));
                aml_append(while_ctx, aml_store(next_cpu_cmd, cpu_cmd));

                /* If scan wrapped around to an earlier UID, exit loop */
                Aml *wrap_check = aml_if(aml_lless(cpu_data, uid));
                aml_append(wrap_check, aml_break());
                aml_append(while_ctx, wrap_check);

                /* Set UID to scanned result */
                aml_append(while_ctx, aml_store(cpu_data, uid));

                /* send CPU device-check(resume) event to OSPM */
                Aml *if_devchk = aml_if(aml_equal(dvchk_evt, one));
                {
                    aml_append(if_devchk,
                        aml_call2(CPU_NOTIFY_METHOD, uid, dev_chk));
                    /* clear local device-check event sent flag */
                    aml_append(if_devchk, aml_store(one, dvchk_evt));
                    aml_append(if_devchk, aml_store(one, has_event));
                }
                aml_append(while_ctx, if_devchk);

                /*
                 * send CPU eject-request event to OSPM to gracefully handle
                 * OSPM related tasks running on this CPU
                 */
                Aml *else_ctx = aml_else();
                Aml *if_ejrq = aml_if(aml_equal(ejrq_evt, one));
                {
                    aml_append(if_ejrq,
                        aml_call2(CPU_NOTIFY_METHOD, uid, eject_req));
                    /* clear local eject-request event sent flag */
                    aml_append(if_ejrq, aml_store(one, ejrq_evt));
                    aml_append(if_ejrq, aml_store(one, has_event));
                }
                aml_append(else_ctx, if_ejrq);
                aml_append(while_ctx, else_ctx);

                /* Increment UID */
                aml_append(while_ctx, aml_increment(uid));
            }
            aml_append(method, while_ctx);

            /* Release cpu lock */
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_OST_METHOD, 4, AML_SERIALIZED);
        {
            Aml *uid = aml_arg(0);
            Aml *ev_cmd = aml_int(ACPI_OST_EVENT_CMD);
            Aml *st_cmd = aml_int(ACPI_OST_STATUS_CMD);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(uid, cpu_selector));
            aml_append(method, aml_store(ev_cmd, cpu_cmd));
            aml_append(method, aml_store(aml_arg(1), cpu_data));
            aml_append(method, aml_store(st_cmd, cpu_cmd));
            aml_append(method, aml_store(aml_arg(2), cpu_data));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        /* build Processor object for each processor */
        for (i = 0; i < arch_ids->len; i++) {
            Aml *dev;
            Aml *uid = aml_int(i);

            dev = aml_device(CPU_NAME_FMT, i);
            aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
            aml_append(dev, aml_name_decl("_UID", uid));

            method = aml_method("_STA", 0, AML_SERIALIZED);
            aml_append(method, aml_return(aml_call1(CPU_STS_METHOD, uid)));
            aml_append(dev, method);

            if (CPU(arch_ids->cpus[i].cpu) != first_cpu) {
                method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
                aml_append(method, aml_call1(CPU_EJECT_METHOD, uid));
                aml_append(dev, method);
            }

            method = aml_method("_OST", 3, AML_SERIALIZED);
            aml_append(method,
                aml_call4(CPU_OST_METHOD, uid, aml_arg(0),
                          aml_arg(1), aml_arg(2))
            );
            aml_append(dev, method);
            aml_append(cpus_dev, dev);
        }
    }
    aml_append(sb_scope, cpus_dev);
    aml_append(table, sb_scope);

    method = aml_method(event_handler_method, 0, AML_NOTSERIALIZED);
    aml_append(method, aml_call0("\\_SB.CPUS." CPU_SCAN_METHOD));
    aml_append(table, method);
}
