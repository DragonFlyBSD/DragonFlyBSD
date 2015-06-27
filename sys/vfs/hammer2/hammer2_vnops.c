/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression) 
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
/*
 * Kernel Filesystem interface
 *
 * NOTE! local ipdata pointers must be reloaded on any modifying operation
 *	 to the inode as its underlying chain may have changed.
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
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>

#include "hammer2.h"

static int hammer2_read_file(hammer2_inode_t *ip, struct uio *uio,
				int seqcount);
static int hammer2_write_file(hammer2_inode_t *ip, struct uio *uio,
				int ioflag, int seqcount);
static void hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize);
static void hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize);

struct objcache *cache_xops;

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
	hammer2_inode_t *ip;
	struct vnode *vp;

	LOCKSTART;
	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(vp);
		LOCKSTOP;
		return (0);
	}

	/*
	 * Check for deleted inodes and recycle immediately on the last
	 * release.  Be sure to destroy any left-over buffer cache buffers
	 * so we do not waste time trying to flush them.
	 *
	 * WARNING: nvtruncbuf() can only be safely called without the inode
	 *	    lock held due to the way our write thread works.
	 */
	if (ip->flags & HAMMER2_INODE_ISUNLINKED) {
		hammer2_key_t lbase;
		int nblksize;

		/*
		 * Detect updates to the embedded data which may be
		 * synchronized by the strategy code.  Simply mark the
		 * inode modified so it gets picked up by our normal flush.
		 */
		nblksize = hammer2_calc_logical(ip, 0, &lbase, NULL);
		nvtruncbuf(vp, 0, nblksize, 0, 0);
		vrecycle(vp);
	}
	LOCKSTOP;
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
	hammer2_inode_t *ip;
	hammer2_pfs_t *pmp;
	struct vnode *vp;

	LOCKSTART;
	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL) {
		LOCKSTOP;
		return(0);
	}
	pmp = ip->pmp;

	/*
	 * The final close of a deleted file or directory marks it for
	 * destruction.  The DELETED flag allows the flusher to shortcut
	 * any modified blocks still unflushed (that is, just ignore them).
	 *
	 * HAMMER2 usually does not try to optimize the freemap by returning
	 * deleted blocks to it as it does not usually know how many snapshots
	 * might be referencing portions of the file/dir.
	 */
	vp->v_data = NULL;
	ip->vp = NULL;

	/*
	 * NOTE! We do not attempt to flush chains here, flushing is
	 *	 really fragile and could also deadlock.
	 */
	vclrisdirty(vp);

	/*
	 * Once reclaimed the inode is disconnected from the normal flush
	 * mechanism and must be tracked
	 *
	 * A reclaim can occur at any time so we cannot safely start a
	 * transaction to handle reclamation of unlinked files.  Instead,
	 * the ip is left with a reference and placed on a linked list and
	 * handled later on.
	 */
	if (ip->flags & HAMMER2_INODE_ISUNLINKED) {
		hammer2_inode_unlink_t *ipul;

		ipul = kmalloc(sizeof(*ipul), pmp->minode, M_WAITOK | M_ZERO);
		ipul->ip = ip;

		hammer2_spin_ex(&pmp->list_spin);
		TAILQ_INSERT_TAIL(&pmp->unlinkq, ipul, entry);
		hammer2_spin_unex(&pmp->list_spin);
		/* retain ref from vp for ipul */
	} else {
		hammer2_inode_drop(ip);			/* vp ref */
	}

	/*
	 * XXX handle background sync when ip dirty, kernel will no longer
	 * notify us regarding this inode because there is no longer a
	 * vnode attached to it.
	 */

	LOCKSTOP;
	return (0);
}

static
int
hammer2_vop_fsync(struct vop_fsync_args *ap)
{
	hammer2_inode_t *ip;
	struct vnode *vp;

	LOCKSTART;
	vp = ap->a_vp;
	ip = VTOI(vp);

#if 0
	/* XXX can't do this yet */
	hammer2_trans_init(ip->pmp, HAMMER2_TRANS_ISFLUSH);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);
#endif
	hammer2_trans_init(ip->pmp, 0);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);

	/*
	 * Calling chain_flush here creates a lot of duplicative
	 * COW operations due to non-optimal vnode ordering.
	 *
	 * Only do it for an actual fsync() syscall.  The other forms
	 * which call this function will eventually call chain_flush
	 * on the volume root as a catch-all, which is far more optimal.
	 */
	hammer2_inode_lock(ip, 0);
	if (ip->flags & HAMMER2_INODE_MODIFIED)
		hammer2_inode_fsync(ip, NULL);
	hammer2_inode_unlock(ip);
	hammer2_trans_done(ip->pmp);

	LOCKSTOP;
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

	LOCKSTART;
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);
	uid = hammer2_to_unix_xid(&ip->meta.uid);
	gid = hammer2_to_unix_xid(&ip->meta.gid);
	error = vop_helper_access(ap, uid, gid, ip->meta.mode, ip->meta.uflags);
	hammer2_inode_unlock(ip);

	LOCKSTOP;
	return (error);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	hammer2_pfs_t *pmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	LOCKSTART;
	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	pmp = ip->pmp;

	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);

	vap->va_fsid = pmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ip->meta.inum;
	vap->va_mode = ip->meta.mode;
	vap->va_nlink = ip->meta.nlinks;
	vap->va_uid = hammer2_to_unix_xid(&ip->meta.uid);
	vap->va_gid = hammer2_to_unix_xid(&ip->meta.gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->meta.size;	/* protected by shared lock */
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = ip->meta.uflags;
	hammer2_time_to_timespec(ip->meta.ctime, &vap->va_ctime);
	hammer2_time_to_timespec(ip->meta.mtime, &vap->va_mtime);
	hammer2_time_to_timespec(ip->meta.mtime, &vap->va_atime);
	vap->va_gen = 1;
	vap->va_bytes = ip->bref.data_count;
	vap->va_type = hammer2_get_vtype(ip->meta.type);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ip->meta.uid;
	vap->va_gid_uuid = ip->meta.gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock(ip);

	LOCKSTOP;
	return (0);
}

