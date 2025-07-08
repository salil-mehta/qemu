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
#ifndef STANDBY_H
#define STANDBY_H

#include "qom/object.h"

#define TYPE_STANDBY_HANDLER "standby-handler"

typedef struct StandbyHandlerClass StandbyHandlerClass;
DECLARE_CLASS_CHECKERS(StandbyHandlerClass, STANDBY_HANDLER,
                       TYPE_STANDBY_HANDLER)
#define STANDBY_HANDLER(obj) \
     INTERFACE_CHECK(StandbyHandler, (obj), TYPE_STANDBY_HANDLER)

typedef struct StandbyHandler StandbyHandler;

/**
 * standby_fn:
 * @handler: a device performing standby/active function
 * @dev: a device that has been put on standby or active mode
 * @errp: returns an error if this function fails
 */
typedef void (*standby_fn)(StandbyHandler *handler, DeviceState *dev,
                            Error **errp);

/**
 * StandbyDeviceClass:
 *
 * Interface to be implemented by a device performing hardware standby
 * enter/exit functions.
 *
 * @parent: Opaque parent interface.
 * @request_standby: This standby request callback might be used to intimate the
                     kernel that active device is about to go on standby and
                     event could be used to initiate asynchronous device disable
                     handling if required.
 * @enter_standby: Callback used to put device on standby mode. The device still
                   remains relaized.
 * @exit_standby: callback called before resuming the device to active state.
 *                This assumes that the device was already realized but was put
 *                on standby mode.
 */
struct StandbyHandlerClass {
    /* <private> */
    InterfaceClass parent;

    /* <public> */
    standby_fn request_standby;
    standby_fn enter_standby;
    standby_fn exit_standby;
};

StandbyHandler *standby_get_handler(DeviceState *dev);

void standby_handler_request(StandbyHandler *handler, DeviceState *dev,
                                Error **errp);

void standby_handler_enter(StandbyHandler *handler, DeviceState *dev,
                            Error **errp);

void standby_handler_exit(StandbyHandler *handler, DeviceState *dev,
                            Error **errp);
#endif /* STANDBY_H */
