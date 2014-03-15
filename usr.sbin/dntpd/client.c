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
 */

#include "defs.h"

static int client_insane(struct server_info **, int, server_info_t);

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
    int calc_offset_correction;
    int didreconnect;
    int i;
    int insane;

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
	 * Check for server insanity.  In large NNTP pools some servers
	 * may just be dead wrong, but report that they are right.
	 */
	if (best_off) {
	    insane = client_insane(info_ary, count, best_off);
	    if (insane > 0) {
		/* 
		 * best_off meets the quorum requirements and is good
		 * (keep best_off)
		 */
		best_off->server_insane = 0;
	    } else if (insane == 0) {
		/*
		 * best_off is probably good, but we do not have enough
		 * servers reporting yet to meet the quorum requirements.
		 */
		best_off = NULL;
	    } else {
		/*
		 * best_off is ugly, mark the server as being insane for
		 * 60 minutes.
		 */
		best_off->server_insane = 60 * 60;
		logdebuginfo(best_off, 1, 
			     "excessive offset deviation, mapping out\n");
		best_off = NULL;
	    }
	}

	/*
	 * Offset correction.
	 */
	if (best_off) {
	    offset = best_off->lin_sumoffset / best_off->lin_countoffset;
	    lin_resetalloffsets(info_ary, count);
	    if (offset < -COURSE_OFFSET_CORRECTION_LIMIT ||
		offset > COURSE_OFFSET_CORRECTION_LIMIT ||
		quickset_opt
	    ) {
		freq = sysntp_correct_course_offset(offset);
		quickset_opt = 0;
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
	didreconnect = 0;
	for (i = 0; i < count; ++i)
	    client_manage_polling_mode(info_ary[i], &didreconnect);
	if (didreconnect)
	    client_check_duplicate_ips(info_ary, count);

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
     * Adjust the insane-server countdown
     */
    if (info->server_insane > poll_interval)
	info->server_insane -= poll_interval;
    else
	info->server_insane = 0;

    /*
     * By default we always poll.  If the polling interval comes under
     * active management the poll_sleep will be non-zero.
     */
    if (info->poll_sleep > poll_interval) {
	info->poll_sleep -= poll_interval;
	return;
    }
    info->poll_sleep = 0;

    /*
     * If the client isn't open don't mess with the poll_failed count
     * or anything else.  We are left in the init or startup phase.
     */
    if (info->fd < 0) {
	if (info->poll_failed < 0x7FFFFFFF)
	    ++info->poll_failed;
	return;
    }

    logdebuginfo(info, 4, "poll, ");
    if (udp_ntptimereq(info->fd, &rtv, &ltv, &lbtv) < 0) {
	++info->poll_failed;
	logdebug(4, "no response (%d failures in a row)\n", info->poll_failed);
	if (info->poll_failed == POLL_FAIL_RESET) {
	    if (info->lin_count != 0) {
		logdebuginfo(info, 4, "resetting regression due to failures\n");
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
    int min_samples;

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
	    logdebuginfo(info, 4, "Switching to alternate, Frequency "
			 "difference is %6.3f ppm\n",
			 freq_diff * 1.0E+6);
	    *checkp = info;
	    free(check);
	    check = info;
	}
    }

    /*
     * BEST CLIENT FOR FREQUENCY CORRECTION:
     *
     * Frequency corrections get better the longer the time separation
     * between samples.
     *
     *	8 samples and a correlation > 0.99, or
     * 16 samples and a correlation > 0.96
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
     *
     * If we are in maintainance mode, require 8 samples instead of 4.
     * Offset corrections get better with more samples.  This reduces
     * ping-pong effects that can occur with a small number of samples.
     *
     * Servers marked as being insane are not allowed
     */
    info = *best_off;
    if (info && info->poll_mode == POLL_MAINTAIN)
	min_samples = 8;
    else
	min_samples = 4;
    if (check->lin_countoffset >= min_samples &&
	(check->lin_cache_stddev <
	 fabs(check->lin_sumoffset / check->lin_countoffset / 4)) &&
	check->server_insane == 0
     ) {
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
client_manage_polling_mode(struct server_info *info, int *didreconnect)
{
    /*
     * Permanently failed servers are ignored.
     */
    if (info->server_state == -2)
	return;

    /*
     * Our polling interval has not yet passed.
     */
    if (info->poll_sleep)
	return;

    /*
     * Standard polling mode progression
     */
    switch(info->poll_mode) {
    case POLL_FIXED:
	/*
	 * Initial state after connect or when a reconnect is required.
	 */
	if (info->fd < 0) {
	    logdebuginfo(info, 2, "polling mode INIT, relookup & reconnect\n");
	    reconnect_server(info);
	    *didreconnect = 1;
	    if (info->fd < 0) {
		if (info->poll_failed >= POLL_RECOVERY_RESTART * 5)
		    info->poll_sleep = max_sleep_opt;
		else if (info->poll_failed >= POLL_RECOVERY_RESTART)
		    info->poll_sleep = nom_sleep_opt;
		else
		    info->poll_sleep = min_sleep_opt;
		break;
	    }

	    /*
	     * Transition the server to the DNS lookup successful state.
	     * Note that the server state does not transition out of
	     * lookup successful if we relookup after a packet failure
	     * so the message is printed only once, usually.
	     */
	    client_setserverstate(info, 0, "DNS lookup success");

	    /*
	     * If we've failed many times switch to the startup state but
	     * do not fall through into it.  break the switch and a single
	     * poll will be made after the nominal polling interval.
	     */
	    if (info->poll_failed >= POLL_RECOVERY_RESTART * 5) {
		logdebuginfo(info, 2, "polling mode INIT->STARTUP (very slow)\n");
		info->poll_mode = POLL_STARTUP;
		info->poll_sleep = max_sleep_opt;
		info->poll_count = 0;
		break;
	    } else if (info->poll_failed >= POLL_RECOVERY_RESTART) {
		logdebuginfo(info, 2, "polling mode INIT->STARTUP (slow)\n");
		info->poll_mode = POLL_STARTUP;
		info->poll_count = 0;
		break;
	    }
	}

	/*
	 * Fall through to the startup state.
	 */
	info->poll_mode = POLL_STARTUP;
	logdebuginfo(info, 2, "polling mode INIT->STARTUP (normal)\n");
	/* fall through */
    case POLL_STARTUP:
	/*
	 * Transition to a FAILED state if too many poll failures occured.
	 */
	if (info->poll_failed >= POLL_FAIL_RESET) {
	    logdebuginfo(info, 2, "polling mode STARTUP->FAILED\n");
	    info->poll_mode = POLL_FAILED;
	    info->poll_count = 0;
	    break;
	}

	/*
	 * Transition the server to operational.  Do a number of minimum
	 * interval polls to try to get a good offset calculation quickly.
	 */
	if (info->poll_count)
	    client_setserverstate(info, 1, "connected ok");
	if (info->poll_count < POLL_STARTUP_MAX) {
	    info->poll_sleep = min_sleep_opt;
	    break;
	}

	/*
	 * Once we've got our polls fall through to aquisition mode to
	 * do aquisition processing.
	 */
	info->poll_mode = POLL_ACQUIRE;
	info->poll_count = 0;
	logdebuginfo(info, 2, "polling mode STARTUP->ACQUIRE\n");
	/* fall through */
    case POLL_ACQUIRE:
	/*
	 * Transition to a FAILED state if too many poll failures occured.
	 */
	if (info->poll_failed >= POLL_FAIL_RESET) {
	    logdebuginfo(info, 2, "polling mode STARTUP->FAILED\n");
	    info->poll_mode = POLL_FAILED;
	    info->poll_count = 0;
	    break;
	}

	/*
	 * Acquisition mode using the nominal timeout.  We do not shift
	 * to maintainance mode unless the correlation is at least 0.90
	 */
	if (info->poll_count < POLL_ACQUIRE_MAX ||
	    info->lin_count < 8 ||
	    fabs(info->lin_cache_corr) < 0.85
	) {
	    if (info->poll_count >= POLL_ACQUIRE_MAX && 
		info->lin_count == LIN_RESTART - 2
	    ) {
		logdebuginfo(info, 2, 
		    "WARNING: Unable to shift this source to "
		    "maintenance mode.  Target correlation is aweful\n");
	    }
	    break;
	}
	info->poll_mode = POLL_MAINTAIN;
	info->poll_count = 0;
	logdebuginfo(info, 2, "polling mode ACQUIRE->MAINTAIN\n");
	/* fall through */
    case POLL_MAINTAIN:
	/*
	 * Transition to a FAILED state if too many poll failures occured.
	 */
	if (info->poll_failed >= POLL_FAIL_RESET) {
	    logdebuginfo(info, 2, "polling mode STARTUP->FAILED\n");
	    info->poll_mode = POLL_FAILED;
	    info->poll_count = 0;
	    break;
	}

	/*
	 * Maintaince mode, max polling interval.
	 *
	 * Transition back to acquisition mode if we are unable to maintain
	 * this mode due to the correlation going bad.
	 */
	if (info->lin_count >= LIN_RESTART / 2 && 
	    fabs(info->lin_cache_corr) < 0.70
	) {
	    logdebuginfo(info, 2, 
		"polling mode MAINTAIN->ACQUIRE.  Unable to maintain\n"
		"the maintenance mode because the correlation went"
		" bad!\n");
	    info->poll_mode = POLL_ACQUIRE;
	    info->poll_count = 0;
	    break;
	}
	info->poll_sleep = max_sleep_opt;
	break;
    case POLL_FAILED:
	/*
	 * We have a communications failure.  A late recovery is possible 
	 * if we enter this state with a good poll.
	 */
	if (info->poll_count != 0) {
	    logdebuginfo(info, 2, "polling mode FAILED->ACQUIRE\n");
	    if (info->poll_failed >= POLL_FAIL_RESET)
		info->poll_mode = POLL_STARTUP;
	    else
		info->poll_mode = POLL_ACQUIRE;
	    /* do not reset poll_count */
	    break;
	}

	/*
	 * If we have been failed too long, disconnect from the server
	 * and start us all over again.  Note that the failed count is not
	 * reset to 0.
	 */
	if (info->poll_failed >= POLL_RECOVERY_RESTART) {
	    logdebuginfo(info, 2, "polling mode FAILED->INIT\n");
	    client_setserverstate(info, 0, "FAILED");
	    disconnect_server(info);
	    info->poll_mode = POLL_FIXED;
	    break;
	}
	break;
    }

    /*
     * If the above state machine has not set a polling interval, set a
     * nominal polling interval.
     */
    if (info->poll_sleep == 0)
	info->poll_sleep = nom_sleep_opt;
}

/*
 * Look for duplicate IP addresses.  This is done very inoften, so we do
 * not use a particularly efficient algorithm.
 *
 * Only reconnect a client which has not done its initial poll.
 */
void
client_check_duplicate_ips(struct server_info **info_ary, int count)
{
    server_info_t info1;
    server_info_t info2;
    int tries;
    int i;
    int j;

    for (i = 0; i < count; ++i) {
	info1 = info_ary[i];
	if (info1->fd < 0 || info1->server_state != 0)
	    continue;
	for (tries = 0; tries < 10; ++tries) {
	    for (j = 0; j < count; ++j) {
		info2 = info_ary[j];
		if (i == j || info2->fd < 0)
		    continue;
		if (info1->fd < 0 || /* info1 was lost in previous reconnect */
		    strcmp(info1->ipstr, info2->ipstr) == 0) {
		    reconnect_server(info1);
		    break;
		}
	    }
	    if (j == count)
		break;
	}
	if (tries == 10) {
	    disconnect_server(info1);
	    client_setserverstate(info1, -2,
				  "permanently disabling duplicate server");
	}
    }
}

/*
 * Calculate whether the server pointed to by *bestp is insane or not.
 * For some reason some servers in e.g. the ntp pool are sometimes an hour
 * off.  If we have at least three servers in the pool require that a
 * quorum agree that the current best server's offset is reasonable.
 *
 * Allow +/- 0.5 seconds of error for now (settable with option).
 *
 * Returns -1 if insane, 0 if not enough samples, and 1 if ok
 */
static
int
client_insane(struct server_info **info_ary, int count, server_info_t best)
{
    server_info_t info;
    double best_offset;
    double info_offset;
    int good;
    int bad;
    int skip;
    int quorum;
    int i;

    /*
     * If only one ntp server we cannot check to see if it is insane
     */
    if (count < 2)
	    return(1);
    best_offset = best->lin_sumoffset / best->lin_countoffset;

    /*
     * Calculated the quorum.  Do not count permanently failed servers
     * in the calculation.
     *
     * adjusted count	quorum
     *   2		  2
     *   3		  2
     *   4		  3
     *   5		  3
     */
    quorum = count;
    for (i = 0; i < count; ++i) {
	info = info_ary[i];
	if (info->server_state == -2)
	    --quorum;
    }

    quorum = quorum / 2 + 1;
    good = 0;
    bad = 0;
    skip = 0;

    /*
     * Find the good, the bad, and the ugly.  We need at least four samples
     * and a stddev within the deviation being checked to count a server
     * in the calculation.
     */
    for (i = 0; i < count; ++i) {
	info = info_ary[i];
	if (info->lin_countoffset < 4 ||
	    info->lin_cache_stddev > insane_deviation
	) {
	    ++skip;
	    continue;
	}

	info_offset = info->lin_sumoffset / info->lin_countoffset;
	info_offset -= best_offset;
	if (info_offset < -insane_deviation || info_offset > insane_deviation)
		++bad;
	else
		++good;
    }

    /*
     * Did we meet our quorum?
     */
    logdebuginfo(best, 5, "insanecheck good=%d bad=%d skip=%d "
			  "quorum=%d (allowed=%-+8.6f)\n",
		 good, bad, skip, quorum, insane_deviation);
    if (good >= quorum)
	return(1);
    if (good + skip >= quorum)
	return(0);
    return(-1);
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
     * and correlation from the linear regression.
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
	logdebuginfo(info, 4, "iter=%2d time=%7.3f off=%+.6f uoff=%+.6f",
	    (int)info->lin_count,
	    time_axis, offset, uncorrected_offset);
	if (info->lin_count > 1) {
	    logdebug(4, " slope %+7.6f"
			    " yint %+3.2f corr %+7.6f freq_ppm %+4.2f", 
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

void
client_setserverstate(server_info_t info, int state, const char *str)
{
    if (info->server_state != state) {
        info->server_state = state;
	logdebuginfo(info, 1, "%s\n", str);
    }
}

