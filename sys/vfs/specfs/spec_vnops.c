/*
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)spec_vnops.c	8.14 (Berkeley) 5/21/95
 * $FreeBSD: src/sys/miscfs/specfs/spec_vnops.c,v 1.131.2.4 2001/02/26 04:23:20 jlemon Exp $
 * $DragonFly: src/sys/vfs/specfs/spec_vnops.c,v 1.19 2004/08/13 17:51:13 dillon Exp $
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/vmmeter.h>
#include <sys/tty.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <sys/buf2.h>

static int	spec_advlock (struct vop_advlock_args *);  
static int	spec_bmap (struct vop_bmap_args *);
static int	spec_close (struct vop_close_args *);
static int	spec_freeblks (struct vop_freeblks_args *);
static int	spec_fsync (struct  vop_fsync_args *);
static int	spec_getpages (struct vop_getpages_args *);
static int	spec_inactive (struct  vop_inactive_args *);
static int	spec_ioctl (struct vop_ioctl_args *);
static int	spec_open (struct vop_open_args *);
static int	spec_poll (struct vop_poll_args *);
static int	spec_kqfilter (struct vop_kqfilter_args *);
static int	spec_print (struct vop_print_args *);
static int	spec_read (struct vop_read_args *);  
static int	spec_strategy (struct vop_strategy_args *);
static int	spec_write (struct vop_write_args *);

struct vop_ops *spec_vnode_vops;
static struct vnodeopv_entry_desc spec_vnodeop_entries[] = {
	{ &vop_default_desc,		vop_defaultop },
	{ &vop_access_desc,		vop_ebadf },
	{ &vop_advlock_desc,		(void *) spec_advlock },
	{ &vop_bmap_desc,		(void *) spec_bmap },
	{ &vop_close_desc,		(void *) spec_close },
	{ &vop_create_desc,		vop_panic },
	{ &vop_freeblks_desc,		(void *) spec_freeblks },
	{ &vop_fsync_desc,		(void *) spec_fsync },
	{ &vop_getpages_desc,		(void *) spec_getpages },
	{ &vop_inactive_desc,		(void *) spec_inactive },
	{ &vop_ioctl_desc,		(void *) spec_ioctl },
	{ &vop_lease_desc,		vop_null },
	{ &vop_link_desc,		vop_panic },
	{ &vop_mkdir_desc,		vop_panic },
	{ &vop_mknod_desc,		vop_panic },
	{ &vop_open_desc,		(void *) spec_open },
	{ &vop_pathconf_desc,		(void *) vop_stdpathconf },
	{ &vop_poll_desc,		(void *) spec_poll },
	{ &vop_kqfilter_desc,		(void *) spec_kqfilter },
	{ &vop_print_desc,		(void *) spec_print },
	{ &vop_read_desc,		(void *) spec_read },
	{ &vop_readdir_desc,		vop_panic },
	{ &vop_readlink_desc,		vop_panic },
	{ &vop_reallocblks_desc,	vop_panic },
	{ &vop_reclaim_desc,		vop_null },
	{ &vop_remove_desc,		vop_panic },
	{ &vop_rename_desc,		vop_panic },
	{ &vop_rmdir_desc,		vop_panic },
	{ &vop_setattr_desc,		vop_ebadf },
	{ &vop_strategy_desc,		(void *) spec_strategy },
	{ &vop_symlink_desc,		vop_panic },
	{ &vop_write_desc,		(void *) spec_write },
	{ NULL, NULL }
};
static struct vnodeopv_desc spec_vnodeop_opv_desc =
	{ &spec_vnode_vops, spec_vnodeop_entries };

VNODEOP_SET(spec_vnodeop_opv_desc);

extern int dev_ref_debug;

/*
 * spec_vnoperate(struct vnodeop_desc *a_desc, ...)
 */
int
spec_vnoperate(struct vop_generic_args *ap)
{
	return (VOCALL(spec_vnode_vops, ap->a_desc->vdesc_offset, ap));
}

static void spec_getpages_iodone (struct buf *bp);

/*
 * Open a special file.
 *
 * spec_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct thread *a_td)
 */
