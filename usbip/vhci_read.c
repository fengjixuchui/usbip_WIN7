#include "vhci.h"

#include "usbip_proto.h"
#include "usbreq.h"

extern struct urb_req *
find_pending_urbr(PPDO_DEVICE_DATA pdodata);

extern void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
	unsigned int direct, USBD_PIPE_HANDLE pipe, unsigned int flags, unsigned int len);

static struct usbip_header *
get_usbip_hdr_from_read_irp(PIRP irp)
{
	PIO_STACK_LOCATION	irpstack;

	irp->IoStatus.Information = 0;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	if (irpstack->Parameters.Read.Length < sizeof(struct usbip_header)) {
		return NULL;
	}
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

static PVOID
get_read_irp_data(PIRP irp, ULONG length)
{
	PIO_STACK_LOCATION	irpstack;

	irp->IoStatus.Information = 0;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	if (irpstack->Parameters.Read.Length < length) {
		return NULL;
	}
	return (PVOID)irp->AssociatedIrp.SystemBuffer;
}

static int
get_read_irp_length(PIRP irp)
{
	PIO_STACK_LOCATION	irpstack;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	return irpstack->Parameters.Read.Length;
}

static NTSTATUS
store_urb_reset_dev(PIRP irp, struct urb_req *urbr)
{
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, 0, 0, 0, 0);

	build_setup_packet(csp, 0, BMREQUEST_CLASS, BMREQUEST_TO_OTHER, USB_REQUEST_SET_FEATURE);
	csp->wLength = 0;
	csp->wValue.LowByte = 4; // Reset
	csp->wIndex.W = 0;

	irp->IoStatus.Information = sizeof(struct usbip_header);

	return STATUS_SUCCESS;
}

static PVOID
get_buf(PVOID buf, PMDL bufMDL)
{
	if (buf == NULL) {
		if (bufMDL != NULL)
			buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
		if (buf == NULL) {
			DBGE(DBG_READ, "No transfer buffer\n");
		}
	}
	return buf;
}

