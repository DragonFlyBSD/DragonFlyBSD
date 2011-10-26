/*
 * Copyright (c) 2003,2004,2009 The DragonFly Project.  All rights reserved.
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
 * lwkt_token - Implement soft token locks.
 *
 * Tokens are locks which serialize a thread only while the thread is
 * running.  If the thread blocks all tokens are released, then reacquired
 * when the thread resumes.
 *
 * This implementation requires no critical sections or spin locks, but
 * does use atomic_cmpset_ptr().
 *
 * Tokens may be recursively acquired by the same thread.  However the
 * caller must be sure to release such tokens in reverse order.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>
#include <sys/spinlock.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <machine/stdarg.h>
#include <machine/smp.h>

extern int lwkt_sched_debug;

#ifndef LWKT_NUM_POOL_TOKENS
#define LWKT_NUM_POOL_TOKENS	4001	/* prime number */
#endif

static lwkt_token	pool_tokens[LWKT_NUM_POOL_TOKENS];

#define TOKEN_STRING	"REF=%p TOK=%p TD=%p"
#define CONTENDED_STRING	"REF=%p TOK=%p TD=%p (contention started)"
#define UNCONTENDED_STRING	"REF=%p TOK=%p TD=%p (contention stopped)"
#if !defined(KTR_TOKENS)
#define	KTR_TOKENS	KTR_ALL
#endif

KTR_INFO_MASTER(tokens);
KTR_INFO(KTR_TOKENS, tokens, fail, 0, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, succ, 1, TOKEN_STRING, sizeof(void *) * 3);
#if 0
KTR_INFO(KTR_TOKENS, tokens, release, 2, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, remote, 3, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, reqremote, 4, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, reqfail, 5, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, drain, 6, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, contention_start, 7, CONTENDED_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, contention_stop, 7, UNCONTENDED_STRING, sizeof(void *) * 3);
#endif

