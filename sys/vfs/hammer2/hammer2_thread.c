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
 * This module implements various PFS-based helper threads.
 */
#include "hammer2.h"

typedef struct hammer2_deferred_ip {
	struct hammer2_deferred_ip *next;
	hammer2_inode_t	*ip;
} hammer2_deferred_ip_t;

typedef struct hammer2_deferred_list {
	hammer2_deferred_ip_t	*base;
	int			count;
} hammer2_deferred_list_t;


#define HAMMER2_THREAD_DEBUG 1

static int hammer2_sync_slaves(hammer2_thread_t *thr, hammer2_inode_t *ip,
				hammer2_deferred_list_t *list);
#if 0
static void hammer2_update_pfs_status(hammer2_thread_t *thr, uint32_t flags);
				nerror = hammer2_sync_insert(
						thr, &parent, &chain,
						focus->bref.modify_tid,
						idx, focus);
#endif
static int hammer2_sync_insert(hammer2_thread_t *thr,
			hammer2_chain_t **parentp, hammer2_chain_t **chainp,
			hammer2_tid_t modify_tid, int idx,
			hammer2_chain_t *focus);
static int hammer2_sync_destroy(hammer2_thread_t *thr,
			hammer2_chain_t **parentp, hammer2_chain_t **chainp,
			int idx);
static int hammer2_sync_replace(hammer2_thread_t *thr,
			hammer2_chain_t *parent, hammer2_chain_t *chain,
			hammer2_tid_t modify_tid, int idx,
			hammer2_chain_t *focus);

