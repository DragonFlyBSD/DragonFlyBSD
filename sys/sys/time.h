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

#ifdef _KERNEL
#include <sys/types.h>
#include <machine/clock.h>
#else
#include <machine/stdint.h>
#endif

#include <sys/_clock_id.h>
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

/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define	timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
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

#endif /* !_KERNEL */

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
void	getmicrouptime(struct timeval *tv);
void	getmicrotime(struct timeval *tv);
void	getnanouptime(struct timespec *tv);
void	getnanotime(struct timespec *tv);
int	itimerdecr(struct itimerval *itp, int usec);
int	itimerfix(struct timeval *tv);
int	itimespecfix(struct timespec *ts);
int 	ppsratecheck(struct timeval *, int *, int usec);
int 	ratecheck(struct timeval *, const struct timeval *);
void	microuptime(struct timeval *tv);
void	microtime(struct timeval *tv);
void	nanouptime(struct timespec *ts);
void	nanotime(struct timespec *ts);
time_t	get_approximate_time_t(void);
void	set_timeofday(struct timespec *ts);
void	kern_adjtime(int64_t, int64_t *);
void	kern_reladjtime(int64_t);
void	timevaladd(struct timeval *, const struct timeval *);
void	timevalsub(struct timeval *, const struct timeval *);
int	tvtohz_high(struct timeval *);
int	tvtohz_low(struct timeval *);
int	tstohz_high(struct timespec *);
int	tstohz_low(struct timespec *);
int	tsc_test_target(int64_t target);
void	tsc_delay(int ns);
int	clock_nanosleep1(clockid_t clock_id, int flags,
		struct timespec *rqt, struct timespec *rmt);
int	nanosleep1(struct timespec *rqt, struct timespec *rmt);

void	timespec2fattime(const struct timespec *tsp, int utc, uint16_t *ddp,
		uint16_t *dtp, uint8_t *dhp);
void	fattime2timespec(unsigned dd, unsigned dt, unsigned dh, int utc,
		struct timespec *tsp);

tsc_uclock_t tsc_get_target(int ns);

#else /* !_KERNEL */

#include <time.h>
#include <sys/cdefs.h>

#endif /* _KERNEL */

__BEGIN_DECLS

#if __BSD_VISIBLE
int	adjtime(const struct timeval *, struct timeval *);
int	futimes(int, const struct timeval *);
int	futimesat(int fd, const char *path, const struct timeval times[2]);
int	lutimes(const char *, const struct timeval *);
int	settimeofday(const struct timeval *, const struct timezone *);

#define HAVE_FUTIMESAT	1

#endif

#if __XSI_VISIBLE
int	getitimer(int, struct itimerval *);
int	gettimeofday(struct timeval * __restrict, struct timezone * __restrict);
int	setitimer(int, const struct itimerval * __restrict,
	    struct itimerval * __restrict);
int	utimes(const char *, const struct timeval *);
#endif

__END_DECLS

#endif /* !_SYS_TIME_H_ */
