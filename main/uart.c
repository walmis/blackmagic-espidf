#include <stdint.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/periph_ctrl.h"
#include "driver/uart.h"
#include "freertos/semphr.h"
#include "hal/uart_hal.h"
#include "nvs_flash.h"
#include "soc/uart_reg.h"
#include "soc/uart_periph.h"

#include "sdkconfig.h"

#include "lwip/sockets.h"

#include "general.h"

#include "CBUF.h"
#include "http.h"
#include "tinyprintf.h"
#include "uart.h"

#if CONFIG_TARGET_UART_IDX == 0
#define TARGET_UART_DEV UART0
#define PERIPH_UART_MODULE PERIPH_UART0_MODULE
#define PERIPH_UART_IRQ ETS_UART0_INTR_SOURCE
#elif CONFIG_TARGET_UART_IDX == 1
#define TARGET_UART_DEV UART1
#define PERIPH_UART_MODULE PERIPH_UART1_MODULE
#define PERIPH_UART_IRQ ETS_UART1_INTR_SOURCE
#elif CONFIG_TARGET_UART_IDX == 2
#define TARGET_UART_DEV UART2
#define PERIPH_UART_MODULE PERIPH_UART2_MODULE
#define PERIPH_UART_IRQ ETS_UART2_INTR_SOURCE
#else
#error "No target UART defined"
#endif

#define UART_CONTEX_INIT_DEF(uart_num)                                                                                 \
	{                                                                                                              \
		.hal.dev = UART_LL_GET_HW(uart_num), .spinlock = portMUX_INITIALIZER_UNLOCKED, .hw_enabled = false,    \
	}

typedef struct {
	uart_hal_context_t hal; /*!< UART hal context*/
	portMUX_TYPE spinlock;
	bool hw_enabled;
} uart_context_t;

// static uart_obj_t *p_uart_obj[UART_NUM_MAX] = {0};

static uart_context_t uart_context[UART_NUM_MAX] = {
	UART_CONTEX_INIT_DEF(UART_NUM_0),
	UART_CONTEX_INIT_DEF(UART_NUM_1),
#if UART_NUM_MAX > 2
	UART_CONTEX_INIT_DEF(UART_NUM_2),
#endif
};

#define UART_ENTER_CRITICAL_ISR(mux) portENTER_CRITICAL_ISR(mux)
#define UART_EXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL_ISR(mux)
#define UART_ENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#define UART_EXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)

static struct sockaddr_in udp_peer_addr;
static int tcp_serv_sock;
static int udp_serv_sock;
static int tcp_client_sock = 0;
extern nvs_handle h_nvs_conf;

// UART statistics counters
uint32_t uart_overrun_cnt;
uint32_t uart_frame_error_cnt;
uint32_t uart_queue_full_cnt;
uint32_t uart_rx_count;
uint32_t uart_tx_count;

struct {
	volatile uint8_t m_get_idx;
	volatile uint8_t m_put_idx;
	uint8_t m_entry[256];
	SemaphoreHandle_t sem;
} dbg_msg_queue;

struct {
	volatile uint16_t m_get_idx;
	volatile uint16_t m_put_idx;
	uint8_t m_entry[16384];
	SemaphoreHandle_t sem;
} uart_msg_queue;

static void dbg_log_task(void *parameters)
{
	(void)parameters;
	while (1) {
		xSemaphoreTake(dbg_msg_queue.sem, portMAX_DELAY);

		while (!CBUF_IsEmpty(dbg_msg_queue)) {
			char c = CBUF_Pop(dbg_msg_queue);
			http_debug_putc(c, c == '\n' ? 1 : 0);
		}
	}
}

void debug_putc(char c, int flush)
{
	CBUF_Push(dbg_msg_queue, c);
	if (flush) {
		xSemaphoreGive(dbg_msg_queue.sem);
	}
}

