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
struct lwkt_token pmap_token = LWKT_TOKEN_UP_INITIALIZER(pmap_token);
struct lwkt_token dev_token = LWKT_TOKEN_UP_INITIALIZER(dev_token);
struct lwkt_token vm_token = LWKT_TOKEN_UP_INITIALIZER(vm_token);
struct lwkt_token vmspace_token = LWKT_TOKEN_UP_INITIALIZER(vmspace_token);
struct lwkt_token kvm_token = LWKT_TOKEN_UP_INITIALIZER(kvm_token);
struct lwkt_token proc_token = LWKT_TOKEN_UP_INITIALIZER(proc_token);
struct lwkt_token tty_token = LWKT_TOKEN_UP_INITIALIZER(tty_token);
struct lwkt_token vnode_token = LWKT_TOKEN_UP_INITIALIZER(vnode_token);
struct lwkt_token vmobj_token = LWKT_TOKEN_UP_INITIALIZER(vmobj_token);

SYSCTL_INT(_lwkt, OID_AUTO, pmap_mpsafe, CTLFLAG_RW,
    &pmap_token.t_flags, 0, "Require MP lock for pmap_token");
SYSCTL_INT(_lwkt, OID_AUTO, dev_mpsafe, CTLFLAG_RW,
    &dev_token.t_flags, 0, "Require MP lock for dev_token");
SYSCTL_INT(_lwkt, OID_AUTO, vm_mpsafe, CTLFLAG_RW,
    &vm_token.t_flags, 0, "Require MP lock for vm_token");
SYSCTL_INT(_lwkt, OID_AUTO, vmspace_mpsafe, CTLFLAG_RW,
    &vmspace_token.t_flags, 0, "Require MP lock for vmspace_token");
SYSCTL_INT(_lwkt, OID_AUTO, kvm_mpsafe, CTLFLAG_RW,
    &kvm_token.t_flags, 0, "Require MP lock for kvm_token");
SYSCTL_INT(_lwkt, OID_AUTO, proc_mpsafe, CTLFLAG_RW,
    &proc_token.t_flags, 0, "Require MP lock for proc_token");
SYSCTL_INT(_lwkt, OID_AUTO, tty_mpsafe, CTLFLAG_RW,
    &tty_token.t_flags, 0, "Require MP lock for tty_token");
SYSCTL_INT(_lwkt, OID_AUTO, vnode_mpsafe, CTLFLAG_RW,
    &vnode_token.t_flags, 0, "Require MP lock for vnode_token");
SYSCTL_INT(_lwkt, OID_AUTO, vmobj_mpsafe, CTLFLAG_RW,
    &vmobj_token.t_flags, 0, "Require MP lock for vmobj_token");

/*
 * The collision count is bumped every time the LWKT scheduler fails
 * to acquire needed tokens in addition to a normal lwkt_gettoken()
 * stall.
 */
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
 * holding the MP lock.  This bypasses unncessary calls to get_mplock() and
 * rel_mplock() on tokens which are not normally MPSAFE when the thread
 * is already holding the MP lock.
 *
 * WARNING: The inherited td_xpcount does not count here because a switch
 *	    could schedule the preempted thread and blow away the inherited
 *	    mplock.
 */
static __inline
void
_lwkt_tokref_init(lwkt_tokref_t ref, lwkt_token_t tok, thread_t td)
{
	ref->tr_tok = tok;
	ref->tr_owner = td;
	ref->tr_flags = tok->t_flags;
#ifdef SMP
	if (td->td_mpcount)
#endif
		ref->tr_flags |= LWKT_TOKEN_MPSAFE;
}

/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.  If a failure occurs
 * any partially acquired tokens will be released prior to return.
 *
 * lwkt_getalltokens is called by the LWKT scheduler to acquire all
 * tokens that the thread had acquired prior to going to sleep.
 *
 * The scheduler is responsible for maintaining the MP lock count, so
 * we don't need to deal with tr_flags here.  We also do not do any
 * logging here.  The logging done by lwkt_gettoken() is plenty good
 * enough to get a feel for it.
 *
 * Called from a critical section.
 */
