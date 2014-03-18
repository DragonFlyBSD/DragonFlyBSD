/*
 * Copyright (c) 2013-2014 The DragonFly Project.  All rights reserved.
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
 * information into a single entity.  It allows direct access to media
 * data as long as it is not blockref array data.  Meaning, basically,
 * just inode and file data.
 *
 * This module also handles I/O dispatch, status rollup, and various
 * mastership arrangements including quorum operations.  It effectively
 * presents one topology to the vnops layer.
 *
 * Many of the API calls mimic chain API calls but operate on clusters
 * instead of chains.  Please see hammer2_chain.c for more complete code
 * documentation of the API functions.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

u_int
hammer2_cluster_bytes(hammer2_cluster_t *cluster)
{
	return(cluster->focus->bytes);
}

uint8_t
hammer2_cluster_type(hammer2_cluster_t *cluster)
{
	return(cluster->focus->bref.type);
}

hammer2_media_data_t *
hammer2_cluster_data(hammer2_cluster_t *cluster)
{
	return(cluster->focus->data);
}

int
hammer2_cluster_modified(hammer2_cluster_t *cluster)
{
	return((cluster->focus->flags & HAMMER2_CHAIN_MODIFIED) != 0);
}

int
hammer2_cluster_unlinked(hammer2_cluster_t *cluster)
{
	return((cluster->focus->flags & HAMMER2_CHAIN_UNLINKED) != 0);
}

void
hammer2_cluster_bref(hammer2_cluster_t *cluster, hammer2_blockref_t *bref)
{
	*bref = cluster->focus->bref;
	bref->data_off = 0;	/* should be opaque to caller */
}

void
hammer2_cluster_set_chainflags(hammer2_cluster_t *cluster, uint32_t flags)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i)
		atomic_set_int(&cluster->array[i]->flags, flags);
}

void
hammer2_cluster_setsubmod(hammer2_trans_t *trans, hammer2_cluster_t *cluster)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i)
		hammer2_chain_setsubmod(trans, cluster->array[i]);
}

/*
 * Allocates a cluster and its underlying chain structures.  The underlying
 * chains will be locked.  The cluster and underlying chains will have one
 * ref.
 */
hammer2_cluster_t *
hammer2_cluster_alloc(hammer2_pfsmount_t *pmp,
		      hammer2_trans_t *trans, hammer2_blockref_t *bref)
{
	hammer2_cluster_t *cluster;
	hammer2_chain_t *chain;
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	int i;

	KKASSERT(pmp != NULL);

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/*
		 * Chain's are really only associated with the hmp but we
		 * maintain a pmp association for per-mount memory tracking
		 * purposes.  The pmp can be NULL.
		 */
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
		chain = NULL;
		panic("hammer2_cluster_alloc volume type illegal for op");
	default:
		chain = NULL;
		panic("hammer2_cluster_alloc: unrecognized blockref type: %d",
		      bref->type);
	}

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->refs = 1;

	for (i = 0; i < pmp->cluster.nchains; ++i) {
		chain = hammer2_chain_alloc(pmp->cluster.array[i]->hmp, pmp,
					    trans, bref);
		chain->pmp = pmp;
		chain->hmp = pmp->cluster.array[i]->hmp;
		chain->bref = *bref;
		chain->bytes = bytes;
		chain->refs = 1;
		chain->flags = HAMMER2_CHAIN_ALLOCATED;
		chain->delete_tid = HAMMER2_MAX_TID;

		/*
		 * Set modify_tid if a transaction is creating the inode.
		 * Enforce update_lo = 0 so nearby transactions do not think
		 * it has been flushed when it hasn't.
		 *
		 * NOTE: When loading a chain from backing store or creating a
		 *	 snapshot, trans will be NULL and the caller is
		 *	 responsible for setting these fields.
		 */
		if (trans) {
			chain->modify_tid = trans->sync_tid;
			chain->update_lo = 0;
		}
		cluster->array[i] = chain;
	}
	cluster->nchains = i;
	cluster->pmp = pmp;
	cluster->focus = cluster->array[0];

	return (cluster);
}

/*
 * Associate an existing core with the chain or allocate a new core.
 *
 * The core is not locked.  No additional refs on the chain are made.
 * (trans) must not be NULL if (core) is not NULL.
 *
 * When chains are delete-duplicated during flushes we insert nchain on
 * the ownerq after ochain instead of at the end in order to give the
 * drop code visibility in the correct order, otherwise drops can be missed.
 */
void
hammer2_cluster_core_alloc(hammer2_trans_t *trans,
			   hammer2_cluster_t *ncluster,
			   hammer2_cluster_t *ocluster)
{
	int i;

	for (i = 0; i < ocluster->nchains; ++i) {
		hammer2_chain_core_alloc(trans,
					 ncluster->array[i],
					 ocluster->array[i]);
	}
}

