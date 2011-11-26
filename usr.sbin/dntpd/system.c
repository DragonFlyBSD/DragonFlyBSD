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
 * $DragonFly: src/usr.sbin/dntpd/system.c,v 1.9 2007/07/11 00:18:00 swildner Exp $
 */

#include "defs.h"
#include <sys/sysctl.h>
#include <sys/timex.h>

/*
 * If a system has multiple independant time-correcting mechanisms, this
 * function should clear out any corrections on those mechanisms that we
 * will NOT be using.  We can leave a prior correction intact on the
 * mechanism that we ARE using.
 *
 * However, it is usually a good idea to clean out any offset correction
 * that is still in progress anyway.  We leave the frequency correction 
 * intact.
 */
void
sysntp_clear_alternative_corrections(void)
{
    struct timex ntp;
    int64_t offset;

    if (no_update_opt)
	return;

    /*
     * Clear the ntp interface.  We will use the sysctl interface
     * (XXX)
     */
    bzero(&ntp, sizeof(ntp));
    ntp.modes = MOD_OFFSET | MOD_FREQUENCY;
    ntp.offset = 0;
    ntp.freq = 0;
    ntp_adjtime(&ntp);

    /*
     * Clean out any offset still being applied to real time.  Leave
     * any prior frequency correction intact.
     */
    offset = 0;
    sysctlbyname("kern.ntp.delta", NULL, 0, &offset, sizeof(offset));
}

/*
 * Obtain a timestamp that contains ONLY corrections made by the system
 * to the base time.  The actual value of the timestamp is not relevant,
 * only the delta from two queries to this routine.
 *
 * This is used by DNTPD to determine what corrections the system has made
 * to the system's real time so DNTPD can uncorrect them for the purpose
 * of calculating the linear regression.
 */
void
sysntp_getbasetime(struct timeval *tvp)
{
    struct timespec ts;
    int error;
    size_t ts_size;

    ts_size = sizeof(ts);
    error = sysctlbyname("kern.basetime", &ts, &ts_size, NULL, 0);
    if (error < 0) {
	logerr("sysctlbyname(\"kern.basetime\") failed, cannot continue");
	exit(1);
    }
    ts_to_tv(&ts, tvp);
}

/*
 * Return 1 if an offset correction is still running, 0 if it isn't.
 */
int
sysntp_offset_correction_is_running(void)
{
    int64_t delta;
    size_t delta_len;

    delta_len = sizeof(delta);
    if (sysctlbyname("kern.ntp.delta", &delta, &delta_len, NULL, 0) == 0) {
	if (delta != 0)
	    return(1);
    }
    return(0);
}

/*
 * The offset error is passed as seconds per second.  Only fairly small
 * offsets are passed to this function (see sysntp_correct_course_offset()
 * for large corrections).  This function may request that the offset
 * be corrected by shifting the frequency by returning the frequency shift
 * (usually a small number in the 1E-6 range) (NOT YET IMPLEMENTED).
 *
 * The 64 bit delta is calculated as nanoseconds per second.  Since we are
 * passed an offset error we must negate the result to correct the error.
 *
 * Because offset corrections skew the accuracy of the clock while the
 * correction is in progress, we do not want to use them once we are
 * reasonably well synchronized.  We can make small offset corrections
 * by shifting the frequency a bit.  XXX
 */
double
sysntp_correct_offset(double offset)
{
    int64_t delta;

    /*
     * Course correction
     */
    if (offset < -0.001 || offset > 0.001) {
	logdebug(1, "issuing offset adjustment: %7.6fs", -offset);
	if (no_update_opt)
	    logdebug(1, " (UPDATES DISABLED)");
	logdebug(1, "\n");
	delta = -(int64_t)(offset * 1.0E+9);
	if (no_update_opt == 0)
	    sysctlbyname("kern.ntp.delta", NULL, 0, &delta, sizeof(delta));
	return(0.0);
    }

    /*
     * Fine correction - do it by adjusting the frequency.
     * XXX
     */
    return(0.0);
}

/*
 * This function is used for what should be a one-time correction on
 * startup.
 */
double
sysntp_correct_course_offset(double offset)
{
    struct timeval tv;
    struct tm *tp;
    time_t t;
    char buf[64];

    offset = -offset;	/* if we are ahead, correct backwards, and vise versa*/
    if (gettimeofday(&tv, NULL) == 0) {
	tv_add_offset(&tv, offset);
	if (no_update_opt == 0 && settimeofday(&tv, NULL) < 0) {
	    logerr("settimeofday");
	} else {
	    logdebug(1, "issuing COARSE offset adjustment: %7.6fs, ",
		    offset);
	    t = tv.tv_sec;
	    tp = localtime(&t);
	    strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tp);
	    logdebug(1, "%s.%03ld", buf, tv.tv_usec / 1000);
	    if (no_update_opt)
		logdebug(1, " (UPDATES DISABLED)");
	    if (quickset_opt)
		logdebug(1, " (ONE-TIME QUICKSET)");
	    logdebug(1, "\n");
	}
    } else {
	logerr("gettimeofday");
    }
    return(0.0);
}

/*
 * freq is passed as seconds per second.
 *
 * The calculated 64 bit correction is nanoseconds per second shifted
 * left 32.
 *
 * Frequency errors greater then 1 second per second will not be corrected.
 * It doesn't hurt to continue correcting the frequency.
 */
void
sysntp_correct_freq(double freq)
{
    static double last_freq;
    int64_t delta;
    int loglevel;

    if (last_freq == 0.0 || fabs(freq - last_freq) >= 20.0E-6)
	loglevel = 1;
    else if (fabs(freq - last_freq) >= 5.0E-6)
	loglevel = 2;
    else
	loglevel = 3;
    last_freq = freq;

    if (freq >= -1.0 && freq < 1.0) {
	logdebug(loglevel, "issuing frequency adjustment: %6.3fppm", 
		-freq * 1000000.0);
	if (no_update_opt)
		logdebug(loglevel, " (UPDATES DISABLED)");
	logdebug(loglevel, "\n");
	delta = -((int64_t)(freq * 1.0E+9) << 32);
	if (no_update_opt == 0)
	    sysctlbyname("kern.ntp.permanent", NULL, 0, &delta, sizeof(delta));
    }
}

