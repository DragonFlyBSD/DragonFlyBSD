/*
 * SYS/THREAD2.H
 *
 * Implements inline procedure support for the LWKT subsystem. 
 *
 * Generally speaking these routines only operate on threads associated
 * with the current cpu.  For example, a higher priority thread pending
 * on a different cpu will not be immediately scheduled by a yield() on
 * this cpu.
 *
 * $DragonFly: src/sys/sys/thread2.h,v 1.28 2006/12/23 00:27:03 swildner Exp $
 */

#ifndef _SYS_THREAD2_H_
#define _SYS_THREAD2_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

/*
 * Userland will have its own globaldata which it includes prior to this.
 */
#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _MACHINE_CPUFUNC_H_
#include <machine/cpufunc.h>
#endif

/*
 * Critical section debugging
 */
#ifdef DEBUG_CRIT_SECTIONS
#define __DEBUG_CRIT_ARG__		const char *id
#define __DEBUG_CRIT_ADD_ARG__		, const char *id
#define __DEBUG_CRIT_PASS_ARG__		, id
#define __DEBUG_CRIT_ENTER(td)		_debug_crit_enter((td), id)
#define __DEBUG_CRIT_EXIT(td)		_debug_crit_exit((td), id)
#define crit_enter()			_crit_enter(__FUNCTION__)
#define crit_enter_id(id)		_crit_enter(id)
#define crit_enter_quick(curtd)		_crit_enter_quick((curtd), __FUNCTION__)
#define crit_enter_gd(curgd)		_crit_enter_gd(curgd, __FUNCTION__)
#define crit_exit()			_crit_exit(__FUNCTION__)
#define crit_exit_id(id)		_crit_exit(id)
#define crit_exit_quick(curtd)		_crit_exit_quick((curtd), __FUNCTION__)
#define crit_exit_noyield(curtd)	_crit_exit_noyield((curtd),__FUNCTION__)
#define crit_exit_gd(curgd)		_crit_exit_gd((curgd), __FUNCTION__)
#else
#define __DEBUG_CRIT_ARG__		void
#define __DEBUG_CRIT_ADD_ARG__
#define __DEBUG_CRIT_PASS_ARG__
#define __DEBUG_CRIT_ENTER(td)
#define __DEBUG_CRIT_EXIT(td)
#define crit_enter()			_crit_enter()
#define crit_enter_id(id)		_crit_enter()
#define crit_enter_quick(curtd)		_crit_enter_quick(curtd)
#define crit_enter_gd(curgd)		_crit_enter_gd(curgd)
#define crit_exit()			_crit_exit()
#define crit_exit_id(id)		_crit_exit()
#define crit_exit_quick(curtd)		_crit_exit_quick(curtd)
#define crit_exit_noyield(curtd)	_crit_exit_noyield(curtd)
#define crit_exit_gd(curgd)		_crit_exit_gd(curgd)
#endif

/*
 * Track crit_enter()/crit_exit() pairs and warn on mismatches.
 */
#ifdef DEBUG_CRIT_SECTIONS

static __inline void
_debug_crit_enter(thread_t td, const char *id)
{
    int wi = td->td_crit_debug_index;

    td->td_crit_debug_array[wi & CRIT_DEBUG_ARRAY_MASK] = id;
    ++td->td_crit_debug_index;
}

static __inline void
_debug_crit_exit(thread_t td, const char *id)
{
    const char *gid;
    int wi;

    wi = td->td_crit_debug_index - 1;
    if ((gid = td->td_crit_debug_array[wi & CRIT_DEBUG_ARRAY_MASK]) != id) {
	if (td->td_in_crit_report == 0) {
	    td->td_in_crit_report = 1;
	    kprintf("crit_exit(%s) expected id %s\n", id, gid);
	    td->td_in_crit_report = 0;
	}
    }
    --td->td_crit_debug_index;
}

#endif

/*
 * Critical sections prevent preemption by raising a thread's priority
 * above the highest possible interrupting priority.  Additionally, the
 * current cpu will not be able to schedule a new thread but will instead
 * place it on a pending list (with interrupts physically disabled) and
 * set mycpu->gd_reqflags to indicate that work needs to be done, which
 * splz_check() takes care of.
 *
 * Some of these routines take a struct thread pointer as an argument.  This
 * pointer MUST be curthread and is only passed as an optimization.
 *
 * Synchronous switching and blocking is allowed while in a critical section.
 */

static __inline void
_crit_enter(__DEBUG_CRIT_ARG__)
{
    struct thread *td = curthread;

#ifdef INVARIANTS
    if (td->td_pri < 0)
	crit_panic();
#endif
    td->td_pri += TDPRI_CRIT;
    __DEBUG_CRIT_ENTER(td);
    cpu_ccfence();
}