/* ARGSUSED */
static int
spec_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	dev_t dev;
	int error;
	int isblk = (vp->v_type == VBLK) ? 1 : 0;
	const char *cp;

	/*
	 * Don't allow open if fs is mounted -nodev.
	 */
	if (vp->v_mount && (vp->v_mount->mnt_flag & MNT_NODEV))
		return (ENXIO);

	/*
	 * Resolve the device.  If the vnode is already open v_rdev may
	 * already be resolved.  However, if the device changes out from
	 * under us we report it (and, for now, we allow it).  Since
	 * v_release_rdev() zero's v_opencount, we have to save and restore
	 * it when replacing the rdev reference.
	 */
	if (vp->v_rdev != NULL) {
		dev = udev2dev(vp->v_udev, isblk);
		if (dev != vp->v_rdev) {
			int oc = vp->v_opencount;
			printf(
			    "Warning: spec_open: dev %s was lost",
			    vp->v_rdev->si_name);
			v_release_rdev(vp);
			error = v_associate_rdev(vp, 
					udev2dev(vp->v_udev, isblk));
			if (error) {
				printf(", reacquisition failed\n");
			} else {
				vp->v_opencount = oc;
				printf(", reacquisition successful\n");
			}
		} else {
			error = 0;
		}
	} else {
		error = v_associate_rdev(vp, udev2dev(vp->v_udev, isblk));
	}
	if (error)
		return(error);

	/*
	 * Prevent degenerate open/close sequences from nulling out rdev.
	 */
	++vp->v_opencount;
	dev = vp->v_rdev;
	KKASSERT(dev != NULL);

	/*
	 * Make this field valid before any I/O in ->d_open.  XXX the
	 * device itself should probably be required to initialize
	 * this field in d_open.
	 */
	if (!dev->si_iosize_max)
		dev->si_iosize_max = DFLTPHYS;

	/*
	 * XXX: Disks get special billing here, but it is mostly wrong.
	 * XXX: diskpartitions can overlap and the real checks should
	 * XXX: take this into account, and consequently they need to
	 * XXX: live in the diskslicing code.  Some checks do.
	 */
	if (vn_isdisk(vp, NULL) && ap->a_cred != FSCRED && 
	    (ap->a_mode & FWRITE)) {
		/*
		 * Never allow opens for write if the device is mounted R/W
		 */
		if (vp->v_rdev && vp->v_rdev->si_mountpoint &&
		    !(vp->v_rdev->si_mountpoint->mnt_flag & MNT_RDONLY)) {
				error = EBUSY;
				goto done;
		}

		/*
		 * When running in secure mode, do not allow opens
		 * for writing if the device is mounted
		 */
		if (securelevel >= 1 && vfs_mountedon(vp)) {
			error = EPERM;
			goto done;
		}

		/*
		 * When running in very secure mode, do not allow
		 * opens for writing of any devices.
		 */
		if (securelevel >= 2) {
			error = EPERM;
			goto done;
		}
	}

	/* XXX: Special casing of ttys for deadfs.  Probably redundant */
	if (dev_dflags(dev) & D_TTY)
		vp->v_flag |= VISTTY;

	/*
	 * dev_dopen() is always called for each open.  dev_dclose() is
	 * only called for the last close unless D_TRACKCLOSE is set.
	 */
	VOP_UNLOCK(vp, NULL, 0, ap->a_td);
	error = dev_dopen(dev, ap->a_mode, S_IFCHR, ap->a_td);
	vn_lock(vp, NULL, LK_EXCLUSIVE | LK_RETRY, ap->a_td);

	if (error)
		goto done;

	if (dev_dflags(dev) & D_TTY) {
		if (dev->si_tty) {
			struct tty *tp;
			tp = dev->si_tty;
			if (!tp->t_stop) {
				printf("Warning:%s: no t_stop, using nottystop\n", devtoname(dev));
				tp->t_stop = nottystop;
			}
		}
	}

	if (vn_isdisk(vp, NULL)) {
		if (!dev->si_bsize_phys)
			dev->si_bsize_phys = DEV_BSIZE;
	}
	if ((dev_dflags(dev) & D_DISK) == 0) {
		cp = devtoname(dev);
		if (*cp == '#') {
			printf("WARNING: driver %s should register devices with make_dev() (dev_t = \"%s\")\n",
			    dev_dname(dev), cp);
		}
	}
	if (dev_ref_debug)
		printf("spec_open: %s %d\n", dev->si_name, vp->v_opencount);
done:
	if (error) {
		KKASSERT(vp->v_opencount > 0);
		if (--vp->v_opencount == 0)
			v_release_rdev(vp);
	}
	return (error);
}

