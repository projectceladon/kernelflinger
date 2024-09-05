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

#ifndef _CRASHDUMP_H_
#define _CRASHDUMP_H_

#define DUMP_MAGIC          "_CRASHDUMP!"
#define DUMP_MAGIC_SIZE     16
#define DUMP_HEAD_VERSION   1
#define DUMP_HEAD_SIZE      4096
#define RESERVED_MEM_SIZE   1048576
#define DUMP_PART_UUID      "cab9b00c-cc1b-4c0f-b932-82920da52251"

#define MAX_RAM_REGION_NUM 64

#define GUEST_NAME_SIZE 64
#define OS_VERSION_SIZE 1024
#define VMCORE_SIZE 4096

#define DUMP_EMPTY  0
#define DUMP_SBL    1
#define DUMP_GUES   2

#define DUMP_EMPTY  0
#define DUMP_FULL   1
#define DUMP_MINI   2

#define SHM_TYPE_EMPTY          0
#define SHM_TYPE_HYPERVISOR     1
#define SHM_TYPE_TEE            2
#define SHM_TYPE_SOS            3
#define SHM_TYPE_VM1            4
#define SHM_TYPE_VM2            5
#define SHM_TYPE_VM3            6

struct ram_region {
    uint64_t    start;          /* memory region start address */
    uint64_t    map_sz;         /* memory region size*/
    uint8_t     rsvd_flag;      /* flag for reserved memory */
    uint8_t     reserved[7];    /* reserved 7 bytes to align to uint64_t */
};

typedef union dump_hdr
 {
    struct {
        char            magic[DUMP_MAGIC_SIZE];
        uint16_t        dump_hdr_ver;       /* version for the dump header */
        uint8_t         owner;              /* mark the dump owern: sbl_cd | acrn_dm */
        uint8_t         region_num;         /* total dump regions */
        uint8_t         reserved[4];        /* reserved 4 bytes to align uint64_t */
        struct ram_region   dump_ram_region[MAX_RAM_REGION_NUM];
    };
    uint8_t             raw_data[DUMP_HEAD_SIZE];
} __attribute__((packed)) dump_hdr_t;

struct shm_hdr
{
    uint16_t        shm_hdr_version;    /* version for the dump header */
    uint8_t         dump_ctl;           /* Full or Mini dump */
    uint8_t         type;               /* mark the dump type */
} __attribute__((packed)) ;

struct shm_vm
{
    struct  shm_hdr shm_header;
    char    guest_name[GUEST_NAME_SIZE];      /* guest name: sos, VM1, VM2, VM3 and etc.*/
    char    guest_version[OS_VERSION_SIZE];   /* the guest version, kernel version is similoar /proc/version */
    uint8_t boot_reason;                      /* used in guest reboot to mark it's normal boot or reset due to panic*/
    char    vmcoreinfo[VMCORE_SIZE];          /* used in guest to save the vmcore kaslr infomation */
}  __attribute__((packed)) ;
#endif
