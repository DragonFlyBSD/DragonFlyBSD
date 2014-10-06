/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko (semenu@FreeBSD.org)
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/fs/hpfs/hpfs_vnops.c,v 1.2.2.2 2002/01/15 18:35:09 semenu Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/dirent.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#if !defined(__DragonFly__)
#include <vm/vm_prot.h>
#endif
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>
#if defined(__DragonFly__)
#include <vm/vnode_pager.h>
#endif
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#if !defined(__DragonFly__)
#include <miscfs/specfs/specdev.h>
#include <miscfs/genfs/genfs.h>
#endif

#include <sys/unistd.h> /* for pathconf(2) constants */

#include "hpfs.h"
#include "hpfsmount.h"
#include "hpfs_subr.h"
#include "hpfs_ioctl.h"

static int	hpfs_de_uiomove (int *, struct hpfsmount *,
				 struct hpfsdirent *, struct uio *);
static int	hpfs_ioctl (struct vop_ioctl_args *ap);
static int	hpfs_read (struct vop_read_args *);
static int	hpfs_write (struct vop_write_args *ap);
static int	hpfs_getattr (struct vop_getattr_args *ap);
static int	hpfs_setattr (struct vop_setattr_args *ap);
static int	hpfs_inactive (struct vop_inactive_args *ap);
static int	hpfs_print (struct vop_print_args *ap);
static int	hpfs_reclaim (struct vop_reclaim_args *ap);
static int	hpfs_strategy (struct vop_strategy_args *ap);
static int	hpfs_access (struct vop_access_args *ap);
static int	hpfs_readdir (struct vop_readdir_args *ap);
static int	hpfs_lookup (struct vop_old_lookup_args *ap);
static int	hpfs_create (struct vop_old_create_args *);
static int	hpfs_remove (struct vop_old_remove_args *);
static int	hpfs_bmap (struct vop_bmap_args *ap);
#if defined(__DragonFly__)
static int	hpfs_fsync (struct vop_fsync_args *ap);
#endif
static int	hpfs_pathconf (struct vop_pathconf_args *ap);

#if defined(__DragonFly__)

/*
 * hpfs_fsync(struct vnode *a_vp, int a_waitfor)
 */
static int
hpfs_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
#ifdef DIAGNOSTIC
loop:
#endif
	vfsync(vp, ap->a_waitfor, 0, NULL, NULL);
#ifdef DIAGNOSTIC
	if (ap->a_waitfor == MNT_WAIT && !RB_EMPTY(&vp->v_rbdirty_tree)) {
		vprint("hpfs_fsync: dirty", vp);
		goto loop;
	}
#endif

	/*
	 * Write out the on-disc version of the vnode.
	 */
	return hpfs_update(VTOHP(vp));
}

#endif

/*
 * hpfs_ioctl(struct vnode *a_vp, u_long a_command, caddr_t a_data,
 *	      int a_fflag, struct ucred *a_cred)
 */
