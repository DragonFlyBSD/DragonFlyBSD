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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>

#include "hammer2.h"

#define ZFOFFSET	(-2LL)

/*
 * Last reference to a vnode is going away but it is still cached.
 */
static
int
hammer2_vop_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
#if 0
	struct hammer2_mount *hmp;
#endif

	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(vp);
		return (0);
	}

	return (0);
}

/*
 * Reclaim a vnode so that it can be reused; after the inode is
 * disassociated, the filesystem must manage it alone.
 */
static
int
hammer2_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL)
		return(0);
	hmp = ip->hmp;

	hammer2_inode_lock_ex(ip);
	vp->v_data = NULL;
	ip->vp = NULL;
	hammer2_chain_flush(hmp, &ip->chain, NULL);
	hammer2_inode_unlock_ex(ip);
	hammer2_chain_drop(hmp, &ip->chain);	/* vp ref removed */

	/*
	 * XXX handle background sync when ip dirty, kernel will no longer
	 * notify us regarding this inode because there is no longer a
	 * vnode attached to it.
	 */

	return (0);
}

static
int
hammer2_vop_fsync(struct vop_fsync_args *ap)
{
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);
	hmp = ip->hmp;

	hammer2_inode_lock_ex(ip);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);
	hammer2_chain_flush(hmp, &ip->chain, NULL);
	hammer2_inode_unlock_ex(ip);
	return (0);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	uid_t uid;
	gid_t gid;
	int error;

	uid = hammer2_to_unix_xid(&ip->ip_data.uid);
	gid = hammer2_to_unix_xid(&ip->ip_data.gid);

	error = vop_helper_access(ap, uid, gid, ip->ip_data.mode,
				  ip->ip_data.uflags);
	return (error);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	hmp = ip->hmp;

	hammer2_inode_lock_sh(ip);

	vap->va_fsid = hmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ip->ip_data.inum;
	vap->va_mode = ip->ip_data.mode;
	vap->va_nlink = ip->ip_data.nlinks;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->ip_data.size;
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = ip->ip_data.uflags;
	hammer2_time_to_timespec(ip->ip_data.ctime, &vap->va_ctime);
	hammer2_time_to_timespec(ip->ip_data.mtime, &vap->va_mtime);
	hammer2_time_to_timespec(ip->ip_data.mtime, &vap->va_atime);
	vap->va_gen = 1;
	vap->va_bytes = vap->va_size;
	vap->va_type = hammer2_get_vtype(ip);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ip->ip_data.uid;
	vap->va_gid_uuid = ip->ip_data.gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock_sh(ip);

	return (0);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_inode_t *xip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lkey;
	struct uio *uio;
	off_t *cookies;
	off_t saveoff;
	int cookie_index;
	int ncookies;
	int error;
	int dtype;
	int r;

	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	uio = ap->a_uio;
	saveoff = uio->uio_offset;

	/*
	 * Setup cookies directory entry cookies if requested
	 */
	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
	} else {
		ncookies = -1;
		cookies = NULL;
	}
	cookie_index = 0;

	/*
	 * Handle artificial entries.  To ensure that only positive 64 bit
	 * quantities are returned to userland we always strip off bit 63.
	 * The hash code is designed such that codes 0x0000-0x7FFF are not
	 * used, allowing us to use these codes for articial entries.
	 *
	 * Entry 0 is used for '.' and entry 1 is used for '..'.  Do not
	 * allow '..' to cross the mount point into (e.g.) the super-root.
	 */
	error = 0;
	chain = (void *)(intptr_t)-1;	/* non-NULL early done means not eof */

	if (saveoff == 0) {
		r = vop_write_dirent(&error, uio,
				     ip->ip_data.inum &
					HAMMER2_DIRHASH_USERMSK,
				     DT_DIR, 1, ".");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}
	if (saveoff == 1) {
		if (ip->pip == NULL || ip == hmp->iroot)
			xip = ip;
		else
			xip = ip->pip;

		r = vop_write_dirent(&error, uio,
				     xip->ip_data.inum &
				      HAMMER2_DIRHASH_USERMSK,
				     DT_DIR, 2, "..");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}

	lkey = saveoff | HAMMER2_DIRHASH_VISIBLE;

	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	error = hammer2_chain_lock(hmp, parent);
	if (error) {
		hammer2_chain_put(hmp, parent);
		goto done;
	}
	chain = hammer2_chain_lookup(hmp, &parent, lkey, (hammer2_key_t)-1, 0);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			dtype = hammer2_get_dtype(chain->u.ip);
			saveoff = chain->bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     chain->u.ip->ip_data.inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype, chain->u.ip->ip_data.name_len,
					     chain->u.ip->ip_data.filename);
			if (r)
				break;
			if (cookies)
				cookies[cookie_index] = saveoff;
			++cookie_index;
		} else {
			/* XXX chain error */
			kprintf("bad chain type readdir %d\n",
				chain->bref.type);
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lkey, (hammer2_key_t)-1, 0);
		if (chain) {
			saveoff = (chain->bref.key &
				   HAMMER2_DIRHASH_USERMSK) + 1;
		} else {
			saveoff = (hammer2_key_t)-1;
		}
		if (cookie_index == ncookies)
			break;
	}
	hammer2_chain_put(hmp, parent);
	if (chain)
		hammer2_chain_put(hmp, chain);
