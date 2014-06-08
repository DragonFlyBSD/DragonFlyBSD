/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah Hdr: vn.c 1.13 94/04/02
 *
 *	from: @(#)vn.c	8.6 (Berkeley) 4/1/94
 * $FreeBSD: src/sys/dev/vn/vn.c,v 1.105.2.4 2001/11/18 07:11:00 dillon Exp $
 */

/*
 * Vnode disk driver.
 *
 * Block/character interface to a vnode.  Allows one to treat a file
 * as a disk (e.g. build a filesystem in it, mount it, etc.).
 *
 * NOTE 1: There is a security issue involved with this driver.
 * Once mounted all access to the contents of the "mapped" file via
 * the special file is controlled by the permissions on the special
 * file, the protection of the mapped file is ignored (effectively,
 * by using root credentials in all transactions).
 *
 * NOTE 2: Doesn't interact with leases, should it?
 */

#include "use_vn.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/nlookup.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/module.h>
#include <sys/vnioctl.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_pageout.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>
#include <sys/devfs.h>

static	d_ioctl_t	vnioctl;
static	d_open_t	vnopen;
static	d_close_t	vnclose;
static	d_psize_t	vnsize;
static	d_strategy_t	vnstrategy;
static	d_clone_t	vnclone;

MALLOC_DEFINE(M_VN, "vn_softc", "vn driver structures");
DEVFS_DECLARE_CLONE_BITMAP(vn);

#if NVN <= 1
#define VN_PREALLOCATED_UNITS	4
#else
#define VN_PREALLOCATED_UNITS	NVN
#endif

#define VN_BSIZE_BEST	8192

/*
 * dev_ops
 *	D_DISK		we want to look like a disk
 *	D_CANFREE	We support BUF_CMD_FREEBLKS
 */

static struct dev_ops vn_ops = {
	{ "vn", 0, D_DISK | D_CANFREE },
	.d_open =	vnopen,
	.d_close =	vnclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	vnioctl,
	.d_strategy =	vnstrategy,
	.d_psize =	vnsize
};

struct vn_softc {
	int		sc_unit;
	int		sc_flags;	/* flags 			*/
	u_int64_t	sc_size;	/* size of vn, sc_secsize scale	*/
	int		sc_secsize;	/* sector size			*/
	struct disk	sc_disk;
	struct vnode	*sc_vp;		/* vnode if not NULL		*/
	vm_object_t	sc_object;	/* backing object if not NULL	*/
	struct ucred	*sc_cred;	/* credentials 			*/
	int		 sc_maxactive;	/* max # of active requests 	*/
	struct buf	 sc_tab;	/* transfer queue 		*/
	u_long		 sc_options;	/* options 			*/
	cdev_t		 sc_dev;	/* devices that refer to this unit */
	SLIST_ENTRY(vn_softc) sc_list;
};

static SLIST_HEAD(, vn_softc) vn_list;

/* sc_flags */
#define VNF_INITED	0x01
#define	VNF_READONLY	0x02
#define VNF_OPENED	0x10
#define	VNF_DESTROY	0x20

static u_long	vn_options;

#define IFOPT(vn,opt) if (((vn)->sc_options|vn_options) & (opt))
#define TESTOPT(vn,opt) (((vn)->sc_options|vn_options) & (opt))

static int	vnsetcred (struct vn_softc *vn, struct ucred *cred);
static void	vnclear (struct vn_softc *vn);
static int	vnget (cdev_t dev, struct vn_softc *vn , struct vn_user *vnu);
static int	vn_modevent (module_t, int, void *);
static int 	vniocattach_file (struct vn_softc *, struct vn_ioctl *, cdev_t dev, int flag, struct ucred *cred);
static int 	vniocattach_swap (struct vn_softc *, struct vn_ioctl *, cdev_t dev, int flag, struct ucred *cred);
static cdev_t	vn_create(int unit, struct devfs_bitmap *bitmap, int clone);

static int
vnclone(struct dev_clone_args *ap)
{
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(vn), 0);
	ap->a_dev = vn_create(unit, &DEVFS_CLONE_BITMAP(vn), 1);

	return 0;
}

static	int
vnclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct vn_softc *vn;

	vn = dev->si_drv1;
	KKASSERT(vn != NULL);

	vn->sc_flags &= ~VNF_OPENED;

	/* The disk has been detached and can now be safely destroyed */
	if (vn->sc_flags & VNF_DESTROY) {
		KKASSERT(disk_getopencount(&vn->sc_disk) == 0);
		disk_destroy(&vn->sc_disk);
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(vn), dkunit(dev));
		SLIST_REMOVE(&vn_list, vn, vn_softc, sc_list);
		kfree(vn, M_VN);
	}
	return (0);
}

