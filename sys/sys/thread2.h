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
 * $DragonFly: src/sys/sys/thread2.h,v 1.15 2004/05/04 17:13:04 dillon Exp $
 */

#ifndef _SYS_THREAD2_H_
#define _SYS_THREAD2_H_

/*
 * Userland will have its own globaldata which it includes prior to this.
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _MACHINE_CPUFUNC_H_
#include <machine/cpufunc.h>
#endif
#endif

/*
 * Critical sections prevent preemption by raising a thread's priority
 * above the highest possible interrupting priority.  Additionally, the
 * current cpu will not be able to schedule a new thread but will instead
 * place it on a pending list (with interrupts physically disabled) and
 * set mycpu->gd_reqflags to indicate that work needs to be done, which
 * lwkt_yield_quick() takes care of.
 *
 * Some of these routines take a struct thread pointer as an argument.  This
 * pointer MUST be curthread and is only passed as an optimization.
 *
 * Synchronous switching and blocking is allowed while in a critical section.
 */

static __inline void
crit_enter(void)
{
    struct thread *td = curthread;

    td->td_pri += TDPRI_CRIT;
#ifdef INVARIANTS
    if (td->td_pri < 0)
	crit_panic();
#endif
}

static __inline void
crit_enter_quick(struct thread *curtd)
{
    curtd->td_pri += TDPRI_CRIT;
}

static __inline void
crit_enter_gd(globaldata_t mygd)
{
    crit_enter_quick(mygd->gd_curthread);
}

static __inline void
crit_exit_noyield(struct thread *curtd)
{
    curtd->td_pri -= TDPRI_CRIT;
#ifdef INVARIANTS
    if (curtd->td_pri < 0)
	crit_panic();
#endif
}

static __inline void
crit_exit(void)
{
    thread_t td = curthread;

    td->td_pri -= TDPRI_CRIT;
#ifdef INVARIANTS
    if (td->td_pri < 0)
	crit_panic();
#endif
    cpu_mb1();	/* must flush td_pri before checking gd_reqflags */
    if (td->td_gd->gd_reqflags && td->td_pri < TDPRI_CRIT)
	lwkt_yield_quick();
}

static __inline void
crit_exit_quick(struct thread *curtd)
{
    globaldata_t gd = curtd->td_gd;

    curtd->td_pri -= TDPRI_CRIT;
    cpu_mb1();	/* must flush td_pri before checking gd_reqflags */
    if (gd->gd_reqflags && curtd->td_pri < TDPRI_CRIT)
	lwkt_yield_quick();
}

static __inline void
crit_exit_gd(globaldata_t mygd)
{
    crit_exit_quick(mygd->gd_curthread);
}

static __inline int
crit_panic_save(void)
{
    thread_t td = curthread;
    int pri = td->td_pri;
    td->td_pri = td->td_pri & TDPRI_MASK;
    return(pri);
}

static __inline void
crit_panic_restore(int cpri)
{
    curthread->td_pri = cpri;
}

/*
 * Initialize a tokref_t.  We only need to initialize the token pointer
 * and the magic number.  We do not have to initialize tr_next, tr_gdreqnext,
 * or tr_reqgd.
 */
static __inline void
lwkt_tokref_init(lwkt_tokref_t ref, lwkt_token_t tok)
{
    ref->tr_magic = LWKT_TOKREF_MAGIC1;
    ref->tr_tok = tok;
}

/*
 * Return whether any threads are runnable, whether they meet mp_lock
 * requirements or not.
 */
static __inline int
lwkt_runnable(void)
{
    return (mycpu->gd_runqmask != 0);
}

#endif