static
int
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	uint64_t ctime;

	LOCKSTART;
	vp = ap->a_vp;
	vap = ap->a_vap;
	hammer2_update_time(&ctime);

	ip = VTOI(vp);

	if (ip->pmp->ronly) {
		LOCKSTOP;
		return(EROFS);
	}

	hammer2_pfs_memory_wait(ip->pmp);
	hammer2_trans_init(ip->pmp, 0);
	hammer2_inode_lock(ip, 0);
	error = 0;

	if (vap->va_flags != VNOVAL) {
		u_int32_t flags;

		flags = ip->meta.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
				     hammer2_to_unix_xid(&ip->meta.uid),
				     ap->a_cred);
		if (error == 0) {
			if (ip->meta.uflags != flags) {
				hammer2_inode_modify(ip);
				ip->meta.uflags = flags;
				ip->meta.ctime = ctime;
				kflags |= NOTE_ATTRIB;
			}
			if (ip->meta.uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ip->meta.uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ip->meta.mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ip->meta.uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ip->meta.gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer2_guid_to_uuid(&uuid_uid, cur_uid);
			hammer2_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ip->meta.uid, sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ip->meta.gid, sizeof(uuid_gid)) ||
			    ip->meta.mode != cur_mode
			) {
				hammer2_inode_modify(ip);
				ip->meta.uid = uuid_uid;
				ip->meta.gid = uuid_gid;
				ip->meta.mode = cur_mode;
				ip->meta.ctime = ctime;
			}
			kflags |= NOTE_ATTRIB;
		}
	}

	/*
	 * Resize the file
	 */
	if (vap->va_size != VNOVAL && ip->meta.size != vap->va_size) {
		switch(vp->v_type) {
		case VREG:
			if (vap->va_size == ip->meta.size)
				break;
			if (vap->va_size < ip->meta.size) {
				hammer2_truncate_file(ip, vap->va_size);
			} else {
				hammer2_extend_file(ip, vap->va_size);
			}
			hammer2_inode_modify(ip);
			ip->meta.mtime = ctime;
			break;
		default:
			error = EINVAL;
			goto done;
		}
	}
#if 0
	/* atime not supported */
	if (vap->va_atime.tv_sec != VNOVAL) {
		hammer2_inode_modify(ip);
		ip->meta.atime = hammer2_timespec_to_time(&vap->va_atime);
		kflags |= NOTE_ATTRIB;
	}
#endif
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t cur_mode = ip->meta.mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ip->meta.uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ip->meta.gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ip->meta.mode != cur_mode) {
			hammer2_inode_modify(ip);
			ip->meta.mode = cur_mode;
			ip->meta.ctime = ctime;
			kflags |= NOTE_ATTRIB;
		}
	}

	if (vap->va_mtime.tv_sec != VNOVAL) {
		hammer2_inode_modify(ip);
		ip->meta.mtime = hammer2_timespec_to_time(&vap->va_mtime);
		kflags |= NOTE_ATTRIB;
	}

