/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2022 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 * This module implements the hammer2 helper thread API, including
 * the frontend/backend XOP API.
 */
#include "hammer2.h"

#define H2XOPDESCRIPTOR(label)					\
	hammer2_xop_desc_t hammer2_##label##_desc = {		\
		.storage_func = hammer2_xop_##label,		\
		.id = #label					\
	}

H2XOPDESCRIPTOR(ipcluster);
H2XOPDESCRIPTOR(readdir);
H2XOPDESCRIPTOR(nresolve);
H2XOPDESCRIPTOR(unlink);
H2XOPDESCRIPTOR(nrename);
H2XOPDESCRIPTOR(scanlhc);
H2XOPDESCRIPTOR(scanall);
H2XOPDESCRIPTOR(lookup);
H2XOPDESCRIPTOR(delete);
H2XOPDESCRIPTOR(inode_mkdirent);
H2XOPDESCRIPTOR(inode_create);
H2XOPDESCRIPTOR(inode_create_det);
H2XOPDESCRIPTOR(inode_create_ins);
H2XOPDESCRIPTOR(inode_destroy);
H2XOPDESCRIPTOR(inode_chain_sync);
H2XOPDESCRIPTOR(inode_unlinkall);
H2XOPDESCRIPTOR(inode_connect);
H2XOPDESCRIPTOR(inode_flush);
H2XOPDESCRIPTOR(strategy_read);
H2XOPDESCRIPTOR(strategy_write);

//struct objcache *cache_xops;
static struct thread dummy_td;
struct thread *curthread = &dummy_td;

/*
 * Set flags and wakeup any waiters.
 *
 * WARNING! During teardown (thr) can disappear the instant our cmpset
 *	    succeeds.
 */
void
hammer2_thr_signal(hammer2_thread_t *thr, uint32_t flags)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = thr->flags;
		cpu_ccfence();
		nflags = (oflags | flags) & ~HAMMER2_THREAD_WAITING;

		if (oflags & HAMMER2_THREAD_WAITING) {
			if (atomic_cmpset_int(&thr->flags, oflags, nflags)) {
				wakeup(&thr->flags);
				break;
			}
		} else {
			if (atomic_cmpset_int(&thr->flags, oflags, nflags))
				break;
		}
	}
}

/*
 * Set and clear flags and wakeup any waiters.
 *
 * WARNING! During teardown (thr) can disappear the instant our cmpset
 *	    succeeds.
 */
void
hammer2_thr_signal2(hammer2_thread_t *thr, uint32_t posflags, uint32_t negflags)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = thr->flags;
		cpu_ccfence();
		nflags = (oflags | posflags) &
			~(negflags | HAMMER2_THREAD_WAITING);
		if (oflags & HAMMER2_THREAD_WAITING) {
			if (atomic_cmpset_int(&thr->flags, oflags, nflags)) {
				wakeup(&thr->flags);
				break;
			}
		} else {
			if (atomic_cmpset_int(&thr->flags, oflags, nflags))
				break;
		}
	}
}

/*
 * Wait until all the bits in flags are set.
 *
 * WARNING! During teardown (thr) can disappear the instant our cmpset
 *	    succeeds.
 */
void
hammer2_thr_wait(hammer2_thread_t *thr, uint32_t flags)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = thr->flags;
		cpu_ccfence();
		if ((oflags & flags) == flags)
			break;
		nflags = oflags | HAMMER2_THREAD_WAITING;
		tsleep_interlock(&thr->flags, 0);
		if (atomic_cmpset_int(&thr->flags, oflags, nflags)) {
			tsleep(&thr->flags, PINTERLOCKED, "h2twait", hz*60);
		}
	}
}

/*
 * Wait until any of the bits in flags are set, with timeout.
 *
 * WARNING! During teardown (thr) can disappear the instant our cmpset
 *	    succeeds.
 */
int
hammer2_thr_wait_any(hammer2_thread_t *thr, uint32_t flags, int timo)
{
	uint32_t oflags;
	uint32_t nflags;
	int error;

	error = 0;
	for (;;) {
		oflags = thr->flags;
		cpu_ccfence();
		if (oflags & flags)
			break;
		nflags = oflags | HAMMER2_THREAD_WAITING;
		tsleep_interlock(&thr->flags, 0);
		if (atomic_cmpset_int(&thr->flags, oflags, nflags)) {
			error = tsleep(&thr->flags, PINTERLOCKED,
				       "h2twait", timo);
		}
		if (error == ETIMEDOUT) {
			error = HAMMER2_ERROR_ETIMEDOUT;
			break;
		}
	}
	return error;
}

/*
 * Wait until the bits in flags are clear.
 *
 * WARNING! During teardown (thr) can disappear the instant our cmpset
 *	    succeeds.
 */
void
hammer2_thr_wait_neg(hammer2_thread_t *thr, uint32_t flags)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = thr->flags;
		cpu_ccfence();
		if ((oflags & flags) == 0)
			break;
		nflags = oflags | HAMMER2_THREAD_WAITING;
		tsleep_interlock(&thr->flags, 0);
		if (atomic_cmpset_int(&thr->flags, oflags, nflags)) {
			tsleep(&thr->flags, PINTERLOCKED, "h2twait", hz*60);
		}
	}
}

/*
 * Initialize the supplied thread structure, starting the specified
 * thread.
 *
 * NOTE: thr structure can be retained across mounts and unmounts for this
 *	 pmp, so make sure the flags are in a sane state.
 */
