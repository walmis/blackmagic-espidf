/* profil.c -- win32 profil.c equivalent

   Copyright 1998, 1999, 2000, 2001, 2002 Red Hat, Inc.

   This file is part of Cygwin.

   This software is a copyrighted work licensed under the terms of the
   Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
   details. */

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include "profil.h"
#include <string.h>
#include <stdint.h>

#define ON  1
#define OFF 0

/* global profinfo for profil() call */
static struct profinfo prof = {0};

static volatile char profile_state = OFF;

/* sample the current program counter */
void profile_handler(uintptr_t *epc)
{
	static size_t pc, idx;

	if (profile_state == ON) {
		pc = (size_t)epc;
		if (pc >= prof.lowpc && pc < prof.highpc) {
			idx = PROFIDX(pc, prof.lowpc, prof.scale);
			prof.counter[idx]++;
		}
	}
}

/* Stop profiling to the profiling buffer pointed to by p. */
static int profile_off(struct profinfo *p)
{
	// disable_timer_interrupts();
	profile_state = OFF;
	return 0;
}

/* Create a timer thread and pass it a pointer P to the profiling buffer. */
static int profile_on(struct profinfo *p)
{
	// enable_timer_interrupts();
	profile_state = ON;
	return 0;
}

/*
 * start or stop profiling
 *
 * profiling goes into the SAMPLES buffer of size SIZE (which is treated
 * as an array of u_shorts of size size/2)
 *
 * each bin represents a range of pc addresses from OFFSET.  The number
 * of pc addresses in a bin depends on SCALE.  (A scale of 65536 maps
 * each bin to two addresses, A scale of 32768 maps each bin to 4 addresses,
 * a scale of 1 maps each bin to 128k address).  Scale may be 1 - 65536,
 * or zero to turn off profiling
 */
int profile_ctl(struct profinfo *p, char *samples, size_t size, size_t offset, u_int scale)
{
	size_t maxbin;

	if (scale > 65536) {
		errno = EINVAL;
		return -1;
	}
	profile_off(p);
	if (scale) {
		memset(samples, 0, size);
		memset(p, 0, sizeof *p);
		maxbin = size >> 1;
		prof.counter = (u_short *)samples;
		prof.lowpc = offset;
		prof.highpc = PROFADDR(maxbin, offset, scale);
		prof.scale = scale;
		return profile_on(p);
	}
	return 0;
}

/* Equivalent to unix profil()
   Every SLEEPTIME interval, the user's program counter (PC) is examined:
   offset is subtracted and the result is multiplied by scale.
   The word pointed to by this address is incremented. */
int profil(char *samples, size_t size, size_t offset, u_int scale)
{
	return profile_ctl(&prof, samples, size, offset, scale);
}
