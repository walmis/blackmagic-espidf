/*
 * http.h
 *
 *  Created on: Dec 14, 2019
 *      Author: walmis
 */

#ifndef SRC_PLATFORMS_ESP8266_HTTP_H_
#define SRC_PLATFORMS_ESP8266_HTTP_H_

/* send data to connected terminal websockets */
void http_term_broadcast_data(uint8_t* data, size_t len);
void http_debug_putc(char c, int flush);

/* start the http server */
void httpd_start();



#endif /* SRC_PLATFORMS_ESP8266_HTTP_H_ */
