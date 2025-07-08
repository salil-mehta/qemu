/*
 * Standby handler interface.
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
#include "hw/standby.h"
#include "qemu/module.h"
#include "hw/boards.h"

StandbyHandler *standby_get_handler(DeviceState *dev)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);

   if (mc->get_standby_handler) {
        return mc->get_standby_handler(machine, dev);
   }

    return NULL;
}

void standby_handler_request(StandbyHandler *handler, DeviceState *dev,
                                Error **errp)
{
    StandbyHandlerClass *sdc = STANDBY_HANDLER_GET_CLASS(handler);

    if (sdc->standby_request) {
        sdc->standby_request(handler, dev, errp);
    }
}

void standby_handler_enter(StandbyHandler *handler, DeviceState *dev,
                            Error **errp)
{
    StandbyHandlerClass *sdc = STANDBY_HANDLER_GET_CLASS(handler);

    if (sdc->enter_standby) {
        sdc->enter_standby(handler, dev, errp);
    }
}

void standby_handler_exit(StandbyHandler *handler, DeviceState *dev,
                            Error **errp)
{
    StandbyHandlerClass *sdc = STANDBY_HANDLER_GET_CLASS(handler);

    if (sdc->exit_standby) {
        sdc->exit_standby(handler, dev, errp);
    }
}

static const TypeInfo standby_handler_info = {
    .name          = TYPE_STANDBY_HANDLER,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(StandbyHandlerClass),
};

static void standby_handler_register_types(void)
{
    type_register_static(&standby_handler_info);
}

type_init(standby_handler_register_types)