static struct vn_softc *
vncreatevn(void)
{
	struct vn_softc *vn;

	vn = kmalloc(sizeof *vn, M_VN, M_WAITOK | M_ZERO);
	return vn;
}

static void
vninitvn(struct vn_softc *vn, cdev_t dev)
{
	int unit;

	KKASSERT(vn != NULL);
	KKASSERT(dev != NULL);
	unit = dkunit(dev);

	vn->sc_unit = unit;
	dev->si_drv1 = vn;
	vn->sc_dev = dev;

	SLIST_INSERT_HEAD(&vn_list, vn, sc_list);
}

static	int
vnopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct vn_softc *vn;

	/*
	 * Locate preexisting device
	 */

	vn = dev->si_drv1;
	KKASSERT(vn != NULL);

	/*
	 * Update si_bsize fields for device.  This data will be overriden by
	 * the slice/parition code for vn accesses through partitions, and
	 * used directly if you open the 'whole disk' device.
	 *
	 * si_bsize_best must be reinitialized in case VN has been 
	 * reconfigured, plus make it at least VN_BSIZE_BEST for efficiency.
	 */
	dev->si_bsize_phys = vn->sc_secsize;
	dev->si_bsize_best = vn->sc_secsize;
	if (dev->si_bsize_best < VN_BSIZE_BEST)
		dev->si_bsize_best = VN_BSIZE_BEST;

	if ((ap->a_oflags & FWRITE) && (vn->sc_flags & VNF_READONLY))
		return (EACCES);

	IFOPT(vn, VN_FOLLOW)
		kprintf("vnopen(%s, 0x%x, 0x%x)\n",
		    devtoname(dev), ap->a_oflags, ap->a_devtype);

	vn->sc_flags |= VNF_OPENED;
	return(0);
}

/*
 *	vnstrategy:
 *
 *	Run strategy routine for VN device.  We use VOP_READ/VOP_WRITE calls
 *	for vnode-backed vn's, and the swap_pager_strategy() call for
 *	vm_object-backed vn's.
 */
static int
vnstrategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct buf *bp;
	struct bio *nbio;
	int unit;
	struct vn_softc *vn;
	int error;

	unit = dkunit(dev);
	vn = dev->si_drv1;
	KKASSERT(vn != NULL);

	bp = bio->bio_buf;

	IFOPT(vn, VN_DEBUG)
		kprintf("vnstrategy(%p): unit %d\n", bp, unit);

	if ((vn->sc_flags & VNF_INITED) == 0) {
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
		return(0);
	}

	bp->b_resid = bp->b_bcount;

	/*
	 * The vnode device is using disk/slice label support.
	 *
	 * The dscheck() function is called for validating the
	 * slices that exist ON the vnode device itself, and
	 * translate the "slice-relative" block number, again.
	 * dscheck() will call biodone() and return NULL if
	 * we are at EOF or beyond the device size.
	 */

	nbio = bio;

	/*
	 * Use the translated nbio from this point on
	 */
	if (vn->sc_vp && bp->b_cmd == BUF_CMD_FREEBLKS) {
		/*
		 * Freeblks is not handled for vnode-backed elements yet.
		 */
		bp->b_resid = 0;
		/* operation complete */
	} else if (vn->sc_vp) {
		/*
		 * VNODE I/O
		 *
		 * If an error occurs, we set B_ERROR but we do not set 
		 * B_INVAL because (for a write anyway), the buffer is 
		 * still valid.
		 */
		struct uio auio;
		struct iovec aiov;

		bzero(&auio, sizeof(auio));

		aiov.iov_base = bp->b_data;
		aiov.iov_len = bp->b_bcount;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = nbio->bio_offset;
		auio.uio_segflg = UIO_SYSSPACE;
		if (bp->b_cmd == BUF_CMD_READ)
			auio.uio_rw = UIO_READ;
		else
			auio.uio_rw = UIO_WRITE;
		auio.uio_resid = bp->b_bcount;
		auio.uio_td = curthread;

		/*
		 * Don't use IO_DIRECT here, it really gets in the way
		 * due to typical blocksize differences between the
		 * fs backing the VN device and whatever is running on
		 * the VN device.
		 */
		switch (bp->b_cmd) {
		case (BUF_CMD_READ):
			vn_lock(vn->sc_vp, LK_SHARED | LK_RETRY);
			error = VOP_READ(vn->sc_vp, &auio, IO_RECURSE,
					 vn->sc_cred);
			break;

		case (BUF_CMD_WRITE):
			vn_lock(vn->sc_vp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_WRITE(vn->sc_vp, &auio, IO_RECURSE,
					  vn->sc_cred);
			break;

		case (BUF_CMD_FLUSH):
			auio.uio_resid = 0;
			vn_lock(vn->sc_vp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_FSYNC(vn->sc_vp, MNT_WAIT, 0);
			break;
		default:
			auio.uio_resid = 0;
			error = 0;
			goto breakunlocked;
		}
		vn_unlock(vn->sc_vp);
breakunlocked:
		bp->b_resid = auio.uio_resid;
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
		}
		/* operation complete */
	} else if (vn->sc_object) {
		/*
		 * OBJT_SWAP I/O (handles read, write, freebuf)
		 *
		 * We have nothing to do if freeing  blocks on a reserved
		 * swap area, othrewise execute the op.
		 */
		if (bp->b_cmd == BUF_CMD_FREEBLKS && TESTOPT(vn, VN_RESERVE)) {
			bp->b_resid = 0;
			/* operation complete */
		} else {
			swap_pager_strategy(vn->sc_object, nbio);
			return(0);
			/* NOT REACHED */
		}
	} else {
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR | B_INVAL;
		bp->b_error = EINVAL;
		/* operation complete */
	}
	biodone(nbio);
	return(0);
}