/*
 * Add a reference to a cluster.
 *
 * We must also ref the underlying chains in order to allow ref/unlock
 * sequences to later re-lock.
 */
void
hammer2_cluster_ref(hammer2_cluster_t *cluster)
{
	int i;

	atomic_add_int(&cluster->refs, 1);
	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_ref(cluster->array[i]);
	}
}

/*
 * Drop the caller's reference to the cluster.  When the ref count drops to
 * zero this function frees the cluster and drops all underlying chains.
 */
void
hammer2_cluster_drop(hammer2_cluster_t *cluster)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_drop(cluster->array[i]);
		if (cluster->refs == 1)
			cluster->array[i] = NULL;
	}
	if (atomic_fetchadd_int(&cluster->refs, -1) != 1) {
		KKASSERT(cluster->refs > 0);
		return;
	}
	kfree(cluster, M_HAMMER2);
}

void
hammer2_cluster_wait(hammer2_cluster_t *cluster)
{
	tsleep(cluster->focus, 0, "h2clcw", 1);
}

/*
 * Lock and ref a cluster.  This adds a ref to the cluster and its chains
 * and then locks them.
 */
int
hammer2_cluster_lock(hammer2_cluster_t *cluster, int how)
{
	int i;
	int error;

	error = 0;
	atomic_add_int(&cluster->refs, 1);
	for (i = 0; i < cluster->nchains; ++i) {
		error = hammer2_chain_lock(cluster->array[i], how);
		if (error) {
			while (--i >= 0)
				hammer2_chain_unlock(cluster->array[i]);
			atomic_add_int(&cluster->refs, -1);
			break;
		}
	}
	return error;
}

/*
 * Replace the contents of dst with src, adding a reference to src's chains.
 * dst is assumed to already have a ref and any chains present in dst are
 * assumed to be locked and will be unlocked.
 *
 * If the chains in src are locked, only one of (src) or (dst) should be
 * considered locked by the caller after return, not both.
 */
void
hammer2_cluster_replace(hammer2_cluster_t *dst, hammer2_cluster_t *src)
{
	int i;

	KKASSERT(dst->refs == 1);

	for (i = 0; i < src->nchains; ++i) {
		hammer2_chain_ref(src->array[i]);
		if (i < dst->nchains)
			hammer2_chain_unlock(dst->array[i]);
		dst->array[i] = src->array[i];
	}
	while (i < dst->nchains) {
		hammer2_chain_unlock(dst->array[i]);
		dst->array[i] = NULL;
		++i;
	}
	dst->nchains = src->nchains;
	dst->focus = src->focus;
}

/*
 * Replace the contents of the locked destination with the contents of the
 * locked source.  Destination must have one ref.
 *
 * Returns with the destination still with one ref and the copied chains
 * with an additional lock (representing their state on the destination).
 * The original chains associated with the destination are unlocked.
 */
void
hammer2_cluster_replace_locked(hammer2_cluster_t *dst, hammer2_cluster_t *src)
{
	int i;

	KKASSERT(dst->refs == 1);

	for (i = 0; i < src->nchains; ++i) {
		hammer2_chain_lock(src->array[i], 0);
		if (i < dst->nchains)
			hammer2_chain_unlock(dst->array[i]);
		dst->array[i] = src->array[i];
	}
	while (i < dst->nchains) {
		hammer2_chain_unlock(dst->array[i]);
		dst->array[i] = NULL;
		++i;
	}
	dst->nchains = src->nchains;
	dst->focus = src->focus;
}

/*
 * Copy a cluster, returned a ref'd cluster.  All underlying chains
 * are also ref'd, but not locked.
 *
 * If with_chains is 0 the returned cluster has a ref count of 1 but
 * no chains will be assigned.
 */
hammer2_cluster_t *
hammer2_cluster_copy(hammer2_cluster_t *ocluster, int with_chains)
{
	hammer2_pfsmount_t *pmp = ocluster->pmp;
	hammer2_cluster_t *ncluster;
	int i;

	ncluster = kmalloc(sizeof(*ncluster), M_HAMMER2, M_WAITOK | M_ZERO);
	ncluster->pmp = pmp;
	ncluster->nchains = ocluster->nchains;
	ncluster->focus = ocluster->focus;
	if (with_chains) {
		ncluster->refs = 1;
		for (i = 0; i < ocluster->nchains; ++i) {
			ncluster->array[i] = ocluster->array[i];
			hammer2_chain_ref(ncluster->array[i]);
		}
	}
	return (ncluster);
}

/*
 * Unlock and deref a cluster.  The cluster is destroyed if this is the
 * last ref.
 */
