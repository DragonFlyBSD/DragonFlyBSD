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
 * $DragonFly: src/usr.sbin/dntpd/client.c,v 1.8 2005/04/26 07:01:43 dillon Exp $
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
    double last_freq;
    double freq;
    double offset;
    int i;
    int calc_offset_correction;

    last_freq = 0.0;

    for (;;) {
	/*
	 * Subtract the interval from poll_sleep and poll the client
	 * if it reaches 0.
	 *
	 * Because we do not compensate for offset corrections which are
	 * in progress, we cannot accumulate data for an offset correction
	 * while a prior correction is still being worked through by the
	 * system.
	 */
	calc_offset_correction = !sysntp_offset_correction_is_running();
	for (i = 0; i < count; ++i)
	    client_poll(info_ary[i], min_sleep_opt, calc_offset_correction);

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
	 */
	if (best_off) {
	    offset = best_off->lin_sumoffset / best_off->lin_countoffset;
	    lin_resetalloffsets(info_ary, count);
	    if (offset < -COURSE_OFFSET_CORRECTION_LIMIT ||
		offset > COURSE_OFFSET_CORRECTION_LIMIT
	    ) {
		freq = sysntp_correct_course_offset(offset);
	    } else {
		freq = sysntp_correct_offset(offset);
	    }
	} else {
	    freq = 0.0;
	}

	/*
	 * Frequency correction (throw away minor freq adjusts from the
	 * offset code if we can't do a frequency correction here).  Do
	 * not reissue if it hasn't changed from the last issued correction.
	 */
	if (best_freq) {
	    freq += best_freq->lin_cache_freq;
	    if (last_freq != freq) {
		sysntp_correct_freq(freq);
		last_freq = freq;
	    }
	}

	/*
	 * This function is responsible for managing the polling mode and
	 * figures out how long we should sleep.
	 */
	for (i = 0; i < count; ++i)
	    client_manage_polling_mode(info_ary[i]);

	/*
	 * Polling loop sleep.
	 */
	usleep(min_sleep_opt * 1000000 + random() % 500000);
    }
}

