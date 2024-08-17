/*
 * Copyright (c) 2024, Intel Corporation
 * All rights reserved.
 *
 * Authors: Austin Sun <austin.sun@intel.com>
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
#ifndef _FATFS_H_
#define _FATFS_H_
#include <efi.h>
#include <efilib.h>
#include <lib.h>
#include "gpt.h"
#include "ff.h"
#define UINT8to16(high, low) (((UINT16)(high) << 8) | (low))
#define UINT8to32(byte3, byte2, byte1, byte0) \
	(((UINT32)(byte3) << 24) | \
	 ((UINT32)(byte2) << 16) | \
	 ((UINT32)(byte1) << 8)  | \
	 (byte0))
#define FAT12 1
#define FAT16 2 
#define FAT32 4

/*https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf*/
typedef struct fat_fs {
	UINT16 BytsPerSec;
	UINT8  SecPerClus;
	UINT16 RsvdSecCnt;
	UINT8  NumFATs;
	UINT16 RootEntCnt;
	UINT16 TotSec16;
	UINT16 FATSz16;
	UINT32 TotSec32;
	//extends BPB for FAT32;
	UINT32 FATSz32;
	UINT32 RootClus;
	UINT16 FSInfo;
	UINT16 BkBootSec;
	UINT16 RootDirSectors;
	//0: FAT12,1:FAT16,2:FAT32
	UINT8  FilSysType;
	UINT32 TotSec;
	UINT32 FATSz;
	UINT32 DataSec;
	UINT32 CountofClusters;
	UINT32 bpb_offset;
	UINT32 fat_offset;
	UINT32 root_offset;
	UINT32 data_offset;
	struct gpt_partition_interface parti;
} FAT_FS;

typedef struct fatsystem {
	struct gpt_partition_interface parti;
	FATFS fatfs;
	UINT32 bpb_offset;
} FATSYSTEM;

typedef struct fatfs_fsobj {
	FAT_FS * fs;
	UINT8 sname[11];
	UINT8 attr;
	UINT8 CrtTimeTenth;
	UINT16 CrtTime;
	UINT16 CrtDate;
	UINT16 LstAccDate;
	UINT16 FstClusHI;
	UINT16 WrtTime;
	UINT16 WrtDate;
	UINT16 FstClusLO;
	UINT16 FileSize;
} FATFS_FSOBJ;
EFI_STATUS fat_readdisk(UINT32 offset, UINT32 len, void *data);
EFI_STATUS fat_writedisk(UINT32 offset, UINT32 len, void *data);
UINT32 fat_getbpb_offset();
EFI_STATUS fat_init();
VOID debug_hex(UINT32 offset, CHAR8 *data, UINT16 size);
EFI_STATUS flash_fwupdate(VOID *data, UINTN size);
#endif /* _FATFS_H_ */
