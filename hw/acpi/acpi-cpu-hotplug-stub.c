#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/acpi/cpu.h"

/* Following stubs are all related to ACPI cpu hotplug(like features) */
const VMStateDescription vmstate_cpu_hotplug;

void cpu_hotplug_hw_init(MemoryRegion *as, Object *owner,
                         CPUHotplugState *state, hwaddr base_addr)
{
}

void acpi_cpu_ospm_status(CPUHotplugState *cpu_st, ACPIOSTInfoList ***list)
{
}

void acpi_cpu_plug_cb(DeviceState *acpi_dev,
                      CPUHotplugState *cpu_st, DeviceState *dev, Error **errp)
{
}

void acpi_cpu_unplug_cb(CPUHotplugState *cpu_st,
                        DeviceState *dev, Error **errp)
{
}

void acpi_cpu_unplug_request_cb(DeviceState *acpi_dev,
                                CPUHotplugState *cpu_st,
                                DeviceState *dev, Error **errp)
{
}
