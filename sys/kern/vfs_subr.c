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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/kern/vfs_subr.c,v 1.43 2004/10/12 19:20:46 dillon Exp $
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
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
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

static MALLOC_DEFINE(M_NETADDR, "Export Host", "Export host address structure");

int numvnodes;
SYSCTL_INT(_debug, OID_AUTO, numvnodes, CTLFLAG_RD, &numvnodes, 0, "");

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int vttoif_tab[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT,
};

static int reassignbufcalls;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufcalls, CTLFLAG_RW,
		&reassignbufcalls, 0, "");
static int reassignbufloops;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufloops, CTLFLAG_RW,
		&reassignbufloops, 0, "");
static int reassignbufsortgood;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufsortgood, CTLFLAG_RW,
		&reassignbufsortgood, 0, "");
static int reassignbufsortbad;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufsortbad, CTLFLAG_RW,
		&reassignbufsortbad, 0, "");
static int reassignbufmethod = 1;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufmethod, CTLFLAG_RW,
		&reassignbufmethod, 0, "");

#ifdef ENABLE_VFS_IOOPT
int vfs_ioopt = 0;
SYSCTL_INT(_vfs, OID_AUTO, ioopt, CTLFLAG_RW, &vfs_ioopt, 0, "");
#endif

int	nfs_mount_type = -1;
static struct lwkt_token spechash_token;
struct nfs_public nfs_pub;	/* publicly exported FS */

int desiredvnodes;
SYSCTL_INT(_kern, KERN_MAXVNODES, maxvnodes, CTLFLAG_RW, 
		&desiredvnodes, 0, "Maximum number of vnodes");

static void	vfs_free_addrlist (struct netexport *nep);
static int	vfs_free_netcred (struct radix_node *rn, void *w);
static int	vfs_hang_addrlist (struct mount *mp, struct netexport *nep,
				       struct export_args *argp);

extern int dev_ref_debug;
extern struct vnodeopv_entry_desc spec_vnodeop_entries[];

/*
 * Return 0 if the vnode is already on the free list or cannot be placed
 * on the free list.  Return 1 if the vnode can be placed on the free list.
 */
static __inline int
vshouldfree(struct vnode *vp, int usecount)
{
	if (vp->v_flag & VFREE)
		return (0);		/* already free */
	if (vp->v_holdcnt != 0 || vp->v_usecount != usecount)
		return (0);		/* other holderse */
	if (vp->v_object &&
	    (vp->v_object->ref_count || vp->v_object->resident_page_count)) {
		return (0);
	}
	return (1);
}

/*
 * Initialize the vnode management data structures. 
 *
 * Called from vfsinit()
 */
void
vfs_subr_init(void)
{
	/*
	 * Desired vnodes is a result of the physical page count
	 * and the size of kernel's heap.  It scales in proportion
	 * to the amount of available physical memory.  This can
	 * cause trouble on 64-bit and large memory platforms.
	 */
	/* desiredvnodes = maxproc + vmstats.v_page_count / 4; */
	desiredvnodes =
		min(maxproc + vmstats.v_page_count /4,
		    2 * (VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS) /
		    (5 * (sizeof(struct vm_object) + sizeof(struct vnode))));

	lwkt_token_init(&spechash_token);
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
		&timestamp_precision, 0, "");

/*
 * Get a current timestamp.
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
	vap->va_rdev = VNOVAL;
	vap->va_atime.tv_sec = VNOVAL;
	vap->va_atime.tv_nsec = VNOVAL;
	vap->va_mtime.tv_sec = VNOVAL;
	vap->va_mtime.tv_nsec = VNOVAL;
	vap->va_ctime.tv_sec = VNOVAL;
	vap->va_ctime.tv_nsec = VNOVAL;
	vap->va_flags = VNOVAL;
	vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;
}

/*
 * Update outstanding I/O count and do wakeup if requested.
 */
