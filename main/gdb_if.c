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

/* This file implements a transparent channel over which the GDB Remote
 * Serial Debugging protocol is implemented.  This implementation for Linux
 * uses a TCP server on port 2022.
 */
#include <stdio.h>

#include <lwip/err.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>

#include "esp_log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "gdb_if.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "gdb_hostio.h"

#include "exception.h"
#include "general.h"
#include "hex_utils.h"
#include "target.h"

#include <string.h>
#include <assert.h>

#define GDB_TLS_INDEX     1
#define EXCEPTION_NETWORK 0x40

static int num_clients;

static QueueHandle_t gdb_mutex;

struct gdb_wifi_instance {
	int sock;
	uint8_t buf[256];
	int bufsize;
	bool no_ack_mode;
	bool is_shutting_down;
	TaskHandle_t pid;
};

void gdb_main(void);

struct exception **get_innermost_exception(void)
{
	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	assert(ptr);
	return (struct exception **)&ptr[1];
}

static void gdb_wifi_destroy(struct gdb_wifi_instance *instance)
{
	ESP_LOGI("gdb", "destroy %d", instance->sock);
	num_clients--;

	close(instance->sock);

	TaskHandle_t pid = instance->pid;
	free(instance);
	vTaskDelete(pid);
}

static void gdb_wifi_task(void *arg)
{
	struct gdb_wifi_instance *instance = (struct gdb_wifi_instance *)arg;

	void *tls[2] = {};
	tls[0] = arg;
	vTaskSetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX, tls); // used for exception handling

	ESP_LOGI("gdb", "Started task %d this:%p tlsp:%p mowner:%p", instance->sock, instance, tls,
		xQueueGetMutexHolder(gdb_mutex));

	int opt = 1;
	setsockopt(instance->sock, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt));
	opt = 1; /* SO_KEEPALIVE */
	setsockopt(instance->sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&opt, sizeof(opt));
	opt = 3; /* s TCP_KEEPIDLE */
	setsockopt(instance->sock, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&opt, sizeof(opt));
	opt = 1; /* s TCP_KEEPINTVL */
	setsockopt(instance->sock, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&opt, sizeof(opt));
	opt = 3; /* TCP_KEEPCNT */
	setsockopt(instance->sock, IPPROTO_TCP, TCP_KEEPCNT, (void *)&opt, sizeof(opt));
	opt = 1;

	num_clients++;

	while (true) {
		struct exception e;
		TRY_CATCH (e, EXCEPTION_ALL) {
			gdb_main();
		}
		if (e.type == EXCEPTION_NETWORK) {
			ESP_LOGE("exception", "network exception -- exiting: %s", e.msg);
			target_list_free();
			break;
		}
		if (e.type) {
			gdb_putpacketz("EFF");
			target_list_free();
			ESP_LOGI("exception", "TARGET LOST e.type:%" PRId32, e.type);
			// morse("TARGET LOST.", 1);
		}
	}

	gdb_wifi_destroy(instance);
}

static unsigned char gdb_wifi_if_getchar(struct gdb_wifi_instance *instance)
{
	uint8_t tmp;
	int ret;

	if (instance->is_shutting_down) {
		return 0;
	}

	ret = recv(instance->sock, &tmp, 1, 0);
	if (ret <= 0) {
		instance->is_shutting_down = true;
		raise_exception(EXCEPTION_NETWORK, "error on getchar");
		// should not be reached
		return 0;
	}
	return tmp;
}

static unsigned char gdb_wifi_if_getchar_to(struct gdb_wifi_instance *instance, int timeout)
{
	if (instance->is_shutting_down) {
		return 0xff;
	}
	// Optimization for "MSG_PEEK"
	// if (timeout == 0) {
	// 	uint8_t tmp;
	// 	int ret = recv(instance->sock, &tmp, 1, MSG_DONTWAIT);
	// 	if (ret == 1) {
	// 		return tmp;
	// 	}
	// 	return 0xFF;
	// }
	fd_set fds;
	struct timeval tv;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(instance->sock, &fds);

	int ret = select(instance->sock + 1, &fds, NULL, NULL, (timeout >= 0) ? &tv : NULL);
	if (ret > 0) {
		char c = gdb_wifi_if_getchar(instance);
		return c;
	}

	if (ret < 0) {
		instance->is_shutting_down = true;
		raise_exception(EXCEPTION_NETWORK, "error on getchar_to");
	}
	return 0xFF;
}

static void gdb_wifi_if_putchar(struct gdb_wifi_instance *instance, unsigned char c, int flush)
{
	if (instance->is_shutting_down) {
		return;
	}

	instance->buf[instance->bufsize++] = c;
	if (flush || (instance->bufsize == sizeof(instance->buf))) {
		if (instance->sock > 0) {
			int ret = send(instance->sock, instance->buf, instance->bufsize, 0);
			if (ret <= 0) {
				instance->is_shutting_down = true;
				raise_exception(EXCEPTION_NETWORK, "error on putchar");
				// should not be reached
				return;
			}
		}
		instance->bufsize = 0;
	}
}

static void new_gdb_wifi_instance(int sock)
{
	char name[CONFIG_FREERTOS_MAX_TASK_NAME_LEN];
	snprintf(name, sizeof(name) - 1, "gdbc fd:%" PRId16, sock);

	struct gdb_wifi_instance *instance = malloc(sizeof(struct gdb_wifi_instance));
	if (!instance) {
		return;
	}

	memset(instance, 0, sizeof(*instance));
	instance->sock = sock;

	// Create the task at the IDLE priority, to allow the idle thread to keep
	// the watchdog happy. This will get preemptively scheduled at the same
	// priority as the IDLE task.
	xTaskCreate(gdb_wifi_task, name, 5500, (void *)instance, tskIDLE_PRIORITY + 1, &instance->pid);
}

void gdb_net_task(void *arg)
{
	struct sockaddr_in addr;
	int gdb_if_serv;
	int opt;

	gdb_mutex = xSemaphoreCreateMutex();

	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFIG_TCP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	assert((gdb_if_serv = socket(PF_INET, SOCK_STREAM, 0)) != -1);
	opt = 1;
	assert(setsockopt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt)) != -1);
	assert(setsockopt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt)) != -1);

	assert(bind(gdb_if_serv, (struct sockaddr *)&addr, sizeof(addr)) != -1);
	assert(listen(gdb_if_serv, 1) != -1);

	ESP_LOGI("gdb", "Listening on TCP:%d", CONFIG_TCP_PORT);

	while (1) {
		int s = accept(gdb_if_serv, NULL, NULL);
		if (s > 0) {
			new_gdb_wifi_instance(s);
		}
	}
}

unsigned char gdb_if_getchar_to(int timeout)
{
	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	assert(ptr);
	return gdb_wifi_if_getchar_to(ptr[0], timeout);
}

unsigned char gdb_if_getchar(void)
{
	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	assert(ptr);
	return gdb_wifi_if_getchar(ptr[0]);
}

void gdb_if_putchar(unsigned char c, int flush)
{
	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	assert(ptr);
	gdb_wifi_if_putchar(ptr[0], c, flush);
}

void gdb_target_printf(struct target_controller *tc, const char *fmt, va_list ap)
{
	(void)tc;
	gdb_voutf(fmt, ap);
}