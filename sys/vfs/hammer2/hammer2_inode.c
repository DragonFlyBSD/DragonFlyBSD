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
 * necessary.
 *
 * Great care must be taken to avoid deadlocks and vnode acquisition/reclaim
 * races.
 *
 * The vnode will be returned exclusively locked and referenced.  The
 * reference on the vnode prevents it from being reclaimed.
 *
 * The inode (ip) must be referenced by the caller and not locked to avoid
 * it getting ripped out from under us or deadlocked.
 */
struct vnode *
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	struct vnode *vp;
	hammer2_pfsmount_t *pmp;

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
			 * Lock the inode and check for a reclaim race
			 */
			hammer2_inode_lock_ex(ip);
			if (ip->vp != vp) {
				hammer2_inode_unlock_ex(ip);
				continue;
			}

			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, vnode is held to prevent
			 * destruction during the vget().  The vget() can
			 * still fail if we lost a reclaim race on the vnode.
			 */
			vhold_interlocked(vp);
			hammer2_inode_unlock_ex(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				continue;
			}
			vdrop(vp);
			/* vp still locked and ref from vget */
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
			vp = NULL;
			break;
		}

		/*
		 * Lock the inode and check for an allocation race.
		 */
		hammer2_inode_lock_ex(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_unlock_ex(ip);
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
		hammer2_inode_unlock_ex(ip);
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

	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	parent = &dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

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
	if (error == 0) {
		chain = hammer2_chain_create(hmp, parent, NULL, lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES);
		if (chain == NULL)
			error = EIO;
	}
	hammer2_chain_unlock(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
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
	nip->ip_data.ctime = 0;
	nip->ip_data.mtime = 0;
	if (vap)
		nip->ip_data.mode = vap->va_mode;
	nip->ip_data.nlinks = 1;
	/* uid, gid, etc */

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
 * Connect inode (ip) to the specified directory using the specified name.
 * (ip) must be locked.
 */
int
hammer2_inode_connect(hammer2_inode_t *dip, hammer2_inode_t *ip,
		      const uint8_t *name, size_t name_len)
{
	hammer2_mount_t *hmp = dip->hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_key_t lhc;
	int error;

	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	parent = &dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

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
		chain = hammer2_chain_create(hmp, parent, &ip->chain, lhc, 0,
					     HAMMER2_BREF_TYPE_INODE /* n/a */,
					     HAMMER2_INODE_BYTES);   /* n/a */
		if (chain == NULL)
			error = EIO;
	}
	hammer2_chain_unlock(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		return (error);
	}

	/*
	 * Directory entries are inodes so if the name has changed we have
	 * to update the inode.
	 */
	if (ip->ip_data.name_len != name_len ||
	    bcmp(ip->ip_data.filename, name, name_len) != 0) {
		hammer2_chain_modify(hmp, chain, 0);
		KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
		bcopy(name, ip->ip_data.filename, name_len);
		ip->ip_data.name_key = lhc;
		ip->ip_data.name_len = name_len;
	}
	/*nip->ip_data.nlinks = 1;*/

	return (0);
}

/*
 * Create a hardlink forwarding entry (dip, name) to the specified (ip).
 *
 * This is one of the more complex implementations in HAMMER2.  The
 * filesystem strictly updates its chains bottom-up in a copy-on-write
 * fashion.  This makes hardlinks difficult to implement but we've come up
 * with a dandy solution.
 *
 * When a file has more than one link the actual inode is created as a
 * hidden directory entry (indexed by inode number) in a common parent of
 * all hardlinks which reference the file.  The hardlinks in each directory
 * are merely forwarding entries to the hidden inode.
 *
 * Implementation:
 *
 *	Most VOPs can be blissfully unaware of the forwarding entries.
 *	nresolve, nlink, and remove code have to be forwarding-aware
 *	in order to return the (ip/vp) for the actual file (and otherwise do
 *	the right thing).
 *
 *	(1) If the ip we are linking to is a normal embedded inode (nlinks==1)
 *	    we have to replace the directory entry with a forwarding inode
 *	    and move the normal ip/vp to a hidden entry indexed by the inode
 *	    number in a common parent directory.
 *
 *	(2) If the ip we are linking to is already a hidden entry but is not
 *	    a common parent we have to move its entry to a common parent by
 *	    moving the entry upward.
 *
 *	(3) The trivial case is the entry is already hidden and already a
 *	    common parent.  We adjust nlinks for the entry and are done.
 *	    (this is the fall-through case).
 */
int
hammer2_hardlink_create(hammer2_inode_t *ip, hammer2_inode_t *dip,
			const uint8_t *name, size_t name_len)
{
	return ENOTSUP;
#if 0
	hammer2_inode_t *nip;
	hammer2_inode_t *xip;


       hammer2_inode_t *nip;   /* hardlink forwarding inode */
        error = hammer2_inode_create(hmp, NULL, ap->a_cred,
                                     dip, name, name_len, &nip);
        if (error) {
                KKASSERT(nip == NULL);
                return error;
        }
        KKASSERT(nip->ip_data.type == HAMMER2_OBJTYPE_HARDLINK);
        hammer2_chain_modify(&nip->chain, 0);
        nip->ip_data.inum = ip->ip_data.inum;
	hammer2_chain_unlock(hmp, &nip->chain);
	/
#endif
}

/*
 * Unlink the file from the specified directory inode.  The directory inode
 * does not need to be locked.
 *
 * isdir determines whether a directory/non-directory check should be made.
 * No check is made if isdir is set to -1.
 *
 * adjlinks tells unlink that we want to adjust the nlinks count of the
 * inode.  When removing the last link for a NON forwarding entry we can
 * just ignore the link count... no point updating the inode that we are
 * about to dereference, it would just result in a lot of wasted I/O.
 *
 * However, if the entry is a forwarding entry (aka a hardlink), and adjlinks
 * is non-zero, we have to locate the hardlink and adjust its nlinks field.
 */
int
hammer2_unlink_file(hammer2_inode_t *dip, const uint8_t *name, size_t name_len,
		    int isdir, int adjlinks)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *dparent;
	hammer2_chain_t *dchain;
	hammer2_key_t lhc;
	int error;

	error = 0;

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
	if (chain->data->ipdata.type == HAMMER2_OBJTYPE_DIRECTORY &&
	    isdir == 0) {
		error = ENOTDIR;
		goto done;
	}
	if (chain->data->ipdata.type != HAMMER2_OBJTYPE_DIRECTORY &&
	    isdir == 1) {
		error = EISDIR;
		goto done;
	}

	/*
	 * If this is a directory the directory must be empty.  However, if
	 * isdir < 0 we are doing a rename and the directory does not have
	 * to be empty.
	 */
	if (chain->data->ipdata.type == HAMMER2_OBJTYPE_DIRECTORY &&
	    isdir >= 0) {
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

#if 0
	/*
	 * If adjlinks is non-zero this is a real deletion (otherwise it is
	 * probably a rename).  XXX
	 */
	if (adjlinks) {
		if (chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
			/*hammer2_adjust_hardlink(chain->u.ip, -1);*/
			/* error handling */
		} else {
			waslastlink = 1;
		}
	} else {
		waslastlink = 0;
	}
#endif

	/*
	 * Found, the chain represents the inode.  Remove the parent reference
	 * to the chain.  The chain itself is no longer referenced and will
	 * be marked unmodified by hammer2_chain_delete(), avoiding unnecessary
	 * I/O.
	 */
	hammer2_chain_delete(hmp, parent, chain);
	/* XXX nlinks (hardlink special case) */
	/* XXX nlinks (parent directory) */

#if 0
	/*
	 * Destroy any associated vnode, but only if this was the last
	 * link.  XXX this might not be needed.
	 */
	if (chain->u.ip->vp) {
		struct vnode *vp;
		vp = hammer2_igetv(chain->u.ip, &error);
		if (error == 0) {
			vn_unlock(vp);
			/* hammer2_knote(vp, NOTE_DELETE); */
			cache_inval_vp(vp, CINV_DESTROY);
			vrele(vp);
		}
	}
#endif
	error = 0;

done:
	hammer2_chain_unlock(hmp, chain);
	hammer2_chain_unlock(hmp, parent);

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
