/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

#define INODE_DEBUG	0

static void hammer2_inode_move_to_hidden(hammer2_trans_t *trans,
					 hammer2_cluster_t **cparentp,
					 hammer2_cluster_t **clusterp,
					 hammer2_tid_t inum);

RB_GENERATE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
	     hammer2_tid_t, meta.inum);

int
hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2)
{
	if (ip1->meta.inum < ip2->meta.inum)
		return(-1);
	if (ip1->meta.inum > ip2->meta.inum)
		return(1);
	return(0);
}

/*
 * HAMMER2 inode locks
 *
 * HAMMER2 offers shared and exclusive locks on inodes.  Pass a mask of
 * flags for options:
 *
 *	- pass HAMMER2_RESOLVE_SHARED if a shared lock is desired.  The
 *	  inode locking function will automatically set the RDONLY flag.
 *
 *	- pass HAMMER2_RESOLVE_ALWAYS if you need the inode's meta-data.
 *	  Most front-end inode locks do.
 *
 *	- pass HAMMER2_RESOLVE_NEVER if you do not want to require that
 *	  the inode data be resolved.  This is used by the syncthr because
 *	  it can run on an unresolved/out-of-sync cluster, and also by the
 *	  vnode reclamation code to avoid unnecessary I/O (particularly when
 *	  disposing of hundreds of thousands of cached vnodes).
 *
 * The inode locking function locks the inode itself, resolves any stale
 * chains in the inode's cluster, and allocates a fresh copy of the
 * cluster with 1 ref and all the underlying chains locked.
 *
 * ip->cluster will be stable while the inode is locked.
 *
 * NOTE: We don't combine the inode/chain lock because putting away an
 *       inode would otherwise confuse multiple lock holders of the inode.
 *
 * NOTE: In-memory inodes always point to hardlink targets (the actual file),
 *	 and never point to a hardlink pointer.
 *
 * NOTE: If caller passes HAMMER2_RESOLVE_RDONLY the exclusive locking code
 *	 will feel free to reduce the chain set in the cluster as an
 *	 optimization.  It will still be validated against the quorum if
 *	 appropriate, but the optimization might be able to reduce data
 *	 accesses to one node.  This flag is automatically set if the inode
 *	 is locked with HAMMER2_RESOLVE_SHARED.
 */
void
hammer2_inode_lock(hammer2_inode_t *ip, int how)
{
	hammer2_inode_ref(ip);

	/* 
	 * Inode structure mutex
	 */
	if (how & HAMMER2_RESOLVE_SHARED) {
		/*how |= HAMMER2_RESOLVE_RDONLY; not used */
		hammer2_mtx_sh(&ip->lock);
	} else {
		hammer2_mtx_ex(&ip->lock);
	}
}

/*
 * Create a locked copy of ip->cluster.  Note that the copy will have a
 * ref on the cluster AND its chains and we don't want a second ref to
 * either when we lock it.
 *
 * Exclusive inode locks set the template focus chain in (ip)
 * as a hint.  Cluster locks can ALWAYS replace the focus in the
 * working copy if the hint does not work out, so beware.
 */
hammer2_cluster_t *
hammer2_inode_cluster(hammer2_inode_t *ip, int how)
{
	hammer2_cluster_t *cluster;

	cluster = hammer2_cluster_copy(&ip->cluster);
	hammer2_cluster_lock(cluster, how);
	hammer2_cluster_resolve(cluster);

	/*
	 * cluster->focus will be set if resolving RESOLVE_ALWAYS, but
	 * only update the cached focus in the inode structure when taking
	 * out an exclusive lock.
	 */
	if ((how & HAMMER2_RESOLVE_SHARED) == 0)
		ip->cluster.focus = cluster->focus;

	return cluster;
}

/*
 * Select a chain out of an inode's cluster and lock it.
 */
hammer2_chain_t *
hammer2_inode_chain(hammer2_inode_t *ip, int clindex, int how)
{
	hammer2_chain_t *chain;

	if (clindex >= ip->cluster.nchains)
		chain = NULL;
	else
		chain = ip->cluster.array[clindex].chain;
	if (chain) {
		hammer2_chain_ref(chain);
		hammer2_chain_lock(chain, how);
	}
	return chain;
}

void
hammer2_inode_unlock(hammer2_inode_t *ip, hammer2_cluster_t *cluster)
{
	if (cluster) {
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
	}
	hammer2_mtx_unlock(&ip->lock);
	hammer2_inode_drop(ip);
}

/*
 * Temporarily release a lock held shared or exclusive.  Caller must
 * hold the lock shared or exclusive on call and lock will be released
 * on return.
 *
 * Restore a lock that was temporarily released.
 */
hammer2_mtx_state_t
hammer2_inode_lock_temp_release(hammer2_inode_t *ip)
{
	return hammer2_mtx_temp_release(&ip->lock);
}

void
hammer2_inode_lock_temp_restore(hammer2_inode_t *ip, hammer2_mtx_state_t ostate)
{
	hammer2_mtx_temp_restore(&ip->lock, ostate);
}

/*
 * Upgrade a shared inode lock to exclusive and return.  If the inode lock
 * is already held exclusively this is a NOP.
 *
 * The caller MUST hold the inode lock either shared or exclusive on call
 * and will own the lock exclusively on return.
 *
 * Returns non-zero if the lock was already exclusive prior to the upgrade.
 */
int
hammer2_inode_lock_upgrade(hammer2_inode_t *ip)
{
	int wasexclusive;

	if (mtx_islocked_ex(&ip->lock)) {
		wasexclusive = 1;
	} else {
		hammer2_mtx_unlock(&ip->lock);
		hammer2_mtx_ex(&ip->lock);
		wasexclusive = 0;
	}
	return wasexclusive;
}

/*
 * Downgrade an inode lock from exclusive to shared only if the inode
 * lock was previously shared.  If the inode lock was previously exclusive,
 * this is a NOP.
 */
void
hammer2_inode_lock_downgrade(hammer2_inode_t *ip, int wasexclusive)
{
	if (wasexclusive == 0)
		mtx_downgrade(&ip->lock);
}

/*
 * Lookup an inode by inode number
 */
hammer2_inode_t *
hammer2_inode_lookup(hammer2_pfs_t *pmp, hammer2_tid_t inum)
{
	hammer2_inode_t *ip;

	KKASSERT(pmp);
	if (pmp->spmp_hmp) {
		ip = NULL;
	} else {
		hammer2_spin_ex(&pmp->inum_spin);
		ip = RB_LOOKUP(hammer2_inode_tree, &pmp->inum_tree, inum);
		if (ip)
			hammer2_inode_ref(ip);
		hammer2_spin_unex(&pmp->inum_spin);
	}
	return(ip);
}

/*
 * Adding a ref to an inode is only legal if the inode already has at least
 * one ref.
 *
 * (can be called with spinlock held)
 */
void
hammer2_inode_ref(hammer2_inode_t *ip)
{
	atomic_add_int(&ip->refs, 1);
}

/*
 * Drop an inode reference, freeing the inode when the last reference goes
 * away.
 */
void
hammer2_inode_drop(hammer2_inode_t *ip)
{
	hammer2_pfs_t *pmp;
	hammer2_inode_t *pip;
	u_int refs;

	while (ip) {
		refs = ip->refs;
		cpu_ccfence();
		if (refs == 1) {
			/*
			 * Transition to zero, must interlock with
			 * the inode inumber lookup tree (if applicable).
			 * It should not be possible for anyone to race
			 * the transition to 0.
			 *
			 */
			pmp = ip->pmp;
			KKASSERT(pmp);
			hammer2_spin_ex(&pmp->inum_spin);

			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				KKASSERT(hammer2_mtx_refs(&ip->lock) == 0);
				if (ip->flags & HAMMER2_INODE_ONRBTREE) {
					atomic_clear_int(&ip->flags,
						     HAMMER2_INODE_ONRBTREE);
					RB_REMOVE(hammer2_inode_tree,
						  &pmp->inum_tree, ip);
				}
				hammer2_spin_unex(&pmp->inum_spin);

				pip = ip->pip;
				ip->pip = NULL;
				ip->pmp = NULL;

				/*
				 * Cleaning out ip->cluster isn't entirely
				 * trivial.
				 */
				hammer2_inode_repoint(ip, NULL, NULL);

				/*
				 * We have to drop pip (if non-NULL) to
				 * dispose of our implied reference from
				 * ip->pip.  We can simply loop on it.
				 */
				kfree(ip, pmp->minode);
				atomic_add_long(&pmp->inmem_inodes, -1);
				ip = pip;
				/* continue with pip (can be NULL) */
			} else {
				hammer2_spin_unex(&ip->pmp->inum_spin);
			}
		} else {
			/*
			 * Non zero transition
			 */
			if (atomic_cmpset_int(&ip->refs, refs, refs - 1))
				break;
		}
	}
}