/****************************************************************************
 *			    HAMMER2 THREAD API			    	    *
 ****************************************************************************/
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
	thr->clindex = clindex;
	thr->repidx = repidx;
	TAILQ_INIT(&thr->xopq);
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
	wakeup(&thr->flags);
	while (thr->td) {
		lksleep(thr, &thr->lk, 0, "h2thr", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
	thr->pmp = NULL;
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
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_thr_freeze_async(hammer2_thread_t *thr)
{
	atomic_set_int(&thr->flags, HAMMER2_THREAD_FREEZE);
	wakeup(&thr->flags);
}

void
hammer2_thr_freeze(hammer2_thread_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_THREAD_FREEZE);
	wakeup(&thr->flags);
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
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

static
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
 *			    HAMMER2 SYNC THREADS 			    *
 ****************************************************************************/
/*
 * Primary management thread for an element of a node.  A thread will exist
 * for each element requiring management.
 *
 * No management threads are needed for the SPMP or for any PMP with only
 * a single MASTER.
 *
 * On the SPMP - handles bulkfree and dedup operations
 * On a PFS    - handles remastering and synchronization
 */
void
hammer2_primary_sync_thread(void *arg)
{
	hammer2_thread_t *thr = arg;
	hammer2_pfs_t *pmp;
	hammer2_deferred_list_t list;
	hammer2_deferred_ip_t *defer;
	int error;

	pmp = thr->pmp;
	bzero(&list, sizeof(list));

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
			lksleep(&thr->flags, &thr->lk, 0, "frozen", 0);
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
		 * Synchronization scan.
		 */
		kprintf("sync_slaves pfs %s clindex %d\n",
			pmp->pfs_names[thr->clindex], thr->clindex);
		hammer2_trans_init(pmp, 0);

		hammer2_inode_ref(pmp->iroot);

		for (;;) {
			int didbreak = 0;
			/* XXX lock synchronize pmp->modify_tid */
			error = hammer2_sync_slaves(thr, pmp->iroot, &list);
			if (error != EAGAIN)
				break;
			while ((defer = list.base) != NULL) {
				hammer2_inode_t *nip;

				nip = defer->ip;
				error = hammer2_sync_slaves(thr, nip, &list);
				if (error && error != EAGAIN)
					break;
				if (hammer2_thr_break(thr)) {
					didbreak = 1;
					break;
				}

				/*
				 * If no additional defers occurred we can
				 * remove this one, otherwrise keep it on
				 * the list and retry once the additional
				 * defers have completed.
				 */
				if (defer == list.base) {
					--list.count;
					list.base = defer->next;
					kfree(defer, M_HAMMER2);
					defer = NULL;	/* safety */
					hammer2_inode_drop(nip);
				}
			}

			/*
			 * If the thread is being remastered, frozen, or
			 * stopped, clean up any left-over deferals.
			 */
			if (didbreak || (error && error != EAGAIN)) {
				kprintf("didbreak\n");
				while ((defer = list.base) != NULL) {
					--list.count;
					hammer2_inode_drop(defer->ip);
					list.base = defer->next;
					kfree(defer, M_HAMMER2);
				}
				if (error == 0 || error == EAGAIN)
					error = EINPROGRESS;
				break;
			}
		}

		hammer2_inode_drop(pmp->iroot);
		hammer2_trans_done(pmp);

		if (error)
			kprintf("hammer2_sync_slaves: error %d\n", error);

		/*
		 * Wait for event, or 5-second poll.
		 */
		lksleep(&thr->flags, &thr->lk, 0, "h2idle", hz * 5);
	}
	thr->td = NULL;
	wakeup(thr);
	lockmgr(&thr->lk, LK_RELEASE);
	/* thr structure can go invalid after this point */
}

#if 0
/*
 * Given a locked cluster created from pmp->iroot, update the PFS's
 * reporting status.
 */
static
void
hammer2_update_pfs_status(hammer2_thread_t *thr, uint32_t flags)
{
	hammer2_pfs_t *pmp = thr->pmp;

	flags &= HAMMER2_CLUSTER_ZFLAGS;
	if (pmp->cluster_flags == flags)
		return;
	pmp->cluster_flags = flags;

	kprintf("pfs %p", pmp);
	if (flags & HAMMER2_CLUSTER_MSYNCED)
		kprintf(" masters-all-good");
	if (flags & HAMMER2_CLUSTER_SSYNCED)
		kprintf(" slaves-all-good");

	if (flags & HAMMER2_CLUSTER_WRHARD)
		kprintf(" quorum/rw");
	else if (flags & HAMMER2_CLUSTER_RDHARD)
		kprintf(" quorum/ro");

	if (flags & HAMMER2_CLUSTER_UNHARD)
		kprintf(" out-of-sync-masters");
	else if (flags & HAMMER2_CLUSTER_NOHARD)
		kprintf(" no-masters-visible");

	if (flags & HAMMER2_CLUSTER_WRSOFT)
		kprintf(" soft/rw");
	else if (flags & HAMMER2_CLUSTER_RDSOFT)
		kprintf(" soft/ro");

	if (flags & HAMMER2_CLUSTER_UNSOFT)
		kprintf(" out-of-sync-slaves");
	else if (flags & HAMMER2_CLUSTER_NOSOFT)
		kprintf(" no-slaves-visible");
	kprintf("\n");
}
#endif

#if 0
static
void
dumpcluster(const char *label,
	    hammer2_cluster_t *cparent, hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	if ((hammer2_debug & 1) == 0)
		return;

	kprintf("%s\t", label);
	KKASSERT(cparent->nchains == cluster->nchains);
	for (i = 0; i < cparent->nchains; ++i) {
		if (i)
			kprintf("\t");
		kprintf("%d ", i);
		if ((chain = cparent->array[i].chain) != NULL) {
			kprintf("%016jx%s ",
				chain->bref.key,
				((cparent->array[i].flags &
				  HAMMER2_CITEM_INVALID) ? "(I)" : "   ")
			);
		} else {
			kprintf("      NULL      %s ", "   ");
		}
		if ((chain = cluster->array[i].chain) != NULL) {
			kprintf("%016jx%s ",
				chain->bref.key,
				((cluster->array[i].flags &
				  HAMMER2_CITEM_INVALID) ? "(I)" : "   ")
			);
		} else {
			kprintf("      NULL      %s ", "   ");
		}
		kprintf("\n");
	}
}
#endif

/*
 * Each out of sync node sync-thread must issue an all-nodes XOP scan of
 * the inode.  This creates a multiplication effect since the XOP scan itself
 * issues to all nodes.  However, this is the only way we can safely
 * synchronize nodes which might have disparate I/O bandwidths and the only
 * way we can safely deal with stalled nodes.
 */
static
int
hammer2_sync_slaves(hammer2_thread_t *thr, hammer2_inode_t *ip,
		    hammer2_deferred_list_t *list)
{
	hammer2_xop_scanall_t *xop;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_next;
	hammer2_tid_t sync_tid;
	int cache_index = -1;
	int needrescan;
	int wantupdate;
	int error;
	int nerror;
	int idx;
	int n;

	pmp = ip->pmp;
	idx = thr->clindex;	/* cluster node we are responsible for */
	needrescan = 0;
	wantupdate = 0;

	if (ip->cluster.focus == NULL)
		return (EINPROGRESS);
	sync_tid = ip->cluster.focus->bref.modify_tid;

#if 0
	/*
	 * Nothing to do if all slaves are synchronized.
	 * Nothing to do if cluster not authoritatively readable.
	 */
	if (pmp->cluster_flags & HAMMER2_CLUSTER_SSYNCED)
		return(0);
	if ((pmp->cluster_flags & HAMMER2_CLUSTER_RDHARD) == 0)
		return(HAMMER2_ERROR_INCOMPLETE);
#endif

	error = 0;

	/*
	 * The inode is left unlocked during the scan.  Issue a XOP
	 * that does *not* include our cluster index to iterate
	 * properly synchronized elements and resolve our cluster index
	 * against it.
	 */
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
	xop = &hammer2_xop_alloc(ip)->xop_scanall;
	xop->key_beg = HAMMER2_KEY_MIN;
	xop->key_end = HAMMER2_KEY_MAX;
	hammer2_xop_start_except(&xop->head, hammer2_xop_scanall, idx);
	parent = hammer2_inode_chain(ip, idx,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent->bref.modify_tid != sync_tid)
		wantupdate = 1;

	hammer2_inode_unlock(ip);

	chain = hammer2_chain_lookup(&parent, &key_next,
				     HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
				     &cache_index,
				     HAMMER2_LOOKUP_SHARED |
				     HAMMER2_LOOKUP_NODIRECT |
				     HAMMER2_LOOKUP_NODATA);
	error = hammer2_xop_collect(&xop->head, 0);
	kprintf("XOP_INITIAL xop=%p clindex %d on %s\n", xop, thr->clindex,
		pmp->pfs_names[thr->clindex]);

	for (;;) {
		/*
		 * We are done if our scan is done and the XOP scan is done.
		 * We are done if the XOP scan failed (that is, we don't
		 * have authoritative data to synchronize with).
		 */
		int advance_local = 0;
		int advance_xop = 0;
		int dodefer = 0;
		hammer2_chain_t *focus;

		kprintf("loop xop=%p chain[1]=%p lockcnt=%d\n",
			xop, xop->head.cluster.array[1].chain,
			(xop->head.cluster.array[1].chain ?
			    xop->head.cluster.array[1].chain->lockcnt : -1)
			);

		if (chain == NULL && error == ENOENT)
			break;
		if (error && error != ENOENT)
			break;

		/*
		 * Compare
		 */
		if (chain && error == ENOENT) {
			/*
			 * If we have local chains but the XOP scan is done,
			 * the chains need to be deleted.
			 */
			n = -1;
			focus = NULL;
		} else if (chain == NULL) {
			/*
			 * If our local scan is done but the XOP scan is not,
			 * we need to create the missing chain(s).
			 */
			n = 1;
			focus = xop->head.cluster.focus;
		} else {
			/*
			 * Otherwise compare to determine the action
			 * needed.
			 */
			focus = xop->head.cluster.focus;
			n = hammer2_chain_cmp(chain, focus);
		}

		/*
		 * Take action based on comparison results.
		 */
		if (n < 0) {
			/*
			 * Delete extranious local data.  This will
			 * automatically advance the chain.
			 */
			nerror = hammer2_sync_destroy(thr, &parent, &chain,
						      idx);
		} else if (n == 0 && chain->bref.modify_tid !=
				     focus->bref.modify_tid) {
			/*
			 * Matching key but local data or meta-data requires
			 * updating.  If we will recurse, we still need to
			 * update to compatible content first but we do not
			 * synchronize modify_tid until the entire recursion
			 * has completed successfully.
			 */
			if (focus->bref.type == HAMMER2_BREF_TYPE_INODE) {
				nerror = hammer2_sync_replace(
						thr, parent, chain,
						0,
						idx, focus);
				dodefer = 1;
			} else {
				nerror = hammer2_sync_replace(
						thr, parent, chain,
						focus->bref.modify_tid,
						idx, focus);
			}
		} else if (n == 0) {
			/*
			 * 100% match, advance both
			 */
			advance_local = 1;
			advance_xop = 1;
			nerror = 0;
		} else if (n > 0) {
			/*
			 * Insert missing local data.
			 *
			 * If we will recurse, we still need to update to
			 * compatible content first but we do not synchronize
			 * modify_tid until the entire recursion has
			 * completed successfully.
			 */
			if (focus->bref.type == HAMMER2_BREF_TYPE_INODE) {
				nerror = hammer2_sync_insert(
						thr, &parent, &chain,
						0,
						idx, focus);
				dodefer = 2;
			} else {
				nerror = hammer2_sync_insert(
						thr, &parent, &chain,
						focus->bref.modify_tid,
						idx, focus);
			}
			advance_local = 1;
			advance_xop = 1;
		}

		/*
		 * We cannot recurse depth-first because the XOP is still
		 * running in node threads for this scan.  Create a placemarker
		 * by obtaining and record the hammer2_inode.
		 *
		 * We excluded our node from the XOP so we must temporarily
		 * add it to xop->head.cluster so it is properly incorporated
		 * into the inode.
		 *
		 * The deferral is pushed onto a LIFO list for bottom-up
		 * synchronization.
		 */
		if (error == 0 && dodefer) {
			hammer2_inode_t *nip;
			hammer2_deferred_ip_t *defer;

			KKASSERT(focus->bref.type == HAMMER2_BREF_TYPE_INODE);

			defer = kmalloc(sizeof(*defer), M_HAMMER2,
					M_WAITOK | M_ZERO);
			KKASSERT(xop->head.cluster.array[idx].chain == NULL);
			xop->head.cluster.array[idx].flags =
							HAMMER2_CITEM_INVALID;
			xop->head.cluster.array[idx].chain = chain;
			nip = hammer2_inode_get(pmp, ip,
						&xop->head.cluster, idx);
			xop->head.cluster.array[idx].chain = NULL;

			hammer2_inode_ref(nip);
			hammer2_inode_unlock(nip);

			defer->next = list->base;
			defer->ip = nip;
			list->base = defer;
			++list->count;
			needrescan = 1;
		}

		/*
		 * If at least one deferral was added and the deferral
		 * list has grown too large, stop adding more.  This
		 * will trigger an EAGAIN return.
		 */
		if (needrescan && list->count > 1000)
			break;

		/*
		 * Advancements for iteration.
		 */
		if (advance_xop) {
			error = hammer2_xop_collect(&xop->head, 0);
		}
		if (advance_local) {
			chain = hammer2_chain_next(&parent, chain, &key_next,
						   key_next, HAMMER2_KEY_MAX,
						   &cache_index,
						   HAMMER2_LOOKUP_SHARED |
						   HAMMER2_LOOKUP_NODIRECT |
						   HAMMER2_LOOKUP_NODATA);
		}
	}
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}

	/*
	 * If we added deferrals we want the caller to synchronize them
	 * and then call us again.
	 *
	 * NOTE: In this situation we do not yet want to synchronize our
	 *	 inode, setting the error code also has that effect.
	 */
	if (error == 0 && needrescan)
		error = EAGAIN;

	/*
	 * If no error occurred and work was performed, synchronize the
	 * inode meta-data itself.
	 *
	 * XXX inode lock was lost
	 */
	if (error == 0 && wantupdate) {
		hammer2_xop_ipcluster_t *xop2;
		hammer2_chain_t *focus;

		xop2 = &hammer2_xop_alloc(ip)->xop_ipcluster;
		hammer2_xop_start_except(&xop2->head, hammer2_xop_ipcluster,
					 idx);
		error = hammer2_xop_collect(&xop2->head, 0);
		if (error == 0) {
			focus = xop2->head.cluster.focus;
			kprintf("syncthr: update inode %p (%s)\n",
				focus,
				(focus ?
				 (char *)focus->data->ipdata.filename : "?"));
			chain = hammer2_inode_chain_and_parent(ip, idx,
						    &parent,
						    HAMMER2_RESOLVE_ALWAYS |
						    HAMMER2_RESOLVE_SHARED);

			KKASSERT(parent != NULL);
			nerror = hammer2_sync_replace(
					thr, parent, chain,
					sync_tid,
					idx, focus);
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
			/* XXX */
		}
		hammer2_xop_retire(&xop2->head, HAMMER2_XOPMASK_VOP);
	}

	return error;
}