void
vwakeup(struct buf *bp)
{
	struct vnode *vp;

	bp->b_flags &= ~B_WRITEINPROG;
	if ((vp = bp->b_vp)) {
		vp->v_numoutput--;
		if (vp->v_numoutput < 0)
			panic("vwakeup: neg numoutput");
		if ((vp->v_numoutput == 0) && (vp->v_flag & VBWAIT)) {
			vp->v_flag &= ~VBWAIT;
			wakeup((caddr_t) &vp->v_numoutput);
		}
	}
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 *
 * vp must be locked.
 */
int
vinvalbuf(struct vnode *vp, int flags, struct thread *td,
	int slpflag, int slptimeo)
{
	struct buf *bp;
	struct buf *nbp, *blist;
	int s, error;
	vm_object_t object;

	if (flags & V_SAVE) {
		s = splbio();
		while (vp->v_numoutput) {
			vp->v_flag |= VBWAIT;
			error = tsleep((caddr_t)&vp->v_numoutput,
			    slpflag, "vinvlbuf", slptimeo);
			if (error) {
				splx(s);
				return (error);
			}
		}
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
			splx(s);
			if ((error = VOP_FSYNC(vp, MNT_WAIT, td)) != 0)
				return (error);
			s = splbio();
			if (vp->v_numoutput > 0 ||
			    !TAILQ_EMPTY(&vp->v_dirtyblkhd))
				panic("vinvalbuf: dirty bufs");
		}
		splx(s);
  	}
	s = splbio();
	for (;;) {
		blist = TAILQ_FIRST(&vp->v_cleanblkhd);
		if (!blist)
			blist = TAILQ_FIRST(&vp->v_dirtyblkhd);
		if (!blist)
			break;

		for (bp = blist; bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
				error = BUF_TIMELOCK(bp,
				    LK_EXCLUSIVE | LK_SLEEPFAIL,
				    "vinvalbuf", slpflag, slptimeo);
				if (error == ENOLCK)
					break;
				splx(s);
				return (error);
			}
			/*
			 * XXX Since there are no node locks for NFS, I
			 * believe there is a slight chance that a delayed
			 * write will occur while sleeping just above, so
			 * check for it.  Note that vfs_bio_awrite expects
			 * buffers to reside on a queue, while VOP_BWRITE and
			 * brelse do not.
			 */
			if (((bp->b_flags & (B_DELWRI | B_INVAL)) == B_DELWRI) &&
				(flags & V_SAVE)) {

				if (bp->b_vp == vp) {
					if (bp->b_flags & B_CLUSTEROK) {
						BUF_UNLOCK(bp);
						vfs_bio_awrite(bp);
					} else {
						bremfree(bp);
						bp->b_flags |= B_ASYNC;
						VOP_BWRITE(bp->b_vp, bp);
					}
				} else {
					bremfree(bp);
					(void) VOP_BWRITE(bp->b_vp, bp);
				}
				break;
			}
			bremfree(bp);
			bp->b_flags |= (B_INVAL | B_NOCACHE | B_RELBUF);
			bp->b_flags &= ~B_ASYNC;
			brelse(bp);
		}
	}

	/*
	 * Wait for I/O to complete.  XXX needs cleaning up.  The vnode can
	 * have write I/O in-progress but if there is a VM object then the
	 * VM object can also have read-I/O in-progress.
	 */
	do {
		while (vp->v_numoutput > 0) {
			vp->v_flag |= VBWAIT;
			tsleep(&vp->v_numoutput, 0, "vnvlbv", 0);
		}
		if (VOP_GETVOBJECT(vp, &object) == 0) {
			while (object->paging_in_progress)
				vm_object_pip_sleep(object, "vnvlbx");
		}
	} while (vp->v_numoutput > 0);

	splx(s);

	/*
	 * Destroy the copy in the VM cache, too.
	 */
	if (VOP_GETVOBJECT(vp, &object) == 0) {
		vm_object_page_remove(object, 0, 0,
			(flags & V_SAVE) ? TRUE : FALSE);
	}

	if (!TAILQ_EMPTY(&vp->v_dirtyblkhd) || !TAILQ_EMPTY(&vp->v_cleanblkhd))
		panic("vinvalbuf: flush failed");
	return (0);
}

/*
 * Truncate a file's buffer and pages to a specified length.  This
 * is in lieu of the old vinvalbuf mechanism, which performed unneeded
 * sync activity.
 *
 * The vnode must be locked.
 */
int
vtruncbuf(struct vnode *vp, struct thread *td, off_t length, int blksize)
{
	struct buf *bp;
	struct buf *nbp;
	int s, anyfreed;
	int trunclbn;

	/*
	 * Round up to the *next* lbn.
	 */
	trunclbn = (length + blksize - 1) / blksize;

	s = splbio();
restart:
	anyfreed = 1;
	for (;anyfreed;) {
		anyfreed = 0;
		for (bp = TAILQ_FIRST(&vp->v_cleanblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (bp->b_lblkno >= trunclbn) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					bp->b_flags |= (B_INVAL | B_RELBUF);
					bp->b_flags &= ~B_ASYNC;
					brelse(bp);
					anyfreed = 1;
				}
				if (nbp &&
				    (((nbp->b_xflags & BX_VNCLEAN) == 0) ||
				    (nbp->b_vp != vp) ||
				    (nbp->b_flags & B_DELWRI))) {
					goto restart;
				}
			}
		}

		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (bp->b_lblkno >= trunclbn) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					bp->b_flags |= (B_INVAL | B_RELBUF);
					bp->b_flags &= ~B_ASYNC;
					brelse(bp);
					anyfreed = 1;
				}
				if (nbp &&
				    (((nbp->b_xflags & BX_VNDIRTY) == 0) ||
				    (nbp->b_vp != vp) ||
				    (nbp->b_flags & B_DELWRI) == 0)) {
					goto restart;
				}
			}
		}
	}

	if (length > 0) {
restartsync:
		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if ((bp->b_flags & B_DELWRI) && (bp->b_lblkno < 0)) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					if (bp->b_vp == vp) {
						bp->b_flags |= B_ASYNC;
					} else {
						bp->b_flags &= ~B_ASYNC;
					}
					VOP_BWRITE(bp->b_vp, bp);
				}
				goto restartsync;
			}

		}
	}

	while (vp->v_numoutput > 0) {
		vp->v_flag |= VBWAIT;
		tsleep(&vp->v_numoutput, 0, "vbtrunc", 0);
	}

	splx(s);

	vnode_pager_setsize(vp, length);

	return (0);
}

/*
 * Associate a buffer with a vnode.
 */
