/*
 *Copyright (C) 2024 Intel Corporation
 *SPDX-License-Identifier: BSD-3-Clause
 */

#include <efi.h>
#include <efilib.h>
#include <ui.h>
#include "lib.h"
#include "timer.h"

#define SPI_CMD_WRSR           0x01
#define SPI_CMD_WRDI           0x04
#define SPI_CMD_RDSR           0x05
#define SPI_CMD_WREN           0x06
#define SPI_CMD_Read           0x0B
#define SPI_CMD_EWSR           0x50
#define ERASE_1K   	       0xD7
#define SPI_CMD_VERSION        0x80

#define EC_READY               0x01
#define EC_FREE                0x02
#define RETRY_INTERVAL_US      100000
#define WAIT_INTERVAL_US       3000000
#define Send_Cmd               0x02
#define Send_Byte              0x03
#define Read_Byte              0x04
#define SIZE_128K              (128*1024)

static UINT8 EC_STATUS_PORT66    = 0x66;
static UINT8 EC_CMD_PORT66       = 0x66;
static UINT8 EC_DATA_PORT62      = 0x62;

static inline UINT8 inb(int port)
{
	UINT8 val;
	__asm__ __volatile__("inb %w1, %b0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline void outb(UINT8 val, int port)
{
	__asm__ __volatile__("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

static void ec_wait_for_ready(void)
{
	int retries = 100;

	register unsigned char status = inb(EC_STATUS_PORT66);

	while (!(status & EC_READY) && retries > 0) {
		pause_us(5000);
		retries--;
		status = inb(EC_STATUS_PORT66);
	}
}

static void ec_wait_for_free (void)
{
	int retries = 100;

	register unsigned char status = inb(EC_STATUS_PORT66);

	while ((status & EC_FREE) && retries > 0) {
		pause_us(5000);
		retries--;
		status = inb(EC_STATUS_PORT66);
	}
}

static void send_cmd_to_ec(UINT8 Cmd)
{
	ec_wait_for_free();
	outb(Cmd, EC_CMD_PORT66);
	ec_wait_for_free();
}

static UINT8 read_data_from_ec(void)
{
	register unsigned char data;
	ec_wait_for_ready();
	data = inb(EC_DATA_PORT62);
	return(data);
}

static void follow_mode(UINT8 mode)
{
	send_cmd_to_ec(mode);
}

static void send_cmd_to_flash(UINT8 cmd)
{
	send_cmd_to_ec(Send_Cmd);
	send_cmd_to_ec(cmd);
}

static void send_byte_to_flash(UINT8 data)
{
	send_cmd_to_ec(Send_Byte);
	send_cmd_to_ec(data);
}

static UINT8 read_byte_from_flash(void)
{
	send_cmd_to_ec(Read_Byte);
	return(read_data_from_ec());
}

static void wait_flash_free(void)
{
	int retries = 10;
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_RDSR);
	while ((read_byte_from_flash() & 0x01) && retries > 0) {
		retries--;
		pause_us(RETRY_INTERVAL_US);
	}

	follow_mode(0x05);
}

static void flash_write_enable(void)
{
	int retries = 10;
	wait_flash_free();
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_WRSR);
	send_byte_to_flash(0x00);

	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_WREN);

	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_RDSR);
	while (!(read_byte_from_flash() & 0x02) && retries > 0) {
		retries--;
		pause_us(RETRY_INTERVAL_US);
	}
	follow_mode(0x05);
}

static void flash_write_disable(void)
{
	int retries = 10;
	wait_flash_free();
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_WRDI);

	wait_flash_free();
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_RDSR);
	while ((read_byte_from_flash() & 0x02) && retries > 0) {
		retries--;
		pause_us(RETRY_INTERVAL_US);
	}

	follow_mode(0x05);
}

static void ec_status_write_enable(void)
{
	wait_flash_free();
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_WREN);

	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_EWSR);

}

static void ec_erase(void)
{
	UINT16 i,j;

	debug(L"Eraseing...\n");

	for(i=0; i<0x02; i++) {
		for(j=0; j<0x100; j+=0x04) {
			ec_status_write_enable();
			flash_write_enable();

			wait_flash_free();
			follow_mode(0x01);
			send_cmd_to_flash(ERASE_1K);
			send_byte_to_flash((UINT8)i);
			send_byte_to_flash((UINT8)j);
			send_byte_to_flash(0);
			wait_flash_free();
		}
	}

	debug(L"Erase Done. \n");
}

static void ec_flash(UINT8 *data)
{
	UINT32 i;

	debug(L"flashing...\n");

	ec_status_write_enable();
	flash_write_enable();
	follow_mode(0x01);
	send_cmd_to_flash(0xAD);
	for (i=0; i<3; i++)
		send_byte_to_flash(0x00);

	send_byte_to_flash(data[0]);
	send_byte_to_flash(data[1]);
	wait_flash_free();

	for(i=2; i<SIZE_128K; i+=2)
	{
		follow_mode(0x01);
		send_cmd_to_flash(0xAD);
		send_byte_to_flash(data[i]);
		send_byte_to_flash(data[i+1]);
		wait_flash_free();
	}

	flash_write_disable();

	debug(L"flash done. \n");
}

static int ec_verify(UINT8 *data)
{
	UINT32 i;

	debug(L"Verifying...\n");

	flash_write_disable();
	wait_flash_free();
	follow_mode(0x01);
	send_cmd_to_flash(SPI_CMD_Read);
	for (i=0; i<4; i++)
		send_byte_to_flash(0x00);

	for (i=0; i<SIZE_128K; i++) {
		if (read_byte_from_flash() != data[i]) {
			wait_flash_free();
			error(L"EC verify failed. \n");
			return -1;
		}
	}

	wait_flash_free();

	debug(L"EC verify successfully\n");

	return 0;
}

UINT8 get_ec_sub_ver(UINT8 Port)
{
	ec_wait_for_free();
	outb(SPI_CMD_VERSION, EC_CMD_PORT66);
	ec_wait_for_free();
	outb(Port, EC_DATA_PORT62);
	ec_wait_for_free();

	return read_data_from_ec();
}

void output_ec_version(void)
{
	info(L"Main version: %x", get_ec_sub_ver(0x0));
	info(L"Sub  version: %x", get_ec_sub_ver(0x1));
	info(L"Test version: %x", get_ec_sub_ver(0x2));
}

EFI_STATUS update_ec(void *data, uint32_t len)
{
	int retries = 5;

	debug(L"Update EC start\n");

	debug(L"Get current EC version");
	output_ec_version();

	if (!data) {
		error(L"EC data is NULL");
		return EFI_INVALID_PARAMETER;
	}

	if (len != SIZE_128K) {
		error(L"We only support EC data length with 128K bytes");
		return EFI_INVALID_PARAMETER;
	}

	send_cmd_to_ec(0xDC);

	while ((read_data_from_ec() != 0x33) && retries > 0) {
		retries--;
		pause_us(RETRY_INTERVAL_US);
	}
	if (retries == 0) {
		error(L"EC: enter flash mode failed\n");
		pause_us(1000000);
		goto end;
	}

	ec_erase();
	ec_flash(data);

	if (ec_verify(data) < 0) {
		error(L"Verify program fail\n");
		goto end;
	}

end:
	pause_us(30000);
	send_cmd_to_ec(0xFC);

	debug(L"EC version after flashing\n");
	output_ec_version();
	debug(L"Update EC Done\n");

	return EFI_SUCCESS;
}
