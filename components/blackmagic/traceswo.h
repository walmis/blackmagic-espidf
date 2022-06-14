/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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
#ifndef __TRACESWO_H
#define __TRACESWO_H

#if defined TRACESWO_PROTOCOL && TRACESWO_PROTOCOL == 2
/* Default line rate of "0" indicates autobaud */
#define SWO_DEFAULT_BAUD 0
void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask);
void traceswo_baud(unsigned int baud);
#else
void traceswo_init(uint32_t swo_chan_bitmask);
#endif
void traceswo_deinit(void);

/* set bitmask of swo channels to be decoded */
void traceswo_setmask(uint32_t mask);

#define DFU_SERIAL_LENGTH 12
char *serial_no_read(char *s);

#endif
