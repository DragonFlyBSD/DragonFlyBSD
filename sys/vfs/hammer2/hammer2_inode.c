/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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

RB_GENERATE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
	     hammer2_tid_t, inum);

int
hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2)
{
	if (ip1->inum < ip2->inum)
		return(-1);
	if (ip1->inum > ip2->inum)
		return(1);
	return(0);
}

/*
 * HAMMER2 inode locks
 *
 * HAMMER2 offers shared locks and exclusive locks on inodes.
 *
 * An inode's ip->chain pointer is resolved and stable while an inode is
 * locked, and can be cleaned out at any time (become NULL) when an inode
 * is not locked.
 *
 * This function handles duplication races and hardlink replacement races
 * which can cause ip's cached chain to become stale.
 *
 * The underlying chain is also locked and returned.
 *
 * NOTE: We don't combine the inode/chain lock because putting away an
 *       inode would otherwise confuse multiple lock holders of the inode.
 */
hammer2_chain_t *
hammer2_inode_lock_ex(hammer2_inode_t *ip)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *ochain;
	hammer2_chain_core_t *core;
	int error;

	hammer2_inode_ref(ip);
	ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);

	chain = ip->chain;
	core = chain->core;
	for (;;) {
		if (chain->flags & HAMMER2_CHAIN_DUPLICATED) {
			spin_lock(&core->cst.spin);
			while (chain->flags & HAMMER2_CHAIN_DUPLICATED)
				chain = TAILQ_NEXT(chain, core_entry);
			hammer2_chain_ref(chain);
			spin_unlock(&core->cst.spin);
			hammer2_inode_repoint(ip, NULL, chain);
			hammer2_chain_drop(chain);
		}
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0)
			break;
		hammer2_chain_unlock(chain);
	}
	if (chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
		error = hammer2_hardlink_find(ip->pip, &chain, &ochain);
		hammer2_chain_drop(ochain);
		KKASSERT(error == 0);
		/* XXX error handling */
	}
	return (chain);
}

void
hammer2_inode_unlock_ex(hammer2_inode_t *ip, hammer2_chain_t *chain)
{
	/*
	 * XXX this will catch parent directories too which we don't
	 *     really want.
	 */
	if (chain)
		hammer2_chain_unlock(chain);
	ccms_thread_unlock(&ip->topo_cst);
	hammer2_inode_drop(ip);
}

/*
 * NOTE: We don't combine the inode/chain lock because putting away an
 *       inode would otherwise confuse multiple lock holders of the inode.
 *
 *	 Shared locks are especially sensitive to having too many shared
 *	 lock counts (from the same thread) on certain paths which might
 *	 need to upgrade them.  Only one count of a shared lock can be
 *	 upgraded.
 */
hammer2_chain_t *
hammer2_inode_lock_sh(hammer2_inode_t *ip)
{
	hammer2_chain_t *chain;

	hammer2_inode_ref(ip);
	for (;;) {
		ccms_thread_lock(&ip->topo_cst, CCMS_STATE_SHARED);

		chain = ip->chain;
		KKASSERT(chain != NULL);	/* for now */
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
					  HAMMER2_RESOLVE_SHARED);

		/*
		 * Resolve duplication races, resolve hardlinks by giving
		 * up and cycling an exclusive lock.
		 */
		if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0 &&
		    chain->data->ipdata.type != HAMMER2_OBJTYPE_HARDLINK) {
			break;
		}
		hammer2_chain_unlock(chain);
		ccms_thread_unlock(&ip->topo_cst);
		chain = hammer2_inode_lock_ex(ip);
		hammer2_inode_unlock_ex(ip, chain);
	}
	return (chain);
}

void
hammer2_inode_unlock_sh(hammer2_inode_t *ip, hammer2_chain_t *chain)
{
	if (chain)
		hammer2_chain_unlock(chain);
	ccms_thread_unlock(&ip->topo_cst);
	hammer2_inode_drop(ip);
}

ccms_state_t
hammer2_inode_lock_temp_release(hammer2_inode_t *ip)
{
	return(ccms_thread_lock_temp_release(&ip->topo_cst));
}

void
hammer2_inode_lock_temp_restore(hammer2_inode_t *ip, ccms_state_t ostate)
{
	ccms_thread_lock_temp_restore(&ip->topo_cst, ostate);
}

ccms_state_t
hammer2_inode_lock_upgrade(hammer2_inode_t *ip)
{
	return(ccms_thread_lock_upgrade(&ip->topo_cst));
}

void
hammer2_inode_lock_downgrade(hammer2_inode_t *ip, ccms_state_t ostate)
{
	ccms_thread_lock_downgrade(&ip->topo_cst, ostate);
}

/*
 * Lookup an inode by inode number
 */
hammer2_inode_t *
hammer2_inode_lookup(hammer2_pfsmount_t *pmp, hammer2_tid_t inum)
{
	hammer2_inode_t *ip;

	if (pmp) {
		spin_lock(&pmp->inum_spin);
		ip = RB_LOOKUP(hammer2_inode_tree, &pmp->inum_tree, inum);
		if (ip)
			hammer2_inode_ref(ip);
		spin_unlock(&pmp->inum_spin);
	} else {
		ip = NULL;
	}
	return(ip);
}

