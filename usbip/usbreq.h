#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "usb_cspkt.h"

#include "device.h"

#define PIPE2DIRECT(handle)	(((INT_PTR)(handle) & 0x80) ? USBIP_DIR_IN : USBIP_DIR_OUT)
#define PIPE2ADDR(handle)	((unsigned char)((INT_PTR)(handle) & 0x7f))
#define PIPE2TYPE(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff0000) >> 16))
#define PIPE2INTERVAL(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff00) >> 8))

struct urb_req {
	PPDO_DEVICE_DATA	pdodata;
	PIRP	irp;
	KEVENT	*event;
	unsigned long	seq_num;
	LIST_ENTRY	list_all;
	LIST_ENTRY	list_state;
};

extern void
build_setup_packet(usb_cspkt_t *csp, unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request);
