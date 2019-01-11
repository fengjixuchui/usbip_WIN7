#pragma once

#include "devconf.h"

#include <usbdi.h>

extern void
show_pipe(unsigned int num, PUSBD_PIPE_INFORMATION pipe);

extern void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, unsigned char speed);

extern NTSTATUS
select_config(struct _URB_SELECT_CONFIGURATION *urb_selc, UCHAR speed);

extern NTSTATUS
select_interface(struct _URB_SELECT_INTERFACE *urb_seli, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR speed);