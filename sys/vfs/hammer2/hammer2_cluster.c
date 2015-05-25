/*
 * Copyright (c) 2013-2015 The DragonFly Project.  All rights reserved.
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
 * The cluster module collects multiple chains representing the same
 * information from different nodes into a single entity.  It allows direct
 * access to media data as long as it is not blockref array data (which
 * will obviously have to be different at each node).
 *
 * This module also handles I/O dispatch, status rollup, and various
 * mastership arrangements including quorum operations.  It effectively
 * presents one topology to the vnops layer.
 *
 * Many of the API calls mimic chain API calls but operate on clusters
 * instead of chains.  Please see hammer2_chain.c for more complete code
 * documentation of the API functions.
 *
 * WARNING! This module is *extremely* complex.  It must issue asynchronous
 *	    locks and I/O, do quorum and/or master-slave processing, and
 *	    it must operate properly even if some nodes are broken (which
 *	    can also mean indefinite locks).
 *
 *				CLUSTER OPERATIONS
 *
 * Cluster operations can be broken down into three pieces:
 *
 * (1) Chain locking and data retrieval.
 *		hammer2_cluster_lock()
 *		hammer2_cluster_parent()
 *
 *	- Most complex functions, quorum management on transaction ids.
 *
 *	- Locking and data accesses must be internally asynchronous.
 *
 *	- Validate and manage cache coherency primitives (cache state
 *	  is stored in chain topologies but must be validated by these
 *	  functions).
 *
 * (2) Lookups and Scans
 *		hammer2_cluster_lookup()
 *		hammer2_cluster_next()
 *
 *	- Depend on locking & data retrieval functions, but still complex.
 *
 *	- Must do quorum management on transaction ids.
 *
 *	- Lookup and Iteration ops Must be internally asynchronous.
 *
 * (3) Modifying Operations
 *		hammer2_cluster_create()
 *		hammer2_cluster_rename()
 *		hammer2_cluster_delete()
 *		hammer2_cluster_modify()
 *		hammer2_cluster_modsync()
 *
 *	- Can usually punt on failures, operation continues unless quorum
 *	  is lost.  If quorum is lost, must wait for resynchronization
 *	  (depending on the management mode).
 *
 *	- Must disconnect node on failures (also not flush), remount, and
 *	  resynchronize.
 *
 *	- Network links (via kdmsg) are relatively easy to issue as the
 *	  complex underworkings of hammer2_chain.c don't have to messed
 *	  with (the protocol is at a higher level than block-level).
 *
 *	- Multiple local disk nodes (i.e. block devices) are another matter.
 *	  Chain operations have to be dispatched to per-node threads (xN)
 *	  because we can't asynchronize potentially very complex chain
 *	  operations in hammer2_chain.c (it would be a huge mess).
 *
 *	  (these threads are also used to terminate incoming kdmsg ops from
 *	  other machines).
 *
 *	- Single-node filesystems do not use threads and will simply call
 *	  hammer2_chain.c functions directly.  This short-cut is handled
 *	  at the base of each cluster function.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

/*
 * Returns non-zero if any chain in the cluster needs to be resized.
 * Errored elements are not used in the calculation.
 */
int
hammer2_cluster_need_resize(hammer2_cluster_t *cluster, int bytes)
{
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		if (chain->error)
			continue;
		if (chain->bytes != bytes)
			return 1;
	}
	return 0;
}

/*
 * Returns the bref type of the cluster's foucs.
 *
 * If the cluster is errored, returns HAMMER2_BREF_TYPE_EMPTY (0).
 * The cluster must be locked.
 */
uint8_t
hammer2_cluster_type(hammer2_cluster_t *cluster)
{
	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	if (cluster->error == 0)
		return(cluster->focus->bref.type);
	return 0;
}

/*
 * Returns non-zero if the cluster's focus is flagged as being modified.
 *
 * If the cluster is errored, returns 0.
 */
int
hammer2_cluster_modified(hammer2_cluster_t *cluster)
{
	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	if (cluster->error == 0)
		return((cluster->focus->flags & HAMMER2_CHAIN_MODIFIED) != 0);
	return 0;
}

/*
 * Returns the bref of the cluster's focus, sans any data-offset information
 * (since offset information is per-node and wouldn't be useful).
 *
 * Callers use this function to access modify_tid, mirror_tid, type,
 * key, and keybits.
 *
 * If the cluster is errored, returns an empty bref.
 * The cluster must be locked.
 */
void
hammer2_cluster_bref(hammer2_cluster_t *cluster, hammer2_blockref_t *bref)
{
	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	if (cluster->error == 0) {
		*bref = cluster->focus->bref;
		bref->data_off = 0;
	} else {
		bzero(bref, sizeof(*bref));
	}
}

/*
 * Return non-zero if the chain representing an inode has been flagged
 * as having been unlinked.  Allows the vnode reclaim to avoid loading
 * the inode data from disk e.g. when unmount or recycling old, clean
 * vnodes.
 *
 * The cluster does not need to be locked.
 * The focus cannot be used since the cluster might not be locked.
 */
int
hammer2_cluster_isunlinked(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int flags;
	int i;

	flags = 0;
	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		chain = cluster->array[i].chain;
		if (chain)
			flags |= chain->flags;
	}
	return (flags & HAMMER2_CHAIN_UNLINKED);
}

/*
 * Set a bitmask of flags in all chains related to a cluster.
 * The cluster should probably be locked.
 *
 * XXX Only operate on FEMOD elements?
 */
void
hammer2_cluster_set_chainflags(hammer2_cluster_t *cluster, uint32_t flags)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain)
			atomic_set_int(&chain->flags, flags);
	}
}

/*
 * Set a bitmask of flags in all chains related to a cluster.
 * The cluster should probably be locked.
 *
 * XXX Only operate on FEMOD elements?
 */
void
hammer2_cluster_clr_chainflags(hammer2_cluster_t *cluster, uint32_t flags)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain)
			atomic_clear_int(&chain->flags, flags);
	}
}

/*
 * Flag the cluster for flushing recursively up to the root.  Despite the
 * work it does, this is relatively benign.  It just makes sure that the
 * flusher has top-down visibility to this cluster.
 *
 * Errored chains are not flagged for flushing.
 *
 * The cluster should probably be locked.
 */
void
hammer2_cluster_setflush(hammer2_trans_t *trans, hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		if (chain->error)
			continue;
		hammer2_chain_setflush(trans, chain);
	}
}

/*
 * Set the check mode for the cluster.
 * Errored elements of the cluster are ignored.
 *
 * The cluster must be locked and modified.
 */
void
hammer2_cluster_setmethod_check(hammer2_trans_t *trans,
				hammer2_cluster_t *cluster,
				int check_algo)
{
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		if (chain->error)
			continue;
		KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);
		chain->bref.methods &= ~HAMMER2_ENC_CHECK(-1);
		chain->bref.methods |= HAMMER2_ENC_CHECK(check_algo);
	}
}

