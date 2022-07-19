#ifndef _FP_WEBSOCKET_H_
#define _FP_WEBSOCKET_H_

#include <stdint.h>

esp_err_t cgi_websocket(httpd_req_t *req);
void http_debug_putc(uint8_t c, int flush);
void http_term_broadcast_rtt(uint8_t *data, size_t len);
void http_term_broadcast_data(uint8_t *data, size_t len);

struct websocket_config;
extern const struct websocket_config debug_websocket;
extern const struct websocket_config uart_websocket;
extern const struct websocket_config rtt_websocket;

#endif /* _FP_WEBSOCKET_H_ */