done:
	if (ap->a_eofflag)
		*ap->a_eofflag = (chain == NULL);
	uio->uio_offset = saveoff;
	if (error && cookie_index == 0) {
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
	return (error);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct buf *bp;
	struct uio *uio;
	int error;
	int seqcount;
	int bigread;

	/*
	 * Read operations supported on this vnode?
	 */
	vp = ap->a_vp;
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Misc
	 */
	ip = VTOI(vp);
	hmp = ip->hmp;
	uio = ap->a_uio;
	error = 0;

	seqcount = ap->a_ioflag >> 16;
	bigread = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * UIO read loop
	 */
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ip_data.size) {
		hammer2_key_t off_hi;
		int off_lo;
		int n;

		off_hi = uio->uio_offset & ~HAMMER2_LBUFMASK64;
		off_lo = (int)(uio->uio_offset & HAMMER2_LBUFMASK64);

		/* XXX bigread & signal check test */

		error = cluster_read(vp, ip->ip_data.size, off_hi,
				     HAMMER2_LBUFSIZE, HAMMER2_PBUFSIZE,
				     seqcount * BKVASIZE, &bp);
		if (error)
			break;
		n = HAMMER2_LBUFSIZE - off_lo;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->ip_data.size - uio->uio_offset)
			n = (int)(ip->ip_data.size - uio->uio_offset);
		bp->b_flags |= B_AGE;
		uiomove((char *)bp->b_data + off_lo, n, uio);
		bqrelse(bp);
	}
	return (error);
}

static
int
hammer2_vop_write(struct vop_write_args *ap)
{
	thread_t td;
	struct vnode *vp;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct buf *bp;
	struct uio *uio;
	int error;
	int kflags;
	int seqcount;
	int bigwrite;

	/*
	 * Read operations supported on this vnode?
	 */
	vp = ap->a_vp;
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Misc
	 */
	ip = VTOI(vp);
	hmp = ip->hmp;
	uio = ap->a_uio;
	error = 0;
	kflags = 0;
	if (hmp->ronly)
		return (EROFS);

	seqcount = ap->a_ioflag >> 16;
	bigwrite = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * Check resource limit
	 */
	if (uio->uio_resid > 0 && (td = uio->uio_td) != NULL && td->td_proc &&
	    uio->uio_offset + uio->uio_resid >
	     td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}

	bigwrite = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * UIO read loop
	 */
	while (uio->uio_resid > 0) {
		hammer2_key_t nsize;
		hammer2_key_t off_hi;
		int fixsize;
		int off_lo;
		int n;
		int trivial;
		int endofblk;

		off_hi = uio->uio_offset & ~HAMMER2_LBUFMASK64;
		off_lo = (int)(uio->uio_offset & HAMMER2_LBUFMASK64);

		n = HAMMER2_LBUFSIZE - off_lo;
		if (n > uio->uio_resid) {
			n = uio->uio_resid;
			endofblk = 0;
		} else {
			endofblk = 1;
		}
		nsize = uio->uio_offset + n;

		/* XXX bigwrite & signal check test */

		/*
		 * Don't allow the buffer build to blow out the buffer
		 * cache.
		 */
		if ((ap->a_ioflag & IO_RECURSE) == 0)
			bwillwrite(HAMMER2_LBUFSIZE);

		/*
		 * Extend the size of the file as needed
		 * XXX lock.
		 */
		if (nsize > ip->ip_data.size) {
			if (uio->uio_offset > ip->ip_data.size)
				trivial = 0;
			else
				trivial = 1;
			nvextendbuf(vp, ip->ip_data.size, nsize,
				    HAMMER2_LBUFSIZE, HAMMER2_LBUFSIZE,
				    (int)(ip->ip_data.size & HAMMER2_LBUFMASK),
				    (int)(nsize),
				    trivial);
			kflags |= NOTE_EXTEND;
			fixsize = 1;
		} else {
			fixsize = 0;
		}

		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ap->a_vp,
					      off_hi, HAMMER2_LBUFSIZE, &bp);
			}
		} else if (off_lo == 0 && uio->uio_resid >= HAMMER2_LBUFSIZE) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a
			 * mmap/write visibility issue.
			 */
			bp = getblk(vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else if (off_hi >= ip->ip_data.size) {
			/*
			 * If the base offset of the buffer is beyond the
			 * file EOF, we don't have to issue a read.
			 */
			bp = getblk(vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 */
			error = bread(vp, off_hi, HAMMER2_LBUFSIZE, &bp);
			if (error == 0)
				bheavy(bp);
		}

		if (error == 0) {
			/* release lock */
			error = uiomove(bp->b_data + off_lo, n, uio);
			/* acquire lock */
		}

		if (error) {
			brelse(bp);
			if (fixsize) {
				nvtruncbuf(vp, ip->ip_data.size,
					   HAMMER2_LBUFSIZE, HAMMER2_LBUFSIZE);
			}
			break;
		}
		kflags |= NOTE_WRITE;
		if (ip->ip_data.size < uio->uio_offset)
			ip->ip_data.size = uio->uio_offset;
		/* XXX update ino_data.mtime */

		/*
		 * Once we dirty a buffer any cached offset becomes invalid.
		 */
		bp->b_bio2.bio_offset = NOOFFSET;
		bp->b_flags |= B_AGE;
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ap->a_ioflag & IO_DIRECT) && endofblk) {
			bawrite(bp);
		} else if (ap->a_ioflag & IO_ASYNC) {
			bawrite(bp);
		} else {
			bdwrite(bp);
		}
	}
	/* hammer2_knote(vp, kflags); */
	return (error);
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	hammer2_key_t lhc;
	int error = 0;
	struct vnode *vp;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	parent = &dip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);
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
	hammer2_chain_put(hmp, parent);

	if (chain) {
		vp = hammer2_igetv(chain->u.ip, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
		hammer2_chain_put(hmp, chain);
	} else {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
	}
	return error;
}

