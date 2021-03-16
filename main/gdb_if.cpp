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


#   include "lwip/err.h"
#   include "lwip/sockets.h"
#   include "lwip/sys.h"
#   include "lwip/netdb.h"
#   include "lwip/dns.h"
#include "esp_log.h"

#include <timers.h>

extern "C" {
#include "exception.h"
#include "target.h"
}

#include <string.h>
#include <assert.h>

#include "gdb_if.hpp"

xSemaphoreHandle gdb_mutex;
static int gdb_if_serv;




class GDB_client : public GDB {
public:
	GDB_client(int sock) : sock(sock) {
		xTaskCreate([](void* arg) {
			GDB_client* _this = (GDB_client*)arg;
			_this->task();
		}, "gdbc", 6000, this, 1, &pid);
	}


	void task() {
		void* tls[2] = {};
		tls[0] = this;
		vTaskSetThreadLocalStoragePointer(0, 0, tls); //used for exception handling
		
		ESP_LOGI("GDB_client", "Started task %d this:%p tlsp:%p", sock, this, tls);

		int opt = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt));

		num_clients++;

		while (true) {
			volatile struct exception e;
			TRY_CATCH(e, EXCEPTION_ALL) {
				gdb_main();
			}
			if (e.type) {
				gdb_putpacketz("EFF");
				GDB_LOCK();
				target_list_free();
				//morse("TARGET LOST.", 1);
			}
		}

		destroy(); //just in case
	}
	
	int fileno() {
		return sock;
	}

private:
	~GDB_client() {
		ESP_LOGI("GDB_client", "delete %p", this);
	}

	void destroy() {
		ESP_LOGI("GDB_client", "destroy %d", sock);
		num_clients--;

		close(sock);

		xTimerPendFunctionCall([](void * _this, uint32_t ulParameter2) {
			GDB_client* c = (GDB_client*)_this;
			delete c;
		 }, this, 0, -1);
		vTaskDelete(pid);
	}

	unsigned char gdb_if_getchar_to(int timeout) {
		fd_set fds;
		struct timeval tv;

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		FD_ZERO(&fds);
		FD_SET(sock, &fds);

		if(select(sock+1, &fds, NULL, NULL, (timeout >= 0) ? &tv : NULL) > 0) {
			return gdb_if_getchar();
		}

		return 0xFF;
	}

	unsigned char gdb_if_getchar(void) {
		uint8_t tmp;

		int ret;
		ret = recv(sock, &tmp, 1, 0);
		if(ret <= 0) {
			destroy();
			//should not be reached
			return 0;
		}

		return tmp;
	}
    void gdb_if_putchar(unsigned char c, int flush) {
		buf[bufsize++] = c;
		if (flush || (bufsize == sizeof(buf))) {
			if (sock > 0) {
				int ret = send(sock, buf, bufsize, 0);
				if(ret <= 0) {
					destroy();
					//should not be reached
					return;
				}
			}
			bufsize = 0;
		}
	}

	uint8_t buf[256];
	int bufsize = 0;
	xTaskHandle pid;
	int sock;
};

extern "C" 
void gdb_net_task(void* arg) {
	struct sockaddr_in addr;
	int opt;

	gdb_mutex = xSemaphoreCreateRecursiveMutex();

	addr.sin_family = AF_INET;
	addr.sin_port = htons(2022);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	assert((gdb_if_serv = socket(PF_INET, SOCK_STREAM, 0)) != -1);
	opt = 1;
	assert(setsockopt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) != -1);
	assert(setsockopt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt)) != -1);

	assert(bind(gdb_if_serv, (sockaddr*)&addr, sizeof(addr)) != -1);
	assert(listen(gdb_if_serv, 1) != -1);

	ESP_LOGI("GDB", "Listening on TCP:2022\n");


	while(1) {
		int s = accept(gdb_if_serv, NULL, NULL);
		if(s > 0)
			new GDB_client(s);
	}

}

extern "C"
unsigned char gdb_if_getchar_to(int timeout) {
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];
	return _this->gdb_if_getchar_to(timeout);	
}

