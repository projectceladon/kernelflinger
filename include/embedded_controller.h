/*
 *Copyright (C) 2024 Intel Corporation
 *SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _EMBEDDED_CONTROLLLER_
#define _EMBEDDED_CONTROLLLER_

UINT8 get_ec_sub_ver(UINT8 Port);
void output_ec_version(void);
EFI_STATUS update_ec(void *data, uint32_t len);

#endif

