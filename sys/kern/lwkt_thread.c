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
 *	Each cpu in a system has its own self-contained light weight kernel
 *	thread scheduler, which means that generally speaking we only need
 *	to use a critical section to prevent hicups.
 *
 * $DragonFly: src/sys/kern/lwkt_thread.c,v 1.4 2003/06/22 04:30:42 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

static int untimely_switch = 0;
SYSCTL_INT(_debug, OID_AUTO, untimely_switch, CTLFLAG_RW, &untimely_switch, 0, "");


static __inline
void
_lwkt_dequeue(thread_t td)
{
    if (td->td_flags & TDF_RUNQ) {
	td->td_flags &= ~TDF_RUNQ;
	TAILQ_REMOVE(&mycpu->gd_tdrunq, td, td_threadq);
    }
}

static __inline
void
_lwkt_enqueue(thread_t td)
{
    if ((td->td_flags & TDF_RUNQ) == 0) {
	td->td_flags |= TDF_RUNQ;
	TAILQ_INSERT_TAIL(&mycpu->gd_tdrunq, td, td_threadq);
    }
}

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
 * Initialize a thread wait structure prior to first use.
 *
 * NOTE!  called from low level boot code, we cannot do anything fancy!
 */
void
lwkt_init_wait(lwkt_wait_t w)
{
    TAILQ_INIT(&w->wa_waitq);
}

/*
 * Create a new thread.  The thread must be associated with a process context
 * or LWKT start address before it can be scheduled.
 */
thread_t
lwkt_alloc_thread(void)
{
	struct thread *td;
	void *stack;

	crit_enter();
	if (mycpu->gd_tdfreecount > 0) {
		--mycpu->gd_tdfreecount;
		td = TAILQ_FIRST(&mycpu->gd_tdfreeq);
		KASSERT(td != NULL, ("unexpected null cache td"));
		TAILQ_REMOVE(&mycpu->gd_tdfreeq, td, td_threadq);
		crit_exit();
		stack = td->td_kstack;
	} else {
		crit_exit();
		td = zalloc(thread_zone);
		stack = (void *)kmem_alloc(kernel_map, UPAGES * PAGE_SIZE);
	}
	lwkt_init_thread(td, stack);
	return(td);
}

/*
 * Initialize a preexisting thread structure.  This function is used by
 * lwkt_alloc_thread() and also used to initialize the per-cpu idlethread.
 *
 * NOTE!  called from low level boot code, we cannot do anything fancy!
 */
void
lwkt_init_thread(thread_t td, void *stack)
{
	bzero(td, sizeof(struct thread));
	lwkt_rwlock_init(&td->td_rwlock);
	td->td_kstack = stack;
	pmap_init_thread(td);
}

/*
 * Switch to the next runnable lwkt.  If no LWKTs are runnable then 
 * switch to the idlethread.  Switching must occur within a critical
 * section to avoid races with the scheduling queue.
 *
 * We always have full control over our cpu's run queue.  Other cpus
 * that wish to manipulate our queue must use the cpu_*msg() calls to
 * talk to our cpu, so a critical section is all that is needed and
 * the result is very, very fast thread switching.
 *
 * We always 'own' our own thread and the threads on our run queue,l
 * due to TDF_RUNNING or TDF_RUNQ being set.  We can safely clear
 * TDF_RUNNING while in a critical section.
 *
 * The td_switch() function must be called while in the critical section.
 * This function saves as much state as is appropriate for the type of
 * thread.
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_switch(void)
{
    thread_t td = curthread;
    thread_t ntd;

    crit_enter();
    if ((ntd = TAILQ_FIRST(&mycpu->gd_tdrunq)) != NULL) {
	TAILQ_REMOVE(&mycpu->gd_tdrunq, ntd, td_threadq);
	TAILQ_INSERT_TAIL(&mycpu->gd_tdrunq, ntd, td_threadq);
    } else {
	ntd = &mycpu->gd_idlethread;
    }
    if (td != ntd) {
	td->td_flags &= ~TDF_RUNNING;
	ntd->td_flags |= TDF_RUNNING;
	td->td_switch(ntd);
    }
    crit_exit();
}

/*
 * Yield our thread while higher priority threads are pending.  This is
 * typically called when we leave a critical section but it can be safely
 * called while we are in a critical section.
 *
 * This function will not generally yield to equal priority threads but it
 * can occur as a side effect.  Note that lwkt_switch() is called from
 * inside the critical section to pervent its own crit_exit() from reentering
 * lwkt_yield_quick().
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_yield_quick(void)
{
    thread_t td = curthread;
    while ((td->td_pri & TDPRI_MASK) < mycpu->gd_reqpri) {
#if 0
	cpu_schedule_reqs();	/* resets gd_reqpri */
