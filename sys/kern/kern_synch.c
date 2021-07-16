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
 *	@(#)kern_synch.c	8.9 (Berkeley) 5/19/95
 * $FreeBSD: src/sys/kern/kern_synch.c,v 1.87.2.6 2002/10/13 07:29:53 kbyanc Exp $
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
#include <sys/priv.h>
#include <sys/kcollect.h>
#include <sys/malloc.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <sys/ktr.h>
#include <sys/serialize.h>

#include <sys/signal2.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mutex2.h>

#include <machine/cpu.h>
#include <machine/smp.h>

#include <vm/vm_extern.h>

struct tslpque {
	TAILQ_HEAD(, thread)	queue;
	const volatile void	*ident0;
	const volatile void	*ident1;
	const volatile void	*ident2;
	const volatile void	*ident3;
};

static void sched_setup (void *dummy);
SYSINIT(sched_setup, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, sched_setup, NULL);
static void sched_dyninit (void *dummy);
SYSINIT(sched_dyninit, SI_BOOT1_DYNALLOC, SI_ORDER_FIRST, sched_dyninit, NULL);

int	lbolt;
void	*lbolt_syncer;
__read_mostly int tsleep_crypto_dump = 0;
__read_mostly int ncpus;
__read_mostly int ncpus_fit, ncpus_fit_mask;	/* note: mask not cpumask_t */
__read_mostly int safepri;
__read_mostly int tsleep_now_works;

MALLOC_DEFINE(M_TSLEEP, "tslpque", "tsleep queues");

#define __DEALL(ident)	__DEQUALIFY(void *, ident)

#if !defined(KTR_TSLEEP)
#define KTR_TSLEEP	KTR_ALL
#endif
KTR_INFO_MASTER(tsleep);
KTR_INFO(KTR_TSLEEP, tsleep, tsleep_beg, 0, "tsleep enter %p", const volatile void *ident);
KTR_INFO(KTR_TSLEEP, tsleep, tsleep_end, 1, "tsleep exit");
KTR_INFO(KTR_TSLEEP, tsleep, wakeup_beg, 2, "wakeup enter %p", const volatile void *ident);
KTR_INFO(KTR_TSLEEP, tsleep, wakeup_end, 3, "wakeup exit");
KTR_INFO(KTR_TSLEEP, tsleep, ilockfail,  4, "interlock failed %p", const volatile void *ident);

#define logtsleep1(name)	KTR_LOG(tsleep_ ## name)
#define logtsleep2(name, val)	KTR_LOG(tsleep_ ## name, val)

__exclusive_cache_line
struct loadavg averunnable =
	{ {0, 0, 0}, FSCALE };	/* load average, of runnable procs */
/*
 * Constants for averages over 1, 5, and 15 minutes
 * when sampling at 5 second intervals.
 */
__read_mostly
static fixpt_t cexp[3] = {
	0.9200444146293232 * FSCALE,	/* exp(-1/12) */
	0.9834714538216174 * FSCALE,	/* exp(-1/60) */
	0.9944598480048967 * FSCALE,	/* exp(-1/180) */
};

static void	endtsleep (void *);
static void	loadav (void *arg);
static void	schedcpu (void *arg);

__read_mostly static int pctcpu_decay = 10;
SYSCTL_INT(_kern, OID_AUTO, pctcpu_decay, CTLFLAG_RW,
	   &pctcpu_decay, 0, "");

/*
 * kernel uses `FSCALE', userland (SHOULD) use kern.fscale
 */
__read_mostly int fscale __unused = FSCALE;	/* exported to systat */
SYSCTL_INT(_kern, OID_AUTO, fscale, CTLFLAG_RD, 0, FSCALE, "");

/*
 * Issue a wakeup() from userland (debugging)
 */
static int
sysctl_wakeup(SYSCTL_HANDLER_ARGS)
{
	uint64_t ident = 1;
	int error = 0;

	if (req->newptr != NULL) {
		if (priv_check(curthread, PRIV_ROOT))
			return (EPERM);
		error = SYSCTL_IN(req, &ident, sizeof(ident));
		if (error)
			return error;
		kprintf("issue wakeup %016jx\n", ident);
		wakeup((void *)(intptr_t)ident);
	}
	if (req->oldptr != NULL) {
		error = SYSCTL_OUT(req, &ident, sizeof(ident));
	}
	return error;
}

static int
sysctl_wakeup_umtx(SYSCTL_HANDLER_ARGS)
{
	uint64_t ident = 1;
	int error = 0;

	if (req->newptr != NULL) {
		if (priv_check(curthread, PRIV_ROOT))
			return (EPERM);
		error = SYSCTL_IN(req, &ident, sizeof(ident));
		if (error)
			return error;
		kprintf("issue wakeup %016jx, PDOMAIN_UMTX\n", ident);
		wakeup_domain((void *)(intptr_t)ident, PDOMAIN_UMTX);
	}
	if (req->oldptr != NULL) {
		error = SYSCTL_OUT(req, &ident, sizeof(ident));
	}
	return error;
}

SYSCTL_PROC(_debug, OID_AUTO, wakeup, CTLTYPE_UQUAD|CTLFLAG_RW, 0, 0,
	    sysctl_wakeup, "Q", "issue wakeup(addr)");
SYSCTL_PROC(_debug, OID_AUTO, wakeup_umtx, CTLTYPE_UQUAD|CTLFLAG_RW, 0, 0,
	    sysctl_wakeup_umtx, "Q", "issue wakeup(addr, PDOMAIN_UMTX)");

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
	allproc_scan(schedcpu_stats, NULL, 1);
	allproc_scan(schedcpu_resource, NULL, 1);
	if (mycpu->gd_cpuid == 0) {
		wakeup((caddr_t)&lbolt);
		wakeup(lbolt_syncer);
	}
	callout_reset(&mycpu->gd_schedcpu_callout, hz, schedcpu, NULL);
}

/*
 * General process statistics once a second
 */