void
bgetvp(struct vnode *vp, struct buf *bp)
{
	KASSERT(bp->b_vp == NULL, ("bgetvp: not free"));

	vhold(vp);
	bp->b_vp = vp;
	bp->b_dev = vn_todev(vp);
	/*
	 * Insert onto list for new vnode.
	 */
	crit_enter();
	bp->b_xflags |= BX_VNCLEAN;
	bp->b_xflags &= ~BX_VNDIRTY;
	TAILQ_INSERT_TAIL(&vp->v_cleanblkhd, bp, b_vnbufs);
	crit_exit();
}

/*
 * Disassociate a buffer from a vnode.
 */
void
brelvp(struct buf *bp)
{
	struct vnode *vp;
	struct buflists *listheadp;

	KASSERT(bp->b_vp != NULL, ("brelvp: NULL"));

	/*
	 * Delete from old vnode list, if on one.
	 */
	vp = bp->b_vp;
	crit_enter();
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) {
		if (bp->b_xflags & BX_VNDIRTY)
			listheadp = &vp->v_dirtyblkhd;
		else 
			listheadp = &vp->v_cleanblkhd;
		TAILQ_REMOVE(listheadp, bp, b_vnbufs);
		bp->b_xflags &= ~(BX_VNDIRTY | BX_VNCLEAN);
	}
	if ((vp->v_flag & VONWORKLST) && TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
		vp->v_flag &= ~VONWORKLST;
		LIST_REMOVE(vp, v_synclist);
	}
	crit_exit();
	bp->b_vp = NULL;
	vdrop(vp);
}

/*
 * Associate a p-buffer with a vnode.
 *
 * Also sets B_PAGING flag to indicate that vnode is not fully associated
 * with the buffer.  i.e. the bp has not been linked into the vnode or
 * ref-counted.
 */
void
pbgetvp(struct vnode *vp, struct buf *bp)
{
	KASSERT(bp->b_vp == NULL, ("pbgetvp: not free"));

	bp->b_vp = vp;
	bp->b_flags |= B_PAGING;
	bp->b_dev = vn_todev(vp);
}

/*
 * Disassociate a p-buffer from a vnode.
 */
void
pbrelvp(struct buf *bp)
{
	KASSERT(bp->b_vp != NULL, ("pbrelvp: NULL"));

	/* XXX REMOVE ME */
	if (TAILQ_NEXT(bp, b_vnbufs) != NULL) {
		panic(
		    "relpbuf(): b_vp was probably reassignbuf()d %p %x", 
		    bp,
		    (int)bp->b_flags
		);
	}
	bp->b_vp = (struct vnode *) 0;
	bp->b_flags &= ~B_PAGING;
}

void
pbreassignbuf(struct buf *bp, struct vnode *newvp)
{
	if ((bp->b_flags & B_PAGING) == 0) {
		panic(
		    "pbreassignbuf() on non phys bp %p", 
		    bp
		);
	}
	bp->b_vp = newvp;
}

/*
 * Reassign a buffer from one vnode to another.
 * Used to assign file specific control information
 * (indirect blocks) to the vnode to which they belong.
 */
void
reassignbuf(struct buf *bp, struct vnode *newvp)
{
	struct buflists *listheadp;
	int delay;

	if (newvp == NULL) {
		printf("reassignbuf: NULL");
		return;
	}
	++reassignbufcalls;

	/*
	 * B_PAGING flagged buffers cannot be reassigned because their vp
	 * is not fully linked in.
	 */
	if (bp->b_flags & B_PAGING)
		panic("cannot reassign paging buffer");

	crit_enter();
	/*
	 * Delete from old vnode list, if on one.
	 */
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) {
		if (bp->b_xflags & BX_VNDIRTY)
			listheadp = &bp->b_vp->v_dirtyblkhd;
		else 
			listheadp = &bp->b_vp->v_cleanblkhd;
		TAILQ_REMOVE(listheadp, bp, b_vnbufs);
		bp->b_xflags &= ~(BX_VNDIRTY | BX_VNCLEAN);
		if (bp->b_vp != newvp) {
			vdrop(bp->b_vp);
			bp->b_vp = NULL;	/* for clarification */
		}
	}
	/*
	 * If dirty, put on list of dirty buffers; otherwise insert onto list
	 * of clean buffers.
	 */
	if (bp->b_flags & B_DELWRI) {
		struct buf *tbp;

		listheadp = &newvp->v_dirtyblkhd;
		if ((newvp->v_flag & VONWORKLST) == 0) {
			switch (newvp->v_type) {
			case VDIR:
				delay = dirdelay;
				break;
			case VCHR:
			case VBLK:
				if (newvp->v_rdev && 
				    newvp->v_rdev->si_mountpoint != NULL) {
					delay = metadelay;
					break;
				}
				/* fall through */
			default:
				delay = filedelay;
			}
			vn_syncer_add_to_worklist(newvp, delay);
		}
		bp->b_xflags |= BX_VNDIRTY;
		tbp = TAILQ_FIRST(listheadp);
		if (tbp == NULL ||
		    bp->b_lblkno == 0 ||
		    (bp->b_lblkno > 0 && tbp->b_lblkno < 0) ||
		    (bp->b_lblkno > 0 && bp->b_lblkno < tbp->b_lblkno)) {
			TAILQ_INSERT_HEAD(listheadp, bp, b_vnbufs);
			++reassignbufsortgood;
		} else if (bp->b_lblkno < 0) {
			TAILQ_INSERT_TAIL(listheadp, bp, b_vnbufs);
			++reassignbufsortgood;
		} else if (reassignbufmethod == 1) {
			/*
			 * New sorting algorithm, only handle sequential case,
			 * otherwise append to end (but before metadata)
			 */
			if ((tbp = gbincore(newvp, bp->b_lblkno - 1)) != NULL &&
			    (tbp->b_xflags & BX_VNDIRTY)) {
				/*
				 * Found the best place to insert the buffer
				 */
				TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
				++reassignbufsortgood;
			} else {
				/*
				 * Missed, append to end, but before meta-data.
				 * We know that the head buffer in the list is
				 * not meta-data due to prior conditionals.
				 *
				 * Indirect effects:  NFS second stage write
				 * tends to wind up here, giving maximum 
				 * distance between the unstable write and the
				 * commit rpc.
				 */
				tbp = TAILQ_LAST(listheadp, buflists);
				while (tbp && tbp->b_lblkno < 0)
					tbp = TAILQ_PREV(tbp, buflists, b_vnbufs);
				TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
				++reassignbufsortbad;
			}
		} else {
			/*
			 * Old sorting algorithm, scan queue and insert
			 */
			struct buf *ttbp;
			while ((ttbp = TAILQ_NEXT(tbp, b_vnbufs)) &&
			    (ttbp->b_lblkno < bp->b_lblkno)) {
				++reassignbufloops;
				tbp = ttbp;
			}
			TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
		}
	} else {
		bp->b_xflags |= BX_VNCLEAN;
		TAILQ_INSERT_TAIL(&newvp->v_cleanblkhd, bp, b_vnbufs);
		if ((newvp->v_flag & VONWORKLST) &&
		    TAILQ_EMPTY(&newvp->v_dirtyblkhd)) {
			newvp->v_flag &= ~VONWORKLST;
			LIST_REMOVE(newvp, v_synclist);
		}
	}
	if (bp->b_vp != newvp) {
		bp->b_vp = newvp;
		vhold(bp->b_vp);
	}
	crit_exit();
}

