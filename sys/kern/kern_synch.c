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
 * $DragonFly: src/sys/kern/kern_synch.c,v 1.17 2003/07/11 01:23:24 dillon Exp $
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

static void sched_setup __P((void *dummy));
SYSINIT(sched_setup, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, sched_setup, NULL)

int	hogticks;
int	lbolt;
int	sched_quantum;		/* Roundrobin scheduling quantum in ticks. */
int	ncpus;

static struct callout loadav_callout;

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

static void	endtsleep __P((void *));
static void	loadav __P((void *arg));
static void	maybe_resched __P((struct proc *chk));
static void	roundrobin __P((void *arg));
static void	schedcpu __P((void *arg));
static void	updatepri __P((struct proc *p));
static void	crit_panicints(void);

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
 * Arrange to reschedule if necessary by checking to see if the current
 * process is on the highest priority user scheduling queue.  This may
 * be run from an interrupt so we have to follow any preemption chains
 * back to the original process.
 */
static void
maybe_resched(struct proc *chk)
{
	struct proc *cur = lwkt_preempted_proc();

	if (cur == NULL)
		return;

	/*
	 * Check the user queue (realtime, normal, idle).  Lower numbers
	 * indicate higher priority queues.  Lower numbers are also better
	 * for p_priority.
	 */
	if (chk->p_rtprio.type < cur->p_rtprio.type) {
		need_resched();
	} else if (chk->p_rtprio.type == cur->p_rtprio.type) {
		if (chk->p_rtprio.type == RTP_PRIO_NORMAL) {
			if (chk->p_priority / PPQ < cur->p_priority / PPQ)
				need_resched();
		} else {
			if (chk->p_rtprio.prio < cur->p_rtprio.prio)
				need_resched();
		}
	}
}

int 
roundrobin_interval(void)
{
	return (sched_quantum);
}

/*
 * Force switch among equal priority processes every 100ms.
 */
#ifdef SMP

static void
roundrobin_remote(void *arg)
{
	struct proc *p = lwkt_preempted_proc();
 	if (p == NULL || RTP_PRIO_NEED_RR(p->p_rtprio.type))
		need_resched();
}

#endif

static void
roundrobin(void *arg)
{
	struct proc *p = lwkt_preempted_proc();
 	if (p == NULL || RTP_PRIO_NEED_RR(p->p_rtprio.type))
		need_resched();
#ifdef SMP
	lwkt_send_ipiq_mask(mycpu->gd_other_cpus, roundrobin_remote, NULL);
#endif
 	timeout(roundrobin, NULL, sched_quantum);
}

void
resched_cpus(u_int32_t mask)
{
	lwkt_send_ipiq_mask(mask, roundrobin_remote, NULL);
}

/*
 * Constants for digital decay and forget:
 *	90% of (p_estcpu) usage in 5 * loadav time
 *	95% of (p_pctcpu) usage in 60 seconds (load insensitive)
 *          Note that, as ps(1) mentions, this can let percentages
 *          total over 100% (I've seen 137.9% for 3 processes).
 *
 * Note that schedclock() updates p_estcpu and p_cpticks asynchronously.
 *
 * We wish to decay away 90% of p_estcpu in (5 * loadavg) seconds.
 * That is, the system wants to compute a value of decay such
 * that the following for loop:
 * 	for (i = 0; i < (5 * loadavg); i++)
 * 		p_estcpu *= decay;
 * will compute
 * 	p_estcpu *= 0.1;
 * for all values of loadavg:
 *
 * Mathematically this loop can be expressed by saying:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * The system computes decay as:
 * 	decay = (2 * loadavg) / (2 * loadavg + 1)
 *
 * We wish to prove that the system's computation of decay
 * will always fulfill the equation:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * If we compute b as:
 * 	b = 2 * loadavg
 * then
 * 	decay = b / (b + 1)
 *
 * We now need to prove two things:
 *	1) Given factor ** (5 * loadavg) ~= .1, prove factor == b/(b+1)
 *	2) Given b/(b+1) ** power ~= .1, prove power == (5 * loadavg)
 *
 * Facts:
 *         For x close to zero, exp(x) =~ 1 + x, since
 *              exp(x) = 0! + x**1/1! + x**2/2! + ... .
 *              therefore exp(-1/b) =~ 1 - (1/b) = (b-1)/b.
 *         For x close to zero, ln(1+x) =~ x, since
 *              ln(1+x) = x - x**2/2 + x**3/3 - ...     -1 < x < 1
 *              therefore ln(b/(b+1)) = ln(1 - 1/(b+1)) =~ -1/(b+1).
 *         ln(.1) =~ -2.30
 *
 * Proof of (1):
 *    Solve (factor)**(power) =~ .1 given power (5*loadav):
 *	solving for factor,
 *      ln(factor) =~ (-2.30/5*loadav), or
 *      factor =~ exp(-1/((5/2.30)*loadav)) =~ exp(-1/(2*loadav)) =
 *          exp(-1/b) =~ (b-1)/b =~ b/(b+1).                    QED
 *
 * Proof of (2):
 *    Solve (factor)**(power) =~ .1 given factor == (b/(b+1)):
 *	solving for power,
 *      power*ln(b/(b+1)) =~ -2.30, or
 *      power =~ 2.3 * (b + 1) = 4.6*loadav + 2.3 =~ 5*loadav.  QED
 *
 * Actual power values for the implemented algorithm are as follows:
 *      loadav: 1       2       3       4
 *      power:  5.68    10.32   14.94   19.55
 */

