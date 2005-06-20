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
 * $DragonFly: src/sys/kern/lwkt_token.c,v 1.18 2005/06/20 07:58:39 dillon Exp $
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>

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

#define	MAKE_TOKENS_SPIN
/* #define MAKE_TOKENS_YIELD */

#ifndef LWKT_NUM_POOL_TOKENS
#define LWKT_NUM_POOL_TOKENS	1024	/* power of 2 */
#endif
#define LWKT_MASK_POOL_TOKENS	(LWKT_NUM_POOL_TOKENS - 1)

#ifdef INVARIANTS
static int token_debug = 0;
#endif

#ifdef SMP
static void lwkt_reqtoken_remote(void *data);
#endif

static lwkt_token	pool_tokens[LWKT_NUM_POOL_TOKENS];

/*
 * Token debugging code, log token operations and who called them.
 */
#ifdef DEBUG_TOKENS

static struct toklog {
    enum tokenum { TOKTRY, TOKGET, TOKREL1, TOKREL2, TOKREMOTE,
		   TOKREQREMOTE, TOKREQFAIL, TOKDRAIN } type;
    int toremote;
    lwkt_tokref_t ref;
    lwkt_token_t tok;
    thread_t td;
    void *caller;
} toklog[SMP_MAXCPU][2048];

static int tokindex[SMP_MAXCPU];

static void
logtoken(lwkt_tokref_t ref, void *stackptr, enum tokenum type)
{
    struct toklog *log;
    globaldata_t gd;

    if (panicstr == NULL) {
	gd = mycpu;
	crit_enter();
	log = &toklog[gd->gd_cpuid][tokindex[gd->gd_cpuid]];
	log->type = type;
	log->ref = ref;
	log->tok = ref->tr_tok;
	if (stackptr)
	    log->caller = ((void **)stackptr)[-1];
	else
	    log->caller = NULL;
	log->td = gd->gd_curthread;
	tokindex[gd->gd_cpuid] = (tokindex[gd->gd_cpuid] + 1) & 2047;
	crit_exit();
    }
}

#else

#define logtoken(ref, stackptr, type)

#endif

#ifdef _KERNEL

#ifdef INVARIANTS
SYSCTL_INT(_lwkt, OID_AUTO, token_debug, CTLFLAG_RW, &token_debug, 0, "");
#endif

#endif

#ifdef SMP

/*
 * Determine if we own all the tokens in the token reference list.
 * Return 1 on success, 0 on failure. 
 *
 * As a side effect, queue requests for tokens we want which are owned
 * by other cpus.  The magic number is used to communicate when the 
 * target cpu has processed the request.  Note, however, that the
 * target cpu may not be able to assign the token to us which is why
 * the scheduler must spin.
 */
int
lwkt_chktokens(thread_t td)
{
    globaldata_t gd = td->td_gd;	/* mycpu */
    lwkt_tokref_t refs;
    globaldata_t dgd;
    lwkt_token_t tok;
    __uint32_t magic;
    int r = 1;

    KKASSERT(gd->gd_curthread->td_pri >= TDPRI_CRIT);
    for (refs = td->td_toks; refs; refs = refs->tr_next) {
	tok = refs->tr_tok;
	if ((dgd = tok->t_cpu) != gd) {
	    cpu_ccfence();	/* don't let the compiler reload tok->t_cpu */
	    r = 0;

	    /*
	     * Queue a request to the target cpu, exit the loop early if
	     * we are unable to queue the IPI message.  The magic number
	     * flags whether we have a pending ipi request queued or not.
	     * It can be set from MAGIC2 to MAGIC1 by a remote cpu but can
	     * only be set from MAGIC1 to MAGIC2 by our cpu.
	     */
	    magic = refs->tr_magic;
	    cpu_ccfence();
	    if (magic == LWKT_TOKREF_MAGIC1) {
		refs->tr_magic = LWKT_TOKREF_MAGIC2;	/* MP synched slowreq*/
		refs->tr_reqgd = gd;
		tok->t_reqcpu = gd;	/* MP unsynchronized 'fast' req */

		logtoken(refs, &td, TOKREQREMOTE);

		if (lwkt_send_ipiq_nowait(dgd, lwkt_reqtoken_remote, refs)) {
		    /* failed */
		    refs->tr_magic = LWKT_TOKREF_MAGIC1;

		    logtoken(refs, &td, TOKREQFAIL);
		    break;
		}
	    } else if (magic != LWKT_TOKREF_MAGIC2) {
		panic("lwkt_chktoken(): token ref %p tok %p bad magic %08x\n",
			refs, refs->tr_tok, magic);
	    }
	}
    }
    return(r);
}

