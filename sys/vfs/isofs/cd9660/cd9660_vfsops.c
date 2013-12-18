/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
 *	@(#)cd9660_vfsops.c	8.18 (Berkeley) 5/22/95
 * $FreeBSD: src/sys/isofs/cd9660/cd9660_vfsops.c,v 1.74.2.7 2002/04/08 09:39:29 bde Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/nlookup.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/cdio.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/iconv.h>

#include <vm/vm_zone.h>

#include <sys/buf2.h>

#include "iso.h"
#include "iso_rrip.h"
#include "cd9660_node.h"
#include "cd9660_mount.h"

extern struct vop_ops cd9660_vnode_vops;
extern struct vop_ops cd9660_spec_vops;
extern struct vop_ops cd9660_fifo_vops;

MALLOC_DEFINE(M_ISOFSMNT, "ISOFS mount", "ISOFS mount structure");
MALLOC_DEFINE(M_ISOFSNODE, "ISOFS node", "ISOFS vnode private part");

struct iconv_functions *cd9660_iconv = NULL;

static int cd9660_mount (struct mount *, char *, caddr_t, struct ucred *);
static int cd9660_unmount (struct mount *, int);
static int cd9660_root (struct mount *, struct vnode **);
static int cd9660_statfs (struct mount *, struct statfs *, struct ucred *);
static int cd9660_vget (struct mount *, struct vnode *, ino_t, struct vnode **);
static int cd9660_fhtovp (struct mount *, struct vnode *rootvp,
				struct fid *, struct vnode **);
static int cd9660_checkexp (struct mount *, struct sockaddr *,
	    int *, struct ucred **);
static int cd9660_vptofh (struct vnode *, struct fid *);

static struct vfsops cd9660_vfsops = {
	.vfs_mount =    	cd9660_mount,
	.vfs_unmount =  	cd9660_unmount,
	.vfs_root =      	cd9660_root,
	.vfs_statfs =   	cd9660_statfs,
	.vfs_sync =     	vfs_stdsync,
	.vfs_vget =     	cd9660_vget,
	.vfs_fhtovp =   	cd9660_fhtovp,
	.vfs_checkexp =  	cd9660_checkexp,
	.vfs_vptofh =   	cd9660_vptofh,
	.vfs_init =     	cd9660_init,
	.vfs_uninit =    	cd9660_uninit,
};
VFS_SET(cd9660_vfsops, cd9660, VFCF_READONLY);
MODULE_VERSION(cd9660, 1);


/*
 * Called by vfs_mountroot when iso is going to be mounted as root.
 */

static int iso_get_ssector (cdev_t dev);
static int iso_mountfs (struct vnode *devvp, struct mount *mp,
			    struct iso_args *argp);

/*
 * Try to find the start of the last data track on this CD-ROM.  This
 * is used to mount the last session of a multi-session CD.  Bail out
 * and return 0 if we fail, this is always a safe bet.
 */
static int
iso_get_ssector(cdev_t dev)
{
	struct ioc_toc_header h;
	struct ioc_read_toc_single_entry t;
	int i;

	if (dev_dioctl(dev, CDIOREADTOCHEADER, (caddr_t)&h, FREAD,
		       proc0.p_ucred, NULL, NULL) != 0)
		return 0;

	for (i = h.ending_track; i >= 0; i--) {
		t.address_format = CD_LBA_FORMAT;
		t.track = i;
		if (dev_dioctl(dev, CDIOREADTOCENTRY, (caddr_t)&t, FREAD,
			       proc0.p_ucred, NULL, NULL) != 0) {
			return 0;
		}
		if ((t.entry.control & 4) != 0)
			/* found a data track */
			break;
	}

	if (i < 0)
		return 0;

	return ntohl(t.entry.addr.lba);
}

static int
iso_mountroot(struct mount *mp)
{
	struct iso_args args;
	struct vnode *rootvp;
	int error;

	if ((error = bdevvp(rootdev, &rootvp))) {
		kprintf("iso_mountroot: can't find rootvp\n");
		return (error);
	}
	args.flags = ISOFSMNT_ROOT;

	vn_lock(rootvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(rootvp, FREAD, FSCRED, NULL);
	vn_unlock(rootvp);
	if (error)
		return (error);

	args.ssector = iso_get_ssector(rootdev);

	vn_lock(rootvp, LK_EXCLUSIVE | LK_RETRY);
	VOP_CLOSE(rootvp, FREAD);
	vn_unlock(rootvp);

	if (bootverbose)
		kprintf("iso_mountroot(): using session at block %d\n",
		       args.ssector);
	if ((error = iso_mountfs(rootvp, mp, &args)) != 0)
		return (error);

	cd9660_statfs(mp, &mp->mnt_stat, proc0.p_ucred);
	return (0);
}

/*
 * VFS Operations.
 *
 * mount system call
 */
static int
cd9660_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct vnode *devvp;
	struct iso_args args;
	size_t size;
	int error;
	mode_t accessmode;
	struct iso_mnt *imp = NULL;
	struct nlookupdata nd;

	if ((mp->mnt_flag & (MNT_ROOTFS|MNT_UPDATE)) == MNT_ROOTFS) {
		return (iso_mountroot(mp));
	}
	if ((error = copyin(data, (caddr_t)&args, sizeof (struct iso_args))))
		return (error);

	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		return (EROFS);

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		imp = VFSTOISOFS(mp);
		if (args.fspec == 0)
			return (vfs_export(mp, &imp->im_export, &args.export));
	}
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	devvp = NULL;
	error = nlookup_init(&nd, args.fspec, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);
	if (error)
		return (error);

	if (!vn_isdisk(devvp, &error)) {
		vrele(devvp);
		return (error);
	}

	/*       
	 * Verify that user has necessary permissions on the device,
	 * or has superuser abilities
	 */
	accessmode = VREAD;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_EACCESS(devvp, accessmode, cred);
	if (error) 
		error = priv_check_cred(cred, PRIV_ROOT, 0);
	if (error) {
		vput(devvp);
		return (error);
	}
	vn_unlock(devvp);

	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		error = iso_mountfs(devvp, mp, &args);
	} else {
		if (devvp != imp->im_devvp)
			error = EINVAL;	/* needs translation */
		else
			vrele(devvp);
	}
	if (error) {
		vrele(devvp);
		return error;
	}
	imp = VFSTOISOFS(mp);
	copyinstr(args.fspec, mp->mnt_stat.f_mntfromname, MNAMELEN - 1,
	    &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	cd9660_statfs(mp, &mp->mnt_stat, cred);
	return 0;
}