static int
schedcpu_stats(struct proc *p, void *data __unused)
{
	struct lwp *lp;

	/*
	 * Threads may not be completely set up if process in SIDL state.
	 */
	if (p->p_stat == SIDL)
		return(0);

	PHOLD(p);
	if (lwkt_trytoken(&p->p_token) == FALSE) {
		PRELE(p);
		return(0);
	}

	p->p_swtime++;
	FOREACH_LWP_IN_PROC(lp, p) {
		if (lp->lwp_stat == LSSLEEP) {
			++lp->lwp_slptime;
			if (lp->lwp_slptime == 1)
				p->p_usched->uload_update(lp);
		}

		/*
		 * Only recalculate processes that are active or have slept
		 * less then 2 seconds.  The schedulers understand this.
		 * Otherwise decay by 50% per second.
		 *
		 * NOTE: uload_update is called separately from kern_synch.c
		 *	 when slptime == 1, removing the thread's
		 *	 uload/ucount.
		 */
		if (lp->lwp_slptime <= 1) {
			p->p_usched->recalculate(lp);
		} else {
			int decay;

			decay = pctcpu_decay;
			cpu_ccfence();
			if (decay <= 1)
				decay = 1;
			if (decay > 100)
				decay = 100;
			lp->lwp_pctcpu = (lp->lwp_pctcpu * (decay - 1)) / decay;
		}
	}
	lwkt_reltoken(&p->p_token);
	lwkt_yield();
	PRELE(p);
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
	struct lwp *lp;

	if (p->p_stat == SIDL)
		return(0);

	PHOLD(p);
	if (lwkt_trytoken(&p->p_token) == FALSE) {
		PRELE(p);
		return(0);
	}

	if (p->p_stat == SZOMB || p->p_limit == NULL) {
		lwkt_reltoken(&p->p_token);
		PRELE(p);
		return(0);
	}

	ttime = 0;
	FOREACH_LWP_IN_PROC(lp, p) {
		/*
		 * We may have caught an lp in the middle of being
		 * created, lwp_thread can be NULL.
		 */
		if (lp->lwp_thread) {
			ttime += lp->lwp_thread->td_sticks;
			ttime += lp->lwp_thread->td_uticks;
		}
	}

	switch(plimit_testcpulimit(p, ttime)) {
	case PLIMIT_TESTCPU_KILL:
		killproc(p, "exceeded maximum CPU limit");
		break;
	case PLIMIT_TESTCPU_XCPU:
		if ((p->p_flags & P_XCPU) == 0) {
			p->p_flags |= P_XCPU;
			ksignal(p, SIGXCPU);
		}
		break;
	default:
		break;
	}
	lwkt_reltoken(&p->p_token);
	lwkt_yield();
	PRELE(p);
	return(0);
}

/*
 * This is only used by ps.  Generate a cpu percentage use over
 * a period of one second.
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
 * Handy macros to calculate hash indices.  LOOKUP() calculates the
 * global cpumask hash index, TCHASHSHIFT() converts that into the
 * pcpu hash index.
 *
 * By making the pcpu hash arrays smaller we save a significant amount
 * of memory at very low cost.  The real cost is in IPIs, which are handled
 * by the much larger global cpumask hash table.
 */
#define LOOKUP_PRIME	66555444443333333ULL
#define LOOKUP(x)	((((uintptr_t)(x) + ((uintptr_t)(x) >> 18)) ^	\
			  LOOKUP_PRIME) % slpque_tablesize)
#define TCHASHSHIFT(x)	((x) >> 4)

__read_mostly static uint32_t	slpque_tablesize;
__read_mostly static cpumask_t *slpque_cpumasks;

SYSCTL_UINT(_kern, OID_AUTO, slpque_tablesize, CTLFLAG_RD, &slpque_tablesize,
    0, "");

/*
 * This is a dandy function that allows us to interlock tsleep/wakeup
 * operations with unspecified upper level locks, such as lockmgr locks,
 * simply by holding a critical section.  The sequence is:
 *
 *	(acquire upper level lock)
 *	tsleep_interlock(blah)
 *	(release upper level lock)
 *	tsleep(blah, ...)
 *
 * Basically this functions queues us on the tsleep queue without actually
 * descheduling us.  When tsleep() is later called with PINTERLOCK it
 * assumes the thread was already queued, otherwise it queues it there.
 *
 * Thus it is possible to receive the wakeup prior to going to sleep and
 * the race conditions are covered.
 */
static __inline void
_tsleep_interlock(globaldata_t gd, const volatile void *ident, int flags)
{
	thread_t td = gd->gd_curthread;
	struct tslpque *qp;
	uint32_t cid;
	uint32_t gid;

	if (ident == NULL) {
		kprintf("tsleep_interlock: NULL ident %s\n", td->td_comm);
		print_backtrace(5);
	}

	crit_enter_quick(td);
	if (td->td_flags & TDF_TSLEEPQ) {
		/*
		 * Shortcut if unchanged
		 */
		if (td->td_wchan == ident &&
		    td->td_wdomain == (flags & PDOMAIN_MASK)) {
			crit_exit_quick(td);
			return;
		}

		/*
		 * Remove current sleepq
		 */
		cid = LOOKUP(td->td_wchan);
		gid = TCHASHSHIFT(cid);
		qp = &gd->gd_tsleep_hash[gid];
		TAILQ_REMOVE(&qp->queue, td, td_sleepq);
		if (TAILQ_FIRST(&qp->queue) == NULL) {
			qp->ident0 = NULL;
			qp->ident1 = NULL;
			qp->ident2 = NULL;
			qp->ident3 = NULL;
			ATOMIC_CPUMASK_NANDBIT(slpque_cpumasks[cid],
					       gd->gd_cpuid);
		}
	} else {
		td->td_flags |= TDF_TSLEEPQ;
	}
	cid = LOOKUP(ident);
	gid = TCHASHSHIFT(cid);
	qp = &gd->gd_tsleep_hash[gid];
	TAILQ_INSERT_TAIL(&qp->queue, td, td_sleepq);
	if (qp->ident0 != ident && qp->ident1 != ident &&
	    qp->ident2 != ident && qp->ident3 != ident) {
		if (qp->ident0 == NULL)
			qp->ident0 = ident;
		else if (qp->ident1 == NULL)
			qp->ident1 = ident;
		else if (qp->ident2 == NULL)
			qp->ident2 = ident;
		else if (qp->ident3 == NULL)
			qp->ident3 = ident;
		else
			qp->ident0 = (void *)(intptr_t)-1;
	}
	ATOMIC_CPUMASK_ORBIT(slpque_cpumasks[cid], gd->gd_cpuid);
	td->td_wchan = ident;
	td->td_wdomain = flags & PDOMAIN_MASK;
	crit_exit_quick(td);
}

