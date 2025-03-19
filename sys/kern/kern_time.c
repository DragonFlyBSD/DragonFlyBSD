/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)kern_time.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/kern_time.c,v 1.68.2.1 2002/10/01 08:00:41 bde Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/sysmsg.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/sysent.h>
#include <sys/proc.h>
#include <sys/caps.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/kern_syscall.h>
#include <sys/upmap.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/msgport2.h>
#include <sys/spinlock2.h>
#include <sys/thread2.h>

extern struct spinlock ntp_spin;

#define	CPUCLOCK_BIT			0x80000000
#define	CPUCLOCK_ID_MASK		~CPUCLOCK_BIT
#define	CPUCLOCK2LWPID(clock_id)	\
	(((clockid_t)(clock_id) >> 32) & CPUCLOCK_ID_MASK)
#define	CPUCLOCK2PID(clock_id)		\
	((clock_id) & CPUCLOCK_ID_MASK)
#define	MAKE_CPUCLOCK(pid, lwp_id)	\
	((clockid_t)(lwp_id) << 32 | (pid) | CPUCLOCK_BIT)

struct timezone tz;

/*
 * Time of day and interval timer support.
 *
 * These routines provide the kernel entry points to get and set
 * the time-of-day and per-process interval timers.  Subroutines
 * here provide support for adding and subtracting timeval structures
 * and decrementing interval timers, optionally reloading the interval
 * timers when they expire.
 */

static int	settime(struct timeval *);
static void	timevalfix(struct timeval *);
static void	realitexpire(void *arg);

static int sysctl_gettimeofday_quick(SYSCTL_HANDLER_ARGS);


/*
 * Nanosleep tries very hard to sleep for a precisely requested time
 * interval, down to 1uS.  The administrator can impose a minimum delay
 * and a delay below which we hard-loop instead of initiate a timer
 * interrupt and sleep.
 *
 * For machines under high loads it might be beneficial to increase min_us
 * to e.g. 1000uS (1ms) so spining processes sleep meaningfully.
 */
static int nanosleep_min_us = 10;
static int nanosleep_hard_us = 100;
static int gettimeofday_quick = 0;
SYSCTL_INT(_kern, OID_AUTO, nanosleep_min_us, CTLFLAG_RW,
	   &nanosleep_min_us, 0, "");
SYSCTL_INT(_kern, OID_AUTO, nanosleep_hard_us, CTLFLAG_RW,
	   &nanosleep_hard_us, 0, "");
SYSCTL_PROC(_kern, OID_AUTO, gettimeofday_quick, CTLTYPE_INT | CTLFLAG_RW,
	    0, 0, sysctl_gettimeofday_quick, "I", "Quick mode gettimeofday");

static struct lock masterclock_lock = LOCK_INITIALIZER("mstrclk", 0, 0);

static int
settime(struct timeval *tv)
{
	struct timeval delta, tv1, tv2;
	static struct timeval maxtime, laststep;
	struct timespec ts;
	int origcpu;

	if ((origcpu = mycpu->gd_cpuid) != 0)
		lwkt_setcpu_self(globaldata_find(0));

	crit_enter();
	microtime(&tv1);
	delta = *tv;
	timevalsub(&delta, &tv1);

	/*
	 * If the system is secure, we do not allow the time to be
	 * set to a value earlier than 1 second less than the highest
	 * time we have yet seen. The worst a miscreant can do in
	 * this circumstance is "freeze" time. He couldn't go
	 * back to the past.
	 *
	 * We similarly do not allow the clock to be stepped more
	 * than one second, nor more than once per second. This allows
	 * a miscreant to make the clock march double-time, but no worse.
	 */
	if (securelevel > 1) {
		if (delta.tv_sec < 0 || delta.tv_usec < 0) {
			/*
			 * Update maxtime to latest time we've seen.
			 */
			if (tv1.tv_sec > maxtime.tv_sec)
				maxtime = tv1;
			tv2 = *tv;
			timevalsub(&tv2, &maxtime);
			if (tv2.tv_sec < -1) {
				tv->tv_sec = maxtime.tv_sec - 1;
				kprintf("Time adjustment clamped to -1 second\n");
			}
		} else {
			if (tv1.tv_sec == laststep.tv_sec) {
				crit_exit();
				return (EPERM);
			}
			if (delta.tv_sec > 1) {
				tv->tv_sec = tv1.tv_sec + 1;
				kprintf("Time adjustment clamped to +1 second\n");
			}
			laststep = *tv;
		}
	}

	ts.tv_sec = tv->tv_sec;
	ts.tv_nsec = tv->tv_usec * 1000;
	set_timeofday(&ts);
	crit_exit();

	if (origcpu != 0)
		lwkt_setcpu_self(globaldata_find(origcpu));

	resettodr();
	return (0);
}

