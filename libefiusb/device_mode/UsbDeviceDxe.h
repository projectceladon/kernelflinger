/** @file
  Copyright (c) 2006 - 2017, Intel Corporation. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __USB_DEVICE_DXE_H__
#define __USB_DEVICE_DXE_H__

#include <efidef.h>
#include "XdciDWC.h"
#include "protocol/UsbDeviceLib.h"
#include "protocol/UsbDeviceModeProtocol.h"
#include "UsbDeviceMode.h"


#define EFI_USB_DEV_SIGNATURE              0x55534244 //"USBD"
#define USBUSBD_CONTEXT_FROM_PROTOCOL(a)   CR (a, USB_XDCI_DEV_CONTEXT, UsbDevModeProtocol, EFI_USB_DEV_SIGNATURE)

#pragma pack(1)
typedef struct {
  UINTN                         Signature;
  UINTN                         XdciMmioBarAddr;
  EFI_HANDLE                    XdciHandle;
  EFI_EVENT                     XdciPollTimer;
  EFI_USB_DEVICE_MODE_PROTOCOL  UsbDevModeProtocol;
  USB_DEVICE_ENDPOINT_INFO      IndexPtrInEp;
  USB_DEVICE_ENDPOINT_INFO      IndexPtrOutEp;
  XDCI_CORE_HANDLE              *XdciDrvIfHandle;
  USB_DEV_CORE                  *DrvCore;
  UINT16                        VendorId;
  UINT16                        DeviceId;
  BOOLEAN                       StartUpController;
  BOOLEAN                       DevReConnect;
  BOOLEAN                       DevResetFlag;
  EFI_EVENT                     TimerEvent;
} USB_XDCI_DEV_CONTEXT;
#pragma pack()

VOID
EFIAPI
PlatformSpecificInit (
  VOID
  );

extern PCI_DEVICE_PATH xhci_path;

#pragma pack(1)
typedef struct {
	UINT8                     ProgInterface;
	UINT8                     SubClassCode;
	UINT8                     BaseCode;
} USB_CLASSC;
#pragma pack()

#define PCI_CLASSCODE_OFFSET		0x09
#define PCI_CLASS_SERIAL		0x0C
#define PCI_CLASS_SERIAL_USB		0x03
#define PCI_IF_USBDEV			0xFE
#define PCI_IF_XHCI                     0x30

#define EventExitBootServices \
    { 0x27ABF055, 0xB1B8, 0x4C26, { 0x80, 0x48, 0x74, 0x8F, 0x37, 0xBA, 0xA2, 0xDF } }

#define R_OTG_BAR0                      0x10  ///< BAR 0
#define B_OTG_BAR0_BA                   0xFFE00000 ///< Base Address
#define R_XHCI_MEM_BASE                 0x10
#define B_XHCI_MEM_BASE_BA              0xFFFFFFFFFFFF0000
#define R_XHCI_MEM_DUAL_ROLE_CFG0       0x80D8
#define R_XHCI_MEM_DUAL_ROLE_CFG1       0x80DC
#define R_XDCI_CMD_OFF			0x04

#define MmPciAddress( Segment, Bus, Device, Function, Register ) \
	( (UINTN)0xE0000000 + \
	  (UINTN)(Bus << 20) + \
	  (UINTN)(Device << 15) + \
	  (UINTN)(Function << 12) + \
	  (UINTN)(Register) \
	)

UINT32 MmioRead32(UINTN address);
UINT16 MmioRead16(UINTN address);
UINT8 MmioRead8(UINTN address);
UINT32 MmioWrite32(UINTN address, UINT32 value);
UINT16 MmioWrite16(UINTN address, UINT16 value);
UINT8 MmioWrite8(UINTN address, UINT8 value);
EFI_STATUS install_usb_device_mode_protocol(void);
#endif