void
tsleep_interlock(const volatile void *ident, int flags)
{
	_tsleep_interlock(mycpu, ident, flags);
}

/*
 * Remove thread from sleepq.  Must be called with a critical section held.
 * The thread must not be migrating.
 */
static __inline void
_tsleep_remove(thread_t td)
{
	globaldata_t gd = mycpu;
	struct tslpque *qp;
	uint32_t cid;
	uint32_t gid;

	KKASSERT(td->td_gd == gd && IN_CRITICAL_SECT(td));
	KKASSERT((td->td_flags & TDF_MIGRATING) == 0);
	if (td->td_flags & TDF_TSLEEPQ) {
		td->td_flags &= ~TDF_TSLEEPQ;
		cid = LOOKUP(td->td_wchan);
		gid = TCHASHSHIFT(cid);
		qp = &gd->gd_tsleep_hash[gid];
		TAILQ_REMOVE(&qp->queue, td, td_sleepq);
		if (TAILQ_FIRST(&qp->queue) == NULL) {
			ATOMIC_CPUMASK_NANDBIT(slpque_cpumasks[cid],
					       gd->gd_cpuid);
		}
		td->td_wchan = NULL;
		td->td_wdomain = 0;
	}
}

void
tsleep_remove(thread_t td)
{
	_tsleep_remove(td);
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
 *
 * WARNING!  This code can't block (short of switching away), or bad things
 *           will happen.  No getting tokens, no blocking locks, etc.
 */
int
tsleep(const volatile void *ident, int flags, const char *wmesg, int timo)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = td->td_proc;		/* may be NULL */
	globaldata_t gd;
	int sig;
	int catch;
	int error;
	int oldpri;
	struct callout thandle1;
	struct _callout thandle2;

	/*
	 * Currently a severe hack.  Make sure any delayed wakeups
	 * are flushed before we sleep or we might deadlock on whatever
	 * event we are sleeping on.
	 */
	if (td->td_flags & TDF_DELAYED_WAKEUP)
		wakeup_end_delayed();

	/*
	 * NOTE: removed KTRPOINT, it could cause races due to blocking
	 * even in stable.  Just scrap it for now.
	 */
	if (!tsleep_crypto_dump && (tsleep_now_works == 0 || panicstr)) {
		/*
		 * After a panic, or before we actually have an operational
		 * softclock, just give interrupts a chance, then just return;
		 *
		 * don't run any other procs or panic below,
		 * in case this is the idle process and already asleep.
		 */
		splz();
		oldpri = td->td_pri;
		lwkt_setpri_self(safepri);
		lwkt_switch();
		lwkt_setpri_self(oldpri);
		return (0);
	}
	logtsleep2(tsleep_beg, ident);
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
	catch = flags & PCATCH;
	error = 0;
	sig = 0;

	crit_enter_quick(td);

	KASSERT(ident != NULL, ("tsleep: no ident"));
	KASSERT(lp == NULL ||
		lp->lwp_stat == LSRUN ||	/* Obvious */
		lp->lwp_stat == LSSTOP,		/* Set in tstop */
		("tsleep %p %s %d",
			ident, wmesg, lp->lwp_stat));

	/*
	 * We interlock the sleep queue if the caller has not already done
	 * it for us.  This must be done before we potentially acquire any
	 * tokens or we can loose the wakeup.
	 */
	if ((flags & PINTERLOCKED) == 0) {
		_tsleep_interlock(gd, ident, flags);
	}

	/*
	 * Setup for the current process (if this is a process).  We must
	 * interlock with lwp_token to avoid remote wakeup races via
	 * setrunnable()
	 */
	if (lp) {
		lwkt_gettoken(&lp->lwp_token);

		/*
		 * If the umbrella process is in the SCORE state then
		 * make sure that the thread is flagged going into a
		 * normal sleep to allow the core dump to proceed, otherwise
		 * the coredump can end up waiting forever.  If the normal
		 * sleep is woken up, the thread will enter a stopped state
		 * upon return to userland.
		 *
		 * We do not want to interrupt or cause a thread exist at
		 * this juncture because that will mess-up the state the
		 * coredump is trying to save.
		 */
		if (p->p_stat == SCORE) {
			lwkt_gettoken(&p->p_token);
			if ((lp->lwp_mpflags & LWP_MP_WSTOP) == 0) {
				atomic_set_int(&lp->lwp_mpflags, LWP_MP_WSTOP);
				++p->p_nstopped;
			}
			lwkt_reltoken(&p->p_token);
		}

		/*
		 * PCATCH requested.
		 */
		if (catch) {
			/*
			 * Early termination if PCATCH was set and a
			 * signal is pending, interlocked with the
			 * critical section.
			 *
			 * Early termination only occurs when tsleep() is
			 * entered while in a normal LSRUN state.
			 */
			if ((sig = CURSIG(lp)) != 0)
				goto resume;

			/*
			 * Causes ksignal to wake us up if a signal is
			 * received (interlocked with lp->lwp_token).
			 */
			lp->lwp_flags |= LWP_SINTR;
		}
	} else {
		KKASSERT(p == NULL);
	}

	/*
	 * Make sure the current process has been untangled from
	 * the userland scheduler and initialize slptime to start
	 * counting.
	 *
	 * NOTE: td->td_wakefromcpu is pre-set by the release function
	 *	 for the dfly scheduler, and then adjusted by _wakeup()
	 */
	if (lp) {
		p->p_usched->release_curproc(lp);
		lp->lwp_slptime = 0;
	}

	/*
	 * For PINTERLOCKED operation, TDF_TSLEEPQ might not be set if
	 * a wakeup() was processed before the thread could go to sleep.
	 *
	 * If TDF_TSLEEPQ is set, make sure the ident matches the recorded
	 * ident.  If it does not then the thread slept inbetween the
	 * caller's initial tsleep_interlock() call and the caller's tsleep()
	 * call.
	 *
	 * Extreme loads can cause the sending of an IPI (e.g. wakeup()'s)
	 * to process incoming IPIs, thus draining incoming wakeups.
	 */
	if ((td->td_flags & TDF_TSLEEPQ) == 0) {
		logtsleep2(ilockfail, ident);
		goto resume;
	} else if (td->td_wchan != ident ||
		   td->td_wdomain != (flags & PDOMAIN_MASK)) {
		logtsleep2(ilockfail, ident);
		goto resume;
	}

	/*
	 * scheduling is blocked while in a critical section.  Coincide
	 * the descheduled-by-tsleep flag with the descheduling of the
	 * lwkt.
	 *
	 * The timer callout is localized on our cpu and interlocked by
	 * our critical section.
	 */
	lwkt_deschedule_self(td);
	td->td_flags |= TDF_TSLEEP_DESCHEDULED;
	td->td_wmesg = wmesg;

	/*
	 * Setup the timeout, if any.  The timeout is only operable while
	 * the thread is flagged descheduled.
	 */
	KKASSERT((td->td_flags & TDF_TIMEOUT) == 0);
	if (timo) {
		_callout_setup_quick(&thandle1, &thandle2, timo, endtsleep, td);
	}

	/*
	 * Beddy bye bye.
	 */
	if (lp) {
		/*
		 * Ok, we are sleeping.  Place us in the SSLEEP state.
		 */
		KKASSERT((lp->lwp_mpflags & LWP_MP_ONRUNQ) == 0);

		/*
		 * tstop() sets LSSTOP, so don't fiddle with that.
		 */
		if (lp->lwp_stat != LSSTOP)
			lp->lwp_stat = LSSLEEP;
		lp->lwp_ru.ru_nvcsw++;
		p->p_usched->uload_update(lp);
		lwkt_switch();

		/*
		 * And when we are woken up, put us back in LSRUN.  If we
		 * slept for over a second, recalculate our estcpu.
		 */
		lp->lwp_stat = LSRUN;
		if (lp->lwp_slptime) {
			p->p_usched->uload_update(lp);
			p->p_usched->recalculate(lp);
		}
		lp->lwp_slptime = 0;
	} else {
		lwkt_switch();
	}

	/*
	 * Make sure we haven't switched cpus while we were asleep.  It's
	 * not supposed to happen.  Cleanup our temporary flags.
	 */
	KKASSERT(gd == td->td_gd);

	/*
	 * Cleanup the timeout.  If the timeout has already occured thandle
	 * has already been stopped, otherwise stop thandle.
	 *
	 * If the timeout is still running the callout thread must be blocked
	 * trying to get lwp_token, or this is a VM where cpu-cpu races are
	 * common, then wait for us to get scheduled.
	 */
	if (timo) {
		while (td->td_flags & TDF_TIMEOUT_RUNNING) {
			/* else we won't get rescheduled! */
			if (lp->lwp_stat != LSSTOP)
				lp->lwp_stat = LSSLEEP;
			lwkt_deschedule_self(td);
			td->td_wmesg = "tsrace";
			lwkt_switch();
		}
		if (td->td_flags & TDF_TIMEOUT) {
			td->td_flags &= ~TDF_TIMEOUT;
			error = EWOULDBLOCK;
		} else {
			/*
			 * We are on the same cpu so use the quick version
			 * which is guaranteed not to block or race.
			 */
			_callout_cancel_quick(&thandle2);
		}
	}
	td->td_flags &= ~TDF_TSLEEP_DESCHEDULED;

	/*
	 * Make sure we have been removed from the sleepq.  In most
	 * cases this will have been done for us already but it is
	 * possible for a scheduling IPI to be in-flight from a
	 * previous tsleep/tsleep_interlock() or due to a straight-out
	 * call to lwkt_schedule() (in the case of an interrupt thread),
	 * causing a spurious wakeup.
	 */
	_tsleep_remove(td);
	td->td_wmesg = NULL;

	/*
	 * Figure out the correct error return.  If interrupted by a
	 * signal we want to return EINTR or ERESTART.
	 */
resume:
	if (lp) {
		if (catch && error == 0) {
			if (sig != 0 || (sig = CURSIG(lp))) {
				if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
					error = EINTR;
				else
					error = ERESTART;
			}
		}

		lp->lwp_flags &= ~LWP_SINTR;

		/*
		 * Unconditionally set us to LSRUN on resume.  lwp_stat could
		 * be in a weird state due to the goto resume, particularly
		 * when tsleep() is called from tstop().
		 */
		lp->lwp_stat = LSRUN;
		lwkt_reltoken(&lp->lwp_token);
	}
	logtsleep1(tsleep_end);
	crit_exit_quick(td);

	return (error);
}

