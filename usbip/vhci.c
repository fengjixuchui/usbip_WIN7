#include "vhci.h"

#include <usbdi.h>

#include "globals.h"
#include "usbreq.h"
#include "vhci_pnp.h"

//
// Global Debug Level
//

GLOBALS Globals;

NPAGED_LOOKASIDE_LIST g_lookaside;

 __drv_dispatchType(IRP_MJ_READ)
DRIVER_DISPATCH Bus_Read;

 __drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH Bus_Write;

 __drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH Bus_IoCtl;

	__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH Bus_Internal_IoCtl;

 __drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH Bus_PnP;

__drv_dispatchType(IRP_MJ_POWER)
DRIVER_DISPATCH Bus_Power;

 __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
DRIVER_DISPATCH Bus_SystemControl;

 DRIVER_ADD_DEVICE Bus_AddDevice;

static  VOID
Bus_DriverUnload(__in PDRIVER_OBJECT DriverObject)
{
	PAGED_CODE();

	DBGI(DBG_GENERAL, "Unload\n");

	ExDeleteNPagedLookasideList(&g_lookaside);

	//
	// All the device objects should be gone.
	//

	ASSERT(NULL == DriverObject->DeviceObject);

	//
	// Here we free all the resources allocated in the DriverEntry
	//

	if (Globals.RegistryPath.Buffer)
		ExFreePool(Globals.RegistryPath.Buffer);

	return;
}

static  NTSTATUS
Bus_Create(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	NTSTATUS            status;
	PFDO_DEVICE_DATA    fdoData;
	PCOMMON_DEVICE_DATA     commonData;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	if (!commonData->IsFDO) {
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//

	if (fdoData->common.DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	Bus_DecIoCount(fdoData);
	return status;
}

static  NTSTATUS
Bus_Cleanup(__in PDEVICE_OBJECT dev, __in PIRP irp)
{
	PIO_STACK_LOCATION  irpstack;
	NTSTATUS            status;
	PFDO_DEVICE_DATA    fdodata;
	PPDO_DEVICE_DATA	pdodata;
	PCOMMON_DEVICE_DATA     commondata;

	PAGED_CODE();

	DBGI(DBG_GENERAL, "Bus_Cleanup: Enter\n");

	commondata = (PCOMMON_DEVICE_DATA)dev->DeviceExtension;

	//
	// We only allow create/close requests for the FDO.
	// That is the bus itself.
	//
	if (!commondata->IsFDO) {
		DBGW(DBG_GENERAL, "Bus_Cleanup: Invalid request\n");
		irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	fdodata = (PFDO_DEVICE_DATA)dev->DeviceExtension;

	Bus_IncIoCount(fdodata);

	//
	// Check to see whether the bus is removed
	//

	if (fdodata->common.DevicePnPState == Deleted) {
		DBGW(DBG_GENERAL, "Bus_Cleanup: No such device\n");
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}
	irpstack = IoGetCurrentIrpStackLocation(irp);
	pdodata = irpstack->FileObject->FsContext;
	if (pdodata) {
		pdodata->fo = NULL;
		irpstack->FileObject->FsContext = NULL;
		if (pdodata->Present)
			bus_unplug_dev(pdodata->SerialNo, fdodata);
	}
	status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	Bus_DecIoCount(fdodata);

	DBGI(DBG_GENERAL, "Bus_Cleanup: Leave\n");

	return status;
}

static  NTSTATUS
Bus_Close(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	NTSTATUS            status;
	PFDO_DEVICE_DATA    fdoData;
	PCOMMON_DEVICE_DATA     commonData;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;
	//
	// We only allow create/close requests for the FDO.
	// That is the bus itself.
	//

	if (!commonData->IsFDO) {
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//

	if (fdoData->common.DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	Bus_DecIoCount(fdoData);
	return status;
}

 NTSTATUS
DriverEntry(__in  PDRIVER_OBJECT DriverObject, __in PUNICODE_STRING RegistryPath)
{
	DBGI(DBG_GENERAL, "DriverEntry: Enter\n");

	ExInitializeNPagedLookasideList(&g_lookaside, NULL,NULL, 0, sizeof(struct urb_req), 'USBV', 0);

	//
	// Save the RegistryPath for WMI.
	//

	Globals.RegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	Globals.RegistryPath.Length = RegistryPath->Length;
	Globals.RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Globals.RegistryPath.MaximumLength, USBIP_VHCI_POOL_TAG);

	if (!Globals.RegistryPath.Buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DBGI(DBG_GENERAL, "RegistryPath %p\r\n", RegistryPath);

	RtlCopyUnicodeString(&Globals.RegistryPath, RegistryPath);

	//
	// Set entry points into the driver
	//
	DriverObject->MajorFunction[IRP_MJ_CREATE] = Bus_Create;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = Bus_Cleanup;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = Bus_Close;
	DriverObject->MajorFunction[IRP_MJ_READ] = Bus_Read;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = Bus_Write;
	DriverObject->MajorFunction[IRP_MJ_PNP] = Bus_PnP;
	DriverObject->MajorFunction[IRP_MJ_POWER] = Bus_Power;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bus_IoCtl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = Bus_Internal_IoCtl;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Bus_SystemControl;
	DriverObject->DriverUnload = Bus_DriverUnload;
	DriverObject->DriverExtension->AddDevice = Bus_AddDevice;

	DBGI(DBG_GENERAL, "DriverEntry: Leave\n");

	return STATUS_SUCCESS;
}

VOID
Bus_IncIoCount(__in PFDO_DEVICE_DATA FdoData)
{
	LONG	result;

	result = InterlockedIncrement(&FdoData->OutstandingIO);

	ASSERT(result > 0);
	//
	// Need to clear StopEvent (when OutstandingIO bumps from 1 to 2)
	//
	if (result == 2) {
		//
		// We need to clear the event
		//
		KeClearEvent(&FdoData->StopEvent);
	}
}

VOID
Bus_DecIoCount(__in PFDO_DEVICE_DATA FdoData)
{
	LONG	result;

	result = InterlockedDecrement(&FdoData->OutstandingIO);

	ASSERT(result >= 0);

	if (result == 1) {
		//
		// Set the stop event. Note that when this happens
		// (i.e. a transition from 2 to 1), the type of requests we
		// want to be processed are already held instead of being
		// passed away, so that we can't "miss" a request that
		// will appear between the decrement and the moment when
		// the value is actually used.
		//

		KeSetEvent (&FdoData->StopEvent, IO_NO_INCREMENT, FALSE);
	}

	if (result == 0) {
		//
		// The count is 1-biased, so it can be zero only if an
		// extra decrement is done when a remove Irp is received
		//

		ASSERT(FdoData->common.DevicePnPState == Deleted);

		//
		// Set the remove event, so the device object can be deleted
		//

		KeSetEvent (&FdoData->RemoveEvent, IO_NO_INCREMENT, FALSE);
	}
}