static vprintf_like_t vprintf_orig = NULL;
static void putc_remote(void *ignored, char c)
{
	(void)ignored;
	if (c == '\n') {
		debug_putc('\r', 0);
	}

	debug_putc(c, c == '\n' ? 1 : 0);
}

int vprintf_remote(const char *fmt, va_list va)
{
	tfp_format(NULL, putc_remote, fmt, va);

	if (vprintf_orig) {
		vprintf_orig(fmt, va);
	}
	return 1;
}

void uart_dbg_install(void)
{
	CBUF_Init(dbg_msg_queue);
	dbg_msg_queue.sem = xSemaphoreCreateBinary();
	vprintf_orig = esp_log_set_vprintf(vprintf_remote);

	xTaskCreate(&dbg_log_task, "dbg_log_main", 2048, NULL, 4, NULL);
}

void IRAM_ATTR uart_write_all(const uint8_t *data, int len)
{
	while (len > 0) {
		while (!uart_ll_is_tx_idle(&TARGET_UART_DEV)) {
		}
		uint16_t fill_len = uart_ll_get_txfifo_len(&TARGET_UART_DEV);
		if (fill_len > len) {
			fill_len = len;
		}
		len -= fill_len;
		if (fill_len > 0) {
			uart_ll_write_txfifo(&TARGET_UART_DEV, data, fill_len);
		}
	}
}

static void IRAM_ATTR net_uart_task(void *params)
{
	tcp_serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	udp_serv_sock = socket(AF_INET, SOCK_DGRAM, 0);
	tcp_client_sock = 0;

	int ret;

	struct sockaddr_in saddr;
	saddr.sin_addr.s_addr = 0;
	saddr.sin_port = ntohs(23);
	saddr.sin_family = AF_INET;

	bind(tcp_serv_sock, (struct sockaddr *)&saddr, sizeof(saddr));

	saddr.sin_addr.s_addr = 0;
	saddr.sin_port = ntohs(2323);
	saddr.sin_family = AF_INET;
	bind(udp_serv_sock, (struct sockaddr *)&saddr, sizeof(saddr));
	listen(tcp_serv_sock, 1);

	while (1) {
		fd_set fds;
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		FD_ZERO(&fds);
		FD_SET(tcp_serv_sock, &fds);
		FD_SET(udp_serv_sock, &fds);
		if (tcp_client_sock)
			FD_SET(tcp_client_sock, &fds);

		int maxfd = MAX(tcp_serv_sock, MAX(udp_serv_sock, tcp_client_sock));
		// int maxfd = MAX(tcp_serv_sock, udp_serv_sock);

		if ((ret = select(maxfd + 1, &fds, NULL, NULL, &tv) > 0)) {
			if (FD_ISSET(tcp_serv_sock, &fds)) {
				tcp_client_sock = accept(tcp_serv_sock, 0, 0);
				if (tcp_client_sock < 0) {
					ESP_LOGE(__func__, "accept() failed");
					tcp_client_sock = 0;
				} else {
					ESP_LOGI(__func__, "accepted tcp connection");

					int opt = 1; /* SO_KEEPALIVE */
					setsockopt(tcp_client_sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&opt,
						   sizeof(opt));
					opt = 3; /* s TCP_KEEPIDLE */
					setsockopt(tcp_client_sock, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&opt,
						   sizeof(opt));
					opt = 1; /* s TCP_KEEPINTVL */
					setsockopt(tcp_client_sock, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&opt,
						   sizeof(opt));
					opt = 3; /* TCP_KEEPCNT */
					setsockopt(tcp_client_sock, IPPROTO_TCP, TCP_KEEPCNT, (void *)&opt,
						   sizeof(opt));
					opt = 0;
					setsockopt(tcp_client_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&opt,
						   sizeof(opt));
				}
			}
			uint8_t buf[128];

			if (FD_ISSET(udp_serv_sock, &fds)) {
				socklen_t slen = sizeof(udp_peer_addr);
				ret = recvfrom(udp_serv_sock, buf, sizeof(buf), 0, (struct sockaddr *)&udp_peer_addr,
					       &slen);
				if (ret > 0) {
					uart_write_all((const uint8_t *)buf, ret);
					uart_tx_count += ret;
				} else {
					ESP_LOGE(__func__, "udp recvfrom() failed");
				}
			}

			if (tcp_client_sock && FD_ISSET(tcp_client_sock, &fds)) {
				ret = recv(tcp_client_sock, buf, sizeof(buf), MSG_DONTWAIT);
				if (ret > 0) {
					uart_write_all((const uint8_t *)buf, ret);
					uart_tx_count += ret;
				} else {
					ESP_LOGE(__func__, "tcp client recv() failed (%s)", strerror(errno));
					close(tcp_client_sock);
					tcp_client_sock = 0;
				}
			}
		}
	}
}

