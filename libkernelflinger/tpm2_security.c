/*
 * Copyright (c) 2017, Intel Corporation
 * All rights reserved.
 *
 * Authors: Anisha Kulkarni <anisha.dattatraya.kulkarni@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer
 *	in the documentation and/or other materials provided with the
 *	distribution.
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
 *
 */

#include <efi.h>
#include <lib.h>
#include <byteswap.h>
#include "ivshmem.h"
#include "Tcg2Protocol.h"
#include "Tpm2CommandLib.h"
#include "tpm2_security.h"
#include "Tpm2Help.h"
#include "security.h"

EFI_STATUS tpm2_fuse_vbmeta_key_hash(
		__attribute__((__unused__)) void *data,
		__attribute__((__unused__)) uint32_t size)
{
	return EFI_UNSUPPORTED;
}

EFI_STATUS tpm2_fuse_bootloader_policy(
		__attribute__((__unused__)) void *data,
		__attribute__((__unused__)) uint32_t size)
{
	return EFI_UNSUPPORTED;
}

enum NV_INDEX {
	NV_INDEX_TRUSTYOS_SEED = 0x01500080,
	NV_INDEX_OPTEEOS_SEED = 0x01500081,
	NV_INDEX_BOOTLOADER = 0x01500082,
};

#define MAX_NV_NUMBER		ARRAY_SIZE(config_table)

#define DIGEST_SIZE 32

typedef struct {
	TPMI_RH_NV_INDEX nv_index;
	TPMA_NV attribute;
} attribute_matrix_t;

static const attribute_matrix_t config_table[] =
{
	{NV_INDEX_TRUSTYOS_SEED,
		{
		/* The Index data can be written if Owner Authorization is provided. */
		.TPMA_NV_OWNERWRITE = 1,
		/* Authorizations to change the Index contents that require
			* USER role may be provided with an HMAC session or password.
		*/
		.TPMA_NV_AUTHWRITE = 1,
		/* The Index data may be read if the authValue is provided. */
		. TPMA_NV_AUTHREAD = 1,
		/* A partial write of the Index data is not allowed. The write size
			* shall match the defined space size.
			*/
		.TPMA_NV_WRITEALL = 1,
		/* TPM2_NV_WriteLock may be used to prevent further writes
			* to this location regardless of TPM reset/restart.
			*/
		.TPMA_NV_WRITEDEFINE = 1,
		/* TPM2_NV_ReadLock may be used to SET TPMA_NV_READLOCKED
			* for this Index. When TPMA_NV_READLOCKED is set after calling TPM2_NV_ReadLock,
			* Reads of this Index are blocked until the next TPM Reset or TPM Restart.
			*/
		.TPMA_NV_READ_STCLEAR = 1,
		}
	},
	{NV_INDEX_OPTEEOS_SEED,
		{
		/* The Index data can be written if Owner Authorization is provided. */
		.TPMA_NV_OWNERWRITE = 1,
		/* Authorizations to change the Index contents that require
			* USER role may be provided with an HMAC session or password.
		*/
		.TPMA_NV_AUTHWRITE = 1,
		/* The Index data may be read if the authValue is provided. */
		. TPMA_NV_AUTHREAD = 1,
		/* A partial write of the Index data is not allowed. The write size
			* shall match the defined space size.
			*/
		.TPMA_NV_WRITEALL = 1,
		/* TPM2_NV_WriteLock may be used to prevent further writes
			* to this location regardless of TPM reset/restart.
			*/
		.TPMA_NV_WRITEDEFINE = 1,
		/* TPM2_NV_ReadLock may be used to SET TPMA_NV_READLOCKED
			* for this Index. When TPMA_NV_READLOCKED is set after calling TPM2_NV_ReadLock,
			* Reads of this Index are blocked until the next TPM Reset or TPM Restart.
			*/
		.TPMA_NV_READ_STCLEAR = 1,
		}
	},
    {NV_INDEX_BOOTLOADER,
		{
		.TPMA_NV_OWNERWRITE = 1,
		.TPMA_NV_AUTHWRITE = 1,
		.TPMA_NV_AUTHREAD = 1,
		.TPMA_NV_WRITE_STCLEAR = 1,
		.TPMA_NV_READ_STCLEAR = 1,
		}
	}
};

#define NV_INDEX_BOOTLOADER_STRUCT_VER	1
/* Since can't create new NV index after lock owner, so alloc more space for future usage */
#define NV_INDEX_BOOTLOADER_SIZE	512
typedef struct {
	UINT8	struct_ver;  /* the version of this struct */
	UINT8	lock_state;
	UINT8	reserved[6];  /* keep 8 bytes align */
	uint64_t rollback_index[8];  /* AVB max rollback index slot is 32, now we support 8 for TPM */
} tpm2_bootloader_t;


