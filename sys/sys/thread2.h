/*
 * SYS/THREAD2.H
 *
 * Implements inline procedure support for the LWKT subsystem. 
 *
 * Generally speaking these routines only operate on threads associated
 * with the current cpu.  For example, a higher priority thread pending
 * on a different cpu will not be immediately scheduled by a yield() on
 * this cpu.
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
#include <machine/cpufunc.h>

/*
 * Is a token held either by the specified thread or held shared?
 *
 * We can't inexpensively validate the thread for a shared token
 * without iterating td->td_toks, so this isn't a perfect test.
 */
static __inline int
_lwkt_token_held_any(lwkt_token_t tok, thread_t td)
{
	long count = tok->t_count;

	cpu_ccfence();
	if (tok->t_ref >= &td->td_toks_base && tok->t_ref < td->td_toks_stop)
		return TRUE;
	if ((count & TOK_EXCLUSIVE) == 0 &&
	    (count & ~(TOK_EXCLUSIVE|TOK_EXCLREQ))) {
		return TRUE;
	}
	return FALSE;
}

/*
 * Is a token held by the specified thread?
 */
static __inline int
_lwkt_token_held_excl(lwkt_token_t tok, thread_t td)
{
	return ((tok->t_ref >= &td->td_toks_base &&
		 tok->t_ref < td->td_toks_stop));
}

/*
 * Critical section debugging
 */
#ifdef DEBUG_CRIT_SECTIONS
#define __DEBUG_CRIT_ARG__		const char *id
#define __DEBUG_CRIT_ADD_ARG__		, const char *id
#define __DEBUG_CRIT_PASS_ARG__		, id
#define __DEBUG_CRIT_ENTER(td)		_debug_crit_enter((td), id)
#define __DEBUG_CRIT_EXIT(td)		_debug_crit_exit((td), id)
#define crit_enter()			_crit_enter(mycpu, __func__)
#define crit_enter_id(id)		_crit_enter(mycpu, id)
#define crit_enter_gd(curgd)		_crit_enter((curgd), __func__)
#define crit_enter_quick(curtd)		_crit_enter_quick((curtd), __func__)
#define crit_enter_hard()		_crit_enter_hard(mycpu, __func__)
#define crit_enter_hard_gd(curgd)	_crit_enter_hard((curgd), __func__)
#define crit_exit()			_crit_exit(mycpu, __func__)
#define crit_exit_id(id)		_crit_exit(mycpu, id)
#define crit_exit_gd(curgd)		_crit_exit((curgd), __func__)
#define crit_exit_quick(curtd)		_crit_exit_quick((curtd), __func__)
#define crit_exit_hard()		_crit_exit_hard(mycpu, __func__)
#define crit_exit_hard_gd(curgd)	_crit_exit_hard((curgd), __func__)
#define crit_exit_noyield(curtd)	_crit_exit_noyield((curtd),__func__)
#else
#define __DEBUG_CRIT_ARG__		void
#define __DEBUG_CRIT_ADD_ARG__
#define __DEBUG_CRIT_PASS_ARG__
#define __DEBUG_CRIT_ENTER(td)
#define __DEBUG_CRIT_EXIT(td)
#define crit_enter()			_crit_enter(mycpu)
#define crit_enter_id(id)		_crit_enter(mycpu)
#define crit_enter_gd(curgd)		_crit_enter((curgd))
#define crit_enter_quick(curtd)		_crit_enter_quick((curtd))
#define crit_enter_hard()		_crit_enter_hard(mycpu)
#define crit_enter_hard_gd(curgd)	_crit_enter_hard((curgd))
#define crit_exit()			crit_exit_wrapper()
#define crit_exit_id(id)		_crit_exit(mycpu)
#define crit_exit_gd(curgd)		_crit_exit((curgd))
#define crit_exit_quick(curtd)		_crit_exit_quick((curtd))
#define crit_exit_hard()		_crit_exit_hard(mycpu)
#define crit_exit_hard_gd(curgd)	_crit_exit_hard((curgd))
#define crit_exit_noyield(curtd)	_crit_exit_noyield((curtd))
#endif

