/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_subr.c	8.31 (Berkeley) 5/26/95
 * $FreeBSD: src/sys/kern/vfs_subr.c,v 1.249.2.30 2003/04/04 20:35:57 tegge Exp $
 */

/*
 * External virtual filesystem routines
 */
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/domain.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/unistd.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <sys/mplock2.h>

static MALLOC_DEFINE(M_NETADDR, "Export Host", "Export host address structure");

int numvnodes;
SYSCTL_INT(_debug, OID_AUTO, numvnodes, CTLFLAG_RD, &numvnodes, 0,
    "Number of vnodes allocated");
int verbose_reclaims;
SYSCTL_INT(_debug, OID_AUTO, verbose_reclaims, CTLFLAG_RD, &verbose_reclaims, 0,
    "Output filename of reclaimed vnode(s)");

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int vttoif_tab[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT,
};

static int reassignbufcalls;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufcalls, CTLFLAG_RW, &reassignbufcalls,
    0, "Number of times buffers have been reassigned to the proper list");

static int check_buf_overlap = 2;	/* invasive check */
SYSCTL_INT(_vfs, OID_AUTO, check_buf_overlap, CTLFLAG_RW, &check_buf_overlap,
    0, "Enable overlapping buffer checks");

int	nfs_mount_type = -1;
static struct lwkt_token spechash_token;
struct nfs_public nfs_pub;	/* publicly exported FS */

int desiredvnodes;
SYSCTL_INT(_kern, KERN_MAXVNODES, maxvnodes, CTLFLAG_RW, 
		&desiredvnodes, 0, "Maximum number of vnodes");

static void	vfs_free_addrlist (struct netexport *nep);
static int	vfs_free_netcred (struct radix_node *rn, void *w);
static int	vfs_hang_addrlist (struct mount *mp, struct netexport *nep,
				       const struct export_args *argp);

/*
 * Red black tree functions
 */
static int rb_buf_compare(struct buf *b1, struct buf *b2);
RB_GENERATE2(buf_rb_tree, buf, b_rbnode, rb_buf_compare, off_t, b_loffset);
RB_GENERATE2(buf_rb_hash, buf, b_rbhash, rb_buf_compare, off_t, b_loffset);

static int
rb_buf_compare(struct buf *b1, struct buf *b2)
{
	if (b1->b_loffset < b2->b_loffset)
		return(-1);
	if (b1->b_loffset > b2->b_loffset)
		return(1);
	return(0);
}

/*
 * Initialize the vnode management data structures. 
 *
 * Called from vfsinit()
 */
void
vfs_subr_init(void)
{
	int factor1;
	int factor2;

	/*
	 * Desiredvnodes is kern.maxvnodes.  We want to scale it 
	 * according to available system memory but we may also have
	 * to limit it based on available KVM, which is capped on 32 bit
	 * systems, to ~80K vnodes or so.
	 *
	 * WARNING!  For machines with 64-256M of ram we have to be sure
	 *	     that the default limit scales down well due to HAMMER
	 *	     taking up significantly more memory per-vnode vs UFS.
	 *	     We want around ~5800 on a 128M machine.
	 */
	factor1 = 20 * (sizeof(struct vm_object) + sizeof(struct vnode));
	factor2 = 25 * (sizeof(struct vm_object) + sizeof(struct vnode));
	desiredvnodes =
		imin((int64_t)vmstats.v_page_count * PAGE_SIZE / factor1,
		     KvaSize / factor2);
	desiredvnodes = imax(desiredvnodes, maxproc * 8);

	lwkt_token_init(&spechash_token, "spechash");
}

/*
 * Knob to control the precision of file timestamps:
 *
 *   0 = seconds only; nanoseconds zeroed.
 *   1 = seconds and nanoseconds, accurate within 1/HZ.
 *   2 = seconds and nanoseconds, truncated to microseconds.
 * >=3 = seconds and nanoseconds, maximum precision.
 */
enum { TSP_SEC, TSP_HZ, TSP_USEC, TSP_NSEC };

static int timestamp_precision = TSP_SEC;
SYSCTL_INT(_vfs, OID_AUTO, timestamp_precision, CTLFLAG_RW,
		&timestamp_precision, 0, "Precision of file timestamps");

/*
 * Get a current timestamp.
 *
 * MPSAFE
 */
void
vfs_timestamp(struct timespec *tsp)
{
	struct timeval tv;

	switch (timestamp_precision) {
	case TSP_SEC:
		tsp->tv_sec = time_second;
		tsp->tv_nsec = 0;
		break;
	case TSP_HZ:
		getnanotime(tsp);
		break;
	case TSP_USEC:
		microtime(&tv);
		TIMEVAL_TO_TIMESPEC(&tv, tsp);
		break;
	case TSP_NSEC:
	default:
		nanotime(tsp);
		break;
	}
}

/*
 * Set vnode attributes to VNOVAL
 */
void
vattr_null(struct vattr *vap)
{
	vap->va_type = VNON;
	vap->va_size = VNOVAL;
	vap->va_bytes = VNOVAL;
	vap->va_mode = VNOVAL;
	vap->va_nlink = VNOVAL;
	vap->va_uid = VNOVAL;
	vap->va_gid = VNOVAL;
	vap->va_fsid = VNOVAL;
	vap->va_fileid = VNOVAL;
	vap->va_blocksize = VNOVAL;
	vap->va_rmajor = VNOVAL;
	vap->va_rminor = VNOVAL;
	vap->va_atime.tv_sec = VNOVAL;
	vap->va_atime.tv_nsec = VNOVAL;
	vap->va_mtime.tv_sec = VNOVAL;
	vap->va_mtime.tv_nsec = VNOVAL;
	vap->va_ctime.tv_sec = VNOVAL;
	vap->va_ctime.tv_nsec = VNOVAL;
	vap->va_flags = VNOVAL;
	vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;
	/* va_*_uuid fields are only valid if related flags are set */
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 *
 * vp must be locked.
 */
static int vinvalbuf_bp(struct buf *bp, void *data);

struct vinvalbuf_bp_info {
	struct vnode *vp;
	int slptimeo;
	int lkflags;
	int flags;
	int clean;
};

int
vinvalbuf(struct vnode *vp, int flags, int slpflag, int slptimeo)
{
	struct vinvalbuf_bp_info info;
	vm_object_t object;
	int error;

	lwkt_gettoken(&vp->v_token);

	/*
	 * If we are being asked to save, call fsync to ensure that the inode
	 * is updated.
	 */
	if (flags & V_SAVE) {
		error = bio_track_wait(&vp->v_track_write, slpflag, slptimeo);
		if (error)
			goto done;
		if (!RB_EMPTY(&vp->v_rbdirty_tree)) {
			if ((error = VOP_FSYNC(vp, MNT_WAIT, 0)) != 0)
				goto done;
#if 0
			/*
			 * Dirty bufs may be left or generated via races
			 * in circumstances where vinvalbuf() is called on
			 * a vnode not undergoing reclamation.   Only
			 * panic if we are trying to reclaim the vnode.
			 */
			if ((vp->v_flag & VRECLAIMED) &&
			    (bio_track_active(&vp->v_track_write) ||
			    !RB_EMPTY(&vp->v_rbdirty_tree))) {
				panic("vinvalbuf: dirty bufs");
			}
#endif
		}
  	}
	info.slptimeo = slptimeo;
	info.lkflags = LK_EXCLUSIVE | LK_SLEEPFAIL;
	if (slpflag & PCATCH)
		info.lkflags |= LK_PCATCH;
	info.flags = flags;
	info.vp = vp;

	/*
	 * Flush the buffer cache until nothing is left, wait for all I/O
	 * to complete.  At least one pass is required.  We might block
	 * in the pip code so we have to re-check.  Order is important.
	 */
	do {
		/*
		 * Flush buffer cache
		 */
		if (!RB_EMPTY(&vp->v_rbclean_tree)) {
			info.clean = 1;
			error = RB_SCAN(buf_rb_tree, &vp->v_rbclean_tree,
					NULL, vinvalbuf_bp, &info);
		}
		if (!RB_EMPTY(&vp->v_rbdirty_tree)) {
			info.clean = 0;
			error = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
					NULL, vinvalbuf_bp, &info);
		}

		/*
		 * Wait for I/O completion.
		 */
		bio_track_wait(&vp->v_track_write, 0, 0);
		if ((object = vp->v_object) != NULL)
			refcount_wait(&object->paging_in_progress, "vnvlbx");
	} while (bio_track_active(&vp->v_track_write) ||
		 !RB_EMPTY(&vp->v_rbclean_tree) ||
		 !RB_EMPTY(&vp->v_rbdirty_tree));

	/*
	 * Destroy the copy in the VM cache, too.
	 */
	if ((object = vp->v_object) != NULL) {
		vm_object_page_remove(object, 0, 0,
			(flags & V_SAVE) ? TRUE : FALSE);
	}

	if (!RB_EMPTY(&vp->v_rbdirty_tree) || !RB_EMPTY(&vp->v_rbclean_tree))
		panic("vinvalbuf: flush failed");
	if (!RB_EMPTY(&vp->v_rbhash_tree))
		panic("vinvalbuf: flush failed, buffers still present");
	error = 0;
done:
	lwkt_reltoken(&vp->v_token);
	return (error);
}

