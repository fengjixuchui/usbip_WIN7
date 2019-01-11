#pragma once

#include <guiddef.h>
#ifdef _NTDDK_
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

//
// Define an Interface Guid for bus vhci class.
// This GUID is used to register (IoRegisterDeviceInterface) 
// an instance of an interface so that vhci application 
// can send an ioctl to the bus driver.
//

DEFINE_GUID(GUID_DEVINTERFACE_VHCI_USBIP,
        0xD35F7840, 0x6A0C, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);


//
// Define a Setup Class GUID for USBIP Class. This is same
// as the TOASTSER CLASS guid in the INF files.
//
DEFINE_GUID(GUID_DEVCLASS_USBIP,
        0xB85B7C50, 0x6A01, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{B85B7C50-6A01-11d2-B841-00C04FAD5171}

//
// Define a WMI GUID to get vhci info.
//
DEFINE_GUID(USBIP_BUS_WMI_STD_DATA_GUID, 
        0x0006A660, 0x8F12, 0x11d2, 0xB8, 0x54, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{0006A660-8F12-11d2-B854-00C04FAD5171}

//
// Define a WMI GUID to get USBIP device info.
//
DEFINE_GUID(USBIP_WMI_STD_DATA_GUID, 
        0xBBA21300L, 0x6DD3, 0x11d2, 0xB8, 0x44, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

//
// Define a WMI GUID to represent device arrival notification WMIEvent class.
//
DEFINE_GUID(USBIP_NOTIFY_DEVICE_ARRIVAL_EVENT, 
        0x1cdaff1, 0xc901, 0x45b4, 0xb3, 0x59, 0xb5, 0x54, 0x27, 0x25, 0xe2, 0x9c);
// {01CDAFF1-C901-45b4-B359-B5542725E29C}

#define USBIP_VHCI_IOCTL(_index_) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, _index_, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE	USBIP_VHCI_IOCTL(0x0)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE	USBIP_VHCI_IOCTL(0x1)
#define IOCTL_USBIP_VHCI_EJECT_HARDWARE		USBIP_VHCI_IOCTL(0x2)
#define IOCTL_USBIP_VHCI_GET_PORTS_STATUS	USBIP_VHCI_IOCTL(0x3)

typedef struct _ioctl_usbip_vhci_plugin
{
	unsigned int devid;
	/* 4 bytes */
	unsigned short vendor;
	unsigned short product;
	/* 8 bytes */
	unsigned short version;
	unsigned char speed;
	unsigned char inum;
	/* 12 bytes */
	unsigned char class;
	unsigned char subclass;
	unsigned char protocol;
	signed char addr;  /* then it can not be bigger then 127 */
	/* 16 bytes */
} ioctl_usbip_vhci_plugin;

typedef struct _ioctl_usbip_vhci_get_ports_status
{
	union {
		signed char max_used_port; /* then it can not be bigger than 127 */
		unsigned char port_status[128];
		/* 128 bytes */
	} u;
} ioctl_usbip_vhci_get_ports_status;

typedef struct _ioctl_usbip_vhci_unplug
{
	signed char addr;
	char unused[3];

} ioctl_usbip_vhci_unplug;

typedef struct _USBIP_VHCI_EJECT_HARDWARE
{
	// sizeof (struct _EJECT_HARDWARE)
	ULONG	Size;

	// Serial number of the device to be ejected
	ULONG	SerialNo;
	ULONG	Reserved[2];
} USBIP_VHCI_EJECT_HARDWARE, *PUSBIP_VHCI_EJECT_HARDWARE;