static
int
hammer2_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	hammer2_mount_t *hmp;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;

	if ((ip = dip->pip) == NULL) {
		*ap->a_vpp = NULL;
		return ENOENT;
	}
	hammer2_chain_ref(hmp, &ip->chain);
	hammer2_chain_lock(hmp, &ip->chain);
	*ap->a_vpp = hammer2_igetv(ip, &error);
	hammer2_chain_put(hmp, &ip->chain);

	return error;
}

static
int
hammer2_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	error = hammer2_create_inode(hmp, ap->a_vap, ap->a_cred,
				     dip, name, name_len, &nip);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);
	hammer2_chain_put(hmp, &nip->chain);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

/*
 * Return the largest contiguous physical disk range for the logical
 * request.
 *
 * (struct vnode *vp, off_t loffset, off_t *doffsetp, int *runp, int *runb)
 */
static
int
hammer2_vop_bmap(struct vop_bmap_args *ap)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t off_hi;

	/*
	 * Only supported on regular files
	 *
	 * Only supported for read operations (required for cluster_read).
	 * The block allocation is delayed for write operations.
	 */
	vp = ap->a_vp;
	if (vp->v_type != VREG)
		return (EOPNOTSUPP);
	if (ap->a_cmd != BUF_CMD_READ)
		return (EOPNOTSUPP);

	ip = VTOI(vp);
	hmp = ip->hmp;
	off_hi = ap->a_loffset & HAMMER2_OFF_MASK_HI;
	KKASSERT((ap->a_loffset & HAMMER2_LBUFMASK64) == 0);

	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);
	chain = hammer2_chain_lookup(hmp, &parent, off_hi, off_hi, 0);
	if (chain) {
		*ap->a_doffsetp = (chain->bref.data_off & ~HAMMER2_LBUFMASK64);
		hammer2_chain_put(hmp, chain);
	} else {
		*ap->a_doffsetp = ZFOFFSET;	/* zero-fill hole */
	}
	hammer2_chain_put(hmp, parent);
	return (0);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
	return vop_stdopen(ap);
}

static
int
hammer2_vop_close(struct vop_close_args *ap)
{
	return vop_stdclose(ap);
}