static EFI_STATUS tpm2_get_capability(
		IN	TPM_CAP			  Capability,
		IN	UINT32			  Property,
		IN	UINT32			  PropertyCount,
		OUT	TPMI_YES_NO		  * MoreData,
		OUT	TPMS_CAPABILITY_DATA	  * CapabilityData
		)
{
	EFI_STATUS ret;
	UINT32 i;
	TPML_TAGGED_TPM_PROPERTY *prop;

	ret = Tpm2GetCapability(Capability, Property, PropertyCount, MoreData, CapabilityData);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Call Tpm2GetCapability failed");
		return ret;
	}
	prop = &CapabilityData->data.tpmProperties;
	debug(L"TPM2 capability: 0x%08x, data.tpmProperties.count: %d, more data: %d",
			bswap_32(CapabilityData->capability), bswap_32(prop->count), *MoreData);
	for (i = 0; i < bswap_32(prop->count); i++)
		debug(L"prop %d: property: 0x%08x, value: 0x%08x", i,
				bswap_32(prop->tpmProperty[i].property),
				bswap_32(prop->tpmProperty[i].value));

	return ret;
}

static EFI_STATUS tpm2_get_cap_permanent(TPMA_PERMANENT *per)
{
	EFI_STATUS ret;
	TPMI_YES_NO more_data;
	TPMS_CAPABILITY_DATA cap_data;
	UINT32 value;
	TPML_TAGGED_TPM_PROPERTY *prop;

	ret = tpm2_get_capability(TPM_CAP_TPM_PROPERTIES, TPM_PT_PERMANENT, 1, &more_data, &cap_data);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Get TPM cap permanent failed");
		return ret;
	}
	prop = &cap_data.data.tpmProperties;
	if (bswap_32(prop->count) <= 0) {
		error(L"Get empty TPM capability data of TPM_PT_PERMANENT");
		ret = EFI_NOT_FOUND;
		return ret;
	}

	value = bswap_32(prop->tpmProperty[0].value);
	*per = *(TPMA_PERMANENT *)&value;

	return ret;
}

static EFI_STATUS tpm2_create_nvindex(TPMI_RH_NV_INDEX nv_index,
				TPMA_NV attributes,
				UINT32 data_size)
{
	TPMI_RH_PROVISION auth_handle = TPM_RH_OWNER;
	TPM2B_NV_PUBLIC public_info = {0};
	TPM2B_AUTH nv_auth = {0};

	nv_auth.size = 0;
	public_info.size = sizeof(TPMI_RH_NV_INDEX)
			   + sizeof(TPMI_ALG_HASH) + sizeof(TPMA_NV)
			   + sizeof(UINT16) + sizeof(UINT16);

	public_info.nvPublic.nvIndex = nv_index;
	public_info.nvPublic.nameAlg = TPM_ALG_SHA256;
	public_info.nvPublic.attributes = attributes;
	public_info.nvPublic.authPolicy.size = 0;
	public_info.nvPublic.dataSize = data_size;

	return Tpm2NvDefineSpace(auth_handle, NULL,
				 &nv_auth, &public_info);
}

static EFI_STATUS tpm2_write_nvindex(TPMI_RH_NV_INDEX nv_index,
				UINT16 data_size,
				BYTE *data,
				UINT16 offset)
{
	EFI_STATUS ret = EFI_SUCCESS;
	TPMS_AUTH_COMMAND session_data = {0};
	TPM2B_MAX_BUFFER nv_write_data = {0};
	INT8 retry_times = 5;

	session_data.sessionHandle = TPM_RS_PW;

	nv_write_data.size = data_size;
	memcpy(nv_write_data.buffer, data, nv_write_data.size);

	do {
		ret = Tpm2NvWrite(nv_index, nv_index, &session_data, &nv_write_data, offset);
		retry_times --;
	} while (ret == EFI_DEVICE_ERROR && retry_times > 0);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write TPM NV index failed, index: 0x%x, size: %d\n",
						nv_index, nv_write_data.size);
	}

	return ret;
}

static EFI_STATUS tpm2_write_lock_nvindex(TPMI_RH_NV_INDEX nv_index)
{
	TPMS_AUTH_COMMAND session_data = {0};

	session_data.sessionHandle = TPM_RS_PW;

	return Tpm2NvWriteLock(nv_index, nv_index, &session_data);
}