/*
 * Create a degenerate cluster with one ref from a single locked chain.
 * The returned cluster will be focused on the chain and inherit its
 * error state.
 *
 * The chain's lock and reference are transfered to the new cluster, so
 * the caller should not try to unlock the chain separately.
 *
 * We fake the flags.
 */
hammer2_cluster_t *
hammer2_cluster_from_chain(hammer2_chain_t *chain)
{
	hammer2_cluster_t *cluster;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->array[0].chain = chain;
	cluster->array[0].flags = HAMMER2_CITEM_FEMOD;
	cluster->nchains = 1;
	cluster->focus = chain;
	cluster->focus_index = 0;
	cluster->pmp = chain->pmp;
	cluster->refs = 1;
	cluster->error = chain->error;
	cluster->flags = HAMMER2_CLUSTER_LOCKED |
			 HAMMER2_CLUSTER_WRHARD |
			 HAMMER2_CLUSTER_RDHARD |
			 HAMMER2_CLUSTER_MSYNCED |
			 HAMMER2_CLUSTER_SSYNCED;

	return cluster;
}

/*
 * Add a reference to a cluster and its underlying chains.
 *
 * We must also ref the underlying chains in order to allow ref/unlock
 * sequences to later re-lock.
 */
void
hammer2_cluster_ref(hammer2_cluster_t *cluster)
{
	atomic_add_int(&cluster->refs, 1);
#if 0
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain)
			hammer2_chain_ref(chain);
	}
#endif
}

/*
 * Drop the caller's reference to the cluster.  When the ref count drops to
 * zero this function frees the cluster and drops all underlying chains.
 *
 * In-progress read I/Os are typically detached from the cluster once the
 * first one returns (the remaining stay attached to the DIOs but are then
 * ignored and drop naturally).
 */
void
hammer2_cluster_drop(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cluster->refs > 0);
	if (atomic_fetchadd_int(&cluster->refs, -1) == 1) {
		cluster->focus = NULL;		/* safety XXX chg to assert */
		cluster->focus_index = 0;

		for (i = 0; i < cluster->nchains; ++i) {
			chain = cluster->array[i].chain;
			if (chain) {
				hammer2_chain_drop(chain);
				cluster->array[i].chain = NULL; /* safety */
			}
		}
		cluster->nchains = 0;				/* safety */

		kfree(cluster, M_HAMMER2);
		/* cluster is invalid */
	}
}

void
hammer2_cluster_wait(hammer2_cluster_t *cluster)
{
	tsleep(cluster->focus, 0, "h2clcw", 1);
}

/*
 * Lock a cluster.  Cluster must already be referenced.  Focus is maintained. 
 *
 * WARNING! This function expects the caller to handle resolution of the
 *	    cluster.  We never re-resolve the cluster in this function,
 *	    because it might be used to temporarily unlock/relock a cparent
 *	    in an iteration or recursrion, and the cparents elements do not
 *	    necessarily match.
 */
void
hammer2_cluster_lock(hammer2_cluster_t *cluster, int how)
{
	hammer2_chain_t *chain;
	int i;

	/* cannot be on inode-embedded cluster template, must be on copy */
	KKASSERT(cluster->refs > 0);
	KKASSERT((cluster->flags & HAMMER2_CLUSTER_INODE) == 0);
	if (cluster->flags & HAMMER2_CLUSTER_LOCKED) {
		panic("hammer2_cluster_lock: cluster %p already locked!\n",
			cluster);
	}
	atomic_set_int(&cluster->flags, HAMMER2_CLUSTER_LOCKED);

	/*
	 * Lock chains and resolve state.
	 */
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		hammer2_chain_lock(chain, how);
	}
}

/*
 * Calculate the clustering state for the cluster and set its focus.
 * This routine must be called with care.  For example, it should not
 * normally be called after relocking a non-leaf cluster because parent
 * clusters help iterations and each element might be at a slightly different
 * indirect node (each node's topology is independently indexed).
 *
 * HAMMER2_CITEM_FEMOD flags which elements can be modified by normal
 * operations.  Typically this is only set on a quorum of MASTERs or
 * on a SOFT_MASTER.  Also as a degenerate case on SUPROOT.  If a SOFT_MASTER
 * is present, this bit is *not* set on a quorum of MASTERs.  The
 * synchronization code ignores this bit, but all hammer2_cluster_*() calls
 * that create/modify/delete elements use it.
 *
 * The chains making up the cluster may be narrowed down based on quorum
 * acceptability, and if RESOLVE_RDONLY is specified the chains can be
 * narrowed down to a single chain as long as the entire subtopology is known
 * to be intact.  So, for example, we can narrow a read-only op to a single
 * fast SLAVE but if we focus a CACHE chain we must still retain at least
 * a SLAVE to ensure that the subtopology can be accessed.
 *
 * RESOLVE_RDONLY operations are effectively as-of so the quorum does not need
 * to be maintained once the topology is validated as-of the top level of
 * the operation.
 *
 * If a failure occurs the operation must be aborted by higher-level code and
 * retried. XXX
 */
