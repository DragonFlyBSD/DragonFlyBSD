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
 * $DragonFly: src/usr.sbin/dntpd/client.c,v 1.1 2005/04/24 02:36:50 dillon Exp $
 */

#include "defs.h"

void
client_init(void)
{
}

int
client_main(struct server_info **info_ary, int count)
{
    struct server_info *best;
    double freq;
    double offset;
    int i;

    for (;;) {
	/*
	 * Poll clients and accumulate data
	 */
	for (i = 0; i < count; ++i)
	    client_poll(info_ary[i]);

	/*
	 * Find the best client (or synthesize one).  If we find one
	 * then program the frequency correction and offset.
	 *
	 * client_check may replace the server_info pointer with a new
	 * one.
	 */
	best = NULL;
	for (i = 0; i < count; ++i)
	    best = client_check(&info_ary[i], best);
	if (best) {
	    offset = best->lin_sumoffset / best->lin_count;
	    freq = sysntp_correct_offset(offset);
	    sysntp_correct_freq(best->lin_cache_freq + freq);
	}
	sleep(5);
    }
}

void
client_poll(server_info_t info)
{
    struct timeval rtv;
    struct timeval ltv;
    struct timeval lbtv;
    double offset;
    int isalt = 0;

    if (debug_opt) {
	fprintf(stderr, "%s: poll, ", info->target);
	fflush(stderr);
    }
    if (udp_ntptimereq(info->fd, &rtv, &ltv, &lbtv) < 0) {
	if (debug_opt)
	    fprintf(stderr, "no response\n");
	return;
    }

    /*
     * Figure out the offset (the difference between the reported
     * time and our current time) for linear regression purposes.
     */
    offset = tv_delta_micro(&rtv, &ltv) / 1000000.0;

    while (info) {
	/*
	 * Linear regression
	 */
	if (debug_opt) {
	    struct tm *tp;
	    char buf[64];
	    time_t t;

	    t = rtv.tv_sec;
	    tp = localtime(&t);
	    strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tp);
	    fprintf(stderr, "%s.%03ld ", 
		    buf, rtv.tv_usec / 1000);
	}
	lin_regress(info, &ltv, &lbtv, offset, isalt);
	info = info->altinfo;
	++isalt;
    }
}

/*
 * Find the best client (or synthesize a fake info structure to return)
 */
struct server_info *
client_check(struct server_info **checkp, struct server_info *best)
{
    struct server_info *check = *checkp;
    struct server_info *info;

    /*
     * Start an alternate linear regression once our current one
     * has passed a certain point.
     */
    if (check->lin_count >= LIN_RESTART / 2 && check->altinfo == NULL) {
	info = malloc(sizeof(*info));
	assert(info != NULL);
	bcopy(check, info, sizeof(*info));
	check->altinfo = info;
	lin_reset(info);
    }

    /*
     * Replace our current linear regression with the alternate once
     * the current one has hit its limit (beyond a certain point the
     * linear regression starts to work against us, preventing us from
     * reacting to changing conditions).
     *
     * Report any significant change in the offset or ppm.
     */
    if (check->lin_count >= LIN_RESTART) {
	if ((info = check->altinfo) && info->lin_count >= LIN_RESTART / 2) {
	    double freq_diff;

	    if (debug_opt) {
		freq_diff = info->lin_cache_freq - check->lin_cache_freq;
		printf("%s: Switching to alternate, Frequence difference is %6.3f ppm\n",
			info->target, freq_diff * 1.0E+6);
	    }
	    *checkp = info;
	    free(check);
	    check = info;
	}
    }

    /*
     * At least 8 samples and a correllation > 0.99 is required for 
     * a frequency correction.
     */
    if (check->lin_count >= 8 && fabs(check->lin_cache_corr) > 0.99) {
	if (best == NULL || 
	    fabs(check->lin_cache_corr) > fabs(best->lin_cache_corr)) {
		best = check;
	}

    }
    return(best);
}

/*
 * Linear regression.
 *
 *	ltv	local time as of when the offset error was calculated between
 *		local time and remote time.
 *
 *	lbtv	base time as of when local time was obtained.  Used to
 *		calculate the cumulative corrections made to the system's
 *		real time clock so we can de-correct the offset for the
 *		linear regression.
 *
 * X is the time axis, in seconds.
 * Y is the uncorrected offset, in seconds.
 */
void
lin_regress(server_info_t info, struct timeval *ltv, struct timeval *lbtv,
	    double offset, int isalt)
{
    double time_axis;
    double uncorrected_offset;

    if (info->lin_count == 0) {
	info->lin_tv = *ltv;
	info->lin_btv = *lbtv;
	time_axis = 0;
	uncorrected_offset = offset;
    } else {
	time_axis = tv_delta_micro(&info->lin_tv, ltv) / 1000000.0;
	uncorrected_offset = offset - 
			tv_delta_micro(&info->lin_btv, lbtv) / 1000000.0;
    }
    ++info->lin_count;
    info->lin_sumx += time_axis;
    info->lin_sumx2 += time_axis * time_axis;
    info->lin_sumy += uncorrected_offset;
    info->lin_sumy2 += uncorrected_offset * uncorrected_offset;
    info->lin_sumxy += time_axis * uncorrected_offset;
    info->lin_sumoffset += uncorrected_offset;

    /*
     * Calculate various derived values (from statistics).
     */

    info->lin_cache_slope = 
	(info->lin_count * info->lin_sumxy - info->lin_sumx * info->lin_sumy) /
	(info->lin_count * info->lin_sumx2 - info->lin_sumx * info->lin_sumx);

    info->lin_cache_yint = 
	(info->lin_sumy - info->lin_cache_slope * info->lin_sumx) /
	(info->lin_count);

    info->lin_cache_corr =
	(info->lin_count * info->lin_sumxy - info->lin_sumx * info->lin_sumy) /
	sqrt((info->lin_count * info->lin_sumx2 - 
		      info->lin_sumx * info->lin_sumx) *
	     (info->lin_count * info->lin_sumy2 - 
		      info->lin_sumy * info->lin_sumy)
	);

    /*
     * Calculate the offset and frequency correction
     */
    info->lin_cache_offset = offset;
    info->lin_cache_freq = info->lin_cache_slope;

    if (debug_opt) {
	fprintf(stderr, "time=%7.3f off=%.6f uoff=%.6f slope %7.6f"
			" yint %3.2f corr %7.6f freq_ppm %4.2f\n", 
	    time_axis, offset, uncorrected_offset,
	    info->lin_cache_slope,
	    info->lin_cache_yint,
	    info->lin_cache_corr,
	    info->lin_cache_freq * 1000000.0);
    }
}

void
lin_reset(server_info_t info)
{
    info->lin_count = 0;
    info->lin_sumx = 0;
    info->lin_sumy = 0;
    info->lin_sumxy = 0;
    info->lin_sumx2 = 0;
    info->lin_sumy2 = 0;
    info->lin_sumoffset = 0;

    info->lin_cache_slope = 0;
    info->lin_cache_yint = 0;
    info->lin_cache_corr = 0;
    info->lin_cache_offset = 0;
    info->lin_cache_freq = 0;
}