static EFI_STATUS tpm2_read_nvindex(TPMI_RH_NV_INDEX nv_index,
				UINT16 data_size,
				BYTE *data,
				UINT16 offset)
{
	EFI_STATUS ret;
	TPMS_AUTH_COMMAND session_data = {0};
	TPM2B_MAX_BUFFER nv_read_data = {0};
	INT8 retry_times = 5;

	session_data.sessionHandle  = TPM_RS_PW;
	session_data.nonce.size	    = 0;
	*((UINT8 *) &(session_data.sessionAttributes)) = 0;
	session_data.hmac.size	    = 0;

	nv_read_data.size = data_size;

	do {
		ret = Tpm2NvRead(nv_index, nv_index, &session_data, nv_read_data.size, offset, &nv_read_data);
		retry_times --;
	} while (ret == EFI_DEVICE_ERROR && retry_times > 0);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read NVIndex failed\n");
		return ret;
	}
	memcpy(data, nv_read_data.buffer, nv_read_data.size);

	return EFI_SUCCESS;
}

static EFI_STATUS tpm2_read_lock_nvindex(TPMI_RH_NV_INDEX nv_index)
{
	TPMS_AUTH_COMMAND session_data = {0};

	session_data.sessionHandle  = TPM_RS_PW;
	session_data.nonce.size	    = 0;
	*((UINT8 *)&(session_data.sessionAttributes)) = 0;
	session_data.hmac.size	    = 0;

	return Tpm2NvReadLock(nv_index, nv_index, &session_data);
}

static EFI_STATUS check_provision_status(void)
{
	EFI_STATUS ret = EFI_SUCCESS;
	TPM2B_NV_PUBLIC NvPublic;
	TPM2B_NAME NvName;
	attribute_matrix_t matrix, expected_table[MAX_NV_NUMBER];
	UINT32 i;

	ret = memcpy_s(expected_table, sizeof(expected_table), config_table, sizeof(expected_table));
	if (EFI_ERROR(ret))
		return ret;

	for (i = 0; i < MAX_NV_NUMBER; i++) {
		ret = Tpm2NvReadPublic(config_table[i].nv_index, &NvPublic, &NvName);
		if (EFI_ERROR(ret)) {
			efi_perror(ret, L"Tpm2NvReadPublic TPM NV index %x", config_table[i].nv_index);
			return ret;
		}
		matrix.nv_index = NvPublic.nvPublic.nvIndex;
		/* TPMA_NV_WRITTEN = 1, Index has been written.
		 * TPMA_NV_WRITELOCKED =1, Index cannot be written.
		 * Check these two additional attributes after provision. They are set by TPM.
		 */
		NvPublic.nvPublic.attributes.TPMA_NV_WRITTEN = 0;
		NvPublic.nvPublic.attributes.TPMA_NV_WRITELOCKED = 0;
		matrix.attribute = NvPublic.nvPublic.attributes;
		if (memcmp(&matrix, &expected_table[i], sizeof(matrix))) {
			error(L"NV index 0x%08x, attribute: 0x%lx, expected: 0x%lx",
					matrix.nv_index,
					*(UINT32 *)&matrix.attribute,
					*(UINT32 *)&expected_table[i].attribute);
			return EFI_DEVICE_ERROR;
		}
	}

	return EFI_SUCCESS;
}

EFI_STATUS tpm2_delete_index(UINT32 index)
{
	EFI_STATUS ret = Tpm2NvUndefineSpace(TPM_RH_OWNER, index, NULL);

	if (EFI_ERROR(ret))
		efi_perror(ret, L"Delete TPM NV index failed, index: %x", index);

	return ret;
}

EFI_STATUS tpm2_fuse_provision_seed(void)
{
	EFI_STATUS ret = EFI_SUCCESS;

	// Check secure boot is enabled
	if (!is_platform_secure_boot_enabled()) {
		error(L"Secure boot is disabled, does not fuse trusty seed to TPM");
		return EFI_SECURITY_VIOLATION;
	}

	ret = tpm2_delete_index(NV_INDEX_TRUSTYOS_SEED);
	if (EFI_ERROR(ret) && ret != EFI_NOT_FOUND) {
		error(L"failed to delete NV_INDEX_TRUSTYOS_SEED");
		return ret;
	}
	return tpm2_fuse_trusty_seed();
}

