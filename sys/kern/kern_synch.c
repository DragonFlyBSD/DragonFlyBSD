/*-
 * Copyright (c) 1982, 1986, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)kern_synch.c	8.9 (Berkeley) 5/19/95
 * $FreeBSD: src/sys/kern/kern_synch.c,v 1.87.2.6 2002/10/13 07:29:53 kbyanc Exp $
 * $DragonFly: src/sys/kern/kern_synch.c,v 1.65 2006/09/03 18:29:16 dillon Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/resourcevar.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#ifdef KTRACE
#include <sys/uio.h>
#include <sys/ktrace.h>
#endif
#include <sys/xwait.h>
#include <sys/ktr.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/cpu.h>
#include <machine/ipl.h>
#include <machine/smp.h>

TAILQ_HEAD(tslpque, thread);

static void sched_setup (void *dummy);
SYSINIT(sched_setup, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, sched_setup, NULL)

int	hogticks;
int	lbolt;
int	lbolt_syncer;
int	sched_quantum;		/* Roundrobin scheduling quantum in ticks. */
int	ncpus;
int	ncpus2, ncpus2_shift, ncpus2_mask;
int	safepri;

static struct callout loadav_callout;
static struct callout schedcpu_callout;
MALLOC_DEFINE(M_TSLEEP, "tslpque", "tsleep queues");

#if !defined(KTR_TSLEEP)
#define KTR_TSLEEP	KTR_ALL
#endif
KTR_INFO_MASTER(tsleep);
KTR_INFO(KTR_TSLEEP, tsleep, tsleep_beg, 0, "tsleep enter", 0);
KTR_INFO(KTR_TSLEEP, tsleep, tsleep_end, 0, "tsleep exit", 0);
KTR_INFO(KTR_TSLEEP, tsleep, wakeup_beg, 0, "wakeup enter", 0);
KTR_INFO(KTR_TSLEEP, tsleep, wakeup_end, 0, "wakeup exit", 0);
#define logtsleep(name)	KTR_LOG(tsleep_ ## name)

struct loadavg averunnable =
	{ {0, 0, 0}, FSCALE };	/* load average, of runnable procs */
/*
 * Constants for averages over 1, 5, and 15 minutes
 * when sampling at 5 second intervals.
 */
static fixpt_t cexp[3] = {
	0.9200444146293232 * FSCALE,	/* exp(-1/12) */
	0.9834714538216174 * FSCALE,	/* exp(-1/60) */
	0.9944598480048967 * FSCALE,	/* exp(-1/180) */
};

static void	endtsleep (void *);
static void	unsleep_and_wakeup_thread(struct thread *td);
static void	loadav (void *arg);
static void	schedcpu (void *arg);

/*
 * Adjust the scheduler quantum.  The quantum is specified in microseconds.
 * Note that 'tick' is in microseconds per tick.
 */
static int
sysctl_kern_quantum(SYSCTL_HANDLER_ARGS)
{
	int error, new_val;

	new_val = sched_quantum * tick;
	error = sysctl_handle_int(oidp, &new_val, 0, req);
        if (error != 0 || req->newptr == NULL)
		return (error);
	if (new_val < tick)
		return (EINVAL);
	sched_quantum = new_val / tick;
	hogticks = 2 * sched_quantum;
	return (0);
}

SYSCTL_PROC(_kern, OID_AUTO, quantum, CTLTYPE_INT|CTLFLAG_RW,
	0, sizeof sched_quantum, sysctl_kern_quantum, "I", "");

