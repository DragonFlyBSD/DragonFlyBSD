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
 * 
 * $DragonFly: src/sys/kern/kern_systimer.c,v 1.5 2004/11/20 20:25:09 dillon Exp $
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
 * Notes on machine-dependant code (in arch/arch/systimer.c)
 *
 * cputimer_intr_reload()	Reload the one-shot (per-cpu basis)
 *
 * cputimer_count()		Get the current absolute sysclock_t value.
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
 * one-shot timer clock interrupt (e.g. clkintr()).  Systimer functions are
 * responsible for calling hardclock, statclock, and other finely-timed
 * routines.
 */
void
systimer_intr(sysclock_t *timep, struct intrframe *frame)
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
	if ((int)(info->time - time) > 0) {
	    cputimer_intr_reload(info->time - time);
	    break;
	}

	/*
	 * Dequeue and execute
	 */
	info->flags &= ~SYSTF_ONQUEUE;
	TAILQ_REMOVE(info->queue, info, node);
	crit_exit();
	info->func(info, frame);
	crit_enter();

	/*
	 * Reinstall if periodic.  If this is a non-queued periodic
	 * interrupt do not allow multiple events to build up (used
	 * for things like the callout timer to prevent premature timeouts
	 * due to long interrupt disablements, BIOS 8254 glitching, and so
	 * forth).  However, we still want to keep things synchronized between
	 * cpus for efficient handling of the timer interrupt so jump in
	 * multiples of the periodic rate.
	 */
	if (info->periodic) {
	    info->time += info->periodic;
	    if ((info->flags & SYSTF_NONQUEUED) &&
		(int)(info->time - time) <= 0
	    ) {
		info->time += ((time - info->time + info->periodic - 1) / 
				info->periodic) * info->periodic;
	    }
	    systimer_add(info);
	}
    }
    if (info)
	gd->gd_nextclock = info->time;
    else
	gd->gd_nextclock = 0;
    --gd->gd_syst_nest;
    crit_exit();
}

void
systimer_add(systimer_t info)
{
    struct globaldata *gd = mycpu;

    KKASSERT((info->flags & (SYSTF_ONQUEUE|SYSTF_IPIRUNNING)) == 0);
    crit_enter();
    if (info->gd == gd) {
	systimer_t scan1;
	systimer_t scan2;
	scan1 = TAILQ_FIRST(&gd->gd_systimerq);
	if (scan1 == NULL || (int)(scan1->time - info->time) > 0) {
	    gd->gd_nextclock = info->time;
	    cputimer_intr_reload(info->time - cputimer_count());
	    TAILQ_INSERT_HEAD(&gd->gd_systimerq, info, node);
	} else {
	    scan2 = TAILQ_LAST(&gd->gd_systimerq, systimerq);
	    for (;;) {
		if (scan1 == NULL) {
		    TAILQ_INSERT_TAIL(&gd->gd_systimerq, info, node);
		    break;
		}
		if ((int)(scan1->time - info->time) > 0) {
		    TAILQ_INSERT_BEFORE(scan1, info, node);
		    break;
		}
		if ((int)(scan2->time - info->time) <= 0) {
		    TAILQ_INSERT_AFTER(&gd->gd_systimerq, scan2, info, node);
		    break;
		}
		scan1 = TAILQ_NEXT(scan1, node);
		scan2 = TAILQ_PREV(scan2, systimerq, node);
	    }
	}
	info->flags = (info->flags | SYSTF_ONQUEUE) & ~SYSTF_IPIRUNNING;
	info->queue = &gd->gd_systimerq;
    } else {
	info->flags |= SYSTF_IPIRUNNING;
	lwkt_send_ipiq(info->gd, (ipifunc_t)systimer_add, info);
    }
    crit_exit();
}

/*
 * systimer_del()
 *
 *	Delete a system timer.  Only the owning cpu can delete a timer.
 */
void
systimer_del(systimer_t info)
{
    KKASSERT(info->gd == mycpu && (info->flags & SYSTF_IPIRUNNING) == 0);
    crit_enter();
    if (info->flags & SYSTF_ONQUEUE) {
	TAILQ_REMOVE(info->queue, info, node);
	info->flags &= ~SYSTF_ONQUEUE;
    }
    crit_exit();
}

/*
 * systimer_init_periodic()
 *
 *	Initialize a periodic timer at the specified frequency and add
 *	it to the system.  The frequency is uncompensated and approximate.
 *
 *	Try to synchronize multi registrations of the same or similar
 *	frequencies so the hardware interrupt is able to dispatch several
 *	at together by adjusting the phase of the initial interrupt.  This
 *	helps SMP.  Note that we are not attempting to synchronize to 
 *	the realtime clock.
 */
void
systimer_init_periodic(systimer_t info, void *func, void *data, int hz)
{
    sysclock_t base_count;

    bzero(info, sizeof(struct systimer));
    info->periodic = cputimer_fromhz(hz);
    base_count = cputimer_count();
    base_count = base_count - (base_count % info->periodic);
    info->time = base_count + info->periodic;
    info->func = func;
    info->data = data;
    info->gd = mycpu;
    systimer_add(info);
}

void
systimer_init_periodic_nq(systimer_t info, void *func, void *data, int hz)
{
    sysclock_t base_count;

    bzero(info, sizeof(struct systimer));
    info->periodic = cputimer_fromhz(hz);
    base_count = cputimer_count();
    base_count = base_count - (base_count % info->periodic);
    info->time = base_count + info->periodic;
    info->func = func;
    info->data = data;
    info->gd = mycpu;
    info->flags |= SYSTF_NONQUEUED;
    systimer_add(info);
}

/*
 * systimer_init_oneshot()
 *
 *	Initialize a periodic timer at the specified frequency and add
 *	it to the system.  The frequency is uncompensated and approximate.
 */
void
systimer_init_oneshot(systimer_t info, void *func, void *data, int us)
{
    bzero(info, sizeof(struct systimer));
    info->time = cputimer_count() + cputimer_fromus(us);
    info->func = func;
    info->data = data;
    info->gd = mycpu;
    systimer_add(info);
}