static void
get_process_cputime(struct proc *p, struct timespec *ats)
{
	struct rusage ru;

	lwkt_gettoken(&p->p_token);
	calcru_proc(p, &ru);
	lwkt_reltoken(&p->p_token);
	timevaladd(&ru.ru_utime, &ru.ru_stime);
	TIMEVAL_TO_TIMESPEC(&ru.ru_utime, ats);
}

static void
get_process_usertime(struct proc *p, struct timespec *ats)
{
	struct rusage ru;

	lwkt_gettoken(&p->p_token);
	calcru_proc(p, &ru);
	lwkt_reltoken(&p->p_token);
	TIMEVAL_TO_TIMESPEC(&ru.ru_utime, ats);
}

static void
get_thread_cputime(struct thread *td, struct timespec *ats)
{
	struct timeval sys, user;

	calcru(td->td_lwp, &user, &sys);
	timevaladd(&user, &sys);
	TIMEVAL_TO_TIMESPEC(&user, ats);
}

/*
 * MPSAFE
 */
int
kern_clock_gettime(clockid_t clock_id, struct timespec *ats)
{
	struct proc *p;
	struct lwp *lp;
	lwpid_t lwp_id;

	p = curproc;
	switch(clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_PRECISE:
		nanotime(ats);
		break;
	case CLOCK_REALTIME_FAST:
		getnanotime(ats);
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_PRECISE:
	case CLOCK_UPTIME:
	case CLOCK_UPTIME_PRECISE:
		nanouptime(ats);
		break;
	case CLOCK_MONOTONIC_FAST:
	case CLOCK_UPTIME_FAST:
		getnanouptime(ats);
		break;
	case CLOCK_VIRTUAL:
		get_process_usertime(p, ats);
		break;
	case CLOCK_PROF:
	case CLOCK_PROCESS_CPUTIME_ID:
		get_process_cputime(p, ats);
		break;
	case CLOCK_SECOND:
		ats->tv_sec = time_second;
		ats->tv_nsec = 0;
		break;
	case CLOCK_THREAD_CPUTIME_ID:
		get_thread_cputime(curthread, ats);
		break;
	default:
		if ((clock_id & CPUCLOCK_BIT) == 0)
			return (EINVAL);
		if ((p = pfind(CPUCLOCK2PID(clock_id))) == NULL)
			return (EINVAL);
		lwp_id = CPUCLOCK2LWPID(clock_id);
		if (lwp_id == 0) {
			get_process_cputime(p, ats);
		} else {
			lwkt_gettoken(&p->p_token);
			lp = lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, lwp_id);
			if (lp == NULL) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				return (EINVAL);
			}
			get_thread_cputime(lp->lwp_thread, ats);
			lwkt_reltoken(&p->p_token);
		}
		PRELE(p);
	}
	return (0);
}

/*
 * MPSAFE
 */
int
sys_clock_gettime(struct sysmsg *sysmsg, const struct clock_gettime_args *uap)
{
	struct timespec ats;
	int error;

	error = kern_clock_gettime(uap->clock_id, &ats);
	if (error == 0)
		error = copyout(&ats, uap->tp, sizeof(ats));

	return (error);
}

int
kern_clock_settime(clockid_t clock_id, struct timespec *ats)
{
	struct timeval atv;
	int error;

	if ((error = caps_priv_check_self(SYSCAP_NOSETTIME)) != 0)
		return (error);
	if (clock_id != CLOCK_REALTIME)
		return (EINVAL);
	if (ats->tv_sec < 0 || ats->tv_nsec < 0 || ats->tv_nsec >= 1000000000)
		return (EINVAL);

	lockmgr(&masterclock_lock, LK_EXCLUSIVE);
	TIMESPEC_TO_TIMEVAL(&atv, ats);
	error = settime(&atv);
	lockmgr(&masterclock_lock, LK_RELEASE);

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_clock_settime(struct sysmsg *sysmsg, const struct clock_settime_args *uap)
{
	struct timespec ats;
	int error;

	if ((error = copyin(uap->tp, &ats, sizeof(ats))) != 0)
		return (error);

	error = kern_clock_settime(uap->clock_id, &ats);

	return (error);
}

/*
 * MPSAFE
 */
int
kern_clock_getres(clockid_t clock_id, struct timespec *ts)
{
	ts->tv_sec = 0;

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_FAST:
	case CLOCK_REALTIME_PRECISE:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_FAST:
	case CLOCK_MONOTONIC_PRECISE:
	case CLOCK_UPTIME:
	case CLOCK_UPTIME_FAST:
	case CLOCK_UPTIME_PRECISE:
		/*
		 * Minimum reportable resolution is 1ns.  Rounding is
		 * otherwise unimportant.
		 */
		ts->tv_nsec = 999999999 / sys_cputimer->freq + 1;
		break;
	case CLOCK_VIRTUAL:
	case CLOCK_PROF:
		/* Accurately round up here because we can do so cheaply. */
		ts->tv_nsec = howmany(1000000000, hz);
		break;
	case CLOCK_SECOND:
		ts->tv_sec = 1;
		ts->tv_nsec = 0;
		break;
	case CLOCK_THREAD_CPUTIME_ID:
	case CLOCK_PROCESS_CPUTIME_ID:
		ts->tv_nsec = 1000;
		break;
	default:
		if ((clock_id & CPUCLOCK_BIT) == CPUCLOCK_BIT) {
			pid_t pid = CPUCLOCK2PID(clock_id);
			if (pid < 2 || pid > PID_MAX)
				return (EINVAL);
			ts->tv_nsec = 1000;
		} else {
			return (EINVAL);
		}
	}

	return (0);
}

