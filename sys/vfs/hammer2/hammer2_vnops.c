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
static int hammer2_write_file(hammer2_inode_t *ip, struct uio *uio, int ioflag,
			      int seqcount);
static hammer2_off_t hammer2_assign_physical(hammer2_inode_t *ip,
				hammer2_key_t lbase, int lblksize, int *errorp);
static void hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize);
static void hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize);

static __inline
void
hammer2_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}

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

	/*
	 * Detect updates to the embedded data which may be synchronized by
	 * the strategy code.  Simply mark the inode modified so it gets
	 * picked up by our normal flush.
	 */
	if (ip->flags & HAMMER2_INODE_DIRTYEMBED) {
		hammer2_inode_lock_ex(ip);
		atomic_clear_int(&ip->flags, HAMMER2_INODE_DIRTYEMBED);
		hammer2_chain_modify(ip->hmp, ip->chain, 0);
		hammer2_inode_unlock_ex(ip);
	}

	/*
	 * Check for deleted inodes and recycle immediately.
	 */
	if (ip->flags & HAMMER2_INODE_DELETED) {
		vrecycle(vp);
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

	/*
	 * Set SUBMODIFIED so we can detect and propagate the DESTROYED
	 * bit in the flush code.
	 */
	hammer2_inode_lock_ex(ip);
	vp->v_data = NULL;
	ip->vp = NULL;
	if (ip->flags & HAMMER2_INODE_DELETED) {
		KKASSERT(ip->chain->flags & HAMMER2_CHAIN_DELETED);
		atomic_set_int(&ip->chain->flags, HAMMER2_CHAIN_DESTROYED |
						 HAMMER2_CHAIN_SUBMODIFIED);
	}
	hammer2_chain_flush(hmp, ip->chain, 0);
	if (ip->refs > 2)			/* (our lock + vp ref) */
		hammer2_inode_unlock_ex(ip);	/* unlock */
	else
		hammer2_inode_put(ip);		/* unlock & disconnect */
	hammer2_inode_drop(ip);			/* vp ref */

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
	 * Detect updates to the embedded data which may be synchronized by
	 * the strategy code.  Simply mark the inode modified so it gets
	 * picked up by our normal flush.
	 */
	if (ip->flags & HAMMER2_INODE_DIRTYEMBED) {
		atomic_clear_int(&ip->flags, HAMMER2_INODE_DIRTYEMBED);
		hammer2_chain_modify(hmp, ip->chain, 0);
	}

	/*
	 * Calling chain_flush here creates a lot of duplicative
	 * COW operations due to non-optimal vnode ordering.
	 *
	 * Only do it for an actual fsync() syscall.  The other forms
	 * which call this function will eventually call chain_flush
	 * on the volume root as a catch-all, which is far more optimal.
	 */
	if (ap->a_flags & VOP_FSYNC_SYSCALL)
		hammer2_chain_flush(hmp, ip->chain, 0);
	hammer2_inode_unlock_ex(ip);
	return (0);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	hammer2_inode_data_t *ipdata;
	uid_t uid;
	gid_t gid;
	int error;

	hammer2_inode_lock_sh(ip);
	ipdata = &ip->chain->data->ipdata;
	uid = hammer2_to_unix_xid(&ipdata->uid);
	gid = hammer2_to_unix_xid(&ipdata->gid);
	error = vop_helper_access(ap, uid, gid, ipdata->mode, ipdata->uflags);
	hammer2_inode_unlock_sh(ip);

	return (error);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	hammer2_inode_data_t *ipdata;
	hammer2_pfsmount_t *pmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	pmp = ip->pmp;

	hammer2_inode_lock_sh(ip);
	ipdata = &ip->chain->data->ipdata;

	vap->va_fsid = pmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ipdata->inum;
	vap->va_mode = ipdata->mode;
	vap->va_nlink = ipdata->nlinks;
	vap->va_uid = hammer2_to_unix_xid(&ipdata->uid);
	vap->va_gid = hammer2_to_unix_xid(&ipdata->gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ipdata->size;
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = ipdata->uflags;
	hammer2_time_to_timespec(ipdata->ctime, &vap->va_ctime);
	hammer2_time_to_timespec(ipdata->mtime, &vap->va_mtime);
	hammer2_time_to_timespec(ipdata->mtime, &vap->va_atime);
	vap->va_gen = 1;
	vap->va_bytes = vap->va_size;	/* XXX */
	vap->va_type = hammer2_get_vtype(ip->chain);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ipdata->uid;
	vap->va_gid_uuid = ipdata->gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock_sh(ip);

	return (0);
}

static
int
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	int domtime = 0;
	uint64_t ctime;

	vp = ap->a_vp;
	vap = ap->a_vap;
	hammer2_update_time(&ctime);

	ip = VTOI(vp);
	hmp = ip->hmp;

	if (hmp->ronly)
		return(EROFS);

	hammer2_inode_lock_ex(ip);
	ipdata = &ip->chain->data->ipdata;
	error = 0;

	if (vap->va_flags != VNOVAL) {
		u_int32_t flags;

		flags = ipdata->uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer2_to_unix_xid(&ipdata->uid),
					 ap->a_cred);
		if (error == 0) {
			if (ipdata->uflags != flags) {
				hammer2_chain_modify(hmp, ip->chain, 0);
				ipdata->uflags = flags;
				ipdata->ctime = ctime;
				kflags |= NOTE_ATTRIB;
			}
			if (ipdata->uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ipdata->uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ipdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ipdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ipdata->gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer2_guid_to_uuid(&uuid_uid, cur_uid);
			hammer2_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ipdata->uid, sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ipdata->gid, sizeof(uuid_gid)) ||
			    ipdata->mode != cur_mode
			) {
				hammer2_chain_modify(hmp, ip->chain, 0);
				ipdata->uid = uuid_uid;
				ipdata->gid = uuid_gid;
				ipdata->mode = cur_mode;
				ipdata->ctime = ctime;
			}
			kflags |= NOTE_ATTRIB;
		}
	}

	/*
	 * Resize the file
	 */
	if (vap->va_size != VNOVAL && ipdata->size != vap->va_size) {
		switch(vp->v_type) {
		case VREG:
			if (vap->va_size == ipdata->size)
				break;
			if (vap->va_size < ipdata->size) {
				hammer2_truncate_file(ip, vap->va_size);
			} else {
				hammer2_extend_file(ip, vap->va_size);
			}
			domtime = 1;
			break;
		default:
			error = EINVAL;
			goto done;
		}
	}