void
hammer2_cluster_resolve(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *focus;
	hammer2_pfs_t *pmp;
	hammer2_tid_t quorum_tid;
	hammer2_tid_t last_best_quorum_tid;
	int focus_pfs_type;
	uint32_t nflags;
	int ttlmasters;
	int ttlslaves;
	int nmasters;
	int nslaves;
	int nquorum;
	int smpresent;
	int i;

	cluster->error = 0;
	cluster->focus = NULL;

	focus_pfs_type = 0;
	nflags = 0;
	ttlmasters = 0;
	ttlslaves = 0;
	nmasters = 0;
	nslaves = 0;

	/*
	 * Calculate quorum
	 */
	pmp = cluster->pmp;
	KKASSERT(pmp != NULL || cluster->nchains == 0);
	nquorum = pmp ? pmp->pfs_nmasters / 2 + 1 : 0;
	smpresent = 0;

	/*
	 * Pass 1
	 *
	 * NOTE: A NULL chain is not necessarily an error, it could be
	 *	 e.g. a lookup failure or the end of an iteration.
	 *	 Process normally.
	 */
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain && chain->error) {
			if (cluster->focus == NULL || cluster->focus == chain) {
				/* error will be overridden by valid focus */
				cluster->error = chain->error;
			}

			/*
			 * Must count total masters and slaves whether the
			 * chain is errored or not.
			 */
			switch (cluster->pmp->pfs_types[i]) {
			case HAMMER2_PFSTYPE_MASTER:
				++ttlmasters;
				break;
			case HAMMER2_PFSTYPE_SLAVE:
				++ttlslaves;
				break;
			}
			continue;
		}
		switch (cluster->pmp->pfs_types[i]) {
		case HAMMER2_PFSTYPE_MASTER:
			++ttlmasters;
			break;
		case HAMMER2_PFSTYPE_SLAVE:
			++ttlslaves;
			break;
		case HAMMER2_PFSTYPE_SOFT_MASTER:
			nflags |= HAMMER2_CLUSTER_WRSOFT;
			nflags |= HAMMER2_CLUSTER_RDSOFT;
			smpresent = 1;
			break;
		case HAMMER2_PFSTYPE_SOFT_SLAVE:
			nflags |= HAMMER2_CLUSTER_RDSOFT;
			break;
		case HAMMER2_PFSTYPE_SUPROOT:
			/*
			 * Degenerate cluster representing the super-root
			 * topology on a single device.  Fake stuff so
			 * cluster ops work as expected.
			 */
			nflags |= HAMMER2_CLUSTER_WRHARD;
			nflags |= HAMMER2_CLUSTER_RDHARD;
			cluster->focus_index = i;
			cluster->focus = chain;
			cluster->error = chain ? chain->error : 0;
			break;
		default:
			break;
		}
	}

	/*
	 * Pass 2
	 *
	 * Resolve masters.  Calculate nmasters for the highest matching
	 * TID, if a quorum cannot be attained try the next lower matching
	 * TID until we exhaust TIDs.
	 *
	 * NOTE: A NULL chain is not necessarily an error, it could be
	 *	 e.g. a lookup failure or the end of an iteration.
	 *	 Process normally.
	 */
	last_best_quorum_tid = HAMMER2_TID_MAX;
	quorum_tid = 0;		/* fix gcc warning */

	while (nmasters < nquorum && last_best_quorum_tid != 0) {
		nmasters = 0;
		quorum_tid = 0;

		for (i = 0; i < cluster->nchains; ++i) {
			if (cluster->pmp->pfs_types[i] !=
			    HAMMER2_PFSTYPE_MASTER) {
				continue;
			}
			chain = cluster->array[i].chain;

			if (cluster->array[i].flags & HAMMER2_CITEM_INVALID) {
				/*
				 * Invalid as in unsynchronized, cannot be
				 * used to calculate the quorum.
				 */
			} else if (chain == NULL && quorum_tid == 0) {
				/*
				 * NULL chain on master matches NULL chains
				 * on other masters.
				 */
				++nmasters;
			} else if (quorum_tid < last_best_quorum_tid &&
				   chain != NULL &&
				   (quorum_tid < chain->bref.modify_tid ||
				    nmasters == 0)) {
				/*
				 * Better TID located, reset nmasters count.
				 */
				nmasters = 1;
				quorum_tid = chain->bref.modify_tid;
			} else if (chain &&
				   quorum_tid == chain->bref.modify_tid) {
				/*
				 * TID matches current collection.
				 */
				++nmasters;
			}
		}
		if (nmasters >= nquorum)
			break;
		last_best_quorum_tid = quorum_tid;
	}

	/*
	 * Pass 3
	 *
	 * NOTE: A NULL chain is not necessarily an error, it could be
	 *	 e.g. a lookup failure or the end of an iteration.
	 *	 Process normally.
	 */
	for (i = 0; i < cluster->nchains; ++i) {
		cluster->array[i].flags &= ~HAMMER2_CITEM_FEMOD;
		chain = cluster->array[i].chain;
		if (chain && chain->error) {
			if (cluster->focus == NULL || cluster->focus == chain) {
				/* error will be overridden by valid focus */
				cluster->error = chain->error;
			}
			continue;
		}

		switch (cluster->pmp->pfs_types[i]) {
		case HAMMER2_PFSTYPE_MASTER:
			/*
			 * We must have enough up-to-date masters to reach
			 * a quorum and the master modify_tid must match
			 * the quorum's modify_tid.
			 *
			 * Do not select an errored or out-of-sync master.
			 */
			if (cluster->array[i].flags & HAMMER2_CITEM_INVALID) {
				nflags |= HAMMER2_CLUSTER_UNHARD;
			} else if (nmasters >= nquorum &&
				   (chain == NULL || chain->error == 0) &&
				   ((chain == NULL && quorum_tid == 0) ||
				    (chain != NULL && quorum_tid ==
						  chain->bref.modify_tid))) {
				nflags |= HAMMER2_CLUSTER_WRHARD;
				nflags |= HAMMER2_CLUSTER_RDHARD;
				if (!smpresent) {
					cluster->array[i].flags |=
							HAMMER2_CITEM_FEMOD;
				}
				if (cluster->focus == NULL ||
				    focus_pfs_type == HAMMER2_PFSTYPE_SLAVE) {
					focus_pfs_type = HAMMER2_PFSTYPE_MASTER;
					cluster->focus_index = i;
					cluster->focus = chain; /* NULL ok */
					cluster->error = chain ? chain->error :
								 0;
				}
			} else if (chain == NULL || chain->error == 0) {
				nflags |= HAMMER2_CLUSTER_UNHARD;
			}
			break;
		case HAMMER2_PFSTYPE_SLAVE:
			/*
			 * We must have enough up-to-date masters to reach
			 * a quorum and the slave modify_tid must match the
			 * quorum's modify_tid.
			 *
			 * Do not select an errored slave.
			 */
			if (cluster->array[i].flags & HAMMER2_CITEM_INVALID) {
				nflags |= HAMMER2_CLUSTER_UNHARD;
			} else if (nmasters >= nquorum &&
				   (chain == NULL || chain->error == 0) &&
				   ((chain == NULL && quorum_tid == 0) ||
				    (chain && quorum_tid ==
					      chain->bref.modify_tid))) {
				++nslaves;
				nflags |= HAMMER2_CLUSTER_RDHARD;
#if 0
				/* XXX optimize for RESOLVE_RDONLY */
				if (cluster->focus == NULL) {
					focus_pfs_type = HAMMER2_PFSTYPE_SLAVE;
					cluster->focus_index = i;
					cluster->focus = chain; /* NULL ok */
					cluster->error = chain ? chain->error :
								 0;
				}
#endif
			} else if (chain == NULL || chain->error == 0) {
				nflags |= HAMMER2_CLUSTER_UNSOFT;
			}
			break;
		case HAMMER2_PFSTYPE_SOFT_MASTER:
			/*
			 * Directly mounted soft master always wins.  There
			 * should be only one.
			 */
			KKASSERT(focus_pfs_type != HAMMER2_PFSTYPE_SOFT_MASTER);
			cluster->focus_index = i;
			cluster->focus = chain;
			cluster->error = chain ? chain->error : 0;
			focus_pfs_type = HAMMER2_PFSTYPE_SOFT_MASTER;
			cluster->array[i].flags |= HAMMER2_CITEM_FEMOD;
			break;
		case HAMMER2_PFSTYPE_SOFT_SLAVE:
			/*
			 * Directly mounted soft slave always wins.  There
			 * should be only one.
			 */
			KKASSERT(focus_pfs_type != HAMMER2_PFSTYPE_SOFT_SLAVE);
			if (focus_pfs_type != HAMMER2_PFSTYPE_SOFT_MASTER) {
				cluster->focus_index = i;
				cluster->focus = chain;
				cluster->error = chain ? chain->error : 0;
				focus_pfs_type = HAMMER2_PFSTYPE_SOFT_SLAVE;
			}
			break;
		case HAMMER2_PFSTYPE_SUPROOT:
			/*
			 * spmp (degenerate case)
			 */
			KKASSERT(i == 0);
			cluster->focus_index = i;
			cluster->focus = chain;
			cluster->error = chain ? chain->error : 0;
			focus_pfs_type = HAMMER2_PFSTYPE_SUPROOT;
			cluster->array[i].flags |= HAMMER2_CITEM_FEMOD;
			break;
		default:
			break;
		}
	}

	/*
	 * Focus now set, adjust ddflag.  Skip this pass if the focus
	 * is bad or if we are at the PFS root (the bref won't match at
	 * the PFS root, obviously).
	 */
	focus = cluster->focus;
	if (focus) {
		cluster->ddflag =
			(cluster->focus->bref.type == HAMMER2_BREF_TYPE_INODE);
	} else {
		cluster->ddflag = 0;
		goto skip4;
	}
	if (cluster->focus->flags & HAMMER2_CHAIN_PFSBOUNDARY)
		goto skip4;

	/*
	 * Pass 4
	 *
	 * Validate the elements that were not marked invalid.  They should
	 * match.
	 */
	for (i = 0; i < cluster->nchains; ++i) {
		int ddflag;

		chain = cluster->array[i].chain;

		if (chain == NULL)
			continue;
		if (chain == focus)
			continue;
		if (cluster->array[i].flags & HAMMER2_CITEM_INVALID)
			continue;

		ddflag = (chain->bref.type == HAMMER2_BREF_TYPE_INODE);
		if (chain->bref.type != focus->bref.type ||
		    chain->bref.key != focus->bref.key ||
		    chain->bref.keybits != focus->bref.keybits ||
		    chain->bref.modify_tid != focus->bref.modify_tid ||
		    chain->bytes != focus->bytes ||
		    ddflag != cluster->ddflag) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			if (hammer2_debug & 1)
			kprintf("cluster_resolve: matching modify_tid failed "
				"bref test: idx=%d type=%02x/%02x "
				"key=%016jx/%d-%016jx/%d "
				"mod=%016jx/%016jx bytes=%u/%u\n",
				i,
				chain->bref.type, focus->bref.type,
				chain->bref.key, chain->bref.keybits,
				focus->bref.key, focus->bref.keybits,
				chain->bref.modify_tid, focus->bref.modify_tid,
				chain->bytes, focus->bytes);
			if (hammer2_debug & 0x4000)
				panic("cluster_resolve");
			/* flag issue and force resync? */
		}
	}