/*
 * If `ccpu' is not equal to `exp(-1/20)' and you still want to use the
 * faster/more-accurate formula, you'll have to estimate CCPU_SHIFT below
 * and possibly adjust FSHIFT in "param.h" so that (FSHIFT >= CCPU_SHIFT).
 *
 * To estimate CCPU_SHIFT for exp(-1/20), the following formula was used:
 *     1 - exp(-1/20) ~= 0.0487 ~= 0.0488 == 1 (fixed pt, *11* bits).
 *
 * If you don't want to bother with the faster/more-accurate formula, you
 * can set CCPU_SHIFT to (FSHIFT + 1) which will use a slower/less-accurate
 * (more general) method of calculating the %age of CPU used by a process.
 *
 * decay 95% of `p_pctcpu' in 60 seconds; see CCPU_SHIFT before changing 
 */
#define CCPU_SHIFT	11

static fixpt_t ccpu = 0.95122942450071400909 * FSCALE; /* exp(-1/20) */
SYSCTL_INT(_kern, OID_AUTO, ccpu, CTLFLAG_RD, &ccpu, 0, "");

/*
 * kernel uses `FSCALE', userland (SHOULD) use kern.fscale 
 */
static int     fscale __unused = FSCALE;
SYSCTL_INT(_kern, OID_AUTO, fscale, CTLFLAG_RD, 0, FSCALE, "");

/*
 * Recompute process priorities, once a second.
 *
 * Since the userland schedulers are typically event oriented, if the
 * estcpu calculation at wakeup() time is not sufficient to make a
 * process runnable relative to other processes in the system we have
 * a 1-second recalc to help out.
 *
 * This code also allows us to store sysclock_t data in the process structure
 * without fear of an overrun, since sysclock_t are guarenteed to hold 
 * several seconds worth of count.
 *
 * WARNING!  callouts can preempt normal threads.  However, they will not
 * preempt a thread holding a spinlock so we *can* safely use spinlocks.
 */
static int schedcpu_stats(struct proc *p, void *data __unused);
static int schedcpu_resource(struct proc *p, void *data __unused);

static void
schedcpu(void *arg)
{
	allproc_scan(schedcpu_stats, NULL);
	allproc_scan(schedcpu_resource, NULL);
	wakeup((caddr_t)&lbolt);
	wakeup((caddr_t)&lbolt_syncer);
	callout_reset(&schedcpu_callout, hz, schedcpu, NULL);
}

/*
 * General process statistics once a second
 */
static int
schedcpu_stats(struct proc *p, void *data __unused)
{
	crit_enter();
	p->p_swtime++;
	if (p->p_stat == SSLEEP)
		p->p_slptime++;

	/*
	 * Only recalculate processes that are active or have slept
	 * less then 2 seconds.  The schedulers understand this.
	 */
	if (p->p_slptime <= 1) {
		p->p_usched->recalculate(&p->p_lwp);
	} else {
		p->p_pctcpu = (p->p_pctcpu * ccpu) >> FSHIFT;
	}
	crit_exit();
	return(0);
}

/*
 * Resource checks.  XXX break out since ksignal/killproc can block,
 * limiting us to one process killed per second.  There is probably
 * a better way.
 */
static int
schedcpu_resource(struct proc *p, void *data __unused)
{
	u_int64_t ttime;

	crit_enter();
	if (p->p_stat == SIDL || 
	    (p->p_flag & P_ZOMBIE) ||
	    p->p_limit == NULL || 
	    p->p_thread == NULL
	) {
		crit_exit();
		return(0);
	}

	ttime = p->p_thread->td_sticks + p->p_thread->td_uticks;

	switch(plimit_testcpulimit(p->p_limit, ttime)) {
	case PLIMIT_TESTCPU_KILL:
		killproc(p, "exceeded maximum CPU limit");
		break;
	case PLIMIT_TESTCPU_XCPU:
		if ((p->p_flag & P_XCPU) == 0) {
			p->p_flag |= P_XCPU;
			ksignal(p, SIGXCPU);
		}
		break;
	default:
		break;
	}
	crit_exit();
	return(0);
}

/*
 * This is only used by ps.  Generate a cpu percentage use over
 * a period of one second.
 *
 * MPSAFE
 */
