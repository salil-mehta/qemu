#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/acpi/cpu.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "qapi/qapi-events-acpi.h"
#include "trace.h"
#include "sysemu/numa.h"

#define ACPI_CPU_SELECTOR_OFFSET_WR 0
#define ACPI_CPU_FLAGS_OFFSET_RW 4
#define ACPI_CPU_CMD_OFFSET_WR 5
#define ACPI_CPU_CMD_DATA_OFFSET_RW 8

enum {
    ACPI_GET_NEXT_CPU_WITH_EVENT_CMD = 0,
    ACPI_OST_EVENT_CMD = 1,
    ACPI_OST_STATUS_CMD = 2,
    ACPI_CMD_MAX
};

static ACPIOSTInfo *
acpi_cpu_device_standby_status(int idx, AcpiCpuStatus *cdev)
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

void
acpi_cpu_ospm_standby_status(CPUStandbyState *cpu_st, ACPIOSTInfoList ***list)
{
    ACPIOSTInfoList ***tail = list;
    int i;

    for (i = 0; i < cpu_st->dev_count; i++) {
        QAPI_LIST_APPEND(*tail,
                         acpi_cpu_device_standby_status(i, &cpu_st->devs[i]));
    }
}

static bool check_cpu_enabled_status(DeviceState *dev)
{
    CPUClass *k = dev ? CPU_GET_CLASS(dev) : NULL;
    CPUState *cpu = CPU(dev);

    if (cpu && (!k->cpu_enabled_status || k->cpu_enabled_status(cpu))) {
        return true;
    }

    return false;
}

static uint64_t
acpi_cpu_device_mr_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    CPUStandbyState *cpu_st = opaque;
    AcpiCpuStatus *cdev;

    if (cpu_st->selector >= cpu_st->dev_count) {
        return val;
    }

    cdev = &cpu_st->devs[cpu_st->selector];
    switch (addr) {
    case ACPI_CPU_FLAGS_OFFSET_RW: /* pack and return is_* fields */
        val |= check_cpu_enabled_status(DEVICE(cdev->cpu)) ? 1 : 0;
        val |= cdev->is_inserting ? 2 : 0;
        val |= cdev->is_removing  ? 4 : 0;
        //val |= cdev->fw_remove  ? 16 : 0;
        val |= cdev->cpu ? 32 : 0;
        trace_cpusb_acpi_read_flags(cpu_st->selector, val);
        break;
    case ACPI_CPU_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case ACPI_GET_NEXT_CPU_WITH_EVENT_CMD:
           val = cpu_st->selector;
           break;
        default:
           break;
        }
        trace_cpusb_acpi_read_cmd_data(cpu_st->selector, val);
        break;
    default:
        break;
    }
    return val;
}

static void
acpi_cpu_device_mr_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned int size)
{
    CPUStandbyState *cpu_st = opaque;
    AcpiCpuStatus *cdev;
    ACPIOSTInfo *info;

    assert(cpu_st->dev_count);

    if (addr) {
        if (cpu_st->selector >= cpu_st->dev_count) {
            trace_cpusb_acpi_invalid_idx_selected(cpu_st->selector);
            return;
        }
    }

    switch (addr) {
    case ACPI_CPU_SELECTOR_OFFSET_WR: /* current CPU selector */
        cpu_st->selector = data;
        trace_cpusb_acpi_write_idx(cpu_st->selector);
        break;
    case ACPI_CPU_FLAGS_OFFSET_RW: /* set is_* fields  */
        cdev = &cpu_st->devs[cpu_st->selector];
        if (data & 2) { /* clear insert event */
            cdev->is_inserting = false;
            trace_cpusb_acpi_clear_inserting_evt(cpu_st->selector);
        } else if (data & 4) { /* clear remove event */
            cdev->is_removing = false;
            trace_cpusb_acpi_clear_remove_evt(cpu_st->selector);
        } else if (data & 8) {
            DeviceState *dev = NULL;
            StandbyHandler *handler = NULL;

            if (!cdev->cpu || cdev->cpu == first_cpu) {
                trace_cpusb_acpi_ejecting_invalid_cpu(cpu_st->selector);
                break;
            }
            /* 
             * OSPM has returned with eject. Hence, it is now safe to put the
             * cpu device on standby
             */
            trace_cpusb_acpi_ejecting_cpu(cpu_st->selector);
            dev = DEVICE(cdev->cpu);
            handler = qdev_get_standby_handler(dev);
            standby_handler_enter(handler, dev, NULL);
            //object_unparent(OBJECT(dev));
        }
        break;
    case ACPI_CPU_CMD_OFFSET_WR:
        trace_cpusb_acpi_write_cmd(cpu_st->selector, data);
        if (data < ACPI_CMD_MAX) {
            cpu_st->command = data;
            if (cpu_st->command == ACPI_GET_NEXT_CPU_WITH_EVENT_CMD) {
                uint32_t iter = cpu_st->selector;

                do {
                    cdev = &cpu_st->devs[iter];
                    if (cdev->is_inserting || cdev->is_removing) {
                        cpu_st->selector = iter;
                        trace_cpusb_acpi_cpu_has_events(cpu_st->selector,
                            cdev->is_inserting, cdev->is_removing);
                        break;
                    }
                    iter = iter + 1 < cpu_st->dev_count ? iter + 1 : 0;
                } while (iter != cpu_st->selector);
            }
        }
        break;
    case ACPI_CPU_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case ACPI_OST_EVENT_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_event = data;
           trace_cpusb_acpi_write_ost_ev(cpu_st->selector, cdev->ost_event);
           break;
        }
        case ACPI_OST_STATUS_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_status = data;
           info = acpi_cpu_device_status(cpu_st->selector, cdev);
           qapi_event_send_acpi_device_ost(info);
           qapi_free_ACPIOSTInfo(info);
           trace_cpusb_acpi_write_ost_status(cpu_st->selector,
                                             cdev->ost_status);
           break;
        }
        default:
           break;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps cpu_device_mr_ops = {
    .read = acpi_cpu_device_mr_read,
    .write = acpi_cpu_device_mr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

void cpu_standby_hw_init(MemoryRegion *as, Object *owner,
                         CPUStandbyState *state, hwaddr base_addr)
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
    memory_region_init_io(&state->ctrl_reg, owner, &cpu_device_mr_ops, state,
                          "acpi-cpu-standby", ACPI_CPU_STANDBY_REG_LEN);
    memory_region_add_subregion(as, base_addr, &state->ctrl_reg);
}