skip4:

	if (ttlslaves == 0)
		nflags |= HAMMER2_CLUSTER_NOSOFT;
	if (ttlmasters == 0)
		nflags |= HAMMER2_CLUSTER_NOHARD;

	/*
	 * Set SSYNCED or MSYNCED for slaves and masters respectively if
	 * all available nodes (even if 0 are available) are fully
	 * synchronized.  This is used by the synchronization thread to
	 * determine if there is work it could potentially accomplish.
	 */
	if (nslaves == ttlslaves)
		nflags |= HAMMER2_CLUSTER_SSYNCED;
	if (nmasters == ttlmasters)
		nflags |= HAMMER2_CLUSTER_MSYNCED;

	/*
	 * Determine if the cluster was successfully locked for the
	 * requested operation and generate an error code.  The cluster
	 * will not be locked (or ref'd) if an error is returned.
	 *
	 * Caller can use hammer2_cluster_rdok() and hammer2_cluster_wrok()
	 * to determine if reading or writing is possible.  If writing, the
	 * cluster still requires a call to hammer2_cluster_modify() first.
	 */
	atomic_set_int(&cluster->flags, nflags);
	atomic_clear_int(&cluster->flags, HAMMER2_CLUSTER_ZFLAGS & ~nflags);
}

/*
 * Copy a cluster, returned a ref'd cluster.  All underlying chains
 * are also ref'd, but not locked.  Focus state is also copied.
 *
 * Original cluster does not have to be locked but usually is.
 * New cluster will not be flagged as locked.
 *
 * Callers using this function to initialize a new cluster from an inode
 * generally lock and resolve the resulting cluster.
 *
 * Callers which use this function to save/restore a cluster structure
 * generally retain the focus state and do not re-resolve it.  Caller should
 * not try to re-resolve internal (cparent) node state during an iteration
 * as the individual tracking elements of cparent in an iteration may not
 * match even though they are correct.
 */
hammer2_cluster_t *
hammer2_cluster_copy(hammer2_cluster_t *ocluster)
{
	hammer2_pfs_t *pmp = ocluster->pmp;
	hammer2_cluster_t *ncluster;
	hammer2_chain_t *chain;
	int i;

	ncluster = kmalloc(sizeof(*ncluster), M_HAMMER2, M_WAITOK | M_ZERO);
	ncluster->pmp = pmp;
	ncluster->nchains = ocluster->nchains;
	ncluster->refs = 1;

	for (i = 0; i < ocluster->nchains; ++i) {
		chain = ocluster->array[i].chain;
		ncluster->array[i].chain = chain;
		ncluster->array[i].flags = ocluster->array[i].flags;
		if (chain)
			hammer2_chain_ref(chain);
	}
	ncluster->focus_index = ocluster->focus_index;
	ncluster->focus = ocluster->focus;
	ncluster->flags = ocluster->flags & ~(HAMMER2_CLUSTER_LOCKED |
					      HAMMER2_CLUSTER_INODE);

	return (ncluster);
}

/*
 * Unlock a cluster.  Refcount and focus is maintained.
 */
void
hammer2_cluster_unlock(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	if ((cluster->flags & HAMMER2_CLUSTER_LOCKED) == 0) {
		kprintf("hammer2_cluster_unlock: cluster %p not locked\n",
			cluster);
	}
	KKASSERT(cluster->flags & HAMMER2_CLUSTER_LOCKED);
	KKASSERT(cluster->refs > 0);
	atomic_clear_int(&cluster->flags, HAMMER2_CLUSTER_LOCKED);

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain)
			hammer2_chain_unlock(chain);
	}
}

/*
 * Resize the cluster's physical storage allocation in-place.  This may
 * replace the cluster's chains.
 */
void
hammer2_cluster_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
		       hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		       int nradix, int flags)
{
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cparent->pmp == cluster->pmp);		/* can be NULL */
	KKASSERT(cparent->nchains == cluster->nchains);

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		chain = cluster->array[i].chain;
		if (chain) {
			KKASSERT(cparent->array[i].chain);
			hammer2_chain_resize(trans, ip,
					     cparent->array[i].chain, chain,
					     nradix, flags);
		}
	}
}

