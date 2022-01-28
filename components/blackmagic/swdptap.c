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

/* This file implements the SW-DP interface. */

#define TAG "swd"
#define TAG_LL "swd-ll"
// #define DEBUG_SWD_TRANSACTIONS

#include "general.h"

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include "adiv5.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_hal.h"
#include "hal/spi_ll.h"
#include "hal/spi_hal.h"

#include "esp32_spi.h"
#include "sdkconfig.h"

enum
{
    SWDIO_STATUS_FLOAT = 0,
    SWDIO_STATUS_DRIVE,
};
static int previous_swd_dir = SWDIO_STATUS_FLOAT;

int swdptap_set_frequency(uint32_t frequency)
{
    return esp32_spi_set_frequency(frequency);
}

int swdptap_get_frequency(void)
{
    return esp32_spi_get_frequency();
}

static IRAM_ATTR void swdptap_turnaround(int dir)
{

    /* Don't turnaround if direction not changing */
    if (dir == previous_swd_dir)
    {
        return;
    }
    previous_swd_dir = dir;

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "%s", dir ? "-> " : "<- ");
#endif
    // Set up MISO and MOSI pins according to our new direction
    if (dir == SWDIO_STATUS_FLOAT)
    {
        spi_ll_enable_miso(bmp_spi_hw, 1);
        spi_ll_enable_mosi(bmp_spi_hw, 0);
        // Let the SWDIO pin float, as it will be driven by the device
        GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1tc = (1 << CONFIG_TMS_SWDIO_GPIO);
    }

    // Receive one dummy bit
    spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }

    if (dir == SWDIO_STATUS_DRIVE)
    {
        spi_ll_enable_miso(bmp_spi_hw, 0);
        spi_ll_enable_mosi(bmp_spi_hw, 1);
        // Drive the SWDIO pin, as we're in control now
        GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1ts = (1 << CONFIG_TMS_SWDIO_GPIO);
    }
}

static IRAM_ATTR uint32_t swdptap_seq_in(int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_ll_set_miso_bitlen(bmp_spi_hw, ticks);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "seq_in: %d bits %08x", ticks, bmp_spi_hw->data_buf[0]);
#endif
    return bmp_spi_hw->data_buf[0];
}

static IRAM_ATTR bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_ll_set_miso_bitlen(bmp_spi_hw, ticks);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }
    *ret = bmp_spi_hw->data_buf[0];

    // Get parity bit
    spi_ll_set_miso_bitlen(bmp_spi_hw, 1);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }

    int parity = __builtin_popcount(*ret) + (bmp_spi_hw->data_buf[0] & 1);
#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "seq_in_parity: %d bits %x %d", ticks, *ret, parity);
#endif
    return (parity & 1);
}

static IRAM_ATTR void swdptap_seq_out(uint32_t MS, int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "seq_out: %d bits %x", ticks, MS);
#endif

    spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks);
    bmp_spi_hw->data_buf[0] = MS;
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }
}

static IRAM_ATTR void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
    int parity = __builtin_popcount(MS);

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "seq_out_parity: %d bits %x %d", ticks, MS, parity);
#endif

    if (ticks == 32)
    {
        bmp_spi_hw->data_buf[0] = MS;
        bmp_spi_hw->data_buf[1] = parity;
    }
    else
    {
        bmp_spi_hw->data_buf[0] = (MS << 1) | (parity & 1);
    }
    spi_ll_set_mosi_bitlen(bmp_spi_hw, ticks + 1);
    spi_ll_master_user_start(bmp_spi_hw);
    while (spi_ll_get_running_cmd(bmp_spi_hw))
    {
        // portYIELD();
    }
}

int swdptap_init(ADIv5_DP_t *dp)
{

    dp->seq_in = swdptap_seq_in;
    dp->seq_in_parity = swdptap_seq_in_parity;
    dp->seq_out = swdptap_seq_out;
    dp->seq_out_parity = swdptap_seq_out_parity;

    esp32_spi_init(1);

    previous_swd_dir = SWDIO_STATUS_FLOAT;

    return 0;
}