/* ARGSUSED */
static	int
vnioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct vn_softc *vn;
	struct vn_ioctl *vio;
	int error;
	u_long *f;

	vn = dev->si_drv1;
	IFOPT(vn,VN_FOLLOW) {
		kprintf("vnioctl(%s, 0x%lx, %p, 0x%x): unit %d\n",
		    devtoname(dev), ap->a_cmd, ap->a_data, ap->a_fflag,
		    dkunit(dev));
	}

	switch (ap->a_cmd) {
	case VNIOCATTACH:
	case VNIOCDETACH:
	case VNIOCGSET:
	case VNIOCGCLEAR:
	case VNIOCGET:
	case VNIOCUSET:
	case VNIOCUCLEAR:
		goto vn_specific;
	}

#if 0
	if (dkslice(dev) != WHOLE_DISK_SLICE ||
		dkpart(dev) != WHOLE_SLICE_PART)
		return (ENOTTY);
#endif

    vn_specific:

	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
	if (error)
		return (error);

	vio = (struct vn_ioctl *)ap->a_data;
	f = (u_long*)ap->a_data;

	switch (ap->a_cmd) {
	case VNIOCATTACH:
		if (vn->sc_flags & VNF_INITED)
			return(EBUSY);

		if (vn->sc_flags & VNF_DESTROY)
			return(ENXIO);

		if (vio->vn_file == NULL)
			error = vniocattach_swap(vn, vio, dev, ap->a_fflag, ap->a_cred);
		else
			error = vniocattach_file(vn, vio, dev, ap->a_fflag, ap->a_cred);
		break;

	case VNIOCDETACH:
		if ((vn->sc_flags & VNF_INITED) == 0)
			return(ENXIO);
		/*
		 * XXX handle i/o in progress.  Return EBUSY, or wait, or
		 * flush the i/o.
		 * XXX handle multiple opens of the device.  Return EBUSY,
		 * or revoke the fd's.
		 * How are these problems handled for removable and failing
		 * hardware devices? (Hint: They are not)
		 */
		if ((disk_getopencount(&vn->sc_disk)) > 1)
			return (EBUSY);

		vnclear(vn);
		IFOPT(vn, VN_FOLLOW)
			kprintf("vnioctl: CLRed\n");

		if (dkunit(dev) >= VN_PREALLOCATED_UNITS) {
			vn->sc_flags |= VNF_DESTROY;
		}

		break;

	case VNIOCGET:
		error = vnget(dev, vn, (struct vn_user *) ap->a_data);
		break;

	case VNIOCGSET:
		vn_options |= *f;
		*f = vn_options;
		break;

	case VNIOCGCLEAR:
		vn_options &= ~(*f);
		*f = vn_options;
		break;

	case VNIOCUSET:
		vn->sc_options |= *f;
		*f = vn->sc_options;
		break;

	case VNIOCUCLEAR:
		vn->sc_options &= ~(*f);
		*f = vn->sc_options;
		break;

	default:
		error = ENOTTY;
		break;
	}
	return(error);
}