static int
vinvalbuf_bp(struct buf *bp, void *data)
{
	struct vinvalbuf_bp_info *info = data;
	int error;

	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		atomic_add_int(&bp->b_refs, 1);
		error = BUF_TIMELOCK(bp, info->lkflags,
				     "vinvalbuf", info->slptimeo);
		atomic_subtract_int(&bp->b_refs, 1);
		if (error == 0) {
			BUF_UNLOCK(bp);
			error = ENOLCK;
		}
		if (error == ENOLCK)
			return(0);
		return (-error);
	}
	KKASSERT(bp->b_vp == info->vp);

	/*
	 * Must check clean/dirty status after successfully locking as
	 * it may race.
	 */
	if ((info->clean && (bp->b_flags & B_DELWRI)) ||
	    (info->clean == 0 && (bp->b_flags & B_DELWRI) == 0)) {
		BUF_UNLOCK(bp);
		return(0);
	}

	/*
	 * NOTE:  NO B_LOCKED CHECK.  Also no buf_checkwrite()
	 * check.  This code will write out the buffer, period.
	 */
	bremfree(bp);
	if (((bp->b_flags & (B_DELWRI | B_INVAL)) == B_DELWRI) &&
	    (info->flags & V_SAVE)) {
		cluster_awrite(bp);
	} else if (info->flags & V_SAVE) {
		/*
		 * Cannot set B_NOCACHE on a clean buffer as this will
		 * destroy the VM backing store which might actually
		 * be dirty (and unsynchronized).
		 */
		bp->b_flags |= (B_INVAL | B_RELBUF);
		brelse(bp);
	} else {
		bp->b_flags |= (B_INVAL | B_NOCACHE | B_RELBUF);
		brelse(bp);
	}
	return(0);
}

/*
 * Truncate a file's buffer and pages to a specified length.  This
 * is in lieu of the old vinvalbuf mechanism, which performed unneeded
 * sync activity.
 *
 * The vnode must be locked.
 */
static int vtruncbuf_bp_trunc_cmp(struct buf *bp, void *data);
static int vtruncbuf_bp_trunc(struct buf *bp, void *data);
static int vtruncbuf_bp_metasync_cmp(struct buf *bp, void *data);
static int vtruncbuf_bp_metasync(struct buf *bp, void *data);

struct vtruncbuf_info {
	struct vnode *vp;
	off_t	truncloffset;
	int	clean;
};

int
vtruncbuf(struct vnode *vp, off_t length, int blksize)
{
	struct vtruncbuf_info info;
	const char *filename;
	int count;

	/*
	 * Round up to the *next* block, then destroy the buffers in question.  
	 * Since we are only removing some of the buffers we must rely on the
	 * scan count to determine whether a loop is necessary.
	 */
	if ((count = (int)(length % blksize)) != 0)
		info.truncloffset = length + (blksize - count);
	else
		info.truncloffset = length;
	info.vp = vp;

	lwkt_gettoken(&vp->v_token);
	do {
		info.clean = 1;
		count = RB_SCAN(buf_rb_tree, &vp->v_rbclean_tree, 
				vtruncbuf_bp_trunc_cmp,
				vtruncbuf_bp_trunc, &info);
		info.clean = 0;
		count += RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
				vtruncbuf_bp_trunc_cmp,
				vtruncbuf_bp_trunc, &info);
	} while(count);

	/*
	 * For safety, fsync any remaining metadata if the file is not being
	 * truncated to 0.  Since the metadata does not represent the entire
	 * dirty list we have to rely on the hit count to ensure that we get
	 * all of it.
	 */
	if (length > 0) {
		do {
			count = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
					vtruncbuf_bp_metasync_cmp,
					vtruncbuf_bp_metasync, &info);
		} while (count);
	}

	/*
	 * Clean out any left over VM backing store.
	 *
	 * It is possible to have in-progress I/O from buffers that were
	 * not part of the truncation.  This should not happen if we
	 * are truncating to 0-length.
	 */
	vnode_pager_setsize(vp, length);
	bio_track_wait(&vp->v_track_write, 0, 0);

	/*
	 * Debugging only
	 */
	spin_lock(&vp->v_spin);
	filename = TAILQ_FIRST(&vp->v_namecache) ?
		   TAILQ_FIRST(&vp->v_namecache)->nc_name : "?";
	spin_unlock(&vp->v_spin);

	/*
	 * Make sure no buffers were instantiated while we were trying
	 * to clean out the remaining VM pages.  This could occur due
	 * to busy dirty VM pages being flushed out to disk.
	 */
	do {
		info.clean = 1;
		count = RB_SCAN(buf_rb_tree, &vp->v_rbclean_tree, 
				vtruncbuf_bp_trunc_cmp,
				vtruncbuf_bp_trunc, &info);
		info.clean = 0;
		count += RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
				vtruncbuf_bp_trunc_cmp,
				vtruncbuf_bp_trunc, &info);
		if (count) {
			kprintf("Warning: vtruncbuf():  Had to re-clean %d "
			       "left over buffers in %s\n", count, filename);
		}
	} while(count);

	lwkt_reltoken(&vp->v_token);

	return (0);
}

/*
 * The callback buffer is beyond the new file EOF and must be destroyed.
 * Note that the compare function must conform to the RB_SCAN's requirements.
 */
static
int
vtruncbuf_bp_trunc_cmp(struct buf *bp, void *data)
{
	struct vtruncbuf_info *info = data;

	if (bp->b_loffset >= info->truncloffset)
		return(0);
	return(-1);
}

static 
int 
vtruncbuf_bp_trunc(struct buf *bp, void *data)
{
	struct vtruncbuf_info *info = data;

	/*
	 * Do not try to use a buffer we cannot immediately lock, but sleep
	 * anyway to prevent a livelock.  The code will loop until all buffers
	 * can be acted upon.
	 *
	 * We must always revalidate the buffer after locking it to deal
	 * with MP races.
	 */
	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		atomic_add_int(&bp->b_refs, 1);
		if (BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL) == 0)
			BUF_UNLOCK(bp);
		atomic_subtract_int(&bp->b_refs, 1);
	} else if ((info->clean && (bp->b_flags & B_DELWRI)) ||
		   (info->clean == 0 && (bp->b_flags & B_DELWRI) == 0) ||
		   bp->b_vp != info->vp ||
		   vtruncbuf_bp_trunc_cmp(bp, data)) {
		BUF_UNLOCK(bp);
	} else {
		bremfree(bp);
		bp->b_flags |= (B_INVAL | B_RELBUF | B_NOCACHE);
		brelse(bp);
	}
	return(1);
}

/*
 * Fsync all meta-data after truncating a file to be non-zero.  Only metadata
 * blocks (with a negative loffset) are scanned.
 * Note that the compare function must conform to the RB_SCAN's requirements.
 */
static int
vtruncbuf_bp_metasync_cmp(struct buf *bp, void *data __unused)
{
	if (bp->b_loffset < 0)
		return(0);
	return(1);
}

static int
vtruncbuf_bp_metasync(struct buf *bp, void *data)
{
	struct vtruncbuf_info *info = data;

	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		atomic_add_int(&bp->b_refs, 1);
		if (BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL) == 0)
			BUF_UNLOCK(bp);
		atomic_subtract_int(&bp->b_refs, 1);
	} else if ((bp->b_flags & B_DELWRI) == 0 ||
		   bp->b_vp != info->vp ||
		   vtruncbuf_bp_metasync_cmp(bp, data)) {
		BUF_UNLOCK(bp);
	} else {
		bremfree(bp);
		if (bp->b_vp == info->vp)
			bawrite(bp);
		else
			bwrite(bp);
	}
	return(1);
}

/*
 * vfsync - implements a multipass fsync on a file which understands
 * dependancies and meta-data.  The passed vnode must be locked.  The 
 * waitfor argument may be MNT_WAIT or MNT_NOWAIT, or MNT_LAZY.
 *
 * When fsyncing data asynchronously just do one consolidated pass starting
 * with the most negative block number.  This may not get all the data due
 * to dependancies.
 *
 * When fsyncing data synchronously do a data pass, then a metadata pass,
 * then do additional data+metadata passes to try to get all the data out.
 */
static int vfsync_wait_output(struct vnode *vp, 
			    int (*waitoutput)(struct vnode *, struct thread *));
