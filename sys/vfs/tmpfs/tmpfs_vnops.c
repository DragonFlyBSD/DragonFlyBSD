/*-
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: tmpfs_vnops.c,v 1.39 2007/07/23 15:41:01 jmmv Exp $
 */

/*
 * tmpfs vnode interface.
 */

#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/vfsops.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>

#include <sys/buf2.h>
#include <vm/vm_page2.h>

#include <vfs/fifofs/fifo.h>
#include <vfs/tmpfs/tmpfs_vnops.h>
#include "tmpfs.h"

static void tmpfs_strategy_done(struct bio *bio);
static void tmpfs_move_pages(vm_object_t src, vm_object_t dst, int movflags);

/*
 * bufcache_mode:
 *	0	Normal page queue operation on flush.  Run through the buffer
 *		cache if free memory is under the minimum.
 *
 *	1	Try to keep in memory, but run through the buffer cache if
 *		the system is under memory pressure (though this might just
 *		require inactive cleaning).
 *
 *	2	Be a bit more aggressive when running writes through the
 *		buffer cache when the system is under memory pressure.
 *
 *	3	Always run tmpfs writes through the buffer cache, thus forcing
 *		them out to swap.
 */
__read_mostly static int tmpfs_cluster_rd_enable = 1;
__read_mostly static int tmpfs_cluster_wr_enable = 1;
__read_mostly int tmpfs_bufcache_mode = 0;
SYSCTL_NODE(_vfs, OID_AUTO, tmpfs, CTLFLAG_RW, 0, "TMPFS filesystem");
SYSCTL_INT(_vfs_tmpfs, OID_AUTO, cluster_rd_enable, CTLFLAG_RW,
		&tmpfs_cluster_rd_enable, 0, "");
SYSCTL_INT(_vfs_tmpfs, OID_AUTO, cluster_wr_enable, CTLFLAG_RW,
		&tmpfs_cluster_wr_enable, 0, "");
SYSCTL_INT(_vfs_tmpfs, OID_AUTO, bufcache_mode, CTLFLAG_RW,
		&tmpfs_bufcache_mode, 0, "");

#define TMPFS_MOVF_FROMBACKING	0x0001
#define TMPFS_MOVF_DEACTIVATE	0x0002


static __inline
void
tmpfs_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}


/* --------------------------------------------------------------------- */

static int
tmpfs_nresolve(struct vop_nresolve_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = NULL;
	struct namecache *ncp = ap->a_nch->ncp;
	struct tmpfs_node *tnode;
	struct tmpfs_dirent *de;
	struct tmpfs_node *dnode;
	int error;

	dnode = VP_TO_TMPFS_DIR(dvp);

	TMPFS_NODE_LOCK_SH(dnode);
loop:
	de = tmpfs_dir_lookup(dnode, NULL, ncp);
	if (de == NULL) {
		error = ENOENT;
	} else {
		/*
		 * Allocate a vnode for the node we found.  Use
		 * tmpfs_alloc_vp()'s deadlock handling mode.
		 */
		tnode = de->td_node;
		error = tmpfs_alloc_vp(dvp->v_mount, dnode, tnode,
				       LK_EXCLUSIVE | LK_RETRY, &vp);
		if (error == EAGAIN)
			goto loop;
		if (error)
			goto out;
		KKASSERT(vp);
	}

out:
	TMPFS_NODE_UNLOCK(dnode);

	if ((dnode->tn_status & TMPFS_NODE_ACCESSED) == 0) {
		TMPFS_NODE_LOCK(dnode);
		dnode->tn_status |= TMPFS_NODE_ACCESSED;
		TMPFS_NODE_UNLOCK(dnode);
	}

	/*
	 * Store the result of this lookup in the cache.  Avoid this if the
	 * request was for creation, as it does not improve timings on
	 * emprical tests.
	 */
	if (vp) {
		vn_unlock(vp);
		cache_setvp(ap->a_nch, vp);
		vrele(vp);
	} else if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
	}
	return (error);
}

static int
tmpfs_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct tmpfs_node *dnode = VP_TO_TMPFS_NODE(dvp);
	struct ucred *cred = ap->a_cred;
	int error;

	*vpp = NULL;

	/* Check accessibility of requested node as a first step. */
	error = VOP_ACCESS(dvp, VEXEC, cred);
	if (error != 0)
		return error;

	if (dnode->tn_dir.tn_parent != NULL) {
		/* Allocate a new vnode on the matching entry. */
		error = tmpfs_alloc_vp(dvp->v_mount,
				       NULL, dnode->tn_dir.tn_parent,
				       LK_EXCLUSIVE | LK_RETRY, vpp);

		if (*vpp)
			vn_unlock(*vpp);
	}
	return (*vpp == NULL) ? ENOENT : 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_ncreate(struct vop_ncreate_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error;

	KKASSERT(vap->va_type == VREG || vap->va_type == VSOCK);

	error = tmpfs_alloc_file(dvp, vpp, vap, ncp, cred, NULL);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
		tmpfs_knote(dvp, NOTE_WRITE);
	}
	return (error);
}
/* --------------------------------------------------------------------- */

static int
tmpfs_nmknod(struct vop_nmknod_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error;

	if (vap->va_type != VBLK && vap->va_type != VCHR &&
	    vap->va_type != VFIFO) {
		return (EINVAL);
	}

	error = tmpfs_alloc_file(dvp, vpp, vap, ncp, cred, NULL);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
		tmpfs_knote(dvp, NOTE_WRITE);
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int mode = ap->a_mode;
	struct tmpfs_node *node;
	int error;

	node = VP_TO_TMPFS_NODE(vp);

#if 0
	/* The file is still active but all its names have been removed
	 * (e.g. by a "rmdir $(pwd)").  It cannot be opened any more as
	 * it is about to die. */
	if (node->tn_links < 1)
		return (ENOENT);
#endif

	/* If the file is marked append-only, deny write requests. */
	if ((node->tn_flags & APPEND) &&
	    (mode & (FWRITE | O_APPEND)) == FWRITE) {
		error = EPERM;
	} else {
		if (node->tn_reg.tn_pages_in_aobj) {
			TMPFS_NODE_LOCK(node);
			if (node->tn_reg.tn_pages_in_aobj) {
				tmpfs_move_pages(node->tn_reg.tn_aobj,
						 vp->v_object,
						 TMPFS_MOVF_FROMBACKING);
				node->tn_reg.tn_pages_in_aobj = 0;
			}
			TMPFS_NODE_UNLOCK(node);
		}
		error = vop_stdopen(ap);
	}

	return (error);
}

/* --------------------------------------------------------------------- */

static int
tmpfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct tmpfs_node *node;
	int error;

	node = VP_TO_TMPFS_NODE(vp);

	if (node->tn_links > 0) {
		/*
		 * Update node times.  No need to do it if the node has
		 * been deleted, because it will vanish after we return.
		 */
		tmpfs_update(vp);
	}

	error = vop_stdclose(ap);

	return (error);
}

/* --------------------------------------------------------------------- */

int
tmpfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error;
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);

	switch (vp->v_type) {
	case VDIR:
		/* FALLTHROUGH */
	case VLNK:
		/* FALLTHROUGH */
	case VREG:
		if ((ap->a_mode & VWRITE) &&
	            (vp->v_mount->mnt_flag & MNT_RDONLY)) {
			error = EROFS;
			goto out;
		}
		break;

	case VBLK:
		/* FALLTHROUGH */
	case VCHR:
		/* FALLTHROUGH */
	case VSOCK:
		/* FALLTHROUGH */
	case VFIFO:
		break;

	default:
		error = EINVAL;
		goto out;
	}

	if ((ap->a_mode & VWRITE) && (node->tn_flags & IMMUTABLE)) {
		error = EPERM;
		goto out;
	}

	error = vop_helper_access(ap, node->tn_uid, node->tn_gid,
			          node->tn_mode, 0);