#define logtoken(name, ref)						\
	KTR_LOG(tokens_ ## name, ref, ref->tr_tok, curthread)

/*
 * Global tokens.  These replace the MP lock for major subsystem locking.
 * These tokens are initially used to lockup both global and individual
 * operations.
 *
 * Once individual structures get their own locks these tokens are used
 * only to protect global lists & other variables and to interlock
 * allocations and teardowns and such.
 *
 * The UP initializer causes token acquisition to also acquire the MP lock
 * for maximum compatibility.  The feature may be enabled and disabled at
 * any time, the MP state is copied to the tokref when the token is acquired
 * and will not race against sysctl changes.
 */
struct lwkt_token mp_token = LWKT_TOKEN_INITIALIZER(mp_token);
struct lwkt_token pmap_token = LWKT_TOKEN_INITIALIZER(pmap_token);
struct lwkt_token dev_token = LWKT_TOKEN_INITIALIZER(dev_token);
struct lwkt_token vm_token = LWKT_TOKEN_INITIALIZER(vm_token);
struct lwkt_token vmspace_token = LWKT_TOKEN_INITIALIZER(vmspace_token);
struct lwkt_token kvm_token = LWKT_TOKEN_INITIALIZER(kvm_token);
struct lwkt_token proc_token = LWKT_TOKEN_INITIALIZER(proc_token);
struct lwkt_token tty_token = LWKT_TOKEN_INITIALIZER(tty_token);
struct lwkt_token vnode_token = LWKT_TOKEN_INITIALIZER(vnode_token);
struct lwkt_token vmobj_token = LWKT_TOKEN_INITIALIZER(vmobj_token);

static int lwkt_token_spin = 5;
SYSCTL_INT(_lwkt, OID_AUTO, token_spin, CTLFLAG_RW,
    &lwkt_token_spin, 0, "Decontention spin loops");
static int lwkt_token_delay = 0;
SYSCTL_INT(_lwkt, OID_AUTO, token_delay, CTLFLAG_RW,
    &lwkt_token_delay, 0, "Decontention spin delay in ns");

/*
 * The collision count is bumped every time the LWKT scheduler fails
 * to acquire needed tokens in addition to a normal lwkt_gettoken()
 * stall.
 */
SYSCTL_LONG(_lwkt, OID_AUTO, mp_collisions, CTLFLAG_RW,
    &mp_token.t_collisions, 0, "Collision counter of mp_token");
SYSCTL_LONG(_lwkt, OID_AUTO, pmap_collisions, CTLFLAG_RW,
    &pmap_token.t_collisions, 0, "Collision counter of pmap_token");
SYSCTL_LONG(_lwkt, OID_AUTO, dev_collisions, CTLFLAG_RW,
    &dev_token.t_collisions, 0, "Collision counter of dev_token");
SYSCTL_LONG(_lwkt, OID_AUTO, vm_collisions, CTLFLAG_RW,
    &vm_token.t_collisions, 0, "Collision counter of vm_token");
SYSCTL_LONG(_lwkt, OID_AUTO, vmspace_collisions, CTLFLAG_RW,
    &vmspace_token.t_collisions, 0, "Collision counter of vmspace_token");
SYSCTL_LONG(_lwkt, OID_AUTO, kvm_collisions, CTLFLAG_RW,
    &kvm_token.t_collisions, 0, "Collision counter of kvm_token");
SYSCTL_LONG(_lwkt, OID_AUTO, proc_collisions, CTLFLAG_RW,
    &proc_token.t_collisions, 0, "Collision counter of proc_token");
SYSCTL_LONG(_lwkt, OID_AUTO, tty_collisions, CTLFLAG_RW,
    &tty_token.t_collisions, 0, "Collision counter of tty_token");
SYSCTL_LONG(_lwkt, OID_AUTO, vnode_collisions, CTLFLAG_RW,
    &vnode_token.t_collisions, 0, "Collision counter of vnode_token");

static int _lwkt_getalltokens_sorted(thread_t td);

#ifdef SMP
/*
 * Acquire the initial mplock
 *
 * (low level boot only)
 */
void
cpu_get_initial_mplock(void)
{
	KKASSERT(mp_token.t_ref == NULL);
	if (lwkt_trytoken(&mp_token) == FALSE)
		panic("cpu_get_initial_mplock");
}
#endif

/*
 * Return a pool token given an address.  Use a prime number to reduce
 * overlaps.
 */
static __inline
lwkt_token_t
_lwkt_token_pool_lookup(void *ptr)
{
	u_int i;

	i = (u_int)(uintptr_t)ptr % LWKT_NUM_POOL_TOKENS;
	return(&pool_tokens[i]);
}

/*
 * Initialize a tokref_t prior to making it visible in the thread's
 * token array.
 */
static __inline
void
_lwkt_tokref_init(lwkt_tokref_t ref, lwkt_token_t tok, thread_t td)
{
	ref->tr_tok = tok;
	ref->tr_owner = td;
}

/*
 * See kern/kern_spinlock.c for the discussion on cache-friendly contention
 * resolution.  We currently do not use cpu_lfence() (expensive!!) and, more
 * importantly, we do a read-test of t_ref before attempting an atomic op,
 * which greatly reduces hw cache bus contention.
 */
static
int
_lwkt_trytoken_spin(lwkt_token_t tok, lwkt_tokref_t ref)
{
	int n;

	for (n = 0; n < lwkt_token_spin; ++n) {
		if (tok->t_ref == NULL &&
		    atomic_cmpset_ptr(&tok->t_ref, NULL, ref)) {
			return TRUE;
		}
		if (lwkt_token_delay) {
			tsc_delay(lwkt_token_delay);
		} else {
			cpu_pause();
		}
	}
	return FALSE;
}

static __inline
void
_lwkt_reltoken_spin(lwkt_token_t tok)
{
	tok->t_ref = NULL;
}

#if 0
/*
 * Helper function used by lwkt_getalltokens[_sorted]().
 *
 * Our attempt to acquire the token has failed.  To reduce cache coherency
 * bandwidth we set our cpu bit in t_collmask then wait for a reasonable
 * period of time for a hand-off from the current token owner.
 */
static
int
_lwkt_trytoken_spin(lwkt_token_t tok, lwkt_tokref_t ref)
{
	globaldata_t gd = mycpu;
	cpumask_t mask;
	int n;

	/*
	 * Add our cpu to the collision mask and wait for the token to be
	 * handed off to us.
	 */
	crit_enter();
	atomic_set_cpumask(&tok->t_collmask, gd->gd_cpumask);
	for (n = 0; n < lwkt_token_spin; ++n) {
		/*
		 * Token was released before we set our collision bit.
		 */
		if (tok->t_ref == NULL &&
		    atomic_cmpset_ptr(&tok->t_ref, NULL, ref)) {
			KKASSERT((tok->t_collmask & gd->gd_cpumask) != 0);
			atomic_clear_cpumask(&tok->t_collmask, gd->gd_cpumask);
			crit_exit();
			return TRUE;
		}

		/*
		 * Token was handed-off to us.
		 */
		if (tok->t_ref == &gd->gd_handoff) {
			KKASSERT((tok->t_collmask & gd->gd_cpumask) == 0);
			tok->t_ref = ref;
			crit_exit();
			return TRUE;
		}
		if (lwkt_token_delay)
			tsc_delay(lwkt_token_delay);
		else
			cpu_pause();
	}

	/*
	 * We failed, attempt to clear our bit in the cpumask.  We may race
	 * someone handing-off to us.  If someone other than us cleared our
	 * cpu bit a handoff is incoming and we must wait for it.
	 */
	for (;;) {
		mask = tok->t_collmask;
		cpu_ccfence();
		if (mask & gd->gd_cpumask) {
			if (atomic_cmpset_cpumask(&tok->t_collmask,
						  mask,
						  mask & ~gd->gd_cpumask)) {
				crit_exit();
				return FALSE;
			}
			continue;
		}
		if (tok->t_ref != &gd->gd_handoff) {
			cpu_pause();
			continue;
		}
		tok->t_ref = ref;
		crit_exit();
		return TRUE;
	}
}

/*
 * Release token with hand-off
 */
static __inline
void
_lwkt_reltoken_spin(lwkt_token_t tok)
{
	globaldata_t xgd;
	cpumask_t sidemask;
	cpumask_t mask;
	int cpuid;

	if (tok->t_collmask == 0) {
		tok->t_ref = NULL;
		return;
	}

	crit_enter();
	sidemask = ~(mycpu->gd_cpumask - 1);	/* high bits >= xcpu */
	for (;;) {
		mask = tok->t_collmask;
		cpu_ccfence();
		if (mask == 0) {
			tok->t_ref = NULL;
			break;
		}
		if (mask & sidemask)
			cpuid = BSFCPUMASK(mask & sidemask);
		else
			cpuid = BSFCPUMASK(mask);
		xgd = globaldata_find(cpuid);
		if (atomic_cmpset_cpumask(&tok->t_collmask, mask,
					  mask & ~CPUMASK(cpuid))) {
			tok->t_ref = &xgd->gd_handoff;
			break;
		}
	}
	crit_exit();
}

#endif


/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.  If a failure occurs
 * any partially acquired tokens will be released prior to return.
 *
 * lwkt_getalltokens is called by the LWKT scheduler to acquire all
 * tokens that the thread had acquired prior to going to sleep.
 *
 * If spinning is non-zero this function acquires the tokens in a particular
 * order to deal with potential deadlocks.  We simply use address order for
 * the case.
 *
 * Called from a critical section.
 */
int
lwkt_getalltokens(thread_t td, int spinning)
{
	lwkt_tokref_t scan;
	lwkt_tokref_t ref;
	lwkt_token_t tok;

	if (spinning)
		return(_lwkt_getalltokens_sorted(td));

	/*
	 * Acquire tokens in forward order, assign or validate tok->t_ref.
	 */
	for (scan = &td->td_toks_base; scan < td->td_toks_stop; ++scan) {
		tok = scan->tr_tok;
		for (;;) {
			/*
			 * Try to acquire the token if we do not already have
			 * it.
			 *
			 * NOTE: If atomic_cmpset_ptr() fails we have to
			 *	 loop and try again.  It just means we
			 *	 lost a cpu race.
			 */
			ref = tok->t_ref;
			if (ref == NULL) {
				if (atomic_cmpset_ptr(&tok->t_ref, NULL,scan))
					break;
				continue;
			}

			/*
			 * Someone holds the token.
			 *
			 * Test if ref is already recursively held by this
			 * thread.  We cannot safely dereference tok->t_ref
			 * (it might belong to another thread and is thus
			 * unstable), but we don't have to. We can simply
			 * range-check it.
			 */
			if (ref >= &td->td_toks_base && ref < td->td_toks_stop)
				break;

			/*
			 * Try hard to acquire this token before giving up
			 * and releasing the whole lot.
			 */
			if (_lwkt_trytoken_spin(tok, scan))
				break;
			if (lwkt_sched_debug > 0) {
				--lwkt_sched_debug;
				kprintf("toka %p %s %s\n",
					tok, tok->t_desc, td->td_comm);
			}

			/*
			 * Otherwise we failed to acquire all the tokens.
			 * Release whatever we did get.
			 */
			td->td_wmesg = tok->t_desc;
			atomic_add_long(&tok->t_collisions, 1);
			lwkt_relalltokens(td);

			return(FALSE);
		}

	}
	return (TRUE);
}

/*
 * Release all tokens owned by the specified thread on the current cpu.
 *
 * This code is really simple.  Even in cases where we own all the tokens
 * note that t_ref may not match the scan for recursively held tokens which
 * are held deeper in the stack, or for the case where a lwkt_getalltokens()
 * failed.
 *
 * Tokens are released in reverse order to reduce chasing race failures.
 * 
 * Called from a critical section.
 */
void
lwkt_relalltokens(thread_t td)
{
	lwkt_tokref_t scan;
	lwkt_token_t tok;

	for (scan = td->td_toks_stop - 1; scan >= &td->td_toks_base; --scan) {
	/*for (scan = &td->td_toks_base; scan < td->td_toks_stop; ++scan) {*/
		tok = scan->tr_tok;
		if (tok->t_ref == scan)
			_lwkt_reltoken_spin(tok);
	}
}

/*
 * This is the decontention version of lwkt_getalltokens().  The tokens are
 * acquired in address-sorted order to deal with any deadlocks.  Ultimately
 * token failures will spin into the scheduler and get here.
 *
 * In addition, to reduce hardware cache coherency contention monitor/mwait
 * is interlocked with gd->gd_reqflags and RQF_SPINNING.  Other cores which
 * release a contended token will clear RQF_SPINNING and cause the mwait
 * to resume.  Any interrupt will also generally set RQF_* flags and cause
 * mwait to resume (or be a NOP in the first place).
 *
 * This code is required to set up RQF_SPINNING in case of failure.  The
 * caller may call monitor/mwait on gd->gd_reqflags on failure.  We do NOT
 * want to call mwait here, and doubly so while we are holding tokens.
 *
 * Called from critical section
 */
static
int
_lwkt_getalltokens_sorted(thread_t td)
{
	/*globaldata_t gd = td->td_gd;*/
	lwkt_tokref_t sort_array[LWKT_MAXTOKENS];
	lwkt_tokref_t scan;
	lwkt_tokref_t ref;
	lwkt_token_t tok;
	int i;
	int j;
	int n;

	/*
	 * Sort the token array.  Yah yah, I know this isn't fun.
	 *
	 * NOTE: Recursively acquired tokens are ordered the same as in the
	 *	 td_toks_array so we can always get the earliest one first.
	 */
	i = 0;
	scan = &td->td_toks_base;
	while (scan < td->td_toks_stop) {
		for (j = 0; j < i; ++j) {
			if (scan->tr_tok < sort_array[j]->tr_tok)
				break;
		}
		if (j != i) {
			bcopy(sort_array + j, sort_array + j + 1,
			      (i - j) * sizeof(lwkt_tokref_t));
		}
		sort_array[j] = scan;
		++scan;
		++i;
	}
	n = i;

	/*
	 * Acquire tokens in forward order, assign or validate tok->t_ref.
	 */
	for (i = 0; i < n; ++i) {
		scan = sort_array[i];
		tok = scan->tr_tok;
		for (;;) {
			/*
			 * Try to acquire the token if we do not already have
			 * it.
			 *
			 * NOTE: If atomic_cmpset_ptr() fails we have to
			 *	 loop and try again.  It just means we
			 *	 lost a cpu race.
			 */
			ref = tok->t_ref;
			if (ref == NULL) {
				if (atomic_cmpset_ptr(&tok->t_ref, NULL, scan))
					break;
				continue;
			}

			/*
			 * Someone holds the token.
			 *
			 * Test if ref is already recursively held by this
			 * thread.  We cannot safely dereference tok->t_ref
			 * (it might belong to another thread and is thus
			 * unstable), but we don't have to. We can simply
			 * range-check it.
			 */
			if (ref >= &td->td_toks_base && ref < td->td_toks_stop)
				break;

			/*
			 * Try hard to acquire this token before giving up
			 * and releasing the whole lot.
			 */
			if (_lwkt_trytoken_spin(tok, scan))
				break;
			if (lwkt_sched_debug > 0) {
				--lwkt_sched_debug;
				kprintf("tokb %p %s %s\n",
					tok, tok->t_desc, td->td_comm);
			}

			/*
			 * Tokens are released in reverse order to reduce
			 * chasing race failures.
			 */
			td->td_wmesg = tok->t_desc;
			atomic_add_long(&tok->t_collisions, 1);

			for (j = i - 1; j >= 0; --j) {
			/*for (j = 0; j < i; ++j) {*/
				scan = sort_array[j];
				tok = scan->tr_tok;
				if (tok->t_ref == scan)
					_lwkt_reltoken_spin(tok);
			}
			return (FALSE);
		}
	}

	/*
	 * We were successful, there is no need for another core to signal
	 * us.
	 */
#if 0
	atomic_clear_int(&gd->gd_reqflags, RQF_SPINNING);
#endif
	return (TRUE);
}

/*
 * Token acquisition helper function.  The caller must have already
 * made nref visible by adjusting td_toks_stop and will be responsible
 * for the disposition of nref on either success or failure.
 *
 * When acquiring tokens recursively we want tok->t_ref to point to
 * the outer (first) acquisition so it gets cleared only on the last
 * release.
 */
static __inline
int
_lwkt_trytokref2(lwkt_tokref_t nref, thread_t td, int blocking)
{
	lwkt_token_t tok;
	lwkt_tokref_t ref;

	/*
	 * Make sure the compiler does not reorder prior instructions
	 * beyond this demark.
	 */
	cpu_ccfence();

	/*
	 * Attempt to gain ownership
	 */
	tok = nref->tr_tok;
	for (;;) {
		/*
		 * Try to acquire the token if we do not already have
		 * it.  This is not allowed if we are in a hard code
		 * section (because it 'might' have blocked).
		 */
		ref = tok->t_ref;
		if (ref == NULL) {
			KASSERT((blocking == 0 ||
				td->td_gd->gd_intr_nesting_level == 0 ||
				panic_cpu_gd == mycpu),
				("Attempt to acquire token %p not already "
				 "held in hard code section", tok));

			/*
			 * NOTE: If atomic_cmpset_ptr() fails we have to
			 *	 loop and try again.  It just means we
			 *	 lost a cpu race.
			 */
			if (atomic_cmpset_ptr(&tok->t_ref, NULL, nref))
				return (TRUE);
			continue;
		}

		/*
		 * Test if ref is already recursively held by this
		 * thread.  We cannot safely dereference tok->t_ref
		 * (it might belong to another thread and is thus
		 * unstable), but we don't have to. We can simply
		 * range-check it.
		 *
		 * It is ok to acquire a token that is already held
		 * by the current thread when in a hard code section.
		 */
		if (ref >= &td->td_toks_base && ref < td->td_toks_stop)
			return(TRUE);

		/*
		 * Spin generously.  This is preferable to just switching
		 * away unconditionally.
		 */
		if (_lwkt_trytoken_spin(tok, nref))
			return(TRUE);

		/*
		 * Otherwise we failed, and it is not ok to attempt to
		 * acquire a token in a hard code section.
		 */
		KASSERT((blocking == 0 ||
			td->td_gd->gd_intr_nesting_level == 0),
			("Attempt to acquire token %p not already "
			 "held in hard code section", tok));

		return(FALSE);
	}
}

/*
 * Get a serializing token.  This routine can block.
 */
void
lwkt_gettoken(lwkt_token_t tok)
{
	thread_t td = curthread;
	lwkt_tokref_t ref;

	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td);

	if (_lwkt_trytokref2(ref, td, 1) == FALSE) {
		/*
		 * Give up running if we can't acquire the token right now.
		 *
		 * Since the tokref is already active the scheduler now
		 * takes care of acquisition, so we need only call
		 * lwkt_switch().
		 *
		 * Since we failed this was not a recursive token so upon
		 * return tr_tok->t_ref should be assigned to this specific
		 * ref.
		 */
		td->td_wmesg = tok->t_desc;
		atomic_add_long(&tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(tok->t_ref == ref);
	}
}

void
lwkt_gettoken_hard(lwkt_token_t tok)
{
	thread_t td = curthread;
	lwkt_tokref_t ref;

	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td);

	if (_lwkt_trytokref2(ref, td, 1) == FALSE) {
		/*
		 * Give up running if we can't acquire the token right now.
		 *
		 * Since the tokref is already active the scheduler now
		 * takes care of acquisition, so we need only call
		 * lwkt_switch().
		 *
		 * Since we failed this was not a recursive token so upon
		 * return tr_tok->t_ref should be assigned to this specific
		 * ref.
		 */
		td->td_wmesg = tok->t_desc;
		atomic_add_long(&tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(tok->t_ref == ref);
	}
	crit_enter_hard_gd(td->td_gd);
}

lwkt_token_t
lwkt_getpooltoken(void *ptr)
{
	thread_t td = curthread;
	lwkt_token_t tok;
	lwkt_tokref_t ref;

	tok = _lwkt_token_pool_lookup(ptr);
	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td);

	if (_lwkt_trytokref2(ref, td, 1) == FALSE) {
		/*
		 * Give up running if we can't acquire the token right now.
		 *
		 * Since the tokref is already active the scheduler now
		 * takes care of acquisition, so we need only call
		 * lwkt_switch().
		 *
		 * Since we failed this was not a recursive token so upon
		 * return tr_tok->t_ref should be assigned to this specific
		 * ref.
		 */
		td->td_wmesg = tok->t_desc;
		atomic_add_long(&tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(tok->t_ref == ref);
	}
	return(tok);
}

/*
 * Attempt to acquire a token, return TRUE on success, FALSE on failure.
 */
int
lwkt_trytoken(lwkt_token_t tok)
{
	thread_t td = curthread;
	lwkt_tokref_t ref;

	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td);

	if (_lwkt_trytokref2(ref, td, 0) == FALSE) {
		/*
		 * Cleanup, deactivate the failed token.
		 */
		cpu_ccfence();
		--td->td_toks_stop;
		return (FALSE);
	}
	return (TRUE);
}