static int vfsync_dummy_cmp(struct buf *bp __unused, void *data __unused);
static int vfsync_data_only_cmp(struct buf *bp, void *data);
static int vfsync_meta_only_cmp(struct buf *bp, void *data);
static int vfsync_lazy_range_cmp(struct buf *bp, void *data);
static int vfsync_bp(struct buf *bp, void *data);

struct vfsync_info {
	struct vnode *vp;
	int synchronous;
	int syncdeps;
	int lazycount;
	int lazylimit;
	int skippedbufs;
	int (*checkdef)(struct buf *);
	int (*cmpfunc)(struct buf *, void *);
};

int
vfsync(struct vnode *vp, int waitfor, int passes,
	int (*checkdef)(struct buf *),
	int (*waitoutput)(struct vnode *, struct thread *))
{
	struct vfsync_info info;
	int error;

	bzero(&info, sizeof(info));
	info.vp = vp;
	if ((info.checkdef = checkdef) == NULL)
		info.syncdeps = 1;

	lwkt_gettoken(&vp->v_token);

	switch(waitfor) {
	case MNT_LAZY | MNT_NOWAIT:
	case MNT_LAZY:
		/*
		 * Lazy (filesystem syncer typ) Asynchronous plus limit the
		 * number of data (not meta) pages we try to flush to 1MB.
		 * A non-zero return means that lazy limit was reached.
		 */
		info.lazylimit = 1024 * 1024;
		info.syncdeps = 1;
		info.cmpfunc = vfsync_lazy_range_cmp;
		error = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, 
				vfsync_lazy_range_cmp, vfsync_bp, &info);
		info.cmpfunc = vfsync_meta_only_cmp;
		RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, 
			vfsync_meta_only_cmp, vfsync_bp, &info);
		if (error == 0)
			vp->v_lazyw = 0;
		else if (!RB_EMPTY(&vp->v_rbdirty_tree))
			vn_syncer_add(vp, 1);
		error = 0;
		break;
	case MNT_NOWAIT:
		/*
		 * Asynchronous.  Do a data-only pass and a meta-only pass.
		 */
		info.syncdeps = 1;
		info.cmpfunc = vfsync_data_only_cmp;
		RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, vfsync_data_only_cmp, 
			vfsync_bp, &info);
		info.cmpfunc = vfsync_meta_only_cmp;
		RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, vfsync_meta_only_cmp, 
			vfsync_bp, &info);
		error = 0;
		break;
	default:
		/*
		 * Synchronous.  Do a data-only pass, then a meta-data+data
		 * pass, then additional integrated passes to try to get
		 * all the dependancies flushed.
		 */
		info.cmpfunc = vfsync_data_only_cmp;
		RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, vfsync_data_only_cmp,
			vfsync_bp, &info);
		error = vfsync_wait_output(vp, waitoutput);
		if (error == 0) {
			info.skippedbufs = 0;
			info.cmpfunc = vfsync_dummy_cmp;
			RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL,
				vfsync_bp, &info);
			error = vfsync_wait_output(vp, waitoutput);
			if (info.skippedbufs) {
				kprintf("Warning: vfsync skipped %d dirty "
					"bufs in pass2!\n", info.skippedbufs);
			}
		}
		while (error == 0 && passes > 0 &&
		       !RB_EMPTY(&vp->v_rbdirty_tree)
		) {
			if (--passes == 0) {
				info.synchronous = 1;
				info.syncdeps = 1;
			}
			info.cmpfunc = vfsync_dummy_cmp;
			error = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL,
					vfsync_bp, &info);
			if (error < 0)
				error = -error;
			info.syncdeps = 1;
			if (error == 0)
				error = vfsync_wait_output(vp, waitoutput);
		}
		break;
	}
	lwkt_reltoken(&vp->v_token);
	return(error);
}

static int
vfsync_wait_output(struct vnode *vp,
		   int (*waitoutput)(struct vnode *, struct thread *))
{
	int error;

	error = bio_track_wait(&vp->v_track_write, 0, 0);
	if (waitoutput)
		error = waitoutput(vp, curthread);
	return(error);
}

static int
vfsync_dummy_cmp(struct buf *bp __unused, void *data __unused)
{
	return(0);
}

static int
vfsync_data_only_cmp(struct buf *bp, void *data)
{
	if (bp->b_loffset < 0)
		return(-1);
	return(0);
}

static int
vfsync_meta_only_cmp(struct buf *bp, void *data)
{
	if (bp->b_loffset < 0)
		return(0);
	return(1);
}

static int
vfsync_lazy_range_cmp(struct buf *bp, void *data)
{
	struct vfsync_info *info = data;

	if (bp->b_loffset < info->vp->v_lazyw)
		return(-1);
	return(0);
}

static int
vfsync_bp(struct buf *bp, void *data)
{
	struct vfsync_info *info = data;
	struct vnode *vp = info->vp;
	int error;

	/*
	 * Ignore buffers that we cannot immediately lock.
	 */
	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		++info->skippedbufs;
		return(0);
	}

	/*
	 * We must revalidate the buffer after locking.
	 */
	if ((bp->b_flags & B_DELWRI) == 0 ||
	    bp->b_vp != info->vp ||
	    info->cmpfunc(bp, data)) {
		BUF_UNLOCK(bp);
		return(0);
	}

	/*
	 * If syncdeps is not set we do not try to write buffers which have
	 * dependancies.
	 */
	if (!info->synchronous && info->syncdeps == 0 && info->checkdef(bp)) {
		BUF_UNLOCK(bp);
		return(0);
	}

	/*
	 * B_NEEDCOMMIT (primarily used by NFS) is a state where the buffer
	 * has been written but an additional handshake with the device
	 * is required before we can dispose of the buffer.  We have no idea
	 * how to do this so we have to skip these buffers.
	 */
	if (bp->b_flags & B_NEEDCOMMIT) {
		BUF_UNLOCK(bp);
		return(0);
	}

	/*
	 * Ask bioops if it is ok to sync.  If not the VFS may have
	 * set B_LOCKED so we have to cycle the buffer.
	 */
	if (LIST_FIRST(&bp->b_dep) != NULL && buf_checkwrite(bp)) {
		bremfree(bp);
		brelse(bp);
		return(0);
	}

	if (info->synchronous) {
		/*
		 * Synchronous flushing.  An error may be returned.
		 */
		bremfree(bp);
		error = bwrite(bp);
	} else { 
		/*
		 * Asynchronous flushing.  A negative return value simply
		 * stops the scan and is not considered an error.  We use
		 * this to support limited MNT_LAZY flushes.
		 */
		vp->v_lazyw = bp->b_loffset;
		bremfree(bp);
		info->lazycount += cluster_awrite(bp);
		waitrunningbufspace();
		vm_wait_nominal();
		if (info->lazylimit && info->lazycount >= info->lazylimit)
			error = 1;
		else
			error = 0;
	}
	return(-error);
}

/*
 * Associate a buffer with a vnode.
 *
 * MPSAFE
 */
int
bgetvp(struct vnode *vp, struct buf *bp, int testsize)
{
	KASSERT(bp->b_vp == NULL, ("bgetvp: not free"));
	KKASSERT((bp->b_flags & (B_HASHED|B_DELWRI|B_VNCLEAN|B_VNDIRTY)) == 0);

	/*
	 * Insert onto list for new vnode.
	 */
	lwkt_gettoken(&vp->v_token);

	if (buf_rb_hash_RB_INSERT(&vp->v_rbhash_tree, bp)) {
		lwkt_reltoken(&vp->v_token);
		return (EEXIST);
	}

	/*
	 * Diagnostics (mainly for HAMMER debugging).  Check for
	 * overlapping buffers.
	 */
	if (check_buf_overlap) {
		struct buf *bx;
		bx = buf_rb_hash_RB_PREV(bp);
		if (bx) {
			if (bx->b_loffset + bx->b_bufsize > bp->b_loffset) {
				kprintf("bgetvp: overlapl %016jx/%d %016jx "
					"bx %p bp %p\n",
					(intmax_t)bx->b_loffset,
					bx->b_bufsize,
					(intmax_t)bp->b_loffset,
					bx, bp);
				if (check_buf_overlap > 1)
					panic("bgetvp - overlapping buffer");
			}
		}
		bx = buf_rb_hash_RB_NEXT(bp);
		if (bx) {
			if (bp->b_loffset + testsize > bx->b_loffset) {
				kprintf("bgetvp: overlapr %016jx/%d %016jx "
					"bp %p bx %p\n",
					(intmax_t)bp->b_loffset,
					testsize,
					(intmax_t)bx->b_loffset,
					bp, bx);
				if (check_buf_overlap > 1)
					panic("bgetvp - overlapping buffer");
			}
		}
	}
	bp->b_vp = vp;
	bp->b_flags |= B_HASHED;
	bp->b_flags |= B_VNCLEAN;
	if (buf_rb_tree_RB_INSERT(&vp->v_rbclean_tree, bp))
		panic("reassignbuf: dup lblk/clean vp %p bp %p", vp, bp);
	/*vhold(vp);*/
	lwkt_reltoken(&vp->v_token);
	return(0);
}

