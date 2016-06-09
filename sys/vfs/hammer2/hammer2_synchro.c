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
 * This module implements the cluster synchronizer.  Basically the way
 * it works is that a thread is created for each cluster node in a PFS.
 * This thread is responsible for synchronizing the current node using
 * data from other nodes.
 *
 * Any out of sync master or slave can get back into synchronization as
 * long as a quorum of masters agree on the update_tid.  If a quorum is
 * not available it may still be possible to synchronize to the highest
 * available update_tid as a way of trying to catch up as much as possible
 * until a quorum is available.
 *
 * If no quorum is possible (which can happen even if all masters are
 * available, if the update_tid does not match), then manual intervention
 * may be required to resolve discrepancies.
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


#define HAMMER2_SYNCHRO_DEBUG 1

static int hammer2_sync_slaves(hammer2_thread_t *thr, hammer2_inode_t *ip,
				hammer2_deferred_list_t *list, int isroot);
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
			hammer2_tid_t mtid, int idx);
static int hammer2_sync_replace(hammer2_thread_t *thr,
			hammer2_chain_t *parent, hammer2_chain_t *chain,
			hammer2_tid_t mtid, int idx,
			hammer2_chain_t *focus, int isroot);

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
	uint32_t flags;
	uint32_t nflags;

	pmp = thr->pmp;
	bzero(&list, sizeof(list));

	for (;;) {
		flags = thr->flags;
		cpu_ccfence();

		/*
		 * Handle stop request
		 */
		if (flags & HAMMER2_THREAD_STOP)
			break;

		/*
		 * Handle freeze request
		 */
		if (flags & HAMMER2_THREAD_FREEZE) {
			nflags = (flags & ~(HAMMER2_THREAD_FREEZE |
					    HAMMER2_THREAD_CLIENTWAIT)) |
				 HAMMER2_THREAD_FROZEN;
			if (!atomic_cmpset_int(&thr->flags, flags, nflags))
				continue;
			if (flags & HAMMER2_THREAD_CLIENTWAIT)
				wakeup(&thr->flags);
			flags = nflags;
			/* fall through */
		}

		if (flags & HAMMER2_THREAD_UNFREEZE) {
			nflags = flags & ~(HAMMER2_THREAD_UNFREEZE |
					   HAMMER2_THREAD_FROZEN |
					   HAMMER2_THREAD_CLIENTWAIT);
			if (!atomic_cmpset_int(&thr->flags, flags, nflags))
				continue;
			if (flags & HAMMER2_THREAD_CLIENTWAIT)
				wakeup(&thr->flags);
			flags = nflags;
			/* fall through */
		}

		/*
		 * Force idle if frozen until unfrozen or stopped.
		 */
		if (flags & HAMMER2_THREAD_FROZEN) {
			nflags = flags | HAMMER2_THREAD_WAITING;
			tsleep_interlock(&thr->flags, 0);
			if (atomic_cmpset_int(&thr->flags, flags, nflags)) {
				tsleep(&thr->flags, PINTERLOCKED, "frozen", 0);
				atomic_clear_int(&thr->flags,
						 HAMMER2_THREAD_WAITING);
			}
			continue;
		}

		/*
		 * Reset state on REMASTER request
		 */
		if (thr->flags & HAMMER2_THREAD_REMASTER) {
			nflags = flags & ~HAMMER2_THREAD_REMASTER;
			if (atomic_cmpset_int(&thr->flags, flags, nflags)) {
				/* reset state here */
			}
			continue;
		}

		/*
		 * Synchronization scan.
		 */
		if (hammer2_debug & 0x8000)
			kprintf("sync_slaves pfs %s clindex %d\n",
				pmp->pfs_names[thr->clindex], thr->clindex);
		hammer2_trans_init(pmp, 0);

		hammer2_inode_ref(pmp->iroot);

		for (;;) {
			int didbreak = 0;
			/* XXX lock synchronize pmp->modify_tid */
			error = hammer2_sync_slaves(thr, pmp->iroot, &list, 1);
			if (hammer2_debug & 0x8000) {
				kprintf("sync_slaves error %d defer %p\n",
					error, list.base);
			}
			if (error != EAGAIN)
				break;
			while ((defer = list.base) != NULL) {
				hammer2_inode_t *nip;

				nip = defer->ip;
				error = hammer2_sync_slaves(thr, nip, &list, 0);
				if (error && error != EAGAIN && error != ENOENT)
					break;
				if (hammer2_thr_break(thr)) {
					didbreak = 1;
					break;
				}

				/*
				 * If no additional defers occurred we can
				 * remove this one, otherwise keep it on
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

		if (error && error != EINPROGRESS)
			kprintf("hammer2_sync_slaves: error %d\n", error);

		/*
		 * Wait for event, or 5-second poll.
		 */
		nflags = flags | HAMMER2_THREAD_WAITING;
		tsleep_interlock(&thr->flags, 0);
		if (atomic_cmpset_int(&thr->flags, flags, nflags)) {
			tsleep(&thr->flags, 0, "h2idle", hz * 5);
			atomic_clear_int(&thr->flags, HAMMER2_THREAD_WAITING);
		}
	}
	thr->td = NULL;
	hammer2_thr_return(thr, HAMMER2_THREAD_STOPPED);
	/* thr structure can go invalid after this point */
	wakeup(thr);
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
		    hammer2_deferred_list_t *list, int isroot)
{
	hammer2_xop_scanall_t *xop;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_next;
	hammer2_tid_t sync_tid;
	int cache_index = -1;
	int needrescan;
	int want_update;
	int error;
	int nerror;
	int idx;
	int n;

	pmp = ip->pmp;
	idx = thr->clindex;	/* cluster node we are responsible for */
	needrescan = 0;
	want_update = 0;
	sync_tid = 0;
	chain = NULL;
	parent = NULL;

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
	 * Resolve the root inode of the PFS and determine if synchronization
	 * is needed by checking modify_tid.
	 */
	{
		hammer2_xop_ipcluster_t *xop2;
		hammer2_chain_t *focus;

		hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
		xop2 = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		hammer2_xop_start_except(&xop2->head, hammer2_xop_ipcluster,
					 idx);
		hammer2_inode_unlock(ip);
		error = hammer2_xop_collect(&xop2->head, 0);
		if (error == 0 && (focus = xop2->head.cluster.focus) != NULL) {
			sync_tid = focus->bref.modify_tid; /* XXX */
			chain = hammer2_inode_chain_and_parent(ip, idx,
						    &parent,
						    HAMMER2_RESOLVE_ALWAYS |
						    HAMMER2_RESOLVE_SHARED);
			want_update = (chain->bref.modify_tid != sync_tid);
			if (chain) {
				hammer2_chain_unlock(chain);
				hammer2_chain_drop(chain);
				chain = NULL;
			}
			if (parent) {
				hammer2_chain_unlock(parent);
				hammer2_chain_drop(parent);
				parent = NULL;
			}
		}
		hammer2_xop_retire(&xop2->head, HAMMER2_XOPMASK_VOP);
	}

	if (want_update == 0)
		return(0);

	/*
	 * The inode is left unlocked during the scan.  Issue a XOP
	 * that does *not* include our cluster index to iterate
	 * properly synchronized elements and resolve our cluster index
	 * against it.
	 */
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
	xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
	xop->key_beg = HAMMER2_KEY_MIN;
	xop->key_end = HAMMER2_KEY_MAX;
	xop->resolve_flags = HAMMER2_RESOLVE_SHARED |
			     HAMMER2_RESOLVE_ALWAYS;
	xop->lookup_flags = HAMMER2_LOOKUP_SHARED |
			    HAMMER2_LOOKUP_NODIRECT |
			    HAMMER2_LOOKUP_ALWAYS;
	hammer2_xop_start_except(&xop->head, hammer2_xop_scanall, idx);
	parent = hammer2_inode_chain(ip, idx,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	hammer2_inode_unlock(ip);

	chain = hammer2_chain_lookup(&parent, &key_next,
				     HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
				     &cache_index,
				     HAMMER2_LOOKUP_SHARED |
				     HAMMER2_LOOKUP_NODIRECT |
				     HAMMER2_LOOKUP_NODATA);
	error = hammer2_xop_collect(&xop->head, 0);
	kprintf("START_SCAN IP=%016jx chain=%p (%016jx)\n",
		ip->meta.name_key, chain,
		(chain ? chain->bref.key : -1));

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
						      0, idx);
		} else if (n == 0 && chain->bref.modify_tid !=
				     focus->bref.modify_tid) {
			/*
			 * Matching key but local data or meta-data requires
			 * updating.  If we will recurse, we still need to
			 * update to compatible content first but we do not
			 * synchronize modify_tid until the entire recursion
			 * has completed successfully.
			 *
			 * NOTE: Do not try to access hardlink pointers as if
			 *	 they were normal inodes, the inode cache will
			 *	 get seriously confused.
			 */
			if (focus->bref.type == HAMMER2_BREF_TYPE_INODE &&
			    focus->data->ipdata.meta.type !=
			    HAMMER2_OBJTYPE_HARDLINK) {
				nerror = hammer2_sync_replace(
						thr, parent, chain,
						0,
						idx, focus, 0);
				dodefer = 1;
			} else {
				nerror = hammer2_sync_replace(
						thr, parent, chain,
						focus->bref.modify_tid,
						idx, focus, 0);
			}
			advance_local = 1;
			advance_xop = 1;
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
			 *
			 * NOTE: Do not try to access hardlink pointers as if
			 *	 they were normal inodes, the inode cache will
			 *	 get seriously confused.
			 */
			if (focus->bref.type == HAMMER2_BREF_TYPE_INODE &&
			    focus->data->ipdata.meta.type !=
			    HAMMER2_OBJTYPE_HARDLINK) {
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
	if ((error == 0 || error == ENOENT) && needrescan)
		error = EAGAIN;

	/*
	 * If no error occurred we can synchronize the inode meta-data
	 * and modify_tid.  Only limited changes are made to PFSROOTs.
	 *
	 * XXX inode lock was lost
	 */
	if (error == 0 || error == ENOENT) {
		hammer2_xop_ipcluster_t *xop2;
		hammer2_chain_t *focus;

		hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
		xop2 = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		hammer2_xop_start_except(&xop2->head, hammer2_xop_ipcluster,
					 idx);
		hammer2_inode_unlock(ip);
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
					idx, focus, isroot);
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
		    hammer2_tid_t mtid, int idx, hammer2_chain_t *focus)
{
	hammer2_chain_t *chain;
	hammer2_key_t dummy;
	int cache_index = -1;

#if HAMMER2_SYNCHRO_DEBUG
	if (hammer2_debug & 1)
	kprintf("insert rec par=%p/%d.%016jx slave %d %d.%016jx mod=%016jx\n",
		*parentp, 
		(*parentp)->bref.type,
		(*parentp)->bref.key,
		idx,
		focus->bref.type, focus->bref.key, mtid);
#endif

	/*
	 * Parent requires an exclusive lock for the insertion.
	 * We must unlock the child to avoid deadlocks while
	 * relocking the parent.
	 */
	if (*chainp) {
		hammer2_chain_unlock(*chainp);
		hammer2_chain_drop(*chainp);
		*chainp = NULL;
	}
	hammer2_chain_unlock(*parentp);
	hammer2_chain_lock(*parentp, HAMMER2_RESOLVE_ALWAYS);

	/*
	 * We must reissue the lookup to properly position (*parentp)
	 * for the insertion.
	 */
	chain = hammer2_chain_lookup(parentp, &dummy,
				     focus->bref.key, focus->bref.key,
				     &cache_index,
				     HAMMER2_LOOKUP_NODIRECT |
				     HAMMER2_LOOKUP_ALWAYS);
	KKASSERT(chain == NULL);

	chain = NULL;
	hammer2_chain_create(parentp, &chain, thr->pmp,
			     focus->bref.key, focus->bref.keybits,
			     focus->bref.type, focus->bytes,
			     mtid, 0, 0);
	hammer2_chain_modify(chain, mtid, 0, 0);

	/*
	 * Copy focus to new chain
	 */

	/* type already set */
	chain->bref.methods = focus->bref.methods;
	/* keybits already set */
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid set by flush */
	KKASSERT(chain->bref.modify_tid == mtid);
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
			/* do not copy block table */
			bcopy(focus->data, chain->data,
			      offsetof(hammer2_inode_data_t, u));
			break;
		}
		/* fall through copy whole thing */
	case HAMMER2_BREF_TYPE_DATA:
		bcopy(focus->data, chain->data, chain->bytes);
		hammer2_chain_setcheck(chain, chain->data);
		break;
	default:
		KKASSERT(0);
		break;
	}

	hammer2_chain_unlock(chain);		/* unlock, leave ref */
	*chainp = chain;			/* will be returned locked */

	/*
	 * Avoid ordering deadlock when relocking shared.
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
		     hammer2_tid_t mtid, int idx)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_key_t key_next;
	hammer2_key_t save_key;
	int cache_index = -1;

	chain = *chainp;

#if HAMMER2_SYNCHRO_DEBUG
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

	hammer2_chain_delete(*parentp, chain, mtid, HAMMER2_DELETE_PERMANENT);
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
		     hammer2_tid_t mtid, int idx,
		     hammer2_chain_t *focus, int isroot)
{
	int nradix;
	uint8_t otype;

#if HAMMER2_SYNCHRO_DEBUG
	if (hammer2_debug & 1)
	kprintf("replace rec %p slave %d %d.%016jx mod=%016jx\n",
		chain,
		idx,
		focus->bref.type, focus->bref.key, mtid);
#endif
	hammer2_chain_unlock(chain);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
	if (chain->bytes != focus->bytes) {
		/* XXX what if compressed? */
		nradix = hammer2_getradix(chain->bytes);
		hammer2_chain_resize(NULL, parent, chain,
				     mtid, 0,
				     nradix, 0);
	}
	hammer2_chain_modify(chain, mtid, 0, 0);
	otype = chain->bref.type;
	chain->bref.type = focus->bref.type;
	chain->bref.methods = focus->bref.methods;
	chain->bref.keybits = focus->bref.keybits;
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid updated by flush */
	KKASSERT(mtid == 0 || chain->bref.modify_tid == mtid);
	chain->bref.flags = focus->bref.flags;
	/* key already present */
	/* check code will be recalculated */
	chain->error = 0;

	/*
	 * Copy data body.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Special case PFSROOTs, only limited changes can be made
		 * since the meta-data contains miscellanious distinguishing
		 * fields.
		 */
		if (isroot) {
			chain->data->ipdata.meta.uflags =
				focus->data->ipdata.meta.uflags;
			chain->data->ipdata.meta.rmajor =
				focus->data->ipdata.meta.rmajor;
			chain->data->ipdata.meta.rminor =
				focus->data->ipdata.meta.rminor;
			chain->data->ipdata.meta.ctime =
				focus->data->ipdata.meta.ctime;
			chain->data->ipdata.meta.mtime =
				focus->data->ipdata.meta.mtime;
			chain->data->ipdata.meta.atime =
				focus->data->ipdata.meta.atime;
			/* not btime */
			chain->data->ipdata.meta.uid =
				focus->data->ipdata.meta.uid;
			chain->data->ipdata.meta.gid =
				focus->data->ipdata.meta.gid;
			chain->data->ipdata.meta.mode =
				focus->data->ipdata.meta.mode;
			chain->data->ipdata.meta.ncopies =
				focus->data->ipdata.meta.ncopies;
			chain->data->ipdata.meta.comp_algo =
				focus->data->ipdata.meta.comp_algo;
			chain->data->ipdata.meta.check_algo =
				focus->data->ipdata.meta.check_algo;
			chain->data->ipdata.meta.data_quota =
				focus->data->ipdata.meta.data_quota;
			chain->data->ipdata.meta.inode_quota =
				focus->data->ipdata.meta.inode_quota;
			chain->data->ipdata.meta.attr_tid =
				focus->data->ipdata.meta.attr_tid;
			chain->data->ipdata.meta.dirent_tid =
				focus->data->ipdata.meta.dirent_tid;
			hammer2_chain_setcheck(chain, chain->data);
			break;
		}

		/*
		 * Normal replacement.
		 */
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
