/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2022 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

/*
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>
*/

#include "hammer2.h"

/*
static int hammer2_read_file(hammer2_inode_t *ip, struct uio *uio,
				int seqcount);
*/
static int hammer2_write_file(hammer2_inode_t *ip, struct uio *uio,
				int ioflag, int seqcount);
static void hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize);
static void hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize);

/*
 * Last reference to a vnode is going away but it is still cached.
 */
static
int
hammer2_vop_inactive(struct vop_inactive_args *ap)
{
#if 0
	hammer2_inode_t *ip;
	struct m_vnode *vp;

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
	 * Aquire the inode lock to interlock against vp updates via
	 * the inode path and file deletions and such (which can be
	 * namespace-only operations that might not hold the vnode).
	 */
	hammer2_inode_lock(ip, 0);
	if (ip->flags & HAMMER2_INODE_ISUNLINKED) {
		hammer2_key_t lbase;
		int nblksize;

		/*
		 * If the inode has been unlinked we can throw away all
		 * buffers (dirty or not) and clean the file out.
		 *
		 * Because vrecycle() calls are not guaranteed, try to
		 * dispose of the inode as much as possible right here.
		 */
		nblksize = hammer2_calc_logical(ip, 0, &lbase, NULL);
		nvtruncbuf(vp, 0, nblksize, 0, 0);

		/*
		 * Delete the file on-media.
		 */
		if ((ip->flags & HAMMER2_INODE_DELETING) == 0) {
			atomic_set_int(&ip->flags, HAMMER2_INODE_DELETING);
			hammer2_inode_delayed_sideq(ip);
		}
		hammer2_inode_unlock(ip);

		/*
		 * Recycle immediately if possible
		 */
		vrecycle(vp);
	} else {
		hammer2_inode_unlock(ip);
	}
	return (0);
#endif
	return (EOPNOTSUPP);
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
	struct m_vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL)
		return(0);

	/*
	 * NOTE! We do not attempt to flush chains here, flushing is
	 *	 really fragile and could also deadlock.
	 */
	vclrisdirty(vp);

	/*
	 * The inode lock is required to disconnect it.
	 */
	hammer2_inode_lock(ip, 0);
	vp->v_data = NULL;
	ip->vp = NULL;

	/*
	 * Delete the file on-media.  This should have been handled by the
	 * inactivation.  The operation is likely still queued on the inode
	 * though so only complain if the stars don't align.
	 */
	if ((ip->flags & (HAMMER2_INODE_ISUNLINKED | HAMMER2_INODE_DELETING)) ==
	    HAMMER2_INODE_ISUNLINKED)
	{
		assert(0);
		atomic_set_int(&ip->flags, HAMMER2_INODE_DELETING);
		hammer2_inode_delayed_sideq(ip);
		kprintf("hammer2: vp=%p ip=%p unlinked but not disposed\n",
			vp, ip);
	}
	hammer2_inode_unlock(ip);

	/*
	 * Modified inodes will already be on SIDEQ or SYNCQ, no further
	 * action is needed.
	 *
	 * We cannot safely synchronize the inode from inside the reclaim
	 * due to potentially deep locks held as-of when the reclaim occurs.
	 * Interactions and potential deadlocks abound.  We also can't do it
	 * here without desynchronizing from the related directory entrie(s).
	 */
	hammer2_inode_drop(ip);			/* vp ref */

	/*
	 * XXX handle background sync when ip dirty, kernel will no longer
	 * notify us regarding this inode because there is no longer a
	 * vnode attached to it.
	 */

	return (0);
}

int
hammer2_reclaim(struct m_vnode *vp)
{
	struct vop_reclaim_args ap = {
		.a_vp = vp,
	};

	return hammer2_vop_reclaim(&ap);
}

/*
 * Currently this function synchronizes the front-end inode state to the
 * backend chain topology, then flushes the inode's chain and sub-topology
 * to backend media.  This function does not flush the root topology down to
 * the inode.
 */
static
int
hammer2_vop_fsync(struct vop_fsync_args *ap)
{
#if 0
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	int error1;
	int error2;

	vp = ap->a_vp;
	ip = VTOI(vp);
	error1 = 0;

	hammer2_trans_init(ip->pmp, 0);

	/*
	 * Flush dirty buffers in the file's logical buffer cache.
	 * It is best to wait for the strategy code to commit the
	 * buffers to the device's backing buffer cache before
	 * then trying to flush the inode.
	 *
	 * This should be quick, but certain inode modifications cached
	 * entirely in the hammer2_inode structure may not trigger a
	 * buffer read until the flush so the fsync can wind up also
	 * doing scattered reads.
	 */
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);
	bio_track_wait(&vp->v_track_write, 0, 0);

	/*
	 * Flush any inode changes
	 */
	hammer2_inode_lock(ip, 0);
	if (ip->flags & (HAMMER2_INODE_RESIZED|HAMMER2_INODE_MODIFIED))
		error1 = hammer2_inode_chain_sync(ip);

	/*
	 * Flush dirty chains related to the inode.
	 *
	 * NOTE! We are not in a flush transaction.  The inode remains on
	 *	 the sideq so the filesystem syncer can synchronize it to
	 *	 the volume root.
	 */
	error2 = hammer2_inode_chain_flush(ip, HAMMER2_XOP_INODE_STOP);
	if (error2)
		error1 = error2;

	/*
	 * We may be able to clear the vnode dirty flag.
	 */
	if ((ip->flags & (HAMMER2_INODE_MODIFIED |
			  HAMMER2_INODE_RESIZED |
			  HAMMER2_INODE_DIRTYDATA)) == 0 &&
	    RB_EMPTY(&vp->v_rbdirty_tree) &&
	    !bio_track_active(&vp->v_track_write)) {
		vclrisdirty(vp);
	}
	hammer2_inode_unlock(ip);
	hammer2_trans_done(ip->pmp, 0);

	return (error1);
#endif
	return (EOPNOTSUPP);
}

/*
 * No lock needed, just handle ip->update
 */