/*
 * Set an inode's cluster modified, marking the related chains RW and
 * duplicating them if necessary.
 *
 * The passed-in chain is a localized copy of the chain previously acquired
 * when the inode was locked (and possilby replaced in the mean time), and
 * must also be updated.  In fact, we update it first and then synchronize
 * the inode's cluster cache.
 */
hammer2_inode_data_t *
hammer2_cluster_modify_ip(hammer2_trans_t *trans, hammer2_inode_t *ip,
			  hammer2_cluster_t *cluster, int flags)
{
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	hammer2_cluster_modify(trans, cluster, flags);

	hammer2_inode_repoint(ip, NULL, cluster);
	if (ip->vp)
		vsetisdirty(ip->vp);
	return (&hammer2_cluster_wdata(cluster)->ipdata);
}

/*
 * Adjust the cluster's chains to allow modification and adjust the
 * focus.  Data will be accessible on return.
 *
 * If our focused master errors on modify, re-resolve the cluster to
 * try to select a different master.
 */
void
hammer2_cluster_modify(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
		       int flags)
{
	hammer2_chain_t *chain;
	int resolve_again;
	int i;

	resolve_again = 0;
	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		if (chain->error)
			continue;
		hammer2_chain_modify(trans, chain, flags);
		if (cluster->focus == chain && chain->error) {
			cluster->error = chain->error;
			resolve_again = 1;
		}
	}
	if (resolve_again)
		hammer2_cluster_resolve(cluster);
}

/*
 * Synchronize modifications from the focus to other chains in a cluster.
 * Convenient because nominal API users can just modify the contents of the
 * focus (at least for non-blockref data).
 *
 * Nominal front-end operations only edit non-block-table data in a single
 * chain.  This code copies such modifications to the other chains in the
 * cluster.  Blocktable modifications are handled on a chain-by-chain basis
 * by both the frontend and the backend and will explode in fireworks if
 * blindly copied.
 */
void
hammer2_cluster_modsync(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *focus;
	hammer2_chain_t *scan;
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_data_t *wipdata;
	int i;

	focus = cluster->focus;
	KKASSERT(focus->flags & HAMMER2_CHAIN_MODIFIED);

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		scan = cluster->array[i].chain;
		if (scan == NULL || scan == focus)
			continue;
		if (scan->error)
			continue;
		KKASSERT(scan->flags & HAMMER2_CHAIN_MODIFIED);
		KKASSERT(focus->bytes == scan->bytes &&
			 focus->bref.type == scan->bref.type);
		switch(focus->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			ripdata = &focus->data->ipdata;
			wipdata = &scan->data->ipdata;
			if ((ripdata->op_flags &
			    HAMMER2_OPFLAG_DIRECTDATA) == 0) {
				bcopy(ripdata, wipdata,
				      offsetof(hammer2_inode_data_t, u));
				break;
			}
			/* fall through to full copy */
		case HAMMER2_BREF_TYPE_DATA:
			bcopy(focus->data, scan->data, focus->bytes);
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		case HAMMER2_BREF_TYPE_FREEMAP:
		case HAMMER2_BREF_TYPE_VOLUME:
			panic("hammer2_cluster_modsync: illegal node type");
			/* NOT REACHED */
			break;
		default:
			panic("hammer2_cluster_modsync: unknown node type");
			break;
		}
	}
}

/*
 * Lookup initialization/completion API.  Returns a locked, fully resolved
 * cluster with one ref.
 */
hammer2_cluster_t *
hammer2_cluster_lookup_init(hammer2_cluster_t *cparent, int flags)
{
	hammer2_cluster_t *cluster;

	cluster = hammer2_cluster_copy(cparent);
	if (flags & HAMMER2_LOOKUP_SHARED) {
		hammer2_cluster_lock(cluster, HAMMER2_RESOLVE_ALWAYS |
					      HAMMER2_RESOLVE_SHARED);
	} else {
		hammer2_cluster_lock(cluster, HAMMER2_RESOLVE_ALWAYS);
	}
	hammer2_cluster_resolve(cluster);

	return (cluster);
}

void
hammer2_cluster_lookup_done(hammer2_cluster_t *cparent)
{
	if (cparent) {
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);
	}
}

/*
 * Locate first match or overlap under parent, return a new, locked, resolved
 * cluster with one ref.
 *
 * Must never be called with HAMMER2_LOOKUP_MATCHIND.
 */
hammer2_cluster_t *
hammer2_cluster_lookup(hammer2_cluster_t *cparent, hammer2_key_t *key_nextp,
		     hammer2_key_t key_beg, hammer2_key_t key_end, int flags)
{
	hammer2_pfs_t *pmp;
	hammer2_cluster_t *cluster;
	hammer2_chain_t *chain;
	hammer2_key_t key_accum;
	hammer2_key_t key_next;
	int null_count;
	int rflags;
	int i;

	KKASSERT((flags & HAMMER2_LOOKUP_MATCHIND) == 0);

	pmp = cparent->pmp;				/* can be NULL */
	key_accum = *key_nextp;
	null_count = 0;
	if (flags & HAMMER2_LOOKUP_SHARED)
		rflags = HAMMER2_RESOLVE_SHARED;
	else
		rflags = 0;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->pmp = pmp;				/* can be NULL */
	cluster->refs = 1;
	if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
		cluster->flags |= HAMMER2_CLUSTER_LOCKED;

	/*
	 * Iterating earlier cluster elements with later elements still
	 * locked is a problem, so we have to unlock the parent and then
	 * re-lock as we go.
	 */
	hammer2_cluster_unlock(cparent);
	cparent->flags |= HAMMER2_CLUSTER_LOCKED;

	/*
	 * Pass-1, issue lookups.
	 */
	for (i = 0; i < cparent->nchains; ++i) {
		cluster->array[i].flags = cparent->array[i].flags;
		key_next = *key_nextp;

		/*
		 * Always relock the parent as we go.
		 */
		if (cparent->array[i].chain) {
			hammer2_chain_lock(cparent->array[i].chain, rflags);
		}

		/*
		 * Nothing to base the lookup, or parent was not synchronized.
		 */
		if (cparent->array[i].chain == NULL ||
		    (cparent->array[i].flags & HAMMER2_CITEM_INVALID)) {
			++null_count;
			continue;
		}

		chain = hammer2_chain_lookup(&cparent->array[i].chain,
					     &key_next,
					     key_beg, key_end,
					     &cparent->array[i].cache_index,
					     flags);
		cluster->array[i].chain = chain;
		if (chain == NULL) {
			++null_count;
		}
		if (key_accum > key_next)
			key_accum = key_next;
	}

	/*
	 * Cleanup
	 */
	cluster->nchains = i;
	*key_nextp = key_accum;

	/*
	 * The cluster must be resolved, out of sync elements may be present.
	 *
	 * If HAMMER2_LOOKUP_ALLNODES is not set focus must be non-NULL.
	 */
	if (null_count != i)
		hammer2_cluster_resolve(cluster);
	if (null_count == i ||
	    (cluster->focus == NULL &&
	     (flags & HAMMER2_LOOKUP_ALLNODES) == 0)) {
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	}

	return (cluster);
}