void
client_poll(server_info_t info, int poll_interval, int calc_offset_correction)
{
    struct timeval rtv;
    struct timeval ltv;
    struct timeval lbtv;
    double offset;

    /*
     * By default we always poll.  If the polling interval comes under
     * active management the poll_sleep will be non-zero.
     */
    if (info->poll_sleep > poll_interval) {
	info->poll_sleep -= poll_interval;
	return;
    }
    info->poll_sleep = 0;

    logdebug(4, "%s: poll, ", info->target);
    if (udp_ntptimereq(info->fd, &rtv, &ltv, &lbtv) < 0) {
	++info->poll_failed;
	logdebug(4, "no response (%d failures in a row)\n", 
		info->poll_failed);
	if (info->poll_failed == POLL_FAIL_RESET) {
	    if (info->lin_count != 0) {
		logdebug(4, "%s: resetting regression due to failures\n", 
			info->target);
	    }
	    lin_reset(info);
	}
	return;
    }

    /*
     * Successful query.  Update polling info for the polling mode manager.
     */
    ++info->poll_count;
    info->poll_failed = 0;

    /*
     * Figure out the offset (the difference between the reported
     * time and our current time) for linear regression purposes.
     */
    offset = tv_delta_double(&rtv, &ltv);

    while (info) {
	/*
	 * Linear regression
	 */
	if (debug_level >= 4) {
	    struct tm *tp;
	    char buf[64];
	    time_t t;

	    t = rtv.tv_sec;
	    tp = localtime(&t);
	    strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tp);
	    logdebug(4, "%s.%03ld ", buf, rtv.tv_usec / 1000);
	}
	lin_regress(info, &ltv, &lbtv, offset, calc_offset_correction);
	info = info->altinfo;
	if (info && debug_level >= 4) {
	    logdebug(4, "%*.*s: poll, ", 
		(int)strlen(info->target), 
		(int)strlen(info->target), "(alt)");
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
	/* note: check->altinfo is NULL as of the bcopy */
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

	    freq_diff = info->lin_cache_freq - check->lin_cache_freq;
	    logdebug(4, "%s: Switching to alternate, Frequence "
		    "difference is %6.3f ppm\n",
		    info->target, freq_diff * 1.0E+6);
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
 * Actively manage the polling interval.  Note that the poll_* fields are
 * always transfered to the alternate regression when the check code replaces
 * the current regression with a new one.
 *
 * This routine is called from the main loop for each base info structure.
 * The polling mode applies to all alternates so we do not have to iterate
 * through the alt's.
 */
void
client_manage_polling_mode(struct server_info *info)
{
    /*
     * If too many query failures occured go into a failure-recovery state.
     * If we were in startup when we failed, go into the second failure
     * state so a recovery returns us back to startup mode.
     */
    if (info->poll_failed >= POLL_FAIL_RESET && 
	info->poll_mode != POLL_FAILED_1 &&
	info->poll_mode != POLL_FAILED_2
    ) {
	logdebug(2, "%s: polling mode moving to a FAILED state.\n",
		info->target);
	if (info->poll_mode != POLL_STARTUP)
	    info->poll_mode = POLL_FAILED_1;
	else
	    info->poll_mode = POLL_FAILED_2;
	info->poll_count = 0;
    }

    /*
     * Standard polling mode progression
     */
    switch(info->poll_mode) {
    case POLL_FIXED:
	info->poll_mode = POLL_STARTUP;
	info->poll_count = 0;
	logdebug(2, "%s: polling mode INIT->STARTUP.\n", info->target);
	/* fall through */
    case POLL_STARTUP:
	if (info->poll_count < POLL_STARTUP_MAX) {
	    if (info->poll_sleep == 0)
		info->poll_sleep = min_sleep_opt;
	    break;
	}
	info->poll_mode = POLL_ACQUIRE;
	info->poll_count = 0;
	logdebug(2, "%s: polling mode STARTUP->ACQUIRE.\n", info->target);
	/* fall through */
    case POLL_ACQUIRE:
	/*
	 * Acquisition mode using the nominal timeout.  We do not shift
	 * to maintainance mode unless the correllation is at least 0.90
	 */
	if (info->poll_count < POLL_ACQUIRE_MAX ||
	    info->lin_count < 8 ||
	    fabs(info->lin_cache_corr) < 0.85
	) {
	    if (info->poll_count >= POLL_ACQUIRE_MAX && 
		info->lin_count == LIN_RESTART - 2
	    ) {
		logdebug(2, 
		    "%s: WARNING: Unable to shift this source to "
		    "maintainance mode.  Target correllation is aweful.\n",
		    info->target);
	    }
	    if (info->poll_sleep == 0)
		info->poll_sleep = nom_sleep_opt;
	    break;
	}
	info->poll_mode = POLL_MAINTAIN;
	info->poll_count = 0;
	logdebug(2, "%s: polling mode ACQUIRE->MAINTAIN.\n", info->target);
	/* fall through */
    case POLL_MAINTAIN:
	if (info->lin_count >= LIN_RESTART / 2 && 
	    fabs(info->lin_cache_corr) < 0.70
	) {
	    logdebug(2, 
		"%s: polling mode MAINTAIN->ACQUIRE.  Unable to maintain\n"
		"the maintainance mode because the correllation went"
		" bad!\n", info->target);
	    info->poll_mode = POLL_ACQUIRE;
	    info->poll_count = 0;
	    break;
	}
	if (info->poll_sleep == 0)
	    info->poll_sleep = max_sleep_opt;
	/* do nothing */
	break;
    case POLL_FAILED_1:
	/*
	 * We have failed recently. If we recover return to the acquisition
	 * state.
	 *
	 * poll_count does not increment while we are failed.  poll_failed
	 * does increment (but gets zero'd once we recover).
	 */
	if (info->poll_count != 0) {
	    logdebug(2, "%s: polling mode FAILED1->ACQUIRE.\n", info->target);
	    info->poll_mode = POLL_ACQUIRE;
	    /* do not reset poll_count */
	    break;
	}
	if (info->poll_failed >= POLL_RECOVERY_RESTART)
	    info->poll_mode = POLL_FAILED_2;
	if (info->poll_sleep == 0)
	    info->poll_sleep = nom_sleep_opt;
	break;
    case POLL_FAILED_2:
	/*
	 * We have been failed for a very long time, or we failed while
	 * in startup.  If we recover we have to go back into startup.
	 */
	if (info->poll_count != 0) {
	    logdebug(2, "%s: polling mode FAILED2->STARTUP.\n", info->target);
	    info->poll_mode = POLL_STARTUP;
	    break;
	}
	if (info->poll_sleep == 0)
	    info->poll_sleep = nom_sleep_opt;
	break;
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
	    double offset, int calc_offset_correction)
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
    if (calc_offset_correction) {
	++info->lin_countoffset;
	info->lin_sumoffset += offset;
	info->lin_sumoffset2 += offset * offset;
    }

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

    if (debug_level >= 4) {
	logdebug(4, "iter=%2d time=%7.3f off=%.6f uoff=%.6f",
	    (int)info->lin_count,
	    time_axis, offset, uncorrected_offset);
	if (info->lin_count > 1) {
	    logdebug(4, " slope %7.6f"
			    " yint %3.2f corr %7.6f freq_ppm %4.2f", 
		info->lin_cache_slope,
		info->lin_cache_yint,
		info->lin_cache_corr,
		info->lin_cache_freq * 1000000.0);
	}
	if (info->lin_countoffset > 1) {
	    logdebug(4, " stddev %7.6f", info->lin_cache_stddev);
	} else if (calc_offset_correction == 0) {
	    /* cannot calculate offset correction due to prior correction */
	    logdebug(4, " offset_ignored");
	}
	logdebug(4, "\n");
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
    server_info_t scan;

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

    /*
     * Destroy any additional alternative regressions.
     */
    while ((scan = info->altinfo) != NULL) {
	info->altinfo = scan->altinfo;
	free(scan);
    }
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