static
int
hammer2_vop_access(struct vop_access_args *ap)
{
#if 0
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	uid_t uid;
	gid_t gid;
	mode_t mode;
	uint32_t uflags;
	int error;
	int update;

retry:
	update = spin_access_start(&ip->cluster_spin);

	/*hammer2_inode_lock(ip, HAMMER2_RESOLVE_SHARED);*/
	uid = hammer2_to_unix_xid(&ip->meta.uid);
	gid = hammer2_to_unix_xid(&ip->meta.gid);
	mode = ip->meta.mode;
	uflags = ip->meta.uflags;
	/*hammer2_inode_unlock(ip);*/

	if (__predict_false(spin_access_end(&ip->cluster_spin, update)))
		goto retry;

	error = vop_helper_access(ap, uid, gid, mode, uflags);

	return (error);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
#if 0
	hammer2_pfs_t *pmp;
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	struct vattr *vap;
	int update;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	pmp = ip->pmp;

retry:
	update = spin_access_start(&ip->cluster_spin);

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
	vap->va_bytes = 0;
	if (ip->meta.type == HAMMER2_OBJTYPE_DIRECTORY) {
		/*
		 * Can't really calculate directory use sans the files under
		 * it, just assume one block for now.
		 */
		vap->va_bytes += HAMMER2_INODE_BYTES;
	} else {
		vap->va_bytes = hammer2_inode_data_count(ip);
	}
	vap->va_type = hammer2_get_vtype(ip->meta.type);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ip->meta.uid;
	vap->va_gid_uuid = ip->meta.gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	if (__predict_false(spin_access_end(&ip->cluster_spin, update)))
		goto retry;

	return (0);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_getattr_lite(struct vop_getattr_lite_args *ap)
{
#if 0
	hammer2_pfs_t *pmp;
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	struct vattr_lite *lvap;
	int update;

	vp = ap->a_vp;
	lvap = ap->a_lvap;

	ip = VTOI(vp);
	pmp = ip->pmp;

retry:
	update = spin_access_start(&ip->cluster_spin);

#if 0
	vap->va_fsid = pmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ip->meta.inum;
#endif
	lvap->va_mode = ip->meta.mode;
	lvap->va_nlink = ip->meta.nlinks;
	lvap->va_uid = hammer2_to_unix_xid(&ip->meta.uid);
	lvap->va_gid = hammer2_to_unix_xid(&ip->meta.gid);
#if 0
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
#endif
	lvap->va_size = ip->meta.size;
#if 0
	vap->va_blocksize = HAMMER2_PBUFSIZE;
#endif
	lvap->va_flags = ip->meta.uflags;
	lvap->va_type = hammer2_get_vtype(ip->meta.type);
#if 0
	vap->va_filerev = 0;
	vap->va_uid_uuid = ip->meta.uid;
	vap->va_gid_uuid = ip->meta.gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;
#endif

	if (__predict_false(spin_access_end(&ip->cluster_spin, update)))
		goto retry;

	return (0);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
#if 0
	hammer2_inode_t *ip;
	struct m_vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	uint64_t ctime;

	vp = ap->a_vp;
	vap = ap->a_vap;
	hammer2_update_time(&ctime);

	ip = VTOI(vp);

	if (ip->pmp->ronly)
		return (EROFS);

	/*
	 * Normally disallow setattr if there is no space, unless we
	 * are in emergency mode (might be needed to chflags -R noschg
	 * files prior to removal).
	 */
	if ((ip->pmp->flags & HAMMER2_PMPF_EMERG) == 0 &&
	    hammer2_vfs_enospace(ip, 0, ap->a_cred) > 1) {
		return (ENOSPC);
	}

	hammer2_trans_init(ip->pmp, 0);
	hammer2_inode_lock(ip, 0);
	error = 0;

	if (vap->va_flags != VNOVAL) {
		uint32_t flags;

		flags = ip->meta.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
				     hammer2_to_unix_xid(&ip->meta.uid),
				     ap->a_cred);
		if (error == 0) {
			if (ip->meta.uflags != flags) {
				hammer2_inode_modify(ip);
				hammer2_spin_lock_update(&ip->cluster_spin);
				ip->meta.uflags = flags;
				ip->meta.ctime = ctime;
				hammer2_spin_unlock_update(&ip->cluster_spin);
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
				hammer2_spin_lock_update(&ip->cluster_spin);
				ip->meta.uid = uuid_uid;
				ip->meta.gid = uuid_gid;
				ip->meta.mode = cur_mode;
				ip->meta.ctime = ctime;
				hammer2_spin_unlock_update(&ip->cluster_spin);
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
				hammer2_mtx_ex(&ip->truncate_lock);
				hammer2_truncate_file(ip, vap->va_size);
				hammer2_mtx_unlock(&ip->truncate_lock);
				kflags |= NOTE_WRITE;
			} else {
				hammer2_extend_file(ip, vap->va_size);
				kflags |= NOTE_WRITE | NOTE_EXTEND;
			}
			hammer2_inode_modify(ip);
			ip->meta.mtime = ctime;
			vclrflags(vp, VLASTWRITETS);
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
		if (error == 0) {
			hammer2_inode_modify(ip);
			hammer2_spin_lock_update(&ip->cluster_spin);
			ip->meta.mode = cur_mode;
			ip->meta.ctime = ctime;
			hammer2_spin_unlock_update(&ip->cluster_spin);
			kflags |= NOTE_ATTRIB;
		}
	}

	if (vap->va_mtime.tv_sec != VNOVAL) {
		hammer2_inode_modify(ip);
		ip->meta.mtime = hammer2_timespec_to_time(&vap->va_mtime);
		kflags |= NOTE_ATTRIB;
		vclrflags(vp, VLASTWRITETS);
	}

done:
	/*
	 * If a truncation occurred we must call chain_sync() now in order
	 * to trim the related data chains, otherwise a later expansion can
	 * cause havoc.
	 *
	 * If an extend occured that changed the DIRECTDATA state, we must
	 * call inode_chain_sync now in order to prepare the inode's indirect
	 * block table.
	 *
	 * WARNING! This means we are making an adjustment to the inode's
	 * chain outside of sync/fsync, and not just to inode->meta, which
	 * may result in some consistency issues if a crash were to occur
	 * at just the wrong time.
	 */
	if (ip->flags & HAMMER2_INODE_RESIZED)
		hammer2_inode_chain_sync(ip);

	/*
	 * Cleanup.
	 */
	hammer2_inode_unlock(ip);
	hammer2_trans_done(ip->pmp, HAMMER2_TRANS_SIDEQ);
	hammer2_knote(ip->vp, kflags);

	return (error);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
#if 0
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
	int r;

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
		inum = ip->meta.inum & HAMMER2_DIRHASH_USERMSK;
		if (ip != ip->pmp->iroot)
			inum = ip->meta.iparent & HAMMER2_DIRHASH_USERMSK;
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

	xop = hammer2_xop_alloc(ip, 0);
	xop->lkey = lkey;
	hammer2_xop_start(&xop->head, &hammer2_readdir_desc);

	for (;;) {
		const hammer2_inode_data_t *ripdata;
		const char *dname;
		int dtype;

		error = hammer2_xop_collect(&xop->head, 0);
		error = hammer2_error_to_errno(error);
		if (error) {
			break;
		}
		if (cookie_index == ncookies)
			break;
		if (hammer2_debug & 0x0020)
			kprintf("cluster chain %p %p\n",
				xop->head.cluster.focus,
				(xop->head.cluster.focus ?
				 xop->head.cluster.focus->data : (void *)-1));
		hammer2_cluster_bref(&xop->head.cluster, &bref);

		if (bref.type == HAMMER2_BREF_TYPE_INODE) {
			ripdata = &hammer2_xop_gdata(&xop->head)->ipdata;
			dtype = hammer2_get_dtype(ripdata->meta.type);
			saveoff = bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     ripdata->meta.inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype,
					     ripdata->meta.name_len,
					     ripdata->filename);
			hammer2_xop_pdata(&xop->head);
			if (r)
				break;
			if (cookies)
				cookies[cookie_index] = saveoff;
			++cookie_index;
		} else if (bref.type == HAMMER2_BREF_TYPE_DIRENT) {
			uint16_t namlen;

			dtype = hammer2_get_dtype(bref.embed.dirent.type);
			saveoff = bref.key & HAMMER2_DIRHASH_USERMSK;
			namlen = bref.embed.dirent.namlen;
			if (namlen <= sizeof(bref.check.buf)) {
				dname = bref.check.buf;
			} else {
				dname = hammer2_xop_gdata(&xop->head)->buf;
			}
			r = vop_write_dirent(&error, uio,
					     bref.embed.dirent.inum, dtype,
					     namlen, dname);
			if (namlen > sizeof(bref.check.buf))
				hammer2_xop_pdata(&xop->head);
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
	return (error);
#endif
	return (EOPNOTSUPP);
}

/*
 * hammer2_vop_readlink { vp, uio, cred }
 */
static
int
hammer2_vop_readlink(struct vop_readlink_args *ap)
{
#if 0
	struct m_vnode *vp;
	hammer2_inode_t *ip;
	int error;

	vp = ap->a_vp;
	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);

	error = hammer2_read_file(ip, ap->a_uio, 0);
	return (error);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
#if 0
	struct m_vnode *vp;
	hammer2_inode_t *ip;
	struct uio *uio;
	int error;
	int seqcount;

	/*
	 * Read operations supported on this vnode?
	 */
	vp = ap->a_vp;
	if (vp->v_type == VDIR)
		return (EISDIR);
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Misc
	 */
	ip = VTOI(vp);
	uio = ap->a_uio;
	error = 0;

	seqcount = ap->a_ioflag >> IO_SEQSHIFT;

	error = hammer2_read_file(ip, uio, seqcount);
	return (error);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_write(struct vop_write_args *ap)
{
	hammer2_inode_t *ip;
	//thread_t td;
	struct m_vnode *vp;
	struct uio *uio;
	int error;
	int seqcount;
	int ioflag;

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
	ioflag = ap->a_ioflag;
	uio = ap->a_uio;
	error = 0;
	if (ip->pmp->ronly || (ip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	switch (hammer2_vfs_enospace(ip, uio->uio_resid, ap->a_cred)) {
	case 2:
		return (ENOSPC);
	case 1:
		ioflag |= IO_DIRECT;	/* semi-synchronous */
		/* fall through */
	default:
		break;
	}

	seqcount = ioflag >> IO_SEQSHIFT;

	/*
	 * Check resource limit
	 */
	/*
	if (uio->uio_resid > 0 && (td = uio->uio_td) != NULL && td->td_proc &&
	    uio->uio_offset + uio->uio_resid >
	     td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}
	*/

	/*
	 * The transaction interlocks against flush initiations
	 * (note: but will run concurrently with the actual flush).
	 *
	 * To avoid deadlocking against the VM system, we must flag any
	 * transaction related to the buffer cache or other direct
	 * VM page manipulation.
	 */
	if (uio->uio_segflg == UIO_NOCOPY) {
		assert(0); /* no UIO_NOCOPY in makefs */
		hammer2_trans_init(ip->pmp, HAMMER2_TRANS_BUFCACHE);
	} else {
		hammer2_trans_init(ip->pmp, 0);
	}
	error = hammer2_write_file(ip, uio, ioflag, seqcount);
	if (uio->uio_segflg == UIO_NOCOPY) {
		assert(0); /* no UIO_NOCOPY in makefs */
		hammer2_trans_done(ip->pmp, HAMMER2_TRANS_BUFCACHE |
					    HAMMER2_TRANS_SIDEQ);
	} else
		hammer2_trans_done(ip->pmp, HAMMER2_TRANS_SIDEQ);

	return (error);
}

int
hammer2_write(struct m_vnode *vp, void *buf, size_t size, off_t offset)
{
	assert(buf);
	assert(size > 0);
	assert(size <= HAMMER2_PBUFSIZE);

	struct iovec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	struct uio uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_offset = offset,
		.uio_resid = size,
		.uio_segflg = UIO_USERSPACE,
		.uio_rw = UIO_WRITE,
		.uio_td = NULL,
	};
	struct vop_write_args ap = {
		.a_vp = vp,
		.a_uio = &uio,
		.a_ioflag = 0,
		.a_cred = NULL,
	};

	return hammer2_vop_write(&ap);
}

#if 0
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
	struct m_buf *bp;
	int error;

	error = 0;

	/*
	 * UIO read loop.
	 *
	 * WARNING! Assumes that the kernel interlocks size changes at the
	 *	    vnode level.
	 */
	hammer2_mtx_sh(&ip->lock);
	hammer2_mtx_sh(&ip->truncate_lock);
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

#if 1
		bp = NULL;
		error = cluster_readx(ip->vp, leof, lbase, lblksize,
				      B_NOTMETA | B_KVABIO,
				      uio->uio_resid,
				      seqcount * MAXBSIZE,
				      &bp);
#else
		if (uio->uio_segflg == UIO_NOCOPY) {
			bp = getblk(ip->vp, lbase, lblksize,
				    GETBLK_BHEAVY | GETBLK_KVABIO, 0);
			if (bp->b_flags & B_CACHE) {
				int i;
				int j = 0;
				if (bp->b_xio.xio_npages != 16)
					kprintf("NPAGES BAD\n");
				for (i = 0; i < bp->b_xio.xio_npages; ++i) {
					vm_page_t m;
					m = bp->b_xio.xio_pages[i];
					if (m == NULL || m->valid == 0) {
						kprintf("bp %016jx %016jx pg %d inv",
							lbase, leof, i);
						if (m)
							kprintf("m->object %p/%p", m->object, ip->vp->v_object);
						kprintf("\n");
						j = 1;
					}
				}
				if (j)
					kprintf("b_flags %08x, b_error %d\n", bp->b_flags, bp->b_error);
			}
			bqrelse(bp);
		}
		error = bread_kvabio(ip->vp, lbase, lblksize, &bp);
#endif
		if (error) {
			brelse(bp);
			break;
		}
		bkvasync(bp);
		loff = (int)(uio->uio_offset - lbase);
		n = lblksize - loff;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > size - uio->uio_offset)
			n = (int)(size - uio->uio_offset);
		bp->b_flags |= B_AGE;
		uiomovebp(bp, (char *)bp->b_data + loff, n, uio);
		bqrelse(bp);
	}
	hammer2_mtx_unlock(&ip->truncate_lock);

	return (error);
}
#endif

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
	struct m_buf *bp;
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
	hammer2_mtx_sh(&ip->truncate_lock);
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

		KKASSERT(lblksize <= MAXBSIZE);

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
		if (lbase >= new_eof)
			trivial = 1;
		trivial = 1; /* force trivial for makefs */

		/*
		 * Get the buffer
		 */
		if (uio->uio_segflg == UIO_NOCOPY) {
			assert(0); /* no UIO_NOCOPY in makefs */
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblkx(ip->vp, lbase, lblksize,
				    GETBLK_BHEAVY | GETBLK_KVABIO, 0);
			/*
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread_kvabio(ip->vp, lbase,
						     lblksize, &bp);
			}
			*/
		} else if (trivial) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a
			 * mmap/write visibility issue.
			 */
			bp = getblkx(ip->vp, lbase, lblksize,
				    GETBLK_BHEAVY | GETBLK_KVABIO, 0);
			/*
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
			*/
		} else {
			assert(0); /* no partial write in makefs */
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 *
			 * (The strategy code will detect zero-fill physical
			 * blocks for this case).
			 */
			error = bread_kvabio(ip->vp, lbase, lblksize, &bp);
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
		bkvasync(bp);
		error = uiomovebp(bp, (char *)bp->b_data + loff, n, uio);
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
		 *
		 * NOTE!    H2 relies on cluster_write() here because it
		 *	    cannot preallocate disk blocks at the logical
		 *	    level due to not knowing what the compression
		 *	    size will be at this time.
		 *
		 *	    We must use cluster_write() here and we depend
		 *	    on the write-behind feature to flush buffers
		 *	    appropriately.  If we let the buffer daemons do
		 *	    it the block allocations will be all over the
		 *	    map.
		 */
		if (1) {
			bp->b_cmd = BUF_CMD_WRITE;

			struct bio bio;
			bio.bio_buf = bp;
			bio.bio_offset = lbase;

			struct vop_strategy_args ap;
			ap.a_vp = ip->vp;
			ap.a_bio = &bio;

			error = hammer2_vop_strategy(&ap);
			assert(!error);

			brelse(bp);
		} else if (ioflag & IO_SYNC) {
			assert(0);
			bwrite(bp);
		} else if ((ioflag & IO_DIRECT) && endofblk) {
			assert(0);
			bawrite(bp);
		} else if (ioflag & IO_ASYNC) {
			assert(0);
			bawrite(bp);
		} else if (0 /*ip->vp->v_mount->mnt_flag & MNT_NOCLUSTERW*/) {
			assert(0);
			bdwrite(bp);
		} else {
			assert(0);
#if 0
#if 1
			bp->b_flags |= B_CLUSTEROK;
			cluster_write(bp, new_eof, lblksize, seqcount);
#else
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
#endif
#endif
		}
	}

	/*
	 * Cleanup.  If we extended the file EOF but failed to write through
	 * the entire write is a failure and we have to back-up.
	 */
	if (error && new_eof != old_eof) {
		hammer2_mtx_unlock(&ip->truncate_lock);
		hammer2_mtx_ex(&ip->lock);		/* note lock order */
		hammer2_mtx_ex(&ip->truncate_lock);	/* note lock order */
		hammer2_truncate_file(ip, old_eof);
		if (ip->flags & HAMMER2_INODE_MODIFIED)
			hammer2_inode_chain_sync(ip);
		hammer2_mtx_unlock(&ip->lock);
	} else if (modified) {
		struct m_vnode *vp = ip->vp;

		hammer2_mtx_ex(&ip->lock);
		hammer2_inode_modify(ip);
		if (uio->uio_segflg == UIO_NOCOPY) {
			assert(0); /* no UIO_NOCOPY in makefs */
			/*
			if (vp->v_flag & VLASTWRITETS) {
				ip->meta.mtime =
				    (unsigned long)vp->v_lastwrite_ts.tv_sec *
				    1000000 +
				    vp->v_lastwrite_ts.tv_nsec / 1000;
			}
			*/
		} else {
			hammer2_update_time(&ip->meta.mtime);
			vclrflags(vp, VLASTWRITETS);
		}

#if 0
		/*
		 * REMOVED - handled by hammer2_extend_file().  Do not issue
		 * a chain_sync() outside of a sync/fsync except for DIRECTDATA
		 * state changes.
		 *
		 * Under normal conditions we only issue a chain_sync if
		 * the inode's DIRECTDATA state changed.
		 */
		if (ip->flags & HAMMER2_INODE_RESIZED)
			hammer2_inode_chain_sync(ip);
#endif
		hammer2_mtx_unlock(&ip->lock);
		hammer2_knote(ip->vp, kflags);
	}
	hammer2_trans_assert_strategy(ip->pmp);
	hammer2_mtx_unlock(&ip->truncate_lock);

	return error;
}

