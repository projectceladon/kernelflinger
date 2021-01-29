/*
 * Copyright (c) 2019 LK Trusty Authors. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <trusty/trusty_dev.h>

/*
 * TIPC defines physical address bits 47:12, memory type and cache
 * attributes bits 55:48. This defition is differential from x86,
 * use this definition to align Trusty.
 */
#define NS_PHYS_MEM_WIDTH_SHIFT 48
#define NS_MEM_TYPE_UNCACHED 0x44ULL /* uncached */

#define FFA_MEM_ATTR_NORMAL_MEMORY_UNCACHED ((2U << 4) | (0x1U << 2))
#define FFA_MEM_PERM_RW (1U << 1)

/*
 * Bootloader, which integrates ql-tipc component, should implement
 * it's own qltipc_x86_get_mapping function call to overwrite weak
 * function.
 * If bootloader creates 1-on-1 mapping, which means physical address
 * is idenical to virtual address, bootloader could utilizes this
 * weak function direclty. Or physcial address shoule be retrived from
 * bootloader's mapping method.
 */
__attribute__((weak))
int qltipc_x86_get_mapping(uint64_t va, uint64_t* pa, uint64_t* flags) {
    *pa = va;

    *flags = NS_MEM_TYPE_UNCACHED << NS_PHYS_MEM_WIDTH_SHIFT;

    return 0;
}

int trusty_encode_page_info(struct ns_mem_page_info* inf, void* va) {
    int ret;
    uint64_t flags;
    uint64_t pa;

    ret = qltipc_x86_get_mapping((uint64_t)va, &pa, &flags);
    if (0 != ret) {
        inf->attr = 0;
        return ret;
    }

    inf->paddr = pa;
    inf->attr = pa | flags;
    inf->ffa_mem_attr = FFA_MEM_ATTR_NORMAL_MEMORY_UNCACHED;
    inf->ffa_mem_perm = FFA_MEM_PERM_RW;

    return 0;
}
