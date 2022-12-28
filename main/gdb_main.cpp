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

/* This file implements the GDB Remote Serial Debugging protocol as
 * described in "Debugging with GDB" build from GDB source.
 *
 * Originally written for GDB 6.8, updated and tested with GDB 7.2.
 */
extern "C" {
#include "general.h"
#include "ctype.h"
#include "hex_utils.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "gdb_main.h"
#include "gdb_hostio.h"
#include "target.h"
#include "command.h"
#include "crc32.h"
#include "morse.h"
#include "exception.h"
#include "target/adiv5.h"
#include "target/cortexm.h"
#include "target/target_internal.h"
}

#include "gdb_if.hpp"
#include "task.h"

enum gdb_signal {
	GDB_SIGINT = 2,
	GDB_SIGTRAP = 5,
	GDB_SIGSEGV = 11,
	GDB_SIGLOST = 29,
};

#define ERROR_IF_NO_TARGET()	\
	GDB_LOCK(); if(!cur_target) { gdb_putpacketz("EFF"); break; }

static target_s *cur_target;
static target_s *last_target;

void gdb_target_destroy_callback(struct target_controller *tc, target *t)
{
		(void)tc;
	//TODO
	//gdb_voutf(fmt, ap);
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];

	ESP_LOGI(__func__, "gdb_target_destroy_callback %p\n", t);

	
	(void)tc;
	if (cur_target == t) {
		_this->gdb_put_notificationz("%Stop:W00");
		_this->gdb_out("You are now detached from the previous target.\n");
		_this->gdb_needs_detach_notify = true;

		cur_target = NULL;

	}

	if (last_target == t)
		last_target = NULL;
}

void gdb_target_printf(struct target_controller *tc,
                              const char *fmt, va_list ap)
{
	(void)tc;
	//TODO
	//gdb_voutf(fmt, ap);
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];
	_this->gdb_voutf(fmt, ap);
}

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
};

static bool cmd_reset(target *t, int argc, const char **argv) {
	if(argc == 2) {
		if(strcmp(argv[1], "init") == 0 || strcmp(argv[1], "halt") == 0) {
			ESP_LOGI(__func__, "resetting target:%s", argv[1]);
			target_halt_request(t);
			target_reset(t);
		} 
	} else {
		ESP_LOGI(__func__, "resetting target");
		target_reset(t);
	}

	return true;
}

static bool cmd_write_dp(target *t, int argc, const char **argv) {
	char* buf = "O.K.\n";
	int i;
	char hexdata[((i = strlen(buf)*2 + 1) + 1)];
	hexify(hexdata, buf, strlen(buf));
	gdb_putpacket(hexdata, i);

	uint32_t addr = strtoul(argv[1], 0, 16);
	uint32_t val = strtoul(argv[2], 0, 16);
	adiv5_access_port_s *ap = cortexm_ap(cur_target);
	//adiv5_dp_write(ap->dp, addr, val);

	return 255;
}

static bool cmd_read_ap(target *t, int argc, const char **argv) {
	if(!cur_target) {
		return false;
	}
	adiv5_access_port_s *ap = cortexm_ap(cur_target);
	uint32_t addr = strtoul(argv[1], 0, 16);
	ESP_LOGI(__func__, "addr: %x", addr);
	uint32_t reg = adiv5_dp_read(ap->dp, ADIV5_AP_BASE);

	char* buf;
	//O.K.:0xe00ff003
	asprintf(&buf, "O.K.:0x%08x\n", reg);
	int i;
	char hexdata[((i = strlen(buf)*2 + 1) + 1)];
	hexify(hexdata, buf, strlen(buf));
	gdb_putpacket(hexdata, i);
	free(buf);
	return 255;
}

