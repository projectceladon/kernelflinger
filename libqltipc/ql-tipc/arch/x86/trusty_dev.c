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

#include <trusty/smc.h>

#define SMC_VMCALL_ID 0x74727500

struct smc_ret8 smc8(unsigned long r0, unsigned long r1,
                     unsigned long r2, unsigned long r3,
                     unsigned long r4, unsigned long r5,
                     unsigned long r6, unsigned long r7) {
    struct smc_ret8 ret;
    register unsigned long smc_id __asm__("rax") = SMC_VMCALL_ID;
    register unsigned long arg0 __asm__("rdi") = r0;
    register unsigned long arg1 __asm__("rsi") = r1;
    register unsigned long arg2 __asm__("rdx") = r2;
    register unsigned long arg3 __asm__("rcx") = r3;
    register unsigned long arg4 __asm__("r8")  = r4;
    register unsigned long arg5 __asm__("r9")  = r5;
    register unsigned long arg6 __asm__("r10") = r6;
    register unsigned long arg7 __asm__("r11") = r7;

    __asm__ __volatile__(
            "vmcall\n"
            : "=r" (arg0), "=r" (arg1), "=r" (arg2), "=r" (arg3),
            "=r" (arg4), "=r" (arg5), "=r" (arg6), "=r" (arg7)
            : "r" (smc_id),  "r" (arg0), "r" (arg1), "r" (arg2), "r" (arg3),
            "r" (arg4), "r" (arg5), "r" (arg6), "r" (arg7)
            : "memory");

    ret.r0 = arg0;
    ret.r1 = arg1;
    ret.r2 = arg2;
    ret.r3 = arg3;
    ret.r4 = arg4;
    ret.r5 = arg5;
    ret.r6 = arg6;
    ret.r7 = arg7;

    return ret;
}
