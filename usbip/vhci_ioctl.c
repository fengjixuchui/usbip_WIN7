#include "vhci.h"

#include "usbreq.h"
#include "vhci_devconf.h"
#include "vhci_pnp.h"
#include "usbip_vhci_api.h"

extern NTSTATUS
submit_urbr(PPDO_DEVICE_DATA pdodata, PIRP irp);

extern  NTSTATUS
bus_plugin_dev(ioctl_usbip_vhci_plugin *plugin, PFDO_DEVICE_DATA dev_data, PFILE_OBJECT fo);

extern  NTSTATUS
bus_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, PFDO_DEVICE_DATA dev_data, ULONG *info);

extern  NTSTATUS
Bus_EjectDevice(PUSBIP_VHCI_EJECT_HARDWARE Eject, PFDO_DEVICE_DATA FdoData);

static NTSTATUS
process_urb_reset_pipe(PPDO_DEVICE_DATA pdodata)
{
	UNREFERENCED_PARAMETER(pdodata);

	////TODO need to check
	DBGI(DBG_IOCTL, "reset_pipe:\n");
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_abort_pipe(PPDO_DEVICE_DATA pdodata, PURB urb)
{
	struct _URB_PIPE_REQUEST	*urb_pipe = &urb->UrbPipeRequest;

	UNREFERENCED_PARAMETER(pdodata);

	////TODO need to check
	DBGI(DBG_IOCTL, "abort_pipe: %x\n", urb_pipe->PipeHandle);
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_get_frame(PPDO_DEVICE_DATA pdodata, PURB urb)
{
	struct _URB_GET_CURRENT_FRAME_NUMBER	*urb_get = &urb->UrbGetCurrentFrameNumber;
	UNREFERENCED_PARAMETER(pdodata);

	urb_get->FrameNumber = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
process_irp_urb_req(PPDO_DEVICE_DATA pdodata, PIRP irp, PURB urb)
{
	if (urb == NULL) {
		DBGE(DBG_IOCTL, "process_irp_urb_req: null urb\n");
		return STATUS_INVALID_PARAMETER;
	}

	DBGI(DBG_IOCTL, "process_irp_urb_req: function: %s\n", dbg_urbfunc(urb->UrbHeader.Function));

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_RESET_PIPE:
		return process_urb_reset_pipe(pdodata);
	case URB_FUNCTION_ABORT_PIPE:
		return process_urb_abort_pipe(pdodata, urb);
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		return process_urb_get_frame(pdodata, urb);
	case URB_FUNCTION_SELECT_CONFIGURATION:
	case URB_FUNCTION_ISOCH_TRANSFER:
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
	case URB_FUNCTION_SELECT_INTERFACE:
		return submit_urbr(pdodata, irp);
	default:
		DBGW(DBG_IOCTL, "process_irp_urb_req: unhandled function: %s: len: %d\n",
			dbg_urbfunc(urb->UrbHeader.Function), urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}
}

 NTSTATUS
Bus_Internal_IoCtl(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS		status;
	PPDO_DEVICE_DATA	pdoData;
	PCOMMON_DEVICE_DATA	commonData;
	ULONG			ioctl_code;

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Internal_Ioctl: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "ioctl code: %s\n", dbg_vhci_ioctl_code(ioctl_code));

	if (commonData->IsFDO) {
		DBGW(DBG_IOCTL, "internal ioctl for fdo is not allowed\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	pdoData = (PPDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	if (!pdoData->Present) {
		DBGW(DBG_IOCTL, "device is not connected\n");
		Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_DEVICE_NOT_CONNECTED;
	}

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(pdoData, Irp, (PURB)irpStack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = STATUS_SUCCESS;
		*(unsigned long *)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr(pdoData, Irp);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled internal ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Internal_Ioctl: Leave: %s\n", dbg_ntstatus(status));
	return status;
}

 NTSTATUS
Bus_IoCtl(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS			status;
	ULONG				inlen, outlen;
	ULONG				info = 0;
	PFDO_DEVICE_DATA	fdoData;
	PVOID				buffer;
	PCOMMON_DEVICE_DATA	commonData;
	ULONG				ioctl_code;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Ioctl: Enter\n");

	//
	// We only allow create/close requests for the FDO.
	// That is the bus itself.
	//
	if (!commonData->IsFDO) {
		DBGE(DBG_IOCTL, "ioctl for fdo is not allowed\n");

		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;
	DBGI(DBG_IOCTL, "ioctl code: %s\n", dbg_vhci_ioctl_code(ioctl_code));

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//
	if (fdoData->common.DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	buffer = Irp->AssociatedIrp.SystemBuffer;
	inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

	status = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		if (sizeof(ioctl_usbip_vhci_plugin) == inlen) {
			status = bus_plugin_dev((ioctl_usbip_vhci_plugin *)buffer, fdoData, irpStack->FileObject);
		}
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		if (sizeof(ioctl_usbip_vhci_get_ports_status) == outlen) {
			status = bus_get_ports_status((ioctl_usbip_vhci_get_ports_status *)buffer, fdoData, &info);
		}
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		if (sizeof(ioctl_usbip_vhci_unplug) == inlen) {
			status = bus_unplug_dev(((ioctl_usbip_vhci_unplug *)buffer)->addr, fdoData);
		}
		break;
	case IOCTL_USBIP_VHCI_EJECT_HARDWARE:
		if (inlen == sizeof(USBIP_VHCI_EJECT_HARDWARE) && ((PUSBIP_VHCI_EJECT_HARDWARE)buffer)->Size == inlen) {
			status = Bus_EjectDevice((PUSBIP_VHCI_EJECT_HARDWARE)buffer, fdoData);
		}
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled ioctl: %s", dbg_vhci_ioctl_code(ioctl_code));
		break;
	}

	Irp->IoStatus.Information = info;
END:
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	Bus_DecIoCount(fdoData);

	DBGI(DBG_GENERAL | DBG_IOCTL, "Bus_Ioctl: Leave: %s\n", dbg_ntstatus(status));

	return status;
}
