/*
 * Device Power State handler interface.
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
#include "qemu/osdep.h"
#include "hw/powerstate.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/boards.h"

PowerStateHandler *powerstate_handler(DeviceState *dev)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);

   if (mc->get_powerstate_handler) {
        return (PowerStateHandler *)mc->get_powerstate_handler(machine, dev);
   }

    return NULL;
}

DeviceOperPowerState qdev_get_oper_power_state(DeviceState *dev)
{
    PowerStateHandler *h = powerstate_handler(dev);
    PowerStateHandlerClass *pshc = h ? POWERSTATE_HANDLER_GET_CLASS(h) : NULL;

    if (pshc && pshc->get_oper_state) {
        return pshc->get_oper_state(dev, &error_warn);
    }

    return DEVICE_OPER_POWER_STATE_UNKNOWN;
}

void handle_poweroff_request(PowerStateHandler *handler, DeviceState *dev,
                                Error **errp)
{
    PowerStateHandlerClass *pshc = POWERSTATE_HANDLER_GET_CLASS(handler);

    if (pshc->poweroff_request) {
        pshc->poweroff_request(handler, dev, errp);
    }
}

void handle_poweroff(PowerStateHandler *handler, DeviceState *dev,
                            Error **errp)
{
    PowerStateHandlerClass *pshc = POWERSTATE_HANDLER_GET_CLASS(handler);

    if (pshc->poweroff) {
        pshc->poweroff(handler, dev, errp);
    }
}

void handle_poweron(PowerStateHandler *handler, DeviceState *dev, Error **errp)
{
    PowerStateHandlerClass *pshc = POWERSTATE_HANDLER_GET_CLASS(handler);

    if (pshc->poweron) {
        pshc->poweron(handler, dev, errp);
    }
}

void handle_standby(PowerStateHandler *handler, DeviceState *dev, Error **errp)
{
    PowerStateHandlerClass *pshc = POWERSTATE_HANDLER_GET_CLASS(handler);

    if (pshc->standby) {
        pshc->standby(handler, dev, errp);
    }
}

static const TypeInfo powerstate_handler_info = {
    .name          = TYPE_POWERSTATE_HANDLER,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(PowerStateHandlerClass),
};

static void powerstate_handler_register_types(void)
{
    type_register_static(&powerstate_handler_info);
}

type_init(powerstate_handler_register_types)
