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

#ifdef INVARIANTS
static int token_debug = 0;
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

#ifdef INVARIANTS
SYSCTL_INT(_lwkt, OID_AUTO, token_debug, CTLFLAG_RW, &token_debug, 0, "");
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
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.
 *
 * lwkt_getalltokens is called by the LWKT scheduler to acquire all
 * tokens that the thread had acquired prior to going to sleep.
 *
 * Called from a critical section.
 */
int
lwkt_getalltokens(thread_t td)
{
	lwkt_tokref_t scan1;
	lwkt_tokref_t scan2;
	lwkt_tokref_t ref;
	lwkt_token_t tok;

	for (scan1 = td->td_toks; scan1; scan1 = scan1->tr_next) {
		tok = scan1->tr_tok;
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
			if (ref == scan1)
				break;
			if (ref == NULL) {
				if (atomic_cmpset_ptr(&tok->t_ref, NULL, scan1))
					break;
				continue;
			}

			/*
			 * If acquisition fails the token might be held
			 * recursively by another ref owned by the same
			 * thread.
			 *
			 * NOTE!  We cannot just dereference 'ref' to test
			 *	  the tr_owner as its storage will be
			 *	  unstable if it belongs to another thread.
			 *
			 * NOTE!  Since tokens are inserted at the head
			 *	  of the list we must migrate such tokens
			 *	  so the actual lock is not cleared until
			 *	  the last release.
			 */
			scan2 = td->td_toks;
			for (;;) {
				if (scan2 == scan1)
					return(FALSE);
				if (scan2 == ref) {
					tok->t_ref = scan1;
					break;
				}
				scan2 = scan2->tr_next;
			}
			break;
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
 * Called from a critical section.
 */
void
lwkt_relalltokens(thread_t td)
{
	lwkt_tokref_t scan1;
	lwkt_token_t tok;

	for (scan1 = td->td_toks; scan1; scan1 = scan1->tr_next) {
		tok = scan1->tr_tok;
		if (tok->t_ref == scan1)
			tok->t_ref = NULL;
	}
}

/*
 * Token acquisition helper function.  Note that get/trytokenref do not
 * reset t_lastowner if the token is already held.  Only lwkt_token_is_stale()
 * is allowed to do that.
 *
 * NOTE: On failure, this function doesn't remove the token from the 
 * thread's token list, so that you have to perform that yourself:
 *
 * 	td->td_toks = ref->tr_next;
 */
static __inline
int
_lwkt_trytokref2(lwkt_tokref_t nref, thread_t td)
{
	lwkt_tokref_t ref;
	lwkt_tokref_t scan2;
	lwkt_token_t tok;

	KKASSERT(td->td_gd->gd_intr_nesting_level == 0);

	/*
	 * Link the tokref into curthread's list.  Make sure the
	 * cpu does not reorder these instructions!
	 */
	nref->tr_next = td->td_toks;
	cpu_ccfence();
	td->td_toks = nref;
	cpu_ccfence();

	/*
	 * Attempt to gain ownership
	 */
	tok = nref->tr_tok;
	for (;;) {
		/*
		 * Try to acquire the token if we do not already have
		 * it.
		 */
		ref = tok->t_ref;
		if (ref == nref)
			return (TRUE);
		if (ref == NULL) {
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
		 * If acquisition fails the token might be held
		 * recursively by another ref owned by the same
		 * thread.
		 *
		 * NOTE!  We cannot just dereference 'ref' to test
		 *	  the tr_owner as its storage will be
		 *	  unstable if it belongs to another thread.
		 *
		 * NOTE!  We do not migrate t_ref to nref here as we
		 *	  want the recursion unwinding in reverse order
		 *	  to NOT release the token until last the
		 *	  recursive ref is released.
		 */
		for (scan2 = nref->tr_next; scan2; scan2 = scan2->tr_next) {
			if (scan2 == ref)
				return(TRUE);
		}
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
	if (_lwkt_trytokref2(ref, td) == FALSE) {
		/*
		 * Cleanup. Remove the token from the thread's list.
		 */
		td->td_toks = ref->tr_next;
		return (FALSE);
	}
	return (TRUE);
}

/*
 * Acquire a serializing token.  This routine can block.
 */
static __inline
void
_lwkt_gettokref(lwkt_tokref_t ref, thread_t td)
{
	if (_lwkt_trytokref2(ref, td) == FALSE) {
		/*
		 * Give up running if we can't acquire the token right now.
		 * But as we have linked in the tokref to the thread's list
		 * (_lwkt_trytokref2), the scheduler now takes care to acquire
		 * the token (by calling lwkt_getalltokens) before resuming
		 * execution.  As such, when we return from lwkt_yield(),
		 * the token is acquired.
		 *
		 * Since we failed this is not a recursive token so upon
		 * return tr_tok->t_ref should be assigned to this specific
		 * ref.
		 */
		logtoken(fail, ref);
		lwkt_yield();
		logtoken(succ, ref);
#if 0
		if (ref->tr_tok->t_ref != ref) {
			lwkt_tokref_t scan;
			kprintf("gettokref %p failed, held by tok %p ref %p\n",
				ref, ref->tr_tok, ref->tr_tok->t_ref);
			for (scan = td->td_toks; scan; scan = scan->tr_next) {
				kprintf("    %p\n", scan);
			}
		}
#endif
		KKASSERT(ref->tr_tok->t_ref == ref);
	}
}

void
lwkt_gettoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
	thread_t td = curthread;

	lwkt_tokref_init(ref, tok, td);
	_lwkt_gettokref(ref, td);
}

void
lwkt_getpooltoken(lwkt_tokref_t ref, void *ptr)
{
	thread_t td = curthread;

	lwkt_tokref_init(ref, _lwkt_token_pool_lookup(ptr), td);
	_lwkt_gettokref(ref, td);
}

void
lwkt_gettokref(lwkt_tokref_t ref)
{
	_lwkt_gettokref(ref, ref->tr_owner);
}

int
lwkt_trytoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
	thread_t td = curthread;

	lwkt_tokref_init(ref, tok, td);
	return(_lwkt_trytokref(ref, td));
}

int
lwkt_trytokref(lwkt_tokref_t ref)
{
	return(_lwkt_trytokref(ref, ref->tr_owner));
}

/*
 * Release a serializing token.
 *
 * WARNING!  Any recursive tokens must be released in reverse order.
 */
void
lwkt_reltoken(lwkt_tokref_t ref)
{
	struct lwkt_tokref **scanp;
	lwkt_token_t tok;
	thread_t td;

	tok = ref->tr_tok;

	/*
	 * Remove the ref from the thread's token list.
	 *
	 * NOTE: td == curthread
	 */
	td = ref->tr_owner;
	for (scanp = &td->td_toks; *scanp != ref; scanp = &((*scanp)->tr_next))
		;
	*scanp = ref->tr_next;
	cpu_ccfence();

	/*
	 * Only clear the token if it matches ref.  If ref was a recursively
	 * acquired token it may not match.
	 */
	if (tok->t_ref == ref)
		tok->t_ref = NULL;
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
		lwkt_token_init(&pool_tokens[i]);
}

lwkt_token_t
lwkt_token_pool_lookup(void *ptr)
{
	return (_lwkt_token_pool_lookup(ptr));
}

/*
 * Initialize the owner and release-to cpu to the current cpu
 * and reset the generation count.
 */
void
lwkt_token_init(lwkt_token_t tok)
{
	tok->t_ref = NULL;
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