static int
hpfs_ioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	int error;

	kprintf("hpfs_ioctl(0x%x, 0x%lx, 0x%p, 0x%x): ",
		hp->h_no, ap->a_command, ap->a_data, ap->a_fflag);

	switch (ap->a_command) {
	case HPFSIOCGEANUM: {
		u_long eanum;
		u_long passed;
		struct ea *eap;

		eanum = 0;

		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			while (passed < hp->h_fn.fn_ealen) {

				kprintf("EAname: %s\n", EA_NAME(eap));

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
			error = 0;
		} else {
			error = ENOENT;
		}

		kprintf("%lu eas\n", eanum);

		*(u_long *)ap->a_data = eanum;

		break;
	}
	case HPFSIOCGEASZ: {
		u_long eanum;
		u_long passed;
		struct ea *eap;

		kprintf("EA%ld\n", *(u_long *)ap->a_data);

		eanum = 0;
		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			error = ENOENT;
			while (passed < hp->h_fn.fn_ealen) {
				kprintf("EAname: %s\n", EA_NAME(eap));

				if (eanum == *(u_long *)ap->a_data) {
					*(u_long *)ap->a_data =
					  	eap->ea_namelen + 1 +
						eap->ea_vallen;

					error = 0;
					break;
				}

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
		} else {
			error = ENOENT;
		}

		break;
	}
	case HPFSIOCRDEA: {
		u_long eanum;
		u_long passed;
		struct hpfs_rdea *rdeap;
		struct ea *eap;

		rdeap = (struct hpfs_rdea *)ap->a_data;
		kprintf("EA%ld\n", rdeap->ea_no);

		eanum = 0;
		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			error = ENOENT;
			while (passed < hp->h_fn.fn_ealen) {
				kprintf("EAname: %s\n", EA_NAME(eap));

				if (eanum == rdeap->ea_no) {
					rdeap->ea_sz = eap->ea_namelen + 1 +
							eap->ea_vallen;
					copyout(EA_NAME(eap),rdeap->ea_data,
						rdeap->ea_sz);
					error = 0;
					break;
				}

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
		} else {
			error = ENOENT;
		}

		break;
	}
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

/*
 * Map file offset to disk offset.
 *
 * hpfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *	     off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
int
hpfs_bmap(struct vop_bmap_args *ap)
{
	struct hpfsnode *hp = VTOHP(ap->a_vp);
	int error;
	daddr_t lbn;
	daddr_t dbn;

	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	if (ap->a_doffsetp == NULL)
		return (0);

	dprintf(("hpfs_bmap(0x%x): ", hp->h_no));

	lbn = ap->a_loffset >> DEV_BSHIFT;
	KKASSERT(((int)ap->a_loffset & DEV_BMASK) == 0);

	error = hpfs_hpbmap (hp, lbn, &dbn, ap->a_runp);
	if (error || dbn == (daddr_t)-1) {
		*ap->a_doffsetp = NOOFFSET;
	} else {
		*ap->a_doffsetp = (off_t)dbn << DEV_BSHIFT;
	}
	return (error);
}

/*
 * hpfs_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
static int
hpfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	u_int xfersz, toread;
	u_int off;
	daddr_t lbn, bn;
	int resid;
	int runl;
	int error = 0;

	resid = (int)szmin(uio->uio_resid, hp->h_fn.fn_size - uio->uio_offset);

	dprintf(("hpfs_read(0x%x, off: %d resid: %zx, segflg: %d): "
		 "[resid: 0x%x]\n",
		 hp->h_no, (u_int32_t)uio->uio_offset,
		 uio->uio_resid, uio->uio_segflg, resid));

	while (resid) {
		lbn = uio->uio_offset >> DEV_BSHIFT;
		off = uio->uio_offset & (DEV_BSIZE - 1);
		dprintf(("hpfs_read: resid: 0x%zx lbn: 0x%x off: 0x%x\n",
			uio->uio_resid, lbn, off));
		error = hpfs_hpbmap(hp, lbn, &bn, &runl);
		if (error)
			return (error);

		toread = min(off + resid, min(64*1024, (runl+1)*DEV_BSIZE));
		xfersz = (toread + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		dprintf(("hpfs_read: bn: 0x%x (0x%x) toread: 0x%x (0x%x)\n",
			bn, runl, toread, xfersz));

		if (toread == 0) 
			break;

		error = bread(hp->h_devvp, dbtodoff(bn), xfersz, &bp);
		if (error) {
			brelse(bp);
			break;
		}

		error = uiomove(bp->b_data + off, (size_t)(toread - off), uio);
		if(error) {
			brelse(bp);
			break;
		}
		brelse(bp);
		resid -= toread;
	}
	dprintf(("hpfs_read: successful\n"));
	return (error);
}

/*
 * hpfs_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
static int
hpfs_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	u_int xfersz, towrite;
	u_int off;
	daddr_t lbn, bn;
	int runl;
	int error = 0;

	dprintf(("hpfs_write(0x%x, off: %d resid: %zd, segflg: %d):\n",
		hp->h_no, (u_int32_t)uio->uio_offset,
		uio->uio_resid, uio->uio_segflg));

	if (ap->a_ioflag & IO_APPEND) {
		dprintf(("hpfs_write: APPEND mode\n"));
		uio->uio_offset = hp->h_fn.fn_size;
	}
	if (uio->uio_offset + uio->uio_resid > hp->h_fn.fn_size) {
		error = hpfs_extend (hp, uio->uio_offset + uio->uio_resid);
		if (error) {
			kprintf("hpfs_write: hpfs_extend FAILED %d\n", error);
			return (error);
		}
	}

	while (uio->uio_resid) {
		lbn = uio->uio_offset >> DEV_BSHIFT;
		off = uio->uio_offset & (DEV_BSIZE - 1);
		dprintf(("hpfs_write: resid: 0x%zx lbn: 0x%x off: 0x%x\n",
			uio->uio_resid, lbn, off));
		error = hpfs_hpbmap(hp, lbn, &bn, &runl);
		if (error)
			return (error);

		towrite = szmin(off + uio->uio_resid,
				min(64*1024, (runl+1)*DEV_BSIZE));
		xfersz = (towrite + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		dprintf(("hpfs_write: bn: 0x%x (0x%x) towrite: 0x%x (0x%x)\n",
			bn, runl, towrite, xfersz));

		/*
		 * We do not have to issue a read-before-write if the xfer
		 * size does not cover the whole block.
		 *
		 * In the UIO_NOCOPY case, however, we are not overwriting
		 * anything and must do a read-before-write to fill in
		 * any missing pieces.
		 */
		if (off == 0 && towrite == xfersz &&
		    uio->uio_segflg != UIO_NOCOPY) {
			bp = getblk(hp->h_devvp, dbtodoff(bn), xfersz, 0, 0);
			clrbuf(bp);
		} else {
			error = bread(hp->h_devvp, dbtodoff(bn), xfersz, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		error = uiomove(bp->b_data + off, (size_t)(towrite - off), uio);
		if(error) {
			brelse(bp);
			return (error);
		}

		if (ap->a_ioflag & IO_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
	}

	dprintf(("hpfs_write: successful\n"));
	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 *
 * hpfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
hpfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct vattr *vap = ap->a_vap;
	int error;

	dprintf(("hpfs_getattr(0x%x):\n", hp->h_no));

#if defined(__DragonFly__)
	vap->va_fsid = dev2udev(hp->h_dev);
#else /* defined(__NetBSD__) */
	vap->va_fsid = ip->i_dev;
#endif
	vap->va_fileid = hp->h_no;
	vap->va_mode = hp->h_mode;
	vap->va_nlink = 1;
	vap->va_uid = hp->h_uid;
	vap->va_gid = hp->h_gid;
	vap->va_rdev = makedev(VNOVAL, VNOVAL);
	vap->va_size = hp->h_fn.fn_size;
	vap->va_bytes = ((hp->h_fn.fn_size + DEV_BSIZE-1) & ~(DEV_BSIZE-1)) +
			DEV_BSIZE;

	if (!(hp->h_flag & H_PARVALID)) {
		error = hpfs_validateparent(hp);
		if (error) 
			return (error);
	}
	vap->va_atime = hpfstimetounix(hp->h_atime);
	vap->va_mtime = hpfstimetounix(hp->h_mtime);
	vap->va_ctime = hpfstimetounix(hp->h_ctime);

	vap->va_flags = 0;
	vap->va_gen = 0;
	vap->va_blocksize = DEV_BSIZE;
	vap->va_type = vp->v_type;
	vap->va_filerev = 0;

	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 *
 * hpfs_setattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred)
 */
static int
hpfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error;

	dprintf(("hpfs_setattr(0x%x):\n", hp->h_no));

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (major(vap->va_rdev) != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		dprintf(("hpfs_setattr: changing nonsettable attr\n"));
		return (EINVAL);
	}

	/* Can't change flags XXX Could be implemented */
	if (vap->va_flags != VNOVAL) {
		kprintf("hpfs_setattr: FLAGS CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Can't change uid/gid XXX Could be implemented */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		kprintf("hpfs_setattr: UID/GID CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Can't change mode XXX Could be implemented */
	if (vap->va_mode != (mode_t)VNOVAL) {
		kprintf("hpfs_setattr: MODE CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Update times */
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != hp->h_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_EACCESS(vp, VWRITE, cred))))
			return (error);
		if (vap->va_atime.tv_sec != VNOVAL)
			hp->h_atime = vap->va_atime.tv_sec;
		if (vap->va_mtime.tv_sec != VNOVAL)
			hp->h_mtime = vap->va_mtime.tv_sec;

		hp->h_flag |= H_PARCHANGE;
	}

	if (vap->va_size != VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			kprintf("hpfs_setattr: WRONG v_type\n");
			return (EINVAL);
		}

		if (vap->va_size < hp->h_fn.fn_size) {
#if defined(__DragonFly__)
			error = vtruncbuf(vp, vap->va_size, DEV_BSIZE);
			if (error)
				return (error);
#else /* defined(__NetBSD__) */
#error Need alternation for vtruncbuf()
#endif
			error = hpfs_truncate(hp, vap->va_size);
			if (error)
				return (error);

		} else if (vap->va_size > hp->h_fn.fn_size) {
#if defined(__DragonFly__)
			vnode_pager_setsize(vp, vap->va_size);
#endif
			error = hpfs_extend(hp, vap->va_size);
			if (error)
				return (error);
		}
	}

	return (0);
}