#endif

/*
 * Check if we already own the token.  Return 1 on success, 0 on failure.
 */
int
lwkt_havetoken(lwkt_token_t tok)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;
    lwkt_tokref_t ref;

    for (ref = td->td_toks; ref; ref = ref->tr_next) {
        if (ref->tr_tok == tok)
            return(1);
    }
    return(0);
}

int
lwkt_havetokref(lwkt_tokref_t xref)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;
    lwkt_tokref_t ref;

    for (ref = td->td_toks; ref; ref = ref->tr_next) {
        if (ref == xref)
            return(1);
    }
    return(0);
}

#ifdef SMP

/*
 * Returns 1 if it is ok to give a token away, 0 if it is not.
 */
static int
lwkt_oktogiveaway_token(lwkt_token_t tok)
{
    globaldata_t gd = mycpu;
    lwkt_tokref_t ref;
    thread_t td;

    for (td = gd->gd_curthread; td; td = td->td_preempted) {
	for (ref = td->td_toks; ref; ref = ref->tr_next) {
	    if (ref->tr_tok == tok)
		return(0);
	}
    }
    return(1);
}

#endif

/*
 * Acquire a serializing token
 */

static __inline
void
_lwkt_gettokref(lwkt_tokref_t ref)
{
    lwkt_tokref_t scan;
    lwkt_token_t tok;
    globaldata_t gd;
    thread_t td;

    gd = mycpu;			/* our cpu */
    KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC1);
    KKASSERT(gd->gd_intr_nesting_level == 0);
    td = gd->gd_curthread;	/* our thread */

    /*
     * Link the request into our thread's list.  This interlocks against
     * remote requests from other cpus, prevents the token from being
     * given away if our cpu already owns it, and interlocks against 
     * preempting threads which may want the token.  This also allows us to
     * avoid using a critical section.
     */
    ref->tr_next = td->td_toks;
    cpu_ccfence();	/* prevent compiler reordering */
    td->td_toks = ref;
    tok = ref->tr_tok;

    /*
     * If we are preempting another thread which owns the token we have to
     * yield to get out from the preemption because we cannot obtain a token
     * owned by the thread we are preempting.
     */
    if (td->td_preempted) {
	while ((td = td->td_preempted) != NULL) {
	    for (scan = td->td_toks; scan; scan = scan->tr_next) {
		if (scan->tr_tok == tok) {
		    lwkt_yield();
		    KKASSERT(tok->t_cpu == gd);
		    goto breakout;
		}
	    }
	}
breakout: ;
	td = gd->gd_curthread;	/* our thread, again */
    }

    /*
     * If our cpu does not own the token then (currently) spin while we
     * await it.  XXX we should yield here but some testing is required
     * before we do so, there could be some interlock issues with e.g.
     * softupdates before we can yield.  ZZZ
     */
#ifdef SMP
    if (tok->t_cpu != gd) {
#if defined(MAKE_TOKENS_SPIN)
	int x = 40000000;
	int y = 10;
	crit_enter();
	while (lwkt_chktokens(td) == 0) {
	    lwkt_process_ipiq();
	    lwkt_drain_token_requests();
	    if (--x == 0) {
		x = 40000000;
		printf("CHKTOKEN looping on cpu %d\n", gd->gd_cpuid);
#ifdef _KERNEL
		if (--y == 0)
			panic("CHKTOKEN looping on cpu %d", gd->gd_cpuid);
#endif
	    }
	    splz();
	}
	crit_exit();
#elif defined(MAKE_TOKENS_YIELD)
	lwkt_yield();
#else
#error MAKE_TOKENS_XXX ?
#endif
	KKASSERT(tok->t_cpu == gd);
    }
