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
 * $DragonFly: src/usr.sbin/dntpd/client.c,v 1.4 2005/04/24 09:39:27 dillon Exp $
 */

#include "defs.h"

void
client_init(void)
{
}

int
client_main(struct server_info **info_ary, int count)
{
    struct server_info *best_off;
    struct server_info *best_freq;
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
	 * Find the best client (or synthesize one).  A different client
	 * can be chosen for frequency and offset.  Note in particular 
	 * that offset counters and averaging code gets reset when an
	 * offset correction is made (otherwise the averaging history will
	 * cause later corrections to overshoot).  
	 * 
	 * The regression used to calculate the frequency is a much 
	 * longer-term entity and is NOT reset, so it is still possible
	 * for the offset correction code to make minor adjustments to
	 * the frequency if it so desires.
	 *
	 * client_check may replace the server_info pointer with a new
	 * one.
	 */
	best_off = NULL;
	best_freq = NULL;
	for (i = 0; i < count; ++i)
	    client_check(&info_ary[i], &best_off, &best_freq);

	/*
	 * Offset correction.
	 *
	 * XXX it might not be a good idea to issue an offset correction if
	 * a prior offset correction is still in progress as this will skew
	 * the offset calculation.  XXX either figure out how to correct the
	 * skew or do not issue a correction.
	 */
	if (best_off) {
	    offset = best_off->lin_sumoffset / best_off->lin_countoffset;
	    lin_resetalloffsets(info_ary, count);
	    freq = sysntp_correct_offset(offset);
	} else {
	    freq = 0.0;
	}

	/*
	 * Frequency correction (throw away minor freq adjusts from the
	 * offset code if we can't do a frequency correction here).
	 */
	if (best_freq) {
	    sysntp_correct_freq(best_freq->lin_cache_freq + freq);
	}

	if (debug_sleep)
	    usleep(debug_sleep * 1000000 + random() % 100000);
	else
	    usleep(60 * 1000000 + random() % 100000);
    }
}

void
client_poll(server_info_t info)
{
    struct timeval rtv;
    struct timeval ltv;
    struct timeval lbtv;
    double offset;

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
    offset = tv_delta_double(&rtv, &ltv);

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
	lin_regress(info, &ltv, &lbtv, offset);
	info = info->altinfo;
	if (info && debug_opt) {
	    fprintf(stderr, "%*.*s: poll, ", 
		(int)strlen(info->target), 
		(int)strlen(info->target),
		"(alt)");
	    fflush(stderr);
	}
    }
}

/*
 * Find the best client (or synthesize a fake info structure to return).
 * We can find separate best clients for offset and frequency.
 */