/*
 * Interlocked spinlock sleep.  An exclusively held spinlock must
 * be passed to ssleep().  The function will atomically release the
 * spinlock and tsleep on the ident, then reacquire the spinlock and
 * return.
 *
 * This routine is fairly important along the critical path, so optimize it
 * heavily.
 */
int
ssleep(const volatile void *ident, struct spinlock *spin, int flags,
       const char *wmesg, int timo)
{
	globaldata_t gd = mycpu;
	int error;

	_tsleep_interlock(gd, ident, flags);
	spin_unlock_quick(gd, spin);
	error = tsleep(ident, flags | PINTERLOCKED, wmesg, timo);
	KKASSERT(gd == mycpu);
	_spin_lock_quick(gd, spin, wmesg);

	return (error);
}

int
lksleep(const volatile void *ident, struct lock *lock, int flags,
	const char *wmesg, int timo)
{
	globaldata_t gd = mycpu;
	int error;

	_tsleep_interlock(gd, ident, flags);
	lockmgr(lock, LK_RELEASE);
	error = tsleep(ident, flags | PINTERLOCKED, wmesg, timo);
	lockmgr(lock, LK_EXCLUSIVE);

	return (error);
}

/*
 * Interlocked mutex sleep.  An exclusively held mutex must be passed
 * to mtxsleep().  The function will atomically release the mutex
 * and tsleep on the ident, then reacquire the mutex and return.
 */
