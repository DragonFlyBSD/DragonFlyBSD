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

#ifndef LWKT_NUM_POOL_TOKENS
#define LWKT_NUM_POOL_TOKENS	1024	/* power of 2 */
#endif
#define LWKT_MASK_POOL_TOKENS	(LWKT_NUM_POOL_TOKENS - 1)

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

static int lwkt_token_ipi_dispatch = 4;
SYSCTL_INT(_lwkt, OID_AUTO, token_ipi_dispatch, CTLFLAG_RW,
    &lwkt_token_ipi_dispatch, 0, "Number of IPIs to dispatch on token release");

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
 * Return a pool token given an address
 */
static __inline
lwkt_token_t
_lwkt_token_pool_lookup(void *ptr)
{
	int i;

	i = ((int)(intptr_t)ptr >> 2) ^ ((int)(intptr_t)ptr >> 12);
	return(&pool_tokens[i & LWKT_MASK_POOL_TOKENS]);
}

/*
 * Initialize a tokref_t prior to making it visible in the thread's
 * token array.
 *
 * As an optimization we set the MPSAFE flag if the thread is already
 * holding the mp_token.  This bypasses unncessary calls to get_mplock() and
 * rel_mplock() on tokens which are not normally MPSAFE when the thread
 * is already holding the MP lock.
 */
static __inline
intptr_t
_lwkt_tok_flags(lwkt_token_t tok, thread_t td)
{
	return(tok->t_flags);
}

static __inline
void
_lwkt_tokref_init(lwkt_tokref_t ref, lwkt_token_t tok, thread_t td,
		  intptr_t flags)
{
	ref->tr_tok = tok;
	ref->tr_owner = td;
	ref->tr_flags = flags;
}

#ifdef SMP
/*
 * Force a LWKT reschedule on the target cpu when a requested token
 * becomes available.
 */
static
void
lwkt_reltoken_mask_remote(void *arg, int arg2, struct intrframe *frame)
{
	need_lwkt_resched();
}
#endif

/*
 * This bit of code sends a LWKT reschedule request to whatever other cpus
 * had contended on the token being released.  We could wake up all the cpus
 * but generally speaking if there is a lot of contention we really only want
 * to wake up a subset of cpus to avoid aggregating O(N^2) IPIs.  The current
 * cpuid is used as a basis to select which other cpus to wake up.
 *
 * For the selected cpus we can avoid issuing the actual IPI if the target
 * cpu's RQF_WAKEUP is already set.  In this case simply setting the
 * reschedule flag RQF_AST_LWKT_RESCHED will be sufficient.
 *
 * lwkt.token_ipi_dispatch specifies the maximum number of IPIs to dispatch
 * on a token release.
 */
static __inline
void
_lwkt_reltoken_mask(lwkt_token_t tok)
{
#ifdef SMP
	globaldata_t ngd;
	cpumask_t mask;
	cpumask_t tmpmask;
	cpumask_t wumask;	/* wakeup mask */
	cpumask_t remask;	/* clear mask */
	int wucount;		/* wakeup count */
	int cpuid;
	int reqflags;

	/*
	 * Mask of contending cpus we want to wake up.
	 */
	mask = tok->t_collmask;
	cpu_ccfence();
	if (mask == 0)
		return;

	/*
	 * Degenerate case - IPI to all contending cpus
	 */
	wucount = lwkt_token_ipi_dispatch;
	if (wucount <= 0 || wucount >= ncpus) {
		wucount = 0;
		wumask = mask;
		remask = mask;
	} else {
		wumask = 0;
		remask = 0;
	}

	/*
	 * Calculate which cpus to IPI.  These cpus are potentially in a
	 * HLT state waiting for token contention to go away.
	 *
	 * Ask the cpu LWKT scheduler to reschedule by setting
	 * RQF_AST_LWKT_RESCHEDULE.  Signal the cpu if RQF_WAKEUP is not
	 * set (otherwise it has already been signalled or will check the
	 * flag very soon anyway).  Both bits must be adjusted atomically
	 * all in one go to avoid races.
	 *
	 * The collision mask is cleared for all cpus we set the resched
	 * flag for, but we only IPI the ones that need signalling.
	 */
	while (wucount && mask) {
		tmpmask = mask & ~(CPUMASK(mycpu->gd_cpuid) - 1);
		if (tmpmask)
			cpuid = BSFCPUMASK(tmpmask);
		else
			cpuid = BSFCPUMASK(mask);
		ngd = globaldata_find(cpuid);
		for (;;) {
			reqflags = ngd->gd_reqflags;
			if (atomic_cmpset_int(&ngd->gd_reqflags, reqflags,
					      reqflags |
					      (RQF_WAKEUP |
					       RQF_AST_LWKT_RESCHED))) {
				break;
			}
		}
		if ((reqflags & RQF_WAKEUP) == 0) {
			wumask |= CPUMASK(cpuid);
			--wucount;
		}
		remask |= CPUMASK(cpuid);
		mask &= ~CPUMASK(cpuid);
	}
	if (remask) {
		atomic_clear_cpumask(&tok->t_collmask, remask);
		lwkt_send_ipiq3_mask(wumask, lwkt_reltoken_mask_remote,
				     NULL, 0);
	}
#endif
}