/*
 * MPSAFE
 */
int
sys_clock_getres(struct sysmsg *sysmsg, const struct clock_getres_args *uap)
{
	int error;
	struct timespec ts;

	error = kern_clock_getres(uap->clock_id, &ts);
	if (error == 0)
		error = copyout(&ts, uap->tp, sizeof(ts));

	return (error);
}

static int
kern_getcpuclockid(pid_t pid, lwpid_t lwp_id, clockid_t *clock_id)
{
	struct proc *p;
	int error = 0;

	if (pid == 0) {
		p = curproc;
		pid = p->p_pid;
		PHOLD(p);
	} else {
		p = pfind(pid);
		if (p == NULL)
			return (ESRCH);
	}
	/* lwp_id can be 0 when called by clock_getcpuclockid() */
	if (lwp_id < 0) {
		error = EINVAL;
		goto out;
	}
	lwkt_gettoken(&p->p_token);
	if (lwp_id > 0 &&
	    lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, lwp_id) == NULL) {
		lwkt_reltoken(&p->p_token);
		error = ESRCH;
		goto out;
	}
	*clock_id = MAKE_CPUCLOCK(pid, lwp_id);
	lwkt_reltoken(&p->p_token);
out:
	PRELE(p);
	return (error);
}

int
sys_getcpuclockid(struct sysmsg *sysmsg, const struct getcpuclockid_args *uap)
{
	clockid_t clk_id;
	int error;

	error = kern_getcpuclockid(uap->pid, uap->lwp_id, &clk_id);
	if (error == 0)
		error = copyout(&clk_id, uap->clock_id, sizeof(clockid_t));

	return (error);
}

/*
 * clock_nanosleep1()
 *
 *	This is a general helper function for clock_nanosleep() and
 *	nanosleep() (aka sleep(), aka usleep()).
 *
 *	If there is less than one tick's worth of time left and
 *	we haven't done a yield, or the remaining microseconds is
 *	ridiculously low, do a yield.  This avoids having
 *	to deal with systimer overheads when the system is under
 *	heavy loads.  If we have done a yield already then use
 *	a systimer and an uninterruptable thread wait.
 *
 *	If there is more than a tick's worth of time left,
 *	calculate the baseline ticks and use an interruptable
 *	tsleep, then handle the fine-grained delay on the next
 *	loop.  This usually results in two sleeps occuring, a long one
 *	and a short one.
 *
 * MPSAFE
 */
static void
ns1_systimer(systimer_t info, int in_ipi __unused,
    struct intrframe *frame __unused)
{
	lwkt_schedule(info->data);
}

int
clock_nanosleep1(clockid_t clock_id, int flags,
    struct timespec *rqt, struct timespec *rmt)
{
	static int nanowait;
	struct timespec ts_cur, ts_tgt, ts_int;
	struct timeval tv;
	bool is_abs;
	int error, error2;