/*
 * Vnode op for read
 *
 * spec_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
/* ARGSUSED */
static int
spec_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	struct thread *td;
	struct uio *uio;
	dev_t dev;
	int error;

	vp = ap->a_vp;
	dev = vp->v_rdev;
	uio = ap->a_uio;
	td = uio->uio_td;

	if (dev == NULL)		/* device was revoked */
		return (EBADF);
	if (uio->uio_resid == 0)
		return (0);

	VOP_UNLOCK(vp, NULL, 0, td);
	error = dev_dread(dev, uio, ap->a_ioflag);
	vn_lock(vp, NULL, LK_EXCLUSIVE | LK_RETRY, td);
	return (error);
}

/*
 * Vnode op for write
 *
 * spec_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
/* ARGSUSED */
static int
spec_write(struct vop_write_args *ap)
{
	struct vnode *vp;
	struct thread *td;
	struct uio *uio;
	dev_t dev;
	int error;

	vp = ap->a_vp;
	dev = vp->v_rdev;
	uio = ap->a_uio;
	td = uio->uio_td;

	if (dev == NULL)		/* device was revoked */
		return (EBADF);

	VOP_UNLOCK(vp, NULL, 0, td);
	error = dev_dwrite(dev, uio, ap->a_ioflag);
	vn_lock(vp, NULL, LK_EXCLUSIVE | LK_RETRY, td);
	return (error);
}

/*
 * Device ioctl operation.
 *
 * spec_ioctl(struct vnode *a_vp, int a_command, caddr_t a_data,
 *	      int a_fflag, struct ucred *a_cred, struct thread *a_td)
 */
/* ARGSUSED */
static int
spec_ioctl(struct vop_ioctl_args *ap)
{
	dev_t dev;

	if ((dev = ap->a_vp->v_rdev) == NULL)
		return (EBADF);		/* device was revoked */

	return (dev_dioctl(dev, ap->a_command, ap->a_data,
		    ap->a_fflag, ap->a_td));
}

/*
 * spec_poll(struct vnode *a_vp, int a_events, struct ucred *a_cred,
 *	     struct thread *a_td)
 */
/* ARGSUSED */
static int
spec_poll(struct vop_poll_args *ap)
{
	dev_t dev;

	if ((dev = ap->a_vp->v_rdev) == NULL)
		return (EBADF);		/* device was revoked */
	return (dev_dpoll(dev, ap->a_events, ap->a_td));
}

/*
 * spec_kqfilter(struct vnode *a_vp, struct knote *a_kn)
 */
/* ARGSUSED */
static int
spec_kqfilter(struct vop_kqfilter_args *ap)
{
	dev_t dev;

	if ((dev = ap->a_vp->v_rdev) == NULL)
		return (EBADF);		/* device was revoked */
	return (dev_dkqfilter(dev, ap->a_kn));
}

/*
 * Synch buffers associated with a block device
 *
 * spec_fsync(struct vnode *a_vp, struct ucred *a_cred,
 *	      int a_waitfor, struct thread *a_td)
 */
/* ARGSUSED */
static int
spec_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct buf *bp;
	struct buf *nbp;
	int s;
	int maxretry = 10000;	/* large, arbitrarily chosen */

	if (!vn_isdisk(vp, NULL))
		return (0);

loop1:
	/*
	 * MARK/SCAN initialization to avoid infinite loops
	 */
	s = splbio();
	for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp;
	     bp = TAILQ_NEXT(bp, b_vnbufs)) {
		bp->b_flags &= ~B_SCANNED;
	}
	splx(s);

	/*
	 * Flush all dirty buffers associated with a block device.
	 */
loop2:
	s = splbio();
	for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = TAILQ_NEXT(bp, b_vnbufs);
		if ((bp->b_flags & B_SCANNED) != 0)
			continue;
		bp->b_flags |= B_SCANNED;
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("spec_fsync: not dirty");
		if ((vp->v_flag & VOBJBUF) && (bp->b_flags & B_CLUSTEROK)) {
			BUF_UNLOCK(bp);
			vfs_bio_awrite(bp);
			splx(s);
		} else {
			bremfree(bp);
			splx(s);
			bawrite(bp);
		}
		goto loop2;
	}

	/*
	 * If synchronous the caller expects us to completely resolve all
	 * dirty buffers in the system.  Wait for in-progress I/O to
	 * complete (which could include background bitmap writes), then
	 * retry if dirty blocks still exist.
	 */
	if (ap->a_waitfor == MNT_WAIT) {
		while (vp->v_numoutput) {
			vp->v_flag |= VBWAIT;
			(void) tsleep((caddr_t)&vp->v_numoutput, 0, "spfsyn", 0);
		}
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
			if (--maxretry != 0) {
				splx(s);
				goto loop1;
			}
			vprint("spec_fsync: giving up on dirty", vp);
		}
	}
	splx(s);
	return (0);
}

