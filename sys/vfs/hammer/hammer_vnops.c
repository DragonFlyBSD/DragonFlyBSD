/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_vnops.c,v 1.70 2008/06/14 01:42:13 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/namecache.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <vm/vm_extern.h>
#include <vfs/fifofs/fifo.h>
#include "hammer.h"

/*
 * USERFS VNOPS
 */
/*static int hammer_vop_vnoperate(struct vop_generic_args *);*/
static int hammer_vop_fsync(struct vop_fsync_args *);
static int hammer_vop_read(struct vop_read_args *);
static int hammer_vop_write(struct vop_write_args *);
static int hammer_vop_access(struct vop_access_args *);
static int hammer_vop_advlock(struct vop_advlock_args *);
static int hammer_vop_close(struct vop_close_args *);
static int hammer_vop_ncreate(struct vop_ncreate_args *);
static int hammer_vop_getattr(struct vop_getattr_args *);
static int hammer_vop_nresolve(struct vop_nresolve_args *);
static int hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *);
static int hammer_vop_nlink(struct vop_nlink_args *);
static int hammer_vop_nmkdir(struct vop_nmkdir_args *);
static int hammer_vop_nmknod(struct vop_nmknod_args *);
static int hammer_vop_open(struct vop_open_args *);
static int hammer_vop_pathconf(struct vop_pathconf_args *);
static int hammer_vop_print(struct vop_print_args *);
static int hammer_vop_readdir(struct vop_readdir_args *);
static int hammer_vop_readlink(struct vop_readlink_args *);
static int hammer_vop_nremove(struct vop_nremove_args *);
static int hammer_vop_nrename(struct vop_nrename_args *);
static int hammer_vop_nrmdir(struct vop_nrmdir_args *);
static int hammer_vop_setattr(struct vop_setattr_args *);
static int hammer_vop_strategy(struct vop_strategy_args *);
static int hammer_vop_bmap(struct vop_bmap_args *ap);
static int hammer_vop_nsymlink(struct vop_nsymlink_args *);
static int hammer_vop_nwhiteout(struct vop_nwhiteout_args *);
static int hammer_vop_ioctl(struct vop_ioctl_args *);
static int hammer_vop_mountctl(struct vop_mountctl_args *);

static int hammer_vop_fifoclose (struct vop_close_args *);
static int hammer_vop_fiforead (struct vop_read_args *);
static int hammer_vop_fifowrite (struct vop_write_args *);

static int hammer_vop_specclose (struct vop_close_args *);
static int hammer_vop_specread (struct vop_read_args *);
static int hammer_vop_specwrite (struct vop_write_args *);

struct vop_ops hammer_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_read =		hammer_vop_read,
	.vop_write =		hammer_vop_write,
	.vop_access =		hammer_vop_access,
	.vop_advlock =		hammer_vop_advlock,
	.vop_close =		hammer_vop_close,
	.vop_ncreate =		hammer_vop_ncreate,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_nresolve =		hammer_vop_nresolve,
	.vop_nlookupdotdot =	hammer_vop_nlookupdotdot,
	.vop_nlink =		hammer_vop_nlink,
	.vop_nmkdir =		hammer_vop_nmkdir,
	.vop_nmknod =		hammer_vop_nmknod,
	.vop_open =		hammer_vop_open,
	.vop_pathconf =		hammer_vop_pathconf,
	.vop_print =		hammer_vop_print,
	.vop_readdir =		hammer_vop_readdir,
	.vop_readlink =		hammer_vop_readlink,
	.vop_nremove =		hammer_vop_nremove,
	.vop_nrename =		hammer_vop_nrename,
	.vop_nrmdir =		hammer_vop_nrmdir,
	.vop_setattr =		hammer_vop_setattr,
	.vop_bmap =		hammer_vop_bmap,
	.vop_strategy =		hammer_vop_strategy,
	.vop_nsymlink =		hammer_vop_nsymlink,
	.vop_nwhiteout =	hammer_vop_nwhiteout,
	.vop_ioctl =		hammer_vop_ioctl,
	.vop_mountctl =		hammer_vop_mountctl
};

struct vop_ops hammer_spec_vops = {
	.vop_default =		spec_vnoperate,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_specread,
	.vop_write =		hammer_vop_specwrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_specclose,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr
};

struct vop_ops hammer_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_fiforead,
	.vop_write =		hammer_vop_fifowrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_fifoclose,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr
};

#ifdef DEBUG_TRUNCATE
struct hammer_inode *HammerTruncIp;
#endif

static int hammer_dounlink(hammer_transaction_t trans, struct nchandle *nch,
			   struct vnode *dvp, struct ucred *cred, int flags);
static int hammer_vop_strategy_read(struct vop_strategy_args *ap);
static int hammer_vop_strategy_write(struct vop_strategy_args *ap);
static void hammer_cleanup_write_io(hammer_inode_t ip);
static void hammer_update_rsv_databufs(hammer_inode_t ip);

#if 0
static
int
hammer_vop_vnoperate(struct vop_generic_args *)
{
	return (VOCALL(&hammer_vnode_vops, ap));
}
#endif

/*
 * hammer_vop_fsync { vp, waitfor }
 */
static
int
hammer_vop_fsync(struct vop_fsync_args *ap)
{
	hammer_inode_t ip = VTOI(ap->a_vp);

	vfsync(ap->a_vp, ap->a_waitfor, 1, NULL, NULL);
	hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
	if (ap->a_waitfor == MNT_WAIT)
		hammer_wait_inode(ip);
	return (ip->error);
}

