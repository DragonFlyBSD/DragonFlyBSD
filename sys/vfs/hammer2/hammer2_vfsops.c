/*-
 * Copyright (c) 2011, 2012 The DragonFly Project.  All rights reserved.
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
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/vfsops.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"

struct hammer2_sync_info {
	int error;
	int waitfor;
};

static int hammer2_vfs_init(struct vfsconf *conf);
static int hammer2_vfs_mount(struct mount *mp, char *path, caddr_t data,
				struct ucred *cred);
static int hammer2_remount(struct mount *, char *, struct vnode *,
				struct ucred *);
static int hammer2_vfs_unmount(struct mount *mp, int mntflags);
static int hammer2_vfs_root(struct mount *mp, struct vnode **vpp);
static int hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int hammer2_vfs_sync(struct mount *mp, int waitfor);
static int hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
static int hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
static int hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp);
static int hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
				int *exflagsp, struct ucred **credanonp);

static int hammer2_install_volume_header(hammer2_mount_t *hmp);
static int hammer2_sync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	.vfs_init	= hammer2_vfs_init,
	.vfs_sync	= hammer2_vfs_sync,
	.vfs_mount	= hammer2_vfs_mount,
	.vfs_unmount	= hammer2_vfs_unmount,
	.vfs_root 	= hammer2_vfs_root,
	.vfs_statfs	= hammer2_vfs_statfs,
	.vfs_statvfs	= hammer2_vfs_statvfs,
	.vfs_vget	= hammer2_vfs_vget,
	.vfs_vptofh	= hammer2_vfs_vptofh,
	.vfs_fhtovp	= hammer2_vfs_fhtovp,
	.vfs_checkexp	= hammer2_vfs_checkexp
};

MALLOC_DEFINE(M_HAMMER2, "HAMMER2-mount", "");

VFS_SET(hammer2_vfsops, hammer2, 0);
MODULE_VERSION(hammer2, 1);

static
int
hammer2_vfs_init(struct vfsconf *conf)
{
	int error;

	error = 0;

	if (HAMMER2_BLOCKREF_BYTES != sizeof(struct hammer2_blockref))
		error = EINVAL;
	if (HAMMER2_INODE_BYTES != sizeof(struct hammer2_inode_data))
		error = EINVAL;
	if (HAMMER2_ALLOCREF_BYTES != sizeof(struct hammer2_allocref))
		error = EINVAL;
	if (HAMMER2_VOLUME_BYTES != sizeof(struct hammer2_volume_data))
		error = EINVAL;

	if (error)
		kprintf("HAMMER2 structure size mismatch; cannot continue.\n");

	return (error);
}

/*
 * Mount or remount HAMMER2 fileystem from physical media
 *
 *	mountroot
 *		mp		mount point structure
 *		path		NULL
 *		data		<unused>
 *		cred		<unused>
 *
 *	mount
 *		mp		mount point structure
 *		path		path to mount point
 *		data		pointer to argument structure in user space
 *			volume	volume path (device@LABEL form)
 *			hflags	user mount flags
 *		cred		user credentials
 *
 * RETURNS:	0	Success
 *		!0	error number
 */
static
int
hammer2_vfs_mount(struct mount *mp, char *path, caddr_t data,
	      struct ucred *cred)
{
	struct hammer2_mount_info info;
	hammer2_mount_t *hmp;
	hammer2_key_t lhc;
	struct vnode *devvp;
	struct nlookupdata nd;
	hammer2_chain_t *parent;
	hammer2_chain_t *schain;
	hammer2_chain_t *rchain;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev;
	char *label;
	int ronly = 1;
	int error;

	hmp = NULL;
	dev = NULL;
	label = NULL;
	devvp = NULL;

	kprintf("hammer2_mount\n");

	if (path == NULL) {
		/*
		 * Root mount
		 */
		return (EOPNOTSUPP);
	} else {
		/*
		 * Non-root mount or updating a mount
		 */
		error = copyin(data, &info, sizeof(info));
		if (error)
			return (error);

		error = copyinstr(info.volume, devstr, MNAMELEN - 1, &done);
		if (error)
			return (error);

		/* Extract device and label */
		dev = devstr;
		label = strchr(devstr, '@');
		if (label == NULL ||
		    ((label + 1) - dev) > done) {
			return (EINVAL);
		}
		*label = '\0';
		label++;
		if (*label == '\0')
			return (EINVAL);

		if (mp->mnt_flag & MNT_UPDATE) {
			/* Update mount */
			/* HAMMER2 implements NFS export via mountctl */
			hmp = MPTOH2(mp);
			devvp = hmp->devvp;
			error = hammer2_remount(mp, path, devvp, cred);
			return error;
		}
	}

