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
	hammer2_mount_t *hmp;
	hammer2_inode_t *pip;
	hammer2_chain_t *chain;
	u_int refs;

	while (ip) {
		refs = ip->refs;
		cpu_ccfence();
		if (refs == 1) {
			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				KKASSERT(ip->topo_cst.count == 0);

				hmp = ip->hmp;
				ip->hmp = NULL;
				pip = ip->pip;
				ip->pip = NULL;
				chain = ip->chain;
				ip->chain = NULL;
				if (chain)
					hammer2_chain_drop(chain);

				/*
				 * We have to drop pip (if non-NULL) to
				 * dispose of our implied reference from
				 * ip->pip.  We can simply loop on it.
				 */
				kfree(ip, hmp->minode);
				ip = pip;
				/* continue with pip (can be NULL) */
			}
		} else {
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
			vhold_interlocked(vp);
			ostate = hammer2_inode_lock_temp_release(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				hammer2_inode_lock_restore(ip, ostate);
				continue;
			}
			hammer2_inode_lock_restore(ip, ostate);
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
			hammer2_inode_lock_restore(ip, ostate);
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
		/* XXX FIFO */
		default:
			panic("hammer2: unhandled objtype %d", ipdata->type);
			break;
		}

		if (ip == pmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_inode_ref(ip);		/* vp association */
		hammer2_inode_lock_restore(ip, ostate);
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	if (hammer2_debug & 0x0002) {
		kprintf("igetv vp %p refs %d aux %d\n",
			vp, vp->v_sysref.refcnt, vp->v_auxrefs);
	}
	return (vp);
}

/*
 * The passed-in chain must be locked and the returned inode will also be
 * locked.  A ref is added to both the chain and the inode.
 *
 * The hammer2_inode structure regulates the interface between the high level
 * kernel VNOPS API and the filesystem backend (the chains).
 *
 * NOTE!     This routine allocates the hammer2_inode structure
 *	     unconditionally, and thus there might be several which
 *	     are associated with the same chain.  Particularly for hardlinks
 *	     but this can also happen temporarily for normal files and
 *	     directories.
 *
 * WARNING!  This routine sucks up the chain's lock (makes it part of the
 *	     inode lock from the point of view of the inode lock API),
 *	     so callers need to be careful.
 *
 * WARNING!  The mount code is allowed to pass dip == NULL for iroot and
 *	     is allowed to pass pmp == NULL and dip == NULL for sroot.
 */
hammer2_inode_t *
hammer2_inode_get(hammer2_mount_t *hmp, hammer2_pfsmount_t *pmp,
		  hammer2_inode_t *dip, hammer2_chain_t *chain)
{
	hammer2_inode_t *nip;

	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);

	nip = kmalloc(sizeof(*nip), hmp->minode, M_WAITOK | M_ZERO);

	nip->chain = chain;
	hammer2_chain_ref(chain);		/* nip->chain */
	nip->pip = dip;				/* can be NULL */
	if (dip)
		hammer2_inode_ref(dip);	/* ref dip for nip->pip */

	nip->pmp = pmp;
	nip->hmp = hmp;

	/*
	 * ref and lock on nip gives it state compatible to after a
	 * hammer2_inode_lock_ex() call.
	 */
	nip->refs = 1;
	ccms_cst_init(&nip->topo_cst, &nip->chain);
	ccms_thread_lock(&nip->topo_cst, CCMS_STATE_EXCLUSIVE);
	/* combination of thread lock and chain lock == inode lock */

	return (nip);
}

/*
 * Put away an inode, unlocking it and disconnecting it from its chain.
 *
 * The inode must be exclusively locked on call and non-recursed, with
 * at least 2 refs (one belonging to the exclusive lock, and one additional
 * ref belonging to the caller).
 *
 * Upon return the inode typically has one ref remaining which the caller
 * drops.
 */
