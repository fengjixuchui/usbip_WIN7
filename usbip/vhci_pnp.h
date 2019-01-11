#pragma once

#include "basetype.h"
#include "device.h"

#define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->common.PreviousPnPState =  (_Data_)->common.DevicePnPState;\
        (_Data_)->common.DevicePnPState = (_state_);

#define RESTORE_PREVIOUS_PNP_STATE(_Data_)   \
        (_Data_)->common.DevicePnPState =   (_Data_)->common.PreviousPnPState;

 NTSTATUS
bus_unplug_dev(int addr, PFDO_DEVICE_DATA DeviceData);