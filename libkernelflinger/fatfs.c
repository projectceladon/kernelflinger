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
#include "fatfs.h"
#include "gpt.h"
#include "ff.h"

static FATSYSTEM g_fatsystem;

VOID debug_ascii(CHAR8 * ch, UINT16 size) {
	UINT16 i;
	CHAR8 *p;
	p=ch;
	for(i = 0; i < size;i++) {
		debug(L"%c", p[i]);
	}
}
VOID debug_hex(UINT32 offset, CHAR8 *data, UINT16 size){
	UINT16 i;
	UINT32 off;
	CHAR8 *d;
	for(i = 0; i < size/8;i++) {
		off=offset+i*8;
		d = &data[i*8];
		debug(L"%x   %x,%x,%x,%x,%x,%x,%x,%x",off, d[0],
				d[1],  d[2], d[3], d[4],
				d[5], d[6], d[7]);
	}
}
UINT32 fat_getbpb_offset(){
	return g_fatsystem.bpb_offset;
}

EFI_STATUS fat_readdisk(UINT32 offset, UINT32 len, void *data) {
	FATSYSTEM *fs = &g_fatsystem;
	EFI_STATUS ret = EFI_SUCCESS;
	if (fs == NULL || fs->parti.dio == NULL || fs->parti.bio == NULL)
	{
		debug(L"fat_readdisk init fail");
		return EFI_INVALID_PARAMETER;
	}
	//ret = fs->dio->ReadDisk(fs->dio, fs->bio->Media->MediaId, offset, len, data);
	ret = uefi_call_wrapper(fs->parti.dio->ReadDisk, 5, fs->parti.dio,
			fs->parti.bio->Media->MediaId,
			offset, len, (VOID *)data);

	if (EFI_ERROR(ret))
		efi_perror(ret, L"fat read failed");
	return ret;
}

EFI_STATUS fat_writedisk( UINT32 offset, UINT32 len, void *data)
{
	FATSYSTEM *fs = &g_fatsystem;
	EFI_STATUS ret = EFI_SUCCESS;
	if (fs == NULL || fs->parti.bio == NULL)
	{
		debug(L"fat_writedisk init fail");
		return EFI_INVALID_PARAMETER;
	}
	ret = uefi_call_wrapper(fs->parti.dio->WriteDisk, 5, fs->parti.dio, fs->parti.bio->Media->MediaId,offset,len,data);
	if (EFI_ERROR(ret))
		efi_perror(ret, L"fat write failed");
	return ret;
}

