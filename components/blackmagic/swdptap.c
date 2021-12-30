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

#define INITIAL_FREQUENCY (10 * 1000 * 1000)
#define SWD_SPI_BUS_ID HSPI_HOST
static spi_device_handle_t swd_spi_handle;
static spi_dev_t *spi_hw = SPI_LL_GET_HW(SWD_SPI_BUS_ID);

enum
{
    SWDIO_STATUS_FLOAT = 0,
    SWDIO_STATUS_DRIVE,
};

static int actual_freq;
int swdptap_set_frequency(uint32_t frequency) {
    esp_err_t ret;
    spi_hal_timing_conf_t temp_timing_conf;
    spi_hal_timing_param_t timing_param = {
        // SWD reuses the same pin for both input and output
        .half_duplex = 1,

        // We need to add dummy cycles to compensate for timing
        .no_compensate = 0,

        // 5 MHz
        .clock_speed_hz = frequency,

        // 50% duty cycle
        .duty_cycle = 128,

        // No input delay
        .input_delay_ns = 0,

        // Use GPIO matrix for more flexible pin assignment at the
        // cost of slightly worse performance.
        .use_gpio = 1,
    };

    ret = spi_hal_cal_clock_conf(&timing_param, &actual_freq, &temp_timing_conf);
    ESP_ERROR_CHECK(ret);
    spi_ll_master_set_clock_by_reg(spi_hw, &temp_timing_conf.clock_reg);
    spi_ll_set_dummy(spi_hw, temp_timing_conf.timing_dummy);

    uint32_t miso_delay_num = 0;
    uint32_t miso_delay_mode = 0;
    if (temp_timing_conf.timing_miso_delay < 0) {
        miso_delay_mode = 2;
        miso_delay_num = 0;
    } else {
        //if the data is so fast that dummy_bit is used, delay some apb clocks to meet the timing
        miso_delay_mode = 0;
        miso_delay_num = temp_timing_conf.timing_miso_delay;
    }
    spi_ll_set_miso_delay(spi_hw, miso_delay_mode, miso_delay_num);

    return actual_freq;
}

int swdptap_get_frequency(void) {
    return actual_freq;
}

static IRAM_ATTR void swdptap_turnaround(int dir)
{
    static int olddir = SWDIO_STATUS_FLOAT;

    /* Don't turnaround if direction not changing */
    if (dir == olddir)
    {
        return;
    }
    olddir = dir;

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "%s", dir ? "-> " : "<- ");
#endif
    // Set up MISO and MOSI pins according to our new direction
    if (dir == SWDIO_STATUS_FLOAT)
    {
        spi_ll_enable_miso(spi_hw, 1);
        spi_ll_enable_mosi(spi_hw, 0);
        // Let the SWDIO pin float, as it will be driven by the device
        GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1tc = (1 << CONFIG_TMS_SWDIO_GPIO);
    }

    // Receive one dummy bit
    spi_ll_set_miso_bitlen(spi_hw, 1);
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
    {
        // portYIELD();
    }

    if (dir == SWDIO_STATUS_DRIVE)
    {
        spi_ll_enable_miso(spi_hw, 0);
        spi_ll_enable_mosi(spi_hw, 1);
        // Drive the SWDIO pin, as we're in control now
        GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1ts = (1 << CONFIG_TMS_SWDIO_GPIO);
    }
}

static IRAM_ATTR uint32_t swdptap_seq_in(int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_ll_set_miso_bitlen(spi_hw, ticks);
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
    {
        // portYIELD();
    }

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI(TAG_LL, "seq_in: %d bits %08x", ticks, spi_hw->data_buf[0]);
#endif
    return spi_hw->data_buf[0];
}