EFI_STATUS tpm2_fuse_lock_owner(void)
{
	TPMS_AUTH_COMMAND session_data = {0};
	TPM2B_AUTH owner_auth;
	EFI_STATUS ret;
	TPMA_PERMANENT per;
	UINT8 state;

	ret = tpm2_get_cap_permanent(&per);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Check TPM cap permanent for lock owner failed");
		return ret;
	}

	if (per.ownerAuthSet) {
		error(L"TPM owner is already locked");
		return EFI_SUCCESS;
	}

	/* Check the provison and secure boot status */
	if (!is_platform_secure_boot_enabled() || EFI_ERROR(check_provision_status())) {
		error(L"Provision is not completed or secure boot is not enabled, DO NOT LOCK OWNER");
		return EFI_DEVICE_ERROR;
	}

	/* Check can read the bootloader NV index */
	ret = read_device_state_tpm2(&state);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read device state failed, should not lock the owner!");
		return ret;
	}

	session_data.sessionHandle = TPM_RS_PW;
	session_data.nonce.size = 0;
	session_data.hmac.size = 0;
	*((UINT8 *)((void *)&session_data.sessionAttributes)) = 0;

	ret = Tpm2GetRandom(DIGEST_SIZE, &owner_auth);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"failed to get random");
		goto out;
	}

	ret = Tpm2HierarchyChangeAuth(TPM_RH_OWNER, &session_data, &owner_auth);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"failed to Tpm2HierarchyChangeAuth");
		goto out;
	}

	ret = tpm2_get_cap_permanent(&per);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Check TPM cap permanent after take owner failed");
		goto out;
	}

	if (!per.ownerAuthSet) {
		error(L"Try to lock TPM owner, success call Tpm2HierarchyChangeAuth, but ownerAuthSet is not set!");
		ret = EFI_SECURITY_VIOLATION;
		goto out;
	}

	debug(L"Success lock TPM owner");

out:
	memset_s(owner_auth.buffer, DIGEST_SIZE, 0, DIGEST_SIZE);
	return ret;
}

static EFI_STATUS create_index_and_write_lock(TPM_NV_INDEX nv_index, TPMA_NV attributes,
					      UINT16 data_size, BYTE *data)
{
	EFI_STATUS ret;

	ret = tpm2_create_nvindex(nv_index, attributes, data_size);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"NV Index failed to create, index: 0x%x, size: %d", nv_index, data_size);
		return ret;
	}

	ret = tpm2_write_nvindex(nv_index, data_size, data, 0);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write to NV Index failed, index: 0x%x, size: %d", nv_index, data_size);
		return ret;
	}

	ret = tpm2_write_lock_nvindex(nv_index);
	if (EFI_ERROR(ret))
		efi_perror(ret, L"Write lock to NV Index failed, index: 0x%x", nv_index);

	return ret;
}

#ifndef USER
EFI_STATUS tpm2_show_index(UINT32 index, uint8_t *out_buffer, UINTN out_buffer_size)
{
	EFI_STATUS ret;
	TPM2B_NV_PUBLIC NvPublic;
	TPM2B_NAME NvName;

	ret = Tpm2NvReadPublic(index, &NvPublic, &NvName);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read TPM NV index %x", index);
		return ret;
	}
	efi_snprintf(out_buffer, out_buffer_size, (CHAR8 *)
		"Read TPM NV index %x success, public size: %d, nvIndex: 0x%x, nameAlg: %d, attributes: 0x%x, data size: %d, name size: %d",
		index,
		NvPublic.size, NvPublic.nvPublic.nvIndex, NvPublic.nvPublic.nameAlg,
		NvPublic.nvPublic.attributes, NvPublic.nvPublic.dataSize, NvName.size);

	return EFI_SUCCESS;
}
#endif // USER

static EFI_STATUS tpm2_check_cap_permanent(void)
{
	EFI_STATUS ret;
	TPMA_PERMANENT per;

	ret = tpm2_get_cap_permanent(&per);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Check TPM cap permanent for lockoutAuthSet failed");
		return ret;
	}

	/* Verify the LOCKOUT_AUTH */
	if (!per.lockoutAuthSet)
		info(L"TPM LOCKOUT_AUTH is not set, set it can get higher security");

	if (!per.ownerAuthSet)
		info(L"TPM owner is not taken! Take it after verification to get higher security!");

	return ret;
}

EFI_STATUS tpm2_fuse_trusty_seed(void)
{
	EFI_STATUS ret;
	TPM2B_DIGEST trusty_seed;
	UINT8 read_seed[TRUSTY_SEED_SIZE];
	UINT16 read_seed_size = TRUSTY_SEED_SIZE;
	UINT16 config_index;

	ret = Tpm2GetRandom(TRUSTY_SEED_SIZE, &trusty_seed);
	if (EFI_ERROR(ret)) {
		error(L"Tpm2GetRandom failed");
		goto out;
	}

	config_index = NV_INDEX_TRUSTYOS_SEED - config_table[0].nv_index;
	ret = create_index_and_write_lock(NV_INDEX_TRUSTYOS_SEED, config_table[config_index].attribute,
					TRUSTY_SEED_SIZE, trusty_seed.buffer);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to create and write trusty seed");
		goto out;
	}
	debug(L"Success create and write trusty seed");

	// Read the data again to verify it
	ret = tpm2_read_nvindex(NV_INDEX_TRUSTYOS_SEED, read_seed_size, read_seed, 0);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read trusty seed back failed just after write it");
		goto out;
	}
	if (memcmp(trusty_seed.buffer, read_seed, sizeof(read_seed))) {
		error(L"Security error! Read trusty seed back but verify failed!");
		ret = EFI_SECURITY_VIOLATION;
		goto out;
	}

