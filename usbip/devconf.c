#include "devconf.h"

#include <usbdlib.h>

PUSB_INTERFACE_DESCRIPTOR
dsc_find_intf(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR intf_num, USHORT alt_setting)
{
	PVOID	start = dsc_conf;

	while (TRUE) {
		PUSB_INTERFACE_DESCRIPTOR	dsc_intf = (PUSB_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (dsc_intf == NULL)
			break;
		if (dsc_intf->bInterfaceNumber == intf_num && dsc_intf->bAlternateSetting == alt_setting)
			return dsc_intf;
		start = NEXT_DESC(dsc_intf);
	}
	return NULL;
}

ULONG
dsc_conf_get_n_intfs(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf)
{
	PVOID	start = dsc_conf;
	ULONG	n_intfs = 0;

	while (TRUE) {
		PUSB_COMMON_DESCRIPTOR	desc = USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (desc == NULL)
			break;
		start = NEXT_DESC(desc);
		n_intfs++;
	}
	return n_intfs;
}