void
hammer2_thr_create(hammer2_thread_t *thr, hammer2_pfs_t *pmp,
		   hammer2_dev_t *hmp,
		   const char *id, int clindex, int repidx,
		   void (*func)(void *arg))
{
	thr->pmp = pmp;		/* xop helpers */
	thr->hmp = hmp;		/* bulkfree */
	thr->clindex = clindex;
	thr->repidx = repidx;
	TAILQ_INIT(&thr->xopq);
	atomic_clear_int(&thr->flags, HAMMER2_THREAD_STOP |
				      HAMMER2_THREAD_STOPPED |
				      HAMMER2_THREAD_FREEZE |
				      HAMMER2_THREAD_FROZEN);
	if (thr->scratch == NULL)
		thr->scratch = kmalloc(MAXPHYS, M_HAMMER2, M_WAITOK | M_ZERO);
#if 0
	if (repidx >= 0) {
		lwkt_create(func, thr, &thr->td, NULL, 0, repidx % ncpus,
			    "%s-%s.%02d", id, pmp->pfs_names[clindex], repidx);
	} else if (pmp) {
		lwkt_create(func, thr, &thr->td, NULL, 0, -1,
			    "%s-%s", id, pmp->pfs_names[clindex]);
	} else {
		lwkt_create(func, thr, &thr->td, NULL, 0, -1, "%s", id);
	}
#else
	thr->td = &dummy_td;
#endif
}

/*
 * Terminate a thread.  This function will silently return if the thread
 * was never initialized or has already been deleted.
 *
 * This is accomplished by setting the STOP flag and waiting for the td
 * structure to become NULL.
 */
void
hammer2_thr_delete(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	hammer2_thr_signal(thr, HAMMER2_THREAD_STOP);
	/* Don't wait, there's no such thread in makefs */
	//hammer2_thr_wait(thr, HAMMER2_THREAD_STOPPED);
	thr->pmp = NULL;
	if (thr->scratch) {
		kfree(thr->scratch, M_HAMMER2);
		thr->scratch = NULL;
	}
	KKASSERT(TAILQ_EMPTY(&thr->xopq));
}

/*
 * Asynchronous remaster request.  Ask the synchronization thread to
 * start over soon (as if it were frozen and unfrozen, but without waiting).
 * The thread always recalculates mastership relationships when restarting.
 */
void
hammer2_thr_remaster(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	hammer2_thr_signal(thr, HAMMER2_THREAD_REMASTER);
}

void
hammer2_thr_freeze_async(hammer2_thread_t *thr)
{
	hammer2_thr_signal(thr, HAMMER2_THREAD_FREEZE);
}

void
hammer2_thr_freeze(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	hammer2_thr_signal(thr, HAMMER2_THREAD_FREEZE);
	hammer2_thr_wait(thr, HAMMER2_THREAD_FROZEN);
}

void
hammer2_thr_unfreeze(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	hammer2_thr_signal(thr, HAMMER2_THREAD_UNFREEZE);
	hammer2_thr_wait_neg(thr, HAMMER2_THREAD_FROZEN);
}

int
hammer2_thr_break(hammer2_thread_t *thr)
{
	if (thr->flags & (HAMMER2_THREAD_STOP |
			  HAMMER2_THREAD_REMASTER |
			  HAMMER2_THREAD_FREEZE)) {
		return 1;
	}
	return 0;
}

/****************************************************************************
 *			    HAMMER2 XOPS API	 			    *
 ****************************************************************************/

/*
 * Allocate or reallocate XOP FIFO.  This doesn't exist in sys/vfs/hammer2
 * where XOP is handled by dedicated kernel threads and when FIFO stalls
 * threads wait for frontend to collect results.
 */
static void
hammer2_xop_fifo_alloc(hammer2_xop_fifo_t *fifo, size_t nmemb)
{
	size_t size;

	/* Assert nmemb requirements. */
	KKASSERT((nmemb & (nmemb - 1)) == 0);
	KKASSERT(nmemb >= HAMMER2_XOPFIFO);

	/* malloc or realloc fifo array. */
	size = nmemb * sizeof(hammer2_chain_t *);
	if (!fifo->array)
		fifo->array = kmalloc(size, M_HAMMER2, M_WAITOK | M_ZERO);
	else
		fifo->array = krealloc(fifo->array, size, M_HAMMER2,
		    M_WAITOK | M_ZERO);
	KKASSERT(fifo->array);

	/* malloc or realloc fifo errors. */
	size = nmemb * sizeof(int);
	if (!fifo->errors)
		fifo->errors = kmalloc(size, M_HAMMER2, M_WAITOK | M_ZERO);
	else
		fifo->errors = krealloc(fifo->errors, size, M_HAMMER2,
		    M_WAITOK | M_ZERO);
	KKASSERT(fifo->errors);
}

/*
 * Allocate a XOP request.
 *
 * Once allocated a XOP request can be started, collected, and retired,
 * and can be retired early if desired.
 *
 * NOTE: Fifo indices might not be zero but ri == wi on objcache_get().
 */
