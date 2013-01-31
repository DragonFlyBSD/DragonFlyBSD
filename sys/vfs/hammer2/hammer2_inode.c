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
	u_int refs;
	hammer2_mount_t *hmp;

	for (;;) {
		refs = ip->refs;
		cpu_ccfence();
		if (refs == 1) {
			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				kprintf("hammer2_inode_drop: 1->0 %p\n", ip);
				KKASSERT(ip->topo_cst.count == 0);
				KKASSERT(ip->chain == NULL);
				hmp = ip->hmp;
				ip->hmp = NULL;
				kfree(ip, hmp->minode);
				break;
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
 * Return an exclusively locked inode associated with the specified
 * chain.  The chain must be a BREF_TYPE_INODE, and (dip) must properly
 * specify the inode's position in the topology.
 *
 * The passed-in chain must be locked and the returned inode will also be
 * locked.
 *
 * WARNING!  This routine sucks up the chain's lock (makes it part of the
 *	     inode lock), so callers need to be careful.
 *
 * WARNING!  The mount code is allowed to pass dip == NULL for iroot.
 */
hammer2_inode_t *
hammer2_inode_get(hammer2_pfsmount_t *pmp, hammer2_inode_t *dip,
		  hammer2_chain_t *chain)
{
	hammer2_mount_t *hmp = pmp->hmp;
	hammer2_inode_t *nip;

	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);
	if (chain->u.ip) {
		nip = chain->u.ip;
		KKASSERT(nip->pip == dip);
		KKASSERT(nip->pmp == pmp);
	} else {
		nip = kmalloc(sizeof(*nip), hmp->minode, M_WAITOK | M_ZERO);
		nip->chain = chain;
		nip->pip = dip;	/* can be NULL */
		if (dip)
			hammer2_inode_ref(dip);
		nip->pmp = pmp;
		nip->hmp = hmp;
		nip->refs = 1;
		ccms_cst_init(&nip->topo_cst, &nip->chain);
		hammer2_chain_ref(hmp, chain);
		chain->u.ip = nip;
	}
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
 * by this function.
 */
void
hammer2_inode_put(hammer2_inode_t *ip, hammer2_chain_t *chain)
{
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_inode_t *pip;

	KKASSERT(chain);
	KKASSERT(chain->u.ip == ip);
	KKASSERT(ip->topo_cst.count == -1);	/* one excl lock allowed */
	chain->u.ip = NULL;
	ip->chain = NULL;
	hammer2_chain_drop(hmp, chain);		/* ref */

	/*
	 * Disconnect ip from pip & related parent ref.
	 *
	 * We have to unlock the chain manually because
	 * the ip->chain pointer has already been NULL'd out.
	 */
	if ((pip = ip->pip) != NULL) {
		ip->pip = NULL;
		hammer2_inode_unlock_ex(ip, chain);
		hammer2_inode_drop(pip);
	} else {
		hammer2_inode_unlock_ex(ip, chain);
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
	nip = hammer2_inode_get(dip->pmp, dip, chain);
	kprintf("nip %p chain %p\n", nip, nip->chain);
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
 * Duplicate the specified existing inode in the specified target directory.
 * If name is NULL the inode is duplicated as a hidden directory entry.
 *
 * Returns the new inode.  The old inode is left alone.
 *
 * XXX name needs to be NULL for now.
 */
int
hammer2_inode_duplicate(hammer2_inode_t *dip, hammer2_inode_t *oip,
			hammer2_inode_t **nipp, hammer2_chain_t **nchainp,
			const uint8_t *name, size_t name_len)
{
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_inode_t *nip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lhc;
	int error;

	hmp = dip->hmp;
	if (name) {
		lhc = hammer2_dirhash(name, name_len);
	} else {
		parent = hammer2_inode_lock_ex(oip);
		lhc = parent->data->ipdata.inum;
		hammer2_inode_unlock_ex(oip, parent);
		KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);
	}

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	nip = NULL;
retry:
	parent = hammer2_inode_lock_ex(dip);

	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		if (chain == NULL)
			break;
		/* XXX bcmp name if not NULL */
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		if ((lhc & HAMMER2_DIRHASH_VISIBLE) == 0) /* shouldn't happen */
			error = ENOSPC;
		hammer2_chain_unlock(hmp, chain);
		chain = NULL;
		++lhc;
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
	hammer2_inode_unlock_ex(dip, parent);

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
	 * We cannot leave oip with any in-memory chains because (for a
	 * hardlink), oip will become a OBJTYPE_HARDLINK which is just a
	 * pointer to the real hardlink's inum and can't have any sub-chains.
	 * XXX might be 0-ref chains left.
	 */
	parent = hammer2_inode_lock_ex(oip);
	hammer2_chain_flush(hmp, parent, 0);
	/*KKASSERT(RB_EMPTY(&oip->chain.rbhead));*/

	/*
	 * nip is a duplicate of oip.  Meta-data will be synchronized to
	 * media when nip is flushed.
	 *
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 */
	nip = hammer2_inode_get(dip->pmp, dip, chain);
	hammer2_chain_modify(hmp, chain, 0);
	nipdata = &chain->data->ipdata;
	*nipdata = parent->data->ipdata;
	hammer2_inode_unlock_ex(oip, parent);

	if (name) {
		/*
		 * Directory entries are inodes so if the name has changed
		 * we have to update the inode.
		 */
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, nipdata->filename, name_len);
		nipdata->name_key = lhc;
		nipdata->name_len = name_len;
	} else {
		/*
		 * Directory entries are inodes but this is a hidden hardlink
		 * target.  The name isn't used but to ease debugging give it
		 * a name after its inode number.
		 */
		ksnprintf(nipdata->filename, sizeof(nipdata->filename),
			  "0x%016jx", (intmax_t)nipdata->inum);
		nipdata->name_len = strlen(nipdata->filename);
		nipdata->name_key = lhc;
	}
	*nipp = nip;
	*nchainp = chain;

	return (0);
}


/*
 * Connect inode (oip) to the specified directory using the specified name.
 * (oip) must be locked.
 *
 * If (oip) is not currently connected we simply connect it up.
 *
 * If (oip) is already connected we create a OBJTYPE_HARDLINK entry which
 * points to (oip)'s inode number.  (oip) is expected to be the terminus of
 * the hardlink sitting as a hidden file in a common parent directory
 * in this situation.
 */
int
hammer2_inode_connect(hammer2_inode_t *dip, hammer2_inode_t *oip,
		      const uint8_t *name, size_t name_len)
{
	hammer2_inode_data_t *nipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_inode_t *nip;
	hammer2_key_t lhc;
	int error;
	int hlink;

	/*
	 * (oip) is the terminus of the hardlink sitting in the common
	 * parent directory.  This means that if oip->pip != dip then
	 * the already locked oip is ABOVE dip.
	 *
	 * But if the common parent directory IS dip, then we would have
	 * a lock-order reversal and must rearrange the lock ordering.
	 * For now the caller deals with this for us by locking dip in
	 * that case (and our lock here winds up just being recursive)
	 */
	hmp = dip->hmp;
retry:
	if (oip->pip == dip) {
		parent = hammer2_inode_lock_ex(dip);
		ochain = hammer2_inode_lock_ex(oip);
	} else {
		ochain = hammer2_inode_lock_ex(oip);
		parent = hammer2_inode_lock_ex(dip);
	}

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
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		if (chain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(hmp, chain);
		chain = NULL;
		++lhc;
	}

	/*
	 * Passing a non-NULL chain to hammer2_chain_create() reconnects the
	 * existing chain instead of creating a new one.  The chain's bref
	 * will be properly updated.
	 */
	if (error == 0) {
		if (hlink) {
			chain = hammer2_chain_create(hmp, parent,
						     NULL, lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES,
						     &error);
		} else {
			chain = hammer2_chain_create(hmp, parent,
						     oip->chain, lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES,
						     &error);
			if (chain) {
				KKASSERT(chain == oip->chain);
				KKASSERT(ochain == oip->chain);
			}
		}
	}

	/*
	 * Unlock stuff.  This is a bit messy, if we have an EAGAIN error
	 * we need to wait for operations on parent to finish.
	 */
	if (error == EAGAIN)
		hammer2_chain_ref(hmp, parent);
	hammer2_inode_unlock_ex(dip, parent);

	/*
	 * oip/ochain still active.
	 *
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
			hammer2_chain_drop(hmp, parent);
			hammer2_inode_unlock_ex(oip, ochain);
			goto retry;
		}
		hammer2_inode_unlock_ex(oip, ochain);
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
		KKASSERT(chain->u.ip == NULL);
		nip = hammer2_inode_get(dip->pmp, dip, chain);
		hammer2_chain_modify(hmp, chain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		nipdata = &nip->chain->data->ipdata;
		bcopy(name, nipdata->filename, name_len);
		nipdata->name_key = lhc;
		nipdata->name_len = name_len;
		nipdata->target_type = ochain->data->ipdata.type;
		nipdata->type = HAMMER2_OBJTYPE_HARDLINK;
		nipdata->inum = ochain->data->ipdata.inum;
		nipdata->nlinks = 1;
		kprintf("created hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
	} else if (hlink && hammer2_hardlink_enable < 0) {
		/*
		 * Create a snapshot (hardlink fake mode for debugging).
		 *
		 * NOTE: *_get() integrates chain's lock into the inode lock.
		 */
		KKASSERT(chain->u.ip == NULL);
		nip = hammer2_inode_get(dip->pmp, dip, chain);
		hammer2_chain_modify(hmp, chain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		nipdata = &nip->chain->data->ipdata;
		*nipdata = ochain->data->ipdata;
		bcopy(name, nipdata->filename, name_len);
		nipdata->name_key = lhc;
		nipdata->name_len = name_len;
		kprintf("created fake hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
	} else {
		/*
		 * Normally disconnected inode (e.g. during a rename) that
		 * was reconnected.  We must fixup the name stored in
		 * oip.
		 *
		 * We are using oip as chain, already locked by caller,
		 * do not unlock it.
		 */
		KKASSERT(chain->u.ip != NULL);
		hammer2_chain_modify(hmp, chain, 0);
		nipdata = &ochain->data->ipdata;

		if (nipdata->name_len != name_len ||
		    bcmp(nipdata->filename, name, name_len) != 0) {
			KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
			bcopy(name, nipdata->filename, name_len);
			nipdata->name_key = lhc;
			nipdata->name_len = name_len;
		}
		nipdata->nlinks = 1;
	}
	hammer2_inode_unlock_ex(oip, ochain);
	return (0);
}

/*
 * Unlink the file from the specified directory inode.  The directory inode
 * does not need to be locked.
 *
 * isdir determines whether a directory/non-directory check should be made.
 * No check is made if isdir is set to -1.
 *
 * If retain_ip is non-NULL this function can fail with an EAGAIN if it
 * catches the object in the middle of a flush.
 */
int
hammer2_unlink_file(hammer2_inode_t *dip,
		    const uint8_t *name, size_t name_len,
		    int isdir, hammer2_inode_t *retain_ip)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_chain_t *chain;
	hammer2_chain_t *dparent;
	hammer2_chain_t *dchain;
	hammer2_chain_t *tmpchain;
	hammer2_key_t lhc;
	hammer2_inode_t *ip;
	hammer2_inode_t *oip;
	int error;
	int parent_ref;
	uint8_t type;

	parent_ref = 0;
	error = 0;
	ip = NULL;
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

		if (ochain == retain_ip->chain && ochain->flushing) {
			hammer2_chain_unlock(hmp, ochain);
			error = EAGAIN;
			goto done;
		}
		if ((oip = ochain->u.ip) != NULL) {
			tmpchain = hammer2_inode_lock_ex(oip);
			oip->flags |= HAMMER2_INODE_DELETED;
			if (oip->vp || oip->refs > 1)
				hammer2_inode_unlock_ex(oip, tmpchain);
			else
				hammer2_inode_put(oip, tmpchain);
			KKASSERT(tmpchain == ochain);
			/* ochain still actively locked */
		}
		hammer2_chain_delete(hmp, parent, ochain,
				     (ochain == retain_ip->chain));
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
			if ((ip = chain->u.ip) != NULL) {
				parent = hammer2_inode_lock_ex(ip);
				ip->flags |= HAMMER2_INODE_DELETED;
				if (ip->vp)
					hammer2_inode_unlock_ex(ip, parent);
				else
					hammer2_inode_put(ip, parent);
				parent = NULL;
			}
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
		ip = hammer2_inode_get(dip->pmp, dip, chain);
		ipdata = &ip->chain->data->ipdata;
		if (ip == retain_ip && chain->flushing) {
			hammer2_inode_unlock_ex(ip, chain);
			chain = NULL;	/* inode_unlock eats chain */
			error = EAGAIN;
			goto done;
		}
		hammer2_chain_modify(hmp, chain, 0);
		--ipdata->nlinks;
		ip->flags |= HAMMER2_INODE_DELETED;
		hammer2_chain_delete(hmp, parent, chain,
				     (retain_ip == ip));
		if (ip->vp)
			hammer2_inode_unlock_ex(ip, chain);
		else
			hammer2_inode_put(ip, chain);
		chain = NULL;	/* inode_unlock eats chain */
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
 * Consolidate for hard link creation.  This moves the specified terminal
 * hardlink inode to a directory common to its current directory and tdip
 * if necessary, replacing *ipp with the new inode chain element and
 * modifying the original inode chain element to OBJTYPE_HARDLINK.
 *
 * If the original inode chain element was a prior incarnation of a hidden
 * inode it can simply be deleted instead of converted.
 *
 * (*ipp) must be referenced on entry and the new (*ipp) will be referenced
 * on return (with the original dropped).  (*ipp) must not be locked.
 *
 * The link count is bumped if requested.
 */
int
hammer2_hardlink_consolidate(hammer2_inode_t **ipp, hammer2_inode_t *tdip)
{
	hammer2_inode_data_t *oipdata;
	hammer2_mount_t *hmp;
	hammer2_inode_t *oip;
	hammer2_inode_t *nip;
	hammer2_inode_t *fdip;
	hammer2_inode_t *cdip;
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *parent;
	int error;

	hmp = tdip->hmp;
	oip = *ipp;
	nip = NULL;

	if (hammer2_hardlink_enable < 0)
		return (0);
	if (hammer2_hardlink_enable == 0)
		return (ENOTSUP);

	fdip = oip->pip;
	cdip = hammer2_inode_common_parent(hmp, fdip, tdip);

	/*
	 * Nothing to do (except bump the link count) if the hardlink has
	 * already been consolidated in the correct place.
	 */
	if (cdip == fdip) {
		ochain = hammer2_inode_lock_ex(oip);
		if ((ochain->data->ipdata.name_key &
		     HAMMER2_DIRHASH_VISIBLE) == 0) {
			hammer2_chain_modify(hmp, ochain, 0);
			++ochain->data->ipdata.nlinks;
			hammer2_inode_unlock_ex(oip, ochain);
			hammer2_inode_drop(cdip);
			return(0);
		}
		hammer2_inode_unlock_ex(oip, ochain);
	}

	/*
	 * Create a hidden inode directory entry in the parent, copying
	 * (*oip)'s state.  Then replace oip with OBJTYPE_HARDLINK.
	 *
	 * The duplication function will either flush or move any chains
	 * under oip to the new hardlink target inode, retiring all chains
	 * related to oip before returning.  XXX vp->ip races.
	 */
	error = hammer2_inode_duplicate(cdip, oip, &nip, &nchain, NULL, 0);
	if (error == 0) {
		/*
		 * Bump nlinks on duplicated hidden inode.
		 */
		hammer2_inode_ref(nip);			/* ref new *ipp */
		hammer2_chain_modify(hmp, nchain, 0);
		++nchain->data->ipdata.nlinks;
		hammer2_inode_unlock_ex(nip, nchain);
		ochain = hammer2_inode_lock_ex(oip);
		hammer2_inode_drop(oip);		/* unref old *ipp */

		if (ochain->data->ipdata.name_key & HAMMER2_DIRHASH_VISIBLE) {
			/*
			 * Replace the old inode with an OBJTYPE_HARDLINK
			 * pointer.
			 */
			hammer2_chain_modify(hmp, ochain, 0);
			oipdata = &ochain->data->ipdata;
			oipdata->target_type = oipdata->type;
			oipdata->type = HAMMER2_OBJTYPE_HARDLINK;
			oipdata->uflags = 0;
			oipdata->rmajor = 0;
			oipdata->rminor = 0;
			oipdata->ctime = 0;
			oipdata->mtime = 0;
			oipdata->atime = 0;
			oipdata->btime = 0;
			bzero(&oipdata->uid, sizeof(oipdata->uid));
			bzero(&oipdata->gid, sizeof(oipdata->gid));
			oipdata->op_flags = HAMMER2_OPFLAG_DIRECTDATA;
			oipdata->cap_flags = 0;
			oipdata->mode = 0;
			oipdata->size = 0;
			oipdata->nlinks = 1;
			oipdata->iparent = 0;	/* XXX */
			oipdata->pfs_type = 0;
			oipdata->pfs_inum = 0;
			bzero(&oipdata->pfs_clid, sizeof(oipdata->pfs_clid));
			bzero(&oipdata->pfs_fsid, sizeof(oipdata->pfs_fsid));
			oipdata->data_quota = 0;
			oipdata->data_count = 0;
			oipdata->inode_quota = 0;
			oipdata->inode_count = 0;
			oipdata->attr_tid = 0;
			oipdata->dirent_tid = 0;
			bzero(&oipdata->u, sizeof(oipdata->u));
			/* XXX transaction ids */

			hammer2_inode_unlock_ex(oip, ochain);
		} else {
			/*
			 * The old inode was a hardlink target, which we
			 * have now moved.  We must delete it so the new
			 * hardlink target at a higher directory level
			 * becomes the only hardlink target for this inode.
			 */
			kprintf("DELETE INVISIBLE\n");
			for (;;) {
				parent = ochain->parent;
				hammer2_chain_ref(hmp, parent);
				hammer2_inode_unlock_ex(oip, ochain);
				hammer2_chain_lock(hmp, parent,
						   HAMMER2_RESOLVE_ALWAYS);
				ochain = hammer2_inode_lock_ex(oip);
				if (oip->chain->parent == parent)
					break;
				hammer2_chain_unlock(hmp, parent);
				hammer2_chain_drop(hmp, parent);
			}
			oip->flags |= HAMMER2_INODE_DELETED;
			hammer2_chain_delete(hmp, parent, ochain, 0);
			hammer2_inode_put(oip, ochain); /* unconditional */
			hammer2_chain_unlock(hmp, parent);
			hammer2_chain_drop(hmp, parent);
		}
		*ipp = nip;
	} else {
		KKASSERT(nip == NULL);
	}
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
		hammer2_inode_drop(ip);
		KKASSERT(parent->bref.type == HAMMER2_BREF_TYPE_INODE);
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		hammer2_chain_unlock(hmp, parent);
		if (chain)
			break;
		pip = ip->pip;		/* safe, ip held locked */
		if (pip)
			hammer2_inode_ref(pip);
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
