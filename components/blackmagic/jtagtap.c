/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements the low-level JTAG TAP interface.  */

#include <stdio.h>

#include "esp32_spi.h"

#include "general.h"
#include "jtagtap.h"
#include "gdb_packet.h"

#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_hal.h"
#include "hal/spi_ll.h"
#include "hal/spi_hal.h"
#include "driver/gpio.h"

#define TAG_LL "jtag-ll"
#define TAG "jtag"

// #define DEBUG_JTAG_TRANSACTIONS

enum jtag_drive_mode
{
	JTAG_DRIVE_TDI = 0,
	JTAG_DRIVE_TMS = 1,
	JTAG_DRIVE_GPIO = 2,
};
enum jtag_drive_mode jtag_drive_mode = JTAG_DRIVE_TDI;

// Symbol is globally visible
jtag_proc_t jtag_proc;

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t MS, int ticks);
void jtagtap_tms_seq_tdi(uint32_t MS, uint32_t tdi, int ticks);
void jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks);
static void jtagtap_tdi_seq(
	const uint8_t final_tms, const uint8_t *DI, int ticks);
static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI);

static void jtag_drive(enum jtag_drive_mode new_mode)
{
	if (jtag_drive_mode == new_mode)
	{
		return;
	}

	if (new_mode == JTAG_DRIVE_TDI)
	{
		// Wire up the TMS pin, which is GPIO controlled (mostly)
		// gpio_set_direction(CONFIG_TMS_SWDIO_GPIO, GPIO_MODE_INPUT_OUTPUT);
		// gpio_iomux_out(CONFIG_TMS_SWDIO_GPIO, PIN_FUNC_GPIO, false);
		esp32_spi_mux_pin(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);

		// TDI = MOSI
		esp32_spi_mux_pin(CONFIG_TDI_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spid_out, spi_periph_signal[BMP_SPI_BUS_ID].spid_in);
	}
	if (new_mode == JTAG_DRIVE_TMS)
	{
		// Wire up the TMS pin, which is GPIO controlled (mostly)
		// gpio_set_direction(CONFIG_TDI_GPIO, GPIO_MODE_INPUT_OUTPUT);
		// gpio_iomux_out(CONFIG_TDI_GPIO, PIN_FUNC_GPIO, false);
		esp32_spi_mux_pin(CONFIG_TDI_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);

		// TDI = MOSI
		esp32_spi_mux_pin(CONFIG_TMS_SWDIO_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spid_out, spi_periph_signal[BMP_SPI_BUS_ID].spid_in);
	}
	if (new_mode == JTAG_DRIVE_GPIO)
	{
		// Wire up the TMS pin, which is GPIO controlled (mostly)
		// gpio_set_direction(CONFIG_TDI_GPIO, GPIO_MODE_INPUT_OUTPUT);
		// gpio_iomux_out(CONFIG_TDI_GPIO, PIN_FUNC_GPIO, false);
		esp32_spi_mux_pin(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);

		// TDI = MOSI
		esp32_spi_mux_pin(CONFIG_TDI_GPIO, SIG_GPIO_OUT_IDX | (1 << 10), 128);
		// gpio_set_direction(CONFIG_TDI_GPIO, GPIO_MODE_INPUT_OUTPUT);
		// gpio_iomux_out(CONFIG_TDI_GPIO, PIN_FUNC_GPIO, false);
	}

	jtag_drive_mode = new_mode;
}