#if 0
	/* atime not supported */
	if (vap->va_atime.tv_sec != VNOVAL) {
		hammer2_chain_modify(hmp, ip->chain, 0);
		ipdata->atime = hammer2_timespec_to_time(&vap->va_atime);
		kflags |= NOTE_ATTRIB;
	}
#endif
	if (vap->va_mtime.tv_sec != VNOVAL) {
		hammer2_chain_modify(hmp, ip->chain, 0);
		ipdata->mtime = hammer2_timespec_to_time(&vap->va_mtime);
		kflags |= NOTE_ATTRIB;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t cur_mode = ipdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ipdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ipdata->gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ipdata->mode != cur_mode) {
			ipdata->mode = cur_mode;
			ipdata->ctime = ctime;
			kflags |= NOTE_ATTRIB;
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
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_inode_t *xip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_tid_t inum;
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

	hammer2_inode_lock_sh(ip);
	ipdata = &ip->chain->data->ipdata;

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
		inum = ipdata->inum & HAMMER2_DIRHASH_USERMSK;
		r = vop_write_dirent(&error, uio, inum, DT_DIR, 1, ".");
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
		/*
		 * Be careful with lockorder when accessing ".."
		 */
		inum = ip->chain->data->ipdata.inum & HAMMER2_DIRHASH_USERMSK;
		while (ip->pip != NULL && ip != ip->pmp->iroot) {
			xip = ip->pip;
			hammer2_inode_ref(xip);
			hammer2_inode_unlock_sh(ip);
			hammer2_inode_lock_sh(xip);
			hammer2_inode_lock_sh(ip);
			hammer2_inode_drop(xip);
			if (xip == ip->pip) {
				inum = xip->chain->data->ipdata.inum &
				       HAMMER2_DIRHASH_USERMSK;
				hammer2_inode_unlock_sh(xip);
				break;
			}
			hammer2_inode_unlock_sh(xip);
		}
		r = vop_write_dirent(&error, uio, inum, DT_DIR, 2, "..");
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

	parent = ip->chain;
	error = hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS |
						HAMMER2_RESOLVE_SHARED);
	if (error) {
		goto done;
	}
	chain = hammer2_chain_lookup(hmp, &parent, lkey, lkey,
				     HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(hmp, &parent,
					     lkey, (hammer2_key_t)-1,
					     HAMMER2_LOOKUP_SHARED);
	}
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			dtype = hammer2_get_dtype(chain);
			saveoff = chain->bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     chain->data->ipdata.inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype,
					     chain->data->ipdata.name_len,
					     chain->data->ipdata.filename);
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
					   HAMMER2_DIRHASH_VISIBLE,
					   (hammer2_key_t)-1,
					   HAMMER2_LOOKUP_SHARED);
		if (chain) {
			saveoff = (chain->bref.key &
				   HAMMER2_DIRHASH_USERMSK) + 1;
		} else {
			saveoff = (hammer2_key_t)-1;
		}
		if (cookie_index == ncookies)
			break;
	}
	if (chain)
		hammer2_chain_unlock(hmp, chain);
	hammer2_chain_unlock(hmp, parent);
done:
	hammer2_inode_unlock_sh(ip);
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
	 *
	 * ip must be marked modified, particularly because the write
	 * might wind up being copied into the embedded data area.
	 */
	hammer2_inode_lock_ex(ip);
	error = hammer2_write_file(ip, uio, ap->a_ioflag, seqcount);
	hammer2_inode_unlock_ex(ip);
	return (error);
}

/*
 * Perform read operations on a file or symlink given an UNLOCKED
 * inode and uio.
 *
 * The passed ip is not locked.
 */