/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.  If a failure occurs
 * any partially acquired tokens will be released prior to return.
 *
 * lwkt_getalltokens is called by the LWKT scheduler to acquire all
 * tokens that the thread had acquired prior to going to sleep.
 *
 * We always clear the collision mask on token aquision.
 *
 * Called from a critical section.
 */
int
lwkt_getalltokens(thread_t td)
{
	lwkt_tokref_t scan;
	lwkt_tokref_t ref;
	lwkt_token_t tok;

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
				if (atomic_cmpset_ptr(&tok->t_ref, NULL, scan))
				{
					if (tok->t_collmask & td->td_gd->gd_cpumask) {
						atomic_clear_cpumask(&tok->t_collmask,
								 td->td_gd->gd_cpumask);
					}
					break;
				}
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

#ifdef SMP
			/*
			 * Otherwise we failed to acquire all the tokens.
			 * Undo and return.  We have to try once more after
			 * setting cpumask to cover possible races against
			 * the checking of t_collmask.
			 */
			atomic_set_cpumask(&tok->t_collmask,
					   td->td_gd->gd_cpumask);
			if (atomic_cmpset_ptr(&tok->t_ref, NULL, scan)) {
				if (tok->t_collmask & td->td_gd->gd_cpumask) {
					atomic_clear_cpumask(&tok->t_collmask,
							 td->td_gd->gd_cpumask);
				}
				break;
			}
#endif
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
 * note that t_ref may not match the scan for recursively held tokens,
 * or for the case where a lwkt_getalltokens() failed.
 *
 * The scheduler is responsible for maintaining the MP lock count, so
 * we don't need to deal with tr_flags here.
 * 
 * Called from a critical section.
 */
void
lwkt_relalltokens(thread_t td)
{
	lwkt_tokref_t scan;
	lwkt_token_t tok;

	for (scan = &td->td_toks_base; scan < td->td_toks_stop; ++scan) {
		tok = scan->tr_tok;
		if (tok->t_ref == scan) {
			tok->t_ref = NULL;
			_lwkt_reltoken_mask(tok);
		}
	}
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
	intptr_t flags;

	flags = _lwkt_tok_flags(tok, td);
	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td, flags);

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
#ifdef SMP
#if 0
		/*
		 * (DISABLED ATM) - Do not set t_collmask on a token
		 * acquisition failure, the scheduler will spin at least
		 * once and deal with hlt/spin semantics.
		 */
		atomic_set_cpumask(&tok->t_collmask, td->td_gd->gd_cpumask);
		if (atomic_cmpset_ptr(&tok->t_ref, NULL, ref)) {
			atomic_clear_cpumask(&tok->t_collmask,
					     td->td_gd->gd_cpumask);
			return;
		}
#endif
#endif
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
	intptr_t flags;

	flags = _lwkt_tok_flags(tok, td);
	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td, flags);

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
#ifdef SMP
#if 0
		/*
		 * (DISABLED ATM) - Do not set t_collmask on a token
		 * acquisition failure, the scheduler will spin at least
		 * once and deal with hlt/spin semantics.
		 */
		atomic_set_cpumask(&tok->t_collmask, td->td_gd->gd_cpumask);
		if (atomic_cmpset_ptr(&tok->t_ref, NULL, ref)) {
			atomic_clear_cpumask(&tok->t_collmask,
					     td->td_gd->gd_cpumask);
			goto success;
		}
#endif
#endif
		td->td_wmesg = tok->t_desc;
		atomic_add_long(&tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(tok->t_ref == ref);
	}
#ifdef SMP
#if 0
success:
#endif
#endif
	crit_enter_hard_gd(td->td_gd);
}

lwkt_token_t
lwkt_getpooltoken(void *ptr)
{
	thread_t td = curthread;
	lwkt_token_t tok;
	lwkt_tokref_t ref;
	intptr_t flags;

	tok = _lwkt_token_pool_lookup(ptr);
	flags = _lwkt_tok_flags(tok, td);
	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td, flags);

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
#ifdef SMP
#if 0
		/*
		 * (DISABLED ATM) - Do not set t_collmask on a token
		 * acquisition failure, the scheduler will spin at least
		 * once and deal with hlt/spin semantics.
		 */
		atomic_set_cpumask(&tok->t_collmask, td->td_gd->gd_cpumask);
		if (atomic_cmpset_ptr(&tok->t_ref, NULL, ref)) {
			atomic_clear_cpumask(&tok->t_collmask,
					     td->td_gd->gd_cpumask);
			goto success;
		}
#endif
#endif
		td->td_wmesg = tok->t_desc;
		atomic_add_long(&tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(tok->t_ref == ref);
	}
#ifdef SMP
#if 0
success:
#endif
#endif
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
	intptr_t flags;

	flags = _lwkt_tok_flags(tok, td);
	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td, flags);

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
	if (tok->t_ref == ref) {
		tok->t_ref = NULL;
		_lwkt_reltoken_mask(tok);
	}
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
	tok->t_flags = 0;
	tok->t_collisions = 0;
	tok->t_collmask = 0;
	tok->t_desc = desc;
}

void
lwkt_token_uninit(lwkt_token_t tok)
{
	/* empty */
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