uint32_t uart_rx_data_relay;

static void IRAM_ATTR uart_rx_task(void *parameters)
{
	(void)parameters;
	uint8_t buf[256];
	int ret;

	while (1) {
		// Consider adding a critical section here
		if (CBUF_IsEmpty(uart_msg_queue)) {
			xSemaphoreTake(uart_msg_queue.sem, portMAX_DELAY);
		}

		int count = 0;
		while ((count < sizeof(buf)) && !CBUF_IsEmpty(uart_msg_queue)) {
			char c = CBUF_Pop(uart_msg_queue);
			buf[count++] = c;
		}

		if (count <= 0) {
			continue;
		}

		// Broadcast the new buffer to all connected websocket clients
		http_term_broadcast_data(buf, count);

		// If there's a TCP client connected, send data there
		if (tcp_client_sock) {
			// ESP_LOGI(__func__, "tcp sending %d bytes (first byte: %02x (%c))", count, buf[0], buf[0]);
			ret = send(tcp_client_sock, buf, count, 0);
			if (ret > 0) {
				uart_rx_data_relay += ret;
			}
			// ESP_LOGI(__func__, "done sending, return value: %d, running count: %d bytes", ret,
			// 	 bytes_written);
			if (ret < 0) {
				ESP_LOGE(__func__, "tcp send() failed (%s)", strerror(errno));
				close(tcp_client_sock);
				tcp_client_sock = 0;
			} else if (ret != count) {
				ESP_LOGE(__func__, "tcp send() wanted to send %d bytes, but only sent %d", count, ret);
			}
		}

		// If there's a UDP client connected, broadcast to that host
		if (udp_peer_addr.sin_addr.s_addr) {
			ret = sendto(udp_serv_sock, buf, count, MSG_DONTWAIT, (struct sockaddr *)&udp_peer_addr,
				     sizeof(udp_peer_addr));
			if (ret < 0) {
				ESP_LOGE(__func__, "udp send() failed (%s)", strerror(errno));
				udp_peer_addr.sin_addr.s_addr = 0;
			}
		}
	}
}

#include "driver/gpio.h"
#define DEFAULT_UART_MASK UART_RXFIFO_FULL_INT_ENA_M | UART_FRM_ERR_INT_ENA_M | UART_RXFIFO_OVF_INT_ENA_M
static volatile int uart_mode = 0;
static const uint32_t bulk_mode_size = 126;
static void IRAM_ATTR uart_byte_mode(void)
{
	if (uart_mode == 1) {
		return;
	}
	gpio_set_level(CONFIG_LED_GPIO, 1);
	uart_mode = 1;
	uart_ll_set_rx_tout(&TARGET_UART_DEV, 0);
	uart_ll_set_rxfifo_full_thr(&TARGET_UART_DEV, 1);
	uart_ll_ena_intr_mask(&TARGET_UART_DEV, DEFAULT_UART_MASK);
	uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_INTR_RXFIFO_TOUT);
}