void
hammer2_inode_put(hammer2_inode_t *ip)
{
	hammer2_inode_t *pip;
	hammer2_chain_t *chain;

	/*
	 * Disconnect and unlock chain
	 */
	KKASSERT(ip->refs >= 2);
	KKASSERT(ip->topo_cst.count == -1);	/* one excl lock allowed */
	if ((chain = ip->chain) != NULL) {
		ip->chain = NULL;
		hammer2_inode_unlock_ex(ip);
		hammer2_chain_unlock(chain);	/* because ip->chain now NULL */
		hammer2_chain_drop(chain);	/* from *_get() */
	}

	/*
	 * Disconnect pip
	 */
	if ((pip = ip->pip) != NULL) {
		ip->pip = NULL;
		hammer2_inode_drop(pip);
	}
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
		     int *errorp)
{
	hammer2_inode_data_t *dipdata;
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_inode_t *nip;
	hammer2_key_t lhc;
	int error;
	uid_t xuid;
	uuid_t dip_uid;
	uuid_t dip_gid;
	uint32_t dip_mode;

	hmp = dip->hmp;
	lhc = hammer2_dirhash(name, name_len);
	*errorp = 0;

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
retry:
	hammer2_inode_lock_ex(dip);
	dipdata = &dip->chain->data->ipdata;
	dip_uid = dipdata->uid;
	dip_gid = dipdata->gid;
	dip_mode = dipdata->mode;

	parent = hammer2_chain_lookup_init(dip->chain, 0);
	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(&parent, lhc, lhc, 0);
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
		error = hammer2_chain_create(trans, parent, &chain,
					     lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES);
	}

	/*
	 * Cleanup and handle retries.
	 */
	if (error == EAGAIN) {
		hammer2_chain_ref(parent);
		hammer2_chain_lookup_done(parent);
		hammer2_inode_unlock_ex(dip);
		hammer2_chain_wait(parent);
		hammer2_chain_drop(parent);
		goto retry;
	}
	hammer2_chain_lookup_done(parent);
	hammer2_inode_unlock_ex(dip);

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
	 */
	nip = hammer2_inode_get(dip->hmp, dip->pmp, dip, chain);
	nipdata = &chain->data->ipdata;

	if (vap) {
		KKASSERT(trans->inodes_created == 0);
		nipdata->type = hammer2_get_obj_type(vap->va_type);
		nipdata->inum = trans->sync_tid;
		++trans->inodes_created;
	} else {
		nipdata->type = HAMMER2_OBJTYPE_DIRECTORY;
		nipdata->inum = 1;
	}
	nipdata->version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nipdata->ctime);
	nipdata->mtime = nipdata->ctime;
	if (vap)
		nipdata->mode = vap->va_mode;
	nipdata->nlinks = 1;
	if (vap) {
		if (dip) {
			xuid = hammer2_to_unix_xid(&dip_uid);
			xuid = vop_helper_create_uid(dip->pmp->mp,
						     dip_mode,
						     xuid,
						     cred,
						     &vap->va_mode);
		} else {
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

	return (nip);
}

/*
 * Create a duplicate of (ochain) in the specified target directory (dip).
 * ochain must represent an inode.  The new chain is returned locked and
 * referenced and its filename is changed to a representation of the
 * inode number "0xBLAH", as a hidden directory entry.
 */
hammer2_chain_t *
hammer2_inode_duplicate(hammer2_trans_t *trans, hammer2_chain_t *ochain,
			hammer2_inode_t *dip, int *errorp)
{
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *tmpchain;
	hammer2_key_t lhc;
	hammer2_blockref_t bref;

	*errorp = 0;
	hmp = dip->hmp;
	lhc = ochain->data->ipdata.inum;
	KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  lhc represents the inode number so there is
	 * no collision iteration.
	 *
	 * There should be no key collisions with invisible inode keys.
	 */
retry:
	parent = hammer2_chain_lookup_init(dip->chain, 0);
	chain = hammer2_chain_lookup(&parent, lhc, lhc, 0);
	if (chain) {
		hammer2_chain_unlock(chain);
		chain = NULL;
		*errorp = ENOSPC;
	}

	/*
	 * Create entry in common parent directory.
	 */
	if (*errorp == 0) {
		KKASSERT(chain == NULL);
		*errorp = hammer2_chain_create(trans, parent, &chain,
					       lhc, 0,
					       HAMMER2_BREF_TYPE_INODE,/* n/a */
					       HAMMER2_INODE_BYTES);   /* n/a */
	}

	/*
	 * Cleanup and handle retries.
	 */
	if (*errorp == EAGAIN) {
		hammer2_chain_ref(parent);
		hammer2_chain_lookup_done(parent);
		hammer2_chain_wait(parent);
		hammer2_chain_drop(parent);
		goto retry;
	}

	/*
	 * Handle the error case
	 */
	if (*errorp) {
		KKASSERT(chain == NULL);
		hammer2_chain_lookup_done(parent);
		return (NULL);
	}

	/*
	 * Use chain as a placeholder for our duplication.
	 *
	 * Gain a second lock on ochain for the duplication function to
	 * unlock, maintain the original lock across the calls.
	 *
	 * This is a bit messy.
	 */
	hammer2_chain_delete(trans, parent, chain);
	hammer2_chain_lock(ochain, HAMMER2_RESOLVE_ALWAYS);
	tmpchain = ochain;
	bref = tmpchain->bref;
	bref.key = lhc;		/* key for create placeholder must match */
	bref.keybits = 0;
	hammer2_chain_duplicate(trans, parent, chain->index, &tmpchain, &bref);
	hammer2_chain_lookup_done(parent);

	hammer2_chain_unlock(chain);
	chain = tmpchain;
	tmpchain = NULL;	/* safety */

	/*
	 * Directory entries are inodes but this is a hidden hardlink
	 * target.  The name isn't used but to ease debugging give it
	 * a name after its inode number.
	 */
	hammer2_chain_modify(trans, chain, 0);
	nipdata = &chain->data->ipdata;
	ksnprintf(nipdata->filename, sizeof(nipdata->filename),
		  "0x%016jx", (intmax_t)nipdata->inum);
	nipdata->name_len = strlen(nipdata->filename);
	nipdata->name_key = lhc;

	return (chain);
}

/*
 * Connect the target inode to the media topology at (dip, name, len).
 * This function creates a directory entry and replace (*chainp).
 *
 * The caller usually holds the related inode exclusive locked through this
 * call and is also responsible for replacing ip->chain after we return.
 *
 * If (*chainp) was marked DELETED then it represents a terminus inode
 * with no other nlinks, we can simply duplicate the chain (in-memory
 * chain structures cannot be moved within the in-memory topology, only
 * duplicated, but the duplicate uses the same bref).
 *
 * if (*chainp) is not marked DELETED then it represents a hardlink
 * terminus which still has a non-zero nlink count.  Instead of duplicating
 * it (which would be like a snapshot), we need to create a
 * OBJTYPE_HARDLINK directory entry which references (*chainp)'s inode
 * number and bump (*chainp)'s nlinks.  In this situation we return
 * the terminus as *chainp.
 *
 * (*chainp) is adjusted if necessary and returned locked.  If different,
 * the original (*chainp) is unlocked.  Note that the (*chainp) that is
 * returned is always the hardlink terminus (the actual inode), which
 * might reside in some parent directory.  It will not be the
 * OBJTYPE_HARDLINK pointer.
 *
 * WARNING!  The caller is likely holding ip/ip->chain locked exclusively.
 *	     Replacing ip->chain here would create confusion so we leave
 *	     it to the caller to do that.
 *
 *	     (The caller is expected to hold the related inode exclusively)
 */
int
hammer2_inode_connect(hammer2_trans_t *trans, hammer2_inode_t *dip,
		      hammer2_chain_t **chainp,
		      const uint8_t *name, size_t name_len)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *nchain;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_key_t lhc;
	int error;
	int hlink;

	hmp = dip->hmp;

	ochain = *chainp;

	/*
	 * Since ochain is either disconnected from the topology or represents
	 * a hardlink terminus which is always a parent of or equal to dip,
	 * we should be able to safely lock dip->chain for our setup.
	 */
	parent = hammer2_chain_lookup_init(dip->chain, 0);

	lhc = hammer2_dirhash(name, name_len);
	hlink = ((ochain->flags & HAMMER2_CHAIN_DELETED) == 0);
	kprintf("reconnect hlink=%d name=%*.*s\n",
		hlink, (int)name_len, (int)name_len, name);

	/*
	 * In fake mode flush oip so we can just snapshot it downbelow.
	 * A flush is not otherwise needed as the new chain inherits
	 * all active children of the old chain (they will share the same
	 * chain_core).
	 */
	if (hlink && hammer2_hardlink_enable < 0)
		hammer2_chain_flush(trans, ochain);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	error = 0;
	while (error == 0) {
		nchain = hammer2_chain_lookup(&parent, lhc, lhc, 0);
		if (nchain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(nchain);
		nchain = NULL;
		++lhc;
	}

	/*
	 * Passing a non-NULL chain to hammer2_chain_create() reconnects the
	 * existing chain instead of creating a new one.  The chain's bref
	 * will be properly updated.
	 */
	if (error == 0) {
		if (hlink) {
			/*
			 * Hardlink pointer needed, create totally fresh
			 * directory entry.
			 */
			KKASSERT(nchain == NULL);
			error = hammer2_chain_create(trans, parent, &nchain,
						     lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES);
		} else {
			/*
			 * Original inode reconnected, duplicate as a
			 * new directory entry, leave unconnected and
			 * then call chain_create() to connect it.
			 */
			nchain = ochain;
			ochain = NULL;
			hammer2_chain_duplicate(trans, NULL, -1, &nchain, NULL);
			error = hammer2_chain_create(trans, parent, &nchain,
						     lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES);
		}
	}

	/*
	 * Unlock stuff.
	 */
	KKASSERT(error != EAGAIN);
	hammer2_chain_lookup_done(parent);
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
		hammer2_chain_modify(trans, nchain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		ipdata = &nchain->data->ipdata;
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		ipdata->target_type = ochain->data->ipdata.type;
		ipdata->type = HAMMER2_OBJTYPE_HARDLINK;
		ipdata->inum = ochain->data->ipdata.inum;
		ipdata->nlinks = 1;
		kprintf("created hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
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
		hammer2_chain_modify(trans, nchain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		ipdata = &nchain->data->ipdata;
		*ipdata = ochain->data->ipdata;
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		kprintf("created fake hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
	} else {
		/*
		 * We are reconnecting a previously DELETED node in a new
		 * location.  nchain is a duplication of the deleted node.
		 *
		 * We must fixup the name stored in oip.
		 */
		hammer2_chain_modify(trans, nchain, 0);
		ipdata = &nchain->data->ipdata;

		if (ipdata->name_len != name_len ||
		    bcmp(ipdata->filename, name, name_len) != 0) {
			KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
			bcopy(name, ipdata->filename, name_len);
			ipdata->name_key = lhc;
			ipdata->name_len = name_len;
		}
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
 * Caller must hold exactly ONE exclusive lock on the inode.  *nchainp
 * must be exclusive locked (its own exclusive lock even if it is the
 * same as ip->chain).
 *
 * This function replaces ip->chain.  The exclusive lock on the passed
 * nchain is inherited by the inode and the caller becomes responsible
 * for unlocking it when the caller unlocks the inode.
 *
 * ochain was locked by the caller indirectly via the inode lock.  Since
 * ip->chain is being repointed, we become responsible for cleaning up
 * that lock.
 *
 * Return *nchainp = NULL as a safety.
 */
void
hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
		      hammer2_chain_t *nchain)
{
	hammer2_chain_t *ochain;
	hammer2_inode_t *opip;

	/*
	 * ip->chain points to the hardlink target, not the hardlink psuedo
	 * inode.  Do not repoint nchain to the pseudo-node.
	 */
	if (nchain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK)
		return;

	/*
	 * Repoint ip->chain if necessary.
	 *
	 * (Inode must be locked exclusively by parent)
	 */
	ochain = ip->chain;
	if (ochain != nchain) {
		hammer2_chain_ref(nchain);		/* for ip->chain */
		ip->chain = nchain;
		if (ochain) {
			hammer2_chain_unlock(ochain);
			hammer2_chain_drop(ochain);	/* for ip->chain */
		}
		/* replace locked chain in ip (additional lock) */
		hammer2_chain_lock(nchain, HAMMER2_RESOLVE_ALWAYS);
	}
	if (ip->pip != pip) {
		opip = ip->pip;
		if (pip)
			hammer2_inode_ref(pip);
		ip->pip = pip;
		if (opip)
			hammer2_inode_drop(opip);
	}
}

/*
 * Unlink the file from the specified directory inode.  The directory inode
 * does not need to be locked.  The caller should pass a non-NULL (ip)
 * representing the object being removed only if the related vnode is
 * potentially inactive (not referenced in the caller's active path),
 * so we can vref/vrele it to trigger the VOP_INACTIVE path and properly
 * recycle it.
 *
 * isdir determines whether a directory/non-directory check should be made.
 * No check is made if isdir is set to -1.
 *
 * NOTE!  This function does not prevent the underlying file from still
 *	  being used if it has other refs (such as from an inode, or if it's
 *	  chain is manually held).  However, the caller is responsible for
 *	  fixing up ip->chain if e.g. a rename occurs (see chain_duplicate()).
 */
int
hammer2_unlink_file(hammer2_trans_t *trans, hammer2_inode_t *dip,
		    const uint8_t *name, size_t name_len, int isdir)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_chain_t *chain;
	hammer2_chain_t *dparent;
	hammer2_chain_t *dchain;
	hammer2_key_t lhc;
	int error;
	int parent_ref;
	uint8_t type;

	parent_ref = 0;
	error = 0;
	ochain = NULL;
	hmp = dip->hmp;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Search for the filename in the directory
	 */
	hammer2_inode_lock_ex(dip);

	parent = hammer2_chain_lookup_init(dip->chain, 0);
	chain = hammer2_chain_lookup(&parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   0);
	}
	hammer2_inode_unlock_ex(dip);	/* retain parent */

	/*
	 * Not found or wrong type (isdir < 0 disables the type check).
	 * If a hardlink pointer, type checks use the hardlink target.
	 */
	if (chain == NULL) {
		error = ENOENT;
		goto done;
	}
	if ((type = chain->data->ipdata.type) == HAMMER2_OBJTYPE_HARDLINK)
		type = chain->data->ipdata.target_type;

	if (type == HAMMER2_OBJTYPE_DIRECTORY && isdir == 0) {
		error = ENOTDIR;
		goto done;
	}
	if (type != HAMMER2_OBJTYPE_DIRECTORY && isdir == 1) {
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
		KKASSERT(parent_ref == 0);
		hammer2_chain_unlock(parent);
		parent = NULL;
		error = hammer2_hardlink_find(dip, &chain, &ochain);
	}

	/*
	 * If this is a directory the directory must be empty.  However, if
	 * isdir < 0 we are doing a rename and the directory does not have
	 * to be empty.
	 *
	 * NOTE: We check the full key range here which covers both visible
	 *	 and invisible entries.  Theoretically there should be no
	 *	 invisible (hardlink target) entries if there are no visible
	 *	 entries.
	 */
	if (type == HAMMER2_OBJTYPE_DIRECTORY && isdir >= 0) {
		dparent = hammer2_chain_lookup_init(chain, 0);
		dchain = hammer2_chain_lookup(&dparent,
					      0, (hammer2_key_t)-1,
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
		parent_ref = 1;
		for (;;) {
			parent = ochain->parent;
			hammer2_chain_ref(parent);
			hammer2_chain_unlock(ochain);
			hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(ochain, HAMMER2_RESOLVE_ALWAYS);
			if (ochain->parent == parent)
				break;
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
		}

		hammer2_chain_delete(trans, parent, ochain);
		hammer2_chain_unlock(ochain);
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
		parent = NULL;

		/*
		 * Then decrement nlinks on hardlink target, deleting
		 * the target when nlinks drops to 0.
		 */
		if (chain->data->ipdata.nlinks == 1) {
			dparent = chain->parent;
			hammer2_chain_ref(chain);
			hammer2_chain_unlock(chain);
			hammer2_chain_lock(dparent, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_drop(chain);
			hammer2_chain_modify(trans, chain, 0);
			--chain->data->ipdata.nlinks;
			hammer2_chain_delete(trans, dparent, chain);
			hammer2_chain_unlock(dparent);
		} else {
			hammer2_chain_modify(trans, chain, 0);
			--chain->data->ipdata.nlinks;
		}
	} else {
		/*
		 * Otherwise this was not a hardlink and we can just
		 * remove the entry and decrement nlinks.
		 *
		 * NOTE: *_get() integrates chain's lock into the inode lock.
		 */
		ipdata = &chain->data->ipdata;
		hammer2_chain_modify(trans, chain, 0);
		--ipdata->nlinks;
		hammer2_chain_delete(trans, parent, chain);
	}

	error = 0;
done:
	if (chain)
		hammer2_chain_unlock(chain);
	if (parent) {
		hammer2_chain_lookup_done(parent);
		if (parent_ref)
			hammer2_chain_drop(parent);
	}
	if (ochain)
		hammer2_chain_drop(ochain);

	return error;
}

/*
 * Calculate the allocation size for the file fragment straddling EOF
 */
int
hammer2_inode_calc_alloc(hammer2_key_t filesize)
{
	int frag = (int)filesize & HAMMER2_PBUFMASK;
	int radix;

	if (frag == 0)
		return(0);
	for (radix = HAMMER2_MINALLOCRADIX; frag > (1 << radix); ++radix)
		;
	return (radix);
}

/*
 * Given an exclusively locked inode we consolidate its chain for hardlink
 * creation, adding (nlinks) to the file's link count and potentially
 * relocating the inode to a directory common to ip->pip and tdip.
 *
 * Returns a locked chain in (*chainp) (the chain's lock is in addition to
 * any lock it might already have due to the inode being locked).  *chainp
 * is set unconditionally and its previous contents can be garbage.
 *
 * The caller is responsible for replacing ip->chain, not us.  For certain
 * operations such as renames the caller may do additional manipulation
 * of the chain before replacing ip->chain.
 */
int
hammer2_hardlink_consolidate(hammer2_trans_t *trans, hammer2_inode_t *ip,
			     hammer2_chain_t **chainp,
			     hammer2_inode_t *tdip, int nlinks)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_inode_t *fdip;
	hammer2_inode_t *cdip;
	hammer2_chain_t *chain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *parent;
	int error;

	/*
	 * Extra lock on chain so it can be returned locked.
	 */
	hmp = tdip->hmp;

	chain = ip->chain;
	error = hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
	KKASSERT(error == 0);

	if (nlinks == 0 &&			/* no hardlink needed */
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		*chainp = chain;
		return (0);
	}
	if (hammer2_hardlink_enable < 0) {	/* fake hardlinks */
		*chainp = chain;
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
	 */
	if (cdip == fdip &&
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		if (nlinks) {
			hammer2_chain_modify(trans, chain, 0);
			chain->data->ipdata.nlinks += nlinks;
		}
		*chainp = chain;
		error = 0;
		goto done;
	}

	/*
	 * We either have to move an existing hardlink target or we have
	 * to create a fresh hardlink target.
	 *
	 * Hardlink targets are hidden inodes in a parent directory common
	 * to all directory entries referencing the hardlink.
	 */
	nchain = hammer2_inode_duplicate(trans, chain, cdip, &error);

	if (error == 0) {
		/*
		 * Bump nlinks on duplicated hidden inode, repoint
		 * ip->chain.
		 */
		hammer2_chain_modify(trans, nchain, 0);
		nchain->data->ipdata.nlinks += nlinks;
		hammer2_inode_repoint(ip, cdip, nchain);

		/*
		 * If the old chain is not a hardlink target then replace
		 * it with a OBJTYPE_HARDLINK pointer.
		 *
		 * If the old chain IS a hardlink target then delete it.
		 */
		if (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) {
			hammer2_chain_modify(trans, chain, 0);
			ipdata = &chain->data->ipdata;
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
			kprintf("DELETE INVISIBLE\n");
			for (;;) {
				parent = chain->parent;
				hammer2_chain_ref(parent);
				hammer2_chain_ref(chain);
				hammer2_chain_unlock(chain);
				hammer2_chain_lock(parent,
						   HAMMER2_RESOLVE_ALWAYS);
				hammer2_chain_lock(chain,
						   HAMMER2_RESOLVE_ALWAYS);
				hammer2_chain_drop(chain);
				if (chain->parent == parent)
					break;
				hammer2_chain_unlock(parent);
				hammer2_chain_drop(parent);
			}
			hammer2_chain_delete(trans, parent, chain);
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
		}

		/*
		 * Return the new chain.
		 */
		hammer2_chain_unlock(chain);
		*chainp = nchain;
	} else {
		/*
		 * Return an error
		 */
		hammer2_chain_unlock(chain);
		*chainp = NULL;
	}

	/*
	 * Cleanup, chain/nchain already dealt with.
	 */
done:
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
	hammer2_key_t lhc;

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
		hammer2_inode_lock_ex(ip);
		parent = hammer2_chain_lookup_init(ip->chain, 0);
		hammer2_inode_drop(ip);			/* loop */
		KKASSERT(parent->bref.type == HAMMER2_BREF_TYPE_INODE);
		chain = hammer2_chain_lookup(&parent, lhc, lhc, 0);
		hammer2_chain_lookup_done(parent);
		if (chain)
			break;
		pip = ip->pip;		/* safe, ip held locked */
		if (pip)
			hammer2_inode_ref(pip);		/* loop */
		hammer2_inode_unlock_ex(ip);
	}

	/*
	 * chain is locked, ip is locked.  Unlock ip, return the locked
	 * chain.  *ipp is already set w/a ref count and not locked.
	 *
	 * (parent is already unlocked).
	 */
	if (ip)
		hammer2_inode_unlock_ex(ip);
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
		}
	}
	panic("hammer2_inode_common_parent: no common parent %p %p\n",
	      fdip, tdip);
	/* NOT REACHED */
	return(NULL);
}
