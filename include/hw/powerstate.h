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
 * PowerStateHandlerClass:
 *
 * Interface to be implemented by devices that support hardware power state
 * transitions such as on, standby, or off. (Support for more states can be
 * added later.)
 *
 * @parent: Opaque parent interface.
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
 */
struct PowerStateHandlerClass {
    /* <private> */
    InterfaceClass parent;

    /* <public> */
    powerstate_fn poweroff_request;
    powerstate_fn poweroff;
    powerstate_fn poweron;
};

PowerStateHandler *powerstate_handler(DeviceState *dev);

void handle_poweroff_request(PowerStateHandler *handler, DeviceState *dev,
                        Error **errp);

void handle_poweroff(PowerStateHandler *handler, DeviceState *dev,
                     Error **errp);

void handle_poweron(PowerStateHandler *handler, DeviceState *dev,
                    Error **errp);
#endif /* POWERSTATE_H */