void
updatepcpu(struct lwp *lp, int cpticks, int ttlticks)
{
	fixpt_t acc;
	int remticks;

	acc = (cpticks << FSHIFT) / ttlticks;
	if (ttlticks >= ESTCPUFREQ) {
		lp->lwp_pctcpu = acc;
	} else {
		remticks = ESTCPUFREQ - ttlticks;
		lp->lwp_pctcpu = (acc * ttlticks + lp->lwp_pctcpu * remticks) /
				ESTCPUFREQ;
	}
}

/*
 * We're only looking at 7 bits of the address; everything is
 * aligned to 4, lots of things are aligned to greater powers
 * of 2.  Shift right by 8, i.e. drop the bottom 256 worth.
 */
#define TABLESIZE	128
#define LOOKUP(x)	(((intptr_t)(x) >> 8) & (TABLESIZE - 1))

static cpumask_t slpque_cpumasks[TABLESIZE];

/*
 * General scheduler initialization.  We force a reschedule 25 times
 * a second by default.  Note that cpu0 is initialized in early boot and
 * cannot make any high level calls.
 *
 * Each cpu has its own sleep queue.
 */
void
sleep_gdinit(globaldata_t gd)
{
	static struct tslpque slpque_cpu0[TABLESIZE];
	int i;

	if (gd->gd_cpuid == 0) {
		sched_quantum = (hz + 24) / 25;
		hogticks = 2 * sched_quantum;

		gd->gd_tsleep_hash = slpque_cpu0;
	} else {
		gd->gd_tsleep_hash = malloc(sizeof(slpque_cpu0), 
					    M_TSLEEP, M_WAITOK | M_ZERO);
	}
	for (i = 0; i < TABLESIZE; ++i)
		TAILQ_INIT(&gd->gd_tsleep_hash[i]);
}

/*
 * General sleep call.  Suspends the current process until a wakeup is
 * performed on the specified identifier.  The process will then be made
 * runnable with the specified priority.  Sleeps at most timo/hz seconds
 * (0 means no timeout).  If flags includes PCATCH flag, signals are checked
 * before and after sleeping, else signals are not checked.  Returns 0 if
 * awakened, EWOULDBLOCK if the timeout expires.  If PCATCH is set and a
 * signal needs to be delivered, ERESTART is returned if the current system
 * call should be restarted if possible, and EINTR is returned if the system
 * call should be interrupted by the signal (return EINTR).
 *
 * Note that if we are a process, we release_curproc() before messing with
 * the LWKT scheduler.
 *
 * During autoconfiguration or after a panic, a sleep will simply
 * lower the priority briefly to allow interrupts, then return.
 */