/*
 * Release a serializing token.
 *
 * WARNING!  All tokens must be released in reverse order.  This will be
 *	     asserted.
 */
void
lwkt_reltoken(lwkt_token_t tok)
{
	thread_t td = curthread;
	lwkt_tokref_t ref;

	/*
	 * Remove ref from thread token list and assert that it matches
	 * the token passed in.  Tokens must be released in reverse order.
	 */
	ref = td->td_toks_stop - 1;
	KKASSERT(ref >= &td->td_toks_base && ref->tr_tok == tok);

	/*
	 * Only clear the token if it matches ref.  If ref was a recursively
	 * acquired token it may not match.  Then adjust td_toks_stop.
	 *
	 * Some comparisons must be run prior to adjusting td_toks_stop
	 * to avoid racing against a fast interrupt/ ipi which tries to
	 * acquire a token.
	 *
	 * We must also be absolutely sure that the compiler does not
	 * reorder the clearing of t_ref and the adjustment of td_toks_stop,
	 * or reorder the adjustment of td_toks_stop against the conditional.
	 *
	 * NOTE: The mplock is a token also so sequencing is a bit complex.
	 */
	if (tok->t_ref == ref)
		_lwkt_reltoken_spin(tok);
	cpu_sfence();
	cpu_ccfence();
	td->td_toks_stop = ref;
	cpu_ccfence();
	KKASSERT(tok->t_ref != ref);
}