	if ((flags & ~(TIMER_RELTIME | TIMER_ABSTIME)) != 0)
		return (EINVAL);
	if (rqt->tv_sec < 0 || rqt->tv_nsec < 0 || rqt->tv_nsec >= 1000000000)
		return (EINVAL);
	if (rqt->tv_sec == 0 && rqt->tv_nsec == 0)
		return (0);

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_FAST:
	case CLOCK_REALTIME_PRECISE:
	case CLOCK_SECOND:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_FAST:
	case CLOCK_MONOTONIC_PRECISE:
	case CLOCK_UPTIME:
	case CLOCK_UPTIME_FAST:
	case CLOCK_UPTIME_PRECISE:
		is_abs = (flags & TIMER_ABSTIME) != 0;
		break;
	case CLOCK_VIRTUAL:
	case CLOCK_PROF:
	case CLOCK_PROCESS_CPUTIME_ID:
		return (ENOTSUP);
	case CLOCK_THREAD_CPUTIME_ID:
	default:
		return (EINVAL);
	}

	error = kern_clock_gettime(clock_id, &ts_cur);
	if (error)
		return (error);

	if (is_abs) {
		if (timespeccmp(&ts_cur, rqt, >=))
			return (0);

		ts_tgt = *rqt; /* target timestamp */
		timespecsub(&ts_tgt, &ts_cur, &ts_int); /* sleep interval */
	} else {
		ts_int = *rqt; /* sleep interval */
		timespecadd(&ts_cur, &ts_int, &ts_tgt); /* target timestamp */
	}

	for (;;) {
		int ticks;
		struct systimer info;
		thread_t td;

		timespecsub(&ts_tgt, &ts_cur, &ts_int);
		TIMESPEC_TO_TIMEVAL(&tv, &ts_int);
		ticks = tv.tv_usec / ustick; /* approximate */

		if (tv.tv_sec == 0 && ticks == 0) {
			td = curthread;
			if (tv.tv_usec > 0 && tv.tv_usec < nanosleep_min_us)
				tv.tv_usec = nanosleep_min_us;
			if (tv.tv_usec < nanosleep_hard_us) {
				lwkt_user_yield();
				cpu_pause();
			} else {
				crit_enter_quick(td);
				systimer_init_oneshot(&info, ns1_systimer,
						td, tv.tv_usec);
				lwkt_deschedule_self(td);
				crit_exit_quick(td);
				lwkt_switch();
				systimer_del(&info); /* make sure it's gone */
			}
			error = iscaught(td->td_lwp);
		} else if (tv.tv_sec == 0) {
			error = tsleep(&nanowait, PCATCH, "nanslp", ticks);
		} else {
			ticks = tvtohz_low(&tv); /* also handles overflow */
			error = tsleep(&nanowait, PCATCH, "nanslp", ticks);
		}

		error2 = kern_clock_gettime(clock_id, &ts_cur);
		if (error2)
			return (error2);

		if (error && error != EWOULDBLOCK) {
			if (error == ERESTART)
				error = EINTR;
			if (rmt != NULL && !is_abs) {
				timespecsub(&ts_tgt, &ts_cur, &ts_int);
				if (ts_int.tv_sec < 0)
					timespecclear(&ts_int);
				*rmt = ts_int;
			}
			return (error);
		}
		if (timespeccmp(&ts_cur, &ts_tgt, >=))
			return (0);
	}
}

int
nanosleep1(struct timespec *rqt, struct timespec *rmt)
{
	return clock_nanosleep1(CLOCK_REALTIME, TIMER_RELTIME, rqt, rmt);
}

/*
 * MPSAFE
 */