out:
	// Always clear the memory
	// Maybe be optimized?
	memset_s(trusty_seed.buffer, TRUSTY_SEED_SIZE, 0, TRUSTY_SEED_SIZE);
	memset_s(read_seed, TRUSTY_SEED_SIZE, 0, TRUSTY_SEED_SIZE);
	barrier();
	return ret;
}

EFI_STATUS tpm2_read_trusty_seed(UINT8 seed[TRUSTY_SEED_SIZE])
{
	EFI_STATUS ret;
	EFI_STATUS ret2;
	UINT16 seed_size = TRUSTY_SEED_SIZE;

	ret = tpm2_read_nvindex(NV_INDEX_TRUSTYOS_SEED, seed_size, seed, 0);
	ret2 = tpm2_read_lock_nvindex(NV_INDEX_TRUSTYOS_SEED);	// Lock anyway
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read trusty seed failed");
		goto out;
	}
	if (EFI_ERROR(ret2)) {
		efi_perror(ret2, L"Security error! Set trusty seed read lock failed!");
		// die?
		ret = ret2;
		goto out;
	}
	if (seed_size != TRUSTY_SEED_SIZE) {
		efi_perror(ret, L"Read trusty seed failed, read %d bytes data, but expect %d",
				TRUSTY_SEED_SIZE, seed_size);
		ret = EFI_COMPROMISED_DATA;
		goto out;
	}

	return EFI_SUCCESS;

out:
	memset_s(seed, TRUSTY_SEED_SIZE, 0, TRUSTY_SEED_SIZE);
	barrier();
	return ret;
}

static EFI_STATUS tpm2_check_trusty_seed_index(void)
{
	EFI_STATUS ret;
	TPM2B_NV_PUBLIC NvPublic;
	TPM2B_NAME NvName;
	UINT32 *attr;
	UINT32 *config_attr;

	ret = Tpm2NvReadPublic(NV_INDEX_TRUSTYOS_SEED, &NvPublic, &NvName);
	if (EFI_ERROR(ret)) {
		if (ret != EFI_NOT_FOUND) {
			efi_perror(ret, L"Read trusty seed NV index failed");
			return ret;
		}

		ret = tpm2_fuse_trusty_seed();
		if (EFI_ERROR(ret))
			efi_perror(ret, L"Failed to fuse trusty seed");

		return ret;
	}

	/* After fuse, the TPM maybe add more attribute, so need to skip them */
	NvPublic.nvPublic.attributes.TPMA_NV_WRITTEN = 0;
	NvPublic.nvPublic.attributes.TPMA_NV_WRITELOCKED = 0;
	attr = (UINT32 *)&NvPublic.nvPublic.attributes;
	config_attr = (UINT32 *)&config_table[NV_INDEX_TRUSTYOS_SEED - config_table[0].nv_index].attribute;
	if (NvPublic.nvPublic.dataSize != TRUSTY_SEED_SIZE || *attr != *config_attr) {
		error(L"Find trusty seed NV index, but the data is wrong, data size: %d, attributes: 0x%lx, need: 0x%lx",
				NvPublic.nvPublic.dataSize, *attr, *config_attr);
		return EFI_COMPROMISED_DATA;
	}

	debug(L"Trusty seed already fused");
	return EFI_SUCCESS;
}