out:
	return error;
}

/* --------------------------------------------------------------------- */

int
tmpfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);

	tmpfs_update(vp);

	vap->va_type = vp->v_type;
	vap->va_mode = node->tn_mode;
	vap->va_nlink = node->tn_links;
	vap->va_uid = node->tn_uid;
	vap->va_gid = node->tn_gid;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = node->tn_id;
	vap->va_size = node->tn_size;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_atime.tv_sec = node->tn_atime;
	vap->va_atime.tv_nsec = node->tn_atimensec;
	vap->va_mtime.tv_sec = node->tn_mtime;
	vap->va_mtime.tv_nsec = node->tn_mtimensec;
	vap->va_ctime.tv_sec = node->tn_ctime;
	vap->va_ctime.tv_nsec = node->tn_ctimensec;
	vap->va_gen = node->tn_gen;
	vap->va_flags = node->tn_flags;
	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		vap->va_rmajor = umajor(node->tn_rdev);
		vap->va_rminor = uminor(node->tn_rdev);
	}
	vap->va_bytes = round_page(node->tn_size);
	vap->va_filerev = 0;

	return 0;
}

/* --------------------------------------------------------------------- */

int
tmpfs_getattr_lite(struct vop_getattr_lite_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr_lite *lvap = ap->a_lvap;
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);

	tmpfs_update(vp);

	lvap->va_type = vp->v_type;
	lvap->va_mode = node->tn_mode;
	lvap->va_nlink = node->tn_links;
	lvap->va_uid = node->tn_uid;
	lvap->va_gid = node->tn_gid;
#if 0
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = node->tn_id;
#endif
	lvap->va_size = node->tn_size;
#if 0
	vap->va_blocksize = PAGE_SIZE;
	vap->va_gen = node->tn_gen;
#endif
	lvap->va_flags = node->tn_flags;
#if 0
	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		vap->va_rmajor = umajor(node->tn_rdev);
		vap->va_rminor = uminor(node->tn_rdev);
	}
	vap->va_bytes = -1;
	vap->va_filerev = 0;
#endif

	return 0;
}


/* --------------------------------------------------------------------- */

int
tmpfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct tmpfs_node *node = VP_TO_TMPFS_NODE(vp);
	int error = 0;
	int kflags = 0;

	TMPFS_NODE_LOCK(node);
	if (error == 0 && (vap->va_flags != VNOVAL)) {
		error = tmpfs_chflags(vp, vap->va_flags, cred);
		kflags |= NOTE_ATTRIB;
	}

	if (error == 0 && (vap->va_size != VNOVAL)) {
		/* restore any saved pages before proceeding */
		if (node->tn_reg.tn_pages_in_aobj) {
			tmpfs_move_pages(node->tn_reg.tn_aobj, vp->v_object,
					 TMPFS_MOVF_FROMBACKING |
					 TMPFS_MOVF_DEACTIVATE);
			node->tn_reg.tn_pages_in_aobj = 0;
		}
		if (vap->va_size > node->tn_size)
			kflags |= NOTE_WRITE | NOTE_EXTEND;
		else
			kflags |= NOTE_WRITE;
		error = tmpfs_chsize(vp, vap->va_size, cred);
	}

	if (error == 0 && (vap->va_uid != (uid_t)VNOVAL ||
			   vap->va_gid != (gid_t)VNOVAL)) {
		error = tmpfs_chown(vp, vap->va_uid, vap->va_gid, cred);
		kflags |= NOTE_ATTRIB;
	}

	if (error == 0 && (vap->va_mode != (mode_t)VNOVAL)) {
		error = tmpfs_chmod(vp, vap->va_mode, cred);
		kflags |= NOTE_ATTRIB;
	}

	if (error == 0 && ((vap->va_atime.tv_sec != VNOVAL &&
	    vap->va_atime.tv_nsec != VNOVAL) ||
	    (vap->va_mtime.tv_sec != VNOVAL &&
	    vap->va_mtime.tv_nsec != VNOVAL) )) {
		error = tmpfs_chtimes(vp, &vap->va_atime, &vap->va_mtime,
				      vap->va_vaflags, cred);
		kflags |= NOTE_ATTRIB;
	}

	/*
	 * Update the node times.  We give preference to the error codes
	 * generated by this function rather than the ones that may arise
	 * from tmpfs_update.
	 */
	tmpfs_update(vp);
	TMPFS_NODE_UNLOCK(node);
	tmpfs_knote(vp, kflags);

	return (error);
}

/* --------------------------------------------------------------------- */

/*
 * fsync is usually a NOP, but we must take action when unmounting or
 * when recycling.
 */