done:
	/*
	 * If a truncation occurred we must call inode_fsync() now in order
	 * to trim the related data chains, otherwise a later expansion can
	 * cause havoc.
	 *
	 * If an extend occured that changed the DIRECTDATA state, we must
	 * call inode_fsync now in order to prepare the inode's indirect
	 * block table.
	 */
	if (ip->flags & HAMMER2_INODE_RESIZED)
		hammer2_inode_fsync(ip, NULL);

	/*
	 * Cleanup.
	 */
	hammer2_inode_unlock(ip);
	hammer2_trans_done(ip->pmp);
	hammer2_knote(ip->vp, kflags);

	LOCKSTOP;
	return (error);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	hammer2_xop_readdir_t *xop;
	hammer2_blockref_t bref;
	hammer2_inode_t *ip;
	hammer2_tid_t inum;
	hammer2_key_t lkey;
	struct uio *uio;
	off_t *cookies;
	off_t saveoff;
	int cookie_index;
	int ncookies;
	int error;
	int eofflag;
	int dtype;
	int r;

	LOCKSTART;
	ip = VTOI(ap->a_vp);
	uio = ap->a_uio;
	saveoff = uio->uio_offset;
	eofflag = 0;
	error = 0;

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

	hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);

	/*
	 * Handle artificial entries.  To ensure that only positive 64 bit
	 * quantities are returned to userland we always strip off bit 63.
	 * The hash code is designed such that codes 0x0000-0x7FFF are not
	 * used, allowing us to use these codes for articial entries.
	 *
	 * Entry 0 is used for '.' and entry 1 is used for '..'.  Do not
	 * allow '..' to cross the mount point into (e.g.) the super-root.
	 */
	if (saveoff == 0) {
		inum = ip->meta.inum & HAMMER2_DIRHASH_USERMSK;
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
		 *
		 * (ip is the current dir. xip is the parent dir).
		 */
		inum = ip->meta.inum & HAMMER2_DIRHASH_USERMSK;
		if (ip->pip && ip != ip->pmp->iroot)
			inum = ip->pip->meta.inum & HAMMER2_DIRHASH_USERMSK;
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
	if (hammer2_debug & 0x0020)
		kprintf("readdir: lkey %016jx\n", lkey);
	if (error)
		goto done;

	/*
	 * Use XOP for cluster scan.
	 *
	 * parent is the inode cluster, already locked for us.  Don't
	 * double lock shared locks as this will screw up upgrades.
	 */
	xop = &hammer2_xop_alloc(ip)->xop_readdir;
	xop->head.lkey = lkey;
	hammer2_xop_start(&xop->head, hammer2_xop_readdir);

	for (;;) {
		const hammer2_inode_data_t *ripdata;

		error = hammer2_xop_collect(&xop->head, 0);
		if (error)
			break;
		if (cookie_index == ncookies)
			break;
		if (hammer2_debug & 0x0020)
		kprintf("cluster chain %p %p\n",
			xop->head.cluster.focus,
			(xop->head.cluster.focus ?
			 xop->head.cluster.focus->data : (void *)-1));
		ripdata = &hammer2_cluster_rdata(&xop->head.cluster)->ipdata;
		hammer2_cluster_bref(&xop->head.cluster, &bref);
		if (bref.type == HAMMER2_BREF_TYPE_INODE) {
			dtype = hammer2_get_dtype(ripdata);
			saveoff = bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     ripdata->meta.inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype,
					     ripdata->meta.name_len,
					     ripdata->filename);
			if (r)
				break;
			if (cookies)
				cookies[cookie_index] = saveoff;
			++cookie_index;
		} else {
			/* XXX chain error */
			kprintf("bad chain type readdir %d\n", bref.type);
		}
	}
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	if (error == ENOENT) {
		error = 0;
		eofflag = 1;
		saveoff = (hammer2_key_t)-1;
	} else {
		saveoff = bref.key & HAMMER2_DIRHASH_USERMSK;
	}
done:
	hammer2_inode_unlock(ip);
	if (ap->a_eofflag)
		*ap->a_eofflag = eofflag;
	if (hammer2_debug & 0x0020)
		kprintf("readdir: done at %016jx\n", saveoff);
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
	LOCKSTOP;
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
	hammer2_inode_t *ip;
	int error;

	vp = ap->a_vp;
	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);

	error = hammer2_read_file(ip, ap->a_uio, 0);
	return (error);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	struct vnode *vp;
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
	hammer2_inode_t *ip;
	thread_t td;
	struct vnode *vp;
	struct uio *uio;
	int error;
	int seqcount;

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
	uio = ap->a_uio;
	error = 0;
	if (ip->pmp->ronly) {
		return (EROFS);
	}

	seqcount = ap->a_ioflag >> 16;

	/*
	 * Check resource limit
	 */
	if (uio->uio_resid > 0 && (td = uio->uio_td) != NULL && td->td_proc &&
	    uio->uio_offset + uio->uio_resid >
	     td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}

	/*
	 * The transaction interlocks against flushes initiations
	 * (note: but will run concurrently with the actual flush).
	 */
	hammer2_trans_init(ip->pmp, 0);
	error = hammer2_write_file(ip, uio, ap->a_ioflag, seqcount);
	hammer2_trans_done(ip->pmp);

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
	hammer2_off_t size;
	struct buf *bp;
	int error;

	error = 0;

	/*
	 * UIO read loop.
	 *
	 * WARNING! Assumes that the kernel interlocks size changes at the
	 *	    vnode level.
	 */
	hammer2_mtx_sh(&ip->lock);
	size = ip->meta.size;
	hammer2_mtx_unlock(&ip->lock);

	while (uio->uio_resid > 0 && uio->uio_offset < size) {
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
		if (n > size - uio->uio_offset)
			n = (int)(size - uio->uio_offset);
		bp->b_flags |= B_AGE;
		uiomove((char *)bp->b_data + loff, n, uio);
		bqrelse(bp);
	}
	return (error);
}

/*
 * Write to the file represented by the inode via the logical buffer cache.
 * The inode may represent a regular file or a symlink.
 *
 * The inode must not be locked.
 */