static void IRAM_ATTR uart_bulk_mode(void)
{
	if (uart_mode == 2) {
		return;
	}
	gpio_set_level(CONFIG_LED_GPIO, 0);
	uart_mode = 2;
	// Symbol length is:
	//      - 1 start bit
	//		- 8 data bits
	//		- 1 stop bit
	const uint32_t symbol_length = 10;

	// Switch back to byte mode if we haven't received a full buffer
	uart_ll_set_rx_tout(&TARGET_UART_DEV, symbol_length * (bulk_mode_size + 1));

	// Fire an interrupt if we get 16 bytes
	uart_ll_set_rxfifo_full_thr(&TARGET_UART_DEV, bulk_mode_size);

	// Enable the TOUT flag
	uart_ll_ena_intr_mask(&TARGET_UART_DEV, DEFAULT_UART_MASK | UART_RXFIFO_TOUT_INT_ENA);
	uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_INTR_RXFIFO_TOUT);
}

static void IRAM_ATTR console_isr(void *param)
{
	uart_context_t *uart = param;
	portBASE_TYPE HPTaskAwoken = pdFALSE;

	uint32_t uart_intr_status = uart_ll_get_intsts_mask(&TARGET_UART_DEV);
	int total_bytes_read = 0;

	while (uart_intr_status != 0) {
		int bytes_read = 0;

		if ((uart_intr_status & UART_INTR_RXFIFO_TOUT) || (uart_intr_status & UART_INTR_RXFIFO_FULL) ||
		    (uart_intr_status & UART_INTR_CMD_CHAR_DET)) {
			bytes_read = uart_ll_get_rxfifo_len(&TARGET_UART_DEV);
			total_bytes_read += bytes_read;
			if (uart_intr_status & UART_INTR_RXFIFO_TOUT) {
				// UART_ENTER_CRITICAL_ISR(&uart->spinlock);
				uart_byte_mode();
				// UART_EXIT_CRITICAL_ISR(&uart->spinlock);
			} else if (total_bytes_read >= 2) {
				// UART_ENTER_CRITICAL_ISR(&uart->spinlock);
				uart_bulk_mode();
				// UART_EXIT_CRITICAL_ISR(&uart->spinlock);
			}

			uart_rx_count += bytes_read;
			// UART_ENTER_CRITICAL_ISR(&uart->spinlock);
			while (bytes_read > 0) {
				bytes_read -= 1;
				char c = READ_PERI_REG(UART_FIFO_REG(CONFIG_TARGET_UART_IDX));
				if (CBUF_IsFull(uart_msg_queue)) {
					uart_queue_full_cnt += 1;
				} else {
					CBUF_Push(uart_msg_queue, c);
				}
			}
			// UART_EXIT_CRITICAL_ISR(&uart->spinlock);
			portBASE_TYPE shouldWake;
			xSemaphoreGiveFromISR(uart_msg_queue.sem, &shouldWake);
			if (shouldWake) {
				HPTaskAwoken = pdTRUE;
			}
			uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL);
		}

		else if (uart_intr_status & UART_INTR_RXFIFO_OVF) {
			// When fifo overflows, we reset the fifo.
			UART_ENTER_CRITICAL_ISR(&uart->spinlock);
			uart_ll_rxfifo_rst(&TARGET_UART_DEV);
			uart_overrun_cnt += 1;
			UART_EXIT_CRITICAL_ISR(&uart->spinlock);
			uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_INTR_RXFIFO_OVF);
		}

		else if (uart_intr_status & UART_INTR_FRAM_ERR) {
			uart_frame_error_cnt += 1;
			UART_ENTER_CRITICAL_ISR(&uart->spinlock);
			uart_ll_rxfifo_rst(&TARGET_UART_DEV);
			uart_overrun_cnt += 1;
			UART_EXIT_CRITICAL_ISR(&uart->spinlock);
			uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_INTR_FRAM_ERR);
		}

		else {
			uart_ll_clr_intsts_mask(&TARGET_UART_DEV, uart_intr_status);
		}

		uart_intr_status = uart_ll_get_intsts_mask(&TARGET_UART_DEV);
	}

	if (HPTaskAwoken == pdTRUE) {
		portYIELD_FROM_ISR();
	}
}