static int
tmpfs_fsync(struct vop_fsync_args *ap)
{
	struct tmpfs_node *node;
	struct vnode *vp = ap->a_vp;

	node = VP_TO_TMPFS_NODE(vp);

	/*
	 * tmpfs vnodes typically remain dirty, avoid long syncer scans
	 * by forcing removal from the syncer list.
	 */
	vn_syncer_remove(vp, 1);

	tmpfs_update(vp);
	if (vp->v_type == VREG) {
		if (vp->v_flag & VRECLAIMED) {
			if (node->tn_links == 0)
				tmpfs_truncate(vp, 0);
			else
				vfsync(ap->a_vp, ap->a_waitfor, 1, NULL, NULL);
		}
	}

	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_read(struct vop_read_args *ap)
{
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct tmpfs_node *node;
	off_t base_offset;
	size_t offset;
	size_t len;
	size_t resid;
	int error;
	int seqcount;

	/*
	 * Check the basics
	 */
	if (uio->uio_offset < 0)
		return (EINVAL);
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Extract node, try to shortcut the operation through
	 * the VM page cache, allowing us to avoid buffer cache
	 * overheads.
	 */
	node = VP_TO_TMPFS_NODE(vp);
        resid = uio->uio_resid;
	seqcount = ap->a_ioflag >> IO_SEQSHIFT;
        error = vop_helper_read_shortcut(ap);
        if (error)
                return error;
        if (uio->uio_resid == 0) {
		if (resid)
			goto finished;
		return error;
	}

	/*
	 * restore any saved pages before proceeding
	 */
	if (node->tn_reg.tn_pages_in_aobj) {
		TMPFS_NODE_LOCK(node);
		if (node->tn_reg.tn_pages_in_aobj) {
			tmpfs_move_pages(node->tn_reg.tn_aobj, vp->v_object,
					 TMPFS_MOVF_FROMBACKING);
			node->tn_reg.tn_pages_in_aobj = 0;
		}
		TMPFS_NODE_UNLOCK(node);
	}

	/*
	 * Fall-through to our normal read code.
	 */
	while (uio->uio_resid > 0 && uio->uio_offset < node->tn_size) {
		/*
		 * Use buffer cache I/O (via tmpfs_strategy)
		 */
		offset = (size_t)uio->uio_offset & TMPFS_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - offset;
		bp = getcacheblk(vp, base_offset,
				 node->tn_blksize, GETBLK_KVABIO);
		if (bp == NULL) {
			if (tmpfs_cluster_rd_enable) {
				error = cluster_readx(vp, node->tn_size,
						     base_offset,
						     node->tn_blksize,
						     B_NOTMETA | B_KVABIO,
						     uio->uio_resid,
						     seqcount * MAXBSIZE,
						     &bp);
			} else {
				error = bread_kvabio(vp, base_offset,
						     node->tn_blksize, &bp);
			}
			if (error) {
				brelse(bp);
				kprintf("tmpfs_read bread error %d\n", error);
				break;
			}

			/*
			 * tmpfs pretty much fiddles directly with the VM
			 * system, don't let it exhaust it or we won't play
			 * nice with other processes.
			 *
			 * Only do this if the VOP is coming from a normal
			 * read/write.  The VM system handles the case for
			 * UIO_NOCOPY.
			 */
			if (uio->uio_segflg != UIO_NOCOPY)
				vm_wait_nominal();
		}
		bp->b_flags |= B_CLUSTEROK;
		bkvasync(bp);

		/*
		 * Figure out how many bytes we can actually copy this loop.
		 */
		len = node->tn_blksize - offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		if (len > node->tn_size - uio->uio_offset)
			len = (size_t)(node->tn_size - uio->uio_offset);

		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		bqrelse(bp);
		if (error) {
			kprintf("tmpfs_read uiomove error %d\n", error);
			break;
		}
	}

finished:
	if ((node->tn_status & TMPFS_NODE_ACCESSED) == 0) {
		TMPFS_NODE_LOCK(node);
		node->tn_status |= TMPFS_NODE_ACCESSED;
		TMPFS_NODE_UNLOCK(node);
	}
	return (error);
}

static int
tmpfs_write(struct vop_write_args *ap)
{
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	struct tmpfs_node *node;
	boolean_t extended;
	off_t oldsize;
	int error;
	off_t base_offset;
	size_t offset;
	size_t len;
	struct rlimit limit;
	int trivial = 0;
	int kflags = 0;
	int seqcount;

	error = 0;
	if (uio->uio_resid == 0) {
		return error;
	}

	node = VP_TO_TMPFS_NODE(vp);

	if (vp->v_type != VREG)
		return (EINVAL);
	seqcount = ap->a_ioflag >> IO_SEQSHIFT;

	TMPFS_NODE_LOCK(node);

	/*
	 * restore any saved pages before proceeding
	 */
	if (node->tn_reg.tn_pages_in_aobj) {
		tmpfs_move_pages(node->tn_reg.tn_aobj, vp->v_object,
				 TMPFS_MOVF_FROMBACKING);
		node->tn_reg.tn_pages_in_aobj = 0;
	}

	oldsize = node->tn_size;
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = node->tn_size;

	/*
	 * Check for illegal write offsets.
	 */
	if (uio->uio_offset + uio->uio_resid >
	  VFS_TO_TMPFS(vp->v_mount)->tm_maxfilesize) {
		error = EFBIG;
		goto done;
	}

	/*
	 * NOTE: Ignore if UIO does not come from a user thread (e.g. VN).
	 */
	if (vp->v_type == VREG && td != NULL && td->td_lwp != NULL) {
		error = kern_getrlimit(RLIMIT_FSIZE, &limit);
		if (error)
			goto done;
		if (uio->uio_offset + uio->uio_resid > limit.rlim_cur) {
			ksignal(td->td_proc, SIGXFSZ);
			error = EFBIG;
			goto done;
		}
	}

	/*
	 * Extend the file's size if necessary
	 */
	extended = ((uio->uio_offset + uio->uio_resid) > node->tn_size);

	while (uio->uio_resid > 0) {
		/*
		 * Don't completely blow out running buffer I/O
		 * when being hit from the pageout daemon.
		 */
		if (uio->uio_segflg == UIO_NOCOPY &&
		    (ap->a_ioflag & IO_RECURSE) == 0) {
			bwillwrite(node->tn_blksize);
		}

		/*
		 * Use buffer cache I/O (via tmpfs_strategy)
		 *
		 * Calculate the maximum bytes we can write to the buffer at
		 * this offset (after resizing).
		 */
		offset = (size_t)uio->uio_offset & TMPFS_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - offset;
		len = uio->uio_resid;
		if (len > TMPFS_BLKSIZE - offset)
			len = TMPFS_BLKSIZE - offset;

		if ((uio->uio_offset + len) > node->tn_size) {
			trivial = (uio->uio_offset <= node->tn_size);
			error = tmpfs_reg_resize(vp, uio->uio_offset + len,
						 trivial);
			if (error)
				break;
		}

		/*
		 * Read to fill in any gaps.  Theoretically we could
		 * optimize this if the write covers the entire buffer
		 * and is not a UIO_NOCOPY write, however this can lead
		 * to a security violation exposing random kernel memory
		 * (whatever junk was in the backing VM pages before).
		 *
		 * So just use bread() to do the right thing.
		 */
		error = bread_kvabio(vp, base_offset, node->tn_blksize, &bp);
		bkvasync(bp);
		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		if (error) {
			kprintf("tmpfs_write uiomove error %d\n", error);
			brelse(bp);
			break;
		}

		if (uio->uio_offset > node->tn_size) {
			node->tn_size = uio->uio_offset;
			kflags |= NOTE_EXTEND;
		}
		kflags |= NOTE_WRITE;

		/*
		 * UIO_NOCOPY is a sensitive state due to potentially being
		 * issued from the pageout daemon while in a low-memory
		 * situation.  However, in order to cluster the I/O nicely
		 * (e.g. 64KB+ writes instead of 16KB writes), we still try
		 * to follow the same semantics that any other filesystem
		 * might use.
		 *
		 * For the normal case we buwrite(), dirtying the underlying
		 * VM pages instead of dirtying the buffer and releasing the
		 * buffer as a clean buffer.  This allows tmpfs to use
		 * essentially all available memory to cache file data.
		 * If we used bdwrite() the buffer cache would wind up
		 * flushing the data to swap too quickly.
		 *
		 * But because tmpfs can seriously load the VM system we
		 * fall-back to using bdwrite() when free memory starts
		 * to get low.  This shifts the load away from the VM system
		 * and makes tmpfs act more like a normal filesystem with
		 * regards to disk activity.
		 *
		 * tmpfs pretty much fiddles directly with the VM
		 * system, don't let it exhaust it or we won't play
		 * nice with other processes.  Only do this if the
		 * VOP is coming from a normal read/write.  The VM system
		 * handles the case for UIO_NOCOPY.
		 */
		bp->b_flags |= B_CLUSTEROK;
		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Flush from the pageout daemon, deal with potentially
			 * very heavy tmpfs write activity causing long stalls
			 * in the pageout daemon before pages get to free/cache.
			 *
			 * We have to be careful not to bypass the page queues
			 * entirely or we can cause write-read thrashing and
			 * delay the paging of data that is more pageable then
			 * our current data.
			 *
			 * (a) Under severe pressure setting B_DIRECT will
			 *     cause a buffer release to try to free the
			 *     underlying pages.
			 *
			 * (b) Under modest memory pressure the B_AGE flag
			 *     we retire the buffer and its underlying pages
			 *     more quickly than normal.
			 *
			 *     We could also force this by setting B_NOTMETA
			 *     but that might have other unintended side-
			 *     effects (e.g. setting PG_NOTMETA on the VM page).
			 *
			 * (c) For the pageout->putpages->generic_putpages->
			 *     UIO_NOCOPY-write (here), issuing an immediate
			 *     write prevents any real clustering from
			 *     happening because the buffers probably aren't
			 *     (yet) marked dirty, or lost due to prior use
			 *     of buwrite().  Try to use the normal
			 *     cluster_write() mechanism for performance.
			 *
			 * Hopefully this will unblock the VM system more
			 * quickly under extreme tmpfs write load.
			 */
			if (tmpfs_bufcache_mode >= 2) {
				if (vm_page_count_min(vm_page_free_hysteresis))
					bp->b_flags |= B_DIRECT | B_TTC;
				if (vm_pages_needed || vm_paging_needed(0))
					bp->b_flags |= B_AGE;
			}
			bp->b_flags |= B_RELBUF;
			bp->b_act_count = 0;	/* buffer->deactivate pgs */
			if (tmpfs_cluster_wr_enable &&
			    (ap->a_ioflag & (IO_SYNC | IO_DIRECT)) == 0) {
				cluster_write(bp, node->tn_size,
					      node->tn_blksize, seqcount);
			} else {
				cluster_awrite(bp);
			}
		} else if (vm_page_count_min(0) ||
			   ((vm_pages_needed || vm_paging_needed(0)) &&
			    tmpfs_bufcache_mode >= 1)) {
			/*
			 * If the pageout daemon is running we cycle the
			 * write through the buffer cache normally to
			 * pipeline the flush, thus avoiding adding any
			 * more memory pressure to the pageout daemon.
			 */
			bp->b_act_count = 0;	/* buffer->deactivate pgs */
			if (tmpfs_cluster_wr_enable) {
				cluster_write(bp, node->tn_size,
					      node->tn_blksize, seqcount);
			} else {
				bdwrite(bp);
			}
		} else {
			/*
			 * Otherwise run the buffer directly through to the
			 * backing VM store, leaving the buffer clean so
			 * buffer limits do not force early flushes to swap.
			 */
			buwrite(bp);
			/*vm_wait_nominal();*/
		}

		if (bp->b_error) {
			kprintf("tmpfs_write bwrite error %d\n", bp->b_error);
			break;
		}
	}

	if (error) {
		if (extended) {
			(void)tmpfs_reg_resize(vp, oldsize, trivial);
			kflags &= ~NOTE_EXTEND;
		}
		goto done;
	}

	/*
	 * Currently we don't set the mtime on files modified via mmap()
	 * because we can't tell the difference between those modifications
	 * and an attempt by the pageout daemon to flush tmpfs pages to
	 * swap.
	 *
	 * This is because in order to defer flushes as long as possible
	 * buwrite() works by marking the underlying VM pages dirty in
	 * order to be able to dispose of the buffer cache buffer without
	 * flushing it.
	 */
	if (uio->uio_segflg == UIO_NOCOPY) {
		if (vp->v_flag & VLASTWRITETS) {
			node->tn_mtime = vp->v_lastwrite_ts.tv_sec;
			node->tn_mtimensec = vp->v_lastwrite_ts.tv_nsec;
		}
	} else {
		node->tn_status |= TMPFS_NODE_MODIFIED;
		vclrflags(vp, VLASTWRITETS);
	}

	if (extended)
		node->tn_status |= TMPFS_NODE_CHANGED;

	if (node->tn_mode & (S_ISUID | S_ISGID)) {
		if (priv_check_cred(ap->a_cred, PRIV_VFS_RETAINSUGID, 0))
			node->tn_mode &= ~(S_ISUID | S_ISGID);
	}
done:
	TMPFS_NODE_UNLOCK(node);
	if (kflags)
		tmpfs_knote(vp, kflags);

	return(error);
}