/*
 * Disassociate a buffer from a vnode.
 *
 * MPSAFE
 */
void
brelvp(struct buf *bp)
{
	struct vnode *vp;

	KASSERT(bp->b_vp != NULL, ("brelvp: NULL"));

	/*
	 * Delete from old vnode list, if on one.
	 */
	vp = bp->b_vp;
	lwkt_gettoken(&vp->v_token);
	if (bp->b_flags & (B_VNDIRTY | B_VNCLEAN)) {
		if (bp->b_flags & B_VNDIRTY)
			buf_rb_tree_RB_REMOVE(&vp->v_rbdirty_tree, bp);
		else
			buf_rb_tree_RB_REMOVE(&vp->v_rbclean_tree, bp);
		bp->b_flags &= ~(B_VNDIRTY | B_VNCLEAN);
	}
	if (bp->b_flags & B_HASHED) {
		buf_rb_hash_RB_REMOVE(&vp->v_rbhash_tree, bp);
		bp->b_flags &= ~B_HASHED;
	}

	/*
	 * Only remove from synclist when no dirty buffers are left AND
	 * the VFS has not flagged the vnode's inode as being dirty.
	 */
	if ((vp->v_flag & (VONWORKLST | VISDIRTY | VOBJDIRTY)) == VONWORKLST &&
	    RB_EMPTY(&vp->v_rbdirty_tree)) {
		vn_syncer_remove(vp);
	}
	bp->b_vp = NULL;

	lwkt_reltoken(&vp->v_token);

	/*vdrop(vp);*/
}

/*
 * Reassign the buffer to the proper clean/dirty list based on B_DELWRI.
 * This routine is called when the state of the B_DELWRI bit is changed.
 *
 * Must be called with vp->v_token held.
 * MPSAFE
 */
void
reassignbuf(struct buf *bp)
{
	struct vnode *vp = bp->b_vp;
	int delay;

	ASSERT_LWKT_TOKEN_HELD(&vp->v_token);
	++reassignbufcalls;

	/*
	 * B_PAGING flagged buffers cannot be reassigned because their vp
	 * is not fully linked in.
	 */
	if (bp->b_flags & B_PAGING)
		panic("cannot reassign paging buffer");

	if (bp->b_flags & B_DELWRI) {
		/*
		 * Move to the dirty list, add the vnode to the worklist
		 */
		if (bp->b_flags & B_VNCLEAN) {
			buf_rb_tree_RB_REMOVE(&vp->v_rbclean_tree, bp);
			bp->b_flags &= ~B_VNCLEAN;
		}
		if ((bp->b_flags & B_VNDIRTY) == 0) {
			if (buf_rb_tree_RB_INSERT(&vp->v_rbdirty_tree, bp)) {
				panic("reassignbuf: dup lblk vp %p bp %p",
				      vp, bp);
			}
			bp->b_flags |= B_VNDIRTY;
		}
		if ((vp->v_flag & VONWORKLST) == 0) {
			switch (vp->v_type) {
			case VDIR:
				delay = dirdelay;
				break;
			case VCHR:
			case VBLK:
				if (vp->v_rdev && 
				    vp->v_rdev->si_mountpoint != NULL) {
					delay = metadelay;
					break;
				}
				/* fall through */
			default:
				delay = filedelay;
			}
			vn_syncer_add(vp, delay);
		}
	} else {
		/*
		 * Move to the clean list, remove the vnode from the worklist
		 * if no dirty blocks remain.
		 */
		if (bp->b_flags & B_VNDIRTY) {
			buf_rb_tree_RB_REMOVE(&vp->v_rbdirty_tree, bp);
			bp->b_flags &= ~B_VNDIRTY;
		}
		if ((bp->b_flags & B_VNCLEAN) == 0) {
			if (buf_rb_tree_RB_INSERT(&vp->v_rbclean_tree, bp)) {
				panic("reassignbuf: dup lblk vp %p bp %p",
				      vp, bp);
			}
			bp->b_flags |= B_VNCLEAN;
		}

		/*
		 * Only remove from synclist when no dirty buffers are left
		 * AND the VFS has not flagged the vnode's inode as being
		 * dirty.
		 */
		if ((vp->v_flag & (VONWORKLST | VISDIRTY | VOBJDIRTY)) ==
		     VONWORKLST &&
		    RB_EMPTY(&vp->v_rbdirty_tree)) {
			vn_syncer_remove(vp);
		}
	}
}

/*
 * Create a vnode for a block device.  Used for mounting the root file
 * system.
 *
 * A vref()'d vnode is returned.
 */
extern struct vop_ops *devfs_vnode_dev_vops_p;
int
bdevvp(cdev_t dev, struct vnode **vpp)
{
	struct vnode *vp;
	struct vnode *nvp;
	int error;

	if (dev == NULL) {
		*vpp = NULLVP;
		return (ENXIO);
	}
	error = getspecialvnode(VT_NON, NULL, &devfs_vnode_dev_vops_p,
				&nvp, 0, 0);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	vp = nvp;
	vp->v_type = VCHR;
#if 0
	vp->v_rdev = dev;
#endif
	v_associate_rdev(vp, dev);
	vp->v_umajor = dev->si_umajor;
	vp->v_uminor = dev->si_uminor;
	vx_unlock(vp);
	*vpp = vp;
	return (0);
}

int
v_associate_rdev(struct vnode *vp, cdev_t dev)
{
	if (dev == NULL)
		return(ENXIO);
	if (dev_is_good(dev) == 0)
		return(ENXIO);
	KKASSERT(vp->v_rdev == NULL);
	vp->v_rdev = reference_dev(dev);
	lwkt_gettoken(&spechash_token);
	SLIST_INSERT_HEAD(&dev->si_hlist, vp, v_cdevnext);
	lwkt_reltoken(&spechash_token);
	return(0);
}

void
v_release_rdev(struct vnode *vp)
{
	cdev_t dev;

	if ((dev = vp->v_rdev) != NULL) {
		lwkt_gettoken(&spechash_token);
		SLIST_REMOVE(&dev->si_hlist, vp, vnode, v_cdevnext);
		vp->v_rdev = NULL;
		release_dev(dev);
		lwkt_reltoken(&spechash_token);
	}
}

/*
 * Add a vnode to the alias list hung off the cdev_t.  We only associate
 * the device number with the vnode.  The actual device is not associated
 * until the vnode is opened (usually in spec_open()), and will be 
 * disassociated on last close.
 */
void
addaliasu(struct vnode *nvp, int x, int y)
{
	if (nvp->v_type != VBLK && nvp->v_type != VCHR)
		panic("addaliasu on non-special vnode");
	nvp->v_umajor = x;
	nvp->v_uminor = y;
}

/*
 * Simple call that a filesystem can make to try to get rid of a
 * vnode.  It will fail if anyone is referencing the vnode (including
 * the caller).
 *
 * The filesystem can check whether its in-memory inode structure still
 * references the vp on return.
 *
 * May only be called if the vnode is in a known state (i.e. being prevented
 * from being deallocated by some other condition such as a vfs inode hold).
 */
void
vclean_unlocked(struct vnode *vp)
{
	vx_get(vp);
	if (VREFCNT(vp) <= 0)
		vgone_vxlocked(vp);
	vx_put(vp);
}

/*
 * Disassociate a vnode from its underlying filesystem. 
 *
 * The vnode must be VX locked and referenced.  In all normal situations
 * there are no active references.  If vclean_vxlocked() is called while
 * there are active references, the vnode is being ripped out and we have
 * to call VOP_CLOSE() as appropriate before we can reclaim it.
 */