static
int
hammer2_read_file(hammer2_inode_t *ip, struct uio *uio, int seqcount)
{
	hammer2_inode_data_t *ipdata;
	struct buf *bp;
	int error;

	error = 0;

	/*
	 * UIO read loop
	 */
	hammer2_inode_lock_sh(ip);
	ipdata = &ip->chain->data->ipdata;
	while (uio->uio_resid > 0 && uio->uio_offset < ipdata->size) {
		hammer2_key_t lbase;
		hammer2_key_t leof;
		int lblksize;
		int loff;
		int n;

		lblksize = hammer2_calc_logical(ip, uio->uio_offset,
						&lbase, &leof);

		error = cluster_read(ip->vp, leof, lbase, lblksize,
				     uio->uio_resid, seqcount * BKVASIZE,
				     &bp);

		if (error)
			break;
		loff = (int)(uio->uio_offset - lbase);
		n = lblksize - loff;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ipdata->size - uio->uio_offset)
			n = (int)(ipdata->size - uio->uio_offset);
		bp->b_flags |= B_AGE;
		hammer2_inode_unlock_sh(ip);
		uiomove((char *)bp->b_data + loff, n, uio);
		bqrelse(bp);
		hammer2_inode_lock_sh(ip);
		ipdata = &ip->chain->data->ipdata;	/* reload */
	}
	hammer2_inode_unlock_sh(ip);
	return (error);
}

/*
 * Called with a locked (ip) to do the underlying write to a file or
 * to build the symlink target.
 */
static
int
hammer2_write_file(hammer2_inode_t *ip, struct uio *uio,
		   int ioflag, int seqcount)
{
	hammer2_inode_data_t *ipdata;
	hammer2_key_t old_eof;
	struct buf *bp;
	int kflags;
	int error;
	int modified = 0;

	/*
	 * Setup if append
	 */
	ipdata = &ip->chain->data->ipdata;
	if (ioflag & IO_APPEND)
		uio->uio_offset = ipdata->size;
	kflags = 0;
	error = 0;

	/*
	 * Extend the file if necessary.  If the write fails at some point
	 * we will truncate it back down to cover as much as we were able
	 * to write.
	 *
	 * Doing this now makes it easier to calculate buffer sizes in
	 * the loop.
	 */
	old_eof = ipdata->size;
	if (uio->uio_offset + uio->uio_resid > ipdata->size) {
		modified = 1;
		hammer2_extend_file(ip, uio->uio_offset + uio->uio_resid);
		kflags |= NOTE_EXTEND;
	}

	/*
	 * UIO write loop
	 */
	while (uio->uio_resid > 0) {
		hammer2_key_t lbase;
		hammer2_key_t leof;
		int trivial;
		int lblksize;
		int loff;
		int n;

		/*
		 * Don't allow the buffer build to blow out the buffer
		 * cache.
		 */
		if ((ioflag & IO_RECURSE) == 0) {
			/*
			 * XXX should try to leave this unlocked through
			 *	the whole loop
			 */
			hammer2_inode_unlock_ex(ip);
			bwillwrite(HAMMER2_PBUFSIZE);
			hammer2_inode_lock_ex(ip);
			ipdata = &ip->chain->data->ipdata; /* reload */
		}

		/* XXX bigwrite & signal check test */

		/*
		 * This nominally tells us how much we can cluster and
		 * what the logical buffer size needs to be.  Currently
		 * we don't try to cluster the write and just handle one
		 * block at a time.
		 */
		lblksize = hammer2_calc_logical(ip, uio->uio_offset,
						&lbase, &leof);
		loff = (int)(uio->uio_offset - lbase);

		/*
		 * Calculate bytes to copy this transfer and whether the
		 * copy completely covers the buffer or not.
		 */
		trivial = 0;
		n = lblksize - loff;
		if (n > uio->uio_resid) {
			n = uio->uio_resid;
			if (uio->uio_offset + n == ipdata->size)
				trivial = 1;
		} else if (loff == 0) {
			trivial = 1;
		}

		/*
		 * Get the buffer
		 */
		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(ip->vp, lbase, lblksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ip->vp, lbase, lblksize, &bp);
			}
		} else if (trivial) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a
			 * mmap/write visibility issue.
			 */
			bp = getblk(ip->vp, lbase, lblksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 *
			 * (The strategy code will detect zero-fill physical
			 * blocks for this case).
			 */
			error = bread(ip->vp, lbase, lblksize, &bp);
			if (error == 0)
				bheavy(bp);
		}

		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * We have to assign physical storage to the buffer we intend
		 * to dirty or write now to avoid deadlocks in the strategy
		 * code later.
		 *
		 * This can return NOOFFSET for inode-embedded data.  The
		 * strategy code will take care of it in that case.
		 */
		bp->b_bio2.bio_offset =
			hammer2_assign_physical(ip, lbase, lblksize, &error);
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Ok, copy the data in
		 */
		hammer2_inode_unlock_ex(ip);
		error = uiomove(bp->b_data + loff, n, uio);
		hammer2_inode_lock_ex(ip);
		ipdata = &ip->chain->data->ipdata;	/* reload */
		kflags |= NOTE_WRITE;
		modified = 1;

		if (error) {
			brelse(bp);
			break;
		}

		/* XXX update ip_data.mtime */

		/*
		 * Once we dirty a buffer any cached offset becomes invalid.
		 *
		 * NOTE: For cluster_write() always use the trailing block
		 *	 size, which is HAMMER2_PBUFSIZE.  lblksize is the
		 *	 eof-straddling blocksize and is incorrect.
		 */
		bp->b_flags |= B_AGE;
		if (ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ioflag & IO_DIRECT) && loff + n == lblksize) {
			if (bp->b_bcount == HAMMER2_PBUFSIZE)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		} else if (ioflag & IO_ASYNC) {
			bawrite(bp);
		} else if (hammer2_cluster_enable) {
			if (bp->b_bcount == HAMMER2_PBUFSIZE)
				bp->b_flags |= B_CLUSTEROK;
			cluster_write(bp, leof, HAMMER2_PBUFSIZE, seqcount);
		} else {
			if (bp->b_bcount == HAMMER2_PBUFSIZE)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
	}

	/*
	 * Cleanup.  If we extended the file EOF but failed to write through
	 * the entire write is a failure and we have to back-up.
	 */
	if (error && ipdata->size != old_eof) {
		hammer2_truncate_file(ip, old_eof);
	} else if (modified) {
		hammer2_chain_modify(ip->hmp, ip->chain, 0);
		hammer2_update_time(&ipdata->mtime);
	}
	hammer2_knote(ip->vp, kflags);
	return error;
}