int
tsleep(void *ident, int flags, const char *wmesg, int timo)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;		/* may be NULL */
	globaldata_t gd;
	int sig;
	int catch;
	int id;
	int error;
	int oldpri;
	struct callout thandle;

	/*
	 * NOTE: removed KTRPOINT, it could cause races due to blocking
	 * even in stable.  Just scrap it for now.
	 */
	if (cold || panicstr) {
		/*
		 * After a panic, or during autoconfiguration,
		 * just give interrupts a chance, then just return;
		 * don't run any other procs or panic below,
		 * in case this is the idle process and already asleep.
		 */
		splz();
		oldpri = td->td_pri & TDPRI_MASK;
		lwkt_setpri_self(safepri);
		lwkt_switch();
		lwkt_setpri_self(oldpri);
		return (0);
	}
	logtsleep(tsleep_beg);
	gd = td->td_gd;
	KKASSERT(td != &gd->gd_idlethread);	/* you must be kidding! */

	/*
	 * NOTE: all of this occurs on the current cpu, including any
	 * callout-based wakeups, so a critical section is a sufficient
	 * interlock.
	 *
	 * The entire sequence through to where we actually sleep must
	 * run without breaking the critical section.
	 */
	id = LOOKUP(ident);
	catch = flags & PCATCH;
	error = 0;
	sig = 0;

	crit_enter_quick(td);

	KASSERT(ident != NULL, ("tsleep: no ident"));
	KASSERT(p == NULL || p->p_stat == SRUN, ("tsleep %p %s %d",
		ident, wmesg, p->p_stat));

	/*
	 * Setup for the current process (if this is a process). 
	 */
	if (p) {
		if (catch) {
			/*
			 * Early termination if PCATCH was set and a
			 * signal is pending, interlocked with the
			 * critical section.
			 *
			 * Early termination only occurs when tsleep() is
			 * entered while in a normal SRUN state.
			 */
			if ((sig = CURSIG(p)) != 0)
				goto resume;

			/*
			 * Causes ksignal to wake us up when.
			 */
			p->p_flag |= P_SINTR;
		}

		/*
		 * Make sure the current process has been untangled from
		 * the userland scheduler and initialize slptime to start
		 * counting.
		 */
		if (flags & PNORESCHED)
			td->td_flags |= TDF_NORESCHED;
		p->p_usched->release_curproc(&p->p_lwp);
		p->p_slptime = 0;
	}

	/*
	 * Move our thread to the correct queue and setup our wchan, etc.
	 */
	lwkt_deschedule_self(td);
	td->td_flags |= TDF_TSLEEPQ;
	TAILQ_INSERT_TAIL(&gd->gd_tsleep_hash[id], td, td_threadq);
	atomic_set_int(&slpque_cpumasks[id], gd->gd_cpumask);

	td->td_wchan = ident;
	td->td_wmesg = wmesg;
	td->td_wdomain = flags & PDOMAIN_MASK;

	/*
	 * Setup the timeout, if any
	 */
	if (timo) {
		callout_init(&thandle);
		callout_reset(&thandle, timo, endtsleep, td);
	}

	/*
	 * Beddy bye bye.
	 */
	if (p) {
		/*
		 * Ok, we are sleeping.  Place us in the SSLEEP state.
		 */
		KKASSERT((p->p_flag & P_ONRUNQ) == 0);
		p->p_stat = SSLEEP;
		p->p_stats->p_ru.ru_nvcsw++;
		lwkt_switch();

		/*
		 * And when we are woken up, put us back in SRUN.  If we
		 * slept for over a second, recalculate our estcpu.
		 */
		p->p_stat = SRUN;
		if (p->p_slptime)
			p->p_usched->recalculate(&p->p_lwp);
		p->p_slptime = 0;
	} else {
		lwkt_switch();
	}

	/* 
	 * Make sure we haven't switched cpus while we were asleep.  It's
	 * not supposed to happen.  Cleanup our temporary flags.
	 */
	KKASSERT(gd == td->td_gd);
	td->td_flags &= ~TDF_NORESCHED;

	/*
	 * Cleanup the timeout.
	 */
	if (timo) {
		if (td->td_flags & TDF_TIMEOUT) {
			td->td_flags &= ~TDF_TIMEOUT;
			if (sig == 0)
				error = EWOULDBLOCK;
		} else {
			callout_stop(&thandle);
		}
	}

	/*
	 * Since td_threadq is used both for our run queue AND for the
	 * tsleep hash queue, we can't still be on it at this point because
	 * we've gotten cpu back.
	 */
	KASSERT((td->td_flags & TDF_TSLEEPQ) == 0, ("tsleep: impossible thread flags %08x", td->td_flags));
	td->td_wchan = NULL;
	td->td_wmesg = NULL;
	td->td_wdomain = 0;

	/*
	 * Figure out the correct error return
	 */
resume:
	if (p) {
		p->p_flag &= ~(P_BREAKTSLEEP | P_SINTR);
		if (catch && error == 0 && (sig != 0 || (sig = CURSIG(p)))) {
			if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
				error = EINTR;
			else
				error = ERESTART;
		}
	}
	logtsleep(tsleep_end);
	crit_exit_quick(td);
	return (error);
}