/*
 * spec_inactive(struct vnode *a_vp, struct thread *a_td)
 */
static int
spec_inactive(struct vop_inactive_args *ap)
{
	VOP_UNLOCK(ap->a_vp, NULL, 0, ap->a_td);
	return (0);
}

/*
 * Just call the device strategy routine
 *
 * spec_strategy(struct vnode *a_vp, struct buf *a_bp)
 */
static int
spec_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct vnode *vp;
	struct mount *mp;

	bp = ap->a_bp;
	if (((bp->b_flags & B_READ) == 0) &&
		(LIST_FIRST(&bp->b_dep)) != NULL && bioops.io_start)
		(*bioops.io_start)(bp);

	/*
	 * Collect statistics on synchronous and asynchronous read
	 * and write counts for disks that have associated filesystems.
	 */
	vp = ap->a_vp;
	KKASSERT(vp->v_rdev != NULL);	/* XXX */
	if (vn_isdisk(vp, NULL) && (mp = vp->v_rdev->si_mountpoint) != NULL) {
		if ((bp->b_flags & B_READ) == 0) {
			if (bp->b_lock.lk_lockholder == LK_KERNTHREAD)
				mp->mnt_stat.f_asyncwrites++;
			else
				mp->mnt_stat.f_syncwrites++;
		} else {
			if (bp->b_lock.lk_lockholder == LK_KERNTHREAD)
				mp->mnt_stat.f_asyncreads++;
			else
				mp->mnt_stat.f_syncreads++;
		}
	}
	bp->b_dev = vp->v_rdev;
	BUF_STRATEGY(bp, 0);
	return (0);
}

/*
 * spec_freeblks(struct vnode *a_vp, daddr_t a_addr, daddr_t a_length)
 */
static int
spec_freeblks(struct vop_freeblks_args *ap)
{
	struct buf *bp;

	/*
	 * XXX: This assumes that strategy does the deed right away.
	 * XXX: this may not be TRTTD.
	 */
	KKASSERT(ap->a_vp->v_rdev != NULL);
	if ((dev_dflags(ap->a_vp->v_rdev) & D_CANFREE) == 0)
		return (0);
	bp = geteblk(ap->a_length);
	bp->b_flags |= B_FREEBUF;
	bp->b_dev = ap->a_vp->v_rdev;
	bp->b_blkno = ap->a_addr;
	bp->b_offset = dbtob(ap->a_addr);
	bp->b_bcount = ap->a_length;
	BUF_STRATEGY(bp, 0);
	return (0);
}

/*
 * Implement degenerate case where the block requested is the block
 * returned, and assume that the entire device is contiguous in regards
 * to the contiguous block range (runp and runb).
 *
 * spec_bmap(struct vnode *a_vp, daddr_t a_bn, struct vnode **a_vpp,
 *	     daddr_t *a_bnp, int *a_runp, int *a_runb)
 */
static int
spec_bmap(struct vop_bmap_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int runp = 0;
	int runb = 0;

	if (ap->a_vpp != NULL)
		*ap->a_vpp = vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	if (vp->v_mount != NULL)
		runp = runb = MAXBSIZE / vp->v_mount->mnt_stat.f_iosize;
	if (ap->a_runp != NULL)
		*ap->a_runp = runp;
	if (ap->a_runb != NULL)
		*ap->a_runb = runb;
	return (0);
}

/*
 * Device close routine
 *
 * spec_close(struct vnode *a_vp, int a_fflag, struct ucred *a_cred,
 *	      struct thread *a_td)
 */
