/* $FreeBSD$ */
/*	$NetBSD: msdosfs_vfsops.c,v 1.51 1997/11/17 15:36:58 ws Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/iconv.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>

#include <vfs/msdosfs/bootsect.h>
#include <vfs/msdosfs/bpb.h>
#include <vfs/msdosfs/direntry.h>
#include <vfs/msdosfs/denode.h>
#include <vfs/msdosfs/fat.h>
#include <vfs/msdosfs/msdosfsmount.h>

extern struct vop_ops msdosfs_vnode_vops;
struct iconv_functions *msdosfs_iconv;

#define ENCODING_UNICODE        "UTF-16BE"
#if 1 /*def PC98*/
/*
 * XXX - The boot signature formatted by NEC PC-98 DOS looks like a
 *       garbage or a random value :-{
 *       If you want to use that broken-signatured media, define the
 *       following symbol even though PC/AT.
 *       (ex. mount PC-98 DOS formatted FD on PC/AT)
 */
#define	MSDOSFS_NOCHECKSIG
#endif

MALLOC_DEFINE(M_MSDOSFSMNT, "MSDOSFS mount", "MSDOSFS mount structure");
static MALLOC_DEFINE(M_MSDOSFSFAT, "MSDOSFS FAT", "MSDOSFS file allocation table");

static int mountmsdosfs(struct vnode *devvp, struct mount *mp,
    struct msdosfs_args *argp);
static int msdosfs_root(struct mount *, struct vnode **);
static int msdosfs_statfs(struct mount *, struct statfs *, struct ucred *);
static int msdosfs_unmount(struct mount *, int);

static int
update_mp(struct mount *mp, struct msdosfs_args *argp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	int error;
	char cs_local[ICONV_CSNMAXLEN];
	char cs_dos[ICONV_CSNMAXLEN];

	pmp->pm_gid = argp->gid;
	pmp->pm_uid = argp->uid;
	pmp->pm_mask = argp->mask & ALLPERMS;
	pmp->pm_dirmask = argp->dirmask & ALLPERMS;
	pmp->pm_flags |= argp->flags & MSDOSFSMNT_MNTOPT;

	if (pmp->pm_flags & MSDOSFSMNT_KICONV && msdosfs_iconv) {
		memcpy(cs_local, argp->cs_local, sizeof(cs_local));
		memcpy(cs_dos, argp->cs_dos, sizeof(cs_dos));
		kprintf("local: %s dos: %s\n",argp->cs_local, argp->cs_dos);
		error = msdosfs_iconv->open(cs_local, ENCODING_UNICODE,
					    &pmp->pm_w2u);
		if(error)
			return error;
		error = msdosfs_iconv->open(ENCODING_UNICODE, cs_local,
					    &pmp->pm_u2w);
		if(error)
			return error;
		error = msdosfs_iconv->open(cs_dos, cs_local, &pmp->pm_u2d);
		if(error)
			return error;
		error = msdosfs_iconv->open(cs_local, cs_dos, &pmp->pm_d2u);
		if(error)
			return error;
	}

	if (pmp->pm_flags & MSDOSFSMNT_NOWIN95)
		pmp->pm_flags |= MSDOSFSMNT_SHORTNAME;
	else
		pmp->pm_flags |= MSDOSFSMNT_LONGNAME;
	return 0;
}

/*
 * mp - path - addr in user space of mount point (ie /usr or whatever)
 * data - addr in user space of mount params including the name of the block
 * special file to treat as a filesystem.
 */
