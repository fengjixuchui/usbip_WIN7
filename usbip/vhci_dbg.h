#pragma once

#define DRVPREFIX	"usbip_vhci"

#define DRVPREFIXEE "usbip_vhci:(EE)"
#define DRVPREFIXWW "usbip_vhci:(WW)"
#define DRVPREFIXQQ "usbip_vhci:(QQ)"

#include "dbgcommon.h"

#ifdef DBG

#include "usbreq.h"
#include "dbgcode.h"

#define DBG_GENERAL	0x00000001
#define DBG_READ	0x00000010
#define DBG_WRITE	0x00000100
#define DBG_PNP		0x00001000
#define DBG_IOCTL	0x00010000
#define DBG_POWER	0x00100000
#define DBG_WMI		0x01000000
#define DBG_URB		0x10000000

extern const char *dbg_urbr(struct urb_req *urbr);

extern const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);
extern const char *dbg_urbfunc(unsigned int urbfunc);

#endif	