/*
 * This is a dandy function that allows us to interlock tsleep/wakeup
 * operations with unspecified upper level locks, such as lockmgr locks,
 * simply by holding a critical section.  The sequence is:
 *
 *	(enter critical section)
 *	(acquire upper level lock)
 *	tsleep_interlock(blah)
 *	(release upper level lock)
 *	tsleep(blah, ...)
 *	(exit critical section)
 *
 * Basically this function sets our cpumask for the ident which informs
 * other cpus that our cpu 'might' be waiting (or about to wait on) the
 * hash index related to the ident.  The critical section prevents another
 * cpu's wakeup() from being processed on our cpu until we are actually
 * able to enter the tsleep().  Thus, no race occurs between our attempt
 * to release a resource and sleep, and another cpu's attempt to acquire
 * a resource and call wakeup.
 *
 * There isn't much of a point to this function unless you call it while
 * holding a critical section.
 */
static __inline void
_tsleep_interlock(globaldata_t gd, void *ident)
{
	int id = LOOKUP(ident);

	atomic_set_int(&slpque_cpumasks[id], gd->gd_cpumask);
}

void
tsleep_interlock(void *ident)
{
	_tsleep_interlock(mycpu, ident);
}

/*
 * Interlocked spinlock sleep.  An exclusively held spinlock must
 * be passed to msleep().  The function will atomically release the
 * spinlock and tsleep on the ident, then reacquire the spinlock and
 * return.
 *
 * This routine is fairly important along the critical path, so optimize it
 * heavily.
 */
int
msleep(void *ident, struct spinlock *spin, int flags,
       const char *wmesg, int timo)
{
	globaldata_t gd = mycpu;
	int error;

	crit_enter_gd(gd);
	_tsleep_interlock(gd, ident);
	spin_unlock_wr_quick(gd, spin);
	error = tsleep(ident, flags, wmesg, timo);
	spin_lock_wr_quick(gd, spin);
	crit_exit_gd(gd);

	return (error);
}

/*
 * Implement the timeout for tsleep.
 *
 * We set P_BREAKTSLEEP to indicate that an event has occured, but
 * we only call setrunnable if the process is not stopped.
 *
 * This type of callout timeout is scheduled on the same cpu the process
 * is sleeping on.  Also, at the moment, the MP lock is held.
 */
static void
endtsleep(void *arg)
{
	thread_t td = arg;
	struct proc *p;

	ASSERT_MP_LOCK_HELD(curthread);
	crit_enter();

	/*
	 * cpu interlock.  Thread flags are only manipulated on
	 * the cpu owning the thread.  proc flags are only manipulated
	 * by the older of the MP lock.  We have both.
	 */
	if (td->td_flags & TDF_TSLEEPQ) {
		td->td_flags |= TDF_TIMEOUT;

		if ((p = td->td_proc) != NULL) {
			p->p_flag |= P_BREAKTSLEEP;
			if ((p->p_flag & P_STOPPED) == 0)
				setrunnable(p);
		} else {
			unsleep_and_wakeup_thread(td);
		}
	}
	crit_exit();
}

/*
 * Unsleep and wakeup a thread.  This function runs without the MP lock
 * which means that it can only manipulate thread state on the owning cpu,
 * and cannot touch the process state at all.
 */
static
void
unsleep_and_wakeup_thread(struct thread *td)
{
	globaldata_t gd = mycpu;
	int id;

#ifdef SMP
	if (td->td_gd != gd) {
		lwkt_send_ipiq(td->td_gd, (ipifunc1_t)unsleep_and_wakeup_thread, td);
		return;
	}
#endif
	crit_enter();
	if (td->td_flags & TDF_TSLEEPQ) {
		td->td_flags &= ~TDF_TSLEEPQ;
		id = LOOKUP(td->td_wchan);
		TAILQ_REMOVE(&gd->gd_tsleep_hash[id], td, td_threadq);
		if (TAILQ_FIRST(&gd->gd_tsleep_hash[id]) == NULL)
			atomic_clear_int(&slpque_cpumasks[id], gd->gd_cpumask);
		lwkt_schedule(td);
	}
	crit_exit();
}