/*
 * Assign physical storage to a logical block.
 *
 * NOOFFSET is returned if the data is inode-embedded.  In this case the
 * strategy code will simply bcopy() the data into the inode.
 *
 * The inode's delta_dcount is adjusted.
 */
static
hammer2_off_t
hammer2_assign_physical(hammer2_inode_t *ip, hammer2_key_t lbase,
			int lblksize, int *errorp)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_off_t pbase;

	/*
	 * Locate the chain associated with lbase, return a locked chain.
	 * However, do not instantiate any data reference (which utilizes a
	 * device buffer) because we will be using direct IO via the
	 * logical buffer cache buffer.
	 */
	hmp = ip->hmp;
	*errorp = 0;
	hammer2_inode_lock_ex(ip);
retry:
	parent = ip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);

	chain = hammer2_chain_lookup(hmp, &parent,
				     lbase, lbase,
				     HAMMER2_LOOKUP_NODATA);

	if (chain == NULL) {
		/*
		 * We found a hole, create a new chain entry.
		 *
		 * NOTE: DATA chains are created without device backing
		 *	 store (nor do we want any).
		 */
		chain = hammer2_chain_create(hmp, parent, NULL,
					     lbase, HAMMER2_PBUFRADIX,
					     HAMMER2_BREF_TYPE_DATA,
					     lblksize, errorp);
		if (chain == NULL) {
			KKASSERT(*errorp == EAGAIN); /* XXX */
			hammer2_chain_unlock(hmp, parent);
			goto retry;
		}

		pbase = chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX;
		/*ip->delta_dcount += lblksize;*/
	} else {
		switch (chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * The data is embedded in the inode.  The
			 * caller is responsible for marking the inode
			 * modified and copying the data to the embedded
			 * area.
			 */
			pbase = NOOFFSET;
			break;
		case HAMMER2_BREF_TYPE_DATA:
			if (chain->bytes != lblksize) {
				panic("hammer2_assign_physical: "
				      "size mismatch %d/%d\n",
				      lblksize, chain->bytes);
			}
			hammer2_chain_modify(hmp, chain,
					     HAMMER2_MODIFY_OPTDATA);
			pbase = chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX;
			break;
		default:
			panic("hammer2_assign_physical: bad type");
			/* NOT REACHED */
			pbase = NOOFFSET;
			break;
		}
	}

	if (chain)
		hammer2_chain_unlock(hmp, chain);
	hammer2_chain_unlock(hmp, parent);

	return (pbase);
}

/*
 * Truncate the size of a file.
 *
 * This routine adjusts ipdata->size smaller, destroying any related
 * data beyond the new EOF and potentially resizing the block straddling
 * the EOF.
 *
 * The inode must be locked.
 */