int
lwkt_getalltokens(thread_t td, const char **msgp, const void **addrp)
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
					break;
				continue;
			}

			/*
			 * Test if ref is already recursively held by this
			 * thread.  We cannot safely dereference tok->t_ref
			 * (it might belong to another thread and is thus
			 * unstable), but we don't have to. We can simply
			 * range-check it.
			 */
			if (ref >= &td->td_toks_base && ref < td->td_toks_stop)
				break;

			/*
			 * Otherwise we failed to acquire all the tokens.
			 * Undo and return.
			 */
			*msgp = tok->t_desc;
			*addrp = scan->tr_stallpc;
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
		if (tok->t_ref == scan)
			tok->t_ref = NULL;
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
 * Acquire a serializing token.  This routine does not block.
 */
static __inline
int
_lwkt_trytokref(lwkt_tokref_t ref, thread_t td)
{
	if ((ref->tr_flags & LWKT_TOKEN_MPSAFE) == 0) {
		if (try_mplock() == 0) {
			--td->td_toks_stop;
			return (FALSE);
		}
	}
	if (_lwkt_trytokref2(ref, td, 0) == FALSE) {
		/*
		 * Cleanup, deactivate the failed token.
		 */
		if ((ref->tr_flags & LWKT_TOKEN_MPSAFE) == 0)
			rel_mplock();
		--td->td_toks_stop;
		return (FALSE);
	}
	return (TRUE);
}

/*
 * Acquire a serializing token.  This routine can block.
 */
static __inline
void
_lwkt_gettokref(lwkt_tokref_t ref, thread_t td, const void **stkframe)
{
	if ((ref->tr_flags & LWKT_TOKEN_MPSAFE) == 0)
		get_mplock();
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
		ref->tr_stallpc = stkframe[-1];
		atomic_add_long(&ref->tr_tok->t_collisions, 1);
		logtoken(fail, ref);
		lwkt_switch();
		logtoken(succ, ref);
		KKASSERT(ref->tr_tok->t_ref == ref);
	}
}

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
	_lwkt_gettokref(ref, td, (const void **)&tok);
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
	_lwkt_gettokref(ref, td, (const void **)&tok);
	crit_enter_hard_gd(td->td_gd);
}

lwkt_token_t
lwkt_getpooltoken(void *ptr)
{
	thread_t td = curthread;
	lwkt_token_t tok;
	lwkt_tokref_t ref;

	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	tok = _lwkt_token_pool_lookup(ptr);
	_lwkt_tokref_init(ref, tok, td);
	_lwkt_gettokref(ref, td, (const void **)&ptr);
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
	return(_lwkt_trytokref(ref, td));
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
	 * acquired token it may not match.
	 *
	 * If the token was not MPSAFE release the MP lock.
	 *
	 * NOTE: We have to do this before adjust td_toks_stop, otherwise
	 *	 a fast interrupt can come along and reuse our ref while
	 *	 tok is still attached to it.
	 */
	if (tok->t_ref == ref)
		tok->t_ref = NULL;
	cpu_ccfence();
	if ((ref->tr_flags & LWKT_TOKEN_MPSAFE) == 0)
		rel_mplock();

	/*
	 * Finally adjust td_toks_stop, be very sure that the compiler
	 * does not reorder the clearing of tok->t_ref with the
	 * decrementing of td->td_toks_stop.
	 */
	cpu_ccfence();
	td->td_toks_stop = ref;
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
		lwkt_token_init(&pool_tokens[i], 1, "pool");
}

lwkt_token_t
lwkt_token_pool_lookup(void *ptr)
{
	return (_lwkt_token_pool_lookup(ptr));
}

/*
 * Initialize a token.  If mpsafe is 0, the MP lock is acquired before
 * acquiring the token and released after releasing the token.
 */
void
lwkt_token_init(lwkt_token_t tok, int mpsafe, const char *desc)
{
	tok->t_ref = NULL;
	tok->t_flags = mpsafe ? LWKT_TOKEN_MPSAFE : 0;
	tok->t_collisions = 0;
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
