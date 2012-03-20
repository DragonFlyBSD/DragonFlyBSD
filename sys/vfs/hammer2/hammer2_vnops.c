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
#include <sys/uio.h>

#include "hammer2.h"

#define ZFOFFSET	(-2LL)

static int hammer2_read_file(hammer2_inode_t *ip, struct uio *uio,
				int seqcount);
static int hammer2_write_file(hammer2_inode_t *ip, struct uio *uio, int ioflag);
static void hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize,
				int trivial);
static void hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize);
static int hammer2_unlink_file(hammer2_inode_t *dip,
				const uint8_t *name, size_t name_len,
				int isdir, int adjlinks);

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
	hammer2_chain_flush(hmp, &ip->chain);
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

	/*
	 * Calling chain_flush here creates a lot of duplicative
	 * COW operations due to non-optimal vnode ordering.
	 *
	 * Only do it for an actual fsync() syscall.  The other forms
	 * which call this function will eventually call chain_flush
	 * on the volume root as a catch-all, which is far more optimal.
	 */
	if (ap->a_flags & VOP_FSYNC_SYSCALL)
		hammer2_chain_flush(hmp, &ip->chain);
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
	vap->va_bytes = vap->va_size;	/* XXX */
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
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	int doctime = 0;
	int domtime = 0;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	hmp = ip->hmp;

	if (hmp->ronly)
		return(EROFS);

	hammer2_inode_lock_ex(ip);
	error = 0;

	if (vap->va_flags != VNOVAL) {
		u_int32_t flags;

		flags = ip->ip_data.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer2_to_unix_xid(&ip->ip_data.uid),
					 ap->a_cred);
		if (error == 0) {
			if (ip->ip_data.uflags != flags) {
				hammer2_chain_modify(hmp, &ip->chain);
				ip->ip_data.uflags = flags;
				doctime = 1;
				kflags |= NOTE_ATTRIB;
			}
			if (ip->ip_data.uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
	}

	if (ip->ip_data.uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	/* uid, gid */

	/*
	 * Resize the file
	 */
	if (vap->va_size != VNOVAL && ip->ip_data.size != vap->va_size) {
		switch(vp->v_type) {
		case VREG:
			if (vap->va_size == ip->ip_data.size)
				break;
			if (vap->va_size < ip->ip_data.size) {
				hammer2_chain_modify(hmp, &ip->chain);
				hammer2_truncate_file(ip, vap->va_size);
				ip->ip_data.size = vap->va_size;
			} else {
				hammer2_chain_modify(hmp, &ip->chain);
				hammer2_extend_file(ip, vap->va_size, 0);
				ip->ip_data.size = vap->va_size;
			}
			domtime = 1;
			break;
		default:
			error = EINVAL;
			goto done;
		}
	}
done:
	hammer2_inode_unlock_ex(ip);
	return (error);
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
	chain = (void *)(intptr_t)-1;	/* non-NULL for early goto done case */

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
	chain = hammer2_chain_lookup(hmp, &parent, lkey, lkey, 0);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(hmp, &parent,
					     lkey, (hammer2_key_t)-1, 0);
	}
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

		/*
		 * Keys may not be returned in order so once we have a
		 * placemarker (chain) the scan must allow the full range
		 * or some entries will be missed.
		 */
		chain = hammer2_chain_next(hmp, &parent, chain,
					   0, (hammer2_key_t)-1, 0);
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
	uio->uio_offset = saveoff & ~HAMMER2_DIRHASH_VISIBLE;
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

/*
 * hammer2_vop_readlink { vp, uio, cred }
 */
static
int
hammer2_vop_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	int error;

	vp = ap->a_vp;
	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);
	hmp = ip->hmp;

	error = hammer2_read_file(ip, ap->a_uio, 0);
	return (error);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
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

	error = hammer2_read_file(ip, uio, seqcount);
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
	struct uio *uio;
	int error;
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
	 * ip must be locked if extending the file.
	 * ip must be locked to avoid racing a truncation.
	 */
	hammer2_inode_lock_ex(ip);
	hammer2_chain_modify(hmp, &ip->chain);
	error = hammer2_write_file(ip, uio, ap->a_ioflag);

	hammer2_inode_unlock_ex(ip);
	return (error);
}

/*
 * Perform read operations on a file or symlink given an UNLOCKED
 * inode and uio.
 */
