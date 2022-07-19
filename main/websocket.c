#include <esp_http_server.h>
#include "lwip/sockets.h"
#include "websocket.h"
#include "driver/uart.h"

#include <esp_log.h>

static int debug_handles[8];
static int rtt_handles[8];
static int uart_handles[8];
extern httpd_handle_t http_daemon;

struct websocket_config {
	int *handles;
	uint32_t handle_count;
	void (*recv_cb)(httpd_handle_t handle, httpd_req_t *req, uint8_t *data, int len);
};

static void on_rtt_receive(httpd_handle_t server, httpd_req_t *req, uint8_t *data, int len)
{
	void rtt_append_data(const uint8_t *data, int len);
	rtt_append_data(data, len);
}

static void on_uart_receive(httpd_handle_t server, httpd_req_t *req, uint8_t *data, int len)
{
	uart_write_bytes(CONFIG_TARGET_UART_IDX, (const uint8_t *)data, len);
}

static void on_debug_receive(httpd_handle_t server, httpd_req_t *req, uint8_t *data, int len)
{
	ESP_LOGI(__func__, "received text from debug channel: %s", data);
}

const struct websocket_config debug_websocket = {
	.handles = debug_handles,
	.handle_count = sizeof(debug_handles) / sizeof(debug_handles[0]),
	.recv_cb = on_debug_receive,
};

const struct websocket_config uart_websocket = {
	.handles = uart_handles,
	.handle_count = sizeof(uart_handles) / sizeof(uart_handles[0]),
	.recv_cb = on_uart_receive,
};

const struct websocket_config rtt_websocket = {
	.handles = rtt_handles,
	.handle_count = sizeof(rtt_handles) / sizeof(rtt_handles[0]),
	.recv_cb = on_rtt_receive,
};

static void websocket_broadcast(httpd_handle_t hd, int *handles, int handle_max, uint8_t *buffer, size_t count)
{
	if (hd == NULL) {
		return;
	}
	httpd_ws_frame_t ws_pkt;
	memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

	int i;
	for (i = 0; i < handle_max; i++) {
		if (handles[i] == 0) {
			continue;
		}
		ws_pkt.payload = buffer;
		ws_pkt.len = count;
		ws_pkt.type = HTTPD_WS_TYPE_BINARY;
		int ret = httpd_ws_send_frame_async(hd, handles[i], &ws_pkt);
		if (ret != ESP_OK) {
			ESP_LOGE(__func__, "sockfd %d (index %d) is invalid! connection closed?", handles[i], i);
			handles[i] = 0;
		}
	}
}

void http_term_broadcast_data(uint8_t *data, size_t len)
{
	websocket_broadcast(http_daemon, uart_handles, sizeof(uart_handles) / sizeof(uart_handles[0]), data, len);
}

void http_term_broadcast_rtt(uint8_t *data, size_t len)
{
	websocket_broadcast(http_daemon, rtt_handles, sizeof(rtt_handles) / sizeof(rtt_handles[0]), data, len);
}

void http_debug_putc(uint8_t c, int flush)
{
	static uint8_t buf[256];
	static int bufsize = 0;

	buf[bufsize++] = c;
	if (flush || (bufsize == sizeof(buf))) {
		websocket_broadcast(http_daemon, debug_handles, sizeof(debug_handles) / sizeof(debug_handles[0]), buf, bufsize);
		bufsize = 0;
	}
}

esp_err_t cgi_websocket(httpd_req_t *req)
{
	esp_err_t ret;
	httpd_ws_frame_t ws_pkt;
	struct websocket_config *cfg = req->user_ctx;

	if (req->method == HTTP_GET) {
		int sockfd = httpd_req_to_sockfd(req);
		ESP_LOGI(__func__, "handshake done on %s, the new connection was opened with sockfd %d", req->uri, sockfd);
		int i;
		for (i = 0; i < cfg->handle_count; i++) {
			if (cfg->handles[i] == 0) {
				int opt = 1;
				setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt));
				cfg->handles[i] = sockfd;
				return ESP_OK;
			}
		}
		ESP_LOGE(__func__, "no free sockets to handle this connection");
		return ESP_OK;
	}

	memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
	ws_pkt.type = HTTPD_WS_TYPE_BINARY;
	ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
	if (ret != ESP_OK) {
		ESP_LOGE(__func__, "httpd_ws_recv_frame failed to get frame len with %d", ret);
		return ret;
	}

	// Keepalive packets are zero bytes
	if (ws_pkt.len <= 0) {
		// ESP_LOGE(__func__, "httpd_ws_recv_frame frame was zero bytes");
		return ESP_OK;
	}

	uint8_t *buffer = NULL;
	uint8_t stack_buffer[256];
	if (ws_pkt.len < sizeof(stack_buffer)) {
		ws_pkt.payload = stack_buffer;
	} else {
		buffer = malloc(ws_pkt.len);
		ws_pkt.payload = buffer;
	}
	ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
	if (ret != ESP_OK) {
		ESP_LOGE(__func__, "httpd_ws_recv_frame frame unable to receive data: %d", ret);
		if (buffer) {
			free(buffer);
		}
		return ret;
	}

	// ESP_LOGI(__func__, "Packet type: %d", ws_pkt.type);

	void (*recv_func)(httpd_handle_t handle, httpd_req_t * req, uint8_t * data, int len) = cfg->recv_cb;
	if (recv_func) {
		// ESP_LOGI(__func__, "going to call rect_func at %p", recv_func);
		recv_func(http_daemon, req, ws_pkt.payload, ws_pkt.len);
	} else {
		ESP_LOGE(__func__, "receive function was NULL");
	}

	if (buffer) {
		free(buffer);
	}

	return ESP_OK;
}