	/*
	 * New non-root mount
	 */
	/* Lookup name and verify it refers to a block device */
	error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);

	if (error == 0) {
		if (vn_isdisk(devvp, &error))
			error = vfs_mountedon(devvp);
	}
	if (error == 0 && vcount(devvp) > 0)
		error = EBUSY;

	/*
	 * Now open the device
	 */
	if (error == 0) {
		ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(devvp, V_SAVE, 0, 0);
		if (error == 0) {
			error = VOP_OPEN(devvp, ronly ? FREAD : FREAD | FWRITE,
					 FSCRED, NULL);
		}
		vn_unlock(devvp);
	}
	if (error && devvp) {
		vrele(devvp);
		devvp = NULL;
	}
	if (error)
		return error;

	/*
	 * Block device opened successfully, finish initializing the
	 * mount structure.
	 *
	 * From this point on we have to call hammer2_unmount() on failure.
	 */
	hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)hmp;
	hmp->mp = mp;
	hmp->ronly = ronly;
	hmp->devvp = devvp;
	kmalloc_create(&hmp->minode, "HAMMER2-inodes");
	kmalloc_create(&hmp->mchain, "HAMMER2-chains");
	
	mp->mnt_flag = MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;	/* all entry pts are SMP */

	/*
	 * vchain setup. vchain.data is special cased to NULL.  vchain.refs
	 * is initialized and will never drop to 0.
	 */
	hmp->vchain.bref.type = HAMMER2_BREF_TYPE_VOLUME;
	hmp->vchain.refs = 1;
	hmp->vchain.data = (void *)&hmp->voldata;
	hmp->vchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
	/* hmp->vchain.u.xxx is left NULL */
	lockinit(&hmp->vchain.lk, "volume", 0, LK_CANRECURSE);

	/*
	 * Install the volume header
	 */
	error = hammer2_install_volume_header(hmp);
	if (error) {
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return error;
	}

	/*
	 * required mount structure initializations
	 */
	mp->mnt_stat.f_iosize = HAMMER2_PBUFSIZE;
	mp->mnt_stat.f_bsize = HAMMER2_PBUFSIZE;

	mp->mnt_vstat.f_frsize = HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;

	/*
	 * First locate the super-root inode, which is key 0 relative to the
	 * volume header's blockset.
	 *
	 * Then locate the root inode by scanning the directory keyspace
	 * represented by the label.
	 */
	lhc = hammer2_dirhash(label, strlen(label));
	parent = &hmp->vchain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);
	schain = hammer2_chain_lookup(hmp, &parent,
				      HAMMER2_SROOT_KEY, HAMMER2_SROOT_KEY, 0);
	hammer2_chain_put(hmp, parent);
	if (schain == NULL) {
		kprintf("hammer2_mount: invalid super-root\n");
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EINVAL;
	}

	parent = schain;
	hammer2_chain_ref(hmp, parent);	/* parent: lock+ref, schain: ref */
	rchain = hammer2_chain_lookup(hmp, &parent,
				      lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				      0);
	while (rchain) {
		if (rchain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    rchain->u.ip &&
		    strcmp(label, rchain->data->ipdata.filename) == 0) {
			break;
		}
		rchain = hammer2_chain_next(hmp, &parent, rchain,
					    lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					    0);
	}
	hammer2_chain_put(hmp, parent);
	if (rchain == NULL) {
		kprintf("hammer2_mount: root label not found\n");
		hammer2_chain_drop(hmp, schain);
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EINVAL;
	}
	hammer2_chain_unlock(hmp, rchain); /* rchain: ref */

	hmp->schain = schain;		/* left held & unlocked */
	hmp->rchain = rchain;		/* left held & unlocked */
	hmp->iroot = rchain->u.ip;	/* implied hold from rchain */
	kprintf("iroot %p\n", rchain->u.ip);

	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &hammer2_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &hammer2_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &hammer2_fifo_vops, &mp->mnt_vn_fifo_ops);

	copyinstr(info.volume, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	copyinstr(path, mp->mnt_stat.f_mntonname,
		  sizeof(mp->mnt_stat.f_mntonname) - 1,
		  &size);

	hammer2_vfs_statfs(mp, &mp->mnt_stat, cred);

	return 0;
}

static
int
hammer2_remount(struct mount *mp, char *path, struct vnode *devvp,
                struct ucred *cred)
{
	return (0);
}

