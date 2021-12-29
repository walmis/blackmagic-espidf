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
// #define DEBUG_SWD_TRANSACTIONS

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include "hal/gpio_ll.h"
#include "hal/gpio_hal.h"
#include "driver/spi_master.h"
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);

enum align
{
    ALIGN_BYTE = 0,
    ALIGN_HALFWORD = 1,
    ALIGN_WORD = 2,
    ALIGN_DWORD = 3
};

/* Try to keep this somewhat absract for later adding SW-DP */
struct ADIv5_AP_s;
typedef struct ADIv5_DP_s
{
    int refcnt;

    uint32_t idcode;
    uint32_t targetid; /* Contains IDCODE for DPv2 devices.*/

    void (*seq_out)(uint32_t MS, int ticks);
    void (*seq_out_parity)(uint32_t MS, int ticks);
    uint32_t (*seq_in)(int ticks);
    bool (*seq_in_parity)(uint32_t *ret, int ticks);
    /* dp_low_write returns true if no OK resonse, but ignores errors */
    bool (*dp_low_write)(struct ADIv5_DP_s *dp, uint16_t addr,
                         const uint32_t data);
    uint32_t (*dp_read)(struct ADIv5_DP_s *dp, uint16_t addr);
    uint32_t (*error)(struct ADIv5_DP_s *dp);
    uint32_t (*low_access)(struct ADIv5_DP_s *dp, uint8_t RnW,
                           uint16_t addr, uint32_t value);
    void (*abort)(struct ADIv5_DP_s *dp, uint32_t abort);

    uint32_t (*ap_read)(struct ADIv5_AP_s *ap, uint16_t addr);
    void (*ap_write)(struct ADIv5_AP_s *ap, uint16_t addr, uint32_t value);

    void (*mem_read)(struct ADIv5_AP_s *ap, void *dest, uint32_t src, size_t len);
    void (*mem_write_sized)(struct ADIv5_AP_s *ap, uint32_t dest, const void *src,
                            size_t len, enum align align);
    uint8_t dp_jd_index;
    uint8_t fault;
} ADIv5_DP_t;
typedef struct ADIv5_DP_s ADIv5_DP_t;

struct ADIv5_AP_s
{
    int refcnt;

    ADIv5_DP_t *dp;
    uint8_t apsel;

    uint32_t idr;
    uint32_t base;
    uint32_t csw;
    uint32_t ap_cortexm_demcr; /* Copy of demcr when starting */
    uint32_t ap_storage;       /* E.g to hold STM32F7 initial DBGMCU_CR value.*/
    uint16_t ap_designer;
    uint16_t ap_partno;
};
typedef struct ADIv5_AP_s ADIv5_AP_t;

// static portMUX_TYPE swd_spinlock = portMUX_INITIALIZER_UNLOCKED;

#define PIN_NUM_SWDIO 21
#define PIN_NUM_SWCLK 25
#define PIN_NUM_SRST 23
#define SWD_SPI_BUS_ID HSPI_HOST
static spi_device_handle_t swd_spi_handle;

enum
{
    SWDIO_STATUS_FLOAT = 0,
    SWDIO_STATUS_DRIVE
};

static IRAM_ATTR void swdptap_turnaround(int dir)
{
    static int olddir = SWDIO_STATUS_DRIVE;

    /* Don't turnaround if direction not changing */
    if (dir == olddir)
    {
        return;
    }
    olddir = dir;

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI("swd-ll", "%s", dir ? "\n-> " : "\n<- ");
#endif
    spi_transaction_t t = {
        .rxlength = (dir == SWDIO_STATUS_FLOAT),
        .length = (dir == SWDIO_STATUS_DRIVE),
        .flags = (dir == SWDIO_STATUS_FLOAT) ? SPI_TRANS_USE_RXDATA : SPI_TRANS_USE_TXDATA,
    };

    esp_err_t err = spi_device_polling_transmit(swd_spi_handle, &t);
    ESP_ERROR_CHECK(err);
}

