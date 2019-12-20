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

#include <assert.h>
#include <sys/time.h>
#include <sys/unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "dhcpserver/dhcpserver.h"
//#include <sysparam.h>
#include "http.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/api.h"
#include "lwip/tcp.h"

#include "rom/ets_sys.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp8266/uart_register.h"

#include "ota-tftp.h"

#define ACCESS_POINT_MODE
#define AP_SSID	 "blackmagic"
#define AP_PSK	 "helloworld"

nvs_handle h_nvs_conf;

uint32_t uart_overrun_cnt;
uint32_t uart_errors;
uint32_t uart_queue_full_cnt;
uint32_t uart_rx_count;
uint32_t uart_tx_count;

static struct netconn *uart_client_sock;
static struct netconn *uart_serv_sock;
static int uart_sockerr;
static xQueueHandle event_queue;
static int client_sock_pending_bytes;
static int clients_pending;

//#define __IOMUX(x) IOMUX_GPIO ## x ##_FUNC_GPIO
//#define _IOMUX(x)  __IOMUX(x)

void platform_init() {
#ifdef USE_GPIO2_UART
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_UART1_TXD_BK);
#else
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
#endif

  //gpio_set_iomux_function(SWDIO_PIN, _IOMUX(SWDIO_PIN));
  //gpio_set_iomux_function(SWCLK_PIN, _IOMUX(SWCLK_PIN));

  gpio_clear(_, SWCLK_PIN);
  gpio_clear(_, SWDIO_PIN);


  gpio_enable(SWCLK_PIN, GPIO_OUTPUT);
  gpio_enable(SWDIO_PIN, GPIO_OUTPUT);

  assert(gdb_if_init() == 0);
}

void platform_buffer_flush(void) {
  ;
}

void platform_srst_set_val(bool assert) {
  (void) assert;
}

bool platform_srst_get_val(void) {
  return false;
}

const char*
platform_target_voltage(void) {
  static char voltage[16];
  int vdd = esp_wifi_get_vdd33();
  sprintf(voltage, "%dmV", vdd);
  return voltage;
}