void *
hammer2_xop_alloc(hammer2_inode_t *ip, int flags)
{
	hammer2_xop_t *xop;

	xop = ecalloc(1, sizeof(*xop));
	KKASSERT(xop->head.cluster.array[0].chain == NULL);

	xop->head.ip1 = ip;
	xop->head.desc = NULL;
	xop->head.flags = flags;
	xop->head.state = 0;
	xop->head.error = 0;
	xop->head.collect_key = 0;
	xop->head.focus_dio = NULL;

	if (flags & HAMMER2_XOP_MODIFYING)
		xop->head.mtid = hammer2_trans_sub(ip->pmp);
	else
		xop->head.mtid = 0;

	xop->head.cluster.nchains = ip->cluster.nchains;
	xop->head.cluster.pmp = ip->pmp;
	xop->head.cluster.flags = HAMMER2_CLUSTER_LOCKED;

	/*
	 * run_mask - Active thread (or frontend) associated with XOP
	 */
	xop->head.run_mask = HAMMER2_XOPMASK_VOP;

	hammer2_xop_fifo_t *fifo = &xop->head.collect[0];
	xop->head.fifo_size = HAMMER2_XOPFIFO;
	hammer2_xop_fifo_alloc(fifo, xop->head.fifo_size);

	hammer2_inode_ref(ip);

	return xop;
}

void
hammer2_xop_setname(hammer2_xop_head_t *xop, const char *name, size_t name_len)
{
	xop->name1 = kmalloc(name_len + 1, M_HAMMER2, M_WAITOK | M_ZERO);
	xop->name1_len = name_len;
	bcopy(name, xop->name1, name_len);
}

void
hammer2_xop_setname2(hammer2_xop_head_t *xop, const char *name, size_t name_len)
{
	xop->name2 = kmalloc(name_len + 1, M_HAMMER2, M_WAITOK | M_ZERO);
	xop->name2_len = name_len;
	bcopy(name, xop->name2, name_len);
}

size_t
hammer2_xop_setname_inum(hammer2_xop_head_t *xop, hammer2_key_t inum)
{
	const size_t name_len = 18;

	xop->name1 = kmalloc(name_len + 1, M_HAMMER2, M_WAITOK | M_ZERO);
	xop->name1_len = name_len;
	ksnprintf(xop->name1, name_len + 1, "0x%016jx", (intmax_t)inum);

	return name_len;
}


void
hammer2_xop_setip2(hammer2_xop_head_t *xop, hammer2_inode_t *ip2)
{
	xop->ip2 = ip2;
	hammer2_inode_ref(ip2);
}

void
hammer2_xop_setip3(hammer2_xop_head_t *xop, hammer2_inode_t *ip3)
{
	xop->ip3 = ip3;
	hammer2_inode_ref(ip3);
}

void
hammer2_xop_setip4(hammer2_xop_head_t *xop, hammer2_inode_t *ip4)
{
	xop->ip4 = ip4;
	hammer2_inode_ref(ip4);
}

void
hammer2_xop_reinit(hammer2_xop_head_t *xop)
{
	xop->state = 0;
	xop->error = 0;
	xop->collect_key = 0;
	xop->run_mask = HAMMER2_XOPMASK_VOP;
}

/*
 * A mounted PFS needs Xops threads to support frontend operations.
 */
void
hammer2_xop_helper_create(hammer2_pfs_t *pmp)
{
	int i;
	int j;

	lockmgr(&pmp->lock, LK_EXCLUSIVE);
	pmp->has_xop_threads = 1;

	pmp->xop_groups = kmalloc(hammer2_xop_nthreads *
				  sizeof(hammer2_xop_group_t),
				  M_HAMMER2, M_WAITOK | M_ZERO);
	for (i = 0; i < pmp->iroot->cluster.nchains; ++i) {
		for (j = 0; j < hammer2_xop_nthreads; ++j) {
			if (pmp->xop_groups[j].thrs[i].td)
				continue;
			hammer2_thr_create(&pmp->xop_groups[j].thrs[i],
					   pmp, NULL,
					   "h2xop", i, j,
					   hammer2_primary_xops_thread);
		}
	}
	lockmgr(&pmp->lock, LK_RELEASE);
}

void
hammer2_xop_helper_cleanup(hammer2_pfs_t *pmp)
{
	int i;
	int j;

	if (pmp->xop_groups == NULL) {
		KKASSERT(pmp->has_xop_threads == 0);
		return;
	}

	for (i = 0; i < pmp->pfs_nmasters; ++i) {
		for (j = 0; j < hammer2_xop_nthreads; ++j) {
			if (pmp->xop_groups[j].thrs[i].td)
				hammer2_thr_delete(&pmp->xop_groups[j].thrs[i]);
		}
	}
	pmp->has_xop_threads = 0;
	kfree(pmp->xop_groups, M_HAMMER2);
	pmp->xop_groups = NULL;
}

/*
 * Start a XOP request, queueing it to all nodes in the cluster to
 * execute the cluster op.
 *
 * XXX optimize single-target case.
 */
