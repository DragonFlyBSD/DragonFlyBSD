/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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

	for (;;) {
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
					hammer2_chain_drop(hmp, chain);

				/*
				 * We have to drop pip (if non-NULL) to
				 * dispose of our implied reference from
				 * ip->pip.  We can simply loop on it.
				 */
				kfree(ip, hmp->minode);
				if (pip == NULL)
					break;
				ip = pip;
				/* continue */
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
	hammer2_chain_ref(hmp, chain);		/* nip->chain */
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
 * Put away an inode, disconnecting it from its chain.  The inode must be
 * exclusively locked.
 *
 * The inode will be unlocked by this function.  Note however that any related
 * chain returned by the hammer2_inode_lock_*() call will NOT be unlocked
 * by this function.  The related chain is dropped to undo the ref that
 * hammer2_inode_get() put on it.
 *
 * passed_chain is unlocked normally and does not have to be directly
 * associated with (ip).  This is simply so the API works the same as
 * the hammer2_inode_unlock_ex() API.  NULL is ok.
 */
void
hammer2_inode_put(hammer2_inode_t *ip, hammer2_chain_t *passed_chain)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_inode_t *pip;
	hammer2_chain_t *chain;

	/*
	 * Disconnect chain
	 */
	if ((chain = ip->chain) != NULL) {
		ip->chain = NULL;
		hammer2_chain_drop(hmp, chain);		/* from *_get() */
	}
	KKASSERT(ip->topo_cst.count == -1);	/* one excl lock allowed */

	/*
	 * Disconnect pip
	 */
	if ((pip = ip->pip) != NULL) {
		ip->pip = NULL;
		hammer2_inode_drop(pip);
	}

	/*
	 * clean up the ip, we use an inode_unlock_ex-compatible API.
	 */
	hammer2_inode_unlock_ex(ip, passed_chain);
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
int
hammer2_inode_create(hammer2_inode_t *dip,
		     struct vattr *vap, struct ucred *cred,
		     const uint8_t *name, size_t name_len,
		     hammer2_inode_t **nipp, hammer2_chain_t **nchainp)
{
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

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
retry:
	parent = hammer2_inode_lock_ex(dip);

	dip_uid = parent->data->ipdata.uid;
	dip_gid = parent->data->ipdata.gid;
	dip_mode = parent->data->ipdata.mode;

	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		if (chain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_VISIBLE) == 0)
			error = ENOSPC;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(hmp, chain);
		chain = NULL;
		++lhc;
	}
	if (error == 0) {
		chain = hammer2_chain_create(hmp, parent, NULL, lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES,
					     &error);
	}

	hammer2_inode_unlock_ex(dip, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
			goto retry;
		}
		*nipp = NULL;
		*nchainp = NULL;
		return (error);
	}

	/*
	 * Set up the new inode.
	 *
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 */
	nip = hammer2_inode_get(dip->hmp, dip->pmp, dip, chain);
	*nipp = nip;
	*nchainp = chain;
	nipdata = &chain->data->ipdata;

	hammer2_voldata_lock(hmp);
	if (vap) {
		nipdata->type = hammer2_get_obj_type(vap->va_type);
		nipdata->inum = hmp->voldata.alloc_tid++;
		/* XXX modify/lock */
	} else {
		nipdata->type = HAMMER2_OBJTYPE_DIRECTORY;
		nipdata->inum = 1;
	}
	hammer2_voldata_unlock(hmp);
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

	return (0);
}

/*
 * Create a duplicate of the inode (chain) in the specified target directory
 * (dip), return the duplicated chain in *nchainp (locked).  chain is locked
 * on call and remains locked on return.
 *
 * If name is NULL the inode is duplicated as a hidden directory entry.
 *
 * XXX name needs to be NULL for now.
 */
