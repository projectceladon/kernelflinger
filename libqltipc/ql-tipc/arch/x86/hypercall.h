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

#ifdef HYPERVISOR_ACRN

#define ACRN_SMC_HC_ID 0x80000071
inline unsigned long smc(unsigned long r0,
                         unsigned long r1,
                         unsigned long r2,
                         unsigned long r3)
{
    register unsigned long smc_id asm("r8") = ACRN_SMC_HC_ID;
    asm volatile(
    "pushq %%rbx;" /* save the rbx */
    "movq %8, %%rbx;"
    "vmcall; \n"
    "movq %%rbx, %3;"
    "popq %%rbx;" /* restore the old rbx */
    : "=D"(r0), "=S"(r1), "=d"(r2), "=r"(r3)
    : "r"(smc_id), "D"(r0), "S"(r1), "d"(r2), "r"(r3)
    : "rax"
    );
    return r0;
}

#else

#define EVMM_SMC_HC_ID 0x74727500
inline unsigned long smc(unsigned long r0,
                         unsigned long r1,
                         unsigned long r2,
                         unsigned long r3)
{
    asm volatile(
#if ARCH_X86_32
    "pushl %%ebx;" /* save the ebx */
    "movl %8, %%ebx;"
    "vmcall; \n"
    "movl %%ebx, %3;"
    "popl %%ebx;" /* restore the old ebx */
#elif ARCH_X86_64
    "pushq %%rbx;" /* save the rbx */
    "movq %8, %%rbx;"
    "vmcall; \n"
    "movq %%rbx, %3;"
    "popq %%rbx;" /* restore the old rbx */
#endif

    : "=D"(r0), "=S"(r1), "=d"(r2), "=r"(r3)
    : "a"(EVMM_SMC_HC_ID), "D"(r0), "S"(r1), "d"(r2), "r"(r3)
    );

    return r0;
}

#endif