/*
 * Truncate the size of a file.  The inode must be locked.
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
	atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);
	hammer2_inode_modify(ip);
}

/*
 * Extend the size of a file.  The inode must be locked.
 *
 * Even though the file size is changing, we do not have to set the
 * INODE_RESIZED bit unless the file size crosses the EMBEDDED_BYTES
 * boundary.  When this occurs a hammer2_inode_chain_sync() is required
 * to prepare the inode cluster's indirect block table, otherwise
 * async execution of the strategy code will implode on us.
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
	int error;

	KKASSERT((ip->flags & HAMMER2_INODE_RESIZED) == 0);
	hammer2_inode_modify(ip);
	osize = ip->meta.size;
	ip->osize = osize;
	ip->meta.size = nsize;

	/*
	 * We must issue a chain_sync() when the DIRECTDATA state changes
	 * to prevent confusion between the flush code and the in-memory
	 * state.  This is not perfect because we are doing it outside of
	 * a sync/fsync operation, so it might not be fully synchronized
	 * with the meta-data topology flush.
	 *
	 * We must retain and re-dirty the buffer cache buffer containing
	 * the direct data so it can be written to a real block.  It should
	 * not be possible for a bread error to occur since the original data
	 * is extracted from the inode structure directly.
	 */
	if (osize <= HAMMER2_EMBEDDED_BYTES && nsize > HAMMER2_EMBEDDED_BYTES) {
		if (osize) {
			assert(0); /* no such transition in makefs */
			struct m_buf *bp;

			oblksize = hammer2_calc_logical(ip, 0, NULL, NULL);
			error = bread_kvabio(ip->vp, 0, oblksize, &bp);
			atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);
			hammer2_inode_chain_sync(ip);
			if (error == 0) {
				bheavy(bp);
				bdwrite(bp);
			} else {
				brelse(bp);
			}
		} else {
			atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);
			hammer2_inode_chain_sync(ip);
		}
	}
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
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	hammer2_xop_nresolve_t *xop;
	hammer2_inode_t *ip;
	hammer2_inode_t *dip;
	struct namecache *ncp;
	struct m_vnode *vp;
	int error;

	dip = VTOI(ap->a_dvp);
	xop = hammer2_xop_alloc(dip, 0);

	ncp = ap->a_nch->ncp;
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);
	hammer2_xop_start(&xop->head, &hammer2_nresolve_desc);

	error = hammer2_xop_collect(&xop->head, 0);
	error = hammer2_error_to_errno(error);
	if (error) {
		ip = NULL;
	} else {
		ip = hammer2_inode_get(dip->pmp, &xop->head, -1, -1);
	}
	hammer2_inode_unlock(dip);

	/*
	 * Acquire the related vnode
	 *
	 * NOTE: For error processing, only ENOENT resolves the namecache
	 *	 entry to NULL, otherwise we just return the error and
	 *	 leave the namecache unresolved.
	 *
	 * WARNING: inode structure is locked exclusively via inode_get
	 *	    but chain was locked shared.  inode_unlock()
	 *	    will handle it properly.
	 */
	if (ip) {
		vp = hammer2_igetv(ip, &error);	/* error set to UNIX error */
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			*ap->a_vpp = vp;
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
	/*
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, ap));
	*/

	return error;
}

