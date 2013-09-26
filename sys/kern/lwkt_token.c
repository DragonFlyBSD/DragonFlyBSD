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

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#endif

extern int lwkt_sched_debug;

#ifndef LWKT_NUM_POOL_TOKENS
#define LWKT_NUM_POOL_TOKENS	4001	/* prime number */
#endif

static lwkt_token	pool_tokens[LWKT_NUM_POOL_TOKENS];
struct spinlock		tok_debug_spin = SPINLOCK_INITIALIZER(&tok_debug_spin);

#define TOKEN_STRING	"REF=%p TOK=%p TD=%p"
#define TOKEN_ARGS	lwkt_tokref_t ref, lwkt_token_t tok, struct thread *td
#define CONTENDED_STRING	TOKEN_STRING " (contention started)"
#define UNCONTENDED_STRING	TOKEN_STRING " (contention stopped)"
#if !defined(KTR_TOKENS)
#define	KTR_TOKENS	KTR_ALL
#endif

KTR_INFO_MASTER(tokens);
KTR_INFO(KTR_TOKENS, tokens, fail, 0, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, succ, 1, TOKEN_STRING, TOKEN_ARGS);
#if 0
KTR_INFO(KTR_TOKENS, tokens, release, 2, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, remote, 3, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, reqremote, 4, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, reqfail, 5, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, drain, 6, TOKEN_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, contention_start, 7, CONTENDED_STRING, TOKEN_ARGS);
KTR_INFO(KTR_TOKENS, tokens, contention_stop, 7, UNCONTENDED_STRING, TOKEN_ARGS);
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
struct lwkt_token ifnet_token = LWKT_TOKEN_INITIALIZER(ifnet_token);

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

int tokens_debug_output;
SYSCTL_INT(_lwkt, OID_AUTO, tokens_debug_output, CTLFLAG_RW,
    &tokens_debug_output, 0, "Generate stack trace N times");


#ifdef DEBUG_LOCKS_LATENCY

static long tokens_add_latency;
SYSCTL_LONG(_debug, OID_AUTO, tokens_add_latency, CTLFLAG_RW,
	    &tokens_add_latency, 0,
	    "Add spinlock latency");

#endif


static int _lwkt_getalltokens_sorted(thread_t td);

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
_lwkt_tokref_init(lwkt_tokref_t ref, lwkt_token_t tok, thread_t td, long excl)
{
	ref->tr_tok = tok;
	ref->tr_count = excl;
	ref->tr_owner = td;
}

/*
 * Attempt to acquire a shared or exclusive token.  Returns TRUE on success,
 * FALSE on failure.
 *
 * If TOK_EXCLUSIVE is set in mode we are attempting to get an exclusive
 * token, otherwise are attempting to get a shared token.
 *
 * If TOK_EXCLREQ is set in mode this is a blocking operation, otherwise
 * it is a non-blocking operation (for both exclusive or shared acquisions).
 */
