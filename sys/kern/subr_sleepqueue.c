/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
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
 * DragonFly implementation of the FreeBSD sleepq*() API.  FreeBSD's sleepq*()
 * API differs from our tsleep*() API in several major ways.
 *
 * ONLY USE THIS FOR FREEBSD COMPATIBILITY, E.G. THE LINUX KPI
 *
 * - Explicit interlock via sleepq_lock(wchan)
 * - Single global wchan hash table
 * - Explicit blocking counts
 * - Expects all blockers on the same wchan to be of the same type
 *
 * In addition, the API calls have a ton of unnecessary redundancy which
 * creates problems with using on-stack structures for things.  To make
 * ends meat, DragonFly records information in the thread structure and
 * only enacts the actual operation when the thread issues a *wait*() API
 * call.  Only the spin interlock is handled in real-time.
 */

#include "opt_ddb.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/caps.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sleepqueue.h>
#include <sys/objcache.h>
#include <sys/sysctl.h>

#include <sys/time.h>

#include <machine/atomic.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <sys/signal2.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mutex2.h>

#include <vm/vm_extern.h>

#define SLEEPQ_HSIZE		1024
#define SLEEPQ_HMASK		(SLEEPQ_HSIZE - 1)
#define SLEEPQ_HASH(wchan)	((((uintptr_t)(wchan) >> 10) ^ \
				  ((uintptr_t)(wchan) & SLEEPQ_HMASK)))

#define SLEEPQ_LOOKUP(wchan)	&sleepq_chains[SLEEPQ_HASH(wchan)]
#define SLEEPQ_NRQUEUES		2
#define SLEEPQ_FREEPERSLOT	4

struct sleepqueue_wchan;

struct sleepqueue_chain {
	struct spinlock	sc_spin;
	TAILQ_HEAD(, sleepqueue_wchan) sc_wchead;
	u_int	sc_free_count;
};

struct sleepqueue_wchan {
	TAILQ_ENTRY(sleepqueue_wchan) wc_entry;
	const void *wc_wchan;
	struct sleepqueue_chain *wc_sc;
	u_int	wc_refs;
	int	wc_type;
	u_int	wc_blocked[SLEEPQ_NRQUEUES];
};

static struct sleepqueue_chain sleepq_chains[SLEEPQ_HSIZE];
static MALLOC_DEFINE(M_SLEEPQ, "sleepq", "fbsd sleepq api");
static struct objcache *sleepq_wc_cache;

/*
 * Return existing wchan, assert not NULL.
 */
static __inline
struct sleepqueue_wchan *
sleepq_wclookup(const void *wchan)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;

	sc = SLEEPQ_LOOKUP(wchan);
	KKASSERT(spin_held(&sc->sc_spin));
	TAILQ_FOREACH(wc, &sc->sc_wchead, wc_entry) {
		if (wc->wc_wchan == wchan)
			return(wc);
	}
	panic("sleepq_wclookup: wchan %p not found\n", wc);

	return NULL;	/* not reached */
}

/*
 * Early initialization of sleep queues
 */
static void
init_sleepqueues(void)
{
	int i;

	for (i = 0; i < SLEEPQ_HSIZE; ++i) {
		spin_init(&sleepq_chains[i].sc_spin, "sleepq");
		TAILQ_INIT(&sleepq_chains[i].sc_wchead);
	}
	/*thread0.td_sleepqueue = sleepq_alloc();*/

	sleepq_wc_cache = objcache_create_simple(M_SLEEPQ,
					    sizeof(struct sleepqueue_wchan));

}
SYSINIT(sysinit_sleepqueues, SI_BOOT2_LWKT_INIT, SI_ORDER_SECOND,
	init_sleepqueues, NULL);

/*
 * DragonFlyBSD Interface Functions
 *
 * Setup and teardown sleepq related thread structures
 *
 * sleepq_alloc() - not applicable
 * sleepq_free()  - not applicable
 */
void
sleepq_setup_thread(struct thread *td)
{
}

void
sleepq_teardown_thread(struct thread *td)
{
}

/*
 * Lock the wchan, creating the structure if necessary.  Returns with
 * the spin-lock held.
 *
 * Also used as an interlock, so we need to actually lock something here.
 */
