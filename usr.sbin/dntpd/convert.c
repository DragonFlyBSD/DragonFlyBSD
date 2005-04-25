/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/usr.sbin/dntpd/convert.c,v 1.4 2005/04/25 17:42:49 dillon Exp $
 */

#include "defs.h"

void
l_fixedpt_to_tv(struct l_fixedpt *fixed, struct timeval *tvp)
{
    tvp->tv_sec = fixed->int_partl - JAN_1970;
    tvp->tv_usec = (long)((double)fixed->fractionl * 1000000.0 / UINT_MAX);
}

/*
 * Subtract usec from tvp, storing the result in tvp.
 */
void
tv_subtract_micro(struct timeval *tvp, long usec)
{
    if (usec < 0) {
	tv_add_micro(tvp, -usec);
    } else {
	tvp->tv_usec -= usec;
	if (tvp->tv_usec < 0) {
	    tvp->tv_sec -= (-tvp->tv_usec + 999999) / 1000000;
	    tvp->tv_usec = 1000000 - (-tvp->tv_usec % 1000000);
	}
    }
}

/*
 * Add usec to tvp, storing the result in tvp.
 */
void
tv_add_micro(struct timeval *tvp, long usec)
{
    if (usec < 0) {
	tv_subtract_micro(tvp, -usec);
    } else {
	tvp->tv_usec += usec;
	if (tvp->tv_usec >= 1000000) {
	    tvp->tv_sec += tvp->tv_usec / 1000000;
	    tvp->tv_usec = tvp->tv_usec % 1000000;
	}
    }
}

/*
 * Add the fp offset to tvp.
 */
void
tv_add_offset(struct timeval *tvp, double offset)
{
    tvp->tv_sec += (long)offset;
    offset -= (double)(long)offset; /* e.g. -1.3 - (-1) = -0.3 */
    tv_add_micro(tvp, (int)(offset * 1000000.0));
}

/*
 * Return the time differential in microseconds.
 */
double
tv_delta_double(struct timeval *tv1, struct timeval *tv2)
{
    double usec;

    usec = (double)(tv2->tv_sec - tv1->tv_sec) +
		(double)(tv2->tv_usec - tv1->tv_usec) / 1000000.0;
    return(usec);
}

void
tv_to_ts(struct timeval *tv, struct timespec *ts)
{
    ts->tv_sec = tv->tv_sec;
    ts->tv_nsec = tv->tv_usec * 1000;
}

void
ts_to_tv(struct timespec *ts, struct timeval *tv)
{
    tv->tv_sec = ts->tv_sec;
    tv->tv_usec = ts->tv_nsec / 1000;
}