/*
 * Common code for mount and mountroot
 */
static int
iso_mountfs(struct vnode *devvp, struct mount *mp, struct iso_args *argp)
{
	struct iso_mnt *isomp = NULL;
	struct buf *bp = NULL;
	struct buf *pribp = NULL, *supbp = NULL;
	cdev_t dev;
	int error = EINVAL;
	int needclose = 0;
	int high_sierra = 0;
	int iso_bsize;
	int iso_blknum;
	int joliet_level;
	struct iso_volume_descriptor *vdp = NULL;
	struct iso_primary_descriptor *pri = NULL;
	struct iso_sierra_primary_descriptor *pri_sierra = NULL;
	struct iso_supplementary_descriptor *sup = NULL;
	struct iso_directory_record *rootp;
	int logical_block_size;
	char cs_local[ICONV_CSNMAXLEN];
	char cs_disk[ICONV_CSNMAXLEN];

	if (!(mp->mnt_flag & MNT_RDONLY))
		return EROFS;

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * Flush out any old buffers remaining from a previous use.
	 */
	if ((error = vfs_mountedon(devvp)))
		return error;
	if (vcount(devvp) > 0)
		return EBUSY;
	if ((error = vinvalbuf(devvp, V_SAVE, 0, 0)))
		return (error);

	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(devvp, FREAD, FSCRED, NULL);
	vn_unlock(devvp);
	if (error)
		return error;
	dev = devvp->v_rdev;
	if (dev->si_iosize_max != 0)
		mp->mnt_iosize_max = dev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	needclose = 1;

	/* This is the "logical sector size".  The standard says this
	 * should be 2048 or the physical sector size on the device,
	 * whichever is greater.  For now, we'll just use a constant.
	 */
	iso_bsize = ISO_DEFAULT_BLOCK_SIZE;

	joliet_level = 0;
	for (iso_blknum = 16 + argp->ssector;
	     iso_blknum < 100 + argp->ssector;
	     iso_blknum++) {
		if ((error = bread(devvp, (off_t)iso_blknum * iso_bsize,
				  iso_bsize, &bp)) != 0)
			goto out;
		
		vdp = (struct iso_volume_descriptor *)bp->b_data;
		if (bcmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) != 0) {
			if (bcmp (vdp->id_sierra, ISO_SIERRA_ID,
				  sizeof vdp->id) != 0) {
				error = EINVAL;
				goto out;
			} else
				high_sierra = 1;
		}
		switch (isonum_711 (high_sierra? vdp->type_sierra: vdp->type)){
		case ISO_VD_PRIMARY:
			if (pribp == NULL) {
				pribp = bp;
				bp = NULL;
				pri = (struct iso_primary_descriptor *)vdp;
				pri_sierra =
				  (struct iso_sierra_primary_descriptor *)vdp;
			}
			break;

		case ISO_VD_SUPPLEMENTARY:
			if (supbp == NULL) {
				supbp = bp;
				bp = NULL;
				sup = (struct iso_supplementary_descriptor *)vdp;

				if (!(argp->flags & ISOFSMNT_NOJOLIET)) {
					if (bcmp(sup->escape, "%/@", 3) == 0)
						joliet_level = 1;
					if (bcmp(sup->escape, "%/C", 3) == 0)
						joliet_level = 2;
					if (bcmp(sup->escape, "%/E", 3) == 0)
						joliet_level = 3;

					if ((isonum_711 (sup->flags) & 1) &&
					    (argp->flags & ISOFSMNT_BROKENJOLIET) == 0)
						joliet_level = 0;
				}
			}
			break;

		case ISO_VD_END:
			goto vd_end;

		default:
			break;
		}
		if (bp) {
			brelse(bp);
			bp = NULL;
		}
	}
 vd_end:
	if (bp) {
		brelse(bp);
		bp = NULL;
	}

	if (pri == NULL) {
		error = EINVAL;
		goto out;
	}

	logical_block_size =
		isonum_723 (high_sierra?
			    pri_sierra->logical_block_size:
			    pri->logical_block_size);

	if (logical_block_size < DEV_BSIZE || logical_block_size > MAXBSIZE
	    || (logical_block_size & (logical_block_size - 1)) != 0) {
		error = EINVAL;
		goto out;
	}

	rootp = (struct iso_directory_record *)
		(high_sierra?
		 pri_sierra->root_directory_record:
		 pri->root_directory_record);

	isomp = kmalloc(sizeof *isomp, M_ISOFSMNT, M_WAITOK | M_ZERO);
	isomp->logical_block_size = logical_block_size;
	isomp->volume_space_size =
		isonum_733 (high_sierra?
			    pri_sierra->volume_space_size:
			    pri->volume_space_size);
	isomp->joliet_level = 0;
	/*
	 * Since an ISO9660 multi-session CD can also access previous
	 * sessions, we have to include them into the space consider-
	 * ations.  This doesn't yield a very accurate number since
	 * parts of the old sessions might be inaccessible now, but we
	 * can't do much better.  This is also important for the NFS
	 * filehandle validation.
	 */
	isomp->volume_space_size += argp->ssector;
	bcopy (rootp, isomp->root, sizeof isomp->root);
	isomp->root_extent = isonum_733 (rootp->extent);
	isomp->root_size = isonum_733 (rootp->size);

	isomp->im_bmask = logical_block_size - 1;
	isomp->im_bshift = ffs(logical_block_size) - 1;

	pribp->b_flags |= B_AGE;
	brelse(pribp);
	pribp = NULL;

	mp->mnt_data = (qaddr_t)isomp;
	mp->mnt_stat.f_fsid.val[0] = dev2udev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_maxsymlinklen = 0;
	mp->mnt_flag |= MNT_LOCAL;
	isomp->im_mountp = mp;
	isomp->im_dev = dev;
	isomp->im_devvp = devvp;

	dev->si_mountpoint = mp;

	/* Check the Rock Ridge Extention support */
	if (!(argp->flags & ISOFSMNT_NORRIP)) {
		if ((error = bread(isomp->im_devvp,
				  lblktooff(isomp, isomp->root_extent + isonum_711(rootp->ext_attr_length)),
				  isomp->logical_block_size, &bp)) != 0)
		    goto out;
		
		rootp = (struct iso_directory_record *)bp->b_data;
		
		if ((isomp->rr_skip = cd9660_rrip_offset(rootp,isomp)) < 0) {
		    argp->flags	 |= ISOFSMNT_NORRIP;
		} else {
		    argp->flags	 &= ~ISOFSMNT_GENS;
		}

		/*
		 * The contents are valid,
		 * but they will get reread as part of another vnode, so...
		 */
		bp->b_flags |= B_AGE;
		brelse(bp);
		bp = NULL;
	}
	isomp->im_flags = argp->flags & (ISOFSMNT_NORRIP | ISOFSMNT_GENS |
					 ISOFSMNT_EXTATT | ISOFSMNT_NOJOLIET |
					 ISOFSMNT_KICONV);
	if (isomp->im_flags & ISOFSMNT_KICONV && cd9660_iconv) {
                bcopy(argp->cs_local, cs_local, sizeof(cs_local));
                bcopy(argp->cs_disk, cs_disk, sizeof(cs_disk));
                cd9660_iconv->open(cs_local, cs_disk, &isomp->im_d2l);
                cd9660_iconv->open(cs_disk, cs_local, &isomp->im_l2d);
        } else {
                isomp->im_d2l = NULL;
                isomp->im_l2d = NULL;
        }

	if (high_sierra) {
		/* this effectively ignores all the mount flags */
		log(LOG_INFO, "cd9660: High Sierra Format\n");
		isomp->iso_ftype = ISO_FTYPE_HIGH_SIERRA;
	} else {
		switch (isomp->im_flags&(ISOFSMNT_NORRIP|ISOFSMNT_GENS)) {
		  default:
			  isomp->iso_ftype = ISO_FTYPE_DEFAULT;
			  break;
		  case ISOFSMNT_GENS|ISOFSMNT_NORRIP:
			  isomp->iso_ftype = ISO_FTYPE_9660;
			  break;
		  case 0:
			  log(LOG_INFO, "cd9660: RockRidge Extension\n");
			  isomp->iso_ftype = ISO_FTYPE_RRIP;
			  break;
		}
	}

	/* Decide whether to use the Joliet descriptor */

	if (isomp->iso_ftype != ISO_FTYPE_RRIP && joliet_level) {
		log(LOG_INFO, "cd9660: Joliet Extension (Level %d)\n", joliet_level);
		rootp = (struct iso_directory_record *)
			sup->root_directory_record;
		bcopy (rootp, isomp->root, sizeof isomp->root);
		isomp->root_extent = isonum_733 (rootp->extent);
		isomp->root_size = isonum_733 (rootp->size);
		isomp->joliet_level = joliet_level;
		supbp->b_flags |= B_AGE;
	}

	if (supbp) {
		brelse(supbp);
		supbp = NULL;
	}

	vfs_add_vnodeops(mp, &cd9660_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &cd9660_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &cd9660_fifo_vops, &mp->mnt_vn_fifo_ops);

	return 0;