static EFI_STATUS tpm2_fuse_bootloader(void)
{
	EFI_STATUS ret;
	UINT16 config_index;
	BYTE data[NV_INDEX_BOOTLOADER_SIZE] = {0};
	BYTE data_read[sizeof(data)];
	UINT16 data_read_size = sizeof(data);
	tpm2_bootloader_t *bootloader = (tpm2_bootloader_t *)data;

	config_index = NV_INDEX_BOOTLOADER - config_table[0].nv_index;
	ret = tpm2_create_nvindex(NV_INDEX_BOOTLOADER, config_table[config_index].attribute, sizeof(data));
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to create bootloader NV index");
		return ret;
	}

	bootloader->struct_ver = NV_INDEX_BOOTLOADER_STRUCT_VER;
	/* Set to unlock in a device just create the NV index. */
	bootloader->lock_state = UNLOCKED;

	ret = tpm2_write_nvindex(NV_INDEX_BOOTLOADER, sizeof(data), data, 0);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write bootloader NV index failed");
		return ret;
	}

	/* Read the data again to verify it */
	ret = tpm2_read_nvindex(NV_INDEX_BOOTLOADER, data_read_size, data_read, 0);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read bootloader NV index back failed just after write it");
		return ret;
	}

	if (memcmp(data, data_read, sizeof(data))) {
		error(L"Security error! Read bootloader NV index back but verify failed!");
		return EFI_SECURITY_VIOLATION;
	}

	debug(L"Success create and write bootloader NV index");
	return EFI_SUCCESS;
}

static EFI_STATUS tpm2_check_bootloader_index(void)
{
	EFI_STATUS ret;
	TPM2B_NV_PUBLIC NvPublic;
	TPM2B_NAME NvName;
	UINT8 struct_ver;
	UINT16 data_size = sizeof(struct_ver);
	UINT32 *attr;
	UINT32 *config_attr;

	ret = Tpm2NvReadPublic(NV_INDEX_BOOTLOADER, &NvPublic, &NvName);
	if (EFI_ERROR(ret)) {
		if (ret != EFI_NOT_FOUND) {
			efi_perror(ret, L"Read bootloader NV index failed");
			return ret;
		}

		ret = tpm2_fuse_bootloader();
		if (EFI_ERROR(ret))
			efi_perror(ret, L"Failed to fuse bootloader NV index");

		return ret;
	}

	/* After fuse, the TPM maybe add more attribute, so need to skip them */
	NvPublic.nvPublic.attributes.TPMA_NV_WRITTEN = 0;
	NvPublic.nvPublic.attributes.TPMA_NV_WRITELOCKED = 0;
	attr = (UINT32 *)&NvPublic.nvPublic.attributes;
	config_attr = (UINT32 *)&config_table[NV_INDEX_BOOTLOADER - config_table[0].nv_index].attribute;
	if (NvPublic.nvPublic.dataSize < sizeof(tpm2_bootloader_t) || *attr != *config_attr) {
		error(L"Find bootloader NV index, but the data is wrong, data size: %d, attributes: 0x%lx, need: 0x%lx",
				NvPublic.nvPublic.dataSize, *attr, *config_attr);
		return EFI_COMPROMISED_DATA;
	}

	ret = tpm2_read_nvindex(NV_INDEX_BOOTLOADER, data_size,
			(BYTE *)&struct_ver,
			offsetof(tpm2_bootloader_t, struct_ver));
	if (EFI_ERROR(ret) || data_size != sizeof(struct_ver)) {
		efi_perror(ret, L"Read bootloader NV index for struct version failed, read size: %d", data_size);
		return ret;
	}

	if (struct_ver > NV_INDEX_BOOTLOADER_STRUCT_VER)
		warning(L"Bootloader NV index is fused with new struct version %d, are you running old software?", struct_ver);
	else if (struct_ver < NV_INDEX_BOOTLOADER_STRUCT_VER)
		warning(L"Bootloader NV index is fused with old struct version %d, are you running in old device?", struct_ver);
	else {
		if (NvPublic.nvPublic.dataSize != NV_INDEX_BOOTLOADER_SIZE) {
			error(L"Find bootloader NV index, but the NV index size is %d", NvPublic.nvPublic.dataSize);
			return EFI_COMPROMISED_DATA;
		}
		debug(L"Bootloader NV index already fused");
	}
	return EFI_SUCCESS;
}

EFI_STATUS read_device_state_tpm2(UINT8 *state)
{
	EFI_STATUS ret;
	UINT16 data_size = sizeof(UINT8);

	ret = tpm2_read_nvindex(NV_INDEX_BOOTLOADER, data_size, (BYTE *)state,
			offsetof(tpm2_bootloader_t, lock_state));
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read device state from TPM failed");
		return ret;
	}

	if (data_size != sizeof(UINT8)) {
		error(L"Read device state from TPM, but data size is wrong: %d", data_size);
		return EFI_COMPROMISED_DATA;
	}

	debug(L"Read device state from TPM success, state: %d", *state);
	return ret;
}

EFI_STATUS write_device_state_tpm2(UINT8 state)
{
	EFI_STATUS ret;

	ret = tpm2_write_nvindex(NV_INDEX_BOOTLOADER, sizeof(UINT8), (BYTE *)&state,
			offsetof(tpm2_bootloader_t, lock_state));
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write device state %d to TPM failed", state);
		return ret;
	}

	debug(L"Write device state %d to TPM success", state);
	return ret;
}