/*
 * Make all processes sleeping on the specified identifier runnable.
 * count may be zero or one only.
 *
 * The domain encodes the sleep/wakeup domain AND the first cpu to check
 * (which is always the current cpu).  As we iterate across cpus
 *
 * This call may run without the MP lock held.  We can only manipulate thread
 * state on the cpu owning the thread.  We CANNOT manipulate process state
 * at all.
 */
static void
_wakeup(void *ident, int domain)
{
	struct tslpque *qp;
	struct thread *td;
	struct thread *ntd;
	globaldata_t gd;
#ifdef SMP
	cpumask_t mask;
	cpumask_t tmask;
	int startcpu;
	int nextcpu;
#endif
	int id;

	crit_enter();
	logtsleep(wakeup_beg);
	gd = mycpu;
	id = LOOKUP(ident);
	qp = &gd->gd_tsleep_hash[id];
restart:
	for (td = TAILQ_FIRST(qp); td != NULL; td = ntd) {
		ntd = TAILQ_NEXT(td, td_threadq);
		if (td->td_wchan == ident && 
		    td->td_wdomain == (domain & PDOMAIN_MASK)
		) {
			KKASSERT(td->td_flags & TDF_TSLEEPQ);
			td->td_flags &= ~TDF_TSLEEPQ;
			TAILQ_REMOVE(qp, td, td_threadq);
			if (TAILQ_FIRST(qp) == NULL) {
				atomic_clear_int(&slpque_cpumasks[id],
						 gd->gd_cpumask);
			}
			lwkt_schedule(td);
			if (domain & PWAKEUP_ONE)
				goto done;
			goto restart;
		}
	}

#ifdef SMP
	/*
	 * We finished checking the current cpu but there still may be
	 * more work to do.  Either wakeup_one was requested and no matching
	 * thread was found, or a normal wakeup was requested and we have
	 * to continue checking cpus.
	 *
	 * The cpu that started the wakeup sequence is encoded in the domain.
	 * We use this information to determine which cpus still need to be
	 * checked, locate a candidate cpu, and chain the wakeup 
	 * asynchronously with an IPI message. 
	 *
	 * It should be noted that this scheme is actually less expensive then
	 * the old scheme when waking up multiple threads, since we send 
	 * only one IPI message per target candidate which may then schedule
	 * multiple threads.  Before we could have wound up sending an IPI
	 * message for each thread on the target cpu (!= current cpu) that
	 * needed to be woken up.
	 *
	 * NOTE: Wakeups occuring on remote cpus are asynchronous.  This
	 * should be ok since we are passing idents in the IPI rather then
	 * thread pointers.
	 */
	if ((domain & PWAKEUP_MYCPU) == 0 && 
	    (mask = slpque_cpumasks[id]) != 0
	) {
		/*
		 * Look for a cpu that might have work to do.  Mask out cpus
		 * which have already been processed.
		 *
		 * 31xxxxxxxxxxxxxxxxxxxxxxxxxxxxx0
		 *        ^        ^           ^
		 *      start   currentcpu    start
		 *      case2                 case1
		 *        *        *           *
		 * 11111111111111110000000000000111	case1
		 * 00000000111111110000000000000000	case2
		 *
		 * case1:  We started at start_case1 and processed through
		 *  	   to the current cpu.  We have to check any bits
		 *	   after the current cpu, then check bits before 
		 *         the starting cpu.
		 *
		 * case2:  We have already checked all the bits from
		 *         start_case2 to the end, and from 0 to the current
		 *         cpu.  We just have the bits from the current cpu
		 *         to start_case2 left to check.
		 */
		startcpu = PWAKEUP_DECODE(domain);
		if (gd->gd_cpuid >= startcpu) {
			/*
			 * CASE1
			 */
			tmask = mask & ~((gd->gd_cpumask << 1) - 1);
			if (mask & tmask) {
				nextcpu = bsfl(mask & tmask);
				lwkt_send_ipiq2(globaldata_find(nextcpu), 
						_wakeup, ident, domain);
			} else {
				tmask = (1 << startcpu) - 1;
				if (mask & tmask) {
					nextcpu = bsfl(mask & tmask);
					lwkt_send_ipiq2(
						    globaldata_find(nextcpu),
						    _wakeup, ident, domain);
				}
			}
		} else {
			/*
			 * CASE2
			 */
			tmask = ~((gd->gd_cpumask << 1) - 1) &
				 ((1 << startcpu) - 1);
			if (mask & tmask) {
				nextcpu = bsfl(mask & tmask);
				lwkt_send_ipiq2(globaldata_find(nextcpu), 
						_wakeup, ident, domain);
			}
		}
	}
#endif
done:
	logtsleep(wakeup_end);
	crit_exit();
}

