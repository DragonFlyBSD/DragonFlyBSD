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

/*
 * Returns TRUE if any chain in the cluster needs to be resized.
 */
int
hammer2_cluster_need_resize(hammer2_cluster_t *cluster, int bytes)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain && chain->bytes != bytes)
			return 1;
	}
	return 0;
}

uint8_t
hammer2_cluster_type(hammer2_cluster_t *cluster)
{
	return(cluster->focus->bref.type);
}

/*
 * NOTE: When modifying a cluster object via hammer2_cluster_wdata()
 *	 and hammer2_cluster_modsync(), remember that block array
 *	 entries are not copied to the elements of the cluster.
 */
const hammer2_media_data_t *
hammer2_cluster_data(hammer2_cluster_t *cluster)
{
	return(cluster->focus->data);
}

hammer2_media_data_t *
hammer2_cluster_wdata(hammer2_cluster_t *cluster)
{
	return(cluster->focus->data);
}

int
hammer2_cluster_modified(hammer2_cluster_t *cluster)
{
	return((cluster->focus->flags & HAMMER2_CHAIN_MODIFIED) != 0);
}

/*
 * Return a bref representative of the cluster.  Any data offset is removed
 * (since it would only be applicable to a particular chain in the cluster).
 *
 * However, the radix portion of data_off is used for many purposes and will
 * be retained.
 */
void
hammer2_cluster_bref(hammer2_cluster_t *cluster, hammer2_blockref_t *bref)
{
	*bref = cluster->focus->bref;
	bref->data_off &= HAMMER2_OFF_MASK_RADIX;
}

void
hammer2_cluster_set_chainflags(hammer2_cluster_t *cluster, uint32_t flags)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain)
			atomic_set_int(&chain->flags, flags);
	}
}

void
hammer2_cluster_setflush(hammer2_trans_t *trans, hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain)
			hammer2_chain_setflush(trans, chain);
	}
}

void
hammer2_cluster_setmethod_check(hammer2_trans_t *trans,
				hammer2_cluster_t *cluster,
				int check_algo)
{
	hammer2_chain_t *chain;
	int i;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain) {
			KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);
			chain->bref.methods &= ~HAMMER2_ENC_CHECK(-1);
			chain->bref.methods |= HAMMER2_ENC_CHECK(check_algo);
		}
	}
}

/*
 * Create a cluster with one ref from the specified chain.  The chain
 * is not further referenced.  The caller typically supplies a locked
 * chain and transfers ownership to the cluster.
 */
hammer2_cluster_t *
hammer2_cluster_from_chain(hammer2_chain_t *chain)
{
	hammer2_cluster_t *cluster;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->array[0] = chain;
	cluster->nchains = 1;
	cluster->focus = chain;
	cluster->pmp = chain->pmp;
	cluster->refs = 1;

	return cluster;
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
	hammer2_cluster_t *rcluster;
	hammer2_chain_t *chain;
#if 0
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
#endif
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

	rcluster = &pmp->iroot->cluster;
	for (i = 0; i < rcluster->nchains; ++i) {
		chain = hammer2_chain_alloc(rcluster->array[i]->hmp,
					    pmp, trans, bref);
#if 0
		chain->hmp = rcluster->array[i]->hmp;
		chain->bref = *bref;
		chain->bytes = bytes;
		chain->refs = 1;
		chain->flags = HAMMER2_CHAIN_ALLOCATED;
#endif

		/*
		 * NOTE: When loading a chain from backing store or creating a
		 *	 snapshot, trans will be NULL and the caller is
		 *	 responsible for setting these fields.
		 */
		cluster->array[i] = chain;
	}
	cluster->nchains = i;
	cluster->pmp = pmp;
	cluster->focus = cluster->array[0];

	return (cluster);
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
	hammer2_chain_t *chain;
	int i;

	atomic_add_int(&cluster->refs, 1);
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain)
			hammer2_chain_ref(chain);
	}
}

/*
 * Drop the caller's reference to the cluster.  When the ref count drops to
 * zero this function frees the cluster and drops all underlying chains.
 */