int
sys_clock_nanosleep(struct sysmsg *sysmsg,
    const struct clock_nanosleep_args *uap)
{
	int error;
	bool is_abs;
	struct timespec rqt;
	struct timespec rmt;

	is_abs = (uap->flags & TIMER_ABSTIME) != 0;

	error = copyin(uap->rqtp, &rqt, sizeof(rqt));
	if (error) {
		sysmsg->sysmsg_result = error;
		return (0);
	}

	bzero(&rmt, sizeof(rmt));
	error = clock_nanosleep1(uap->clock_id, uap->flags, &rqt, &rmt);

	/*
	 * copyout the residual if nanosleep was interrupted.
	 */
	if (error == EINTR && uap->rmtp != NULL && !is_abs) {
		int error2;

		error2 = copyout(&rmt, uap->rmtp, sizeof(rmt));
		if (error2)
			error = error2;
	}

	sysmsg->sysmsg_result = error;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_nanosleep(struct sysmsg *sysmsg, const struct nanosleep_args *uap)
{
	int error;
	struct timespec rqt;
	struct timespec rmt;

	error = copyin(uap->rqtp, &rqt, sizeof(rqt));
	if (error)
		return (error);

	bzero(&rmt, sizeof(rmt));
	error = nanosleep1(&rqt, &rmt);

	/*
	 * copyout the residual if nanosleep was interrupted.
	 */
	if (error == EINTR && uap->rmtp != NULL) {
		int error2;

		error2 = copyout(&rmt, uap->rmtp, sizeof(rmt));
		if (error2)
			error = error2;
	}
	return (error);
}

/*
 * The gettimeofday() system call is supposed to return a fine-grained
 * realtime stamp.  However, acquiring a fine-grained stamp can create a
 * bottleneck when multiple cpu cores are trying to accessing e.g. the
 * HPET hardware timer all at the same time, so we have a sysctl that
 * allows its behavior to be changed to a more coarse-grained timestamp
 * which does not have to access a hardware timer.
 */
int
sys_gettimeofday(struct sysmsg *sysmsg, const struct gettimeofday_args *uap)
{
	struct timeval atv;
	int error = 0;

	if (uap->tp) {
		if (gettimeofday_quick)
			getmicrotime(&atv);
		else
			microtime(&atv);
		if ((error = copyout((caddr_t)&atv, (caddr_t)uap->tp,
		    sizeof (atv))))
			return (error);
	}
	if (uap->tzp)
		error = copyout((caddr_t)&tz, (caddr_t)uap->tzp,
		    sizeof (tz));
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_settimeofday(struct sysmsg *sysmsg, const struct settimeofday_args *uap)
{
	struct timeval atv;
	struct timezone atz;
	int error;

	if ((error = caps_priv_check_self(SYSCAP_NOSETTIME)))
		return (error);
	/*
	 * Verify all parameters before changing time.
	 *
	 * XXX: We do not allow the time to be set to 0.0, which also by
	 *	happy coincidence works around a pkgsrc bulk build bug.
	 */
	if (uap->tv) {
		if ((error = copyin((caddr_t)uap->tv, (caddr_t)&atv,
		    sizeof(atv))))
			return (error);
		if (atv.tv_usec < 0 || atv.tv_usec >= 1000000)
			return (EINVAL);
		if (atv.tv_sec == 0 && atv.tv_usec == 0)
			return (EINVAL);
	}
	if (uap->tzp &&
	    (error = copyin((caddr_t)uap->tzp, (caddr_t)&atz, sizeof(atz))))
		return (error);

	lockmgr(&masterclock_lock, LK_EXCLUSIVE);
	if (uap->tv && (error = settime(&atv))) {
		lockmgr(&masterclock_lock, LK_RELEASE);
		return (error);
	}
	lockmgr(&masterclock_lock, LK_RELEASE);

	if (uap->tzp)
		tz = atz;
	return (0);
}

/*
 * WARNING! Run with ntp_spin held
 */
static void
kern_adjtime_common(void)
{
	if ((ntp_delta >= 0 && ntp_delta < ntp_default_tick_delta) ||
	    (ntp_delta < 0 && ntp_delta > -ntp_default_tick_delta))
		ntp_tick_delta = ntp_delta;
	else if (ntp_delta > ntp_big_delta)
		ntp_tick_delta = 10 * ntp_default_tick_delta;
	else if (ntp_delta < -ntp_big_delta)
		ntp_tick_delta = -10 * ntp_default_tick_delta;
	else if (ntp_delta > 0)
		ntp_tick_delta = ntp_default_tick_delta;
	else
		ntp_tick_delta = -ntp_default_tick_delta;
}

void
kern_adjtime(int64_t delta, int64_t *odelta)
{
	spin_lock(&ntp_spin);
	*odelta = ntp_delta;
	ntp_delta = delta;
	kern_adjtime_common();
	spin_unlock(&ntp_spin);
}

static void
kern_get_ntp_delta(int64_t *delta)
{
	*delta = ntp_delta;
}

void
kern_reladjtime(int64_t delta)
{
	spin_lock(&ntp_spin);
	ntp_delta += delta;
	kern_adjtime_common();
	spin_unlock(&ntp_spin);
}

static void
kern_adjfreq(int64_t rate)
{
	spin_lock(&ntp_spin);
	ntp_tick_permanent = rate;
	spin_unlock(&ntp_spin);
}

/*
 * MPALMOSTSAFE
 */
int
sys_adjtime(struct sysmsg *sysmsg, const struct adjtime_args *uap)
{
	struct timeval atv;
	int64_t ndelta, odelta;
	int error;

	if ((error = caps_priv_check_self(SYSCAP_NOSETTIME)))
		return (error);
	error = copyin(uap->delta, &atv, sizeof(struct timeval));
	if (error)
		return (error);

	/*
	 * Compute the total correction and the rate at which to apply it.
	 * Round the adjustment down to a whole multiple of the per-tick
	 * delta, so that after some number of incremental changes in
	 * hardclock(), tickdelta will become zero, lest the correction
	 * overshoot and start taking us away from the desired final time.
	 */
	ndelta = (int64_t)atv.tv_sec * 1000000000 + atv.tv_usec * 1000;
	kern_adjtime(ndelta, &odelta);

	if (uap->olddelta) {
		atv.tv_sec = odelta / 1000000000;
		atv.tv_usec = odelta % 1000000000 / 1000;
		copyout(&atv, uap->olddelta, sizeof(struct timeval));
	}
	return (0);
}

static int
sysctl_adjtime(SYSCTL_HANDLER_ARGS)
{
	int64_t delta;
	int error;

	if (req->newptr != NULL) {
		if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
			return (EPERM);
		error = SYSCTL_IN(req, &delta, sizeof(delta));
		if (error)
			return (error);
		kern_reladjtime(delta);
	}

	if (req->oldptr)
		kern_get_ntp_delta(&delta);
	error = SYSCTL_OUT(req, &delta, sizeof(delta));
	return (error);
}

/*
 * delta is in nanoseconds.
 */
static int
sysctl_delta(SYSCTL_HANDLER_ARGS)
{
	int64_t delta, old_delta;
	int error;

	if (req->newptr != NULL) {
		if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
			return (EPERM);
		error = SYSCTL_IN(req, &delta, sizeof(delta));
		if (error)
			return (error);
		kern_adjtime(delta, &old_delta);
	}

	if (req->oldptr != NULL)
		kern_get_ntp_delta(&old_delta);
	error = SYSCTL_OUT(req, &old_delta, sizeof(old_delta));
	return (error);
}

/*
 * frequency is in nanoseconds per second shifted left 32.
 * kern_adjfreq() needs it in nanoseconds per tick shifted left 32.
 */
static int
sysctl_adjfreq(SYSCTL_HANDLER_ARGS)
{
	int64_t freqdelta;
	int error;

	if (req->newptr != NULL) {
		if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
			return (EPERM);
		error = SYSCTL_IN(req, &freqdelta, sizeof(freqdelta));
		if (error)
			return (error);

		freqdelta /= hz;
		kern_adjfreq(freqdelta);
	}

	if (req->oldptr != NULL)
		freqdelta = ntp_tick_permanent * hz;
	error = SYSCTL_OUT(req, &freqdelta, sizeof(freqdelta));
	if (error)
		return (error);

	return (0);
}

SYSCTL_NODE(_kern, OID_AUTO, ntp, CTLFLAG_RW, 0, "NTP related controls");
SYSCTL_PROC(_kern_ntp, OID_AUTO, permanent,
    CTLTYPE_QUAD|CTLFLAG_RW, 0, 0,
    sysctl_adjfreq, "Q", "permanent correction per second");
SYSCTL_PROC(_kern_ntp, OID_AUTO, delta,
    CTLTYPE_QUAD|CTLFLAG_RW, 0, 0,
    sysctl_delta, "Q", "one-time delta");
SYSCTL_OPAQUE(_kern_ntp, OID_AUTO, big_delta, CTLFLAG_RD,
    &ntp_big_delta, sizeof(ntp_big_delta), "Q",
    "threshold for fast adjustment");
SYSCTL_OPAQUE(_kern_ntp, OID_AUTO, tick_delta, CTLFLAG_RD,
    &ntp_tick_delta, sizeof(ntp_tick_delta), "LU",
    "per-tick adjustment");
SYSCTL_OPAQUE(_kern_ntp, OID_AUTO, default_tick_delta, CTLFLAG_RD,
    &ntp_default_tick_delta, sizeof(ntp_default_tick_delta), "LU",
    "default per-tick adjustment");
SYSCTL_OPAQUE(_kern_ntp, OID_AUTO, next_leap_second, CTLFLAG_RW,
    &ntp_leap_second, sizeof(ntp_leap_second), "LU",
    "next leap second");
SYSCTL_INT(_kern_ntp, OID_AUTO, insert_leap_second, CTLFLAG_RW,
    &ntp_leap_insert, 0, "insert or remove leap second");
SYSCTL_PROC(_kern_ntp, OID_AUTO, adjust,
    CTLTYPE_QUAD|CTLFLAG_RW, 0, 0,
    sysctl_adjtime, "Q", "relative adjust for delta");

/*
 * Get value of an interval timer.  The process virtual and
 * profiling virtual time timers are kept in the p_stats area, since
 * they can be swapped out.  These are kept internally in the
 * way they are specified externally: in time until they expire.
 *
 * The real time interval timer is kept in the process table slot
 * for the process, and its value (it_value) is kept as an
 * absolute time rather than as a delta, so that it is easy to keep
 * periodic real-time signals from drifting.
 *
 * Virtual time timers are processed in the hardclock() routine of
 * kern_clock.c.  The real time timer is processed by a timeout
 * routine, called from the softclock() routine.  Since a callout
 * may be delayed in real time due to interrupt processing in the system,
 * it is possible for the real time timeout routine (realitexpire, given below),
 * to be delayed in real time past when it is supposed to occur.  It
 * does not suffice, therefore, to reload the real timer .it_value from the
 * real time timers .it_interval.  Rather, we compute the next time in
 * absolute time the timer should go off.
 *
 * MPALMOSTSAFE
 */
int
sys_getitimer(struct sysmsg *sysmsg, const struct getitimer_args *uap)
{
	struct proc *p = curproc;
	struct timeval ctv;
	struct itimerval aitv;

	if (uap->which > ITIMER_PROF)
		return (EINVAL);
	lwkt_gettoken(&p->p_token);
	if (uap->which == ITIMER_REAL) {
		/*
		 * Convert from absolute to relative time in .it_value
		 * part of real time timer.  If time for real time timer
		 * has passed return 0, else return difference between
		 * current time and time for the timer to go off.
		 */
		aitv = p->p_realtimer;
		if (timevalisset(&aitv.it_value)) {
			getmicrouptime(&ctv);
			if (timevalcmp(&aitv.it_value, &ctv, <))
				timevalclear(&aitv.it_value);
			else
				timevalsub(&aitv.it_value, &ctv);
		}
	} else {
		aitv = p->p_timer[uap->which];
	}
	lwkt_reltoken(&p->p_token);
	return (copyout(&aitv, uap->itv, sizeof (struct itimerval)));
}

/*
 * MPALMOSTSAFE
 */
int
sys_setitimer(struct sysmsg *sysmsg, const struct setitimer_args *uap)
{
	struct itimerval aitv;
	struct timeval ctv;
	struct itimerval *itvp;
	struct proc *p = curproc;
	struct getitimer_args gitargs;
	int error;

	if (uap->which > ITIMER_PROF)
		return (EINVAL);
	itvp = uap->itv;
	if (itvp && (error = copyin((caddr_t)itvp, (caddr_t)&aitv,
	    sizeof(struct itimerval))))
		return (error);

	if (uap->oitv) {
		gitargs.which = uap->which;
		gitargs.itv = uap->oitv;
		error = sys_getitimer(sysmsg, &gitargs);
		if (error)
			return error;
	}
	if (itvp == NULL)
		return (0);
	if (itimerfix(&aitv.it_value))
		return (EINVAL);
	if (!timevalisset(&aitv.it_value))
		timevalclear(&aitv.it_interval);
	else if (itimerfix(&aitv.it_interval))
		return (EINVAL);
	lwkt_gettoken(&p->p_token);
	if (uap->which == ITIMER_REAL) {
		if (timevalisset(&p->p_realtimer.it_value))
			callout_cancel(&p->p_ithandle);
		if (timevalisset(&aitv.it_value))
			callout_reset(&p->p_ithandle,
			    tvtohz_high(&aitv.it_value), realitexpire, p);
		getmicrouptime(&ctv);
		timevaladd(&aitv.it_value, &ctv);
		p->p_realtimer = aitv;
	} else {
		p->p_timer[uap->which] = aitv;
		switch(uap->which) {
		case ITIMER_VIRTUAL:
			p->p_flags &= ~P_SIGVTALRM;
			break;
		case ITIMER_PROF:
			p->p_flags &= ~P_SIGPROF;
			break;
		}
	}
	lwkt_reltoken(&p->p_token);
	return (0);
}

/*
 * Real interval timer expired:
 * send process whose timer expired an alarm signal.
 * If time is not set up to reload, then just return.
 * Else compute next time timer should go off which is > current time.
 * This is where delay in processing this timeout causes multiple
 * SIGALRM calls to be compressed into one.
 * tvtohz_high() always adds 1 to allow for the time until the next clock
 * interrupt being strictly less than 1 clock tick, but we don't want
 * that here since we want to appear to be in sync with the clock
 * interrupt even when we're delayed.
 */
static
void
realitexpire(void *arg)
{
	struct proc *p;
	struct timeval ctv, ntv;

	p = (struct proc *)arg;
	PHOLD(p);
	lwkt_gettoken(&p->p_token);
	ksignal(p, SIGALRM);
	if (!timevalisset(&p->p_realtimer.it_interval)) {
		timevalclear(&p->p_realtimer.it_value);
		goto done;
	}
	for (;;) {
		timevaladd(&p->p_realtimer.it_value,
			   &p->p_realtimer.it_interval);
		getmicrouptime(&ctv);
		if (timevalcmp(&p->p_realtimer.it_value, &ctv, >)) {
			ntv = p->p_realtimer.it_value;
			timevalsub(&ntv, &ctv);
			callout_reset(&p->p_ithandle, tvtohz_low(&ntv),
				      realitexpire, p);
			goto done;
		}
	}
done:
	lwkt_reltoken(&p->p_token);
	PRELE(p);
}

/*
 * Used to validate itimer timeouts and utimes*() timespecs.
 */
int
itimerfix(struct timeval *tv)
{
	if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000)
		return (EINVAL);
	if (tv->tv_sec == 0 && tv->tv_usec != 0 && tv->tv_usec < ustick)
		tv->tv_usec = ustick;
	return (0);
}

/*
 * Used to validate timeouts and utimes*() timespecs.
 */
int
itimespecfix(struct timespec *ts)
{
	if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000ULL)
		return (EINVAL);
	if (ts->tv_sec == 0 && ts->tv_nsec != 0 && ts->tv_nsec < nstick)
		ts->tv_nsec = nstick;
	return (0);
}