/*
 * hammer_vop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer2_vop_ncreate(struct vop_ncreate_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	error = hammer2_create_inode(hmp, ap->a_vap, ap->a_cred,
				     dip, name, name_len, &nip);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);
	hammer2_chain_put(hmp, &nip->chain);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

static int hammer2_strategy_read(struct vop_strategy_args *ap);
static int hammer2_strategy_write(struct vop_strategy_args *ap);

static
int
hammer2_vop_strategy(struct vop_strategy_args *ap)
{
	struct bio *biop;
	struct buf *bp;
	int error;

	biop = ap->a_bio;
	bp = biop->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer2_strategy_read(ap);
		break;
	case BUF_CMD_WRITE:
		error = hammer2_strategy_write(ap);
		break;
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(biop);
		break;
	}

	return (error);
}

static
int
hammer2_strategy_read(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct bio *bio;
	struct bio *nbio;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t off_hi;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	nbio = push_bio(bio);

	if (nbio->bio_offset == NOOFFSET) {
		off_hi = bio->bio_offset & HAMMER2_OFF_MASK_HI;
		KKASSERT((bio->bio_offset & HAMMER2_LBUFMASK64) == 0);

		parent = &ip->chain;
		hammer2_chain_ref(hmp, parent);
		hammer2_chain_lock(hmp, parent);

		/*
		 * Specifying NOLOCK avoids unnecessary bread()s of the
		 * chain element's content.  We just need the block device
		 * offset.
		 */
		kprintf("lookup data logical %016jx\n", off_hi);
		chain = hammer2_chain_lookup(hmp, &parent, off_hi, off_hi,
					     HAMMER2_LOOKUP_NOLOCK);
		if (chain) {
			kprintf("lookup success\n");
			nbio->bio_offset = (chain->bref.data_off &
					    ~HAMMER2_LBUFMASK64);
			hammer2_chain_drop(hmp, chain);
		} else {
			kprintf("lookup zero-fill\n");
			nbio->bio_offset = ZFOFFSET;
		}
		hammer2_chain_put(hmp, parent);
	}
	if (nbio->bio_offset == ZFOFFSET) {
		bp->b_resid = 0;
		bp->b_error = 0;
		vfs_bio_clrbuf(bp);
		biodone(nbio);
	} else {
		kprintf("data read %016jx\n", nbio->bio_offset);
		vn_strategy(hmp->devvp, nbio);
	}
	return (0);
}

static
int
hammer2_strategy_write(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct bio *bio;
	struct bio *nbio;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t off_hi;
	int off_lo;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	nbio = push_bio(bio);

	/*
	 * Our bmap doesn't support writes atm, and a vop_write should
	 * clear the physical disk offset cache for the copy-on-write
	 * operation.
	 */
	KKASSERT(nbio->bio_offset == NOOFFSET);

	off_hi = bio->bio_offset & HAMMER2_OFF_MASK_HI;
	off_lo = bio->bio_offset & HAMMER2_OFF_MASK_LO;
	KKASSERT((bio->bio_offset & HAMMER2_LBUFMASK64) == 0);

	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);
	chain = hammer2_chain_lookup(hmp, &parent, off_hi, off_hi, 0);
	if (chain) {
		hammer2_chain_modify(hmp, chain);
		bcopy(bp->b_data, chain->data->buf + off_lo, bp->b_bcount);
		hammer2_chain_put(hmp, chain);
	} else {
		chain = hammer2_chain_create(hmp, parent,
					     off_hi, HAMMER2_PBUFRADIX,
					     HAMMER2_BREF_TYPE_DATA,
					     HAMMER2_PBUFSIZE);
		bcopy(bp->b_data, chain->data->buf + off_lo, bp->b_bcount);
		hammer2_chain_put(hmp, chain);
	}
	hammer2_chain_put(hmp, parent);

	bp->b_resid = 0;
	bp->b_error = 0;
	biodone(nbio);

	return (0);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	struct hammer2_mount *hmp;
	int rc;

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		mp = ap->a_head.a_ops->head.vv_mount;
		hmp = MPTOH2(mp);

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &hmp->export,
					(const struct export_args *)ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}
	return (rc);
}

struct vop_ops hammer2_vnode_vops = {
	.vop_default	= vop_defaultop,
	.vop_fsync	= hammer2_vop_fsync,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_access	= hammer2_vop_access,
	.vop_close	= hammer2_vop_close,
	.vop_ncreate	= hammer2_vop_ncreate,
	.vop_getattr	= hammer2_vop_getattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_read	= hammer2_vop_read,
	.vop_write	= hammer2_vop_write,
	.vop_open	= hammer2_vop_open,
	.vop_inactive	= hammer2_vop_inactive,
	.vop_reclaim 	= hammer2_vop_reclaim,
	.vop_nresolve	= hammer2_vop_nresolve,
	.vop_nlookupdotdot = hammer2_vop_nlookupdotdot,
	.vop_nmkdir 	= hammer2_vop_nmkdir,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};