/* calculations for digital decay to forget 90% of usage in 5*loadav sec */
#define	loadfactor(loadav)	(2 * (loadav))
#define	decay_cpu(loadfac, cpu)	(((loadfac) * (cpu)) / ((loadfac) + FSCALE))

/* decay 95% of `p_pctcpu' in 60 seconds; see CCPU_SHIFT before changing */
static fixpt_t	ccpu = 0.95122942450071400909 * FSCALE;	/* exp(-1/20) */
SYSCTL_INT(_kern, OID_AUTO, ccpu, CTLFLAG_RD, &ccpu, 0, "");

/* kernel uses `FSCALE', userland (SHOULD) use kern.fscale */
static int	fscale __unused = FSCALE;
SYSCTL_INT(_kern, OID_AUTO, fscale, CTLFLAG_RD, 0, FSCALE, "");

/*
 * If `ccpu' is not equal to `exp(-1/20)' and you still want to use the
 * faster/more-accurate formula, you'll have to estimate CCPU_SHIFT below
 * and possibly adjust FSHIFT in "param.h" so that (FSHIFT >= CCPU_SHIFT).
 *
 * To estimate CCPU_SHIFT for exp(-1/20), the following formula was used:
 *	1 - exp(-1/20) ~= 0.0487 ~= 0.0488 == 1 (fixed pt, *11* bits).
 *
 * If you don't want to bother with the faster/more-accurate formula, you
 * can set CCPU_SHIFT to (FSHIFT + 1) which will use a slower/less-accurate
 * (more general) method of calculating the %age of CPU used by a process.
 */
#define	CCPU_SHIFT	11

/*
 * Recompute process priorities, every hz ticks.
 */
/* ARGSUSED */
static void
schedcpu(void *arg)
{
	fixpt_t loadfac = loadfactor(averunnable.ldavg[0]);
	struct proc *p;
	struct proc *curp;
	int realstathz, s;

	curp = lwkt_preempted_proc(); /* YYY temporary hack */

	realstathz = stathz ? stathz : hz;
	LIST_FOREACH(p, &allproc, p_list) {
		/*
		 * Increment time in/out of memory and sleep time
		 * (if sleeping).  We ignore overflow; with 16-bit int's
		 * (remember them?) overflow takes 45 days.
		 */
		p->p_swtime++;
		if (p->p_stat == SSLEEP || p->p_stat == SSTOP)
			p->p_slptime++;
		p->p_pctcpu = (p->p_pctcpu * ccpu) >> FSHIFT;
		/*
		 * If the process has slept the entire second,
		 * stop recalculating its priority until it wakes up.
		 */
		if (p->p_slptime > 1)
			continue;
		s = splhigh();	/* prevent state changes and protect run queue */
		/*
		 * p_pctcpu is only for ps.
		 */
#if	(FSHIFT >= CCPU_SHIFT)
		p->p_pctcpu += (realstathz == 100)?
			((fixpt_t) p->p_cpticks) << (FSHIFT - CCPU_SHIFT):
                	100 * (((fixpt_t) p->p_cpticks)
				<< (FSHIFT - CCPU_SHIFT)) / realstathz;
#else
		p->p_pctcpu += ((FSCALE - ccpu) *
			(p->p_cpticks * FSCALE / realstathz)) >> FSHIFT;
#endif
		p->p_cpticks = 0;
		p->p_estcpu = decay_cpu(loadfac, p->p_estcpu);
		resetpriority(p);
		splx(s);
	}
	wakeup((caddr_t)&lbolt);
	timeout(schedcpu, (void *)0, hz);
}