void
sleepq_lock(const void *wchan)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;

	sc = SLEEPQ_LOOKUP(wchan);
	spin_lock(&sc->sc_spin);

	for (;;) {
		/*
		 * Look for the wchan, if not found then allocate one from
		 * the existing in-list cache if available.
		 */
		TAILQ_FOREACH(wc, &sc->sc_wchead, wc_entry) {
			if (wc->wc_wchan == wchan) {
				++wc->wc_refs;
				return;
			}
			if (wc->wc_wchan == NULL) {
				wc->wc_wchan = wchan;
				++wc->wc_refs;
				--sc->sc_free_count;
				return;
			}
		}

		/*
		 * Not found and no free entries available, allocate
		 * a new, free wc, then relock and repeat the search.
		 */
		spin_unlock(&sc->sc_spin);
		wc = objcache_get(sleepq_wc_cache, M_WAITOK);
		KKASSERT(wc->wc_wchan == NULL && wc->wc_refs == 0);
		wc->wc_sc = sc;
		spin_lock(&sc->sc_spin);
		TAILQ_INSERT_TAIL(&sc->sc_wchead, wc, wc_entry);
		++sc->sc_free_count;
		/* loop re-search */
	}
}

/*
 * Unlock the sleep queue chain associated with a given wait channel.
 */
void
sleepq_release(const void *wchan)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;

	wc = sleepq_wclookup(wchan);
	sc = wc->wc_sc;
	KKASSERT(wc->wc_refs > 0);
	if (--wc->wc_refs == 0) {
		/* just sanity-check one for brevity */
		KKASSERT(wc->wc_blocked[0] == 0);
		wc->wc_wchan = NULL;
		wc->wc_type = 0;
		++sc->sc_free_count;
	}

	spin_unlock(&sc->sc_spin);
}

/*
 * Place the current thread on the specified wait channel and sub-queue.
 *
 * The caller must be holding the wchan lock and it will remain locked
 * on return.  The caller must follow-up with a sleepq_*wait*() call.
 *
 * If INVARIANTS is enabled, then it associates the passed in lock with
 * the sleepq to make sure it is held when that sleep queue is woken up
 * (XXX not implemented)
 */
void
sleepq_add(const void *wchan, struct lock_object *lock, const char *wmesg,
	   int flags, int queue)
{
	struct sleepqueue_wchan *wc;
	struct thread *td;
	int domain;

	/*
	 * Locate the wc (also asserts that the lock is held).  Bump
	 * wc->wc_refs and wc->wc_blocked[queue] to indicate that the
	 * thread has been placed on the sleepq.
	 */
	wc = sleepq_wclookup(wchan);
	++wc->wc_refs;
	++wc->wc_blocked[queue];
	wc->wc_type = flags & SLEEPQ_TYPE;

	/*
	 * tsleep_interlock() sets TDF_TSLEEPQ, sets td_wchan, and td_wdomain,
	 * and places the thread on the appropriate sleepq.
	 */
	td = curthread;
	td->td_wmesg = wmesg;
	domain = PDOMAIN_FBSD0 + queue * PDOMAIN_FBSDINC;
	tsleep_interlock(wchan, domain);

	/*
	 * Clear timeout to discern a possible later sleepq_set_timeout_sbt()
	 * call.
	 */
	td->td_sqwc = wc;
	td->td_sqtimo = 0;
	td->td_sqqueue = queue;
}

/*
 * Set a timeout for the thread after it has been added to a wait channel.
 */
void
sleepq_set_timeout_sbt(const void *wchan, sbintime_t sbt, sbintime_t pr,
		       int flags)
{
	struct sleepqueue_wchan *wc;
	struct thread *td;

	td = curthread;
	wc = td->td_sqwc;
	KKASSERT(wc && wc->wc_wchan == wchan);
	td->td_sqtimo = sbticks + sbt;
}

/*
 * Return the number of actual sleepers for the specified queue.
 *
 * Caller must be holding the wchan locked
 */
u_int
sleepq_sleepcnt(const void *wchan, int queue)
{
	struct sleepqueue_wchan *wc;

	wc = sleepq_wclookup(wchan);
	return (wc->wc_blocked[queue]);
}

static __inline
int
_sleepq_wait_begin(struct thread *td, struct sleepqueue_chain *sc,
		   struct sleepqueue_wchan *wc, sbintime_t timo, int tflags)
{
	int domain;
	int ret;

	spin_unlock(&sc->sc_spin);

	domain = PDOMAIN_FBSD0 + td->td_sqqueue * PDOMAIN_FBSDINC;
	if (timo) {
		timo -= sbticks;
		if (timo > 0) {
			ret = tsleep(td->td_wchan, tflags, td->td_wmesg, timo);
		} else {
			ret = EWOULDBLOCK;
		}
	} else {
		ret = tsleep(td->td_wchan, tflags, td->td_wmesg, 0);
	}

	return ret;
}

/*
 * Wait for completion
 */
static __inline
void
_sleepq_wait_complete(struct thread *td, struct sleepqueue_chain *sc,
		      struct sleepqueue_wchan *wc)
{
	struct sleepqueue_wchan *wcn;

