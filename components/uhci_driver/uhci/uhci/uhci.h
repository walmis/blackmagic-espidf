// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/xtensa_api.h"
#include "freertos/ringbuf.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "sdkconfig.h"

#define UHCI_NUM_0 (0)		 /*!< UHCI number 0 */
#define UHCI_NUM_1 (1)		 /*!< UHCI number 1 */
#define UHCI_NUM_MAX (2)	 /*!< UHCI number max */
#define UHCI_INTR_MASK (0x1ffff) //All interrupt mask

typedef enum {
	UHCI_EVENT_DATA = 0x1,
	UHCI_EVENT_EOF = 0x2,
	UHCI_EVENT_BUF_FULL = 0x4,
} uhci_event_type_t;

typedef struct {
	uint32_t type;
	int len;
} uhci_event_t;

int uart_dma_read(int uhci_num, uint8_t *addr, size_t read_size, TickType_t ticks_to_wait);

int uart_dma_write(int uhci_num, const uint8_t *pbuf, size_t wr);

esp_err_t uhci_driver_install(int uhci_num, size_t tx_buf_size, size_t rx_buf_size, int intr_flag,
			      QueueHandle_t *uart_queue, int queue_cnt);

esp_err_t uhci_attach_uart_port(int uhci_num, int uart_num, const uart_config_t *uart_config);