/*
 * Recalculate the priority of a process after it has slept for a while.
 * For all load averages >= 1 and max p_estcpu of 255, sleeping for at
 * least six times the loadfactor will decay p_estcpu to zero.
 */
static void
updatepri(struct proc *p)
{
	unsigned int newcpu = p->p_estcpu;
	fixpt_t loadfac = loadfactor(averunnable.ldavg[0]);

	if (p->p_slptime > 5 * loadfac) {
		p->p_estcpu = 0;
	} else {
		p->p_slptime--;	/* the first time was done in schedcpu */
		while (newcpu && --p->p_slptime)
			newcpu = decay_cpu(loadfac, newcpu);
		p->p_estcpu = newcpu;
	}
	resetpriority(p);
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
 * During autoconfiguration or after a panic, a sleep will simply
 * lower the priority briefly to allow interrupts, then return.
 * The priority to be used (safepri) is machine-dependent, thus this
 * value is initialized and maintained in the machine-dependent layers.
 * This priority will typically be 0, or the lowest priority
 * that is safe for use on the interrupt stack; it can be made
 * higher to block network software interrupts after panics.
 */
int safepri;

void
sleepinit(void)
{
	int i;

	sched_quantum = hz/10;
	hogticks = 2 * sched_quantum;
	for (i = 0; i < TABLESIZE; i++)
		TAILQ_INIT(&slpque[i]);
}

/*
 * General sleep call.  Suspends the current process until a wakeup is
 * performed on the specified identifier.  The process will then be made
 * runnable with the specified priority.  Sleeps at most timo/hz seconds
 * (0 means no timeout).  If pri includes PCATCH flag, signals are checked
 * before and after sleeping, else signals are not checked.  Returns 0 if
 * awakened, EWOULDBLOCK if the timeout expires.  If PCATCH is set and a
 * signal needs to be delivered, ERESTART is returned if the current system
 * call should be restarted if possible, and EINTR is returned if the system
 * call should be interrupted by the signal (return EINTR).
 *
 * If the process has P_CURPROC set mi_switch() will not re-queue it to
 * the userland scheduler queues because we are in a SSLEEP state.  If
 * we are not the current process then we have to remove ourselves from
 * the scheduler queues.
 *
 * YYY priority now unused
 */
int
tsleep(ident, priority, wmesg, timo)
	void *ident;
	int priority, timo;
	const char *wmesg;
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;		/* may be NULL */
	int s, sig = 0, catch = priority & PCATCH;
	int id = LOOKUP(ident);
	struct callout_handle thandle;

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
		crit_panicints();
		return (0);
	}
	KKASSERT(td != &mycpu->gd_idlethread);	/* you must be kidding! */
	s = splhigh();
	KASSERT(ident != NULL, ("tsleep: no ident"));
	KASSERT(p == NULL || p->p_stat == SRUN, ("tsleep %p %s %d",
		ident, wmesg, p->p_stat));

	crit_enter();
	td->td_wchan = ident;
	td->td_wmesg = wmesg;
	if (p) 
		p->p_slptime = 0;
	lwkt_deschedule_self();
	TAILQ_INSERT_TAIL(&slpque[id], td, td_threadq);
	if (timo)
		thandle = timeout(endtsleep, (void *)td, timo);
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
					lwkt_schedule_self();
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
		KKASSERT(td->td_release || (p->p_flag & P_CURPROC) == 0);
		mi_switch();
		KASSERT(p->p_stat == SRUN, ("tsleep: stat not srun"));
	} else {
		lwkt_switch();
	}
resume:
	crit_exit();
	if (p)
		p->p_flag &= ~P_SINTR;
	splx(s);
	if (td->td_flags & TDF_TIMEOUT) {
		td->td_flags &= ~TDF_TIMEOUT;
		if (sig == 0)
			return (EWOULDBLOCK);
	} else if (timo) {
		untimeout(endtsleep, (void *)td, thandle);
	}
	if (p) {
		if (catch && (sig != 0 || (sig = CURSIG(p)))) {
			if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
				return (EINTR);
			return (ERESTART);
		}
	}
	return (0);
}

