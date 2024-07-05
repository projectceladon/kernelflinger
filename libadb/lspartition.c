/*
 * Copyright (c) 2019, Intel Corporation
 * All rights reserved.
 *
 * Authors: Jeremy Compostella <jeremy.compostella@intel.com>
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

#include <lib.h>
#include <gpt.h>

#include "lspartition.h"

static EFI_STATUS lspartition_main(INTN argc,
				   __attribute__((__unused__)) const char **argv)
{
	EFI_STATUS ret;
	struct gpt_partition_interface *gparti;
	UINTN part_count, i, max_len = 0;
	CHAR16 title_fmt[64], part_fmt[64];

	if (argc != 1)
		return EFI_INVALID_PARAMETER;


	ret = gpt_list_partition(&gparti, &part_count, LOGICAL_UNIT_USER);
	if (EFI_ERROR(ret) || part_count == 0) {
		if (gparti) 
			FreePool(gparti);
		return EFI_SUCCESS;
	}

	for (i = 0; i < part_count; i++)
		max_len = max(max_len, StrLen(gparti[i].part.name));

	SPrint(title_fmt, sizeof(title_fmt),
	       L"%%.-2a  %%-.%da  %%-.12a  %%a\n-\n", max_len);
	ret = ss_printf(title_fmt, "#", "Name", "Offset", "Size (MB)");

	SPrint(part_fmt, sizeof(part_fmt),
	       L"%%.-2d  %%.-%ds  0x%%010llx  %%8lld\n", max_len);
	for (i = 0; i < part_count; i++) {
		UINT64 size, offset;
		size = gparti[i].bio->Media->BlockSize
			* (gparti[i].part.ending_lba + 1 - gparti[i].part.starting_lba);
		offset = gparti[i].part.starting_lba * gparti[i].bio->Media->BlockSize;
		ss_printf(part_fmt, i + 1, gparti[i].part.name, offset,
			  size / (1024 * 1024));
	}

	return EFI_SUCCESS;
}

shcmd_t lspartition_shcmd = {
	.name = "lspartition",
	.summary = "List the GPT partitions",
	.help = "Usage: lspartition",
	.main = lspartition_main
};