static
int
hammer2_vfs_unmount(struct mount *mp, int mntflags)
{
	hammer2_mount_t *hmp;
	int flags;
	int error = 0;
	int ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
	struct vnode *devvp;

	kprintf("hammer2_unmount\n");

	hmp = MPTOH2(mp);
	flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	hammer2_mount_exlock(hmp);

	/*
	 * If mount initialization proceeded far enough we must flush
	 * its vnodes.
	 */
	kprintf("iroot %p\n", hmp->iroot);
	if (hmp->iroot)
		error = vflush(mp, 0, flags);

	if (error)
		return error;

	/*
	 * Work to do:
	 *	1) Wait on the flusher having no work; heat up if needed
	 *	2) Scan inode RB tree till all the inodes are free
	 *	3) Destroy the kmalloc inode zone
	 *	4) Free the mount point
	 */
	hmp->iroot = NULL;
	if (hmp->rchain) {
		KKASSERT(hmp->rchain->refs == 1);
		hammer2_chain_drop(hmp, hmp->rchain);
		hmp->rchain = NULL;
	}
	if (hmp->schain) {
		KKASSERT(hmp->schain->refs == 1);
		hammer2_chain_drop(hmp, hmp->schain);
		hmp->schain = NULL;
	}
	if ((devvp = hmp->devvp) != NULL) {
		vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
		hmp->devvp = NULL;
		VOP_CLOSE(devvp, (ronly ? FREAD : FREAD|FWRITE));
		vrele(devvp);
		devvp = NULL;
	}

	kmalloc_destroy(&hmp->minode);
	kmalloc_destroy(&hmp->mchain);

	hammer2_mount_unlock(hmp);

	mp->mnt_data = NULL;
	hmp->mp = NULL;
	kfree(hmp, M_HAMMER2);

	return (error);
}

static
int
hammer2_vfs_vget(struct mount *mp, struct vnode *dvp,
	     ino_t ino, struct vnode **vpp)
{
	kprintf("hammer2_vget\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vfs_root(struct mount *mp, struct vnode **vpp)
{
	hammer2_mount_t *hmp;
	int error;
	struct vnode *vp;

	kprintf("hammer2_root\n");

	hmp = MPTOH2(mp);
	hammer2_mount_exlock(hmp);
	if (hmp->iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		vp = hammer2_igetv(hmp->iroot, &error);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}
	hammer2_mount_unlock(hmp);

	return (error);
}

static
int
hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	hammer2_mount_t *hmp;

	hmp = MPTOH2(mp);

	mp->mnt_stat.f_files = 10;
	mp->mnt_stat.f_bfree = 10;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;

	*sbp = mp->mnt_stat;
	return (0);
}

static
int
hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	hammer2_mount_t *hmp;

	hmp = MPTOH2(mp);

	mp->mnt_vstat.f_files = 10;
	mp->mnt_vstat.f_bfree = 10;
	mp->mnt_vstat.f_bavail = mp->mnt_stat.f_bfree;

	*sbp = mp->mnt_vstat;
	return (0);
}

/*
 * Sync the entire filesystem; this is called from the filesystem syncer
 * process periodically and whenever a user calls sync(1) on the hammer
 * mountpoint.
 *
 * Currently is actually called from the syncer! \o/
 *
 * This task will have to snapshot the state of the dirty inode chain.
 * From that, it will have to make sure all of the inodes on the dirty
 * chain have IO initiated. We make sure that io is initiated for the root
 * block.
 *
 * If waitfor is set, we wait for media to acknowledge the new rootblock.
 *
 * THINKS: side A vs side B, to have sync not stall all I/O?
 */