void
hammer2_xop_start_except(hammer2_xop_head_t *xop, hammer2_xop_desc_t *desc,
			 int notidx)
{
	hammer2_inode_t *ip1;
	hammer2_pfs_t *pmp;
	hammer2_thread_t *thr;
	int i;
	int ng;
	int nchains;

	ip1 = xop->ip1;
	pmp = ip1->pmp;
	if (pmp->has_xop_threads == 0)
		hammer2_xop_helper_create(pmp);

	/*
	 * The sequencer assigns a worker thread to the XOP.
	 *
	 * (1) The worker threads are partitioned into two sets, one for
	 *     NON-STRATEGY XOPs, and the other for STRATEGY XOPs.  This
	 *     guarantees that strategy calls will always be able to make
	 *     progress and will not deadlock against non-strategy calls.
	 *
	 * (2) If clustered, non-strategy operations to the same inode must
	 *     be serialized.  This is to avoid confusion when issuing
	 *     modifying operations because a XOP completes the instant a
	 *     quorum is reached.
	 *
	 * TODO - RENAME fails here because it is potentially modifying
	 *	  three different inodes, but we triple-lock the inodes
	 *	  involved so it shouldn't create a sequencing schism.
	 */
	if (xop->flags & HAMMER2_XOP_STRATEGY) {
		/*
		 * Use worker space 0 associated with the current cpu
		 * for strategy ops.
		 */
		/*
		hammer2_xop_strategy_t *xopst;
		u_int which;

		xopst = &((hammer2_xop_t *)xop)->xop_strategy;
		which = ((unsigned int)ip1->ihash +
			 ((unsigned int)xopst->lbase >> HAMMER2_PBUFRADIX)) %
			hammer2_xop_sgroups;
		ng = mycpu->gd_cpuid % hammer2_xop_mod +
		     hammer2_xop_mod * which;
		*/
		ng = 0;
	} else if (hammer2_spread_workers == 0 && ip1->cluster.nchains == 1) {
		/*
		 * For now try to keep the work on the same cpu to reduce
		 * IPI overhead.  Several threads are assigned to each cpu,
		 * don't be very smart and select the one to use based on
		 * the inode hash.
		 */
		/*
		u_int which;

		which = (unsigned int)ip1->ihash % hammer2_xop_xgroups;
		ng = mycpu->gd_cpuid % hammer2_xop_mod +
		     (which * hammer2_xop_mod) +
		     hammer2_xop_xbase;
		*/
		ng = 0;
	} else {
		/*
		 * Hash based on inode only, must serialize inode to same
		 * thread regardless of current cpu.
		 */
		/*
		ng = (unsigned int)ip1->ihash %
		     (hammer2_xop_mod * hammer2_xop_xgroups) +
		     hammer2_xop_xbase;
		*/
		ng = 0;
	}
	xop->desc = desc;

	/*
	 * The instant xop is queued another thread can pick it off.  In the
	 * case of asynchronous ops, another thread might even finish and
	 * deallocate it.
	 */
	hammer2_spin_ex(&pmp->xop_spin);
	nchains = ip1->cluster.nchains;
	for (i = 0; i < nchains; ++i) {
		/*
		 * XXX ip1->cluster.array* not stable here.  This temporary
		 *     hack fixes basic issues in target XOPs which need to
		 *     obtain a starting chain from the inode but does not
		 *     address possible races against inode updates which
		 *     might NULL-out a chain.
		 */
		if (i != notidx && ip1->cluster.array[i].chain) {
			thr = &pmp->xop_groups[ng].thrs[i];
			atomic_set_64(&xop->run_mask, 1LLU << i);
			atomic_set_64(&xop->chk_mask, 1LLU << i);
			xop->collect[i].thr = thr;
			TAILQ_INSERT_TAIL(&thr->xopq, xop, collect[i].entry);
		}
	}
	hammer2_spin_unex(&pmp->xop_spin);
	/* xop can become invalid at this point */

	/*
	 * Each thread has its own xopq
	 */
	for (i = 0; i < nchains; ++i) {
		if (i != notidx) {
			thr = &pmp->xop_groups[ng].thrs[i];
			hammer2_thr_signal(thr, HAMMER2_THREAD_XOPQ);
			hammer2_primary_xops_thread(thr);
		}
	}
}

void
hammer2_xop_start(hammer2_xop_head_t *xop, hammer2_xop_desc_t *desc)
{
	hammer2_xop_start_except(xop, desc, -1);
}

/*
 * Retire a XOP.  Used by both the VOP frontend and by the XOP backend.
 */