/*
 * Get the vnode associated with the given inode, allocating the vnode if
 * necessary.  The vnode will be returned exclusively locked.
 *
 * The caller must lock the inode (shared or exclusive).
 *
 * Great care must be taken to avoid deadlocks and vnode acquisition/reclaim
 * races.
 */
struct vnode *
hammer2_igetv(hammer2_inode_t *ip, hammer2_cluster_t *cparent, int *errorp)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_pfs_t *pmp;
	struct vnode *vp;

	pmp = ip->pmp;
	KKASSERT(pmp != NULL);
	*errorp = 0;

	ripdata = &hammer2_cluster_rdata(cparent)->ipdata;

	for (;;) {
		/*
		 * Attempt to reuse an existing vnode assignment.  It is
		 * possible to race a reclaim so the vget() may fail.  The
		 * inode must be unlocked during the vget() to avoid a
		 * deadlock against a reclaim.
		 */
		int wasexclusive;

		vp = ip->vp;
		if (vp) {
			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, but leave the ip ref intact.
			 *
			 * vnode is held to prevent destruction during the
			 * vget().  The vget() can still fail if we lost
			 * a reclaim race on the vnode.
			 */
			hammer2_mtx_state_t ostate;

			vhold(vp);
			ostate = hammer2_inode_lock_temp_release(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				hammer2_inode_lock_temp_restore(ip, ostate);
				continue;
			}
			hammer2_inode_lock_temp_restore(ip, ostate);
			vdrop(vp);
			/* vp still locked and ref from vget */
			if (ip->vp != vp) {
				kprintf("hammer2: igetv race %p/%p\n",
					ip->vp, vp);
				vput(vp);
				continue;
			}
			*errorp = 0;
			break;
		}

		/*
		 * No vnode exists, allocate a new vnode.  Beware of
		 * allocation races.  This function will return an
		 * exclusively locked and referenced vnode.
		 */
		*errorp = getnewvnode(VT_HAMMER2, pmp->mp, &vp, 0, 0);
		if (*errorp) {
			kprintf("hammer2: igetv getnewvnode failed %d\n",
				*errorp);
			vp = NULL;
			break;
		}

		/*
		 * Lock the inode and check for an allocation race.
		 */
		wasexclusive = hammer2_inode_lock_upgrade(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_lock_downgrade(ip, wasexclusive);
			continue;
		}

		switch (ripdata->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			vp->v_type = VREG;
			vinitvmio(vp, ripdata->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ripdata->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			/*
			 * XXX for now we are using the generic file_read
			 * and file_write code so we need a buffer cache
			 * association.
			 */
			vp->v_type = VLNK;
			vinitvmio(vp, ripdata->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ripdata->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_CDEV:
			vp->v_type = VCHR;
			/* fall through */
		case HAMMER2_OBJTYPE_BDEV:
			vp->v_ops = &pmp->mp->mnt_vn_spec_ops;
			if (ripdata->meta.type != HAMMER2_OBJTYPE_CDEV)
				vp->v_type = VBLK;
			addaliasu(vp,
				  ripdata->meta.rmajor,
				  ripdata->meta.rminor);
			break;
		case HAMMER2_OBJTYPE_FIFO:
			vp->v_type = VFIFO;
			vp->v_ops = &pmp->mp->mnt_vn_fifo_ops;
			break;
		default:
			panic("hammer2: unhandled objtype %d",
			      ripdata->meta.type);
			break;
		}

		if (ip == pmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_inode_ref(ip);		/* vp association */
		hammer2_inode_lock_downgrade(ip, wasexclusive);
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	if (hammer2_debug & 0x0002) {
		kprintf("igetv vp %p refs 0x%08x aux 0x%08x\n",
			vp, vp->v_refcnt, vp->v_auxrefs);
	}
	return (vp);
}

/*
 * Returns the inode associated with the passed-in cluster, creating the
 * inode if necessary and synchronizing it to the passed-in cluster otherwise.
 *
 * The passed-in cluster must be locked and will remain locked on return.
 * The returned inode will be locked and the caller may dispose of both
 * via hammer2_inode_unlock() + hammer2_inode_drop().  However, if the caller
 * needs to resolve a hardlink it must ref/unlock/relock/drop the inode.
 *
 * The hammer2_inode structure regulates the interface between the high level
 * kernel VNOPS API and the filesystem backend (the chains).
 *
 * On return the inode is locked with the supplied cluster.
 */
hammer2_inode_t *
hammer2_inode_get(hammer2_pfs_t *pmp, hammer2_inode_t *dip,
		  hammer2_cluster_t *cluster)
{
	hammer2_inode_t *nip;
	const hammer2_inode_data_t *iptmp;
	const hammer2_inode_data_t *nipdata;

	KKASSERT(cluster == NULL ||
		 hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE);
	KKASSERT(pmp);

	/*
	 * Interlocked lookup/ref of the inode.  This code is only needed
	 * when looking up inodes with nlinks != 0 (TODO: optimize out
	 * otherwise and test for duplicates).
	 *
	 * Cluster can be NULL during the initial pfs allocation.
	 */
again:
	while (cluster) {
		iptmp = &hammer2_cluster_rdata(cluster)->ipdata;
		nip = hammer2_inode_lookup(pmp, iptmp->meta.inum);
		if (nip == NULL)
			break;

		hammer2_mtx_ex(&nip->lock);

		/*
		 * Handle SMP race (not applicable to the super-root spmp
		 * which can't index inodes due to duplicative inode numbers).
		 */
		if (pmp->spmp_hmp == NULL &&
		    (nip->flags & HAMMER2_INODE_ONRBTREE) == 0) {
			hammer2_mtx_unlock(&nip->lock);
			hammer2_inode_drop(nip);
			continue;
		}
		hammer2_inode_repoint(nip, NULL, cluster);

		return nip;
	}

	/*
	 * We couldn't find the inode number, create a new inode.
	 */
	nip = kmalloc(sizeof(*nip), pmp->minode, M_WAITOK | M_ZERO);
	spin_init(&nip->cluster_spin, "h2clspin");
	atomic_add_long(&pmp->inmem_inodes, 1);
	hammer2_pfs_memory_inc(pmp);
	hammer2_pfs_memory_wakeup(pmp);
	if (pmp->spmp_hmp)
		nip->flags = HAMMER2_INODE_SROOT;

	/*
	 * Initialize nip's cluster.  A cluster is provided for normal
	 * inodes but typically not for the super-root or PFS inodes.
	 */
	nip->cluster.refs = 1;
	nip->cluster.pmp = pmp;
	nip->cluster.flags |= HAMMER2_CLUSTER_INODE;
	if (cluster) {
		nipdata = &hammer2_cluster_rdata(cluster)->ipdata;
		nip->meta = nipdata->meta;
		hammer2_cluster_bref(cluster, &nip->bref);
		atomic_set_int(&nip->flags, HAMMER2_INODE_METAGOOD);
		hammer2_inode_repoint(nip, NULL, cluster);
	} else {
		nip->meta.inum = 1;		/* PFS inum is always 1 XXX */
		/* mtime will be updated when a cluster is available */
		atomic_set_int(&nip->flags, HAMMER2_INODE_METAGOOD);/*XXX*/
	}

	nip->pip = dip;				/* can be NULL */
	if (dip)
		hammer2_inode_ref(dip);	/* ref dip for nip->pip */

	nip->pmp = pmp;

	/*
	 * ref and lock on nip gives it state compatible to after a
	 * hammer2_inode_lock() call.
	 */
	nip->refs = 1;
	hammer2_mtx_init(&nip->lock, "h2inode");
	hammer2_mtx_ex(&nip->lock);
	/* combination of thread lock and chain lock == inode lock */

	/*
	 * Attempt to add the inode.  If it fails we raced another inode
	 * get.  Undo all the work and try again.
	 */
	if (pmp->spmp_hmp == NULL) {
		hammer2_spin_ex(&pmp->inum_spin);
		if (RB_INSERT(hammer2_inode_tree, &pmp->inum_tree, nip)) {
			hammer2_spin_unex(&pmp->inum_spin);
			hammer2_mtx_unlock(&nip->lock);
			hammer2_inode_drop(nip);
			goto again;
		}
		atomic_set_int(&nip->flags, HAMMER2_INODE_ONRBTREE);
		hammer2_spin_unex(&pmp->inum_spin);
	}

	return (nip);
}

/*
 * Create a new inode in the specified directory using the vattr to
 * figure out the type of inode.
 *
 * If no error occurs the new inode with its cluster locked is returned in
 * *nipp, otherwise an error is returned and *nipp is set to NULL.
 *
 * If vap and/or cred are NULL the related fields are not set and the
 * inode type defaults to a directory.  This is used when creating PFSs
 * under the super-root, so the inode number is set to 1 in this case.
 *
 * dip is not locked on entry.
 *
 * NOTE: When used to create a snapshot, the inode is temporarily associated
 *	 with the super-root spmp. XXX should pass new pmp for snapshot.
 */
hammer2_inode_t *
hammer2_inode_create(hammer2_trans_t *trans, hammer2_inode_t *dip,
		     struct vattr *vap, struct ucred *cred,
		     const uint8_t *name, size_t name_len,
		     hammer2_cluster_t **clusterp,
		     int flags, int *errorp)
{
	const hammer2_inode_data_t *dipdata;
	hammer2_inode_data_t *nipdata;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *cparent;
	hammer2_inode_t *nip;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	int error;
	uid_t xuid;
	uuid_t dip_uid;
	uuid_t dip_gid;
	uint32_t dip_mode;
	uint8_t dip_comp_algo;
	uint8_t dip_check_algo;

	lhc = hammer2_dirhash(name, name_len);
	*errorp = 0;

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 *
	 * NOTE: hidden inodes do not have iterators.
	 */
retry:
	hammer2_inode_lock(dip, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(dip, HAMMER2_RESOLVE_ALWAYS);
	dipdata = &hammer2_cluster_rdata(cparent)->ipdata;
	dip_uid = dipdata->meta.uid;
	dip_gid = dipdata->meta.gid;
	dip_mode = dipdata->meta.mode;
	dip_comp_algo = dipdata->meta.comp_algo;
	dip_check_algo = dipdata->meta.check_algo;

	error = 0;
	while (error == 0) {
		cluster = hammer2_cluster_lookup(cparent, &key_dummy,
						 lhc, lhc, 0);
		if (cluster == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_VISIBLE) == 0)
			error = ENOSPC;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
		cluster = NULL;
		++lhc;
	}

	if (error == 0) {
		error = hammer2_cluster_create(trans, cparent, &cluster,
					     lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     flags);
	}
#if INODE_DEBUG
	kprintf("CREATE INODE %*.*s chain=%p\n",
		(int)name_len, (int)name_len, name,
		(cluster ? cluster->focus : NULL));
#endif

	/*
	 * Cleanup and handle retries.
	 */
	if (error == EAGAIN) {
		hammer2_cluster_ref(cparent);
		hammer2_inode_unlock(dip, cparent);
		hammer2_cluster_wait(cparent);
		hammer2_cluster_drop(cparent);
		goto retry;
	}
	hammer2_inode_unlock(dip, cparent);
	cparent = NULL;

	if (error) {
		KKASSERT(cluster == NULL);
		*errorp = error;
		return (NULL);
	}

	/*
	 * Set up the new inode.
	 *
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 *
	 * NOTE: Only one new inode can currently be created per
	 *	 transaction.  If the need arises we can adjust
	 *	 hammer2_trans_init() to allow more.
	 *
	 * NOTE: nipdata will have chain's blockset data.
	 */
	KKASSERT(cluster->focus->flags & HAMMER2_CHAIN_MODIFIED);
	nipdata = &hammer2_cluster_wdata(cluster)->ipdata;
	nipdata->meta.inum = trans->inode_tid;
	hammer2_cluster_modsync(cluster);
	nip = hammer2_inode_get(dip->pmp, dip, cluster);
	nipdata = &hammer2_cluster_wdata(cluster)->ipdata;

	if (vap) {
		KKASSERT(trans->inodes_created == 0);
		nipdata->meta.type = hammer2_get_obj_type(vap->va_type);
		nipdata->meta.inum = trans->inode_tid;
		++trans->inodes_created;

		switch (nipdata->meta.type) {
		case HAMMER2_OBJTYPE_CDEV:
		case HAMMER2_OBJTYPE_BDEV:
			nipdata->meta.rmajor = vap->va_rmajor;
			nipdata->meta.rminor = vap->va_rminor;
			break;
		default:
			break;
		}
	} else {
		nipdata->meta.type = HAMMER2_OBJTYPE_DIRECTORY;
		nipdata->meta.inum = 1;
	}
	
	/* Inherit parent's inode compression mode. */
	nip->comp_heuristic = 0;
	nipdata->meta.comp_algo = dip_comp_algo;
	nipdata->meta.check_algo = dip_check_algo;
	nipdata->meta.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nipdata->meta.ctime);
	nipdata->meta.mtime = nipdata->meta.ctime;
	if (vap)
		nipdata->meta.mode = vap->va_mode;
	nipdata->meta.nlinks = 1;
	if (vap) {
		if (dip && dip->pmp) {
			xuid = hammer2_to_unix_xid(&dip_uid);
			xuid = vop_helper_create_uid(dip->pmp->mp,
						     dip_mode,
						     xuid,
						     cred,
						     &vap->va_mode);
		} else {
			/* super-root has no dip and/or pmp */
			xuid = 0;
		}
		if (vap->va_vaflags & VA_UID_UUID_VALID)
			nipdata->meta.uid = vap->va_uid_uuid;
		else if (vap->va_uid != (uid_t)VNOVAL)
			hammer2_guid_to_uuid(&nipdata->meta.uid, vap->va_uid);
		else
			hammer2_guid_to_uuid(&nipdata->meta.uid, xuid);

		if (vap->va_vaflags & VA_GID_UUID_VALID)
			nipdata->meta.gid = vap->va_gid_uuid;
		else if (vap->va_gid != (gid_t)VNOVAL)
			hammer2_guid_to_uuid(&nipdata->meta.gid, vap->va_gid);
		else if (dip)
			nipdata->meta.gid = dip_gid;
	}

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (nipdata->meta.type == HAMMER2_OBJTYPE_REGFILE ||
	    nipdata->meta.type == HAMMER2_OBJTYPE_SOFTLINK) {
		nipdata->meta.op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}

	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
	bcopy(name, nipdata->filename, name_len);
	nipdata->meta.name_key = lhc;
	nipdata->meta.name_len = name_len;
	nip->meta = nipdata->meta;
	hammer2_cluster_modsync(cluster);
	*clusterp = cluster;

	return (nip);
}

/*
 * The cluster has been removed from the original directory and replaced
 * with a hardlink pointer.  Move the cluster to the specified parent
 * directory, change the filename to "0xINODENUMBER", and adjust the key.
 * The cluster becomes our invisible hardlink target.
 *
 * The original cluster must be deleted on entry.
 */
static
void
hammer2_hardlink_shiftup(hammer2_trans_t *trans, hammer2_cluster_t *cluster,
			hammer2_inode_t *ip, hammer2_inode_t *dip,
			hammer2_cluster_t *dcluster,
			int nlinks, int *errorp)
{
	hammer2_inode_data_t *nipdata;
	hammer2_cluster_t *xcluster;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	hammer2_blockref_t bref;

	lhc = ip->meta.inum;
#if 0
	iptmp = &hammer2_cluster_rdata(cluster)->ipdata;
	lhc = iptmp->meta.inum;
#endif
	KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  lhc represents the inode number so there is
	 * no collision iteration.
	 *
	 * There should be no key collisions with invisible inode keys.
	 *
	 * WARNING! Must use inode_lock_ex() on dip to handle a stale
	 *	    dip->cluster cache.
	 */
	*errorp = 0;
	xcluster = hammer2_cluster_lookup(dcluster, &key_dummy,
				      lhc, lhc, 0);
	if (xcluster) {
		kprintf("X3 chain %p dip %p dchain %p dip->chain %p\n",
			xcluster->focus, dip, dcluster->focus,
			dip->cluster.focus);
		hammer2_cluster_unlock(xcluster);
		hammer2_cluster_drop(xcluster);
		xcluster = NULL;
		*errorp = ENOSPC;
#if 0
		Debugger("X3");
#endif
	}

	/*
	 * Handle the error case
	 */
	if (*errorp) {
		panic("error2");
		KKASSERT(xcluster == NULL);
		return;
	}

	/*
	 * Use xcluster as a placeholder for (lhc).  Duplicate cluster to the
	 * same target bref as xcluster and then delete xcluster.  The
	 * duplication occurs after xcluster in flush order even though
	 * xcluster is deleted after the duplication. XXX
	 *
	 * WARNING! Duplications (to a different parent) can cause indirect
	 *	    blocks to be inserted, refactor xcluster.
	 *
	 * WARNING! Only key and keybits is extracted from a passed-in bref.
	 */
	hammer2_cluster_bref(cluster, &bref);
	bref.key = lhc;			/* invisible dir entry key */
	bref.keybits = 0;
	hammer2_cluster_rename(trans, &bref, dcluster, cluster, 0);

	/*
	 * cluster is now 'live' again.. adjust the filename.
	 *
	 * Directory entries are inodes but this is a hidden hardlink
	 * target.  The name isn't used but to ease debugging give it
	 * a name after its inode number.
	 */
	hammer2_inode_modify(trans, ip);
	hammer2_cluster_modify(trans, cluster, 0);

	nipdata = &hammer2_cluster_wdata(cluster)->ipdata;
	ksnprintf(nipdata->filename, sizeof(nipdata->filename),
		  "0x%016jx", (intmax_t)nipdata->meta.inum);
	ip->meta.name_len = strlen(nipdata->filename);
	ip->meta.name_key = lhc;
	ip->meta.nlinks += nlinks;

	/*
	 * Resync nipdata->meta from the local copy.
	 */
	nipdata->meta = ip->meta;
	hammer2_cluster_modsync(cluster);
}

/*
 * Connect the target inode represented by (cluster) to the media topology
 * at (dip, name, len).  The caller can pass a rough *chainp, this function
 * will issue lookup()s to position the parent chain properly for the
 * chain insertion.
 *
 * If hlink is TRUE this function creates an OBJTYPE_HARDLINK directory
 * entry instead of connecting (cluster).
 *
 * If hlink is FALSE this function expects (cluster) to be unparented.
 */
int
hammer2_inode_connect(hammer2_trans_t *trans,
		      hammer2_inode_t *ip, hammer2_cluster_t **clusterp,
		      int hlink,
		      hammer2_inode_t *dip, hammer2_cluster_t *dcluster,
		      const uint8_t *name, size_t name_len,
		      hammer2_key_t lhc)
{
	hammer2_inode_data_t *wipdata;
	hammer2_cluster_t *ocluster;
	hammer2_cluster_t *ncluster;
	hammer2_key_t key_dummy;
	int error;

	/*
	 * Since ocluster is either disconnected from the topology or
	 * represents a hardlink terminus which is always a parent of or
	 * equal to dip, we should be able to safely lock dip->chain for
	 * our setup.
	 *
	 * WARNING! Must use inode_lock_ex() on dip to handle a stale
	 *	    dip->cluster.
	 *
	 * If name is non-NULL we calculate lhc, else we use the passed-in
	 * lhc.
	 */
	ocluster = *clusterp;

	if (name) {
		lhc = hammer2_dirhash(name, name_len);

		/*
		 * Locate the inode or indirect block to create the new
		 * entry in.  At the same time check for key collisions
		 * and iterate until we don't get one.
		 */
		error = 0;
		while (error == 0) {
			ncluster = hammer2_cluster_lookup(dcluster, &key_dummy,
						      lhc, lhc, 0);
			if (ncluster == NULL)
				break;
			if ((lhc & HAMMER2_DIRHASH_LOMASK) ==
			    HAMMER2_DIRHASH_LOMASK) {
				error = ENOSPC;
			}
			hammer2_cluster_unlock(ncluster);
			hammer2_cluster_drop(ncluster);
			ncluster = NULL;
			++lhc;
		}
	} else {
		/*
		 * Reconnect to specific key (used when moving
		 * unlinked-but-open files into the hidden directory).
		 */
		ncluster = hammer2_cluster_lookup(dcluster, &key_dummy,
						  lhc, lhc, 0);
		KKASSERT(ncluster == NULL);
		error = 0;
	}

	if (error == 0) {
		if (hlink) {
			/*
			 * Hardlink pointer needed, create totally fresh
			 * directory entry.
			 *
			 * We must refactor ocluster because it might have
			 * been shifted into an indirect cluster by the
			 * create.
			 */
			KKASSERT(ncluster == NULL);
			error = hammer2_cluster_create(trans,
						       dcluster, &ncluster,
						       lhc, 0,
						       HAMMER2_BREF_TYPE_INODE,
						       HAMMER2_INODE_BYTES,
						       0);
		} else {
			/*
			 * Reconnect the original cluster under the new name.
			 * Original cluster must have already been deleted by
			 * teh caller.
			 *
			 * WARNING! Can cause held-over clusters to require a
			 *	    refactor.  Fortunately we have none (our
			 *	    locked clusters are passed into and
			 *	    modified by the call).
			 */
			ncluster = ocluster;
			ocluster = NULL;
			error = hammer2_cluster_create(trans,
						       dcluster, &ncluster,
						       lhc, 0,
						       HAMMER2_BREF_TYPE_INODE,
						       HAMMER2_INODE_BYTES,
						       0);
		}
	}

	/*
	 * Unlock stuff.
	 */
	KKASSERT(error != EAGAIN);

	/*
	 * ncluster should be NULL on error, leave ocluster
	 * (ocluster == *clusterp) alone.
	 */
	if (error) {
		KKASSERT(ncluster == NULL);
		return (error);
	}

	/*
	 * Directory entries are inodes so if the name has changed we have
	 * to update the inode.
	 *
	 * When creating an OBJTYPE_HARDLINK entry remember to unlock the
	 * cluster, the caller will access the hardlink via the actual hardlink
	 * target file and not the hardlink pointer entry, so we must still
	 * return ocluster.
	 */
	if (hlink && hammer2_hardlink_enable >= 0) {
		/*
		 * Create the HARDLINK pointer.  oip represents the hardlink
		 * target in this situation.
		 *
		 * We will return ocluster (the hardlink target).
		 */
		hammer2_cluster_modify(trans, ncluster, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		wipdata = &hammer2_cluster_wdata(ncluster)->ipdata;
		bcopy(name, wipdata->filename, name_len);
		wipdata->meta.name_key = lhc;
		wipdata->meta.name_len = name_len;
		wipdata->meta.target_type =
			    hammer2_cluster_rdata(ocluster)->ipdata.meta.type;
		wipdata->meta.type = HAMMER2_OBJTYPE_HARDLINK;
		wipdata->meta.inum =
			    hammer2_cluster_rdata(ocluster)->ipdata.meta.inum;
		wipdata->meta.version = HAMMER2_INODE_VERSION_ONE;
		wipdata->meta.nlinks = 1;
		wipdata->meta.op_flags = HAMMER2_OPFLAG_DIRECTDATA;
		hammer2_cluster_modsync(ncluster);
		hammer2_cluster_unlock(ncluster);
		hammer2_cluster_drop(ncluster);
		ncluster = ocluster;
		ocluster = NULL;
	} else {
		/*
		 * ncluster is a duplicate of ocluster at the new location.
		 * We must fixup the name stored in the inode data.
		 * The bref key has already been adjusted by inode_connect().
		 */
		hammer2_inode_modify(trans, ip);
		hammer2_cluster_modify(trans, ncluster, 0);
		wipdata = &hammer2_cluster_wdata(ncluster)->ipdata;

		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, wipdata->filename, name_len);
		ip->meta.name_key = lhc;
		ip->meta.name_len = name_len;
		ip->meta.nlinks = 1;

		/*
		 * Resync wipdata->meta from the local copy.
		 */
		wipdata->meta = ip->meta;
		hammer2_cluster_modsync(ncluster);
	}

	/*
	 * We are replacing ocluster with ncluster, unlock ocluster.  In the
	 * case where ocluster is left unchanged the code above sets
	 * ncluster to ocluster and ocluster to NULL, resulting in a NOP here.
	 */
	if (ocluster) {
		hammer2_cluster_unlock(ocluster);
		hammer2_cluster_drop(ocluster);
	}
	*clusterp = ncluster;

	return (0);
}

/*
 * Repoint ip->cluster's chains to cluster's chains and fixup the default
 * focus.  Only valid elements are repointed.  Invalid elements have to be
 * adjusted by the appropriate slave sync threads.
 *
 * Caller must hold the inode and cluster exclusive locked, if not NULL,
 * must also be locked.
 *
 * Cluster may be NULL to clean out any chains in ip->cluster.
 */
void
hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
		      hammer2_cluster_t *cluster)
{
	hammer2_chain_t *dropch[HAMMER2_MAXCLUSTER];
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_inode_t *opip;
	int i;

	bzero(dropch, sizeof(dropch));

	/*
	 * Replace chains in ip->cluster with chains from cluster and
	 * adjust the focus if necessary.
	 *
	 * NOTE: nchain and/or ochain can be NULL due to gaps
	 *	 in the cluster arrays.
	 */
	hammer2_spin_ex(&ip->cluster_spin);
	for (i = 0; cluster && i < cluster->nchains; ++i) {
		/*
		 * Do not replace invalid elements as this might race
		 * syncthr replacements.
		 */
		if (cluster->array[i].flags & HAMMER2_CITEM_INVALID)
			continue;

		/*
		 * Do not replace elements which are the same.  Also handle
		 * element count discrepancies.
		 */
		nchain = cluster->array[i].chain;
		if (i < ip->cluster.nchains) {
			ochain = ip->cluster.array[i].chain;
			if (ochain == nchain)
				continue;
		} else {
			ochain = NULL;
		}

		/*
		 * Make adjustments
		 */
		ip->cluster.array[i].chain = nchain;
		ip->cluster.array[i].flags &= ~HAMMER2_CITEM_INVALID;
		ip->cluster.array[i].flags |= cluster->array[i].flags &
					      HAMMER2_CITEM_INVALID;
		if (nchain)
			hammer2_chain_ref(nchain);
		dropch[i] = ochain;
	}

	/*
	 * Release any left-over chains in ip->cluster.
	 */
	while (i < ip->cluster.nchains) {
		nchain = ip->cluster.array[i].chain;
		if (nchain) {
			ip->cluster.array[i].chain = NULL;
			ip->cluster.array[i].flags |= HAMMER2_CITEM_INVALID;
		}
		dropch[i] = nchain;
		++i;
	}

	/*
	 * Fixup fields.  Note that the inode-embedded cluster is never
	 * directly locked.
	 */
	if (cluster) {
		ip->cluster.nchains = cluster->nchains;
		ip->cluster.focus = cluster->focus;
		ip->cluster.flags = cluster->flags & ~HAMMER2_CLUSTER_LOCKED;
	} else {
		ip->cluster.nchains = 0;
		ip->cluster.focus = NULL;
		ip->cluster.flags &= ~HAMMER2_CLUSTER_ZFLAGS;
	}

	/*
	 * Repoint ip->pip if requested (non-NULL pip).
	 */
	if (pip && ip->pip != pip) {
		opip = ip->pip;
		hammer2_inode_ref(pip);
		ip->pip = pip;
	} else {
		opip = NULL;
	}
	hammer2_spin_unex(&ip->cluster_spin);

	/*
	 * Cleanup outside of spinlock
	 */
	while (--i >= 0) {
		if (dropch[i])
			hammer2_chain_drop(dropch[i]);
	}
	if (opip)
		hammer2_inode_drop(opip);
}

/*
 * Repoint a single element from the cluster to the ip.  Used by the
 * synchronization threads to piecemeal update inodes.  Does not change
 * focus and requires inode to be re-locked to clean-up flags (XXX).
 */
void
hammer2_inode_repoint_one(hammer2_inode_t *ip, hammer2_cluster_t *cluster,
			  int idx)
{
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	int i;

	hammer2_spin_ex(&ip->cluster_spin);
	KKASSERT(idx < cluster->nchains);
	if (idx < ip->cluster.nchains) {
		ochain = ip->cluster.array[idx].chain;
		nchain = cluster->array[idx].chain;
	} else {
		ochain = NULL;
		nchain = cluster->array[idx].chain;
		ip->cluster.nchains = idx + 1;
		for (i = ip->cluster.nchains; i <= idx; ++i) {
			bzero(&ip->cluster.array[i],
			      sizeof(ip->cluster.array[i]));
			ip->cluster.array[i].flags |= HAMMER2_CITEM_INVALID;
		}
	}
	if (ochain != nchain) {
		/*
		 * Make adjustments.
		 */
		ip->cluster.array[idx].chain = nchain;
		ip->cluster.array[idx].flags &= ~HAMMER2_CITEM_INVALID;
		ip->cluster.array[idx].flags |= cluster->array[idx].flags &
						HAMMER2_CITEM_INVALID;
	}
	hammer2_spin_unex(&ip->cluster_spin);
	if (ochain != nchain) {
		if (nchain)
			hammer2_chain_ref(nchain);
		if (ochain)
			hammer2_chain_drop(ochain);
	}
}

/*
 * Unlink the file (dip, name, name_len), from the specified directory inode.
 * If the caller also has the hammer2_inode the caller must pass it locked as
 * (ip), but may pass NULL if it does not have the inode in hand.
 *
 * The directory inode does not need to be locked.
 *
 * isdir determines whether a directory/non-directory check should be made.
 * No check is made if isdir is set to -1.
 *
 * isopen specifies whether special unlink-with-open-descriptor handling
 * must be performed.  If set to -1 the caller is deleting a PFS and we
 * check whether the chain is mounted or not (chain->pmp != NULL).  1 is
 * implied if it is mounted.
 *
 * If isopen is 1 and nlinks drops to 0 this function must move the chain
 * to a special hidden directory until last-close occurs on the file.
 *
 * NOTE!  The underlying file can still be active with open descriptors
 *	  or if the inode is being manually held (e.g. for rename).
 *
 * NOTE!  When unlinking an open file the inode will be temporarily moved to
 *	  a hidden directory, otherwise the inode will be deleted.
 */
int
hammer2_unlink_file(hammer2_trans_t *trans,
		    hammer2_inode_t *dip, hammer2_inode_t *ip,
		    const uint8_t *name, size_t name_len,
		    int isdir, int *hlinkp, struct nchandle *nch,
		    int nlinks)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *hcluster;
	hammer2_cluster_t *hparent;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *dparent;
	hammer2_cluster_t *dcluster;
	hammer2_key_t key_dummy;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int last_link;
	int error;
	int hlink;
	int myip;
	uint8_t type;

	error = 0;
	hlink = 0;
	myip = 0;
	hcluster = NULL;
	hparent = NULL;
	lhc = hammer2_dirhash(name, name_len);

again:
	/*
	 * Locate the filename in the directory and instantiate the ip
	 * if necessary.  If the ip is already known we must still locate
	 * the filename to adjust cparent for possible deletion.
	 */
	hammer2_inode_lock(dip, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(dip, HAMMER2_RESOLVE_ALWAYS);
	cluster = hammer2_cluster_lookup(cparent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK, 0);
	while (cluster) {
		if (hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE) {
			ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
			if (ripdata->meta.name_len == name_len &&
			    bcmp(ripdata->filename, name, name_len) == 0) {
				break;
			}
		}
		cluster = hammer2_cluster_next(cparent, cluster, &key_next,
					       key_next,
					       lhc + HAMMER2_DIRHASH_LOMASK,
					       0);
	}
	hammer2_inode_unlock(dip, NULL);	/* retain cparent */

	/*
	 * Not found or wrong type (isdir < 0 disables the type check).
	 * If a hardlink pointer, type checks use the hardlink target.
	 */
	if (cluster == NULL) {
		error = ENOENT;
		goto done;
	}

	ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
	type = ripdata->meta.type;
	if (type == HAMMER2_OBJTYPE_HARDLINK) {
		hlink = 1;
		type = ripdata->meta.target_type;
	}

	if (type == HAMMER2_OBJTYPE_DIRECTORY && isdir == 0) {
		error = ENOTDIR;
		goto done;
	}
	if (type != HAMMER2_OBJTYPE_DIRECTORY && isdir >= 1) {
		error = EISDIR;
		goto done;
	}

	/*
	 * Hardlink must be resolved.  We can't hold the parent locked
	 * while we do this or we could deadlock.  The physical file will
	 * be located at or above the current directory.
	 *
	 * We loop to reacquire the hardlink origination.
	 *
	 * NOTE: hammer2_hardlink_find() will locate the hardlink target,
	 *	 returning a modified hparent and hcluster.
	 */
	if (ripdata->meta.type == HAMMER2_OBJTYPE_HARDLINK) {
		if (hcluster == NULL) {
			hcluster = cluster;
			cluster = NULL;	/* safety */
			hammer2_cluster_unlock(cparent);
			hammer2_cluster_drop(cparent);
			cparent = NULL; /* safety */
			ripdata = NULL;	/* safety (associated w/cparent) */
			error = hammer2_hardlink_find(dip, &hparent, &hcluster);

			/*
			 * If we couldn't find the hardlink target then some
			 * parent directory containing the hardlink pointer
			 * probably got renamed to above the original target,
			 * a case not yet handled by H2.
			 */
			if (error) {
				kprintf("H2 unlink_file: hardlink target for "
					"\"%s\" not found\n",
					name);
				kprintf("(likely due to known directory "
					"rename bug)\n");
				goto done;
			}
			goto again;
		}
	}

	/*
	 * If this is a directory the directory must be empty.  However, if
	 * isdir < 0 we are doing a rename and the directory does not have
	 * to be empty, and if isdir > 1 we are deleting a PFS/snapshot
	 * and the directory does not have to be empty.
	 *
	 * NOTE: We check the full key range here which covers both visible
	 *	 and invisible entries.  Theoretically there should be no
	 *	 invisible (hardlink target) entries if there are no visible
	 *	 entries.
	 */
	if (type == HAMMER2_OBJTYPE_DIRECTORY && isdir == 1) {
		dparent = hammer2_cluster_lookup_init(cluster, 0);
		dcluster = hammer2_cluster_lookup(dparent, &key_dummy,
					          0, (hammer2_key_t)-1,
					          HAMMER2_LOOKUP_NODATA);
		if (dcluster) {
			hammer2_cluster_unlock(dcluster);
			hammer2_cluster_drop(dcluster);
			hammer2_cluster_lookup_done(dparent);
			error = ENOTEMPTY;
			goto done;
		}
		hammer2_cluster_lookup_done(dparent);
		dparent = NULL;
		/* dcluster NULL */
	}

	/*
	 * If this was a hardlink then (cparent, cluster) is the hardlink
	 * pointer, which we can simply destroy outright.  Discard the
	 * clusters and replace with the hardlink target.
	 */
	if (hcluster) {
		hammer2_cluster_delete(trans, cparent, cluster,
				       HAMMER2_DELETE_PERMANENT);
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
		cparent = hparent;
		cluster = hcluster;
		hparent = NULL;
		hcluster = NULL;
	}

	/*
	 * This leaves us with the hardlink target or non-hardlinked file
	 * or directory in (cparent, cluster).
	 *
	 * Delete the target when nlinks reaches 0 with special handling
	 * to avoid I/O (to avoid actually updating the inode) for the 1->0
	 * transition, if possible.  This optimization makes rm -rf very
	 * fast.
	 *
	 * NOTE! In DragonFly the vnops function calls cache_unlink() after
	 *	 calling us here to clean out the namecache association,
	 *	 (which does not represent a ref for the open-test), and to
	 *	 force finalization of the vnode if/when the last ref gets
	 *	 dropped.
	 *
	 * NOTE! Files are unlinked by rename and then relinked.  nch will be
	 *	 passed as NULL in this situation.  hammer2_inode_connect()
	 *	 will bump nlinks.
	 */
	KKASSERT(cluster != NULL);

	/*
	 * Instantiate ip if necessary for ip->meta data consolidation.
	 */
	if (ip == NULL) {
		ip = hammer2_inode_get(dip->pmp, dip, cluster);
		myip = 1;
	}


	/*
	 * Note: nlinks is negative when decrementing, positive when
	 *	 incrementing.
	 */
	last_link = (ip->meta.nlinks + nlinks == 0);

	if (last_link) {
		/*
		 * Target nlinks has reached 0, file now unlinked (but may
		 * still be open).
		 *
		 * nlinks will be -1 for a normal remove().  If this is the
		 * last link we must flag the inode so we can optimally
		 * throw away buffer data and destroy the file on reclaim.
		 */
		if (nlinks == -1)
			atomic_set_int(&ip->flags, HAMMER2_INODE_ISUNLINKED);

		if (nch && cache_isopen(nch)) {
			/*
			 * If an unlinked file is still open we must update
			 * the inodes link count.
			 */
			/*hammer2_cluster_modify(trans, cluster, 0);*/
			hammer2_inode_modify(trans, ip);
			ip->meta.nlinks += nlinks;
			if ((int64_t)ip->meta.nlinks < 0)	/* safety */
				ip->meta.nlinks = 0;
			hammer2_inode_move_to_hidden(trans, &cparent, &cluster,
						     ip->meta.inum);
			/* hammer2_cluster_modsync(cluster); */
		} else {
			/*
			 * This won't get everything if a vnode is still
			 * present, but the cache_unlink() call the caller
			 * makes will.
			 */
			hammer2_cluster_delete(trans, cparent, cluster,
					       HAMMER2_DELETE_PERMANENT);
		}
	} else if (hlink == 0) {
		/*
		 * In this situation a normal non-hardlinked file (which can
		 * only have nlinks == 1) still has a non-zero nlinks, the
		 * caller must be doing a RENAME operation and so is passing
		 * a nlinks adjustment of 0, and only wishes to remove file
		 * in order to be able to reconnect it under a different name.
		 *
		 * In this situation we do a temporary deletion of the
		 * chain in order to allow the file to be reconnected in
		 * a different location.
		 */
		KKASSERT(nlinks == 0);
		hammer2_cluster_delete(trans, cparent, cluster, 0);
	} else {
		/*
		 * Links remain, must update the inode link count.
		 */
		/*hammer2_cluster_modify(trans, cluster, 0);*/
		hammer2_inode_modify(trans, ip);
		ip->meta.nlinks += nlinks;
		if ((int64_t)ip->meta.nlinks < 0)
			ip->meta.nlinks = 0;
		/* hammer2_cluster_modsync(cluster); */
	}

	if (myip) {
		hammer2_inode_unlock(ip, NULL);
	}

	error = 0;
done:
	if (cparent) {
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);
	}
	if (cluster) {
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
	}
	if (hparent) {
		hammer2_cluster_unlock(hparent);
		hammer2_cluster_drop(hparent);
	}
	if (hcluster) {
		hammer2_cluster_unlock(hcluster);
		hammer2_cluster_drop(hcluster);
	}
	if (hlinkp)
		*hlinkp = hlink;

	return error;
}