int jtagtap_init()
{
	jtag_proc.jtagtap_reset = jtagtap_reset;
	jtag_proc.jtagtap_next = jtagtap_next;
	jtag_proc.jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jtagtap_tdi_seq;

	esp32_spi_init(0);
	gpio_ll_output_enable(GPIO_HAL_GET_HW(GPIO_PORT_0), CONFIG_TMS_SWDIO_GPIO);
	gpio_ll_output_enable(GPIO_HAL_GET_HW(GPIO_PORT_0), CONFIG_TDI_GPIO);

	// // Wire up the TMS pin, which is GPIO controlled (mostly)
	// gpio_iomux_out(CONFIG_TMS_SWDIO_GPIO, PIN_FUNC_GPIO, false);
	// esp_rom_gpio_connect_out_signal(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX, false, false);
	// gpio_set_direction(CONFIG_TMS_SWDIO_GPIO, GPIO_MODE_OUTPUT);

	// // TDI = MOSI
	// gpio_iomux_out(CONFIG_TDI_GPIO, PIN_FUNC_GPIO, false);
	// esp_rom_gpio_connect_out_signal(CONFIG_TDI_GPIO, SIG_GPIO_OUT_IDX, false, false);
	// gpio_set_direction(CONFIG_TDI_GPIO, GPIO_MODE_OUTPUT);

	// // TDO = MISO
	// esp32_spi_mux_pin(CONFIG_TDO_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spiq_out, spi_periph_signal[BMP_SPI_BUS_ID].spiq_in);

	// // TCK = CLK
	// esp32_spi_mux_pin(CONFIG_TCK_SWCLK_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spiclk_out, spi_periph_signal[BMP_SPI_BUS_ID].spiclk_in);

	// jtag_drive_mode = JTAG_DRIVE_GPIO;

	// // Set full-duplex SPI operation
	// spi_ll_set_half_duplex(bmp_spi_hw, 0);
	// spi_ll_set_sio_mode(bmp_spi_hw, 0);

	// Go to JTAG mode for SWJ-DP. It starts with 50 clocks of TMS:1 TDI:0,
	// then sends the sequence 0xE73C.

	// Reset SW-DP
	jtagtap_tms_seq_tdi(0xffffffff, 0, 32);
	jtagtap_tms_seq_tdi(0xffffffff, 0, 50 - 32);

	// SWD to JTAG sequence
	// 0011 1100 1110 0111
	jtagtap_tms_seq(0xE73C, 16);
	jtagtap_soft_reset();

	return 0;
}

static void jtagtap_reset(void)
{
#ifdef TRST_PORT
	if (platform_hwversion() == 0)
	{
		volatile int i;
		gpio_clear(TRST_PORT, TRST_PIN);
		for (i = 0; i < 10000; i++)
			asm("nop");
		gpio_set(TRST_PORT, TRST_PIN);
	}
#endif
	jtagtap_soft_reset();
}

static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI)
{
	uint16_t ret;

	jtag_drive(JTAG_DRIVE_GPIO);

	gpio_set_level(CONFIG_TMS_SWDIO_GPIO, dTMS);
	gpio_set_level(CONFIG_TDI_GPIO, dTDI);

	bmp_spi_hw->data_buf[0] = 0;

	spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
	spi_ll_set_mosi_bitlen(bmp_spi_hw, 1);
	spi_ll_master_user_start(bmp_spi_hw);
	while (spi_ll_get_running_cmd(bmp_spi_hw))
	{
		// portYIELD();
	}

	ret = bmp_spi_hw->data_buf[0];
#ifdef DEBUG_JTAG_TRANSACTIONS
	ESP_LOGI(TAG_LL, "jtagtap_next(TMS = %d, TDI = %d) = %d", dTMS, dTDI, ret);
#endif

	return ret != 0;
}

void jtagtap_tms_seq_tdi(uint32_t MS, uint32_t tdi, int ticks)
{
#ifdef DEBUG_JTAG_TRANSACTIONS
	ESP_LOGI(TAG_LL, "jtagtap_tms_seq_tdi(MS = %08x, tdi = %d, ticks = %d)", MS, tdi, ticks);
#endif

	// Swap TMS and TDI so we hold TDI fixed and write out TMS
	gpio_set_level(CONFIG_TDI_GPIO, tdi);
	jtag_drive(JTAG_DRIVE_TMS);

	// Pre-set the final TMS bit for when we mux back to GPIO control
	gpio_set_level(CONFIG_TMS_SWDIO_GPIO, MS & (1 << (ticks - 1)));

	spi_ll_set_miso_bitlen(bmp_spi_hw, ticks);
	spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks);
	bmp_spi_hw->data_buf[0] = MS;
	spi_ll_master_user_start(bmp_spi_hw);
	while (spi_ll_get_running_cmd(bmp_spi_hw))
	{
		// portYIELD();
	}


	// // Swap TDI and TMS back
	// esp32_spi_mux_pin(CONFIG_TDI_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spid_out, spi_periph_signal[BMP_SPI_BUS_ID].spid_in);
	// gpio_iomux_out(CONFIG_TMS_SWDIO_GPIO, PIN_FUNC_GPIO, false);
	// esp_rom_gpio_connect_out_signal(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX, false, false);
	// gpio_set_direction(CONFIG_TMS_SWDIO_GPIO, GPIO_MODE_OUTPUT);
}

