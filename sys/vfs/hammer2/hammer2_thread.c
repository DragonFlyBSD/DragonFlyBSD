/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
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

/*
 * Initialize the suspplied thread structure, starting the specified
 * thread.
 */
void
hammer2_thr_create(hammer2_thread_t *thr, hammer2_pfs_t *pmp,
		   const char *id, int clindex, int repidx,
		   void (*func)(void *arg))
{
	lockinit(&thr->lk, "h2thr", 0, 0);
	thr->pmp = pmp;
	thr->xopq = &pmp->xopq[clindex];
	thr->clindex = clindex;
	thr->repidx = repidx;
	if (repidx >= 0) {
		lwkt_create(func, thr, &thr->td, NULL, 0, -1,
			    "%s-%s.%02d", id, pmp->pfs_names[clindex], repidx);
	} else {
		lwkt_create(func, thr, &thr->td, NULL, 0, -1,
			    "%s-%s", id, pmp->pfs_names[clindex]);
	}
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
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_THREAD_STOP);
	wakeup(thr->xopq);
	while (thr->td) {
		lksleep(thr, &thr->lk, 0, "h2thr", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
	thr->pmp = NULL;
	thr->xopq = NULL;
	lockuninit(&thr->lk);
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
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_THREAD_REMASTER);
	wakeup(thr->xopq);
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_thr_freeze_async(hammer2_thread_t *thr)
{
	atomic_set_int(&thr->flags, HAMMER2_THREAD_FREEZE);
	wakeup(thr->xopq);
}

void
hammer2_thr_freeze(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_THREAD_FREEZE);
	wakeup(thr->xopq);
	while ((thr->flags & HAMMER2_THREAD_FROZEN) == 0) {
		lksleep(thr, &thr->lk, 0, "h2frz", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_thr_unfreeze(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_clear_int(&thr->flags, HAMMER2_THREAD_FROZEN);
	wakeup(thr->xopq);
	lockmgr(&thr->lk, LK_RELEASE);
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

void
hammer2_xop_group_init(hammer2_pfs_t *pmp, hammer2_xop_group_t *xgrp)
{
	/* no extra fields in structure at the moment */
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

	xop = objcache_get(cache_xops, M_WAITOK);
	KKASSERT(xop->head.cluster.array[0].chain == NULL);
	xop->head.ip1 = ip;
	xop->head.func = NULL;
	xop->head.state = 0;
	xop->head.error = 0;
	xop->head.collect_key = 0;
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

	for (i = 0; i < pmp->iroot->cluster.nchains; ++i) {
		for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
			if (pmp->xop_groups[j].thrs[i].td)
				continue;
			hammer2_thr_create(&pmp->xop_groups[j].thrs[i], pmp,
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

	for (i = 0; i < pmp->pfs_nmasters; ++i) {
		for (j = 0; j < HAMMER2_XOPGROUPS; ++j) {
			if (pmp->xop_groups[j].thrs[i].td)
				hammer2_thr_delete(&pmp->xop_groups[j].thrs[i]);
		}
	}
}

/*
 * Start a XOP request, queueing it to all nodes in the cluster to
 * execute the cluster op.
 *
 * XXX optimize single-target case.
 */
void
hammer2_xop_start_except(hammer2_xop_head_t *xop, hammer2_xop_func_t func,
			 int notidx)
{
#if 0
	hammer2_xop_group_t *xgrp;
	hammer2_thread_t *thr;
#endif
	hammer2_pfs_t *pmp;
#if 0
	int g;
#endif
	int i;
	int nchains;

	pmp = xop->ip1->pmp;
	if (pmp->has_xop_threads == 0)
		hammer2_xop_helper_create(pmp);

#if 0
	g = pmp->xop_iterator++;
	g = g & HAMMER2_XOPGROUPS_MASK;
	xgrp = &pmp->xop_groups[g];
	xop->xgrp = xgrp;
#endif
	xop->func = func;

	/*
	 * The XOP sequencer is based on ip1, ip2, and ip3.  Because ops can
	 * finish early and unlock the related inodes, some targets may get
	 * behind.  The sequencer ensures that ops on the same inode execute
	 * in the same order.
	 *
	 * The instant xop is queued another thread can pick it off.  In the
	 * case of asynchronous ops, another thread might even finish and
	 * deallocate it.
	 */
	hammer2_spin_ex(&pmp->xop_spin);
	nchains = xop->ip1->cluster.nchains;
	for (i = 0; i < nchains; ++i) {
		if (i != notidx) {
			atomic_set_int(&xop->run_mask, 1U << i);
			atomic_set_int(&xop->chk_mask, 1U << i);
			TAILQ_INSERT_TAIL(&pmp->xopq[i], xop, collect[i].entry);
		}
	}
	hammer2_spin_unex(&pmp->xop_spin);
	/* xop can become invalid at this point */

	/*
	 * Try to wakeup just one xop thread for each cluster node.
	 */
	for (i = 0; i < nchains; ++i) {
		if (i != notidx)
			wakeup_one(&pmp->xopq[i]);
	}
}

void
hammer2_xop_start(hammer2_xop_head_t *xop, hammer2_xop_func_t func)
{
	hammer2_xop_start_except(xop, func, -1);
}

/*
 * Retire a XOP.  Used by both the VOP frontend and by the XOP backend.
 */
void
hammer2_xop_retire(hammer2_xop_head_t *xop, uint32_t mask)
{
	hammer2_chain_t *chain;
	int i;

	/*
	 * Remove the frontend or remove a backend feeder.  When removing
	 * the frontend we must wakeup any backend feeders who are waiting
	 * for FIFO space.
	 *
	 * XXX optimize wakeup.
	 */
	KKASSERT(xop->run_mask & mask);
	if (atomic_fetchadd_int(&xop->run_mask, -mask) != mask) {
		if (mask == HAMMER2_XOPMASK_VOP)
			wakeup(xop);
		return;
	}

	/*
	 * Cleanup the collection cluster.
	 */
	for (i = 0; i < xop->cluster.nchains; ++i) {
		xop->cluster.array[i].flags = 0;
		chain = xop->cluster.array[i].chain;
		if (chain) {
			xop->cluster.array[i].chain = NULL;
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
		}
	}

	/*
	 * Cleanup the fifos, use check_counter to optimize the loop.
	 */
	mask = xop->chk_mask;
	for (i = 0; mask && i < HAMMER2_MAXCLUSTER; ++i) {
		hammer2_xop_fifo_t *fifo = &xop->collect[i];
		while (fifo->ri != fifo->wi) {
			chain = fifo->array[fifo->ri & HAMMER2_XOPFIFO_MASK];
			if (chain) {
				hammer2_chain_unlock(chain);
				hammer2_chain_drop(chain);
			}
			++fifo->ri;
			if (fifo->wi - fifo->ri < HAMMER2_XOPFIFO / 2)
				wakeup(xop);	/* XXX optimize */
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

	objcache_put(cache_xops, xop);
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
 * No xop lock is needed because we are only manipulating fields under
 * our direct control.
 *
 * Returns 0 on success and a hammer error code if sync is permanently
 * lost.  The caller retains a ref on the chain but by convention
 * the lock is typically inherited by the xop (caller loses lock).
 *
 * Returns non-zero on error.  In this situation the caller retains a
 * ref on the chain but loses the lock (we unlock here).
 *
 * WARNING!  The chain is moving between two different threads, it must
 *	     be locked SHARED to retain its data mapping, not exclusive.
 *	     When multiple operations are in progress at once, chains fed
 *	     back to the frontend for collection can wind up being locked
 *	     in different orders, only a shared lock can prevent a deadlock.
 *
 *	     Exclusive locks may only be used by a XOP backend node thread
 *	     temporarily, with no direct or indirect dependencies (aka
 *	     blocking/waiting) on other nodes.
 */
int
hammer2_xop_feed(hammer2_xop_head_t *xop, hammer2_chain_t *chain,
		 int clindex, int error)
{
	hammer2_xop_fifo_t *fifo;

	/*
	 * Multi-threaded entry into the XOP collector.  We own the
	 * fifo->wi for our clindex.
	 */
	fifo = &xop->collect[clindex];

	while (fifo->ri == fifo->wi - HAMMER2_XOPFIFO) {
		tsleep_interlock(xop, 0);
		if (hammer2_xop_active(xop) == 0) {
			error = EINTR;
			goto done;
		}
		if (fifo->ri == fifo->wi - HAMMER2_XOPFIFO) {
			tsleep(xop, PINTERLOCKED, "h2feed", hz*60);
		}
	}
	if (chain)
		hammer2_chain_ref(chain);
	fifo->errors[fifo->wi & HAMMER2_XOPFIFO_MASK] = error;
	fifo->array[fifo->wi & HAMMER2_XOPFIFO_MASK] = chain;
	cpu_sfence();
	++fifo->wi;
	atomic_add_int(&xop->check_counter, 1);
	wakeup(&xop->check_counter);	/* XXX optimize */
	error = 0;

	/*
	 * Cleanup.  If an error occurred we eat the lock.  If no error
	 * occurred the fifo inherits the lock and gains an additional ref.
	 *
	 * The caller's ref remains in both cases.
	 */
done:
	if (error && chain)
		hammer2_chain_unlock(chain);
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
 * HAMMER2_XOP_SYNCHRONIZER	- Reqeuest synchronization with a particular
 *				  cluster index, prevents looping when that
 *				  index is out of sync so caller can act on
 *				  the out of sync element.  ESRCH and EDEADLK
 *				  can be returned if this flag is specified.
 *
 * Returns 0 on success plus a filled out xop->cluster structure.
 * Return ENOENT on normal termination.
 * Otherwise return an error.
 */
int
hammer2_xop_collect(hammer2_xop_head_t *xop, int flags)
{
	hammer2_xop_fifo_t *fifo;
	hammer2_chain_t *chain;
	hammer2_key_t lokey;
	int error;
	int keynull;
	int adv;		/* advance the element */
	int i;
	uint32_t check_counter;

loop:
	/*
	 * First loop tries to advance pieces of the cluster which
	 * are out of sync.
	 */
	lokey = HAMMER2_KEY_MAX;
	keynull = HAMMER2_CHECK_NULL;
	check_counter = xop->check_counter;
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
		if (chain) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
		}
		fifo = &xop->collect[i];
		if (fifo->ri != fifo->wi) {
			cpu_lfence();
			chain = fifo->array[fifo->ri & HAMMER2_XOPFIFO_MASK];
			++fifo->ri;
			xop->cluster.array[i].chain = chain;
			if (chain == NULL) {
				/* XXX */
				xop->cluster.array[i].flags |=
							HAMMER2_CITEM_NULL;
			}
			if (fifo->wi - fifo->ri < HAMMER2_XOPFIFO / 2)
				wakeup(xop);	/* XXX optimize */
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
	 * requirements.  Returns:
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
	 */
	if ((flags & HAMMER2_XOP_COLLECT_WAITALL) &&
	    xop->run_mask != HAMMER2_XOPMASK_VOP) {
		error = EINPROGRESS;
	} else {
		error = hammer2_cluster_check(&xop->cluster, lokey, keynull);
	}
	if (error == EINPROGRESS) {
		if (xop->check_counter == check_counter) {
			if (flags & HAMMER2_XOP_COLLECT_NOWAIT)
				goto done;
			tsleep_interlock(&xop->check_counter, 0);
			cpu_lfence();
			if (xop->check_counter == check_counter) {
				tsleep(&xop->check_counter, PINTERLOCKED,
					"h2coll", hz*60);
			}
		}
		goto loop;
	}
	if (error == ESRCH) {
		if (lokey != HAMMER2_KEY_MAX) {
			xop->collect_key = lokey + 1;
			goto loop;
		}
		error = ENOENT;
	}
	if (error == EDEADLK) {
		kprintf("hammer2: no quorum possible lokey %016jx\n",
			lokey);
		if (lokey != HAMMER2_KEY_MAX) {
			xop->collect_key = lokey + 1;
			goto loop;
		}
		error = ENOENT;
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
 * index x M cluster nodes.  All the threads for any given cluster index
 * share and pull from the same xopq.
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
	TAILQ_FOREACH(xop, thr->xopq, collect[clindex].entry) {
		/*
		 * Check dependency
		 */
		if (xop_testhash(thr, xop->ip1, hash) ||
		    (xop->ip2 && xop_testhash(thr, xop->ip2, hash)) ||
		    (xop->ip3 && xop_testhash(thr, xop->ip3, hash))) {
			continue;
		}
		xop_sethash(thr, xop->ip1, hash);
		if (xop->ip2)
			xop_sethash(thr, xop->ip2, hash);
		if (xop->ip3)
			xop_sethash(thr, xop->ip3, hash);

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
	TAILQ_REMOVE(thr->xopq, xop, collect[clindex].entry);
	atomic_clear_int(&xop->collect[clindex].flags,
			 HAMMER2_XOP_FIFO_RUN);
	hammer2_spin_unex(&pmp->xop_spin);
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
	hammer2_pfs_t *pmp;
	hammer2_xop_head_t *xop;
	uint32_t mask;

	pmp = thr->pmp;
	/*xgrp = &pmp->xop_groups[thr->repidx]; not needed */
	mask = 1U << thr->clindex;

	lockmgr(&thr->lk, LK_EXCLUSIVE);
	while ((thr->flags & HAMMER2_THREAD_STOP) == 0) {
		/*
		 * Handle freeze request
		 */
		if (thr->flags & HAMMER2_THREAD_FREEZE) {
			atomic_set_int(&thr->flags, HAMMER2_THREAD_FROZEN);
			atomic_clear_int(&thr->flags, HAMMER2_THREAD_FREEZE);
		}

		/*
		 * Force idle if frozen until unfrozen or stopped.
		 */
		if (thr->flags & HAMMER2_THREAD_FROZEN) {
			lksleep(thr->xopq, &thr->lk, 0, "frozen", 0);
			continue;
		}

		/*
		 * Reset state on REMASTER request
		 */
		if (thr->flags & HAMMER2_THREAD_REMASTER) {
			atomic_clear_int(&thr->flags, HAMMER2_THREAD_REMASTER);
			/* reset state */
		}

		/*
		 * Process requests.  Each request can be multi-queued.
		 *
		 * If we get behind and the frontend VOP is no longer active,
		 * we retire the request without processing it.  The callback
		 * may also abort processing if the frontend VOP becomes
		 * inactive.
		 */
		tsleep_interlock(thr->xopq, 0);
		while ((xop = hammer2_xop_next(thr)) != NULL) {
			if (hammer2_xop_active(xop)) {
				lockmgr(&thr->lk, LK_RELEASE);
				xop->func((hammer2_xop_t *)xop, thr->clindex);
				hammer2_xop_dequeue(thr, xop);
				hammer2_xop_retire(xop, mask);
				lockmgr(&thr->lk, LK_EXCLUSIVE);
			} else {
				hammer2_xop_feed(xop, NULL, thr->clindex,
						 ECONNABORTED);
				hammer2_xop_dequeue(thr, xop);
				hammer2_xop_retire(xop, mask);
			}
		}

		/*
		 * Wait for event.  The xopq is not interlocked by thr->lk,
		 * use the tsleep interlock sequence.
		 *
		 * For robustness poll on a 30-second interval, but nominally
		 * expect to be woken up.
		 */
		lksleep(thr->xopq, &thr->lk, PINTERLOCKED, "h2idle", hz*30);
	}

#if 0
	/*
	 * Cleanup / termination
	 */
	while ((xop = TAILQ_FIRST(&thr->xopq)) != NULL) {
		kprintf("hammer2_thread: aborting xop %p\n", xop->func);
		TAILQ_REMOVE(&thr->xopq, xop,
			     collect[thr->clindex].entry);
		hammer2_xop_retire(xop, mask);
	}
#endif

	thr->td = NULL;
	wakeup(thr);
	lockmgr(&thr->lk, LK_RELEASE);
	/* thr structure can go invalid after this point */
}