#endif
}


/*
 * Attempt to acquire a serializing token
 */
static __inline
int
_lwkt_trytokref(lwkt_tokref_t ref)
{
    lwkt_token_t tok;
    globaldata_t gd;
    thread_t td;

    gd = mycpu;			/* our cpu */
    KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC1);
    KKASSERT(gd->gd_intr_nesting_level == 0);
    td = gd->gd_curthread;	/* our thread */

    /*
     * Link the request into our thread's list.  This interlocks against
     * remote requests from other cpus and prevents the token from being
     * given away if our cpu already owns it.  This also allows us to
     * avoid using a critical section.
     *
     * Force a panic to occur if chktokens is called while the reference
     * is linked to td_toks but before we have resolved whether we can
     * keep it.  chktokens should never be called on our ref list
     * preemptively.
     */
    ref->tr_magic = LWKT_TOKREF_MAGIC3;
    ref->tr_next = td->td_toks;
    cpu_ccfence();	/* prevent compiler reordering */
    td->td_toks = ref;

    /*
     * If our cpu does not own the token then stop now.
     *
     * Otherwise make sure the token is not held by a thread we are
     * preempting.  If it is, stop.
     */
    tok = ref->tr_tok;
#ifdef SMP
    if (tok->t_cpu != gd) {
	td->td_toks = ref->tr_next;	/* remove ref */
	ref->tr_magic = LWKT_TOKREF_MAGIC1;
	return(0);
    }
#endif
    if (td->td_preempted) {
	while ((td = td->td_preempted) != NULL) {
	    lwkt_tokref_t scan;
	    for (scan = td->td_toks; scan; scan = scan->tr_next) {
		if (scan->tr_tok == tok) {
		    td = gd->gd_curthread;	/* our thread */
		    td->td_toks = ref->tr_next;	/* remove ref */
		    ref->tr_magic = LWKT_TOKREF_MAGIC1;
		    return(0);
		}
	    }
	}
    }

    /*
     * We own the token, legitimize the reference.
     */
    ref->tr_magic = LWKT_TOKREF_MAGIC1;
    /* 'td' variable no longer valid */
    return(1);
}

void
lwkt_gettoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
    lwkt_tokref_init(ref, tok);
    logtoken(ref, &ref, TOKGET);
    _lwkt_gettokref(ref);
}

void
lwkt_gettokref(lwkt_tokref_t ref)
{
    logtoken(ref, &ref, TOKGET);
    _lwkt_gettokref(ref);
}

int
lwkt_trytoken(lwkt_tokref_t ref, lwkt_token_t tok)
{
    lwkt_tokref_init(ref, tok);
    logtoken(ref, &ref, TOKTRY);
    return(_lwkt_trytokref(ref));
}

int
lwkt_trytokref(lwkt_tokref_t ref)
{
    logtoken(ref, &ref, TOKTRY);
    return(_lwkt_trytokref(ref));
}

/*
 * Release a serializing token
 */
void
lwkt_reltoken(lwkt_tokref *_ref)
{
    lwkt_tokref_t scan;
    lwkt_tokref *ref;
    lwkt_tokref **pref;
    lwkt_token_t tok;
    globaldata_t gd;
    thread_t td;
    int giveaway;

    logtoken(_ref, &_ref, TOKREL1);
    /*
     * Guard check and stack check (if in the same stack page).  We must
     * also wait for any action pending on remote cpus which we do by
     * checking the magic number and yielding in a loop.
     */
    ref = _ref;
#ifdef INVARIANTS
    if ((((intptr_t)ref ^ (intptr_t)&_ref) & ~(intptr_t)PAGE_MASK) == 0)
	KKASSERT((char *)ref > (char *)&_ref);
    KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC1 || 
	     ref->tr_magic == LWKT_TOKREF_MAGIC2);
