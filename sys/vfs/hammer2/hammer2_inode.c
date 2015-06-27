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
 *
 * The inode does not have to be locked.
 */
hammer2_chain_t *
hammer2_inode_chain(hammer2_inode_t *ip, int clindex, int how)
{
	hammer2_chain_t *chain;

	hammer2_spin_sh(&ip->cluster_spin);
	if (clindex >= ip->cluster.nchains)
		chain = NULL;
	else
		chain = ip->cluster.array[clindex].chain;
	if (chain) {
		hammer2_chain_ref(chain);
		hammer2_spin_unsh(&ip->cluster_spin);
		hammer2_chain_lock(chain, how);
	} else {
		hammer2_spin_unsh(&ip->cluster_spin);
	}
	return chain;
}

void
hammer2_inode_unlock(hammer2_inode_t *ip)
{
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
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	hammer2_pfs_t *pmp;
	struct vnode *vp;

	pmp = ip->pmp;
	KKASSERT(pmp != NULL);
	*errorp = 0;

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

		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			vp->v_type = VREG;
			vinitvmio(vp, ip->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			/*
			 * XXX for now we are using the generic file_read
			 * and file_write code so we need a buffer cache
			 * association.
			 */
			vp->v_type = VLNK;
			vinitvmio(vp, ip->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_CDEV:
			vp->v_type = VCHR;
			/* fall through */
		case HAMMER2_OBJTYPE_BDEV:
			vp->v_ops = &pmp->mp->mnt_vn_spec_ops;
			if (ip->meta.type != HAMMER2_OBJTYPE_CDEV)
				vp->v_type = VBLK;
			addaliasu(vp,
				  ip->meta.rmajor,
				  ip->meta.rminor);
			break;
		case HAMMER2_OBJTYPE_FIFO:
			vp->v_type = VFIFO;
			vp->v_ops = &pmp->mp->mnt_vn_fifo_ops;
			break;
		default:
			panic("hammer2: unhandled objtype %d",
			      ip->meta.type);
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
hammer2_inode_create(hammer2_inode_t *dip,
		     struct vattr *vap, struct ucred *cred,
		     const uint8_t *name, size_t name_len, hammer2_key_t lhc,
		     hammer2_key_t inum, uint8_t type, uint8_t target_type,
		     int flags, int *errorp)
{
	hammer2_xop_create_t *xop;
	hammer2_inode_t *nip;
	int error;
	uid_t xuid;
	uuid_t dip_uid;
	uuid_t dip_gid;
	uint32_t dip_mode;
	uint8_t dip_comp_algo;
	uint8_t dip_check_algo;

	if (name)
		lhc = hammer2_dirhash(name, name_len);
	*errorp = 0;
	nip = NULL;

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 *
	 * NOTE: hidden inodes do not have iterators.
	 *
	 * Lock the directory exclusively for now to guarantee that
	 * we can find an unused lhc for the name.  Due to collisions,
	 * two different creates can end up with the same lhc so we
	 * cannot depend on the OS to prevent the collision.
	 */
	hammer2_inode_lock(dip, 0);

	dip_uid = dip->meta.uid;
	dip_gid = dip->meta.gid;
	dip_mode = dip->meta.mode;
	dip_comp_algo = dip->meta.comp_algo;
	dip_check_algo = dip->meta.check_algo;

	/*
	 * If name specified, locate an unused key in the collision space.
	 * Otherwise use the passed-in lhc directly.
	 */
	if (name) {
		hammer2_xop_scanlhc_t *sxop;
		hammer2_key_t lhcbase;

		lhcbase = lhc;
		sxop = &hammer2_xop_alloc(dip)->xop_scanlhc;
		sxop->lhc = lhc;
		hammer2_xop_start(&sxop->head, hammer2_xop_scanlhc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (lhc != sxop->head.cluster.focus->bref.key)
				break;
			++lhc;
		}
		hammer2_xop_retire(&sxop->head, HAMMER2_XOPMASK_VOP);

		if (error) {
			if (error != ENOENT)
				goto done2;
			++lhc;
			error = 0;
		}
		if ((lhcbase ^ lhc) & ~HAMMER2_DIRHASH_LOMASK) {
			error = ENOSPC;
			goto done2;
		}
	}

	/*
	 * Create the inode with the lhc as the key.
	 */
	xop = &hammer2_xop_alloc(dip)->xop_create;
	xop->lhc = lhc;
	xop->flags = flags;
	bzero(&xop->meta, sizeof(xop->meta));

	if (vap) {
		xop->meta.type = hammer2_get_obj_type(vap->va_type);

		switch (xop->meta.type) {
		case HAMMER2_OBJTYPE_CDEV:
		case HAMMER2_OBJTYPE_BDEV:
			xop->meta.rmajor = vap->va_rmajor;
			xop->meta.rminor = vap->va_rminor;
			break;
		default:
			break;
		}
		type = xop->meta.type;
	} else {
		xop->meta.type = type;
		xop->meta.target_type = target_type;
	}
	xop->meta.inum = inum;
	
	/* Inherit parent's inode compression mode. */
	xop->meta.comp_algo = dip_comp_algo;
	xop->meta.check_algo = dip_check_algo;
	xop->meta.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&xop->meta.ctime);
	xop->meta.mtime = xop->meta.ctime;
	if (vap)
		xop->meta.mode = vap->va_mode;
	xop->meta.nlinks = 1;
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
			xop->meta.uid = vap->va_uid_uuid;
		else if (vap->va_uid != (uid_t)VNOVAL)
			hammer2_guid_to_uuid(&xop->meta.uid, vap->va_uid);
		else
			hammer2_guid_to_uuid(&xop->meta.uid, xuid);

		if (vap->va_vaflags & VA_GID_UUID_VALID)
			xop->meta.gid = vap->va_gid_uuid;
		else if (vap->va_gid != (gid_t)VNOVAL)
			hammer2_guid_to_uuid(&xop->meta.gid, vap->va_gid);
		else if (dip)
			xop->meta.gid = dip_gid;
	}

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (xop->meta.type == HAMMER2_OBJTYPE_REGFILE ||
	    xop->meta.type == HAMMER2_OBJTYPE_SOFTLINK ||
	    xop->meta.type == HAMMER2_OBJTYPE_HARDLINK) {
		xop->meta.op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}
	if (name)
		hammer2_xop_setname(&xop->head, name, name_len);
	xop->meta.name_len = name_len;
	xop->meta.name_key = lhc;
	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);

	hammer2_xop_start(&xop->head, hammer2_inode_xop_create);

	error = hammer2_xop_collect(&xop->head, 0);
#if INODE_DEBUG
	kprintf("CREATE INODE %*.*s\n",
		(int)name_len, (int)name_len, name);
#endif

	if (error) {
		*errorp = error;
		goto done;
	}

	/*
	 * Set up the new inode if not a hardlink pointer.
	 *
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 *
	 * NOTE: Only one new inode can currently be created per
	 *	 transaction.  If the need arises we can adjust
	 *	 hammer2_trans_init() to allow more.
	 *
	 * NOTE: nipdata will have chain's blockset data.
	 */
	if (type != HAMMER2_OBJTYPE_HARDLINK) {
		nip = hammer2_inode_get(dip->pmp, dip, &xop->head.cluster);
		nip->comp_heuristic = 0;
	} else {
		nip = NULL;
	}

done:
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
done2:
	hammer2_inode_unlock(dip);

	return (nip);
}