int GDB::gdb_main_loop(struct target_controller *tc, bool in_syscall)
{
	
	{
		GDB_LOCK();

		if(!cur_target && !last_target) {
			ESP_LOGI("GDB", "Scanning SWD");
			int devs = -1;
			volatile struct exception e;
			TRY_CATCH (e, EXCEPTION_ALL) {
				devs = adiv5_swdp_scan(0);
				ESP_LOGI("GDB", "Found %d", devs);
				if(devs > 0) {
					cur_target = target_attach_n(1, &gdb_controller);
					if(cur_target) {
						static const command_s cmds[]  = { 
							{"reset", cmd_reset, "OpenOCD style target reset: reset [init halt run]"}, 
							{"WriteDP", cmd_write_dp, "STLINK helper"},
							{"ReadAP", cmd_read_ap, "STLINK helper"},

							{0,0,0} 
						};
						target_add_commands(cur_target, cmds, "Target");
					}
				}
			}
			switch (e.type) {
			case EXCEPTION_TIMEOUT:
				ESP_LOGI("GDB", "Timeout during scan. Is target stuck in WFI?\n");
				break;
			case EXCEPTION_ERROR:
				ESP_LOGI("GDB", "Exception: %s\n", e.msg);
				break;
			}
		}
	}

	int size;

	/* GDB protocol main loop */
	while(1) {
		SET_IDLE_STATE(1);
		size = gdb_getpacket(pbuf, BUF_SIZE);
		if(size == 0) {
			GDB_LOCK();
			if(run_state && cur_target) {
				target_addr_t watch;
				enum target_halt_reason reason = TARGET_HALT_RUNNING;
				reason = target_halt_poll(cur_target, &watch);

				if(non_stop) {
					// if(reason)
					// 	DEBUG_GDB("send state %d", reason);
					switch (reason) {
					case TARGET_HALT_ERROR:
						DEBUG_WARN("target_halt_poll = TARGET_HALT_ERROR");
						gdb_putnotifpacket_f("Stop:X%02X", GDB_SIGLOST);
						break;
					case TARGET_HALT_REQUEST:
						gdb_putnotifpacket_f("Stop:T%02Xthread:1;core:0;", GDB_SIGINT);
						break;
					case TARGET_HALT_WATCHPOINT:
						gdb_putnotifpacket_f("Stop:T%02Xthread:1;core:0;watch:%08X;", GDB_SIGTRAP, watch);
						break;
					case TARGET_HALT_FAULT:
						gdb_putnotifpacket_f("Stop:T%02Xthread:1;core:0;", GDB_SIGSEGV);
						break;
					case TARGET_HALT_RUNNING:
						break;
					default:
						gdb_putnotifpacket_f("Stop:T%02Xthread:1;core:0;", GDB_SIGTRAP);
						break;
					}		
					
				} else {
					switch (reason) {
					case TARGET_HALT_ERROR:
						gdb_putpacket_f("X%02X", GDB_SIGLOST);
						break;
					case TARGET_HALT_REQUEST:
						gdb_putpacket_f("T%02X", GDB_SIGINT);
						break;
					case TARGET_HALT_WATCHPOINT:
						gdb_putpacket_f("T%02Xwatch:%08X;", GDB_SIGTRAP, watch);
						break;
					case TARGET_HALT_FAULT:
						gdb_putpacket_f("T%02X", GDB_SIGSEGV);
						break;
					case TARGET_HALT_RUNNING:
						break;
					default:
						gdb_putpacket_f("T%02X", GDB_SIGTRAP);
						break;
					}
				}
				if(run_state && reason != TARGET_HALT_RUNNING) {
					run_state = false;
					single_step = false;
				} 

			}

			continue;
		} 
		SET_IDLE_STATE(0);
		switch(pbuf[0]) {
		/* Implementation of these is mandatory! */
		case 'g': { /* 'g': Read general registers */
			ERROR_IF_NO_TARGET();
			uint8_t gp_regs[target_regs_size(cur_target)];
			target_regs_read(cur_target, gp_regs);
			gdb_putpacket(hexify(pbuf, gp_regs, sizeof(gp_regs)), sizeof(gp_regs) * 2U);
			break;
			}
		case 'T': //thread select
			gdb_putpacketz("OK");
			break;
		case 'm': {	/* 'm addr,len': Read len bytes from addr */
			uint32_t addr, len;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "m%" SCNx32 ",%" SCNx32, &addr, &len);
			if (len > sizeof(pbuf) / 2) {
				gdb_putpacketz("E02");
				break;
			}
			DEBUG_GDB("m packet: addr = %" PRIx32 ", len = %" PRIx32 "\n",
					  addr, len);
			uint8_t mem[len];
			if (target_mem_read(cur_target, mem, addr, len)) {
				DEBUG_WARN("target_mem_read error");
				gdb_putpacketz("E01");
			} else
				gdb_putpacket(hexify(pbuf, mem, len), len*2);
			break;
			}
		case 'G': {	/* 'G XX': Write general registers */
			ERROR_IF_NO_TARGET();
			uint8_t arm_regs[target_regs_size(cur_target)];
			unhexify(arm_regs, &pbuf[1], sizeof(arm_regs));
			target_regs_write(cur_target, arm_regs);
			gdb_putpacketz("OK");
			break;
			}
		case 'H': //select thread: Hc0, Hg-1
			gdb_putpacketz("OK");
			break;
		case 'M': { /* 'M addr,len:XX': Write len bytes to addr */
			uint32_t addr, len;
			int hex;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "M%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &hex);
			if (len > (unsigned)(size - hex) / 2) {
				gdb_putpacketz("E02");
				break;
			}
			DEBUG_GDB("M packet: addr = %" PRIx32 ", len = %" PRIx32 "\n",
					  addr, len);
			uint8_t mem[len];
			unhexify(mem, pbuf + hex, len);
			if (target_mem_write(cur_target, addr, mem, len))
				gdb_putpacketz("E01");
			else
				gdb_putpacketz("OK");
			break;
			}
		case 'S':
			/* fall through */
		case 's':	/* 's [addr]': Single step [start at addr] */
			single_step = true;
			/* fall through */
		case 'C':
		/* fall through */
		case 'c': {	/* 'c [addr]': Continue [at addr] */
			GDB_LOCK();
			if(!cur_target) {
				gdb_putpacketz("X1D");
				break;
			}
			target_halt_resume(cur_target, single_step);
			run_state = true;
			SET_RUN_STATE(1);
			break;
		}
		case 0x03: {
			{
				DEBUG_GDB("Interrupt :%d", pbuf[0]);
				GDB_LOCK();
				run_state = true;
				target_halt_request(cur_target);
			}
			break;
		}
		case '?': {	/* '?': Request reason for target halt */
			/* This packet isn't documented as being mandatory,
			 * but GDB doesn't work without it. */
			target_addr_t watch;
			enum target_halt_reason reason = TARGET_HALT_RUNNING;
			GDB_LOCK();

			if(!cur_target) {
				/* Report "target exited" if no target */
				gdb_putpacketz("W00");
				break;
			}

			/* Wait for target halt */
			// while(!(reason) && run_state) {
				//ESP_LOGI("?", "Wait halt %d", reason);
				{
					if(!cur_target) {
						/* Report "target exited" if no target */
						gdb_putpacketz("W00");
						break;
					}
					reason = target_halt_poll(cur_target, &watch);
				}
				// unsigned char c = gdb_if_getchar_to(20);
				// if((c == '\x03') || (c == '\x04')) {
					// GDB_LOCK();
					// ESP_LOGW(__func__, "Interrupt request");

					// target_halt_request(cur_target);
				// }
			// }
			ESP_LOGW("?", "halted %d", reason);
			SET_RUN_STATE(0);

			/* Translate reason to GDB signal */
			switch (reason) {
			case TARGET_HALT_ERROR:
				gdb_putpacket_f("X%02X", GDB_SIGLOST);
				morse("TARGET LOST.", true);
				break;
			case TARGET_HALT_REQUEST:
				gdb_putpacket_f("T%02Xthread:1;core:0;", GDB_SIGINT);
				break;
			case TARGET_HALT_WATCHPOINT:
				gdb_putpacket_f("T%02Xwatch:%08X;", GDB_SIGTRAP, watch);
				break;
			case TARGET_HALT_FAULT:
				gdb_putpacket_f("T%02Xthread:1;core:0;", GDB_SIGSEGV);
				break;
			case TARGET_HALT_RUNNING:
				gdb_putpacket_f("T%02Xthread:1;core:0;", 0);
				break;
			default:
				gdb_putpacket_f("T%02Xthread:1;core:0;", GDB_SIGTRAP);
			}
			break;
			}

		/* Optional GDB packet support */
		case 'p': { /* Read single register */
			ERROR_IF_NO_TARGET();
			uint32_t reg;
			sscanf(pbuf, "p%" SCNx32, &reg);
			uint8_t val[8];
			size_t s = target_reg_read(cur_target, reg, val, sizeof(val));
			if (s > 0) {
				gdb_putpacket(hexify(pbuf, val, s), s * 2);
			} else {
				gdb_putpacketz("EFF");
			}
			break;
			}
		case 'P': { /* Write single register */
			ERROR_IF_NO_TARGET();
			uint32_t reg;
			int n;
			sscanf(pbuf, "P%" SCNx32 "=%n", &reg, &n);
			uint8_t val[strlen(&pbuf[n])/2];
			unhexify(val, pbuf + n, sizeof(val));
			if (target_reg_write(cur_target, reg, val, sizeof(val)) > 0) {
				gdb_putpacketz("OK");
			} else {
				gdb_putpacketz("EFF");
			}
			break;
			}

		case 'F':	/* Semihosting call finished */
			if (in_syscall) {
				return hostio_reply(tc, pbuf, size);
			} else {
				DEBUG_GDB("*** F packet when not in syscall! '%s'\n", pbuf);
				gdb_putpacketz("");
			}
			break;

		case '!':	/* Enable Extended GDB Protocol. */
			/* This doesn't do anything, we support the extended
			 * protocol anyway, but GDB will never send us a 'R'
			 * packet unless we answer 'OK' here.
			 */
			gdb_putpacketz("OK");
			break;

		case 0x04:
		case 'D': {	/* GDB 'detach' command. */
			GDB_LOCK();
			if(cur_target) {
				SET_RUN_STATE(1);
				target_detach(cur_target);
			}
			last_target = cur_target;
			cur_target = NULL;
			gdb_putpacketz("OK");
			break;
		}
		case 'k': {	/* Kill the target */
			GDB_LOCK();
			if(cur_target) {
				target_reset(cur_target);
				target_detach(cur_target);
				last_target = cur_target;
				cur_target = NULL;
			}
			break;
		}
		case 'r':	/* Reset the target system */
		case 'R':	/* Restart the target program */
		{
			GDB_LOCK();
			if(cur_target)
				target_reset(cur_target);
			else if(last_target) {
				cur_target = target_attach(last_target,
						           &gdb_controller);
				target_reset(cur_target);
			}
		}
			break;

		case 'X': { /* 'X addr,len:XX': Write binary data to addr */
			uint32_t addr, len;
			int bin;
			ERROR_IF_NO_TARGET();
			sscanf(pbuf, "X%" SCNx32 ",%" SCNx32 ":%n", &addr, &len, &bin);
			if (len > (unsigned)(size - bin)) {
				gdb_putpacketz("E02");
				break;
			}
			DEBUG_GDB("X packet: addr = %" PRIx32 ", len = %" PRIx32 "\n",
					  addr, len);
		    uint8_t alignas(4) data[len] ;
		    memcpy(data, pbuf+bin, len);
			if (target_mem_write(cur_target, addr, data, len))
				gdb_putpacketz("E01");
			else
				gdb_putpacketz("OK");
			break;
			}

		case 'q':	/* General query packet */
			handle_q_packet(pbuf, size);
			break;

		case 'v':	/* General query packet */
			handle_v_packet(pbuf, size);
			break;

		/* These packet implement hardware break-/watchpoints */
		case 'Z':	/* Z type,addr,len: Set breakpoint packet */
		case 'z': {	/* z type,addr,len: Clear breakpoint packet */
			ERROR_IF_NO_TARGET();
			handle_z_packet(pbuf, size);
			break;
		}
		default: 	{
			int val;
			if (!strcmp(pbuf, "QStartNoAckMode")) {
				no_ack_mode = true;
				gdb_putpacketz("OK");
			} else if (sscanf(pbuf, "QNonStop:%d", &val) == 1) {
				non_stop = val;
				gdb_putpacketz("OK");
			} else {
				DEBUG_GDB("*** Unsupported packet: %s\n", pbuf);
				gdb_putpacketz("");
			}
		}
		}
	}
}