int
hammer2_nresolve(struct m_vnode *dvp, struct m_vnode **vpp, char *name, int nlen)
{
	*vpp = NULL;
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	struct vop_nresolve_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vpp = vpp,
	};

	return hammer2_vop_nresolve(&ap);
}

static
int
hammer2_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
#if 0
	hammer2_inode_t *dip;
	hammer2_tid_t inum;
	int error;

	dip = VTOI(ap->a_dvp);
	inum = dip->meta.iparent;
	*ap->a_vpp = NULL;

	if (inum) {
		error = hammer2_vfs_vget(ap->a_dvp->v_mount, NULL,
					 inum, ap->a_vpp);
	} else {
		error = ENOENT;
	}
	return error;
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	struct namecache *ncp;
	const char *name;
	size_t name_len;
	hammer2_tid_t inum;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly || (dip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_trans_init(dip->pmp, 0);

	inum = hammer2_trans_newinum(dip->pmp);

	/*
	 * Create the actual inode as a hidden file in the iroot, then
	 * create the directory entry.  The creation of the actual inode
	 * sets its nlinks to 1 which is the value we desire.
	 *
	 * dip must be locked before nip to avoid deadlock.
	 */
	hammer2_inode_lock(dip, 0);
	nip = hammer2_inode_create_normal(dip, ap->a_vap, ap->a_cred,
					  inum, &error);
	if (error) {
		error = hammer2_error_to_errno(error);
	} else {
		error = hammer2_dirent_create(dip, name, name_len,
					      nip->meta.inum, nip->meta.type);
		/* returns UNIX error code */
	}
	if (error) {
		if (nip) {
			hammer2_inode_unlink_finisher(nip, NULL);
			hammer2_inode_unlock(nip);
			nip = NULL;
		}
		*ap->a_vpp = NULL;
	} else {
		/*
		 * inode_depend() must occur before the igetv() because
		 * the igetv() can temporarily release the inode lock.
		 */
		hammer2_inode_depend(dip, nip);	/* before igetv */
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}

	/*
	 * Update dip's mtime
	 *
	 * We can use a shared inode lock and allow the meta.mtime update
	 * SMP race.  hammer2_inode_modify() is MPSAFE w/a shared lock.
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		hammer2_knote(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	}
	return error;
}

int
hammer2_nmkdir(struct m_vnode *dvp, struct m_vnode **vpp, char *name, int nlen,
		mode_t mode)
{
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	uid_t va_uid = VNOVAL; //getuid();
	uid_t va_gid = VNOVAL; //getgid();
	struct vattr va = {
		.va_type = VDIR,
		.va_mode = mode & ~S_IFMT,
		.va_uid = va_uid,
		.va_gid = va_gid,
	};
	struct vop_nmkdir_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vpp = vpp,
		.a_vap = &va,
	};

	return hammer2_vop_nmkdir(&ap);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
#if 0
	return vop_stdopen(ap);
#endif
	return (EOPNOTSUPP);
}

/*
 * hammer2_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer2_vop_advlock(struct vop_advlock_args *ap)
{
#if 0
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	hammer2_off_t size;

	size = ip->meta.size;
	return (lf_advlock(ap, &ip->advlock, size));
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_close(struct vop_close_args *ap)
{
#if 0
	return vop_stdclose(ap);
#endif
	return (EOPNOTSUPP);
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
	hammer2_inode_t *tdip;	/* target directory to create link in */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	struct namecache *ncp;
	const char *name;
	size_t name_len;
	int error;
	uint64_t cmtime;

	/* We know it's the same in makefs */
	/*
	if (ap->a_dvp->v_mount != ap->a_vp->v_mount)
		return(EXDEV);
	*/

	tdip = VTOI(ap->a_dvp);
	if (tdip->pmp->ronly || (tdip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(tdip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	/*
	 * ip represents the file being hardlinked.  The file could be a
	 * normal file or a hardlink target if it has already been hardlinked.
	 * (with the new semantics, it will almost always be a hardlink
	 * target).
	 *
	 * Bump nlinks and potentially also create or move the hardlink
	 * target in the parent directory common to (ip) and (tdip).  The
	 * consolidation code can modify ip->cluster.  The returned cluster
	 * is locked.
	 */
	ip = VTOI(ap->a_vp);
	KASSERT(ip->pmp, ("ip->pmp is NULL %p %p", ip, ip->pmp));
	hammer2_trans_init(ip->pmp, 0);

	/*
	 * Target should be an indexed inode or there's no way we will ever
	 * be able to find it!
	 */
	KKASSERT((ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) == 0);

	error = 0;

	/*
	 * Can return NULL and error == EXDEV if the common parent
	 * crosses a directory with the xlink flag set.
	 */
	hammer2_inode_lock4(tdip, ip, NULL, NULL);

	hammer2_update_time(&cmtime);

	/*
	 * Create the directory entry and bump nlinks.
	 * Also update ip's ctime.
	 */
	if (error == 0) {
		error = hammer2_dirent_create(tdip, name, name_len,
					      ip->meta.inum, ip->meta.type);
		hammer2_inode_modify(ip);
		++ip->meta.nlinks;
		ip->meta.ctime = cmtime;
	}
	if (error == 0) {
		/*
		 * Update dip's [cm]time
		 */
		hammer2_inode_modify(tdip);
		tdip->meta.mtime = cmtime;
		tdip->meta.ctime = cmtime;

		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
	hammer2_inode_unlock(ip);
	hammer2_inode_unlock(tdip);

	hammer2_trans_done(ip->pmp, HAMMER2_TRANS_SIDEQ);
	hammer2_knote(ap->a_vp, NOTE_LINK);
	hammer2_knote(ap->a_dvp, NOTE_WRITE);

	return error;
}

int
hammer2_nlink(struct m_vnode *dvp, struct m_vnode *vp, char *name, int nlen)
{
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	struct vop_nlink_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vp = vp,
	};

	return hammer2_vop_nlink(&ap);
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
	const char *name;
	size_t name_len;
	hammer2_tid_t inum;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly || (dip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_trans_init(dip->pmp, 0);

	inum = hammer2_trans_newinum(dip->pmp);

	/*
	 * Create the actual inode as a hidden file in the iroot, then
	 * create the directory entry.  The creation of the actual inode
	 * sets its nlinks to 1 which is the value we desire.
	 *
	 * dip must be locked before nip to avoid deadlock.
	 */
	hammer2_inode_lock(dip, 0);
	nip = hammer2_inode_create_normal(dip, ap->a_vap, ap->a_cred,
					  inum, &error);

	if (error) {
		error = hammer2_error_to_errno(error);
	} else {
		error = hammer2_dirent_create(dip, name, name_len,
					      nip->meta.inum, nip->meta.type);
	}
	if (error) {
		if (nip) {
			hammer2_inode_unlink_finisher(nip, NULL);
			hammer2_inode_unlock(nip);
			nip = NULL;
		}
		*ap->a_vpp = NULL;
	} else {
		hammer2_inode_depend(dip, nip);	/* before igetv */
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}

	/*
	 * Update dip's mtime
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		hammer2_knote(ap->a_dvp, NOTE_WRITE);
	}
	return error;
}

int
hammer2_ncreate(struct m_vnode *dvp, struct m_vnode **vpp, char *name, int nlen,
		mode_t mode)
{
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	uid_t va_uid = VNOVAL; //getuid();
	uid_t va_gid = VNOVAL; //getgid();
	struct vattr va = {
		.va_type = VREG,
		.va_mode = mode & ~S_IFMT,
		.va_uid = va_uid,
		.va_gid = va_gid,
	};
	struct vop_ncreate_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vpp = vpp,
		.a_vap = &va,
	};

	return hammer2_vop_ncreate(&ap);
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
	const char *name;
	size_t name_len;
	hammer2_tid_t inum;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly || (dip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_trans_init(dip->pmp, 0);

	/*
	 * Create the device inode and then create the directory entry.
	 *
	 * dip must be locked before nip to avoid deadlock.
	 */
	inum = hammer2_trans_newinum(dip->pmp);

	hammer2_inode_lock(dip, 0);
	nip = hammer2_inode_create_normal(dip, ap->a_vap, ap->a_cred,
					  inum, &error);
	if (error == 0) {
		error = hammer2_dirent_create(dip, name, name_len,
					      nip->meta.inum, nip->meta.type);
	}
	if (error) {
		if (nip) {
			hammer2_inode_unlink_finisher(nip, NULL);
			hammer2_inode_unlock(nip);
			nip = NULL;
		}
		*ap->a_vpp = NULL;
	} else {
		hammer2_inode_depend(dip, nip);	/* before igetv */
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock(nip);
	}

	/*
	 * Update dip's mtime
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		hammer2_knote(ap->a_dvp, NOTE_WRITE);
	}
	return error;
}

int
hammer2_nmknod(struct m_vnode *dvp, struct m_vnode **vpp, char *name, int nlen,
		int type, mode_t mode)
{
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	uid_t va_uid = VNOVAL; //getuid();
	uid_t va_gid = VNOVAL; //getgid();
	struct vattr va = {
		.va_type = type,
		.va_mode = mode & ~S_IFMT,
		.va_uid = va_uid,
		.va_gid = va_gid,
	};
	struct vop_nmknod_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vpp = vpp,
		.a_vap = &va,
	};

	return hammer2_vop_nmknod(&ap);
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
	const char *name;
	size_t name_len;
	hammer2_tid_t inum;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly || (dip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_trans_init(dip->pmp, 0);

	ap->a_vap->va_type = VLNK;	/* enforce type */

	/*
	 * Create the softlink as an inode and then create the directory
	 * entry.
	 *
	 * dip must be locked before nip to avoid deadlock.
	 */
	inum = hammer2_trans_newinum(dip->pmp);

	hammer2_inode_lock(dip, 0);
	nip = hammer2_inode_create_normal(dip, ap->a_vap, ap->a_cred,
					  inum, &error);
	if (error == 0) {
		error = hammer2_dirent_create(dip, name, name_len,
					      nip->meta.inum, nip->meta.type);
	}
	if (error) {
		if (nip) {
			hammer2_inode_unlink_finisher(nip, NULL);
			hammer2_inode_unlock(nip);
			nip = NULL;
		}
		*ap->a_vpp = NULL;
		hammer2_inode_unlock(dip);
		hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);
		return error;
	}
	hammer2_inode_depend(dip, nip);	/* before igetv */
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

	/*
	 * Update dip's mtime
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);

	/*
	 * Finalize namecache
	 */
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		hammer2_knote(ap->a_dvp, NOTE_WRITE);
	}
	return error;
}

int
hammer2_nsymlink(struct m_vnode *dvp, struct m_vnode **vpp, char *name, int nlen,
			char *target, mode_t mode)
{
	struct namecache nc = {
		.nc_name = name,
		.nc_nlen = nlen,
	};
	struct nchandle nch = {
		.ncp = &nc,
	};
	uid_t va_uid = VNOVAL; //getuid();
	uid_t va_gid = VNOVAL; //getgid();
	struct vattr va = {
		.va_type = VDIR,
		.va_mode = mode & ~S_IFMT,
		.va_uid = va_uid,
		.va_gid = va_gid,
	};
	struct vop_nsymlink_args ap = {
		.a_nch = &nch,
		.a_dvp = dvp,
		.a_vpp = vpp,
		.a_vap = &va,
		.a_target = target,
	};

	return hammer2_vop_nsymlink(&ap);
}

/*
 * hammer2_vop_nremove { nch, dvp, cred }
 */
static
int
hammer2_vop_nremove(struct vop_nremove_args *ap)
{
#if 0
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	struct m_vnode *vprecycle;
	struct namecache *ncp;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);
#if 0
	/* allow removals, except user to also bulkfree */
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);
#endif

	ncp = ap->a_nch->ncp;

	if (hammer2_debug_inode && dip->meta.inum == hammer2_debug_inode) {
		kprintf("hammer2: attempt to delete inside debug inode: %s\n",
			ncp->nc_name);
		while (hammer2_debug_inode &&
		       dip->meta.inum == hammer2_debug_inode) {
			tsleep(&hammer2_debug_inode, 0, "h2debug", hz*5);
		}
	}

	hammer2_trans_init(dip->pmp, 0);
	hammer2_inode_lock(dip, 0);

	/*
	 * The unlink XOP unlinks the path from the directory and
	 * locates and returns the cluster associated with the real inode.
	 * We have to handle nlinks here on the frontend.
	 */
	xop = hammer2_xop_alloc(dip, HAMMER2_XOP_MODIFYING);
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);

	xop->isdir = 0;
	xop->dopermanent = 0;
	hammer2_xop_start(&xop->head, &hammer2_unlink_desc);

	/*
	 * Collect the real inode and adjust nlinks, destroy the real
	 * inode if nlinks transitions to 0 and it was the real inode
	 * (else it has already been removed).
	 */
	error = hammer2_xop_collect(&xop->head, 0);
	error = hammer2_error_to_errno(error);
	vprecycle = NULL;

	if (error == 0) {
		ip = hammer2_inode_get(dip->pmp, &xop->head, -1, -1);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (ip) {
			if (hammer2_debug_inode &&
			    ip->meta.inum == hammer2_debug_inode) {
				kprintf("hammer2: attempt to delete debug "
					"inode!\n");
				while (hammer2_debug_inode &&
				       ip->meta.inum == hammer2_debug_inode) {
					tsleep(&hammer2_debug_inode, 0,
					       "h2debug", hz*5);
				}
			}
			hammer2_inode_unlink_finisher(ip, &vprecycle);
			hammer2_inode_depend(dip, ip); /* after modified */
			hammer2_inode_unlock(ip);
		}
	} else {
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}

	/*
	 * Update dip's mtime
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);
	if (error == 0) {
		cache_unlink(ap->a_nch);
		hammer2_knote(ap->a_dvp, NOTE_WRITE);
	}
	if (vprecycle)
		hammer2_inode_vprecycle(vprecycle);

	return (error);
#endif
	return (EOPNOTSUPP);
}

/*
 * hammer2_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer2_vop_nrmdir(struct vop_nrmdir_args *ap)
{
#if 0
	hammer2_xop_unlink_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	struct namecache *ncp;
	struct m_vnode *vprecycle;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);
#if 0
	/* allow removals, except user to also bulkfree */
	if (hammer2_vfs_enospace(dip, 0, ap->a_cred) > 1)
		return (ENOSPC);