void
hammer2_xop_retire(hammer2_xop_head_t *xop, uint64_t mask)
{
	hammer2_chain_t *chain;
	uint64_t nmask;
	int i;

	/*
	 * Remove the frontend collector or remove a backend feeder.
	 *
	 * When removing the frontend we must wakeup any backend feeders
	 * who are waiting for FIFO space.
	 *
	 * When removing the last backend feeder we must wakeup any waiting
	 * frontend.
	 */
	KKASSERT(xop->run_mask & mask);
	nmask = atomic_fetchadd_64(&xop->run_mask,
				   -mask + HAMMER2_XOPMASK_FEED);

	/*
	 * More than one entity left
	 */
	if ((nmask & HAMMER2_XOPMASK_ALLDONE) != mask) {
		/*
		 * Frontend terminating, wakeup any backends waiting on
		 * fifo full.
		 *
		 * NOTE!!! The xop can get ripped out from under us at
		 *	   this point, so do not reference it again.
		 *	   The wakeup(xop) doesn't touch the xop and
		 *	   is ok.
		 */
		if (mask == HAMMER2_XOPMASK_VOP) {
			if (nmask & HAMMER2_XOPMASK_FIFOW)
				wakeup(xop);
		}

		/*
		 * Wakeup frontend if the last backend is terminating.
		 */
		nmask -= mask;
		if ((nmask & HAMMER2_XOPMASK_ALLDONE) == HAMMER2_XOPMASK_VOP) {
			if (nmask & HAMMER2_XOPMASK_WAIT)
				wakeup(xop);
		}

		return;
	}
	/* else nobody else left, we can ignore FIFOW */

	/*
	 * All collectors are gone, we can cleanup and dispose of the XOP.
	 * Note that this can wind up being a frontend OR a backend.
	 * Pending chains are locked shared and not owned by any thread.
	 */

	/*
	 * Cleanup the xop's cluster.  If there is an inode reference,
	 * cache the cluster chains in the inode to improve performance,
	 * preventing them from recursively destroying the chain recursion.
	 *
	 * Note that ip->ccache[i] does NOT necessarily represent usable
	 * chains or chains that are related to the inode.  The chains are
	 * simply held to prevent bottom-up lastdrop destruction of
	 * potentially valuable resolved chain data.
	 */
	if (xop->ip1) {
		/*
		 * Cache cluster chains in a convenient inode.  The chains
		 * are cache ref'd but not held.  The inode simply serves
		 * as a place to cache the chains to prevent the chains
		 * from being cleaned up.
		 */
		hammer2_chain_t *dropch[HAMMER2_MAXCLUSTER];
		hammer2_inode_t *ip;
		int prior_nchains;

		ip = xop->ip1;
		hammer2_spin_ex(&ip->cluster_spin);
		prior_nchains = ip->ccache_nchains;
		for (i = 0; i < prior_nchains; ++i) {
			dropch[i] = ip->ccache[i].chain;
			ip->ccache[i].chain = NULL;
		}
		for (i = 0; i < xop->cluster.nchains; ++i) {
			ip->ccache[i] = xop->cluster.array[i];
			if (ip->ccache[i].chain)
				hammer2_chain_ref(ip->ccache[i].chain);
		}
		ip->ccache_nchains = i;
		hammer2_spin_unex(&ip->cluster_spin);

		/*
		 * Drop prior cache
		 */
		for (i = 0; i < prior_nchains; ++i) {
			chain = dropch[i];
			if (chain)
				hammer2_chain_drop(chain);
		}
	}

	/*
	 * Drop and unhold chains in xop cluster
	 */
	for (i = 0; i < xop->cluster.nchains; ++i) {
		xop->cluster.array[i].flags = 0;
		chain = xop->cluster.array[i].chain;
		if (chain) {
			xop->cluster.array[i].chain = NULL;
			hammer2_chain_drop_unhold(chain);
		}
	}

	/*
	 * Cleanup the fifos.  Since we are the only entity left on this
	 * xop we don't have to worry about fifo flow control, and one
	 * lfence() will do the job.
	 */
	cpu_lfence();
	mask = xop->chk_mask;
	for (i = 0; mask && i < HAMMER2_MAXCLUSTER; ++i) {
		hammer2_xop_fifo_t *fifo = &xop->collect[i];
		while (fifo->ri != fifo->wi) {
			chain = fifo->array[fifo->ri & fifo_mask(xop)];
			if (chain)
				hammer2_chain_drop_unhold(chain);
			++fifo->ri;
		}
		mask &= ~(1U << i);
	}

	/*
	 * The inode is only held at this point, simply drop it.
	 */
	if (xop->ip1) {
		hammer2_inode_drop(xop->ip1);
		xop->ip1 = NULL;
	}
	if (xop->ip2) {
		hammer2_inode_drop(xop->ip2);
		xop->ip2 = NULL;
	}
	if (xop->ip3) {
		hammer2_inode_drop(xop->ip3);
		xop->ip3 = NULL;
	}
	if (xop->ip4) {
		hammer2_inode_drop(xop->ip4);
		xop->ip4 = NULL;
	}
	if (xop->name1) {
		kfree(xop->name1, M_HAMMER2);
		xop->name1 = NULL;
		xop->name1_len = 0;
	}
	if (xop->name2) {
		kfree(xop->name2, M_HAMMER2);
		xop->name2 = NULL;
		xop->name2_len = 0;
	}

	for (i = 0; i < xop->cluster.nchains; ++i) {
		kfree(xop->collect[i].array, M_HAMMER2);
		kfree(xop->collect[i].errors, M_HAMMER2);
	}

	free(xop);
}

/*
 * (Backend) Returns non-zero if the frontend is still attached.
 */
int
hammer2_xop_active(hammer2_xop_head_t *xop)
{
	if (xop->run_mask & HAMMER2_XOPMASK_VOP)
		return 1;
	else
		return 0;
}

/*
 * (Backend) Feed chain data through the cluster validator and back to
 * the frontend.  Chains are fed from multiple nodes concurrently
 * and pipelined via per-node FIFOs in the XOP.
 *
 * The chain must be locked (either shared or exclusive).  The caller may
 * unlock and drop the chain on return.  This function will add an extra
 * ref and hold the chain's data for the pass-back.
 *
 * No xop lock is needed because we are only manipulating fields under
 * our direct control.
 *
 * Returns 0 on success and a hammer2 error code if sync is permanently
 * lost.  The caller retains a ref on the chain but by convention
 * the lock is typically inherited by the xop (caller loses lock).
 *
 * Returns non-zero on error.  In this situation the caller retains a
 * ref on the chain but loses the lock (we unlock here).
 */