/*
 * Locate next match or overlap under parent, replace the passed-in cluster.
 * The returned cluster is a new, locked, resolved cluster with one ref.
 *
 * Must never be called with HAMMER2_LOOKUP_MATCHIND.
 */
hammer2_cluster_t *
hammer2_cluster_next(hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		     hammer2_key_t *key_nextp,
		     hammer2_key_t key_beg, hammer2_key_t key_end, int flags)
{
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_key_t key_accum;
	hammer2_key_t key_next;
	int parent_index;
	int cluster_index;
	int null_count;
	int rflags;
	int i;

	KKASSERT((flags & HAMMER2_LOOKUP_MATCHIND) == 0);

	key_accum = *key_nextp;
	null_count = 0;
	parent_index = cparent->focus_index;	/* save prior focus */
	cluster_index = cluster->focus_index;
	if (flags & HAMMER2_LOOKUP_SHARED)
		rflags = HAMMER2_RESOLVE_SHARED;
	else
		rflags = 0;

	cluster->focus = NULL;		/* XXX needed any more? */
	/*cparent->focus = NULL;*/
	cluster->focus_index = 0;	/* XXX needed any more? */
	/*cparent->focus_index = 0;*/

	cluster->ddflag = 0;

	/*
	 * The parent is always locked on entry, the iterator may be locked
	 * depending on flags.
	 *
	 * We must temporarily unlock the passed-in clusters to avoid a
	 * deadlock between elements of the cluster with other threads.
	 * We will fixup the lock in the loop.
	 *
	 * Note that this will clear the focus.
	 *
	 * Reflag the clusters as locked, because we will relock them
	 * as we go.
	 */
	if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0) {
		hammer2_cluster_unlock(cluster);
		cluster->flags |= HAMMER2_CLUSTER_LOCKED;
	}
	hammer2_cluster_unlock(cparent);
	cparent->flags |= HAMMER2_CLUSTER_LOCKED;

	for (i = 0; i < cparent->nchains; ++i) {
		key_next = *key_nextp;
		ochain = cluster->array[i].chain;

		/*
		 * Always relock the parent as we go.
		 */
		if (cparent->array[i].chain)
			hammer2_chain_lock(cparent->array[i].chain, rflags);

		/*
		 * Nothing to iterate from.  These cases can occur under
		 * normal operations.  For example, during synchronization
		 * a slave might reach the end of its scan while records
		 * are still left on the master(s).
		 */
		if (ochain == NULL) {
			++null_count;
			continue;
		}
		if (cparent->array[i].chain == NULL ||
		    (cparent->array[i].flags & HAMMER2_CITEM_INVALID) ||
		    (cluster->array[i].flags & HAMMER2_CITEM_INVALID)) {
			/* ochain has not yet been relocked */
			hammer2_chain_drop(ochain);
			cluster->array[i].chain = NULL;
			++null_count;
			continue;
		}

		/*
		 * Relock the child if necessary.  Parent and child will then
		 * be locked as expected by hammer2_chain_next() and flags.
		 */
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_chain_lock(ochain, rflags);
		nchain = hammer2_chain_next(&cparent->array[i].chain, ochain,
					    &key_next, key_beg, key_end,
					    &cparent->array[i].cache_index,
					    flags);
		/* ochain now invalid but can still be used for focus check */
		if (parent_index == i) {
			cparent->focus_index = i;
			cparent->focus = cparent->array[i].chain;
		}

		cluster->array[i].chain = nchain;
		if (nchain == NULL) {
			++null_count;
		}
		if (key_accum > key_next)
			key_accum = key_next;
	}

	/*
	 * Cleanup
	 */
	cluster->nchains = i;
	*key_nextp = key_accum;

	/*
	 * The cluster must be resolved, out of sync elements may be present.
	 *
	 * If HAMMER2_LOOKUP_ALLNODES is not set focus must be non-NULL.
	 */
	if (null_count != i)
		hammer2_cluster_resolve(cluster);
	if (null_count == i ||
	    (cluster->focus == NULL &&
	     (flags & HAMMER2_LOOKUP_ALLNODES) == 0)) {
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	}
	return(cluster);
}

/*
 * Advance just one chain in the cluster and recalculate the invalid bit.
 * The cluster index is allowed to be flagged invalid on input and is
 * recalculated on return.
 *
 * (used during synchronization to advance past a chain being deleted).
 *
 * The chain being advanced must not be the focus and the clusters in
 * question must have already passed normal cluster_lookup/cluster_next
 * checks.
 *
 * The cluster always remains intact on return, so void function.
 */
void
hammer2_cluster_next_single_chain(hammer2_cluster_t *cparent,
				  hammer2_cluster_t *cluster,
				  hammer2_key_t *key_nextp,
				  hammer2_key_t key_beg,
				  hammer2_key_t key_end,
				  int i, int flags)
{
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *focus;
	hammer2_key_t key_accum;
	hammer2_key_t key_next;
	int ddflag;

	key_accum = *key_nextp;
	key_next = *key_nextp;
	ochain = cluster->array[i].chain;
	if (ochain == NULL)
		goto done;
	KKASSERT(ochain != cluster->focus);

	nchain = hammer2_chain_next(&cparent->array[i].chain, ochain,
				    &key_next, key_beg, key_end,
				    &cparent->array[i].cache_index,
				    flags);
	/* ochain now invalid */
	if (cparent->focus_index == i)
		cparent->focus = cparent->array[i].chain;

	/*
	 * Install nchain.  Note that nchain can be NULL, and can also
	 * be in an unlocked state depending on flags.
	 */
	cluster->array[i].chain = nchain;
	cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;

	if (key_accum > key_next)
		key_accum = key_next;

	focus = cluster->focus;
	if (focus == NULL)
		goto done;
	if (nchain == NULL)
		goto done;
#if 0
	if (nchain == focus)	/* ASSERTED NOT TRUE */
		...
#endif
	ddflag = (nchain->bref.type == HAMMER2_BREF_TYPE_INODE);
	if (nchain->bref.type != focus->bref.type ||
	    nchain->bref.key != focus->bref.key ||
	    nchain->bref.keybits != focus->bref.keybits ||
	    nchain->bref.modify_tid != focus->bref.modify_tid ||
	    nchain->bytes != focus->bytes ||
	    ddflag != cluster->ddflag) {
		cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
	}

done:
	*key_nextp = key_accum;
#if 0
	/*
	 * For now don't re-resolve cluster->flags.
	 */
	hammer2_cluster_resolve(cluster);
#endif
}

