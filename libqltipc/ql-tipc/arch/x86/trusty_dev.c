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

#include "sm_err.h"
#include "smcall.h"
#include "hypercall.h"

struct trusty_dev;

#define LOCAL_LOG 0

#define UNUSED(x) (void)(x)

static int32_t trusty_fast_call32(struct trusty_dev *dev, uint32_t smcnr,
                                  uint32_t a0, uint32_t a1, uint32_t a2)
{
    UNUSED(dev);
    trusty_assert(dev);
    trusty_assert(SMC_IS_FASTCALL(smcnr));

    return smc(smcnr, a0, a1, a2);
}

static unsigned long trusty_std_call_inner(struct trusty_dev *dev,
                                           unsigned long smcnr,
                                           unsigned long a0,
                                           unsigned long a1,
                                           unsigned long a2)
{
    unsigned long ret;
    int retry = 5;

    UNUSED(dev);

    trusty_debug("%a(0x%lx 0x%lx 0x%lx 0x%lx)\n", __func__, smcnr, a0, a1, a2);

    while (true) {
        ret = smc(smcnr, a0, a1, a2);
        while ((int32_t)ret == SM_ERR_FIQ_INTERRUPTED)
            ret = smc(SMC_SC_RESTART_FIQ, 0, 0, 0);
        if ((int)ret != SM_ERR_BUSY || !retry)
            break;

        trusty_debug("%a(0x%lx 0x%lx 0x%lx 0x%lx) returned busy, retry\n",
                     __func__, smcnr, a0, a1, a2);

        retry--;
    }

    return ret;
}

static unsigned long trusty_std_call_helper(struct trusty_dev *dev,
                                            unsigned long smcnr,
                                            unsigned long a0,
                                            unsigned long a1,
                                            unsigned long a2)
{
    unsigned long ret;
    unsigned long irq_state;

    while (true) {
        trusty_local_irq_disable(&irq_state);
        ret = trusty_std_call_inner(dev, smcnr, a0, a1, a2);
        trusty_local_irq_restore(&irq_state);

        if ((int)ret != SM_ERR_BUSY)
            break;

        trusty_idle(dev);
    }

    return ret;
}

static int32_t trusty_std_call32(struct trusty_dev *dev, uint32_t smcnr,
                                 uint32_t a0, uint32_t a1, uint32_t a2)
{
    int ret;

    trusty_assert(dev);
    trusty_assert(!SMC_IS_FASTCALL(smcnr));

    if (smcnr != SMC_SC_NOP) {
        trusty_lock(dev);
    }

    trusty_debug("%a(0x%x 0x%x 0x%x 0x%x) started\n", __func__,
                 smcnr, a0, a1, a2);

    ret = trusty_std_call_helper(dev, smcnr, a0, a1, a2);
    while (ret == SM_ERR_INTERRUPTED || ret == SM_ERR_CPU_IDLE) {
        trusty_debug("%a(0x%x 0x%x 0x%x 0x%x) interrupted\n", __func__,
                     smcnr, a0, a1, a2);
        if (ret == SM_ERR_CPU_IDLE) {
            trusty_idle(dev);
        }
        ret = trusty_std_call_helper(dev, SMC_SC_RESTART_LAST, 0, 0, 0);
    }

    trusty_debug("%a(0x%x 0x%x 0x%x 0x%x) returned 0x%x\n",
                 __func__, smcnr, a0, a1, a2, ret);

    if (smcnr != SMC_SC_NOP) {
        trusty_unlock(dev);
    }

    return ret;
}

static int trusty_call32_mem_buf(struct trusty_dev *dev, uint32_t smcnr,
                                 struct ns_mem_page_info *page, uint32_t size)
{
    trusty_assert(dev);
    trusty_assert(page);

    if (SMC_IS_FASTCALL(smcnr)) {
        return trusty_fast_call32(dev, smcnr,
                                  (uint32_t)page->attr,
                                  (uint32_t)(page->attr >> 32), size);
    } else {
        return trusty_std_call32(dev, smcnr,
                                 (uint32_t)page->attr,
                                 (uint32_t)(page->attr >> 32), size);
    }
}

int trusty_dev_init_ipc(struct trusty_dev *dev,
                        struct ns_mem_page_info *buf, uint32_t buf_size)
{
    return trusty_call32_mem_buf(dev, SMC_SC_TRUSTY_IPC_CREATE_QL_DEV,
                                 buf, buf_size);
}

int trusty_dev_exec_ipc(struct trusty_dev *dev,
                        struct ns_mem_page_info *buf, uint32_t buf_size)
{
    return trusty_call32_mem_buf(dev, SMC_SC_TRUSTY_IPC_HANDLE_QL_DEV_CMD,
                                 buf, buf_size);
}

int trusty_dev_shutdown_ipc(struct trusty_dev *dev,
                            struct ns_mem_page_info *buf, uint32_t buf_size)
{
    return trusty_call32_mem_buf(dev, SMC_SC_TRUSTY_IPC_SHUTDOWN_QL_DEV,
                                 buf, buf_size);
}


static int trusty_init_api_version(struct trusty_dev *dev)
{
    uint32_t api_version;

    api_version = trusty_fast_call32(dev, SMC_FC_API_VERSION,
                                     TRUSTY_API_VERSION_CURRENT, 0, 0);
    if (api_version == SM_ERR_UNDEFINED_SMC)
        api_version = 0;

    if (api_version > TRUSTY_API_VERSION_CURRENT) {
        trusty_error("unsupported trusty api version %d > %d\n",
                     api_version, TRUSTY_API_VERSION_CURRENT);
        return -1;
    }

    trusty_info("selected trusty api version: %d (requested %d)\n",
                api_version, TRUSTY_API_VERSION_CURRENT);

    dev->api_version = api_version;

    return 0;
}

int trusty_dev_init(struct trusty_dev *dev, void *priv_data)
{
    trusty_assert(dev);

    dev->priv_data = priv_data;
    return trusty_init_api_version(dev);
}

int trusty_dev_shutdown(struct trusty_dev *dev)
{
    trusty_assert(dev);

    dev->priv_data = NULL;
    return 0;
}

