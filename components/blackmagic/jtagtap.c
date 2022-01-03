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

#define TAG_LL "jtag-ll"
#define TAG "jtag"

jtag_proc_t jtag_proc;

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t MS, int ticks);
static void jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks);
static void jtagtap_tdi_seq(
	const uint8_t final_tms, const uint8_t *DI, int ticks);
static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI);

int jtagtap_init()
{
	TMS_SET_MODE();

	jtag_proc.jtagtap_reset = jtagtap_reset;
	jtag_proc.jtagtap_next = jtagtap_next;
	jtag_proc.jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jtagtap_tdi_seq;

    esp32_spi_init();

	// Wire up the TMS pin, which is GPIO controlled
    gpio_iomux_out(CONFIG_TMS_SWDIO_GPIO, PIN_FUNC_GPIO, false);
    esp_rom_gpio_connect_out_signal(CONFIG_TMS_SWDIO_GPIO, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(CONFIG_TMS_SWDIO_GPIO, GPIO_MODE_INPUT);

	// TDI = MOSI
    esp32_spi_mux_pin(CONFIG_TDI_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spid_out, spi_periph_signal[BMP_SPI_BUS_ID].spid_in);

	// TDO = MISO
    esp32_spi_mux_pin(CONFIG_TDO_GPIO, spi_periph_signal[BMP_SPI_BUS_ID].spiq_out, spi_periph_signal[BMP_SPI_BUS_ID].spiq_in);

    // Set full-duplex SPI operation
    spi_ll_set_half_duplex(bmp_spi_hw, 0);
    spi_ll_set_sio_mode(bmp_spi_hw, 0);
    
	/* Go to JTAG mode for SWJ-DP */
	for (int i = 0; i <= 50; i++)
		jtagtap_next(1, 0);		 /* Reset SW-DP */
	jtagtap_tms_seq(0xE73C, 16); /* SWD to JTAG sequence */
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
	register volatile int32_t cnt;

	// gpio_set_val(TMS_PORT, TMS_PIN, dTMS);
	gpio_set_level(CONFIG_TMS_SWDIO_GPIO, dTMS);

	// gpio_set_val(TDI_PORT, TDI_PIN, dTDI);
	bmp_spi_hw->data_buf[0] = dTDI;

    spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
    spi_ll_set_mosi_bitlen(bmp_spi_hw, 1);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }

	ret = bmp_spi_hw->data_buf[0];
#ifdef DEBUG_SWD_TRANSACTIONS
	ESP_LOGI(TAG_LL, "jtagtap_next(TMS = %d, TDI = %d) = %d\n", dTMS, dTDI, ret);
#endif

	return ret != 0;
}

static void jtagtap_tms_seq(uint32_t MS, int ticks)
{
	// gpio_set_val(TDI_PORT, TDI_PIN, 1);
	// int data = MS & 1;
	// register volatile int32_t cnt;
	// while (ticks)
	// {
	// 	gpio_set_val(TMS_PORT, TMS_PIN, data);
	// 	gpio_set(TCK_PORT, TCK_PIN);
	// 	MS >>= 1;
	// 	data = MS & 1;
	// 	ticks--;
	// 	gpio_clear(TCK_PORT, TCK_PIN);
	// }
}

static void jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	// uint8_t index = 1;
	// gpio_set_val(TMS_PORT, TMS_PIN, 0);
	// uint8_t res = 0;
	// register volatile int32_t cnt;
	// while (ticks > 1)
	// {
	// 	gpio_set_val(TDI_PORT, TDI_PIN, *DI & index);
	// 	gpio_set(TCK_PORT, TCK_PIN);
	// 	if (gpio_get(TDO_PORT, TDO_PIN))
	// 	{
	// 		res |= index;
	// 	}
	// 	if (!(index <<= 1))
	// 	{
	// 		*DO = res;
	// 		res = 0;
	// 		index = 1;
	// 		DI++;
	// 		DO++;
	// 	}
	// 	ticks--;
	// 	gpio_clear(TCK_PORT, TCK_PIN);
	// }
	// gpio_set_val(TMS_PORT, TMS_PIN, final_tms);
	// gpio_set_val(TDI_PORT, TDI_PIN, *DI & index);
	// gpio_set(TCK_PORT, TCK_PIN);
	// for (cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
	// 	;
	// if (gpio_get(TDO_PORT, TDO_PIN))
	// {
	// 	res |= index;
	// }
	// *DO = res;
	// gpio_clear(TCK_PORT, TCK_PIN);
	// for (cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
	// 	;
}

static void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	// uint8_t index = 1;
	// register volatile int32_t cnt;
	// while (ticks--)
	// {
	// 	gpio_set_val(TMS_PORT, TMS_PIN, ticks ? 0 : final_tms);
	// 	gpio_set_val(TDI_PORT, TDI_PIN, *DI & index);
	// 	gpio_set(TCK_PORT, TCK_PIN);
	// 	if (!(index <<= 1))
	// 	{
	// 		index = 1;
	// 		DI++;
	// 	}
	// 	gpio_clear(TCK_PORT, TCK_PIN);
	// }
}