void
vclean_vxlocked(struct vnode *vp, int flags)
{
	int active;
	int n;
	vm_object_t object;
	struct namecache *ncp;

	/*
	 * If the vnode has already been reclaimed we have nothing to do.
	 */
	if (vp->v_flag & VRECLAIMED)
		return;

	/*
	 * Set flag to interlock operation, flag finalization to ensure
	 * that the vnode winds up on the inactive list, and set v_act to 0.
	 */
	vsetflags(vp, VRECLAIMED);
	atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);
	vp->v_act = 0;

	if (verbose_reclaims) {
		if ((ncp = TAILQ_FIRST(&vp->v_namecache)) != NULL)
			kprintf("Debug: reclaim %p %s\n", vp, ncp->nc_name);
	}

	/*
	 * Scrap the vfs cache
	 */
	while (cache_inval_vp(vp, 0) != 0) {
		kprintf("Warning: vnode %p clean/cache_resolution "
			"race detected\n", vp);
		tsleep(vp, 0, "vclninv", 2);
	}

	/*
	 * Check to see if the vnode is in use. If so we have to reference it
	 * before we clean it out so that its count cannot fall to zero and
	 * generate a race against ourselves to recycle it.
	 */
	active = (VREFCNT(vp) > 0);

	/*
	 * Clean out any buffers associated with the vnode and destroy its
	 * object, if it has one. 
	 */
	vinvalbuf(vp, V_SAVE, 0, 0);
	KKASSERT(lockcountnb(&vp->v_lock) == 1);

	/*
	 * If purging an active vnode (typically during a forced unmount
	 * or reboot), it must be closed and deactivated before being
	 * reclaimed.  This isn't really all that safe, but what can
	 * we do? XXX.
	 *
	 * Note that neither of these routines unlocks the vnode.
	 */
	if (active && (flags & DOCLOSE)) {
		while ((n = vp->v_opencount) != 0) {
			if (vp->v_writecount)
				VOP_CLOSE(vp, FWRITE|FNONBLOCK);
			else
				VOP_CLOSE(vp, FNONBLOCK);
			if (vp->v_opencount == n) {
				kprintf("Warning: unable to force-close"
				       " vnode %p\n", vp);
				break;
			}
		}
	}

	/*
	 * If the vnode has not been deactivated, deactivated it.  Deactivation
	 * can create new buffers and VM pages so we have to call vinvalbuf()
	 * again to make sure they all get flushed.
	 *
	 * This can occur if a file with a link count of 0 needs to be
	 * truncated.
	 *
	 * If the vnode is already dead don't try to deactivate it.
	 */
	if ((vp->v_flag & VINACTIVE) == 0) {
		vsetflags(vp, VINACTIVE);
		if (vp->v_mount)
			VOP_INACTIVE(vp);
		vinvalbuf(vp, V_SAVE, 0, 0);
	}
	KKASSERT(lockcountnb(&vp->v_lock) == 1);

	/*
	 * If the vnode has an object, destroy it.
	 */
	while ((object = vp->v_object) != NULL) {
		vm_object_hold(object);
		if (object == vp->v_object)
			break;
		vm_object_drop(object);
	}

	if (object != NULL) {
		if (object->ref_count == 0) {
			if ((object->flags & OBJ_DEAD) == 0)
				vm_object_terminate(object);
			vm_object_drop(object);
			vclrflags(vp, VOBJBUF);
		} else {
			vm_pager_deallocate(object);
			vclrflags(vp, VOBJBUF);
			vm_object_drop(object);
		}
	}
	KKASSERT((vp->v_flag & VOBJBUF) == 0);

	/*
	 * Reclaim the vnode if not already dead.
	 */
	if (vp->v_mount && VOP_RECLAIM(vp))
		panic("vclean: cannot reclaim");

	/*
	 * Done with purge, notify sleepers of the grim news.
	 */
	vp->v_ops = &dead_vnode_vops_p;
	vn_gone(vp);
	vp->v_tag = VT_NON;

	/*
	 * If we are destroying an active vnode, reactivate it now that
	 * we have reassociated it with deadfs.  This prevents the system
	 * from crashing on the vnode due to it being unexpectedly marked
	 * as inactive or reclaimed.
	 */
	if (active && (flags & DOCLOSE)) {
		vclrflags(vp, VINACTIVE | VRECLAIMED);
	}
}

/*
 * Eliminate all activity associated with the requested vnode
 * and with all vnodes aliased to the requested vnode.
 *
 * The vnode must be referenced but should not be locked.
 */
int
vrevoke(struct vnode *vp, struct ucred *cred)
{
	struct vnode *vq;
	struct vnode *vqn;
	cdev_t dev;
	int error;

	/*
	 * If the vnode has a device association, scrap all vnodes associated
	 * with the device.  Don't let the device disappear on us while we
	 * are scrapping the vnodes.
	 *
	 * The passed vp will probably show up in the list, do not VX lock
	 * it twice!
	 *
	 * Releasing the vnode's rdev here can mess up specfs's call to
	 * device close, so don't do it.  The vnode has been disassociated
	 * and the device will be closed after the last ref on the related
	 * fp goes away (if not still open by e.g. the kernel).
	 */
	if (vp->v_type != VCHR) {
		error = fdrevoke(vp, DTYPE_VNODE, cred);
		return (error);
	}
	if ((dev = vp->v_rdev) == NULL) {
		return(0);
	}
	reference_dev(dev);
	lwkt_gettoken(&spechash_token);

restart:
	vqn = SLIST_FIRST(&dev->si_hlist);
	if (vqn)
		vhold(vqn);
	while ((vq = vqn) != NULL) {
		if (VREFCNT(vq) > 0) {
			vref(vq);
			fdrevoke(vq, DTYPE_VNODE, cred);
			/*v_release_rdev(vq);*/
			vrele(vq);
			if (vq->v_rdev != dev) {
				vdrop(vq);
				goto restart;
			}
		}
		vqn = SLIST_NEXT(vq, v_cdevnext);
		if (vqn)
			vhold(vqn);
		vdrop(vq);
	}
	lwkt_reltoken(&spechash_token);
	dev_drevoke(dev);
	release_dev(dev);
	return (0);
}

/*
 * This is called when the object underlying a vnode is being destroyed,
 * such as in a remove().  Try to recycle the vnode immediately if the
 * only active reference is our reference.
 *
 * Directory vnodes in the namecache with children cannot be immediately
 * recycled because numerous VOP_N*() ops require them to be stable.
 *
 * To avoid recursive recycling from VOP_INACTIVE implemenetations this
 * function is a NOP if VRECLAIMED is already set.
 */
int
vrecycle(struct vnode *vp)
{
	if (VREFCNT(vp) <= 1 && (vp->v_flag & VRECLAIMED) == 0) {
		if (cache_inval_vp_nonblock(vp))
			return(0);
		vgone_vxlocked(vp);
		return (1);
	}
	return (0);
}

/*
 * Return the maximum I/O size allowed for strategy calls on VP.
 *
 * If vp is VCHR or VBLK we dive the device, otherwise we use
 * the vp's mount info.
 *
 * The returned value is clamped at MAXPHYS as most callers cannot use
 * buffers larger than that size.
 */
int
vmaxiosize(struct vnode *vp)
{
	int maxiosize;

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		maxiosize = vp->v_rdev->si_iosize_max;
	else
		maxiosize = vp->v_mount->mnt_iosize_max;

	if (maxiosize > MAXPHYS)
		maxiosize = MAXPHYS;
	return (maxiosize);
}

/*
 * Eliminate all activity associated with a vnode in preparation for
 * destruction.
 *
 * The vnode must be VX locked and refd and will remain VX locked and refd
 * on return.  This routine may be called with the vnode in any state, as
 * long as it is VX locked.  The vnode will be cleaned out and marked
 * VRECLAIMED but will not actually be reused until all existing refs and
 * holds go away.
 *
 * NOTE: This routine may be called on a vnode which has not yet been
 * already been deactivated (VOP_INACTIVE), or on a vnode which has
 * already been reclaimed.
 *
 * This routine is not responsible for placing us back on the freelist. 
 * Instead, it happens automatically when the caller releases the VX lock
 * (assuming there aren't any other references).
 */
void
vgone_vxlocked(struct vnode *vp)
{
	/*
	 * assert that the VX lock is held.  This is an absolute requirement
	 * now for vgone_vxlocked() to be called.
	 */
	KKASSERT(lockcountnb(&vp->v_lock) == 1);

	/*
	 * Clean out the filesystem specific data and set the VRECLAIMED
	 * bit.  Also deactivate the vnode if necessary. 
	 */
	vclean_vxlocked(vp, DOCLOSE);

	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if (vp->v_mount != NULL) {
		KKASSERT(vp->v_data == NULL);
		insmntque(vp, NULL);
	}

	/*
	 * If special device, remove it from special device alias list
	 * if it is on one.  This should normally only occur if a vnode is
	 * being revoked as the device should otherwise have been released
	 * naturally.
	 */
	if ((vp->v_type == VBLK || vp->v_type == VCHR) && vp->v_rdev != NULL) {
		v_release_rdev(vp);
	}

	/*
	 * Set us to VBAD
	 */
	vp->v_type = VBAD;
}

/*
 * Lookup a vnode by device number.
 *
 * Returns non-zero and *vpp set to a vref'd vnode on success.
 * Returns zero on failure.
 */
int
vfinddev(cdev_t dev, enum vtype type, struct vnode **vpp)
{
	struct vnode *vp;

	lwkt_gettoken(&spechash_token);
	SLIST_FOREACH(vp, &dev->si_hlist, v_cdevnext) {
		if (type == vp->v_type) {
			*vpp = vp;
			vref(vp);
			lwkt_reltoken(&spechash_token);
			return (1);
		}
	}
	lwkt_reltoken(&spechash_token);
	return (0);
}