static int
msdosfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct vnode *devvp;	  /* vnode for blk device to mount */
	struct msdosfs_args args; /* will hold data from mount request */
	/* msdosfs specific mount control block */
	struct msdosfsmount *pmp = NULL;
	size_t size;
	int error, flags;
	mode_t accessmode;
	struct nlookupdata nd;

	error = copyin(data, (caddr_t)&args, sizeof(struct msdosfs_args));
	if (error)
		return (error);
	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		pmp = VFSTOMSDOSFS(mp);
		error = 0;
		if (!(pmp->pm_flags & MSDOSFSMNT_RONLY) &&
		    (mp->mnt_flag & MNT_RDONLY)) {
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = vflush(mp, 0, flags);
			if (error == 0) {
				devvp = pmp->pm_devvp;
				vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
				VOP_OPEN(devvp, FREAD, FSCRED, NULL);
				VOP_CLOSE(devvp, FREAD|FWRITE, NULL);
				vn_unlock(devvp);
				pmp->pm_flags |= MSDOSFSMNT_RONLY;
			}
		}
		if (!error && (mp->mnt_flag & MNT_RELOAD))
			error = EOPNOTSUPP; /* not yet implemented */
		if (error)
			return (error);
		if ((pmp->pm_flags & MSDOSFSMNT_RONLY) &&
		    (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			devvp = pmp->pm_devvp;
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			if (cred->cr_uid != 0) {
				error = VOP_EACCESS(devvp, VREAD | VWRITE,
						    cred);
				if (error) {
					vn_unlock(devvp);
					return (error);
				}
			}
			VOP_OPEN(devvp, FREAD|FWRITE, FSCRED, NULL);
			VOP_CLOSE(devvp, FREAD, NULL);
			vn_unlock(devvp);
			pmp->pm_flags &= ~MSDOSFSMNT_RONLY;
		}
		if (args.fspec == NULL) {
#ifdef __notyet__	/* doesn't work correctly with current mountd XXX */
			if (args.flags & MSDOSFSMNT_MNTOPT) {
				pmp->pm_flags &= ~MSDOSFSMNT_MNTOPT;
				pmp->pm_flags |= args.flags & MSDOSFSMNT_MNTOPT;
				if (pmp->pm_flags & MSDOSFSMNT_NOWIN95)
					pmp->pm_flags |= MSDOSFSMNT_SHORTNAME;
			}
#endif
			/*
			 * Process export requests.
			 */
			return (vfs_export(mp, &pmp->pm_export, &args.export));
		}
	}
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible disk device.
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
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	if (cred->cr_uid != 0) {
		accessmode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			accessmode |= VWRITE;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_EACCESS(devvp, accessmode, cred);
		if (error) {
			vput(devvp);
			return (error);
		}
		vn_unlock(devvp);
	}
	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		error = mountmsdosfs(devvp, mp, &args);
#ifdef MSDOSFS_DEBUG		/* only needed for the kprintf below */
		pmp = VFSTOMSDOSFS(mp);
#endif
	} else {
		if (devvp != pmp->pm_devvp)
			error = EINVAL;	/* XXX needs translation */
		else
			vrele(devvp);
	}
	if (error) {
		vrele(devvp);
		return (error);
	}

	error = update_mp(mp, &args);
	if (error) {
		if ((mp->mnt_flag & MNT_UPDATE) == 0)
			msdosfs_unmount(mp, MNT_FORCE);
		return error;
	}

	copyinstr(args.fspec, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	memset(mp->mnt_stat.f_mntfromname + size, 0, MNAMELEN - size);
	msdosfs_statfs(mp, &mp->mnt_stat, cred);
#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_mount(): mp %p, pmp %p, inusemap %p\n",
		mp, pmp, pmp->pm_inusemap);
#endif
	return (0);
}

static int
mountmsdosfs(struct vnode *devvp, struct mount *mp, struct msdosfs_args *argp)
{
	struct msdosfsmount *pmp;
	struct buf *bp;
	cdev_t dev;
	union bootsector *bsp;
	struct byte_bpb33 *b33;
	struct byte_bpb50 *b50;
	struct byte_bpb710 *b710;
	uint8_t SecPerClust;
	u_long clusters;
	int ronly, error;

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * Flush out any old buffers remaining from a previous use.
	 */
	error = vfs_mountedon(devvp);
	if (error)
		return (error);
	if (vcount(devvp) > 0)
		return (EBUSY);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
	vn_unlock(devvp);
	if (error)
		return (error);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, NULL);
	vn_unlock(devvp);
	if (error)
		return (error);
	dev = devvp->v_rdev;
	bp  = NULL; /* both used in error_exit */
	pmp = NULL;

	/*
	 * Read the boot sector of the filesystem, and then check the
	 * boot signature.  If not a dos boot sector then error out.
	 *
	 * NOTE: 8192 is a magic size that works for ffs.
	 */
	error = bread(devvp, 0, 8192, &bp);
	if (error)
		goto error_exit;
	bp->b_flags |= B_AGE;
	bsp = (union bootsector *)bp->b_data;
	b33 = (struct byte_bpb33 *)bsp->bs33.bsBPB;
	b50 = (struct byte_bpb50 *)bsp->bs50.bsBPB;
	b710 = (struct byte_bpb710 *)bsp->bs710.bsBPB;