static bool should_remain_acpi_present(DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    /*
     * A system may contain CPUs that are always present on one die, NUMA node,
     * or socket, yet may be non-present on another simultaneously. Check from
     * architecture specific code.
     */
    return k->cpu_persistent_status && k->cpu_persistent_status(CPU(dev));
}

static AcpiCpuStatus *get_cpu_status(CPUStandbyState *cpu_st, DeviceState *dev)
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

void
acpi_ged_device_resume_cb(StandbyHandler *handler, CPUStandbyState *cpu_st,
                          DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    assert(cdev->cpu);

    // cdev->cpu = CPU(dev);
    //if (dev->hotplugged) {
    if (phase_check(PHASE_MACHINE_READY)) {
        cdev->is_inserting = true; /* ACPI device check in progress */
        acpi_send_event(DEVICE(handler), ACPI_CPU_STANDBY_STATUS);
    }
}

void acpi_cpu_standby_request_cb(StandbyHandler *handler,
                                CPUStandbyState *cpu_st,
                                DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    assert(cdev->cpu);

    cdev->is_removing = true; /* ACPI device remove in progress */
    acpi_send_event(DEVICE(handler), ACPI_CPU_STANDBY_STATUS);
}

void acpi_cpu_standby_cb(CPUStandbyState *cpu_st,
                        DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    if (!should_remain_acpi_present(dev)) {
        cdev->cpu = NULL;
    }
}

static const VMStateDescription vmstate_cpu_standby_sts = {
    .name = "CPU standby status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(is_inserting, AcpiCpuStatus),
        VMSTATE_BOOL(is_removing, AcpiCpuStatus),
        VMSTATE_UINT32(ost_event, AcpiCpuStatus),
        VMSTATE_UINT32(ost_status, AcpiCpuStatus),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cpu_standby = {
    .name = "CPU standby state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(selector, CPUStandbyState),
        VMSTATE_UINT8(command, CPUStandbyState),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(devs, CPUStandbyState, dev_count,
                                             vmstate_cpu_standby_sts,
                                             AcpiCpuStatus),
        VMSTATE_END_OF_LIST()
    }
};

#define CPU_NAME_FMT      "C%.03X"
#define CPUSB_RES_DEVICE  "PRSB"
#define CPU_LOCK          "CPLK"
#define CPU_STS_METHOD    "CSTA"
#define CPU_SCAN_METHOD   "CSCN"
#define CPU_NOTIFY_METHOD "CTFY"
#define CPU_EJECT_METHOD  "CEJ0"
#define CPU_OST_METHOD    "COST"
#define CPU_ADDED_LIST    "CNEW"

#define CPU_ENABLED       "CPEN"
#define CPU_SELECTOR      "CSEL"
#define CPU_COMMAND       "CCMD"
#define CPU_DATA          "CDAT"
#define CPU_INSERT_EVENT  "CINS"
#define CPU_REMOVE_EVENT  "CRMV"
#define CPU_EJECT_EVENT   "CEJ0"
#define CPU_PRESENT       "CPRS"