int
hammer2_xop_feed(hammer2_xop_head_t *xop, hammer2_chain_t *chain,
		 int clindex, int error)
{
	hammer2_xop_fifo_t *fifo;
	uint64_t mask;

	/*
	 * Early termination (typicaly of xop_readir)
	 */
	if (hammer2_xop_active(xop) == 0) {
		error = HAMMER2_ERROR_ABORTED;
		goto done;
	}

	/*
	 * Multi-threaded entry into the XOP collector.  We own the
	 * fifo->wi for our clindex.
	 */
	fifo = &xop->collect[clindex];

	if (fifo->ri == fifo->wi - HAMMER2_XOPFIFO)
		lwkt_yield();
	while (fifo->ri == fifo->wi - xop->fifo_size) {
		atomic_set_int(&fifo->flags, HAMMER2_XOP_FIFO_STALL);
		mask = xop->run_mask;
		if ((mask & HAMMER2_XOPMASK_VOP) == 0) {
			error = HAMMER2_ERROR_ABORTED;
			goto done;
		}
		xop->fifo_size *= 2;
		hammer2_xop_fifo_alloc(fifo, xop->fifo_size);
	}
	atomic_clear_int(&fifo->flags, HAMMER2_XOP_FIFO_STALL);
	if (chain)
		hammer2_chain_ref_hold(chain);
	if (error == 0 && chain)
		error = chain->error;
	fifo->errors[fifo->wi & fifo_mask(xop)] = error;
	fifo->array[fifo->wi & fifo_mask(xop)] = chain;
	cpu_sfence();
	++fifo->wi;

	mask = atomic_fetchadd_64(&xop->run_mask, HAMMER2_XOPMASK_FEED);
	if (mask & HAMMER2_XOPMASK_WAIT) {
		atomic_clear_64(&xop->run_mask, HAMMER2_XOPMASK_WAIT);
		wakeup(xop);
	}
	error = 0;

	/*
	 * Cleanup.  If no error
	 * occurred the fifo inherits the lock and gains an additional ref.
	 *
	 * The caller's ref remains in both cases.
	 */
done:
	return error;
}

/*
 * (Frontend) collect a response from a running cluster op.
 *
 * Responses are fed from all appropriate nodes concurrently
 * and collected into a cohesive response >= collect_key.
 *
 * The collector will return the instant quorum or other requirements
 * are met, even if some nodes get behind or become non-responsive.
 *
 * HAMMER2_XOP_COLLECT_NOWAIT	- Used to 'poll' a completed collection,
 *				  usually called synchronously from the
 *				  node XOPs for the strategy code to
 *				  fake the frontend collection and complete
 *				  the BIO as soon as possible.
 *
 * Returns 0 on success plus a filled out xop->cluster structure.
 * Return ENOENT on normal termination.
 * Otherwise return an error.
 *
 * WARNING! If the xop returns a cluster with a non-NULL focus, note that
 *	    none of the chains in the cluster (or the focus) are either
 *	    locked or I/O synchronized with the cpu.  hammer2_xop_gdata()
 *	    and hammer2_xop_pdata() must be used to safely access the focus
 *	    chain's content.
 *
 *	    The frontend can make certain assumptions based on higher-level
 *	    locking done by the frontend, but data integrity absolutely
 *	    requires using the gdata/pdata API.
 */
