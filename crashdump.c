/*
 * Copyright (c) 2024, Intel Corporation
 * All rights reserved.
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
 *
 */


#include <efi.h>
#include <efiapi.h>
#include <efilib.h>
#include <pae.h>
#include "gpt.h"
#include "protocol.h"
#include "uefi_utils.h"
#include "security_interface.h"
#include "crashdump.h"

BOOLEAN tee_tpm = 0;
BOOLEAN andr_tpm = 0;

static struct gpt_partition_interface gparti;
static UINT64 cur_offset;

#define part_start (gparti.part.starting_lba * gparti.bio->Media->BlockSize)
#define part_end ((gparti.part.ending_lba + 1) * gparti.bio->Media->BlockSize)

#define is_inside_partition(off, sz) \
		(off >= part_start && off + sz <= part_end)

void __attribute__((weak)) part_select(int num)
{
	(void)num;
}

EFI_STATUS flash_write(VOID *data, UINTN size)
{
	EFI_STATUS ret;

	if (!gparti.bio)
		return EFI_INVALID_PARAMETER;

	if (!is_inside_partition(cur_offset, size)) {
		error(L"Attempt to write outside of partition [%ld %ld] [%ld %ld]",
				part_start, part_end, cur_offset, cur_offset + size);
		return EFI_INVALID_PARAMETER;
	}
	ret = uefi_call_wrapper(gparti.dio->WriteDisk, 5, gparti.dio, gparti.bio->Media->MediaId, cur_offset, size, data);

	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to write bytes");
		return ret;
	}

	cur_offset += size;
	ret = uefi_call_wrapper(gparti.bio->FlushBlocks, 1, gparti.bio);

	return ret;
}

EFI_STATUS flash_write_as_block(VOID *data, UINTN size)
{
	EFI_STATUS ret;
	UINT32 *aligned_buf;
	VOID *buf;
	UINTN buf_size, write_size;

	if (!gparti.bio || !size || size % gparti.bio->Media->BlockSize)
		return EFI_INVALID_PARAMETER;

	// the N_BLOCK is 4096, to save the allocate heap, here reduce to 2048
	buf_size = min(gparti.bio->Media->BlockSize * 2048, size);
	ret = alloc_aligned(&buf, (VOID **)&aligned_buf, buf_size, gparti.bio->Media->IoAlign);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Unable to allocate the buf");
		return ret;
	}

	for (; size; size -= write_size) {
		write_size = min(size, buf_size);
		memcpy(aligned_buf,data,write_size);
		ret = flash_write(aligned_buf, write_size);
		if (EFI_ERROR(ret))
			goto out;
		data +=write_size;
	}
out:
	FreePool(buf);
	return ret;
}

EFI_STATUS crashdump_to_partition(EFI_GUID * uuid)
{
	EFI_STATUS ret;
	UINTN nr_entries, key, entry_sz;
	CHAR8 *mem_entries;
	UINT32 entry_ver;
	UINTN i;
	CHAR8 *mem_map;
	dump_hdr_t head;
	VOID * shm_start = NULL;

	mem_entries = (CHAR8 *)LibMemoryMap(&nr_entries, &key, &entry_sz, &entry_ver);
	if (!mem_entries) {
		return EFI_OUT_OF_RESOURCES;
	}

	sort_memory_map(mem_entries, nr_entries, entry_sz);
	mem_map = mem_entries;
#ifndef __LP64__
	ret = pae_init(mem_entries, nr_entries, entry_sz);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"pae_init failed\n");
		goto err;
	}
#endif
	ret = gpt_get_partition_by_uuid(uuid, &gparti, LOGICAL_UNIT_USER);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get partition by UUID");
		goto err;
	}

	cur_offset = gparti.part.starting_lba * gparti.bio->Media->BlockSize;

	memset(&head, 0, sizeof(dump_hdr_t));
	strncpy(head.magic, DUMP_MAGIC, sizeof(DUMP_MAGIC));
	head.dump_hdr_ver = DUMP_HEAD_VERSION;
	head.owner = DUMP_SBL;
	head.region_num = 0;

	for (i = 0; i < nr_entries; mem_entries += entry_sz, i++) {
		EFI_MEMORY_DESCRIPTOR *entry;

		entry = (EFI_MEMORY_DESCRIPTOR *)mem_entries;
		if (entry->Type == EfiConventionalMemory) {
			head.dump_ram_region[head.region_num].start = entry->PhysicalStart;
			head.dump_ram_region[head.region_num].map_sz = entry->NumberOfPages * EFI_PAGE_SIZE;
			head.region_num += 1;
		} else if (entry->Type == EfiLoaderCode) {
			shm_start = (VOID *)(entry->PhysicalStart);
		}
	}

	if (shm_start == NULL)
	{
		efi_perror(ret, L"Can't find reserved share memory region\n");
		goto err;
        }

	ret = flash_write_as_block((void *)&head, DUMP_HEAD_SIZE);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to write dump head to partition\n");
		goto err;
	}

	//write reserved share memory, should below 4G, aigned.
	ret = flash_write_as_block(shm_start, (UINTN)RESERVED_MEM_SIZE);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to write shm region to partition\n");
		goto err;
	}


	for (i = 0; i < head.region_num; i++) {
		int blocks = 0;
		EFI_PHYSICAL_ADDRESS start = head.dump_ram_region[i].start;
		UINT64 map_sz = head.dump_ram_region[i].map_sz, len,lba_skip;
		void *buf;

		lba_skip = cur_offset / (UINT64)(gparti.bio->Media->BlockSize) - gparti.part.starting_lba;
		debug(L"%d",head.region_num-i);

		for (; map_sz > 0; map_sz -= len, start += len) {
			len = map_sz;
#ifdef __LP64__
			buf = (void *)start;
#else
			ret = pae_map(start, (unsigned char **)&buf, &len);
			if (EFI_ERROR(ret)) {
				efi_perror(ret, L"PAE map fail for high-mem\n");
				goto pae_err;
			}
#endif
			ret = flash_write_as_block(buf, len);
			if (EFI_ERROR(ret)) {
				efi_perror(ret, L"Failed to write dump ram 0x%llx to partition\n",buf);
				goto err;
			}
			log(L".");
			if (blocks % 16 == 0) {
				log(L"\n");
			}
			blocks ++;
		}
	}
	debug(L"Dump done!!");

#ifndef __LP64__
pae_err:
		pae_exit();
#endif
err:
	FreePool((void *)mem_map);
	//debug(L"PAE exit buf=0x%x, head=0x%x\n",buf,*(UINT32*)buf);
	return ret;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys_table)
{
#ifdef __CRASH_DUMP
	EFI_GUID dump_partition =  { 0xCAB9B00C, 0xCC1B, 0x4C0F, {0xB9, 0x32, 0x82, 0x92, 0x0D, 0xA5, 0x22, 0x51} };
#endif

	set_boottime_stamp(TM_EFI_MAIN);
	/* gnu-efi initialization */
	InitializeLib(image, sys_table);

#ifdef __CRASH_DUMP
	debug(L"Saving crash dump to partition");
	crashdump_to_partition(&dump_partition);

	reboot_to_target(NORMAL_BOOT, EfiResetCold);
#endif
	return EFI_SUCCESS;
}