static
void
hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_mount_t *hmp = ip->hmp;
	hammer2_key_t lbase;
	hammer2_key_t leof;
	struct buf *bp;
	int loff;
	int error;
	int oblksize;
	int nblksize;

	hammer2_chain_modify(hmp, ip->chain, 0);
	bp = NULL;
	ipdata = &ip->chain->data->ipdata;

	/*
	 * Destroy any logical buffer cache buffers beyond the file EOF.
	 *
	 * We call nvtruncbuf() w/ trivial == 1 to prevent it from messing
	 * around with the buffer straddling EOF, because we need to assign
	 * a new physical offset to it.
	 */
	if (ip->vp) {
		nvtruncbuf(ip->vp, nsize,
			   HAMMER2_PBUFSIZE, (int)nsize & HAMMER2_PBUFMASK,
			   1);
	}

	/*
	 * Setup for lookup/search
	 */
	parent = ip->chain;
	error = hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
	if (error) {
		hammer2_chain_unlock(hmp, parent);
		/* XXX error reporting */
		return;
	}

	/*
	 * Handle the case where a chain/logical-buffer straddles the new
	 * EOF.  We told nvtruncbuf() above not to mess with the logical
	 * buffer straddling the EOF because we need to reassign its storage
	 * and can't let the strategy code do it for us.
	 */
	loff = (int)nsize & HAMMER2_PBUFMASK;
	if (loff && ip->vp) {
		oblksize = hammer2_calc_logical(ip, nsize, &lbase, &leof);
		error = bread(ip->vp, lbase, oblksize, &bp);
		KKASSERT(error == 0);
	}
	ipdata->size = nsize;
	nblksize = hammer2_calc_logical(ip, nsize, &lbase, &leof);

	/*
	 * Fixup the chain element.  If we have a logical buffer in-hand
	 * we don't want to create a conflicting device buffer.
	 */
	if (loff && bp) {
		chain = hammer2_chain_lookup(hmp, &parent, lbase, lbase,
					     HAMMER2_LOOKUP_NODATA);
		if (chain) {
			allocbuf(bp, nblksize);
			switch(chain->bref.type) {
			case HAMMER2_BREF_TYPE_DATA:
				hammer2_chain_resize(ip, chain,
					     hammer2_allocsize(nblksize),
					     HAMMER2_MODIFY_OPTDATA);
				bzero(bp->b_data + loff, nblksize - loff);
				bp->b_bio2.bio_offset = chain->bref.data_off &
							HAMMER2_OFF_MASK;
				break;
			case HAMMER2_BREF_TYPE_INODE:
				bzero(bp->b_data + loff, nblksize - loff);
				bp->b_bio2.bio_offset = NOOFFSET;
				break;
			default:
				panic("hammer2_truncate_file: bad type");
				break;
			}
			hammer2_chain_unlock(hmp, chain);
			if (bp->b_bcount == HAMMER2_PBUFSIZE)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		} else {
			/*
			 * Destroy clean buffer w/ wrong buffer size.  Retain
			 * backing store.
			 */
			bp->b_flags |= B_RELBUF;
			KKASSERT(bp->b_bio2.bio_offset == NOOFFSET);
			KKASSERT((bp->b_flags & B_DIRTY) == 0);
			bqrelse(bp);
		}
	} else if (loff) {
		/*
		 * WARNING: This utilizes a device buffer for the data.
		 *
		 * This case should not occur because file truncations without
		 * a vnode (and hence no logical buffer cache) should only
		 * always truncate to 0-length.
		 */
		panic("hammer2_truncate_file: non-zero truncation, no-vnode");
#if 0
		chain = hammer2_chain_lookup(hmp, &parent, lbase, lbase, 0);
		if (chain) {
			switch(chain->bref.type) {
			case HAMMER2_BREF_TYPE_DATA:
				hammer2_chain_resize(ip, chain,
					     hammer2_allocsize(nblksize),
					     0);
				hammer2_chain_modify(hmp, chain, 0);
				bzero(chain->data->buf + loff, nblksize - loff);
				break;
			case HAMMER2_BREF_TYPE_INODE:
				if (loff < HAMMER2_EMBEDDED_BYTES) {
					hammer2_chain_modify(hmp, chain, 0);
					bzero(chain->data->ipdata.u.data + loff,
					      HAMMER2_EMBEDDED_BYTES - loff);
				}
				break;
			}
			hammer2_chain_unlock(hmp, chain);
		}
#endif
	}

	/*
	 * Clean up any fragmentory VM pages now that we have properly
	 * resized the straddling buffer.  These pages are no longer
	 * part of the buffer.
	 */
	if (ip->vp) {
		nvtruncbuf(ip->vp, nsize,
			   nblksize, (int)nsize & (nblksize - 1),
			   1);
	}

	/*
	 * Destroy any physical blocks after the new EOF point.
	 */
	lbase = (nsize + HAMMER2_PBUFMASK64) & ~HAMMER2_PBUFMASK64;
	chain = hammer2_chain_lookup(hmp, &parent,
				     lbase, (hammer2_key_t)-1,
				     HAMMER2_LOOKUP_NODATA);
	while (chain) {
		/*
		 * Degenerate embedded data case, nothing to loop on.
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			hammer2_chain_unlock(hmp, chain);
			break;
		}

		/*
		 * Delete physical data blocks past the file EOF.
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			/*ip->delta_dcount -= chain->bytes;*/
			hammer2_chain_delete(hmp, parent, chain, 0);
		}
		/* XXX check parent if empty indirect block & delete */
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lbase, (hammer2_key_t)-1,
					   HAMMER2_LOOKUP_NODATA);
	}
	hammer2_chain_unlock(hmp, parent);
}

/*
 * Extend the size of a file.  The inode must be locked.
 *
 * We may have to resize the block straddling the old EOF.
 */