void
hammer2_cluster_unlock(hammer2_cluster_t *cluster)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i)
		hammer2_chain_unlock(cluster->array[i]);
	if (atomic_fetchadd_int(&cluster->refs, -1) == 1) {
		for (i = 0; i < cluster->nchains; ++i)	/* safety */
			cluster->array[i] = NULL;
		kfree(cluster, M_HAMMER2);
		return;
	}
	KKASSERT(cluster->refs > 0);
}

/*
 * Refactor the chains of a locked cluster
 */
void
hammer2_cluster_refactor(hammer2_cluster_t *cluster)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_refactor(&cluster->array[i]);
	}
	cluster->focus = cluster->array[0];
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
	int i;

	KKASSERT(cparent->pmp == cluster->pmp);		/* can be NULL */
	KKASSERT(cparent->nchains == cluster->nchains);

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_resize(trans, ip,
				     cparent->array[i], &cluster->array[i],
				     nradix, flags);
	}
	cluster->focus = cluster->array[0];
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
	return (&hammer2_cluster_data(cluster)->ipdata);
}

/*
 * Adjust the cluster's chains to allow modification.
 */
void
hammer2_cluster_modify(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
		       int flags)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i)
		hammer2_chain_modify(trans, &cluster->array[i], flags);
	cluster->focus = cluster->array[0];
}

/*
 * Lookup initialization/completion API
 */
hammer2_cluster_t *
hammer2_cluster_lookup_init(hammer2_cluster_t *cparent, int flags)
{
	hammer2_cluster_t *cluster;
	int i;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->pmp = cparent->pmp;			/* can be NULL */
	for (i = 0; i < cparent->nchains; ++i)
		cluster->array[i] = cparent->array[i];
	cluster->nchains = cparent->nchains;
	cluster->focus = cluster->array[0];

	/*
	 * Independently lock (this will also give cluster 1 ref)
	 */
	if (flags & HAMMER2_LOOKUP_SHARED) {
		hammer2_cluster_lock(cluster, HAMMER2_RESOLVE_ALWAYS |
					      HAMMER2_RESOLVE_SHARED);
	} else {
		hammer2_cluster_lock(cluster, HAMMER2_RESOLVE_ALWAYS);
	}
	return (cluster);
}

void
hammer2_cluster_lookup_done(hammer2_cluster_t *cparent)
{
	if (cparent)
		hammer2_cluster_unlock(cparent);
}

/*
 * Locate first match or overlap under parent, return a new cluster
 */
hammer2_cluster_t *
hammer2_cluster_lookup(hammer2_cluster_t *cparent, hammer2_key_t *key_nextp,
		     hammer2_key_t key_beg, hammer2_key_t key_end,
		     int flags, int *ddflagp)
{
	hammer2_pfsmount_t *pmp;
	hammer2_cluster_t *cluster;
	hammer2_chain_t *chain;
	hammer2_key_t key_accum;
	hammer2_key_t key_next;
	int null_count;
	int ddflag;
	int i;
	uint8_t bref_type;
	u_int bytes;

	pmp = cparent->pmp;				/* can be NULL */
	key_accum = *key_nextp;
	null_count = 0;
	bref_type = 0;
	bytes = 0;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->pmp = pmp;				/* can be NULL */
	cluster->refs = 1;
	*ddflagp = 0;

	for (i = 0; i < cparent->nchains; ++i) {
		key_next = *key_nextp;
		chain = hammer2_chain_lookup(&cparent->array[i], &key_next,
					     key_beg, key_end,
					     &cparent->cache_index[i],
					     flags, &ddflag);
		cluster->array[i] = chain;
		if (chain == NULL) {
			++null_count;
		} else {
			if (bref_type == 0)
				bref_type = chain->bref.type;
			KKASSERT(bref_type == chain->bref.type);
			if (bytes == 0)
				bytes = chain->bytes;
			KKASSERT(bytes == chain->bytes);
		}
		if (key_accum > key_next)
			key_accum = key_next;
		KKASSERT(i == 0 || *ddflagp == ddflag);
		*ddflagp = ddflag;
	}
	*key_nextp = key_accum;
	cluster->nchains = i;
	cluster->focus = cluster->array[0];

	if (null_count == i) {
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	}

	return (cluster);
}

/*
 * Locate next match or overlap under parent, replace cluster
 */
hammer2_cluster_t *
hammer2_cluster_next(hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		     hammer2_key_t *key_nextp,
		     hammer2_key_t key_beg, hammer2_key_t key_end, int flags)
{
	hammer2_chain_t *chain;
	hammer2_key_t key_accum;
	hammer2_key_t key_next;
	int null_count;
	int i;

	key_accum = *key_nextp;
	null_count = 0;

	for (i = 0; i < cparent->nchains; ++i) {
		key_next = *key_nextp;
		chain = hammer2_chain_next(&cparent->array[i],
					   cluster->array[i],
					   &key_next,
					   key_beg, key_end,
					   &cparent->cache_index[i], flags);
		cluster->array[i] = chain;
		if (chain == NULL)
			++null_count;
		if (key_accum > key_next)
			key_accum = key_next;
	}

	if (null_count == i) {
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	} else {
		cluster->focus = cluster->array[0];
	}
	return(cluster);
}