#ifndef MSDOSFS_NOCHECKSIG
	if (bsp->bs50.bsBootSectSig0 != BOOTSIG0
	    || bsp->bs50.bsBootSectSig1 != BOOTSIG1) {
		error = EINVAL;
		goto error_exit;
	}
#endif

	pmp = kmalloc(sizeof(*pmp), M_MSDOSFSMNT, M_WAITOK | M_ZERO);
	pmp->pm_mountp = mp;

	/*
	 * Compute several useful quantities from the bpb in the
	 * bootsector.  Copy in the dos 5 variant of the bpb then fix up
	 * the fields that are different between dos 5 and dos 3.3.
	 */
	SecPerClust = b50->bpbSecPerClust;
	pmp->pm_BytesPerSec = getushort(b50->bpbBytesPerSec);
	if (pmp->pm_BytesPerSec < DEV_BSIZE) {
		error = EINVAL;
		goto error_exit;
	}
	pmp->pm_ResSectors = getushort(b50->bpbResSectors);
	pmp->pm_FATs = b50->bpbFATs;
	pmp->pm_RootDirEnts = getushort(b50->bpbRootDirEnts);
	pmp->pm_Sectors = getushort(b50->bpbSectors);
	pmp->pm_FATsecs = getushort(b50->bpbFATsecs);
	pmp->pm_SecPerTrack = getushort(b50->bpbSecPerTrack);
	pmp->pm_Heads = getushort(b50->bpbHeads);
	pmp->pm_Media = b50->bpbMedia;

	/* calculate the ratio of sector size to DEV_BSIZE */
	pmp->pm_BlkPerSec = pmp->pm_BytesPerSec / DEV_BSIZE;

	/*
	 * We don't check pm_Heads nor pm_SecPerTrack, because
	 * these may not be set for EFI file systems. We don't
	 * use these anyway, so we're unaffected if they are
	 * invalid.
	 */
	if (!pmp->pm_BytesPerSec || !SecPerClust) {
		error = EINVAL;
		goto error_exit;
	}

	if (pmp->pm_Sectors == 0) {
		pmp->pm_HiddenSects = getulong(b50->bpbHiddenSecs);
		pmp->pm_HugeSectors = getulong(b50->bpbHugeSectors);
	} else {
		pmp->pm_HiddenSects = getushort(b33->bpbHiddenSecs);
		pmp->pm_HugeSectors = pmp->pm_Sectors;
	}

	if (pmp->pm_RootDirEnts == 0) {
		if (pmp->pm_FATsecs
		    || getushort(b710->bpbFSVers)) {
			error = EINVAL;
			kprintf("mountmsdosfs(): bad FAT32 filesystem\n");
			goto error_exit;
		}
		pmp->pm_fatmask = FAT32_MASK;
		pmp->pm_fatmult = 4;
		pmp->pm_fatdiv = 1;
		pmp->pm_FATsecs = getulong(b710->bpbBigFATsecs);
		if (getushort(b710->bpbExtFlags) & FATMIRROR)
			pmp->pm_curfat = getushort(b710->bpbExtFlags) & FATNUM;
		else
			pmp->pm_flags |= MSDOSFS_FATMIRROR;
	} else
		pmp->pm_flags |= MSDOSFS_FATMIRROR;

	/*
	 * Check a few values (could do some more):
	 * - logical sector size: power of 2, >= block size
	 * - sectors per cluster: power of 2, >= 1
	 * - number of sectors:   >= 1, <= size of partition
	 * - number of FAT sectors: >= 1
	 */
	if ( (SecPerClust == 0)
	  || (SecPerClust & (SecPerClust - 1))
	  || (pmp->pm_BytesPerSec < DEV_BSIZE)
	  || (pmp->pm_BytesPerSec & (pmp->pm_BytesPerSec - 1))
	  || (pmp->pm_HugeSectors == 0)
	  || (pmp->pm_FATsecs == 0)
	  || (SecPerClust * pmp->pm_BlkPerSec > MAXBSIZE / DEV_BSIZE)) {
		error = EINVAL;
		goto error_exit;
	}

	pmp->pm_HugeSectors *= pmp->pm_BlkPerSec;
	pmp->pm_HiddenSects *= pmp->pm_BlkPerSec;	/* XXX not used? */
	pmp->pm_FATsecs     *= pmp->pm_BlkPerSec;
	SecPerClust         *= pmp->pm_BlkPerSec;

	pmp->pm_fatblk = pmp->pm_ResSectors * pmp->pm_BlkPerSec;

	if (FAT32(pmp)) {
		pmp->pm_rootdirblk = getulong(b710->bpbRootClust);
		pmp->pm_firstcluster = pmp->pm_fatblk
			+ (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_fsinfo = getushort(b710->bpbFSInfo) * pmp->pm_BlkPerSec;
	} else {
		pmp->pm_rootdirblk = pmp->pm_fatblk +
			(pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_rootdirsize = howmany(pmp->pm_RootDirEnts *
			sizeof(struct direntry), DEV_BSIZE); /* in blocks */
		pmp->pm_firstcluster = pmp->pm_rootdirblk + pmp->pm_rootdirsize;
	}

	pmp->pm_maxcluster = (pmp->pm_HugeSectors - pmp->pm_firstcluster) /
	    SecPerClust + 1;

	if (pmp->pm_fatmask == 0) {
		if (pmp->pm_maxcluster
		    <= ((CLUST_RSRVD - CLUST_FIRST) & FAT12_MASK)) {
			/*
			 * This will usually be a floppy disk. This size makes
			 * sure that one FAT entry will not be split across
			 * multiple blocks.
			 */
			pmp->pm_fatmask = FAT12_MASK;
			pmp->pm_fatmult = 3;
			pmp->pm_fatdiv = 2;
		} else {
			pmp->pm_fatmask = FAT16_MASK;
			pmp->pm_fatmult = 2;
			pmp->pm_fatdiv = 1;
		}
	}

	clusters = ((pmp->pm_FATsecs * DEV_BSIZE) / pmp->pm_fatmult) *
		pmp->pm_fatdiv;
	if (pmp->pm_maxcluster >= clusters) {
		kprintf("Warning: number of clusters (%ld) exceeds FAT "
		    "capacity (%ld)\n", pmp->pm_maxcluster + 1, clusters);
		pmp->pm_maxcluster = clusters - 1;
	}

	if (FAT12(pmp))
		pmp->pm_fatblocksize = 3 * 512;
	else
		pmp->pm_fatblocksize = PAGE_SIZE;
	pmp->pm_fatblocksize = roundup(pmp->pm_fatblocksize,
	    pmp->pm_BytesPerSec);
	pmp->pm_fatblocksec = pmp->pm_fatblocksize / DEV_BSIZE;
	pmp->pm_bnshift = DEV_BSHIFT;

	/*
	 * Compute mask and shift value for isolating cluster relative byte
	 * offsets and cluster numbers from a file offset.
	 */
	pmp->pm_bpcluster = SecPerClust * DEV_BSIZE;
	pmp->pm_crbomask = pmp->pm_bpcluster - 1;
	pmp->pm_cnshift = ffs(pmp->pm_bpcluster) - 1;

	/*
	 * Check for valid cluster size
	 * must be a power of 2
	 */
	if (pmp->pm_bpcluster ^ (1 << pmp->pm_cnshift)) {
		error = EINVAL;
		goto error_exit;
	}

	/*
	 * Release the bootsector buffer.
	 */
	bp->b_flags |= B_RELBUF;
	brelse(bp);
	bp = NULL;

	/*
	 * Check the fsinfo sector if we have one.  Silently fix up our
	 * in-core copy of fp->fsinxtfree if it is unknown (0xffffffff)
	 * or too large.  Ignore fp->fsinfree for now, since we need to
	 * read the entire FAT anyway to fill the inuse map.
	 */
	if (pmp->pm_fsinfo) {
		struct fsinfo *fp;

		if ((error = bread(devvp, de_bn2doff(pmp, pmp->pm_fsinfo),
		    pmp->pm_BytesPerSec, &bp)) != 0)
			goto error_exit;
		fp = (struct fsinfo *)bp->b_data;
		if (!memcmp(fp->fsisig1, "RRaA", 4) &&
		    !memcmp(fp->fsisig2, "rrAa", 4) &&
		    !memcmp(fp->fsisig3, "\0\0\125\252", 4)) {
			pmp->pm_nxtfree = getulong(fp->fsinxtfree);
			if (pmp->pm_nxtfree > pmp->pm_maxcluster)
				pmp->pm_nxtfree = CLUST_FIRST;
		} else
			pmp->pm_fsinfo = 0;
		bp->b_flags |= B_RELBUF;
		brelse(bp);
		bp = NULL;
	}

	/*
	 * Finish initializing pmp->pm_nxtfree (just in case the first few
	 * sectors aren't properly reserved in the FAT).  This completes
	 * the fixup for fp->fsinxtfree, and fixes up the zero-initialized
	 * value if there is no fsinfo.  We will use pmp->pm_nxtfree
	 * internally even if there is no fsinfo.
	 */
	if (pmp->pm_nxtfree < CLUST_FIRST)
		pmp->pm_nxtfree = CLUST_FIRST;

	/*
	 * Allocate memory for the bitmap of allocated clusters, and then
	 * fill it in.
	 */
	pmp->pm_inusemap = kmalloc(howmany(pmp->pm_maxcluster + 1, N_INUSEBITS)
				   * sizeof(*pmp->pm_inusemap),
				   M_MSDOSFSFAT, M_WAITOK);

	/*
	 * fillinusemap() needs pm_devvp.
	 */
	pmp->pm_devvp = devvp;
	pmp->pm_dev = dev;

	/*
	 * Have the inuse map filled in.
	 */
	MSDOSFS_LOCK_MP(pmp);
	error = fillinusemap(pmp);
	MSDOSFS_UNLOCK_MP(pmp);
	if (error != 0)
		goto error_exit;

	/*
	 * If they want FAT updates to be synchronous then let them suffer
	 * the performance degradation in exchange for the on disk copy of
	 * the FAT being correct just about all the time.  I suppose this
	 * would be a good thing to turn on if the kernel is still flakey.
	 */
	if (mp->mnt_flag & MNT_SYNCHRONOUS)
		pmp->pm_flags |= MSDOSFSMNT_WAITONFAT;

	/*
	 * Finish up.
	 */
	if (ronly)
		pmp->pm_flags |= MSDOSFSMNT_RONLY;
	else
		pmp->pm_fmod = 1;
	mp->mnt_data = (qaddr_t) pmp;
	mp->mnt_stat.f_fsid.val[0] = devid_from_dev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_flag |= MNT_LOCAL;
	vfs_add_vnodeops(mp, &msdosfs_vnode_vops, &mp->mnt_vn_norm_ops);
	dev->si_mountpoint = mp;

	return (0);

error_exit:
	if (bp) {
		bp->b_flags |= B_RELBUF;
		brelse(bp);
	}
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	VOP_CLOSE(devvp, ronly ? FREAD : FREAD | FWRITE, NULL);
	vn_unlock(devvp);
	if (pmp) {
		if (pmp->pm_inusemap)
			kfree(pmp->pm_inusemap, M_MSDOSFSFAT);
		kfree(pmp, M_MSDOSFSMNT);
		mp->mnt_data = (qaddr_t)0;
	}
	return (error);
}

/*
 * Unmount the filesystem described by mp.
 */
static int
msdosfs_unmount(struct mount *mp, int mntflags)
{
	struct msdosfsmount *pmp;
	int error, flags;
#ifdef MSDOSFS_DEBUG
	struct vnode *vp;
#endif
	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 0, flags);
	if (error)
		return error;

	pmp = VFSTOMSDOSFS(mp);
	pmp->pm_devvp->v_rdev->si_mountpoint = NULL;

	if (pmp->pm_flags & MSDOSFSMNT_KICONV && msdosfs_iconv) {
		if (pmp->pm_w2u)
			msdosfs_iconv->close(pmp->pm_w2u);
		if (pmp->pm_u2w)
			msdosfs_iconv->close(pmp->pm_u2w);
		if (pmp->pm_d2u)
			msdosfs_iconv->close(pmp->pm_d2u);
		if (pmp->pm_u2d)
			msdosfs_iconv->close(pmp->pm_u2d);
	}

#ifdef MSDOSFS_DEBUG
	vp = pmp->pm_devvp;
	kprintf("msdosfs_umount(): just before calling VOP_CLOSE()\n");
	kprintf("flag %08x, refcnt 0x%08x, writecount %d, auxrefs 0x%08x\n",
		vp->v_flag, vp->v_refcnt, vp->v_writecount, vp->v_auxrefs);
	kprintf("mount %p, op %p\n", vp->v_mount, vp->v_ops);
	kprintf("mount %p\n", vp->v_mount);
	kprintf("cleanblkhd %p, dirtyblkhd %p, numoutput %d, type %d\n",
		RB_ROOT(&vp->v_rbclean_tree), RB_ROOT(&vp->v_rbdirty_tree),
		bio_track_active(&vp->v_track_write), vp->v_type);
	kprintf("union %p, tag %d, data[0] %08x, data[1] %08x\n",
		vp->v_socket, vp->v_tag, ((u_int *)vp->v_data)[0],
		((u_int *)vp->v_data)[1]);
#endif

	vn_lock(pmp->pm_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_CLOSE(pmp->pm_devvp,
		    pmp->pm_flags & MSDOSFSMNT_RONLY ? FREAD : FREAD | FWRITE,
		    NULL);
	vn_unlock(pmp->pm_devvp);
	vrele(pmp->pm_devvp);
	kfree(pmp->pm_inusemap, M_MSDOSFSFAT);
	kfree(pmp, M_MSDOSFSMNT);
	mp->mnt_data = (qaddr_t)0;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (error);
}

static int
msdosfs_root(struct mount *mp, struct vnode **vpp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct denode *ndep;
	int error;

	mprintf("msdosfs_root(); mp %p, pmp %p\n", mp, pmp);
	error = deget(pmp, MSDOSFSROOT, MSDOSFSROOT_OFS, &ndep);
	if (error)
		return (error);
	*vpp = DETOV(ndep);
	return (0);
}

static int
msdosfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct msdosfsmount *pmp;

	pmp = VFSTOMSDOSFS(mp);
	sbp->f_bsize = pmp->pm_bpcluster;
	sbp->f_iosize = pmp->pm_bpcluster;
	sbp->f_blocks = pmp->pm_maxcluster + 1;
	sbp->f_bfree = pmp->pm_freeclustercount;
	sbp->f_bavail = pmp->pm_freeclustercount;
	sbp->f_files = pmp->pm_RootDirEnts;	/* XXX */
	sbp->f_ffree = 0;	/* what to put in here? */
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		memcpy(sbp->f_mntfromname, mp->mnt_stat.f_mntfromname, MNAMELEN);
	}
	strncpy(sbp->f_fstypename, mp->mnt_vfc->vfc_name, MFSNAMELEN);
	return (0);
}