int
hammer2_xop_collect(hammer2_xop_head_t *xop, int flags)
{
	hammer2_xop_fifo_t *fifo;
	hammer2_chain_t *chain;
	hammer2_key_t lokey;
	uint64_t mask;
	int error;
	int keynull;
	int adv;		/* advance the element */
	int i;

loop:
	/*
	 * First loop tries to advance pieces of the cluster which
	 * are out of sync.
	 */
	lokey = HAMMER2_KEY_MAX;
	keynull = HAMMER2_CHECK_NULL;
	mask = xop->run_mask;
	cpu_lfence();

	for (i = 0; i < xop->cluster.nchains; ++i) {
		chain = xop->cluster.array[i].chain;
		if (chain == NULL) {
			adv = 1;
		} else if (chain->bref.key < xop->collect_key) {
			adv = 1;
		} else {
			keynull &= ~HAMMER2_CHECK_NULL;
			if (lokey > chain->bref.key)
				lokey = chain->bref.key;
			adv = 0;
		}
		if (adv == 0)
			continue;

		/*
		 * Advance element if possible, advanced element may be NULL.
		 */
		if (chain)
			hammer2_chain_drop_unhold(chain);

		fifo = &xop->collect[i];
		if (fifo->ri != fifo->wi) {
			cpu_lfence();
			chain = fifo->array[fifo->ri & fifo_mask(xop)];
			error = fifo->errors[fifo->ri & fifo_mask(xop)];
			++fifo->ri;
			xop->cluster.array[i].chain = chain;
			xop->cluster.array[i].error = error;
			if (chain == NULL) {
				/* XXX */
				xop->cluster.array[i].flags |=
							HAMMER2_CITEM_NULL;
			}
			if (fifo->wi - fifo->ri <= HAMMER2_XOPFIFO / 2) {
				if (fifo->flags & HAMMER2_XOP_FIFO_STALL) {
					atomic_clear_int(&fifo->flags,
						    HAMMER2_XOP_FIFO_STALL);
					wakeup(xop);
					lwkt_yield();
				}
			}
			--i;		/* loop on same index */
		} else {
			/*
			 * Retain CITEM_NULL flag.  If set just repeat EOF.
			 * If not, the NULL,0 combination indicates an
			 * operation in-progress.
			 */
			xop->cluster.array[i].chain = NULL;
			/* retain any CITEM_NULL setting */
		}
	}

	/*
	 * Determine whether the lowest collected key meets clustering
	 * requirements.  Returns HAMMER2_ERROR_*:
	 *
	 * 0	 	 - key valid, cluster can be returned.
	 *
	 * ENOENT	 - normal end of scan, return ENOENT.
	 *
	 * ESRCH	 - sufficient elements collected, quorum agreement
	 *		   that lokey is not a valid element and should be
	 *		   skipped.
	 *
	 * EDEADLK	 - sufficient elements collected, no quorum agreement
	 *		   (and no agreement possible).  In this situation a
	 *		   repair is needed, for now we loop.
	 *
	 * EINPROGRESS	 - insufficient elements collected to resolve, wait
	 *		   for event and loop.
	 *
	 * EIO		 - IO error or CRC check error from hammer2_cluster_check()
	 */
	if ((flags & HAMMER2_XOP_COLLECT_WAITALL) &&
	    (mask & HAMMER2_XOPMASK_ALLDONE) != HAMMER2_XOPMASK_VOP) {
		error = HAMMER2_ERROR_EINPROGRESS;
	} else {
		error = hammer2_cluster_check(&xop->cluster, lokey, keynull);
	}
	if (error == HAMMER2_ERROR_EINPROGRESS) {
		if (flags & HAMMER2_XOP_COLLECT_NOWAIT)
			goto done;
		tsleep_interlock(xop, 0);
		if (atomic_cmpset_64(&xop->run_mask,
				     mask, mask | HAMMER2_XOPMASK_WAIT)) {
			tsleep(xop, PINTERLOCKED, "h2coll", hz*60);
		}
		goto loop;
	}
	if (error == HAMMER2_ERROR_ESRCH) {
		if (lokey != HAMMER2_KEY_MAX) {
			xop->collect_key = lokey + 1;
			goto loop;
		}
		error = HAMMER2_ERROR_ENOENT;
	}
	if (error == HAMMER2_ERROR_EDEADLK) {
		kprintf("hammer2: no quorum possible lokey %016jx\n",
			lokey);
		if (lokey != HAMMER2_KEY_MAX) {
			xop->collect_key = lokey + 1;
			goto loop;
		}
		error = HAMMER2_ERROR_ENOENT;
	}
	if (lokey == HAMMER2_KEY_MAX)
		xop->collect_key = lokey;
	else
		xop->collect_key = lokey + 1;
done:
	return error;
}

/*
 * N x M processing threads are available to handle XOPs, N per cluster
 * index x M cluster nodes.
 *
 * Locate and return the next runnable xop, or NULL if no xops are
 * present or none of the xops are currently runnable (for various reasons).
 * The xop is left on the queue and serves to block other dependent xops
 * from being run.
 *
 * Dependent xops will not be returned.
 *
 * Sets HAMMER2_XOP_FIFO_RUN on the returned xop or returns NULL.
 *
 * NOTE! Xops run concurrently for each cluster index.
 */
#define XOP_HASH_SIZE	16
#define XOP_HASH_MASK	(XOP_HASH_SIZE - 1)

static __inline
int
xop_testhash(hammer2_thread_t *thr, hammer2_inode_t *ip, uint32_t *hash)
{
	uint32_t mask;
	int hv;

	hv = (int)((uintptr_t)ip + (uintptr_t)thr) / sizeof(hammer2_inode_t);
	mask = 1U << (hv & 31);
	hv >>= 5;

	return ((int)(hash[hv & XOP_HASH_MASK] & mask));
}

static __inline
void
xop_sethash(hammer2_thread_t *thr, hammer2_inode_t *ip, uint32_t *hash)
{
	uint32_t mask;
	int hv;

	hv = (int)((uintptr_t)ip + (uintptr_t)thr) / sizeof(hammer2_inode_t);
	mask = 1U << (hv & 31);
	hv >>= 5;

	hash[hv & XOP_HASH_MASK] |= mask;
}

static
hammer2_xop_head_t *
hammer2_xop_next(hammer2_thread_t *thr)
{
	hammer2_pfs_t *pmp = thr->pmp;
	int clindex = thr->clindex;
	uint32_t hash[XOP_HASH_SIZE] = { 0 };
	hammer2_xop_head_t *xop;

	hammer2_spin_ex(&pmp->xop_spin);
	TAILQ_FOREACH(xop, &thr->xopq, collect[clindex].entry) {
		/*
		 * Check dependency
		 */
		if (xop_testhash(thr, xop->ip1, hash) ||
		    (xop->ip2 && xop_testhash(thr, xop->ip2, hash)) ||
		    (xop->ip3 && xop_testhash(thr, xop->ip3, hash)) ||
		    (xop->ip4 && xop_testhash(thr, xop->ip4, hash)))
		{
			continue;
		}
		xop_sethash(thr, xop->ip1, hash);
		if (xop->ip2)
			xop_sethash(thr, xop->ip2, hash);
		if (xop->ip3)
			xop_sethash(thr, xop->ip3, hash);
		if (xop->ip4)
			xop_sethash(thr, xop->ip4, hash);

		/*
		 * Check already running
		 */
		if (xop->collect[clindex].flags & HAMMER2_XOP_FIFO_RUN)
			continue;

		/*
		 * Found a good one, return it.
		 */
		atomic_set_int(&xop->collect[clindex].flags,
			       HAMMER2_XOP_FIFO_RUN);
		break;
	}
	hammer2_spin_unex(&pmp->xop_spin);

	return xop;
}