#if 0

/*
 * General sleep call.  Suspends the current process until a wakeup is
 * performed on the specified xwait structure.  The process will then be made
 * runnable with the specified priority.  Sleeps at most timo/hz seconds
 * (0 means no timeout).  If pri includes PCATCH flag, signals are checked
 * before and after sleeping, else signals are not checked.  Returns 0 if
 * awakened, EWOULDBLOCK if the timeout expires.  If PCATCH is set and a
 * signal needs to be delivered, ERESTART is returned if the current system
 * call should be restarted if possible, and EINTR is returned if the system
 * call should be interrupted by the signal (return EINTR).
 *
 * If the passed generation number is different from the generation number
 * in the xwait, return immediately.
 */
int
xsleep(struct xwait *w, int priority, const char *wmesg, int timo, int *gen)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int s, sig, catch = priority & PCATCH;
	struct callout_handle thandle;

#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW))
		ktrcsw(p->p_tracep, 1, 0);
#endif
	if (cold || panicstr) {
		/*
		 * After a panic, or during autoconfiguration,
		 * just give interrupts a chance, then just return;
		 * don't run any other procs or panic below,
		 * in case this is the idle process and already asleep.
		 */
		crit_panicints();
		return (0);
	}
	s = splhigh();
	KASSERT(p != NULL, ("xsleep1"));
	KASSERT(w != NULL && p->p_stat == SRUN, ("xsleep"));

	/*
	 * If the generation number does not match we return immediately.
	 */
	if (*gen != w->gen) {
		*gen = w->gen;
		splx(s);
#ifdef KTRACE
		if (KTRPOINT(td, KTR_CSW))
			ktrcsw(p->p_tracep, 0, 0);
#endif
		return(0);
	}

	p->p_wchan = w;
	p->p_wmesg = wmesg;
	p->p_slptime = 0;
	p->p_flag |= P_XSLEEP;
	TAILQ_INSERT_TAIL(&w->waitq, p, p_procq);
	if (timo)
		thandle = timeout(endtsleep, (void *)p, timo);
	/*
	 * We put ourselves on the sleep queue and start our timeout
	 * before calling CURSIG, as we could stop there, and a wakeup
	 * or a SIGCONT (or both) could occur while we were stopped.
	 * A SIGCONT would cause us to be marked as SSLEEP
	 * without resuming us, thus we must be ready for sleep
	 * when CURSIG is called.  If the wakeup happens while we're
	 * stopped, p->p_wchan will be 0 upon return from CURSIG.
	 */
	if (catch) {
		p->p_flag |= P_SINTR;
		if ((sig = CURSIG(p))) {
			if (p->p_wchan) {
				unsleep(p);
				lwkt_schedule_self();
			}
			p->p_stat = SRUN;
			goto resume;
		}
		if (p->p_wchan == NULL) {
			catch = 0;
			goto resume;
		}
	} else {
		sig = 0;
	}
	clrrunnable(p, SSLEEP);
	p->p_stats->p_ru.ru_nvcsw++;
	mi_switch();
resume:
	*gen = w->gen;	/* update generation number */
	splx(s);
	p->p_flag &= ~P_SINTR;
	if (p->p_flag & P_TIMEOUT) {
		p->p_flag &= ~P_TIMEOUT;
		if (sig == 0) {
#ifdef KTRACE
			if (KTRPOINT(td, KTR_CSW))
				ktrcsw(p->p_tracep, 0, 0);
#endif
			return (EWOULDBLOCK);
		}
	} else if (timo)
		untimeout(endtsleep, (void *)p, thandle);
	if (catch && (sig != 0 || (sig = CURSIG(p)))) {
#ifdef KTRACE
		if (KTRPOINT(td, KTR_CSW))
			ktrcsw(p->p_tracep, 0, 0);
#endif
		if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
			return (EINTR);
		return (ERESTART);
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_CSW))
		ktrcsw(p->p_tracep, 0, 0);
#endif
	return (0);
}

#endif

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
	int s;

	s = splhigh();
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
	splx(s);
}

/*
 * Remove a process from its wait queue
 */