#endif

    tok = ref->tr_tok;
    gd = mycpu;
    td = gd->gd_curthread;

    KKASSERT(tok->t_cpu == gd);
    KKASSERT(gd->gd_intr_nesting_level == 0);

    /*
     * We can only give away the token if we aren't holding it recursively.
     * Also use the opportunity to locate the link field for the token.
     *
     * We do not have to scan preempted threads since by definition we cannot
     * be holding any token held by a thread we are preempting.
     */
    giveaway = 1;
    for (pref = &td->td_toks; (ref = *pref) != _ref; pref = &ref->tr_next) {
	KKASSERT(ref != NULL);
	if (ref->tr_tok == tok)
	    giveaway = 0;
    }
    for (scan = ref->tr_next; scan; scan = scan->tr_next) {
	if (scan->tr_tok == tok)
	    giveaway = 0;
    }

    /*
     * Give the token away (if we can) before removing the interlock.  Once
     * the interlock is removed, the token can be given away by an IPI.
     */
    if (giveaway)
	tok->t_cpu = tok->t_reqcpu;	
    KKASSERT(*pref == ref);
    *pref = ref->tr_next;

    /*
     * If we had gotten the token opportunistically and it still happens to
     * be queued to a target cpu, we have to wait for the target cpu
     * to finish processing it.  This does not happen very often and does
     * not need to be optimal.
     */
    while (ref->tr_magic == LWKT_TOKREF_MAGIC2) {
#if defined(MAKE_TOKENS_SPIN)
	crit_enter();
#ifdef SMP
	lwkt_process_ipiq();
#endif
	splz();
	crit_exit();
#elif defined(MAKE_TOKENS_YIELD)
	lwkt_yield();
#else
#error MAKE_TOKENS_XXX ?
#endif
    }
    KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC1);
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

#ifdef SMP

/*
 * This is the receiving side of a remote IPI requesting a token.  If we
 * cannot immediately hand the token off to another cpu we queue it.
 *
 * NOTE!  we 'own' the ref structure, but we only 'own' the token if
 * t_cpu == mycpu.
 */
static void
lwkt_reqtoken_remote(void *data)
{
    lwkt_tokref_t ref = data;
    globaldata_t gd = mycpu;
    lwkt_token_t tok = ref->tr_tok;

    logtoken(ref, &data, TOKREMOTE);
    /*
     * We do not have to queue the token if we can give it away
     * immediately.  Otherwise we queue it to our globaldata structure.
     */
    KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC2);
    if (lwkt_oktogiveaway_token(tok)) {
	if (tok->t_cpu == gd)
	    tok->t_cpu = ref->tr_reqgd;
	cpu_ccfence();	/* prevent compiler reordering */
	ref->tr_magic = LWKT_TOKREF_MAGIC1;
    } else {
	ref->tr_gdreqnext = gd->gd_tokreqbase;
	gd->gd_tokreqbase = ref;
    }
}

/*
 * Must be called from a critical section.  Satisfy all remote token
 * requests that are pending on our globaldata structure.  The request
 * does not have to be satisfied with a successful change of ownership
 * but we do have to acknowledge that we have completed processing the
 * request by setting the magic number back to MAGIC1.
 *
 * NOTE!  we 'own' the ref structure, but we only 'own' the token if
 * t_cpu == mycpu.
 */
void
lwkt_drain_token_requests(void)
{
    globaldata_t gd = mycpu;
    lwkt_tokref_t ref;

    KKASSERT(gd->gd_curthread->td_pri >= TDPRI_CRIT);
    while ((ref = gd->gd_tokreqbase) != NULL) {
	gd->gd_tokreqbase = ref->tr_gdreqnext;
	KKASSERT(ref->tr_magic == LWKT_TOKREF_MAGIC2);
	if (ref->tr_tok->t_cpu == gd)
	    ref->tr_tok->t_cpu = ref->tr_reqgd;
	cpu_ccfence();	/* prevent compiler reordering */
	ref->tr_magic = LWKT_TOKREF_MAGIC1;
    }
}

#endif

/*
 * Initialize the owner and release-to cpu to the current cpu
 * and reset the generation count.
 */
void
lwkt_token_init(lwkt_token_t tok)
{
    tok->t_cpu = tok->t_reqcpu = mycpu;
}

void
lwkt_token_uninit(lwkt_token_t tok)
{
    /* empty */
}