static int
msdosfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct msdosfsmount *pmp;

	pmp = VFSTOMSDOSFS(mp);
	sbp->f_bsize = pmp->pm_bpcluster;
	sbp->f_frsize = pmp->pm_bpcluster;
	sbp->f_blocks = pmp->pm_maxcluster + 1;
	sbp->f_bfree = pmp->pm_freeclustercount;
	sbp->f_bavail = pmp->pm_freeclustercount;
	sbp->f_files = pmp->pm_RootDirEnts;	/* XXX */
	sbp->f_ffree = 0;	/* what to put in here? */
	if (sbp != &mp->mnt_vstat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
	}
	return (0);
}

struct scaninfo {
	int rescan;
	int allerror;
	int waitfor;
};

static int msdosfs_sync_scan(struct mount *mp, struct vnode *vp, void *data);

/*
 * If we have an FSInfo block, update it.
 */
static int
msdosfs_fsiflush(struct msdosfsmount *pmp, int waitfor)
{
	struct fsinfo *fp;
	struct buf *bp;
	int error;

	MSDOSFS_LOCK_MP(pmp);
	if (pmp->pm_fsinfo == 0 || (pmp->pm_flags & MSDOSFS_FSIMOD) == 0) {
		error = 0;
		goto unlock;
	}
	error = bread(pmp->pm_devvp, de_bn2doff(pmp, pmp->pm_fsinfo),
	    pmp->pm_BytesPerSec, &bp);
	if (error != 0) {
		brelse(bp);
		goto unlock;
	}

	fp = (struct fsinfo *)bp->b_data;
	putulong(fp->fsinfree, pmp->pm_freeclustercount);
	putulong(fp->fsinxtfree, pmp->pm_nxtfree);
	pmp->pm_flags &= ~MSDOSFS_FSIMOD;
	if (waitfor == MNT_WAIT)
		error = bwrite(bp);
	else
		bawrite(bp);
unlock:
	MSDOSFS_UNLOCK_MP(pmp);
	return (error);
}