/*
 * Create a vnode for a block device.
 * Used for mounting the root file system.
 */
int
bdevvp(dev_t dev, struct vnode **vpp)
{
	struct vnode *vp;
	struct vnode *nvp;
	int error;

	if (dev == NODEV) {
		*vpp = NULLVP;
		return (ENXIO);
	}
	error = getnewvnode(VT_NON, NULL, spec_vnode_vops, &nvp, 0, 0);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	vp = nvp;
	vp->v_type = VCHR;
	vp->v_udev = dev->si_udev;
	vx_unlock(vp);
	*vpp = vp;
	return (0);
}

int
v_associate_rdev(struct vnode *vp, dev_t dev)
{
	lwkt_tokref ilock;

	if (dev == NULL || dev == NODEV)
		return(ENXIO);
	if (dev_is_good(dev) == 0)
		return(ENXIO);
	KKASSERT(vp->v_rdev == NULL);
	if (dev_ref_debug)
		printf("Z1");
	vp->v_rdev = reference_dev(dev);
	lwkt_gettoken(&ilock, &spechash_token);
	SLIST_INSERT_HEAD(&dev->si_hlist, vp, v_specnext);
	lwkt_reltoken(&ilock);
	return(0);
}

void
v_release_rdev(struct vnode *vp)
{
	lwkt_tokref ilock;
	dev_t dev;

	if ((dev = vp->v_rdev) != NULL) {
		lwkt_gettoken(&ilock, &spechash_token);
		SLIST_REMOVE(&dev->si_hlist, vp, vnode, v_specnext);
		if (dev_ref_debug && vp->v_opencount != 0) {
			printf("releasing rdev with non-0 "
				"v_opencount(%d) (revoked?)\n",
				vp->v_opencount);
		}
		vp->v_rdev = NULL;
		vp->v_opencount = 0;
		release_dev(dev);
		lwkt_reltoken(&ilock);
	}
}

/*
 * Add a vnode to the alias list hung off the dev_t.  We only associate
 * the device number with the vnode.  The actual device is not associated
 * until the vnode is opened (usually in spec_open()), and will be 
 * disassociated on last close.
 */
void
addaliasu(struct vnode *nvp, udev_t nvp_udev)
{
	if (nvp->v_type != VBLK && nvp->v_type != VCHR)
		panic("addaliasu on non-special vnode");
	nvp->v_udev = nvp_udev;
}

/*
 * Disassociate a vnode from its underlying filesystem. 
 *
 * The vnode must be VX locked and refd
 *
 * If there are v_usecount references to the vnode other then ours we have
 * to VOP_CLOSE the vnode before we can deactivate and reclaim it.
 */