/*
 * hammer_vop_read { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_read(struct vop_read_args *ap)
{
	struct hammer_transaction trans;
	hammer_inode_t ip;
	off_t offset;
	struct buf *bp;
	struct uio *uio;
	int error;
	int n;
	int seqcount;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;
	seqcount = ap->a_ioflag >> 16;

	hammer_start_transaction(&trans, ip->hmp);

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	uio = ap->a_uio;
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ino_data.size) {
		offset = uio->uio_offset & HAMMER_BUFMASK;
		if (hammer_debug_cluster_enable) {
			error = cluster_read(ap->a_vp, ip->ino_data.size,
					     uio->uio_offset - offset,
					     HAMMER_BUFSIZE,
					     MAXBSIZE, seqcount, &bp);
		} else {
			error = bread(ap->a_vp, uio->uio_offset - offset,
				      HAMMER_BUFSIZE, &bp);
		}
		if (error) {
			brelse(bp);
			break;
		}

		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		n = HAMMER_BUFSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->ino_data.size - uio->uio_offset)
			n = (int)(ip->ino_data.size - uio->uio_offset);
		error = uiomove((char *)bp->b_data + offset, n, uio);

		/* data has a lower priority then meta-data */
		bp->b_flags |= B_AGE;
		bqrelse(bp);
		if (error)
			break;
	}
	if ((ip->flags & HAMMER_INODE_RO) == 0 &&
	    (ip->hmp->mp->mnt_flag & MNT_NOATIME) == 0) {
		ip->ino_leaf.atime = trans.time;
		hammer_modify_inode(ip, HAMMER_INODE_ITIMES);
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_write { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_write(struct vop_write_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct uio *uio;
	int rel_offset;
	off_t base_offset;
	struct buf *bp;
	int error;
	int n;
	int flags;
	int count;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;

	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, ip->hmp);
	uio = ap->a_uio;

	/*
	 * Check append mode
	 */
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = ip->ino_data.size;

	/*
	 * Check for illegal write offsets.  Valid range is 0...2^63-1.
	 *
	 * NOTE: the base_off assignment is required to work around what
	 * I consider to be a GCC-4 optimization bug.
	 */
	if (uio->uio_offset < 0) {
		hammer_done_transaction(&trans);
		return (EFBIG);
	}
	base_offset = uio->uio_offset + uio->uio_resid;	/* work around gcc-4 */
	if (uio->uio_resid > 0 && base_offset <= 0) {
		hammer_done_transaction(&trans);
		return (EFBIG);
	}

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	count = 0;
	while (uio->uio_resid > 0) {
		int fixsize = 0;

		if ((error = hammer_checkspace(trans.hmp)) != 0)
			break;

		/*
		 * Do not allow HAMMER to blow out the buffer cache.
		 *
		 * Do not allow HAMMER to blow out system memory by
		 * accumulating too many records.   Records are decoupled
		 * from the buffer cache.
		 *
		 * Always check at the beginning so separate writes are
		 * not able to bypass this code.
		 *
		 * WARNING: Cannot unlock vp when doing a NOCOPY write as
		 * part of a putpages operation.  Doing so could cause us
		 * to deadlock against the VM system when we try to re-lock.
		 */
		if ((count++ & 15) == 0) {
			if (uio->uio_segflg != UIO_NOCOPY) {
				vn_unlock(ap->a_vp);
				if ((ap->a_ioflag & IO_NOBWILL) == 0)
					bwillwrite();
			}
			if (ip->rsv_recs > hammer_limit_irecs)
				hammer_wait_inode_recs(ip);
			if (uio->uio_segflg != UIO_NOCOPY)
				vn_lock(ap->a_vp, LK_EXCLUSIVE|LK_RETRY);
		}

		rel_offset = (int)(uio->uio_offset & HAMMER_BUFMASK);
		base_offset = uio->uio_offset & ~HAMMER_BUFMASK64;
		n = HAMMER_BUFSIZE - rel_offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (uio->uio_offset + n > ip->ino_data.size) {
			vnode_pager_setsize(ap->a_vp, uio->uio_offset + n);
			fixsize = 1;
		}

		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(ap->a_vp, base_offset,
				    HAMMER_BUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ap->a_vp, base_offset,
					      HAMMER_BUFSIZE, &bp);
			}
		} else if (rel_offset == 0 && uio->uio_resid >= HAMMER_BUFSIZE) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a 
			 * mmap/write visibility issue.
			 */
			bp = getblk(ap->a_vp, base_offset,
				    HAMMER_BUFSIZE, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else if (base_offset >= ip->ino_data.size) {
			/*
			 * If the base offset of the buffer is beyond the
			 * file EOF, we don't have to issue a read.
			 */
			bp = getblk(ap->a_vp, base_offset,
				    HAMMER_BUFSIZE, GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 */
			error = bread(ap->a_vp, base_offset,
				      HAMMER_BUFSIZE, &bp);
			if (error == 0)
				bheavy(bp);
		}
		if (error == 0) {
			error = uiomove((char *)bp->b_data + rel_offset,
					n, uio);
		}

		/*
		 * If we screwed up we have to undo any VM size changes we
		 * made.
		 */
		if (error) {
			brelse(bp);
			if (fixsize) {
				vtruncbuf(ap->a_vp, ip->ino_data.size,
					  HAMMER_BUFSIZE);
			}
			break;
		}
		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		if (ip->ino_data.size < uio->uio_offset) {
			ip->ino_data.size = uio->uio_offset;
			flags = HAMMER_INODE_DDIRTY;
			vnode_pager_setsize(ap->a_vp, ip->ino_data.size);
		} else {
			flags = 0;
		}
		ip->ino_data.mtime = trans.time;
		flags |= HAMMER_INODE_ITIMES | HAMMER_INODE_BUFS;
		flags |= HAMMER_INODE_DDIRTY;	/* XXX mtime */
		hammer_modify_inode(ip, flags);

		/*
		 * Try to keep track of cached dirty data.
		 */
		if ((bp->b_flags & B_DIRTY) == 0) {
			++ip->rsv_databufs;
			++ip->hmp->rsv_databufs;
		}

		/*
		 * Final buffer disposition.
		 */
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if (ap->a_ioflag & IO_DIRECT) {
			bawrite(bp);
#if 1
		} else if ((ap->a_ioflag >> 16) == IO_SEQMAX &&
			   (uio->uio_offset & HAMMER_BUFMASK) == 0) {
			/*
			 * If seqcount indicates sequential operation and
			 * we just finished filling a buffer, push it out
			 * now to prevent the buffer cache from becoming
			 * too full, which would trigger non-optimal
			 * flushes.
			 */
			bawrite(bp);
#endif
		} else {
			bdwrite(bp);
		}
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_access { vp, mode, cred }
 */
static
int
hammer_vop_access(struct vop_access_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	uid_t uid;
	gid_t gid;
	int error;

	uid = hammer_to_unix_xid(&ip->ino_data.uid);
	gid = hammer_to_unix_xid(&ip->ino_data.gid);

	error = vop_helper_access(ap, uid, gid, ip->ino_data.mode,
				  ip->ino_data.uflags);
	return (error);
}

/*
 * hammer_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer_vop_advlock(struct vop_advlock_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &ip->advlock, ip->ino_data.size));
}

/*
 * hammer_vop_close { vp, fflag }
 */
static
int
hammer_vop_close(struct vop_close_args *ap)
{
	return (vop_stdclose(ap));
}

/*
 * hammer_vop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_ncreate(struct vop_ncreate_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced and shared-locked to prevent
	 * it from being moved to the flusher.
	 */

	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hkprintf("hammer_create_inode error %d\n", error);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);
	if (error)
		hkprintf("hammer_ip_add_directory error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_done_transaction(&trans);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	return (error);
}

/*
 * hammer_vop_getattr { vp, vap }
 *
 * Retrieve an inode's attribute information.  When accessing inodes
 * historically we fake the atime field to ensure consistent results.
 * The atime field is stored in the B-Tree element and allowed to be
 * updated without cycling the element.
 */
static
int
hammer_vop_getattr(struct vop_getattr_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	struct vattr *vap = ap->a_vap;

#if 0
	if (cache_check_fsmid_vp(ap->a_vp, &ip->fsmid) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0 &&
	    ip->obj_asof == XXX
	) {
		/* LAZYMOD XXX */
	}
	hammer_itimes(ap->a_vp);
#endif

	vap->va_fsid = ip->hmp->fsid_udev;
	vap->va_fileid = ip->ino_leaf.base.obj_id;
	vap->va_mode = ip->ino_data.mode;
	vap->va_nlink = ip->ino_data.nlinks;
	vap->va_uid = hammer_to_unix_xid(&ip->ino_data.uid);
	vap->va_gid = hammer_to_unix_xid(&ip->ino_data.gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->ino_data.size;
	if (ip->flags & HAMMER_INODE_RO)
		hammer_to_timespec(ip->ino_data.mtime, &vap->va_atime);
	else
		hammer_to_timespec(ip->ino_leaf.atime, &vap->va_atime);
	hammer_to_timespec(ip->ino_data.mtime, &vap->va_mtime);
	hammer_to_timespec(ip->ino_data.ctime, &vap->va_ctime);
	vap->va_flags = ip->ino_data.uflags;
	vap->va_gen = 1;	/* hammer inums are unique for all time */
	vap->va_blocksize = HAMMER_BUFSIZE;
	vap->va_bytes = (ip->ino_data.size + 63) & ~63;
	vap->va_type = hammer_get_vnode_type(ip->ino_data.obj_type);
	vap->va_filerev = 0; 	/* XXX */
	/* mtime uniquely identifies any adjustments made to the file */
	vap->va_fsmid = ip->ino_data.mtime;
	vap->va_uid_uuid = ip->ino_data.uid;
	vap->va_gid_uuid = ip->ino_data.gid;
	vap->va_fsid_uuid = ip->hmp->fsid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	switch (ip->ino_data.obj_type) {
	case HAMMER_OBJTYPE_CDEV:
	case HAMMER_OBJTYPE_BDEV:
		vap->va_rmajor = ip->ino_data.rmajor;
		vap->va_rminor = ip->ino_data.rminor;
		break;
	default:
		break;
	}

	return(0);
}

/*
 * hammer_vop_nresolve { nch, dvp, cred }
 *
 * Locate the requested directory entry.
 */
static
int
hammer_vop_nresolve(struct vop_nresolve_args *ap)
{
	struct hammer_transaction trans;
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	hammer_tid_t asof;
	struct hammer_cursor cursor;
	struct vnode *vp;
	int64_t namekey;
	int error;
	int i;
	int nlen;
	int flags;
	u_int64_t obj_id;

	/*
	 * Misc initialization, plus handle as-of name extensions.  Look for
	 * the '@@' extension.  Note that as-of files and directories cannot
	 * be modified.
	 */
	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	asof = dip->obj_asof;
	nlen = ncp->nc_nlen;
	flags = dip->flags;

	hammer_simple_transaction(&trans, dip->hmp);

	for (i = 0; i < nlen; ++i) {
		if (ncp->nc_name[i] == '@' && ncp->nc_name[i+1] == '@') {
			asof = hammer_str_to_tid(ncp->nc_name + i + 2);
			flags |= HAMMER_INODE_RO;
			break;
		}
	}
	nlen = i;

	/*
	 * If there is no path component the time extension is relative to
	 * dip.
	 */
	if (nlen == 0) {
		ip = hammer_get_inode(&trans, &dip->cache[1], dip->obj_id,
				      asof, flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
		goto done;
	}

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(ncp->nc_name, nlen);

	error = hammer_init_cursor(&trans, &cursor, &dip->cache[0], dip);
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	obj_id = 0;

	if (error == 0) {
		error = hammer_ip_first(&cursor);
		while (error == 0) {
			error = hammer_ip_resolve_data(&cursor);
			if (error)
				break;
			if (nlen == cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF &&
			    bcmp(ncp->nc_name, cursor.data->entry.name, nlen) == 0) {
				obj_id = cursor.data->entry.obj_id;
				break;
			}
			error = hammer_ip_next(&cursor);
		}
	}
	hammer_done_cursor(&cursor);
	if (error == 0) {
		ip = hammer_get_inode(&trans, &dip->cache[1],
				      obj_id, asof, flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
	} else if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
	}
done:
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nlookupdotdot { dvp, vpp, cred }
 *
 * Locate the parent directory of a directory vnode.
 *
 * dvp is referenced but not locked.  *vpp must be returned referenced and
 * locked.  A parent_obj_id of 0 does not necessarily indicate that we are
 * at the root, instead it could indicate that the directory we were in was
 * removed.
 *
 * NOTE: as-of sequences are not linked into the directory structure.  If
 * we are at the root with a different asof then the mount point, reload
 * the same directory with the mount point's asof.   I'm not sure what this
 * will do to NFS.  We encode ASOF stamps in NFS file handles so it might not
 * get confused, but it hasn't been tested.
 */
static
int
hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *ip;
	int64_t parent_obj_id;
	hammer_tid_t asof;
	int error;

	dip = VTOI(ap->a_dvp);
	asof = dip->obj_asof;
	parent_obj_id = dip->ino_data.parent_obj_id;

	if (parent_obj_id == 0) {
		if (dip->obj_id == HAMMER_OBJID_ROOT &&
		   asof != dip->hmp->asof) {
			parent_obj_id = dip->obj_id;
			asof = dip->hmp->asof;
			*ap->a_fakename = kmalloc(19, M_TEMP, M_WAITOK);
			ksnprintf(*ap->a_fakename, 19, "0x%016llx",
				   dip->obj_asof);
		} else {
			*ap->a_vpp = NULL;
			return ENOENT;
		}
	}

	hammer_simple_transaction(&trans, dip->hmp);

	ip = hammer_get_inode(&trans, &dip->cache[1], parent_obj_id,
			      asof, dip->flags, &error);
	if (ip) {
		error = hammer_get_vnode(ip, ap->a_vpp);
		hammer_rel_inode(ip, 0);
	} else {
		*ap->a_vpp = NULL;
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nlink { nch, dvp, vp, cred }
 */
static
int
hammer_vop_nlink(struct vop_nlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *ip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);
	ip = VTOI(ap->a_vp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Add the filesystem object to the directory.  Note that neither
	 * dip nor ip are referenced or locked, but their vnodes are
	 * referenced.  This function will bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, ip);

	/*
	 * Finish up.
	 */
	if (error == 0) {
		cache_setunresolved(nch);
		cache_setvp(nch, ap->a_vp);
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nmkdir { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hkprintf("hammer_mkdir error %d\n", error);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}
	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);
	if (error)
		hkprintf("hammer_mkdir (add) error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nmknod { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmknod(struct vop_nmknod_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_open { vp, mode, cred, fp }
 */
static
int
hammer_vop_open(struct vop_open_args *ap)
{
	hammer_inode_t ip;

	ip = VTOI(ap->a_vp);

	if ((ap->a_mode & FWRITE) && (ip->flags & HAMMER_INODE_RO))
		return (EROFS);
	return(vop_stdopen(ap));
}

/*
 * hammer_vop_pathconf { vp, name, retval }
 */
static
int
hammer_vop_pathconf(struct vop_pathconf_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_print { vp }
 */
static
int
hammer_vop_print(struct vop_print_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_readdir { vp, uio, cred, *eofflag, *ncookies, off_t **cookies }
 */
static
int
hammer_vop_readdir(struct vop_readdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	struct uio *uio;
	hammer_base_elm_t base;
	int error;
	int cookie_index;
	int ncookies;
	off_t *cookies;
	off_t saveoff;
	int r;

	ip = VTOI(ap->a_vp);
	uio = ap->a_uio;
	saveoff = uio->uio_offset;

	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		cookie_index = 0;
	} else {
		ncookies = -1;
		cookies = NULL;
		cookie_index = 0;
	}

	hammer_simple_transaction(&trans, ip->hmp);

	/*
	 * Handle artificial entries
	 */
	error = 0;
	if (saveoff == 0) {
		r = vop_write_dirent(&error, uio, ip->obj_id, DT_DIR, 1, ".");
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
		if (ip->ino_data.parent_obj_id) {
			r = vop_write_dirent(&error, uio,
					     ip->ino_data.parent_obj_id,
					     DT_DIR, 2, "..");
		} else {
			r = vop_write_dirent(&error, uio,
					     ip->obj_id, DT_DIR, 2, "..");
		}
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	hammer_init_cursor(&trans, &cursor, &ip->cache[0], ip);
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = saveoff;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	error = hammer_ip_first(&cursor);

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		base = &cursor.leaf->base;
		saveoff = base->key;
		KKASSERT(cursor.leaf->data_len > HAMMER_ENTRY_NAME_OFF);

		if (base->obj_id != ip->obj_id)
			panic("readdir: bad record at %p", cursor.node);

		r = vop_write_dirent(
			     &error, uio, cursor.data->entry.obj_id,
			     hammer_get_dtype(cursor.leaf->base.obj_type),
			     cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF ,
			     (void *)cursor.data->entry.name);
		if (r)
			break;
		++saveoff;
		if (cookies)
			cookies[cookie_index] = base->key;
		++cookie_index;
		if (cookie_index == ncookies)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);

done:
	hammer_done_transaction(&trans);

	if (ap->a_eofflag)
		*ap->a_eofflag = (error == ENOENT);
	uio->uio_offset = saveoff;
	if (error && cookie_index == 0) {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
	return(error);
}

/*
 * hammer_vop_readlink { vp, uio, cred }
 */
static
int
hammer_vop_readlink(struct vop_readlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	int error;

	ip = VTOI(ap->a_vp);

	/*
	 * Shortcut if the symlink data was stuffed into ino_data.
	 */
	if (ip->ino_data.size <= HAMMER_INODE_BASESYMLEN) {
		error = uiomove(ip->ino_data.ext.symlink,
				ip->ino_data.size, ap->a_uio);
		return(error);
	}

	/*
	 * Long version
	 */
	hammer_simple_transaction(&trans, ip->hmp);
	hammer_init_cursor(&trans, &cursor, &ip->cache[0], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC; /* XXX */
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_FIX;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = HAMMER_FIXKEY_SYMLINK;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	error = hammer_ip_lookup(&cursor);
	if (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error == 0) {
			KKASSERT(cursor.leaf->data_len >=
				 HAMMER_SYMLINK_NAME_OFF);
			error = uiomove(cursor.data->symlink.name,
					cursor.leaf->data_len -
						HAMMER_SYMLINK_NAME_OFF,
					ap->a_uio);
		}
	}
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);
	return(error);
}

/*
 * hammer_vop_nremove { nch, dvp, cred }
 */
static
int
hammer_vop_nremove(struct vop_nremove_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp, ap->a_cred, 0);
	hammer_done_transaction(&trans);

	return (error);
}

/*
 * hammer_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer_vop_nrename(struct vop_nrename_args *ap)
{
	struct hammer_transaction trans;
	struct namecache *fncp;
	struct namecache *tncp;
	struct hammer_inode *fdip;
	struct hammer_inode *tdip;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	int64_t namekey;
	int nlen, error;

	fdip = VTOI(ap->a_fdvp);
	tdip = VTOI(ap->a_tdvp);
	fncp = ap->a_fnch->ncp;
	tncp = ap->a_tnch->ncp;
	ip = VTOI(fncp->nc_vp);
	KKASSERT(ip != NULL);

	if (fdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (tdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(fdip->hmp)) != 0)
		return (error);

	hammer_start_transaction(&trans, fdip->hmp);

	/*
	 * Remove tncp from the target directory and then link ip as
	 * tncp. XXX pass trans to dounlink
	 *
	 * Force the inode sync-time to match the transaction so it is
	 * in-sync with the creation of the target directory entry.
	 */
	error = hammer_dounlink(&trans, ap->a_tnch, ap->a_tdvp, ap->a_cred, 0);
	if (error == 0 || error == ENOENT) {
		error = hammer_ip_add_directory(&trans, tdip, tncp, ip);
		if (error == 0) {
			ip->ino_data.parent_obj_id = tdip->obj_id;
			hammer_modify_inode(ip, HAMMER_INODE_DDIRTY);
		}
	}
	if (error)
		goto failed; /* XXX */

	/*
	 * Locate the record in the originating directory and remove it.
	 *
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(fncp->nc_name, fncp->nc_nlen);
retry:
	hammer_init_cursor(&trans, &cursor, &fdip->cache[0], fdip);
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
        cursor.key_beg.obj_id = fdip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = fdip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor);
	while (error == 0) {
		if (hammer_ip_resolve_data(&cursor) != 0)
			break;
		nlen = cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF;
		KKASSERT(nlen > 0);
		if (fncp->nc_nlen == nlen &&
		    bcmp(fncp->nc_name, cursor.data->entry.name, nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 *
	 * WARNING: hammer_ip_del_directory() may have to terminate the
	 * cursor to avoid a recursion.  It's ok to call hammer_done_cursor()
	 * twice.
	 */
	if (error == 0)
		error = hammer_ip_del_directory(&trans, &cursor, fdip, ip);

	/*
	 * XXX A deadlock here will break rename's atomicy for the purposes
	 * of crash recovery.
	 */
	if (error == EDEADLK) {
		hammer_done_cursor(&cursor);
		goto retry;
	}

	/*
	 * Cleanup and tell the kernel that the rename succeeded.
	 */
        hammer_done_cursor(&cursor);
	if (error == 0)
		cache_rename(ap->a_fnch, ap->a_tnch);

failed:
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp, ap->a_cred, 0);
	hammer_done_transaction(&trans);

	return (error);
}

/*
 * hammer_vop_setattr { vp, vap, cred }
 */
static
int
hammer_vop_setattr(struct vop_setattr_args *ap)
{
	struct hammer_transaction trans;
	struct vattr *vap;
	struct hammer_inode *ip;
	int modflags;
	int error;
	int truncating;
	off_t aligned_size;
	u_int32_t flags;

	vap = ap->a_vap;
	ip = ap->a_vp->v_data;
	modflags = 0;

	if (ap->a_vp->v_mount->mnt_flag & MNT_RDONLY)
		return(EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (hammer_nohistory(ip) == 0 &&
	    (error = hammer_checkspace(ip->hmp)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, ip->hmp);
	error = 0;

	if (vap->va_flags != VNOVAL) {
		flags = ip->ino_data.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer_to_unix_xid(&ip->ino_data.uid),
					 ap->a_cred);
		if (error == 0) {
			if (ip->ino_data.uflags != flags) {
				ip->ino_data.uflags = flags;
				modflags |= HAMMER_INODE_DDIRTY;
			}
			if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ip->ino_data.mode;
		uid_t cur_uid = hammer_to_unix_xid(&ip->ino_data.uid);
		gid_t cur_gid = hammer_to_unix_xid(&ip->ino_data.gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer_guid_to_uuid(&uuid_uid, cur_uid);
			hammer_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ip->ino_data.uid,
				 sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ip->ino_data.gid,
				 sizeof(uuid_gid)) ||
			    ip->ino_data.mode != cur_mode
			) {
				ip->ino_data.uid = uuid_uid;
				ip->ino_data.gid = uuid_gid;
				ip->ino_data.mode = cur_mode;
			}
			modflags |= HAMMER_INODE_DDIRTY;
		}
	}
	while (vap->va_size != VNOVAL && ip->ino_data.size != vap->va_size) {
		switch(ap->a_vp->v_type) {
		case VREG:
			if (vap->va_size == ip->ino_data.size)
				break;
			/*
			 * XXX break atomicy, we can deadlock the backend
			 * if we do not release the lock.  Probably not a
			 * big deal here.
			 */
			if (vap->va_size < ip->ino_data.size) {
				vtruncbuf(ap->a_vp, vap->va_size,
					  HAMMER_BUFSIZE);
				truncating = 1;
			} else {
				vnode_pager_setsize(ap->a_vp, vap->va_size);
				truncating = 0;
			}
			ip->ino_data.size = vap->va_size;
			modflags |= HAMMER_INODE_DDIRTY;
			aligned_size = (vap->va_size + HAMMER_BUFMASK) &
				       ~HAMMER_BUFMASK64;

			/*
			 * on-media truncation is cached in the inode until
			 * the inode is synchronized.
			 */
			if (truncating) {
				hammer_ip_frontend_trunc(ip, vap->va_size);
				hammer_update_rsv_databufs(ip);
#ifdef DEBUG_TRUNCATE
				if (HammerTruncIp == NULL)
					HammerTruncIp = ip;
#endif
				if ((ip->flags & HAMMER_INODE_TRUNCATED) == 0) {
					ip->flags |= HAMMER_INODE_TRUNCATED;
					ip->trunc_off = vap->va_size;
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate1 %016llx\n", ip->trunc_off);
#endif
				} else if (ip->trunc_off > vap->va_size) {
					ip->trunc_off = vap->va_size;
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate2 %016llx\n", ip->trunc_off);
#endif
				} else {
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate3 %016llx (ignored)\n", vap->va_size);
#endif
				}
			}

			/*
			 * If truncating we have to clean out a portion of
			 * the last block on-disk.  We do this in the
			 * front-end buffer cache.
			 */
			if (truncating && vap->va_size < aligned_size) {
				struct buf *bp;
				int offset;

				aligned_size -= HAMMER_BUFSIZE;

				offset = vap->va_size & HAMMER_BUFMASK;
				error = bread(ap->a_vp, aligned_size,
					      HAMMER_BUFSIZE, &bp);
				hammer_ip_frontend_trunc(ip, aligned_size);
				if (error == 0) {
					bzero(bp->b_data + offset,
					      HAMMER_BUFSIZE - offset);
					bdwrite(bp);
				} else {
					kprintf("ERROR %d\n", error);
					brelse(bp);
				}
			}
			break;
		case VDATABASE:
			if ((ip->flags & HAMMER_INODE_TRUNCATED) == 0) {
				ip->flags |= HAMMER_INODE_TRUNCATED;
				ip->trunc_off = vap->va_size;
			} else if (ip->trunc_off > vap->va_size) {
				ip->trunc_off = vap->va_size;
			}
			hammer_ip_frontend_trunc(ip, vap->va_size);
			ip->ino_data.size = vap->va_size;
			modflags |= HAMMER_INODE_DDIRTY;
			break;
		default:
			error = EINVAL;
			goto done;
		}
		break;
	}
	if (vap->va_atime.tv_sec != VNOVAL) {
		ip->ino_leaf.atime =
			hammer_timespec_to_transid(&vap->va_atime);
		modflags |= HAMMER_INODE_ITIMES;
	}
	if (vap->va_mtime.tv_sec != VNOVAL) {
		ip->ino_data.mtime =
			hammer_timespec_to_transid(&vap->va_mtime);
		modflags |= HAMMER_INODE_ITIMES;
		modflags |= HAMMER_INODE_DDIRTY;	/* XXX mtime */
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t   cur_mode = ip->ino_data.mode;
		uid_t cur_uid = hammer_to_unix_xid(&ip->ino_data.uid);
		gid_t cur_gid = hammer_to_unix_xid(&ip->ino_data.gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ip->ino_data.mode != cur_mode) {
			ip->ino_data.mode = cur_mode;
			modflags |= HAMMER_INODE_DDIRTY;
		}
	}
done:
	if (error == 0)
		hammer_modify_inode(ip, modflags);
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	hammer_record_t record;
	int error;
	int bytes;

	ap->a_vap->va_type = VLNK;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */

	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add a record representing the symlink.  symlink stores the link
	 * as pure data, not a string, and is no \0 terminated.
	 */
	if (error == 0) {
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER_INODE_BASESYMLEN) {
			bcopy(ap->a_target, nip->ino_data.ext.symlink, bytes);
		} else {
			record = hammer_alloc_mem_record(nip, bytes);
			record->type = HAMMER_MEM_RECORD_GENERAL;

			record->leaf.base.localization = HAMMER_LOCALIZE_MISC;
			record->leaf.base.key = HAMMER_FIXKEY_SYMLINK;
			record->leaf.base.rec_type = HAMMER_RECTYPE_FIX;
			record->leaf.data_len = bytes;
			KKASSERT(HAMMER_SYMLINK_NAME_OFF == 0);
			bcopy(ap->a_target, record->data->symlink.name, bytes);
			error = hammer_ip_add_record(&trans, record);
		}

		/*
		 * Set the file size to the length of the link.
		 */
		if (error == 0) {
			nip->ino_data.size = bytes;
			hammer_modify_inode(nip, HAMMER_INODE_DDIRTY);
		}
	}
	if (error == 0)
		error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nwhiteout { nch, dvp, cred, flags }
 */
static
int
hammer_vop_nwhiteout(struct vop_nwhiteout_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp,
				ap->a_cred, ap->a_flags);
	hammer_done_transaction(&trans);

	return (error);
}

/*
 * hammer_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer_vop_ioctl(struct vop_ioctl_args *ap)
{
	struct hammer_inode *ip = ap->a_vp->v_data;

	return(hammer_ioctl(ip, ap->a_command, ap->a_data,
			    ap->a_fflag, ap->a_cred));
}

static
int
hammer_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	int error;

	mp = ap->a_head.a_ops->head.vv_mount;

	switch(ap->a_op) {
	case MOUNTCTL_SET_EXPORT:
		if (ap->a_ctllen != sizeof(struct export_args))
			error = EINVAL;
		error = hammer_vfs_export(mp, ap->a_op,
				      (const struct export_args *)ap->a_ctl);
		break;
	default:
		error = journal_mountctl(ap);
		break;
	}
	return(error);
}

/*
 * hammer_vop_strategy { vp, bio }
 *
 * Strategy call, used for regular file read & write only.  Note that the
 * bp may represent a cluster.
 *
 * To simplify operation and allow better optimizations in the future,
 * this code does not make any assumptions with regards to buffer alignment
 * or size.
 */
static
int
hammer_vop_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp;
	int error;

	bp = ap->a_bio->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer_vop_strategy_read(ap);
		break;
	case BUF_CMD_WRITE:
		error = hammer_vop_strategy_write(ap);
		break;
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
		break;
	}
	return (error);
}

/*
 * Read from a regular file.  Iterate the related records and fill in the
 * BIO/BUF.  Gaps are zero-filled.
 *
 * The support code in hammer_object.c should be used to deal with mixed
 * in-memory and on-disk records.
 *
 * XXX atime update
 */
static
int
hammer_vop_strategy_read(struct vop_strategy_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	hammer_base_elm_t base;
	struct bio *bio;
	struct bio *nbio;
	struct buf *bp;
	int64_t rec_offset;
	int64_t ran_end;
	int64_t tmp64;
	int error;
	int boff;
	int roff;
	int n;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = ap->a_vp->v_data;

	/*
	 * The zone-2 disk offset may have been set by the cluster code via
	 * a BMAP operation.  Take care not to confuse it with the bio_offset
	 * set by hammer_io_direct_write(), which is a device-relative offset.
	 *
	 * Checking the high bits should suffice.
	 */
	nbio = push_bio(bio);
	if ((nbio->bio_offset & HAMMER_OFF_ZONE_MASK) ==
	    HAMMER_ZONE_RAW_BUFFER) {
		error = hammer_io_direct_read(ip->hmp, nbio->bio_offset, bio);
		return (error);
	}

	/*
	 * Hard way
	 */
	hammer_simple_transaction(&trans, ip->hmp);
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Note that the key's
	 * stored in the actual records represent BASE+LEN, not BASE.  The
	 * first record containing bio_offset will have a key > bio_offset.
	 */
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = bio->bio_offset + 1;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	cursor.key_end = cursor.key_beg;
	KKASSERT(ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE);
#if 0
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
	} else
#endif
	{
		ran_end = bio->bio_offset + bp->b_bufsize;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
		tmp64 = ran_end + MAXPHYS + 1;	/* work-around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor.key_end.key = ran_end + MAXPHYS + 1;
	}
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor);
	boff = 0;

	while (error == 0) {
		/*
		 * Get the base file offset of the record.  The key for
		 * data records is (base + bytes) rather then (base).
		 */
		base = &cursor.leaf->base;
		rec_offset = base->key - cursor.leaf->data_len;

		/*
		 * Calculate the gap, if any, and zero-fill it.
		 *
		 * n is the offset of the start of the record verses our
		 * current seek offset in the bio.
		 */
		n = (int)(rec_offset - (bio->bio_offset + boff));
		if (n > 0) {
			if (n > bp->b_bufsize - boff)
				n = bp->b_bufsize - boff;
			bzero((char *)bp->b_data + boff, n);
			boff += n;
			n = 0;
		}

		/*
		 * Calculate the data offset in the record and the number
		 * of bytes we can copy.
		 *
		 * There are two degenerate cases.  First, boff may already
		 * be at bp->b_bufsize.  Secondly, the data offset within
		 * the record may exceed the record's size.
		 */
		roff = -n;
		rec_offset += roff;
		n = cursor.leaf->data_len - roff;
		if (n <= 0) {
			kprintf("strategy_read: bad n=%d roff=%d\n", n, roff);
			n = 0;
		} else if (n > bp->b_bufsize - boff) {
			n = bp->b_bufsize - boff;
		}

		/*
		 * Deal with cached truncations.  This cool bit of code
		 * allows truncate()/ftruncate() to avoid having to sync
		 * the file.
		 *
		 * If the frontend is truncated then all backend records are
		 * subject to the frontend's truncation.
		 *
		 * If the backend is truncated then backend records on-disk
		 * (but not in-memory) are subject to the backend's
		 * truncation.  In-memory records owned by the backend
		 * represent data written after the truncation point on the
		 * backend and must not be truncated.
		 *
		 * Truncate operations deal with frontend buffer cache
		 * buffers and frontend-owned in-memory records synchronously.
		 */
		if (ip->flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor) ||
			    cursor.iprec->flush_state == HAMMER_FST_FLUSH) {
				if (ip->trunc_off <= rec_offset)
					n = 0;
				else if (ip->trunc_off < rec_offset + n)
					n = (int)(ip->trunc_off - rec_offset);
			}
		}
		if (ip->sync_flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor)) {
				if (ip->sync_trunc_off <= rec_offset)
					n = 0;
				else if (ip->sync_trunc_off < rec_offset + n)
					n = (int)(ip->sync_trunc_off - rec_offset);
			}
		}

		/*
		 * Try to issue a direct read into our bio if possible,
		 * otherwise resolve the element data into a hammer_buffer
		 * and copy.
		 */
		if (n && boff == 0 &&
		    ((cursor.leaf->data_offset + roff) & HAMMER_BUFMASK) == 0) {
			error = hammer_io_direct_read(
					trans.hmp,
					cursor.leaf->data_offset + roff,
					bio);
			goto done;
		} else if (n) {
			error = hammer_ip_resolve_data(&cursor);
			if (error == 0) {
				bcopy((char *)cursor.data + roff,
				      (char *)bp->b_data + boff, n);
			}
		}
		if (error)
			break;

		/*
		 * Iterate until we have filled the request.
		 */
		boff += n;
		if (boff == bp->b_bufsize)
			break;
		error = hammer_ip_next(&cursor);
	}

	/*
	 * There may have been a gap after the last record
	 */
	if (error == ENOENT)
		error = 0;
	if (error == 0 && boff != bp->b_bufsize) {
		KKASSERT(boff < bp->b_bufsize);
		bzero((char *)bp->b_data + boff, bp->b_bufsize - boff);
		/* boff = bp->b_bufsize; */
	}
	bp->b_resid = 0;
	bp->b_error = error;
	if (error)
		bp->b_flags |= B_ERROR;
	biodone(ap->a_bio);