static
int
hammer2_write_file(hammer2_inode_t *ip, struct uio *uio,
		   int ioflag, int seqcount)
{
	hammer2_key_t old_eof;
	hammer2_key_t new_eof;
	struct buf *bp;
	int kflags;
	int error;
	int modified;

	/*
	 * Setup if append
	 *
	 * WARNING! Assumes that the kernel interlocks size changes at the
	 *	    vnode level.
	 */
	hammer2_mtx_ex(&ip->lock);
	if (ioflag & IO_APPEND)
		uio->uio_offset = ip->meta.size;
	old_eof = ip->meta.size;

	/*
	 * Extend the file if necessary.  If the write fails at some point
	 * we will truncate it back down to cover as much as we were able
	 * to write.
	 *
	 * Doing this now makes it easier to calculate buffer sizes in
	 * the loop.
	 */
	kflags = 0;
	error = 0;
	modified = 0;

	if (uio->uio_offset + uio->uio_resid > old_eof) {
		new_eof = uio->uio_offset + uio->uio_resid;
		modified = 1;
		hammer2_extend_file(ip, new_eof);
		kflags |= NOTE_EXTEND;
	} else {
		new_eof = old_eof;
	}
	hammer2_mtx_unlock(&ip->lock);
	
	/*
	 * UIO write loop
	 */
	while (uio->uio_resid > 0) {
		hammer2_key_t lbase;
		int trivial;
		int endofblk;
		int lblksize;
		int loff;
		int n;

		/*
		 * Don't allow the buffer build to blow out the buffer
		 * cache.
		 */
		if ((ioflag & IO_RECURSE) == 0)
			bwillwrite(HAMMER2_PBUFSIZE);

		/*
		 * This nominally tells us how much we can cluster and
		 * what the logical buffer size needs to be.  Currently
		 * we don't try to cluster the write and just handle one
		 * block at a time.
		 */
		lblksize = hammer2_calc_logical(ip, uio->uio_offset,
						&lbase, NULL);
		loff = (int)(uio->uio_offset - lbase);
		
		KKASSERT(lblksize <= 65536);

		/*
		 * Calculate bytes to copy this transfer and whether the
		 * copy completely covers the buffer or not.
		 */
		trivial = 0;
		n = lblksize - loff;
		if (n > uio->uio_resid) {
			n = uio->uio_resid;
			if (loff == lbase && uio->uio_offset + n == new_eof)
				trivial = 1;
			endofblk = 0;
		} else {
			if (loff == 0)
				trivial = 1;
			endofblk = 1;
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
		 * Ok, copy the data in
		 */
		error = uiomove(bp->b_data + loff, n, uio);
		kflags |= NOTE_WRITE;
		modified = 1;
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * WARNING: Pageout daemon will issue UIO_NOCOPY writes
		 *	    with IO_SYNC or IO_ASYNC set.  These writes
		 *	    must be handled as the pageout daemon expects.
		 */
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

	/*
	 * Cleanup.  If we extended the file EOF but failed to write through
	 * the entire write is a failure and we have to back-up.
	 */
	if (error && new_eof != old_eof) {
		hammer2_mtx_ex(&ip->lock);
		hammer2_truncate_file(ip, old_eof);
		if (ip->flags & HAMMER2_INODE_MODIFIED)
			hammer2_inode_fsync(ip, NULL);
		hammer2_mtx_unlock(&ip->lock);
	} else if (modified) {
		hammer2_mtx_ex(&ip->lock);
		hammer2_inode_modify(ip);
		hammer2_update_time(&ip->meta.mtime);
		if (ip->flags & HAMMER2_INODE_MODIFIED)
			hammer2_inode_fsync(ip, NULL);
		hammer2_mtx_unlock(&ip->lock);
		hammer2_knote(ip->vp, kflags);
	}
	hammer2_trans_assert_strategy(ip->pmp);

	return error;
}

/*
 * Truncate the size of a file.  The inode must not be locked.
 *
 * We must unconditionally set HAMMER2_INODE_RESIZED to properly
 * ensure that any on-media data beyond the new file EOF has been destroyed.
 *
 * WARNING: nvtruncbuf() can only be safely called without the inode lock
 *	    held due to the way our write thread works.  If the truncation
 *	    occurs in the middle of a buffer, nvtruncbuf() is responsible
 *	    for dirtying that buffer and zeroing out trailing bytes.
 *
 * WARNING! Assumes that the kernel interlocks size changes at the
 *	    vnode level.
 *
 * WARNING! Caller assumes responsibility for removing dead blocks
 *	    if INODE_RESIZED is set.
 */
static
void
hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_key_t lbase;
	int nblksize;

	LOCKSTART;
	hammer2_mtx_unlock(&ip->lock);
	if (ip->vp) {
		nblksize = hammer2_calc_logical(ip, nsize, &lbase, NULL);
		nvtruncbuf(ip->vp, nsize,
			   nblksize, (int)nsize & (nblksize - 1),
			   0);
	}
	hammer2_mtx_ex(&ip->lock);
	KKASSERT((ip->flags & HAMMER2_INODE_RESIZED) == 0);
	ip->osize = ip->meta.size;
	ip->meta.size = nsize;
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED |
				   HAMMER2_INODE_RESIZED);
	LOCKSTOP;
}

/*
 * Extend the size of a file.  The inode must not be locked.
 *
 * Even though the file size is changing, we do not have to set the
 * INODE_RESIZED bit unless the file size crosses the EMBEDDED_BYTES
 * boundary.  When this occurs a hammer2_inode_fsync() is required
 * to prepare the inode cluster's indirect block table.
 *
 * WARNING! Assumes that the kernel interlocks size changes at the
 *	    vnode level.
 *
 * WARNING! Caller assumes responsibility for transitioning out
 *	    of the inode DIRECTDATA mode if INODE_RESIZED is set.
 */
