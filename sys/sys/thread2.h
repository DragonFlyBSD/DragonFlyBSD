/*
 * SYS/THREAD2.H
 *
 *	Implements inline procedure support for the LWKT subsystem. 
 *
 *	Generally speaking these routines only operate on threads associated
 *	with the current cpu.  For example, a higher priority thread pending
 *	on a different cpu will not be immediately scheduled by a yield() on
 *	this cpu.
 *
 * $DragonFly: src/sys/sys/thread2.h,v 1.2 2003/06/27 20:27:19 dillon Exp $
 */

#ifndef _SYS_THREAD2_H_
#define _SYS_THREAD2_H_

/*
 * Critical sections prevent preemption by raising a thread's priority
 * above the highest possible interrupting priority.  Additionally, the
 * current cpu will not be able to schedule a new thread but will instead
 * place it on a pending list (with interrupts physically disabled) and
 * set mycpu->gd_reqpri to indicate that work needs to be done, which
 * lwkt_yield_quick() takes care of.
 *
 * Synchronous switching and blocking is allowed while in a critical section.
 */
static __inline void
crit_enter(void)
{
    curthread->td_pri += TDPRI_CRIT;
}

static __inline void
crit_exit_noyield(void)
{
    thread_t td = curthread;

    td->td_pri -= TDPRI_CRIT;
    KASSERT(td->td_pri >= 0, ("crit_exit nesting error"));
}

static __inline void
crit_exit(void)
{
    thread_t td = curthread;

    td->td_pri -= TDPRI_CRIT;
    KASSERT(td->td_pri >= 0, ("crit_exit nesting error"));
    if (td->td_pri < mycpu->gd_reqpri)
	lwkt_yield_quick();
}

static __inline int
lwkt_raisepri(int pri)
{
    int opri = curthread->td_pri;
    if (opri < pri)
	curthread->td_pri = pri;
    return(opri);
}

static __inline int
lwkt_lowerpri(int pri)
{
    thread_t td = curthread;
    int opri = td->td_pri;
    if (opri > pri) {
	td->td_pri = pri;
	if (pri < mycpu->gd_reqpri)
	    lwkt_yield_quick();
    }
    return(opri);
}

static __inline void
lwkt_setpri(int pri)
{
    curthread->td_pri = pri;
    if (pri < mycpu->gd_reqpri)
	lwkt_yield_quick();
}

static __inline int
lwkt_havetoken(lwkt_token_t tok)
{
    return (tok->t_cpu == mycpu->gd_cpuid);
}

#endif