/*
 * Create a new cluster using the specified key
 */
int
hammer2_cluster_create(hammer2_trans_t *trans, hammer2_cluster_t *cparent,
		     hammer2_cluster_t **clusterp,
		     hammer2_key_t key, int keybits,
		     int type, size_t bytes, int flags)
{
	hammer2_cluster_t *cluster;
	hammer2_pfs_t *pmp;
	int error;
	int i;

	pmp = trans->pmp;				/* can be NULL */

	if ((cluster = *clusterp) == NULL) {
		cluster = kmalloc(sizeof(*cluster), M_HAMMER2,
				  M_WAITOK | M_ZERO);
		cluster->pmp = pmp;			/* can be NULL */
		cluster->refs = 1;
		cluster->flags = HAMMER2_CLUSTER_LOCKED;
	}
	cluster->focus_index = 0;
	cluster->focus = NULL;

	/*
	 * NOTE: cluster->array[] entries can initially be NULL.  If
	 *	 *clusterp is supplied, skip NULL entries, otherwise
	 *	 create new chains.
	 */
	for (i = 0; i < cparent->nchains; ++i) {
		if ((cparent->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		if (*clusterp) {
			if ((cluster->array[i].flags &
			     HAMMER2_CITEM_FEMOD) == 0) {
				cluster->array[i].flags |=
						HAMMER2_CITEM_INVALID;
				continue;
			}
			if (cluster->array[i].chain == NULL)
				continue;
		}
		error = hammer2_chain_create(trans, &cparent->array[i].chain,
					     &cluster->array[i].chain, pmp,
					     key, keybits,
					     type, bytes, flags);
		if (cparent->focus_index == i)
			cparent->focus = cparent->array[i].chain;
		KKASSERT(error == 0);
		if (cluster->focus == NULL) {
			cluster->focus_index = i;
			cluster->focus = cluster->array[i].chain;
		}
		if (cparent->focus == cparent->array[i].chain) {
			cluster->focus_index = i;
			cluster->focus = cluster->array[i].chain;
		}
	}
	cluster->nchains = i;
	*clusterp = cluster;
	hammer2_cluster_resolve(cluster);

	return error;
}

/*
 * Rename a cluster to a new parent.
 *
 * WARNING! Any passed-in bref is probaly from hammer2_cluster_bref(),
 *	    So the data_off field is not relevant.  Only the key and
 *	    keybits are used.
 */
void
hammer2_cluster_rename(hammer2_trans_t *trans, hammer2_blockref_t *bref,
		       hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		       int flags)
{
	hammer2_chain_t *chain;
	hammer2_blockref_t xbref;
	int i;

#if 0
	cluster->focus = NULL;
	cparent->focus = NULL;
	cluster->focus_index = 0;
	cparent->focus_index = 0;
#endif

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		chain = cluster->array[i].chain;
		if (chain) {
			if (bref) {
				xbref = chain->bref;
				xbref.key = bref->key;
				xbref.keybits = bref->keybits;
				hammer2_chain_rename(trans, &xbref,
						     &cparent->array[i].chain,
						     chain, flags);
			} else {
				hammer2_chain_rename(trans, NULL,
						     &cparent->array[i].chain,
						     chain, flags);
			}
			if (cparent->focus_index == i)
				cparent->focus = cparent->array[i].chain;
			KKASSERT(cluster->array[i].chain == chain); /*remove*/
		}
	}
}

/*
 * Mark a cluster deleted
 */
void
hammer2_cluster_delete(hammer2_trans_t *trans, hammer2_cluster_t *cparent,
		       hammer2_cluster_t *cluster, int flags)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	int i;

	if (cparent == NULL) {
		kprintf("cparent is NULL\n");
		return;
	}

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0) {
			cluster->array[i].flags |= HAMMER2_CITEM_INVALID;
			continue;
		}
		parent = cparent->array[i].chain;
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		if (chain->parent != parent) {
			kprintf("hammer2_cluster_delete: parent "
				"mismatch chain=%p parent=%p against=%p\n",
				chain, chain->parent, parent);
		} else {
			hammer2_chain_delete(trans, parent, chain, flags);
		}
	}
}

/*
 * Create a snapshot of the specified {parent, ochain} with the specified
 * label.  The originating hammer2_inode must be exclusively locked for
 * safety.
 *
 * The ioctl code has already synced the filesystem.
 */
int
hammer2_cluster_snapshot(hammer2_trans_t *trans, hammer2_cluster_t *ocluster,
		       hammer2_ioc_pfs_t *pfs)
{
	hammer2_dev_t *hmp;
	hammer2_cluster_t *ncluster;
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_data_t *wipdata;
	hammer2_chain_t *nchain;
	hammer2_inode_t *nip;
	size_t name_len;
	hammer2_key_t lhc;
	struct vattr vat;
#if 0
	uuid_t opfs_clid;
#endif
	int error;
	int i;

	kprintf("snapshot %s\n", pfs->name);

	name_len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, name_len);

	/*
	 * Get the clid
	 */
	ripdata = &hammer2_cluster_rdata(ocluster)->ipdata;
#if 0
	opfs_clid = ripdata->pfs_clid;
#endif
	hmp = ocluster->focus->hmp;	/* XXX find synchronized local disk */

	/*
	 * Create the snapshot directory under the super-root
	 *
	 * Set PFS type, generate a unique filesystem id, and generate
	 * a cluster id.  Use the same clid when snapshotting a PFS root,
	 * which theoretically allows the snapshot to be used as part of
	 * the same cluster (perhaps as a cache).
	 *
	 * Copy the (flushed) blockref array.  Theoretically we could use
	 * chain_duplicate() but it becomes difficult to disentangle
	 * the shared core so for now just brute-force it.
	 */
	VATTR_NULL(&vat);
	vat.va_type = VDIR;
	vat.va_mode = 0755;
	ncluster = NULL;
	nip = hammer2_inode_create(trans, hmp->spmp->iroot, &vat,
				   proc0.p_ucred, pfs->name, name_len,
				   &ncluster,
				   HAMMER2_INSERT_PFSROOT, &error);

	if (nip) {
		wipdata = hammer2_cluster_modify_ip(trans, nip, ncluster, 0);
		wipdata->pfs_type = HAMMER2_PFSTYPE_MASTER;
		wipdata->pfs_subtype = HAMMER2_PFSSUBTYPE_SNAPSHOT;
		wipdata->op_flags |= HAMMER2_OPFLAG_PFSROOT;
		kern_uuidgen(&wipdata->pfs_fsid, 1);

		/*
		 * Give the snapshot its own private cluster.  As a snapshot
		 * no further synchronization with the original cluster will
		 * be done.
		 */
#if 0
		if (ocluster->focus->flags & HAMMER2_CHAIN_PFSBOUNDARY)
			wipdata->pfs_clid = opfs_clid;
		else
			kern_uuidgen(&wipdata->pfs_clid, 1);
#endif
		kern_uuidgen(&wipdata->pfs_clid, 1);

		for (i = 0; i < ncluster->nchains; ++i) {
			if ((ncluster->array[i].flags &
			     HAMMER2_CITEM_FEMOD) == 0) {
				ncluster->array[i].flags |=
					HAMMER2_CITEM_INVALID;
				continue;
			}
			nchain = ncluster->array[i].chain;
			if (nchain)
				nchain->bref.flags |= HAMMER2_BREF_FLAG_PFSROOT;
		}
#if 0
		/* XXX can't set this unless we do an explicit flush, which
		   we also need a pmp assigned to do, else the flush code
		   won't flush ncluster because it thinks it is crossing a
		   flush boundary */
		hammer2_cluster_set_chainflags(ncluster,
					       HAMMER2_CHAIN_PFSBOUNDARY);
#endif

		/* XXX hack blockset copy */
		/* XXX doesn't work with real cluster */
		KKASSERT(ocluster->nchains == 1);
		wipdata->u.blockset = ripdata->u.blockset;
		hammer2_cluster_modsync(ncluster);
		for (i = 0; i < ncluster->nchains; ++i) {
			nchain = ncluster->array[i].chain;
			if (nchain)
				hammer2_flush(trans, nchain, 1);
		}
		hammer2_inode_unlock(nip, ncluster);
	}
	return (error);
}

