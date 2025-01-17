/*
 * Copyright (c) 2011, Intel Corporation
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
 * This file contains some wrappers around the gnu-efi functions. As
 * we're not going through uefi_call_wrapper() directly, this allows
 * us to get some type-safety for function call arguments and for the
 * compiler to check that the number of function call arguments is
 * correct.
 *
 * It's also a good place to document the EFI interface.
 */

#ifndef __EFILINUX_H__
#define __EFILINUX_H__

#define EFILINUX_VERSION_MAJOR 1
#define EFILINUX_VERSION_MINOR 0

#define EFILINUX_CONFIG        L"efilinux.cfg"
#define SETUP_HDR              0x53726448  /* 0x53726448 == "HdrS" */
#define XLF_EFI_HANDOVER_32    (1<<2)
#define XLF_EFI_HANDOVER_64    (1<<3)

/**
 * allocate_pages - Allocate memory pages from the system
 * @atype: type of allocation to perform
 * @mtype: type of memory to allocate
 * @num_pages: number of contiguous 4KB pages to allocate
 * @memory: used to return the address of allocated pages
 *
 * Allocate @num_pages physically contiguous pages from the system
 * memory and return a pointer to the base of the allocation in
 * @memory if the allocation succeeds. On success, the firmware memory
 * map is updated accordingly.
 *
 * If @atype is AllocateAddress then, on input, @memory specifies the
 * address at which to attempt to allocate the memory pages.
 */
static inline EFI_STATUS
allocate_pages(EFI_ALLOCATE_TYPE atype, EFI_MEMORY_TYPE mtype,
               UINTN num_pages, EFI_PHYSICAL_ADDRESS *memory)
{
        return uefi_call_wrapper(BS->AllocatePages, 4, atype,
                                 mtype, num_pages, memory);
}

/**
 * free_pages - Return memory allocated by allocate_pages() to the firmware
 * @memory: physical base address of the page range to be freed
 * @num_pages: number of contiguous 4KB pages to free
 *
 * On success, the firmware memory map is updated accordingly.
 */
static inline EFI_STATUS
free_pages(EFI_PHYSICAL_ADDRESS memory, UINTN num_pages)
{
        return uefi_call_wrapper(BS->FreePages, 2, memory, num_pages);
}

/**
 * allocate_pool - Allocate pool memory
 * @type: the type of pool to allocate
 * @size: number of bytes to allocate from pool of @type
 * @buffer: used to return the address of allocated memory
 *
 * Allocate memory from pool of @type. If the pool needs more memory
 * pages are allocated from EfiConventionalMemory in order to grow the
 * pool.
 *
 * All allocations are eight-byte aligned.
 */
static inline EFI_STATUS
allocate_pool(EFI_MEMORY_TYPE type, UINTN size, void **buffer)
{
        return uefi_call_wrapper(BS->AllocatePool, 3, type, size, buffer);
}

/**
 * free_pool - Return pool memory to the system
 * @buffer: the buffer to free
 *
 * Return @buffer to the system. The returned memory is marked as
 * EfiConventionalMemory.
 */
static inline EFI_STATUS free_pool(void *buffer)
{
        return uefi_call_wrapper(BS->FreePool, 1, buffer);
}

/**
 * get_memory_map - Return the current memory map
 * @size: the size in bytes of @map
 * @map: buffer to hold the current memory map
 * @key: used to return the key for the current memory map
 * @descr_size: used to return the size in bytes of EFI_MEMORY_DESCRIPTOR
 * @descr_version: used to return the version of EFI_MEMORY_DESCRIPTOR
 *
 * Get a copy of the current memory map. The memory map is an array of
 * EFI_MEMORY_DESCRIPTORs. An EFI_MEMORY_DESCRIPTOR describes a
 * contiguous block of memory.
 *
 * On success, @key is updated to contain an identifer for the current
 * memory map. The firmware's key is changed every time something in
 * the memory map changes. @size is updated to indicate the size of
 * the memory map pointed to by @map.
 *
 * @descr_size and @descr_version are used to ensure backwards
 * compatibility with future changes made to the EFI_MEMORY_DESCRIPTOR
 * structure. @descr_size MUST be used when the size of an
 * EFI_MEMORY_DESCRIPTOR is used in a calculation, e.g when iterating
 * over an array of EFI_MEMORY_DESCRIPTORs.
 *
 * On failure, and if the buffer pointed to by @map is too small to
 * hold the memory map, EFI_BUFFER_TOO_SMALL is returned and @size is
 * updated to reflect the size of a buffer required to hold the memory
 * map.
 */
static inline EFI_STATUS
get_memory_map(UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key,
               UINTN *descr_size, UINT32 *descr_version)
{
        return uefi_call_wrapper(BS->GetMemoryMap, 5, size, map,
                                 key, descr_size, descr_version);
}

#define PAGE_SIZE        4096

static const CHAR16 *memory_types[] = {
        L"EfiReservedMemoryType",
        L"EfiLoaderCode",
        L"EfiLoaderData",
        L"EfiBootServicesCode",
        L"EfiBootServicesData",
        L"EfiRuntimeServicesCode",
        L"EfiRuntimeServicesData",
        L"EfiConventionalMemory",
        L"EfiUnusableMemory",
        L"EfiACPIReclaimMemory",
        L"EfiACPIMemoryNVS",
        L"EfiMemoryMappedIO",
        L"EfiMemoryMappedIOPortSpace",
        L"EfiPalCode",
        L"EfiVENDOR_RSVD",
};

static inline const CHAR16 *memory_type_to_str(UINT32 type)
{
        if (type > sizeof(memory_types)/sizeof(CHAR16 *))
                return L"Unknown";

        return memory_types[type];
}

EFI_STATUS emalloc(UINTN, UINTN, EFI_PHYSICAL_ADDRESS *, BOOLEAN);
void efree(EFI_PHYSICAL_ADDRESS memory, UINTN size);

EFI_STATUS memory_map(EFI_MEMORY_DESCRIPTOR **map_buf,
                             UINTN *map_size, UINTN *map_key,
                             UINTN *desc_size, UINT32 *desc_version);

#endif /* __EFILINUX_H__ */