void
vclean(struct vnode *vp, int flags, struct thread *td)
{
	int active;

	/*
	 * If the vnode has already been reclaimed we have nothing to do.
	 */
	if (vp->v_flag & VRECLAIMED)
		return;
	vp->v_flag |= VRECLAIMED;

	/*
	 * Scrap the vfs cache
	 */
	cache_inval_vp(vp, CINV_SELF);

	/*
	 * Check to see if the vnode is in use. If so we have to reference it
	 * before we clean it out so that its count cannot fall to zero and
	 * generate a race against ourselves to recycle it.
	 */
	active = (vp->v_usecount > 1);

	/*
	 * Clean out any buffers associated with the vnode and destroy its
	 * object, if it has one.
	 */
	vinvalbuf(vp, V_SAVE, td, 0, 0);
	VOP_DESTROYVOBJECT(vp);

	/*
	 * If purging an active vnode, it must be closed and
	 * deactivated before being reclaimed.   XXX
	 *
	 * Note that neither of these routines unlocks the vnode.
	 */
	if (active) {
		if (flags & DOCLOSE)
			VOP_CLOSE(vp, FNONBLOCK, td);
	}

	/*
	 * If the vnode has not be deactivated, deactivated it.
	 */
	if ((vp->v_flag & VINACTIVE) == 0) {
		vp->v_flag |= VINACTIVE;
		VOP_INACTIVE(vp, td);
	}

	/*
	 * Reclaim the vnode.
	 */
	if (VOP_RECLAIM(vp, td))
		panic("vclean: cannot reclaim");

	/*
	 * Done with purge, notify sleepers of the grim news.
	 */
	vp->v_ops = dead_vnode_vops;
	vn_pollgone(vp);
	vp->v_tag = VT_NON;
}

/*
 * Eliminate all activity associated with the requested vnode
 * and with all vnodes aliased to the requested vnode.
 *
 * The vnode must be referenced and vx_lock()'d
 *
 * revoke { struct vnode *a_vp, int a_flags }
 */
int
vop_stdrevoke(struct vop_revoke_args *ap)
{
	struct vnode *vp, *vq;
	lwkt_tokref ilock;
	dev_t dev;

	KASSERT((ap->a_flags & REVOKEALL) != 0, ("vop_revoke"));

	vp = ap->a_vp;

	/*
	 * If the vnode is already dead don't try to revoke it
	 */
	if (vp->v_flag & VRECLAIMED)
		return (0);

	/*
	 * If the vnode has a device association, scrap all vnodes associated
	 * with the device.  Don't let the device disappear on us while we
	 * are scrapping the vnodes.
	 *
	 * The passed vp will probably show up in the list, do not VX lock
	 * it twice!
	 */
	if (vp->v_type != VCHR && vp->v_type != VBLK)
		return(0);
	if ((dev = vp->v_rdev) == NULL) {
		if ((dev = udev2dev(vp->v_udev, vp->v_type == VBLK)) == NODEV)
			return(0);
	}
	reference_dev(dev);
	lwkt_gettoken(&ilock, &spechash_token);
	while ((vq = SLIST_FIRST(&dev->si_hlist)) != NULL) {
		if (vp == vq || vx_get(vq) == 0) {
			if (vq == SLIST_FIRST(&dev->si_hlist))
				vgone(vq);
			if (vp != vq)
				vx_put(vq);
		}
	}
	lwkt_reltoken(&ilock);
	release_dev(dev);
	return (0);
}

/*
 * Recycle an unused vnode to the front of the free list.
 *
 * Returns 1 if we were successfully able to recycle the vnode, 
 * 0 otherwise.
 */
int
vrecycle(struct vnode *vp, struct thread *td)
{
	if (vp->v_usecount == 1) {
		vgone(vp);
		return (1);
	}
	return (0);
}

/*
 * Eliminate all activity associated with a vnode in preparation for reuse.
 *
 * The vnode must be VX locked and will remain VX locked on return.  This
 * routine may be called with the vnode in any state, as long as it is
 * VX locked.  The vnode will be cleaned out and marked VRECLAIMED but will
 * not actually be reused until all existing refs and holds go away.
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
vgone(struct vnode *vp)
{
	/*
	 * assert that the VX lock is held.  This is an absolute requirement
	 * now for vgone() to be called.
	 */
	KKASSERT(vp->v_lock.lk_exclusivecount == 1);

	/*
	 * Clean out the filesystem specific data and set the VRECLAIMED
	 * bit.  Also deactivate the vnode if necessary.
	 */
	vclean(vp, DOCLOSE, curthread);

	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if (vp->v_mount != NULL)
		insmntque(vp, NULL);

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
 */
int
vfinddev(dev_t dev, enum vtype type, struct vnode **vpp)
{
	lwkt_tokref ilock;
	struct vnode *vp;

	lwkt_gettoken(&ilock, &spechash_token);
	SLIST_FOREACH(vp, &dev->si_hlist, v_specnext) {
		if (type == vp->v_type) {
			*vpp = vp;
			lwkt_reltoken(&ilock);
			return (1);
		}
	}
	lwkt_reltoken(&ilock);
	return (0);
}

/*
 * Calculate the total number of references to a special device.  This
 * routine may only be called for VBLK and VCHR vnodes since v_rdev is
 * an overloaded field.  Since udev2dev can now return NODEV, we have
 * to check for a NULL v_rdev.
 */
int
count_dev(dev_t dev)
{
	lwkt_tokref ilock;
	struct vnode *vp;
	int count = 0;

	if (SLIST_FIRST(&dev->si_hlist)) {
		lwkt_gettoken(&ilock, &spechash_token);
		SLIST_FOREACH(vp, &dev->si_hlist, v_specnext) {
			count += vp->v_usecount;
		}
		lwkt_reltoken(&ilock);
	}
	return(count);
}

int
count_udev(udev_t udev)
{
	dev_t dev;

	if ((dev = udev2dev(udev, 0)) == NODEV)
		return(0);
	return(count_dev(dev));
}

