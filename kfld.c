/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Author: Ming Tan <ming.tan@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <efi.h>
#include <efiapi.h>
#include <efilib.h>
#include <stdio.h>
#include <version.h>

#include "lib.h"
#include "uefi_utils.h"
#include "protocol.h"
#include "gpt.h"
#include "android.h"
#include "slot.h"

#include "libavb_ab.h"

/* BIOS Capsule update file */
#define FWUPDATE_FILE             L"\\BIOSUPDATE.fv"
#define KF_FILE                   L"\\EFI\\INTEL\\KF4UEFI.EFI"

#define KFLD_SELF_FILE            L"\\EFI\\INTEL\\KFLD.EFI"
#define KFLD_UPDATE_FILE          L"\\EFI\\INTEL\\KFLD_NEW.EFI"
#define KFLD_BACKUP_FILE          L"\\EFI\\INTEL\\KFLD_BAK.EFI"

#ifndef ARCH_X86_64
#define BOOTLOADER_FILE           L"\\EFI\\BOOT\\bootia32.efi"
#define BOOTLOADER_FILE_BAK       L"\\EFI\\BOOT\\bootia32_bak.efi"
#else
#define BOOTLOADER_FILE           L"\\EFI\\BOOT\\bootx64.efi"
#define BOOTLOADER_FILE_BAK       L"\\EFI\\BOOT\\bootx64_bak.efi"
#endif  // ARCH_X86_64


static EFI_HANDLE g_disk_device;
static EFI_LOADED_IMAGE *g_loaded_image;


EFI_STATUS avb_ab_read_misc(AvbABData *avbABData)
{
	EFI_STATUS ret;
	struct gpt_partition_interface gpart;
	UINT32 MediaId;
	UINTN partition_start;
	UINTN partition_size;
	CHAR16 *label = L"misc";

	ret = gpt_get_partition_by_label(label, &gpart, LOGICAL_UNIT_USER);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Partition %s not found", label);
		return ret;
	}
	if (gpart.part.ending_lba < gpart.part.starting_lba) {
		ret = EFI_COMPROMISED_DATA;
		efi_perror(ret, L"Partition LBA is wrong");
		return ret;
	}
	// Check the partition size is enough
	MediaId = gpart.bio->Media->MediaId;
	partition_start = gpart.part.starting_lba * gpart.bio->Media->BlockSize;
	partition_size = (gpart.part.ending_lba + 1 - gpart.part.starting_lba) * gpart.bio->Media->BlockSize;
	if (partition_size < 2048 + sizeof(*avbABData)) { // AB_METADATA_MISC_PARTITION_OFFSET = 2048;
		ret = EFI_COMPROMISED_DATA;
		efi_perror(ret, L"Partition %s is too small", label);
		return ret;
	}
	ret = uefi_call_wrapper(gpart.dio->ReadDisk, 5, gpart.dio, MediaId,
			vm_offset + partition_start + 2048,
			sizeof(*avbABData), avbABData);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read partition %s failed", label);
		return ret;
	}

	if (avbABData->magic != BOOT_CTRL_MAGIC) {
		debug(L"AVB AB Magic is incorrect");
		return EFI_COMPROMISED_DATA;
	}

	return EFI_SUCCESS;
}

EFI_STATUS get_active_slot(UINT8 *slot)
{
	EFI_STATUS ret;
	AvbABData avbABData;
	UINT8 i;
	uint8_t highest_priority;

	// Default use slot 0
	*slot = 0;

	ret = avb_ab_read_misc(&avbABData);
	if (EFI_ERROR(ret)) {
		// Read AVB AVB data failed
		return ret;
	}
	for (i = 0, highest_priority = 0; i < ARRAY_SIZE(avbABData.slot_info); i++) {
		if (avbABData.slot_info[i].successful_boot == 0
				&& avbABData.slot_info[i].tries_remaining == 0)
			continue;

		if (highest_priority < avbABData.slot_info[i].priority) {
			highest_priority = avbABData.slot_info[i].priority;
			*slot = i;
		}
	}

	return ret;
}