static
int
hammer2_read_file(hammer2_inode_t *ip, struct uio *uio, int seqcount)
{
	struct buf *bp;
	int error;

	error = 0;

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

		error = cluster_read(ip->vp,
				     ip->ip_data.size, off_hi,
				     HAMMER2_LBUFSIZE,
				     uio->uio_resid, seqcount * BKVASIZE,
				     &bp);
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

/*
 * Called with a locked (ip) to do the underlying write to a file or
 * to build the symlink target.
 */
static
int
hammer2_write_file(hammer2_inode_t *ip, struct uio *uio, int ioflag)
{
	struct buf *bp;
	int kflags;
	int error;

	/*
	 * Setup if append
	 */
	if (ioflag & IO_APPEND)
		uio->uio_offset = ip->ip_data.size;
	kflags = 0;
	error = 0;

	/*
	 * UIO write loop
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
		if ((ioflag & IO_RECURSE) == 0)
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
			hammer2_extend_file(ip, nsize, trivial);
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
			bp = getblk(ip->vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ip->vp, off_hi,
					      HAMMER2_LBUFSIZE, &bp);
			}
		} else if (off_lo == 0 && uio->uio_resid >= HAMMER2_LBUFSIZE) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a
			 * mmap/write visibility issue.
			 */
			bp = getblk(ip->vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else if (off_hi >= ip->ip_data.size) {
			/*
			 * If the base offset of the buffer is beyond the
			 * file EOF, we don't have to issue a read.
			 */
			bp = getblk(ip->vp, off_hi,
				    HAMMER2_LBUFSIZE, GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 */
			error = bread(ip->vp, off_hi, HAMMER2_LBUFSIZE, &bp);
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
			if (fixsize)
				hammer2_truncate_file(ip, ip->ip_data.size);
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
		if (ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ioflag & IO_DIRECT) && endofblk) {
			bawrite(bp);
		} else if (ioflag & IO_ASYNC) {
			bawrite(bp);
		} else {
			bdwrite(bp);
		}
	}
	/* hammer2_knote(ip->vp, kflags); */
	return error;
}

/*
 * Truncate the size of a file.  The inode must be locked and marked
 * for modification.  The caller will set ip->ip_data.size after we
 * return, we do not do it ourselves.
 */
static
void
hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_key_t pkey;
	int error;
	int nradix;
	int nbytes;

	/*
	 * Destroy any logical buffer cache buffers beyond the file EOF
	 * and partially clean out any straddling buffer.
	 */
	if (ip->vp) {
		nvtruncbuf(ip->vp, nsize,
			   HAMMER2_LBUFSIZE, nsize & HAMMER2_LBUFMASK);
	}

	/*
	 * Setup for lookup/next
	 */
	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	error = hammer2_chain_lock(hmp, parent);
	if (error) {
		hammer2_chain_put(hmp, parent);
		/* XXX error reporting */
		return;
	}

	/*
	 * Any straddling logical block will have been dirtied, so the
	 * zeroing of any partial logical data is already handled.
	 *
	 * However any data beyond the logical block size present for the
	 * straddling physical block has to be erased.
	 *
	 * This can be done simply by rewriting the blockref to indicate a
	 * smaller physical allocation.  We do not have to reallocate the
	 * physical block.
	 */
	pkey = nsize & ~HAMMER2_PBUFMASK64;

	/*
	 * Calculate the physical block allocation for the old block
	 * and the new block.
	 *
	 * A radix of 0 is returned if nsize is 0.
	 * The returned radix may be smaller than HAMMER_LBUFSIZE.
	 */
	nradix = hammer2_inode_calc_alloc(nsize);
	nbytes = 1 << nradix;

	if (nbytes) {
		chain = hammer2_chain_lookup(hmp, &parent, pkey, pkey,
					     HAMMER2_LOOKUP_NOLOCK);
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA &&
		    chain->bytes > nbytes) {
			hammer2_chain_lock(hmp, chain);
			hammer2_chain_resize(hmp, chain, nradix);
			hammer2_chain_unlock(hmp, chain);
		}
		hammer2_chain_drop(hmp, chain);
	}

	/*
	 * Destroy any physical blocks after the new EOF point.
	 */
	pkey = (nsize + HAMMER2_PBUFMASK64) & ~HAMMER2_PBUFMASK64;

	chain = hammer2_chain_lookup(hmp, &parent,
				     pkey, (hammer2_key_t)-1,
				     HAMMER2_LOOKUP_NOLOCK);
	while (chain) {
		/*
		 * Degenerate embedded data case, nothing to loop on.
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
			break;

		/*
		 * Delete physical data blocks past the file EOF.
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			hammer2_chain_delete(hmp, parent, chain);
		}
		/* XXX check parent if empty indirect block & delete */
		chain = hammer2_chain_next(hmp, &parent, chain,
					   pkey, (hammer2_key_t)-1,
					   HAMMER2_LOOKUP_NOLOCK);
	}
	hammer2_chain_put(hmp, parent);
}

