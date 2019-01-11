#include "vhci.h"

#include "usbip_proto.h"
#include "usbip_vhci_api.h"
#include "usbreq.h"

extern NTSTATUS
store_urbr(PIRP irp, struct urb_req *urbr);

#ifdef DBG

const char *
dbg_urbr(struct urb_req *urbr)
{
	static char	buf[128];

	if (urbr == NULL)
		return "[null]";
	dbg_snprintf(buf, 128, "[seq:%d]", urbr->seq_num);
	return buf;
}

#endif

void
build_setup_packet(usb_cspkt_t *csp, unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request)
{
	csp->bmRequestType.B = 0;
	csp->bmRequestType.Type = type;
	if (direct_in)
		csp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
	csp->bmRequestType.Recipient = recip;
	csp->bRequest = request;
}

struct urb_req *
find_sent_urbr(PPDO_DEVICE_DATA pdodata, struct usbip_header *hdr)
{
	KIRQL		oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);
	for (le = pdodata->head_urbr_sent.Flink; le != &pdodata->head_urbr_sent; le = le->Flink) {
		struct urb_req	*urbr;
		urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryList(le);
			RemoveEntryList(&urbr->list_all);
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			return urbr;
		}
	}
	KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

	return NULL;
}

struct urb_req *
find_pending_urbr(PPDO_DEVICE_DATA pdodata)
{
	struct urb_req	*urbr;

	if (IsListEmpty(&pdodata->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(pdodata->head_urbr_pending.Flink, struct urb_req, list_state);
	urbr->seq_num = ++(pdodata->seq_num);
	RemoveEntryList(&urbr->list_state);
	InitializeListHead(&urbr->list_state);
	return urbr;
}

static void
remove_cancelled_urbr(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
	KIRQL	oldirql = irp->CancelIrql;
	PLIST_ENTRY	le;

	KeAcquireSpinLockAtDpcLevel(&pdodata->lock_urbr);

	for (le = pdodata->head_urbr.Flink; le != &pdodata->head_urbr; le = le->Flink) {
		struct urb_req	*urbr;

		urbr = CONTAINING_RECORD(le, struct urb_req, list_all);
		if (urbr->irp == irp) {
			RemoveEntryList(le);
			RemoveEntryList(&urbr->list_state);
			if (pdodata->urbr_sent_partial == urbr) {
				pdodata->urbr_sent_partial = NULL;
				pdodata->len_sent_partial = 0;
			}
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

			DBGI(DBG_GENERAL, "urb cancelled: %s\n", dbg_urbr(urbr));
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
			return;
		}
	}

	KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

	DBGW(DBG_GENERAL, "no matching urbr\n");
}

static void
cancel_urbr(PDEVICE_OBJECT pdo, PIRP irp)
{
	PPDO_DEVICE_DATA	pdodata;

	pdodata = (PPDO_DEVICE_DATA)pdo->DeviceExtension;
	DBGI(DBG_GENERAL, "irp will be cancelled: %p\n", irp);

	remove_cancelled_urbr(pdodata, irp);

	irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	IoReleaseCancelSpinLock(irp->CancelIrql);
}

static struct urb_req *
create_urbr(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
	struct urb_req	*urbr;

	urbr = ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (urbr == NULL) {
		DBGE(DBG_URB, "create_urbr: out of memory\n");
		return NULL;
	}
	RtlZeroMemory(urbr, sizeof(*urbr));
	urbr->pdodata = pdodata;
	urbr->irp = irp;
	return urbr;
}

static BOOLEAN
insert_pending_or_sent_urbr(PPDO_DEVICE_DATA pdodata, struct urb_req *urbr, BOOLEAN is_pending)
{
	PIRP	irp = urbr->irp;

	IoSetCancelRoutine(irp, cancel_urbr);
	if (irp->Cancel) {
		IoSetCancelRoutine(irp, NULL);
		return FALSE;
	}
	else {
		IoMarkIrpPending(irp);
		if (is_pending)
			InsertTailList(&pdodata->head_urbr_pending, &urbr->list_state);
		else
			InsertTailList(&pdodata->head_urbr_sent, &urbr->list_state);
	}
	return TRUE;
}

NTSTATUS
submit_urbr(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
	struct urb_req	*urbr;
	KIRQL	oldirql;
	PIRP	read_irp;
	NTSTATUS	status = STATUS_PENDING;

	if ((urbr = create_urbr(pdodata, irp)) == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);

	if (pdodata->urbr_sent_partial || pdodata->pending_read_irp == NULL) {
		if (!insert_pending_or_sent_urbr(pdodata, urbr, TRUE)) {
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
			DBGI(DBG_URB, "submit_urbr: urb cancelled\n");
			return STATUS_CANCELLED;
		}
		InsertTailList(&pdodata->head_urbr, &urbr->list_all);
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
		DBGI(DBG_URB, "submit_urbr: urb pending\n");
		return STATUS_PENDING;
	}

	read_irp = pdodata->pending_read_irp;
	pdodata->urbr_sent_partial = urbr;

	urbr->seq_num = ++(pdodata->seq_num);

	KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&pdodata->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
		if (pdodata->len_sent_partial == 0) {
			pdodata->urbr_sent_partial = NULL;
			if (!insert_pending_or_sent_urbr(pdodata, urbr, FALSE))
				status = STATUS_CANCELLED;
		}

		if (status == STATUS_SUCCESS) {
			InsertTailList(&pdodata->head_urbr, &urbr->list_all);
			pdodata->pending_read_irp = NULL;
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

			read_irp->IoStatus.Status = STATUS_SUCCESS;
			IoCompleteRequest(read_irp, IO_NO_INCREMENT);
			status = STATUS_PENDING;
		}
		else {
			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		}
	}
	else {
		pdodata->urbr_sent_partial = NULL;
		KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);

		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		status = STATUS_INVALID_PARAMETER;
	}
	DBGI(DBG_URB, "submit_urbr: urb requested: status:%s\n", dbg_ntstatus(status));
	return status;
}