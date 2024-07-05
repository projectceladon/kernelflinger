#include <efi.h>
#include <efilib.h>
#include <lib.h>
#include "efilinux.h"

/**
 * memory_map - Allocate and fill out an array of memory descriptors
 * @map_buf: buffer containing the memory map
 * @map_size: size of the buffer containing the memory map
 * @map_key: key for the current memory map
 * @desc_size: size of the desc
 * @desc_version: memory descriptor version
 *
 * On success, @map_size contains the size of the memory map pointed
 * to by @map_buf and @map_key, @desc_size and @desc_version are
 * updated.
 */
EFI_STATUS
memory_map(EFI_MEMORY_DESCRIPTOR **map_buf, UINTN *map_size,
           UINTN *map_key, UINTN *desc_size, UINT32 *desc_version)
{
        EFI_STATUS err;

        *map_size = sizeof(**map_buf) * 31;
get_map:

        /*
         * Because we're about to allocate memory, we may
         * potentially create a new memory descriptor, thereby
         * increasing the size of the memory map. So increase
         * the buffer size by the size of one memory
         * descriptor, just in case.
         */
        *map_size += sizeof(**map_buf);

        err = allocate_pool(EfiLoaderData, *map_size,
                            (void **)map_buf);
        if (err != EFI_SUCCESS) {
                error(L"Failed to allocate pool for memory map");
                goto failed;
        }

        err = get_memory_map(map_size, *map_buf, map_key,
                             desc_size, desc_version);
        if (err != EFI_SUCCESS) {
                if (err == EFI_BUFFER_TOO_SMALL) {
                        /*
                         * 'map_size' has been updated to reflect the
                         * required size of a map buffer.
                         */
                        free_pool((void *)*map_buf);
                        goto get_map;
                }

                error(L"Failed to get memory map");
                goto failed;
        }

failed:
        return err;
}



/**
 * emalloc - Allocate memory with a strict alignment requirement
 * @size: size in bytes of the requested allocation
 * @align: the required alignment of the allocation
 * @addr: a pointer to the allocated address on success
 * @low: pick up an address in low memory region
 *
 * If we cannot satisfy @align we return 0.
 */
EFI_STATUS emalloc(UINTN size, UINTN align, EFI_PHYSICAL_ADDRESS *addr, BOOLEAN low)
{
        UINTN map_size, map_key, desc_size;
        EFI_MEMORY_DESCRIPTOR *map_buf;
        UINTN d, map_end;
        UINT32 desc_version;
        EFI_STATUS err;
        UINTN nr_pages = EFI_SIZE_TO_PAGES(size);

        err = memory_map(&map_buf, &map_size, &map_key,
                         &desc_size, &desc_version);
        if (err != EFI_SUCCESS)
                goto fail;

        d = (UINTN)map_buf;
        map_end = (UINTN)map_buf + map_size;

        for (; d < map_end; d += desc_size) {
                EFI_MEMORY_DESCRIPTOR *desc;
                EFI_PHYSICAL_ADDRESS start, end, aligned;

                desc = (EFI_MEMORY_DESCRIPTOR *)d;
                if (desc->Type != EfiConventionalMemory)
                        continue;

                if (desc->NumberOfPages < nr_pages)
                        continue;

                start = desc->PhysicalStart;
                end = start + (desc->NumberOfPages << EFI_PAGE_SHIFT);

                /* Low-memory is super-precious! */
                if (!low) {
                        if (end <= 1 << 20)
                                continue;
                        if (start < 1 << 20) {
                                size -= (1 << 20) - start;
                                start = (1 << 20);
                        }
                }
                if (start == 0)
                        start += 8;

                aligned = (start + align -1) & ~(align -1);

                if ((aligned + size) <= end) {
                        err = allocate_pages(AllocateAddress, EfiLoaderData,
                                             nr_pages, &aligned);
                        if (err == EFI_SUCCESS) {
                                *addr = aligned;
                                break;
                        }
                }
        }

        if (d == map_end)
                err = EFI_OUT_OF_RESOURCES;

fail:
        if (map_buf) {
                free_pool(map_buf);
        }
        return err;
}


/**
 * efree - Return memory allocated with emalloc
 * @memory: the address of the emalloc() allocation
 * @size: the size of the allocation
 */
void efree(EFI_PHYSICAL_ADDRESS memory, UINTN size)
{
        UINTN nr_pages = EFI_SIZE_TO_PAGES(size);

        free_pages(memory, nr_pages);
}