/*
 * Create a missing chain by copying the focus from another device.
 *
 * On entry *parentp and focus are both locked shared.  The chain will be
 * created and returned in *chainp also locked shared.
 */
static
int
hammer2_sync_insert(hammer2_thread_t *thr,
		    hammer2_chain_t **parentp, hammer2_chain_t **chainp,
		    hammer2_tid_t modify_tid, int idx,
		    hammer2_chain_t *focus)
{
	hammer2_chain_t *chain;

#if HAMMER2_THREAD_DEBUG
	if (hammer2_debug & 1)
	kprintf("insert rec par=%p/%d.%016jx slave %d %d.%016jx mod=%016jx\n",
		*parentp, 
		(*parentp)->bref.type,
		(*parentp)->bref.key,
		idx,
		focus->bref.type, focus->bref.key, modify_tid);
#endif

	/*
	 * Create the missing chain.  Exclusive locks are needed.
	 *
	 * Have to be careful to avoid deadlocks.
	 */
	if (*chainp)
		hammer2_chain_unlock(*chainp);
	hammer2_chain_unlock(*parentp);
	hammer2_chain_lock(*parentp, HAMMER2_RESOLVE_ALWAYS);
	/* reissue lookup? */

	chain = NULL;
	hammer2_chain_create(parentp, &chain, thr->pmp,
			     focus->bref.key, focus->bref.keybits,
			     focus->bref.type, focus->bytes,
			     0);
	hammer2_chain_modify(chain, HAMMER2_MODIFY_KEEPMODIFY);

	/*
	 * Copy focus to new chain
	 */

	/* type already set */
	chain->bref.methods = focus->bref.methods;
	/* keybits already set */
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid set by flush */
	chain->bref.modify_tid = modify_tid;
	chain->bref.flags = focus->bref.flags;
	/* key already present */
	/* check code will be recalculated */

	/*
	 * Copy data body.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		if ((focus->data->ipdata.meta.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			bcopy(focus->data, chain->data,
			      offsetof(hammer2_inode_data_t, u));
			break;
		}
		/* fall through */
	case HAMMER2_BREF_TYPE_DATA:
		bcopy(focus->data, chain->data, chain->bytes);
		hammer2_chain_setcheck(chain, chain->data);
		break;
	default:
		KKASSERT(0);
		break;
	}

	hammer2_chain_unlock(chain);		/* unlock, leave ref */
	if (*chainp)
		hammer2_chain_drop(*chainp);
	*chainp = chain;			/* will be returned locked */

	/*
	 * Avoid ordering deadlock when relocking.
	 */
	hammer2_chain_unlock(*parentp);
	hammer2_chain_lock(*parentp, HAMMER2_RESOLVE_SHARED |
				     HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_SHARED |
				  HAMMER2_RESOLVE_ALWAYS);

	return 0;
}