/*
 * Decrement an interval timer by a specified number
 * of microseconds, which must be less than a second,
 * i.e. < 1000000.  If the timer expires, then reload
 * it.  In this case, carry over (usec - old value) to
 * reduce the value reloaded into the timer so that
 * the timer does not drift.  This routine assumes
 * that it is called in a context where the timers
 * on which it is operating cannot change in value.
 */
int
itimerdecr(struct itimerval *itp, int usec)
{

	if (itp->it_value.tv_usec < usec) {
		if (itp->it_value.tv_sec == 0) {
			/* expired, and already in next interval */
			usec -= itp->it_value.tv_usec;
			goto expire;
		}
		itp->it_value.tv_usec += 1000000;
		itp->it_value.tv_sec--;
	}
	itp->it_value.tv_usec -= usec;
	usec = 0;
	if (timevalisset(&itp->it_value))
		return (1);
	/* expired, exactly at end of interval */
expire:
	if (timevalisset(&itp->it_interval)) {
		itp->it_value = itp->it_interval;
		itp->it_value.tv_usec -= usec;
		if (itp->it_value.tv_usec < 0) {
			itp->it_value.tv_usec += 1000000;
			itp->it_value.tv_sec--;
		}
	} else
		itp->it_value.tv_usec = 0;		/* sec is already 0 */
	return (0);
}