static
void
hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_key_t lbase;
	hammer2_key_t osize;
	int oblksize;
	int nblksize;

	LOCKSTART;

	KKASSERT((ip->flags & HAMMER2_INODE_RESIZED) == 0);
	osize = ip->meta.size;
	ip->osize = osize;
	ip->meta.size = nsize;
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);

	if (osize <= HAMMER2_EMBEDDED_BYTES && nsize > HAMMER2_EMBEDDED_BYTES)
		atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);

	hammer2_mtx_unlock(&ip->lock);
	if (ip->vp) {
		oblksize = hammer2_calc_logical(ip, osize, &lbase, NULL);
		nblksize = hammer2_calc_logical(ip, nsize, &lbase, NULL);
		nvextendbuf(ip->vp,
			    osize, nsize,
			    oblksize, nblksize,
			    -1, -1, 0);
	}
	hammer2_mtx_ex(&ip->lock);

	LOCKSTOP;
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	hammer2_xop_nresolve_t *xop;
	hammer2_inode_t *ip;
	hammer2_inode_t *dip;
	struct namecache *ncp;
	struct vnode *vp;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	xop = &hammer2_xop_alloc(dip)->xop_nresolve;

	ncp = ap->a_nch->ncp;
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);
	hammer2_xop_start(&xop->head, hammer2_xop_nresolve);

	error = hammer2_xop_collect(&xop->head, 0);
	if (error) {
		ip = NULL;
	} else {
		ip = hammer2_inode_get(dip->pmp, dip, &xop->head.cluster);
	}
	hammer2_inode_unlock(dip);

	/*
	 * Acquire the related vnode
	 *
	 * NOTE: For error processing, only ENOENT resolves the namecache
	 *	 entry to NULL, otherwise we just return the error and
	 *	 leave the namecache unresolved.
	 *
	 * NOTE: multiple hammer2_inode structures can be aliased to the
	 *	 same chain element, for example for hardlinks.  This
	 *	 use case does not 'reattach' inode associations that
	 *	 might already exist, but always allocates a new one.
	 *
	 * WARNING: inode structure is locked exclusively via inode_get
	 *	    but chain was locked shared.  inode_unlock()
	 *	    will handle it properly.
	 */
	if (ip) {
		vp = hammer2_igetv(ip, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
		} else if (error == ENOENT) {
			cache_setvp(ap->a_nch, NULL);
		}
		hammer2_inode_unlock(ip);

		/*
		 * The vp should not be released until after we've disposed
		 * of our locks, because it might cause vop_inactive() to
		 * be called.
		 */
		if (vp)
			vrele(vp);
	} else {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
	}
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, ap));
	LOCKSTOP;

	return error;
}

static
int
hammer2_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);

	if ((ip = dip->pip) == NULL) {
		*ap->a_vpp = NULL;
		LOCKSTOP;
		return ENOENT;
	}
	hammer2_inode_lock(ip, 0);
	*ap->a_vpp = hammer2_igetv(ip, &error);
	hammer2_inode_unlock(ip);

	LOCKSTOP;
	return error;
}

static
int
hammer2_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);
	nip = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				   name, name_len, 0,
				   hammer2_trans_newinum(dip->pmp), 0, 0,
				   0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}
	hammer2_trans_done(dip->pmp);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
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

	size = ip->meta.size;
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
	hammer2_xop_nlink_t *xop1;
	hammer2_inode_t *fdip;	/* target directory to create link in */
	hammer2_inode_t *tdip;	/* target directory to create link in */
	hammer2_inode_t *cdip;	/* common parent directory */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	tdip = VTOI(ap->a_dvp);
	if (tdip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	/*
	 * ip represents the file being hardlinked.  The file could be a
	 * normal file or a hardlink target if it has already been hardlinked.
	 * If ip is a hardlinked target then ip->pip represents the location
	 * of the hardlinked target, NOT the location of the hardlink pointer.
	 *
	 * Bump nlinks and potentially also create or move the hardlink
	 * target in the parent directory common to (ip) and (tdip).  The
	 * consolidation code can modify ip->cluster and ip->pip.  The
	 * returned cluster is locked.
	 */
	ip = VTOI(ap->a_vp);
	hammer2_pfs_memory_wait(ip->pmp);
	hammer2_trans_init(ip->pmp, 0);

	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 */
	fdip = ip->pip;
	cdip = hammer2_inode_common_parent(fdip, tdip);
	hammer2_inode_lock(cdip, 0);
	hammer2_inode_lock(fdip, 0);
	hammer2_inode_lock(tdip, 0);
	hammer2_inode_lock(ip, 0);
	error = 0;

	/*
	 * If ip is not a hardlink target we must convert it to a hardlink.
	 * If fdip != cdip we must shift the inode to cdip.
	 */
	if (fdip != cdip || (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		xop1 = &hammer2_xop_alloc(fdip)->xop_nlink;
		hammer2_xop_setip2(&xop1->head, ip);
		hammer2_xop_setip3(&xop1->head, cdip);

		hammer2_xop_start(&xop1->head, hammer2_xop_nlink);
		error = hammer2_xop_collect(&xop1->head, 0);
		hammer2_xop_retire(&xop1->head, HAMMER2_XOPMASK_VOP);
		if (error == ENOENT)
			error = 0;
	}

	/*
	 * Must synchronize original inode whos chains are now a hardlink
	 * target.  We must match what the backend XOP did to the
	 * chains.
	 */
	if (error == 0 && (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		hammer2_inode_modify(ip);
		ip->meta.name_key = ip->meta.inum;
		ip->meta.name_len = 18;	/* "0x%016jx" */
	}

	/*
	 * Create the hardlink target and bump nlinks.
	 */
	if (error == 0) {
		hammer2_inode_create(tdip, NULL, NULL,
				     name, name_len, 0,
				     ip->meta.inum,
				     HAMMER2_OBJTYPE_HARDLINK, ip->meta.type,
				     0, &error);
		hammer2_inode_modify(ip);
		++ip->meta.nlinks;
	}
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
	hammer2_inode_unlock(ip);
	hammer2_inode_unlock(tdip);
	hammer2_inode_unlock(fdip);
	hammer2_inode_unlock(cdip);
	hammer2_inode_drop(cdip);
	hammer2_trans_done(ip->pmp);

	LOCKSTOP;
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
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);

	nip = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				   name, name_len, 0,
				   hammer2_trans_newinum(dip->pmp), 0, 0,
				   0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}
	hammer2_trans_done(dip->pmp);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
}