/*
 * Last reference to an node.  If necessary, write or delete it.
 *
 * hpfs_inactive(struct vnode *a_vp)
 */
int
hpfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	int error;

	dprintf(("hpfs_inactive(0x%x): \n", hp->h_no));

	if (hp->h_flag & H_CHANGE) {
		dprintf(("hpfs_inactive: node changed, update\n"));
		error = hpfs_update (hp);
		if (error)
			return (error);
	}

	if (hp->h_flag & H_PARCHANGE) {
		dprintf(("hpfs_inactive: parent node changed, update\n"));
		error = hpfs_updateparent (hp);
		if (error)
			return (error);
	}

	if (prtactive && VREFCNT(vp) > 1)
		vprint("hpfs_inactive: pushing active", vp);

	if (hp->h_flag & H_INVAL) {
#if defined(__DragonFly__)
		vrecycle(vp);
#else /* defined(__NetBSD__) */
		vgone(vp);
#endif
		return (0);
	}
	return (0);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 *
 * hpfs_reclaim(struct vnode *a_vp)
 */
int
hpfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);

	dprintf(("hpfs_reclaim(0x%x0): \n", hp->h_no));

	hpfs_hphashrem(hp);

	/* Purge old data structures associated with the inode. */
	if (hp->h_devvp) {
		vrele(hp->h_devvp);
		hp->h_devvp = NULL;
	}

	vp->v_data = NULL;

	kfree(hp, M_HPFSNO);

	return (0);
}