void build_cpus_standby_aml(Aml *table, hwaddr base_addr, const char *res_root,
                            const char *event_handler_method)
{
    Aml *ifctx;
    Aml *field;
    Aml *method;
    Aml *cpu_ctrl_dev;
    Aml *cpus_dev;
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    Aml *sb_scope = aml_scope("_SB");
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
    char *cphp_res_path = g_strdup_printf("%s." CPUSB_RES_DEVICE, res_root);

    cpu_ctrl_dev = aml_device("%s", cphp_res_path);
    {
        Aml *crs;

        aml_append(cpu_ctrl_dev,
            aml_name_decl("_HID", aml_eisaid("PNP0A06")));
        aml_append(cpu_ctrl_dev,
            aml_name_decl("_UID", aml_string("CPU Standby resources")));
        aml_append(cpu_ctrl_dev, aml_mutex(CPU_LOCK, 0));

        crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(base_addr, AML_SYSTEM_MEMORY,
                   AML_READ_WRITE));

        aml_append(cpu_ctrl_dev, aml_name_decl("_CRS", crs));

        /* declare CPU standby MMIO region with related access fields */
        aml_append(cpu_ctrl_dev,
            aml_operation_region("PRST", AML_SYSTEM_MEMORY, aml_int(base_addr),
                                 ACPI_CPU_STANDBY_REG_LEN));

        field = aml_field("PRST", AML_BYTE_ACC, AML_NOLOCK,
                          AML_WRITE_AS_ZEROS);
        aml_append(field, aml_reserved_field(ACPI_CPU_FLAGS_OFFSET_RW * 8));
        /* 1 if enabled, read only */
        aml_append(field, aml_named_field(CPU_ENABLED, 1));
        /* 1 if present, read only */
        aml_append(field, aml_named_field(CPU_PRESENT, 1));
        /* (read) 1 if has a insert event. (write) 1 to clear event */
        aml_append(field, aml_named_field(CPU_INSERT_EVENT, 1));
        /* (read) 1 if has a remove event. (write) 1 to clear event */
        aml_append(field, aml_named_field(CPU_REMOVE_EVENT, 1));
        /* initiates device eject, write only */
        aml_append(field, aml_named_field(CPU_EJECT_EVENT, 1));
        aml_append(field, aml_reserved_field(3));
        aml_append(field, aml_named_field(CPU_COMMAND, 8));
        aml_append(cpu_ctrl_dev, field);

        field = aml_field("PRST", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
        /* CPU selector, write only */
        aml_append(field, aml_named_field(CPU_SELECTOR, 32));
        /* flags + cmd + 2byte align */
        aml_append(field, aml_reserved_field(4 * 8));
        aml_append(field, aml_named_field(CPU_DATA, 32));
        aml_append(cpu_ctrl_dev, field);
    }
    aml_append(sb_scope, cpu_ctrl_dev);

    cpus_dev = aml_device("\\_SB.CPUS");
    {
        int i;
        Aml *ctrl_lock = aml_name("%s.%s", cphp_res_path, CPU_LOCK);
        Aml *cpu_selector = aml_name("%s.%s", cphp_res_path, CPU_SELECTOR);
        Aml *is_enabled = aml_name("%s.%s", cphp_res_path, CPU_ENABLED);
        Aml *is_present = aml_name("%s.%s", cphp_res_path, CPU_PRESENT);
        Aml *cpu_cmd = aml_name("%s.%s", cphp_res_path, CPU_COMMAND);
        Aml *cpu_data = aml_name("%s.%s", cphp_res_path, CPU_DATA);
        Aml *ins_evt = aml_name("%s.%s", cphp_res_path, CPU_INSERT_EVENT);
        Aml *rm_evt = aml_name("%s.%s", cphp_res_path, CPU_REMOVE_EVENT);
        Aml *ej_evt = aml_name("%s.%s", cphp_res_path, CPU_EJECT_EVENT);

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
            Aml *ifctx2;
            Aml *else_ctx;

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(idx, cpu_selector));
            aml_append(method, aml_store(zero, sta));
            ifctx = aml_if(aml_equal(is_present, one));
            {
                ifctx2 = aml_if(aml_equal(is_enabled, one));
                {
                    /* cpu is present and enabled */
                    aml_append(ifctx2, aml_store(aml_int(0xF), sta));
                }
                aml_append(ifctx, ifctx2);
                else_ctx = aml_else();
                {
                    /* cpu is present but disabled */
                    aml_append(else_ctx, aml_store(aml_int(0xD), sta));
                }
                aml_append(ifctx, else_ctx);
            }
            aml_append(method, ifctx);
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
            Aml *dev_chk = aml_int(1); /* Notify: device check for insert */
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

                /* Handle Insert Event */
                Aml *if_ins = aml_if(aml_equal(ins_evt, one));
                {
                    aml_append(if_ins,
                        aml_call2(CPU_NOTIFY_METHOD, uid, dev_chk));
                    /* clear insert (device check) event */
                    aml_append(if_ins, aml_store(one, ins_evt));
                    aml_append(if_ins, aml_store(one, has_event));
                }
                aml_append(while_ctx, if_ins);

                /* Handle Remove Event */
                Aml *else_ctx = aml_else();
                Aml *if_rm = aml_if(aml_equal(rm_evt, one));
                {
                    aml_append(if_rm,
                        aml_call2(CPU_NOTIFY_METHOD, uid, eject_req));
                    /* clear remove event */
                    aml_append(if_rm, aml_store(one, rm_evt));
                    aml_append(if_rm, aml_store(one, has_event));
                }
                aml_append(else_ctx, if_rm);
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
            GArray *madt_buf = g_array_new(0, 1, 1);
            int arch_id = arch_ids->cpus[i].arch_id;

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

    g_free(cphp_res_path);
}