/*
 * Make a device node (typically a fifo)
 */
static
int
hammer2_vop_nmknod(struct vop_nmknod_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return (EROFS);
	}

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);

	nip = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				   name, name_len, 0,
				   hammer2_trans_newinum(dip->pmp), 0, 0,
				   0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}
	hammer2_trans_done(dip->pmp);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	LOCKSTOP;
	return error;
}

/*
 * hammer2_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer2_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;
	
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);

	ap->a_vap->va_type = VLNK;	/* enforce type */

	nip = hammer2_inode_create(dip, ap->a_vap, ap->a_cred,
				   name, name_len, 0,
				   hammer2_trans_newinum(dip->pmp), 0, 0,
				   0, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		hammer2_trans_done(dip->pmp);
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

		hammer2_inode_unlock(nip);
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
	} else {
		hammer2_inode_unlock(nip);
	}
	hammer2_trans_done(dip->pmp);

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
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	struct namecache *ncp;
	int error;
	int isopen;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return(EROFS);
	}

	ncp = ap->a_nch->ncp;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);
	hammer2_inode_lock(dip, 0);

	/*
	 * The unlink XOP unlinks the path from the directory and
	 * locates and returns the cluster associated with the real inode.
	 * We have to handle nlinks here on the frontend.
	 */
	xop = &hammer2_xop_alloc(dip)->xop_unlink;
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);
	isopen = cache_isopen(ap->a_nch);
	xop->isdir = 0;
	xop->dopermanent = isopen ?  0 : HAMMER2_DELETE_PERMANENT;
	hammer2_xop_start(&xop->head, hammer2_xop_unlink);

	/*
	 * Collect the real inode and adjust nlinks, destroy the real
	 * inode if nlinks transitions to 0 and it was the real inode
	 * (else it has already been removed).
	 */
	error = hammer2_xop_collect(&xop->head, 0);
	hammer2_inode_unlock(dip);

	if (error == 0) {
		ip = hammer2_inode_get(dip->pmp, dip, &xop->head.cluster);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (ip) {
			hammer2_inode_unlink_finisher(ip, isopen);
			hammer2_inode_unlock(ip);
		}
	} else {
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}

	hammer2_inode_run_unlinkq(dip->pmp);
	hammer2_trans_done(dip->pmp);
	if (error == 0)
		cache_unlink(ap->a_nch);
	LOCKSTOP;
	return (error);
}

