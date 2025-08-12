/*
 * Device Power State transition handler interface
 *
 * An administrative request to 'enable' or 'disable' a device results in a
 * change of its operational status. The transition may be performed either
 * synchronously or asynchronously, with OSPM assistance where required.
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

void device_request_poweroff(DeviceState *dev, Error **errp)
{
    PowerStateHandler *h = powerstate_handler(dev);
    PowerStateHandlerClass *pshc = h ? POWERSTATE_HANDLER_GET_CLASS(h) : NULL;

    if (pshc && pshc->request_poweroff) {
        pshc->request_poweroff(h, dev, errp);
    }
}

void device_post_poweroff(DeviceState *dev, Error **errp)
{
    PowerStateHandler *h = powerstate_handler(dev);
    PowerStateHandlerClass *pshc = h ? POWERSTATE_HANDLER_GET_CLASS(h) : NULL;

    if (pshc && pshc->post_poweroff) {
        pshc->post_poweroff(h, dev, errp);
    }
}

void device_pre_poweron(DeviceState *dev, Error **errp)
{
    PowerStateHandler *h = powerstate_handler(dev);
    PowerStateHandlerClass *pshc = h ? POWERSTATE_HANDLER_GET_CLASS(h) : NULL;

    if (pshc && pshc->pre_poweron) {
        pshc->pre_poweron(h, dev, errp);
    }
}

void device_request_standby(DeviceState *dev, Error **errp)
{
    PowerStateHandler *h = powerstate_handler(dev);
    PowerStateHandlerClass *pshc = h ? POWERSTATE_HANDLER_GET_CLASS(h) : NULL;

    if (pshc && pshc->request_standby) {
        pshc->request_standby(h, dev, errp);
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