void
unsleep(struct thread *td)
{
	int s;

	s = splhigh();
	if (td->td_wchan) {
#if 0
		if (p->p_flag & P_XSLEEP) {
			struct xwait *w = p->p_wchan;
			TAILQ_REMOVE(&w->waitq, p, p_procq);
			p->p_flag &= ~P_XSLEEP;
		} else
#endif
		TAILQ_REMOVE(&slpque[LOOKUP(td->td_wchan)], td, td_threadq);
		td->td_wchan = NULL;
	}
	splx(s);
}

#if 0
/*
 * Make all processes sleeping on the explicit lock structure runnable.
 */
void
xwakeup(struct xwait *w)
{
	struct proc *p;
	int s;

	s = splhigh();
	++w->gen;
	while ((p = TAILQ_FIRST(&w->waitq)) != NULL) {
		TAILQ_REMOVE(&w->waitq, p, p_procq);
		KASSERT(p->p_wchan == w && (p->p_flag & P_XSLEEP),
		    ("xwakeup: wchan mismatch for %p (%p/%p) %08x", p, p->p_wchan, w, p->p_flag & P_XSLEEP));
		p->p_wchan = NULL;
		p->p_flag &= ~P_XSLEEP;
		if (p->p_stat == SSLEEP) {
			/* OPTIMIZED EXPANSION OF setrunnable(p); */
			if (p->p_slptime > 1)
				updatepri(p);
			p->p_slptime = 0;
			p->p_stat = SRUN;
			if (p->p_flag & P_INMEM) {
				setrunqueue(p);
				maybe_resched(p);
			} else {
				p->p_flag |= P_SWAPINREQ;
				wakeup((caddr_t)&proc0);
			}
		}
	}
	splx(s);
}
#endif

/*
 * Make all processes sleeping on the specified identifier runnable.
 */