/*
 * Remove the completed XOP from the queue, clear HAMMER2_XOP_FIFO_RUN.
 *
 * NOTE! Xops run concurrently for each cluster index.
 */
static
void
hammer2_xop_dequeue(hammer2_thread_t *thr, hammer2_xop_head_t *xop)
{
	hammer2_pfs_t *pmp = thr->pmp;
	int clindex = thr->clindex;

	hammer2_spin_ex(&pmp->xop_spin);
	TAILQ_REMOVE(&thr->xopq, xop, collect[clindex].entry);
	atomic_clear_int(&xop->collect[clindex].flags,
			 HAMMER2_XOP_FIFO_RUN);
	hammer2_spin_unex(&pmp->xop_spin);
	if (TAILQ_FIRST(&thr->xopq))
		hammer2_thr_signal(thr, HAMMER2_THREAD_XOPQ);
}

/*
 * Primary management thread for xops support.  Each node has several such
 * threads which replicate front-end operations on cluster nodes.
 *
 * XOPS thread node operations, allowing the function to focus on a single
 * node in the cluster after validating the operation with the cluster.
 * This is primarily what prevents dead or stalled nodes from stalling
 * the front-end.
 */
void
hammer2_primary_xops_thread(void *arg)
{
	hammer2_thread_t *thr = arg;
	hammer2_xop_head_t *xop;
	uint64_t mask;
	uint32_t flags;
	uint32_t nflags;

	mask = 1LLU << thr->clindex;

	for (;;) {
		flags = thr->flags;

		/*
		 * Handle stop request
		 */
		if (flags & HAMMER2_THREAD_STOP)
			break;

		/*
		 * Handle freeze request
		 */
		if (flags & HAMMER2_THREAD_FREEZE) {
			hammer2_thr_signal2(thr, HAMMER2_THREAD_FROZEN,
						 HAMMER2_THREAD_FREEZE);
			continue;
		}

		if (flags & HAMMER2_THREAD_UNFREEZE) {
			hammer2_thr_signal2(thr, 0,
						 HAMMER2_THREAD_FROZEN |
						 HAMMER2_THREAD_UNFREEZE);
			continue;
		}

		/*
		 * Force idle if frozen until unfrozen or stopped.
		 */
		if (flags & HAMMER2_THREAD_FROZEN) {
			hammer2_thr_wait_any(thr,
					     HAMMER2_THREAD_UNFREEZE |
					     HAMMER2_THREAD_STOP,
					     0);
			continue;
		}

		/*
		 * Reset state on REMASTER request
		 */
		if (flags & HAMMER2_THREAD_REMASTER) {
			hammer2_thr_signal2(thr, 0, HAMMER2_THREAD_REMASTER);
			/* reset state here */
			continue;
		}

		/*
		 * Process requests.  Each request can be multi-queued.
		 *
		 * If we get behind and the frontend VOP is no longer active,
		 * we retire the request without processing it.  The callback
		 * may also abort processing if the frontend VOP becomes
		 * inactive.
		 */
		if (flags & HAMMER2_THREAD_XOPQ) {
			nflags = flags & ~HAMMER2_THREAD_XOPQ;
			if (!atomic_cmpset_int(&thr->flags, flags, nflags))
				continue;
			flags = nflags;
			/* fall through */
		}
		while ((xop = hammer2_xop_next(thr)) != NULL) {
			if (hammer2_xop_active(xop)) {
				xop->desc->storage_func((hammer2_xop_t *)xop,
							thr->scratch,
							thr->clindex);
				hammer2_xop_dequeue(thr, xop);
				hammer2_xop_retire(xop, mask);
			} else {
				hammer2_xop_feed(xop, NULL, thr->clindex,
						 ECONNABORTED);
				hammer2_xop_dequeue(thr, xop);
				hammer2_xop_retire(xop, mask);
			}
		}

		/* Don't wait, this is a XOP caller thread in makefs */
		break;

		/*
		 * Wait for event, interlock using THREAD_WAITING and
		 * THREAD_SIGNAL.
		 *
		 * For robustness poll on a 30-second interval, but nominally
		 * expect to be woken up.
		 */
		nflags = flags | HAMMER2_THREAD_WAITING;

		tsleep_interlock(&thr->flags, 0);
		if (atomic_cmpset_int(&thr->flags, flags, nflags)) {
			tsleep(&thr->flags, PINTERLOCKED, "h2idle", hz*30);
		}
	}

#if 0
	/*
	 * Cleanup / termination
	 */
	while ((xop = TAILQ_FIRST(&thr->xopq)) != NULL) {
		kprintf("hammer2_thread: aborting xop %s\n", xop->desc->id);
		TAILQ_REMOVE(&thr->xopq, xop,
			     collect[thr->clindex].entry);
		hammer2_xop_retire(xop, mask);
	}
#endif
	thr->td = NULL;
	hammer2_thr_signal(thr, HAMMER2_THREAD_STOPPED);
	/* thr structure can go invalid after this point */
}