#endif
	splz();
    }

    /*
     * YYY enabling will cause wakeup() to task-switch, which really
     * confused the old 4.x code.  This is a good way to simulate
     * preemption and MP without actually doing preemption or MP, because a
     * lot of code assumes that wakeup() does not block.
     */
    if (untimely_switch && intr_nesting_level == 0) {
	crit_enter();
	/*
	 * YYY temporary hacks until we disassociate the userland scheduler
	 * from the LWKT scheduler.
	 */
	if (td->td_flags & TDF_RUNQ) {
	    lwkt_switch();		/* will not reenter yield function */
	} else {
	    lwkt_schedule_self();	/* make sure we are scheduled */
	    lwkt_switch();		/* will not reenter yield function */
	    lwkt_deschedule_self();	/* make sure we are descheduled */
	}
	crit_exit_noyield();
    }
}

/*
 * This implements a normal yield which, unlike _quick, will yield to equal
 * priority threads as well.  Note that gd_reqpri tests will be handled by
 * the crit_exit() call in lwkt_switch().
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_yield(void)
{
    lwkt_schedule_self();
    lwkt_switch();
}

/*
 * Schedule a thread to run.  As the current thread we can always safely
 * schedule ourselves, and a shortcut procedure is provided for that
 * function.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_schedule_self(void)
{
    thread_t td = curthread;

    crit_enter();
    KASSERT(td->td_wait == NULL, ("lwkt_schedule_self(): td_wait not NULL!"));
    KASSERT(td->td_flags & TDF_RUNNING, ("lwkt_schedule_self(): TDF_RUNNING not set!"));
    _lwkt_enqueue(td);
    crit_exit();
}

/*
 * Generic schedule.  Possibly schedule threads belonging to other cpus and
 * deal with threads that might be blocked on a wait queue.
 *
 * This function will queue requests asynchronously when possible, but may
 * block if no request structures are available.  Upon return the caller
 * should note that the scheduling request may not yet have been processed
 * by the target cpu.
 *
 * YYY this is one of the best places to implement any load balancing code.
 * Load balancing can be accomplished by requesting other sorts of actions
 * for the thread in question.
 */
void
lwkt_schedule(thread_t td)
{
    crit_enter();
    if (td == curthread) {
	_lwkt_enqueue(td);
    } else {
	lwkt_wait_t w;

	/*
	 * If the thread is on a wait list we have to send our scheduling
	 * request to the owner of the wait structure.  Otherwise we send
	 * the scheduling request to the cpu owning the thread.  Races
	 * are ok, the target will forward the message as necessary (the
	 * message may chase the thread around before it finally gets
	 * acted upon).
	 *
	 * (remember, wait structures use stable storage)
	 */
	if ((w = td->td_wait) != NULL) {
	    if (lwkt_havetoken(&w->wa_token)) {
		TAILQ_REMOVE(&w->wa_waitq, td, td_threadq);
		--w->wa_count;
		td->td_wait = NULL;
		if (td->td_cpu == mycpu->gd_cpu) {
		    _lwkt_enqueue(td);
		} else {
		    panic("lwkt_schedule: cpu mismatch1");
#if 0
		    lwkt_cpu_msg_union_t msg = lwkt_getcpumsg();
		    initScheduleReqMsg_Wait(&msg.mu_SchedReq, td, w);
		    cpu_sendnormsg(&msg.mu_Msg);
#endif
		}
	    } else {
		panic("lwkt_schedule: cpu mismatch2");
#if 0
		lwkt_cpu_msg_union_t msg = lwkt_getcpumsg();
		initScheduleReqMsg_Wait(&msg.mu_SchedReq, td, w);
		cpu_sendnormsg(&msg.mu_Msg);
#endif
	    }
	} else {
	    /*
	     * If the wait structure is NULL and we own the thread, there
	     * is no race (since we are in a critical section).  If we
	     * do not own the thread there might be a race but the
	     * target cpu will deal with it.
	     */
	    if (td->td_cpu == mycpu->gd_cpu) {
		_lwkt_enqueue(td);
	    } else {
		panic("lwkt_schedule: cpu mismatch3");
#if 0
		lwkt_cpu_msg_union_t msg = lwkt_getcpumsg();
		initScheduleReqMsg_Thread(&msg.mu_SchedReq, td);
		cpu_sendnormsg(&msg.mu_Msg);
#endif
	    }
	}
    }
    crit_exit();
}

/*
 * Deschedule a thread.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_deschedule_self(void)
{
    thread_t td = curthread;

    crit_enter();
    KASSERT(td->td_wait == NULL, ("lwkt_schedule_self(): td_wait not NULL!"));
    KASSERT(td->td_flags & TDF_RUNNING, ("lwkt_schedule_self(): TDF_RUNNING not set!"));
    _lwkt_dequeue(td);
    crit_exit();
}

/*
 * Generic deschedule.  Descheduling threads other then your own should be
 * done only in carefully controlled circumstances.  Descheduling is 
 * asynchronous.  
 *
 * This function may block if the cpu has run out of messages.
 */