	td->td_sqwc = NULL;
	spin_lock(&sc->sc_spin);
	--wc->wc_blocked[td->td_sqqueue];
	if (--wc->wc_refs == 0) {
		wc->wc_wchan = NULL;
		wc->wc_type = 0;
		++sc->sc_free_count;
		if (sc->sc_free_count <= SLEEPQ_FREEPERSLOT) {
			wcn = TAILQ_NEXT(wc, wc_entry);
			if (wcn && wcn->wc_wchan) {
				TAILQ_REMOVE(&sc->sc_wchead, wc, wc_entry);
				TAILQ_INSERT_TAIL(&sc->sc_wchead, wc, wc_entry);
			}
			spin_unlock(&sc->sc_spin);
		} else {
			--sc->sc_free_count;
			TAILQ_REMOVE(&sc->sc_wchead, wc, wc_entry);
			spin_unlock(&sc->sc_spin);
			objcache_put(sleepq_wc_cache, wc);
		}
	} else {
		spin_unlock(&sc->sc_spin);
	}
}

/*
 * Various combinations of wait until unblocked, with and without
 * signaling and timeouts.
 *
 * The wchan lock must be held by the caller and will be released upon
 * return.
 *
 * NOTE: tsleep_interlock() was already issued by sleepq_add().
 */
void
sleepq_wait(const void *wchan, int pri)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;
	struct thread *td;

	td = curthread;
	wc = td->td_sqwc;
	KKASSERT(wc != NULL && wc->wc_wchan == wchan);
	sc = wc->wc_sc;

	(void)_sleepq_wait_begin(td, sc, wc, 0, PINTERLOCKED);
	_sleepq_wait_complete(td, sc, wc);
}

int
sleepq_wait_sig(const void *wchan, int pri)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;
	struct thread *td;
	int ret;

	td = curthread;
	wc = td->td_sqwc;
	KKASSERT(wc != NULL && wc->wc_wchan == wchan);
	sc = wc->wc_sc;

	ret = _sleepq_wait_begin(td, sc, wc, 0, PINTERLOCKED | PCATCH);
	_sleepq_wait_complete(td, sc, wc);

	return ret;
}

int
sleepq_timedwait(const void *wchan, int pri)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;
	struct thread *td;
	int ret;

	td = curthread;
	wc = td->td_sqwc;
	KKASSERT(wc != NULL && wc->wc_wchan == wchan);
	sc = wc->wc_sc;

	ret = _sleepq_wait_begin(td, sc, wc, td->td_sqtimo, PINTERLOCKED);
	_sleepq_wait_complete(td, sc, wc);

	return ret;
}

int
sleepq_timedwait_sig(const void *wchan, int pri)
{
	struct sleepqueue_chain *sc;
	struct sleepqueue_wchan *wc;
	struct thread *td;
	int ret;

	td = curthread;
	wc = td->td_sqwc;
	KKASSERT(wc != NULL && wc->wc_wchan == wchan);
	sc = wc->wc_sc;

	ret = _sleepq_wait_begin(td, sc, wc, td->td_sqtimo,
				 PINTERLOCKED | PCATCH);
	_sleepq_wait_complete(td, sc, wc);

	return ret;
}

/*
 * Returns the type of sleepqueue given a waitchannel.  Returns 0 if
 * sleepq_add() has not been called no the wchan.
 *
 * wchan must be locked.
 */
int
sleepq_type(const void *wchan)
{
	struct sleepqueue_wchan *wc;
	int type;

	wc = sleepq_wclookup(wchan);
	type = wc->wc_type;

	return type;
}

/*
 * Wakeup one thread sleeping on a wait channel.
 *
 * DragonFly: Presumably callers also deal with multiple wakeups,
 *	      our wakeup_domaoin_one() function is a bit non-deterministic.
 *
 *	      Nominally returns whether the swapper should be woken up which
 *	      is not applicable to DragonFlyBSD.
 */
int
sleepq_signal(const void *wchan, int flags, int pri, int queue)
{
	int domain;

	domain = PDOMAIN_FBSD0 + queue * PDOMAIN_FBSDINC;
	wakeup_domain_one(wchan, domain);

	return 0;
}

/*
 * Resume everything sleeping on the specified wait channel and queue.
 *
 * Nominally returns whether the swapper should be woken up which is not
 * applicable to DragonFlyBSD.
 */
int
sleepq_broadcast(const void *wchan, int flags, int pri, int queue)
{
	int domain;

	domain = PDOMAIN_FBSD0 + queue * PDOMAIN_FBSDINC;
	wakeup_domain(wchan, domain);

	return 0;
}