int
mtxsleep(const volatile void *ident, struct mtx *mtx, int flags,
	 const char *wmesg, int timo)
{
	globaldata_t gd = mycpu;
	int error;

	_tsleep_interlock(gd, ident, flags);
	mtx_unlock(mtx);
	error = tsleep(ident, flags | PINTERLOCKED, wmesg, timo);
	mtx_lock_ex_quick(mtx);

	return (error);
}

/*
 * Interlocked serializer sleep.  An exclusively held serializer must
 * be passed to zsleep().  The function will atomically release
 * the serializer and tsleep on the ident, then reacquire the serializer
 * and return.
 */
int
zsleep(const volatile void *ident, struct lwkt_serialize *slz, int flags,
       const char *wmesg, int timo)
{
	globaldata_t gd = mycpu;
	int ret;

	ASSERT_SERIALIZED(slz);

	_tsleep_interlock(gd, ident, flags);
	lwkt_serialize_exit(slz);
	ret = tsleep(ident, flags | PINTERLOCKED, wmesg, timo);
	lwkt_serialize_enter(slz);

	return ret;
}

/*
 * Directly block on the LWKT thread by descheduling it.  This
 * is much faster then tsleep(), but the only legal way to wake
 * us up is to directly schedule the thread.
 *
 * Setting TDF_SINTR will cause new signals to directly schedule us.
 *
 * This routine must be called while in a critical section.
 */
int
lwkt_sleep(const char *wmesg, int flags)
{
	thread_t td = curthread;
	int sig;

	if ((flags & PCATCH) == 0 || td->td_lwp == NULL) {
		td->td_flags |= TDF_BLOCKED;
		td->td_wmesg = wmesg;
		lwkt_deschedule_self(td);
		lwkt_switch();
		td->td_wmesg = NULL;
		td->td_flags &= ~TDF_BLOCKED;
		return(0);
	}
	if ((sig = CURSIG(td->td_lwp)) != 0) {
		if (SIGISMEMBER(td->td_proc->p_sigacts->ps_sigintr, sig))
			return(EINTR);
		else
			return(ERESTART);

	}
	td->td_flags |= TDF_BLOCKED | TDF_SINTR;
	td->td_wmesg = wmesg;
	lwkt_deschedule_self(td);
	lwkt_switch();
	td->td_flags &= ~(TDF_BLOCKED | TDF_SINTR);
	td->td_wmesg = NULL;
	return(0);
}

/*
 * Implement the timeout for tsleep.
 *
 * This type of callout timeout is scheduled on the same cpu the process
 * is sleeping on.  Also, at the moment, the MP lock is held.
 */
static void
endtsleep(void *arg)
{
	thread_t td = arg;
	struct lwp *lp;

	/*
	 * We are going to have to get the lwp_token, which means we might
	 * block.  This can race a tsleep getting woken up by other means
	 * so set TDF_TIMEOUT_RUNNING to force the tsleep to wait for our
	 * processing to complete (sorry tsleep!).
	 *
	 * We can safely set td_flags because td MUST be on the same cpu
	 * as we are.
	 */
	KKASSERT(td->td_gd == mycpu);
	crit_enter();
	td->td_flags |= TDF_TIMEOUT_RUNNING | TDF_TIMEOUT;

	/*
	 * This can block but TDF_TIMEOUT_RUNNING will prevent the thread
	 * from exiting the tsleep on us.  The flag is interlocked by virtue
	 * of lp being on the same cpu as we are.
	 */
	if ((lp = td->td_lwp) != NULL)
		lwkt_gettoken(&lp->lwp_token);

	KKASSERT(td->td_flags & TDF_TSLEEP_DESCHEDULED);

	if (lp) {
		/*
		 * callout timer should normally never be set in tstop()
		 * because it passes a timeout of 0.  However, there is a
		 * case during thread exit (which SSTOP's all the threads)
		 * for which tstop() must break out and can (properly) leave
		 * the thread in LSSTOP.
		 */
		KKASSERT(lp->lwp_stat != LSSTOP ||
			 (lp->lwp_mpflags & LWP_MP_WEXIT));
		setrunnable(lp);
		lwkt_reltoken(&lp->lwp_token);
	} else {
		_tsleep_remove(td);
		lwkt_schedule(td);
	}
	KKASSERT(td->td_gd == mycpu);
	td->td_flags &= ~TDF_TIMEOUT_RUNNING;
	crit_exit();
}

/*
 * Make all processes sleeping on the specified identifier runnable.
 * count may be zero or one only.
 *
 * The domain encodes the sleep/wakeup domain, flags, plus the originating
 * cpu.
 *
 * This call may run without the MP lock held.  We can only manipulate thread
 * state on the cpu owning the thread.  We CANNOT manipulate process state
 * at all.
 *
 * _wakeup() can be passed to an IPI so we can't use (const volatile
 * void *ident).
 */