out:
	dev->si_mountpoint = NULL;
	if (bp)
		brelse(bp);
	if (pribp)
		brelse(pribp);
	if (supbp)
		brelse(supbp);
	if (needclose) {
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		VOP_CLOSE(devvp, FREAD);
		vn_unlock(devvp);
	}
	if (isomp) {
		kfree((caddr_t)isomp, M_ISOFSMNT);
		mp->mnt_data = (qaddr_t)0;
	}
	return error;
}

/*
 * unmount system call
 */
static int
cd9660_unmount(struct mount *mp, int mntflags)
{
	struct iso_mnt *isomp;
	int error, flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
#if 0
	mntflushbuf(mp, 0);
	if (mntinvalbuf(mp))
		return EBUSY;
#endif
	if ((error = vflush(mp, 0, flags)))
		return (error);

	isomp = VFSTOISOFS(mp);

	if (isomp->im_flags & ISOFSMNT_KICONV && cd9660_iconv) {
                if (isomp->im_d2l)
                        cd9660_iconv->close(isomp->im_d2l);
                if (isomp->im_l2d)
                        cd9660_iconv->close(isomp->im_l2d);
        }

	isomp->im_devvp->v_rdev->si_mountpoint = NULL;
	vn_lock(isomp->im_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_CLOSE(isomp->im_devvp, FREAD);
	vn_unlock(isomp->im_devvp);
	vrele(isomp->im_devvp);
	kfree((caddr_t)isomp, M_ISOFSMNT);
	mp->mnt_data = (qaddr_t)0;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (error);
}

/*
 * Return root of a filesystem
 */
static int
cd9660_root(struct mount *mp, struct vnode **vpp)
{
	struct iso_mnt *imp = VFSTOISOFS(mp);
	struct iso_directory_record *dp =
	    (struct iso_directory_record *)imp->root;
	ino_t ino = isodirino(dp, imp);
	
	/*
	 * With RRIP we must use the `.' entry of the root directory.
	 * Simply tell vget, that it's a relocated directory.
	 */
	return (cd9660_vget_internal(mp, ino, vpp,
	    imp->iso_ftype == ISO_FTYPE_RRIP, dp));
}

/*
 * Get file system statistics.
 */
int
cd9660_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct iso_mnt *isomp;

	isomp = VFSTOISOFS(mp);

	sbp->f_bsize = isomp->logical_block_size;
	sbp->f_iosize = sbp->f_bsize;	/* XXX */
	sbp->f_blocks = isomp->volume_space_size;
	sbp->f_bfree = 0; /* total free blocks */
	sbp->f_bavail = 0; /* blocks free for non superuser */
	sbp->f_files =	0; /* total files */
	sbp->f_ffree = 0; /* free file nodes */
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}
	return 0;
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is in range
 * - call iget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the generation number matches
 */