void
hammer2_cluster_drop(hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cluster->refs > 0);
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain) {
			hammer2_chain_drop(chain);
			if (cluster->refs == 1)
				cluster->array[i] = NULL;
		}
	}
	if (atomic_fetchadd_int(&cluster->refs, -1) == 1) {
		cluster->focus = NULL;
		kfree(cluster, M_HAMMER2);
		/* cluster = NULL; safety */
	}
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
	hammer2_chain_t *chain;
	int i;
	int error;

	error = 0;
	atomic_add_int(&cluster->refs, 1);
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain) {
			error = hammer2_chain_lock(chain, how);
			if (error) {
				while (--i >= 0)
					hammer2_chain_unlock(cluster->array[i]);
				atomic_add_int(&cluster->refs, -1);
				break;
			}
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
	hammer2_chain_t *chain;
	int i;

	KKASSERT(dst->refs == 1);
	dst->focus = NULL;

	for (i = 0; i < src->nchains; ++i) {
		chain = src->array[i];
		if (chain) {
			hammer2_chain_ref(chain);
			if (i < dst->nchains && dst->array[i])
				hammer2_chain_unlock(dst->array[i]);
			dst->array[i] = chain;
			if (dst->focus == NULL)
				dst->focus = chain;
		}
	}
	while (i < dst->nchains) {
		chain = dst->array[i];
		if (chain) {
			hammer2_chain_unlock(chain);
			dst->array[i] = NULL;
		}
		++i;
	}
	dst->nchains = src->nchains;
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
	hammer2_chain_t *chain;
	int i;

	KKASSERT(dst->refs == 1);

	dst->focus = NULL;
	for (i = 0; i < src->nchains; ++i) {
		chain = src->array[i];
		if (chain) {
			hammer2_chain_lock(chain, 0);
			if (i < dst->nchains && dst->array[i])
				hammer2_chain_unlock(dst->array[i]);
			dst->array[i] = src->array[i];
			if (dst->focus == NULL)
				dst->focus = chain;
		}
	}
	while (i < dst->nchains) {
		chain = dst->array[i];
		if (chain) {
			hammer2_chain_unlock(chain);
			dst->array[i] = NULL;
		}
		++i;
	}
	dst->nchains = src->nchains;
}

/*
 * Copy a cluster, returned a ref'd cluster.  All underlying chains
 * are also ref'd, but not locked.
 *
 * If HAMMER2_CLUSTER_COPY_CHAINS is specified, the chains are copied
 * to the new cluster and a reference is nominally added to them and to
 * the cluster.  The cluster will have 1 ref.
 *
 * If HAMMER2_CLUSTER_COPY_NOREF is specified along with CHAINS, the chains
 * are copied but no additional references are made and the cluster will have
 * 0 refs.  Callers must ref the cluster and the chains before dropping it
 * (typically by locking it).
 *
 * If flags are passed as 0 the copy is setup as if it contained the chains
 * but the chains will not be copied over, and the cluster will have 0 refs.
 * Callers must ref the cluster before dropping it (typically by locking it).
 */