static void
_wakeup(void *ident, int domain)
{
	struct tslpque *qp;
	struct thread *td;
	struct thread *ntd;
	globaldata_t gd;
	cpumask_t mask;
	uint32_t cid;
	uint32_t gid;
	int wids = 0;

	crit_enter();
	logtsleep2(wakeup_beg, ident);
	gd = mycpu;
	cid = LOOKUP(ident);
	gid = TCHASHSHIFT(cid);
	qp = &gd->gd_tsleep_hash[gid];
restart:
	for (td = TAILQ_FIRST(&qp->queue); td != NULL; td = ntd) {
		ntd = TAILQ_NEXT(td, td_sleepq);
		if (td->td_wchan == ident &&
		    td->td_wdomain == (domain & PDOMAIN_MASK)
		) {
			KKASSERT(td->td_gd == gd);
			_tsleep_remove(td);
			td->td_wakefromcpu = PWAKEUP_DECODE(domain);
			if (td->td_flags & TDF_TSLEEP_DESCHEDULED) {
				lwkt_schedule(td);
				if (domain & PWAKEUP_ONE)
					goto done;
			}
			goto restart;
		}
		if (td->td_wchan == qp->ident0)
			wids |= 1;
		else if (td->td_wchan == qp->ident1)
			wids |= 2;
		else if (td->td_wchan == qp->ident2)
			wids |= 4;
		else if (td->td_wchan == qp->ident3)
			wids |= 8;
		else
			wids |= 16;	/* force ident0 to be retained (-1) */
	}

	/*
	 * Because a bunch of cpumask array entries cover the same queue, it
	 * is possible for our bit to remain set in some of them and cause
	 * spurious wakeup IPIs later on.  Make sure that the bit is cleared
	 * when a spurious IPI occurs to prevent further spurious IPIs.
	 */
	if (TAILQ_FIRST(&qp->queue) == NULL) {
		ATOMIC_CPUMASK_NANDBIT(slpque_cpumasks[cid], gd->gd_cpuid);
		qp->ident0 = NULL;
		qp->ident1 = NULL;
		qp->ident2 = NULL;
		qp->ident3 = NULL;
	} else {
		if ((wids & 1) == 0) {
			if ((wids & 16) == 0) {
				qp->ident0 = NULL;
			} else {
				KKASSERT(qp->ident0 == (void *)(intptr_t)-1);
			}
		}
		if ((wids & 2) == 0)
			qp->ident1 = NULL;
		if ((wids & 4) == 0)
			qp->ident2 = NULL;
		if ((wids & 8) == 0)
			qp->ident3 = NULL;
	}

	/*
	 * We finished checking the current cpu but there still may be
	 * more work to do.  Either wakeup_one was requested and no matching
	 * thread was found, or a normal wakeup was requested and we have
	 * to continue checking cpus.
	 *
	 * It should be noted that this scheme is actually less expensive then
	 * the old scheme when waking up multiple threads, since we send
	 * only one IPI message per target candidate which may then schedule
	 * multiple threads.  Before we could have wound up sending an IPI
	 * message for each thread on the target cpu (!= current cpu) that
	 * needed to be woken up.
	 *
	 * NOTE: Wakeups occuring on remote cpus are asynchronous.  This
	 *	 should be ok since we are passing idents in the IPI rather
	 *	 then thread pointers.
	 *
	 * NOTE: We MUST mfence (or use an atomic op) prior to reading
	 *	 the cpumask, as another cpu may have written to it in
	 *	 a fashion interlocked with whatever the caller did before
	 *	 calling wakeup().  Otherwise we might miss the interaction
	 *	 (kern_mutex.c can cause this problem).
	 *
	 *	 lfence is insufficient as it may allow a written state to
	 *	 reorder around the cpumask load.
	 */
	if ((domain & PWAKEUP_MYCPU) == 0) {
		globaldata_t tgd;
		const volatile void *id0;
		int n;

		cpu_mfence();
		/* cpu_lfence(); */
		mask = slpque_cpumasks[cid];
		CPUMASK_ANDMASK(mask, gd->gd_other_cpus);
		while (CPUMASK_TESTNZERO(mask)) {
			n = BSRCPUMASK(mask);
			CPUMASK_NANDBIT(mask, n);
			tgd = globaldata_find(n);

			/*
			 * Both ident0 compares must from a single load
			 * to avoid ident0 update races crossing the two
			 * compares.
			 */
			qp = &tgd->gd_tsleep_hash[gid];
			id0 = qp->ident0;
			cpu_ccfence();
			if (id0 == (void *)(intptr_t)-1) {
				lwkt_send_ipiq2(tgd, _wakeup, ident,
						domain | PWAKEUP_MYCPU);
				++tgd->gd_cnt.v_wakeup_colls;
			} else if (id0 == ident ||
				   qp->ident1 == ident ||
				   qp->ident2 == ident ||
				   qp->ident3 == ident) {
				lwkt_send_ipiq2(tgd, _wakeup, ident,
						domain | PWAKEUP_MYCPU);
			}
		}
#if 0
		if (CPUMASK_TESTNZERO(mask)) {
			lwkt_send_ipiq2_mask(mask, _wakeup, ident,
					     domain | PWAKEUP_MYCPU);
		}
#endif
	}
done:
	logtsleep1(wakeup_end);
	crit_exit();
}

/*
 * Wakeup all threads tsleep()ing on the specified ident, on all cpus
 */
void
wakeup(const volatile void *ident)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;

    if (td && (td->td_flags & TDF_DELAYED_WAKEUP)) {
	/*
	 * If we are in a delayed wakeup section, record up to two wakeups in
	 * a per-CPU queue and issue them when we block or exit the delayed
	 * wakeup section.
	 */
	if (atomic_cmpset_ptr(&gd->gd_delayed_wakeup[0], NULL, ident))
		return;
	if (atomic_cmpset_ptr(&gd->gd_delayed_wakeup[1], NULL, ident))
		return;

	ident = atomic_swap_ptr(__DEQUALIFY(volatile void **, &gd->gd_delayed_wakeup[1]),
				__DEALL(ident));
	ident = atomic_swap_ptr(__DEQUALIFY(volatile void **, &gd->gd_delayed_wakeup[0]),
				__DEALL(ident));
    }

    _wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, gd->gd_cpuid));
}

/*
 * Wakeup one thread tsleep()ing on the specified ident, on any cpu.
 */
void
wakeup_one(const volatile void *ident)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, mycpu->gd_cpuid) |
			    PWAKEUP_ONE);
}

/*
 * Wakeup threads tsleep()ing on the specified ident on the current cpu
 * only.
 */
void
wakeup_mycpu(const volatile void *ident)
{
    _wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, mycpu->gd_cpuid) |
			    PWAKEUP_MYCPU);
}

/*
 * Wakeup one thread tsleep()ing on the specified ident on the current cpu
 * only.
 */
void
wakeup_mycpu_one(const volatile void *ident)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, mycpu->gd_cpuid) |
			    PWAKEUP_MYCPU | PWAKEUP_ONE);
}

/*
 * Wakeup all thread tsleep()ing on the specified ident on the specified cpu
 * only.
 */
void
wakeup_oncpu(globaldata_t gd, const volatile void *ident)
{
    globaldata_t mygd = mycpu;
    if (gd == mycpu) {
	_wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, mygd->gd_cpuid) |
				PWAKEUP_MYCPU);
    } else {
	lwkt_send_ipiq2(gd, _wakeup, __DEALL(ident),
			PWAKEUP_ENCODE(0, mygd->gd_cpuid) |
			PWAKEUP_MYCPU);
    }
}

