/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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

/*
 * WARNING!  THE SYSTIMER MODULE DOES NOT OPERATE OR DISPATCH WITH THE
 * MP LOCK HELD.  ALL CODE USING THIS MODULE MUST BE MP-SAFE.
 *
 * This code implements a fine-grained per-cpu system timer which is
 * ultimately based on a hardware timer.  The hardware timer abstraction
 * is sufficiently disconnected from this code to support both per-cpu
 * hardware timers or a single system-wide hardware timer.
 *
 * WARNING!  During early boot if a new system timer is selected, existing
 * timeouts will not be effected and will thus occur slower or faster.
 * periodic timers will be adjusted at the next periodic load.
 *
 * Notes on machine-dependant code (in arch/arch/systimer.c)
 *
 * cputimer_intr_reload()	Reload the one-shot (per-cpu basis)
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/systimer.h>
#include <sys/thread2.h>

/*
 * Execute ready systimers.  Called directly from the platform-specific
 * one-shot timer clock interrupt (e.g. clkintr()) or via an IPI.  May
 * be called simultaniously on multiple cpus and always operations on 
 * the current cpu's queue.  Systimer functions are responsible for calling
 * hardclock, statclock, and other finely-timed routines.
 */
void
systimer_intr(sysclock_t *timep, int in_ipi, struct intrframe *frame)
{
    globaldata_t gd = mycpu;
    sysclock_t time = *timep;
    systimer_t info;

    if (gd->gd_syst_nest)
	return;

    crit_enter();
    ++gd->gd_syst_nest;
    while ((info = TAILQ_FIRST(&gd->gd_systimerq)) != NULL) {
	/*
	 * If we haven't reached the requested time, tell the cputimer
	 * how much is left and break out.
	 */
	if ((ssysclock_t)(info->time - time) > 0) {
	    cputimer_intr_reload(info->time - time);
	    break;
	}

	/*
	 * Dequeue and execute, detect a loss of the systimer.  Note
	 * that the in-progress systimer pointer can only be used to
	 * detect a loss of the systimer, it is only useful within
	 * this code sequence and becomes stale otherwise.
	 */
	info->flags &= ~SYSTF_ONQUEUE;
	TAILQ_REMOVE(info->queue, info, node);
	gd->gd_systimer_inprog = info;
	crit_exit();
	info->func(info, in_ipi, frame);
	crit_enter();

	/*
	 * The caller may deleted or even re-queue the systimer itself
	 * with a delete/add sequence.  If the caller does not mess with
	 * the systimer we will requeue the periodic interval automatically.
	 *
	 * If this is a non-queued periodic interrupt, do not allow multiple
	 * events to build up (used for things like the callout timer to
	 * prevent premature timeouts due to long interrupt disablements,
	 * BIOS 8254 glitching, and so forth).  However, we still want to
	 * keep things synchronized between cpus for efficient handling of
	 * the timer interrupt so jump in multiples of the periodic rate.
	 */
	if (gd->gd_systimer_inprog == info && info->periodic) {
	    if (info->which != sys_cputimer) {
		info->periodic = sys_cputimer->fromhz(info->freq);
		info->which = sys_cputimer;
	    }
	    info->time += info->periodic;
	    if ((info->flags & SYSTF_NONQUEUED) &&
		(ssysclock_t)(info->time - time) <= 0
	    ) {
		info->time += roundup(time - info->time, info->periodic);
	    }
	    systimer_add(info);
	}
	gd->gd_systimer_inprog = NULL;
    }
    --gd->gd_syst_nest;
    crit_exit();
}

void
systimer_intr_enable(void)
{
    cputimer_intr_enable();
}

/*
 * MPSAFE
 */
