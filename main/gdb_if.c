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

#define GDB_TLS_INDEX 1

static int num_clients;

static xSemaphoreHandle gdb_mutex;
static int gdb_if_serv;
static target *cur_target;
static target *last_target;

struct gdb_wifi_instance {
	int sock;
	uint8_t buf[256];
	int bufsize;
	bool no_ack_mode;
	xTaskHandle pid;
};

int gdb_main_loop(struct target_controller *tc, bool in_syscall);

static void gdb_target_destroy_callback(struct target_controller *tc, target *t)
{
	(void)tc;
	if (cur_target == t)
		cur_target = NULL;

	if (last_target == t)
		last_target = NULL;
}

void gdb_target_printf(struct target_controller *tc, const char *fmt, va_list ap);

static struct target_controller gdb_controller = {
	.destroy_callback = gdb_target_destroy_callback,
	.printf = gdb_target_printf,

	.open = hostio_open,
	.close = hostio_close,
	.read = hostio_read,
	.write = hostio_write,
	.lseek = hostio_lseek,
	.rename = hostio_rename,
	.unlink = hostio_unlink,
	.stat = hostio_stat,
	.fstat = hostio_fstat,
	.gettimeofday = hostio_gettimeofday,
	.isatty = hostio_isatty,
	.system = hostio_system,
	.errno_ = TARGET_EUNKNOWN,
	.interrupted = false,
};

static void gdb_wifi_task(void *arg);
static void gdb_wifi_destroy(struct gdb_wifi_instance *instance);
static struct gdb_wifi_instance *new_gdb_wifi_instance(int sock);
// static int gdb_wifi_fileno(struct gdb_wifi_instance *instance);
unsigned char gdb_wifi_if_getchar_to(struct gdb_wifi_instance *instance, int timeout);
static unsigned char gdb_wifi_if_getchar(struct gdb_wifi_instance *instance);
static void gdb_wifi_if_putchar(struct gdb_wifi_instance *instance, unsigned char c, int flush);

struct exception **get_innermost_exception()
{
	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	// ESP_LOGI("EX", "exceptiongot ptr %p %p %p", ptr, ptr[0], ptr[1]);
	assert(ptr);
	return (struct exception **)&ptr[1];
}

static struct gdb_wifi_instance *new_gdb_wifi_instance(int sock)
{
	char name[32];
	snprintf(name, sizeof(name) - 1, "gdbc fd:%d", sock);

	struct gdb_wifi_instance *instance = malloc(sizeof(struct gdb_wifi_instance));
	if (!instance) {
		return 0;
	}

	memset(instance, 0, sizeof(*instance));
	instance->sock = sock;

	xTaskCreate(gdb_wifi_task, name, 5500, (void *)instance, 1, &instance->pid);
	return instance;
}

static void gdb_wifi_task(void *arg)
{
	struct gdb_wifi_instance *instance = (struct gdb_wifi_instance *)arg;

	void *tls[2] = {};
	tls[0] = arg;
	vTaskSetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX, tls); // used for exception handling

	ESP_LOGI("GDB_client", "Started task %d this:%p tlsp:%p mowner:%p", instance->sock, instance, tls,
		 xQueueGetMutexHolder(gdb_mutex));

	int opt = 0;
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
		TRY_CATCH(e, EXCEPTION_ALL)
		{
			gdb_main_loop(&gdb_controller, false);
		}
		if (e.type) {
			gdb_putpacketz("EFF");
			target_list_free();
			ESP_LOGI("Exception", "TARGET LOST e.type:%d", e.type);
			// morse("TARGET LOST.", 1);
		}
	}

	gdb_wifi_destroy(instance); // just in case
}

// static int gdb_wifi_fileno(struct gdb_wifi_instance *instance)
// {
// 	return instance->sock;
// }

static void gdb_wifi_destroy(struct gdb_wifi_instance *instance)
{
	ESP_LOGI("GDB_client", "destroy %d", instance->sock);
	num_clients--;

	// gdb_breaklock();
	close(instance->sock);

	xTaskHandle pid = instance->pid;
	free(instance);
	vTaskDelete(pid);
}

unsigned char gdb_wifi_if_getchar_to(struct gdb_wifi_instance *instance, int timeout)
{
	fd_set fds;
	struct timeval tv;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(instance->sock, &fds);

	if (select(instance->sock + 1, &fds, NULL, NULL, (timeout >= 0) ? &tv : NULL) > 0) {
		char c = gdb_wifi_if_getchar(instance);
		return c;
	}

	return 0xFF;
}

static unsigned char gdb_wifi_if_getchar(struct gdb_wifi_instance *instance)
{
	uint8_t tmp;

	int ret;
	ret = recv(instance->sock, &tmp, 1, 0);
	if (ret <= 0) {
		gdb_wifi_destroy(instance);
		// should not be reached
		return 0;
	}
	// if((tmp == '\x03') || (tmp == '\x04')) {
	// 	ESP_LOGW(__func__, "Got Interrupt request");
	// }
	return tmp;
}

static void gdb_wifi_if_putchar(struct gdb_wifi_instance *instance, unsigned char c, int flush)
{
	instance->buf[instance->bufsize++] = c;
	if (flush || (instance->bufsize == sizeof(instance->buf))) {
		if (instance->sock > 0) {
			int ret = send(instance->sock, instance->buf, instance->bufsize, 0);
			if (ret <= 0) {
				gdb_wifi_destroy(instance);
				// should not be reached
				return;
			}
		}
		instance->bufsize = 0;
	}
}