void
GDB::handle_q_string_reply(const char *str, const char *param)
{
	unsigned long addr, len;

	if (sscanf(param, "%08lx,%08lx", &addr, &len) != 2) {
		gdb_putpacketz("E01");
		return;
	}
	if (addr < strlen (str)) {
		char reply[len+2];
		reply[0] = 'm';
		strncpy (reply + 1, &str[addr], len);
		if(len > strlen(&str[addr]))
			len = strlen(&str[addr]);
		gdb_putpacket(reply, len + 1);
	} else if (addr == strlen (str)) {
		gdb_putpacketz("l");
	} else
		gdb_putpacketz("E01");
}

void
GDB::handle_q_packet(char *packet, int len)
{
	uint32_t addr, alen;

	if(!strncmp(packet, "qRcmd,", 6)) {
		char *data;
		int datalen;
		GDB_LOCK();
		/* calculate size and allocate buffer for command */
		datalen = (len - 6) / 2;
		data = (char*)alloca(datalen+1);
		/* dehexify command */
		unhexify(data, packet+6, datalen);
		data[datalen] = 0;	/* add terminating null */
		ESP_LOGI("CMD", "%s", data);

		if(!strncmp(data, "reset", 5)) {
			if(cur_target) {
				ESP_LOGI(__func__, "resetting target");
				target_reset(cur_target);
				gdb_putpacketz("OK");
			} else {
			gdb_putpacketz("E");
			}
			return;
		}

		int c = command_process(cur_target, data);
		if(!strcmp(data, "ReadAP") || !strcmp(data, "WriteDP")) {
			return;
		}
		if(c < 0)
			gdb_putpacketz("");
		else if(c == 0)
			gdb_putpacketz("OK");
		else if(c == 255) {}
		else
			gdb_putpacketz("E");

	} else if (!strncmp (packet, "qSupported", 10)) {
		/* Query supported protocol features */
		gdb_putpacket_f("PacketSize=%X;qXfer:memory-map:read+;qXfer:features:read+;QNonStop+;QStartNoAckMode+"/*;qXfer:threads:read+"*/, BUF_SIZE);
	// } else if (strncmp (packet, "qXfer:threads:read::", 20) == 0) {
	// 	gdb_putpacket_f("l<?xml version=\"1.0\"?><threads><thread id=\"1\" core=\"0\" name=\"main\"></thread></threads>");
	} else if (strncmp (packet, "qAttached", 9) == 0) {
		gdb_putpacket_f("%d", !!cur_target);

	} else if (strncmp (packet, "qXfer:memory-map:read::", 23) == 0) {
		GDB_LOCK();
		/* Read target XML memory map */
		if((!cur_target) && last_target) {
			/* Attach to last target if detached. */
			cur_target = target_attach(last_target,
						   &gdb_controller);
		}
		if (!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		char* buf = (char*)malloc(1024);
		target_mem_map(cur_target, buf, 1024); /* Fixme: Check size!*/
		//HACK to get SFRs working
		strcpy(buf+strlen(buf)-strlen("</memory-map>"), 
		    "<memory type=\"ram\" start=\"0x30000000\" length=\"0xCFFFFFFF\"/></memory-map>\0");

		handle_q_string_reply(buf, packet + 23);

		free(buf);

	} else if (strncmp (packet, "qXfer:features:read:target.xml:", 31) == 0) {
		GDB_LOCK();
		/* Read target description */
		if((!cur_target) && last_target) {
			/* Attach to last target if detached. */
			cur_target = target_attach(last_target,
						   &gdb_controller);
		}
		if (!cur_target) {
			gdb_putpacketz("E01");
			return;
		}

		const char *const description = target_regs_description(cur_target);
	    handle_q_string_reply(description ? description : "", packet + 31);
	    free((void *)description);
		
	} else if (sscanf(packet, "qCRC:%" PRIx32 ",%" PRIx32, &addr, &alen) == 2) {
		GDB_LOCK();
		if(!cur_target) {
			gdb_putpacketz("E01");
			return;
		}
		uint32_t crc;
		if (!generic_crc32(cur_target, &crc, addr, alen))
			gdb_putpacketz("E03");
		else
			gdb_putpacket_f("C%lx", crc);

	} else if (strcmp(packet, "qC") == 0) {
		/*
 * qC queries are for the current thread. We don't support threads but GDB 11 and 12 require this,
 * so we always answer that the current thread is thread 1.
 */
		gdb_putpacketz("QC1");


	} else if (strcmp(packet, "qfThreadInfo") == 0 || strcmp(packet, "qsThreadInfo") == 0) {
		/*
		* qfThreadInfo queries are required in GDB 11 and 12 as these GDBs require the server to support
		* threading even when there's only the possiblity for one thread to exist. In this instance,
		* we have to tell GDB that there is a single active thread so it doesn't think the "thread" died.
		* qsThreadInfo will always follow qfThreadInfo when we reply as we have to specify 'l' at the
		* end to terminate the list.. GDB doesn't like this not happening.
		*/
		if (packet[1] == 'f')
			gdb_putpacketz("m1");
		else
			gdb_putpacketz("l");

	} else {
		DEBUG_GDB("*** Unsupported packet: %s\n", packet);
		gdb_putpacket("", 0);
	}
}

void
GDB::handle_v_packet(char *packet, int plen)
{
	unsigned long addr, len;
	int bin;
	static uint8_t flash_mode = 0;

	if (sscanf(packet, "vAttach;%08lx", &addr) == 1) {
		/* Attach to remote target processor */
		GDB_LOCK();
		cur_target = target_attach_n(addr, &gdb_controller);
		if(cur_target)
			gdb_putpacketz("T05thread:1;");
		else
			gdb_putpacketz("E01");

	} else if (!strncmp(packet, "vRun", 4)) {
		/* Parse command line for get_cmdline semihosting call */
		char cmdline[83];
		char *pbuf = cmdline;
		char *tok = packet + 4;
		if (*tok == ';') tok++;
		*cmdline='\0';
		while(*tok != '\0') {
			if(strlen(cmdline)+3 >= sizeof(cmdline)) break;
			if (*tok == ';') {
				*pbuf++=' ';
				*pbuf='\0';
				tok++;
				continue;
			}
			if (isxdigit(*tok) && isxdigit(*(tok+1))) {
				unhexify(pbuf, tok, 2);
				if ((*pbuf == ' ') || (*pbuf == '\\')) {
					*(pbuf+1)=*pbuf;
					*pbuf++='\\';
				}
				pbuf++;
				tok+=2;
				*pbuf='\0';
				continue;
			}
			break;
		}
		/* Run target program. For us (embedded) this means reset. */
		GDB_LOCK();

		if(cur_target) {
			target_set_cmdline(cur_target, cmdline);
			target_reset(cur_target);
			gdb_putpacketz("T05");
		} else if(last_target) {
			GDB_LOCK();
			cur_target = target_attach(last_target,
						   &gdb_controller);

                        /* If we were able to attach to the target again */
                        if (cur_target) {
				target_set_cmdline(cur_target, cmdline);
                        	target_reset(cur_target);
                        	gdb_putpacketz("T05");
                        } else	gdb_putpacketz("E01");

		} else	gdb_putpacketz("E01");

	} else if (sscanf(packet, "vFlashErase:%08lx,%08lx", &addr, &len) == 2) {
		GDB_LOCK();
		/* Erase Flash Memory */
		DEBUG_GDB("Flash Erase %08lX len:%d\n", addr, len);
		if(!cur_target) { gdb_putpacketz("EFF"); return; }
		if(!flash_mode) {
			/* Reset target if first flash command! */
			/* This saves us if we're interrupted in IRQ context */
			target_reset(cur_target);
			target_halt_request(cur_target);
			flash_mode = 1;
		}
		if(target_flash_erase(cur_target, addr, len)) {
			gdb_putpacketz("OK");
		} else {
			DEBUG_GDB("Flash Erase Failed\n");

			flash_mode = 0;
			gdb_putpacketz("EFF");
		}

	} else if (sscanf(packet, "vFlashWrite:%08lx:%n", &addr, &bin) == 1) {
		/* Write Flash Memory */
		//DEBUG_GDB("plen: %d(%x) %d", plen, plen, bin);
		GDB_LOCK();
		len = plen - bin;
		DEBUG_GDB("Flash Write %08lX len:%d\n", addr, len);
		//ESP_LOG_BUFFER_HEXDUMP("Flash", packet+bin, len, 3);
		if(cur_target && target_flash_write(cur_target, addr, (void*)(packet + bin), len)) {
			gdb_putpacketz("OK");
		} else {
			flash_mode = 0;
			gdb_putpacketz("EFF");
		}
	} else if (!strncmp(packet, "vCont", 5)) {
		GDB_LOCK();
		char* c = packet+5;
		if(*c == ';') c++;
		if(*c == '?') {
			gdb_putpacketz("vCont;c;s;t");
			return;
		}
		if(!cur_target) { gdb_putpacketz("EFF"); return; }
		single_step = false;
		while(*c) {
			switch(*c) {
				case 'c': //continue
					run_state = true;
					DEBUG_GDB("vCont: resume (single_step:%d)", single_step);
					target_halt_resume(cur_target, single_step);
					break;
				case 't': //stop
					DEBUG_GDB("vCont: halt");
					target_halt_request(cur_target);
					run_state = true;
					break;
				case 's': //step
					run_state = true;
					single_step = true;
					break;
			}
			c++;
		}
		gdb_putpacketz("OK");

	} else if (!strncmp(packet, "vKill;", 6)) {
		GDB_LOCK();
		/* Kill the target - we don't actually care about the PID that follows "vKill;" */
		if (cur_target) {
			target_reset(cur_target);
			target_detach(cur_target);
			last_target = cur_target;
			cur_target = NULL;
		}
		gdb_putpacketz("OK");

	} else if (!strcmp(packet, "vFlashDone")) {
		/* Commit flash operations. */
		GDB_LOCK();
		gdb_putpacketz(target_flash_complete(cur_target) ? "OK" : "EFF");
		flash_mode = 0;
	} else if (!strcmp(packet, "vStopped")) {
		if (gdb_needs_detach_notify) {
			gdb_putpacketz("W00");
			gdb_needs_detach_notify = false;
		} else
			gdb_putpacketz("OK");
	} else {
		DEBUG_GDB("*** Unsupported packet: %s\n", packet);
		gdb_putpacket("", 0);
	}
}

void
GDB::handle_z_packet(char *packet, int plen)
{
	(void)plen;

	uint8_t set = (packet[0] == 'Z') ? 1 : 0;
	int type, len;
	uint32_t addr;
	int ret;
	GDB_LOCK();
	/* I have no idea why this doesn't work. Seems to work
	 * with real sscanf() though... */
	//sscanf(packet, "%*[zZ]%hhd,%08lX,%hhd", &type, &addr, &len);
	type = packet[1] - '0';
	sscanf(packet + 2, ",%" PRIx32 ",%d", &addr, &len);
	if(set)
		ret = target_breakwatch_set(cur_target, (target_breakwatch)type, addr, len);
	else
		ret = target_breakwatch_clear(cur_target, (target_breakwatch)type, addr, len);

	if (ret < 0) {
		gdb_putpacketz("E01");
	} else if (ret > 0) {
		gdb_putpacketz("");
	} else {
		gdb_putpacketz("OK");
	}
}

void GDB::gdb_main(void)
{
	gdb_main_loop(&gdb_controller, false);
}

extern "C"
int gdb_main_loop(struct target_controller * tc, bool in_syscall) {
	ESP_LOGI("C", "gdb_main_loop");
	void** ptr = (void**)pvTaskGetThreadLocalStoragePointer(NULL, 0);
	assert(ptr);
	GDB* _this = (GDB*)ptr[0];
	return _this->gdb_main_loop(tc, in_syscall);	
}