/*
 * Extend the size of a file.  The inode must be locked and marked
 * for modification.  The caller will set ip->ip_data.size after we
 * return, we do not do it ourselves.
 *
 * We don't bother resizing the block straddling EOF until the
 * strategy write commits a related buffer.  In otherwords,
 * zero-fill is implied.
 */
static
void
hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize, int trivial)
{
	struct buf *bp;
	int error;

	/*
	 * Disable direct-data mode if necessary.  It's better to do this
	 * here than to try to code it in the strategy routine.
	 *
	 * No other action on the physical blockmap for the file is required
	 * when simply resizing a file.  All other actions will be handled by
	 * any necessary block reallocation in the strategy write code.
	 */
	if ((ip->ip_data.op_flags & HAMMER2_OPFLAG_DIRECTDATA) &&
	    nsize > HAMMER2_EMBEDDED_BYTES) {
		error = bread(ip->vp, 0, HAMMER2_LBUFSIZE, &bp);
		KKASSERT(error == 0);
		ip->ip_data.op_flags &= ~HAMMER2_OPFLAG_DIRECTDATA;
		bzero(&ip->ip_data.u.blockset,
		      sizeof(ip->ip_data.u.blockset));
		bdwrite(bp);
	}

	/*
	 * This will fix up the logical buffers
	 */
	if (ip->vp) {
		nvextendbuf(ip->vp, ip->ip_data.size, nsize,
			    HAMMER2_LBUFSIZE, HAMMER2_LBUFSIZE,
			    (int)(ip->ip_data.size & HAMMER2_LBUFMASK),
			    (int)(nsize & HAMMER2_LBUFMASK),
			    trivial);
	}
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

	error = hammer2_inode_create(hmp, ap->a_vap, ap->a_cred,
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
	hammer2_key_t lbeg;
	hammer2_key_t lend;
	hammer2_off_t pbeg;
	hammer2_off_t pbytes;
	hammer2_off_t array[HAMMER2_BMAP_COUNT][2];
	int loff;
	int ai;

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
	bzero(array, sizeof(array));

	/*
	 * Calculate logical range
	 */
	KKASSERT((ap->a_loffset & HAMMER2_LBUFMASK64) == 0);
	lbeg = ap->a_loffset & HAMMER2_OFF_MASK_HI;
	lend = lbeg + HAMMER2_BMAP_COUNT * HAMMER2_PBUFSIZE - 1;
	if (lend < lbeg)
		lend = lbeg;
	loff = ap->a_loffset & HAMMER2_OFF_MASK_LO;

	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);
	chain = hammer2_chain_lookup(hmp, &parent,
				     lbeg, lend,
				     HAMMER2_LOOKUP_NOLOCK);
	if (chain == NULL) {
		*ap->a_doffsetp = ZFOFFSET;
		hammer2_chain_put(hmp, parent);
		return (0);
	}

	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			ai = (chain->bref.key - lbeg) / HAMMER2_PBUFSIZE;
			KKASSERT(ai >= 0 && ai < HAMMER2_BMAP_COUNT);
			array[ai][0] = chain->bref.data_off & HAMMER2_OFF_MASK;
			array[ai][1] = chain->bytes;
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lbeg, lend,
					   HAMMER2_LOOKUP_NOLOCK);
	}
	hammer2_chain_put(hmp, parent);

	/*
	 * If the requested loffset is not mappable physically we can't
	 * bmap.  The caller will have to access the file data via a
	 * device buffer.
	 */
	if (array[0][0] == 0 || array[0][1] < loff + HAMMER2_LBUFSIZE) {
		*ap->a_doffsetp = NOOFFSET;
		return (0);
	}

	/*
	 * Calculate the physical disk offset range for array[0]
	 */
	pbeg = array[0][0] + loff;
	pbytes = array[0][1] - loff;

	for (ai = 1; ai < HAMMER2_BMAP_COUNT; ++ai) {
		if (array[ai][0] != pbeg + pbytes)
			break;
		pbytes += array[ai][1];
	}

	*ap->a_doffsetp = pbeg;
	if (ap->a_runp)
		*ap->a_runp = pbytes;
	return (0);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
	return vop_stdopen(ap);
}