void
client_check(struct server_info **checkp, 
	     struct server_info **best_off,
	     struct server_info **best_freq)
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
     * BEST CLIENT FOR FREQUENCY CORRECTION:
     *
     *	8 samples and a correllation > 0.99, or
     * 16 samples and a correllation > 0.96
     */
    info = *best_freq;
    if ((check->lin_count >= 8 && fabs(check->lin_cache_corr) >= 0.99) ||
	(check->lin_count >= 16 && fabs(check->lin_cache_corr) >= 0.96)
    ) {
	if (info == NULL || 
	    fabs(check->lin_cache_corr) > fabs(info->lin_cache_corr)
	) {
	    info = check;
	    *best_freq = info;
	}

    }

    /*
     * BEST CLIENT FOR OFFSET CORRECTION:
     *
     * Use the standard-deviation and require at least 4 samples.  An
     * offset correction is valid if the standard deviation is less then
     * the average offset divided by 4.
     */
    info = *best_off;
    if (check->lin_countoffset >= 4 && 
	check->lin_cache_stddev < fabs(check->lin_sumoffset / check->lin_countoffset / 4)) {
	if (info == NULL || 
	    fabs(check->lin_cache_stddev) < fabs(info->lin_cache_stddev)
	) {
	    info = check;
	    *best_off = info;
	}
    }
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
	    double offset)
{
    double time_axis;
    double uncorrected_offset;

    /*
     * De-correcting the offset:
     *
     *	The passed offset is (our_real_time - remote_real_time).  To remove
     *  corrections from our_real_time we take the difference in the basetime
     *  (new_base_time - old_base_time) and subtract that from the offset.
     *  That is, if the basetime goesup, the uncorrected offset goes down.
     */
    if (info->lin_count == 0) {
	info->lin_tv = *ltv;
	info->lin_btv = *lbtv;
	time_axis = 0;
	uncorrected_offset = offset;
    } else {
	time_axis = tv_delta_double(&info->lin_tv, ltv);
	uncorrected_offset = offset - tv_delta_double(&info->lin_btv, lbtv);
    }

    /*
     * We have to use the uncorrected offset for frequency calculations.
     */
    ++info->lin_count;
    info->lin_sumx += time_axis;
    info->lin_sumx2 += time_axis * time_axis;
    info->lin_sumy += uncorrected_offset;
    info->lin_sumy2 += uncorrected_offset * uncorrected_offset;
    info->lin_sumxy += time_axis * uncorrected_offset;

    /*
     * We have to use the corrected offset for offset calculations.
     */
    ++info->lin_countoffset;
    info->lin_sumoffset += offset;
    info->lin_sumoffset2 += offset * offset;

    /*
     * Calculate various derived values.   This gets us slope, y-intercept,
     * and correllation from the linear regression.
     */
    if (info->lin_count > 1) {
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
    }

    /*
     * Calculate more derived values.  This gets us the standard-deviation
     * of offsets.  The standard deviation approximately means that 68%
     * of the samples fall within the calculated stddev of the mean.
     */
    if (info->lin_countoffset > 1) {
	 info->lin_cache_stddev = 
	     sqrt((info->lin_sumoffset2 - 
		 ((info->lin_sumoffset * info->lin_sumoffset / 
		   info->lin_countoffset))) /
	         (info->lin_countoffset - 1.0));
    }

    /*
     * Save the most recent offset, we might use it in the future.
     * Save the frequency correction (we might scale the slope later so
     * we have a separate field for the actual frequency correction in
     * seconds per second).
     */
    info->lin_cache_offset = offset;
    info->lin_cache_freq = info->lin_cache_slope;

    if (debug_opt) {
	fprintf(stderr, "iter=%2d time=%7.3f off=%.6f uoff=%.6f",
	    (int)info->lin_count,
	    time_axis, offset, uncorrected_offset);
	if (info->lin_count > 1) {
	    fprintf(stderr, " slope %7.6f"
			    " yint %3.2f corr %7.6f freq_ppm %4.2f", 
		info->lin_cache_slope,
		info->lin_cache_yint,
		info->lin_cache_corr,
		info->lin_cache_freq * 1000000.0);
	}
	if (info->lin_countoffset > 1) {
	    fprintf(stderr, " stddev %7.6f", info->lin_cache_stddev);
	}
	fprintf(stderr, "\n");
    }
}

/*
 * Reset the linear regression data.  The info structure will not again be
 * a candidate for frequency or offset correction until sufficient data
 * has been accumulated to make a decision.
 */
void
lin_reset(server_info_t info)
{
    info->lin_count = 0;
    info->lin_sumx = 0;
    info->lin_sumy = 0;
    info->lin_sumxy = 0;
    info->lin_sumx2 = 0;
    info->lin_sumy2 = 0;

    info->lin_countoffset = 0;
    info->lin_sumoffset = 0;
    info->lin_sumoffset2 = 0;

    info->lin_cache_slope = 0;
    info->lin_cache_yint = 0;
    info->lin_cache_corr = 0;
    info->lin_cache_offset = 0;
    info->lin_cache_freq = 0;
}

/*
 * Sometimes we want to clean out the offset calculations without
 * destroying the linear regression used to figure out the frequency
 * correction.  This usually occurs whenever we issue an offset
 * adjustment to the system, which invalidates any offset data accumulated
 * up to that point.
 */
void
lin_resetalloffsets(struct server_info **info_ary, int count)
{
    server_info_t info;
    int i;

    for (i = 0; i < count; ++i) {
	for (info = info_ary[i]; info; info = info->altinfo)
	    lin_resetoffsets(info);
    }
}

void
lin_resetoffsets(server_info_t info)
{
    info->lin_countoffset = 0;
    info->lin_sumoffset = 0;
    info->lin_sumoffset2 = 0;
}