void
lwkt_reltoken_hard(lwkt_token_t tok)
{
	lwkt_reltoken(tok);
	crit_exit_hard();
}

/*
 * It is faster for users of lwkt_getpooltoken() to use the returned
 * token and just call lwkt_reltoken(), but for convenience we provide
 * this function which looks the token up based on the ident.
 */
void
lwkt_relpooltoken(void *ptr)
{
	lwkt_token_t tok = _lwkt_token_pool_lookup(ptr);
	lwkt_reltoken(tok);
}

/*
 * Return a count of the number of token refs the thread has to the
 * specified token, whether it currently owns the token or not.
 */
int
lwkt_cnttoken(lwkt_token_t tok, thread_t td)
{
	lwkt_tokref_t scan;
	int count = 0;

	for (scan = &td->td_toks_base; scan < td->td_toks_stop; ++scan) {
		if (scan->tr_tok == tok)
			++count;
	}
	return(count);
}


/*
 * Pool tokens are used to provide a type-stable serializing token
 * pointer that does not race against disappearing data structures.
 *
 * This routine is called in early boot just after we setup the BSP's
 * globaldata structure.
 */
void
lwkt_token_pool_init(void)
{
	int i;

	for (i = 0; i < LWKT_NUM_POOL_TOKENS; ++i)
		lwkt_token_init(&pool_tokens[i], "pool");
}