static int
msdosfs_sync(struct mount *mp, int waitfor)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct scaninfo scaninfo;
	int error;

	/*
	 * If we ever switch to not updating all of the FATs all the time,
	 * this would be the place to update them from the first one.
	 */
	if (pmp->pm_fmod != 0) {
		if (pmp->pm_flags & MSDOSFSMNT_RONLY) {
			panic("msdosfs_sync: rofs mod");
		} else {
			/* update FATs here */
		}
	}
	/*
	 * Write back each (modified) denode.
	 */
	scaninfo.allerror = 0;
	scaninfo.rescan = 1;
	while (scaninfo.rescan) {
		scaninfo.rescan = 0;
		vmntvnodescan(mp, VMSC_GETVP|VMSC_NOWAIT, NULL,
			      msdosfs_sync_scan, &scaninfo);
	}

	/*
	 * Flush filesystem control info.
	 */
	if ((waitfor & MNT_LAZY) == 0) {
		vn_lock(pmp->pm_devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(pmp->pm_devvp, waitfor, 0)) != 0)
			scaninfo.allerror = error;
		vn_unlock(pmp->pm_devvp);
	}

	error = msdosfs_fsiflush(pmp, waitfor);
	if (error != 0)
		scaninfo.allerror = error;
	return (scaninfo.allerror);
}