/*
 * Adding a ref to an inode is only legal if the inode already has at least
 * one ref.
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
	hammer2_pfsmount_t *pmp;
	hammer2_inode_t *pip;
	u_int refs;

	while (ip) {
		refs = ip->refs;
		cpu_ccfence();
		if (refs == 1) {
			/*
			 * Transition to zero, must interlock with
			 * the inode inumber lookup tree (if applicable).
			 *
			 * NOTE: The super-root inode has no pmp.
			 */
			pmp = ip->pmp;
			if (pmp)
				spin_lock(&pmp->inum_spin);

			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				KKASSERT(ip->topo_cst.count == 0);
				if (ip->flags & HAMMER2_INODE_ONRBTREE) {
					atomic_clear_int(&ip->flags,
						     HAMMER2_INODE_ONRBTREE);
					RB_REMOVE(hammer2_inode_tree,
						  &pmp->inum_tree, ip);
				}
				if (pmp)
					spin_unlock(&pmp->inum_spin);

				pip = ip->pip;
				ip->pip = NULL;
				ip->pmp = NULL;

				/*
				 * Cleaning out ip->chain isn't entirely
				 * trivial.
				 */
				hammer2_inode_repoint(ip, NULL, NULL);

				/*
				 * We have to drop pip (if non-NULL) to
				 * dispose of our implied reference from
				 * ip->pip.  We can simply loop on it.
				 */
				if (pmp) {
					KKASSERT((ip->flags &
						  HAMMER2_INODE_SROOT) == 0);
					kfree(ip, pmp->minode);
					atomic_add_long(&pmp->inmem_inodes, -1);
				} else {
					KKASSERT(ip->flags &
						 HAMMER2_INODE_SROOT);
					kfree(ip, M_HAMMER2);
				}
				ip = pip;
				/* continue with pip (can be NULL) */
			} else {
				if (pmp)
					spin_unlock(&ip->pmp->inum_spin);
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
	hammer2_inode_data_t *ipdata;
	hammer2_pfsmount_t *pmp;
	struct vnode *vp;
	ccms_state_t ostate;

	pmp = ip->pmp;
	KKASSERT(pmp != NULL);
	*errorp = 0;
	ipdata = &ip->chain->data->ipdata;

	for (;;) {
		/*
		 * Attempt to reuse an existing vnode assignment.  It is
		 * possible to race a reclaim so the vget() may fail.  The
		 * inode must be unlocked during the vget() to avoid a
		 * deadlock against a reclaim.
		 */
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
		ostate = hammer2_inode_lock_upgrade(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_lock_downgrade(ip, ostate);
			continue;
		}

		switch (ipdata->type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			vp->v_type = VREG;
			vinitvmio(vp, ipdata->size,
				  HAMMER2_LBUFSIZE,
				  (int)ipdata->size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			/*
			 * XXX for now we are using the generic file_read
			 * and file_write code so we need a buffer cache
			 * association.
			 */
			vp->v_type = VLNK;
			vinitvmio(vp, ipdata->size,
				  HAMMER2_LBUFSIZE,
				  (int)ipdata->size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_CDEV:
			vp->v_type = VCHR;
			/* fall through */
		case HAMMER2_OBJTYPE_BDEV:
			vp->v_ops = &pmp->mp->mnt_vn_spec_ops;
			if (ipdata->type != HAMMER2_OBJTYPE_CDEV)
				vp->v_type = VBLK;
			addaliasu(vp, ipdata->rmajor, ipdata->rminor);
			break;
		case HAMMER2_OBJTYPE_FIFO:
			vp->v_type = VFIFO;
			vp->v_ops = &pmp->mp->mnt_vn_fifo_ops;
			break;
		default:
			panic("hammer2: unhandled objtype %d", ipdata->type);
			break;
		}

		if (ip == pmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_inode_ref(ip);		/* vp association */
		hammer2_inode_lock_downgrade(ip, ostate);
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
 * The passed-in chain must be locked and the returned inode will also be
 * locked.  This routine typically locates or allocates the inode, assigns
 * ip->chain (adding a ref to chain if necessary), and returns the inode.
 *
 * The hammer2_inode structure regulates the interface between the high level
 * kernel VNOPS API and the filesystem backend (the chains).
 *
 * WARNING!  This routine sucks up the chain's lock (makes it part of the
 *	     inode lock from the point of view of the inode lock API),
 *	     so callers need to be careful.
 *
 * WARNING!  The mount code is allowed to pass dip == NULL for iroot and
 *	     is allowed to pass pmp == NULL and dip == NULL for sroot.
 */
hammer2_inode_t *
hammer2_inode_get(hammer2_pfsmount_t *pmp, hammer2_inode_t *dip,
		  hammer2_chain_t *chain)
{
	hammer2_inode_t *nip;

	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);

	/*
	 * Interlocked lookup/ref of the inode.  This code is only needed
	 * when looking up inodes with nlinks != 0 (TODO: optimize out
	 * otherwise and test for duplicates).
	 */
again:
	for (;;) {
		nip = hammer2_inode_lookup(pmp, chain->data->ipdata.inum);
		if (nip == NULL)
			break;
		ccms_thread_lock(&nip->topo_cst, CCMS_STATE_EXCLUSIVE);
		if ((nip->flags & HAMMER2_INODE_ONRBTREE) == 0) { /* race */
			ccms_thread_unlock(&nip->topo_cst);
			hammer2_inode_drop(nip);
			continue;
		}
		if (nip->chain != chain)
			hammer2_inode_repoint(nip, NULL, chain);

		/*
		 * Consolidated nip/nip->chain is locked (chain locked
		 * by caller).
		 */
		return nip;
	}

	/*
	 * We couldn't find the inode number, create a new inode.
	 */
	if (pmp) {
		nip = kmalloc(sizeof(*nip), pmp->minode, M_WAITOK | M_ZERO);
		atomic_add_long(&pmp->inmem_inodes, 1);
		hammer2_chain_memory_inc(pmp);
		hammer2_chain_memory_wakeup(pmp);
	} else {
		nip = kmalloc(sizeof(*nip), M_HAMMER2, M_WAITOK | M_ZERO);
		nip->flags = HAMMER2_INODE_SROOT;
	}
	nip->inum = chain->data->ipdata.inum;
	nip->size = chain->data->ipdata.size;
	nip->mtime = chain->data->ipdata.mtime;
	hammer2_inode_repoint(nip, NULL, chain);
	nip->pip = dip;				/* can be NULL */
	if (dip)
		hammer2_inode_ref(dip);	/* ref dip for nip->pip */

	nip->pmp = pmp;

	/*
	 * ref and lock on nip gives it state compatible to after a
	 * hammer2_inode_lock_ex() call.
	 */
	nip->refs = 1;
	ccms_cst_init(&nip->topo_cst, &nip->chain);
	ccms_thread_lock(&nip->topo_cst, CCMS_STATE_EXCLUSIVE);
	/* combination of thread lock and chain lock == inode lock */

	/*
	 * Attempt to add the inode.  If it fails we raced another inode
	 * get.  Undo all the work and try again.
	 */
	if (pmp) {
		spin_lock(&pmp->inum_spin);
		if (RB_INSERT(hammer2_inode_tree, &pmp->inum_tree, nip)) {
			spin_unlock(&pmp->inum_spin);
			ccms_thread_unlock(&nip->topo_cst);
			hammer2_inode_drop(nip);
			goto again;
		}
		atomic_set_int(&nip->flags, HAMMER2_INODE_ONRBTREE);
		spin_unlock(&pmp->inum_spin);
	}

	return (nip);
}

/*
 * Create a new inode in the specified directory using the vattr to
 * figure out the type of inode.
 *
 * If no error occurs the new inode with its chain locked is returned in
 * *nipp, otherwise an error is returned and *nipp is set to NULL.
 *
 * If vap and/or cred are NULL the related fields are not set and the
 * inode type defaults to a directory.  This is used when creating PFSs
 * under the super-root, so the inode number is set to 1 in this case.
 *
 * dip is not locked on entry.
 */
hammer2_inode_t *
hammer2_inode_create(hammer2_trans_t *trans, hammer2_inode_t *dip,
		     struct vattr *vap, struct ucred *cred,
		     const uint8_t *name, size_t name_len,
		     hammer2_chain_t **chainp, int *errorp)
{
	hammer2_inode_data_t *dipdata;
	hammer2_inode_data_t *nipdata;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_inode_t *nip;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	int error;
	uid_t xuid;
	uuid_t dip_uid;
	uuid_t dip_gid;
	uint32_t dip_mode;
	uint8_t dip_algo;
	int cache_index = -1;

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
	parent = hammer2_inode_lock_ex(dip);
	dipdata = &dip->chain->data->ipdata;
	dip_uid = dipdata->uid;
	dip_gid = dipdata->gid;
	dip_mode = dipdata->mode;
	dip_algo = dipdata->comp_algo;

	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(&parent, &key_dummy,
					     lhc, lhc, &cache_index, 0);
		if (chain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_VISIBLE) == 0)
			error = ENOSPC;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(chain);
		chain = NULL;
		++lhc;
	}

	if (error == 0) {
		error = hammer2_chain_create(trans, &parent, &chain,
					     lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES);
	}

	/*
	 * Cleanup and handle retries.
	 */
	if (error == EAGAIN) {
		hammer2_chain_ref(parent);
		hammer2_inode_unlock_ex(dip, parent);
		hammer2_chain_wait(parent);
		hammer2_chain_drop(parent);
		goto retry;
	}
	hammer2_inode_unlock_ex(dip, parent);

	if (error) {
		KKASSERT(chain == NULL);
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
	chain->data->ipdata.inum = trans->inode_tid;
	nip = hammer2_inode_get(dip->pmp, dip, chain);
	nipdata = &chain->data->ipdata;

	if (vap) {
		KKASSERT(trans->inodes_created == 0);
		nipdata->type = hammer2_get_obj_type(vap->va_type);
		nipdata->inum = trans->inode_tid;
		++trans->inodes_created;

		switch (nipdata->type) {
		case HAMMER2_OBJTYPE_CDEV:
		case HAMMER2_OBJTYPE_BDEV:
			nipdata->rmajor = vap->va_rmajor;
			nipdata->rminor = vap->va_rminor;
			break;
		default:
			break;
		}
	} else {
		nipdata->type = HAMMER2_OBJTYPE_DIRECTORY;
		nipdata->inum = 1;
	}
	
	/* Inherit parent's inode compression mode. */
	nip->comp_heuristic = 0;
	nipdata->comp_algo = dip_algo;
	nipdata->version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nipdata->ctime);
	nipdata->mtime = nipdata->ctime;
	if (vap)
		nipdata->mode = vap->va_mode;
	nipdata->nlinks = 1;
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
			nipdata->uid = vap->va_uid_uuid;
		else if (vap->va_uid != (uid_t)VNOVAL)
			hammer2_guid_to_uuid(&nipdata->uid, vap->va_uid);
		else
			hammer2_guid_to_uuid(&nipdata->uid, xuid);

		if (vap->va_vaflags & VA_GID_UUID_VALID)
			nipdata->gid = vap->va_gid_uuid;
		else if (vap->va_gid != (gid_t)VNOVAL)
			hammer2_guid_to_uuid(&nipdata->gid, vap->va_gid);
		else if (dip)
			nipdata->gid = dip_gid;
	}

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (nipdata->type == HAMMER2_OBJTYPE_REGFILE ||
	    nipdata->type == HAMMER2_OBJTYPE_SOFTLINK) {
		nipdata->op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}

	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
	bcopy(name, nipdata->filename, name_len);
	nipdata->name_key = lhc;
	nipdata->name_len = name_len;
	*chainp = chain;

	return (nip);
}

