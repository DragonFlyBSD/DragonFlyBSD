/*
 * Copyright (c) 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * UKP-Optimized gettimeofday().  Use the kpmap after the 10th call.
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/upmap.h>
#include <sys/time.h>
#include <machine/cpufunc.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
/*#include "un-namespace.h"*/
#include "libc_private.h"
#include "upmap.h"

extern int __sys_gettimeofday(struct timeval *, struct timezone *);
int __gettimeofday(struct timeval *, struct timezone *);

static int fast_clock;
static int fast_count;
static int *fast_gtod;
static int *upticksp;
static struct timespec *ts_realtime;

#ifdef KPTYPE_FAST_GTOD
int
__gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct timespec ts;
	int res;
	int w;

	if (fast_clock == 0 && fast_count++ >= 10) {
                __kpmap_map(&upticksp, &fast_clock, KPTYPE_UPTICKS);
		__kpmap_map(&ts_realtime, &fast_clock, KPTYPE_TS_REALTIME);
		__kpmap_map(&fast_gtod, &fast_clock, KPTYPE_FAST_GTOD);
		__kpmap_map(NULL, &fast_clock, 0);
	}
	if (fast_clock > 0 && *fast_gtod && tz == NULL) {
		do {
			w = *upticksp;
			cpu_lfence();
			ts = ts_realtime[w & 1];
			cpu_lfence();
			w = *upticksp - w;
		} while (w > 1);
		res = 0;
		if (tv) {
			tv->tv_sec = ts.tv_sec;
			tv->tv_usec = ts.tv_nsec / 1000;
		}
	} else {
		res = __sys_gettimeofday(tv, tz);
	}
	return res;
}

#else

int
__gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int res;

	res = __sys_gettimeofday(tv, tz);
	return res;
}

#endif

__weak_reference(__gettimeofday, gettimeofday);
