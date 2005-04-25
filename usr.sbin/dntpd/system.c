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
 * $DragonFly: src/usr.sbin/dntpd/system.c,v 1.3 2005/04/25 20:50:59 dillon Exp $
 */

#include "defs.h"
#include <sys/sysctl.h>

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
    int ts_size;

    ts_size = sizeof(ts);
    error = sysctlbyname("kern.basetime", &ts, &ts_size, NULL, 0);
    if (error < 0) {
	logerr("sysctlbyname(\"kern.basetime\") failed, cannot continue");
	exit(1);
    }
    ts_to_tv(&ts, tvp);
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
	logdebug(1, "issuing offset adjustment: %7.6f\n", -offset);
	delta = -(int64_t)(offset * 1.0E+9);
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
	if (settimeofday(&tv, NULL) < 0) {
	    logerr("settimeofday");
	} else {
	    logdebug(1, "issuing COURSE offset adjustment: %7.6f, ",
		    offset);
	    t = tv.tv_sec;
	    tp = localtime(&t);
	    strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tp);
	    logdebug(1, "%s.%03ld\n", buf, tv.tv_usec / 1000);
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
	logdebug(loglevel, "issuing frequency adjustment: %6.3fppm\n", 
		-freq * 1000000.0);
	delta = -((int64_t)(freq * 1.0E+9) << 32);
	sysctlbyname("kern.ntp.permanent", NULL, 0, &delta, sizeof(delta));
    }
}

