/*
 * Device Power State handler interface.
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
#ifndef POWERSTATE_H
#define POWERSTATE_H

#include "qom/object.h"

#define TYPE_POWERSTATE_HANDLER "powerstate-handler"

typedef struct PowerStateHandlerClass PowerStateHandlerClass;
DECLARE_CLASS_CHECKERS(PowerStateHandlerClass, POWERSTATE_HANDLER,
                       TYPE_POWERSTATE_HANDLER)
#define POWERSTATE_HANDLER(obj) \
     INTERFACE_CHECK(PowerStateHandler, (obj), TYPE_POWERSTATE_HANDLER)

typedef struct PowerStateHandler PowerStateHandler;

/**
 * DeviceOperPowerState:
 *
 * Enumeration of operational power states for devices. These represent runtime
 * states controlled through platform interfaces (e.g. ACPI, PSCI, or other
 * OSPM mechanisms), and are distinct from administrative presence or enable/
 * disable state.
 *
 * Transitions may be initiated by the guest OSPM in response to workload or
 * policy, or triggered by administrative actions due to policy change. Please
 * check PowerStateHandlerClass for more details on these.
 *
 * Platforms may optionally implement a callback to fetch the current state.
 * That callback must map internal platform state to one of the values here.
 *
 * @DEVICE_OPER_POWER_STATE_UNKNOWN: State reporting unsupported, or state
 *                                   could not be determined. If @errp is set,
 *                                   this indicates an error. Platform firmware
 *                                   may also enforce state changes directly;
 *                                   the callback must return the resulting
 *                                   state.
 *
 * @DEVICE_OPER_POWER_STATE_ON:      Device is powered on and fully active.
 *
 * @DEVICE_OPER_POWER_STATE_OFF:     Device is powered off and inactive. It
 *                                   should not consume resources and may
 *                                   require reinitialization on power on.
 *
 * @DEVICE_OPER_POWER_STATE_STANDBY: Device is in a low-power standby state.
 *                                   It retains enough state to allow fast
 *                                   resume without full reinitialization.
 *
 * See also: PowerStateHandlerClass, powerstate_get_fn
 */
typedef enum DeviceOperPowerState {
    DEVICE_OPER_POWER_STATE_UNKNOWN = -1,
    DEVICE_OPER_POWER_STATE_ON = 0,
    DEVICE_OPER_POWER_STATE_OFF,
    DEVICE_OPER_POWER_STATE_STANDBY,
    DEVICE_OPER_POWER_STATE_MAX
} DeviceOperPowerState;

/**
 * powerstate_fn:
 * @handler: Power state handler for the device performing the transition.
 * @dev: The device being transitioned between power states (e.g., on, off,
 *       or standby).
 * @errp: Pointer to return an error if the function fails.
 *
 * Generic function signature for power state transitions. Implementations
 * may use this to perform on/off/standby operations for a specific device.
 */
typedef void (*powerstate_fn)(PowerStateHandler *handler, DeviceState *dev,
                              Error **errp);

/**
 * powerstate_get_fn:
 * @dev:  The device whose operational state is being queried.
 * @errp: Pointer to an error object, set on failure.
 *
 * Callback type to query the current operational power state of a device.
 * Platforms may optionally implement this to expose their internal power
 * management status. When present, the callback must map the platform’s
 * internal state into one of the DeviceOperPowerState values.
 *
 * Returns: A DeviceOperPowerState value on success. If the platform does not
 * support state reporting, returns DEVICE_OPER_POWER_STATE_UNKNOWN without
 * setting @errp. If the state could not be determined due to an error, sets
 * @errp and also returns DEVICE_OPER_POWER_STATE_UNKNOWN. In this case, the
 * return value must be ignored when @errp is set.
 */
typedef DeviceOperPowerState (*powerstate_get_fn)(DeviceState *dev,
                                                  Error **errp);

/**
 * PowerStateHandlerClass:
 *
 * Interface for devices that support operational power state transitions
 * (on, off, standby). These are runtime states, distinct from administrative
 * presence or enable/disable.
 *
 * Transitions may be initiated by the guest OSPM in response to workload or
 * policy, or triggered by administrative actions due to policy (e.g. device-
 * set). In the latter case QEMU signals the guest via platform interfaces
 * (like ACPI) and OSPM coordinates the change. Some platforms may also enforce
 * state changes directly, without OSPM involvement.
 *
 * @parent: Opaque parent interface.
 *
 * @get_oper_state: Optional callback to query the current operational state.
 *                  Platform-specific implementations must map internal power
 *                  state to the DeviceOperPowerState enum.
 *
 * @poweroff_request: Optional callback to notify the guest or internal logic
 *                    that the device is about to power off. This may be used
 *                    to initiate graceful shutdown or cleanup.
 *
 * @poweroff: Callback used to fully power off the device. The device is
 *            expected to become inactive and not consume resources.
 *
 * @poweron: Callback used to transition the device from a powered-off state
 *           to active. This may include reinitializing internal state and
 *           notifying the guest that the device has resumed operation.
 *
 * @standby: Optional callback to place the device into a standby state
 *           without fully powering it off. The device is expected to retain
 *           sufficient state to allow efficient resume. Generally maps to
 *           device suspend at certain level. for example, CPU_SUSPEND
 */
struct PowerStateHandlerClass {
    /* <private> */
    InterfaceClass parent;

    /* <public> */
    powerstate_get_fn get_oper_state;

    powerstate_fn poweroff_request;
    powerstate_fn poweroff;
    powerstate_fn poweron;
    powerstate_fn standby;
};

PowerStateHandler *powerstate_handler(DeviceState *dev);

DeviceOperPowerState qdev_get_oper_power_state(DeviceState *dev);

void device_poweroff_request(DeviceState *dev, Error **errp);

void device_poweroff(DeviceState *dev, Error **errp);

void device_poweron(DeviceState *dev, Error **errp);

void device_standby(DeviceState *dev, Error **errp);
#endif /* POWERSTATE_H */