/*
 *	vniocattach_file:
 *
 *	Attach a file to a VN partition.  Return the size in the vn_size
 *	field.
 */

static int
vniocattach_file(struct vn_softc *vn, struct vn_ioctl *vio, cdev_t dev,
		 int flag, struct ucred *cred)
{
	struct vattr vattr;
	struct nlookupdata nd;
	int error, flags;
	struct vnode *vp;
	struct disk_info info;

	flags = FREAD|FWRITE;
	error = nlookup_init(&nd, vio->vn_file, 
				UIO_USERSPACE, NLC_FOLLOW|NLC_LOCKVP);
	if (error)
		return (error);
	if ((error = vn_open(&nd, NULL, flags, 0)) != 0) {
		if (error != EACCES && error != EPERM && error != EROFS)
			goto done;
		flags &= ~FWRITE;
		nlookup_done(&nd);
		error = nlookup_init(&nd, vio->vn_file, UIO_USERSPACE, NLC_FOLLOW|NLC_LOCKVP);
		if (error)
			return (error);
		if ((error = vn_open(&nd, NULL, flags, 0)) != 0)
			goto done;
	}
	vp = nd.nl_open_vp;
	if (vp->v_type != VREG ||
	    (error = VOP_GETATTR(vp, &vattr))) {
		if (error == 0)
			error = EINVAL;
		goto done;
	}
	vn_unlock(vp);
	vn->sc_secsize = DEV_BSIZE;
	vn->sc_vp = vp;
	nd.nl_open_vp = NULL;

	/*
	 * If the size is specified, override the file attributes.  Note that
	 * the vn_size argument is in PAGE_SIZE sized blocks.
	 */
	if (vio->vn_size)
		vn->sc_size = vio->vn_size * PAGE_SIZE / vn->sc_secsize;
	else
		vn->sc_size = vattr.va_size / vn->sc_secsize;
	error = vnsetcred(vn, cred);
	if (error) {
		vn->sc_vp = NULL;
		vn_close(vp, flags, NULL);
		goto done;
	}
	vn->sc_flags |= VNF_INITED;
	if (flags == FREAD)
		vn->sc_flags |= VNF_READONLY;

	/*
	 * Set the disk info so that probing is triggered
	 */
	bzero(&info, sizeof(struct disk_info));
	info.d_media_blksize = vn->sc_secsize;
	info.d_media_blocks = vn->sc_size;
	/*
	 * reserve mbr sector for backwards compatibility
	 * when no slices exist.
	 */
	info.d_dsflags = DSO_COMPATMBR | DSO_RAWPSIZE;
	info.d_secpertrack = 32;
	info.d_nheads = 64 / (vn->sc_secsize / DEV_BSIZE);
	info.d_secpercyl = info.d_secpertrack * info.d_nheads;
	info.d_ncylinders = vn->sc_size / info.d_secpercyl;
	disk_setdiskinfo_sync(&vn->sc_disk, &info);

	error = dev_dopen(dev, flag, S_IFCHR, cred, NULL);
	if (error)
		vnclear(vn);

	IFOPT(vn, VN_FOLLOW)
		kprintf("vnioctl: SET vp %p size %llx blks\n",
		       vn->sc_vp, (long long)vn->sc_size);
done:
	nlookup_done(&nd);
	return(error);
}

/*
 *	vniocattach_swap:
 *
 *	Attach swap backing store to a VN partition of the size specified
 *	in vn_size.
 */