/*
 * hammer2_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer2_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	struct namecache *ncp;
	int isopen;
	int error;

	LOCKSTART;
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly) {
		LOCKSTOP;
		return(EROFS);
	}

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(dip->pmp, 0);
	hammer2_inode_lock(dip, 0);

	xop = &hammer2_xop_alloc(dip)->xop_unlink;

	ncp = ap->a_nch->ncp;
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);
	isopen = cache_isopen(ap->a_nch);
	xop->isdir = 1;
	xop->dopermanent = isopen ?  0 : HAMMER2_DELETE_PERMANENT;
	hammer2_xop_start(&xop->head, hammer2_xop_unlink);

	/*
	 * Collect the real inode and adjust nlinks, destroy the real
	 * inode if nlinks transitions to 0 and it was the real inode
	 * (else it has already been removed).
	 */
	error = hammer2_xop_collect(&xop->head, 0);
	hammer2_inode_unlock(dip);

	if (error == 0) {
		ip = hammer2_inode_get(dip->pmp, dip, &xop->head.cluster);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (ip) {
			hammer2_inode_unlink_finisher(ip, isopen);
			hammer2_inode_unlock(ip);
		}
	} else {
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}
	hammer2_inode_run_unlinkq(dip->pmp);
	hammer2_trans_done(dip->pmp);
	if (error == 0)
		cache_unlink(ap->a_nch);
	LOCKSTOP;
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
	hammer2_inode_t *cdip;
	hammer2_inode_t *fdip;
	hammer2_inode_t *tdip;
	hammer2_inode_t *ip;
	const uint8_t *fname;
	size_t fname_len;
	const uint8_t *tname;
	size_t tname_len;
	int error;
	int tnch_error;
	hammer2_key_t tlhc;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);	/* source directory */
	tdip = VTOI(ap->a_tdvp);	/* target directory */

	if (fdip->pmp->ronly)
		return(EROFS);

	LOCKSTART;
	fncp = ap->a_fnch->ncp;		/* entry name in source */
	fname = fncp->nc_name;
	fname_len = fncp->nc_nlen;

	tncp = ap->a_tnch->ncp;		/* entry name in target */
	tname = tncp->nc_name;
	tname_len = tncp->nc_nlen;

	hammer2_pfs_memory_wait(tdip->pmp);
	hammer2_trans_init(tdip->pmp, 0);

	/*
	 * ip is the inode being renamed.  If this is a hardlink then
	 * ip represents the actual file and not the hardlink marker.
	 */
	ip = VTOI(fncp->nc_vp);

	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 */
	cdip = hammer2_inode_common_parent(ip->pip, tdip);
	hammer2_inode_lock(cdip, 0);
	hammer2_inode_lock(fdip, 0);
	hammer2_inode_lock(tdip, 0);
	hammer2_inode_ref(ip);		/* extra ref */
	error = 0;

	/*
	 * If ip is a hardlink target and fdip != cdip we must shift the
	 * inode to cdip.
	 */
	if (fdip != cdip &&
	    (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) == 0) {
		hammer2_xop_nlink_t *xop1;

		xop1 = &hammer2_xop_alloc(fdip)->xop_nlink;
		hammer2_xop_setip2(&xop1->head, ip);
		hammer2_xop_setip3(&xop1->head, cdip);

		hammer2_xop_start(&xop1->head, hammer2_xop_nlink);
		error = hammer2_xop_collect(&xop1->head, 0);
		hammer2_xop_retire(&xop1->head, HAMMER2_XOPMASK_VOP);
	}

	/*
	 * Delete the target namespace.
	 */
	{
		hammer2_xop_unlink_t *xop2;
		hammer2_inode_t *tip;
		int isopen;

		/*
		 * The unlink XOP unlinks the path from the directory and
		 * locates and returns the cluster associated with the real
		 * inode.  We have to handle nlinks here on the frontend.
		 */
		xop2 = &hammer2_xop_alloc(tdip)->xop_unlink;
		hammer2_xop_setname(&xop2->head, tname, tname_len);
		isopen = cache_isopen(ap->a_tnch);
		xop2->isdir = -1;
		xop2->dopermanent = isopen ?  0 : HAMMER2_DELETE_PERMANENT;
		hammer2_xop_start(&xop2->head, hammer2_xop_unlink);

		/*
		 * Collect the real inode and adjust nlinks, destroy the real
		 * inode if nlinks transitions to 0 and it was the real inode
		 * (else it has already been removed).
		 */
		tnch_error = hammer2_xop_collect(&xop2->head, 0);
		/* hammer2_inode_unlock(tdip); */

		if (tnch_error == 0) {
			tip = hammer2_inode_get(tdip->pmp, NULL,
						&xop2->head.cluster);
			hammer2_xop_retire(&xop2->head, HAMMER2_XOPMASK_VOP);
			if (tip) {
				hammer2_inode_unlink_finisher(tip, isopen);
				hammer2_inode_unlock(tip);
			}
		} else {
			hammer2_xop_retire(&xop2->head, HAMMER2_XOPMASK_VOP);
		}
		/* hammer2_inode_lock(tdip, 0); */

		if (tnch_error && tnch_error != ENOENT) {
			error = tnch_error;
			goto done2;
		}
	}

	/*
	 * Resolve the collision space for (tdip, tname, tname_len)
	 *
	 * tdip must be held exclusively locked to prevent races.
	 */
	{
		hammer2_xop_scanlhc_t *sxop;
		hammer2_tid_t lhcbase;

		tlhc = hammer2_dirhash(tname, tname_len);
		lhcbase = tlhc;
		sxop = &hammer2_xop_alloc(tdip)->xop_scanlhc;
		sxop->lhc = tlhc;
		hammer2_xop_start(&sxop->head, hammer2_xop_scanlhc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (tlhc != sxop->head.cluster.focus->bref.key)
				break;
			++tlhc;
		}
		hammer2_xop_retire(&sxop->head, HAMMER2_XOPMASK_VOP);

		if (error) {
			if (error != ENOENT)
				goto done2;
			++tlhc;
			error = 0;
		}
		if ((lhcbase ^ tlhc) & ~HAMMER2_DIRHASH_LOMASK) {
			error = ENOSPC;
			goto done2;
		}
	}

	/*
	 * Everything is setup, do the rename.
	 *
	 * We have to synchronize ip->meta to the underlying operation.
	 *
	 * NOTE: To avoid deadlocks we cannot lock (ip) while we are
	 *	 unlinking elements from their directories.  Locking
	 *	 the nlinks field does not lock the whole inode.
	 */
	hammer2_inode_lock(ip, 0);
	if (error == 0) {
		hammer2_xop_nrename_t *xop4;

		xop4 = &hammer2_xop_alloc(fdip)->xop_nrename;
		xop4->lhc = tlhc;
		xop4->ip_key = ip->meta.name_key;
		hammer2_xop_setip2(&xop4->head, ip);
		hammer2_xop_setip3(&xop4->head, tdip);
		hammer2_xop_setname(&xop4->head, fname, fname_len);
		hammer2_xop_setname2(&xop4->head, tname, tname_len);
		hammer2_xop_start(&xop4->head, hammer2_xop_nrename);

		error = hammer2_xop_collect(&xop4->head, 0);
		hammer2_xop_retire(&xop4->head, HAMMER2_XOPMASK_VOP);

		if (error == ENOENT)
			error = 0;
		if (error == 0 &&
		    (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
			hammer2_inode_modify(ip);
			ip->meta.name_len = tname_len;
			ip->meta.name_key = tlhc;

		}
	}

	/*
	 * Fixup ip->pip if we were renaming the actual file and not a
	 * hardlink pointer.
	 */
	if (error == 0 && (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
		hammer2_inode_t *opip;

		if (ip->pip != tdip) {
			hammer2_inode_ref(tdip);
			opip = ip->pip;
			ip->pip = tdip;
			if (opip)
				hammer2_inode_drop(opip);
		}
	}
	hammer2_inode_unlock(ip);
done2:
	hammer2_inode_unlock(tdip);
	hammer2_inode_unlock(fdip);
	hammer2_inode_unlock(cdip);
	hammer2_inode_drop(ip);
	hammer2_inode_drop(cdip);
	hammer2_inode_run_unlinkq(fdip->pmp);
	hammer2_trans_done(tdip->pmp);

	/*
	 * Issue the namecache update after unlocking all the internal
	 * hammer structures, otherwise we might deadlock.
	 */
	if (tnch_error == 0) {
		cache_unlink(ap->a_tnch);
		cache_setunresolved(ap->a_tnch);
	}
	if (error == 0)
		cache_rename(ap->a_fnch, ap->a_tnch);

	LOCKSTOP;
	return (error);
}

/*
 * hammer2_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer2_vop_ioctl(struct vop_ioctl_args *ap)
{
	hammer2_inode_t *ip;
	int error;

	LOCKSTART;
	ip = VTOI(ap->a_vp);

	error = hammer2_ioctl(ip, ap->a_command, (void *)ap->a_data,
			      ap->a_fflag, ap->a_cred);
	LOCKSTOP;
	return (error);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	hammer2_pfs_t *pmp;
	int rc;

	LOCKSTART;
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
	LOCKSTOP;
	return (rc);
}

/*
 * KQFILTER
 */
static void filt_hammer2detach(struct knote *kn);
static int filt_hammer2read(struct knote *kn, long hint);
static int filt_hammer2write(struct knote *kn, long hint);
static int filt_hammer2vnode(struct knote *kn, long hint);

static struct filterops hammer2read_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2read };
static struct filterops hammer2write_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2write };
static struct filterops hammer2vnode_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2vnode };