/*
 * The raw scan function is similar to lookup/next but does not seek to a key.
 * Blockrefs are iterated via first_chain = (parent, NULL) and
 * next_chain = (parent, chain).
 *
 * The passed-in parent must be locked and its data resolved.  The returned
 * chain will be locked.  Pass chain == NULL to acquire the first sub-chain
 * under parent and then iterate with the passed-in chain (which this
 * function will unlock).
 */
hammer2_cluster_t *
hammer2_cluster_scan(hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		     int flags)
{
	hammer2_chain_t *chain;
	int null_count;
	int i;

	null_count = 0;

	for (i = 0; i < cparent->nchains; ++i) {
		chain = hammer2_chain_scan(cparent->array[i],
					   cluster->array[i],
					   &cparent->cache_index[i], flags);
		cluster->array[i] = chain;
		if (chain == NULL)
			++null_count;
	}

	if (null_count == i) {
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	}
	return(cluster);
}

/*
 * Create a new cluster using the specified key
 */
int
hammer2_cluster_create(hammer2_trans_t *trans, hammer2_cluster_t *cparent,
		     hammer2_cluster_t **clusterp,
		     hammer2_key_t key, int keybits, int type, size_t bytes)
{
	hammer2_cluster_t *cluster;
	hammer2_chain_t *chain;
	hammer2_pfsmount_t *pmp;
	int error;
	int i;

	pmp = trans->pmp;				/* can be NULL */

	if ((cluster = *clusterp) == NULL) {
		cluster = kmalloc(sizeof(*cluster), M_HAMMER2,
				  M_WAITOK | M_ZERO);
		cluster->pmp = pmp;			/* can be NULL */
		cluster->refs = 1;
	}
	for (i = 0; i < cparent->nchains; ++i) {
		chain = cluster->array[i];
		error = hammer2_chain_create(trans, &cparent->array[i], &chain,
					     key, keybits, type, bytes);
		KKASSERT(error == 0);
		cluster->array[i] = chain;
	}
	cluster->focus = cluster->array[0];
	*clusterp = cluster;

	return error;
}

/*
 * Duplicate a cluster under a new parent
 */
void
hammer2_cluster_duplicate(hammer2_trans_t *trans, hammer2_cluster_t *cparent,
			  hammer2_cluster_t *cluster, hammer2_blockref_t *bref,
			  int snapshot, int duplicate_reason)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_duplicate(trans, &cparent->array[i],
					&cluster->array[i], bref,
					snapshot, duplicate_reason);
	}
	cluster->focus = cluster->array[0];
}

/*
 * Delete-duplicate a cluster in-place.
 */
void
hammer2_cluster_delete_duplicate(hammer2_trans_t *trans,
				 hammer2_cluster_t *cluster, int flags)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_delete_duplicate(trans, &cluster->array[i],
					       flags);
	}
	cluster->focus = cluster->array[0];
}

/*
 * Mark a cluster deleted
 */
void
hammer2_cluster_delete(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
		       int flags)
{
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_delete(trans, cluster->array[i], flags);
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
	hammer2_mount_t *hmp;
	hammer2_cluster_t *ncluster;
	hammer2_inode_data_t *ipdata;
	hammer2_inode_t *nip;
	size_t name_len;
	hammer2_key_t lhc;
	struct vattr vat;
	uuid_t opfs_clid;
	int error;

	kprintf("snapshot %s\n", pfs->name);

	name_len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, name_len);

	ipdata = &hammer2_cluster_data(ocluster)->ipdata;
	opfs_clid = ipdata->pfs_clid;
	hmp = ocluster->focus->hmp;

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
	nip = hammer2_inode_create(trans, hmp->sroot, &vat, proc0.p_ucred,
				   pfs->name, name_len, &ncluster, &error);

	if (nip) {
		ipdata = hammer2_cluster_modify_ip(trans, nip, ncluster, 0);
		ipdata->pfs_type = HAMMER2_PFSTYPE_SNAPSHOT;
		kern_uuidgen(&ipdata->pfs_fsid, 1);
		if (ocluster->focus->flags & HAMMER2_CHAIN_PFSROOT)
			ipdata->pfs_clid = opfs_clid;
		else
			kern_uuidgen(&ipdata->pfs_clid, 1);
		hammer2_cluster_set_chainflags(ncluster, HAMMER2_CHAIN_PFSROOT);

		/* XXX hack blockset copy */
		ipdata->u.blockset = ocluster->focus->data->ipdata.u.blockset;

		hammer2_inode_unlock_ex(nip, ncluster);
	}
	return (error);
}