/*
 * hpfs_print(struct vnode *a_vp)
 */
static int
hpfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);

	kprintf("tag VT_HPFS, ino 0x%x",hp->h_no);
	lockmgr_printinfo(&vp->v_lock);
	kprintf("\n");
	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the VOP_BMAP operation may not
 * deadlock on memory.  See hpfs_bmap() for details. XXXXXXX (not impl)
 *
 * hpfs_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
int
hpfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp;
	int error;

	dprintf(("hpfs_strategy(): \n"));

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("hpfs_strategy: spec");

	nbio = push_bio(bio);
	if (nbio->bio_offset == NOOFFSET) {
		error = VOP_BMAP(vp, bio->bio_offset, &nbio->bio_offset,
				 NULL, NULL, bp->b_cmd);
		if (error) {
			kprintf("hpfs_strategy: VOP_BMAP FAILED %d\n", error);
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			/* I/O was never started on nbio, must biodone(bio) */
			biodone(bio);
			return (error);
		}
		if (nbio->bio_offset == NOOFFSET)
			vfs_bio_clrbuf(bp);
	}
	if (nbio->bio_offset == NOOFFSET) {
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return (0);
	}
        hp = VTOHP(ap->a_vp);
	vn_strategy(hp->h_devvp, nbio);
	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 *
 * hpfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
int
hpfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);

	dprintf(("hpfs_access(0x%x):\n", hp->h_no));
	return (vop_helper_access(ap, hp->h_uid, hp->h_gid, hp->h_mode, 0));
}