int
vcount(struct vnode *vp)
{
	if (vp->v_rdev == NULL)
		return(0);
	return(count_dev(vp->v_rdev));
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
		printf("%s: %p: ", label, (void *)vp);
	else
		printf("%p: ", (void *)vp);
	printf("type %s, usecount %d, writecount %d, refcount %d,",
	    typename[vp->v_type], vp->v_usecount, vp->v_writecount,
	    vp->v_holdcnt);
	buf[0] = '\0';
	if (vp->v_flag & VROOT)
		strcat(buf, "|VROOT");
	if (vp->v_flag & VTEXT)
		strcat(buf, "|VTEXT");
	if (vp->v_flag & VSYSTEM)
		strcat(buf, "|VSYSTEM");
	if (vp->v_flag & VBWAIT)
		strcat(buf, "|VBWAIT");
	if (vp->v_flag & VFREE)
		strcat(buf, "|VFREE");
	if (vp->v_flag & VOBJBUF)
		strcat(buf, "|VOBJBUF");
	if (buf[0] != '\0')
		printf(" flags (%s)", &buf[1]);
	if (vp->v_data == NULL) {
		printf("\n");
	} else {
		printf("\n\t");
		VOP_PRINT(vp);
	}
}

#ifdef DDB
#include <ddb/ddb.h>
/*
 * List all of the locked vnodes in the system.
 * Called when debugging the kernel.
 */
DB_SHOW_COMMAND(lockedvnodes, lockedvnodes)
{
	struct thread *td = curthread;	/* XXX */
	lwkt_tokref ilock;
	struct mount *mp, *nmp;
	struct vnode *vp;

	printf("Locked vnodes\n");
	lwkt_gettoken(&ilock, &mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &ilock, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
			if (VOP_ISLOCKED(vp, NULL))
				vprint((char *)0, vp);
		}
		lwkt_gettokref(&ilock);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&ilock);
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
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[0])
				break;
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
		return (SYSCTL_OUT(req, &maxvfsconf, sizeof(int)));
	case VFS_CONF:
		if (namelen != 3)
			return (ENOTDIR);	/* overloaded */
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[2])
				break;
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
sysctl_ovfs_conf(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct vfsconf *vfsp;
	struct ovfsconf ovfs;

	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		ovfs.vfc_vfsops = vfsp->vfc_vfsops;	/* XXX used as flag */
		strcpy(ovfs.vfc_name, vfsp->vfc_name);
		ovfs.vfc_index = vfsp->vfc_typenum;
		ovfs.vfc_refcount = vfsp->vfc_refcount;
		ovfs.vfc_flags = vfsp->vfc_flags;
		error = SYSCTL_OUT(req, &ovfs, sizeof ovfs);
		if (error)
			return error;
	}
	return 0;
}

#endif /* 1 || COMPAT_PRELITE2 */

#if 0
#define KINFO_VNODESLOP	10
/*
 * Dump vnode list (via sysctl).
 * Copyout address of vnode followed by vnode.
 */
/* ARGSUSED */
static int
sysctl_vnode(SYSCTL_HANDLER_ARGS)
{
	struct proc *p = curproc;	/* XXX */
	struct mount *mp, *nmp;
	struct vnode *nvp, *vp;
	lwkt_tokref ilock;
	lwkt_tokref jlock;
	int error;

#define VPTRSZ	sizeof (struct vnode *)
#define VNODESZ	sizeof (struct vnode)

	req->lock = 0;
	if (!req->oldptr) /* Make an estimate */
		return (SYSCTL_OUT(req, 0,
			(numvnodes + KINFO_VNODESLOP) * (VPTRSZ + VNODESZ)));

	lwkt_gettoken(&ilock, &mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &ilock, p)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		lwkt_gettoken(&jlock, &mntvnode_token);
again:
		for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist);
		     vp != NULL;
		     vp = nvp) {
			/*
			 * Check that the vp is still associated with
			 * this filesystem.  RACE: could have been
			 * recycled onto the same filesystem.
			 */
			if (vp->v_mount != mp)
				goto again;
			nvp = TAILQ_NEXT(vp, v_nmntvnodes);
			if ((error = SYSCTL_OUT(req, &vp, VPTRSZ)) ||
			    (error = SYSCTL_OUT(req, vp, VNODESZ))) {
				lwkt_reltoken(&jlock);
				return (error);
			}
		}
		lwkt_reltoken(&jlock);
		lwkt_gettokref(&ilock);
		nmp = TAILQ_NEXT(mp, mnt_list);	/* ZZZ */
		vfs_unbusy(mp, p);
	}
	lwkt_reltoken(&ilock);

	return (0);
}
#endif

/*
 * XXX
 * Exporting the vnode list on large systems causes them to crash.
 * Exporting the vnode list on medium systems causes sysctl to coredump.
 */
#if 0
SYSCTL_PROC(_kern, KERN_VNODE, vnode, CTLTYPE_OPAQUE|CTLFLAG_RD,
	0, 0, sysctl_vnode, "S,vnode", "");
#endif

/*
 * Check to see if a filesystem is mounted on a block device.
 */
int
vfs_mountedon(struct vnode *vp)
{
	dev_t dev;

	if ((dev = vp->v_rdev) == NULL)
		dev = udev2dev(vp->v_udev, (vp->v_type == VBLK));
	if (dev != NODEV && dev->si_mountpoint)
		return (EBUSY);
	return (0);
}

/*
 * Unmount all filesystems. The list is traversed in reverse order
 * of mounting to avoid dependencies.
 */