static
void
hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_inode_data_t *ipdata;
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	struct buf *bp;
	hammer2_key_t osize;
	hammer2_key_t obase;
	hammer2_key_t nbase;
	hammer2_key_t leof;
	int oblksize;
	int nblksize;
	int nradix;
	int error;

	KKASSERT(ip->vp);
	hmp = ip->hmp;

	hammer2_chain_modify(hmp, ip->chain, 0);
	ipdata = &ip->chain->data->ipdata;

	/*
	 * Nothing to do if the direct-data case is still intact
	 */
	if ((ipdata->op_flags & HAMMER2_OPFLAG_DIRECTDATA) &&
	    nsize <= HAMMER2_EMBEDDED_BYTES) {
		ipdata->size = nsize;
		nvextendbuf(ip->vp,
			    ipdata->size, nsize,
			    0, HAMMER2_EMBEDDED_BYTES,
			    0, (int)nsize,
			    1);
		return;
	}

	/*
	 * Calculate the blocksize at the original EOF and resize the block
	 * if necessary.  Adjust the file size in the inode.
	 */
	osize = ipdata->size;
	oblksize = hammer2_calc_logical(ip, osize, &obase, &leof);
	ipdata->size = nsize;
	nblksize = hammer2_calc_logical(ip, osize, &nbase, &leof);

	/*
	 * Do all required vnode operations, but do not mess with the
	 * buffer straddling the orignal EOF.
	 */
	nvextendbuf(ip->vp,
		    ipdata->size, nsize,
		    0, nblksize,
		    0, (int)nsize & HAMMER2_PBUFMASK,
		    1);

	/*
	 * Early return if we have no more work to do.
	 */
	if (obase == nbase && oblksize == nblksize &&
	    (ipdata->op_flags & HAMMER2_OPFLAG_DIRECTDATA) == 0) {
		return;
	}

	/*
	 * We have work to do, including possibly resizing the buffer
	 * at the previous EOF point and turning off DIRECTDATA mode.
	 */
	bp = NULL;
	if (((int)osize & HAMMER2_PBUFMASK)) {
		error = bread(ip->vp, obase, oblksize, &bp);
		KKASSERT(error == 0);

		if (obase != nbase) {
			if (oblksize != HAMMER2_PBUFSIZE)
				allocbuf(bp, HAMMER2_PBUFSIZE);
		} else {
			if (oblksize != nblksize)
				allocbuf(bp, nblksize);
		}
	}

	/*
	 * Disable direct-data mode by loading up a buffer cache buffer
	 * with the data, then converting the inode data area into the
	 * inode indirect block array area.
	 */
	if (ipdata->op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
		ipdata->op_flags &= ~HAMMER2_OPFLAG_DIRECTDATA;
		bzero(&ipdata->u.blockset, sizeof(ipdata->u.blockset));
	}

	/*
	 * Resize the chain element at the old EOF.
	 */
	if (((int)osize & HAMMER2_PBUFMASK)) {
retry:
		parent = ip->chain;
		error = hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
		KKASSERT(error == 0);

		nradix = hammer2_allocsize(nblksize);

		chain = hammer2_chain_lookup(hmp, &parent,
					     obase, obase,
					     HAMMER2_LOOKUP_NODATA);
		if (chain == NULL) {
			chain = hammer2_chain_create(hmp, parent, NULL,
						     obase, nblksize,
						     HAMMER2_BREF_TYPE_DATA,
						     nblksize, &error);
			if (chain == NULL) {
				KKASSERT(error == EAGAIN);
				hammer2_chain_unlock(hmp, parent);
				goto retry;
			}
			/*ip->delta_dcount += nblksize;*/
		} else {
			KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_DATA);
			hammer2_chain_resize(ip, chain, nradix,
					     HAMMER2_MODIFY_OPTDATA);
		}
		bp->b_bio2.bio_offset = chain->bref.data_off &
					HAMMER2_OFF_MASK;
		hammer2_chain_unlock(hmp, chain);
		if (bp->b_bcount == HAMMER2_PBUFSIZE)
			bp->b_flags |= B_CLUSTEROK;
		bdwrite(bp);
		hammer2_chain_unlock(hmp, parent);
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
	hammer2_chain_t *ochain;
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
	hammer2_inode_lock_sh(dip);
	parent = dip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS |
					HAMMER2_RESOLVE_SHARED);
	chain = hammer2_chain_lookup(hmp, &parent,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    chain->u.ip &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(hmp, &parent, chain,
					   lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					   HAMMER2_LOOKUP_SHARED);
	}
	hammer2_chain_unlock(hmp, parent);
	hammer2_inode_unlock_sh(dip);

	/*
	 * If the inode represents a forwarding entry for a hardlink we have
	 * to locate the actual inode.  The original ip is saved for possible
	 * deconsolidation.  (ip) will only be set to non-NULL when we have
	 * to locate the real file via a hardlink.  ip will be referenced but
	 * not locked in that situation.  chain is passed in locked and
	 * returned locked.
	 *
	 * XXX what kind of chain lock?
	 */
	ochain = NULL;
	if (chain && chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
		error = hammer2_hardlink_find(dip, &chain, &ochain);
		if (error) {
			kprintf("hammer2: unable to find hardlink\n");
			if (chain) {
				hammer2_chain_unlock(hmp, chain);
				chain = NULL;
			}
			goto failed;
		}
	}

	/*
	 * Deconsolidate any hardlink whos nlinks == 1.  Ignore errors.
	 * If an error occurs chain and ip are left alone.
	 *
	 * XXX upgrade shared lock?
	 */
	if (ochain && chain && chain->data->ipdata.nlinks == 1 && !hmp->ronly) {
		kprintf("hammer2: need to unconsolidate hardlink for %s\n",
			chain->data->ipdata.filename);
		/* XXX retain shared lock on dip? (currently not held) */
		hammer2_hardlink_deconsolidate(dip, &chain, &ochain);
	}

	/*
	 * Acquire the related vnode
	 *
	 * NOTE: For error processing, only ENOENT resolves the namecache
	 *	 entry to NULL, otherwise we just return the error and
	 *	 leave the namecache unresolved.
	 */
	if (chain) {
		vp = hammer2_igetv(chain->u.ip, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		} else if (error == ENOENT) {
			cache_setvp(ap->a_nch, NULL);
		}
		hammer2_chain_unlock(hmp, chain);
	} else {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
	}