static int
vniocattach_swap(struct vn_softc *vn, struct vn_ioctl *vio, cdev_t dev,
		 int flag, struct ucred *cred)
{
	int error;
	struct disk_info info;

	/*
	 * Range check.  Disallow negative sizes or any size less then the
	 * size of a page.  Then round to a page.
	 */

	if (vio->vn_size <= 0)
		return(EDOM);

	/*
	 * Allocate an OBJT_SWAP object.
	 *
	 * sc_secsize is PAGE_SIZE'd
	 *
	 * vio->vn_size is in PAGE_SIZE'd chunks.
	 * sc_size must be in PAGE_SIZE'd chunks.  
	 * Note the truncation.
	 */

	vn->sc_secsize = PAGE_SIZE;
	vn->sc_size = vio->vn_size;
	vn->sc_object = swap_pager_alloc(NULL,
					 vn->sc_secsize * (off_t)vio->vn_size,
					 VM_PROT_DEFAULT, 0);
	IFOPT(vn, VN_RESERVE) {
		if (swap_pager_reserve(vn->sc_object, 0, vn->sc_size) < 0) {
			vm_pager_deallocate(vn->sc_object);
			vn->sc_object = NULL;
			return(EDOM);
		}
	}
	vn->sc_flags |= VNF_INITED;

	error = vnsetcred(vn, cred);
	if (error == 0) {
		/*
		 * Set the disk info so that probing is triggered
		 */
		bzero(&info, sizeof(struct disk_info));
		info.d_media_blksize = vn->sc_secsize;
		info.d_media_blocks = vn->sc_size;
		/*
		 * reserve mbr sector for backwards compatibility
		 * when no slices exist.
		 */
		info.d_dsflags = DSO_COMPATMBR | DSO_RAWPSIZE;
		info.d_secpertrack = 32;
		info.d_nheads = 64 / (vn->sc_secsize / DEV_BSIZE);
		info.d_secpercyl = info.d_secpertrack * info.d_nheads;
		info.d_ncylinders = vn->sc_size / info.d_secpercyl;
		disk_setdiskinfo_sync(&vn->sc_disk, &info);

		error = dev_dopen(dev, flag, S_IFCHR, cred, NULL);
	}
	if (error == 0) {
		IFOPT(vn, VN_FOLLOW) {
			kprintf("vnioctl: SET vp %p size %llx\n",
			       vn->sc_vp, (long long)vn->sc_size);
		}
	}
	if (error)
		vnclear(vn);
	return(error);
}

/*
 * Duplicate the current processes' credentials.  Since we are called only
 * as the result of a SET ioctl and only root can do that, any future access
 * to this "disk" is essentially as root.  Note that credentials may change
 * if some other uid can write directly to the mapped file (NFS).
 */
