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

#include "XdciUtility.h"

VOID
PrintDeviceDescriptor (
  IN __attribute((unused)) USB_DEVICE_DESCRIPTOR    *DevDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- Device Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length            : 0x%x\n", DevDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType    : 0x%x\n", DevDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "BcdUSB            : 0x%x\n", DevDesc->BcdUSB));
  DEBUG ((DEBUG_INFO, "DeviceClass       : 0x%x\n", DevDesc->DeviceClass));
  DEBUG ((DEBUG_INFO, "DeviceSubClass    : 0x%x\n", DevDesc->DeviceSubClass));
  DEBUG ((DEBUG_INFO, "DeviceProtocol    : 0x%x\n", DevDesc->DeviceProtocol));
  DEBUG ((DEBUG_INFO, "MaxPacketSize0    : 0x%x\n", DevDesc->MaxPacketSize0));
  DEBUG ((DEBUG_INFO, "IdVendor          : 0x%x\n", DevDesc->IdVendor));
  DEBUG ((DEBUG_INFO, "IdProduct         : 0x%x\n", DevDesc->IdProduct));
  DEBUG ((DEBUG_INFO, "BcdDevice         : 0x%x\n", DevDesc->BcdDevice));
  DEBUG ((DEBUG_INFO, "StrManufacturer   : 0x%x\n", DevDesc->StrManufacturer));
  DEBUG ((DEBUG_INFO, "StrProduct        : 0x%x\n", DevDesc->StrProduct));
  DEBUG ((DEBUG_INFO, "StrSerialNumber   : 0x%x\n", DevDesc->StrSerialNumber));
  DEBUG ((DEBUG_INFO, "NumConfigurations : 0x%x\n", DevDesc->NumConfigurations));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintConfigDescriptor (
  IN __attribute((unused)) EFI_USB_CONFIG_DESCRIPTOR    *ConfigDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- Configuration Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length             : 0x%x\n", ConfigDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType     : 0x%x\n", ConfigDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "TotalLength        : 0x%x\n", ConfigDesc->TotalLength));
  DEBUG ((DEBUG_INFO, "NumInterfaces      : 0x%x\n", ConfigDesc->NumInterfaces));
  DEBUG ((DEBUG_INFO, "ConfigurationValue : 0x%x\n", ConfigDesc->ConfigurationValue));
  DEBUG ((DEBUG_INFO, "Configuration      : 0x%x\n", ConfigDesc->Configuration));
  DEBUG ((DEBUG_INFO, "Attributes         : 0x%x\n", ConfigDesc->Attributes));
  DEBUG ((DEBUG_INFO, "MaxPower           : 0x%x\n", ConfigDesc->MaxPower));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintInterfaceDescriptor (
  IN __attribute((unused)) EFI_USB_INTERFACE_DESCRIPTOR    *IfDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- Interface Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length            : 0x%x\n", IfDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType    : 0x%x\n", IfDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "InterfaceNumber   : 0x%x\n", IfDesc->InterfaceNumber));
  DEBUG ((DEBUG_INFO, "AlternateSetting  : 0x%x\n", IfDesc->AlternateSetting));
  DEBUG ((DEBUG_INFO, "NumEndpoints      : 0x%x\n", IfDesc->NumEndpoints));
  DEBUG ((DEBUG_INFO, "InterfaceClass    : 0x%x\n", IfDesc->InterfaceClass));
  DEBUG ((DEBUG_INFO, "InterfaceSubClass : 0x%x\n", IfDesc->InterfaceSubClass));
  DEBUG ((DEBUG_INFO, "InterfaceProtocol : 0x%x\n", IfDesc->InterfaceProtocol));
  DEBUG ((DEBUG_INFO, "Interface         : 0x%x\n", IfDesc->Interface));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintEpDescriptor (
  IN __attribute((unused)) EFI_USB_ENDPOINT_DESCRIPTOR    *EpDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- Endpoint Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length          : 0x%x\n", EpDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType  : 0x%x\n", EpDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "EndpointAddress : 0x%x\n", EpDesc->EndpointAddress));
  DEBUG ((DEBUG_INFO, "Attributes      : 0x%x\n", EpDesc->Attributes));
  DEBUG ((DEBUG_INFO, "MaxPacketSize   : 0x%x\n", EpDesc->MaxPacketSize));
  DEBUG ((DEBUG_INFO, "Interval        : 0x%x\n", EpDesc->Interval));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintEpCompDescriptor (
  IN __attribute((unused)) EFI_USB_ENDPOINT_COMPANION_DESCRIPTOR    *EpDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- Endpoint Companion Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length           : 0x%x\n", EpDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType   : 0x%x\n", EpDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "MaxBurst         : 0x%x\n", EpDesc->MaxBurst));
  DEBUG ((DEBUG_INFO, "Attributes       : 0x%x\n", EpDesc->Attributes));
  DEBUG ((DEBUG_INFO, "BytesPerInterval : 0x%x\n", EpDesc->BytesPerInterval));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintStringDescriptor (
  IN USB_STRING_DESCRIPTOR    *StrDesc
  )
{
  if (StrDesc->Length > 2) {
    DEBUG ((DEBUG_INFO, "--- String Descriptor ---\n"));
    DEBUG ((DEBUG_INFO, "Length         : 0x%x\n", StrDesc->Length));
    DEBUG ((DEBUG_INFO, "DescriptorType : 0x%x\n", StrDesc->DescriptorType));
    DEBUG ((DEBUG_INFO, "String         : %s\n",   StrDesc->LangID));
  }
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintDeviceRequest (
  IN __attribute__((unused)) EFI_USB_DEVICE_REQUEST    *DevReq
  )
{
  DEBUG ((DEBUG_INFO, "--- Device Request ---\n"));
  DEBUG ((DEBUG_INFO, "RequestType : 0x%x\n", DevReq->RequestType));
  DEBUG ((DEBUG_INFO, "Request     : 0x%x\n", DevReq->Request));
  DEBUG ((DEBUG_INFO, "Value       : 0x%x\n", DevReq->Value));
  DEBUG ((DEBUG_INFO, "Index       : 0x%x\n", DevReq->Index));
  DEBUG ((DEBUG_INFO, "Length      : 0x%x\n", DevReq->Length));
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
PrintBOSDescriptor (
  IN __attribute__((unused)) EFI_USB_BOS_DESCRIPTOR    *BosDesc
  )
{
  DEBUG ((DEBUG_INFO, "--- BOS Descriptor ---\n"));
  DEBUG ((DEBUG_INFO, "Length           : 0x%x\n", BosDesc->Length));
  DEBUG ((DEBUG_INFO, "DescriptorType   : 0x%x\n", BosDesc->DescriptorType));
  DEBUG ((DEBUG_INFO, "TotalLength      : 0x%x\n", BosDesc->TotalLength));
  DEBUG ((DEBUG_INFO, "NumDeviceCaps    : 0x%x\n", BosDesc->NumDeviceCaps));
  DEBUG ((DEBUG_INFO, "\n"));
}