/*
 * Connect the disconnected inode (ip) to the directory (dip) with the
 * specified (name, name_len).  If name is NULL, (lhc) will be used as
 * the directory key and the inode's embedded name will not be modified
 * for future recovery purposes.
 *
 * dip and ip must both be locked exclusively (dip in particular to avoid
 * lhc collisions).
 */
int
hammer2_inode_connect_simple(hammer2_inode_t *dip, hammer2_inode_t *ip,
			     const char *name, size_t name_len,
			     hammer2_key_t lhc)
{
	hammer2_xop_scanlhc_t *sxop;
	hammer2_xop_connect_t *xop;
	hammer2_inode_t *opip;
	hammer2_key_t lhcbase;
	int error;

	/*
	 * Calculate the lhc and resolve the collision space.
	 */
	if (name) {
		lhc = lhcbase = hammer2_dirhash(name, name_len);
		sxop = &hammer2_xop_alloc(dip)->xop_scanlhc;
		sxop->lhc = lhc;
		hammer2_xop_start(&sxop->head, hammer2_xop_scanlhc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (lhc != sxop->head.cluster.focus->bref.key)
				break;
			++lhc;
		}
		hammer2_xop_retire(&sxop->head, HAMMER2_XOPMASK_VOP);

		if (error) {
			if (error != ENOENT)
				goto done;
			++lhc;
			error = 0;
		}
		if ((lhcbase ^ lhc) & ~HAMMER2_DIRHASH_LOMASK) {
			error = ENOSPC;
			goto done;
		}
	} else {
		error = 0;
	}

	/*
	 * Formally reconnect the in-memory structure.  ip must
	 * be locked exclusively to safely change ip->pip.
	 */
	if (ip->pip != dip) {
		hammer2_inode_ref(dip);
		opip = ip->pip;
		ip->pip = dip;
		if (opip)
			hammer2_inode_drop(opip);
	}

	/*
	 * Connect her up
	 */
	xop = &hammer2_xop_alloc(dip)->xop_connect;
	if (name)
		hammer2_xop_setname(&xop->head, name, name_len);
	hammer2_xop_setip2(&xop->head, ip);
	xop->lhc = lhc;
	hammer2_xop_start(&xop->head, hammer2_inode_xop_connect);
	error = hammer2_xop_collect(&xop->head, 0);
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

	/*
	 * On success make the same adjustments to ip->meta or the
	 * next flush may blow up the chain.
	 */
	if (error == 0) {
		hammer2_inode_modify(ip);
		ip->meta.name_key = lhc;
		if (name)
			ip->meta.name_len = name_len;
	}
done:
	return error;
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
 * Called with a locked inode to finish unlinking an inode after xop_unlink
 * had been run.  This function is responsible for decrementing nlinks and
 * moving deleted inodes to the hidden directory if they are still open.
 *
 * We don't bother decrementing nlinks if the file is not open and this was
 * the last link.
 *
 * If the inode is a hardlink target it's chain has not yet been deleted,
 * otherwise it's chain has been deleted.
 *
 * If isopen then any prior deletion was not permanent and the inode must
 * be moved to the hidden directory.
 */
int
hammer2_inode_unlink_finisher(hammer2_inode_t *ip, int isopen)
{
	hammer2_pfs_t *pmp;
	int error;

	pmp = ip->pmp;

	/*
	 * Decrement nlinks.  If this is the last link and the file is
	 * not open, the chain has already been removed and we don't bother
	 * dirtying the inode.
	 */
	if (ip->meta.nlinks == 1) {
		atomic_set_int(&ip->flags, HAMMER2_INODE_ISUNLINKED);
		if (isopen == 0)
			return 0;
	}

	hammer2_inode_modify(ip);
	--ip->meta.nlinks;
	if ((int64_t)ip->meta.nlinks < 0)
		ip->meta.nlinks = 0;	/* safety */

	/*
	 * If nlinks is not zero we are done.  However, this should only be
	 * possible with a hardlink target.  If the inode is an embedded
	 * hardlink nlinks should have dropped to zero, warn and proceed
	 * with the next step.
	 */
	if (ip->meta.nlinks) {
		if ((ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) == 0)
			return 0;
		kprintf("hammer2_inode_unlink: nlinks was not 0 (%jd)\n",
			(intmax_t)ip->meta.nlinks);
		return 0;
	}

	/*
	 * nlinks is now zero, the inode should have already been deleted.
	 * If the file is open it was deleted non-permanently and must be
	 * moved to the hidden directory.
	 *
	 * When moving to the hidden directory we force the name_key to the
	 * inode number to avoid collisions.
	 */
	if (isopen) {
		hammer2_inode_lock(pmp->ihidden, 0);
		error = hammer2_inode_connect_simple(pmp->ihidden, ip,
						     NULL, 0, ip->meta.inum);
		hammer2_inode_unlock(pmp->ihidden);
	} else {
		error = 0;
	}
	return error;
}

/*
 * This is called from the mount code to initialize pmp->ihidden
 */
void
hammer2_inode_install_hidden(hammer2_pfs_t *pmp)
{
	int error;

	if (pmp->ihidden)
		return;

	hammer2_trans_init(pmp, 0);
	hammer2_inode_lock(pmp->iroot, 0);

	/*
	 * Find the hidden directory
	 */
	{
		hammer2_xop_lookup_t *xop;

		xop = &hammer2_xop_alloc(pmp->iroot)->xop_lookup;
		xop->lhc = HAMMER2_INODE_HIDDENDIR;
		hammer2_xop_start(&xop->head, hammer2_xop_lookup);
		error = hammer2_xop_collect(&xop->head, 0);

		if (error == 0) {
			/*
			 * Found the hidden directory
			 */
			kprintf("PFS FOUND HIDDEN DIR\n");
			pmp->ihidden = hammer2_inode_get(pmp, pmp->iroot,
							 &xop->head.cluster);
			hammer2_inode_ref(pmp->ihidden);
			hammer2_inode_unlock(pmp->ihidden);
		}
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}

	/*
	 * Create the hidden directory if it could not be found.
	 */
	if (error == ENOENT) {
		kprintf("PFS CREATE HIDDEN DIR\n");

		pmp->ihidden = hammer2_inode_create(pmp->iroot, NULL, NULL,
						    NULL, 0,
				/* lhc */	    HAMMER2_INODE_HIDDENDIR,
				/* inum */	    HAMMER2_INODE_HIDDENDIR,
				/* type */	    HAMMER2_OBJTYPE_DIRECTORY,
				/* target_type */   0,
				/* flags */	    0,
						    &error);
		if (pmp->ihidden) {
			hammer2_inode_ref(pmp->ihidden);
			hammer2_inode_unlock(pmp->ihidden);
		}
		if (error)
			kprintf("PFS CREATE ERROR %d\n", error);
	}

	/*
	 * Scan the hidden directory on-mount and destroy its contents
	 */
	if (error == 0) {
		hammer2_xop_unlinkall_t *xop;

		hammer2_inode_lock(pmp->ihidden, 0);
		xop = &hammer2_xop_alloc(pmp->ihidden)->xop_unlinkall;
		xop->head.lkey = 0;
		hammer2_xop_start(&xop->head, hammer2_inode_xop_unlinkall);

		while ((error = hammer2_xop_collect(&xop->head, 0)) == 0) {
			;
		}
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		hammer2_inode_unlock(pmp->ihidden);
	}

	hammer2_inode_unlock(pmp->iroot);
	hammer2_trans_done(pmp);
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
hammer2_inode_modify(hammer2_inode_t *ip)
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
hammer2_inode_fsync(hammer2_inode_t *ip, hammer2_cluster_t *cparent)
{
	int clear_directdata = 0;

	/* temporary hack, allow cparent to be NULL */
	if (cparent == NULL) {
		cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS);
		hammer2_inode_fsync(ip, cparent);
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
				hammer2_cluster_delete(dparent, cluster,
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
		hammer2_cluster_modify(cparent, 0);
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

/*
 * This handles unlinked open files after the vnode is finally dereferenced.
 * To avoid deadlocks it cannot be called from the normal vnode recycling
 * path, so we call it (1) after a unlink, rmdir, or rename, (2) on every
 * flush, and (3) on umount.
 *
 * Caller must be in a transaction.
 */
void
hammer2_inode_run_unlinkq(hammer2_pfs_t *pmp)
{
	hammer2_xop_destroy_t *xop;
	hammer2_inode_unlink_t *ipul;
	hammer2_inode_t *ip;
	int error;

	if (TAILQ_EMPTY(&pmp->unlinkq))
		return;

	LOCKSTART;
	hammer2_spin_ex(&pmp->list_spin);
	while ((ipul = TAILQ_FIRST(&pmp->unlinkq)) != NULL) {
		TAILQ_REMOVE(&pmp->unlinkq, ipul, entry);
		hammer2_spin_unex(&pmp->list_spin);
		ip = ipul->ip;
		kfree(ipul, pmp->minode);

		hammer2_inode_lock(ip, 0);
		xop = &hammer2_xop_alloc(ip)->xop_destroy;
		hammer2_xop_start(&xop->head, hammer2_inode_xop_destroy);
		error = hammer2_xop_collect(&xop->head, 0);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

		hammer2_inode_unlock(ip);
		hammer2_inode_drop(ip);			/* ipul ref */

		hammer2_spin_ex(&pmp->list_spin);
	}
	hammer2_spin_unex(&pmp->list_spin);
	LOCKSTOP;
}

/*
 * Inode create helper (threaded, backend)
 *
 * Used by ncreate, nmknod, nsymlink, nmkdir.
 * Used by nlink and rename to create HARDLINK pointers.
 *
 * Frontend holds the parent directory ip locked exclusively.  We
 * create the inode and feed the exclusively locked chain to the
 * frontend.
 */
void
hammer2_inode_xop_create(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_create_t *xop = &arg->xop_create;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;
	int error;

	chain = NULL;
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = EIO;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_next,
				     xop->lhc, xop->lhc,
				     &cache_index, 0);
	if (chain) {
		hammer2_chain_unlock(chain);
		error = EEXIST;
		goto fail;
	}

	error = hammer2_chain_create(&parent, &chain,
				     xop->head.ip->pmp,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     xop->flags);
	if (error == 0) {
		hammer2_chain_modify(chain, 0);
		chain->data->ipdata.meta = xop->meta;
		if (xop->head.name) {
			bcopy(xop->head.name,
			      chain->data->ipdata.filename,
			      xop->head.name_len);
			chain->data->ipdata.meta.name_len = xop->head.name_len;
		}
		chain->data->ipdata.meta.name_key = xop->lhc;
	}
	hammer2_chain_unlock(chain);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
				  HAMMER2_RESOLVE_SHARED);
fail:
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
}

/*
 * Inode delete helper (backend, threaded)
 *
 * Generally used by hammer2_run_unlinkq()
 */
void
hammer2_inode_xop_destroy(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_destroy_t *xop = &arg->xop_destroy;
	hammer2_pfs_t *pmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
	int error;

	/*
	 * We need the precise parent chain to issue the deletion.
	 */
	ip = xop->head.ip;
	pmp = ip->pmp;
	chain = NULL;

	parent = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	if (parent)
		hammer2_chain_getparent(&parent, HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		error = EIO;
		goto done;
	}
	chain = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	if (chain == NULL) {
		error = EIO;
		goto done;
	}
	hammer2_chain_delete(parent, chain, 0);
	error = 0;
done:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

void
hammer2_inode_xop_unlinkall(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_unlinkall_t *xop = &arg->xop_unlinkall;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int cache_index = -1;

	/*
	 * We need the precise parent chain to issue the deletion.
	 */
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS);
	while (chain) {
		hammer2_chain_delete(parent, chain, HAMMER2_DELETE_PERMANENT);
		hammer2_chain_unlock(chain);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
					  HAMMER2_RESOLVE_SHARED);
		hammer2_xop_feed(&xop->head, chain, clindex, chain->error);
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, HAMMER2_KEY_MAX,
					   &cache_index,
					   HAMMER2_LOOKUP_ALWAYS |
					   HAMMER2_LOOKUP_NOUNLOCK);
	}
	hammer2_xop_feed(&xop->head, NULL, clindex, ENOENT);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}