static IRAM_ATTR uint32_t swdptap_seq_in(int ticks)
{
    esp_err_t err;
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_transaction_t t = {
        .length = 0,
        .rxlength = ticks,
        .flags = SPI_TRANS_USE_RXDATA,
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t);
    ESP_ERROR_CHECK(err);

#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI("swd-ll", "seq_in: %d bits %08x", ticks, *((uint32_t *)t.rx_data));
#endif
    return *((uint32_t *)t.rx_data);
}

static IRAM_ATTR bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
    esp_err_t err;
    swdptap_turnaround(SWDIO_STATUS_FLOAT);

    spi_transaction_t t1 = {
        .length = 0,
        .rxlength = ticks,
        .rx_buffer = ret,
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t1);
    ESP_ERROR_CHECK(err);

    spi_transaction_t t2 = {
        .length = 0,
        .rxlength = 1,
        .flags = SPI_TRANS_USE_RXDATA,
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t2);
    ESP_ERROR_CHECK(err);

    int parity = __builtin_popcount(*ret) + (t2.rx_data[0] & 1);
#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI("swd-ll", "seq_in_parity: %d bits %x %d", ticks, *ret, parity);
#endif
    return (parity & 1);
}

static IRAM_ATTR void swdptap_seq_out(uint32_t MS, int ticks)
{
    esp_err_t err;
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI("swd-ll", "seq_out: %d bits %x", ticks, MS);
#endif
    spi_transaction_t t = {
        .length = ticks,
        .rxlength = 0,
        .tx_buffer = &MS,
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t);
    ESP_ERROR_CHECK(err);
}

static IRAM_ATTR void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
    esp_err_t err;
    int parity = __builtin_popcount(MS);
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
#ifdef DEBUG_SWD_TRANSACTIONS
    ESP_LOGI("swd-ll", "seq_out_parity: %d bits %x %d", ticks, MS, parity);
#endif

    spi_transaction_t t1 = {
        .length = ticks,
        .rxlength = 0,
        .tx_buffer = &MS,
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t1);
    ESP_ERROR_CHECK(err);

    spi_transaction_t t2 = {
        .length = 1,
        .rxlength = 0,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {parity},
    };
    err = spi_device_polling_transmit(swd_spi_handle, &t2);
    ESP_ERROR_CHECK(err);
}

int swdptap_init(ADIv5_DP_t *dp)
{
    static bool initialized = false;

    dp->seq_in = swdptap_seq_in;
    dp->seq_in_parity = swdptap_seq_in_parity;
    dp->seq_out = swdptap_seq_out;
    dp->seq_out_parity = swdptap_seq_out_parity;

    if (initialized) {
        return 0;
    }
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing bus SPI%d...", SWD_SPI_BUS_ID + 1);
    const spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_NUM_SWDIO,
        .sclk_io_num = PIN_NUM_SWCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // Maximum transfer size is in bytes. We define 5 bytes here because
        // the system will provide us at most one uint32_t of data, and we
        // sometimes need to add a parity bit.
        .max_transfer_sz = 5,
    };
    // Initialize the SPI bus
    // Note: DMA may need to be disabled --
    // https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/peripherals/spi_master.html#known-issues
    ret = spi_bus_initialize(SWD_SPI_BUS_ID, &buscfg, SPI_DMA_DISABLED);
    ESP_ERROR_CHECK(ret);

    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5 * 1000 * 1000,
        .mode = 3, // SPI mode 0
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_BIT_LSBFIRST,
        // .pre_cb = cs_high,
        // .post_cb = cs_low,
        .input_delay_ns = 0,
    };

    // Attach the SWD device to the SPI bus
    ESP_LOGI(TAG, "Adding interface to SPI bus...");
    ret = spi_bus_add_device(SWD_SPI_BUS_ID, &devcfg, &swd_spi_handle);
    if (ret != ESP_OK)
    {
        goto cleanup;
    }

    gpio_set_direction(PIN_NUM_SRST, GPIO_MODE_OUTPUT);

    // Acquire the bus, and don't release it
    ret = spi_device_acquire_bus(swd_spi_handle, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    initialized = true;
    return 0;

cleanup:
    spi_bus_remove_device(swd_spi_handle);
    return -1;
}