extern "C"
unsigned char gdb_if_getchar() {
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];
	return _this->gdb_if_getchar();	
}

extern "C"
void gdb_if_putchar(unsigned char c, int flush) {
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];
	_this->gdb_if_putchar(c, flush);	
}

#if 0
#include "general.h"
#include "gdb_if.h"

static int gdb_if_serv, gdb_if_conn;
static int gdb_if_udp;
static struct sockaddr_in udp_peer;

int gdb_if_init(void)
{
	struct sockaddr_in addr;
	int opt;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(2022);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	assert((gdb_if_serv = socket(PF_INET, SOCK_STREAM, 0)) != -1);
	opt = 1;
	assert(setsockopt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) != -1);
	assert(setsockopt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt)) != -1);

	assert(bind(gdb_if_serv, (void*)&addr, sizeof(addr)) != -1);
	assert(listen(gdb_if_serv, 1) != -1);

	gdb_if_conn = -1;

	DEBUG("GDB Listening on TCP:2022\n");


	gdb_if_udp = socket(PF_INET, SOCK_DGRAM, 0);
	addr.sin_port = htons(2023);
	bind(gdb_if_udp, (void*)&addr, sizeof(addr));
	DEBUG("GDB Listening on UDP:2023\n");

	return 0;
}

IRAM_ATTR
unsigned char gdb_if_getchar_to(int timeout)
{
	fd_set fds;
	struct timeval tv;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&fds);

	int maxfd = MAX(gdb_if_conn, MAX(gdb_if_udp, gdb_if_serv));

	if (gdb_if_conn == -1) {
		FD_SET(gdb_if_udp, &fds);
		FD_SET(gdb_if_serv, &fds);
	} else {
		FD_SET(gdb_if_conn, &fds);
	}

	static uint8_t buf[TCP_MSS];
	static int bufpos;
	static int buflen;

	if(bufpos < buflen) {
		return buf[bufpos++];
	}

	if(select(maxfd+1, &fds, NULL, NULL, (timeout >= 0) ? &tv : NULL) > 0) {
		if(FD_ISSET(gdb_if_serv, &fds)) {
			if(gdb_if_conn <= 0) {
				gdb_if_conn = accept(gdb_if_serv, NULL, NULL);
				int opt = 1;
				setsockopt(gdb_if_conn, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt));
				ESP_LOGI(__func__, "Got connection\n");
			}
		}
		if(FD_ISSET(gdb_if_udp, &fds)) {
			socklen_t slen = sizeof(udp_peer);
			buflen = recvfrom(gdb_if_udp, buf, sizeof(buf), 0, (struct sockaddr*)&udp_peer, &slen);
			bufpos = 0;
			ESP_LOGI(__func__, "UDP recv %d\n", buflen);
			if(buflen > 0) {
				return buf[bufpos++];
			} else {
				buflen = 0;
			}
		}
		if(FD_ISSET(gdb_if_conn, &fds)) {
			buflen = recv(gdb_if_conn, buf, sizeof(buf), 0);
			bufpos = 0;

			if(buflen > 0) {
				return buf[bufpos++];
			} else {
			    close(gdb_if_conn);
				gdb_if_conn = -1;
				ESP_LOGE(__func__, "Dropped broken connection\n");
				/* Return '+' in case we were waiting for an ACK */
				return '+';
			}
		}
	}

	return -1;
}

unsigned char gdb_if_getchar(void)
{
	return gdb_if_getchar_to(-1);
}

void gdb_if_putchar(unsigned char c, int flush)
{
	static uint8_t buf[2048];
	static int bufsize = 0;

	buf[bufsize++] = c;
	if (flush || (bufsize == sizeof(buf))) {
		if (gdb_if_conn > 0) {
			send(gdb_if_conn, buf, bufsize, 0);
		}
		if(udp_peer.sin_addr.s_addr != 0) {
			sendto(gdb_if_udp, buf, bufsize, 0, (struct sockaddr*)&udp_peer, sizeof(udp_peer));
		}
		bufsize = 0;
	}

}
#endif