/*
 * Return locked parent cluster given a locked child.  The child remains
 * locked on return.  The new parent's focus follows the child's focus
 * and the parent is always resolved.
 *
 * We must temporarily unlock the passed-in cluster to avoid a deadlock
 * between elements of the cluster.
 *
 * We must not try to hammer2_cluster_resolve() cparent.  The individual
 * parent chains for the nodes are the correct parents for the cluster but
 * do not necessarily match, so resolve would likely implode.
 */
hammer2_cluster_t *
hammer2_cluster_parent(hammer2_cluster_t *cluster)
{
	hammer2_cluster_t *cparent;
	int i;

	cparent = hammer2_cluster_copy(cluster);
	hammer2_cluster_unlock(cluster);

	for (i = 0; i < cparent->nchains; ++i) {
		hammer2_chain_t *chain;
		hammer2_chain_t *rchain;

		/*
		 * Calculate parent for each element.  Old chain has an extra
		 * ref for cparent but the lock remains with cluster.
		 */
		chain = cparent->array[i].chain;
		if (chain == NULL)
			continue;
		while ((rchain = chain->parent) != NULL) {
			hammer2_chain_ref(rchain);
			hammer2_chain_lock(rchain, HAMMER2_RESOLVE_ALWAYS);
			if (chain->parent == rchain)
				break;
			hammer2_chain_unlock(rchain);
			hammer2_chain_drop(rchain);
		}
		cparent->array[i].chain = rchain;
		hammer2_chain_drop(chain);
	}
	cparent->flags |= HAMMER2_CLUSTER_LOCKED;
	/* hammer2_cluster_resolve(cparent); */
	hammer2_cluster_lock(cluster, HAMMER2_RESOLVE_ALWAYS);

	return cparent;
}

/************************************************************************
 *			        CLUSTER I/O 				*
 ************************************************************************
 *
 *
 * WARNING! blockref[] array data is not universal.  These functions should
 *	    only be used to access universal data.
 *
 * NOTE!    The rdata call will wait for at least one of the chain I/Os to
 *	    complete if necessary.  The I/O's should have already been
 *	    initiated by the cluster_lock/chain_lock operation.
 *
 *	    The cluster must already be in a modified state before wdata
 *	    is called.  The data will already be available for this case.
 */
const hammer2_media_data_t *
hammer2_cluster_rdata(hammer2_cluster_t *cluster)
{
	return(cluster->focus->data);
}

hammer2_media_data_t *
hammer2_cluster_wdata(hammer2_cluster_t *cluster)
{
	KKASSERT(hammer2_cluster_modified(cluster));
	return(cluster->focus->data);
}

/*
 * Load cluster data asynchronously with callback.
 *
 * The callback is made for the first validated data found, or NULL
 * if no valid data is available.
 *
 * NOTE! The cluster structure is either unique or serialized (e.g. embedded
 *	 in the inode with an exclusive lock held), the chain structure may be
 *	 shared.
 */
void
hammer2_cluster_load_async(hammer2_cluster_t *cluster,
			   void (*callback)(hammer2_iocb_t *iocb), void *ptr)
{
	hammer2_chain_t *chain;
	hammer2_iocb_t *iocb;
	hammer2_dev_t *hmp;
	hammer2_blockref_t *bref;
	int i;

	i = cluster->focus_index;
	chain = cluster->focus;

	iocb = &cluster->iocb;
	iocb->callback = callback;
	iocb->dio = NULL;		/* for already-validated case */
	iocb->cluster = cluster;
	iocb->chain = chain;
	iocb->ptr = ptr;
	iocb->lbase = (off_t)i;
	iocb->flags = 0;
	iocb->error = 0;

	/*
	 * Data already validated
	 */
	if (chain->data) {
		callback(iocb);
		return;
	}

	/*
	 * We must resolve to a device buffer, either by issuing I/O or
	 * by creating a zero-fill element.  We do not mark the buffer
	 * dirty when creating a zero-fill element (the hammer2_chain_modify()
	 * API must still be used to do that).
	 *
	 * The device buffer is variable-sized in powers of 2 down
	 * to HAMMER2_MIN_ALLOC (typically 1K).  A 64K physical storage
	 * chunk always contains buffers of the same size. (XXX)
	 *
	 * The minimum physical IO size may be larger than the variable
	 * block size.
	 *
	 * XXX TODO - handle HAMMER2_CHAIN_INITIAL for case where chain->bytes
	 *	      matches hammer2_devblksize()?  Or does the freemap's
	 *	      pre-zeroing handle the case for us?
	 */
	bref = &chain->bref;
	hmp = chain->hmp;

#if 0
	/* handled by callback? <- TODO XXX even needed for loads? */
	/*
	 * The getblk() optimization for a 100% overwrite can only be used
	 * if the physical block size matches the request.
	 */
	if ((chain->flags & HAMMER2_CHAIN_INITIAL) &&
	    chain->bytes == hammer2_devblksize(chain->bytes)) {
		error = hammer2_io_new(hmp, bref->data_off, chain->bytes, &dio);
		KKASSERT(error == 0);
		iocb->dio = dio;
		callback(iocb);
		return;
	}
#endif

	/*
	 * Otherwise issue a read
	 */
	hammer2_adjreadcounter(&chain->bref, chain->bytes);
	hammer2_io_getblk(hmp, bref->data_off, chain->bytes, iocb);
}