hammer2_cluster_t *
hammer2_cluster_copy(hammer2_cluster_t *ocluster, int copy_flags)
{
	hammer2_pfsmount_t *pmp = ocluster->pmp;
	hammer2_cluster_t *ncluster;
	hammer2_chain_t *chain;
	int i;

	ncluster = kmalloc(sizeof(*ncluster), M_HAMMER2, M_WAITOK | M_ZERO);
	ncluster->pmp = pmp;
	ncluster->nchains = ocluster->nchains;
	ncluster->refs = (copy_flags & HAMMER2_CLUSTER_COPY_NOREF) ? 0 : 1;
	if ((copy_flags & HAMMER2_CLUSTER_COPY_NOCHAINS) == 0) {
		ncluster->focus = ocluster->focus;
		for (i = 0; i < ocluster->nchains; ++i) {
			chain = ocluster->array[i];
			ncluster->array[i] = chain;
			if ((copy_flags & HAMMER2_CLUSTER_COPY_NOREF) == 0 &&
			    chain) {
				hammer2_chain_ref(chain);
			}
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
	hammer2_chain_t *chain;
	int i;

	KKASSERT(cluster->refs > 0);
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain) {
			hammer2_chain_unlock(chain);
			if (cluster->refs == 1)
				cluster->array[i] = NULL;	/* safety */
		}
	}
	if (atomic_fetchadd_int(&cluster->refs, -1) == 1) {
		cluster->focus = NULL;
		kfree(cluster, M_HAMMER2);
		/* cluster = NULL; safety */
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
	int i;

	KKASSERT(cparent->pmp == cluster->pmp);		/* can be NULL */
	KKASSERT(cparent->nchains == cluster->nchains);

	cluster->focus = NULL;
	for (i = 0; i < cluster->nchains; ++i) {
		if (cluster->array[i]) {
			KKASSERT(cparent->array[i]);
			hammer2_chain_resize(trans, ip,
					     cparent->array[i],
					     cluster->array[i],
					     nradix, flags);
			if (cluster->focus == NULL)
				cluster->focus = cluster->array[i];
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
 * Adjust the cluster's chains to allow modification.
 */
void
hammer2_cluster_modify(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
		       int flags)
{
	int i;

	cluster->focus = NULL;
	for (i = 0; i < cluster->nchains; ++i) {
		if (cluster->array[i]) {
			hammer2_chain_modify(trans, cluster->array[i], flags);
			if (cluster->focus == NULL)
				cluster->focus = cluster->array[i];
		}
	}
}

/*
 * Synchronize modifications with other chains in a cluster.
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
		scan = cluster->array[i];
		if (scan == NULL || scan == focus)
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
			/* fall through */
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
 * Lookup initialization/completion API
 */
hammer2_cluster_t *
hammer2_cluster_lookup_init(hammer2_cluster_t *cparent, int flags)
{
	hammer2_cluster_t *cluster;
	int i;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->pmp = cparent->pmp;			/* can be NULL */
	/* cluster->focus = NULL; already null */

	for (i = 0; i < cparent->nchains; ++i) {
		cluster->array[i] = cparent->array[i];
		if (cluster->focus == NULL)
			cluster->focus = cluster->array[i];
	}
	cluster->nchains = cparent->nchains;

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
	hammer2_key_t bref_key;
	int bref_keybits;
	int null_count;
	int ddflag;
	int i;
	uint8_t bref_type;
	u_int bytes;

	pmp = cparent->pmp;				/* can be NULL */
	key_accum = *key_nextp;
	null_count = 0;
	bref_type = 0;
	bref_key = 0;
	bref_keybits = 0;
	bytes = 0;

	cluster = kmalloc(sizeof(*cluster), M_HAMMER2, M_WAITOK | M_ZERO);
	cluster->pmp = pmp;				/* can be NULL */
	cluster->refs = 1;
	/* cluster->focus = NULL; already null */
	cparent->focus = NULL;
	*ddflagp = 0;

	for (i = 0; i < cparent->nchains; ++i) {
		key_next = *key_nextp;
		if (cparent->array[i] == NULL) {
			++null_count;
			continue;
		}
		chain = hammer2_chain_lookup(&cparent->array[i], &key_next,
					     key_beg, key_end,
					     &cparent->cache_index[i],
					     flags, &ddflag);
		if (cparent->focus == NULL)
			cparent->focus = cparent->array[i];
		cluster->array[i] = chain;
		if (chain == NULL) {
			++null_count;
		} else {
			if (cluster->focus == NULL) {
				bref_type = chain->bref.type;
				bref_key = chain->bref.key;
				bref_keybits = chain->bref.keybits;
				bytes = chain->bytes;
				*ddflagp = ddflag;
				cluster->focus = chain;
			}
			KKASSERT(bref_type == chain->bref.type);
			KKASSERT(bref_key == chain->bref.key);
			KKASSERT(bref_keybits == chain->bref.keybits);
			KKASSERT(bytes == chain->bytes);
			KKASSERT(*ddflagp == ddflag);
		}
		if (key_accum > key_next)
			key_accum = key_next;
	}
	*key_nextp = key_accum;
	cluster->nchains = i;

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
	cluster->focus = NULL;
	cparent->focus = NULL;

	for (i = 0; i < cparent->nchains; ++i) {
		key_next = *key_nextp;
		chain = cluster->array[i];
		if (chain == NULL) {
			if (cparent->focus == NULL)
				cparent->focus = cparent->array[i];
			++null_count;
			continue;
		}
		if (cparent->array[i] == NULL) {
			if (flags & HAMMER2_LOOKUP_NOLOCK)
				hammer2_chain_drop(chain);
			else
				hammer2_chain_unlock(chain);
			++null_count;
			continue;
		}
		chain = hammer2_chain_next(&cparent->array[i], chain,
					   &key_next, key_beg, key_end,
					   &cparent->cache_index[i], flags);
		if (cparent->focus == NULL)
			cparent->focus = cparent->array[i];
		cluster->array[i] = chain;
		if (chain == NULL) {
			++null_count;
		} else if (cluster->focus == NULL) {
			cluster->focus = chain;
		}
		if (key_accum > key_next)
			key_accum = key_next;
	}

	if (null_count == i) {
		hammer2_cluster_drop(cluster);
		cluster = NULL;
	}
	return(cluster);
}

#if 0
/*
 * XXX initial NULL cluster needs reworking (pass **clusterp ?)
 *
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
		chain = cluster->array[i];
		if (chain == NULL) {
			++null_count;
			continue;
		}
		if (cparent->array[i] == NULL) {
			if (flags & HAMMER2_LOOKUP_NOLOCK)
				hammer2_chain_drop(chain);
			else
				hammer2_chain_unlock(chain);
			++null_count;
			continue;
		}

		chain = hammer2_chain_scan(cparent->array[i], chain,
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

#endif

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
	cluster->focus = NULL;
	cparent->focus = NULL;

	/*
	 * NOTE: cluster->array[] entries can initially be NULL.  If
	 *	 *clusterp is supplied, skip NULL entries, otherwise
	 *	 create new chains.
	 */
	for (i = 0; i < cparent->nchains; ++i) {
		if (*clusterp && cluster->array[i] == NULL) {
			if (cparent->focus == NULL)
				cparent->focus = cparent->array[i];
			continue;
		}
		error = hammer2_chain_create(trans, &cparent->array[i],
					     &cluster->array[i], pmp,
					     key, keybits,
					     type, bytes, flags);
		KKASSERT(error == 0);
		if (cparent->focus == NULL)
			cparent->focus = cparent->array[i];
		if (cluster->focus == NULL)
			cluster->focus = cluster->array[i];
	}
	cluster->nchains = i;
	*clusterp = cluster;

	return error;
}

/*
 * Rename a cluster to a new parent.
 *
 * WARNING! Unlike hammer2_chain_rename(), only the key and keybits fields
 *	    are used from a passed-in non-NULL bref pointer.  All other fields
 *	    are extracted from the original chain for each chain in the
 *	    iteration.
 */
void
hammer2_cluster_rename(hammer2_trans_t *trans, hammer2_blockref_t *bref,
		       hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		       int flags)
{
	hammer2_chain_t *chain;
	hammer2_blockref_t xbref;
	int i;

	cluster->focus = NULL;
	cparent->focus = NULL;

	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain) {
			if (bref) {
				xbref = chain->bref;
				xbref.key = bref->key;
				xbref.keybits = bref->keybits;
				hammer2_chain_rename(trans, &xbref,
						     &cparent->array[i],
						     chain, flags);
			} else {
				hammer2_chain_rename(trans, NULL,
						     &cparent->array[i],
						     chain, flags);
			}
			cluster->array[i] = chain;
			if (cluster->focus == NULL)
				cluster->focus = chain;
			if (cparent->focus == NULL)
				cparent->focus = cparent->array[i];
		} else {
			if (cparent->focus == NULL)
				cparent->focus = cparent->array[i];
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
		parent = (i < cparent->nchains) ? cparent->array[i] : NULL;
		chain = cluster->array[i];
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
	hammer2_mount_t *hmp;
	hammer2_cluster_t *ncluster;
	const hammer2_inode_data_t *ipdata;
	hammer2_inode_data_t *wipdata;
	hammer2_inode_t *nip;
	size_t name_len;
	hammer2_key_t lhc;
	struct vattr vat;
	uuid_t opfs_clid;
	int error;
	int i;

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
	nip = hammer2_inode_create(trans, hmp->spmp->iroot, &vat,
				   proc0.p_ucred, pfs->name, name_len,
				   &ncluster, &error);

	if (nip) {
		wipdata = hammer2_cluster_modify_ip(trans, nip, ncluster, 0);
		wipdata->pfs_type = HAMMER2_PFSTYPE_SNAPSHOT;
		kern_uuidgen(&wipdata->pfs_fsid, 1);
		if (ocluster->focus->flags & HAMMER2_CHAIN_PFSBOUNDARY)
			wipdata->pfs_clid = opfs_clid;
		else
			kern_uuidgen(&wipdata->pfs_clid, 1);

		for (i = 0; i < ncluster->nchains; ++i) {
			if (ncluster->array[i]) {
				ncluster->array[i]->bref.flags |=
				    HAMMER2_BREF_FLAG_PFSROOT;
			}
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
		wipdata->u.blockset = ocluster->focus->data->ipdata.u.blockset;
		hammer2_cluster_modsync(ncluster);
		for (i = 0; i < ncluster->nchains; ++i) {
			if (ncluster->array[i])
				hammer2_flush(trans, ncluster->array[i]);
		}
		hammer2_inode_unlock_ex(nip, ncluster);
	}
	return (error);
}

/*
 * Return locked parent cluster given a locked child.  The child remains
 * locked on return.
 */
hammer2_cluster_t *
hammer2_cluster_parent(hammer2_cluster_t *cluster)
{
	hammer2_cluster_t *cparent;
	int i;

	cparent = hammer2_cluster_copy(cluster, HAMMER2_CLUSTER_COPY_NOCHAINS);
	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_chain_t *chain;
		hammer2_chain_t *rchain;

		chain = cluster->array[i];
		if (chain == NULL)
			continue;
		hammer2_chain_ref(chain);
		while ((rchain = chain->parent) != NULL) {
			hammer2_chain_ref(rchain);
			hammer2_chain_unlock(chain);
			hammer2_chain_lock(rchain, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_drop(rchain);
			if (chain->parent == rchain)
				break;
			hammer2_chain_unlock(rchain);
		}
		hammer2_chain_drop(chain);
		cparent->array[i] = rchain;
	}
	return cparent;
}

/************************************************************************
 *			    NODE FAILURES 				*
 ************************************************************************
 *
 * A node failure can occur for numerous reasons.
 *
 *	- A read I/O may fail
 *	- A write I/O may fail
 *	- An unexpected chain might be found (or be missing)
 *	- A node might disconnect temporarily and reconnect later
 *	  (for example, a USB stick could get pulled, or a node might
 *	  be programmatically disconnected).
 *	- A node might run out of space during a modifying operation.
 *
 * When a read failure or an unexpected chain state is found, the chain and
 * parent chain at the failure point for the nodes involved (the nodes
 * which we determine to be in error) are flagged as failed and removed
 * from the cluster.  The node itself is allowed to remain active.  The
 * highest common point (usually a parent chain) is queued to the
 * resynchronization thread for action.
 *
 * When a write I/O fails or a node runs out of space, we first adjust
 * as if a read failure occurs but we further disable flushes on the
 * ENTIRE node.  Concurrent modifying transactions are allowed to complete
 * but any new modifying transactions will automatically remove the node
 * from consideration in all related cluster structures and not generate
 * any new modified chains.  The ROOT chain for the failed node(s) is queued
 * to the resynchronization thread for action.
 *
 * A temporary disconnect is handled as if a write failure occurred.
 *
 * Any of these failures might or might not stall related high level VNOPS,
 * depending on what has failed, what nodes remain, the type of cluster,
 * and the operating state of the cluster.
 *
 *			    FLUSH ON WRITE-DISABLED NODES
 *
 * A flush on a write-disabled node is not allowed to write anything because
 * we cannot safely update the mirror_tid anywhere on the failed node.  The
 * synchronization thread uses mirror_tid to calculate incremental resyncs.
 * Dirty meta-data related to the failed node is thrown away.
 *
 * Dirty buffer cache buffers and inodes are only thrown away if they can be
 * retired... that is, if the filesystem still has enough nodes to complete
 * the operation.
 */

/************************************************************************
 *			SYNCHRONIZATION THREAD				*
 ************************************************************************
 *
 * This thread is responsible for [re]synchronizing the cluster representing
 * a PFS.  Any out-of-sync or failed node starts this thread on a
 * node-by-node basis when the failure is detected.
 *
 * Clusters needing resynchronization are queued at the highest point
 * where the parent on the failed node is still valid, or a special
 * incremental scan from the ROOT is queued if no parent exists.  This
 * thread is also responsible for waiting for reconnections of the failed
 * node if the cause was due to a disconnect, and waiting for space to be
 * freed up if the cause was due to running out of space.
 *
 * If the cause is due to a node running out of space, this thread will also
 * remove older (unlocked) snapshots to make new space, recover space, and
 * then start resynchronization.
 *
 * Each resynchronization pass virtually snapshots the PFS on the good nodes
 * and synchronizes using that snapshot against the target node.  This
 * ensures a consistent chain topology and also avoid interference between
 * the resynchronization thread and frontend operations.
 *
 * Since these are per-node threads it is possible to resynchronize several
 * nodes at once.
 */