/*
 * Wakeup one thread tsleep()ing on the specified ident on the specified cpu
 * only.
 */
void
wakeup_oncpu_one(globaldata_t gd, const volatile void *ident)
{
    globaldata_t mygd = mycpu;
    if (gd == mygd) {
	_wakeup(__DEALL(ident), PWAKEUP_ENCODE(0, mygd->gd_cpuid) |
				PWAKEUP_MYCPU | PWAKEUP_ONE);
    } else {
	lwkt_send_ipiq2(gd, _wakeup, __DEALL(ident),
			PWAKEUP_ENCODE(0, mygd->gd_cpuid) |
			PWAKEUP_MYCPU | PWAKEUP_ONE);
    }
}

/*
 * Wakeup all threads waiting on the specified ident that slept using
 * the specified domain, on all cpus.
 */
void
wakeup_domain(const volatile void *ident, int domain)
{
    _wakeup(__DEALL(ident), PWAKEUP_ENCODE(domain, mycpu->gd_cpuid));
}

/*
 * Wakeup one thread waiting on the specified ident that slept using
 * the specified  domain, on any cpu.
 */
void
wakeup_domain_one(const volatile void *ident, int domain)
{
    /* XXX potentially round-robin the first responding cpu */
    _wakeup(__DEALL(ident),
	    PWAKEUP_ENCODE(domain, mycpu->gd_cpuid) | PWAKEUP_ONE);
}

void
wakeup_start_delayed(void)
{
    globaldata_t gd = mycpu;

    crit_enter();
    gd->gd_curthread->td_flags |= TDF_DELAYED_WAKEUP;
    crit_exit();
}

void
wakeup_end_delayed(void)
{
    globaldata_t gd = mycpu;

    if (gd->gd_curthread->td_flags & TDF_DELAYED_WAKEUP) {
	crit_enter();
	gd->gd_curthread->td_flags &= ~TDF_DELAYED_WAKEUP;
	if (gd->gd_delayed_wakeup[0] || gd->gd_delayed_wakeup[1]) {
	    if (gd->gd_delayed_wakeup[0]) {
		    wakeup(gd->gd_delayed_wakeup[0]);
		    gd->gd_delayed_wakeup[0] = NULL;
	    }
	    if (gd->gd_delayed_wakeup[1]) {
		    wakeup(gd->gd_delayed_wakeup[1]);
		    gd->gd_delayed_wakeup[1] = NULL;
	    }
	}
	crit_exit();
    }
}

/*
 * setrunnable()
 *
 * Make a process runnable.  lp->lwp_token must be held on call and this
 * function must be called from the cpu owning lp.
 *
 * This only has an effect if we are in LSSTOP or LSSLEEP.
 */
void
setrunnable(struct lwp *lp)
{
	thread_t td = lp->lwp_thread;

	ASSERT_LWKT_TOKEN_HELD(&lp->lwp_token);
	KKASSERT(td->td_gd == mycpu);
	crit_enter();
	if (lp->lwp_stat == LSSTOP)
		lp->lwp_stat = LSSLEEP;
	if (lp->lwp_stat == LSSLEEP) {
		_tsleep_remove(td);
		lwkt_schedule(td);
	} else if (td->td_flags & TDF_SINTR) {
		lwkt_schedule(td);
	}
	crit_exit();
}

/*
 * The process is stopped due to some condition, usually because p_stat is
 * set to SSTOP, but also possibly due to being traced.
 *
 * Caller must hold p->p_token
 *
 * NOTE!  If the caller sets SSTOP, the caller must also clear P_WAITED
 * because the parent may check the child's status before the child actually
 * gets to this routine.
 *
 * This routine is called with the current lwp only, typically just
 * before returning to userland if the process state is detected as
 * possibly being in a stopped state.
 */
void
tstop(void)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = lp->lwp_proc;
	struct proc *q;

	lwkt_gettoken(&lp->lwp_token);
	crit_enter();

	/*
	 * If LWP_MP_WSTOP is set, we were sleeping
	 * while our process was stopped.  At this point
	 * we were already counted as stopped.
	 */
	if ((lp->lwp_mpflags & LWP_MP_WSTOP) == 0) {
		/*
		 * If we're the last thread to stop, signal
		 * our parent.
		 */
		p->p_nstopped++;
		atomic_set_int(&lp->lwp_mpflags, LWP_MP_WSTOP);
		wakeup(&p->p_nstopped);
		if (p->p_nstopped == p->p_nthreads) {
			/*
			 * Token required to interlock kern_wait()
			 */
			q = p->p_pptr;
			PHOLD(q);
			lwkt_gettoken(&q->p_token);
			p->p_flags &= ~P_WAITED;
			wakeup(p->p_pptr);
			if ((q->p_sigacts->ps_flag & PS_NOCLDSTOP) == 0)
				ksignal(q, SIGCHLD);
			lwkt_reltoken(&q->p_token);
			PRELE(q);
		}
	}

	/*
	 * Wait here while in a stopped state, interlocked with lwp_token.
	 * We must break-out if the whole process is trying to exit.
	 */
	while (STOPLWP(p, lp)) {
		lp->lwp_stat = LSSTOP;
		tsleep(p, 0, "stop", 0);
	}
	p->p_nstopped--;
	atomic_clear_int(&lp->lwp_mpflags, LWP_MP_WSTOP);
	crit_exit();
	lwkt_reltoken(&lp->lwp_token);
}

/*
 * Compute a tenex style load average of a quantity on
 * 1, 5 and 15 minute intervals.  This is a pcpu callout.
 *
 * We segment the lwp scan on a pcpu basis.  This does NOT
 * mean the associated lwps are on this cpu, it is done
 * just to break the work up.
 *
 * The callout on cpu0 rolls up the stats from the other
 * cpus.
 */
static int loadav_count_runnable(struct lwp *p, void *data);

