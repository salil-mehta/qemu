/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM Generic Interrupt Controller v3
 *
 * Copyright (c) 2016 Linaro Limited
 * Written by Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "gicv3_internal.h"
#include "cpu.h"
#include "qemu/log.h"
#include "monitor/monitor.h"
#include "qapi/visitor.h"

void gicv3_set_gicv3state(CPUState *cpu, GICv3CPUState *s)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    env->gicv3state = (void *)s;
};

static void
gicv3_get_gicc_accessibility(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    GICv3CPUState *cs = (GICv3CPUState *)opaque;
    bool value = cs->gicc_accessible;

    visit_type_bool(v, name, &value, errp);
}

static void
gicv3_set_gicc_accessibility(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    GICv3CPUState *gcs = opaque;
    CPUState *cs = gcs->cpu;
    bool value;

    visit_type_bool(v, name, &value, errp);

    /* Block external attempts to set */
    if (monitor_cur_is_qmp()) {
        error_setg(errp, "Property 'gicc-accessible' is read-only externally");
        return;
    }

    if (gcs->gicc_accessible != value) {
        gcs->gicc_accessible = value;

        qemu_log_mask(LOG_UNIMP,
                      "GICC accessibility changed: vCPU %d = %s\n",
                      cs->cpu_index, value ? "accessible" : "inaccessible");
    }
}

void gicv3_init_cpuif(GICv3State *s)
{
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_GET_CLASS(s);
    int i;

    /* define and register `system registers` with the vCPU  */
    for (i = 0; i < s->num_cpu; i++) {
        g_autofree char *propname = g_strdup_printf("gicc-accessible[%d]", i);
        object_property_add(OBJECT(s), propname, "bool",
                            gicv3_get_gicc_accessibility,
                            gicv3_set_gicc_accessibility,
                            NULL, &s->cpu[i]);

        object_property_set_description(OBJECT(s), propname,
            "Per-vCPU GICC interface accessibility (internal set only)");

        agcc->init_cpu_reginfo(s->cpu[i].cpu);
    }
}