void
hammer2_inode_xop_connect(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_connect_t *xop = &arg->xop_connect;
	hammer2_inode_data_t *wipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_pfs_t *pmp;
	hammer2_key_t key_dummy;
	int cache_index = -1;
	int error;

	/*
	 * Get directory, then issue a lookup to prime the parent chain
	 * for the create.  The lookup is expected to fail.
	 */
	pmp = xop->head.ip->pmp;
	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS);
	if (parent == NULL) {
		chain = NULL;
		error = EIO;
		goto fail;
	}
	chain = hammer2_chain_lookup(&parent, &key_dummy,
				     xop->lhc, xop->lhc,
				     &cache_index, 0);
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;
		error = EEXIST;
		goto fail;
	}

	/*
	 * Adjust the filename in the inode, set the name key.
	 *
	 * NOTE: Frontend must also adjust ip2->meta on success, we can't
	 *	 do it here.
	 */
	chain = hammer2_inode_chain(xop->head.ip2, clindex,
				    HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_modify(chain, 0);
	wipdata = &chain->data->ipdata;

	hammer2_inode_modify(xop->head.ip2);
	if (xop->head.name) {
		bzero(wipdata->filename, sizeof(wipdata->filename));
		bcopy(xop->head.name, wipdata->filename, xop->head.name_len);
		wipdata->meta.name_len = xop->head.name_len;
	}
	wipdata->meta.name_key = xop->lhc;

	/*
	 * Reconnect the chain to the new parent directory
	 */
	error = hammer2_chain_create(&parent, &chain, pmp,
				     xop->lhc, 0,
				     HAMMER2_BREF_TYPE_INODE,
				     HAMMER2_INODE_BYTES,
				     0);

	/*
	 * Feed result back.
	 */
fail:
	hammer2_xop_feed(&xop->head, NULL, clindex, error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
}