/*
 * Calculate the total number of references to a special device.  This
 * routine may only be called for VBLK and VCHR vnodes since v_rdev is
 * an overloaded field.  Since udev2dev can now return NULL, we have
 * to check for a NULL v_rdev.
 */
int
count_dev(cdev_t dev)
{
	struct vnode *vp;
	int count = 0;

	if (SLIST_FIRST(&dev->si_hlist)) {
		lwkt_gettoken(&spechash_token);
		SLIST_FOREACH(vp, &dev->si_hlist, v_cdevnext) {
			count += vp->v_opencount;
		}
		lwkt_reltoken(&spechash_token);
	}
	return(count);
}

int
vcount(struct vnode *vp)
{
	if (vp->v_rdev == NULL)
		return(0);
	return(count_dev(vp->v_rdev));
}

/*
 * Initialize VMIO for a vnode.  This routine MUST be called before a
 * VFS can issue buffer cache ops on a vnode.  It is typically called
 * when a vnode is initialized from its inode.
 */
int
vinitvmio(struct vnode *vp, off_t filesize, int blksize, int boff)
{
	vm_object_t object;
	int error = 0;

	object = vp->v_object;
	if (object) {
		vm_object_hold(object);
		KKASSERT(vp->v_object == object);
	}

	if (object == NULL) {
		object = vnode_pager_alloc(vp, filesize, 0, 0, blksize, boff);

		/*
		 * Dereference the reference we just created.  This assumes
		 * that the object is associated with the vp.  Allow it to
		 * have zero refs.  It cannot be destroyed as long as it
		 * is associated with the vnode.
		 */
		vm_object_hold(object);
		atomic_add_int(&object->ref_count, -1);
		vrele(vp);
	} else {
		KKASSERT((object->flags & OBJ_DEAD) == 0);
	}
	KASSERT(vp->v_object != NULL, ("vinitvmio: NULL object"));
	vsetflags(vp, VOBJBUF);
	vm_object_drop(object);

	return (error);
}


/*
 * Print out a description of a vnode.
 */
static char *typename[] =
{"VNON", "VREG", "VDIR", "VBLK", "VCHR", "VLNK", "VSOCK", "VFIFO", "VBAD"};

void
vprint(char *label, struct vnode *vp)
{
	char buf[96];

	if (label != NULL)
		kprintf("%s: %p: ", label, (void *)vp);
	else
		kprintf("%p: ", (void *)vp);
	kprintf("type %s, refcnt %08x, writecount %d, holdcnt %d,",
		typename[vp->v_type],
		vp->v_refcnt, vp->v_writecount, vp->v_auxrefs);
	buf[0] = '\0';
	if (vp->v_flag & VROOT)
		strcat(buf, "|VROOT");
	if (vp->v_flag & VPFSROOT)
		strcat(buf, "|VPFSROOT");
	if (vp->v_flag & VTEXT)
		strcat(buf, "|VTEXT");
	if (vp->v_flag & VSYSTEM)
		strcat(buf, "|VSYSTEM");
	if (vp->v_flag & VOBJBUF)
		strcat(buf, "|VOBJBUF");
	if (buf[0] != '\0')
		kprintf(" flags (%s)", &buf[1]);
	if (vp->v_data == NULL) {
		kprintf("\n");
	} else {
		kprintf("\n\t");
		VOP_PRINT(vp);
	}
}

/*
 * Do the usual access checking.
 * file_mode, uid and gid are from the vnode in question,
 * while acc_mode and cred are from the VOP_ACCESS parameter list
 */
