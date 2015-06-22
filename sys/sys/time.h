/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 *	@(#)time.h	8.5 (Berkeley) 5/4/95
 * $FreeBSD: src/sys/sys/time.h,v 1.42 1999/12/29 04:24:48 peter Exp $
 */

#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#include <sys/_timespec.h>
#include <sys/_timeval.h>
#include <sys/select.h>

#define	TIMEVAL_TO_TIMESPEC(tv, ts)					\
	do {								\
		(ts)->tv_sec = (tv)->tv_sec;				\
		(ts)->tv_nsec = (tv)->tv_usec * 1000;			\
	} while (0)
#define	TIMESPEC_TO_TIMEVAL(tv, ts)					\
	do {								\
		(tv)->tv_sec = (ts)->tv_sec;				\
		(tv)->tv_usec = (ts)->tv_nsec / 1000;			\
	} while (0)

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};
#define	DST_NONE	0	/* not on dst */
#define	DST_USA		1	/* USA style dst */
#define	DST_AUST	2	/* Australian style dst */
#define	DST_WET		3	/* Western European dst */
#define	DST_MET		4	/* Middle European dst */
#define	DST_EET		5	/* Eastern European dst */
#define	DST_CAN		6	/* Canada */

#ifdef _KERNEL

/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timespecadd(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec += (uvp)->tv_sec;				\
		(vvp)->tv_nsec += (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	} while (0)
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)

/* Operations on timevals. */

#define	timevalclear(tvp)		((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define	timevalisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timevalcmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))

/* timevaladd and timevalsub are not inlined */

#endif /* _KERNEL */

#ifndef _KERNEL			/* NetBSD/OpenBSD compatible interfaces */

#define	timerclear(tvp)		((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define	timerisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timercmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timeradd(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;	\
		if ((vvp)->tv_usec >= 1000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_usec -= 1000000;			\
		}							\
	} while (0)
#define timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};

/*
 * Getkerninfo clock information structure
 */
struct clockinfo {
	int	hz;		/* clock frequency */
	int	tick;		/* micro-seconds per hz tick */
	int	tickadj;	/* clock skew rate for adjtime() */
	int	stathz;		/* statistics clock frequency */
	int	profhz;		/* profiling clock frequency */
};

/* CLOCK_REALTIME and TIMER_ABSTIME are supposed to be in time.h */

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME		0
#endif
#define CLOCK_VIRTUAL		1
#define CLOCK_PROF		2
#define CLOCK_MONOTONIC		4

#define CLOCK_UPTIME		5	/* from freebsd */
#define CLOCK_UPTIME_PRECISE	7	/* from freebsd */
#define CLOCK_UPTIME_FAST	8	/* from freebsd */
#define CLOCK_REALTIME_PRECISE	9	/* from freebsd */
#define CLOCK_REALTIME_FAST	10	/* from freebsd */
#define CLOCK_MONOTONIC_PRECISE	11	/* from freebsd */
#define CLOCK_MONOTONIC_FAST	12	/* from freebsd */
#define CLOCK_SECOND		13	/* from freebsd */
#define CLOCK_THREAD_CPUTIME_ID		14
#define CLOCK_PROCESS_CPUTIME_ID	15

#define TIMER_RELTIME	0x0	/* relative timer */
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME	0x1	/* absolute timer */
#endif

#ifdef _KERNEL

extern time_t	time_second;		/* simple time_t (can step) */
extern time_t	time_uptime;		/* monotonic simple uptime / seconds */
extern int64_t	ntp_tick_permanent;
extern int64_t	ntp_tick_acc;
extern int64_t	ntp_delta;
extern int64_t	ntp_big_delta;
extern int32_t	ntp_tick_delta;
extern int32_t	ntp_default_tick_delta;
extern time_t	ntp_leap_second;
extern int	ntp_leap_insert;

void	initclocks_pcpu(void);
void	getmicrouptime (struct timeval *tv);
void	getmicrotime (struct timeval *tv);
void	getnanouptime (struct timespec *tv);
void	getnanotime (struct timespec *tv);
int	itimerdecr (struct itimerval *itp, int usec);
int	itimerfix (struct timeval *tv);
int	itimespecfix (struct timespec *ts);
int 	ppsratecheck (struct timeval *, int *, int usec);
int 	ratecheck (struct timeval *, const struct timeval *);
void	microuptime (struct timeval *tv);
void	microtime (struct timeval *tv);
void	nanouptime (struct timespec *ts);
void	nanotime (struct timespec *ts);
time_t	get_approximate_time_t(void);
void	set_timeofday(struct timespec *ts);
void	kern_adjtime(int64_t, int64_t *);
void	kern_reladjtime(int64_t);
void	timevaladd (struct timeval *, const struct timeval *);
void	timevalsub (struct timeval *, const struct timeval *);
int	tvtohz_high (struct timeval *);
int	tvtohz_low (struct timeval *);
int	tstohz_high (struct timespec *);
int	tstohz_low (struct timespec *);
int64_t	tsc_get_target(int ns);
int	tsc_test_target(int64_t target);
void	tsc_delay(int ns);
int	nanosleep1(struct timespec *rqt, struct timespec *rmt);

#else /* !_KERNEL */

#include <time.h>
#include <sys/cdefs.h>

#endif /* !_KERNEL */

__BEGIN_DECLS

#if __BSD_VISIBLE
int	adjtime(const struct timeval *, struct timeval *);
int	futimes(int, const struct timeval *);
int	lutimes(const char *, const struct timeval *);
int	settimeofday(const struct timeval *, const struct timezone *);
#endif

#if __XSI_VISIBLE
int	getitimer(int, struct itimerval *);
int	gettimeofday(struct timeval *, struct timezone *);
int	setitimer(int, const struct itimerval *, struct itimerval *);
int	utimes(const char *, const struct timeval *);
#endif

__END_DECLS

#endif /* !_SYS_TIME_H_ */
