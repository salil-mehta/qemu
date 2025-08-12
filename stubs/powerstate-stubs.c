/*
 * Device Power State handler interface Stubs.
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
#include "hw/qdev-core.h"

PowerStateHandler *powerstate_handler(DeviceState *dev)
{
    return NULL;
}

DeviceOperPowerState get_oper_power_state(DeviceState *dev, Error **errp)
{
    return DEVICE_OPER_POWER_STATE_UNKNOWN;
}

void handle_poweroff_request(PowerStateHandler *handler, DeviceState *dev,
                             Error **errp)
{
    g_assert_not_reached();
}

void handle_poweroff(PowerStateHandler *handler, DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}

void handle_poweron(PowerStateHandler *handler, DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}

void handle_standby(PowerStateHandler *handler, DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}