EFI_STATUS load_kf(UINT8 slot)
{
	EFI_STATUS ret, unload_ret;
	CHAR16 *label;
	EFI_HANDLE kf_handle = NULL;
	EFI_DEVICE_PATH *edp;
	EFI_FILE_IO_INTERFACE *io = NULL;
	EFI_HANDLE kf_image = 0;
	EFI_GUID SimpleFileSystemProtocol = SIMPLE_FILE_SYSTEM_PROTOCOL;
	EFI_LOADED_IMAGE *loaded_image;

	label = (slot == 1) ? L"bootloader_b" : L"bootloader_a";

	ret = gpt_get_partition_handle(label, LOGICAL_UNIT_USER,
			 &kf_handle);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get partition %s", label);
		goto out;
	}

	ret = handle_protocol(kf_handle, &SimpleFileSystemProtocol,
			(void **)&io);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Load FAT for partition %s failed", label);
		goto out;
	}

	if (!uefi_exist_file_root(io, KF_FILE)) {
		debug(L"File %s is not exist", KF_FILE);
		goto out;
	}

	edp = FileDevicePath(kf_handle, KF_FILE);
	if (!edp) {
		error(L"Couldn't generate a path for '%s'", KF_FILE);
		return EFI_INVALID_PARAMETER;
	}

	ret = uefi_call_wrapper(BS->LoadImage, 6, FALSE, g_parent_image,
			edp, NULL, 0, &kf_image);
	FreePool(edp);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to load '%s'", KF_FILE);
		goto out;
	}

	if (g_loaded_image->LoadOptionsSize > 0) {
		ret = uefi_call_wrapper(BS->OpenProtocol, 6, kf_image,
				&LoadedImageProtocol, (VOID **)&loaded_image,
				kf_image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (EFI_ERROR(ret)) {
			efi_perror(ret, L"OpenProtocol: LoadedImageProtocol");
			goto out;
		}
		if (loaded_image == NULL) {
			error(L"LoadedImageProtocol, but return image is NULL");
			ret = EFI_INVALID_PARAMETER;
			goto out;
		}
		loaded_image->LoadOptionsSize = g_loaded_image->LoadOptionsSize;
		loaded_image->LoadOptions = g_loaded_image->LoadOptions;
	}

	// Set the active slot efi variable
	ret = set_efi_loaded_slot(slot);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to set loaded slot %d to efi variable", slot);
		return ret;
	}

	ret = uefi_call_wrapper(BS->StartImage, 3, kf_image, NULL, NULL);

out:
	if (kf_image != 0) {
		unload_ret = uefi_call_wrapper(BS->UnloadImage, 1, kf_image);
		if (EFI_ERROR(unload_ret))
			efi_perror(unload_ret, L"Failed to unload image");
	}
	return ret;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *_table)
{
	EFI_STATUS ret;
	UINT8 active_slot;

	InitializeLib(image, _table);

	debug(L"KF loader, version 1.0");

	/* populate globals */
	g_parent_image = image;
	ret = uefi_call_wrapper(BS->OpenProtocol, 6, image,
			&LoadedImageProtocol, (VOID **)&g_loaded_image,
			image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"OpenProtocol: LoadedImageProtocol");
		return ret;
	}
	g_disk_device = g_loaded_image->DeviceHandle;

	/* loaded from mass storage (not DnX) */
	if (g_disk_device) {
		ret = storage_set_boot_device(g_disk_device);
		if (EFI_ERROR(ret))
			error(L"Failed to set boot device");
	}
	// Set the boot device now
	if (!get_boot_device_handle()) {
		if (!get_boot_device()) {
			// Get boot device failed
			error(L"Failed to find boot device");
			return EFI_NO_MEDIA;
		}
	}

	// Check the BIOS upgrade
	uefi_bios_update_capsule(g_disk_device, FWUPDATE_FILE);

	// Check upgrade the kfld
	uefi_check_upgrade(g_loaded_image, L"esp", KFLD_UPDATE_FILE,
			BOOTLOADER_FILE, BOOTLOADER_FILE_BAK, KFLD_SELF_FILE, KFLD_BACKUP_FILE);

	// Get the active slot
	get_active_slot(&active_slot);

	debug(L"Try to load slot: %d", active_slot);
	ret = load_kf(active_slot);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Load slot %d failed", active_slot);
		set_efi_loaded_slot_failed(active_slot, ret);
		// Set the slot to unbootable
		active_slot = ((active_slot == 0) ? 1 : 0);
		error(L"Try another slot: %d", active_slot);
		ret = load_kf(active_slot);
	}

	return ret;
}

/* vim: tabstop=8:shiftwidth=8
 */