/*
 * Wakeup all threads tsleep()ing on the specified ident, on all cpus
 */
void
wakeup(void *ident)
{
    _wakeup(ident, PWAKEUP_ENCODE(0, mycpu->gd_cpuid));
}

/*
 * Wakeup one thread tsleep()ing on the specified ident, on any cpu.
 */
void
wakeup_one(void *ident)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(ident, PWAKEUP_ENCODE(0, mycpu->gd_cpuid) | PWAKEUP_ONE);
}

/*
 * Wakeup threads tsleep()ing on the specified ident on the current cpu
 * only.
 */
void
wakeup_mycpu(void *ident)
{
    _wakeup(ident, PWAKEUP_MYCPU);
}

/*
 * Wakeup one thread tsleep()ing on the specified ident on the current cpu
 * only.
 */
void
wakeup_mycpu_one(void *ident)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(ident, PWAKEUP_MYCPU|PWAKEUP_ONE);
}

/*
 * Wakeup all thread tsleep()ing on the specified ident on the specified cpu
 * only.
 */
void
wakeup_oncpu(globaldata_t gd, void *ident)
{
#ifdef SMP
    if (gd == mycpu) {
	_wakeup(ident, PWAKEUP_MYCPU);
    } else {
	lwkt_send_ipiq2(gd, _wakeup, ident, PWAKEUP_MYCPU);
    }
#else
    _wakeup(ident, PWAKEUP_MYCPU);
#endif
}

/*
 * Wakeup one thread tsleep()ing on the specified ident on the specified cpu
 * only.
 */
void
wakeup_oncpu_one(globaldata_t gd, void *ident)
{
#ifdef SMP
    if (gd == mycpu) {
	_wakeup(ident, PWAKEUP_MYCPU | PWAKEUP_ONE);
    } else {
	lwkt_send_ipiq2(gd, _wakeup, ident, PWAKEUP_MYCPU | PWAKEUP_ONE);
    }
#else
    _wakeup(ident, PWAKEUP_MYCPU | PWAKEUP_ONE);
#endif
}

/*
 * Wakeup all threads waiting on the specified ident that slept using
 * the specified domain, on all cpus.
 */
void
wakeup_domain(void *ident, int domain)
{
    _wakeup(ident, PWAKEUP_ENCODE(domain, mycpu->gd_cpuid));
}

/*
 * Wakeup one thread waiting on the specified ident that slept using
 * the specified  domain, on any cpu.
 */
void
wakeup_domain_one(void *ident, int domain)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(ident, PWAKEUP_ENCODE(domain, mycpu->gd_cpuid) | PWAKEUP_ONE);
}

/*
 * setrunnable()
 *
 * Make a process runnable.  The MP lock must be held on call.  This only
 * has an effect if we are in SSLEEP.  We only break out of the
 * tsleep if P_BREAKTSLEEP is set, otherwise we just fix-up the state.
 *
 * NOTE: With the MP lock held we can only safely manipulate the process
 * structure.  We cannot safely manipulate the thread structure.
 */