uint32_t platform_time_ms(void) {
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

#define vTaskDelayMs(ms)	vTaskDelay((ms)/portTICK_PERIOD_MS)

void platform_delay(uint32_t ms) {
  vTaskDelayMs(ms);
}

int platform_hwversion(void) {
  return 0;
}

#define EV_NETNEWDATA 0xFF
#define EV_NETNEWCONN 0xFE
#define EV_NETERR 0xFD

void netconn_serv_cb(struct netconn *nc, enum netconn_evt evt, u16_t len) {
  DEBUG("evt %d len %d\n", evt, len);

  if (evt == NETCONN_EVT_RCVPLUS)
  {
    clients_pending++;
    uart_event_t evt;
    evt.type = EV_NETNEWCONN;
    evt.size = len;
    xQueueSend(event_queue, (void*)&evt, 0);

  }
  if (evt == NETCONN_EVT_RCVMINUS)
  {
    clients_pending--;
  }
}

void netconn_cb(struct netconn *nc, enum netconn_evt evt, u16_t len) {
  //printf("evt %d len %d\n", evt, len);

  if (evt == NETCONN_EVT_RCVPLUS) {
    client_sock_pending_bytes += len;
    if (len == 0) {
      uart_sockerr = 1;
      client_sock_pending_bytes = 0;
    }

    uart_event_t evt;
    evt.type = EV_NETNEWDATA;
    evt.size = len;
    xQueueSend(event_queue, (void*)&evt, 0);

  } else if (evt == NETCONN_EVT_RCVMINUS) {
    client_sock_pending_bytes -= len;
  }

  else if (evt == NETCONN_EVT_ERROR) {
    DEBUG("NETCONN_EVT_ERROR\n");

    uart_sockerr = 1;
    client_sock_pending_bytes = 0;
    uart_event_t evt;
    evt.type = EV_NETERR;
    evt.size = 0;
    xQueueSend(event_queue, (void*)&evt, 0);
  }
}

void uart_rx_task(void *parameters) {
  static uint8_t buf[TCP_MSS];
  int bufpos = 0;

  uart_serv_sock = netconn_new(NETCONN_TCP);
  uart_serv_sock->callback = netconn_serv_cb;

  netconn_bind(uart_serv_sock, IP_ADDR_ANY, 23);
  netconn_listen(uart_serv_sock);


  ESP_LOGI(__func__, "Listening on :23\n");

  while (1) {
    int c;
    uart_event_t evt;

    if (xQueueReceive(event_queue, (void*)&evt, 10) != pdFALSE) {

      if(evt.type == UART_FIFO_OVF) {
        uart_overrun_cnt++;
      }
      if(evt.type == UART_FRAME_ERR) {
        uart_errors++;
      }
      if(evt.type == UART_BUFFER_FULL) {
        uart_queue_full_cnt++;
      }

      if (uart_sockerr && uart_client_sock) {
        uart_client_sock->callback = NULL;
        netconn_delete(uart_client_sock);
        uart_client_sock = 0;
        uart_sockerr = 0;
        DEBUG("Finish telnet connection\n");
      }

      if (!uart_client_sock && clients_pending) {
        netconn_accept(uart_serv_sock, &uart_client_sock);

        uart_sockerr = 0;
        client_sock_pending_bytes = 0;

        if (uart_client_sock) {
          tcp_nagle_disable(uart_client_sock->pcb.tcp);

          DEBUG("New telnet connection\n");
          uart_client_sock->callback = netconn_cb;
        }
      }

      size_t rxcount = 0;

      bufpos = uart_read_bytes(0, &buf[bufpos], sizeof(buf)-bufpos, 0);

      //DEBUG("uart rx:%d\n", bufpos);
      if(bufpos > 0) {
        uart_rx_count += 1;
        http_term_broadcast_data(buf, bufpos);

        if (uart_client_sock) {
          if (bufpos) {
            uart_get_buffered_data_len(0, &rxcount);
            netconn_write(uart_client_sock, buf, bufpos, NETCONN_COPY |
                (rxcount > 0 ? NETCONN_MORE : 0));
          }
        } // if (uart_client_sock)
        bufpos = 0;
      } //if(bufpos)

      if (uart_client_sock && client_sock_pending_bytes) {
        struct netbuf *nb = 0;
        err_t err = netconn_recv(uart_client_sock, &nb);

        if(err == ERR_OK) {
          char *data = 0;
          u16_t len = 0;
          netbuf_data(nb, (void*) &data, &len);
#ifdef USE_GPIO2_UART
          uart_write_bytes(1, data, len);
#else
          uart_write_bytes(0, data, len);
          uart_tx_count += len;
#endif
          netbuf_delete(nb);
        }
      }
    }
  }
}

void platform_set_baud(uint32_t baud) {
	uart_set_baudrate(0, baud);
	uart_set_baudrate(1, baud);
	nvs_set_u32(h_nvs_conf, "uartbaud", baud);
}

bool cmd_setbaud(target *t, int argc, const char **argv) {

	if (argc == 1) {
		uint32_t baud;
		uart_get_baudrate(0, &baud);
		gdb_outf("Current baud: %d\n", baud);
	}
	  if (argc == 2) {
		int baud = atoi(argv[1]);
		gdb_outf("Setting baud: %d\n", baud);

		platform_set_baud(baud);
	  }

  return 1;
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI("WIFI", "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        //xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI("WIFI", "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI("WIFI", "station:"MACSTR" leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGE("WIFI", "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        esp_wifi_connect();
        //xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    //wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .ap = {
            .password = AP_PSK,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK

        },
    };

    uint64_t chipid;
    esp_read_mac((uint8_t*)&chipid, ESP_MAC_WIFI_SOFTAP);

    wifi_config.ap.ssid_len = sprintf((char*)wifi_config.ap.ssid, AP_SSID "_%X", (uint32_t)chipid);


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

}
extern void main();




int putc_noop(int c) {
	return 0;
}

int putc_remote(int c) {
  return 0;
}

void app_main(void) {
  esp_log_set_putchar(putc_noop);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(nvs_open("config", NVS_READWRITE, &h_nvs_conf));


  wifi_init_softap();

  esp_log_set_putchar(putc_remote);

  uint32_t baud = 230400;
  nvs_get_u32(h_nvs_conf, "uartbaud", &baud);

  uart_set_baudrate(0, baud);
  uart_set_baudrate(1, baud);

  esp_wifi_set_ps (WIFI_PS_NONE);

  ESP_ERROR_CHECK(uart_driver_install(0, 4096, 256, 16, &event_queue, 0));

  uart_intr_config_t uart_intr = {
      .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
      | UART_RXFIFO_TOUT_INT_ENA_M
      | UART_FRM_ERR_INT_ENA_M
      | UART_RXFIFO_OVF_INT_ENA_M,
      .rxfifo_full_thresh = 80,
      .rx_timeout_thresh = 2,
      .txfifo_empty_intr_thresh = 10
  };
  uart_intr_config(0, &uart_intr);

  httpd_start();

  xTaskCreate(&main, "bmp_main", 8192, NULL, 2, NULL);
  xTaskCreate(&uart_rx_task, "io_main", 1500, NULL, 3, NULL);

  ota_tftp_init_server(69);


  ESP_LOGI(__func__, "Free heap %d\n", esp_get_free_heap_size());

}

#ifndef ENABLE_DEBUG
__attribute((used))
int ets_printf(const char *__restrict c, ...) { return 0; }

__attribute((used))
int printf(const char *__restrict c, ...) { return 0; }
#endif