void
vfs_unmountall(void)
{
	struct mount *mp;
	struct thread *td = curthread;
	int error;

	if (td->td_proc == NULL)
		td = initproc->p_thread;	/* XXX XXX use proc0 instead? */

	/*
	 * Since this only runs when rebooting, it is not interlocked.
	 */
	while(!TAILQ_EMPTY(&mountlist)) {
		mp = TAILQ_LAST(&mountlist, mntlist);
		error = dounmount(mp, MNT_FORCE, td);
		if (error) {
			TAILQ_REMOVE(&mountlist, mp, mnt_list);
			printf("unmount of %s failed (",
			    mp->mnt_stat.f_mntonname);
			if (error == EBUSY)
				printf("BUSY)\n");
			else
				printf("%d)\n", error);
		} else {
			/* The unmount has removed mp from the mountlist */
		}
	}
}

/*
 * Build hash lists of net addresses and hang them off the mount point.
 * Called by ufs_mount() to set up the lists of export addresses.
 */
static int
vfs_hang_addrlist(struct mount *mp, struct netexport *nep,
		struct export_args *argp)
{
	struct netcred *np;
	struct radix_node_head *rnh;
	int i;
	struct radix_node *rn;
	struct sockaddr *saddr, *smask = 0;
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
	np = (struct netcred *) malloc(i, M_NETADDR, M_WAITOK);
	bzero((caddr_t) np, i);
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
	if ((rnh = nep->ne_rtable[i]) == 0) {
		/*
		 * Seems silly to initialize every AF when most are not used,
		 * do so on demand here
		 */
		for (dom = domains; dom; dom = dom->dom_next)
			if (dom->dom_family == i && dom->dom_rtattach) {
				dom->dom_rtattach((void **) &nep->ne_rtable[i],
				    dom->dom_rtoffset);
				break;
			}
		if ((rnh = nep->ne_rtable[i]) == 0) {
			error = ENOBUFS;
			goto out;
		}
	}
	rn = (*rnh->rnh_addaddr) ((caddr_t) saddr, (caddr_t) smask, rnh,
	    np->netc_rnodes);
	if (rn == 0 || np != (struct netcred *) rn) {	/* already exists */
		error = EPERM;
		goto out;
	}
	np->netc_exflags = argp->ex_flags;
	np->netc_anon = argp->ex_anon;
	np->netc_anon.cr_ref = 1;
	return (0);
out:
	free(np, M_NETADDR);
	return (error);
}

/* ARGSUSED */
static int
vfs_free_netcred(struct radix_node *rn, void *w)
{
	struct radix_node_head *rnh = (struct radix_node_head *) w;

	(*rnh->rnh_deladdr) (rn->rn_key, rn->rn_mask, rnh);
	free((caddr_t) rn, M_NETADDR);
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
			free((caddr_t) rnh, M_RTABLE);
			nep->ne_rtable[i] = 0;
		}
}

int
vfs_export(struct mount *mp, struct netexport *nep, struct export_args *argp)
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
		struct export_args *argp)
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
				FREE(nfs_pub.np_index, M_TEMP);
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
		MALLOC(nfs_pub.np_index, char *, MAXNAMLEN + 1, M_TEMP,
		    M_WAITOK);
		error = copyinstr(argp->ex_indexfile, nfs_pub.np_index,
		    MAXNAMLEN, (size_t *)0);
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
			FREE(nfs_pub.np_index, M_TEMP);
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
					(*rnh->rnh_matchaddr)((caddr_t)saddr,
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
 */
static int vfs_msync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int vfs_msync_scan2(struct mount *mp, struct vnode *vp, void *data);

void
vfs_msync(struct mount *mp, int flags) 
{
	vmntvnodescan(mp, VMSC_REFVP, vfs_msync_scan1, vfs_msync_scan2,
			(void *)flags);
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
	int flags = (int)data;

	if ((vp->v_flag & VRECLAIMED) == 0) {
		if (vshouldfree(vp, 0))
			return(0);	/* call scan2 */
		if ((mp->mnt_flag & MNT_RDONLY) == 0 &&
		    (vp->v_flag & VOBJDIRTY) &&
		    (flags == MNT_WAIT || VOP_ISLOCKED(vp, NULL) == 0)) {
			return(0);	/* call scan2 */
		}
	}

	/*
	 * do not call scan2, continue the loop
	 */
	return(-1);
}

static
int
vfs_msync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	vm_object_t obj;
	int flags = (int)data;

	if (vp->v_flag & VRECLAIMED)
		return(0);

	if ((mp->mnt_flag & MNT_RDONLY) == 0 &&
	    (vp->v_flag & VOBJDIRTY) &&
	    (flags == MNT_WAIT || VOP_ISLOCKED(vp, NULL) == 0)) {
		if (VOP_GETVOBJECT(vp, &obj) == 0) {
			vm_object_page_clean(obj, 0, 0, 
			 flags == MNT_WAIT ? OBJPC_SYNC : OBJPC_NOSYNC);
		}
	}
	return(0);
}

/*
 * Create the VM object needed for VMIO and mmap support.  This
 * is done for all VREG files in the system.  Some filesystems might
 * afford the additional metadata buffering capability of the
 * VMIO code by making the device node be VMIO mode also.
 *
 * vp must be locked when vfs_object_create is called.
 */
int
vfs_object_create(struct vnode *vp, struct thread *td)
{
	return (VOP_CREATEVOBJECT(vp, td));
}

