/*-
 * Copyright (c) 1983, 1992, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file is taken from Cygwin distribution. Please keep it in sync.
 * The differences should be within __MINGW32__ guard.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "gmon.h"
#include "profil.h"
#include <stdint.h>
#include <string.h>

#define MINUS_ONE_P      (-1)
#define bzero(ptr, size) memset(ptr, 0, size);
#define ERR(s)           semi_write(s, sizeof(s))

struct gmonparam _gmonparam = {GMON_PROF_OFF, NULL, 0, NULL, 0, NULL, 0, 0L, 0, 0, 0};
static int s_scale;
/* see profil(2) where this is described (incorrectly) */
#define SCALE_1_TO_1 0x10000L

static volatile char gprof_called = 0; //initialize gprof during start-up

/*
 * Control profiling
 *    profiling is what mcount checks to see if
 *    all the data structures are ready.
 */
static void moncontrol(int mode)
{
	struct gmonparam *p = &_gmonparam;

	if (mode) {
		/* start */
		profil((char *)p->kcount, p->kcountsize, p->lowpc, s_scale);
		p->state = GMON_PROF_ON;
	} else {
		/* stop */
		profil((char *)0, 0, 0, 0);
		p->state = GMON_PROF_OFF;
	}
}

void monstartup(size_t lowpc, size_t highpc)
{ //gmonparam expects size_t; to avoid typecasting
	register size_t o;
	char *cp;
	struct gmonparam *p = &_gmonparam;

	char debug[80];
	char str[64];
	/*
     * round lowpc and highpc to multiples of the density we're using
     * so the rest of the scaling (here and in gprof) stays in ints.
     */
	p->lowpc = ROUNDDOWN(lowpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->highpc = ROUNDUP(highpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->textsize = p->highpc - p->lowpc;
	p->kcountsize = p->textsize / HISTFRACTION;
	p->fromssize = p->textsize / HASHFRACTION;
	p->tolimit = p->textsize * ARCDENSITY / 100;
	if (p->tolimit < MINARCS) {
		p->tolimit = MINARCS;
	} else if (p->tolimit > MAXARCS) {
		p->tolimit = MAXARCS;
	}
	p->tossize = p->tolimit * sizeof(struct tostruct);

#if 0
    sprintf(debug,"requested amount %u\n",p->kcountsize + p->fromssize + p->tossize);
    semi_write(debug,strlen(debug)+1);

#endif

	cp = malloc(p->kcountsize + p->fromssize + p->tossize);

	if (cp == NULL) {
#if 1
		sprintf(str, "monstartup: out of memory\n");
		// semi_write(str, strlen(str) + 1);
#endif
		return;
	}

	/* zero out cp as value will be added there */
	bzero(cp, p->kcountsize + p->fromssize + p->tossize);

	p->tos = (struct tostruct *)cp;
	cp += p->tossize;
	p->kcount = (u_short *)cp;
	cp += p->kcountsize;
	p->froms = (u_short *)cp;

	p->tos[0].link = 0;

	o = p->highpc - p->lowpc;
	if (p->kcountsize < o) {
#ifdef notdef
		s_scale = ((float)p->kcountsize / o) * SCALE_1_TO_1;
#else /* avoid floating point */
		int quot = o / p->kcountsize;

		if (quot >= 0x10000)
			s_scale = 1;
		else if (quot >= 0x100)
			s_scale = 0x10000 / quot;
		else if (o >= 0x800000)
			s_scale = 0x1000000 / (o / (p->kcountsize >> 8));
		else
			s_scale = 0x1000000 / ((o << 8) / p->kcountsize);
#endif
	} else {
		s_scale = SCALE_1_TO_1;
	}
	moncontrol(1); /* start */
}

void _mcount_internal(size_t frompc, size_t selfpc)
{
	uint16_t *frompcindex;
	register struct tostruct *top;
	register struct tostruct *prevtop;
	volatile long toindex;
	struct gmonparam *p = &_gmonparam;

#ifdef SDEBUG
	char *overflow = "overflow\n";
	char tofro[64];
	char *dd[64];
	sprintf(tofro, "to:%p fro:%p\n", selfpc, frompcindex);
	semi_write(tofro, strlen(tofro) + 1);
#endif

	if (!gprof_called) {
		extern char _sprof[];
		extern char _eprof[];
		gprof_called = 1;
		monstartup((size_t)_sprof, (size_t)_eprof);
	}
	/*
   *    check that we are profiling
   *    and that we aren't recursively invoked.
   */
	if (p->state == GMON_PROF_ON)
		p->state = GMON_PROF_BUSY;
	else
		return;

	/*
   *    check that frompcindex is a reasonable pc value.
   *    for example:    signal catchers get called from the stack,
   *            not from text space.  too bad.
   */
	frompc = (size_t)((long)frompc - (long)p->lowpc);
	if (frompc > p->textsize) {
		goto done;
	}

	frompcindex = (size_t)&p->froms[frompc / (HASHFRACTION * sizeof(*p->froms))];
	//frompcindex = ((long)frompcindex + 3) & ~0x03;

#ifdef SDEBUG
	sprintf(dd, "tmp:%p fromindex:%p\n", tmp, frompcindex);
	semi_write(dd, strlen(dd));
#endif

	toindex = *frompcindex;
	if (toindex == 0) {
		/*
    *    first time traversing this arc
    */
		toindex = ++p->tos[0].link;  /* the link of tos[0] points to the last used record in the array */
		if (toindex >= p->tolimit) { /* more tos[] entries than we can handle! */
			goto overflow;
		}
		*frompcindex = toindex;
		top = &p->tos[toindex];
		top->selfpc = selfpc;
		top->count = 1;
		top->link = 0;
		goto done;
	}
	top = &p->tos[toindex];
	if (top->selfpc == selfpc) {
		/*
     *    arc at front of chain; usual case.
     */
		top->count++;
		goto done;
	}
	/*
   *    have to go looking down chain for it.
   *    top points to what we are looking at,
   *    prevtop points to previous top.
   *    we know it is not at the head of the chain.
   */
	for (; /* goto done */;) {
		if (top->link == 0) {
			/*
       *    top is end of the chain and none of the chain
       *    had top->selfpc == selfpc.
       *    so we allocate a new tostruct
       *    and link it to the head of the chain.
       */
			toindex = ++p->tos[0].link;
			if (toindex >= p->tolimit) {
				goto overflow;
			}
			top = &p->tos[toindex];
			top->selfpc = selfpc;
			top->count = 1;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}
		/*
     *    otherwise, check the next arc on the chain.
     */
		prevtop = top;
		top = &p->tos[top->link];
		if (top->selfpc == selfpc) {
			/*
       *    there it is.
       *    increment its count
       *    move it to the head of the chain.
       */
			top->count++;
			toindex = prevtop->link;
			prevtop->link = top->link;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}
	}
done:
	p->state = GMON_PROF_ON;
	return;
overflow:
	p->state = GMON_PROF_ERROR;
#ifdef SDEBUG
	semi_write(overflow, strlen(overflow) + 1);
#endif
	return;
}

void _mcleanup(void)
{
	static const char gmon_out[] = "gmon.out";
	int fd;
	int hz;
	int fromindex;
	int endfrom;
	size_t frompc;
	int toindex;
	struct rawarc rawarc;
	struct gmonparam *p = &_gmonparam;
	struct gmonhdr gmonhdr, *hdr;
	const char *proffile;

	const char *str = "tos Err\n";

#ifdef DEBUG
	int log, len;
	char dbuf[200];
#endif

	if (p->state == GMON_PROF_ERROR) {
		// semi_write(str, strlen(str));
	}
	hz = PROF_HZ;
	moncontrol(0); /* stop */
	proffile = gmon_out;

	fd = open(proffile, O_RDWR | O_CREAT | O_TRUNC);
	if (fd < 0) {
		// semi_write(str, strlen(str) + 1);
		return;
	}
#ifdef DEBUG
	log = open("gmon.log", O_CREAT | O_TRUNC | O_WRONLY, 0664);
	if (log < 0) {
		perror("mcount: gmon.log");
		return;
	}
	len = sprintf(dbuf, "[mcleanup1] kcount 0x%x ssiz %d\n", p->kcount, p->kcountsize);
	write(log, dbuf, len);
#endif
	hdr = (struct gmonhdr *)&gmonhdr;
	hdr->lpc = p->lowpc;
	hdr->hpc = p->highpc;
	hdr->ncnt = p->kcountsize + sizeof(gmonhdr);
	hdr->version = GMONVERSION;
	hdr->profrate = hz;
	write(fd, (char *)hdr, sizeof *hdr);
	write(fd, p->kcount, p->kcountsize);
	endfrom = p->fromssize / sizeof(*p->froms);
	for (fromindex = 0; fromindex < endfrom; fromindex++) {
		if (p->froms[fromindex] == 0) {
			continue;
		}
		frompc = p->lowpc;
		frompc += fromindex * HASHFRACTION * sizeof(*p->froms);
		for (toindex = p->froms[fromindex]; toindex != 0; toindex = p->tos[toindex].link) {
#ifdef DEBUG
			len = sprintf(dbuf, "[mcleanup2] frompc 0x%x selfpc 0x%x count %d\n", frompc, p->tos[toindex].selfpc,
				p->tos[toindex].count);
			write(log, dbuf, len);
#endif
			rawarc.raw_frompc = frompc;
			rawarc.raw_selfpc = p->tos[toindex].selfpc;
			rawarc.raw_count = p->tos[toindex].count;
			write(fd, &rawarc, sizeof rawarc);
		}
	}
	close(fd);
}