extern void crit_exit_wrapper(__DEBUG_CRIT_ARG__);

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
 * Critical sections prevent preemption, but allowing explicit blocking
 * and thread switching.  Any interrupt occuring while in a critical
 * section is made pending and returns immediately.  Interrupts are not
 * physically disabled.
 *
 * Hard critical sections prevent preemption and disallow any blocking
 * or thread switching, and in addition will assert on any blockable
 * operation (acquire token not already held, lockmgr, mutex ops, or
 * splz).  Spinlocks can still be used in hard sections.
 *
 * All critical section routines only operate on the current thread.
 * Passed gd or td arguments are simply optimizations when mycpu or
 * curthread is already available to the caller.
 */

/*
 * crit_enter
 */
static __inline void
_crit_enter_quick(thread_t td __DEBUG_CRIT_ADD_ARG__)
{
    ++td->td_critcount;
    __DEBUG_CRIT_ENTER(td);
    cpu_ccfence();
}

static __inline void
_crit_enter(globaldata_t gd __DEBUG_CRIT_ADD_ARG__)
{
    _crit_enter_quick(gd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
}

static __inline void
_crit_enter_hard(globaldata_t gd __DEBUG_CRIT_ADD_ARG__)
{
    _crit_enter_quick(gd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
    ++gd->gd_intr_nesting_level;
}


/*
 * crit_exit*()
 *
 * NOTE: Conditionalizing just gd_reqflags, a case which is virtually
 *	 never true regardless of crit_count, should result in 100%
 *	 optimal code execution.  We don't check crit_count because
 *	 it just bloats the inline and does not improve performance.
 *
 * NOTE: This can produce a considerable amount of code despite the
 *	 relatively few lines of code so the non-debug case typically
 *	 just wraps it in a real function, crit_exit_wrapper().
 */
static __inline void
_crit_exit_noyield(thread_t td __DEBUG_CRIT_ADD_ARG__)
{
    __DEBUG_CRIT_EXIT(td);
    --td->td_critcount;
#ifdef INVARIANTS
    if (__predict_false(td->td_critcount < 0))
	crit_panic();
#endif
    cpu_ccfence();	/* prevent compiler reordering */
}

static __inline void
_crit_exit_quick(thread_t td __DEBUG_CRIT_ADD_ARG__)
{
    _crit_exit_noyield(td __DEBUG_CRIT_PASS_ARG__);
    if (__predict_false(td->td_gd->gd_reqflags & RQF_IDLECHECK_MASK))
	lwkt_maybe_splz(td);
}

static __inline void
_crit_exit(globaldata_t gd __DEBUG_CRIT_ADD_ARG__)
{
    _crit_exit_quick(gd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
}

static __inline void
_crit_exit_hard(globaldata_t gd __DEBUG_CRIT_ADD_ARG__)
{
    --gd->gd_intr_nesting_level;
    _crit_exit_quick(gd->gd_curthread __DEBUG_CRIT_PASS_ARG__);
}

static __inline int
crit_test(thread_t td)
{
    return(td->td_critcount);
}

/*
 * Return whether any threads are runnable.
 */
static __inline int
lwkt_runnable(void)
{
    return (TAILQ_FIRST(&mycpu->gd_tdrunq) != NULL);
}

static __inline int
lwkt_getpri(thread_t td)
{
    return(td->td_pri);
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
#ifndef NO_LWKT_SPLIT_USERPRI
    if (td->td_release == NULL)
	lwkt_setpri_self(TDPRI_USER_NORM);
    td->td_release = NULL;
#endif
}

/*
 * cpusync support
 */
static __inline void
lwkt_cpusync_init(lwkt_cpusync_t cs, cpumask_t mask,
		  cpusync_func_t func, void *data)
{
	cs->cs_mask = mask;
	/* cs->cs_mack = 0; handled by _interlock */
	cs->cs_func = func;
	cs->cs_data = data;
}

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
lwkt_send_ipiq_mask(cpumask_t mask, ipifunc1_t func, void *arg)
{
    return(lwkt_send_ipiq3_mask(mask, (ipifunc3_t)func, arg, 0));
}

static __inline int
lwkt_send_ipiq2_mask(cpumask_t mask, ipifunc2_t func, void *arg1, int arg2)
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

static __inline int
lwkt_need_ipiq_process(globaldata_t gd)
{
    lwkt_ipiq_t ipiq;

    if (CPUMASK_TESTNZERO(gd->gd_ipimask))
	return 1;

    ipiq = &gd->gd_cpusyncq;
    return (ipiq->ip_rindex != ipiq->ip_windex);
}

#endif	/* _KERNEL */
#endif	/* _SYS_THREAD2_H_ */

