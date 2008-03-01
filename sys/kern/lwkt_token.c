/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/kern/lwkt_token.c,v 1.30 2008/03/01 06:21:28 dillon Exp $
 */

#ifdef _KERNEL

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

#else

#include <sys/stdint.h>
#include <libcaps/thread.h>
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include <libcaps/globaldata.h>
#include <machine/cpufunc.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <machine/lock.h>
#include <machine/cpu.h>

#endif

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
KTR_INFO(KTR_TOKENS, tokens, try, 0, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, get, 1, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, release, 2, TOKEN_STRING, sizeof(void *) * 3);
#if 0
KTR_INFO(KTR_TOKENS, tokens, remote, 3, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, reqremote, 4, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, reqfail, 5, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, drain, 6, TOKEN_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, contention_start, 7, CONTENDED_STRING, sizeof(void *) * 3);
KTR_INFO(KTR_TOKENS, tokens, contention_stop, 7, UNCONTENDED_STRING, sizeof(void *) * 3);
#endif

#define logtoken(name, ref)						\
	KTR_LOG(tokens_ ## name, ref, ref->tr_tok, curthread)

#ifdef _KERNEL

#ifdef INVARIANTS
SYSCTL_INT(_lwkt, OID_AUTO, token_debug, CTLFLAG_RW, &token_debug, 0, "");
#endif

#endif

/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.
 *
 * The preemption code will not allow a target thread holding spinlocks to
 * preempt the current thread so we do not have to implement this for UP.
 * The only reason why we implement this for UP is that we want to detect
 * stale tokens (lwkt_token_is_stale).
 * 
 * lwkt_getalltokens is called by the LWKT scheduler to acquire all
 * tokens that the thread had aquired prior to going to sleep.
 *
 * Called from a critical section.
 */
int
lwkt_getalltokens(thread_t td)
{
    lwkt_tokref_t refs;
#ifdef SMP
    lwkt_tokref_t undo;
#endif
    lwkt_token_t tok;

    for (refs = td->td_toks; refs; refs = refs->tr_next) {
	KKASSERT(refs->tr_state == 0);
	tok = refs->tr_tok;
	if (tok->t_owner != td) {
#ifdef SMP
	    if (spin_trylock_wr(&tok->t_spinlock) == 0) {
		/*
		 * Release the partial list of tokens obtained and return
		 * failure.
		 */
		for (undo = td->td_toks; undo != refs; undo = undo->tr_next) {
		    tok = undo->tr_tok;
		    undo->tr_state = 0;
		    if (--tok->t_count == 0) {
			tok->t_owner = NULL;
			spin_unlock_wr(&tok->t_spinlock);
		    }
		}
		return (FALSE);
	    }
#endif
	    KKASSERT(tok->t_owner == NULL && tok->t_count == 0);
	    tok->t_owner = td;

	    /*
	     * Detect the situation where the token was acquired by
	     * another thread while the token was released from the
	     * current thread due to a blocking condition.
	     * In this case we set t_lastowner to NULL to mark the
	     * token as stale from the point of view of BOTH threads.
	     * See lwkt_token_is_stale().
	     */
	    if (tok->t_lastowner != tok->t_owner) 
		tok->t_lastowner = NULL;
	}
	++tok->t_count;
	refs->tr_state = 1;
    }
    return (TRUE);
}

/*
 * Release all tokens owned by the specified thread on the current cpu.
 * 
 * Called from a critical section.
 */
void
lwkt_relalltokens(thread_t td)
{
    lwkt_tokref_t refs;
    lwkt_token_t tok;

    for (refs = td->td_toks; refs; refs = refs->tr_next) {
	if (refs->tr_state) {
	    refs->tr_state = 0;
	    tok = refs->tr_tok;
	    KKASSERT(tok->t_owner == td && tok->t_count > 0);
	    if (--tok->t_count == 0) {
		tok->t_owner = NULL;
#ifdef SMP
		spin_unlock_wr(&tok->t_spinlock);
#endif
	    }
	}
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
_lwkt_trytokref2(lwkt_tokref_t ref, thread_t td)
{
#ifndef SMP
    lwkt_tokref_t scan;
    thread_t itd;
#endif
    lwkt_token_t tok;

    KKASSERT(mycpu->gd_intr_nesting_level == 0);
    KKASSERT(ref->tr_state == 0);
    tok = ref->tr_tok;

    /*
     * Link the tokref to the thread's list
     */
    ref->tr_next = td->td_toks;
    cpu_ccfence();

    /* 
     * Once td_toks is set to a non NULL value, we can't preempt
     * another thread anymore (the scheduler takes care that this
     * won't happen). Additionally, we can't get preempted by
     * another thread that wants to access the same token (tok).
     */
    td->td_toks = ref;

    if (tok->t_owner != td) {
#ifdef SMP
	/*
	 * Gain ownership of the token's spinlock, SMP version.
	 */
	if (spin_trylock_wr(&tok->t_spinlock) == 0) {
	    return (FALSE);
	}
#else
	/*
	 * Gain ownership of the token, UP version.   All we have to do
	 * is check the token if we are preempting someone owning the
	 * same token, in which case we fail to acquire the token. 
	 */
	itd = td;
	while ((itd = itd->td_preempted) != NULL) {
	    for (scan = itd->td_toks; scan; scan = scan->tr_next) {
		if (scan->tr_tok == tok) {
		    return (FALSE);
		}
	    }
	}
#endif
	KKASSERT(tok->t_owner == NULL && tok->t_count == 0);
	tok->t_owner = td; 
	tok->t_lastowner = td; 
    }
    ++tok->t_count;
    ref->tr_state = 1;

    return (TRUE);
}

static __inline
int
_lwkt_trytokref(lwkt_tokref_t ref)
{
    thread_t td = curthread;

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
 *
 * We track ownership and a per-owner counter.  Tokens are
 * released when a thread switches out and reacquired when a thread
 * switches back in.  
 */
static __inline
void
_lwkt_gettokref(lwkt_tokref_t ref)
{
  if (_lwkt_trytokref2(ref, curthread) == FALSE) {
	/*
	 * Give up running if we can't acquire the token right now. But as we
	 * have linked in the tokref to the thread's list (_lwkt_trytokref2),
	 * the scheduler now takes care to acquire the token (by calling
	 * lwkt_getalltokens) before resuming execution. As such, when we
	 * return from lwkt_yield(), the token is acquired.
	 */
	lwkt_yield();
  }
}

void
lwkt_gettoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
    lwkt_tokref_init(ref, tok);
    logtoken(get, ref);
    _lwkt_gettokref(ref);
}

void
lwkt_gettokref(lwkt_tokref_t ref)
{
    logtoken(get, ref);
    _lwkt_gettokref(ref);
}

int
lwkt_trytoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
    lwkt_tokref_init(ref, tok);
    logtoken(try, ref);
    return(_lwkt_trytokref(ref));
}

int
lwkt_trytokref(lwkt_tokref_t ref)
{
    logtoken(try, ref);
    return(_lwkt_trytokref(ref));
}

/*
 * Release a serializing token
 */
void
lwkt_reltoken(lwkt_tokref_t ref)
{
    struct lwkt_tokref **scanp;
    lwkt_token_t tok;
    thread_t td;

    td = curthread;
    tok = ref->tr_tok;

    KKASSERT(tok->t_owner == td && ref->tr_state == 1 && tok->t_count > 0);

    ref->tr_state = 0;

    /*
     * Fix-up the count now to avoid racing a preemption which may occur
     * after the token has been removed from td_toks.
     */
    if (--tok->t_count == 0) {
	tok->t_owner = NULL;
	tok->t_lastowner = NULL;
#ifdef SMP
	spin_unlock_wr(&tok->t_spinlock);
#endif
     }

    /*
     * Remove ref from thread's token list.
     *
     * After removing the token from the thread's list, it's unsafe 
     * on a UP machine to modify the token, because we might get
     * preempted by another thread that wants to acquire the same token.
     * This thread now thinks that it can acquire the token, because it's
     * no longer in our thread's list. Bang!
     *
     * SMP: Do not modify token after spin_unlock_wr.
     */
    for (scanp = &td->td_toks; *scanp != ref; scanp = &((*scanp)->tr_next))
	;
    *scanp = ref->tr_next;

    logtoken(release, ref);
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
lwkt_token_pool_get(void *ptraddr)
{
    int i;

    i = ((int)(intptr_t)ptraddr >> 2) ^ ((int)(intptr_t)ptraddr >> 12);
    return(&pool_tokens[i & LWKT_MASK_POOL_TOKENS]);
}

/*
 * Initialize the owner and release-to cpu to the current cpu
 * and reset the generation count.
 */
void
lwkt_token_init(lwkt_token_t tok)
{
#ifdef SMP
    spin_init(&tok->t_spinlock);
#endif
    tok->t_owner = NULL;
    tok->t_lastowner = NULL;
    tok->t_count = 0;
}

void
lwkt_token_uninit(lwkt_token_t tok)
{
    /* empty */
}

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