static int
tmpfs_advlock(struct vop_advlock_args *ap)
{
	struct tmpfs_node *node;
	struct vnode *vp = ap->a_vp;
	int error;

	node = VP_TO_TMPFS_NODE(vp);
	error = (lf_advlock(ap, &node->tn_advlock, node->tn_size));

	return (error);
}

/*
 * The strategy function is typically only called when memory pressure
 * forces the system to attempt to pageout pages.  It can also be called
 * by [n]vtruncbuf() when a truncation cuts a page in half.  Normal write
 * operations
 *
 * We set VKVABIO for VREG files so bp->b_data may not be synchronized to
 * our cpu.  swap_pager_strategy() is all we really use, and it directly
 * supports this.
 */
static int
tmpfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct tmpfs_node *node;
	vm_object_t uobj;
	vm_page_t m;
	int i;

	if (vp->v_type != VREG) {
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR | B_INVAL;
		bp->b_error = EINVAL;
		biodone(bio);
		return(0);
	}

	node = VP_TO_TMPFS_NODE(vp);

	uobj = node->tn_reg.tn_aobj;

	/*
	 * Don't bother flushing to swap if there is no swap, just
	 * ensure that the pages are marked as needing a commit (still).
	 */
	if (bp->b_cmd == BUF_CMD_WRITE && vm_swap_size == 0) {
		for (i = 0; i < bp->b_xio.xio_npages; ++i) {
			m = bp->b_xio.xio_pages[i];
			vm_page_need_commit(m);
		}
		bp->b_resid = 0;
		bp->b_error = 0;
		biodone(bio);
	} else {
#if 0
		/*
		 * XXX removed, this does not work well because under heavy
		 * filesystem loads it often
		 * forces the data to be read right back in again after
		 * being written due to bypassing normal LRU operation.
		 *
		 * Tell the buffer cache to try to recycle the pages
		 * to PQ_CACHE on release.
		 */
		if (tmpfs_bufcache_mode >= 2 ||
		    (tmpfs_bufcache_mode == 1 && vm_paging_needed(0))) {
			bp->b_flags |= B_TTC;
		}
#endif
		nbio = push_bio(bio);
		nbio->bio_done = tmpfs_strategy_done;
		nbio->bio_offset = bio->bio_offset;
		swap_pager_strategy(uobj, nbio);
	}
	return 0;
}

/*
 * If we were unable to commit the pages to swap make sure they are marked
 * as needing a commit (again).  If we were, clear the flag to allow the
 * pages to be freed.
 *
 * Do not error-out the buffer.  In particular, vinvalbuf() needs to
 * always work.
 */
static void
tmpfs_strategy_done(struct bio *bio)
{
	struct buf *bp;
	vm_page_t m;
	int i;

	bp = bio->bio_buf;

	if (bp->b_flags & B_ERROR) {
		bp->b_flags &= ~B_ERROR;
		bp->b_error = 0;
		bp->b_resid = 0;
		for (i = 0; i < bp->b_xio.xio_npages; ++i) {
			m = bp->b_xio.xio_pages[i];
			vm_page_need_commit(m);
		}
	} else {
		for (i = 0; i < bp->b_xio.xio_npages; ++i) {
			m = bp->b_xio.xio_pages[i];
			vm_page_clear_commit(m);
		}
	}
	bio = pop_bio(bio);
	biodone(bio);
}

/*
 * To make write clustering work well make the backing store look
 * contiguous to the cluster_*() code.  The swap_strategy() function
 * will take it from there.
 *
 * Use MAXBSIZE-sized chunks as a micro-optimization to make random
 * flushes leave full-sized gaps.
 */