/*
 * chain may have been moved around by the create.
 */
void
hammer2_chain_refactor(hammer2_chain_t **chainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_core_t *core;

	core = chain->core;
	while (chain->flags & HAMMER2_CHAIN_DUPLICATED) {
		spin_lock(&core->cst.spin);
		chain = TAILQ_NEXT(chain, core_entry);
		while (chain->flags & HAMMER2_CHAIN_DUPLICATED)
			chain = TAILQ_NEXT(chain, core_entry);
		hammer2_chain_ref(chain);
		spin_unlock(&core->cst.spin);
		KKASSERT(chain->core == core);

		hammer2_chain_unlock(*chainp);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
					  HAMMER2_RESOLVE_NOREF); /* eat ref */
		*chainp = chain;
	}
}

/*
 * Shift *chainp up to the specified directory, change the filename
 * to "0xINODENUMBER", and adjust the key.  The chain becomes the
 * invisible hardlink target.
 *
 * The original *chainp has already been marked deleted.
 */
static
void
hammer2_hardlink_shiftup(hammer2_trans_t *trans, hammer2_chain_t **chainp,
			hammer2_inode_t *dip, int nlinks, int *errorp)
{
	hammer2_inode_data_t *nipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *xchain;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	hammer2_blockref_t bref;
	int cache_index = -1;

	chain = *chainp;
	lhc = chain->data->ipdata.inum;
	KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  lhc represents the inode number so there is
	 * no collision iteration.
	 *
	 * There should be no key collisions with invisible inode keys.
	 *
	 * WARNING! Must use inode_lock_ex() on dip to handle a stale
	 *	    dip->chain cache.
	 */
retry:
	*errorp = 0;
	parent = hammer2_inode_lock_ex(dip);
	/*parent = hammer2_chain_lookup_init(dip->chain, 0);*/
	xchain = hammer2_chain_lookup(&parent, &key_dummy,
				      lhc, lhc, &cache_index, 0);
	if (xchain) {
		kprintf("X3 chain %p parent %p dip %p dip->chain %p\n",
			xchain, parent, dip, dip->chain);
		hammer2_chain_unlock(xchain);
		xchain = NULL;
		*errorp = ENOSPC;
#if 0
		Debugger("X3");
#endif
	}

	/*
	 * Create entry in common parent directory using the seek position
	 * calculated above.
	 *
	 * We must refactor chain because it might have been shifted into
	 * an indirect chain by the create.
	 */
	if (*errorp == 0) {
		KKASSERT(xchain == NULL);
#if 0
		*errorp = hammer2_chain_create(trans, &parent, &xchain,
					       lhc, 0,
					       HAMMER2_BREF_TYPE_INODE,/* n/a */
					       HAMMER2_INODE_BYTES);   /* n/a */
#endif
		/*XXX this somehow isn't working on chain XXX*/
		/*KKASSERT(xxx)*/
	}

	/*
	 * Cleanup and handle retries.
	 */
	if (*errorp == EAGAIN) {
		hammer2_chain_ref(parent);
		/* hammer2_chain_lookup_done(parent); */
		hammer2_inode_unlock_ex(dip, parent);
		hammer2_chain_wait(parent);
		hammer2_chain_drop(parent);
		goto retry;
	}

	/*
	 * Handle the error case
	 */
	if (*errorp) {
		panic("error2");
		KKASSERT(xchain == NULL);
		hammer2_inode_unlock_ex(dip, parent);
		/*hammer2_chain_lookup_done(parent);*/
		return;
	}

	/*
	 * Use xchain as a placeholder for (lhc).  Duplicate chain to the
	 * same target bref as xchain and then delete xchain.  The duplication
	 * occurs after xchain in flush order even though xchain is deleted
	 * after the duplication. XXX
	 *
	 * WARNING! Duplications (to a different parent) can cause indirect
	 *	    blocks to be inserted, refactor xchain.
	 */
	bref = chain->bref;
	bref.key = lhc;			/* invisible dir entry key */
	bref.keybits = 0;
#if 0
	hammer2_chain_delete(trans, xchain, 0);
#endif
	hammer2_chain_duplicate(trans, &parent, &chain, &bref, 0, 2);
#if 0
	hammer2_chain_refactor(&xchain);
	/*hammer2_chain_delete(trans, xchain, 0);*/
#endif

	hammer2_inode_unlock_ex(dip, parent);
	/*hammer2_chain_lookup_done(parent);*/
#if 0
	hammer2_chain_unlock(xchain);	/* no longer needed */
#endif

	/*
	 * chain is now 'live' again.. adjust the filename.
	 *
	 * Directory entries are inodes but this is a hidden hardlink
	 * target.  The name isn't used but to ease debugging give it
	 * a name after its inode number.
	 */
	hammer2_chain_modify(trans, &chain, 0);
	nipdata = &chain->data->ipdata;
	ksnprintf(nipdata->filename, sizeof(nipdata->filename),
		  "0x%016jx", (intmax_t)nipdata->inum);
	nipdata->name_len = strlen(nipdata->filename);
	nipdata->name_key = lhc;
	nipdata->nlinks += nlinks;

	*chainp = chain;
}