/* ARGSUSED */
static int
spec_close(struct vop_close_args *ap)
{
	struct proc *p = ap->a_td->td_proc;
	struct vnode *vp = ap->a_vp;
	dev_t dev = vp->v_rdev;
	int error;

	/*
	 * Hack: a tty device that is a controlling terminal
	 * has a reference from the session structure.
	 * We cannot easily tell that a character device is
	 * a controlling terminal, unless it is the closing
	 * process' controlling terminal.  In that case,
	 * if the reference count is 2 (this last descriptor
	 * plus the session), release the reference from the session.
	 *
	 * It is possible for v_opencount to be 0 or 1 in this case, 0
	 * because the tty might have been revoked.
	 */
	if (dev)
		reference_dev(dev);
	if (vcount(vp) == 2 && vp->v_opencount <= 1 && 
	    p && (vp->v_flag & VXLOCK) == 0 && vp == p->p_session->s_ttyvp) {
		p->p_session->s_ttyvp = NULL;
		vrele(vp);
	}

	/*
	 * Vnodes can be opened and close multiple times.  Do not really
	 * close the device unless (1) it is being closed forcibly,
	 * (2) the device wants to track closes, or (3) this is the last
	 * vnode doing its last close on the device.
	 *
	 * XXX the VXLOCK (force close) case can leave vnodes referencing
	 * a closed device.
	 */
	if (dev && ((vp->v_flag & VXLOCK) ||
	    (dev_dflags(dev) & D_TRACKCLOSE) ||
	    (vcount(vp) <= 1 && vp->v_opencount == 1))) {
		error = dev_dclose(dev, ap->a_fflag, S_IFCHR, ap->a_td);
	} else {
		error = 0;
	}

	/*
	 * Track the actual opens and closes on the vnode.  The last close
	 * disassociates the rdev.  If the rdev is already disassociated 
	 * the vnode might have been revoked and no further opencount
	 * tracking occurs.
	 */
	if (dev) {
		KKASSERT(vp->v_opencount > 0);
		if (dev_ref_debug) {
			printf("spec_close: %s %d\n",
				dev->si_name, vp->v_opencount - 1);
		}
		if (--vp->v_opencount == 0)
			v_release_rdev(vp);
		release_dev(dev);
	}
	return(error);
}

/*
 * Print out the contents of a special device vnode.
 *
 * spec_print(struct vnode *a_vp)
 */
static int
spec_print(struct vop_print_args *ap)
{
	printf("tag VT_NON, dev %s\n", devtoname(ap->a_vp->v_rdev));
	return (0);
}

/*
 * Special device advisory byte-level locks.
 *
 * spec_advlock(struct vnode *a_vp, caddr_t a_id, int a_op,
 *		struct flock *a_fl, int a_flags)
 */
/* ARGSUSED */
static int
spec_advlock(struct vop_advlock_args *ap)
{
	return (ap->a_flags & F_FLOCK ? EOPNOTSUPP : EINVAL);
}

static void
spec_getpages_iodone(struct buf *bp)
{
	bp->b_flags |= B_DONE;
	wakeup(bp);
}