static NTSTATUS
store_urb_get_dev_desc(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	build_setup_packet(csp, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);

	csp->wLength = (unsigned short)urb_desc->TransferBufferLength;
	csp->wValue.HiByte = urb_desc->DescriptorType;
	csp->wValue.LowByte = urb_desc->Index;

	switch (urb_desc->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		csp->wIndex.W = 0;
		break;
	case USB_INTERFACE_DESCRIPTOR_TYPE:
		csp->wIndex.W = urb_desc->Index;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		csp->wIndex.W = urb_desc->LanguageId;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_get_intf_desc(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	build_setup_packet(csp, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_DESCRIPTOR);

	csp->wLength = (unsigned short)urb_desc->TransferBufferLength;
	csp->wValue.HiByte = urb_desc->DescriptorType;
	csp->wValue.LowByte = urb_desc->Index;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_class_vendor_partial(PPDO_DEVICE_DATA pdodata, PIRP irp, PURB urb)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vc = &urb->UrbControlVendorClassRequest;
	PVOID	dst;

	dst = get_read_irp_data(irp, urb_vc->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	RtlCopyMemory(dst, urb_vc->TransferBuffer, urb_vc->TransferBufferLength);
	irp->IoStatus.Information = urb_vc->TransferBufferLength;
	pdodata->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_class_vendor(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vc = &urb->UrbControlVendorClassRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;
	char	type, recip;
	int	in = urb_vc->TransferFlags & USBD_TRANSFER_DIRECTION_IN ? 1: 0;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	switch (urb_vc->Hdr.Function) {
	case URB_FUNCTION_CLASS_DEVICE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_CLASS_INTERFACE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_CLASS_ENDPOINT:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_CLASS_OTHER:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_OTHER;
		break;
	case URB_FUNCTION_VENDOR_DEVICE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_VENDOR_INTERFACE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_VENDOR_ENDPOINT:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_VENDOR_OTHER:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, in, 0,
				    urb_vc->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_vc->TransferBufferLength);
	build_setup_packet(csp, (unsigned char)in, type, recip, urb_vc->Request);
	//FIXME what is the usage of RequestTypeReservedBits?
	csp->wLength = (unsigned short)urb_vc->TransferBufferLength;
	csp->wValue.W = urb_vc->Value;
	csp->wIndex.W = urb_vc->Index;

	irp->IoStatus.Information = sizeof(struct usbip_header);

	if (!in) {
		ULONG	len = (ULONG)(get_read_irp_length(irp) - sizeof(struct usbip_header));
		if (len >= urb_vc->TransferBufferLength) {
			RtlCopyMemory(hdr + 1, urb_vc->TransferBuffer, urb_vc->TransferBufferLength);
			irp->IoStatus.Information += urb_vc->TransferBufferLength;
		}
		else {
			urbr->pdodata->len_sent_partial = sizeof(struct usbip_header);
		}
	}
	return  STATUS_SUCCESS;
}

static NTSTATUS
store_urb_select_config(PIRP irp, struct urb_req *urbr)
{
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, 0, 0, 0, 0);
	build_setup_packet(csp, 0, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	csp->wLength = 0;
	csp->wValue.W = 1;
	csp->wIndex.W = 0;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_select_interface(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_SELECT_INTERFACE	*urb_si = &urb->UrbSelectInterface;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, 0, 0, 0, 0);
	build_setup_packet(csp, 0, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	csp->wLength = 0;
	csp->wValue.W = urb_si->Interface.AlternateSetting;
	csp->wIndex.W = urb_si->Interface.InterfaceNumber;

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return  STATUS_SUCCESS;
}

static NTSTATUS
store_urb_bulk_partial(PPDO_DEVICE_DATA pdodata, PIRP irp, PURB urb)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*urb_bi = &urb->UrbBulkOrInterruptTransfer;
	PVOID	dst, src;

	dst = get_read_irp_data(irp, urb_bi->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	src = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
	if (src == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, src, urb_bi->TransferBufferLength);
	irp->IoStatus.Information = urb_bi->TransferBufferLength;
	pdodata->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_bulk(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*urb_bi = &urb->UrbBulkOrInterruptTransfer;
	struct usbip_header	*hdr;
	int	in, type;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	/* Sometimes, direction in TransferFlags of _URB_BULK_OR_INTERRUPT_TRANSFER is not consistent with PipeHandle.
	 * Use a direction flag in pipe handle.
	 */
	in = PIPE2DIRECT(urb_bi->PipeHandle);
	type = PIPE2TYPE(urb_bi->PipeHandle);
	if (type != USB_ENDPOINT_TYPE_BULK && type != USB_ENDPOINT_TYPE_INTERRUPT) {
		DBGE(DBG_READ, "Error, not a bulk pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid, in, urb_bi->PipeHandle,
				    urb_bi->TransferFlags, urb_bi->TransferBufferLength);
	RtlZeroMemory(hdr->u.cmd_submit.setup, 8);

	irp->IoStatus.Information = sizeof(struct usbip_header);

	if (!in) {
		ULONG	len = (ULONG)(get_read_irp_length(irp) - sizeof(struct usbip_header));
		if (len >= urb_bi->TransferBufferLength) {
			PVOID	buf = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
			if (buf == NULL)
				return STATUS_INSUFFICIENT_RESOURCES;
			RtlCopyMemory(hdr + 1, buf, urb_bi->TransferBufferLength);
		}
		else {
			urbr->pdodata->len_sent_partial = sizeof(struct usbip_header);
		}
	}
	return STATUS_SUCCESS;
}

static NTSTATUS
copy_iso_data(PVOID dst, struct _URB_ISOCH_TRANSFER *urb_iso)
{
	struct usbip_iso_packet_descriptor	*iso_desc;
	char	*buf;
	ULONG	i, offset;

	buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	RtlCopyMemory(dst, buf, urb_iso->TransferBufferLength);

	iso_desc = (struct usbip_iso_packet_descriptor *)((char *)dst + urb_iso->TransferBufferLength);
	offset = 0;
	for (i = 0; i < urb_iso->NumberOfPackets; i++) {
		if (urb_iso->IsoPacket[i].Offset < offset) {
			DBGW(DBG_READ, "strange iso packet offset:%d %d", offset, urb_iso->IsoPacket[i].Offset);
			return STATUS_INVALID_PARAMETER;
		}
		iso_desc->offset = urb_iso->IsoPacket[i].Offset;
		if (i > 0)
			(iso_desc - 1)->length = urb_iso->IsoPacket[i].Offset - offset;
		offset = urb_iso->IsoPacket[i].Offset;
		iso_desc->actual_length = 0;
		iso_desc->status = 0;
		iso_desc++;
	}
	(iso_desc - 1)->length = urb_iso->TransferBufferLength - offset;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_iso_partial(PPDO_DEVICE_DATA pdodata, PIRP irp, PURB urb)
{
	struct _URB_ISOCH_TRANSFER	*urb_iso = &urb->UrbIsochronousTransfer;
	ULONG	len_iso = urb_iso->TransferBufferLength + urb_iso->NumberOfPackets * sizeof(struct usbip_iso_packet_descriptor);
	PVOID	dst;

	dst = get_read_irp_data(irp, len_iso);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	copy_iso_data(dst, urb_iso);
	irp->IoStatus.Information = len_iso;
	pdodata->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_iso(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_ISOCH_TRANSFER	*urb_iso = &urb->UrbIsochronousTransfer;
	struct usbip_header	*hdr;
	int	in, type;

	in = PIPE2DIRECT(urb_iso->PipeHandle);
	type = PIPE2TYPE(urb_iso->PipeHandle);
	if (type != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
		DBGE(DBG_READ, "Error, not a iso pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->pdodata->devid,
				    in, urb_iso->PipeHandle, urb_iso->TransferFlags | USBD_SHORT_TRANSFER_OK,
				    urb_iso->TransferBufferLength);
	hdr->u.cmd_submit.start_frame = urb_iso->StartFrame;
	hdr->u.cmd_submit.number_of_packets = urb_iso->NumberOfPackets;

	irp->IoStatus.Information = sizeof(struct usbip_header);

	if (!in) {
		ULONG	len_irp = (ULONG)(get_read_irp_length(irp) - sizeof(struct usbip_header));
		ULONG	len_iso = urb_iso->TransferBufferLength + urb_iso->NumberOfPackets * sizeof(struct usbip_iso_packet_descriptor);
		if (len_irp >= len_iso) {
			copy_iso_data(hdr + 1, urb_iso);
			irp->IoStatus.Information += len_iso;
		}
		else {
			urbr->pdodata->len_sent_partial = sizeof(struct usbip_header);
		}
	}

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urbr_submit(PIRP irp, struct urb_req *urbr)
{
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USHORT		code_func;
	NTSTATUS	status;

	DBGI(DBG_READ, "store_urbr_submit: urbr: %s\n", dbg_urbr(urbr));

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	urb = irpstack->Parameters.Others.Argument1;
	if (urb == NULL) {
		DBGE(DBG_READ, "process_urb_req_submit: null urb\n");

		irp->IoStatus.Information = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	code_func = urb->UrbHeader.Function;
	DBGI(DBG_READ, "process_urb_req_submit: urbr: %s, func:%s\n", dbg_urbr(urbr), dbg_urbfunc(code_func));

	switch (code_func) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk(irp, urb, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso(irp, urb, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urb_get_dev_desc(irp, urb, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		status = store_urb_get_intf_desc(irp, urb, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urb_class_vendor(irp, urb, urbr);
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		status = store_urb_select_config(irp, urbr);
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		status = store_urb_select_interface(irp, urb, urbr);
		break;
	default:
		irp->IoStatus.Information = 0;
		DBGE(DBG_READ, "unhandled urb function: %s\n", dbg_urbfunc(code_func));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static NTSTATUS
store_urbr_partial(PIRP irp, struct urb_req *urbr)
{
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USHORT		code_func;
	NTSTATUS	status;

	DBGI(DBG_READ, "store_urbr_partial: urbr: %s\n", dbg_urbr(urbr));

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	urb = irpstack->Parameters.Others.Argument1;
	code_func = urb->UrbHeader.Function;

	switch (code_func) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk_partial(urbr->pdodata, irp, urb);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso_partial(urbr->pdodata, irp, urb);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urb_class_vendor_partial(urbr->pdodata, irp, urb);
		break;
	default:
		irp->IoStatus.Information = 0;
		DBGE(DBG_READ, "store_urbr_partial: unexpected partial urbr: %s\n", dbg_urbfunc(code_func));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

NTSTATUS
store_urbr(PIRP irp, struct urb_req *urbr)
{
	PIO_STACK_LOCATION	irpstack;
	ULONG		ioctl_code;
	NTSTATUS	status;

	DBGI(DBG_READ, "store_urbr: urbr: %s\n", dbg_urbr(urbr));

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;
	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = store_urbr_submit(irp, urbr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = store_urb_reset_dev(irp, urbr);
		break;
	default:
		DBGW(DBG_READ, "unhandled ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		irp->IoStatus.Information = 0;
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static NTSTATUS
process_read_irp(PPDO_DEVICE_DATA pdodata, PIRP read_irp)
{
	struct urb_req	*urbr;
	KIRQL	oldirql;
	NTSTATUS status;

	KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);
	if (pdodata->pending_read_irp) {
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (pdodata->urbr_sent_partial != NULL) {
		urbr = pdodata->urbr_sent_partial;
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

		status = store_urbr_partial(read_irp, urbr);

		KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);
		pdodata->len_sent_partial = 0;
	}
	else {
		urbr = find_pending_urbr(pdodata);
		if (urbr == NULL) {
			IoMarkIrpPending(read_irp);
			pdodata->pending_read_irp = read_irp;
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			return STATUS_PENDING;
		}
		pdodata->urbr_sent_partial = urbr;
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

		status = store_urbr(read_irp, urbr);

		KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);
	}

	if (status != STATUS_SUCCESS) {
		RemoveEntryList(&urbr->list_all);
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

		IoSetCancelRoutine(urbr->irp, NULL);
		urbr->irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		IoCompleteRequest(urbr->irp, IO_NO_INCREMENT);
		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
	}
	else {
		if (pdodata->len_sent_partial == 0) {
			InsertTailList(&pdodata->head_urbr_sent, &urbr->list_state);
			pdodata->urbr_sent_partial = NULL;
		}
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
	}
	return status;
}

 NTSTATUS
Bus_Read(__in PDEVICE_OBJECT DeviceObject, __in PIRP irp)
{
	PFDO_DEVICE_DATA	fdoData;
	PPDO_DEVICE_DATA	pdodata;
	PCOMMON_DEVICE_DATA     commonData;
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS		status;

	PAGED_CODE();

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_READ, "Bus_Read: Enter\n");

	if (!commonData->IsFDO) {
		DBGE(DBG_READ, "read for fdo is not allowed\n");

		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	Bus_IncIoCount(fdoData);

	//
	// Check to see whether the bus is removed
	//
	if (fdoData->common.DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}
	irpstack = IoGetCurrentIrpStackLocation(irp);
	pdodata = irpstack->FileObject->FsContext;
	if (pdodata == NULL || !pdodata->Present)
		status = STATUS_INVALID_DEVICE_REQUEST;
	else
		status = process_read_irp(pdodata, irp);

END:
	DBGI(DBG_GENERAL | DBG_READ, "Bus_Read: Leave: %s\n", dbg_ntstatus(status));
	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}
	Bus_DecIoCount(fdoData);
	return status;
}
