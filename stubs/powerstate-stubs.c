/*
 * Device Power State handler interface Stubs.
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
#include "hw/qdev-core.h"

PowerStateHandler *powerstate_handler(DeviceState *dev)
{
    return NULL;
}

DeviceOperPowerState qdev_get_oper_power_state(DeviceState *dev)
{
    return DEVICE_OPER_POWER_STATE_UNKNOWN;
}

void device_request_poweroff(DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}

void device_post_poweroff(DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}

void device_pre_poweron(DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}

void device_request_standby(DeviceState *dev, Error **errp)
{
    g_assert_not_reached();
}