failed:
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p chain %p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, chain, ap));
	if (ochain)
		hammer2_chain_drop(hmp, ochain);
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
	hammer2_inode_lock_ex(ip);
	*ap->a_vpp = hammer2_igetv(ip, &error);
	hammer2_inode_unlock_ex(ip);

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

	error = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				     name, name_len, &nip);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);
	hammer2_inode_unlock_ex(nip);

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

	hammer2_inode_lock_sh(ip);
	parent = ip->chain;
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS |
					HAMMER2_RESOLVE_SHARED);
	chain = hammer2_chain_lookup(hmp, &parent,
				     lbeg, lend,
				     HAMMER2_LOOKUP_NODATA |
				     HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		*ap->a_doffsetp = ZFOFFSET;
		hammer2_chain_unlock(hmp, parent);
		hammer2_inode_unlock_sh(ip);
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
					   HAMMER2_LOOKUP_NODATA |
					   HAMMER2_LOOKUP_SHARED);
	}
	hammer2_chain_unlock(hmp, parent);
	hammer2_inode_unlock_sh(ip);

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
	hammer2_off_t size;

	hammer2_inode_lock_sh(ip);
	size = ip->chain->data->ipdata.size;
	hammer2_inode_unlock_sh(ip);
	return (lf_advlock(ap, &ip->advlock, size));
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
 * Create a hardlink from (vp) to {dvp, nch}.
 */