static int
tmpfs_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = MAXBSIZE - (ap->a_loffset & (MAXBSIZE - 1));
	if (ap->a_runb != NULL)
		*ap->a_runb = ap->a_loffset & (MAXBSIZE - 1);

	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nremove(struct vop_nremove_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vnode *vp;
	int error;
	struct tmpfs_dirent *de;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *dnode;
	struct tmpfs_node *node;

	/*
	 * We have to acquire the vp from ap->a_nch because we will likely
	 * unresolve the namecache entry, and a vrele/vput is needed to
	 * trigger the tmpfs_inactive/tmpfs_reclaim sequence.
	 *
	 * We have to use vget to clear any inactive state on the vnode,
	 * otherwise the vnode may remain inactive and thus tmpfs_inactive
	 * will not get called when we release it.
	 */
	error = cache_vget(ap->a_nch, ap->a_cred, LK_SHARED, &vp);
	KKASSERT(vp->v_mount == dvp->v_mount);
	KKASSERT(error == 0);
	vn_unlock(vp);

	if (vp->v_type == VDIR) {
		error = EISDIR;
		goto out2;
	}

	dnode = VP_TO_TMPFS_DIR(dvp);
	node = VP_TO_TMPFS_NODE(vp);
	tmp = VFS_TO_TMPFS(vp->v_mount);

	TMPFS_NODE_LOCK(dnode);
	TMPFS_NODE_LOCK(node);
	de = tmpfs_dir_lookup(dnode, node, ncp);
	if (de == NULL) {
		error = ENOENT;
		TMPFS_NODE_UNLOCK(node);
		TMPFS_NODE_UNLOCK(dnode);
		goto out;
	}

	/* Files marked as immutable or append-only cannot be deleted. */
	if ((node->tn_flags & (IMMUTABLE | APPEND | NOUNLINK)) ||
	    (dnode->tn_flags & APPEND)) {
		error = EPERM;
		TMPFS_NODE_UNLOCK(node);
		TMPFS_NODE_UNLOCK(dnode);
		goto out;
	}

	/* Remove the entry from the directory; as it is a file, we do not
	 * have to change the number of hard links of the directory. */
	tmpfs_dir_detach_locked(dnode, de);
	TMPFS_NODE_UNLOCK(dnode);

	/* Free the directory entry we just deleted.  Note that the node
	 * referred by it will not be removed until the vnode is really
	 * reclaimed. */
	tmpfs_free_dirent(tmp, de);

	if (node->tn_links > 0)
		node->tn_status |= TMPFS_NODE_CHANGED;
	TMPFS_NODE_UNLOCK(node);

	cache_unlink(ap->a_nch);
	tmpfs_knote(vp, NOTE_DELETE);
	error = 0;

out:
	if (error == 0)
		tmpfs_knote(dvp, NOTE_WRITE);
out2:
	vrele(vp);

	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nlink(struct vop_nlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct tmpfs_mount *tmp = VFS_TO_TMPFS(vp->v_mount);
	struct namecache *ncp = ap->a_nch->ncp;
	struct tmpfs_dirent *de;
	struct tmpfs_node *node;
	struct tmpfs_node *dnode;
	int error;

	KKASSERT(dvp != vp); /* XXX When can this be false? */

	node = VP_TO_TMPFS_NODE(vp);
	dnode = VP_TO_TMPFS_NODE(dvp);
	TMPFS_NODE_LOCK(dnode);

	/* XXX: Why aren't the following two tests done by the caller? */

	/* Hard links of directories are forbidden. */
	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}

	/* Cannot create cross-device links. */
	if (dvp->v_mount != vp->v_mount) {
		error = EXDEV;
		goto out;
	}

	/* Cannot hard-link into a deleted directory */
	if (dnode != tmp->tm_root && dnode->tn_dir.tn_parent == NULL) {
		error = ENOENT;
		goto out;
	}

	/* Ensure that we do not overflow the maximum number of links imposed
	 * by the system. */
	KKASSERT(node->tn_links <= LINK_MAX);
	if (node->tn_links >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}

	/* We cannot create links of files marked immutable or append-only. */
	if (node->tn_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}

	/* Allocate a new directory entry to represent the node. */
	error = tmpfs_alloc_dirent(VFS_TO_TMPFS(vp->v_mount), node,
				   ncp->nc_name, ncp->nc_nlen, &de);
	if (error != 0)
		goto out;

	/* Insert the new directory entry into the appropriate directory. */
	tmpfs_dir_attach_locked(dnode, de);

	/* vp link count has changed, so update node times. */

	TMPFS_NODE_LOCK(node);
	node->tn_status |= TMPFS_NODE_CHANGED;
	TMPFS_NODE_UNLOCK(node);
	tmpfs_update(vp);

	tmpfs_knote(vp, NOTE_LINK);
	cache_setunresolved(ap->a_nch);
	cache_setvp(ap->a_nch, vp);
	error = 0;

out:
	TMPFS_NODE_UNLOCK(dnode);
	if (error == 0)
		tmpfs_knote(dvp, NOTE_WRITE);
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nrename(struct vop_nrename_args *ap)
{
	struct vnode *fdvp = ap->a_fdvp;
	struct namecache *fncp = ap->a_fnch->ncp;
	struct vnode *fvp = fncp->nc_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct namecache *tncp = ap->a_tnch->ncp;
	struct vnode *tvp;
	struct tmpfs_dirent *de, *tde, *de2;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *fdnode;
	struct tmpfs_node *tdnode;
	struct tmpfs_node *fnode;
	struct tmpfs_node *tnode;
	char *newname;
	char *oldname;
	int error;

	KKASSERT(fdvp->v_mount == fvp->v_mount);

	/*
	 * Because tvp can get overwritten we have to vget it instead of
	 * just vref or use it, otherwise it's VINACTIVE flag may not get
	 * cleared and the node won't get destroyed.
	 */
	error = cache_vget(ap->a_tnch, ap->a_cred, LK_SHARED, &tvp);
	if (error == 0) {
		tnode = VP_TO_TMPFS_NODE(tvp);
		vn_unlock(tvp);
	} else {
		tnode = NULL;
	}

	/* Disallow cross-device renames.
	 * XXX Why isn't this done by the caller? */
	if (fvp->v_mount != tdvp->v_mount ||
	    (tvp != NULL && fvp->v_mount != tvp->v_mount)) {
		error = EXDEV;
		goto out;
	}

	tmp = VFS_TO_TMPFS(tdvp->v_mount);
	tdnode = VP_TO_TMPFS_DIR(tdvp);

	/* If source and target are the same file, there is nothing to do. */
	if (fvp == tvp) {
		error = 0;
		goto out;
	}

	fdnode = VP_TO_TMPFS_DIR(fdvp);
	fnode = VP_TO_TMPFS_NODE(fvp);

	tmpfs_lock4(fdnode, tdnode, fnode, tnode);

	/*
	 * Cannot rename into a deleted directory
	 */
	if (tdnode != tmp->tm_root && tdnode->tn_dir.tn_parent == NULL) {
		error = ENOENT;
		goto out_locked;
	}

	/* Avoid manipulating '.' and '..' entries. */
	de = tmpfs_dir_lookup(fdnode, fnode, fncp);
	if (de == NULL) {
		error = ENOENT;
		goto out_locked;
	}
	KKASSERT(de->td_node == fnode);

	/*
	 * If replacing an entry in the target directory and that entry
	 * is a directory, it must be empty.
	 *
	 * Kern_rename gurantees the destination to be a directory
	 * if the source is one (it does?).
	 */
	if (tvp != NULL) {
		KKASSERT(tnode != NULL);

		if ((tnode->tn_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
		    (tdnode->tn_flags & (APPEND | IMMUTABLE))) {
			error = EPERM;
			goto out_locked;
		}

		if (fnode->tn_type == VDIR && tnode->tn_type == VDIR) {
			if (tnode->tn_size > 0) {
				error = ENOTEMPTY;
				goto out_locked;
			}
		} else if (fnode->tn_type == VDIR && tnode->tn_type != VDIR) {
			error = ENOTDIR;
			goto out_locked;
		} else if (fnode->tn_type != VDIR && tnode->tn_type == VDIR) {
			error = EISDIR;
			goto out_locked;
		} else {
			KKASSERT(fnode->tn_type != VDIR &&
				tnode->tn_type != VDIR);
		}
	}

	if ((fnode->tn_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (fdnode->tn_flags & (APPEND | IMMUTABLE))) {
		error = EPERM;
		goto out_locked;
	}

	/*
	 * Ensure that we have enough memory to hold the new name, if it
	 * has to be changed.
	 */
	if (fncp->nc_nlen != tncp->nc_nlen ||
	    bcmp(fncp->nc_name, tncp->nc_name, fncp->nc_nlen) != 0) {
		newname = kmalloc(tncp->nc_nlen + 1, tmp->tm_name_zone,
				  M_WAITOK | M_NULLOK);
		if (newname == NULL) {
			error = ENOSPC;
			goto out_locked;
		}
		bcopy(tncp->nc_name, newname, tncp->nc_nlen);
		newname[tncp->nc_nlen] = '\0';
	} else {
		newname = NULL;
	}

	/*
	 * Unlink entry from source directory.  Note that the kernel has
	 * already checked for illegal recursion cases (renaming a directory
	 * into a subdirectory of itself).
	 */
	if (fdnode != tdnode) {
		tmpfs_dir_detach_locked(fdnode, de);
	} else {
		/* XXX depend on namecache lock */
		KKASSERT(de == tmpfs_dir_lookup(fdnode, fnode, fncp));
		RB_REMOVE(tmpfs_dirtree, &fdnode->tn_dir.tn_dirtree, de);
		RB_REMOVE(tmpfs_dirtree_cookie,
			  &fdnode->tn_dir.tn_cookietree, de);
	}

	/*
	 * Handle any name change.  Swap with newname, we will
	 * deallocate it at the end.
	 */
	if (newname != NULL) {
		oldname = de->td_name;
		de->td_name = newname;
		de->td_namelen = (uint16_t)tncp->nc_nlen;
		newname = oldname;
	}

	/*
	 * If we are overwriting an entry, we have to remove the old one
	 * from the target directory.
	 */
	if (tvp != NULL) {
		/* Remove the old entry from the target directory. */
		tde = tmpfs_dir_lookup(tdnode, tnode, tncp);
		tmpfs_dir_detach_locked(tdnode, tde);
		tmpfs_knote(tdnode->tn_vnode, NOTE_DELETE);

		/*
		 * Free the directory entry we just deleted.  Note that the
		 * node referred by it will not be removed until the vnode is
		 * really reclaimed.
		 */
		tmpfs_free_dirent(VFS_TO_TMPFS(tvp->v_mount), tde);
		/*cache_inval_vp(tvp, CINV_DESTROY);*/
	}

	/*
	 * Link entry to target directory.  If the entry
	 * represents a directory move the parent linkage
	 * as well.
	 */
	if (fdnode != tdnode) {
		if (de->td_node->tn_type == VDIR) {
			TMPFS_VALIDATE_DIR(fnode);
		}
		tmpfs_dir_attach_locked(tdnode, de);
	} else {
		tdnode->tn_status |= TMPFS_NODE_MODIFIED;
		de2 = RB_INSERT(tmpfs_dirtree, &tdnode->tn_dir.tn_dirtree, de);
		KASSERT(de2 == NULL,
			("tmpfs_nrenameA: duplicate insertion of %p, has %p\n",
			de, de2));
		de2 = RB_INSERT(tmpfs_dirtree_cookie,
				&tdnode->tn_dir.tn_cookietree, de);
		KASSERT(de2 == NULL,
			("tmpfs_nrenameB: duplicate insertion of %p, has %p\n",
			de, de2));
	}
	tmpfs_unlock4(fdnode, tdnode, fnode, tnode);

	/*
	 * Finish up
	 */
	if (newname) {
		kfree(newname, tmp->tm_name_zone);
		newname = NULL;
	}
	cache_rename(ap->a_fnch, ap->a_tnch);
	tmpfs_knote(ap->a_fdvp, NOTE_WRITE);
	tmpfs_knote(ap->a_tdvp, NOTE_WRITE);
	if (fnode->tn_vnode)
		tmpfs_knote(fnode->tn_vnode, NOTE_RENAME);
	if (tvp)
		vrele(tvp);
	return 0;

out_locked:
	tmpfs_unlock4(fdnode, tdnode, fnode, tnode);
out:
	if (tvp)
		vrele(tvp);
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nmkdir(struct vop_nmkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error;

	KKASSERT(vap->va_type == VDIR);

	error = tmpfs_alloc_file(dvp, vpp, vap, ncp, cred, NULL);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
		tmpfs_knote(dvp, NOTE_WRITE | NOTE_LINK);
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nrmdir(struct vop_nrmdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vnode *vp;
	struct tmpfs_dirent *de;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *dnode;
	struct tmpfs_node *node;
	int error;

	/*
	 * We have to acquire the vp from ap->a_nch because we will likely
	 * unresolve the namecache entry, and a vrele/vput is needed to
	 * trigger the tmpfs_inactive/tmpfs_reclaim sequence.
	 *
	 * We have to use vget to clear any inactive state on the vnode,
	 * otherwise the vnode may remain inactive and thus tmpfs_inactive
	 * will not get called when we release it.
	 */
	error = cache_vget(ap->a_nch, ap->a_cred, LK_SHARED, &vp);
	KKASSERT(error == 0);
	vn_unlock(vp);

	/*
	 * Prevalidate so we don't hit an assertion later
	 */
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	tmp = VFS_TO_TMPFS(dvp->v_mount);
	dnode = VP_TO_TMPFS_DIR(dvp);
	node = VP_TO_TMPFS_DIR(vp);

	/*
	 *
	 */
	TMPFS_NODE_LOCK(dnode);
	TMPFS_NODE_LOCK(node);

	/*
	 * Only empty directories can be removed.
	 */
	if (node->tn_size > 0) {
		error = ENOTEMPTY;
		goto out_locked;
	}

	if ((dnode->tn_flags & APPEND)
	    || (node->tn_flags & (NOUNLINK | IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out_locked;
	}

	/*
	 * This invariant holds only if we are not trying to
	 * remove "..".  We checked for that above so this is safe now.
	 */
	KKASSERT(node->tn_dir.tn_parent == dnode);

	/*
	 * Get the directory entry associated with node (vp)
	 */
	de = tmpfs_dir_lookup(dnode, node, ncp);
	KKASSERT(TMPFS_DIRENT_MATCHES(de, ncp->nc_name, ncp->nc_nlen));

	/* Check flags to see if we are allowed to remove the directory. */
	if ((dnode->tn_flags & APPEND) ||
	    node->tn_flags & (NOUNLINK | IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out_locked;
	}

	/* Detach the directory entry from the directory (dnode). */
	tmpfs_dir_detach_locked(dnode, de);

	/*
	 * Must set parent linkage to NULL (tested by ncreate to disallow
	 * the creation of new files/dirs in a deleted directory)
	 */
	node->tn_status |= TMPFS_NODE_CHANGED;

	dnode->tn_status |= TMPFS_NODE_ACCESSED | TMPFS_NODE_CHANGED |
			    TMPFS_NODE_MODIFIED;

	/* Free the directory entry we just deleted.  Note that the node
	 * referred by it will not be removed until the vnode is really
	 * reclaimed. */
	tmpfs_free_dirent(tmp, de);

	/* Release the deleted vnode (will destroy the node, notify
	 * interested parties and clean it from the cache). */

	dnode->tn_status |= TMPFS_NODE_CHANGED;

	TMPFS_NODE_UNLOCK(node);
	TMPFS_NODE_UNLOCK(dnode);

	tmpfs_update(dvp);
	cache_unlink(ap->a_nch);
	tmpfs_knote(dvp, NOTE_WRITE | NOTE_LINK);
	vrele(vp);
	return 0;

out_locked:
	TMPFS_NODE_UNLOCK(node);
	TMPFS_NODE_UNLOCK(dnode);

out:
	vrele(vp);

	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_nsymlink(struct vop_nsymlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct namecache *ncp = ap->a_nch->ncp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	char *target = ap->a_target;
	int error;

	vap->va_type = VLNK;
	error = tmpfs_alloc_file(dvp, vpp, vap, ncp, cred, target);
	if (error == 0) {
		tmpfs_knote(*vpp, NOTE_WRITE);
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int *eofflag = ap->a_eofflag;
	off_t **cookies = ap->a_cookies;
	int *ncookies = ap->a_ncookies;
	struct tmpfs_mount *tmp;
	int error;
	off_t startoff;
	off_t cnt = 0;
	struct tmpfs_node *node;

	/* This operation only makes sense on directory nodes. */
	if (vp->v_type != VDIR) {
		return ENOTDIR;
	}

	tmp = VFS_TO_TMPFS(vp->v_mount);
	node = VP_TO_TMPFS_DIR(vp);
	startoff = uio->uio_offset;

	if (uio->uio_offset == TMPFS_DIRCOOKIE_DOT) {
		error = tmpfs_dir_getdotdent(node, uio);
		if (error != 0) {
			TMPFS_NODE_LOCK_SH(node);
			goto outok;
		}
		cnt++;
	}

	if (uio->uio_offset == TMPFS_DIRCOOKIE_DOTDOT) {
		/* may lock parent, cannot hold node lock */
		error = tmpfs_dir_getdotdotdent(tmp, node, uio);
		if (error != 0) {
			TMPFS_NODE_LOCK_SH(node);
			goto outok;
		}
		cnt++;
	}

	TMPFS_NODE_LOCK_SH(node);
	error = tmpfs_dir_getdents(node, uio, &cnt);

outok:
	KKASSERT(error >= -1);

	if (error == -1)
		error = 0;

	if (eofflag != NULL)
		*eofflag =
		    (error == 0 && uio->uio_offset == TMPFS_DIRCOOKIE_EOF);

	/* Update NFS-related variables. */
	if (error == 0 && cookies != NULL && ncookies != NULL) {
		off_t i;
		off_t off = startoff;
		struct tmpfs_dirent *de = NULL;

		*ncookies = cnt;
		*cookies = kmalloc(cnt * sizeof(off_t), M_TEMP, M_WAITOK);

		for (i = 0; i < cnt; i++) {
			KKASSERT(off != TMPFS_DIRCOOKIE_EOF);
			if (off == TMPFS_DIRCOOKIE_DOT) {
				off = TMPFS_DIRCOOKIE_DOTDOT;
			} else {
				if (off == TMPFS_DIRCOOKIE_DOTDOT) {
					de = RB_MIN(tmpfs_dirtree_cookie,
						&node->tn_dir.tn_cookietree);
				} else if (de != NULL) {
					de = RB_NEXT(tmpfs_dirtree_cookie,
					       &node->tn_dir.tn_cookietree, de);
				} else {
					de = tmpfs_dir_lookupbycookie(node,
								      off);
					KKASSERT(de != NULL);
					de = RB_NEXT(tmpfs_dirtree_cookie,
					       &node->tn_dir.tn_cookietree, de);
				}
				if (de == NULL)
					off = TMPFS_DIRCOOKIE_EOF;
				else
					off = tmpfs_dircookie(de);
			}
			(*cookies)[i] = off;
		}
		KKASSERT(uio->uio_offset == off);
	}
	TMPFS_NODE_UNLOCK(node);

	if ((node->tn_status & TMPFS_NODE_ACCESSED) == 0) {
		TMPFS_NODE_LOCK(node);
		node->tn_status |= TMPFS_NODE_ACCESSED;
		TMPFS_NODE_UNLOCK(node);
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int error;
	struct tmpfs_node *node;

	KKASSERT(uio->uio_offset == 0);
	KKASSERT(vp->v_type == VLNK);

	node = VP_TO_TMPFS_NODE(vp);
	TMPFS_NODE_LOCK_SH(node);
	error = uiomove(node->tn_link,
			MIN(node->tn_size, uio->uio_resid), uio);
	TMPFS_NODE_UNLOCK(node);
	if ((node->tn_status & TMPFS_NODE_ACCESSED) == 0) {
		TMPFS_NODE_LOCK(node);
		node->tn_status |= TMPFS_NODE_ACCESSED;
		TMPFS_NODE_UNLOCK(node);
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct tmpfs_node *node;
	struct mount *mp;

	mp = vp->v_mount;
	lwkt_gettoken(&mp->mnt_token);
	node = VP_TO_TMPFS_NODE(vp);

	/*
	 * Degenerate case
	 */
	if (node == NULL) {
		vrecycle(vp);
		lwkt_reltoken(&mp->mnt_token);
		return(0);
	}

	/*
	 * Get rid of unreferenced deleted vnodes sooner rather than
	 * later so the data memory can be recovered immediately.
	 *
	 * We must truncate the vnode to prevent the normal reclamation
	 * path from flushing the data for the removed file to disk.
	 */
	TMPFS_NODE_LOCK(node);
	if (node->tn_links == 0) {
		node->tn_vpstate = TMPFS_VNODE_DOOMED;
		TMPFS_NODE_UNLOCK(node);
		if (node->tn_type == VREG)
			tmpfs_truncate(vp, 0);
		vrecycle(vp);
	} else {
		/*
		 * We must retain any VM pages belonging to the vnode's
		 * object as the vnode will destroy the object during a
		 * later reclaim.  We call vinvalbuf(V_SAVE) to clean
		 * out the buffer cache.
		 *
		 * On DragonFlyBSD, vnodes are not immediately deactivated
		 * on the 1->0 refs, so this is a relatively optimal
		 * operation.  We have to do this in tmpfs_inactive()
		 * because the pages will have already been thrown away
		 * at the time tmpfs_reclaim() is called.
		 */
		if (node->tn_type == VREG &&
		    node->tn_reg.tn_pages_in_aobj == 0) {
			vinvalbuf(vp, V_SAVE, 0, 0);
			KKASSERT(RB_EMPTY(&vp->v_rbdirty_tree));
			KKASSERT(RB_EMPTY(&vp->v_rbclean_tree));
			tmpfs_move_pages(vp->v_object, node->tn_reg.tn_aobj,
					 TMPFS_MOVF_DEACTIVATE);
			node->tn_reg.tn_pages_in_aobj = 1;
		}

		TMPFS_NODE_UNLOCK(node);
	}
	lwkt_reltoken(&mp->mnt_token);

	return 0;
}

/* --------------------------------------------------------------------- */

int
tmpfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	struct mount *mp;

	mp = vp->v_mount;
	lwkt_gettoken(&mp->mnt_token);

	node = VP_TO_TMPFS_NODE(vp);
	tmp = VFS_TO_TMPFS(vp->v_mount);
	KKASSERT(mp == tmp->tm_mount);

        TMPFS_NODE_LOCK(node);
	KKASSERT(node->tn_vnode == vp);
        node->tn_vnode = NULL;
        vp->v_data = NULL;

	/*
	 * If the node referenced by this vnode was deleted by the
	 * user, we must free its associated data structures now that
	 * the vnode is being reclaimed.
	 *
	 * Directories have an extra link ref.
	 */
	if (node->tn_links == 0) {
		node->tn_vpstate = TMPFS_VNODE_DOOMED;
		tmpfs_free_node(tmp, node);
		/* eats the lock */
	} else {
		TMPFS_NODE_UNLOCK(node);
	}
	lwkt_reltoken(&mp->mnt_token);

	KKASSERT(vp->v_data == NULL);
	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_mountctl(struct vop_mountctl_args *ap)
{
	struct tmpfs_mount *tmp;
	struct mount *mp;
	int rc;

	mp = ap->a_head.a_ops->head.vv_mount;
	lwkt_gettoken(&mp->mnt_token);

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		tmp = (struct tmpfs_mount *) mp->mnt_data;

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &tmp->tm_export,
					(const struct export_args *) ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}

	lwkt_reltoken(&mp->mnt_token);
	return (rc);
}

/* --------------------------------------------------------------------- */

static int
tmpfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;

	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);

	kprintf("tag VT_TMPFS, tmpfs_node %p, flags 0x%x, links %d\n",
	    node, node->tn_flags, node->tn_links);
	kprintf("\tmode 0%o, owner %d, group %d, size %ju, status 0x%x\n",
	    node->tn_mode, node->tn_uid, node->tn_gid,
	    (uintmax_t)node->tn_size, node->tn_status);

	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);

	kprintf("\n");

	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_pathconf(struct vop_pathconf_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int name = ap->a_name;
	register_t *retval = ap->a_retval;
	struct tmpfs_mount *tmp;
	int error;

	error = 0;

	switch (name) {
	case _PC_CHOWN_RESTRICTED:
		*retval = 1;
		break;

	case _PC_FILESIZEBITS:
		tmp = VFS_TO_TMPFS(vp->v_mount);
		*retval = max(32, flsll(tmp->tm_pages_max * PAGE_SIZE) + 1);
		break;

	case _PC_LINK_MAX:
		*retval = LINK_MAX;
		break;

	case _PC_NAME_MAX:
		*retval = NAME_MAX;
		break;

	case _PC_NO_TRUNC:
		*retval = 1;
		break;

	case _PC_PATH_MAX:
		*retval = PATH_MAX;
		break;

	case _PC_PIPE_BUF:
		*retval = PIPE_BUF;
		break;

	case _PC_SYNC_IO:
		*retval = 1;
		break;

	case _PC_2_SYMLINKS:
		*retval = 1;
		break;

	default:
		error = EINVAL;
	}

	return error;
}

/************************************************************************
 *                          KQFILTER OPS                                *
 ************************************************************************/

static void filt_tmpfsdetach(struct knote *kn);
static int filt_tmpfsread(struct knote *kn, long hint);
static int filt_tmpfswrite(struct knote *kn, long hint);
static int filt_tmpfsvnode(struct knote *kn, long hint);

static struct filterops tmpfsread_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_tmpfsdetach, filt_tmpfsread };
static struct filterops tmpfswrite_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_tmpfsdetach, filt_tmpfswrite };
static struct filterops tmpfsvnode_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_tmpfsdetach, filt_tmpfsvnode };

static int
tmpfs_kqfilter (struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tmpfsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tmpfswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &tmpfsvnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);

	return(0);
}

static void
filt_tmpfsdetach(struct knote *kn)
{
	struct vnode *vp = (void *)kn->kn_hook;

	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
}

static int
filt_tmpfsread(struct knote *kn, long hint)
{
	struct vnode *vp = (void *)kn->kn_hook;
	struct tmpfs_node *node = VP_TO_TMPFS_NODE(vp);
	off_t off;

	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return(1);
	}

	/*
	 * Interlock against MP races when performing this function.
	 */
	TMPFS_NODE_LOCK_SH(node);
	off = node->tn_size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI) {
		TMPFS_NODE_UNLOCK(node);
		return(1);
	}
	if (kn->kn_data == 0) {
		kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	}
	TMPFS_NODE_UNLOCK(node);
	return (kn->kn_data != 0);
}

static int
filt_tmpfswrite(struct knote *kn, long hint)
{
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
	kn->kn_data = 0;
	return (1);
}

static int
filt_tmpfsvnode(struct knote *kn, long hint)
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
 * Helper to move VM pages between objects
 *
 * NOTE: The vm_page_rename() dirties the page, so we can clear the
 *	 PG_NEED_COMMIT flag.  If the pages are being moved into tn_aobj,
 *	 the pageout daemon will be able to page them out.
 */
static int
tmpfs_move_pages_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_pindex_t pindex;

	/*
	 * Take control of the page
	 */
	pindex = p->pindex;
	if (vm_page_busy_try(p, TRUE)) {
		vm_page_sleep_busy(p, TRUE, "tpgmov");
		info->error = -1;
		return -1;
	}
	if (p->object != info->object || p->pindex != pindex) {
		vm_page_wakeup(p);
		info->error = -1;
		return -1;
	}

	/*
	 * Make sure the page is not mapped.  These flags might also still be
	 * set heuristically even if we know the page is not mapped and must
	 * be properly cleaned up.
	 */
	if (__predict_false((p->flags & (PG_MAPPED|PG_WRITEABLE)) != 0))
		vm_page_protect(p, VM_PROT_NONE);

	/*
	 * Free or rename the page as appropriate
	 */
	if ((info->pagerflags & TMPFS_MOVF_FROMBACKING) &&
	    (p->flags & PG_SWAPPED) &&
	    (p->flags & PG_NEED_COMMIT) == 0 &&
	    p->dirty == 0) {
		/*
		 * If the page in the backing aobj was paged out to swap
		 * it will be clean and it is better to free it rather
		 * than re-dirty it.  We will assume that the page was
		 * paged out to swap for a reason!
		 *
		 * This helps avoid unnecessary swap thrashing on the page.
		 */
		vm_page_free(p);
	} else if ((info->pagerflags & TMPFS_MOVF_FROMBACKING) == 0 &&
		   (p->flags & PG_NEED_COMMIT) == 0 &&
		   p->dirty == 0) {
		/*
		 * If the page associated with the vnode was cleaned via
		 * a tmpfs_strategy() call, it exists as a swap block in
		 * aobj and it is again better to free it rather than
		 * re-dirty it.  We will assume that the page was
		 * paged out to swap for a reason!
		 *
		 * This helps avoid unnecessary swap thrashing on the page.
		 */
		vm_page_free(p);
	} else {
		/*
		 * Rename the page, which will also ensure that it is flagged
		 * as dirty and check whether a swap block association exists
		 * in the target object or not, setting appropriate flags if
		 * it does.
		 */
		vm_page_rename(p, info->dest_object, pindex);
		vm_page_clear_commit(p);
		if (info->pagerflags & TMPFS_MOVF_DEACTIVATE)
			vm_page_deactivate(p);
		vm_page_wakeup(p);
		/* page automaticaly made dirty */
	}

	return 0;
}

static
void
tmpfs_move_pages(vm_object_t src, vm_object_t dst, int movflags)
{
	struct rb_vm_page_scan_info info;

	vm_object_hold(src);
	vm_object_hold(dst);
	info.object = src;
	info.dest_object = dst;
	info.pagerflags = movflags;
	do {
		if (src->paging_in_progress)
			vm_object_pip_wait(src, "objtfs");
		info.error = 1;
		vm_page_rb_tree_RB_SCAN(&src->rb_memq, NULL,
					tmpfs_move_pages_callback, &info);
	} while (info.error < 0 || !RB_EMPTY(&src->rb_memq) ||
		 src->paging_in_progress);
	vm_object_drop(dst);
	vm_object_drop(src);
}

/* --------------------------------------------------------------------- */

/*
 * vnode operations vector used for files stored in a tmpfs file system.
 */
struct vop_ops tmpfs_vnode_vops = {
	.vop_default =			vop_defaultop,
	.vop_getpages = 		vop_stdgetpages,
	.vop_putpages = 		vop_stdputpages,
	.vop_ncreate =			tmpfs_ncreate,
	.vop_nresolve =			tmpfs_nresolve,
	.vop_nlookupdotdot =		tmpfs_nlookupdotdot,
	.vop_nmknod =			tmpfs_nmknod,
	.vop_open =			tmpfs_open,
	.vop_close =			tmpfs_close,
	.vop_access =			tmpfs_access,
	.vop_getattr =			tmpfs_getattr,
	.vop_getattr_lite =		tmpfs_getattr_lite,
	.vop_setattr =			tmpfs_setattr,
	.vop_read =			tmpfs_read,
	.vop_write =			tmpfs_write,
	.vop_fsync =			tmpfs_fsync,
	.vop_mountctl =			tmpfs_mountctl,
	.vop_nremove =			tmpfs_nremove,
	.vop_nlink =			tmpfs_nlink,
	.vop_nrename =			tmpfs_nrename,
	.vop_nmkdir =			tmpfs_nmkdir,
	.vop_nrmdir =			tmpfs_nrmdir,
	.vop_nsymlink =			tmpfs_nsymlink,
	.vop_readdir =			tmpfs_readdir,
	.vop_readlink =			tmpfs_readlink,
	.vop_inactive =			tmpfs_inactive,
	.vop_reclaim =			tmpfs_reclaim,
	.vop_print =			tmpfs_print,
	.vop_pathconf =			tmpfs_pathconf,
	.vop_bmap =			tmpfs_bmap,
	.vop_strategy =			tmpfs_strategy,
	.vop_advlock =			tmpfs_advlock,
	.vop_kqfilter =			tmpfs_kqfilter
};