#endif

	hammer2_trans_init(dip->pmp, 0);
	hammer2_inode_lock(dip, 0);

	xop = hammer2_xop_alloc(dip, HAMMER2_XOP_MODIFYING);

	ncp = ap->a_nch->ncp;
	hammer2_xop_setname(&xop->head, ncp->nc_name, ncp->nc_nlen);
	xop->isdir = 1;
	xop->dopermanent = 0;
	hammer2_xop_start(&xop->head, &hammer2_unlink_desc);

	/*
	 * Collect the real inode and adjust nlinks, destroy the real
	 * inode if nlinks transitions to 0 and it was the real inode
	 * (else it has already been removed).
	 */
	error = hammer2_xop_collect(&xop->head, 0);
	error = hammer2_error_to_errno(error);
	vprecycle = NULL;

	if (error == 0) {
		ip = hammer2_inode_get(dip->pmp, &xop->head, -1, -1);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (ip) {
			hammer2_inode_unlink_finisher(ip, &vprecycle);
			hammer2_inode_depend(dip, ip);	/* after modified */
			hammer2_inode_unlock(ip);
		}
	} else {
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}

	/*
	 * Update dip's mtime
	 */
	if (error == 0) {
		uint64_t mtime;

		/*hammer2_inode_lock(dip, HAMMER2_RESOLVE_SHARED);*/
		hammer2_update_time(&mtime);
		hammer2_inode_modify(dip);
		dip->meta.mtime = mtime;
		/*hammer2_inode_unlock(dip);*/
	}
	hammer2_inode_unlock(dip);

	hammer2_trans_done(dip->pmp, HAMMER2_TRANS_SIDEQ);
	if (error == 0) {
		cache_unlink(ap->a_nch);
		hammer2_knote(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	}
	if (vprecycle)
		hammer2_inode_vprecycle(vprecycle);
	return (error);
#endif
	return (EOPNOTSUPP);
}