/*
 * Connect the target inode represented by (*chainp) to the media topology
 * at (dip, name, len).
 *
 * If hlink is TRUE this function creates an OBJTYPE_HARDLINK directory
 * entry instead of connecting (*chainp).
 *
 * If hlink is FALSE this function uses chain_duplicate() to make a copy
 * if (*chainp) in the directory entry.  (*chainp) is likely to be deleted
 * by the caller in this case (e.g. rename).
 */
int
hammer2_inode_connect(hammer2_trans_t *trans, int hlink,
		      hammer2_inode_t *dip, hammer2_chain_t **chainp,
		      const uint8_t *name, size_t name_len)
{
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *nchain;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	int cache_index = -1;
	int error;

	ochain = *chainp;

	/*
	 * Since ochain is either disconnected from the topology or represents
	 * a hardlink terminus which is always a parent of or equal to dip,
	 * we should be able to safely lock dip->chain for our setup.
	 *
	 * WARNING! Must use inode_lock_ex() on dip to handle a stale
	 *	    dip->chain cache.
	 */
	parent = hammer2_inode_lock_ex(dip);
	/*parent = hammer2_chain_lookup_init(dip->chain, 0);*/

	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	error = 0;
	while (error == 0) {
		nchain = hammer2_chain_lookup(&parent, &key_dummy,
					      lhc, lhc, &cache_index, 0);
		if (nchain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(nchain);
		nchain = NULL;
		++lhc;
	}

	if (error == 0) {
		if (hlink) {
			/*
			 * Hardlink pointer needed, create totally fresh
			 * directory entry.
			 *
			 * We must refactor ochain because it might have
			 * been shifted into an indirect chain by the
			 * create.
			 */
			KKASSERT(nchain == NULL);
			error = hammer2_chain_create(trans, &parent, &nchain,
						     lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES);
			hammer2_chain_refactor(&ochain);
		} else {
			/*
			 * Reconnect the original chain and rename.  Use
			 * chain_duplicate().  The caller will likely delete
			 * or has already deleted the original chain in
			 * this case.
			 *
			 * NOTE: chain_duplicate() generates a new chain
			 *	 with CHAIN_DELETED cleared (ochain typically
			 *	 has it set from the file unlink).
			 *
			 * WARNING! Can cause held-over chains to require a
			 *	    refactor.  Fortunately we have none (our
			 *	    locked chains are passed into and
			 *	    modified by the call).
			 */
			nchain = ochain;
			ochain = NULL;
			hammer2_chain_duplicate(trans, NULL, &nchain, NULL,
						0, 3);
			error = hammer2_chain_create(trans, &parent, &nchain,
						     lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES);
		}
	}

	/*
	 * Unlock stuff.
	 */
	KKASSERT(error != EAGAIN);
	hammer2_inode_unlock_ex(dip, parent);
	/*hammer2_chain_lookup_done(parent);*/
	parent = NULL;

	/*
	 * nchain should be NULL on error, leave ochain (== *chainp) alone.
	 */
	if (error) {
		KKASSERT(nchain == NULL);
		return (error);
	}

	/*
	 * Directory entries are inodes so if the name has changed we have
	 * to update the inode.
	 *
	 * When creating an OBJTYPE_HARDLINK entry remember to unlock the
	 * chain, the caller will access the hardlink via the actual hardlink
	 * target file and not the hardlink pointer entry, so we must still
	 * return ochain.
	 */
	if (hlink && hammer2_hardlink_enable >= 0) {
		/*
		 * Create the HARDLINK pointer.  oip represents the hardlink
		 * target in this situation.
		 *
		 * We will return ochain (the hardlink target).
		 */
		hammer2_chain_modify(trans, &nchain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		ipdata = &nchain->data->ipdata;
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		ipdata->target_type = ochain->data->ipdata.type;
		ipdata->type = HAMMER2_OBJTYPE_HARDLINK;
		ipdata->inum = ochain->data->ipdata.inum;
		ipdata->nlinks = 1;
		hammer2_chain_unlock(nchain);
		nchain = ochain;
		ochain = NULL;
	} else if (hlink && hammer2_hardlink_enable < 0) {
		/*
		 * Create a snapshot (hardlink fake mode for debugging).
		 * (ochain already flushed above so we can just copy the
		 * bref XXX).
		 *
		 * Since this is a snapshot we return nchain in the fake
		 * hardlink case.
		 */
		hammer2_chain_modify(trans, &nchain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		ipdata = &nchain->data->ipdata;
		*ipdata = ochain->data->ipdata;
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		atomic_clear_int(&nchain->core->flags,
				 HAMMER2_CORE_COUNTEDBREFS);
		kprintf("created fake hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
	} else {
		/*
		 * nchain is a duplicate of ochain at the new location.
		 * We must fixup the name stored in oip.  The bref key
		 * has already been set up.
		 */
		hammer2_chain_modify(trans, &nchain, 0);
		ipdata = &nchain->data->ipdata;

		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		ipdata->nlinks = 1;
	}

	/*
	 * We are replacing ochain with nchain, unlock ochain.  In the
	 * case where ochain is left unchanged the code above sets
	 * nchain to ochain and ochain to NULL, resulting in a NOP here.
	 */
	if (ochain)
		hammer2_chain_unlock(ochain);
	*chainp = nchain;

	return (0);
}

/*
 * Repoint ip->chain to nchain.  Caller must hold the inode exclusively
 * locked.
 *
 * ip->chain is set to nchain.  The prior chain in ip->chain is dropped
 * and nchain is ref'd.
 */
void
hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
		      hammer2_chain_t *nchain)
{
	hammer2_chain_t *ochain;
	hammer2_inode_t *opip;

	/*
	 * Repoint ip->chain if requested.
	 */
	ochain = ip->chain;
	ip->chain = nchain;
	if (nchain)
		hammer2_chain_ref(nchain);
	if (ochain)
		hammer2_chain_drop(ochain);

	/*
	 * Repoint ip->pip if requested (non-NULL pip).
	 */
	if (pip && ip->pip != pip) {
		opip = ip->pip;
		hammer2_inode_ref(pip);
		ip->pip = pip;
		if (opip)
			hammer2_inode_drop(opip);
	}
}

/*
 * Unlink the file from the specified directory inode.  The directory inode
 * does not need to be locked.
 *
 * isdir determines whether a directory/non-directory check should be made.
 * No check is made if isdir is set to -1.
 *
 * NOTE!  The underlying file can still be active with open descriptors
 *	  or if the chain is being manually held (e.g. for rename).
 *
 *	  The caller is responsible for fixing up ip->chain if e.g. a
 *	  rename occurs (see chain_duplicate()).
 */
int
hammer2_unlink_file(hammer2_trans_t *trans, hammer2_inode_t *dip,
		    const uint8_t *name, size_t name_len,
		    int isdir, int *hlinkp)
{
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_chain_t *chain;
	hammer2_chain_t *dparent;
	hammer2_chain_t *dchain;
	hammer2_key_t key_dummy;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	int error;
	int cache_index = -1;
	uint8_t type;

	error = 0;
	ochain = NULL;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Search for the filename in the directory
	 */
	if (hlinkp)
		*hlinkp = 0;
	parent = hammer2_inode_lock_ex(dip);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index, 0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index, 0);
	}
	hammer2_inode_unlock_ex(dip, NULL);	/* retain parent */

	/*
	 * Not found or wrong type (isdir < 0 disables the type check).
	 * If a hardlink pointer, type checks use the hardlink target.
	 */
	if (chain == NULL) {
		error = ENOENT;
		goto done;
	}
	if ((type = chain->data->ipdata.type) == HAMMER2_OBJTYPE_HARDLINK) {
		if (hlinkp)
			*hlinkp = 1;
		type = chain->data->ipdata.target_type;
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
	 * Hardlink must be resolved.  We can't hold parent locked while we
	 * do this or we could deadlock.
	 *
	 * On success chain will be adjusted to point at the hardlink target
	 * and ochain will point to the hardlink pointer in the original
	 * directory.  Otherwise chain remains pointing to the original.
	 */
	if (chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
		hammer2_chain_unlock(parent);
		parent = NULL;
		error = hammer2_hardlink_find(dip, &chain, &ochain);
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
		dparent = hammer2_chain_lookup_init(chain, 0);
		dchain = hammer2_chain_lookup(&dparent, &key_dummy,
					      0, (hammer2_key_t)-1,
					      &cache_index,
					      HAMMER2_LOOKUP_NODATA);
		if (dchain) {
			hammer2_chain_unlock(dchain);
			hammer2_chain_lookup_done(dparent);
			error = ENOTEMPTY;
			goto done;
		}
		hammer2_chain_lookup_done(dparent);
		dparent = NULL;
		/* dchain NULL */
	}

	/*
	 * Ok, we can now unlink the chain.  We always decrement nlinks even
	 * if the entry can be deleted in case someone has the file open and
	 * does an fstat().
	 *
	 * The chain itself will no longer be in the on-media topology but
	 * can still be flushed to the media (e.g. if an open descriptor
	 * remains).  When the last vnode/ip ref goes away the chain will
	 * be marked unmodified, avoiding any further (now unnecesary) I/O.
	 *
	 * A non-NULL ochain indicates a hardlink.
	 */
	if (ochain) {
		/*
		 * Delete the original hardlink pointer.
		 *
		 * NOTE: parent from above is NULL when ochain != NULL
		 *	 so we can reuse it.
		 */
		hammer2_chain_lock(ochain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_delete(trans, ochain, 0);
		hammer2_chain_unlock(ochain);

		/*
		 * Then decrement nlinks on hardlink target, deleting
		 * the target when nlinks drops to 0.
		 */
		hammer2_chain_modify(trans, &chain, 0);
		--chain->data->ipdata.nlinks;
		if (chain->data->ipdata.nlinks == 0)
			hammer2_chain_delete(trans, chain, 0);
	} else {
		/*
		 * Otherwise this was not a hardlink and we can just
		 * remove the entry and decrement nlinks.
		 *
		 * NOTE: *_get() integrates chain's lock into the inode lock.
		 */
		hammer2_chain_modify(trans, &chain, 0);
		ipdata = &chain->data->ipdata;
		--ipdata->nlinks;
		hammer2_chain_delete(trans, chain, 0);
	}

	error = 0;
done:
	if (chain)
		hammer2_chain_unlock(chain);
	if (parent)
		hammer2_chain_lookup_done(parent);
	if (ochain)
		hammer2_chain_drop(ochain);

	return error;
}

/*
 * Given an exclusively locked inode and chain we consolidate its chain
 * for hardlink creation, adding (nlinks) to the file's link count and
 * potentially relocating the inode to a directory common to ip->pip and tdip.
 *
 * Replaces (*chainp) if consolidation occurred, unlocking the old chain
 * and returning a new locked chain.
 *
 * NOTE!  This function will also replace ip->chain.
 */
int
hammer2_hardlink_consolidate(hammer2_trans_t *trans, hammer2_inode_t *ip,
			     hammer2_chain_t **chainp,
			     hammer2_inode_t *tdip, int nlinks)
{
	hammer2_inode_data_t *ipdata;
	hammer2_inode_t *fdip;
	hammer2_inode_t *cdip;
	hammer2_chain_t *chain;
	hammer2_chain_t *nchain;
	int error;

	chain = *chainp;
	if (nlinks == 0 &&			/* no hardlink needed */
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		return (0);
	}
	if (hammer2_hardlink_enable < 0) {	/* fake hardlinks */
		return (0);
	}

	if (hammer2_hardlink_enable == 0) {	/* disallow hardlinks */
		hammer2_chain_unlock(chain);
		*chainp = NULL;
		return (ENOTSUP);
	}

	/*
	 * cdip will be returned with a ref, but not locked.
	 */
	fdip = ip->pip;
	cdip = hammer2_inode_common_parent(fdip, tdip);

	/*
	 * If no change in the hardlink's target directory is required and
	 * this is already a hardlink target, all we need to do is adjust
	 * the link count.
	 *
	 * XXX The common parent is a big wiggly due to duplication from
	 *     renames.  Compare the core (RBTREE) pointer instead of the
	 *     ip's.
	 */
	if (cdip == fdip &&
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		if (nlinks) {
			hammer2_chain_modify(trans, &chain, 0);
			chain->data->ipdata.nlinks += nlinks;
		}
		error = 0;
		goto done;
	}


	/*
	 * chain is the real inode.  If it's visible we have to convert it
	 * to a hardlink pointer.  If it is not visible then it is already
	 * a hardlink target and only needs to be deleted.
	 */
	KKASSERT((chain->flags & HAMMER2_CHAIN_DELETED) == 0);
	KKASSERT(chain->data->ipdata.type != HAMMER2_OBJTYPE_HARDLINK);
	if (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) {
		/*
		 * We are going to duplicate chain later, causing its
		 * media block to be shifted to the duplicate.  Even though
		 * we are delete-duplicating nchain here it might decide not
		 * to reallocate the block.  Set FORCECOW to force it to.
		 */
		nchain = chain;
		hammer2_chain_lock(nchain, HAMMER2_RESOLVE_ALWAYS);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_FORCECOW);
		hammer2_chain_delete_duplicate(trans, &nchain,
					       HAMMER2_DELDUP_RECORE);
		KKASSERT((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0);

		ipdata = &nchain->data->ipdata;
		ipdata->target_type = ipdata->type;
		ipdata->type = HAMMER2_OBJTYPE_HARDLINK;
		ipdata->uflags = 0;
		ipdata->rmajor = 0;
		ipdata->rminor = 0;
		ipdata->ctime = 0;
		ipdata->mtime = 0;
		ipdata->atime = 0;
		ipdata->btime = 0;
		bzero(&ipdata->uid, sizeof(ipdata->uid));
		bzero(&ipdata->gid, sizeof(ipdata->gid));
		ipdata->op_flags = HAMMER2_OPFLAG_DIRECTDATA;
		ipdata->cap_flags = 0;
		ipdata->mode = 0;
		ipdata->size = 0;
		ipdata->nlinks = 1;
		ipdata->iparent = 0;	/* XXX */
		ipdata->pfs_type = 0;
		ipdata->pfs_inum = 0;
		bzero(&ipdata->pfs_clid, sizeof(ipdata->pfs_clid));
		bzero(&ipdata->pfs_fsid, sizeof(ipdata->pfs_fsid));
		ipdata->data_quota = 0;
		ipdata->data_count = 0;
		ipdata->inode_quota = 0;
		ipdata->inode_count = 0;
		ipdata->attr_tid = 0;
		ipdata->dirent_tid = 0;
		bzero(&ipdata->u, sizeof(ipdata->u));
		/* XXX transaction ids */
	} else {
		hammer2_chain_delete(trans, chain, 0);
		nchain = NULL;
	}

	/*
	 * chain represents the hardlink target and is now flagged deleted.
	 * duplicate it to the parent directory and adjust nlinks.
	 *
	 * WARNING! The shiftup() call can cause nchain to be moved into
	 *	    an indirect block, and our nchain will wind up pointing
	 *	    to the older/original version.
	 */
	KKASSERT(chain->flags & HAMMER2_CHAIN_DELETED);
	hammer2_hardlink_shiftup(trans, &chain, cdip, nlinks, &error);

	if (error == 0)
		hammer2_inode_repoint(ip, cdip, chain);

	/*
	 * Unlock the original chain last as the lock blocked races against
	 * the creation of the new hardlink target.
	 */
	if (nchain)
		hammer2_chain_unlock(nchain);

done:
	/*
	 * Cleanup, chain/nchain already dealt with.
	 */
	*chainp = chain;
	hammer2_inode_drop(cdip);

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
 * The caller presents a locked *chainp pointing to a HAMMER2_BREF_TYPE_INODE
 * with an obj_type of HAMMER2_OBJTYPE_HARDLINK.  This routine will gobble
 * the *chainp and return a new locked *chainp representing the file target
 * (the original *chainp will be unlocked).
 *
 * When a match is found the chain representing the original HARDLINK
 * will be returned in *ochainp with a ref, but not locked.
 *
 * When no match is found *chainp is set to NULL and EIO is returned.
 * (*ochainp) will still be set to the original chain with a ref but not
 * locked.
 */
int
hammer2_hardlink_find(hammer2_inode_t *dip, hammer2_chain_t **chainp,
		      hammer2_chain_t **ochainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *parent;
	hammer2_inode_t *ip;
	hammer2_inode_t *pip;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	int cache_index = -1;

	pip = dip;
	hammer2_inode_ref(pip);		/* for loop */
	hammer2_chain_ref(chain);	/* for (*ochainp) */
	*ochainp = chain;

	/*
	 * Locate the hardlink.  pip is referenced and not locked,
	 * ipp.
	 *
	 * chain is reused.
	 */
	lhc = chain->data->ipdata.inum;
	hammer2_chain_unlock(chain);
	chain = NULL;

	while ((ip = pip) != NULL) {
		parent = hammer2_inode_lock_ex(ip);
		hammer2_inode_drop(ip);			/* loop */
		KKASSERT(parent->bref.type == HAMMER2_BREF_TYPE_INODE);
		chain = hammer2_chain_lookup(&parent, &key_dummy,
					     lhc, lhc, &cache_index, 0);
		hammer2_chain_lookup_done(parent);	/* discard parent */
		if (chain)
			break;
		pip = ip->pip;		/* safe, ip held locked */
		if (pip)
			hammer2_inode_ref(pip);		/* loop */
		hammer2_inode_unlock_ex(ip, NULL);
	}

	/*
	 * chain is locked, ip is locked.  Unlock ip, return the locked
	 * chain.  *ipp is already set w/a ref count and not locked.
	 *
	 * (parent is already unlocked).
	 */
	if (ip)
		hammer2_inode_unlock_ex(ip, NULL);
	*chainp = chain;
	if (chain) {
		KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);
		/* already locked */
		return (0);
	} else {
		return (EIO);
	}
}

/*
 * Find the directory common to both fdip and tdip, hold and return
 * its inode.
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
 * Synchronize the inode's frontend state with the chain state prior
 * to any explicit flush of the inode or any strategy write call.
 *
 * Called with a locked inode.
 */
void
hammer2_inode_fsync(hammer2_trans_t *trans, hammer2_inode_t *ip, 
		    hammer2_chain_t **chainp)
{
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lbase;
	hammer2_key_t key_next;
	int cache_index;

	ipdata = &ip->chain->data->ipdata;

	if (ip->flags & HAMMER2_INODE_MTIME) {
		ipdata = hammer2_chain_modify_ip(trans, ip, chainp, 0);
		atomic_clear_int(&ip->flags, HAMMER2_INODE_MTIME);
		ipdata->mtime = ip->mtime;
	}
	if ((ip->flags & HAMMER2_INODE_RESIZED) && ip->size < ipdata->size) {
		ipdata = hammer2_chain_modify_ip(trans, ip, chainp, 0);
		ipdata->size = ip->size;
		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED);

		/*
		 * We must delete any chains beyond the EOF.  The chain
		 * straddling the EOF will be pending in the bioq.
		 */
		lbase = (ipdata->size + HAMMER2_PBUFMASK64) &
			~HAMMER2_PBUFMASK64;
		parent = hammer2_chain_lookup_init(ip->chain, 0);
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lbase, (hammer2_key_t)-1,
					     &cache_index,
					     HAMMER2_LOOKUP_NODATA);
		while (chain) {
			/*
			 * Degenerate embedded case, nothing to loop on
			 */
			if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
				hammer2_chain_unlock(chain);
				break;
			}
			if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
				hammer2_chain_delete(trans, chain, 0);
			}
			chain = hammer2_chain_next(&parent, chain, &key_next,
						   key_next, (hammer2_key_t)-1,
						   &cache_index,
						   HAMMER2_LOOKUP_NODATA);
		}
		hammer2_chain_lookup_done(parent);
	} else
	if ((ip->flags & HAMMER2_INODE_RESIZED) && ip->size > ipdata->size) {
		ipdata = hammer2_chain_modify_ip(trans, ip, chainp, 0);
		ipdata->size = ip->size;
		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED);

		/*
		 * When resizing larger we may not have any direct-data
		 * available.
		 */
		if ((ipdata->op_flags & HAMMER2_OPFLAG_DIRECTDATA) &&
		    ip->size > HAMMER2_EMBEDDED_BYTES) {
			ipdata->op_flags &= ~HAMMER2_OPFLAG_DIRECTDATA;
			bzero(&ipdata->u.blockset, sizeof(ipdata->u.blockset));
		}
	}
}
