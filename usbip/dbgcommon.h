#pragma once

#include <ntddk.h>

/* DPFLTR_SYSTEM_ID emits too many unrelated kernel logs */
#define MY_DFPLTR	DPFLTR_IHVDRIVER_ID

#ifdef DBG

#include "usbip_proto.h"

#define DBGE(part, fmt, ...)	DbgPrintEx(MY_DFPLTR, DPFLTR_ERROR_LEVEL, DRVPREFIXEE fmt, ## __VA_ARGS__)
#define DBGW(part, fmt, ...)	DbgPrintEx(MY_DFPLTR, DPFLTR_ERROR_LEVEL, DRVPREFIXWW fmt, ## __VA_ARGS__)
#define DBGI(part, fmt, ...)	DbgPrintEx(MY_DFPLTR, DPFLTR_ERROR_LEVEL, DRVPREFIXQQ fmt, ## __VA_ARGS__)

int dbg_snprintf(char *buf, int size, const char *fmt, ...);

const char *dbg_usbip_hdr(struct usbip_header *hdr);
const char *dbg_command(UINT32 command);

#else

#define DBGE(part, fmt, ...)
#define DBGW(part, fmt, ...)
#define DBGI(part, fmt, ...)

#endif	

#define ERROR(fmt, ...)	DbgPrintEx(MY_DFPLTR, DPFLTR_ERROR_LEVEL, DRVPREFIX ":(EE) " fmt, ## __VA_ARGS__)
#define INFO(fmt, ...)	DbgPrintEx(MY_DFPLTR, DPFLTR_INFO_LEVEL, DRVPREFIX ": " fmt, ## __VA_ARGS__)