/*
 * Destroy an extranious chain.
 *
 * Both *parentp and *chainp are locked shared.
 *
 * On return, *chainp will be adjusted to point to the next element in the
 * iteration and locked shared.
 */
static
int
hammer2_sync_destroy(hammer2_thread_t *thr,
		     hammer2_chain_t **parentp, hammer2_chain_t **chainp,
		     int idx)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_key_t key_next;
	hammer2_key_t save_key;
	int cache_index = -1;

	chain = *chainp;

#if HAMMER2_THREAD_DEBUG
	if (hammer2_debug & 1)
	kprintf("destroy rec %p/%p slave %d %d.%016jx\n",
		*parentp, chain,
		idx, chain->bref.type, chain->bref.key);
#endif

	save_key = chain->bref.key;
	if (save_key != HAMMER2_KEY_MAX)
		++save_key;

	/*
	 * Try to avoid unnecessary I/O.
	 *
	 * XXX accounting not propagated up properly.  We might have to do
	 *     a RESOLVE_MAYBE here and pass 0 for the flags.
	 */
	hammer2_chain_unlock(chain);	/* relock exclusive */
	hammer2_chain_unlock(*parentp);
	hammer2_chain_lock(*parentp, HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER);

	hammer2_chain_delete(*parentp, chain, HAMMER2_DELETE_PERMANENT);
	hammer2_chain_unlock(chain);
	hammer2_chain_drop(chain);
	chain = NULL;			/* safety */

	hammer2_chain_unlock(*parentp);	/* relock shared */
	hammer2_chain_lock(*parentp, HAMMER2_RESOLVE_SHARED |
				     HAMMER2_RESOLVE_ALWAYS);
	*chainp = hammer2_chain_lookup(&parent, &key_next,
				     save_key, HAMMER2_KEY_MAX,
				     &cache_index,
				     HAMMER2_LOOKUP_SHARED |
				     HAMMER2_LOOKUP_NODIRECT |
				     HAMMER2_LOOKUP_NODATA);
	return 0;
}