static __inline void
_crit_enter_quick(struct thread *curtd __DEBUG_CRIT_ADD_ARG__)
{
    curtd->td_pri += TDPRI_CRIT;
    __DEBUG_CRIT_ENTER(curtd);
    cpu_ccfence();
}

static __inline void
_crit_enter_gd(globaldata_t mygd __DEBUG_CRIT_ADD_ARG__)
{
    _crit_enter_quick(mygd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
}

static __inline void
_crit_exit_noyield(struct thread *curtd __DEBUG_CRIT_ADD_ARG__)
{
    __DEBUG_CRIT_EXIT(curtd);
    curtd->td_pri -= TDPRI_CRIT;
#ifdef INVARIANTS
    if (curtd->td_pri < 0)
	crit_panic();
#endif
    cpu_ccfence();	/* prevent compiler reordering */
}

static __inline void
_crit_exit(__DEBUG_CRIT_ARG__)
{
    thread_t td = curthread;

    __DEBUG_CRIT_EXIT(td);
    td->td_pri -= TDPRI_CRIT;
#ifdef INVARIANTS
    if (td->td_pri < 0)
	crit_panic();
#endif
    cpu_ccfence();	/* prevent compiler reordering */
    if (td->td_gd->gd_reqflags && td->td_pri < TDPRI_CRIT)
	splz_check();
}

static __inline void
_crit_exit_quick(struct thread *curtd __DEBUG_CRIT_ADD_ARG__)
{
    globaldata_t gd = curtd->td_gd;

    __DEBUG_CRIT_EXIT(curtd);
    curtd->td_pri -= TDPRI_CRIT;
    cpu_ccfence();	/* prevent compiler reordering */
    if (gd->gd_reqflags && curtd->td_pri < TDPRI_CRIT)
	splz_check();
}

static __inline void
_crit_exit_gd(globaldata_t mygd __DEBUG_CRIT_ADD_ARG__)
{
    _crit_exit_quick(mygd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
}

static __inline int
crit_test(thread_t td)
{
    return(td->td_pri >= TDPRI_CRIT);
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

static __inline int
lwkt_getpri(thread_t td)
{
    return(td->td_pri & TDPRI_MASK);
}

static __inline int
lwkt_getpri_self(void)
{
    return(lwkt_getpri(curthread));
}

/*
 * Reduce our priority in preparation for a return to userland.  If
 * our passive release function was still in place, our priority was
 * never raised and does not need to be reduced.
 *
 * See also lwkt_passive_release() and platform/blah/trap.c
 */
static __inline void
lwkt_passive_recover(thread_t td)
{
    if (td->td_release == NULL)
	lwkt_setpri_self(TDPRI_USER_NORM);
    td->td_release = NULL;
}

#ifdef SMP

/*
 * IPIQ messaging wrappers.  IPIQ remote functions are passed three arguments:
 * a void * pointer, an integer, and a pointer to the trap frame (or NULL if
 * the trap frame is not known).  However, we wish to provide opaque 
 * interfaces for simpler callbacks... the basic IPI messaging function as
 * used by the kernel takes a single argument.
 */
static __inline int
lwkt_send_ipiq(globaldata_t target, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3(target, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2(globaldata_t target, ipifunc2_t func, void *arg1, int arg2)
{
    return(lwkt_send_ipiq3(target, (ipifunc3_t)func, arg1, arg2));
}

static __inline int
lwkt_send_ipiq_mask(u_int32_t mask, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3_mask(mask, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2_mask(u_int32_t mask, ipifunc2_t func, void *arg1, int arg2)
{
    return(lwkt_send_ipiq3_mask(mask, (ipifunc3_t)func, arg1, arg2));
}

static __inline int
lwkt_send_ipiq_nowait(globaldata_t target, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3_nowait(target, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2_nowait(globaldata_t target, ipifunc2_t func, 
		       void *arg1, int arg2)
{
    return(lwkt_send_ipiq3_nowait(target, (ipifunc3_t)func, arg1, arg2));
}

static __inline int
lwkt_send_ipiq_passive(globaldata_t target, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3_passive(target, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2_passive(globaldata_t target, ipifunc2_t func, 
		       void *arg1, int arg2)
{
    return(lwkt_send_ipiq3_passive(target, (ipifunc3_t)func, arg1, arg2));
}

static __inline int
lwkt_send_ipiq_bycpu(int dcpu, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3_bycpu(dcpu, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2_bycpu(int dcpu, ipifunc2_t func, void *arg1, int arg2)
{
    return(lwkt_send_ipiq3_bycpu(dcpu, (ipifunc3_t)func, arg1, arg2));
}

#endif	/* SMP */
#endif	/* _KERNEL */
#endif	/* _SYS_THREAD2_H_ */