static int
msdosfs_sync_scan(struct mount *mp, struct vnode *vp, void *data)
{
	struct scaninfo *info = data;
	struct denode *dep;
	int error;

	dep = VTODE(vp);
	if (vp->v_type == VNON || vp->v_type == VBAD ||
	    ((dep->de_flag &
	    (DE_ACCESS | DE_CREATE | DE_UPDATE | DE_MODIFIED)) == 0 &&
	    (RB_EMPTY(&vp->v_rbdirty_tree) || (info->waitfor & MNT_LAZY)))) {
		return(0);
	}
	if ((error = VOP_FSYNC(vp, info->waitfor, 0)) != 0)
		info->allerror = error;
	return(0);
}

static int
msdosfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct defid *defhp = (struct defid *) fhp;
	struct denode *dep;
	int error;

	error = deget(pmp, defhp->defid_dirclust, defhp->defid_dirofs, &dep);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	*vpp = DETOV(dep);
	return (0);
}

static int
msdosfs_checkexp(struct mount *mp, struct sockaddr *nam, int *exflagsp,
		 struct ucred **credanonp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct netcred *np;

	np = vfs_export_lookup(mp, &pmp->pm_export, nam);
	if (np == NULL)
		return (EACCES);
	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}

static int
msdosfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct denode *dep;
	struct defid *defhp;

	dep = VTODE(vp);
	defhp = (struct defid *)fhp;
	defhp->defid_len = sizeof(struct defid);
	defhp->defid_dirclust = dep->de_dirclust;
	defhp->defid_dirofs = dep->de_diroffset;
	/* defhp->defid_gen = dep->de_gen; */
	return (0);
}

static struct vfsops msdosfs_vfsops = {
	.vfs_flags =		0,
	.vfs_mount =		msdosfs_mount,
	.vfs_root =		msdosfs_root,
	.vfs_statfs =		msdosfs_statfs,
	.vfs_statvfs =		msdosfs_statvfs,
	.vfs_sync =		msdosfs_sync,
	.vfs_fhtovp =		msdosfs_fhtovp,
	.vfs_checkexp =		msdosfs_checkexp,
	.vfs_vptofh =		msdosfs_vptofh,
	.vfs_init =		msdosfs_init,
	.vfs_uninit =		msdosfs_uninit,
	.vfs_unmount =		msdosfs_unmount,
};

VFS_SET(msdosfs_vfsops, msdos, 0);
MODULE_VERSION(msdos, 1);
