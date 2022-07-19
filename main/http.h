/*
 * http.h
 *
 *  Created on: Dec 14, 2019
 *      Author: walmis
 */

#ifndef SRC_PLATFORMS_ESP32_HTTP_H_
#define SRC_PLATFORMS_ESP32_HTTP_H_

#include <esp_http_server.h>

/* send data to connected terminal websockets */
void http_term_broadcast_data(uint8_t *data, size_t len);
void http_debug_putc(uint8_t c, int flush);
void http_term_broadcast_rtt(uint8_t *data, size_t len);

/* start the http server */
httpd_handle_t webserver_start(void);

#endif /* SRC_PLATFORMS_ESP32_HTTP_H_ */