// static void gdb_wifi_putpacket(struct gdb_wifi_instance *instance, const char *packet, int size, char pktstart)
// {
// 	int i;
// 	unsigned char csum;
// 	unsigned char c;
// 	char xmit_csum[3];
// 	int tries = 0;

// 	if (packet == NULL) {
//     	ESP_LOGE(__func__, "gdb_putpacket packet is NULL");
// 	}
// 	do {
// 		DEBUG_GDB_WIRE("%s : ", __func__);
// 		csum = 0;
// 		gdb_wifi_if_putchar(instance, pktstart, 0);
// 		for(i = 0; i < size; i++) {
// 			c = packet[i];
// #if PC_HOSTED == 1
// 			if ((c >= 32) && (c < 127))
// 				DEBUG_GDB_WIRE("%c", c);
// 			else
// 				DEBUG_GDB_WIRE("\\x%02X", c);
// #endif
// 			if((c == '$') || (c == '#') || (c == '}') || (c == '*')) {
// 				gdb_wifi_if_putchar(instance, '}', 0);
// 				gdb_wifi_if_putchar(instance, c ^ 0x20, 0);
// 				csum += '}' + (c ^ 0x20);
// 			} else {
// 				gdb_wifi_if_putchar(instance, c, 0);
// 				csum += c;
// 			}
// 		}
// 		gdb_wifi_if_putchar(instance, '#', 0);
// 		snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
// 		gdb_wifi_if_putchar(instance, xmit_csum[0], 0);
// 		gdb_wifi_if_putchar(instance, xmit_csum[1], 1);
// 		DEBUG_GDB_WIRE("\n");
// 	} while(!instance->no_ack_mode && (gdb_wifi_if_getchar_to(instance, 2000) != '+') && (tries++ < 3));
// }

// static void gdb_wifi_putpacket_f(struct gdb_wifi_instance *instance, const char *fmt, ...)
// {
// 	va_list ap;
// 	char *buf;
// 	int size;

// 	va_start(ap, fmt);
// 	size = vasprintf(&buf, fmt, ap);
// 	gdb_wifi_putpacket(instance, buf, size, '$');
// 	free(buf);
// 	va_end(ap);
// }

// static void gdb_wifi_putnotifpacket_f(struct gdb_wifi_instance *instance, const char *fmt, ...)
// {
// 	va_list ap;
// 	char *buf;
// 	int size;

// 	va_start(ap, fmt);
// 	size = vasprintf(&buf, fmt, ap);
// 	gdb_wifi_putpacket(instance, buf, size, '%');
// 	free(buf);
// 	va_end(ap);
// }

// static void gdb_wifi_out(struct gdb_wifi_instance *instance, const char *buf)
// {
// 	char *hexdata;
// 	int i;

// 	hexdata = (char *)alloca((i = strlen(buf) * 2 + 1) + 1);
// 	hexdata[0] = 'O';
// 	hexify(hexdata + 1, buf, strlen(buf));
// 	gdb_wifi_putpacket_f(instance, hexdata, i);
// }

// static void gdb_wifi_voutf(struct gdb_wifi_instance *instance, const char *fmt, va_list ap)
// {
// 	char *buf;

// 	if (vasprintf(&buf, fmt, ap) < 0)
// 		return;
// 	gdb_wifi_out(instance, buf);
// 	free(buf);
// }

// static void gdb_wifi_outf(struct gdb_wifi_instance *instance, const char *fmt, ...)
// {
// 	va_list ap;

// 	va_start(ap, fmt);
// 	gdb_wifi_voutf(instance, fmt, ap);
// 	va_end(ap);
// }

void gdb_net_task(void *arg)
{
	struct sockaddr_in addr;
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

	ESP_LOGI("GDB", "Listening on TCP:%d", CONFIG_TCP_PORT);

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

// void gdb_out(const char *buf)
// {
// 	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
// 	assert(ptr);
// 	gdb_wifi_out(ptr[0], buf);
// }

// void gdb_voutf(const char *fmt, va_list ap)
// {
// 	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
// 	assert(ptr);
// 	gdb_wifi_voutf(ptr[0], fmt, ap);
// }

// void gdb_outf(const char *fmt, ...)
// {
// 	va_list ap;
// 	va_start(ap, fmt);
// 	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
// 	assert(ptr);
// 	gdb_wifi_voutf(ptr[0], fmt, ap);
// 	va_end(ap);
// }

// void gdb_putpacket(const char *packet, int size)
// {
// 	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
// 	assert(ptr);
// 	gdb_wifi_putpacket_f(ptr[0], packet, size);
// }

// void gdb_putpacket_f(const char *fmt, ...)
// {
// 	va_list ap;
// 	va_start(ap, fmt);
// 	void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
// 	assert(ptr);
// 	gdb_wifi_putpacket_f(ptr[0], fmt, ap);
// 	va_end(ap);
// }

void gdb_target_printf(struct target_controller *tc, const char *fmt, va_list ap)
{
	(void)tc;
	// void **ptr = (void **)pvTaskGetThreadLocalStoragePointer(NULL, GDB_TLS_INDEX);
	// assert(ptr);
	// gdb_wifi_voutf(ptr[0], fmt, ap);
	gdb_voutf(fmt, ap);
}