static int
hpfs_de_uiomove(int *error, struct hpfsmount *hpmp, struct hpfsdirent *dep,
		struct uio *uio)
{
	char convname[HPFS_MAXFILENAME + 1];
	int i, success;

	dprintf(("[no: 0x%x, size: %d, name: %2d:%.*s, flag: 0x%x] ",
		dep->de_fnode, dep->de_size, dep->de_namelen,
		dep->de_namelen, dep->de_name, dep->de_flag));

	/*strncpy(cde.d_name, dep->de_name, dep->de_namelen);*/
	for (i=0; i<dep->de_namelen; i++) 
		convname[i] = hpfs_d2u(hpmp, dep->de_name[i]);
	convname[dep->de_namelen] = '\0';

	success = vop_write_dirent(error, uio, dep->de_fnode,
			(dep->de_flag & DE_DIR) ? DT_DIR : DT_REG,
			dep->de_namelen, convname);

	dprintf(("[0x%zx] ", uio->uio_resid));
	return (success);
}


/*
 * hpfs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		int *a_ncookies, off_t **cookies)
 */
int
hpfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct hpfsmount *hpmp = hp->h_hpmp;
	struct uio *uio = ap->a_uio;
	int ncookies = 0, i, num, cnum;
	int error = 0;
	struct buf *bp;
	struct dirblk *dp;
	struct hpfsdirent *dep;
	lsn_t olsn;
	lsn_t lsn;
	int level;

	dprintf(("hpfs_readdir(0x%x, 0x%x, 0x%zx): ",
		hp->h_no, (u_int32_t)uio->uio_offset, uio->uio_resid));

	/*
	 * As we need to fake up . and .., and the remaining directory structure
	 * can't be expressed in one off_t as well, we just increment uio_offset
	 * by 1 for each entry.
	 *
	 * num is the entry we need to start reporting
	 * cnum is the current entry
	 */
	if (uio->uio_offset < 0 || uio->uio_offset > INT_MAX)
		return(EINVAL);
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	num = uio->uio_offset;
	cnum = 0;

	if( num <= cnum ) {
		dprintf((". faked, "));
		if (vop_write_dirent(&error, uio, hp->h_no, DT_DIR, 1, "."))
			goto done;
		if (error)
			goto done;
		ncookies ++;
	}
	cnum++;

	if( num <= cnum ) {
		dprintf((".. faked, "));
		if (vop_write_dirent(&error, uio, hp->h_fn.fn_parent, DT_DIR, 2, ".."))
			goto readdone;
		if (error)
			goto done;
		ncookies ++;
	}
	cnum++;

	lsn = ((alleaf_t *)hp->h_fn.fn_abd)->al_lsn;

	olsn = 0;
	level = 1;

dive:
	dprintf(("[dive 0x%x] ", lsn));
	error = bread(hp->h_devvp, dbtodoff(lsn), D_BSIZE, &bp);
	if (error) {
		brelse(bp);
		goto done;
	}

	dp = (struct dirblk *) bp->b_data;
	if (dp->d_magic != D_MAGIC) {
		kprintf("hpfs_readdir: MAGIC DOESN'T MATCH\n");
		brelse(bp);
		error = EINVAL;
		goto done;
	}

	dep = D_DIRENT(dp);

	if (olsn) {
		dprintf(("[restore 0x%x] ", olsn));

		while(!(dep->de_flag & DE_END) ) {
			if((dep->de_flag & DE_DOWN) &&
			   (olsn == DE_DOWNLSN(dep)))
					 break;
			dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
		}

		if((dep->de_flag & DE_DOWN) && (olsn == DE_DOWNLSN(dep))) {
			if (dep->de_flag & DE_END)
				goto blockdone;

			if (!(dep->de_flag & DE_SPECIAL)) {
				if (num <= cnum) {
					if (hpfs_de_uiomove(&error, hpmp, dep, uio)) {
						brelse(bp);
						dprintf(("[resid] "));
						goto readdone;
					}
					if (error) {
						brelse (bp);
						goto done;
					}
					ncookies++;
				}
				cnum++;
			}

			dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
		} else {
			kprintf("hpfs_readdir: ERROR! oLSN not found\n");
			brelse(bp);
			error = EINVAL;
			goto done;
		}
	}

	olsn = 0;

	while(!(dep->de_flag & DE_END)) {
		if(dep->de_flag & DE_DOWN) {
			lsn = DE_DOWNLSN(dep);
			brelse(bp);
			level++;
			goto dive;
		}

		if (!(dep->de_flag & DE_SPECIAL)) {
			if (num <= cnum) {
				if (hpfs_de_uiomove(&error, hpmp, dep, uio)) {
					brelse(bp);
					dprintf(("[resid] "));
					goto readdone;
				}
				if (error) {
					brelse (bp);
					goto done;
				}
				ncookies++;
			}
			cnum++;
		}

		dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
	}

	if(dep->de_flag & DE_DOWN) {
		dprintf(("[enddive] "));
		lsn = DE_DOWNLSN(dep);
		brelse(bp);
		level++;
		goto dive;
	}