static
int
hammer2_vop_nlink(struct vop_nlink_args *ap)
{
	hammer2_inode_t *dip;	/* target directory to create link in */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	hammer2_inode_t *oip;
	hammer2_mount_t *hmp;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	hmp = dip->hmp;
	if (hmp->ronly)
		return (EROFS);

	/*
	 * (ip) is the inode we are linking to.
	 */
	ip = oip = VTOI(ap->a_vp);
	hammer2_inode_ref(ip);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	/*
	 * Create a consolidated real file for the hardlink, adjust (ip),
	 * and move the nlinks lock if necessary.  Tell the function to
	 * bump the hardlink count on the consolidated file.
	 */
	error = hammer2_hardlink_consolidate(&ip, dip);
	if (error)
		goto done;

	/*
	 * If the consolidation changed ip to a HARDLINK pointer we have
	 * to adjust the vnode to point to the actual ip.
	 *
	 * XXX this can race against concurrent vnode ops.
	 */
	if (oip != ip) {
		hammer2_inode_ref(ip);		/* vp ref+ */
		hammer2_inode_lock_ex(ip);
		hammer2_inode_lock_ex(oip);
		ip->vp = ap->a_vp;
		ap->a_vp->v_data = ip;
		oip->vp = NULL;
		hammer2_inode_unlock_ex(oip);
		hammer2_inode_unlock_ex(ip);
		hammer2_inode_drop(oip);	/* vp ref- */
	}

	/*
	 * The act of connecting the existing (ip) will properly bump the
	 * nlinks count.  However, vp will incorrectly point at the old
	 * inode which has now been turned into a OBJTYPE_HARDLINK pointer.
	 *
	 * We must reconnect the vp.
	 */
	error = hammer2_inode_connect(dip, ip, name, name_len);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
done:
	hammer2_inode_drop(ip);
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

	error = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				     name, name_len, &nip);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);
	hammer2_inode_unlock_ex(nip);

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

	error = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				     name, name_len, &nip);
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
		hammer2_inode_data_t *nipdata;

		nipdata = &nip->chain->data->ipdata;
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER2_EMBEDDED_BYTES) {
			KKASSERT(nipdata->op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			bcopy(ap->a_target, nipdata->u.data, bytes);
			nipdata->size = bytes;
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
			error = hammer2_write_file(nip, &auio, IO_APPEND, 0);
			/* XXX handle error */
			error = 0;
		}
	}
	hammer2_inode_unlock_ex(nip);

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

	error = hammer2_unlink_file(dip, name, name_len, 0, NULL);

	if (error == 0) {
		cache_unlink(ap->a_nch);
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

	error = hammer2_unlink_file(dip, name, name_len, 1, NULL);

	if (error == 0) {
		cache_unlink(ap->a_nch);
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

	/*
	 * ip is the inode being removed.  If this is a hardlink then
	 * ip represents the actual file and not the hardlink marker.
	 */
	ip = VTOI(fncp->nc_vp);

	/*
	 * Keep a tight grip on the inode as removing it should disconnect
	 * it and we don't want to destroy it.
	 *
	 * NOTE: To avoid deadlocks we cannot lock (ip) while we are
	 *	 unlinking elements from their directories.  Locking
	 *	 the nlinks field does not lock the whole inode.
	 */
	hammer2_inode_ref(ip);

	/*
	 * Remove target if it exists
	 */
	error = hammer2_unlink_file(tdip, tname, tname_len, -1, NULL);
	if (error && error != ENOENT)
		goto done;
	cache_setunresolved(ap->a_tnch);

	/*
	 * Disconnect (fdip, fname) from the source directory.  This will
	 * disconnect (ip) if it represents a direct file.  If (ip) represents
	 * a hardlink the HARDLINK pointer object will be removed but the
	 * hardlink will stay intact.
	 *
	 * If (ip) is already hardlinked we have to resolve to a consolidated
	 * file but we do not bump the nlinks count.  (ip) must hold the nlinks
	 * lock & ref for the operation.  If the consolidated file has been
	 * relocated (ip) will be adjusted and the related nlinks lock moved
	 * along with it.
	 *
	 * If (ip) does not have multiple links we can just copy the physical
	 * contents of the inode.
	 */
	hammer2_inode_lock_sh(ip);
	if (ip->chain->data->ipdata.nlinks > 1) {
		hammer2_inode_unlock_sh(ip);
		error = hammer2_hardlink_consolidate(&ip, tdip);
		if (error)
			goto done;
	} else {
		hammer2_inode_unlock_sh(ip);
	}

	/*
	 * NOTE! Because we are retaining (ip) the unlink can fail with
	 *	 an EAGAIN.
	 */
	for (;;) {
		error = hammer2_unlink_file(fdip, fname, fname_len, -1, ip);
		if (error != EAGAIN)
			break;
		kprintf("hammer2_vop_nrename: unlink race %s\n", fname);
		tsleep(fdip, 0, "h2renr", 1);
	}
	if (error)
		goto done;

	/*
	 * Reconnect ip to target directory.
	 *
	 * WARNING: chain locks can lock buffer cache buffers, to avoid
	 *	    deadlocks we want to unlock before issuing a cache_*()
	 *	    op (that might have to lock a vnode).
	 */
	error = hammer2_inode_connect(tdip, ip, tname, tname_len);
	if (error == 0) {
		cache_rename(ap->a_fnch, ap->a_tnch);
	}
done:
	hammer2_inode_drop(ip);

	return (error);
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
		++hammer2_iod_file_read;
		break;
	case BUF_CMD_WRITE:
		error = hammer2_strategy_write(ap);
		++hammer2_iod_file_write;
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
	hammer2_key_t lbase;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	nbio = push_bio(bio);

	lbase = bio->bio_offset;
	chain = NULL;
	KKASSERT(((int)lbase & HAMMER2_PBUFMASK) == 0);

	/*
	 * We must characterize the logical->physical translation if it
	 * has not already been cached.
	 *
	 * Physical data references < LBUFSIZE are never cached.  This
	 * includes both small-block allocations and inode-embedded data.
	 */
	if (nbio->bio_offset == NOOFFSET) {
		hammer2_inode_lock_sh(ip);
		parent = ip->chain;
		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS |
						HAMMER2_RESOLVE_SHARED);

		chain = hammer2_chain_lookup(hmp, &parent, lbase, lbase,
					     HAMMER2_LOOKUP_NODATA |
					     HAMMER2_LOOKUP_SHARED);
		if (chain == NULL) {
			/*
			 * Data is zero-fill
			 */
			nbio->bio_offset = ZFOFFSET;
		} else if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			/*
			 * Data is embedded in the inode (do nothing)
			 */
			KKASSERT(chain == parent);
			hammer2_chain_unlock(hmp, chain);
		} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			/*
			 * Data is on-media
			 */
			KKASSERT(bp->b_bcount == chain->bytes);
			nbio->bio_offset = chain->bref.data_off &
					   HAMMER2_OFF_MASK;
			hammer2_chain_unlock(hmp, chain);
			KKASSERT(nbio->bio_offset != 0);
		} else {
			panic("hammer2_strategy_read: unknown bref type");
		}
		hammer2_chain_unlock(hmp, parent);
		hammer2_inode_unlock_sh(ip);
	}

	if (hammer2_debug & 0x0020) {
		kprintf("read %016jx %016jx\n",
			bio->bio_offset, nbio->bio_offset);
	}

	if (nbio->bio_offset == ZFOFFSET) {
		/*
		 * Data is zero-fill
		 */
		bp->b_resid = 0;
		bp->b_error = 0;
		bzero(bp->b_data, bp->b_bcount);
		biodone(nbio);
	} else if (nbio->bio_offset != NOOFFSET) {
		/*
		 * Forward direct IO to the device
		 */
		vn_strategy(hmp->devvp, nbio);
	} else {
		/*
		 * Data is embedded in inode.
		 */
		bcopy(chain->data->ipdata.u.data, bp->b_data,
		      HAMMER2_EMBEDDED_BYTES);
		bzero(bp->b_data + HAMMER2_EMBEDDED_BYTES,
		      bp->b_bcount - HAMMER2_EMBEDDED_BYTES);
		bp->b_resid = 0;
		bp->b_error = 0;
		biodone(nbio);
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

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	nbio = push_bio(bio);

	KKASSERT((bio->bio_offset & HAMMER2_PBUFMASK64) == 0);
	KKASSERT(nbio->bio_offset != 0 && nbio->bio_offset != ZFOFFSET);

	if (nbio->bio_offset == NOOFFSET) {
		/*
		 * Must be embedded in the inode.
		 *
		 * Because the inode is dirty, the chain must exist whether
		 * the inode is locked or not. XXX
		 */
		KKASSERT(bio->bio_offset == 0);
		KKASSERT(ip->chain && ip->chain->data);
		bcopy(bp->b_data, ip->chain->data->ipdata.u.data,
		      HAMMER2_EMBEDDED_BYTES);
		bp->b_resid = 0;
		bp->b_error = 0;
		biodone(nbio);

		/*
		 * This special flag does not follow the normal MODIFY rules
		 * because we might deadlock on ip.  Instead we depend on
		 * VOP_FSYNC() to detect the case.
		 */
		atomic_set_int(&ip->flags, HAMMER2_INODE_DIRTYEMBED);
	} else {
		/*
		 * Forward direct IO to the device
		 */
		vn_strategy(hmp->devvp, nbio);
	}
	return (0);
}

/*
 * hammer2_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer2_vop_ioctl(struct vop_ioctl_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	int error;

	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;

	error = hammer2_ioctl(ip, ap->a_command, (void *)ap->a_data,
			      ap->a_fflag, ap->a_cred);
	return (error);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	hammer2_pfsmount_t *pmp;
	int rc;

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		mp = ap->a_head.a_ops->head.vv_mount;
		pmp = MPTOPMP(mp);

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &pmp->export,
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
	.vop_ioctl	= hammer2_vop_ioctl,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};
