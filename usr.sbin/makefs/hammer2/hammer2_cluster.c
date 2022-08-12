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
/*
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
*/

#include "hammer2.h"

/*
 * Returns the bref type of the cluster's foucs.
 *
 * If the cluster is errored, returns HAMMER2_BREF_TYPE_EMPTY (0).
 * The cluster must be locked.
 */
uint8_t
hammer2_cluster_type(hammer2_cluster_t *cluster)
{
	if (cluster->error == 0) {
		KKASSERT(cluster->focus != NULL);
		return(cluster->focus->bref.type);
	}
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
	if (cluster->error == 0) {
		KKASSERT(cluster->focus != NULL);
		*bref = cluster->focus->bref;
		bref->data_off = 0;
	} else {
		bzero(bref, sizeof(*bref));
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
void
hammer2_dummy_xop_from_chain(hammer2_xop_head_t *xop, hammer2_chain_t *chain)
{
	hammer2_cluster_t *cluster;

	bzero(xop, sizeof(*xop));

	cluster = &xop->cluster;
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

void
hammer2_cluster_unhold(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		hammer2_chain_unhold(chain);
	}
}

void
hammer2_cluster_rehold(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		if (chain == NULL)
			continue;
		hammer2_chain_rehold(chain);
	}
}

/*
 * This is used by the XOPS subsystem to calculate the state of
 * the collection and tell hammer2_xop_collect() what to do with it.
 * The collection can be in various states of desynchronization, the
 * caller specifically wants to resolve the passed-in key.
 *
 * Return values (HAMMER2_ERROR_*):
 *
 *	0		- Quorum agreement, key is valid
 *
 *	ENOENT		- Quorum agreement, end of scan
 *
 *	ESRCH		- Quorum agreement, key is INVALID (caller should
 *			  skip key).
 *
 *	EIO		- Quorum agreement but all elements had errors.
 *
 *	EDEADLK		- No quorum agreement possible for key, a repair
 *			  may be needed.  Caller has to decide what to do,
 *			  possibly iterating the key or generating an EIO.
 *
 *	EINPROGRESS	- No quorum agreement yet, but agreement is still
 *			  possible if caller waits for more responses.  Caller
 *			  should not iterate key.
 *
 *	CHECK		- CRC check error
 *
 * NOTE! If the pmp is in HMNT2_LOCAL mode, the cluster check always succeeds.
 *
 * XXX needs to handle SOFT_MASTER and SOFT_SLAVE
 */
int
hammer2_cluster_check(hammer2_cluster_t *cluster, hammer2_key_t key, int flags)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *focus;
	hammer2_pfs_t *pmp;
	hammer2_tid_t quorum_tid;
	hammer2_tid_t last_best_quorum_tid;
	uint32_t nflags;
	int ttlmasters;
	int ttlslaves;
	int nmasters;
	int nmasters_keymatch;
	int nslaves;
	int nquorum;
	int umasters;	/* unknown masters (still in progress) */
	int error;
	int i;

	cluster->error = 0;
	cluster->focus = NULL;

	pmp = cluster->pmp;
	KKASSERT(pmp != NULL || cluster->nchains == 0);

	/*
	 * Calculate quorum
	 */
	nquorum = pmp ? pmp->pfs_nmasters / 2 + 1 : 0;
	nflags = 0;
	ttlmasters = 0;
	ttlslaves = 0;

	/*
	 * Pass 1
	 *
	 * NOTE: A NULL chain is not necessarily an error, it could be
	 *	 e.g. a lookup failure or the end of an iteration.
	 *	 Process normally.
	 */
	for (i = 0; i < cluster->nchains; ++i) {
		cluster->array[i].flags &= ~HAMMER2_CITEM_FEMOD;
		cluster->array[i].flags |= HAMMER2_CITEM_INVALID;

		chain = cluster->array[i].chain;
		error = cluster->array[i].error;
		if (chain && error) {
			if (cluster->focus == NULL || cluster->focus == chain) {
				/* error will be overridden by valid focus */
				/* XXX */
			}

			/*
			 * Must count total masters and slaves whether the
			 * chain is errored or not.
			 */
			switch (cluster->pmp->pfs_types[i]) {
			case HAMMER2_PFSTYPE_SUPROOT:
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
			++ttlmasters;
			nflags |= HAMMER2_CLUSTER_WRHARD;
			nflags |= HAMMER2_CLUSTER_RDHARD;
			cluster->focus_index = i;
			cluster->focus = chain;
			cluster->error = error;
			break;
		default:
			break;
		}
	}

	/*
	 * Pass 2
	 *
	 * Resolve nmasters		- master nodes fully match
	 *
	 * Resolve umasters		- master nodes operation still
	 *				  in progress
	 *
	 * Resolve nmasters_keymatch	- master nodes match the passed-in
	 *				  key and may or may not match
	 *				  the quorum-agreed tid.
	 *
	 * The quorum-agreed TID is the highest matching TID.
	 */
	last_best_quorum_tid = HAMMER2_TID_MAX;
	umasters = 0;
	nmasters = 0;
	nmasters_keymatch = 0;
	quorum_tid = 0;		/* fix gcc warning */

	while (nmasters < nquorum && last_best_quorum_tid != 0) {
		umasters = 0;
		nmasters = 0;
		nmasters_keymatch = 0;
		quorum_tid = 0;

		for (i = 0; i < cluster->nchains; ++i) {
			/* XXX SOFT smpresent handling */
			switch(cluster->pmp->pfs_types[i]) {
			case HAMMER2_PFSTYPE_MASTER:
			case HAMMER2_PFSTYPE_SUPROOT:
				break;
			default:
				continue;
			}

			chain = cluster->array[i].chain;
			error = cluster->array[i].error;

			/*
			 * Skip elements still in progress.  umasters keeps
			 * track of masters that might still be in-progress.
			 */
			if (chain == NULL && (cluster->array[i].flags &
					      HAMMER2_CITEM_NULL) == 0) {
				++umasters;
				continue;
			}

			/*
			 * Key match?
			 */
			if (flags & HAMMER2_CHECK_NULL) {
				if (chain == NULL) {
					++nmasters;
					++nmasters_keymatch;
					if (cluster->error == 0)
						cluster->error = error;
				}
			} else if (chain &&
				   (key == (hammer2_key_t)-1 ||
				    chain->bref.key == key)) {
				++nmasters_keymatch;

				if (chain->bref.modify_tid <
				     last_best_quorum_tid &&
				    quorum_tid < chain->bref.modify_tid) {
					/*
					 * Select new TID as master if better
					 * than any found so far in this loop,
					 * as long as it does not reach the
					 * best tid found in the previous loop.
					 */
					nmasters = 0;
					quorum_tid = chain->bref.modify_tid;
				}
				if (quorum_tid == chain->bref.modify_tid) {
					/*
					 * TID matches current collection.
					 *
					 * (error handled in next pass)
					 */
					++nmasters;
					if (chain->error == 0) {
						cluster->focus = chain;
						cluster->focus_index = i;
					}
				}
			}
		}
		if (nmasters >= nquorum)
			break;
		last_best_quorum_tid = quorum_tid;
	}

	/*
	kprintf("nmasters %d/%d nmaster_keymatch=%d umasters=%d\n",
		nmasters, nquorum, nmasters_keymatch, umasters);
	*/

	/*
	 * Early return if we do not have enough masters.
	 */
	if (nmasters < nquorum) {
		if (nmasters + umasters >= nquorum)
			return HAMMER2_ERROR_EINPROGRESS;
		if (nmasters_keymatch < nquorum)
			return HAMMER2_ERROR_ESRCH;
		return HAMMER2_ERROR_EDEADLK;
	}

	/*
	 * Validated end of scan.
	 */
	if (flags & HAMMER2_CHECK_NULL) {
		if (cluster->error == 0)
			cluster->error = HAMMER2_ERROR_ENOENT;
		return cluster->error;
	}

	/*
	 * If we have a NULL focus at this point the agreeing quorum all
	 * had chain errors.
	 */
	if (cluster->focus == NULL)
		return HAMMER2_ERROR_EIO;

	/*
	 * Pass 3
	 *
	 * We have quorum agreement, validate elements, not end of scan.
	 */
	nslaves = 0;
	cluster->error = 0;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i].chain;
		error = cluster->array[i].error;
		if (chain == NULL ||
		    chain->bref.key != key ||
		    chain->bref.modify_tid != quorum_tid) {
			continue;
		}

		/*
		 * Quorum Match
		 *
		 * XXX for now, cumulative error.
		 */
		if (cluster->error == 0)
			cluster->error = error;

		switch (cluster->pmp->pfs_types[i]) {
		case HAMMER2_PFSTYPE_MASTER:
			cluster->array[i].flags |= HAMMER2_CITEM_FEMOD;
			cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;
			nflags |= HAMMER2_CLUSTER_WRHARD;
			nflags |= HAMMER2_CLUSTER_RDHARD;
			break;
		case HAMMER2_PFSTYPE_SLAVE:
			/*
			 * We must have enough up-to-date masters to reach
			 * a quorum and the slave modify_tid must match the
			 * quorum's modify_tid.
			 *
			 * Do not select an errored slave.
			 */
			cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;
			nflags |= HAMMER2_CLUSTER_RDHARD;
			++nslaves;
			break;
		case HAMMER2_PFSTYPE_SOFT_MASTER:
			/*
			 * Directly mounted soft master always wins.  There
			 * should be only one.
			 */
			cluster->array[i].flags |= HAMMER2_CITEM_FEMOD;
			cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;
			break;
		case HAMMER2_PFSTYPE_SOFT_SLAVE:
			/*
			 * Directly mounted soft slave always wins.  There
			 * should be only one.
			 *
			 * XXX
			 */
			cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;
			break;
		case HAMMER2_PFSTYPE_SUPROOT:
			/*
			 * spmp (degenerate case)
			 */
			cluster->array[i].flags |= HAMMER2_CITEM_FEMOD;
			cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;
			nflags |= HAMMER2_CLUSTER_WRHARD;
			nflags |= HAMMER2_CLUSTER_RDHARD;
			break;
		default:
			break;
		}
	}

	/*
	 * Focus now set, adjust ddflag.  Skip this pass if the focus
	 * is bad or if we are at the PFS root (the bref won't match at
	 * the PFS root, obviously).
	 *
	 * focus is probably not locked and it isn't safe to test its
	 * content (e.g. focus->data, focus->dio, other content).  We
	 * do not synchronize the dio to the cpu here.  In fact, in numerous
	 * situations the frontend doesn't even need to access its dio/data,
	 * so synchronizing it here would be wasteful.
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
			kprintf("cluster_check: matching modify_tid failed "
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
				panic("cluster_check");
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
	 */
	atomic_set_int(&cluster->flags, nflags);
	atomic_clear_int(&cluster->flags, HAMMER2_CLUSTER_ZFLAGS & ~nflags);

	return cluster->error;
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