lwkt_token_t
lwkt_token_pool_lookup(void *ptr)
{
	return (_lwkt_token_pool_lookup(ptr));
}

/*
 * Initialize a token.  
 */
void
lwkt_token_init(lwkt_token_t tok, const char *desc)
{
	tok->t_ref = NULL;
	tok->t_collisions = 0;
	tok->t_collmask = 0;
	tok->t_desc = desc;
}

void
lwkt_token_uninit(lwkt_token_t tok)
{
	/* empty */
}

/*
 * Exchange the two most recent tokens on the tokref stack.  This allows
 * you to release a token out of order.
 *
 * We have to be careful about the case where the top two tokens are
 * the same token.  In this case tok->t_ref will point to the deeper
 * ref and must remain pointing to the deeper ref.  If we were to swap
 * it the first release would clear the token even though a second
 * ref is still present.
 */
void
lwkt_token_swap(void)
{
	lwkt_tokref_t ref1, ref2;
	lwkt_token_t tok1, tok2;
	thread_t td = curthread;

	crit_enter();

	ref1 = td->td_toks_stop - 1;
	ref2 = td->td_toks_stop - 2;
	KKASSERT(ref1 > &td->td_toks_base);
	KKASSERT(ref2 > &td->td_toks_base);

	tok1 = ref1->tr_tok;
	tok2 = ref2->tr_tok;
	if (tok1 != tok2) {
		ref1->tr_tok = tok2;
		ref2->tr_tok = tok1;
		if (tok1->t_ref == ref1)
			tok1->t_ref = ref2;
		if (tok2->t_ref == ref2)
			tok2->t_ref = ref1;
	}

	crit_exit();
}

#if 0
int
lwkt_token_is_stale(lwkt_tokref_t ref)
{
	lwkt_token_t tok = ref->tr_tok;

	KKASSERT(tok->t_owner == curthread && ref->tr_state == 1 &&
		 tok->t_count > 0);

	/* Token is not stale */
	if (tok->t_lastowner == tok->t_owner)
		return (FALSE);

	/*
	 * The token is stale. Reset to not stale so that the next call to
	 * lwkt_token_is_stale will return "not stale" unless the token
	 * was acquired in-between by another thread.
	 */
	tok->t_lastowner = tok->t_owner;
	return (TRUE);
}
#endif