int
vnsetcred(struct vn_softc *vn, struct ucred *cred)
{
	char *tmpbuf;
	int error = 0;

	/*
	 * Set credits in our softc
	 */

	if (vn->sc_cred)
		crfree(vn->sc_cred);
	vn->sc_cred = crdup(cred);

	/*
	 * Horrible kludge to establish credentials for NFS  XXX.
	 */

	if (vn->sc_vp) {
		struct uio auio;
		struct iovec aiov;

		tmpbuf = kmalloc(vn->sc_secsize, M_TEMP, M_WAITOK);
		bzero(&auio, sizeof(auio));

		aiov.iov_base = tmpbuf;
		aiov.iov_len = vn->sc_secsize;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_resid = aiov.iov_len;
		vn_lock(vn->sc_vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_READ(vn->sc_vp, &auio, 0, vn->sc_cred);
		vn_unlock(vn->sc_vp);
		kfree(tmpbuf, M_TEMP);
	}
	return (error);
}

void
vnclear(struct vn_softc *vn)
{
	IFOPT(vn, VN_FOLLOW)
		kprintf("vnclear(%p): vp=%p\n", vn, vn->sc_vp);
	vn->sc_flags &= ~VNF_INITED;
	if (vn->sc_vp != NULL) {
		vn_close(vn->sc_vp,
		    (vn->sc_flags & VNF_READONLY) ? FREAD : (FREAD|FWRITE),
		    NULL);
		vn->sc_vp = NULL;
	}
	vn->sc_flags &= ~VNF_READONLY;
	if (vn->sc_cred) {
		crfree(vn->sc_cred);
		vn->sc_cred = NULL;
	}
	if (vn->sc_object != NULL) {
		vm_pager_deallocate(vn->sc_object);
		vn->sc_object = NULL;
	}

	disk_unprobe(&vn->sc_disk);

	vn->sc_size = 0;
}

/*
 * 	vnget:
 *
 *	populate a struct vn_user for the VNIOCGET ioctl.
 *	interface conventions defined in sys/sys/vnioctl.h.
 */

static int
vnget(cdev_t dev, struct vn_softc *vn, struct vn_user *vnu)
{
	int error, found = 0; 
	char *freepath, *fullpath;
	struct vattr vattr;

	if (vnu->vnu_unit == -1) {
		vnu->vnu_unit = dkunit(dev);
	}
	else if (vnu->vnu_unit < 0)
		return (EINVAL);

	SLIST_FOREACH(vn, &vn_list, sc_list) {

		if(vn->sc_unit != vnu->vnu_unit)
			continue;

		found = 1;

		if (vn->sc_flags & VNF_INITED && vn->sc_vp != NULL) {

			/* note: u_cred checked in vnioctl above */
			error = VOP_GETATTR(vn->sc_vp, &vattr);
			if (error) {
				kprintf("vnget: VOP_GETATTR for %p failed\n",
					vn->sc_vp);
				return (error);
			}

			error = vn_fullpath(curproc, vn->sc_vp,
						&fullpath, &freepath, 0);

			if (error) {
				kprintf("vnget: unable to resolve vp %p\n",
					vn->sc_vp);
				return(error);
			}
			
			strlcpy(vnu->vnu_file, fullpath,
				sizeof(vnu->vnu_file));
			kfree(freepath, M_TEMP);
			vnu->vnu_dev = vattr.va_fsid;
			vnu->vnu_ino = vattr.va_fileid;

		} 
		else if (vn->sc_flags & VNF_INITED && vn->sc_object != NULL){

			strlcpy(vnu->vnu_file, _VN_USER_SWAP,
				sizeof(vnu->vnu_file));
			vnu->vnu_size = vn->sc_size;
			vnu->vnu_secsize = vn->sc_secsize;

		} else {

			bzero(vnu->vnu_file, sizeof(vnu->vnu_file));
			vnu->vnu_dev = 0;
			vnu->vnu_ino = 0;

		}
		break;
	}

	if (!found)
		return(ENXIO);

	return(0);
}

static int
vnsize(struct dev_psize_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct vn_softc *vn;

	vn = dev->si_drv1;
	if (!vn)
		return(ENXIO);
	if ((vn->sc_flags & VNF_INITED) == 0)
		return(ENXIO);
	ap->a_result = (int64_t)vn->sc_size;
	return(0);
}

static cdev_t
vn_create(int unit, struct devfs_bitmap *bitmap, int clone)
{
	struct vn_softc *vn;
	struct disk_info info;
	cdev_t dev, ret_dev;

	vn = vncreatevn();
	if (clone) {
		/*
		 * For clone devices we need to return the top-level cdev,
		 * not the raw dev we'd normally work with.
		 */
		dev = disk_create_clone(unit, &vn->sc_disk, &vn_ops);
		ret_dev = vn->sc_disk.d_cdev;
	} else {
		ret_dev = dev = disk_create(unit, &vn->sc_disk, &vn_ops);
	}
	vninitvn(vn, dev);

	bzero(&info, sizeof(struct disk_info));
	info.d_media_blksize = 512;
	info.d_media_blocks = 0;
	info.d_dsflags = DSO_MBRQUIET | DSO_RAWPSIZE;
	info.d_secpertrack = 32;
	info.d_nheads = 64;
	info.d_secpercyl = info.d_secpertrack * info.d_nheads;
	info.d_ncylinders = 0;
	disk_setdiskinfo_sync(&vn->sc_disk, &info);

	if (bitmap != NULL)
		devfs_clone_bitmap_set(bitmap, unit);

	return ret_dev;
}

static int 
vn_modevent(module_t mod, int type, void *data)
{
	struct vn_softc *vn;
	static cdev_t dev = NULL;
	int i;

	switch (type) {
	case MOD_LOAD:
		dev = make_autoclone_dev(&vn_ops, &DEVFS_CLONE_BITMAP(vn), vnclone, UID_ROOT,
		    GID_OPERATOR, 0640, "vn");

		for (i = 0; i < VN_PREALLOCATED_UNITS; i++) {
			vn_create(i, &DEVFS_CLONE_BITMAP(vn), 0);
		}
		break;

	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		while ((vn = SLIST_FIRST(&vn_list)) != NULL) {
			/*
			 * XXX: no idea if we can return EBUSY even in the
			 *	shutdown case, so err on the side of caution
			 *	and just rip stuff out on shutdown.
			 */
			if (type != MOD_SHUTDOWN) {
				if (vn->sc_flags & VNF_OPENED)
					return (EBUSY);
			}

			disk_destroy(&vn->sc_disk);

			SLIST_REMOVE_HEAD(&vn_list, sc_list);

			if (vn->sc_flags & VNF_INITED)
				vnclear(vn);

			kfree(vn, M_VN);
		}
		destroy_autoclone_dev(dev, &DEVFS_CLONE_BITMAP(vn));
		dev_ops_remove_all(&vn_ops);
		break;
	default:
		break;
	}
	return 0;
}

DEV_MODULE(vn, vn_modevent, 0);