void uart_send_break(void)
{
	uart_wait_tx_done(CONFIG_TARGET_UART_IDX, 10);

	uint32_t baud;
	uart_get_baudrate(CONFIG_TARGET_UART_IDX, &baud);    // save current baudrate
	uart_set_baudrate(CONFIG_TARGET_UART_IDX, baud / 2); // set half the baudrate
	const uint8_t b = 0x00;
	uart_write_all(&b, 1);
	while (!uart_ll_is_tx_idle(&TARGET_UART_DEV)) {
	}
	uart_wait_tx_done(CONFIG_TARGET_UART_IDX, 10);
	uart_set_baudrate(CONFIG_TARGET_UART_IDX, baud); // restore baudrate
}

void uart_init(void)
{
#if !defined(CONFIG_TARGET_UART_NONE)
	ESP_LOGI(__func__, "configuring UART%d for target", CONFIG_TARGET_UART_IDX);

	CBUF_Init(uart_msg_queue);
	uart_msg_queue.sem = xSemaphoreCreateBinary();

	uint32_t baud = 115200;
	nvs_get_u32(h_nvs_conf, "uartbaud", &baud);

	uart_config_t uart_config;
	memset(&uart_config, 0, sizeof(uart_config));
	uart_config.baud_rate = baud;
	uart_config.data_bits = UART_DATA_8_BITS;
	uart_config.parity = UART_PARITY_DISABLE;
	uart_config.stop_bits = UART_STOP_BITS_1;
	uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

	ESP_ERROR_CHECK(uart_param_config(CONFIG_TARGET_UART_IDX, &uart_config));
	uart_set_baudrate(CONFIG_TARGET_UART_IDX, baud);
	ESP_ERROR_CHECK(uart_set_pin(CONFIG_TARGET_UART_IDX, CONFIG_UART_TX_GPIO, CONFIG_UART_RX_GPIO,
				     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	// The ESP32 ISR doesn't handle continuous streams of large amounts of data.
	// Remove their ISR and use our own.
	uart_ll_disable_intr_mask(&TARGET_UART_DEV, UART_LL_INTR_MASK);
	uart_ll_clr_intsts_mask(&TARGET_UART_DEV, UART_LL_INTR_MASK);
	static uart_isr_handle_t console_isr_handle;
	ESP_ERROR_CHECK(esp_intr_alloc(PERIPH_UART_IRQ, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1, console_isr,
				       &uart_context[CONFIG_TARGET_UART_IDX], &console_isr_handle));

	const uart_intr_config_t uart_intr = {
		.intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M | UART_FRM_ERR_INT_ENA_M | UART_RXFIFO_OVF_INT_ENA_M
		/*|UART_TXFIFO_EMPTY_INT_ENA_M*/,
		.rx_timeout_thresh = 2,
		.txfifo_empty_intr_thresh = 1,
		.rxfifo_full_thresh = 1,
	};
	ESP_ERROR_CHECK(uart_intr_config(CONFIG_TARGET_UART_IDX, &uart_intr));
	uart_byte_mode();

	uart_set_wakeup_threshold(CONFIG_TARGET_UART_IDX, 3);
	esp_sleep_enable_uart_wakeup(CONFIG_TARGET_UART_IDX);

	// Start UART tasks
	xTaskCreate(uart_rx_task, "uart_rx_task", TCP_MSS + 2048, NULL, 1, NULL);
	xTaskCreate(net_uart_task, "net_uart_task", TCP_MSS + 4096, NULL, 1, NULL);

#endif
}