struct ifid {
	ushort	ifid_len;
	ushort	ifid_pad;
	int	ifid_ino;
	long	ifid_start;
};

/* ARGSUSED */
int
cd9660_fhtovp(struct mount *mp, struct vnode *rootvp,
	      struct fid *fhp, struct vnode **vpp)
{
	struct ifid *ifhp = (struct ifid *)fhp;
	struct iso_node *ip;
	struct vnode *nvp;
	int error;
	
#ifdef	ISOFS_DBG
	kprintf("fhtovp: ino %d, start %ld\n",
	       ifhp->ifid_ino, ifhp->ifid_start);
#endif
	
	if ((error = VFS_VGET(mp, NULL, ifhp->ifid_ino, &nvp)) != 0) {
		*vpp = NULLVP;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->inode.iso_mode == 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	return (0);
}

int
cd9660_checkexp(struct mount *mp, struct sockaddr *nam, int *exflagsp,
		struct ucred **credanonp)
{
	struct netcred *np;
	struct iso_mnt *imp;

	imp = VFSTOISOFS(mp);	

	/*
	 * Get the export permission structure for this <mp, client> tuple.
	 */
	np = vfs_export_lookup(mp, &imp->im_export, nam);
	if (np == NULL)
		return (EACCES);

	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}

int
cd9660_vget(struct mount *mp, struct vnode *dvp, ino_t ino, struct vnode **vpp)
{

	/*
	 * XXXX
	 * It would be nice if we didn't always set the `relocated' flag
	 * and force the extra read, but I don't want to think about fixing
	 * that right now.
	 */
	return (cd9660_vget_internal(mp, ino, vpp,
#if 0
	    VFSTOISOFS(mp)->iso_ftype == ISO_FTYPE_RRIP,
#else
	    0,
#endif
	    NULL));
}

int
cd9660_vget_internal(struct mount *mp, ino_t ino, struct vnode **vpp,
		     int relocated, struct iso_directory_record *isodir)
{
	struct iso_mnt *imp;
	struct iso_node *ip;
	struct buf *bp;
	struct vnode *vp;
	cdev_t dev;
	int error;

	imp = VFSTOISOFS(mp);
	dev = imp->im_dev;
again:
	if ((*vpp = cd9660_ihashget(dev, ino)) != NULLVP)
		return (0);

	/* Allocate a new vnode/iso_node. */
	error = getnewvnode(VT_ISOFS, mp, &vp, 0, 0);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	ip = kmalloc(sizeof(struct iso_node), M_ISOFSNODE, M_WAITOK | M_ZERO);
	ip->i_vnode = vp;
	ip->i_dev = dev;
	ip->i_number = ino;

	/*
	 * Insert it into the inode hash table and check for a collision.
	 * If a collision occurs, throw away the vnode and try again.
	 */
	if (cd9660_ihashins(ip) != 0) {
		kprintf("debug: cd9660 ihashins collision, retrying\n");
		vx_put(vp);
		kfree(ip, M_ISOFSNODE);
		goto again;
	}
	vp->v_data = ip;

	if (isodir == NULL) {
		int lbn, off;

		lbn = lblkno(imp, ino);
		if (lbn >= imp->volume_space_size) {
			vx_put(vp);
			kprintf("fhtovp: lbn exceed volume space %d\n", lbn);
			return (ESTALE);
		}
	
		off = blkoff(imp, ino);
		if (off + ISO_DIRECTORY_RECORD_SIZE > imp->logical_block_size) {
			vx_put(vp);
			kprintf("fhtovp: crosses block boundary %d\n",
			       off + ISO_DIRECTORY_RECORD_SIZE);
			return (ESTALE);
		}
	
		error = bread(imp->im_devvp,
			      lblktooff(imp, lbn),
			      imp->logical_block_size, &bp);
		if (error) {
			vx_put(vp);
			brelse(bp);
			kprintf("fhtovp: bread error %d\n",error);
			return (error);
		}
		isodir = (struct iso_directory_record *)(bp->b_data + off);

		if (off + isonum_711(isodir->length) >
		    imp->logical_block_size) {
			vx_put(vp);
			if (bp != NULL)
				brelse(bp);
			kprintf("fhtovp: directory crosses block boundary %d[off=%d/len=%d]\n",
			       off +isonum_711(isodir->length), off,
			       isonum_711(isodir->length));
			return (ESTALE);
		}
	
#if 0
		if (isonum_733(isodir->extent) +
		    isonum_711(isodir->ext_attr_length) != ifhp->ifid_start) {
			if (bp != NULL)
				brelse(bp);
			kprintf("fhtovp: file start miss %d vs %d\n",
			       isonum_733(isodir->extent) + isonum_711(isodir->ext_attr_length),
			       ifhp->ifid_start);
			return (ESTALE);
		}
#endif
	} else
		bp = NULL;

	ip->i_mnt = imp;
	ip->i_devvp = imp->im_devvp;
	vref(ip->i_devvp);

	if (relocated) {
		/*
		 * On relocated directories we must
		 * read the `.' entry out of a dir.
		 */
		ip->iso_start = ino >> imp->im_bshift;
		if (bp != NULL)
			brelse(bp);
		if ((error = cd9660_devblkatoff(vp, (off_t)0, NULL, &bp)) != 0) {
			vx_put(vp);
			return (error);
		}
		isodir = (struct iso_directory_record *)bp->b_data;
	}

	ip->iso_extent = isonum_733(isodir->extent);
	ip->i_size = isonum_733(isodir->size);
	ip->iso_start = isonum_711(isodir->ext_attr_length) + ip->iso_extent;

	/*
	 * Setup time stamp, attribute
	 */
	vp->v_type = VNON;
	switch (imp->iso_ftype) {
	default:	/* ISO_FTYPE_9660 */
	    {
		struct buf *bp2;
		int off;
		if ((imp->im_flags & ISOFSMNT_EXTATT)
		    && (off = isonum_711(isodir->ext_attr_length)))
			cd9660_devblkatoff(vp, (off_t)-(off << imp->im_bshift), NULL,
				     &bp2);
		else
			bp2 = NULL;
		cd9660_defattr(isodir, ip, bp2, ISO_FTYPE_9660);
		cd9660_deftstamp(isodir, ip, bp2, ISO_FTYPE_9660);
		if (bp2)
			brelse(bp2);
		break;
	    }
	case ISO_FTYPE_RRIP:
		cd9660_rrip_analyze(isodir, ip, imp);
		break;
	}

	if (bp != NULL)
		brelse(bp);

	/*
	 * Initialize the associated vnode
	 */
	vp->v_type = IFTOVT(ip->inode.iso_mode);

	switch (vp->v_type) {
	case VFIFO:
		vp->v_ops = &mp->mnt_vn_fifo_ops;
		break;
	case VCHR:
	case VBLK:
		vp->v_ops = &mp->mnt_vn_spec_ops;
		addaliasu(vp, umajor(ip->inode.iso_rdev),
			  uminor(ip->inode.iso_rdev));
		break;
	case VREG:
	case VDIR:
		vinitvmio(vp, ip->i_size, PAGE_SIZE, -1);
		break;
	default:
		break;
	}
	
	if (ip->iso_extent == imp->root_extent)
		vsetflags(vp, VROOT);

	/*
	 * Return the locked and refd vp
	 */
	*vpp = vp;
	return (0);
}

/*
 * Vnode pointer to File handle
 */
/* ARGSUSED */
int
cd9660_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct iso_node *ip = VTOI(vp);
	struct ifid *ifhp;

	ifhp = (struct ifid *)fhp;
	ifhp->ifid_len = sizeof(struct ifid);

	ifhp->ifid_ino = ip->i_number;
	ifhp->ifid_start = ip->iso_start;

#ifdef	ISOFS_DBG
	kprintf("vptofh: ino %d, start %ld\n",
	       ifhp->ifid_ino,ifhp->ifid_start);
#endif
	return 0;
}