static
int
hammer2_vop_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &hammer2read_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &hammer2write_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &hammer2vnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);

	return(0);
}

static void
filt_hammer2detach(struct knote *kn)
{
	struct vnode *vp = (void *)kn->kn_hook;

	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
}

static int
filt_hammer2read(struct knote *kn, long hint)
{
	struct vnode *vp = (void *)kn->kn_hook;
	hammer2_inode_t *ip = VTOI(vp);
	off_t off;

	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return(1);
	}
	off = ip->meta.size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI)
		return(1);
	return (kn->kn_data != 0);
}


static int
filt_hammer2write(struct knote *kn, long hint)
{
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
	kn->kn_data = 0;
	return (1);
}

static int
filt_hammer2vnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}
	return (kn->kn_fflags != 0);
}

/*
 * FIFO VOPS
 */
static
int
hammer2_vop_markatime(struct vop_markatime_args *ap)
{
	hammer2_inode_t *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

	if (ip->pmp->ronly)
		return(EROFS);
	return(0);
}

static
int
hammer2_vop_fifokqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = hammer2_vop_kqfilter(ap);
	return(error);
}

/*
 * VOPS vector
 */
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
	.vop_nmknod 	= hammer2_vop_nmknod,
	.vop_ioctl	= hammer2_vop_ioctl,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
        .vop_kqfilter	= hammer2_vop_kqfilter
};

struct vop_ops hammer2_spec_vops = {
        .vop_default =          vop_defaultop,
        .vop_fsync =            hammer2_vop_fsync,
        .vop_read =             vop_stdnoread,
        .vop_write =            vop_stdnowrite,
        .vop_access =           hammer2_vop_access,
        .vop_close =            hammer2_vop_close,
        .vop_markatime =        hammer2_vop_markatime,
        .vop_getattr =          hammer2_vop_getattr,
        .vop_inactive =         hammer2_vop_inactive,
        .vop_reclaim =          hammer2_vop_reclaim,
        .vop_setattr =          hammer2_vop_setattr
};

struct vop_ops hammer2_fifo_vops = {
        .vop_default =          fifo_vnoperate,
        .vop_fsync =            hammer2_vop_fsync,
#if 0
        .vop_read =             hammer2_vop_fiforead,
        .vop_write =            hammer2_vop_fifowrite,
#endif
        .vop_access =           hammer2_vop_access,
#if 0
        .vop_close =            hammer2_vop_fifoclose,
#endif
        .vop_markatime =        hammer2_vop_markatime,
        .vop_getattr =          hammer2_vop_getattr,
        .vop_inactive =         hammer2_vop_inactive,
        .vop_reclaim =          hammer2_vop_reclaim,
        .vop_setattr =          hammer2_vop_setattr,
        .vop_kqfilter =         hammer2_vop_fifokqfilter
};

