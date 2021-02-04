/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <trusty/trusty_dev.h>
#include <trusty/util.h>
#include <stdint.h>

#define NS_MAIR_NORMAL_UNCACHED            0x44 /* uncached */

int trusty_encode_page_info(struct ns_mem_page_info *inf, void *va)
{
    uint64_t par = (uint64_t)(uintptr_t)va;

    /* ToDo */
    /* _PAGE_USER */
    //par |= (1 << 6);
    /* _PAGE_RW */
    //par |= (1 << 7);

    inf->attr = (par & 0x0000FFFFFFFFFFFFull) | ((uint64_t)NS_MAIR_NORMAL_UNCACHED << 48);

    return 0;
}

