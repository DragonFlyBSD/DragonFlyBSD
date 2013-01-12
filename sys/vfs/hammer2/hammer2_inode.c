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
	hammer2_chain_ref(ip->hmp, &ip->chain);
}

/*
 * Drop an inode reference, freeing the inode when the last reference goes
 * away.
 */
void
hammer2_inode_drop(hammer2_inode_t *ip)
{
	hammer2_chain_drop(ip->hmp, &ip->chain);
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
	struct vnode *vp;
	hammer2_pfsmount_t *pmp;
	ccms_state_t ostate;

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
		vp = ip->vp;
		if (vp) {
			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, vnode is held to prevent
			 * destruction during the vget().  The vget() can
			 * still fail if we lost a reclaim race on the vnode.
			 */
			vhold_interlocked(vp);
			ccms_thread_unlock(&ip->chain.cst);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				ccms_thread_lock(&ip->chain.cst,
						 CCMS_STATE_EXCLUSIVE);
				continue;
			}
			ccms_thread_lock(&ip->chain.cst, CCMS_STATE_EXCLUSIVE);
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
		ostate = ccms_thread_lock_upgrade(&ip->chain.cst);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			ccms_thread_lock_restore(&ip->chain.cst, ostate);
			continue;
		}

		switch (ip->ip_data.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			vp->v_type = VREG;
			vinitvmio(vp, ip->ip_data.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->ip_data.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			/*
			 * XXX for now we are using the generic file_read
			 * and file_write code so we need a buffer cache
			 * association.
			 */
			vp->v_type = VLNK;
			vinitvmio(vp, ip->ip_data.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->ip_data.size & HAMMER2_LBUFMASK);
			break;
		/* XXX FIFO */
		default:
			panic("hammer2: unhandled objtype %d",
			      ip->ip_data.type);
			break;
		}

		if (ip == pmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_chain_ref(ip->hmp, &ip->chain);	/* vp association */
		ccms_thread_lock_restore(&ip->chain.cst, ostate);
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
 * Create a new inode in the specified directory using the vattr to
 * figure out the type of inode.
 *
 * If no error occurs the new inode with its chain locked is returned in
 * *nipp, otherwise an error is returned and *nipp is set to NULL.
 *
 * If vap and/or cred are NULL the related fields are not set and the
 * inode type defaults to a directory.  This is used when creating PFSs
 * under the super-root, so the inode number is set to 1 in this case.
 */
int
hammer2_inode_create(hammer2_inode_t *dip,
		     struct vattr *vap, struct ucred *cred,
		     const uint8_t *name, size_t name_len,
		     hammer2_inode_t **nipp)
{
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_inode_t *nip;
	hammer2_key_t lhc;
	int error;
	uid_t xuid;

	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
retry:
	parent = &dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

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
	hammer2_chain_unlock(hmp, parent);

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
		return (error);
	}

	/*
	 * Set up the new inode
	 */
	nip = chain->u.ip;
	*nipp = nip;

	hammer2_voldata_lock(hmp);
	if (vap) {
		nip->ip_data.type = hammer2_get_obj_type(vap->va_type);
		nip->ip_data.inum = hmp->voldata.alloc_tid++;
		/* XXX modify/lock */
	} else {
		nip->ip_data.type = HAMMER2_OBJTYPE_DIRECTORY;
		nip->ip_data.inum = 1;
	}
	hammer2_voldata_unlock(hmp);
	nip->ip_data.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nip->ip_data.ctime);
	nip->ip_data.mtime = nip->ip_data.ctime;
	if (vap)
		nip->ip_data.mode = vap->va_mode;
	nip->ip_data.nlinks = 1;
	if (vap) {
		if (dip) {
			xuid = hammer2_to_unix_xid(&dip->ip_data.uid);
			xuid = vop_helper_create_uid(dip->pmp->mp,
						     dip->ip_data.mode,
						     xuid,
						     cred,
						     &vap->va_mode);
		} else {
			xuid = 0;
		}
		if (vap->va_vaflags & VA_UID_UUID_VALID)
			nip->ip_data.uid = vap->va_uid_uuid;
		else if (vap->va_uid != (uid_t)VNOVAL)
			hammer2_guid_to_uuid(&nip->ip_data.uid, vap->va_uid);
		else
			hammer2_guid_to_uuid(&nip->ip_data.uid, xuid);

		if (vap->va_vaflags & VA_GID_UUID_VALID)
			nip->ip_data.gid = vap->va_gid_uuid;
		else if (vap->va_gid != (gid_t)VNOVAL)
			hammer2_guid_to_uuid(&nip->ip_data.gid, vap->va_gid);
		else if (dip)
			nip->ip_data.gid = dip->ip_data.gid;
	}

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (nip->ip_data.type == HAMMER2_OBJTYPE_REGFILE ||
	    nip->ip_data.type == HAMMER2_OBJTYPE_SOFTLINK) {
		nip->ip_data.op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}

	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
	bcopy(name, nip->ip_data.filename, name_len);
	nip->ip_data.name_key = lhc;
	nip->ip_data.name_len = name_len;

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
			hammer2_inode_t **nipp,
			const uint8_t *name, size_t name_len)
{
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_inode_t *nip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lhc;
	int error;

	if (name) {
		lhc = hammer2_dirhash(name, name_len);
	} else {
		lhc = oip->ip_data.inum;
		KKASSERT((lhc & HAMMER2_DIRHASH_VISIBLE) == 0);
	}

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	nip = NULL;
retry:
	parent = &dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

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
	hammer2_chain_unlock(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
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
	hammer2_inode_lock_ex(oip);
	hammer2_chain_flush(hmp, &oip->chain, 0);
	hammer2_inode_unlock_ex(oip);
	/*KKASSERT(RB_EMPTY(&oip->chain.rbhead));*/

	nip = chain->u.ip;
	hammer2_chain_modify(hmp, chain, 0);
	nip->ip_data = oip->ip_data;	/* sync media data after flush */

	if (name) {
		/*
		 * Directory entries are inodes so if the name has changed
		 * we have to update the inode.
		 */
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, nip->ip_data.filename, name_len);
		nip->ip_data.name_key = lhc;
		nip->ip_data.name_len = name_len;
	} else {
		/*
		 * Directory entries are inodes but this is a hidden hardlink
		 * target.  The name isn't used but to ease debugging give it
		 * a name after its inode number.
		 */
		ksnprintf(nip->ip_data.filename, sizeof(nip->ip_data.filename),
			  "0x%016jx", (intmax_t)nip->ip_data.inum);
		nip->ip_data.name_len = strlen(nip->ip_data.filename);
		nip->ip_data.name_key = lhc;
	}
	*nipp = nip;

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
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
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
retry:
	parent = &dip->chain;
	if (oip->pip == dip) {
		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_lock(hmp, &oip->chain, HAMMER2_RESOLVE_ALWAYS);
	} else {
		hammer2_chain_lock(hmp, &oip->chain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
	}

	lhc = hammer2_dirhash(name, name_len);
	hlink = (oip->chain.parent != NULL);

	/*
	 * In fake mode flush oip so we can just snapshot it downbelow.
	 */
	if (hlink && hammer2_hardlink_enable < 0)
		hammer2_chain_flush(hmp, &oip->chain, 0);

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
						     &oip->chain, lhc, 0,
						     HAMMER2_BREF_TYPE_INODE,
						     HAMMER2_INODE_BYTES,
						     &error);
			if (chain)
				KKASSERT(chain == &oip->chain);
		}
	}
	hammer2_chain_unlock(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		if (error == EAGAIN) {
			hammer2_chain_wait(hmp, parent);
			hammer2_chain_unlock(hmp, &oip->chain);
			goto retry;
		}
		hammer2_chain_unlock(hmp, &oip->chain);
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
		 */
		nip = chain->u.ip;
		hammer2_chain_modify(hmp, chain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, nip->ip_data.filename, name_len);
		nip->ip_data.name_key = lhc;
		nip->ip_data.name_len = name_len;
		nip->ip_data.target_type = oip->ip_data.type;
		nip->ip_data.type = HAMMER2_OBJTYPE_HARDLINK;
		nip->ip_data.inum = oip->ip_data.inum;
		nip->ip_data.nlinks = 1;
		kprintf("created hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
		hammer2_chain_unlock(hmp, chain);
	} else if (hlink && hammer2_hardlink_enable < 0) {
		/*
		 * Create a snapshot (hardlink fake mode for debugging).
		 */
		nip = chain->u.ip;
		nip->ip_data = oip->ip_data;
		hammer2_chain_modify(hmp, chain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, nip->ip_data.filename, name_len);
		nip->ip_data.name_key = lhc;
		nip->ip_data.name_len = name_len;
		kprintf("created fake hardlink %*.*s\n",
			(int)name_len, (int)name_len, name);
		hammer2_chain_unlock(hmp, chain);
	} else {
		/*
		 * Normally disconnected inode (e.g. during a rename) that
		 * was reconnected.  We must fixup the name stored in
		 * oip.
		 *
		 * We are using oip as chain, already locked by caller,
		 * do not unlock it.
		 */
		hammer2_chain_modify(hmp, chain, 0);
		if (oip->ip_data.name_len != name_len ||
		    bcmp(oip->ip_data.filename, name, name_len) != 0) {
			KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
			bcopy(name, oip->ip_data.filename, name_len);
			oip->ip_data.name_key = lhc;
			oip->ip_data.name_len = name_len;
		}
		oip->ip_data.nlinks = 1;
	}
	hammer2_chain_unlock(hmp, &oip->chain);
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
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *dparent;
	hammer2_chain_t *dchain;
	hammer2_key_t lhc;
	hammer2_inode_t *ip;
	hammer2_inode_t *oip;
	int error;
	uint8_t type;

	error = 0;
	oip = NULL;
	hmp = dip->hmp;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Search for the filename in the directory
	 */
	parent = &dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
	chain = hammer2_chain_lookup(hmp, &parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    chain->u.ip &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   0);
	}

	/*
	 * Not found or wrong type (isdir < 0 disables the type check).
	 */
	if (chain == NULL) {
		hammer2_chain_unlock(hmp, parent);
		return ENOENT;
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
	 */
	if (chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
		hammer2_chain_unlock(hmp, parent);
		parent = NULL;
		error = hammer2_hardlink_find(dip, &chain, &oip);
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
	 */
	if (oip) {
		/*
		 * If this was a hardlink we first delete the hardlink
		 * pointer entry.  parent is NULL on entry due to the oip
		 * path.
		 */
		parent = oip->chain.parent;
		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_lock(hmp, &oip->chain, HAMMER2_RESOLVE_ALWAYS);
		if (oip == retain_ip && oip->chain.flushing) {
			hammer2_chain_unlock(hmp, &oip->chain);
			error = EAGAIN;
			goto done;
		}
		hammer2_chain_delete(hmp, parent, &oip->chain,
				     (retain_ip == oip));
		hammer2_chain_unlock(hmp, &oip->chain);
		hammer2_chain_unlock(hmp, parent);
		parent = NULL;

		/*
		 * Then decrement nlinks on hardlink target.
		 */
		ip = chain->u.ip;
		if (ip->ip_data.nlinks == 1) {
			dparent = chain->parent;
			hammer2_chain_ref(hmp, chain);
			hammer2_chain_unlock(hmp, chain);
			hammer2_chain_lock(hmp, dparent,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(hmp, chain,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_drop(hmp, chain);
			hammer2_chain_modify(hmp, chain, 0);
			--ip->ip_data.nlinks;
			hammer2_chain_delete(hmp, dparent, chain, 0);
			hammer2_chain_unlock(hmp, dparent);
		} else {
			hammer2_chain_modify(hmp, chain, 0);
			--ip->ip_data.nlinks;
		}
	} else {
		/*
		 * Otherwise this was not a hardlink and we can just
		 * remove the entry and decrement nlinks.
		 */
		ip = chain->u.ip;
		if (ip == retain_ip && chain->flushing) {
			error = EAGAIN;
			goto done;
		}
		hammer2_chain_modify(hmp, chain, 0);
		--ip->ip_data.nlinks;
		hammer2_chain_delete(hmp, parent, chain,
				     (retain_ip == ip));
	}

	error = 0;

done:
	if (chain)
		hammer2_chain_unlock(hmp, chain);
	if (parent)
		hammer2_chain_unlock(hmp, parent);
	if (oip)
		hammer2_chain_drop(oip->hmp, &oip->chain);

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

void
hammer2_inode_lock_nlinks(hammer2_inode_t *ip)
{
	hammer2_chain_ref(ip->hmp, &ip->chain);
}

void
hammer2_inode_unlock_nlinks(hammer2_inode_t *ip)
{
	hammer2_chain_drop(ip->hmp, &ip->chain);
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
 * (*ipp)'s nlinks field is locked on entry and the new (*ipp)'s nlinks
 * field will be locked on return (with the original's unlocked).
 *
 * The link count is bumped if requested.
 */
int
hammer2_hardlink_consolidate(hammer2_inode_t **ipp, hammer2_inode_t *tdip)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *oip = *ipp;
	hammer2_inode_t *nip = NULL;
	hammer2_inode_t *fdip;
	hammer2_inode_t *cdip;
	hammer2_chain_t *parent;
	int error;

	hmp = tdip->hmp;

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
	if (cdip == fdip &&
	    (oip->ip_data.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		kprintf("hardlink already consolidated correctly\n");
		nip = oip;
		hammer2_inode_lock_ex(nip);
		hammer2_chain_modify(hmp, &nip->chain, 0);
		++nip->ip_data.nlinks;
		hammer2_inode_unlock_ex(nip);
		hammer2_inode_drop(cdip);
		return (0);
	}

	/*
	 * Create a hidden inode directory entry in the parent, copying
	 * (*oip)'s state.  Then replace oip with OBJTYPE_HARDLINK.
	 *
	 * The duplication function will either flush or move any chains
	 * under oip to the new hardlink target inode, retiring all chains
	 * related to oip before returning.  XXX vp->ip races.
	 */
	error = hammer2_inode_duplicate(cdip, oip, &nip, NULL, 0);
	if (error == 0) {
		/*
		 * Bump nlinks on duplicated hidden inode.
		 */
		kprintf("hardlink consolidation success in parent dir %s\n",
			cdip->ip_data.filename);
		hammer2_inode_lock_nlinks(nip);
		hammer2_inode_unlock_nlinks(oip);
		hammer2_chain_modify(hmp, &nip->chain, 0);
		++nip->ip_data.nlinks;
		hammer2_inode_unlock_ex(nip);

		if (oip->ip_data.name_key & HAMMER2_DIRHASH_VISIBLE) {
			/*
			 * Replace the old inode with an OBJTYPE_HARDLINK
			 * pointer.
			 */
			hammer2_inode_lock_ex(oip);
			hammer2_chain_modify(hmp, &oip->chain, 0);
			oip->ip_data.target_type = oip->ip_data.type;
			oip->ip_data.type = HAMMER2_OBJTYPE_HARDLINK;
			oip->ip_data.uflags = 0;
			oip->ip_data.rmajor = 0;
			oip->ip_data.rminor = 0;
			oip->ip_data.ctime = 0;
			oip->ip_data.mtime = 0;
			oip->ip_data.atime = 0;
			oip->ip_data.btime = 0;
			bzero(&oip->ip_data.uid, sizeof(oip->ip_data.uid));
			bzero(&oip->ip_data.gid, sizeof(oip->ip_data.gid));
			oip->ip_data.op_flags = HAMMER2_OPFLAG_DIRECTDATA;
			oip->ip_data.cap_flags = 0;
			oip->ip_data.mode = 0;
			oip->ip_data.size = 0;
			oip->ip_data.nlinks = 1;
			oip->ip_data.iparent = 0;	/* XXX */
			oip->ip_data.pfs_type = 0;
			oip->ip_data.pfs_inum = 0;
			bzero(&oip->ip_data.pfs_clid,
			      sizeof(oip->ip_data.pfs_clid));
			bzero(&oip->ip_data.pfs_fsid,
			      sizeof(oip->ip_data.pfs_fsid));
			oip->ip_data.data_quota = 0;
			oip->ip_data.data_count = 0;
			oip->ip_data.inode_quota = 0;
			oip->ip_data.inode_count = 0;
			oip->ip_data.attr_tid = 0;
			oip->ip_data.dirent_tid = 0;
			bzero(&oip->ip_data.u, sizeof(oip->ip_data.u));
			/* XXX transaction ids */

			hammer2_inode_unlock_ex(oip);
		} else {
			/*
			 * The old inode was a hardlink target, which we
			 * have now moved.  We must delete it so the new
			 * hardlink target at a higher directory level
			 * becomes the only hardlink target for this inode.
			 */
			kprintf("DELETE INVISIBLE\n");
			parent = oip->chain.parent;
			hammer2_chain_lock(hmp, parent,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_lock(hmp, &oip->chain,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_chain_delete(hmp, parent, &oip->chain, 0);
			hammer2_chain_unlock(hmp, &oip->chain);
			hammer2_chain_unlock(hmp, parent);
		}
		*ipp = nip;
	} else {
		KKASSERT(nip == NULL);
	}
	hammer2_inode_drop(cdip);

	return (error);
}

/*
 * If (*ipp) is non-NULL it points to the forward OBJTYPE_HARDLINK inode while
 * (*chainp) points to the resolved (hidden hardlink target) inode.  In this
 * situation when nlinks is 1 we wish to deconsolidate the hardlink, moving
 * it back to the directory that now represents the only remaining link.
 */
int
hammer2_hardlink_deconsolidate(hammer2_inode_t *dip, hammer2_chain_t **chainp,
			       hammer2_inode_t **ipp)
{
	if (*ipp == NULL)
		return (0);
	/* XXX */
	return (0);
}

/*
 * When presented with a (*chainp) representing an inode of type
 * OBJTYPE_HARDLINK this code will save the original inode (with a ref)
 * in (*ipp), and then locate the hidden hardlink target in (dip) or
 * any parent directory above (dip).  The locked (*chainp) is replaced
 * with a new locked (*chainp) representing the hardlink target.
 */
int
hammer2_hardlink_find(hammer2_inode_t *dip, hammer2_chain_t **chainp,
		      hammer2_inode_t **ipp)
{
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *parent;
	hammer2_inode_t *pip;
	hammer2_key_t lhc;

	*ipp = chain->u.ip;
	hammer2_inode_ref(chain->u.ip);
	lhc = chain->u.ip->ip_data.inum;

	hammer2_inode_unlock_ex(chain->u.ip);
	pip = chain->u.ip->pip;

	chain = NULL;
	while (pip) {
		parent = &pip->chain;
		KKASSERT(parent->bref.type == HAMMER2_BREF_TYPE_INODE);

		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc, 0);
		hammer2_chain_unlock(hmp, parent);
		if (chain)
			break;
		pip = pip->pip;	/* XXX SMP RACE */
	}
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
