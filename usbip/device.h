#pragma once

#include <ntddk.h>
#include <wmilib.h>	// required for WMILIB_CONTEXT

#include "vhci_devconf.h"

//
// These are the states a PDO or FDO transition upon
// receiving a specific PnP Irp. Refer to the PnP Device States
// diagram in DDK documentation for better understanding.
//
typedef enum _DEVICE_PNP_STATE {
	NotStarted = 0,         // Not started yet
	Started,                // Device has received the START_DEVICE IRP
	StopPending,            // Device has received the QUERY_STOP IRP
	Stopped,                // Device has received the STOP_DEVICE IRP
	RemovePending,          // Device has received the QUERY_REMOVE IRP
	SurpriseRemovePending,  // Device has received the SURPRISE_REMOVE IRP
	Deleted,                // Device has received the REMOVE_DEVICE IRP
	UnKnown                 // Unknown state
} DEVICE_PNP_STATE;

//
// Structure for reporting data to WMI
//
typedef struct _USBIP_BUS_WMI_STD_DATA
{
	// The error Count
	UINT32   ErrorCount;
} USBIP_BUS_WMI_STD_DATA, *PUSBIP_BUS_WMI_STD_DATA;

//
// A common header for the device extensions of the PDOs and FDO
//
typedef struct _COMMON_DEVICE_DATA
{
	// A back pointer to the device object for which this is the extension
	PDEVICE_OBJECT	Self;

	// This flag helps distinguish between PDO and FDO
	BOOLEAN		IsFDO;

	// We track the state of the device with every PnP Irp
	// that affects the device through these two variables.
	DEVICE_PNP_STATE	DevicePnPState;
	DEVICE_PNP_STATE	PreviousPnPState;

	// Stores the current system power state
	SYSTEM_POWER_STATE	SystemPowerState;

	// Stores current device power state
	DEVICE_POWER_STATE	DevicePowerState;
} COMMON_DEVICE_DATA, *PCOMMON_DEVICE_DATA;

struct urb_req;

//
// The device extension for the PDOs.
// That's of the USBIP device which this bus driver enumerates.
//
typedef struct _PDO_DEVICE_DATA
{
	COMMON_DEVICE_DATA	common;

	// A back pointer to the bus
	PDEVICE_OBJECT	ParentFdo;

	// An array of (zero terminated wide character strings).
	// The array itself also null terminated
	USHORT	vendor, product, revision;
	UCHAR	usbclass, subclass, protocol, inum;

	// Unique serail number of the device on the bus
	ULONG SerialNo;

	// Link point to hold all the PDOs for a single bus together
	LIST_ENTRY  Link;

	//
	// Present is set to TRUE when the PDO is exposed via PlugIn IOCTL,
	// and set to FALSE when a UnPlug IOCTL is received.
	// We will delete the PDO in IRP_MN_REMOVE only after we have reported
	// to the Plug and Play manager that it's missing.
	//
	BOOLEAN		Present;
	BOOLEAN		ReportedMissing;
	UCHAR	speed;
	UCHAR	unused; /* 4 bytes alignment */

	// Used to track the intefaces handed out to other drivers.
	// If this value is non-zero, we fail query-remove.
	LONG	InterfaceRefCount;
	// a pending irp when no urb is requested
	PIRP	pending_read_irp;
	// a partially transferred urb_req
	struct urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG	len_sent_partial;
	// all urb_req's. This list will be used for clear or cancellation.
	LIST_ENTRY	head_urbr;
	// pending urb_req's which are not transferred yet
	LIST_ENTRY	head_urbr_pending;
	// urb_req's which had been sent and have waited for response
	LIST_ENTRY	head_urbr_sent;
	KSPIN_LOCK	lock_urbr;
	PFILE_OBJECT	fo;
	unsigned int	devid;
	unsigned long	seq_num;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	KTIMER	timer;
	KDPC	dpc;
	UNICODE_STRING	usb_dev_interface;

	//
	// In order to reduce the complexity of the driver, I chose not
	// to use any queues for holding IRPs when the system tries to
	// rebalance resources to accommodate a new device, and stops our
	// device temporarily. But in a real world driver this is required.
	// If you hold Irps then you should also support Cancel and
	// Cleanup functions. The function driver demonstrates these techniques.
	//
	// The queue where the incoming requests are held when
	// the device is stopped for resource rebalance.

	//LIST_ENTRY	PendingQueue;

	// The spin lock that protects access to  the queue

	//KSPIN_LOCK	PendingQueueLock;
} PDO_DEVICE_DATA, *PPDO_DEVICE_DATA;

//
// The device extension of the bus itself.  From whence the PDO's are born.
//
typedef struct _FDO_DEVICE_DATA
{
	COMMON_DEVICE_DATA	common;

	PDEVICE_OBJECT	UnderlyingPDO;

	// The underlying bus PDO and the actual device object to which our
	// FDO is attached
	PDEVICE_OBJECT	NextLowerDriver;

	// List of PDOs created so far
	LIST_ENTRY		ListOfPDOs;

	// The PDOs currently enumerated.
	ULONG			NumPDOs;

	// A synchronization for access to the device extension.
	FAST_MUTEX		Mutex;

	// The number of IRPs sent from the bus to the underlying device object
	LONG		OutstandingIO; // Biased to 1

	// On remove device plug & play request we must wait until all outstanding
	// requests have been completed before we can actually delete the device
	// object. This event is when the Outstanding IO count goes to zero
	KEVENT		RemoveEvent;

	// This event is set when the Outstanding IO count goes to 1.
	KEVENT		StopEvent;

	// The name returned from IoRegisterDeviceInterface,
	// which is used as a handle for IoSetDeviceInterfaceState.
	UNICODE_STRING	InterfaceName;

	// WMI Information
	WMILIB_CONTEXT	WmiLibInfo;

	USBIP_BUS_WMI_STD_DATA	StdUSBIPBusData;
} FDO_DEVICE_DATA, *PFDO_DEVICE_DATA;

void
Bus_IncIoCount(__in PFDO_DEVICE_DATA Data);

void
Bus_DecIoCount(__in PFDO_DEVICE_DATA Data);