static int
spec_getpages(struct vop_getpages_args *ap)
{
	vm_offset_t kva;
	int error;
	int i, pcount, size, s;
	daddr_t blkno;
	struct buf *bp;
	vm_page_t m;
	vm_ooffset_t offset;
	int toff, nextoff, nread;
	struct vnode *vp = ap->a_vp;
	int blksiz;
	int gotreqpage;

	error = 0;
	pcount = round_page(ap->a_count) / PAGE_SIZE;

	/*
	 * Calculate the offset of the transfer and do sanity check.
	 * FreeBSD currently only supports an 8 TB range due to b_blkno
	 * being in DEV_BSIZE ( usually 512 ) byte chunks on call to
	 * VOP_STRATEGY.  XXX
	 */
	offset = IDX_TO_OFF(ap->a_m[0]->pindex) + ap->a_offset;

#define	DADDR_T_BIT	(sizeof(daddr_t)*8)
#define	OFFSET_MAX	((1LL << (DADDR_T_BIT + DEV_BSHIFT)) - 1)

	if (offset < 0 || offset > OFFSET_MAX) {
		/* XXX still no %q in kernel. */
		printf("spec_getpages: preposterous offset 0x%x%08x\n",
		       (u_int)((u_quad_t)offset >> 32),
		       (u_int)(offset & 0xffffffff));
		return (VM_PAGER_ERROR);
	}

	blkno = btodb(offset);

	/*
	 * Round up physical size for real devices.  We cannot round using
	 * v_mount's block size data because v_mount has nothing to do with
	 * the device.  i.e. it's usually '/dev'.  We need the physical block
	 * size for the device itself.
	 *
	 * We can't use v_rdev->si_mountpoint because it only exists when the
	 * block device is mounted.  However, we can use v_rdev.
	 */

	if (vn_isdisk(vp, NULL))
		blksiz = vp->v_rdev->si_bsize_phys;
	else
		blksiz = DEV_BSIZE;

	size = (ap->a_count + blksiz - 1) & ~(blksiz - 1);

	bp = getpbuf(NULL);
	kva = (vm_offset_t)bp->b_data;

	/*
	 * Map the pages to be read into the kva.
	 */
	pmap_qenter(kva, ap->a_m, pcount);

	/* Build a minimal buffer header. */
	bp->b_flags = B_READ | B_CALL;
	bp->b_iodone = spec_getpages_iodone;

	/* B_PHYS is not set, but it is nice to fill this in. */
	bp->b_blkno = blkno;
	bp->b_lblkno = blkno;
	pbgetvp(ap->a_vp, bp);
	bp->b_bcount = size;
	bp->b_bufsize = size;
	bp->b_resid = 0;
	bp->b_runningbufspace = bp->b_bufsize;
	runningbufspace += bp->b_runningbufspace;

	mycpu->gd_cnt.v_vnodein++;
	mycpu->gd_cnt.v_vnodepgsin += pcount;

	/* Do the input. */
	VOP_STRATEGY(bp->b_vp, bp);

	s = splbio();

	/* We definitely need to be at splbio here. */
	while ((bp->b_flags & B_DONE) == 0) {
		tsleep(bp, 0, "spread", 0);
	}

	splx(s);

	if ((bp->b_flags & B_ERROR) != 0) {
		if (bp->b_error)
			error = bp->b_error;
		else
			error = EIO;
	}

	nread = size - bp->b_resid;

	if (nread < ap->a_count) {
		bzero((caddr_t)kva + nread,
			ap->a_count - nread);
	}
	pmap_qremove(kva, pcount);


	gotreqpage = 0;
	for (i = 0, toff = 0; i < pcount; i++, toff = nextoff) {
		nextoff = toff + PAGE_SIZE;
		m = ap->a_m[i];

		m->flags &= ~PG_ZERO;

		if (nextoff <= nread) {
			m->valid = VM_PAGE_BITS_ALL;
			vm_page_undirty(m);
		} else if (toff < nread) {
			/*
			 * Since this is a VM request, we have to supply the
			 * unaligned offset to allow vm_page_set_validclean()
			 * to zero sub-DEV_BSIZE'd portions of the page.
			 */
			vm_page_set_validclean(m, 0, nread - toff);
		} else {
			m->valid = 0;
			vm_page_undirty(m);
		}

		if (i != ap->a_reqpage) {
			/*
			 * Just in case someone was asking for this page we
			 * now tell them that it is ok to use.
			 */
			if (!error || (m->valid == VM_PAGE_BITS_ALL)) {
				if (m->valid) {
					if (m->flags & PG_WANTED) {
						vm_page_activate(m);
					} else {
						vm_page_deactivate(m);
					}
					vm_page_wakeup(m);
				} else {
					vm_page_free(m);
				}
			} else {
				vm_page_free(m);
			}
		} else if (m->valid) {
			gotreqpage = 1;
			/*
			 * Since this is a VM request, we need to make the
			 * entire page presentable by zeroing invalid sections.
			 */
			if (m->valid != VM_PAGE_BITS_ALL)
			    vm_page_zero_invalid(m, FALSE);
		}
	}
	if (!gotreqpage) {
		m = ap->a_m[ap->a_reqpage];
		printf(
	    "spec_getpages:(%s) I/O read failure: (error=%d) bp %p vp %p\n",
			devtoname(bp->b_dev), error, bp, bp->b_vp);
		printf(
	    "               size: %d, resid: %ld, a_count: %d, valid: 0x%x\n",
		    size, bp->b_resid, ap->a_count, m->valid);
		printf(
	    "               nread: %d, reqpage: %d, pindex: %lu, pcount: %d\n",
		    nread, ap->a_reqpage, (u_long)m->pindex, pcount);
		/*
		 * Free the buffer header back to the swap buffer pool.
		 */
		relpbuf(bp, NULL);
		return VM_PAGER_ERROR;
	}
	/*
	 * Free the buffer header back to the swap buffer pool.
	 */
	relpbuf(bp, NULL);
	return VM_PAGER_OK;
}
