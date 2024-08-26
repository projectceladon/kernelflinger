/*
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Authors: Jeremy Compostella <jeremy.compostella@intel.com>
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

#include <lib.h>
#include <transport.h>

static transport_t *transports;
static UINTN nb_transport;
static transport_t *current;

EFI_STATUS transport_register(transport_t *trans, UINTN nb)
{
	if (!trans || !nb)
		return EFI_INVALID_PARAMETER;

	transports = trans;
	nb_transport = nb;

	return EFI_SUCCESS;
}

void transport_unregister(void)
{
	transports = NULL;
	nb_transport = 0;
}

EFI_STATUS transport_start(start_callback_t start_cb,
			   data_callback_t rx_cb,
			   data_callback_t tx_cb)
{
	EFI_STATUS ret = EFI_NOT_READY;
	UINTN i;

	if (!start_cb || !rx_cb || !tx_cb)
		return EFI_INVALID_PARAMETER;

	for (i = 0; i < nb_transport; i++) {
		current = &transports[i];
		ret = current->start(start_cb, rx_cb, tx_cb);
		if (!EFI_ERROR(ret))
			break;
		current = NULL;

		if (ret == EFI_UNSUPPORTED) {
			debug(L"%a transport layer is not supported, skipping",
			      transports[i].name);
			continue;
		}
		efi_perror(ret, L"Failed to initialize %a transport layer",
			   transports[i].name);
		break;
	}

	if (current)
		debug(L"%a transport layer selected", current->name);

	return ret;
}

EFI_STATUS transport_stop(void)
{
	EFI_STATUS ret;

	ret = current ? current->stop() : EFI_NOT_STARTED;
	current = NULL;

	return ret;
}

EFI_STATUS transport_run(UINT32 *state)
{
	return current ? current->run(state) : EFI_NOT_STARTED;
}

EFI_STATUS transport_read(void *buf, UINT32 size)
{
	return current ? current->read(buf, size) : EFI_NOT_STARTED;
}

EFI_STATUS transport_write(void *buf, UINT32 size)
{
	return current ? current->write(buf, size) : EFI_NOT_STARTED;
}