int
hammer2_inode_duplicate(hammer2_inode_t *dip,
			hammer2_chain_t *ochain, hammer2_chain_t **nchainp)
{
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lhc;
	int error = 0;

	hmp = dip->hmp;
	lhc = ochain->data->ipdata.inum;
	*nchainp = NULL;
	KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.
	 *
	 * There should be no key collisions with invisible inode keys.
	 */
retry:
	parent = dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
	chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
	if (chain) {
		hammer2_chain_unlock(hmp, chain);
		chain = NULL;
		error = ENOSPC;
	}

	/*
	 * Create entry in common parent directory.
	 */
	if (error == 0) {
		chain = hammer2_chain_create(hmp, parent, NULL, lhc, 0,
					     HAMMER2_BREF_TYPE_INODE, /* n/a */
					     HAMMER2_INODE_BYTES,     /* n/a */
					     &error);
	}

	/*
	 * Clean up, but we need to retain a ref on parent so we can wait
	 * on it for certain errors.
	 */
	if (error == EAGAIN)
		hammer2_chain_ref(hmp, parent);
	hammer2_chain_unlock(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
			hammer2_chain_drop(hmp, parent);
			goto retry;
		}
		return (error);
	}

	/*
	 * XXX This is currently a horrible hack.  Well, if we wanted to
	 *     duplicate a file, i.e. as in a snapshot, we definitely
	 *     would have to flush it first.
	 *
	 *     For hardlink target generation we can theoretically move any
	 *     active chain structures without flushing, but that gets really
	 *     iffy for code which follows chain->parent and ip->pip links.
	 *
	 * XXX only works with files.  Duplicating a directory hierarchy
	 *     requires a flush but doesn't deal with races post-flush.
	 *     Well, it would work I guess, but you might catch some files
	 *     mid-operation.
	 *
	 * We cannot leave ochain with any in-memory chains because (for a
	 * hardlink), ochain will become a OBJTYPE_HARDLINK which is just a
	 * pointer to the real hardlink's inum and can't have any sub-chains.
	 * XXX might be 0-ref chains left.
	 */
	hammer2_chain_flush(hmp, ochain, 0);
	/*KKASSERT(RB_EMPTY(&ochain.rbhead));*/

	hammer2_chain_modify(hmp, chain, 0);
	nipdata = &chain->data->ipdata;
	*nipdata = ochain->data->ipdata;

	/*
	 * Directory entries are inodes but this is a hidden hardlink
	 * target.  The name isn't used but to ease debugging give it
	 * a name after its inode number.
	 */
	ksnprintf(nipdata->filename, sizeof(nipdata->filename),
		  "0x%016jx", (intmax_t)nipdata->inum);
	nipdata->name_len = strlen(nipdata->filename);
	nipdata->name_key = lhc;

	*nchainp = chain;

	return (0);
}

/*
 * Connect *chainp to the media topology represented by (dip, name, len).
 * A directory entry is created which points to *chainp.  *chainp is then
 * unlocked and set to NULL.
 *
 * If *chainp is not currently connected we simply connect it up.
 *
 * If *chainp is already connected we create a OBJTYPE_HARDLINK entry which
 * points to chain's inode number.  *chainp is expected to be the terminus of
 * the hardlink sitting as a hidden file in a common parent directory
 * in this situation.
 *
 * The caller always wants to reference the hardlink terminus, not the
 * hardlink pointer that we might be creating, so we do NOT replace
 * *chainp here, we simply unlock and NULL it out.
 */
