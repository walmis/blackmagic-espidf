/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Copyright (C) 2014 Fredrik Ahlberg <fredrik@z80.se>
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

/* This file implements capture of the TRACESWO output.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

#include "general.h"
#include "traceswo.h"
#include <esp32/clk.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include "sdkconfig.h"

#include "driver/uart.h"
#include "hal/uart_ll.h"

static const char TAG[] = "traceswo";

static xTaskHandle rx_pid;
static volatile bool should_exit_calibration;
static xQueueHandle uart_event_queue;

#define UART_RECALCULATE_BAUD 0x1000
#define UART_TERMINATE 0x1001

static esp_err_t uart_reset_rx_fifo(uart_port_t uart_num)
{
	//Due to hardware issue, we can not use fifo_rst to reset uart fifo.
	//See description about UART_TXFIFO_RST and UART_RXFIFO_RST in <<esp32_technical_reference_manual>> v2.6 or later.

	// we read the data out and make `fifo_len == 0 && rd_addr == wr_addr`.
	while (UART_LL_GET_HW(uart_num)->status.rxfifo_cnt != 0 ||
	       (UART_LL_GET_HW(uart_num)->mem_rx_status.wr_addr != UART_LL_GET_HW(uart_num)->mem_rx_status.rd_addr)) {
		READ_PERI_REG(UART_FIFO_REG(uart_num));
	}
	return ESP_OK;
}

static int32_t uart_baud_detect(uart_port_t uart_num, int sample_bits, int max_tries)
{
	int low_period = 0;
	int high_period = 0;
	int tries = 0;
	uint32_t intena_reg = UART_LL_GET_HW(uart_num)->int_ena.val;

	// Disable the interruput.
	UART_LL_GET_HW(uart_num)->int_ena.val = 0;
	UART_LL_GET_HW(uart_num)->int_clr.val = ~0;

	// Filter
	UART_LL_GET_HW(uart_num)->auto_baud.glitch_filt = 4;

	// Clear the previous result
	UART_LL_GET_HW(uart_num)->auto_baud.en = 0;
	UART_LL_GET_HW(uart_num)->auto_baud.en = 1;
	ESP_LOGI(__func__, "waiting for %d samples", sample_bits);
	while (UART_LL_GET_HW(uart_num)->rxd_cnt.edge_cnt < sample_bits) {
		if (tries++ >= max_tries) {
			// Disable the baudrate detection
			UART_LL_GET_HW(uart_num)->auto_baud.en = 0;

			// Reset the fifo
			ESP_LOGD(__func__, "resetting the fifo");
			uart_reset_rx_fifo(uart_num);
			UART_LL_GET_HW(uart_num)->int_ena.val = intena_reg;
			return -1;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
		// esp_task_wdt_reset();
		// ets_delay_us(10);
	}
	low_period = UART_LL_GET_HW(uart_num)->lowpulse.min_cnt;
	high_period = UART_LL_GET_HW(uart_num)->highpulse.min_cnt;

	// Disable the baudrate detection
	UART_LL_GET_HW(uart_num)->auto_baud.en = 0;

	// Reset the fifo
	ESP_LOGD(__func__, "resetting the fifo");
	uart_reset_rx_fifo(uart_num);
	UART_LL_GET_HW(uart_num)->int_ena.val = intena_reg;

	// Set the clock divider reg
	UART_LL_GET_HW(uart_num)->clk_div.div_int = (low_period > high_period) ? high_period : low_period;

	// Return the divider. baud = APB / divider
	return esp_clk_apb_freq() / ((low_period > high_period) ? high_period : low_period);
}

/**
 * @brief UART Receive Task
 *
 */