/*
 * Add and subtract routines for timevals.
 * N.B.: subtract routine doesn't deal with
 * results which are before the beginning,
 * it just gets very confused in this case.
 * Caveat emptor.
 */
void
timevaladd(struct timeval *t1, const struct timeval *t2)
{

	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	timevalfix(t1);
}

void
timevalsub(struct timeval *t1, const struct timeval *t2)
{

	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	timevalfix(t1);
}

static void
timevalfix(struct timeval *t1)
{

	if (t1->tv_usec < 0) {
		t1->tv_sec--;
		t1->tv_usec += 1000000;
	}
	if (t1->tv_usec >= 1000000) {
		t1->tv_sec++;
		t1->tv_usec -= 1000000;
	}
}

/*
 * ratecheck(): simple time-based rate-limit checking.
 */
int
ratecheck(struct timeval *lasttime, const struct timeval *mininterval)
{
	struct timeval tv, delta;
	int rv = 0;

	getmicrouptime(&tv);		/* NB: 10ms precision */
	delta = tv;
	timevalsub(&delta, lasttime);

	/*
	 * check for 0,0 is so that the message will be seen at least once,
	 * even if interval is huge.
	 */
	if (timevalcmp(&delta, mininterval, >=) ||
	    (lasttime->tv_sec == 0 && lasttime->tv_usec == 0)) {
		*lasttime = tv;
		rv = 1;
	}

	return (rv);
}