static
int
hammer2_vfs_sync(struct mount *mp, int waitfor)
{
	struct hammer2_sync_info info;
	hammer2_mount_t *hmp;
	int flags;
	int error;

	kprintf("hammer2_sync \n");
	hmp = MPTOH2(mp);

	flags = VMSC_GETVP;
	if (waitfor & MNT_LAZY)
		flags |= VMSC_ONEPASS;

	info.error = 0;
	info.waitfor = MNT_NOWAIT;
	vmntvnodescan(mp, flags | VMSC_NOWAIT,
		      hammer2_sync_scan1,
		      hammer2_sync_scan2, &info);
	if (info.error == 0 && (waitfor & MNT_WAIT)) {
		info.waitfor = waitfor;
		    vmntvnodescan(mp, flags,
				  hammer2_sync_scan1,
				  hammer2_sync_scan2, &info);

	}
#if 0
	if (waitfor == MNT_WAIT) {
		/* XXX */
	} else {
		/* XXX */
	}
#endif
	hammer2_chain_lock(hmp, &hmp->vchain);
	hammer2_chain_flush(hmp, &hmp->vchain, NULL);
	hammer2_chain_unlock(hmp, &hmp->vchain);
	error = vinvalbuf(hmp->devvp, V_SAVE, 0, 0);
	if (error == 0) {
		struct buf *bp;

		bp = getpbuf(NULL);
		bp->b_bio1.bio_offset = 0;
		bp->b_bufsize = 0;
		bp->b_bcount = 0;
		bp->b_cmd = BUF_CMD_FLUSH;
		bp->b_bio1.bio_done = biodone_sync;
		bp->b_bio1.bio_flags |= BIO_SYNC;
		vn_strategy(hmp->devvp, &bp->b_bio1);
		biowait(&bp->b_bio1, "h2vol");
		relpbuf(bp, NULL);

		kprintf("flush volume header\n");

		bp = getblk(hmp->devvp, 0, HAMMER2_PBUFSIZE, 0, 0);
		bcopy(&hmp->voldata, bp->b_data, HAMMER2_PBUFSIZE);
		bawrite(bp);
	}
	return (error);
}


static int
hammer2_sync_scan1(struct mount *mp, struct vnode *vp, void *data)
{
	hammer2_inode_t *ip;

	ip = VTOI(vp);
	if (vp->v_type == VNON || ip == NULL ||
	    ((ip->chain.flags & HAMMER2_CHAIN_MODIFIED) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(-1);
	}
	return(0);
}

static int
hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct hammer2_sync_info *info = data;
	hammer2_inode_t *ip;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VNON || vp->v_type == VBAD ||
	    ((ip->chain.flags & HAMMER2_CHAIN_MODIFIED) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(0);
	}
	error = VOP_FSYNC(vp, MNT_NOWAIT, 0);
	if (error)
		info->error = error;
	return(0);
}

static
int
hammer2_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	return (0);
}

static
int
hammer2_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	return (0);
}

static
int
hammer2_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	return (0);
}

/*
 * Support code for hammer2_mount().  Read, verify, and install the volume
 * header into the HMP
 *
 * XXX read four volhdrs and use the one with the highest TID whos CRC
 *     matches.
 *
 * XXX check iCRCs.
 *
 * XXX For filesystems w/ less than 4 volhdrs, make sure to not write to
 *     nonexistant locations.
 *
 * XXX Record selected volhdr and ring updates to each of 4 volhdrs
 */
static
int
hammer2_install_volume_header(hammer2_mount_t *hmp)
{
	hammer2_volume_data_t *vd;
	struct buf *bp;
	hammer2_crc32_t crc0, crc, bcrc0, bcrc;
	int error_reported;
	int error;
	int valid;
	int i;

	error_reported = 0;
	error = 0;
	valid = 0;
	bp = NULL;

	/*
	 * There are up to 4 copies of the volume header (syncs iterate
	 * between them so there is no single master).  We don't trust the
	 * volu_size field so we don't know precisely how large the filesystem
	 * is, so depend on the OS to return an error if we go beyond the
	 * block device's EOF.
	 */
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; i++) {
		error = bread(hmp->devvp, i * HAMMER2_RESERVE_BYTES64, 
			      HAMMER2_VOLUME_BYTES, &bp);
		if (error) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		vd = (struct hammer2_volume_data *)bp->b_data;
		if (vd->magic != HAMMER2_VOLUME_ID_HBO) 
			continue;

		crc = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
		crc0 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC0_OFF,
				      HAMMER2_VOLUME_ICRC0_SIZE);
		bcrc = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
		bcrc0 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC1_OFF,
				       HAMMER2_VOLUME_ICRC1_SIZE);
		if ((crc0 != crc) || (bcrc0 != bcrc)) {
			kprintf("hammer2 volume header crc "
				"mismatch copy #%d\t%08x %08x",
				i, crc0, crc);
			error_reported = 1;
			brelse(bp);
			bp = NULL;
			continue;
		}
		if (valid == 0 || hmp->voldata.last_tid < vd->last_tid) {
			valid = 1;
			hmp->voldata = *vd;
		}
		brelse(bp);
		bp = NULL;
	}
	if (valid) {
		error = 0;
		if (error_reported)
			kprintf("hammer2: a valid volume header was found\n");
	} else {
		error = EINVAL;
		kprintf("hammer2: no valid volume headers found!\n");
	}
	return (error);
}