static TCHAR * fwuImage = L"/FwuImage.bin";
EFI_STATUS flash_fwupdate(VOID *data, UINTN size)
{
	FATSYSTEM  *fs = &g_fatsystem;
	EFI_STATUS ret = EFI_SUCCESS;
	FRESULT f_ret;
	FIL fp;
	UINT bsize; //getback size
    //find efi system partition to put fw update image
    ret = gpt_get_efi_partition(&fs->parti);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get efi system partition");
		return ret;
	}
	fs->bpb_offset = fs->parti.part.starting_lba*512;
	ret = fat_readdisk(fs->bpb_offset,512,fs->fatfs.win);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get FAT BPB");
		return ret;
	}
	fs->fatfs.pdrv = 4;
	if(FR_OK != f_mount(&fs->fatfs,L"/",1)) {
		debug(L"the file is not mount success");
		return EFI_NOT_FOUND;
	}
	debug(L"f_mount success");
	f_ret = f_open(&fp, fwuImage, FA_READ|FA_WRITE);
	if( f_ret == FR_NO_FILE ) {
		debug(L"%s file is not existing", fwuImage);
		f_ret = f_open(&fp, fwuImage,FA_READ|FA_WRITE|FA_CREATE_NEW);
		if (f_ret != 0) {
			debug(L"f_create err:%d", f_ret);
			return ret;
		}
	} else if( f_ret == 0 ) {
		debug(L"open %s success", fwuImage);
	} else {
		debug(L"f_open err:%d", f_ret);
		return ret;
	}
	ret = f_write(&fp,data,size,&bsize);
	if(ret != 0) {
		debug(L"f_write error:%d", ret);
		return ret;
	}
	debug(L"f_write OK:%d", ret);
	if(size != bsize) {
		debug(L"write %x is not equal %x",size,bsize);
		return EFI_VOLUME_CORRUPTED;
	}
	debug(L"good size is same");
	ret = f_close(&fp);

	if(ret != 0) {
		debug(L"f_close error:%d", ret);
		return ret;
	}
	return ret;
}
EFI_STATUS fat_test()
{
	FATSYSTEM  *fs = &g_fatsystem;
	EFI_STATUS ret = EFI_SUCCESS;
	EFI_GUID guid;
	FRESULT f_ret;
	FIL fp;
	CHAR8 ch[33];
	UINT32 readb;
	ret = gpt_get_partition_by_label(PRIMARY_LABEL, &fs->parti, LOGICAL_UNIT_USER);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get disk information");
		return ret;
	}
	guid = fs->parti.part.type;
	debug(L"the guid(%x-%x-%x) is matched",guid.Data1,guid.Data2,guid.Data3 );
	if (0 == CompareGuid(&fs->parti.part.type, &EfiPartTypeSystemPartitionGuid)) {
		debug(L"the guid is matched");
	}else {
		error(L"can not find Efi system partition");
	}
	fs->bpb_offset = fs->parti.part.starting_lba*512;
	debug(L"starting_lba is %x",fs->parti.part.starting_lba);
	debug(L"bpb_offset is %x",fs->bpb_offset);
	ret = fat_readdisk(fs->bpb_offset,512,fs->fatfs.win);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get FAT BPB");
		return ret;
	}
	debug_hex(0,fs->fatfs.win,512);
	fs->fatfs.pdrv = 4;
	if(FR_OK != f_mount(&fs->fatfs,L"/",1)) {
		debug(L"the file is not mount success");
		return EFI_NOT_FOUND;
	}

	debug(L"f_mount success");
	f_ret = f_open(&fp, L"/fat16.txt",FA_READ);
	if (!f_ret) {
		debug(L"open fat16.txt success");
	} else {
		debug(L"open fat16.txt result %d,", f_ret);
	}
	f_ret = f_read(&fp,ch,32,&readb);
	ch[32] = 0;
	if (!f_ret) {
		debug(L"read fat16.txt len %d",readb);
		debug_ascii(ch,32);
	}else {
		debug(L"read fat16.txt error %d", f_ret);
	}
	f_close(&fp);
	f_ret = f_open(&fp, L"/austin.txt",FA_READ|FA_WRITE);
	if( f_ret == FR_NO_FILE ) {
		debug(L"/austin.txt file is not existing");
		f_ret = f_open(&fp, L"/austin.txt",FA_READ|FA_WRITE|FA_CREATE_NEW);
		if (f_ret != 0) {
			debug(L"f_create err:%d", f_ret);
			return ret;
		}
	} else if( f_ret == 0 ) {
		debug(L"open /austin.txt success");
	} else {
		debug(L"f_open err:%d", f_ret);
		return ret;
	}
	INT16 i;
	for(i = 0;i<26;i++) {
		ch[i] = 'a'+i;
	}
	for (i = 0; i < 3;i++) {
		ret = f_write(&fp,ch+i*8,8,&readb);
		if(ret != 0) {
			debug(L"f_write error:%d", ret);
			break;
		}
	}
	f_ret = f_read(&fp,ch,24,&readb);
	if (!f_ret) {
		debug(L"read austin.txt len %d",readb);
		debug_ascii(ch,24);
	}else {
		debug(L"read austin.txt error %d", f_ret);
	}
	f_close(&fp);
	return ret;
}



UINT32 get_fattime() {
	EFI_STATUS ret;
	EFI_TIME now;

	ret = uefi_call_wrapper(RT->GetTime, 2, &now, NULL);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get the current time");
		return 42<<25|1<<21|1<<16;
	}
	return ((UINT32)now.Year-1980)<<25 | (UINT32)(now.Month)<<21 \
		| (UINT32)(now.Day)<<16 |(UINT32)(now.Hour)<<11 \
		| (UINT32)(now.Minute) << 5 | now.Second;
}