/*
 * cparent is locked exclusively, with an extra ref, cluster is not locked.
 * Replace element [i] in the cluster.
 */
static
int
hammer2_sync_replace(hammer2_thread_t *thr,
		     hammer2_chain_t *parent, hammer2_chain_t *chain,
		     hammer2_tid_t modify_tid, int idx,
		     hammer2_chain_t *focus)
{
	int nradix;
	uint8_t otype;

#if HAMMER2_THREAD_DEBUG
	if (hammer2_debug & 1)
	kprintf("replace rec %p slave %d %d.%016jx mod=%016jx\n",
		chain,
		idx,
		focus->bref.type, focus->bref.key, modify_tid);
#endif
	hammer2_chain_unlock(chain);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
	if (chain->bytes != focus->bytes) {
		/* XXX what if compressed? */
		nradix = hammer2_getradix(chain->bytes);
		hammer2_chain_resize(NULL, parent, chain, nradix, 0);
	}
	hammer2_chain_modify(chain, HAMMER2_MODIFY_KEEPMODIFY);
	otype = chain->bref.type;
	chain->bref.type = focus->bref.type;
	chain->bref.methods = focus->bref.methods;
	chain->bref.keybits = focus->bref.keybits;
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid updated by flush */
	chain->bref.modify_tid = modify_tid;
	chain->bref.flags = focus->bref.flags;
	/* key already present */
	/* check code will be recalculated */
	chain->error = 0;

	/*
	 * Copy data body.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		if ((focus->data->ipdata.meta.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			/*
			 * If DIRECTDATA is transitioning to 0 or the old
			 * chain is not an inode we have to initialize
			 * the block table.
			 */
			if (otype != HAMMER2_BREF_TYPE_INODE ||
			    (chain->data->ipdata.meta.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA)) {
				kprintf("chain inode trans away from dd\n");
				bzero(&chain->data->ipdata.u,
				      sizeof(chain->data->ipdata.u));
			}
			bcopy(focus->data, chain->data,
			      offsetof(hammer2_inode_data_t, u));
			/* XXX setcheck on inode should not be needed */
			hammer2_chain_setcheck(chain, chain->data);
			break;
		}
		/* fall through */
	case HAMMER2_BREF_TYPE_DATA:
		bcopy(focus->data, chain->data, chain->bytes);
		hammer2_chain_setcheck(chain, chain->data);
		break;
	default:
		KKASSERT(0);
		break;
	}

	hammer2_chain_unlock(chain);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_SHARED |
				  HAMMER2_RESOLVE_MAYBE);

	return 0;
}