/*
 * hammer2_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer2_vop_advlock(struct vop_advlock_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &ip->advlock, ip->ip_data.size));
}


static
int
hammer2_vop_close(struct vop_close_args *ap)
{
	return vop_stdclose(ap);
}

/*
 * hammer2_vop_nlink { nch, dvp, vp, cred }
 *
 * Create a hardlink to vp.
 */
static
int
hammer2_vop_nlink(struct vop_nlink_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	hammer2_mount_t *hmp;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return (EROFS);

	ip = VTOI(ap->a_vp);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	error = hammer2_hardlink_create(ip, dip, name, name_len);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
	return error;
}

/*
 * hammer2_vop_ncreate { nch, dvp, vpp, cred, vap }
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

	error = hammer2_inode_create(hmp, ap->a_vap, ap->a_cred,
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
 * hammer2_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer2_vop_nsymlink(struct vop_nsymlink_args *ap)
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

	ap->a_vap->va_type = VLNK;	/* enforce type */

	error = hammer2_inode_create(hmp, ap->a_vap, ap->a_cred,
				     dip, name, name_len, &nip);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);

	/*
	 * Build the softlink (~like file data) and finalize the namecache.
	 */
	if (error == 0) {
		size_t bytes;
		struct uio auio;
		struct iovec aiov;

		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER2_EMBEDDED_BYTES) {
			KKASSERT(nip->ip_data.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			bcopy(ap->a_target, nip->ip_data.u.data, bytes);
			nip->ip_data.size = bytes;
		} else {
			bzero(&auio, sizeof(auio));
			bzero(&aiov, sizeof(aiov));
			auio.uio_iov = &aiov;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_resid = bytes;
			auio.uio_iovcnt = 1;
			auio.uio_td = curthread;
			aiov.iov_base = ap->a_target;
			aiov.iov_len = bytes;
			error = hammer2_write_file(nip, &auio, IO_APPEND);
			/* XXX handle error */
			error = 0;
		}
	}
	hammer2_chain_put(hmp, &nip->chain);

	/*
	 * Finalize namecache
	 */
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		/* hammer2_knote(ap->a_dvp, NOTE_WRITE); */
	}
	return error;
}

/*
 * hammer2_vop_nremove { nch, dvp, cred }
 */
static
int
hammer2_vop_nremove(struct vop_nremove_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_mount_t *hmp;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return(EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	error = hammer2_unlink_file(dip, name, name_len, 0, 1);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, NULL);
	}
	return (error);
}

/*
 * hammer2_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer2_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_mount_t *hmp;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return(EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	error = hammer2_unlink_file(dip, name, name_len, 1, 1);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, NULL);
	}
	return (error);
}

/*
 * hammer2_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer2_vop_nrename(struct vop_nrename_args *ap)
{
	struct namecache *fncp;
	struct namecache *tncp;
	hammer2_inode_t *fdip;
	hammer2_inode_t *tdip;
	hammer2_inode_t *ip;
	hammer2_mount_t *hmp;
	const uint8_t *fname;
	size_t fname_len;
	const uint8_t *tname;
	size_t tname_len;
	int error;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);	/* source directory */
	tdip = VTOI(ap->a_tdvp);	/* target directory */

	hmp = fdip->hmp;		/* check read-only filesystem */
	if (hmp->ronly)
		return(EROFS);

	fncp = ap->a_fnch->ncp;		/* entry name in source */
	fname = fncp->nc_name;
	fname_len = fncp->nc_nlen;

	tncp = ap->a_tnch->ncp;		/* entry name in target */
	tname = tncp->nc_name;
	tname_len = tncp->nc_nlen;

	ip = VTOI(fncp->nc_vp);		/* inode being moved */

	/*
	 * Keep a tight grip on the inode as removing it should disconnect
	 * it and we don't want to destroy it.
	 */
	hammer2_chain_ref(hmp, &ip->chain);
	hammer2_chain_lock(hmp, &ip->chain);

	/*
	 * Remove target if it exists
	 */
	error = hammer2_unlink_file(tdip, tname, tname_len, -1, 1);
	if (error && error != ENOENT)
		goto done;
	cache_setunresolved(ap->a_tnch);
	cache_setvp(ap->a_tnch, NULL);

	/*
	 * Disconnect ip from the source directory, do not adjust
	 * the link count.  Note that rename doesn't need to understand
	 * whether this is a hardlink or not, we can just rename the
	 * forwarding entry and don't even have to adjust the related
	 * hardlink's link count.
	 */
	error = hammer2_unlink_file(fdip, fname, fname_len, -1, 0);
	if (error)
		goto done;

	if (ip->chain.parent != NULL)
		panic("hammer2_vop_nrename(): rename source != ip!");

	/*
	 * Reconnect ip to target directory.
	 */
	error = hammer2_inode_connect(tdip, ip, tname, tname_len);

	if (error == 0) {
		cache_rename(ap->a_fnch, ap->a_tnch);
	}