static IRAM_ATTR bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_ll_set_miso_bitlen(spi_hw, ticks);
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
    {
        // portYIELD();
    }
    *ret = spi_hw->data_buf[0];

    // Get parity bit
    spi_ll_set_miso_bitlen(spi_hw, 1);
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
    {
        // portYIELD();
    }

    int parity = __builtin_popcount(*ret) + (spi_hw->data_buf[0] & 1);
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

    spi_ll_set_mosi_bitlen(spi_hw, ticks);
    spi_hw->data_buf[0] = MS;
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
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

    if (ticks == 32) {
        spi_hw->data_buf[0] = MS;
        spi_hw->data_buf[1] = parity;
    } else {
        spi_hw->data_buf[0] = (MS << 1) | (parity & 1);
    }
    spi_ll_set_mosi_bitlen(spi_hw, ticks + 1);
    spi_ll_master_user_start(spi_hw);
    while (spi_ll_get_running_cmd(spi_hw))
    {
        // portYIELD();
    }
}

int swdptap_init(ADIv5_DP_t *dp)
{
    static bool initialized = false;

    dp->seq_in = swdptap_seq_in;
    dp->seq_in_parity = swdptap_seq_in_parity;
    dp->seq_out = swdptap_seq_out;
    dp->seq_out_parity = swdptap_seq_out_parity;

    if (initialized)
    {
        return 0;
    }
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing bus SPI%d...", SWD_SPI_BUS_ID + 1);
    const spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = CONFIG_TMS_SWDIO_GPIO,
        .sclk_io_num = CONFIG_TCK_SWCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // Maximum transfer size is in bytes. We define 5 bytes here because
        // the system will provide us at most one uint32_t of data, and we
        // sometimes need to add a parity bit.
        .max_transfer_sz = 5,
    };
    // Initialize the SPI bus without DMA. Our packets all fit into one or two 32-bit registers, so we
    // don't need DMA.
    ret = spi_bus_initialize(SWD_SPI_BUS_ID, &buscfg, SPI_DMA_DISABLED);
    ESP_ERROR_CHECK(ret);

    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = INITIAL_FREQUENCY,
        .mode = 3, // SPI mode 3
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_BIT_LSBFIRST,
        .input_delay_ns = 0,
    };

    // Attach the SWD device to the SPI bus
    ESP_LOGI(TAG, "Adding interface to SPI bus...");
    ret = spi_bus_add_device(SWD_SPI_BUS_ID, &devcfg, &swd_spi_handle);
    if (ret != ESP_OK)
    {
        goto cleanup;
    }

    // Acquire the bus, and don't release it
    ret = spi_device_acquire_bus(swd_spi_handle, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    swdptap_set_frequency(INITIAL_FREQUENCY);

    // SWD is an LSB protocol
    spi_ll_set_rx_lsbfirst(spi_hw, 1);
    spi_ll_set_tx_lsbfirst(spi_hw, 1);

    // Use SPI mode 3
    spi_ll_master_set_mode(spi_hw, 3);

    // Set half-duplex
    spi_ll_set_half_duplex(spi_hw, 1);
    spi_ll_set_sio_mode(spi_hw, 1);
    
    // Normal IO mode, not DIO or QIO
    spi_ll_master_set_io_mode(spi_hw, SPI_LL_IO_MODE_NORMAL);

    //output values of timing configuration

    spi_ll_set_addr_bitlen(spi_hw, 0);
    spi_ll_set_command_bitlen(spi_hw, 0);

    // spi_ll_set_command(spi_hw, trans->cmd, cmdlen, dev->tx_lsbfirst);
    // spi_ll_set_address(spi_hw, trans->addr, addrlen, dev->tx_lsbfirst);

    // Set the OEN bit to be controlled by us, rather than by the peripheral. This bit will
    // be altered when we do the turnaround above.
    GPIO_HAL_GET_HW(GPIO_PORT_0)->func_out_sel_cfg[CONFIG_TMS_SWDIO_GPIO].oen_sel = 1;
    GPIO_HAL_GET_HW(GPIO_PORT_0)->enable_w1tc = (1 << CONFIG_TMS_SWDIO_GPIO);

    initialized = true;
    return 0;

cleanup:
    spi_bus_remove_device(swd_spi_handle);
    return -1;
}