static void
_wakeup(void *ident, int count)
{
	struct slpquehead *qp;
	struct thread *td;
	struct thread *ntd;
	struct proc *p;
	int s;
	int id = LOOKUP(ident);

	s = splhigh();
	qp = &slpque[id];
restart:
	for (td = TAILQ_FIRST(qp); td != NULL; td = ntd) {
		ntd = TAILQ_NEXT(td, td_threadq);
		if (td->td_wchan == ident) {
			TAILQ_REMOVE(qp, td, td_threadq);
			td->td_wchan = NULL;
			if ((p = td->td_proc) != NULL && p->p_stat == SSLEEP) {
				/* OPTIMIZED EXPANSION OF setrunnable(p); */
				if (p->p_slptime > 1)
					updatepri(p);
				p->p_slptime = 0;
				p->p_stat = SRUN;
				if (p->p_flag & P_INMEM) {
					setrunqueue(p);
					if (p->p_flag & P_CURPROC)
					    maybe_resched(p);
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
	splx(s);
}

void
wakeup(void *ident)
{
    _wakeup(ident, 0);
}

void
wakeup_one(void *ident)
{
    _wakeup(ident, 1);
}

/*
 * The machine independent parts of mi_switch().
 * Must be called at splstatclock() or higher.
 */
void
mi_switch()
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;	/* XXX */
	struct rlimit *rlim;
	int x;
	u_int64_t ttime;

	/*
	 * XXX this spl is almost unnecessary.  It is partly to allow for
	 * sloppy callers that don't do it (issignal() via CURSIG() is the
	 * main offender).  It is partly to work around a bug in the i386
	 * cpu_switch() (the ipl is not preserved).  We ran for years
	 * without it.  I think there was only a interrupt latency problem.
	 * The main caller, tsleep(), does an splx() a couple of instructions
	 * after calling here.  The buggy caller, issignal(), usually calls
	 * here at spl0() and sometimes returns at splhigh().  The process
	 * then runs for a little too long at splhigh().  The ipl gets fixed
	 * when the process returns to user mode (or earlier).
	 *
	 * It would probably be better to always call here at spl0(). Callers
	 * are prepared to give up control to another process, so they must
	 * be prepared to be interrupted.  The clock stuff here may not
	 * actually need splstatclock().
	 */
	x = splstatclock();
	clear_resched();

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
	 * Pick a new current process and record its start time.  If we
	 * are in a SSTOPped state we deschedule ourselves.  YYY this needs
	 * to be cleaned up, remember that LWKTs stay on their run queue
	 * which works differently then the user scheduler which removes
	 * the process from the runq when it runs it.
	 */
	mycpu->gd_cnt.v_swtch++;
	if (p->p_stat == SSTOP)
		lwkt_deschedule_self();
	lwkt_switch();

	splx(x);
}

/*
 * Change process state to be runnable,
 * placing it on the run queue if it is in memory,
 * and awakening the swapper if it isn't in memory.
 */
void
setrunnable(struct proc *p)
{
	int s;

	s = splhigh();
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
	if (p->p_flag & P_INMEM)
		setrunqueue(p);
	splx(s);
	if (p->p_slptime > 1)
		updatepri(p);
	p->p_slptime = 0;
	if ((p->p_flag & P_INMEM) == 0) {
		p->p_flag |= P_SWAPINREQ;
		wakeup((caddr_t)&proc0);
	} else {
		maybe_resched(p);
	}
}

/*
 * Change the process state to NOT be runnable, removing it from the run
 * queue.  If P_CURPROC is not set and we are in SRUN the process is on the
 * run queue (If P_INMEM is not set then it isn't because it is swapped).
 */
void
clrrunnable(struct proc *p, int stat)
{
	int s;

	s = splhigh();
	switch(p->p_stat) {
	case SRUN:
		if (p->p_flag & P_ONRUNQ)
			remrunqueue(p);
		break;
	default:
		break;
	}
	p->p_stat = stat;
	splx(s);
}

/*
 * Compute the priority of a process when running in user mode.
 * Arrange to reschedule if the resulting priority is better
 * than that of the current process.
 *
 * YYY real time / idle procs do not use p_priority XXX
 */
void
resetpriority(struct proc *p)
{
	unsigned int newpriority;
	int opq;
	int npq;

	if (p->p_rtprio.type != RTP_PRIO_NORMAL)
		return;
	newpriority = PUSER + p->p_estcpu / INVERSE_ESTCPU_WEIGHT +
	    NICE_WEIGHT * p->p_nice;
	newpriority = min(newpriority, MAXPRI);
	npq = newpriority / PPQ;
	crit_enter();
	opq = p->p_priority / PPQ;
	if (p->p_stat == SRUN && (p->p_flag & P_ONRUNQ) && opq != npq) {
		/*
		 * We have to move the process to another queue
		 */
		remrunqueue(p);
		p->p_priority = newpriority;
		setrunqueue(p);
	} else {
		/*
		 * We can just adjust the priority and it will be picked
		 * up later.
		 */
		KKASSERT(opq == npq || (p->p_flag & P_ONRUNQ) == 0);
		p->p_priority = newpriority;
	}
	crit_exit();
	maybe_resched(p);
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

	avg = &averunnable;
	nrun = 0;
	LIST_FOREACH(p, &allproc, p_list) {
		switch (p->p_stat) {
		case SRUN:
		case SIDL:
			nrun++;
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
sched_setup(dummy)
	void *dummy;
{

	callout_init(&loadav_callout);

	/* Kick off timeout driven events by calling first time. */
	roundrobin(NULL);
	schedcpu(NULL);
	loadav(NULL);
}

/*
 * We adjust the priority of the current process.  The priority of
 * a process gets worse as it accumulates CPU time.  The cpu usage
 * estimator (p_estcpu) is increased here.  resetpriority() will
 * compute a different priority each time p_estcpu increases by
 * INVERSE_ESTCPU_WEIGHT
 * (until MAXPRI is reached).  The cpu usage estimator ramps up
 * quite quickly when the process is running (linearly), and decays
 * away exponentially, at a rate which is proportionally slower when
 * the system is busy.  The basic principle is that the system will
 * 90% forget that the process used a lot of CPU time in 5 * loadav
 * seconds.  This causes the system to favor processes which haven't
 * run much recently, and to round-robin among other processes.
 */
void
schedclock(p)
	struct proc *p;
{

	p->p_cpticks++;
	p->p_estcpu = ESTCPULIM(p->p_estcpu + 1);
	if ((p->p_estcpu % INVERSE_ESTCPU_WEIGHT) == 0)
		resetpriority(p);
}

static
void
crit_panicints(void)
{
    int s;
    int cpri;

    s = splhigh();
    cpri = crit_panic_save();
    splx(safepri);
    crit_panic_restore(cpri);
    splx(s);
}