EFI_STATUS read_rollback_index_tpm2(size_t rollback_index_slot, uint64_t *out_rollback_index)
{
	EFI_STATUS ret;
	UINT16 data_size = sizeof(uint64_t);

	if (rollback_index_slot >= ARRAY_SIZE(((tpm2_bootloader_t *)0)->rollback_index)) {
		error(L"The rollback index slot is too large to write into TPM: %d", rollback_index_slot);
		return EFI_INVALID_PARAMETER;
	}

	ret = tpm2_read_nvindex(NV_INDEX_BOOTLOADER, data_size, (BYTE *)out_rollback_index,
			rollback_index_slot * sizeof(uint64_t) + offsetof(tpm2_bootloader_t, rollback_index));
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read rollback index from TPM failed, slot: %d", rollback_index_slot);
		return ret;
	}

	if (data_size != sizeof(uint64_t)) {
		error(L"Read rollback index from TPM, but data size is wrong: %d", data_size);
		return EFI_COMPROMISED_DATA;
	}

	debug(L"Read rollback index from TPM success, slot: %d, index: 0x%llx", rollback_index_slot, *out_rollback_index);
	return ret;
}

EFI_STATUS write_rollback_index_tpm2(size_t rollback_index_slot, uint64_t rollback_index)
{
	EFI_STATUS ret;

	if (rollback_index_slot >= ARRAY_SIZE(((tpm2_bootloader_t *)0)->rollback_index)) {
		error(L"The rollback index slot is too large to write into TPM: %d", rollback_index_slot);
		return EFI_INVALID_PARAMETER;
	}

	ret = tpm2_write_nvindex(NV_INDEX_BOOTLOADER, sizeof(uint64_t), (BYTE *)&rollback_index,
			rollback_index_slot * sizeof(uint64_t) + offsetof(tpm2_bootloader_t, rollback_index));
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write rollback index to TPM failed, slot: %d, index: 0x%llx",
				rollback_index_slot, rollback_index);
		return ret;
	}

	debug(L"Write rollback index to TPM success, slot: %d, index: 0x%llx", rollback_index_slot, rollback_index);
	return ret;
}

BOOLEAN tpm2_bootloader_need_init(void)
{
	EFI_STATUS ret;
	TPM2B_NV_PUBLIC NvPublic;
	TPM2B_NAME NvName;

	ret = Tpm2NvReadPublic(NV_INDEX_BOOTLOADER, &NvPublic, &NvName);
	if (EFI_ERROR(ret)) {
		if (ret == EFI_NOT_FOUND)
			return TRUE;
		efi_perror(ret, L"Failed to read NV_INDEX_BOOTLOADER");
	}

	return FALSE;
}

EFI_STATUS tpm2_init(void)
{
	EFI_STATUS ret;

	ret = tpm2_check_cap_permanent();
	if (EFI_ERROR(ret))
		return ret;

	ret = tpm2_check_bootloader_index();
	if (EFI_ERROR(ret))
		return ret;

	ret = tpm2_check_trusty_seed_index();
	if (EFI_ERROR(ret))
		return ret;

	if (is_platform_secure_boot_enabled())
		debug(L"Android TPM init OK. Secure boot ENABLED.");
	else
		debug(L"Android TPM init OK. Secure boot DISABLED.");

	return ret;
}

EFI_STATUS tpm2_end(void)
{
	/* Maybe set read/write lock again */
	tpm2_read_lock_nvindex(NV_INDEX_TRUSTYOS_SEED);
	tpm2_read_lock_nvindex(NV_INDEX_BOOTLOADER);
	tpm2_write_lock_nvindex(NV_INDEX_BOOTLOADER);

	return EFI_SUCCESS;
}

////////////////////////////TPM Requests are forwared to OPTEE/////////////////////////////

EFI_STATUS tee_tpm2_init(void)
{
	struct tpm2_int_req req = {0};
	req.cmd = TEE_TPM2_INIT;
	ivshmem_rollback_index_interrupt(&req);

	if (EFI_ERROR(req.ret)) {
		efi_perror(req.ret, L"TPM init failed.");
		return req.ret;
	}

	if (is_platform_secure_boot_enabled())
		debug(L"TEE TPM init OK. Secure boot ENABLED.");
	else
		debug(L"TEE TPM init OK. Secure boot DISABLED.");

	return req.ret;
}

EFI_STATUS tee_tpm2_end(void)
{
	struct tpm2_int_req req = {0};
	req.cmd = TEE_TPM2_END;
	ivshmem_rollback_index_interrupt(&req);

	return req.ret;
}