done:
	if (cursor.node)
		hammer_cache_node(cursor.node, &ip->cache[1]);
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);
	return(error);
}

/*
 * BMAP operation - used to support cluster_read() only.
 *
 * (struct vnode *vp, off_t loffset, off_t *doffsetp, int *runp, int *runb)
 *
 * This routine may return EOPNOTSUPP if the opration is not supported for
 * the specified offset.  The contents of the pointer arguments do not
 * need to be initialized in that case. 
 *
 * If a disk address is available and properly aligned return 0 with 
 * *doffsetp set to the zone-2 address, and *runp / *runb set appropriately
 * to the run-length relative to that offset.  Callers may assume that
 * *doffsetp is valid if 0 is returned, even if *runp is not sufficiently
 * large, so return EOPNOTSUPP if it is not sufficiently large.
 */
static
int
hammer_vop_bmap(struct vop_bmap_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	hammer_base_elm_t base;
	int64_t rec_offset;
	int64_t ran_end;
	int64_t tmp64;
	int64_t base_offset;
	int64_t base_disk_offset;
	int64_t last_offset;
	hammer_off_t last_disk_offset;
	hammer_off_t disk_offset;
	int	rec_len;
	int	error;

	ip = ap->a_vp->v_data;

	/*
	 * We can only BMAP regular files.  We can't BMAP database files,
	 * directories, etc.
	 */
	if (ip->ino_data.obj_type != HAMMER_OBJTYPE_REGFILE)
		return(EOPNOTSUPP);

	/*
	 * bmap is typically called with runp/runb both NULL when used
	 * for writing.  We do not support BMAP for writing atm.
	 */
	if (ap->a_runp == NULL && ap->a_runb == NULL)
		return(EOPNOTSUPP);

	/*
	 * Scan the B-Tree to acquire blockmap addresses, then translate
	 * to raw addresses.
	 */
	hammer_simple_transaction(&trans, ip->hmp);
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Note that the key's
	 * stored in the actual records represent BASE+LEN, not BASE.  The
	 * first record containing bio_offset will have a key > bio_offset.
	 */
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	if (ap->a_runb)
		cursor.key_beg.key = ap->a_loffset - MAXPHYS + 1;
	else
		cursor.key_beg.key = ap->a_loffset + 1;
	if (cursor.key_beg.key < 0)
		cursor.key_beg.key = 0;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	cursor.key_end = cursor.key_beg;
	KKASSERT(ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE);

	ran_end = ap->a_loffset + MAXPHYS;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
	cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
	tmp64 = ran_end + MAXPHYS + 1;	/* work-around GCC-4 bug */
	if (tmp64 < ran_end)
		cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
	else
		cursor.key_end.key = ran_end + MAXPHYS + 1;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor);
	base_offset = last_offset = 0;
	base_disk_offset = last_disk_offset = 0;

	while (error == 0) {
		/*
		 * Get the base file offset of the record.  The key for
		 * data records is (base + bytes) rather then (base).
		 */
		base = &cursor.leaf->base;
		rec_offset = base->key - cursor.leaf->data_len;
		rec_len    = cursor.leaf->data_len;

		/*
		 * Incorporate any cached truncation
		 */
		if (ip->flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor) ||
			    cursor.iprec->flush_state == HAMMER_FST_FLUSH) {
				if (ip->trunc_off <= rec_offset)
					rec_len = 0;
				else if (ip->trunc_off < rec_offset + rec_len)
					rec_len = (int)(ip->trunc_off - rec_offset);
			}
		}
		if (ip->sync_flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor)) {
				if (ip->sync_trunc_off <= rec_offset)
					rec_len = 0;
				else if (ip->sync_trunc_off < rec_offset + rec_len)
					rec_len = (int)(ip->sync_trunc_off - rec_offset);
			}
		}

		/*
		 * Accumulate information.  If we have hit a discontiguous
		 * block reset base_offset unless we are already beyond the
		 * requested offset.  If we are, that's it, we stop.
		 */
		disk_offset = hammer_blockmap_lookup(trans.hmp,
						     cursor.leaf->data_offset,
						     &error);
		if (error)
			break;
		if (rec_offset != last_offset ||
		    disk_offset != last_disk_offset) {
			if (rec_offset > ap->a_loffset)
				break;
			base_offset = rec_offset;
			base_disk_offset = disk_offset;
		}
		last_offset = rec_offset + rec_len;
		last_disk_offset = disk_offset + rec_len;

		error = hammer_ip_next(&cursor);
	}