static void jtagtap_tms_seq(uint32_t MS, int ticks)
{
	jtagtap_tms_seq_tdi(MS, 1, ticks);
}

void jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint32_t data_in;
	uint32_t data_out = 0;

	gpio_set_level(CONFIG_TMS_SWDIO_GPIO, 0);
	jtag_drive(JTAG_DRIVE_TDI);

	// DI might not be byte-aligned, so use memcpy to copy it
	memcpy(&data_in, DI, 4);
	bmp_spi_hw->data_buf[0] = data_in;

	if (final_tms)
	{
		if (ticks > 1)
		{
			spi_ll_set_miso_bitlen(bmp_spi_hw, ticks - 1);
			spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks - 1);
			spi_ll_master_user_start(bmp_spi_hw);
			while (spi_ll_get_running_cmd(bmp_spi_hw))
			{
				// portYIELD();
			}
			data_out = bmp_spi_hw->data_buf[0];
		}

		// Transfer final bit with TMS high
		gpio_set_level(CONFIG_TMS_SWDIO_GPIO, final_tms);

		spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
		spi_ll_set_mosi_bitlen(bmp_spi_hw, 1);
		bmp_spi_hw->data_buf[0] = data_in >> ticks;
		spi_ll_master_user_start(bmp_spi_hw);
		while (spi_ll_get_running_cmd(bmp_spi_hw))
		{
			// portYIELD();
		}

		if (bmp_spi_hw->data_buf[0] & 1)
		{
			data_out |= (1 << ticks);
		}
	}
	else
	{
		spi_ll_set_miso_bitlen(bmp_spi_hw, ticks);
		spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks);
		spi_ll_master_user_start(bmp_spi_hw);
		while (spi_ll_get_running_cmd(bmp_spi_hw))
		{
			// portYIELD();
		}
		data_out = bmp_spi_hw->data_buf[0];
	}

	memcpy(DO, &data_out, 4);

#ifdef DEBUG_JTAG_TRANSACTIONS
	ESP_LOGI(TAG_LL, "jtagtap_tdi_tdo_seq(DO = %08x, final_tms = %d, DI = %08x, ticks = %d)", data_out, final_tms, data_in, ticks);
#endif
}

static void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint32_t data_in;

	gpio_set_level(CONFIG_TMS_SWDIO_GPIO, 0);
	jtag_drive(JTAG_DRIVE_TDI);

	// DI might not be byte-aligned, so use memcpy to copy it
	memcpy(&data_in, DI, 4);
	bmp_spi_hw->data_buf[0] = data_in;

#ifdef DEBUG_JTAG_TRANSACTIONS
	ESP_LOGI(TAG_LL, "jtagtap_tdi_seq(final_tms = %d, DI = %08x, ticks = %d)", final_tms, data_in, ticks);
#endif

	if (final_tms)
	{
		if (ticks > 1)
		{
			spi_ll_set_miso_bitlen(bmp_spi_hw, ticks - 1);
			spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks - 1);
			spi_ll_master_user_start(bmp_spi_hw);
			while (spi_ll_get_running_cmd(bmp_spi_hw))
			{
				// portYIELD();
			}
		}

		// Transfer final bit with TMS high
		gpio_set_level(CONFIG_TMS_SWDIO_GPIO, final_tms);

		spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
		spi_ll_set_mosi_bitlen(bmp_spi_hw, 1);
		bmp_spi_hw->data_buf[0] = data_in >> ticks;
		spi_ll_master_user_start(bmp_spi_hw);
		while (spi_ll_get_running_cmd(bmp_spi_hw))
		{
			// portYIELD();
		}
	}
	else
	{
		spi_ll_set_miso_bitlen(bmp_spi_hw, ticks);
		spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks);
		spi_ll_master_user_start(bmp_spi_hw);
		while (spi_ll_get_running_cmd(bmp_spi_hw))
		{
			// portYIELD();
		}
	}
}