void
systimer_add(systimer_t info)
{
    struct globaldata *gd = mycpu;

    KKASSERT((info->flags & SYSTF_ONQUEUE) == 0);
    crit_enter();
    if (info->gd == gd) {
	systimer_t scan1;
	systimer_t scan2;
	scan1 = TAILQ_FIRST(&gd->gd_systimerq);
	if (scan1 == NULL || (ssysclock_t)(scan1->time - info->time) > 0) {
	    cputimer_intr_reload(info->time - sys_cputimer->count());
	    TAILQ_INSERT_HEAD(&gd->gd_systimerq, info, node);
	} else {
	    scan2 = TAILQ_LAST(&gd->gd_systimerq, systimerq);
	    for (;;) {
		if (scan1 == NULL) {
		    TAILQ_INSERT_TAIL(&gd->gd_systimerq, info, node);
		    break;
		}
		if (info->flags & SYSTF_FIRST) {
			/*
			 * When coincident events occur, the event being
			 * added wants to be placed before the others.
			 */
			if ((ssysclock_t)(scan1->time - info->time) >= 0) {
			    TAILQ_INSERT_BEFORE(scan1, info, node);
			    break;
			}
			if ((ssysclock_t)(scan2->time - info->time) < 0) {
			    TAILQ_INSERT_AFTER(&gd->gd_systimerq, scan2,
					       info, node);
			    break;
			}
		} else {
			/*
			 * When coincident events occur, the event being
			 * added should be placed after the others.  This
			 * is the default.
			 */
			if ((ssysclock_t)(scan1->time - info->time) > 0) {
			    TAILQ_INSERT_BEFORE(scan1, info, node);
			    break;
			}
			if ((ssysclock_t)(scan2->time - info->time) <= 0) {
			    TAILQ_INSERT_AFTER(&gd->gd_systimerq, scan2,
					        info, node);
			    break;
			}
		}
		scan1 = TAILQ_NEXT(scan1, node);
		scan2 = TAILQ_PREV(scan2, systimerq, node);
	    }
	}
	info->flags = (info->flags | SYSTF_ONQUEUE) & ~SYSTF_IPIRUNNING;
	info->queue = &gd->gd_systimerq;
    } else {
	KKASSERT((info->flags & SYSTF_IPIRUNNING) == 0);
	info->flags |= SYSTF_IPIRUNNING;
	lwkt_send_ipiq(info->gd, (ipifunc1_t)systimer_add, info);
    }
    crit_exit();
}

/*
 * systimer_del()
 *
 *	Delete a system timer.  Only the owning cpu can delete a timer.
 *
 * MPSAFE
 */
void
systimer_del(systimer_t info)
{
    struct globaldata *gd = info->gd;

    KKASSERT(gd == mycpu && (info->flags & SYSTF_IPIRUNNING) == 0);

    crit_enter();

    if (info->flags & SYSTF_ONQUEUE) {
	TAILQ_REMOVE(info->queue, info, node);
	info->flags &= ~SYSTF_ONQUEUE;
    }

    /*
     * Deal with dispatch races by clearing the in-progress systimer
     * pointer.  Only a direct pointer comparison can be used, the
     * actual contents of the structure gd_systimer_inprog points to,
     * if not equal to info, may be stale.
     */
    if (gd->gd_systimer_inprog == info)
	gd->gd_systimer_inprog = NULL;

    crit_exit();
}

/*
 * systimer_init_periodic*()
 *
 *	Initialize a periodic timer at the specified frequency and add
 *	it to the system.  The frequency is uncompensated and approximate.
 *
 *	Try to synchronize multiple registrations of the same or similar
 *	frequencies so the hardware interrupt is able to dispatch several
 *	together.  We do this by adjusting the phase of the initial timeout.
 *	This helps SMP.  Note that we are not attempting to synchronize to
 *	the realtime clock.
 *
 *	This synchronization is also depended upon for statclock, hardclock,
 *	and schedclock.
 */
static __inline
void
_systimer_init_periodic(systimer_t info, systimer_func_t func, void *data,
			int64_t freq, int flags)
{
    sysclock_t base_count;

    if (sys_cputimer->sync_base == 0)
	sys_cputimer->sync_base = sys_cputimer->count();

    bzero(info, sizeof(struct systimer));

    if ((flags & SYSTF_100KHZSYNC) && freq <= 100000)
	    info->periodic = sys_cputimer->fromhz(100000) * (100000 / freq);
    if ((flags & SYSTF_MSSYNC) && freq <= 1000)
	    info->periodic = sys_cputimer->fromhz(1000) * (1000 / freq);
    else
	    info->periodic = sys_cputimer->fromhz(freq);

    base_count = sys_cputimer->count();
    base_count = base_count -
		 (base_count - sys_cputimer->sync_base) % info->periodic;
    info->time = base_count + info->periodic;
    if (flags & SYSTF_OFFSETCPU)
	    info->time += mycpu->gd_cpuid * info->periodic / ncpus;
    if (flags & SYSTF_OFFSET50)
	    info->time += info->periodic / 2;
    info->func = func;
    info->data = data;
    info->freq = freq;
    info->which = sys_cputimer;
    info->gd = mycpu;
    info->flags |= flags;
    systimer_add(info);
}

