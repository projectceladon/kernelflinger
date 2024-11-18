/** @file
  Copyright (c) 2006 - 2017, Intel Corporation. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/
#include <lib.h>
#include <efiapi.h>
#include <uefi_utils.h>
#include <vars.h>
#include "pci.h"
#include "UsbDeviceDxe.h"
#include "UsbDeviceMode.h"
#include "XdciDWC.h"

static EFI_HANDLE xdci = 0;
PCI_DEVICE_PATH xhci_path = {.Device = -1, .Function = -1};

VOID
EFIAPI
PlatformSpecificInit (
  VOID
  )
{
  UINTN                 XhciPciMmBase;
  EFI_PHYSICAL_ADDRESS  XhciMemBaseAddress;

  XhciPciMmBase   = MmPciAddress (
                      0,
                      0,
                      xhci_path.Device,
                      xhci_path.Function,
                      0
                      );


  XhciMemBaseAddress = MmioRead32 ((UINTN) (XhciPciMmBase + R_XHCI_MEM_BASE)) & B_XHCI_MEM_BASE_BA;
  DEBUG ((DEBUG_INFO, "XhciPciMmBase=%x, XhciMemBaseAddress=%x\n", XhciPciMmBase, XhciMemBaseAddress));

  MmioWrite32 ((UINTN)(XhciMemBaseAddress + R_XHCI_MEM_DUAL_ROLE_CFG0), 0x1310800);

  return;
}

static
VOID
EFIAPI
UsbDeviceDxeExitBootService (
  __attribute__((unused))EFI_EVENT Event,
  VOID *Context
  )
{
  USB_XDCI_DEV_CONTEXT  *UsbXdciDevContext;

  UsbXdciDevContext = (USB_XDCI_DEV_CONTEXT *) Context;
  DEBUG ((EFI_D_INFO, "UsbDeviceDxeExitBootService enter\n"));

  if (UsbXdciDevContext->XdciPollTimer != NULL) {
    uefi_call_wrapper(BS->SetTimer,
          3,
          UsbXdciDevContext->XdciPollTimer,
          TimerCancel,
          0);

    uefi_call_wrapper(BS->CloseEvent, 1, UsbXdciDevContext->XdciPollTimer);
    UsbXdciDevContext->XdciPollTimer = NULL;
  }

  return;
}

static EFI_STATUS find_usb_device_controller (EFI_HANDLE Controller)
{
  EFI_STATUS status = EFI_UNSUPPORTED;
  EFI_PCI_IO *pci;
  USB_CLASSC class_reg;
  UINTN seg;
  UINTN bus;
  UINTN dev;
  UINTN fun;

  status = uefi_call_wrapper(BS->OpenProtocol,
           6,
           Controller,
           &PciIoProtocol,
           (VOID **) &pci,
           g_parent_image,
           Controller,
           EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (EFI_ERROR (status))
    return status;

  status = uefi_call_wrapper(pci->Pci.Read,
           5,
           pci,
           EfiPciIoWidthUint8,
           PCI_CLASSCODE_OFFSET,
           sizeof (USB_CLASSC) / sizeof (UINT8),
           &class_reg);

  if (EFI_ERROR (status))
    return status;

  // Test whether the controller belongs to USB device type
  // 0x0C03FE / 0x0C0380

  if ((class_reg.BaseCode == PCI_CLASS_SERIAL) &&
      (class_reg.SubClassCode == PCI_CLASS_SERIAL_USB) &&
      ((class_reg.ProgInterface == PCI_IF_USBDEV) ||
      (class_reg.ProgInterface == 0x80))) {
    return EFI_SUCCESS;
  }


  if ((class_reg.BaseCode == PCI_CLASS_SERIAL) &&
    (class_reg.SubClassCode == PCI_CLASS_SERIAL_USB) &&
    (class_reg.ProgInterface == PCI_IF_XHCI)) {

    status = uefi_call_wrapper(pci->GetLocation,
             5,
             pci,
             &seg,
             &bus,
             &dev,
             &fun);
    xhci_path.Device = (UINT8)dev;
    xhci_path.Function = (UINT8)fun;
  }

  return EFI_UNSUPPORTED;
}

EFI_GUID gEfiEventExitBootServicesGuid  =  EventExitBootServices;

static EFI_STATUS usb_device_mode_start (EFI_HANDLE Controller, EFI_USB_DEVICE_MODE_PROTOCOL **usb_device)
{
  EFI_STATUS Status;
  USB_XDCI_DEV_CONTEXT *UsbXdciDevContext = NULL;
  EFI_PCI_IO *PciIo;
  EFI_EVENT ExitBootServicesEvent;

  // Provide protocol interface
  // Get the PCI I/O Protocol on PciHandle
  Status = uefi_call_wrapper(BS->OpenProtocol,
           6,
           Controller,
           &PciIoProtocol,
           (VOID **) &PciIo,
           g_parent_image,
           Controller,
           EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  UsbXdciDevContext = AllocateZeroPool (sizeof (USB_XDCI_DEV_CONTEXT));
  if (UsbXdciDevContext == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorExit;
  }

  // Initialize the driver context
  //
  UsbXdciDevContext->StartUpController = FALSE;
  UsbXdciDevContext->XdciHandle = Controller;
  UsbXdciDevContext->Signature = EFI_USB_DEV_SIGNATURE;

  Status = uefi_call_wrapper(PciIo->Pci.Read,
           5,
           PciIo,
           EfiPciIoWidthUint32,
           R_OTG_BAR0,
           1,
           &UsbXdciDevContext->XdciMmioBarAddr);
  UsbXdciDevContext->XdciMmioBarAddr &= B_OTG_BAR0_BA;

  UINT16 command = 0x6;
  Status = uefi_call_wrapper(PciIo->Pci.Write,
           5,
           PciIo,
           EfiPciIoWidthUint16,
           R_XDCI_CMD_OFF,
           1,
           &command);
  //read after write to ensure the former write take effect
  command = 0;
  Status = uefi_call_wrapper(PciIo->Pci.Read,
           5,
           PciIo,
           EfiPciIoWidthUint16,
           R_XDCI_CMD_OFF,
           1,
           &command);

  CopyMem (&(UsbXdciDevContext->UsbDevModeProtocol),
     &mUsbDeviceModeProtocol,
     sizeof (EFI_USB_DEVICE_MODE_PROTOCOL));

  Status = uefi_call_wrapper(BS->CreateEventEx,
           6,
           EVT_NOTIFY_SIGNAL,
           TPL_NOTIFY,
           UsbDeviceDxeExitBootService,
           UsbXdciDevContext,
           &gEfiEventExitBootServicesGuid,
           &ExitBootServicesEvent);
  if (EFI_ERROR (Status))
    goto ErrorExit;

  *usb_device = &(UsbXdciDevContext->UsbDevModeProtocol);

  return Status;

ErrorExit:

  if (UsbXdciDevContext != NULL) {
    if (UsbXdciDevContext->XdciPollTimer != NULL) {
      uefi_call_wrapper(BS->CloseEvent,
            1,
            UsbXdciDevContext->XdciPollTimer);
      UsbXdciDevContext->XdciPollTimer = NULL;
    }
    FreePool (UsbXdciDevContext);
  }

  efi_perror(Status, L"ERROR - install driver failed - Exit\n");
  return Status;
}

static BOOLEAN usb_xdci_enabled(void)
{
  EFI_STATUS ret;
  UINTN NumberHandles, Index;
  EFI_HANDLE *Handles;

  ret = LibLocateHandle(ByProtocol,
            &PciIoProtocol,
            NULL,
            &NumberHandles,
            &Handles);
  if (EFI_ERROR(ret)) {
    efi_perror(ret, L"LibLocateProtocol: Handle not found\n");
    return ret;
  }

  for (Index=0; Index < NumberHandles; Index++) {
    ret = find_usb_device_controller(Handles[Index]);
    if (!EFI_ERROR(ret)) {
      xdci = Handles[Index];
      break;
    }
  }

  if (Handles) {
    FreePool (Handles);
  }

  if (!EFI_ERROR(ret))
    return TRUE;

  return FALSE;
}

EFI_STATUS init_usb_device_mode_protocol(EFI_USB_DEVICE_MODE_PROTOCOL **usb_device)
{
  EFI_STATUS ret = EFI_UNSUPPORTED;

  if (usb_xdci_enabled()) {
    ret = usb_device_mode_start(xdci, usb_device);
  } else {
    efi_perror(ret, L"XDCI is disabled, please enable it in BIOS");
  }

  return ret;
}