static void swo_uart_rx_task(void *arg)
{
	uint32_t default_baud = (uint32_t)arg;
	int baud_rate = default_baud;
	esp_err_t ret;
	uint8_t buf[256];
	int bufpos = 0;

	if (baud_rate == 0) {
		baud_rate = 115200;
	}

	ret = uart_set_pin(CONFIG_TRACE_SWO_UART_IDX, UART_PIN_NO_CHANGE, CONFIG_TDO_GPIO, UART_PIN_NO_CHANGE,
			   UART_PIN_NO_CHANGE);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "unable to configure SWO UART pin: %s", esp_err_to_name(ret));
		goto out;
	}

	uart_config_t uart_config = {
		.baud_rate = baud_rate,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};
	ret = uart_driver_install(CONFIG_TRACE_SWO_UART_IDX, 4096, 256, 16, &uart_event_queue, 0);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "unable to install SWO UART driver: %s", esp_err_to_name(ret));
		goto out;
	}

	ret = uart_param_config(CONFIG_TRACE_SWO_UART_IDX, &uart_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "unable to configure SWO UART driver: %s", esp_err_to_name(ret));
		goto out;
	}

	const uart_intr_config_t uart_intr = {
		.intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M | UART_RXFIFO_TOUT_INT_ENA_M | UART_FRM_ERR_INT_ENA_M |
				    UART_RXFIFO_OVF_INT_ENA_M,
		.rxfifo_full_thresh = 80,
		.rx_timeout_thresh = 2,
		.txfifo_empty_intr_thresh = 10,
	};

	ret = uart_intr_config(CONFIG_TRACE_SWO_UART_IDX, &uart_intr);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "unable to configure UART interrupt: %s", esp_err_to_name(ret));
		goto out;
	}

	if (default_baud == 0) {
		ESP_LOGI(TAG, "baud rate not specified, initiating autobaud detection");
		baud_rate = uart_baud_detect(CONFIG_TRACE_SWO_UART_IDX, 20, 10);
		ESP_LOGI(TAG, "baud rate detected as %d", baud_rate);
	}

	ESP_LOGI(TAG, "UART driver started with baud rate of %d, beginning reception...", baud_rate);

	while (1) {
		uart_event_t evt;

		if (xQueueReceive(uart_event_queue, (void *)&evt, 100)) {
			if (evt.type == UART_FIFO_OVF) {
				// uart_overrun_cnt++;
			}
			if (evt.type == UART_FRAME_ERR) {
				// uart_errors++;
			}
			if (evt.type == UART_BUFFER_FULL) {
				// uart_queue_full_cnt++;
			}

			if (evt.type == UART_TERMINATE) {
				break;
			}

			if (baud_rate == -1 || evt.type == UART_RECALCULATE_BAUD) {
				esp_err_t ret = uart_set_pin(CONFIG_TRACE_SWO_UART_IDX, UART_PIN_NO_CHANGE,
							     CONFIG_TDO_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
				if (ret != ESP_OK) {
					ESP_LOGE(TAG, "unable to configure SWO UART pin: %s", esp_err_to_name(ret));
					goto out;
				}

				if ((evt.size != 0) && (evt.type == UART_RECALCULATE_BAUD)) {
					baud_rate = evt.size;
					ESP_LOGI(TAG, "setting baud rate to %d", baud_rate);
					uart_set_baudrate(CONFIG_TRACE_SWO_UART_IDX, baud_rate);
				} else {
					ESP_LOGD(TAG, "detecting baud rate");
					baud_rate = uart_baud_detect(CONFIG_TRACE_SWO_UART_IDX, 20, 50);
					ESP_LOGI(TAG, "baud rate detected as %d", baud_rate);
				}
			}

			bufpos = uart_read_bytes(CONFIG_TRACE_SWO_UART_IDX, &buf[bufpos], sizeof(buf) - bufpos, 0);

			if (bufpos > 0) {
				char logstr[bufpos * 3 + 1];
				memset(logstr, 0, sizeof(logstr));
				int j;
				for (j = 0; j < bufpos; j++) {
					sprintf(logstr + (j * 3), " %02x", buf[j]);
				}
				ESP_LOGI(TAG, "uart has rx %d bytes: %s", bufpos, logstr);
				// uart_rx_count += bufpos;
				// http_term_broadcast_data(buf, bufpos);

				bufpos = 0;
			} // if(bufpos)
		} else if (baud_rate == -1) {
			esp_err_t ret = uart_set_pin(CONFIG_TRACE_SWO_UART_IDX, UART_PIN_NO_CHANGE, CONFIG_TDO_GPIO,
						     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
			if (ret != ESP_OK) {
				ESP_LOGE(TAG, "unable to configure SWO UART pin: %s", esp_err_to_name(ret));
				goto out;
			}

			ESP_LOGD(TAG, "detecting baud rate");
			baud_rate = uart_baud_detect(CONFIG_TRACE_SWO_UART_IDX, 20, 50);
			ESP_LOGI(TAG, "baud rate detected as %d", baud_rate);
		}
	}

out:
	uart_driver_delete(CONFIG_TRACE_SWO_UART_IDX);
	rx_pid = NULL;
	vTaskDelete(NULL);
}

char *serial_no_read(char *s)
{
	uint64_t chipid;
	esp_read_mac((uint8_t *)&chipid, ESP_MAC_WIFI_SOFTAP);
	memset(s, 0, DFU_SERIAL_LENGTH);
	snprintf(s, DFU_SERIAL_LENGTH - 1, "FP-%08X", (uint32_t)chipid);
	return s;
}

void traceswo_deinit(void)
{
	if (rx_pid) {
		uart_event_t msg;
		msg.type = UART_TERMINATE;
		xQueueSend(uart_event_queue, &msg, portMAX_DELAY);

		while (rx_pid != NULL) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
}
void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask)
{
	if (!rx_pid) {
		ESP_LOGI(TAG, "initializing traceswo");
		// xTaskCreate(swo_uart_rx_task, "swo_rx_task", 2048, (void *)baudrate, 10, &rx_pid);
		xTaskCreatePinnedToCore(swo_uart_rx_task, (const char *)"swo_rx_task", 2048, (void *)baudrate, 10,
					&rx_pid, tskNO_AFFINITY);
	} else {
		ESP_LOGI(TAG, "traceswo already initialized, reinitializing...");
		traceswo_baud(baudrate);
	}
}

void traceswo_baud(unsigned int baud)
{
	uart_event_t msg;
	msg.type = 0x1000;
	msg.size = baud;
	xQueueSend(uart_event_queue, &msg, portMAX_DELAY);
}

// #define FIFO_SIZE 256

// /* RX Fifo buffer */
// static volatile uint8_t buf_rx[FIFO_SIZE];
// /* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
// static volatile uint32_t buf_rx_in = 0;
// /* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
// static volatile uint32_t buf_rx_out = 0;

// void trace_buf_push(void)
// {
// 	size_t len;

// 	if (buf_rx_in == buf_rx_out) {
// 		return;
// 	} else if (buf_rx_in > buf_rx_out) {
// 		len = buf_rx_in - buf_rx_out;
// 	} else {
// 		len = FIFO_SIZE - buf_rx_out;
// 	}

// 	if (len > 64) {
// 		len = 64;
// 	}

// 	if (usbd_ep_write_packet(usbdev, 0x85, (uint8_t *)&buf_rx[buf_rx_out], len) == len) {
// 		buf_rx_out += len;
// 		buf_rx_out %= FIFO_SIZE;
// 	}
// }

// void trace_buf_drain(usbd_device *dev, uint8_t ep)
// {
// 	(void) dev;
// 	(void) ep;
// 	trace_buf_push();
// }

// void trace_tick(void)
// {
// 	trace_buf_push();
// }

// void TRACEUART_ISR(void)
// {
// 	uint32_t flush = uart_is_interrupt_source(TRACEUART, UART_INT_RT);

// 	while (!uart_is_rx_fifo_empty(TRACEUART)) {
// 		uint32_t c = uart_recv(TRACEUART);

// 		/* If the next increment of rx_in would put it at the same point
// 		* as rx_out, the FIFO is considered full.
// 		*/
// 		if (((buf_rx_in + 1) % FIFO_SIZE) != buf_rx_out)
// 		{
// 			/* insert into FIFO */
// 			buf_rx[buf_rx_in++] = c;

// 			/* wrap out pointer */
// 			if (buf_rx_in >= FIFO_SIZE)
// 			{
// 				buf_rx_in = 0;
// 			}
// 		} else {
// 			flush = 1;
// 			break;
// 		}
// 	}

// 	if (flush) {
// 		/* advance fifo out pointer by amount written */
// 		trace_buf_push();
// 	}
// }
