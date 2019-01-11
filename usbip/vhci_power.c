#include "vhci.h"

#include "device.h"

static NTSTATUS
Bus_FDO_Power(PFDO_DEVICE_DATA Data, PIRP Irp)
/*++
Handles power Irps sent to the FDO.
This driver is power policy owner for the bus itself
(not the devices on the bus).Power handling for the bus FDO
should be implemented similar to the function driver (USBIP.sys)
power code. We will just print some debug outputs and
forward this Irp to the next level.

Arguments:

Data -  Pointer to the FDO device extension.
Irp  -  Pointer to the irp.

Return Value:

NT status is returned.

--*/

{
	NTSTATUS            status;
	POWER_STATE         powerState;
	POWER_STATE_TYPE    powerType;
	PIO_STACK_LOCATION  stack;

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	Bus_IncIoCount(Data);

	//
	// If the device is not stated yet, just pass it down.
	//

	if (Data->common.DevicePnPState == NotStarted) {
		PoStartNextPowerIrp(Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		status = PoCallDriver(Data->NextLowerDriver, Irp);
		Bus_DecIoCount(Data);
		return status;

	}

	if (stack->MinorFunction == IRP_MN_SET_POWER) {
		DBGI(DBG_POWER, "\tRequest to set %s state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));
	}

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	status = PoCallDriver(Data->NextLowerDriver, Irp);
	Bus_DecIoCount(Data);
	return status;
}

static NTSTATUS
Bus_PDO_Power(PPDO_DEVICE_DATA PdoData, PIRP Irp)
/*++
Handles power Irps sent to the PDOs.
Typically a bus driver, that is not a power
policy owner for the device, does nothing
more than starting the next power IRP and
completing this one.

Arguments:

PdoData - Pointer to the PDO device extension.
Irp     - Pointer to the irp.

Return Value:

NT status is returned.

--*/

{
	NTSTATUS            status;
	PIO_STACK_LOCATION  stack;
	POWER_STATE         powerState;
	POWER_STATE_TYPE    powerType;

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	switch (stack->MinorFunction) {
	case IRP_MN_SET_POWER:

		DBGI(DBG_POWER, "\tSetting %s power state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));

		switch (powerType) {
		case DevicePowerState:
			PoSetPowerState(PdoData->common.Self, powerType, powerState);
			PdoData->common.DevicePowerState = powerState.DeviceState;
			status = STATUS_SUCCESS;
			break;

		case SystemPowerState:
			PdoData->common.SystemPowerState = powerState.SystemState;
			status = STATUS_SUCCESS;
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
			break;
		}
		break;

	case IRP_MN_QUERY_POWER:
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_WAIT_WAKE:
		//
		// We cannot support wait-wake because we are root-enumerated
		// driver, and our parent, the PnP manager, doesn't support wait-wake.
		// If you are a bus enumerated device, and if  your parent bus supports
		// wait-wake,  you should send a wait/wake IRP (PoRequestPowerIrp)
		// in response to this request.
		// If you want to test the wait/wake logic implemented in the function
		// driver (USBIP.sys), you could do the following simulation:
		// a) Mark this IRP pending.
		// b) Set a cancel routine.
		// c) Save this IRP in the device extension
		// d) Return STATUS_PENDING.
		// Later on if you suspend and resume your system, your BUS_FDO_POWER
		// will be called to power the bus. In response to IRP_MN_SET_POWER, if the
		// powerstate is PowerSystemWorking, complete this Wake IRP.
		// If the function driver, decides to cancel the wake IRP, your cancel routine
		// will be called. There you just complete the IRP with STATUS_CANCELLED.
		//
	case IRP_MN_POWER_SEQUENCE:
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (status != STATUS_NOT_SUPPORTED) {

		Irp->IoStatus.Status = status;
	}

	PoStartNextPowerIrp(Irp);
	status = Irp->IoStatus.Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS
Bus_Power(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status;
	PCOMMON_DEVICE_DATA commonData;

	DBGI(DBG_GENERAL | DBG_POWER, "Bus_Power: Enter\n");

	status = STATUS_SUCCESS;
	irpStack = IoGetCurrentIrpStackLocation (Irp);
	ASSERT (IRP_MJ_POWER == irpStack->MajorFunction);

	commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

	//
	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	//
	if (commonData->DevicePnPState == Deleted) {
		PoStartNextPowerIrp (Irp);
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		return status;
	}

	if (commonData->IsFDO) {
		DBGI(DBG_POWER, "FDO: minor: %s IRP:0x%p %s %s\n",
		     dbg_power_minor(irpStack->MinorFunction), Irp,
		     dbg_system_power(commonData->SystemPowerState),
		     dbg_device_power(commonData->DevicePowerState));

		status = Bus_FDO_Power((PFDO_DEVICE_DATA)DeviceObject->DeviceExtension, Irp);
	} else {
		DBGI(DBG_POWER, "PDO: minor: %s IRP:0x%p %s %s\n",
			 dbg_power_minor(irpStack->MinorFunction), Irp,
			 dbg_system_power(commonData->SystemPowerState),
			 dbg_device_power(commonData->DevicePowerState));

		status = Bus_PDO_Power ((PPDO_DEVICE_DATA)DeviceObject->DeviceExtension, Irp);
	}

	return status;
}