static void
loadav(void *arg)
{
	globaldata_t gd = mycpu;
	struct loadavg *avg;
	int i, nrun;

	nrun = 0;
	alllwp_scan(loadav_count_runnable, &nrun, 1);
	gd->gd_loadav_nrunnable = nrun;
	if (gd->gd_cpuid == 0) {
		avg = &averunnable;
		nrun = 0;
		for (i = 0; i < ncpus; ++i)
			nrun += globaldata_find(i)->gd_loadav_nrunnable;
		for (i = 0; i < 3; i++) {
			avg->ldavg[i] = (cexp[i] * avg->ldavg[i] +
			    (long)nrun * FSCALE * (FSCALE - cexp[i])) >> FSHIFT;
		}
	}

	/*
	 * Schedule the next update to occur after 5 seconds, but add a
	 * random variation to avoid synchronisation with processes that
	 * run at regular intervals.
	 */
	callout_reset(&gd->gd_loadav_callout,
		      hz * 4 + (int)(krandom() % (hz * 2 + 1)),
		      loadav, NULL);
}

static int
loadav_count_runnable(struct lwp *lp, void *data)
{
	int *nrunp = data;
	thread_t td;

	switch (lp->lwp_stat) {
	case LSRUN:
		if ((td = lp->lwp_thread) == NULL)
			break;
		if (td->td_flags & TDF_BLOCKED)
			break;
		++*nrunp;
		break;
	default:
		break;
	}
	lwkt_yield();
	return(0);
}

/*
 * Regular data collection
 */
static uint64_t
collect_load_callback(int n)
{
	int fscale = averunnable.fscale;

	return ((averunnable.ldavg[0] * 100 + (fscale >> 1)) / fscale);
}

static void
sched_setup(void *dummy __unused)
{
	globaldata_t save_gd = mycpu;
	globaldata_t gd;
	int n;

	kcollect_register(KCOLLECT_LOAD, "load", collect_load_callback,
			  KCOLLECT_SCALE(KCOLLECT_LOAD_FORMAT, 0));

	/*
	 * Kick off timeout driven events by calling first time.  We
	 * split the work across available cpus to help scale it,
	 * it can eat a lot of cpu when there are a lot of processes
	 * on the system.
	 */
	for (n = 0; n < ncpus; ++n) {
		gd = globaldata_find(n);
		lwkt_setcpu_self(gd);
		callout_init_mp(&gd->gd_loadav_callout);
		callout_init_mp(&gd->gd_schedcpu_callout);
		schedcpu(NULL);
		loadav(NULL);
	}
	lwkt_setcpu_self(save_gd);
}

/*
 * Extremely early initialization, dummy-up the tables so we don't have
 * to conditionalize for NULL in _wakeup() and tsleep_interlock().  Even
 * though the system isn't blocking this early, these functions still
 * try to access the hash table.
 *
 * This setup will be overridden once sched_dyninit() -> sleep_gdinit()
 * is called.
 */
void
sleep_early_gdinit(globaldata_t gd)
{
	static struct tslpque	dummy_slpque;
	static cpumask_t dummy_cpumasks;

	slpque_tablesize = 1;
	gd->gd_tsleep_hash = &dummy_slpque;
	slpque_cpumasks = &dummy_cpumasks;
	TAILQ_INIT(&dummy_slpque.queue);
}

/*
 * PCPU initialization.  Called after KMALLOC is operational, by
 * sched_dyninit() for cpu 0, and by mi_gdinit() for other cpus later.
 *
 * WARNING! The pcpu hash table is smaller than the global cpumask
 *	    hash table, which can save us a lot of memory when maxproc
 *	    is set high.
 */
void
sleep_gdinit(globaldata_t gd)
{
	struct thread *td;
	size_t hash_size;
	uint32_t n;
	uint32_t i;

	/*
	 * This shouldn't happen, that is there shouldn't be any threads
	 * waiting on the dummy tsleep queue this early in the boot.
	 */
	if (gd->gd_cpuid == 0) {
		struct tslpque *qp = &gd->gd_tsleep_hash[0];
		TAILQ_FOREACH(td, &qp->queue, td_sleepq) {
			kprintf("SLEEP_GDINIT SWITCH %s\n", td->td_comm);
		}
	}

	/*
	 * Note that we have to allocate one extra slot because we are
	 * shifting a modulo value.  TCHASHSHIFT(slpque_tablesize - 1) can
	 * return the same value as TCHASHSHIFT(slpque_tablesize).
	 */
	n = TCHASHSHIFT(slpque_tablesize) + 1;

	hash_size = sizeof(struct tslpque) * n;
	gd->gd_tsleep_hash = (void *)kmem_alloc3(kernel_map, hash_size,
						 VM_SUBSYS_GD,
						 KM_CPU(gd->gd_cpuid));
	memset(gd->gd_tsleep_hash, 0, hash_size);
	for (i = 0; i < n; ++i)
		TAILQ_INIT(&gd->gd_tsleep_hash[i].queue);
}

/*
 * Dynamic initialization after the memory system is operational.
 */
static void
sched_dyninit(void *dummy __unused)
{
	int tblsize;
	int tblsize2;
	int n;

	/*
	 * Calculate table size for slpque hash.  We want a prime number
	 * large enough to avoid overloading slpque_cpumasks when the
	 * system has a large number of sleeping processes, which will
	 * spam IPIs on wakeup().
	 *
	 * While it is true this is really a per-lwp factor, generally
	 * speaking the maxproc limit is a good metric to go by.
	 */
	for (tblsize = maxproc | 1; ; tblsize += 2) {
		if (tblsize % 3 == 0)
			continue;
		if (tblsize % 5 == 0)
			continue;
		tblsize2 = (tblsize / 2) | 1;
		for (n = 7; n < tblsize2; n += 2) {
			if (tblsize % n == 0)
				break;
		}
		if (n == tblsize2)
			break;
	}

	/*
	 * PIDs are currently limited to 6 digits.  Cap the table size
	 * at double this.
	 */
	if (tblsize > 2000003)
		tblsize = 2000003;

	slpque_tablesize = tblsize;
	slpque_cpumasks = kmalloc(sizeof(*slpque_cpumasks) * slpque_tablesize,
				  M_TSLEEP, M_WAITOK | M_ZERO);
	sleep_gdinit(mycpu);
}