int
hammer2_inode_connect(hammer2_inode_t *dip, hammer2_chain_t **chainp,
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
	*chainp = NULL;

	/*
	 * Since ochain is either disconnected from the topology or represents
	 * a hardlink terminus which is always a parent of or equal to dip,
	 * we should be able to safely lock dip->chain for our setup.
	 */
retry:
	parent = dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

	lhc = hammer2_dirhash(name, name_len);
	hlink = (ochain->parent != NULL);

	/*
	 * In fake mode flush oip so we can just snapshot it downbelow.
	 */
	if (hlink && hammer2_hardlink_enable < 0)
		hammer2_chain_flush(hmp, ochain, 0);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	error = 0;
	while (error == 0) {
		nchain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		if (nchain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(hmp, nchain);
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
			nchain = hammer2_chain_create(hmp, parent,
						     NULL, lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES,
						     &error);
		} else {
			/*
			 * NOTE: reconnects oip->chain to the media
			 *	 topology and returns its argument
			 *	 (oip->chain).
			 *
			 * No additional locks or refs are obtained on
			 * the returned chain so don't double-unlock!
			 */
			nchain = hammer2_chain_create(hmp, parent,
						     ochain, lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES,
						     &error);
		}
	}

	/*
	 * Unlock stuff.  This is a bit messy, if we have an EAGAIN error
	 * we need to wait for operations on parent to finish.
	 */
	if (error == EAGAIN)
		hammer2_chain_ref(hmp, parent);
	hammer2_chain_unlock(hmp, parent);

	/*
	 * ochain still active.
	 *
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(nchain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
			hammer2_chain_drop(hmp, parent);
			goto retry;
		}
		hammer2_chain_unlock(hmp, ochain);
		return (error);
	}

	/*
	 * Directory entries are inodes so if the name has changed we have
	 * to update the inode.
	 *
	 * When creating an OBJTYPE_HARDLINK entry remember to unlock the
	 * chain, the caller will access the hardlink via the actual hardlink
	 * target file and not the hardlink pointer entry.
	 */
	if (hlink && hammer2_hardlink_enable >= 0) {
		/*
		 * Create the HARDLINK pointer.  oip represents the hardlink
		 * target in this situation.
		 *
		 * NOTE: *_get() integrates chain's lock into the inode lock.
		 */
		hammer2_chain_modify(hmp, nchain, 0);
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
		hammer2_chain_unlock(hmp, nchain);
	} else if (hlink && hammer2_hardlink_enable < 0) {
		/*
		 * Create a snapshot (hardlink fake mode for debugging).
		 *
		 * NOTE: *_get() integrates nchain's lock into the inode lock.
		 */
		hammer2_chain_modify(hmp, nchain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		ipdata = &nchain->data->ipdata;
		*ipdata = ochain->data->ipdata;
		bcopy(name, ipdata->filename, name_len);
		ipdata->name_key = lhc;
		ipdata->name_len = name_len;
		kprintf("created fake hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
		hammer2_chain_unlock(hmp, nchain);
	} else {
		/*
		 * Normally disconnected inode (e.g. during a rename) that
		 * was reconnected.  We must fixup the name stored in
		 * oip.
		 *
		 * We are using oip as chain, already locked by caller,
		 * do not unlock it.
		 */
		hammer2_chain_modify(hmp, ochain, 0);
		ipdata = &ochain->data->ipdata;

		if (ipdata->name_len != name_len ||
		    bcmp(ipdata->filename, name, name_len) != 0) {
			KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
			bcopy(name, ipdata->filename, name_len);
			ipdata->name_key = lhc;
			ipdata->name_len = name_len;
		}
		ipdata->nlinks = 1;
	}
	hammer2_chain_unlock(hmp, ochain);
	return (0);
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
 * If retain_chain is non-NULL this function can fail with an EAGAIN if it
 * catches the object in the middle of a flush.
 */
int
hammer2_unlink_file(hammer2_inode_t *dip,
		    const uint8_t *name, size_t name_len,
		    int isdir, hammer2_chain_t *retain_chain)
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
	parent = hammer2_inode_lock_ex(dip);
	chain = hammer2_chain_lookup(hmp, &parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   0);
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
		hammer2_chain_unlock(hmp, parent);
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
		dparent = chain;
		hammer2_chain_lock(hmp, dparent, HAMMER2_RESOLVE_ALWAYS);
		dchain = hammer2_chain_lookup(hmp, &dparent,
					      0, (hammer2_key_t)-1,
					      HAMMER2_LOOKUP_NODATA);
		if (dchain) {
			hammer2_chain_unlock(hmp, dchain);
			hammer2_chain_unlock(hmp, dparent);
			error = ENOTEMPTY;
			goto done;
		}
		hammer2_chain_unlock(hmp, dparent);
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
		hammer2_chain_lock(hmp, ochain, HAMMER2_RESOLVE_ALWAYS);
		parent_ref = 1;
		for (;;) {
			parent = ochain->parent;
			hammer2_chain_ref(hmp, parent);
			hammer2_chain_unlock(hmp, ochain);
			hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(hmp, ochain, HAMMER2_RESOLVE_ALWAYS);
			if (ochain->parent == parent)
				break;
			hammer2_chain_unlock(hmp, parent);
			hammer2_chain_drop(hmp, parent);
		}

		if (ochain == retain_chain && ochain->flushing) {
			hammer2_chain_unlock(hmp, ochain);
			error = EAGAIN;
			goto done;
		}
		hammer2_chain_delete(hmp, parent, ochain,
				     (ochain == retain_chain));
		hammer2_chain_unlock(hmp, ochain);
		hammer2_chain_unlock(hmp, parent);
		hammer2_chain_drop(hmp, parent);
		parent = NULL;

		/*
		 * Then decrement nlinks on hardlink target, deleting
		 * the target when nlinks drops to 0.
		 */
		if (chain->data->ipdata.nlinks == 1) {
			dparent = chain->parent;
			hammer2_chain_ref(hmp, chain);
			hammer2_chain_unlock(hmp, chain);
			hammer2_chain_lock(hmp, dparent,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(hmp, chain,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_drop(hmp, chain);
			hammer2_chain_modify(hmp, chain, 0);
			--chain->data->ipdata.nlinks;
			hammer2_chain_delete(hmp, dparent, chain, 0);
			hammer2_chain_unlock(hmp, dparent);
		} else {
			hammer2_chain_modify(hmp, chain, 0);
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
		if (chain == retain_chain && chain->flushing) {
			error = EAGAIN;
			goto done;
		}
		hammer2_chain_modify(hmp, chain, 0);
		--ipdata->nlinks;
		hammer2_chain_delete(hmp, parent, chain,
				     (retain_chain == chain));
	}

	error = 0;
done:
	if (chain)
		hammer2_chain_unlock(hmp, chain);
	if (parent) {
		hammer2_chain_unlock(hmp, parent);
		if (parent_ref)
			hammer2_chain_drop(hmp, parent);
	}
	if (ochain)
		hammer2_chain_drop(hmp, ochain);

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
 * Given an unlocked ip consolidate for hardlink creation, adding (nlinks)
 * to the file's link count and potentially relocating the file to a
 * directory common to ip->pip and tdip.
 *
 * If the file has to be relocated ip->chain will also be adjusted.
 */
int
hammer2_hardlink_consolidate(hammer2_inode_t *ip, hammer2_chain_t **chainp,
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

	hmp = tdip->hmp;
	*chainp = NULL;
	chain = hammer2_inode_lock_ex(ip);

	if (nlinks == 0 &&			/* no hardlink needed */
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		hammer2_inode_unlock_ex(ip, NULL);
		*chainp = chain;
		return (0);
	}
	if (hammer2_hardlink_enable < 0) {	/* fake hardlinks */
		hammer2_inode_unlock_ex(ip, NULL);
		*chainp = chain;
		return (0);
	}
	if (hammer2_hardlink_enable == 0) {	/* disallow hardlinks */
		hammer2_inode_unlock_ex(ip, chain);
		return (ENOTSUP);
	}

	/*
	 * cdip will be returned with a ref, but not locked.
	 */
	fdip = ip->pip;
	cdip = hammer2_inode_common_parent(hmp, fdip, tdip);

	/*
	 * If no change in the hardlink's target directory is required and
	 * this is already a hardlink target, all we need to do is adjust
	 * the link count.
	 */
	if (cdip == fdip &&
	    (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		if (nlinks) {
			hammer2_chain_modify(hmp, chain, 0);
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
	error = hammer2_inode_duplicate(cdip, chain, &nchain);
	if (error == 0) {
		/*
		 * Bump nlinks on duplicated hidden inode.
		 */
		hammer2_chain_modify(hmp, nchain, 0);
		nchain->data->ipdata.nlinks += nlinks;

		/*
		 * If the old chain is not a hardlink target then replace
		 * it with a OBJTYPE_HARDLINK pointer.
		 *
		 * If the old chain IS a hardlink target then delete it.
		 */
		if (chain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) {
			hammer2_chain_modify(hmp, chain, 0);
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
				hammer2_chain_ref(hmp, parent);
				hammer2_chain_ref(hmp, chain);
				hammer2_chain_unlock(hmp, chain);
				hammer2_chain_lock(hmp, parent,
						   HAMMER2_RESOLVE_ALWAYS);
				hammer2_chain_lock(hmp, chain,
						   HAMMER2_RESOLVE_ALWAYS);
				hammer2_chain_drop(hmp, chain);
				if (chain->parent == parent)
					break;
				hammer2_chain_unlock(hmp, parent);
				hammer2_chain_drop(hmp, parent);
			}
			hammer2_chain_delete(hmp, parent, chain, 0);
			hammer2_chain_unlock(hmp, parent);
			hammer2_chain_drop(hmp, parent);
		}

		/*
		 * Replace ip->chain with nchain (ip is still locked).
		 */
		hammer2_chain_ref(hmp, nchain);			/* ip->chain */
		if (ip->chain)
			hammer2_chain_drop(hmp, ip->chain);	/* ip->chain */
		ip->chain = nchain;

		hammer2_chain_unlock(hmp, chain);
		*chainp = nchain;
	} else {
		hammer2_chain_unlock(hmp, chain);
	}

	/*
	 * Cleanup, chain/nchain already dealt with.
	 */
done:
	hammer2_inode_unlock_ex(ip, NULL);
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
hammer2_hardlink_deconsolidate(hammer2_inode_t *dip,
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
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *parent;
	hammer2_inode_t *ip;
	hammer2_inode_t *pip;
	hammer2_key_t lhc;

	pip = dip;
	hammer2_inode_ref(pip);		/* for loop */
	hammer2_chain_ref(hmp, chain);	/* for (*ochainp) */

	*ochainp = chain;

	/*
	 * Locate the hardlink.  pip is referenced and not locked,
	 * ipp.
	 *
	 * chain is reused.
	 */
	lhc = chain->data->ipdata.inum;
	hammer2_chain_unlock(hmp, chain);
	chain = NULL;

	while ((ip = pip) != NULL) {
		parent = hammer2_inode_lock_ex(ip);
		hammer2_inode_drop(ip);			/* loop */
		KKASSERT(parent->bref.type == HAMMER2_BREF_TYPE_INODE);
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		hammer2_chain_unlock(hmp, parent);
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
hammer2_inode_common_parent(hammer2_mount_t *hmp,
			    hammer2_inode_t *fdip, hammer2_inode_t *tdip)
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