static __inline
int
_lwkt_trytokref(lwkt_tokref_t ref, thread_t td, long mode)
{
	lwkt_token_t tok;
	lwkt_tokref_t oref;
	long count;

	tok = ref->tr_tok;
	KASSERT(((mode & TOK_EXCLREQ) == 0 ||	/* non blocking */
		td->td_gd->gd_intr_nesting_level == 0 ||
		panic_cpu_gd == mycpu),
		("Attempt to acquire token %p not already "
		"held in hard code section", tok));

	if (mode & TOK_EXCLUSIVE) {
		/*
		 * Attempt to get an exclusive token
		 */
		for (;;) {
			count = tok->t_count;
			oref = tok->t_ref;	/* can be NULL */
			cpu_ccfence();
			if ((count & ~TOK_EXCLREQ) == 0) {
				/*
				 * It is possible to get the exclusive bit.
				 * We must clear TOK_EXCLREQ on successful
				 * acquisition.
				 */
				if (atomic_cmpset_long(&tok->t_count, count,
						       (count & ~TOK_EXCLREQ) |
						       TOK_EXCLUSIVE)) {
					KKASSERT(tok->t_ref == NULL);
					tok->t_ref = ref;
					return TRUE;
				}
				/* retry */
			} else if ((count & TOK_EXCLUSIVE) &&
				   oref >= &td->td_toks_base &&
				   oref < td->td_toks_stop) {
				/*
				 * Our thread already holds the exclusive
				 * bit, we treat this tokref as a shared
				 * token (sorta) to make the token release
				 * code easier.
				 *
				 * NOTE: oref cannot race above if it
				 *	 happens to be ours, so we're good.
				 *	 But we must still have a stable
				 *	 variable for both parts of the
				 *	 comparison.
				 *
				 * NOTE: Since we already have an exclusive
				 *	 lock and don't need to check EXCLREQ
				 *	 we can just use an atomic_add here
				 */
				atomic_add_long(&tok->t_count, TOK_INCR);
				ref->tr_count &= ~TOK_EXCLUSIVE;
				return TRUE;
			} else if ((mode & TOK_EXCLREQ) &&
				   (count & TOK_EXCLREQ) == 0) {
				/*
				 * Unable to get the exclusive bit but being
				 * asked to set the exclusive-request bit.
				 * Since we are going to retry anyway just
				 * set the bit unconditionally.
				 */
				atomic_set_long(&tok->t_count, TOK_EXCLREQ);
				return FALSE;
			} else {
				/*
				 * Unable to get the exclusive bit and not
				 * being asked to set the exclusive-request
				 * (aka lwkt_trytoken()), or EXCLREQ was
				 * already set.
				 */
				cpu_pause();
				return FALSE;
			}
			/* retry */
		}
	} else {
		/*
		 * Attempt to get a shared token.  Note that TOK_EXCLREQ
		 * for shared tokens simply means the caller intends to
		 * block.  We never actually set the bit in tok->t_count.
		 */
		for (;;) {
			count = tok->t_count;
			oref = tok->t_ref;	/* can be NULL */
			cpu_ccfence();
			if ((count & (TOK_EXCLUSIVE/*|TOK_EXCLREQ*/)) == 0) {
				/* XXX EXCLREQ should work */
				/*
				 * It is possible to get the token shared.
				 */
				if (atomic_cmpset_long(&tok->t_count, count,
						       count + TOK_INCR)) {
					return TRUE;
				}
				/* retry */
			} else if ((count & TOK_EXCLUSIVE) &&
				   oref >= &td->td_toks_base &&
				   oref < td->td_toks_stop) {
				/*
				 * We own the exclusive bit on the token so
				 * we can in fact also get it shared.
				 */
				atomic_add_long(&tok->t_count, TOK_INCR);
				return TRUE;
			} else {
				/*
				 * We failed to get the token shared
				 */
				return FALSE;
			}
			/* retry */
		}
	}
}