#if 0
	kprintf("BMAP %016llx:  %016llx - %016llx\n",
		ap->a_loffset, base_offset, last_offset);
	kprintf("BMAP %16s:  %016llx - %016llx\n",
		"", base_disk_offset, last_disk_offset);
#endif

	if (cursor.node)
		hammer_cache_node(cursor.node, &ip->cache[1]);
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);

	if (base_offset == 0 || base_offset > ap->a_loffset ||
	    last_offset < ap->a_loffset) {
		error = EOPNOTSUPP;
	} else {
		disk_offset = base_disk_offset + (ap->a_loffset - base_offset);

		/*
		 * If doffsetp is not aligned or the forward run size does
		 * not cover a whole buffer, disallow the direct I/O.
		 */
		if ((disk_offset & HAMMER_BUFMASK) ||
		    (last_offset - ap->a_loffset) < HAMMER_BUFSIZE) {
			error = EOPNOTSUPP;
		} else {
			*ap->a_doffsetp = disk_offset;
			if (ap->a_runb)
				*ap->a_runb = ap->a_loffset - base_offset;
			if (ap->a_runp)
				*ap->a_runp = last_offset - ap->a_loffset;
			error = 0;
		}
	}
	return(error);
}

/*
 * Write to a regular file.   Because this is a strategy call the OS is
 * trying to actually sync data to the media.   HAMMER can only flush
 * the entire inode (so the TID remains properly synchronized).
 *
 * Basically all we do here is place the bio on the inode's flush queue
 * and activate the flusher.
 */