void
setrunnable(struct proc *p)
{
	crit_enter();
	ASSERT_MP_LOCK_HELD(curthread);
	p->p_flag &= ~P_STOPPED;
	if (p->p_stat == SSLEEP && (p->p_flag & P_BREAKTSLEEP)) {
		unsleep_and_wakeup_thread(p->p_thread);
	}
	crit_exit();
}

/*
 * The process is stopped due to some condition, usually because P_STOPPED
 * is set but also possibly due to being traced.  
 *
 * NOTE!  If the caller sets P_STOPPED, the caller must also clear P_WAITED
 * because the parent may check the child's status before the child actually
 * gets to this routine.
 *
 * This routine is called with the current process only, typically just
 * before returning to userland.
 *
 * Setting P_BREAKTSLEEP before entering the tsleep will cause a passive
 * SIGCONT to break out of the tsleep.
 */
void
tstop(struct proc *p)
{
	wakeup((caddr_t)p->p_pptr);
	p->p_flag |= P_BREAKTSLEEP;
	tsleep(p, 0, "stop", 0);
}

/*
 * Yield / synchronous reschedule.  This is a bit tricky because the trap
 * code might have set a lazy release on the switch function.   Setting
 * P_PASSIVE_ACQ will ensure that the lazy release executes when we call
 * switch, and that we are given a greater chance of affinity with our
 * current cpu.
 *
 * We call lwkt_setpri_self() to rotate our thread to the end of the lwkt
 * run queue.  lwkt_switch() will also execute any assigned passive release
 * (which usually calls release_curproc()), allowing a same/higher priority
 * process to be designated as the current process.  
 *
 * While it is possible for a lower priority process to be designated,
 * it's call to lwkt_maybe_switch() in acquire_curproc() will likely
 * round-robin back to us and we will be able to re-acquire the current
 * process designation.
 */
void
uio_yield(void)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;

	lwkt_setpri_self(td->td_pri & TDPRI_MASK);
	if (p) {
		p->p_flag |= P_PASSIVE_ACQ;
		lwkt_switch();
		p->p_flag &= ~P_PASSIVE_ACQ;
	} else {
		lwkt_switch();
	}
}

/*
 * Compute a tenex style load average of a quantity on
 * 1, 5 and 15 minute intervals.
 */
static int loadav_count_runnable(struct proc *p, void *data);

static void
loadav(void *arg)
{
	struct loadavg *avg;
	int i, nrun;

	nrun = 0;
	allproc_scan(loadav_count_runnable, &nrun);
	avg = &averunnable;
	for (i = 0; i < 3; i++) {
		avg->ldavg[i] = (cexp[i] * avg->ldavg[i] +
		    nrun * FSCALE * (FSCALE - cexp[i])) >> FSHIFT;
	}

	/*
	 * Schedule the next update to occur after 5 seconds, but add a
	 * random variation to avoid synchronisation with processes that
	 * run at regular intervals.
	 */
	callout_reset(&loadav_callout, hz * 4 + (int)(random() % (hz * 2 + 1)),
		      loadav, NULL);
}

static int
loadav_count_runnable(struct proc *p, void *data)
{
	int *nrunp = data;
	thread_t td;

	switch (p->p_stat) {
	case SRUN:
		if ((td = p->p_thread) == NULL)
			break;
		if (td->td_flags & TDF_BLOCKED)
			break;
		/* fall through */
	case SIDL:
		++*nrunp;
		break;
	default:
		break;
	}
	return(0);
}

/* ARGSUSED */
static void
sched_setup(void *dummy)
{
	callout_init(&loadav_callout);
	callout_init(&schedcpu_callout);

	/* Kick off timeout driven events by calling first time. */
	schedcpu(NULL);
	loadav(NULL);
}