static __inline
int
_lwkt_trytokref_spin(lwkt_tokref_t ref, thread_t td, long mode)
{
	int spin;

	if (_lwkt_trytokref(ref, td, mode)) {
#ifdef DEBUG_LOCKS_LATENCY
		long j;
		for (j = tokens_add_latency; j > 0; --j)
			cpu_ccfence();
#endif
		return TRUE;
	}
	for (spin = lwkt_token_spin; spin > 0; --spin) {
		if (lwkt_token_delay)
			tsc_delay(lwkt_token_delay);
		else
			cpu_pause();
		if (_lwkt_trytokref(ref, td, mode)) {
#ifdef DEBUG_LOCKS_LATENCY
			long j;
			for (j = tokens_add_latency; j > 0; --j)
				cpu_ccfence();
#endif
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Release a token that we hold.
 */
static __inline
void
_lwkt_reltokref(lwkt_tokref_t ref, thread_t td)
{
	lwkt_token_t tok;
	long count;

	tok = ref->tr_tok;
	for (;;) {
		count = tok->t_count;
		cpu_ccfence();
		if (tok->t_ref == ref) {
			/*
			 * We are an exclusive holder.  We must clear tr_ref
			 * before we clear the TOK_EXCLUSIVE bit.  If we are
			 * unable to clear the bit we must restore
			 * tok->t_ref.
			 */
			KKASSERT(count & TOK_EXCLUSIVE);
			tok->t_ref = NULL;
			if (atomic_cmpset_long(&tok->t_count, count,
					       count & ~TOK_EXCLUSIVE)) {
				return;
			}
			tok->t_ref = ref;
			/* retry */
		} else {
			/*
			 * We are a shared holder
			 */
			KKASSERT(count & TOK_COUNTMASK);
			if (atomic_cmpset_long(&tok->t_count, count,
					       count - TOK_INCR)) {
				return;
			}
			/* retry */
		}
		/* retry */
	}
}

/*
 * Obtain all the tokens required by the specified thread on the current
 * cpu, return 0 on failure and non-zero on success.  If a failure occurs
 * any partially acquired tokens will be released prior to return.
 *
 * lwkt_getalltokens is called by the LWKT scheduler to re-acquire all
 * tokens that the thread had to release when it switched away.
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
			 * Only try really hard on the last token
			 */
			if (scan == td->td_toks_stop - 1) {
			    if (_lwkt_trytokref_spin(scan, td, scan->tr_count))
				    break;
			} else {
			    if (_lwkt_trytokref(scan, td, scan->tr_count))
				    break;
			}

			/*
			 * Otherwise we failed to acquire all the tokens.
			 * Release whatever we did get.
			 */
			KASSERT(tok->t_desc, ("token %p is not initialized", tok));
			strncpy(td->td_gd->gd_cnt.v_token_name,
				tok->t_desc,
				sizeof(td->td_gd->gd_cnt.v_token_name) - 1);

			if (lwkt_sched_debug > 0) {
				--lwkt_sched_debug;
				kprintf("toka %p %s %s\n",
					tok, tok->t_desc, td->td_comm);
			}
			td->td_wmesg = tok->t_desc;
			++tok->t_collisions;
			while (--scan >= &td->td_toks_base)
				_lwkt_reltokref(scan, td);
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

	/*
	 * Weird order is to try to avoid a panic loop
	 */
	if (td->td_toks_have) {
		scan = td->td_toks_have;
		td->td_toks_have = NULL;
	} else {
		scan = td->td_toks_stop;
	}
	while (--scan >= &td->td_toks_base)
		_lwkt_reltokref(scan, td);
}

/*
 * This is the decontention version of lwkt_getalltokens().  The tokens are
 * acquired in address-sorted order to deal with any deadlocks.  Ultimately
 * token failures will spin into the scheduler and get here.
 *
 * Called from critical section
 */
static
int
_lwkt_getalltokens_sorted(thread_t td)
{
	lwkt_tokref_t sort_array[LWKT_MAXTOKENS];
	lwkt_tokref_t scan;
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
			 * Only try really hard on the last token
			 */
			if (scan == td->td_toks_stop - 1) {
			    if (_lwkt_trytokref_spin(scan, td, scan->tr_count))
				    break;
			} else {
			    if (_lwkt_trytokref(scan, td, scan->tr_count))
				    break;
			}

			/*
			 * Otherwise we failed to acquire all the tokens.
			 * Release whatever we did get.
			 */
			if (lwkt_sched_debug > 0) {
				--lwkt_sched_debug;
				kprintf("tokb %p %s %s\n",
					tok, tok->t_desc, td->td_comm);
			}
			td->td_wmesg = tok->t_desc;
			++tok->t_collisions;
			while (--i >= 0) {
				scan = sort_array[i];
				_lwkt_reltokref(scan, td);
			}
			return(FALSE);
		}
	}

	/*
	 * We were successful, there is no need for another core to signal
	 * us.
	 */
	return (TRUE);
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
	_lwkt_tokref_init(ref, tok, td, TOK_EXCLUSIVE|TOK_EXCLREQ);

#ifdef DEBUG_LOCKS
	/*
	 * Taking an exclusive token after holding it shared will
	 * livelock. Scan for that case and assert.
	 */
	lwkt_tokref_t tk;
	int found = 0;
	for (tk = &td->td_toks_base; tk < ref; tk++) {
		if (tk->tr_tok != tok)
			continue;
		
		found++;
		if (tk->tr_count & TOK_EXCLUSIVE) 
			goto good;
	}
	/* We found only shared instances of this token if found >0 here */
	KASSERT((found == 0), ("Token %p s/x livelock", tok));
good:
#endif

	if (_lwkt_trytokref_spin(ref, td, TOK_EXCLUSIVE|TOK_EXCLREQ))
		return;

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
	++tok->t_collisions;
	logtoken(fail, ref);
	td->td_toks_have = td->td_toks_stop - 1;

	if (tokens_debug_output > 0) {
		--tokens_debug_output;
		spin_lock(&tok_debug_spin);
		kprintf("Excl Token thread %p %s %s\n",
			td, tok->t_desc, td->td_comm);
		print_backtrace(6);
		kprintf("\n");
		spin_unlock(&tok_debug_spin);
	}

	lwkt_switch();
	logtoken(succ, ref);
	KKASSERT(tok->t_ref == ref);
}

/*
 * Similar to gettoken but we acquire a shared token instead of an exclusive
 * token.
 */