void
lwkt_deschedule(thread_t td)
{
    crit_enter();
    if (td == curthread) {
	_lwkt_dequeue(td);
    } else {
	if (td->td_cpu == mycpu->gd_cpu) {
	    _lwkt_dequeue(td);
	} else {
	    panic("lwkt_deschedule: cpu mismatch");
#if 0
	    lwkt_cpu_msg_union_t msg = lwkt_getcpumsg();
	    initDescheduleReqMsg_Thread(&msg.mu_DeschedReq, td);
	    cpu_sendnormsg(&msg.mu_Msg);
#endif
	}
    }
    crit_exit();
}

/*
 * This function deschedules the current thread and blocks on the specified
 * wait queue.  We obtain ownership of the wait queue in order to block
 * on it.  A generation number is used to interlock the wait queue in case
 * it gets signalled while we are blocked waiting on the token.
 *
 * Note: alternatively we could dequeue our thread and then message the
 * target cpu owning the wait queue.  YYY implement as sysctl.
 *
 * Note: wait queue signals normally ping-pong the cpu as an optimization.
 */
void
lwkt_block(lwkt_wait_t w, const char *wmesg, int *gen)
{
    thread_t td = curthread;

    lwkt_gettoken(&w->wa_token);
    if (w->wa_gen == *gen) {
	_lwkt_dequeue(td);
	TAILQ_INSERT_TAIL(&w->wa_waitq, td, td_threadq);
	++w->wa_count;
	td->td_wait = w;
	td->td_wmesg = wmesg;
	lwkt_switch();
    }
    /* token might be lost, doesn't matter for gen update */
    *gen = w->wa_gen;
    lwkt_reltoken(&w->wa_token);
}

/*
 * Signal a wait queue.  We gain ownership of the wait queue in order to
 * signal it.  Once a thread is removed from the wait queue we have to
 * deal with the cpu owning the thread.
 *
 * Note: alternatively we could message the target cpu owning the wait
 * queue.  YYY implement as sysctl.
 */
void
lwkt_signal(lwkt_wait_t w)
{
    thread_t td;
    int count;

    lwkt_gettoken(&w->wa_token);
    ++w->wa_gen;
    count = w->wa_count;
    while ((td = TAILQ_FIRST(&w->wa_waitq)) != NULL && count) {
	--count;
	--w->wa_count;
	TAILQ_REMOVE(&w->wa_waitq, td, td_threadq);
	td->td_wait = NULL;
	td->td_wmesg = NULL;
	if (td->td_cpu == mycpu->gd_cpu) {
	    _lwkt_enqueue(td);
	} else {
#if 0
	    lwkt_cpu_msg_union_t msg = lwkt_getcpumsg();
	    initScheduleReqMsg_Thread(&msg.mu_SchedReq, td);
	    cpu_sendnormsg(&msg.mu_Msg);
#endif
	    panic("lwkt_signal: cpu mismatch");
	}
	lwkt_regettoken(&w->wa_token);
    }
    lwkt_reltoken(&w->wa_token);
}

/*
 * Aquire ownership of a token
 *
 * Aquire ownership of a token.  The token may have spl and/or critical
 * section side effects, depending on its purpose.  These side effects
 * guarentee that you will maintain ownership of the token as long as you
 * do not block.  If you block you may lose access to the token (but you
 * must still release it even if you lose your access to it).
 *
 * Note that the spl and critical section characteristics of a token
 * may not be changed once the token has been initialized.
 */
void
lwkt_gettoken(lwkt_token_t tok)
{
    /*
     * Prevent preemption so the token can't be taken away from us once
     * we gain ownership of it.  Use a synchronous request which might
     * block.  The request will be forwarded as necessary playing catchup
     * to the token.
     */
    crit_enter();
#if 0
    while (tok->t_cpu != mycpu->gd_cpu) {
	lwkt_cpu_msg_union msg;
	initTokenReqMsg(&msg.mu_TokenReq);
	cpu_domsg(&msg);
    }
#endif
    /*
     * leave us in a critical section on return.  This will be undone
     * by lwkt_reltoken()
     */
}

/*
 * Release your ownership of a token.  Releases must occur in reverse
 * order to aquisitions, eventually so priorities can be unwound properly
 * like SPLs.  At the moment the actual implemention doesn't care.
 *
 * We can safely hand a token that we own to another cpu without notifying
 * it, but once we do we can't get it back without requesting it (unless
 * the other cpu hands it back to us before we check).
 *
 * We might have lost the token, so check that.
 */
void
lwkt_reltoken(lwkt_token_t tok)
{
    if (tok->t_cpu == mycpu->gd_cpu) {
	tok->t_cpu = tok->t_reqcpu;
    }
    crit_exit();
}

/*
 * Reaquire a token that might have been lost.  Returns 1 if we blocked
 * while reaquiring the token (meaning that you might have lost other
 * tokens you held when you made this call), return 0 if we did not block.
 */
int
lwkt_regettoken(lwkt_token_t tok)
{
#if 0
    if (tok->t_cpu != mycpu->gd_cpu) {
	while (tok->t_cpu != mycpu->gd_cpu) {
	    lwkt_cpu_msg_union msg;
	    initTokenReqMsg(&msg.mu_TokenReq);
	    cpu_domsg(&msg);
	}
	return(1);
    }
#endif
    return(0);
}