/*
 * ppsratecheck(): packets (or events) per second limitation.
 *
 * Return 0 if the limit is to be enforced (e.g. the caller
 * should drop a packet because of the rate limitation).
 *
 * maxpps of 0 always causes zero to be returned.  maxpps of -1
 * always causes 1 to be returned; this effectively defeats rate
 * limiting.
 *
 * Note that we maintain the struct timeval for compatibility
 * with other bsd systems.  We reuse the storage and just monitor
 * clock ticks for minimal overhead.
 */
int
ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)
{
	int now;

	/*
	 * Reset the last time and counter if this is the first call
	 * or more than a second has passed since the last update of
	 * lasttime.
	 */
	now = ticks;
	if (lasttime->tv_sec == 0 || (u_int)(now - lasttime->tv_sec) >= hz) {
		lasttime->tv_sec = now;
		*curpps = 1;
		return (maxpps != 0);
	} else {
		(*curpps)++;		/* NB: ignore potential overflow */
		return (maxpps < 0 || *curpps < maxpps);
	}
}

static int
sysctl_gettimeofday_quick(SYSCTL_HANDLER_ARGS)
{
	int error;
	int gtod;

	gtod = gettimeofday_quick;
	error = sysctl_handle_int(oidp, &gtod, 0, req);
	if (error || req->newptr == NULL)
		return error;
	gettimeofday_quick = gtod;
	if (kpmap)
		kpmap->fast_gtod = gtod;
	return 0;
}