done:
	hammer2_chain_unlock(hmp, &ip->chain);
	hammer2_chain_drop(hmp, &ip->chain);

	return (error);
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
static
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

	/*
	 * Not found or wrong type (isdir < 0 disables the type check).
	 */
	if (chain == NULL) {
		hammer2_chain_put(hmp, parent);
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
		hammer2_chain_ref(hmp, dparent);
		hammer2_chain_lock(hmp, dparent);
		dchain = hammer2_chain_lookup(hmp, &dparent,
					      0, (hammer2_key_t)-1,
					      HAMMER2_LOOKUP_NOLOCK);
		if (dchain) {
			hammer2_chain_drop(hmp, dchain);
			hammer2_chain_put(hmp, dparent);
			error = ENOTEMPTY;
			goto done;
		}
		hammer2_chain_put(hmp, dparent);
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
	hammer2_chain_put(hmp, chain);
	hammer2_chain_put(hmp, parent);

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
	hammer2_key_t pkey;
	int poff;
	int ddlen = 0;		/* direct data shortcut */
	char *ddata = NULL;
	int didlock;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	nbio = push_bio(bio);

	KKASSERT((bio->bio_offset & HAMMER2_LBUFMASK64) == 0);
	pkey = bio->bio_offset & HAMMER2_OFF_MASK_HI;
	poff = bio->bio_offset & HAMMER2_OFF_MASK_LO;
	chain = NULL;
	didlock = 0;

	/*
	 * We must characterize the logical->physical translation if it
	 * has not already been cached.
	 *
	 * Physical data references < LBUFSIZE are never cached.  This
	 * includes both small-block allocations and inode-embedded data.
	 */
	if (nbio->bio_offset == NOOFFSET) {
		parent = &ip->chain;
		hammer2_chain_ref(hmp, parent);
		hammer2_chain_lock(hmp, parent);

		/*
		 * Specifying NOLOCK avoids unnecessary bread()s of the
		 * chain element's content.  We just need the block device
		 * offset.
		 */
		chain = hammer2_chain_lookup(hmp, &parent, pkey, pkey,
					     HAMMER2_LOOKUP_NOLOCK);
		if (chain == NULL) {
			/*
			 * Data is zero-fill
			 */
			nbio->bio_offset = ZFOFFSET;
		} else if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			/*
			 * Data is embedded in the inode
			 *
			 * leave nbio->bio_offset set to NOOFFSET
			 */
			ddata = chain->data->ipdata.u.data;
			ddlen = HAMMER2_EMBEDDED_BYTES;
			KKASSERT(chain == parent);
		} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			/*
			 * Data is on-media
			 *
			 * We can set nbio->bio_offset only if the entire
			 * request can be handled by the chain element.  That
			 * is, the chain element's storage size is at least
			 * the size of the IO.  Hopefully this will also
			 * read-ahead.
			 *
			 * For now double-buffer reads for small files in
			 * order to take advantage of clustering in the
			 * physical device layer.
			 */
			if (chain->bytes >= bp->b_bcount &&
			    ip->ip_data.size >= HAMMER2_PBUFSIZE / 2) {
				/*
				 * Direct read
				 */
				nbio->bio_offset = (chain->bref.data_off &
						    HAMMER2_OFF_MASK) + poff;
			} else {
				/*
				 * Double-buffer through physical buffer
				 */
				hammer2_chain_lock(hmp, chain);
				ddata = chain->data->buf;
				ddlen = chain->bytes;
				didlock = 1;
			}
		} else {
			panic("hammer2_strategy_read: unknown bref type");
		}
		hammer2_chain_put(hmp, parent);
	}

	if (nbio->bio_offset == ZFOFFSET) {
		/*
		 * Data is zero-fill
		 */
		bp->b_resid = 0;
		bp->b_error = 0;
		vfs_bio_clrbuf(bp);
		biodone(nbio);
	} else if (nbio->bio_offset != NOOFFSET) {
		/*
		 * Direct IO is possible
		 */
		if (chain) {
			hammer2_chain_drop(hmp, chain);
			chain = NULL;
		}
		vn_strategy(hmp->devvp, nbio);
	} else if (ddata) {
		/*
		 * We can immediately supply the data
		 */
		if (poff >= ddlen) {
			bzero(bp->b_data, bp->b_bcount);
		} else {
			ddlen -= poff;
			ddata += poff;

			if (ddlen > bp->b_bcount)
				ddlen = bp->b_bcount;
			bcopy(ddata, bp->b_data, ddlen);
			if (ddlen < bp->b_bcount)
				bzero(bp->b_data + ddlen, bp->b_bcount - ddlen);
		}
		bp->b_resid = 0;
		bp->b_error = 0;
		biodone(nbio);
	} else {
		panic("hammer2_strategy_read: illegal state");
	}

	if (chain) {
		if (didlock)
			hammer2_chain_unlock(hmp, chain);
		hammer2_chain_drop(hmp, chain);
		chain = NULL;
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
	hammer2_key_t pkey;
	hammer2_key_t pkey_eof;
	int poff;
	int radix;
	char *ddata = NULL;
	size_t ddlen = 0;

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
	KKASSERT((bio->bio_offset & HAMMER2_LBUFMASK64) == 0);
	KKASSERT(nbio->bio_offset == NOOFFSET);

	pkey = bio->bio_offset & HAMMER2_OFF_MASK_HI;
	pkey_eof = ip->ip_data.size & HAMMER2_OFF_MASK_HI;
	poff = bio->bio_offset & HAMMER2_OFF_MASK_LO;

	if (pkey < pkey_eof)
		radix = HAMMER2_PBUFRADIX;
	else
		radix = hammer2_inode_calc_alloc(ip->ip_data.size);

	/*
	 * Locate the physical block
	 */
	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);

	/*
	 * XXX implement NODATA flag to avoid instantiating bp if
	 * it isn't already present for direct-write implementation.
	 */
	chain = hammer2_chain_lookup(hmp, &parent, pkey, pkey, 0);

	/*
	 * Allocate a zero-fill block, resize an existing block if necessary.
	 * Modify the chain element to reallocate the block.
	 */
	if (chain == NULL) {
		/*
		 * Allocate a new chain as necessary
		 */
		KKASSERT(radix > 0);
		chain = hammer2_chain_create(hmp, parent, NULL,
					     pkey, HAMMER2_PBUFRADIX,
					     HAMMER2_BREF_TYPE_DATA,
					     (size_t)1 << radix);
		ddata = chain->data->buf;
		ddlen = chain->bytes;
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		/*
		 * The data is embedded in the inode
		 */
		hammer2_chain_modify(hmp, chain);
		ddata = chain->data->ipdata.u.data;
		ddlen = HAMMER2_EMBEDDED_BYTES;
	} else if (chain->bytes != 1 << radix) {
		/*
		 * If the existing physical allocation is not properly
		 * sized we have to resize it.
		 */
		hammer2_chain_resize(hmp, chain, radix);
		hammer2_chain_modify(hmp, chain);
		ddata = chain->data->buf;
		ddlen = chain->bytes;
	} else {
		hammer2_chain_modify(hmp, chain);
		ddata = chain->data->buf;
		ddlen = chain->bytes;
	}

	if (poff >= ddlen) {
		/* lbuf beyond file EOF?  Do nothing */
	} else {
		ddlen -= poff;
		ddata += poff;

		if (ddlen > bp->b_bcount)	/* urmm.  partial beyond EOF? */
			ddlen = bp->b_bcount;
		bcopy(bp->b_data, ddata, ddlen);
		if (ddlen < bp->b_bcount)
			;/* urmm.  Shouldn't be possible, LBUF < PBUF */
	}

	if (poff + bp->b_bcount == HAMMER2_PBUFSIZE)
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_IOFLUSH);

	hammer2_chain_put(hmp, chain);
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
	.vop_advlock	= hammer2_vop_advlock,
	.vop_close	= hammer2_vop_close,
	.vop_nlink	= hammer2_vop_nlink,
	.vop_ncreate	= hammer2_vop_ncreate,
	.vop_nsymlink	= hammer2_vop_nsymlink,
	.vop_nremove	= hammer2_vop_nremove,
	.vop_nrmdir	= hammer2_vop_nrmdir,
	.vop_nrename	= hammer2_vop_nrename,
	.vop_getattr	= hammer2_vop_getattr,
	.vop_setattr	= hammer2_vop_setattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_readlink	= hammer2_vop_readlink,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
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
