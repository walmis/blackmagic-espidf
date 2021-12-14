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

#include "general.h"
#include "timing.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_hal.h"
#include <freertos/FreeRTOS.h>
#include "adiv5.h"

static portMUX_TYPE swd_spinlock = portMUX_INITIALIZER_UNLOCKED;

static IRAM_ATTR void swd_delay(void)
{
    int cnt;
    // Force domain syncrhonization
    (void)GPIO.in;
    for (cnt = swd_delay_cnt; --cnt > 0;)
    {
        asm("nop");
    }
}

static IRAM_ATTR void swclk_high(void)
{
    GPIO.out_w1ts = (0x1 << SWCLK_PIN);
    swd_delay();
}

static IRAM_ATTR void swclk_low(void)
{
    GPIO.out_w1tc = (0x1 << SWCLK_PIN);
    swd_delay();
}

static IRAM_ATTR void swdio_set(uint32_t val) {
    if (val) {
        GPIO.out_w1ts = (0x1 << SWDIO_PIN);
    } else {
        GPIO.out_w1tc = (0x1 << SWDIO_PIN);
    }
}

static IRAM_ATTR int swdio_get(void) {
    return GPIO.in & SWDIO_PIN;
}

enum
{
    SWDIO_STATUS_FLOAT = 0,
    SWDIO_STATUS_DRIVE
};

static IRAM_ATTR void swdptap_turnaround(int dir) __attribute__((optimize(3)));
static IRAM_ATTR uint32_t swdptap_seq_in(int ticks) __attribute__((optimize(3)));
static IRAM_ATTR bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
    __attribute__((optimize(3)));
static IRAM_ATTR void swdptap_seq_out(uint32_t MS, int ticks)
    __attribute__((optimize(3)));
static IRAM_ATTR void swdptap_seq_out_parity(uint32_t MS, int ticks)
    __attribute__((optimize(3)));

static void swdptap_turnaround(int dir)
{
    static int olddir = SWDIO_STATUS_FLOAT;

    /* Don't turnaround if direction not changing */
    if (dir == olddir)
        return;
    olddir = dir;

#ifdef DEBUG_SWD_BITS
    DEBUG("%s", dir ? "\n-> " : "\n<- ");
#endif

    if (dir == SWDIO_STATUS_FLOAT)
    {
        GPIO.enable_w1tc = (0x1 << SWDIO_PIN);
        (void)GPIO.enable;
        // SWDIO_MODE_FLOAT();
    }

    // gpio_set(SWCLK_PORT, SWCLK_PIN);
    swclk_high();

    // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    swclk_low();

    if (dir == SWDIO_STATUS_DRIVE)
    {
        GPIO.enable_w1ts = (0x1 << SWDIO_PIN);
        (void)GPIO.enable;
        // SWDIO_MODE_DRIVE();
    }
}

static uint32_t swdptap_seq_in(int ticks)
{
    uint32_t index = 1;
    uint32_t ret = 0;
    int len = ticks;
    portENTER_CRITICAL(&swd_spinlock);
    swdptap_turnaround(SWDIO_STATUS_FLOAT);
    // if (swd_delay_cnt)
    // {
    while (len--)
    {
        int res;
        // res = gpio_get(SWDIO_PORT, SWDIO_PIN);
        res = swdio_get();
        ret |= (res) ? index : 0;
        index <<= 1;

        // gpio_set(SWCLK_PORT, SWCLK_PIN);
        swclk_high();

        // gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swclk_low();
    }
    // }
    // else
    // {
    //     volatile int res;
    //     while (len--)
    //     {
    //         // res = gpio_get(SWDIO_PORT, SWDIO_PIN);
    //         res = gpio_get_level(SWDIO_PIN);

    //         // gpio_set(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 1);
    //         ret |= (res) ? index : 0;
    //         index <<= 1;

    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 0);
    //         // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    //     }
    // }
#ifdef DEBUG_SWD_BITS
    for (int i = 0; i < len; i++)
        DEBUG("%d", (ret & (1 << i)) ? 1 : 0);
#endif
    portEXIT_CRITICAL(&swd_spinlock);
    // ESP_LOGI("swd", "seq_in: %d bits %x", ticks, ret);
    return ret;
}

static bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
    uint32_t index = 1;
    uint32_t res = 0;
    bool bit;
    int len = ticks;
    portENTER_CRITICAL(&swd_spinlock);

    swdptap_turnaround(SWDIO_STATUS_FLOAT);
    // if (swd_delay_cnt)
    // {
    while (len--)
    {
        // bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
        bit = swdio_get();
        res |= (bit) ? index : 0;
        index <<= 1;

        // gpio_set(SWCLK_PORT, SWCLK_PIN);
        swclk_high();

        // gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swclk_low();
    }
    // }
    // else
    // {
    //     while (len--)
    //     {
    //         // bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
    //         bit = gpio_get_level(SWDIO_PIN);
    //         res |= (bit) ? index : 0;
    //         index <<= 1;

    //         // gpio_set(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 1);

    //         // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 0);
    //     }
    // }
    int parity = __builtin_popcount(res);
    // bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
    bit = gpio_get_level(SWDIO_PIN);
    parity += (bit) ? 1 : 0;

    // gpio_set(SWCLK_PORT, SWCLK_PIN);
    swclk_high();

    // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    swclk_low();

#ifdef DEBUG_SWD_BITS
    for (int i = 0; i < len; i++)
        DEBUG("%d", (res & (1 << i)) ? 1 : 0);
#endif
    *ret = res;
    /* Terminate the read cycle now */
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
    portEXIT_CRITICAL(&swd_spinlock);

    // ESP_LOGI("swd", "seq_in_parity: %d bits %x %d", ticks, res, parity);
    return (parity & 1);
}

static void swdptap_seq_out(uint32_t MS, int ticks)
{
    // ESP_LOGI("swd", "seq_out: %d bits %x", ticks, MS);
#ifdef DEBUG_SWD_BITS
    for (int i = 0; i < ticks; i++)
        DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
    portENTER_CRITICAL(&swd_spinlock);
    swdptap_turnaround(SWDIO_STATUS_DRIVE);
    // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
    swdio_set(MS & 1);
    // if (swd_delay_cnt)
    // {
    while (ticks--)
    {
        // gpio_set(SWCLK_PORT, SWCLK_PIN);
        swclk_high();

        MS >>= 1;
        // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
        swdio_set(MS & 1);

        // gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swclk_low();
    }
    // }
    // else
    // {
    //     while (ticks--)
    //     {
    //         // gpio_set(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 1);

    //         MS >>= 1;
    //         // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
    //         gpio_set_level(SWDIO_PIN, MS & 1);

    //         // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 0);
    //     }
    // }
    portEXIT_CRITICAL(&swd_spinlock);
}

static void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
    int parity = __builtin_popcount(MS);
    // ESP_LOGI("swd", "seq_out_parity: %d bits %x %d", ticks, MS, parity);
#ifdef DEBUG_SWD_BITS
    for (int i = 0; i < ticks; i++)
        DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
    portENTER_CRITICAL(&swd_spinlock);
    swdptap_turnaround(SWDIO_STATUS_DRIVE);

    // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
    // gpio_set_level(SWDIO_PIN, MS & 1);
    swdio_set(MS & 1);
    MS >>= 1;

    // if (swd_delay_cnt)
    // {
    while (ticks--)
    {
        // gpio_set(SWCLK_PORT, SWCLK_PIN);
        swclk_high();

        // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
        // gpio_set_level(SWDIO_PIN, MS & 1);
        swdio_set(MS & 1);
        MS >>= 1;

        // gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swclk_low();
    }
    // }
    // else
    // {
    //     while (ticks--)
    //     {
    //         // gpio_set(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 1);

    //         // gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
    //         gpio_set_level(SWDIO_PIN, MS & 1);
    //         MS >>= 1;

    //         // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    //         gpio_ll_set_level(GPIO_HAL_GET_HW(GPIO_PORT_0), SWCLK_PIN, 0);
    //     }
    // }
    // gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity & 1);
    gpio_set_level(SWDIO_PIN, parity & 1);

    // gpio_set(SWCLK_PORT, SWCLK_PIN);
    swclk_high();

    // gpio_clear(SWCLK_PORT, SWCLK_PIN);
    swclk_low();

    portEXIT_CRITICAL(&swd_spinlock);
}

int swdptap_init(ADIv5_DP_t *dp)
{
    dp->seq_in = swdptap_seq_in;
    dp->seq_in_parity = swdptap_seq_in_parity;
    dp->seq_out = swdptap_seq_out;
    dp->seq_out_parity = swdptap_seq_out_parity;

    gpio_set_direction(SRST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SWCLK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SWDIO_PIN, GPIO_MODE_INPUT_OUTPUT);
    swclk_low();

    return 0;
}