blockdone:
	dprintf(("[EOB] "));
	olsn = lsn;
	lsn = dp->d_parent;
	brelse(bp);
	level--;

	dprintf(("[level %d] ", level));

	if (level > 0)
		goto dive;	/* undive really */

	if (ap->a_eofflag) {
	    dprintf(("[EOF] "));
	    *ap->a_eofflag = 1;
	}

readdone:
	uio->uio_offset = cnum;
	dprintf(("[readdone]\n"));
	if (!error && ap->a_ncookies != NULL) {
		off_t *cookies;
		off_t *cookiep;

		dprintf(("%d cookies, ",ncookies));
		if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
			panic("hpfs_readdir: unexpected uio from NFS server");
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		for (cookiep = cookies, i=0; i < ncookies; i++)
			*cookiep++ = ++num;

		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	}

done:
	vn_unlock(ap->a_vp);
	return (error);
}

/*
 * hpfs_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp)
 */
int
hpfs_lookup(struct vop_old_lookup_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct hpfsnode *dhp = VTOHP(dvp);
	struct hpfsmount *hpmp = dhp->h_hpmp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	struct vnode **vpp = ap->a_vpp;
	int error;
	int nameiop = cnp->cn_nameiop;
	int flags = cnp->cn_flags;
	int lockparent = flags & CNP_LOCKPARENT;
#ifdef HPFS_DEBUG
	int wantparent = flags & (CNP_LOCKPARENT | CNP_WANTPARENT);
#endif
	*vpp = NULL;
	dprintf(("hpfs_lookup(0x%x, %s, %ld, %d, %d): \n",
		dhp->h_no, cnp->cn_nameptr, cnp->cn_namelen,
		lockparent, wantparent));

	if (nameiop != NAMEI_CREATE && nameiop != NAMEI_DELETE && nameiop != NAMEI_LOOKUP) {
		kprintf("hpfs_lookup: LOOKUP, DELETE and CREATE are only supported\n");
		return (EOPNOTSUPP);
	}

	error = VOP_EACCESS(dvp, VEXEC, cred);
	if(error)
		return (error);

	if( (cnp->cn_namelen == 1) &&
	    !strncmp(cnp->cn_nameptr,".",1) ) {
		dprintf(("hpfs_lookup(0x%x,...): . faked\n",dhp->h_no));

		vref(dvp);
		*vpp = dvp;

		return (0);
	} else if( (cnp->cn_namelen == 2) &&
	    !strncmp(cnp->cn_nameptr,"..",2) && (flags & CNP_ISDOTDOT) ) {
		dprintf(("hpfs_lookup(0x%x,...): .. faked (0x%x)\n",
			dhp->h_no, dhp->h_fn.fn_parent));

		VOP__UNLOCK(dvp, 0);

		error = VFS_VGET(hpmp->hpm_mp, NULL,
				 dhp->h_fn.fn_parent, vpp);
		if (error) {
			VOP__LOCK(dvp, 0);
			return(error);
		}

		if (lockparent && (error = VOP__LOCK(dvp, 0))) {
			vput(*vpp);
			return (error);
		}
		return (error);
	} else {
		struct buf *bp;
		struct hpfsdirent *dep;
		struct hpfsnode *hp;

		error = hpfs_genlookupbyname(dhp,
				cnp->cn_nameptr, cnp->cn_namelen, &bp, &dep);
		if (error) {
			if (error == ENOENT && 
			    (nameiop == NAMEI_CREATE || nameiop == NAMEI_RENAME)) {
				if(!lockparent) {
					cnp->cn_flags |= CNP_PDIRUNLOCK;
					VOP__UNLOCK(dvp, 0);
				}
				return (EJUSTRETURN);
			}

			return (error);
		}

		dprintf(("hpfs_lookup: fnode: 0x%x, CPID: 0x%x\n",
			 dep->de_fnode, dep->de_cpid));

		if (nameiop == NAMEI_DELETE) {
			error = VOP_EACCESS(dvp, VWRITE, cred);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		if (dhp->h_no == dep->de_fnode) {
			brelse(bp);
			vref(dvp);
			*vpp = dvp;
			return (0);
		}

		error = VFS_VGET(hpmp->hpm_mp, NULL, dep->de_fnode, vpp);
		if (error) {
			kprintf("hpfs_lookup: VFS_VGET FAILED %d\n", error);
			brelse(bp);
			return(error);
		}

		hp = VTOHP(*vpp);

		hp->h_mtime = dep->de_mtime;
		hp->h_ctime = dep->de_ctime;
		hp->h_atime = dep->de_atime;
		bcopy(dep->de_name, hp->h_name, dep->de_namelen);
		hp->h_name[dep->de_namelen] = '\0';
		hp->h_namelen = dep->de_namelen;
		hp->h_flag |= H_PARVALID;

		brelse(bp);

		if(!lockparent) {
			cnp->cn_flags |= CNP_PDIRUNLOCK;
			VOP__UNLOCK(dvp, 0);
		}
	}
	return (error);
}

/*
 * hpfs_remove(struct vnode *a_dvp, struct vnode *a_vp,
 *		struct componentname *a_cnp)
 */
int
hpfs_remove(struct vop_old_remove_args *ap)
{
	int error;

	dprintf(("hpfs_remove(0x%x, %s, %ld): \n", VTOHP(ap->a_vp)->h_no,
		ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen));

	if (ap->a_vp->v_type == VDIR)
		return (EPERM);

	error = hpfs_removefnode (ap->a_dvp, ap->a_vp, ap->a_cnp);
	return (error);
}

/*
 * hpfs_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap)
 */
int
hpfs_create(struct vop_old_create_args *ap)
{
	int error;

	dprintf(("hpfs_create(0x%x, %s, %ld): \n", VTOHP(ap->a_dvp)->h_no,
		ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen));

	error = hpfs_makefnode (ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap);

	return (error);
}

/*
 * Return POSIX pathconf information applicable to NTFS filesystem
 *
 * hpfs_pathconf(struct vnode *a_vp, int a_name, t *a_retval)
 */
int
hpfs_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = HPFS_MAXFILENAME;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		return (0);
#if defined(__NetBSD__)
	case _PC_SYNC_IO:
		*ap->a_retval = 1;
		return (0);
	case _PC_FILESIZEBITS:
		*ap->a_retval = 32;
		return (0);
#endif
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}


/*
 * Global vfs data structures
 */

struct vop_ops hpfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_getattr =		hpfs_getattr,
	.vop_setattr =		hpfs_setattr,
	.vop_inactive =		hpfs_inactive,
	.vop_reclaim =		hpfs_reclaim,
	.vop_print =		hpfs_print,
	.vop_old_create =	hpfs_create,
	.vop_old_remove =	hpfs_remove,
	.vop_old_lookup =	hpfs_lookup,
	.vop_access =		hpfs_access,
	.vop_readdir =		hpfs_readdir,
	.vop_fsync =		hpfs_fsync,
	.vop_bmap =		hpfs_bmap,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_strategy =		hpfs_strategy,
	.vop_read =		hpfs_read,
	.vop_write =		hpfs_write,
	.vop_ioctl =		hpfs_ioctl,
	.vop_pathconf =		hpfs_pathconf
};

