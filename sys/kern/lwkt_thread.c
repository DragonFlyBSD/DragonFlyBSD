/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_switch.c,v 1.3.2.1 2000/05/16 06:58:12 dillon Exp $
 * $DragonFly: src/sys/kern/lwkt_thread.c,v 1.1 2003/06/20 02:09:56 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>

/*
 * LWKTs operate on a per-cpu basis
 *
 * YYY implement strict priorities & round-robin at the same priority
 */
void
lwkt_gdinit(struct globaldata *gd)
{
    TAILQ_INIT(&gd->gd_tdrunq);
}

/*
 * Switch to the next runnable lwkt.  If no LWKTs are runnable then 
 * switch to the idlethread.
 */
void
lwkt_switch(void)
{
    thread_t ntd;

    if ((ntd = TAILQ_FIRST(&mycpu->gd_tdrunq)) != NULL) {
	TAILQ_REMOVE(&mycpu->gd_tdrunq, ntd, td_threadq);
	TAILQ_INSERT_TAIL(&mycpu->gd_tdrunq, ntd, td_threadq);
	if (curthread != ntd)
	    curthread->td_switch(ntd);
    } else {
	if (curthread != &mycpu->gd_idlethread)
	    curthread->td_switch(&mycpu->gd_idlethread);
    }
}

#if 0
/*
 * Switch to the next runnable lwkt preemptively ?
 */
void
lwkt_preempt(void)
{
}
#endif

/*
 * Schedule an LWKT.  You can legally schedule yourself.
 */
void
lwkt_schedule(thread_t td)
{
    if ((td->td_flags & TDF_RUNQ) == 0) {
#if 0
	if (td->td_flags & TDF_WAITQ) {
	    TAILQ_REMOVE(td->td_waitq, td, td_threadq);
	    td->td_flags &= ~TDF_WAITQ;
	}
#endif
	td->td_flags |= TDF_RUNQ;
	TAILQ_INSERT_TAIL(&mycpu->gd_tdrunq, td, td_threadq);
    }
}

/*
 * Deschedule an LWKT.  You can legally deschedule yourself, but if you
 * are preempted the thread will automatically be rescheduled.  Preemption
 * must be disabled (e.g. splhi()) to avoid unexpected rescheduling of
 * the thread.
 */
void
lwkt_deschedule(thread_t td)
{
    if (td->td_flags & TDF_RUNQ) {
	TAILQ_REMOVE(&mycpu->gd_tdrunq, td, td_threadq);
	td->td_flags &= ~TDF_RUNQ;
    }
}

