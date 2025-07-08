/*
 * Standby handler stubs
 *
 * Copyright (c) 2025 Huawei Technologies R&D (UK) Ltd
 *
 * Author: Salil Mehta <salil.mehta@huawei.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/qdev-core.h"

StandbyHandler *standby_get_handler(DeviceState *dev)
{
    return NULL;
}

void standby_handler_request(StandbyHandler *handler, DeviceState *dev,
                             Error **errp)
{
    g_assert_not_reached();
}

void standby_handler_enter(StandbyHandler *handler, DeviceState *dev,
                           Error **errp)
{
    g_assert_not_reached();
}

void standby_handler_exit(StandbyHandler *handler, DeviceState *dev,
                          Error **errp)
{
    g_assert_not_reached();
}
