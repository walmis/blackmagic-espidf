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
#include "general.h"
#include "gdb_if.h"
#include "version.h"

#include "gdb_packet.h"
#include "gdb_main.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "morse.h"
#include "platform.h"
#include "CBUF.h"

#include <assert.h>
#include <sys/time.h>
#include <sys/unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dhcpserver/dhcpserver.h"
#include "http.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/api.h"
#include "lwip/tcp.h"

#include "esp32/rom/ets_sys.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "driver/uart.h"

#include "uart.h"
#include "wifi_manager.h"
#include "wifi.h"

#include <lwip/sockets.h>

#include "ota-tftp.h"

#define TAG "bmp-esp32"

nvs_handle h_nvs_conf;

void platform_max_frequency_set(uint32_t freq)
{
	if (freq < 100) {
		return;
	}
	if (freq > 48 * 1000 * 1000) {
		return;
	}
	int swdptap_set_frequency(uint32_t frequency);
	int actual_frequency = swdptap_set_frequency(freq);
	ESP_LOGI(__func__, "freq:%u", actual_frequency);
}

uint32_t platform_max_frequency_get(void)
{
	int swdptap_get_frequency(void);
	return swdptap_get_frequency();
}

static bool IRAM_ATTR perform_wifi_reset(void *user_ctx)
{
	(void)user_ctx;
	BaseType_t high_task_awoken = pdFALSE;

	if (gpio_get_level(GPIO_NUM_0) == 0) {
		wifi_manager_send_message_from_isr(WM_ORDER_FORGET_CONFIG, NULL, &high_task_awoken);
	}

	// return whether we need to yield at the end of ISR
	return high_task_awoken == pdTRUE;
}
#define TIMER_DIVIDER (16)			     //  Hardware timer clock divider
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

static bool timer_initialized = false;
static void IRAM_ATTR handle_wifi_reset(void *parameter)
{
	(void)parameter;

	// ESP_LOGI(__func__, "handling button press");
	int group = TIMER_GROUP_0;
	int timer = TIMER_0;
	if (!timer_initialized) {
		bool auto_reload = false;
		int timer_interval_sec = 3;

		/* Select and initialize basic parameters of the timer */
		timer_config_t config = {
			.divider = TIMER_DIVIDER,
			.counter_dir = TIMER_COUNT_UP,
			.counter_en = TIMER_PAUSE,
			.alarm_en = TIMER_ALARM_EN,
			.auto_reload = auto_reload,
		}; // default clock source is APB
		timer_init(group, timer, &config);

		/* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
		timer_set_counter_value(group, timer, 0);

		/* Configure the alarm value and the interrupt on alarm. */
		timer_set_alarm_value(group, timer, timer_interval_sec * TIMER_SCALE);
		timer_enable_intr(group, timer);

		// example_timer_info_t *timer_info = calloc(1, sizeof(example_timer_info_t));
		// timer_info->timer_group = group;
		// timer_info->timer_idx = timer;
		// timer_info->auto_reload = auto_reload;
		// timer_info->alarm_interval = timer_interval_sec;
		timer_isr_callback_add(group, timer, perform_wifi_reset, NULL /*timer_info*/, 0);

		timer_initialized = true;
	}

	timer_start(group, timer);
}

void platform_init(void)
{
	// Reset Button
	{
		const gpio_config_t gpio_conf = {
			.pin_bit_mask = BIT64(GPIO_NUM_0),
			.mode = GPIO_MODE_INPUT,
			.pull_up_en = 0,
			.pull_down_en = 0,
			.intr_type = GPIO_INTR_NEGEDGE,
		};
		gpio_config(&gpio_conf);
		gpio_install_isr_service(0);
		gpio_intr_enable(GPIO_NUM_0);
		gpio_isr_handler_add(GPIO_NUM_0, handle_wifi_reset, NULL);
	}
	{
		const gpio_config_t gpio_conf = {
			.pin_bit_mask = BIT64(CONFIG_TMS_SWDIO_GPIO),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = 0,
			.pull_down_en = 0,
			.intr_type = GPIO_INTR_DISABLE,
		};
		gpio_config(&gpio_conf);
		gpio_set_level(CONFIG_TMS_SWDIO_GPIO, 1);
	}
	{
		const gpio_config_t gpio_conf = {
			.pin_bit_mask = BIT64(CONFIG_TCK_SWCLK_GPIO),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = 0,
			.pull_down_en = 0,
			.intr_type = GPIO_INTR_DISABLE,
		};
		gpio_config(&gpio_conf);
		gpio_set_level(CONFIG_TMS_SWDIO_GPIO, 1);
	}
	{
		const gpio_config_t gpio_conf = {
			.pin_bit_mask = BIT64(CONFIG_NRST_GPIO),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = 0,
			.pull_down_en = 0,
			.intr_type = GPIO_INTR_DISABLE,
		};
		gpio_set_level(CONFIG_NRST_GPIO, 1);
		gpio_config(&gpio_conf);
	}
}