/*
 * Record a process's interest in events which might happen to
 * a vnode.  Because poll uses the historic select-style interface
 * internally, this routine serves as both the ``check for any
 * pending events'' and the ``record my interest in future events''
 * functions.  (These are done together, while the lock is held,
 * to avoid race conditions.)
 */
int
vn_pollrecord(struct vnode *vp, struct thread *td, int events)
{
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_revents & events) {
		/*
		 * This leaves events we are not interested
		 * in available for the other process which
		 * which presumably had requested them
		 * (otherwise they would never have been
		 * recorded).
		 */
		events &= vp->v_pollinfo.vpi_revents;
		vp->v_pollinfo.vpi_revents &= ~events;

		lwkt_reltoken(&ilock);
		return events;
	}
	vp->v_pollinfo.vpi_events |= events;
	selrecord(td, &vp->v_pollinfo.vpi_selinfo);
	lwkt_reltoken(&ilock);
	return 0;
}

/*
 * Note the occurrence of an event.  If the VN_POLLEVENT macro is used,
 * it is possible for us to miss an event due to race conditions, but
 * that condition is expected to be rare, so for the moment it is the
 * preferred interface.
 */
void
vn_pollevent(struct vnode *vp, int events)
{
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_events & events) {
		/*
		 * We clear vpi_events so that we don't
		 * call selwakeup() twice if two events are
		 * posted before the polling process(es) is
		 * awakened.  This also ensures that we take at
		 * most one selwakeup() if the polling process
		 * is no longer interested.  However, it does
		 * mean that only one event can be noticed at
		 * a time.  (Perhaps we should only clear those
		 * event bits which we note?) XXX
		 */
		vp->v_pollinfo.vpi_events = 0;	/* &= ~events ??? */
		vp->v_pollinfo.vpi_revents |= events;
		selwakeup(&vp->v_pollinfo.vpi_selinfo);
	}
	lwkt_reltoken(&ilock);
}

/*
 * Wake up anyone polling on vp because it is being revoked.
 * This depends on dead_poll() returning POLLHUP for correct
 * behavior.
 */
void
vn_pollgone(struct vnode *vp)
{
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_events) {
		vp->v_pollinfo.vpi_events = 0;
		selwakeup(&vp->v_pollinfo.vpi_selinfo);
	}
	lwkt_reltoken(&ilock);
}

/*
 * extract the dev_t from a VBLK or VCHR.  The vnode must have been opened
 * (or v_rdev might be NULL).
 */
dev_t
vn_todev(struct vnode *vp)
{
	if (vp->v_type != VBLK && vp->v_type != VCHR)
		return (NODEV);
	KKASSERT(vp->v_rdev != NULL);
	return (vp->v_rdev);
}

/*
 * Check if vnode represents a disk device.  The vnode does not need to be
 * opened.
 */
int
vn_isdisk(struct vnode *vp, int *errp)
{
	dev_t dev;

	if (vp->v_type != VBLK && vp->v_type != VCHR) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}

	if ((dev = vp->v_rdev) == NULL)
		dev = udev2dev(vp->v_udev, (vp->v_type == VBLK));
	if (dev == NULL || dev == NODEV) {
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

void
NDFREE(struct nameidata *ndp, const uint flags)
{
	if (!(flags & NDF_NO_FREE_PNBUF) &&
	    (ndp->ni_cnd.cn_flags & CNP_HASBUF)) {
		zfree(namei_zone, ndp->ni_cnd.cn_pnbuf);
		ndp->ni_cnd.cn_flags &= ~CNP_HASBUF;
	}
	if (!(flags & NDF_NO_DVP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & CNP_LOCKPARENT) &&
	    ndp->ni_dvp != ndp->ni_vp) {
		VOP_UNLOCK(ndp->ni_dvp, 0, ndp->ni_cnd.cn_td);
	}
	if (!(flags & NDF_NO_DVP_RELE) &&
	    (ndp->ni_cnd.cn_flags & (CNP_LOCKPARENT|CNP_WANTPARENT))) {
		vrele(ndp->ni_dvp);
		ndp->ni_dvp = NULL;
	}
	if (!(flags & NDF_NO_VP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & CNP_LOCKLEAF) && ndp->ni_vp) {
		VOP_UNLOCK(ndp->ni_vp, 0, ndp->ni_cnd.cn_td);
	}
	if (!(flags & NDF_NO_VP_RELE) &&
	    ndp->ni_vp) {
		vrele(ndp->ni_vp);
		ndp->ni_vp = NULL;
	}
	if (!(flags & NDF_NO_STARTDIR_RELE) &&
	    (ndp->ni_cnd.cn_flags & CNP_SAVESTART)) {
		vrele(ndp->ni_startdir);
		ndp->ni_startdir = NULL;
	}
}

#ifdef DEBUG_VFS_LOCKS

void
assert_vop_locked(struct vnode *vp, const char *str)
{
	if (vp && IS_LOCKING_VFS(vp) && !VOP_ISLOCKED(vp, NULL)) {
		panic("%s: %p is not locked shared but should be", str, vp);
	}
}

void
assert_vop_unlocked(struct vnode *vp, const char *str)
{
	if (vp && IS_LOCKING_VFS(vp)) {
		if (VOP_ISLOCKED(vp, curthread) == LK_EXCLUSIVE) {
			panic("%s: %p is locked but should not be", str, vp);
		}
	}
}

#endif