/*
 * hammer2_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer2_vop_nrename(struct vop_nrename_args *ap)
{
#if 0
	struct namecache *fncp;
	struct namecache *tncp;
	hammer2_inode_t *fdip;	/* source directory */
	hammer2_inode_t *tdip;	/* target directory */
	hammer2_inode_t *ip;	/* file being renamed */
	hammer2_inode_t *tip;	/* replaced target during rename or NULL */
	struct m_vnode *vprecycle;
	const char *fname;
	size_t fname_len;
	const char *tname;
	size_t tname_len;
	int error;
	int update_tdip;
	int update_fdip;
	hammer2_key_t tlhc;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);	/* source directory */
	tdip = VTOI(ap->a_tdvp);	/* target directory */

	if (fdip->pmp->ronly || (fdip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	if (hammer2_vfs_enospace(fdip, 0, ap->a_cred) > 1)
		return (ENOSPC);

	fncp = ap->a_fnch->ncp;		/* entry name in source */
	fname = fncp->nc_name;
	fname_len = fncp->nc_nlen;

	tncp = ap->a_tnch->ncp;		/* entry name in target */
	tname = tncp->nc_name;
	tname_len = tncp->nc_nlen;

	hammer2_trans_init(tdip->pmp, 0);

	update_tdip = 0;
	update_fdip = 0;

	ip = VTOI(fncp->nc_vp);
	hammer2_inode_ref(ip);		/* extra ref */

	/*
	 * Lookup the target name to determine if a directory entry
	 * is being overwritten.  We only hold related inode locks
	 * temporarily, the operating system is expected to protect
	 * against rename races.
	 */
	tip = tncp->nc_vp ? VTOI(tncp->nc_vp) : NULL;
	if (tip)
		hammer2_inode_ref(tip);	/* extra ref */

	/*
	 * Can return NULL and error == EXDEV if the common parent
	 * crosses a directory with the xlink flag set.
	 *
	 * For now try to avoid deadlocks with a simple pointer address
	 * test.  (tip) can be NULL.
	 */
	error = 0;
	{
		hammer2_inode_t *ip1 = fdip;
		hammer2_inode_t *ip2 = tdip;
		hammer2_inode_t *ip3 = ip;
		hammer2_inode_t *ip4 = tip;	/* may be NULL */

		if (fdip > tdip) {
			ip1 = tdip;
			ip2 = fdip;
		}
		if (tip && ip > tip) {
			ip3 = tip;
			ip4 = ip;
		}
		hammer2_inode_lock4(ip1, ip2, ip3, ip4);
	}

	/*
	 * Resolve the collision space for (tdip, tname, tname_len)
	 *
	 * tdip must be held exclusively locked to prevent races since
	 * multiple filenames can end up in the same collision space.
	 */
	{
		hammer2_xop_scanlhc_t *sxop;
		hammer2_tid_t lhcbase;

		tlhc = hammer2_dirhash(tname, tname_len);
		lhcbase = tlhc;
		sxop = hammer2_xop_alloc(tdip, HAMMER2_XOP_MODIFYING);
		sxop->lhc = tlhc;
		hammer2_xop_start(&sxop->head, &hammer2_scanlhc_desc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (tlhc != sxop->head.cluster.focus->bref.key)
				break;
			++tlhc;
		}
		error = hammer2_error_to_errno(error);
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
	 * Ready to go, issue the rename to the backend.  Note that meta-data
	 * updates to the related inodes occur separately from the rename
	 * operation.
	 *
	 * NOTE: While it is not necessary to update ip->meta.name*, doing
	 *	 so aids catastrophic recovery and debugging.
	 */
	if (error == 0) {
		hammer2_xop_nrename_t *xop4;

		xop4 = hammer2_xop_alloc(fdip, HAMMER2_XOP_MODIFYING);
		xop4->lhc = tlhc;
		xop4->ip_key = ip->meta.name_key;
		hammer2_xop_setip2(&xop4->head, ip);
		hammer2_xop_setip3(&xop4->head, tdip);
		if (tip && tip->meta.type == HAMMER2_OBJTYPE_DIRECTORY)
		    hammer2_xop_setip4(&xop4->head, tip);
		hammer2_xop_setname(&xop4->head, fname, fname_len);
		hammer2_xop_setname2(&xop4->head, tname, tname_len);
		hammer2_xop_start(&xop4->head, &hammer2_nrename_desc);

		error = hammer2_xop_collect(&xop4->head, 0);
		error = hammer2_error_to_errno(error);
		hammer2_xop_retire(&xop4->head, HAMMER2_XOPMASK_VOP);

		if (error == ENOENT)
			error = 0;

		/*
		 * Update inode meta-data.
		 *
		 * WARNING!  The in-memory inode (ip) structure does not
		 *	     maintain a copy of the inode's filename buffer.
		 */
		if (error == 0 &&
		    (ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE)) {
			hammer2_inode_modify(ip);
			ip->meta.name_len = tname_len;
			ip->meta.name_key = tlhc;
		}
		if (error == 0) {
			hammer2_inode_modify(ip);
			ip->meta.iparent = tdip->meta.inum;
		}
		update_fdip = 1;
		update_tdip = 1;
	}

done2:
	/*
	 * If no error, the backend has replaced the target directory entry.
	 * We must adjust nlinks on the original replace target if it exists.
	 */
	vprecycle = NULL;
	if (error == 0 && tip) {
		hammer2_inode_unlink_finisher(tip, &vprecycle);
	}

	/*
	 * Update directory mtimes to represent the something changed.
	 */
	if (update_fdip || update_tdip) {
		uint64_t mtime;

		hammer2_update_time(&mtime);
		if (update_fdip) {
			hammer2_inode_modify(fdip);
			fdip->meta.mtime = mtime;
		}
		if (update_tdip) {
			hammer2_inode_modify(tdip);
			tdip->meta.mtime = mtime;
		}
	}
	if (tip) {
		hammer2_inode_unlock(tip);
		hammer2_inode_drop(tip);
	}
	hammer2_inode_unlock(ip);
	hammer2_inode_unlock(tdip);
	hammer2_inode_unlock(fdip);
	hammer2_inode_drop(ip);
	hammer2_trans_done(tdip->pmp, HAMMER2_TRANS_SIDEQ);

	/*
	 * Issue the namecache update after unlocking all the internal
	 * hammer2 structures, otherwise we might deadlock.
	 *
	 * WARNING! The target namespace must be updated atomically,
	 *	    and we depend on cache_rename() to handle that for
	 *	    us.  Do not do a separate cache_unlink() because
	 *	    that leaves a small window of opportunity for other
	 *	    threads to allocate the target namespace before we
	 *	    manage to complete our rename.
	 *
	 * WARNING! cache_rename() (and cache_unlink()) will properly
	 *	    set VREF_FINALIZE on any attached vnode.  Do not
	 *	    call cache_setunresolved() manually before-hand as
	 *	    this will prevent the flag from being set later via
	 *	    cache_rename().  If VREF_FINALIZE is not properly set
	 *	    and the inode is no longer in the topology, related
	 *	    chains can remain dirty indefinitely.
	 */
	if (error == 0 && tip) {
		/*cache_unlink(ap->a_tnch); see above */
		/*cache_setunresolved(ap->a_tnch); see above */
	}
	if (error == 0) {
		cache_rename(ap->a_fnch, ap->a_tnch);
		hammer2_knote(ap->a_fdvp, NOTE_WRITE);
		hammer2_knote(ap->a_tdvp, NOTE_WRITE);
		hammer2_knote(fncp->nc_vp, NOTE_RENAME);
	}
	if (vprecycle)
		hammer2_inode_vprecycle(vprecycle);

	return (error);
#endif
	return (EOPNOTSUPP);
}

/*
 * hammer2_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer2_vop_ioctl(struct vop_ioctl_args *ap)
{
#if 0
	hammer2_inode_t *ip;
	int error;

	ip = VTOI(ap->a_vp);

	error = hammer2_ioctl(ip, ap->a_command, (void *)ap->a_data,
			      ap->a_fflag, ap->a_cred);
	return (error);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
#if 0
	struct mount *mp;
	hammer2_pfs_t *pmp;
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
#endif
	return (EOPNOTSUPP);
}

/*
 * KQFILTER
 */
/*
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
*/

static
int
hammer2_vop_kqfilter(struct vop_kqfilter_args *ap)
{
#if 0
	struct m_vnode *vp = ap->a_vp;
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
#endif
	return (EOPNOTSUPP);
}

#if 0
static void
filt_hammer2detach(struct knote *kn)
{
	struct m_vnode *vp = (void *)kn->kn_hook;

	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
}

static int
filt_hammer2read(struct knote *kn, long hint)
{
	struct m_vnode *vp = (void *)kn->kn_hook;
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
#endif

/*
 * FIFO VOPS
 */
static
int
hammer2_vop_markatime(struct vop_markatime_args *ap)
{
#if 0
	hammer2_inode_t *ip;
	struct m_vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

	if (ip->pmp->ronly || (ip->pmp->flags & HAMMER2_PMPF_EMERG))
		return (EROFS);
	return(0);
#endif
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_fifokqfilter(struct vop_kqfilter_args *ap)
{
#if 0
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = hammer2_vop_kqfilter(ap);
	return(error);
#endif
	return (EOPNOTSUPP);
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
	.vop_getattr_lite = hammer2_vop_getattr_lite,
	.vop_setattr	= hammer2_vop_setattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_readlink	= hammer2_vop_readlink,
	.vop_read	= hammer2_vop_read,
	.vop_write	= hammer2_vop_write,
	.vop_open	= hammer2_vop_open,
	.vop_inactive	= hammer2_vop_inactive,
	.vop_reclaim	= hammer2_vop_reclaim,
	.vop_nresolve	= hammer2_vop_nresolve,
	.vop_nlookupdotdot = hammer2_vop_nlookupdotdot,
	.vop_nmkdir	= hammer2_vop_nmkdir,
	.vop_nmknod	= hammer2_vop_nmknod,
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