int
vaccess(enum vtype type, mode_t file_mode, uid_t uid, gid_t gid,
    mode_t acc_mode, struct ucred *cred)
{
	mode_t mask;
	int ismember;

	/*
	 * Super-user always gets read/write access, but execute access depends
	 * on at least one execute bit being set.
	 */
	if (priv_check_cred(cred, PRIV_ROOT, 0) == 0) {
		if ((acc_mode & VEXEC) && type != VDIR &&
		    (file_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0)
			return (EACCES);
		return (0);
	}

	mask = 0;

	/* Otherwise, check the owner. */
	if (cred->cr_uid == uid) {
		if (acc_mode & VEXEC)
			mask |= S_IXUSR;
		if (acc_mode & VREAD)
			mask |= S_IRUSR;
		if (acc_mode & VWRITE)
			mask |= S_IWUSR;
		return ((file_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check the groups. */
	ismember = groupmember(gid, cred);
	if (cred->cr_svgid == gid || ismember) {
		if (acc_mode & VEXEC)
			mask |= S_IXGRP;
		if (acc_mode & VREAD)
			mask |= S_IRGRP;
		if (acc_mode & VWRITE)
			mask |= S_IWGRP;
		return ((file_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check everyone else. */
	if (acc_mode & VEXEC)
		mask |= S_IXOTH;
	if (acc_mode & VREAD)
		mask |= S_IROTH;
	if (acc_mode & VWRITE)
		mask |= S_IWOTH;
	return ((file_mode & mask) == mask ? 0 : EACCES);
}

#ifdef DDB
#include <ddb/ddb.h>

static int db_show_locked_vnodes(struct mount *mp, void *data);

/*
 * List all of the locked vnodes in the system.
 * Called when debugging the kernel.
 */
DB_SHOW_COMMAND(lockedvnodes, lockedvnodes)
{
	kprintf("Locked vnodes\n");
	mountlist_scan(db_show_locked_vnodes, NULL, 
			MNTSCAN_FORWARD|MNTSCAN_NOBUSY);
}

static int
db_show_locked_vnodes(struct mount *mp, void *data __unused)
{
	struct vnode *vp;

	TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
		if (vn_islocked(vp))
			vprint(NULL, vp);
	}
	return(0);
}
#endif

/*
 * Top level filesystem related information gathering.
 */
static int	sysctl_ovfs_conf (SYSCTL_HANDLER_ARGS);

static int
vfs_sysctl(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *)arg1 - 1;	/* XXX */
	u_int namelen = arg2 + 1;	/* XXX */
	struct vfsconf *vfsp;
	int maxtypenum;

#if 1 || defined(COMPAT_PRELITE2)
	/* Resolve ambiguity between VFS_VFSCONF and VFS_GENERIC. */
	if (namelen == 1)
		return (sysctl_ovfs_conf(oidp, arg1, arg2, req));
#endif

#ifdef notyet
	/* all sysctl names at this level are at least name and field */
	if (namelen < 2)
		return (ENOTDIR);		/* overloaded */
	if (name[0] != VFS_GENERIC) {
		vfsp = vfsconf_find_by_typenum(name[0]);
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return ((*vfsp->vfc_vfsops->vfs_sysctl)(&name[1], namelen - 1,
		    oldp, oldlenp, newp, newlen, p));
	}
#endif
	switch (name[1]) {
	case VFS_MAXTYPENUM:
		if (namelen != 2)
			return (ENOTDIR);
		maxtypenum = vfsconf_get_maxtypenum();
		return (SYSCTL_OUT(req, &maxtypenum, sizeof(maxtypenum)));
	case VFS_CONF:
		if (namelen != 3)
			return (ENOTDIR);	/* overloaded */
		vfsp = vfsconf_find_by_typenum(name[2]);
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return (SYSCTL_OUT(req, vfsp, sizeof *vfsp));
	}
	return (EOPNOTSUPP);
}

SYSCTL_NODE(_vfs, VFS_GENERIC, generic, CTLFLAG_RD, vfs_sysctl,
	"Generic filesystem");

#if 1 || defined(COMPAT_PRELITE2)

static int
sysctl_ovfs_conf_iter(struct vfsconf *vfsp, void *data)
{
	int error;
	struct ovfsconf ovfs;
	struct sysctl_req *req = (struct sysctl_req*) data;

	bzero(&ovfs, sizeof(ovfs));
	ovfs.vfc_vfsops = vfsp->vfc_vfsops;	/* XXX used as flag */
	strcpy(ovfs.vfc_name, vfsp->vfc_name);
	ovfs.vfc_index = vfsp->vfc_typenum;
	ovfs.vfc_refcount = vfsp->vfc_refcount;
	ovfs.vfc_flags = vfsp->vfc_flags;
	error = SYSCTL_OUT(req, &ovfs, sizeof ovfs);
	if (error)
		return error; /* abort iteration with error code */
	else
		return 0; /* continue iterating with next element */
}

static int
sysctl_ovfs_conf(SYSCTL_HANDLER_ARGS)
{
	return vfsconf_each(sysctl_ovfs_conf_iter, (void*)req);
}

#endif /* 1 || COMPAT_PRELITE2 */

/*
 * Check to see if a filesystem is mounted on a block device.
 */
int
vfs_mountedon(struct vnode *vp)
{
	cdev_t dev;

	if ((dev = vp->v_rdev) == NULL) {
/*		if (vp->v_type != VBLK)
			dev = get_dev(vp->v_uminor, vp->v_umajor); */
	}
	if (dev != NULL && dev->si_mountpoint)
		return (EBUSY);
	return (0);
}

/*
 * Unmount all filesystems. The list is traversed in reverse order
 * of mounting to avoid dependencies.
 */

static int vfs_umountall_callback(struct mount *mp, void *data);

void
vfs_unmountall(void)
{
	int count;

	do {
		count = mountlist_scan(vfs_umountall_callback, 
					NULL, MNTSCAN_REVERSE|MNTSCAN_NOBUSY);
	} while (count);
}

static
int
vfs_umountall_callback(struct mount *mp, void *data)
{
	int error;

	error = dounmount(mp, MNT_FORCE);
	if (error) {
		mountlist_remove(mp);
		kprintf("unmount of filesystem mounted from %s failed (", 
			mp->mnt_stat.f_mntfromname);
		if (error == EBUSY)
			kprintf("BUSY)\n");
		else
			kprintf("%d)\n", error);
	}
	return(1);
}

/*
 * Checks the mount flags for parameter mp and put the names comma-separated
 * into a string buffer buf with a size limit specified by len.
 *
 * It returns the number of bytes written into buf, and (*errorp) will be
 * set to 0, EINVAL (if passed length is 0), or ENOSPC (supplied buffer was
 * not large enough).  The buffer will be 0-terminated if len was not 0.
 */
size_t
vfs_flagstostr(int flags, const struct mountctl_opt *optp,
	       char *buf, size_t len, int *errorp)
{
	static const struct mountctl_opt optnames[] = {
		{ MNT_ASYNC,            "asynchronous" },
		{ MNT_EXPORTED,         "NFS exported" },
		{ MNT_LOCAL,            "local" },
		{ MNT_NOATIME,          "noatime" },
		{ MNT_NODEV,            "nodev" },
		{ MNT_NOEXEC,           "noexec" },
		{ MNT_NOSUID,           "nosuid" },
		{ MNT_NOSYMFOLLOW,      "nosymfollow" },
		{ MNT_QUOTA,            "with-quotas" },
		{ MNT_RDONLY,           "read-only" },
		{ MNT_SYNCHRONOUS,      "synchronous" },
		{ MNT_UNION,            "union" },
		{ MNT_NOCLUSTERR,       "noclusterr" },
		{ MNT_NOCLUSTERW,       "noclusterw" },
		{ MNT_SUIDDIR,          "suiddir" },
		{ MNT_SOFTDEP,          "soft-updates" },
		{ MNT_IGNORE,           "ignore" },
		{ 0,			NULL}
	};
	int bwritten;
	int bleft;
	int optlen;
	int actsize;

	*errorp = 0;
	bwritten = 0;
	bleft = len - 1;	/* leave room for trailing \0 */

	/*
	 * Checks the size of the string. If it contains
	 * any data, then we will append the new flags to
	 * it.
	 */
	actsize = strlen(buf);
	if (actsize > 0)
		buf += actsize;

	/* Default flags if no flags passed */
	if (optp == NULL)
		optp = optnames;

	if (bleft < 0) {	/* degenerate case, 0-length buffer */
		*errorp = EINVAL;
		return(0);
	}

	for (; flags && optp->o_opt; ++optp) {
		if ((flags & optp->o_opt) == 0)
			continue;
		optlen = strlen(optp->o_name);
		if (bwritten || actsize > 0) {
			if (bleft < 2) {
				*errorp = ENOSPC;
				break;
			}
			buf[bwritten++] = ',';
			buf[bwritten++] = ' ';
			bleft -= 2;
		}
		if (bleft < optlen) {
			*errorp = ENOSPC;
			break;
		}
		bcopy(optp->o_name, buf + bwritten, optlen);
		bwritten += optlen;
		bleft -= optlen;
		flags &= ~optp->o_opt;
	}

	/*
	 * Space already reserved for trailing \0
	 */
	buf[bwritten] = 0;
	return (bwritten);
}

/*
 * Build hash lists of net addresses and hang them off the mount point.
 * Called by ufs_mount() to set up the lists of export addresses.
 */
static int
vfs_hang_addrlist(struct mount *mp, struct netexport *nep,
		const struct export_args *argp)
{
	struct netcred *np;
	struct radix_node_head *rnh;
	int i;
	struct radix_node *rn;
	struct sockaddr *saddr, *smask = NULL;
	struct domain *dom;
	int error;

	if (argp->ex_addrlen == 0) {
		if (mp->mnt_flag & MNT_DEFEXPORTED)
			return (EPERM);
		np = &nep->ne_defexported;
		np->netc_exflags = argp->ex_flags;
		np->netc_anon = argp->ex_anon;
		np->netc_anon.cr_ref = 1;
		mp->mnt_flag |= MNT_DEFEXPORTED;
		return (0);
	}

	if (argp->ex_addrlen < 0 || argp->ex_addrlen > MLEN)
		return (EINVAL);
	if (argp->ex_masklen < 0 || argp->ex_masklen > MLEN)
		return (EINVAL);

	i = sizeof(struct netcred) + argp->ex_addrlen + argp->ex_masklen;
	np = (struct netcred *) kmalloc(i, M_NETADDR, M_WAITOK | M_ZERO);
	saddr = (struct sockaddr *) (np + 1);
	if ((error = copyin(argp->ex_addr, (caddr_t) saddr, argp->ex_addrlen)))
		goto out;
	if (saddr->sa_len > argp->ex_addrlen)
		saddr->sa_len = argp->ex_addrlen;
	if (argp->ex_masklen) {
		smask = (struct sockaddr *)((caddr_t)saddr + argp->ex_addrlen);
		error = copyin(argp->ex_mask, (caddr_t)smask, argp->ex_masklen);
		if (error)
			goto out;
		if (smask->sa_len > argp->ex_masklen)
			smask->sa_len = argp->ex_masklen;
	}
	i = saddr->sa_family;
	if ((rnh = nep->ne_rtable[i]) == NULL) {
		/*
		 * Seems silly to initialize every AF when most are not used,
		 * do so on demand here
		 */
		SLIST_FOREACH(dom, &domains, dom_next)
			if (dom->dom_family == i && dom->dom_rtattach) {
				dom->dom_rtattach((void **) &nep->ne_rtable[i],
				    dom->dom_rtoffset);
				break;
			}
		if ((rnh = nep->ne_rtable[i]) == NULL) {
			error = ENOBUFS;
			goto out;
		}
	}
	rn = (*rnh->rnh_addaddr) ((char *) saddr, (char *) smask, rnh,
	    np->netc_rnodes);
	if (rn == NULL || np != (struct netcred *) rn) {	/* already exists */
		error = EPERM;
		goto out;
	}
	np->netc_exflags = argp->ex_flags;
	np->netc_anon = argp->ex_anon;
	np->netc_anon.cr_ref = 1;
	return (0);
out:
	kfree(np, M_NETADDR);
	return (error);
}

/* ARGSUSED */
static int
vfs_free_netcred(struct radix_node *rn, void *w)
{
	struct radix_node_head *rnh = (struct radix_node_head *) w;

	(*rnh->rnh_deladdr) (rn->rn_key, rn->rn_mask, rnh);
	kfree((caddr_t) rn, M_NETADDR);
	return (0);
}

/*
 * Free the net address hash lists that are hanging off the mount points.
 */
static void
vfs_free_addrlist(struct netexport *nep)
{
	int i;
	struct radix_node_head *rnh;

	for (i = 0; i <= AF_MAX; i++)
		if ((rnh = nep->ne_rtable[i])) {
			(*rnh->rnh_walktree) (rnh, vfs_free_netcred,
			    (caddr_t) rnh);
			kfree((caddr_t) rnh, M_RTABLE);
			nep->ne_rtable[i] = 0;
		}
}

int
vfs_export(struct mount *mp, struct netexport *nep,
	   const struct export_args *argp)
{
	int error;

	if (argp->ex_flags & MNT_DELEXPORT) {
		if (mp->mnt_flag & MNT_EXPUBLIC) {
			vfs_setpublicfs(NULL, NULL, NULL);
			mp->mnt_flag &= ~MNT_EXPUBLIC;
		}
		vfs_free_addrlist(nep);
		mp->mnt_flag &= ~(MNT_EXPORTED | MNT_DEFEXPORTED);
	}
	if (argp->ex_flags & MNT_EXPORTED) {
		if (argp->ex_flags & MNT_EXPUBLIC) {
			if ((error = vfs_setpublicfs(mp, nep, argp)) != 0)
				return (error);
			mp->mnt_flag |= MNT_EXPUBLIC;
		}
		if ((error = vfs_hang_addrlist(mp, nep, argp)))
			return (error);
		mp->mnt_flag |= MNT_EXPORTED;
	}
	return (0);
}


/*
 * Set the publicly exported filesystem (WebNFS). Currently, only
 * one public filesystem is possible in the spec (RFC 2054 and 2055)
 */
int
vfs_setpublicfs(struct mount *mp, struct netexport *nep,
		const struct export_args *argp)
{
	int error;
	struct vnode *rvp;
	char *cp;

	/*
	 * mp == NULL -> invalidate the current info, the FS is
	 * no longer exported. May be called from either vfs_export
	 * or unmount, so check if it hasn't already been done.
	 */
	if (mp == NULL) {
		if (nfs_pub.np_valid) {
			nfs_pub.np_valid = 0;
			if (nfs_pub.np_index != NULL) {
				kfree(nfs_pub.np_index, M_TEMP);
				nfs_pub.np_index = NULL;
			}
		}
		return (0);
	}

	/*
	 * Only one allowed at a time.
	 */
	if (nfs_pub.np_valid != 0 && mp != nfs_pub.np_mount)
		return (EBUSY);

	/*
	 * Get real filehandle for root of exported FS.
	 */
	bzero((caddr_t)&nfs_pub.np_handle, sizeof(nfs_pub.np_handle));
	nfs_pub.np_handle.fh_fsid = mp->mnt_stat.f_fsid;

	if ((error = VFS_ROOT(mp, &rvp)))
		return (error);

	if ((error = VFS_VPTOFH(rvp, &nfs_pub.np_handle.fh_fid)))
		return (error);

	vput(rvp);

	/*
	 * If an indexfile was specified, pull it in.
	 */
	if (argp->ex_indexfile != NULL) {
		int namelen;

		error = vn_get_namelen(rvp, &namelen);
		if (error)
			return (error);
		nfs_pub.np_index = kmalloc(namelen, M_TEMP, M_WAITOK);
		error = copyinstr(argp->ex_indexfile, nfs_pub.np_index,
		    namelen, NULL);
		if (!error) {
			/*
			 * Check for illegal filenames.
			 */
			for (cp = nfs_pub.np_index; *cp; cp++) {
				if (*cp == '/') {
					error = EINVAL;
					break;
				}
			}
		}
		if (error) {
			kfree(nfs_pub.np_index, M_TEMP);
			return (error);
		}
	}

	nfs_pub.np_mount = mp;
	nfs_pub.np_valid = 1;
	return (0);
}

struct netcred *
vfs_export_lookup(struct mount *mp, struct netexport *nep,
		struct sockaddr *nam)
{
	struct netcred *np;
	struct radix_node_head *rnh;
	struct sockaddr *saddr;

	np = NULL;
	if (mp->mnt_flag & MNT_EXPORTED) {
		/*
		 * Lookup in the export list first.
		 */
		if (nam != NULL) {
			saddr = nam;
			rnh = nep->ne_rtable[saddr->sa_family];
			if (rnh != NULL) {
				np = (struct netcred *)
					(*rnh->rnh_matchaddr)((char *)saddr,
							      rnh);
				if (np && np->netc_rnodes->rn_flags & RNF_ROOT)
					np = NULL;
			}
		}
		/*
		 * If no address match, use the default if it exists.
		 */
		if (np == NULL && mp->mnt_flag & MNT_DEFEXPORTED)
			np = &nep->ne_defexported;
	}
	return (np);
}

/*
 * perform msync on all vnodes under a mount point.  The mount point must
 * be locked.  This code is also responsible for lazy-freeing unreferenced
 * vnodes whos VM objects no longer contain pages.
 *
 * NOTE: MNT_WAIT still skips vnodes in the VXLOCK state.
 *
 * NOTE: XXX VOP_PUTPAGES and friends requires that the vnode be locked,
 * but vnode_pager_putpages() doesn't lock the vnode.  We have to do it
 * way up in this high level function.
 */
static int vfs_msync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int vfs_msync_scan2(struct mount *mp, struct vnode *vp, void *data);

void
vfs_msync(struct mount *mp, int flags) 
{
	int vmsc_flags;

	/*
	 * tmpfs sets this flag to prevent msync(), sync, and the
	 * filesystem periodic syncer from trying to flush VM pages
	 * to swap.  Only pure memory pressure flushes tmpfs VM pages
	 * to swap.
	 */
	if (mp->mnt_kern_flag & MNTK_NOMSYNC)
		return;

	/*
	 * Ok, scan the vnodes for work.  If the filesystem is using the
	 * syncer thread feature we can use vsyncscan() instead of
	 * vmntvnodescan(), which is much faster.
	 */
	vmsc_flags = VMSC_GETVP;
	if (flags != MNT_WAIT)
		vmsc_flags |= VMSC_NOWAIT;

	if (mp->mnt_kern_flag & MNTK_THR_SYNC) {
		vsyncscan(mp, vmsc_flags, vfs_msync_scan2,
			  (void *)(intptr_t)flags);
	} else {
		vmntvnodescan(mp, vmsc_flags,
			      vfs_msync_scan1, vfs_msync_scan2,
			      (void *)(intptr_t)flags);
	}
}

/*
 * scan1 is a fast pre-check.  There could be hundreds of thousands of
 * vnodes, we cannot afford to do anything heavy weight until we have a
 * fairly good indication that there is work to do.
 */
static
int
vfs_msync_scan1(struct mount *mp, struct vnode *vp, void *data)
{
	int flags = (int)(intptr_t)data;

	if ((vp->v_flag & VRECLAIMED) == 0) {
		if (vp->v_auxrefs == 0 && VREFCNT(vp) <= 0 &&
		    vp->v_object) {
			return(0);	/* call scan2 */
		}
		if ((mp->mnt_flag & MNT_RDONLY) == 0 &&
		    (vp->v_flag & VOBJDIRTY) &&
		    (flags == MNT_WAIT || vn_islocked(vp) == 0)) {
			return(0);	/* call scan2 */
		}
	}

	/*
	 * do not call scan2, continue the loop
	 */
	return(-1);
}

/*
 * This callback is handed a locked vnode.
 */
static
int
vfs_msync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	vm_object_t obj;
	int flags = (int)(intptr_t)data;

	if (vp->v_flag & VRECLAIMED)
		return(0);

	if ((mp->mnt_flag & MNT_RDONLY) == 0 && (vp->v_flag & VOBJDIRTY)) {
		if ((obj = vp->v_object) != NULL) {
			vm_object_page_clean(obj, 0, 0, 
			 flags == MNT_WAIT ? OBJPC_SYNC : OBJPC_NOSYNC);
		}
	}
	return(0);
}

/*
 * Wake up anyone interested in vp because it is being revoked.
 */
void
vn_gone(struct vnode *vp)
{
	lwkt_gettoken(&vp->v_token);
	KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, NOTE_REVOKE);
	lwkt_reltoken(&vp->v_token);
}

/*
 * extract the cdev_t from a VBLK or VCHR.  The vnode must have been opened
 * (or v_rdev might be NULL).
 */
cdev_t
vn_todev(struct vnode *vp)
{
	if (vp->v_type != VBLK && vp->v_type != VCHR)
		return (NULL);
	KKASSERT(vp->v_rdev != NULL);
	return (vp->v_rdev);
}

/*
 * Check if vnode represents a disk device.  The vnode does not need to be
 * opened.
 *
 * MPALMOSTSAFE
 */
int
vn_isdisk(struct vnode *vp, int *errp)
{
	cdev_t dev;

	if (vp->v_type != VCHR) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}

	dev = vp->v_rdev;

	if (dev == NULL) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
	if (dev_is_good(dev) == 0) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
	if ((dev_dflags(dev) & D_DISK) == 0) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
	if (errp != NULL)
		*errp = 0;
	return (1);
}

int
vn_get_namelen(struct vnode *vp, int *namelen)
{
	int error;
	register_t retval[2];

	error = VOP_PATHCONF(vp, _PC_NAME_MAX, retval);
	if (error)
		return (error);
	*namelen = (int)retval[0];
	return (0);
}

int
vop_write_dirent(int *error, struct uio *uio, ino_t d_ino, uint8_t d_type, 
		uint16_t d_namlen, const char *d_name)
{
	struct dirent *dp;
	size_t len;

	len = _DIRENT_RECLEN(d_namlen);
	if (len > uio->uio_resid)
		return(1);

	dp = kmalloc(len, M_TEMP, M_WAITOK | M_ZERO);

	dp->d_ino = d_ino;
	dp->d_namlen = d_namlen;
	dp->d_type = d_type;
	bcopy(d_name, dp->d_name, d_namlen);

	*error = uiomove((caddr_t)dp, len, uio);

	kfree(dp, M_TEMP);

	return(0);
}

void
vn_mark_atime(struct vnode *vp, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct ucred *cred = p ? p->p_ucred : proc0.p_ucred;

	if ((vp->v_mount->mnt_flag & (MNT_NOATIME | MNT_RDONLY)) == 0) {
		VOP_MARKATIME(vp, cred);
	}
}
