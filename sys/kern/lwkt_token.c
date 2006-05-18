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
 * $DragonFly: src/sys/kern/lwkt_token.c,v 1.24 2006/05/18 16:25:19 dillon Exp $
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
#include <machine/ipl.h>
#include <machine/smp.h>

#define THREAD_STACK	(UPAGES * PAGE_SIZE)

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
#ifdef SMP
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

#ifdef SMP

/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.
 */
int
lwkt_getalltokens(thread_t td)
{
    lwkt_tokref_t refs;
    lwkt_tokref_t undo;
    lwkt_token_t tok;

    for (refs = td->td_toks; refs; refs = refs->tr_next) {
	KKASSERT(refs->tr_state == 0);
	tok = refs->tr_tok;
	if (tok->t_owner != td) {
	    if (spin_trylock(td, &tok->t_spinlock) == 0) {
		/*
		 * Release the partial list of tokens obtained and return
		 * failure.
		 */
		for (undo = td->td_toks; undo != refs; undo = undo->tr_next) {
		    tok = undo->tr_tok;
		    undo->tr_state = 0;
		    if (--tok->t_count == 0) {
			tok->t_owner = NULL;
			spin_tryunlock(td, &tok->t_spinlock);
		    }
		}
		return (FALSE);
	    }
	    tok->t_owner = td;
	    KKASSERT(tok->t_count == 0);
	}
	++tok->t_count;
	refs->tr_state = 1;
    }
    return (TRUE);
}

/*
 * Release all tokens owned by the specified thread on the current cpu.
 */
void
lwkt_relalltokens(thread_t td)
{
    lwkt_tokref_t scan;
    lwkt_token_t tok;

    for (scan = td->td_toks; scan; scan = scan->tr_next) {
	if (scan->tr_state) {
	    scan->tr_state = 0;
	    tok = scan->tr_tok;
	    KKASSERT(tok->t_owner == td && tok->t_count > 0);
	    if (--tok->t_count == 0) {
		tok->t_owner = NULL;
		spin_unlock_quick(&tok->t_spinlock);
	    }
	}
    }
}

#endif

/*
 * Acquire a serializing token
 */

static __inline
void
_lwkt_gettokref(lwkt_tokref_t ref)
{
    lwkt_token_t tok;
    thread_t td;

    KKASSERT(mycpu->gd_intr_nesting_level == 0);
    td = curthread;
    tok = ref->tr_tok;

    /*
     * Link the tokref to the thread's list
     */
    ref->tr_next = td->td_toks;
    cpu_ccfence();
    td->td_toks = ref;

    /*
     * Gain ownership of the token's spinlock.
     */
    if (tok->t_owner != td) {
	if (spin_trylock(td, &tok->t_spinlock) == 0) {
	    lwkt_yield();
	    return;
	}
	KKASSERT(tok->t_owner == NULL && tok->t_count == 0);
	tok->t_owner = td;
    }
    ref->tr_state = 1;
    ++tok->t_count;
}

static __inline
int
_lwkt_trytokref(lwkt_tokref_t ref)
{
    lwkt_token_t tok;
    thread_t td;

    KKASSERT(mycpu->gd_intr_nesting_level == 0);
    td = curthread;
    tok = ref->tr_tok;

    /*
     * Link the tokref to the thread's list
     */
    ref->tr_next = td->td_toks;
    cpu_ccfence();
    td->td_toks = ref;

    /*
     * Gain ownership of the token's spinlock.
     */
    if (tok->t_owner != td) {
	if (spin_trylock(td, &tok->t_spinlock) == 0) {
	    td->td_toks = ref->tr_next;
	    return (FALSE);
	}
	KKASSERT(tok->t_owner == NULL && tok->t_count == 0);
	tok->t_owner = td;
    }
    ref->tr_state = 1;
    ++tok->t_count;
    return (TRUE);
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
lwkt_reltoken(lwkt_tokref *ref)
{
    struct lwkt_tokref **scanp;
    lwkt_token_t tok;
    thread_t td;

    td = curthread;
    tok = ref->tr_tok;
    KKASSERT(ref->tr_state == 1 && tok->t_owner == td && tok->t_count > 0);

    for (scanp = &td->td_toks; *scanp != ref; scanp = &((*scanp)->tr_next))
	;
    *scanp = ref->tr_next;
    ref->tr_state = 0;
    if (--tok->t_count == 0) {
	tok->t_owner = NULL;
	spin_unlock_quick(&tok->t_spinlock);
    }
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
    spin_init(&tok->t_spinlock);
    tok->t_owner = NULL;
    tok->t_count = 0;
}

void
lwkt_token_uninit(lwkt_token_t tok)
{
    /* empty */
}