void
lwkt_gettoken_shared(lwkt_token_t tok)
{
	thread_t td = curthread;
	lwkt_tokref_t ref;

	ref = td->td_toks_stop;
	KKASSERT(ref < &td->td_toks_end);
	++td->td_toks_stop;
	cpu_ccfence();
	_lwkt_tokref_init(ref, tok, td, TOK_EXCLREQ);

#ifdef DEBUG_LOCKS
        /*
         * Taking a pool token in shared mode is a bad idea; other
         * addresses deeper in the call stack may hash to the same pool
         * token and you may end up with an exclusive-shared livelock.
         * Warn in this condition.
         */
        if ((tok >= &pool_tokens[0]) &&
            (tok < &pool_tokens[LWKT_NUM_POOL_TOKENS]))
                kprintf("Warning! Taking pool token %p in shared mode\n", tok);
#endif


	if (_lwkt_trytokref_spin(ref, td, TOK_EXCLREQ))
		return;

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
	++tok->t_collisions;
	logtoken(fail, ref);
	td->td_toks_have = td->td_toks_stop - 1;

	if (tokens_debug_output > 0) {
		--tokens_debug_output;
		spin_lock(&tok_debug_spin);
		kprintf("Shar Token thread %p %s %s\n",
			td, tok->t_desc, td->td_comm);
		print_backtrace(6);
		kprintf("\n");
		spin_unlock(&tok_debug_spin);
	}

	lwkt_switch();
	logtoken(succ, ref);
}

/*
 * Attempt to acquire a token, return TRUE on success, FALSE on failure.
 *
 * We setup the tokref in case we actually get the token (if we switch later
 * it becomes mandatory so we set TOK_EXCLREQ), but we call trytokref without
 * TOK_EXCLREQ in case we fail.
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
	_lwkt_tokref_init(ref, tok, td, TOK_EXCLUSIVE|TOK_EXCLREQ);

	if (_lwkt_trytokref(ref, td, TOK_EXCLUSIVE))
		return TRUE;

	/*
	 * Failed, unpend the request
	 */
	cpu_ccfence();
	--td->td_toks_stop;
	++tok->t_collisions;
	return FALSE;
}


void
lwkt_gettoken_hard(lwkt_token_t tok)
{
	lwkt_gettoken(tok);
	crit_enter_hard();
}

lwkt_token_t
lwkt_getpooltoken(void *ptr)
{
	lwkt_token_t tok;

	tok = _lwkt_token_pool_lookup(ptr);
	lwkt_gettoken(tok);
	return (tok);
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
	_lwkt_reltokref(ref, td);
	cpu_sfence();
	td->td_toks_stop = ref;
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
	tok->t_count = 0;
	tok->t_ref = NULL;
	tok->t_collisions = 0;
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
 *
 * Only exclusively held tokens contain a reference to the tokref which
 * has to be flipped along with the swap.
 */
void
lwkt_token_swap(void)
{
	lwkt_tokref_t ref1, ref2;
	lwkt_token_t tok1, tok2;
	long count1, count2;
	thread_t td = curthread;

	crit_enter();

	ref1 = td->td_toks_stop - 1;
	ref2 = td->td_toks_stop - 2;
	KKASSERT(ref1 >= &td->td_toks_base);
	KKASSERT(ref2 >= &td->td_toks_base);

	tok1 = ref1->tr_tok;
	tok2 = ref2->tr_tok;
	count1 = ref1->tr_count;
	count2 = ref2->tr_count;

	if (tok1 != tok2) {
		ref1->tr_tok = tok2;
		ref1->tr_count = count2;
		ref2->tr_tok = tok1;
		ref2->tr_count = count1;
		if (tok1->t_ref == ref1)
			tok1->t_ref = ref2;
		if (tok2->t_ref == ref2)
			tok2->t_ref = ref1;
	}

	crit_exit();
}

#ifdef DDB
DB_SHOW_COMMAND(tokens, db_tok_all)
{
	struct lwkt_token *tok, **ptr;
	struct lwkt_token *toklist[16] = {
		&mp_token,
		&pmap_token,
		&dev_token,
		&vm_token,
		&vmspace_token,
		&kvm_token,
		&proc_token,
		&tty_token,
		&vnode_token,
		&vmobj_token,
		NULL
	};

	ptr = toklist;
	for (tok = *ptr; tok; tok = *(++ptr)) {
		db_printf("tok=%p tr_owner=%p t_colissions=%ld t_desc=%s\n", tok,
		    (tok->t_ref ? tok->t_ref->tr_owner : NULL),
		    tok->t_collisions, tok->t_desc);
	}
}
#endif /* DDB */