EFI_STATUS tee_read_device_state_tpm2(UINT8 *state)
{
	struct tpm2_int_req *req = (struct tpm2_int_req *)AllocateZeroPool(sizeof(struct tpm2_int_req) + sizeof(state));
	if (!req)
		return EFI_OUT_OF_RESOURCES;

	req->cmd = TEE_TPM2_READ_DEVICE_STATE;
	req->size = sizeof(*state);

	ivshmem_rollback_index_interrupt(req);

	EFI_STATUS ret = req->ret;
	*state = *(UINT8 *)(req->payload);

	FreePool(req);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read device state from TPM failed");
		return ret;
	}

	debug(L"Read device state from TPM success, state: %d", *state);
	return ret;
}

EFI_STATUS tee_write_device_state_tpm2(UINT8 state)
{
	struct tpm2_int_req *req = (struct tpm2_int_req *)AllocateZeroPool(sizeof(struct tpm2_int_req) + sizeof(state));
	if (!req)
		return EFI_OUT_OF_RESOURCES;

	req->cmd = TEE_TPM2_WRITE_DEVICE_STATE;
	req->size = sizeof(state);
	*(UINT8 *)(req->payload) = state;

	ivshmem_rollback_index_interrupt(req);
	EFI_STATUS ret = req->ret;

	FreePool(req);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write device state[%u] to TPM failed", state);
		return ret;
	}

	debug(L"Write device state %d to TPM success", state);
	return ret;
}

EFI_STATUS tee_read_rollback_index_tpm2(size_t rollback_index_slot, uint64_t *out_rollback_index)
{
	uint32_t payload_len = sizeof(rollback_index_slot) + sizeof(*out_rollback_index);
	struct tpm2_int_req *req = (struct tpm2_int_req *)AllocateZeroPool(sizeof(struct tpm2_int_req) + payload_len);
	if (!req)
		return EFI_OUT_OF_RESOURCES;

	req->cmd = TEE_TPM2_READ_ROLLBACK_INDEX;
	req->size = payload_len;

	ivshmem_rollback_index_interrupt(req);

	*out_rollback_index = *(uint64_t*)((req->payload)+sizeof(rollback_index_slot));
	EFI_STATUS ret = req->ret;

	FreePool(req);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Read rollback index from TPM failed, slot: %d", rollback_index_slot);
		return ret;
	}

	debug(L"Read rollback index from TPM success, slot: %d, index: 0x%llx", rollback_index_slot, *out_rollback_index);
	return ret;
}

EFI_STATUS tee_write_rollback_index_tpm2(size_t rollback_index_slot, uint64_t rollback_index)
{
	uint32_t payload_len = sizeof(rollback_index_slot) + sizeof(rollback_index);
	struct tpm2_int_req *req = (struct tpm2_int_req *)AllocateZeroPool(sizeof(struct tpm2_int_req) + payload_len);
	if (!req)
		return EFI_OUT_OF_RESOURCES;

	req->cmd = TEE_TPM2_WRITE_ROLLBACK_INDEX;
	req->size = payload_len;

	ivshmem_rollback_index_interrupt(req);

	EFI_STATUS ret = req->ret;
	FreePool(req);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Write rollback index to TPM failed, slot: %d, index: 0x%llx",
				rollback_index_slot, rollback_index);
		return ret;
	}

	debug(L"Write rollback index to TPM success, slot: %d, index: 0x%llx", rollback_index_slot, rollback_index);
	return ret;
}

BOOLEAN tee_tpm2_bootloader_need_init(void)
{
	struct tpm2_int_req req = {0};
	req.cmd = TEE_TPM2_BOOTLOADER_NEED_INIT;
	ivshmem_rollback_index_interrupt(&req);

	return (BOOLEAN)req.ret;
}

#ifndef USER
EFI_STATUS tee_tpm2_show_index(__attribute__((unused)) UINT32 index, __attribute__((unused)) uint8_t *out_buffer, __attribute__((unused)) UINTN out_buffer_size)
{
	return EFI_UNSUPPORTED;
}

EFI_STATUS tee_tpm2_delete_index(__attribute__((unused)) UINT32 index)
{
	return EFI_UNSUPPORTED;
}

#endif	// USER

EFI_STATUS tee_tpm2_fuse_lock_owner(void)
{
	struct tpm2_int_req req = {0};
	req.cmd = TEE_TPM2_FUSE_LOCK_OWNER;
	ivshmem_rollback_index_interrupt(&req);

	return req.ret;
}

EFI_STATUS tee_tpm2_fuse_provision_seed(void)
{
	return EFI_UNSUPPORTED;
}