/*
 * This is called from the mount code to initialize pmp->ihidden
 */
void
hammer2_inode_install_hidden(hammer2_pfs_t *pmp)
{
	hammer2_trans_t trans;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *scan;
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_data_t *wipdata;
	hammer2_key_t key_dummy;
	hammer2_key_t key_next;
	int error;
	int count;
	int dip_check_algo;
	int dip_comp_algo;

	if (pmp->ihidden)
		return;

	/*
	 * Find the hidden directory
	 */
	bzero(&key_dummy, sizeof(key_dummy));
	hammer2_trans_init(&trans, pmp, 0);

	/*
	 * Setup for lookup, retrieve iroot's check and compression
	 * algorithm request which was likely generated by newfs_hammer2.
	 *
	 * The check/comp fields will probably never be used since inodes
	 * are renamed into the hidden directory and not created relative to
	 * the hidden directory, chain creation inherits from bref.methods,
	 * and data chains inherit from their respective file inode *_algo
	 * fields.
	 */
	hammer2_inode_lock(pmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	cparent = hammer2_inode_cluster(pmp->iroot, HAMMER2_RESOLVE_ALWAYS);
	ripdata = &hammer2_cluster_rdata(cparent)->ipdata;
	dip_check_algo = ripdata->meta.check_algo;
	dip_comp_algo = ripdata->meta.comp_algo;
	ripdata = NULL;

	cluster = hammer2_cluster_lookup(cparent, &key_dummy,
					 HAMMER2_INODE_HIDDENDIR,
					 HAMMER2_INODE_HIDDENDIR,
					 0);
	if (cluster) {
		pmp->ihidden = hammer2_inode_get(pmp, pmp->iroot, cluster);
		hammer2_inode_ref(pmp->ihidden);

		/*
		 * Remove any unlinked files which were left open as-of
		 * any system crash.
		 *
		 * Don't pass NODATA, we need the inode data so the delete
		 * can do proper statistics updates.
		 */
		count = 0;
		scan = hammer2_cluster_lookup(cluster, &key_next,
					      0, HAMMER2_TID_MAX, 0);
		while (scan) {
			if (hammer2_cluster_type(scan) ==
			    HAMMER2_BREF_TYPE_INODE) {
				hammer2_cluster_delete(&trans, cluster, scan,
						   HAMMER2_DELETE_PERMANENT);
				++count;
			}
			scan = hammer2_cluster_next(cluster, scan, &key_next,
						    0, HAMMER2_TID_MAX, 0);
		}

		hammer2_inode_unlock(pmp->ihidden, cluster);
		hammer2_inode_unlock(pmp->iroot, cparent);
		hammer2_trans_done(&trans);
		kprintf("hammer2: PFS loaded hidden dir, "
			"removed %d dead entries\n", count);
		return;
	}

	/*
	 * Create the hidden directory
	 */
	error = hammer2_cluster_create(&trans, cparent, &cluster,
				       HAMMER2_INODE_HIDDENDIR, 0,
				       HAMMER2_BREF_TYPE_INODE,
				       HAMMER2_INODE_BYTES,
				       0);
	hammer2_inode_unlock(pmp->iroot, cparent);

	hammer2_cluster_modify(&trans, cluster, 0);
	wipdata = &hammer2_cluster_wdata(cluster)->ipdata;
	wipdata->meta.type = HAMMER2_OBJTYPE_DIRECTORY;
	wipdata->meta.inum = HAMMER2_INODE_HIDDENDIR;
	wipdata->meta.nlinks = 1;
	wipdata->meta.comp_algo = dip_comp_algo;
	wipdata->meta.check_algo = dip_check_algo;
	hammer2_cluster_modsync(cluster);
	kprintf("hammer2: PFS root missing hidden directory, creating\n");

	pmp->ihidden = hammer2_inode_get(pmp, pmp->iroot, cluster);
	hammer2_inode_ref(pmp->ihidden);
	hammer2_inode_unlock(pmp->ihidden, cluster);
	hammer2_trans_done(&trans);
}

/*
 * If an open file is unlinked H2 needs to retain the file in the topology
 * to ensure that its backing store is not recovered by the bulk free scan.
 * This also allows us to avoid having to special-case the CHAIN_DELETED flag.
 *
 * To do this the file is moved to a hidden directory in the PFS root and
 * renamed.  The hidden directory must be created if it does not exist.
 */
static
void
hammer2_inode_move_to_hidden(hammer2_trans_t *trans,
			     hammer2_cluster_t **cparentp,
			     hammer2_cluster_t **clusterp,
			     hammer2_tid_t inum)
{
	hammer2_cluster_t *dcluster;
	hammer2_pfs_t *pmp;
	int error;

	pmp = (*clusterp)->pmp;
	KKASSERT(pmp != NULL);
	KKASSERT(pmp->ihidden != NULL);

	hammer2_cluster_delete(trans, *cparentp, *clusterp, 0);
	hammer2_inode_lock(pmp->ihidden, HAMMER2_RESOLVE_ALWAYS);
	dcluster = hammer2_inode_cluster(pmp->ihidden, HAMMER2_RESOLVE_ALWAYS);
	error = hammer2_inode_connect(trans,
				      NULL/*XXX*/, clusterp, 0,
				      pmp->ihidden, dcluster,
				      NULL, 0, inum);
	hammer2_inode_unlock(pmp->ihidden, dcluster);
	KKASSERT(error == 0);
}

/*
 * Given an exclusively locked inode and cluster we consolidate the cluster
 * for hardlink creation, adding (nlinks) to the file's link count and
 * potentially relocating the inode to (cdip) which is a parent directory
 * common to both the current location of the inode and the intended new
 * hardlink.
 *
 * Replaces (*clusterp) if consolidation occurred, unlocking the old cluster
 * and returning a new locked cluster.
 *
 * NOTE!  This function will also replace ip->cluster.
 */
int
hammer2_hardlink_consolidate(hammer2_trans_t *trans,
			     hammer2_inode_t *ip,
			     hammer2_cluster_t **clusterp,
			     hammer2_inode_t *cdip,
			     hammer2_cluster_t *cdcluster,
			     int nlinks)
{
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *cparent;
	int error;

	cluster = *clusterp;
	if (nlinks == 0 &&			/* no hardlink needed */
	    (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		return (0);
	}

	if (hammer2_hardlink_enable == 0) {	/* disallow hardlinks */
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
		*clusterp = NULL;
		return (ENOTSUP);
	}

	cparent = NULL;

	/*
	 * If no change in the hardlink's target directory is required and
	 * this is already a hardlink target, all we need to do is adjust
	 * the link count.
	 */
	if (cdip == ip->pip &&
	    (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		if (nlinks) {
			hammer2_inode_modify(trans, ip);
			ip->meta.nlinks += nlinks;
#if 0
			hammer2_cluster_modify(trans, cluster, 0);
			wipdata = &hammer2_cluster_wdata(cluster)->ipdata;
			wipdata->meta.nlinks += nlinks;
			hammer2_cluster_modsync(cluster);
			ripdata = wipdata;
#endif
		}
		error = 0;
		goto done;
	}

	/*
	 * Cluster is the real inode.  The originating directory is locked
	 * by the caller so we can manipulate it without worrying about races
	 * against other lookups.
	 *
	 * If cluster is visible we need to delete it from the current
	 * location and create a hardlink pointer in its place.  If it is
	 * not visible we need only delete it.  Then later cluster will be
	 * renamed to a parent directory and converted (if necessary) to
	 * a hidden inode (via shiftup).
	 *
	 * NOTE! We must hold cparent locked through the delete/create/rename
	 *	 operation to ensure that other threads block resolving to
	 *	 the same hardlink, otherwise the other threads may not see
	 *	 the hardlink.
	 */
	KKASSERT((cluster->focus->flags & HAMMER2_CHAIN_DELETED) == 0);
	cparent = hammer2_cluster_parent(cluster);

	hammer2_cluster_delete(trans, cparent, cluster, 0);

	KKASSERT(ip->meta.type != HAMMER2_OBJTYPE_HARDLINK);
	if (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) {
		const hammer2_inode_data_t *ripdata;
		hammer2_inode_data_t *wipdata;
		hammer2_cluster_t *ncluster;
		hammer2_key_t lhc;

		ncluster = NULL;
		lhc = cluster->focus->bref.key;
		error = hammer2_cluster_create(trans, cparent, &ncluster,
					     lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     0);
		hammer2_cluster_modify(trans, ncluster, 0);
		wipdata = &hammer2_cluster_wdata(ncluster)->ipdata;

		/* wipdata->meta.comp_algo = ip->meta.comp_algo; */
		wipdata->meta.comp_algo = 0;
		wipdata->meta.check_algo = 0;
		wipdata->meta.version = HAMMER2_INODE_VERSION_ONE;
		wipdata->meta.inum = ip->meta.inum;
		wipdata->meta.target_type = ip->meta.type;
		wipdata->meta.type = HAMMER2_OBJTYPE_HARDLINK;
		wipdata->meta.uflags = 0;
		wipdata->meta.rmajor = 0;
		wipdata->meta.rminor = 0;
		wipdata->meta.ctime = 0;
		wipdata->meta.mtime = 0;
		wipdata->meta.atime = 0;
		wipdata->meta.btime = 0;
		bzero(&wipdata->meta.uid, sizeof(wipdata->meta.uid));
		bzero(&wipdata->meta.gid, sizeof(wipdata->meta.gid));
		wipdata->meta.op_flags = HAMMER2_OPFLAG_DIRECTDATA;
		wipdata->meta.cap_flags = 0;
		wipdata->meta.mode = 0;
		wipdata->meta.size = 0;
		wipdata->meta.nlinks = 1;
		wipdata->meta.iparent = 0;	/* XXX */
		wipdata->meta.pfs_type = 0;
		wipdata->meta.pfs_inum = 0;
		bzero(&wipdata->meta.pfs_clid, sizeof(wipdata->meta.pfs_clid));
		bzero(&wipdata->meta.pfs_fsid, sizeof(wipdata->meta.pfs_fsid));
		wipdata->meta.data_quota = 0;
		/* wipdata->data_count = 0; */
		wipdata->meta.inode_quota = 0;
		/* wipdata->inode_count = 0; */
		wipdata->meta.attr_tid = 0;
		wipdata->meta.dirent_tid = 0;
		bzero(&wipdata->u, sizeof(wipdata->u));
		ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
		KKASSERT(ip->meta.name_len <= sizeof(wipdata->filename));
		bcopy(ripdata->filename, wipdata->filename,
		      ip->meta.name_len);
		wipdata->meta.name_key = ncluster->focus->bref.key;
		wipdata->meta.name_len = ip->meta.name_len;
		/* XXX transaction ids */
		hammer2_cluster_modsync(ncluster);
		hammer2_cluster_unlock(ncluster);
		hammer2_cluster_drop(ncluster);
	}

	/*
	 * cluster represents the hardlink target and is now flagged deleted.
	 * duplicate it to the parent directory and adjust nlinks.
	 *
	 * WARNING! The shiftup() call can cause ncluster to be moved into
	 *	    an indirect block, and our ncluster will wind up pointing
	 *	    to the older/original version.
	 */
	KKASSERT(cluster->focus->flags & HAMMER2_CHAIN_DELETED);
	hammer2_hardlink_shiftup(trans, cluster, ip, cdip, cdcluster,
				 nlinks, &error);

	if (error == 0)
		hammer2_inode_repoint(ip, cdip, cluster);

done:
	/*
	 * Cleanup, cluster/ncluster already dealt with.
	 *
	 * Return the shifted cluster in *clusterp.
	 */
	if (cparent) {
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);
	}
	*clusterp = cluster;

	return (error);
}

/*
 * If (*ochainp) is non-NULL it points to the forward OBJTYPE_HARDLINK
 * inode while (*chainp) points to the resolved (hidden hardlink
 * target) inode.  In this situation when nlinks is 1 we wish to
 * deconsolidate the hardlink, moving it back to the directory that now
 * represents the only remaining link.
 */
int
hammer2_hardlink_deconsolidate(hammer2_trans_t *trans,
			       hammer2_inode_t *dip,
			       hammer2_chain_t **chainp,
			       hammer2_chain_t **ochainp)
{
	if (*ochainp == NULL)
		return (0);
	/* XXX */
	return (0);
}

/*
 * The caller presents a locked cluster with an obj_type of
 * HAMMER2_OBJTYPE_HARDLINK in (*clusterp).  This routine will locate
 * the inode and replace (*clusterp) with a new locked cluster containing
 * the target hardlink, also locked.  The original cluster will be
 * unlocked and released.
 *
 * If cparentp is not NULL a locked cluster representing the hardlink's
 * parent is also returned.
 *
 * If we are unable to locate the hardlink target EIO is returned,
 * (*cparentp) is set to NULL, the original passed-in (*clusterp)
 * will be unlocked and released and (*clusterp) will be set to NULL
 * as well.
 */
int
hammer2_hardlink_find(hammer2_inode_t *dip,
		      hammer2_cluster_t **cparentp,
		      hammer2_cluster_t **clusterp)
{
	const hammer2_inode_data_t *ipdata;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *rcluster;
	hammer2_inode_t *ip;
	hammer2_inode_t *pip;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;

	cluster = *clusterp;
	pip = dip;
	hammer2_inode_ref(pip);		/* for loop */

	/*
	 * Locate the hardlink.  pip is referenced and not locked.
	 * Unlock and release (*clusterp) after extracting the needed
	 * data.
	 */
	ipdata = &hammer2_cluster_rdata(cluster)->ipdata;
	lhc = ipdata->meta.inum;
	ipdata = NULL;			/* safety */
	hammer2_cluster_unlock(cluster);
	hammer2_cluster_drop(cluster);
	*clusterp = NULL;		/* safety */

	rcluster = NULL;
	cparent = NULL;

	while ((ip = pip) != NULL) {
		hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
		cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);
		hammer2_inode_drop(ip);			/* loop */
		KKASSERT(hammer2_cluster_type(cparent) ==
			 HAMMER2_BREF_TYPE_INODE);
		rcluster = hammer2_cluster_lookup(cparent, &key_dummy,
					     lhc, lhc, 0);
		if (rcluster)
			break;
		hammer2_cluster_lookup_done(cparent);	/* discard parent */
		cparent = NULL;				/* safety */
		pip = ip->pip;		/* safe, ip held locked */
		if (pip)
			hammer2_inode_ref(pip);		/* loop */
		hammer2_inode_unlock(ip, NULL);
	}

	/*
	 * chain is locked, ip is locked.  Unlock ip, return the locked
	 * chain.  *ipp is already set w/a ref count and not locked.
	 *
	 * (cparent is already unlocked).
	 */
	*clusterp = rcluster;
	if (rcluster) {
		if (cparentp) {
			*cparentp = cparent;
			hammer2_inode_unlock(ip, NULL);
		} else {
			hammer2_inode_unlock(ip, cparent);
		}
		return (0);
	} else {
		if (cparentp)
			*cparentp = NULL;
		if (ip)
			hammer2_inode_unlock(ip, cparent);
		return (EIO);
	}
}

/*
 * Find the directory common to both fdip and tdip.
 *
 * Returns a held but not locked inode.  Caller typically locks the inode,
 * and when through unlocks AND drops it.
 */
hammer2_inode_t *
hammer2_inode_common_parent(hammer2_inode_t *fdip, hammer2_inode_t *tdip)
{
	hammer2_inode_t *scan1;
	hammer2_inode_t *scan2;

	/*
	 * We used to have a depth field but it complicated matters too
	 * much for directory renames.  So now its ugly.  Check for
	 * simple cases before giving up and doing it the expensive way.
	 *
	 * XXX need a bottom-up topology stability lock
	 */
	if (fdip == tdip || fdip == tdip->pip) {
		hammer2_inode_ref(fdip);
		return(fdip);
	}
	if (fdip->pip == tdip) {
		hammer2_inode_ref(tdip);
		return(tdip);
	}

	/*
	 * XXX not MPSAFE
	 */
	for (scan1 = fdip; scan1->pmp == fdip->pmp; scan1 = scan1->pip) {
		scan2 = tdip;
		while (scan2->pmp == tdip->pmp) {
			if (scan1 == scan2) {
				hammer2_inode_ref(scan1);
				return(scan1);
			}
			scan2 = scan2->pip;
			if (scan2 == NULL)
				break;
		}
	}
	panic("hammer2_inode_common_parent: no common parent %p %p\n",
	      fdip, tdip);
	/* NOT REACHED */
	return(NULL);
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
void
hammer2_inode_modify(hammer2_trans_t *trans, hammer2_inode_t *ip)
{
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	if (ip->vp)
		vsetisdirty(ip->vp);
}

/*
 * Synchronize the inode's frontend state with the chain state prior
 * to any explicit flush of the inode or any strategy write call.
 *
 * Called with a locked inode.
 */
void
hammer2_inode_fsync(hammer2_trans_t *trans, hammer2_inode_t *ip, 
		    hammer2_cluster_t *cparent)
{
	int clear_directdata = 0;

	/* temporary hack, allow cparent to be NULL */
	if (cparent == NULL) {
		cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);
		hammer2_inode_fsync(trans, ip, cparent);
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);
		return;
	}

	if ((ip->flags & HAMMER2_INODE_RESIZED) == 0) {
		/* do nothing */
	} else if (ip->meta.size < ip->osize) {
		/*
		 * We must delete any chains beyond the EOF.  The chain
		 * straddling the EOF will be pending in the bioq.
		 */
		hammer2_cluster_t *dparent;
		hammer2_cluster_t *cluster;
		hammer2_key_t lbase;
		hammer2_key_t key_next;

		lbase = (ip->meta.size + HAMMER2_PBUFMASK64) &
			~HAMMER2_PBUFMASK64;
		dparent = hammer2_cluster_lookup_init(&ip->cluster, 0);
		cluster = hammer2_cluster_lookup(dparent, &key_next,
					         lbase, (hammer2_key_t)-1,
						 HAMMER2_LOOKUP_NODATA);
		while (cluster) {
			/*
			 * Degenerate embedded case, nothing to loop on
			 */
			switch (hammer2_cluster_type(cluster)) {
			case HAMMER2_BREF_TYPE_INODE:
				hammer2_cluster_unlock(cluster);
				hammer2_cluster_drop(cluster);
				cluster = NULL;
				break;
			case HAMMER2_BREF_TYPE_DATA:
				hammer2_cluster_delete(trans, dparent, cluster,
						   HAMMER2_DELETE_PERMANENT);
				/* fall through */
			default:
				cluster = hammer2_cluster_next(dparent, cluster,
						   &key_next,
						   key_next, (hammer2_key_t)-1,
						   HAMMER2_LOOKUP_NODATA);
				break;
			}
		}
		hammer2_cluster_lookup_done(dparent);
		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED);
		KKASSERT(ip->flags & HAMMER2_INODE_MODIFIED);
	} else if (ip->meta.size > ip->osize) {
		/*
		 * When resizing larger we may not have any direct-data
		 * available.
		 */
		if ((ip->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) &&
		    ip->meta.size > HAMMER2_EMBEDDED_BYTES) {
			ip->meta.op_flags &= ~HAMMER2_OPFLAG_DIRECTDATA;
			clear_directdata = 1;
		}
		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED);
		KKASSERT(ip->flags & HAMMER2_INODE_MODIFIED);
	} else {
		/*
		 * RESIZED was set but size didn't change.
		 */
		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED);
		KKASSERT(ip->flags & HAMMER2_INODE_MODIFIED);
	}

	/*
	 * Sync inode meta-data
	 */
	if (ip->flags & HAMMER2_INODE_MODIFIED) {
		hammer2_inode_data_t *wipdata;

		atomic_clear_int(&ip->flags, HAMMER2_INODE_MODIFIED);
		hammer2_cluster_modify(trans, cparent, 0);
		hammer2_inode_repoint(ip, NULL, cparent);

		wipdata = &hammer2_cluster_wdata(cparent)->ipdata;
		wipdata->meta = ip->meta;
		if (clear_directdata) {
			bzero(&wipdata->u.blockset,
			      sizeof(wipdata->u.blockset));
		}
		hammer2_cluster_modsync(cparent);
	}
}
