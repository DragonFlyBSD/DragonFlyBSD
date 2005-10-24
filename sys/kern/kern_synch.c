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
 * $DragonFly: src/sys/kern/kern_synch.c,v 1.50 2005/10/24 22:31:35 eirikn Exp $
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
#include <sys/thread2.h>
#ifdef KTRACE
#include <sys/uio.h>
#include <sys/ktrace.h>
#endif
#include <sys/xwait.h>

#include <machine/cpu.h>
#include <machine/ipl.h>
#include <machine/smp.h>

static void sched_setup (void *dummy);
SYSINIT(sched_setup, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, sched_setup, NULL)

int	hogticks;
int	lbolt;
int	sched_quantum;		/* Roundrobin scheduling quantum in ticks. */
int	ncpus;
int	ncpus2, ncpus2_shift, ncpus2_mask;
int	safepri;

static struct callout loadav_callout;
static struct callout schedcpu_callout;

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
 */
/* ARGSUSED */
static void
schedcpu(void *arg)
{
	struct proc *p;

	FOREACH_PROC_IN_SYSTEM(p) {
		/*
		 * Increment time in/out of memory and sleep time
		 * (if sleeping).  We ignore overflow; with 16-bit int's
		 * (remember them?) overflow takes 45 days.
		 */
		crit_enter();
		p->p_swtime++;
		if (p->p_stat == SSLEEP || p->p_stat == SSTOP)
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
	}
	wakeup((caddr_t)&lbolt);
	callout_reset(&schedcpu_callout, hz, schedcpu, NULL);
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
 * We're only looking at 7 bits of the address; everything is
 * aligned to 4, lots of things are aligned to greater powers
 * of 2.  Shift right by 8, i.e. drop the bottom 256 worth.
 */
#define TABLESIZE	128
static TAILQ_HEAD(slpquehead, thread) slpque[TABLESIZE];
#define LOOKUP(x)	(((intptr_t)(x) >> 8) & (TABLESIZE - 1))

/*
 * General scheduler initialization.  We force a reschedule 25 times
 * a second by default.
 */
void
sleepinit(void)
{
	int i;

	sched_quantum = (hz + 24) / 25;
	hogticks = 2 * sched_quantum;
	for (i = 0; i < TABLESIZE; i++)
		TAILQ_INIT(&slpque[i]);
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
	int sig = 0, catch = flags & PCATCH;
	int id = LOOKUP(ident);
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
	KKASSERT(td != &mycpu->gd_idlethread);	/* you must be kidding! */
	crit_enter_quick(td);
	KASSERT(ident != NULL, ("tsleep: no ident"));
	KASSERT(p == NULL || p->p_stat == SRUN, ("tsleep %p %s %d",
		ident, wmesg, p->p_stat));

	td->td_wchan = ident;
	td->td_wmesg = wmesg;
	td->td_wdomain = flags & PDOMAIN_MASK;
	if (p) {
		if (flags & PNORESCHED)
			td->td_flags |= TDF_NORESCHED;
		p->p_usched->release_curproc(&p->p_lwp);
		p->p_slptime = 0;
	}
	lwkt_deschedule_self(td);
	TAILQ_INSERT_TAIL(&slpque[id], td, td_threadq);
	if (timo) {
		callout_init(&thandle);
		callout_reset(&thandle, timo, endtsleep, td);
	}
	/*
	 * We put ourselves on the sleep queue and start our timeout
	 * before calling CURSIG, as we could stop there, and a wakeup
	 * or a SIGCONT (or both) could occur while we were stopped.
	 * A SIGCONT would cause us to be marked as SSLEEP
	 * without resuming us, thus we must be ready for sleep
	 * when CURSIG is called.  If the wakeup happens while we're
	 * stopped, td->td_wchan will be 0 upon return from CURSIG.
	 */
	if (p) {
		if (catch) {
			p->p_flag |= P_SINTR;
			if ((sig = CURSIG(p))) {
				if (td->td_wchan) {
					unsleep(td);
					lwkt_schedule_self(td);
				}
				p->p_stat = SRUN;
				goto resume;
			}
			if (td->td_wchan == NULL) {
				catch = 0;
				goto resume;
			}
		} else {
			sig = 0;
		}

		/*
		 * If we are not the current process we have to remove ourself
		 * from the run queue.
		 */
		KASSERT(p->p_stat == SRUN, ("PSTAT NOT SRUN %d %d", p->p_pid, p->p_stat));
		/*
		 * If this is the current 'user' process schedule another one.
		 */
		clrrunnable(p, SSLEEP);
		p->p_stats->p_ru.ru_nvcsw++;
		mi_switch(p);
		KASSERT(p->p_stat == SRUN, ("tsleep: stat not srun"));
	} else {
		lwkt_switch();
	}
resume:
	if (p)
		p->p_flag &= ~P_SINTR;
	crit_exit_quick(td);
	td->td_flags &= ~TDF_NORESCHED;
	if (td->td_flags & TDF_TIMEOUT) {
		td->td_flags &= ~TDF_TIMEOUT;
		if (sig == 0)
			return (EWOULDBLOCK);
	} else if (timo) {
		callout_stop(&thandle);
	} else if (td->td_wmesg) {
		/*
		 * This can happen if a thread is woken up directly.  Clear
		 * wmesg to avoid debugging confusion.
		 */
		td->td_wmesg = NULL;
	}
	/* inline of iscaught() */
	if (p) {
		if (catch && (sig != 0 || (sig = CURSIG(p)))) {
			if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
				return (EINTR);
			return (ERESTART);
		}
	}
	return (0);
}

/*
 * Implement the timeout for tsleep.  We interlock against
 * wchan when setting TDF_TIMEOUT.  For processes we remove
 * the sleep if the process is stopped rather then sleeping,
 * so it remains stopped.
 */
static void
endtsleep(void *arg)
{
	thread_t td = arg;
	struct proc *p;

	crit_enter();
	if (td->td_wchan) {
		td->td_flags |= TDF_TIMEOUT;
		if ((p = td->td_proc) != NULL) {
			if (p->p_stat == SSLEEP)
				setrunnable(p);
			else
				unsleep(td);
		} else {
			unsleep(td);
			lwkt_schedule(td);
		}
	}
	crit_exit();
}

/*
 * Remove a process from its wait queue
 */
void
unsleep(struct thread *td)
{
	crit_enter();
	if (td->td_wchan) {
		TAILQ_REMOVE(&slpque[LOOKUP(td->td_wchan)], td, td_threadq);
		td->td_wchan = NULL;
	}
	crit_exit();
}

/*
 * Make all processes sleeping on the specified identifier runnable.
 */
static void
_wakeup(void *ident, int domain, int count)
{
	struct slpquehead *qp;
	struct thread *td;
	struct thread *ntd;
	struct proc *p;
	int id = LOOKUP(ident);

	crit_enter();
	qp = &slpque[id];
restart:
	for (td = TAILQ_FIRST(qp); td != NULL; td = ntd) {
		ntd = TAILQ_NEXT(td, td_threadq);
		if (td->td_wchan == ident && td->td_wdomain == domain) {
			TAILQ_REMOVE(qp, td, td_threadq);
			td->td_wchan = NULL;
			if ((p = td->td_proc) != NULL && p->p_stat == SSLEEP) {
				p->p_stat = SRUN;
				if (p->p_flag & P_INMEM) {
					/*
					 * LWKT scheduled now, there is no
					 * userland runq interaction until
					 * the thread tries to return to user
					 * mode.  We do NOT call setrunqueue().
					 */
					lwkt_schedule(td);
				} else {
					p->p_flag |= P_SWAPINREQ;
					wakeup((caddr_t)&proc0);
				}
				/* END INLINE EXPANSION */
			} else if (p == NULL) {
				lwkt_schedule(td);
			}
			if (--count == 0)
				break;
			goto restart;
		}
	}
	crit_exit();
}

void
wakeup(void *ident)
{
    _wakeup(ident, 0, 0);
}

void
wakeup_one(void *ident)
{
    _wakeup(ident, 0, 1);
}

void
wakeup_domain(void *ident, int domain)
{
    _wakeup(ident, domain, 0);
}

void
wakeup_domain_one(void *ident, int domain)
{
    _wakeup(ident, domain, 1);
}

/*
 * The machine independent parts of mi_switch().
 *
 * 'p' must be the current process.
 */
void
mi_switch(struct proc *p)
{
	thread_t td = p->p_thread;
	struct rlimit *rlim;
	u_int64_t ttime;

	KKASSERT(td == mycpu->gd_curthread);

	crit_enter_quick(td);

	/*
	 * Check if the process exceeds its cpu resource allocation.
	 * If over max, kill it.  Time spent in interrupts is not 
	 * included.  YYY 64 bit match is expensive.  Ick.
	 */
	ttime = td->td_sticks + td->td_uticks;
	if (p->p_stat != SZOMB && p->p_limit->p_cpulimit != RLIM_INFINITY &&
	    ttime > p->p_limit->p_cpulimit) {
		rlim = &p->p_rlimit[RLIMIT_CPU];
		if (ttime / (rlim_t)1000000 >= rlim->rlim_max) {
			killproc(p, "exceeded maximum CPU limit");
		} else {
			psignal(p, SIGXCPU);
			if (rlim->rlim_cur < rlim->rlim_max) {
				/* XXX: we should make a private copy */
				rlim->rlim_cur += 5;
			}
		}
	}

	/*
	 * If we are in a SSTOPped state we deschedule ourselves.  
	 * YYY this needs to be cleaned up, remember that LWKTs stay on
	 * their run queue which works differently then the user scheduler
	 * which removes the process from the runq when it runs it.
	 */
	mycpu->gd_cnt.v_swtch++;
	if (p->p_stat == SSTOP)
		lwkt_deschedule_self(td);
	lwkt_switch();
	crit_exit_quick(td);
}

/*
 * Change process state to be runnable,
 * placing it on the run queue if it is in memory,
 * and awakening the swapper if it isn't in memory.
 */
void
setrunnable(struct proc *p)
{
	crit_enter();

	switch (p->p_stat) {
	case 0:
	case SRUN:
	case SZOMB:
	default:
		panic("setrunnable");
	case SSTOP:
	case SSLEEP:
		unsleep(p->p_thread);	/* e.g. when sending signals */
		break;

	case SIDL:
		break;
	}
	p->p_stat = SRUN;

	/*
	 * The process is controlled by LWKT at this point, we do not mess
	 * around with the userland scheduler until the thread tries to 
	 * return to user mode.  We do not clear p_slptime or call
	 * setrunqueue().
	 */
	if (p->p_flag & P_INMEM) {
		lwkt_schedule(p->p_thread);
	} else {
		p->p_flag |= P_SWAPINREQ;
		wakeup((caddr_t)&proc0);
	}
	crit_exit();
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
 * Change the process state to NOT be runnable, removing it from the run
 * queue.
 */
void
clrrunnable(struct proc *p, int stat)
{
	crit_enter_quick(p->p_thread);
	if (p->p_stat == SRUN && (p->p_flag & P_ONRUNQ))
		p->p_usched->remrunqueue(&p->p_lwp);
	p->p_stat = stat;
	crit_exit_quick(p->p_thread);
}

/*
 * Compute a tenex style load average of a quantity on
 * 1, 5 and 15 minute intervals.
 */
static void
loadav(void *arg)
{
	int i, nrun;
	struct loadavg *avg;
	struct proc *p;
	thread_t td;

	avg = &averunnable;
	nrun = 0;
	FOREACH_PROC_IN_SYSTEM(p) {
		switch (p->p_stat) {
		case SRUN:
			if ((td = p->p_thread) == NULL)
				break;
			if (td->td_flags & TDF_BLOCKED)
				break;
			/* fall through */
		case SIDL:
			nrun++;
			break;
		default:
			break;
		}
	}
	for (i = 0; i < 3; i++)
		avg->ldavg[i] = (cexp[i] * avg->ldavg[i] +
		    nrun * FSCALE * (FSCALE - cexp[i])) >> FSHIFT;

	/*
	 * Schedule the next update to occur after 5 seconds, but add a
	 * random variation to avoid synchronisation with processes that
	 * run at regular intervals.
	 */
	callout_reset(&loadav_callout, hz * 4 + (int)(random() % (hz * 2 + 1)),
	    loadav, NULL);
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