void platform_buffer_flush(void)
{
	;
}

void platform_nrst_set_val(bool assert)
{
	if (assert) {
		gpio_set_direction(CONFIG_NRST_GPIO, GPIO_OUTPUT);
		gpio_set_level(CONFIG_NRST_GPIO, 0);
	} else {
		gpio_set_level(CONFIG_NRST_GPIO, 1);
		gpio_set_direction(CONFIG_NRST_GPIO, GPIO_OUTPUT);
	}
}

bool platform_nrst_get_val(void)
{
	return !gpio_get_level(CONFIG_NRST_GPIO);
}

const char *platform_target_voltage(void)
{
	static char voltage[16];
	extern int32_t adc_read_system_voltage(void);

	int adjusted_voltage = adc_read_system_voltage();
	if (adjusted_voltage == -1) {
		snprintf(voltage, sizeof(voltage) - 1, "unknown");
		return voltage;
	}

	snprintf(voltage, sizeof(voltage) - 1, "%dmV", adjusted_voltage);
	return voltage;
}

uint32_t platform_time_ms(void)
{
	return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

#define vTaskDelayMs(ms) vTaskDelay((ms) / portTICK_PERIOD_MS)

void platform_delay(uint32_t ms)
{
	vTaskDelayMs(ms);
}

int platform_hwversion(void)
{
	return 0;
}

void platform_set_baud(uint32_t baud)
{
	uart_set_baudrate(CONFIG_TARGET_UART_IDX, baud);
	nvs_set_u32(h_nvs_conf, "uartbaud", baud);
}

bool cmd_setbaud(target *t, int argc, const char **argv)
{
	if (argc == 1) {
		uint32_t baud;
		uart_get_baudrate(CONFIG_TARGET_UART_IDX, &baud);
		gdb_outf("Current baud: %d\n", baud);
	}
	if (argc == 2) {
		int baud = atoi(argv[1]);
		gdb_outf("Setting baud: %d\n", baud);

		platform_set_baud(baud);
	}

	return 1;
}

int vprintf_noop(const char *s, va_list va)
{
	return 1;
}

extern void gdb_net_task();

void app_main(void)
{
	gpio_reset_pin(CONFIG_LED_GPIO);
	gpio_set_direction(CONFIG_LED_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(CONFIG_LED_GPIO, 1);
	extern void esp32_spi_mux_out(int pin, int out_signal);
	esp32_spi_mux_out(CONFIG_LED_GPIO, SIG_GPIO_OUT_IDX | (1 << 10));

#ifdef CONFIG_DEBUG_UART
	uart_dbg_install();
#else /* !CONFIG_DEBUG_UART */
	ESP_LOGI(__func__, "deactivating debug uart");
	esp_log_set_vprintf(vprintf_noop);
#endif

	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_ERROR_CHECK(nvs_open("config", NVS_READWRITE, &h_nvs_conf));

	bm_update_wifi_ssid();
	bm_update_wifi_ps();

	wifi_manager_start();
	httpd_start();

	platform_init();

	uart_init();

	xTaskCreate(&gdb_net_task, "gdb_net", 8192, NULL, 1, NULL);

	ota_tftp_init_server(69, 4);

	ESP_LOGI(__func__, "Free heap %d", esp_get_free_heap_size());

	// Wait two seconds for the system to stabilize before confirming the
	// new firmware image works. This gives us time to ensure the new
	// environment works well.
	vTaskDelay(pdMS_TO_TICKS(2000));
	esp_ota_mark_app_valid_cancel_rollback();
}