static
int
hammer_vop_strategy_write(struct vop_strategy_args *ap)
{
	hammer_record_t record;
	hammer_mount_t hmp;
	hammer_inode_t ip;
	struct bio *bio;
	struct buf *bp;
	int bytes;
	int error;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = ap->a_vp->v_data;
	hmp = ip->hmp;

	if (ip->flags & HAMMER_INODE_RO) {
		bp->b_error = EROFS;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
		hammer_cleanup_write_io(ip);
		return(EROFS);
	}

	/*
	 * Interlock with inode destruction (no in-kernel or directory
	 * topology visibility).  If we queue new IO while trying to
	 * destroy the inode we can deadlock the vtrunc call in
	 * hammer_inode_unloadable_check().
	 */
	if (ip->flags & (HAMMER_INODE_DELETING|HAMMER_INODE_DELETED)) {
		bp->b_resid = 0;
		biodone(ap->a_bio);
		hammer_cleanup_write_io(ip);
		return(0);
	}

	/*
	 * Reserve space and issue a direct-write from the front-end. 
	 * NOTE: The direct_io code will hammer_bread/bcopy smaller
	 * allocations.
	 *
	 * An in-memory record will be installed to reference the storage
	 * until the flusher can get to it.
	 *
	 * Since we own the high level bio the front-end will not try to
	 * do a direct-read until the write completes.
	 *
	 * NOTE: The only time we do not reserve a full-sized buffers
	 * worth of data is if the file is small.  We do not try to
	 * allocate a fragment (from the small-data zone) at the end of
	 * an otherwise large file as this can lead to wildly separated
	 * data.
	 */
	KKASSERT((bio->bio_offset & HAMMER_BUFMASK) == 0);
	KKASSERT(bio->bio_offset < ip->ino_data.size);
	if (bio->bio_offset || ip->ino_data.size > HAMMER_BUFSIZE / 2)
		bytes = (bp->b_bufsize + HAMMER_BUFMASK) & ~HAMMER_BUFMASK;
	else
		bytes = ((int)ip->ino_data.size + 15) & ~15;

	record = hammer_ip_add_bulk(ip, bio->bio_offset, bp->b_data,
				    bytes, &error);
	if (record) {
		hammer_io_direct_write(hmp, &record->leaf, bio);
		hammer_rel_mem_record(record);
		if (hmp->rsv_recs > hammer_limit_recs &&
		    ip->rsv_recs > hammer_limit_irecs / 10) {
			hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
		} else if (ip->rsv_recs > hammer_limit_irecs / 2) {
			hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
		}
	} else {
		bp->b_bio2.bio_offset = NOOFFSET;
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
	}
	hammer_cleanup_write_io(ip);
	return(error);
}