/****************************************************************************
 *			    HAMMER2 XOPS THREADS 			    *
 ****************************************************************************/

void
hammer2_xop_group_init(hammer2_pfs_t *pmp, hammer2_xop_group_t *xgrp)
{
	hammer2_mtx_init(&xgrp->mtx, "h2xopq");
	hammer2_mtx_init(&xgrp->mtx2, "h2xopio");
}

/*
 * Allocate a XOP request.
 *
 * Once allocated a XOP request can be started, collected, and retired,
 * and can be retired early if desired.
 *
 * NOTE: Fifo indices might not be zero but ri == wi on objcache_get().
 */
hammer2_xop_t *
hammer2_xop_alloc(hammer2_inode_t *ip)
{
	hammer2_xop_t *xop;

	xop = objcache_get(cache_xops, M_WAITOK);
	KKASSERT(xop->head.cluster.array[0].chain == NULL);
	xop->head.ip = ip;
	xop->head.func = NULL;
	xop->head.state = 0;
	xop->head.error = 0;
	xop->head.collect_key = 0;

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
	xop->name = kmalloc(name_len + 1, M_HAMMER2, M_WAITOK | M_ZERO);
	xop->name_len = name_len;
	bcopy(name, xop->name, name_len);
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
	hammer2_xop_group_t *xgrp;
	hammer2_thread_t *thr;
	hammer2_pfs_t *pmp;
	int g;
	int i;

	pmp = xop->ip->pmp;
	if (pmp->has_xop_threads == 0)
		hammer2_xop_helper_create(pmp);

	g = pmp->xop_iterator++;
	g = g & HAMMER2_XOPGROUPS_MASK;
	xgrp = &pmp->xop_groups[g];
	xop->func = func;
	xop->xgrp = xgrp;

	/* XXX do cluster_resolve or cluster_check here, only start
	 * synchronized elements */

	for (i = 0; i < xop->ip->cluster.nchains; ++i) {
		thr = &xgrp->thrs[i];
		if (thr->td && i != notidx) {
			lockmgr(&thr->lk, LK_EXCLUSIVE);
			if (thr->td &&
			    (thr->flags & HAMMER2_THREAD_STOP) == 0) {
				atomic_set_int(&xop->run_mask, 1U << i);
				atomic_set_int(&xop->chk_mask, 1U << i);
				TAILQ_INSERT_TAIL(&thr->xopq, xop,
						  collect[i].entry);
			}
			lockmgr(&thr->lk, LK_RELEASE);
			wakeup(&thr->flags);
		}
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
	hammer2_xop_group_t *xgrp;
	hammer2_chain_t *chain;
	int i;

	xgrp = xop->xgrp;

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
	if (xop->ip) {
		hammer2_inode_drop(xop->ip);
		xop->ip = NULL;
	}
	if (xop->ip2) {
		hammer2_inode_drop(xop->ip2);
		xop->ip2 = NULL;
	}
	if (xop->ip3) {
		hammer2_inode_drop(xop->ip3);
		xop->ip3 = NULL;
	}
	if (xop->name) {
		kfree(xop->name, M_HAMMER2);
		xop->name = NULL;
		xop->name_len = 0;
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
	hammer2_xop_group_t *xgrp;
	uint32_t mask;

	pmp = thr->pmp;
	xgrp = &pmp->xop_groups[thr->repidx];
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
			lksleep(&thr->flags, &thr->lk, 0, "frozen", 0);
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
		while ((xop = TAILQ_FIRST(&thr->xopq)) != NULL) {
			TAILQ_REMOVE(&thr->xopq, xop,
				     collect[thr->clindex].entry);
			if (hammer2_xop_active(xop)) {
				lockmgr(&thr->lk, LK_RELEASE);
				xop->func((hammer2_xop_t *)xop, thr->clindex);
				hammer2_xop_retire(xop, mask);
				lockmgr(&thr->lk, LK_EXCLUSIVE);
			} else {
				hammer2_xop_feed(xop, NULL, thr->clindex,
						 ECONNABORTED);
				hammer2_xop_retire(xop, mask);
			}
		}

		/*
		 * Wait for event.
		 */
		lksleep(&thr->flags, &thr->lk, 0, "h2idle", 0);
	}

	/*
	 * Cleanup / termination
	 */
	while ((xop = TAILQ_FIRST(&thr->xopq)) != NULL) {
		kprintf("hammer2_thread: aborting xop %p\n", xop->func);
		TAILQ_REMOVE(&thr->xopq, xop,
			     collect[thr->clindex].entry);
		hammer2_xop_retire(xop, mask);
	}

	thr->td = NULL;
	wakeup(thr);
	lockmgr(&thr->lk, LK_RELEASE);
	/* thr structure can go invalid after this point */
}