void
systimer_init_periodic(systimer_t info, systimer_func_t func, void *data,
		       int64_t freq)
{
	_systimer_init_periodic(info, func, data, freq, 0);
}

void
systimer_init_periodic_nq(systimer_t info, systimer_func_t func, void *data,
			  int64_t freq)
{
	_systimer_init_periodic(info, func, data, freq, SYSTF_NONQUEUED);
}

/*
 * These provide systimers whos periods are in perfect multiples of 1ms
 * or 0.1uS.  This is used in situations where the caller wants to gang
 * multiple systimers together whos periods may have some coincident events,
 * in order for those coincident events to generate only one interrupt.
 *
 * This also allows the caller to make event ordering assumptions for
 * said coincident events.
 */
void
systimer_init_periodic_nq1khz(systimer_t info, systimer_func_t func,
			      void *data, int64_t freq)
{
	_systimer_init_periodic(info, func, data, freq,
				SYSTF_NONQUEUED | SYSTF_MSSYNC);
}

void
systimer_init_periodic_nq100khz(systimer_t info, systimer_func_t func,
				void *data, int64_t freq)
{
	_systimer_init_periodic(info, func, data, freq,
				SYSTF_NONQUEUED | SYSTF_100KHZSYNC);
}

void
systimer_init_periodic_flags(systimer_t info, systimer_func_t func,
				void *data, int64_t freq, int flags)
{
	_systimer_init_periodic(info, func, data, freq, flags);
}


/*
 * Adjust the periodic interval for a periodic timer which is already
 * running.  The current timeout is not effected.
 */
void
systimer_adjust_periodic(systimer_t info, int64_t freq)
{
    crit_enter();
    info->periodic = sys_cputimer->fromhz(freq);
    info->freq = freq;
    info->which = sys_cputimer;
    crit_exit();
}

/*
 * systimer_init_oneshot()
 *
 *	Initialize a periodic timer at the specified frequency and add
 *	it to the system.  The frequency is uncompensated and approximate.
 */
void
systimer_init_oneshot(systimer_t info, systimer_func_t func,
		      void *data, int64_t us)
{
    bzero(info, sizeof(struct systimer));
    info->time = sys_cputimer->count() + sys_cputimer->fromus(us);
    info->func = func;
    info->data = data;
    info->which = sys_cputimer;
    info->gd = mycpu;
    info->us = us;
    systimer_add(info);
}

/*
 * sys_cputimer was changed, recalculate all existing systimers and kick the
 * new interrupt.
 */
static void
systimer_changed_pcpu(void *arg __unused)
{
    globaldata_t gd = mycpu;
    systimer_t info;

    crit_enter();
again:
    TAILQ_FOREACH(info, &gd->gd_systimerq, node) {
	if (info->which == sys_cputimer)
		continue;
	TAILQ_REMOVE(&gd->gd_systimerq, info, node);
	info->flags &= ~SYSTF_ONQUEUE;
	if (info->periodic) {
		_systimer_init_periodic(info, info->func, info->data,
					info->freq, info->flags);
	} else {
		info->time = sys_cputimer->count() +
			     sys_cputimer->fromus(info->us);
		systimer_add(info);
	}
	goto again;
    }
    cputimer_intr_reload(1);
    crit_exit();
}

void
systimer_changed(void)
{
    globaldata_t gd = mycpu;
    int i;

    systimer_changed_pcpu(NULL);
    for (i = 0; i < ncpus; ++i) {
	if (i != gd->gd_cpuid) {
		lwkt_send_ipiq(globaldata_find(i),
			       systimer_changed_pcpu, NULL);
	}
    }
}