/*
 * Clean-up after disposing of a dirty frontend buffer's data.
 * This is somewhat heuristical so try to be robust.
 */
static void
hammer_cleanup_write_io(hammer_inode_t ip)
{
	if (ip->rsv_databufs) {
		--ip->rsv_databufs;
		--ip->hmp->rsv_databufs;
	}
}

/*
 * We can lose track of dirty buffer cache buffers if we truncate, this
 * routine will resynchronize the count.
 */
static
void
hammer_update_rsv_databufs(hammer_inode_t ip)
{
	struct buf *bp;
	int delta;
	int n;

	if (ip->vp) {
		n = 0;
		RB_FOREACH(bp, buf_rb_tree, &ip->vp->v_rbdirty_tree) {
			++n;
		}
	} else {
		n = 0;
	}
	delta = n - ip->rsv_databufs;
	ip->rsv_databufs += delta;
	ip->hmp->rsv_databufs += delta;
}

/*
 * dounlink - disconnect a directory entry
 *
 * XXX whiteout support not really in yet
 */
static int
hammer_dounlink(hammer_transaction_t trans, struct nchandle *nch,
		struct vnode *dvp, struct ucred *cred, int flags)
{
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	struct hammer_cursor cursor;
	int64_t namekey;
	int nlen, error;

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	dip = VTOI(dvp);
	ncp = nch->ncp;

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	namekey = hammer_directory_namekey(ncp->nc_name, ncp->nc_nlen);
retry:
	hammer_init_cursor(trans, &cursor, &dip->cache[0], dip);
	cursor.key_beg.localization = HAMMER_LOCALIZE_MISC;
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = dip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.  info->last_error contains the
	 * error code on search termination and could be 0, ENOENT, or
	 * something else.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor);

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		nlen = cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF;
		KKASSERT(nlen > 0);
		if (ncp->nc_nlen == nlen &&
		    bcmp(ncp->nc_name, cursor.data->entry.name, nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 * To avoid a deadlock with the flusher we must release the inode
	 * lock on the directory when acquiring the inode for the entry.
	 *
	 * If the target is a directory, it must be empty.
	 */
	if (error == 0) {
		hammer_unlock(&cursor.ip->lock);
		ip = hammer_get_inode(trans, &dip->cache[1],
				      cursor.data->entry.obj_id,
				      dip->hmp->asof, 0, &error);
		hammer_lock_sh(&cursor.ip->lock);
		if (error == ENOENT) {
			kprintf("obj_id %016llx\n", cursor.data->entry.obj_id);
			Debugger("ENOENT unlinking object that should exist");
		}

		/*
		 * If we are trying to remove a directory the directory must
		 * be empty.
		 *
		 * WARNING: hammer_ip_check_directory_empty() may have to
		 * terminate the cursor to avoid a deadlock.  It is ok to
		 * call hammer_done_cursor() twice.
		 */
		if (error == 0 && ip->ino_data.obj_type ==
				  HAMMER_OBJTYPE_DIRECTORY) {
			error = hammer_ip_check_directory_empty(trans, ip);
		}

		/*
		 * Delete the directory entry.
		 *
		 * WARNING: hammer_ip_del_directory() may have to terminate
		 * the cursor to avoid a deadlock.  It is ok to call
		 * hammer_done_cursor() twice.
		 */
		if (error == 0) {
			error = hammer_ip_del_directory(trans, &cursor,
							dip, ip);
		}
		hammer_done_cursor(&cursor);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, NULL);
			/* XXX locking */
			if (ip->vp)
				cache_inval_vp(ip->vp, CINV_DESTROY);
		}
		if (ip)
			hammer_rel_inode(ip, 0);
	} else {
		hammer_done_cursor(&cursor);
	}
	if (error == EDEADLK)
		goto retry;

	return (error);
}

/************************************************************************
 *			    FIFO AND SPECFS OPS				*
 ************************************************************************
 *
 */

static int
hammer_vop_fifoclose (struct vop_close_args *ap)
{
	/* XXX update itimes */
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

static int
hammer_vop_fiforead (struct vop_read_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static int
hammer_vop_fifowrite (struct vop_write_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static int
hammer_vop_specclose (struct vop_close_args *ap)
{
	/* XXX update itimes */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

static int
hammer_vop_specread (struct vop_read_args *ap)
{
	/* XXX update access time */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

static int
hammer_vop_specwrite (struct vop_write_args *ap)
{
	/* XXX update last change time */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

