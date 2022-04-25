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

#ifndef __PLATFORM_H
#define __PLATFORM_H

#undef PRIx32
#define PRIx32 "x"

#undef SCNx32
#define SCNx32 "x"

#include "esp_log.h"
#include "esp_attr.h"

void platform_buffer_flush(void);
void platform_set_baud(uint32_t baud);

#define SET_RUN_STATE(state)
#define SET_IDLE_STATE(state)
#define SET_ERROR_STATE(state) gpio_set_level(CONFIG_LED_GPIO, !state)

#define ENABLE_DEBUG 1
#define DEBUG(x, ...)                        \
	do                                       \
	{                                        \
		TRIM(out, x);                        \
		ESP_LOGD("BMP", out, ##__VA_ARGS__); \
	} while (0)

#include "timing.h"
#include "driver/gpio.h"

#define TMS_SET_MODE() \
	do                 \
	{                  \
	} while (0)

// no-connects on ESP-01: 12,13,14,15
#define TMS_PIN CONFIG_TMS_SWDIO_GPIO
#define TCK_PIN CONFIG_TCK_SWCLK_GPIO

#define TDI_PIN CONFIG_TDI_GPIO // "
#define TDO_PIN CONFIG_TDO_GPIO // "

#define SWDIO_PIN CONFIG_TMS_SWDIO_GPIO
#define SWCLK_PIN CONFIG_TCK_SWCLK_GPIO
#define SRST_PIN CONFIG_SRST_GPIO

#define SWCLK_PORT 0
#define SWDIO_PORT 0

#define gpio_enable(pin, mode)         \
	do                                 \
	{                                  \
		gpio_set_direction(pin, mode); \
	} while (0)
#define gpio_set(port, pin)           \
	do                                \
	{                                 \
		GPIO.out_w1ts = (0x1 << pin); \
	} while (0)
#define gpio_clear(port, pin)         \
	do                                \
	{                                 \
		GPIO.out_w1tc = (0x1 << pin); \
	} while (0)
#define gpio_get(port, pin) ((GPIO.in >> pin) & 0x1)
#define gpio_set_val(port, pin, value) \
	if (value)                         \
	{                                  \
		gpio_set(port, pin);           \
	}                                  \
	else                               \
	{                                  \
		gpio_clear(port, pin);         \
	}

#define GPIO_INPUT GPIO_MODE_INPUT
#define GPIO_OUTPUT GPIO_MODE_OUTPUT

#define SWDIO_MODE_FLOAT() \
	gpio_set_direction(SWDIO_PIN, GPIO_MODE_INPUT);

#define SWDIO_MODE_DRIVE() \
	gpio_set_direction(SWDIO_PIN, GPIO_MODE_OUTPUT);

#define PLATFORM_HAS_DEBUG
#define PLATFORM_IDENT "esp32"
#endif

#define PLATFORM_HAS_TRACESWO
#define NUM_TRACE_PACKETS		(128)		/* This is an 8K buffer */
#define TRACESWO_PROTOCOL		2			/* 1 = Manchester, 2 = NRZ / async */

extern uint32_t swd_delay_cnt;
