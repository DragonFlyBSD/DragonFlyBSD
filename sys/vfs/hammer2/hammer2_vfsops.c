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
#include <sys/sysctl.h>
#include <sys/socket.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"
#include "hammer2_network.h"

struct hammer2_sync_info {
	int error;
	int waitfor;
};

TAILQ_HEAD(hammer2_mntlist, hammer2_mount);
static struct hammer2_mntlist hammer2_mntlist;
static struct lock hammer2_mntlk;

int hammer2_debug;
int hammer2_cluster_enable = 1;
int hammer2_hardlink_enable = 1;
long hammer2_iod_file_read;
long hammer2_iod_meta_read;
long hammer2_iod_indr_read;
long hammer2_iod_file_write;
long hammer2_iod_meta_write;
long hammer2_iod_indr_write;
long hammer2_iod_volu_write;
long hammer2_ioa_file_read;
long hammer2_ioa_meta_read;
long hammer2_ioa_indr_read;
long hammer2_ioa_file_write;
long hammer2_ioa_meta_write;
long hammer2_ioa_indr_write;
long hammer2_ioa_volu_write;

SYSCTL_NODE(_vfs, OID_AUTO, hammer2, CTLFLAG_RW, 0, "HAMMER2 filesystem");

SYSCTL_INT(_vfs_hammer2, OID_AUTO, debug, CTLFLAG_RW,
	   &hammer2_debug, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, cluster_enable, CTLFLAG_RW,
	   &hammer2_cluster_enable, 0, "");
SYSCTL_INT(_vfs_hammer2, OID_AUTO, hardlink_enable, CTLFLAG_RW,
	   &hammer2_hardlink_enable, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_read, CTLFLAG_RW,
	   &hammer2_iod_file_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_read, CTLFLAG_RW,
	   &hammer2_iod_meta_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_read, CTLFLAG_RW,
	   &hammer2_iod_indr_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_write, CTLFLAG_RW,
	   &hammer2_iod_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_write, CTLFLAG_RW,
	   &hammer2_iod_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_write, CTLFLAG_RW,
	   &hammer2_iod_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_write, CTLFLAG_RW,
	   &hammer2_iod_volu_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_read, CTLFLAG_RW,
	   &hammer2_ioa_file_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_read, CTLFLAG_RW,
	   &hammer2_ioa_meta_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_read, CTLFLAG_RW,
	   &hammer2_ioa_indr_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_write, CTLFLAG_RW,
	   &hammer2_ioa_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_write, CTLFLAG_RW,
	   &hammer2_ioa_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_write, CTLFLAG_RW,
	   &hammer2_ioa_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_volu_write, CTLFLAG_RW,
	   &hammer2_ioa_volu_write, 0, "");

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

static void hammer2_cluster_thread_rd(void *arg);
static void hammer2_cluster_thread_wr(void *arg);
static int hammer2_msg_conn_reply(hammer2_state_t *state, hammer2_msg_t *msg);
static int hammer2_msg_span_reply(hammer2_state_t *state, hammer2_msg_t *msg);
static int hammer2_msg_lnk_rcvmsg(hammer2_msg_t *msg);
static void hammer2_drain_msgq(hammer2_pfsmount_t *pmp);

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

	lockinit(&hammer2_mntlk, "mntlk", 0, 0);
	TAILQ_INIT(&hammer2_mntlist);

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
	hammer2_pfsmount_t *pmp;
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
	int create_hmp;
	int error;

	hmp = NULL;
	pmp = NULL;
	dev = NULL;
	label = NULL;
	devvp = NULL;

	kprintf("hammer2_mount\n");

	if (path == NULL) {
		/*
		 * Root mount
		 */
		bzero(&info, sizeof(info));
		info.cluster_fd = -1;
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
			hmp = MPTOHMP(mp);
			devvp = hmp->devvp;
			error = hammer2_remount(mp, path, devvp, cred);
			return error;
		}
	}

	/*
	 * PFS mount
	 *
	 * Lookup name and verify it refers to a block device.
	 */
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

	/*
	 * Determine if the device has already been mounted.  After this
	 * check hmp will be non-NULL if we are doing the second or more
	 * hammer2 mounts from the same device.
	 */
	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);
	TAILQ_FOREACH(hmp, &hammer2_mntlist, mntentry) {
		if (hmp->devvp == devvp)
			break;
	}

	/*
	 * Open the device if this isn't a secondary mount
	 */
	if (hmp) {
		create_hmp = 0;
	} else {
		create_hmp = 1;
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
				error = VOP_OPEN(devvp,
						 ronly ? FREAD : FREAD | FWRITE,
						 FSCRED, NULL);
			}
			vn_unlock(devvp);
		}
		if (error && devvp) {
			vrele(devvp);
			devvp = NULL;
		}
		if (error) {
			lockmgr(&hammer2_mntlk, LK_RELEASE);
			return error;
		}
	}

	/*
	 * Block device opened successfully, finish initializing the
	 * mount structure.
	 *
	 * From this point on we have to call hammer2_unmount() on failure.
	 */
	pmp = kmalloc(sizeof(*pmp), M_HAMMER2, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)pmp;
	pmp->mp = mp;
	kmalloc_create(&pmp->mmsg, "HAMMER2-pfsmsg");
	lockinit(&pmp->msglk, "h2msg", 0, 0);
	TAILQ_INIT(&pmp->msgq);
	RB_INIT(&pmp->staterd_tree);
	RB_INIT(&pmp->statewr_tree);

	if (create_hmp) {
		hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
		hmp->ronly = ronly;
		hmp->devvp = devvp;
		kmalloc_create(&hmp->minode, "HAMMER2-inodes");
		kmalloc_create(&hmp->mchain, "HAMMER2-chains");
		TAILQ_INSERT_TAIL(&hammer2_mntlist, hmp, mntentry);
	}
	ccms_domain_init(&pmp->ccms_dom);
	pmp->hmp = hmp;
	pmp->router.pmp = pmp;
	++hmp->pmp_count;
	lockmgr(&hammer2_mntlk, LK_RELEASE);
	kprintf("hammer2_mount hmp=%p pmpcnt=%d\n", hmp, hmp->pmp_count);
	
	mp->mnt_flag = MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;	/* all entry pts are SMP */

	if (create_hmp) {
		/*
		 * vchain setup. vchain.data is special cased to NULL.
		 * vchain.refs is initialized and will never drop to 0.
		 */
		hmp->vchain.refs = 1;
		hmp->vchain.data = (void *)&hmp->voldata;
		hmp->vchain.bref.type = HAMMER2_BREF_TYPE_VOLUME;
		hmp->vchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
		hmp->vchain.bref_flush = hmp->vchain.bref;
		ccms_cst_init(&hmp->vchain.cst, NULL);
		/* hmp->vchain.u.xxx is left NULL */
		lockinit(&hmp->alloclk, "h2alloc", 0, 0);
		lockinit(&hmp->voldatalk, "voldata", 0, LK_CANRECURSE);

		/*
		 * Install the volume header
		 */
		error = hammer2_install_volume_header(hmp);
		if (error) {
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return error;
		}
	}

	/*
	 * required mount structure initializations
	 */
	mp->mnt_stat.f_iosize = HAMMER2_PBUFSIZE;
	mp->mnt_stat.f_bsize = HAMMER2_PBUFSIZE;

	mp->mnt_vstat.f_frsize = HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;

	/*
	 * Optional fields
	 */
	mp->mnt_iosize_max = MAXPHYS;

	/*
	 * First locate the super-root inode, which is key 0 relative to the
	 * volume header's blockset.
	 *
	 * Then locate the root inode by scanning the directory keyspace
	 * represented by the label.
	 */
	if (create_hmp) {
		parent = &hmp->vchain;
		hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_ALWAYS);
		schain = hammer2_chain_lookup(hmp, &parent,
				      HAMMER2_SROOT_KEY, HAMMER2_SROOT_KEY, 0);
		hammer2_chain_unlock(hmp, parent);
		if (schain == NULL) {
			kprintf("hammer2_mount: invalid super-root\n");
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return EINVAL;
		}
		hammer2_chain_ref(hmp, schain);	/* for hmp->schain */
		hmp->schain = schain;		/* left locked */
	} else {
		schain = hmp->schain;
		hammer2_chain_lock(hmp, schain, HAMMER2_RESOLVE_ALWAYS);
	}

	parent = schain;
	lhc = hammer2_dirhash(label, strlen(label));
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
	hammer2_chain_unlock(hmp, parent);
	if (rchain == NULL) {
		kprintf("hammer2_mount: PFS label not found\n");
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EINVAL;
	}
	if (rchain->flags & HAMMER2_CHAIN_MOUNTED) {
		hammer2_chain_unlock(hmp, rchain);
		kprintf("hammer2_mount: PFS label already mounted!\n");
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EBUSY;
	}
	atomic_set_int(&rchain->flags, HAMMER2_CHAIN_MOUNTED);

	hammer2_chain_ref(hmp, rchain);	/* for pmp->rchain */
	hammer2_chain_unlock(hmp, rchain);
	pmp->rchain = rchain;		/* left held & unlocked */
	pmp->iroot = rchain->u.ip;	/* implied hold from rchain */
	pmp->iroot->pmp = pmp;

	kprintf("iroot %p\n", pmp->iroot);

	/*
	 * Ref the cluster management messaging descriptor.  The mount
	 * program deals with the other end of the communications pipe.
	 */
	pmp->msg_fp = holdfp(curproc->p_fd, info.cluster_fd, -1);
	if (pmp->msg_fp == NULL) {
		kprintf("hammer2_mount: bad cluster_fd!\n");
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EBADF;
	}
	lwkt_create(hammer2_cluster_thread_rd, pmp, &pmp->msgrd_td,
		    NULL, 0, -1, "hammer2-msgrd");
	lwkt_create(hammer2_cluster_thread_wr, pmp, &pmp->msgwr_td,
		    NULL, 0, -1, "hammer2-msgwr");

	/*
	 * Finish setup
	 */
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

	/*
	 * Initial statfs to prime mnt_stat.
	 */
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
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;
	int flags;
	int error = 0;
	int ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
	struct vnode *devvp;

	pmp = MPTOPMP(mp);
	hmp = pmp->hmp;
	flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	hammer2_mount_exlock(hmp);

	/*
	 * If mount initialization proceeded far enough we must flush
	 * its vnodes.
	 */
	if (pmp->iroot)
		error = vflush(mp, 0, flags);

	if (error)
		return error;

	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);
	--hmp->pmp_count;
	kprintf("hammer2_unmount hmp=%p pmpcnt=%d\n", hmp, hmp->pmp_count);

	/*
	 * Flush any left over chains.  The voldata lock is only used
	 * to synchronize against HAMMER2_CHAIN_MODIFIED_AUX.
	 */
	hammer2_voldata_lock(hmp);
	if (hmp->vchain.flags & (HAMMER2_CHAIN_MODIFIED |
				 HAMMER2_CHAIN_MODIFIED_AUX |
				 HAMMER2_CHAIN_SUBMODIFIED)) {
		hammer2_voldata_unlock(hmp);
		hammer2_vfs_sync(mp, MNT_WAIT);
	} else {
		hammer2_voldata_unlock(hmp);
	}
	if (hmp->pmp_count == 0) {
		if (hmp->vchain.flags & (HAMMER2_CHAIN_MODIFIED |
					 HAMMER2_CHAIN_MODIFIED_AUX |
					 HAMMER2_CHAIN_SUBMODIFIED)) {
			kprintf("hammer2_unmount: chains left over after "
				"final sync\n");
			if (hammer2_debug & 0x0010)
				Debugger("entered debugger");
		}
	}

	/*
	 * Cleanup the root and super-root chain elements (which should be
	 * clean).
	 */
	pmp->iroot = NULL;
	if (pmp->rchain) {
		atomic_clear_int(&pmp->rchain->flags, HAMMER2_CHAIN_MOUNTED);
		KKASSERT(pmp->rchain->refs == 1);
		hammer2_chain_drop(hmp, pmp->rchain);
		pmp->rchain = NULL;
	}
	ccms_domain_uninit(&pmp->ccms_dom);

	/*
	 * Ask the cluster controller to go away
	 */
	atomic_set_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_KILL);
	while (pmp->msgrd_td || pmp->msgwr_td) {
		wakeup(&pmp->msg_ctl);
		tsleep(pmp, 0, "clstrkl", hz);
	}

	/*
	 * Drop communications descriptor
	 */
	if (pmp->msg_fp) {
		fdrop(pmp->msg_fp);
		pmp->msg_fp = NULL;
	}

	/*
	 * If no PFS's left drop the master hammer2_mount for the device.
	 */
	if (hmp->pmp_count == 0) {
		if (hmp->schain) {
			KKASSERT(hmp->schain->refs == 1);
			hammer2_chain_drop(hmp, hmp->schain);
			hmp->schain = NULL;
		}

		/*
		 * Finish up with the device vnode
		 */
		if ((devvp = hmp->devvp) != NULL) {
			vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
			hmp->devvp = NULL;
			VOP_CLOSE(devvp, (ronly ? FREAD : FREAD|FWRITE));
			vrele(devvp);
			devvp = NULL;
		}
	}
	hammer2_mount_unlock(hmp);

	pmp->mp = NULL;
	pmp->hmp = NULL;
	mp->mnt_data = NULL;

	kmalloc_destroy(&pmp->mmsg);

	kfree(pmp, M_HAMMER2);
	if (hmp->pmp_count == 0) {
		TAILQ_REMOVE(&hammer2_mntlist, hmp, mntentry);
		kmalloc_destroy(&hmp->minode);
		kmalloc_destroy(&hmp->mchain);
		kfree(hmp, M_HAMMER2);
	}
	lockmgr(&hammer2_mntlk, LK_RELEASE);
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
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;
	int error;
	struct vnode *vp;

	pmp = MPTOPMP(mp);
	hmp = pmp->hmp;
	hammer2_mount_exlock(hmp);
	if (pmp->iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		hammer2_chain_lock(hmp, &pmp->iroot->chain,
				   HAMMER2_RESOLVE_ALWAYS |
				   HAMMER2_RESOLVE_SHARED);
		vp = hammer2_igetv(pmp->iroot, &error);
		hammer2_chain_unlock(hmp, &pmp->iroot->chain);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}
	hammer2_mount_unlock(hmp);

	return (error);
}

/*
 * Filesystem status
 *
 * XXX incorporate pmp->iroot->ip_data.inode_quota and data_quota
 */
static
int
hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;

	pmp = MPTOPMP(mp);
	hmp = MPTOHMP(mp);

	mp->mnt_stat.f_files = pmp->iroot->ip_data.inode_count +
			       pmp->iroot->delta_icount;
	mp->mnt_stat.f_ffree = 0;
	mp->mnt_stat.f_blocks = hmp->voldata.allocator_size / HAMMER2_PBUFSIZE;
	mp->mnt_stat.f_bfree = (hmp->voldata.allocator_size -
				hmp->voldata.allocator_beg) / HAMMER2_PBUFSIZE;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;

	*sbp = mp->mnt_stat;
	return (0);
}

static
int
hammer2_vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;

	pmp = MPTOPMP(mp);
	hmp = MPTOHMP(mp);

	mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_files = pmp->iroot->ip_data.inode_count +
				pmp->iroot->delta_icount;
	mp->mnt_vstat.f_ffree = 0;
	mp->mnt_vstat.f_blocks = hmp->voldata.allocator_size / HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_bfree = (hmp->voldata.allocator_size -
				 hmp->voldata.allocator_beg) / HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_bavail = mp->mnt_vstat.f_bfree;

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
	int haswork;

	hmp = MPTOHMP(mp);

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
	hammer2_chain_lock(hmp, &hmp->vchain, HAMMER2_RESOLVE_ALWAYS);
	if (hmp->vchain.flags & (HAMMER2_CHAIN_MODIFIED |
				 HAMMER2_CHAIN_MODIFIED_AUX |
				 HAMMER2_CHAIN_SUBMODIFIED)) {
		hammer2_chain_flush(hmp, &hmp->vchain, 0);
		haswork = 1;
	} else {
		haswork = 0;
	}
	hammer2_chain_unlock(hmp, &hmp->vchain);

	error = 0;

	if ((waitfor & MNT_LAZY) == 0) {
		waitfor = MNT_NOWAIT;
		vn_lock(hmp->devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(hmp->devvp, waitfor, 0);
		vn_unlock(hmp->devvp);
	}

	if (error == 0 && haswork) {
		struct buf *bp;

		/*
		 * Synchronize the disk before flushing the volume
		 * header.
		 */
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

		/*
		 * Then we can safely flush the volume header.  Volume
		 * data is locked separately to prevent ioctl functions
		 * from deadlocking due to a configuration issue.
		 */
		bp = getblk(hmp->devvp, 0, HAMMER2_PBUFSIZE, 0, 0);
		hammer2_voldata_lock(hmp);
		bcopy(&hmp->voldata, bp->b_data, HAMMER2_PBUFSIZE);
		hammer2_voldata_unlock(hmp);
		bawrite(bp);
	}
	return (error);
}

/*
 * Sync passes.
 *
 * NOTE: We don't test SUBMODIFIED or MOVED here because the fsync code
 *	 won't flush on those flags.  The syncer code above will do a
 *	 general meta-data flush globally that will catch these flags.
 */
static int
hammer2_sync_scan1(struct mount *mp, struct vnode *vp, void *data)
{
	hammer2_inode_t *ip;

	ip = VTOI(vp);
	if (vp->v_type == VNON || ip == NULL ||
	    ((ip->chain.flags & (HAMMER2_CHAIN_MODIFIED |
				 HAMMER2_CHAIN_DIRTYEMBED)) == 0 &&
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
	    ((ip->chain.flags & (HAMMER2_CHAIN_MODIFIED |
				 HAMMER2_CHAIN_DIRTYEMBED)) == 0 &&
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
		error = bread(hmp->devvp, i * HAMMER2_ZONE_BYTES64,
			      HAMMER2_VOLUME_BYTES, &bp);
		if (error) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		vd = (struct hammer2_volume_data *) bp->b_data;
		if ((vd->magic != HAMMER2_VOLUME_ID_HBO) &&
		    (vd->magic != HAMMER2_VOLUME_ID_ABO)) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		if (vd->magic == HAMMER2_VOLUME_ID_ABO) {
			/* XXX: Reversed-endianness filesystem */
			kprintf("hammer2: reverse-endian filesystem detected");
			brelse(bp);
			bp = NULL;
			continue;
		}

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
		if (valid == 0 || hmp->voldata.mirror_tid < vd->mirror_tid) {
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

/*
 * Reconnect using the passed file pointer.  The caller must ref the
 * fp for us.
 */
void
hammer2_cluster_reconnect(hammer2_pfsmount_t *pmp, struct file *fp)
{
	/*
	 * Destroy the current connection
	 */
	atomic_set_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_KILL);
	while (pmp->msgrd_td || pmp->msgwr_td) {
	       wakeup(&pmp->msg_ctl);
	       tsleep(pmp, 0, "clstrkl", hz);
	}

	/*
	 * Drop communications descriptor
	 */
	if (pmp->msg_fp) {
		fdrop(pmp->msg_fp);
		pmp->msg_fp = NULL;
	}
	kprintf("RESTART CONNECTION\n");

	/*
	 * Setup new communications descriptor
	 */
	pmp->msg_ctl = 0;
	pmp->msg_fp = fp;
	pmp->msg_seq = 0;
	lwkt_create(hammer2_cluster_thread_rd, pmp, &pmp->msgrd_td,
		    NULL, 0, -1, "hammer2-msgrd");
	lwkt_create(hammer2_cluster_thread_wr, pmp, &pmp->msgwr_td,
		    NULL, 0, -1, "hammer2-msgwr");
}

/*
 * Cluster controller thread.  Perform messaging functions.  We have one
 * thread for the reader and one for the writer.  The writer handles
 * shutdown requests (which should break the reader thread).
 */
static
void
hammer2_cluster_thread_rd(void *arg)
{
	hammer2_pfsmount_t *pmp = arg;
	hammer2_msg_hdr_t hdr;
	hammer2_msg_t *msg;
	hammer2_state_t *state;
	size_t hbytes;
	int error = 0;

	while ((pmp->msg_ctl & HAMMER2_CLUSTERCTL_KILL) == 0) {
		/*
		 * Retrieve the message from the pipe or socket.
		 */
		error = fp_read(pmp->msg_fp, &hdr, sizeof(hdr),
				NULL, 1, UIO_SYSSPACE);
		if (error)
			break;
		if (hdr.magic != HAMMER2_MSGHDR_MAGIC) {
			kprintf("hammer2: msgrd: bad magic: %04x\n",
				hdr.magic);
			error = EINVAL;
			break;
		}
		hbytes = (hdr.cmd & HAMMER2_MSGF_SIZE) * HAMMER2_MSG_ALIGN;
		if (hbytes < sizeof(hdr) || hbytes > HAMMER2_MSGAUX_MAX) {
			kprintf("hammer2: msgrd: bad header size %zd\n",
				hbytes);
			error = EINVAL;
			break;
		}
		/* XXX messy: mask cmd to avoid allocating state */
		msg = hammer2_msg_alloc(&pmp->router,
					hdr.cmd & HAMMER2_MSGF_BASECMDMASK,
					NULL, NULL);
		msg->any.head = hdr;
		msg->hdr_size = hbytes;
		if (hbytes > sizeof(hdr)) {
			error = fp_read(pmp->msg_fp, &msg->any.head + 1,
					hbytes - sizeof(hdr),
					NULL, 1, UIO_SYSSPACE);
			if (error) {
				kprintf("hammer2: short msg received\n");
				error = EINVAL;
				break;
			}
		}
		msg->aux_size = hdr.aux_bytes * HAMMER2_MSG_ALIGN;
		if (msg->aux_size > HAMMER2_MSGAUX_MAX) {
			kprintf("hammer2: illegal msg payload size %zd\n",
				msg->aux_size);
			error = EINVAL;
			break;
		}
		if (msg->aux_size) {
			msg->aux_data = kmalloc(msg->aux_size, pmp->mmsg,
						M_WAITOK | M_ZERO);
			error = fp_read(pmp->msg_fp, msg->aux_data,
					msg->aux_size,
					NULL, 1, UIO_SYSSPACE);
			if (error) {
				kprintf("hammer2: short msg "
					"payload received\n");
				break;
			}
		}

		/*
		 * State machine tracking, state assignment for msg,
		 * returns error and discard status.  Errors are fatal
		 * to the connection except for EALREADY which forces
		 * a discard without execution.
		 */
		error = hammer2_state_msgrx(msg);
		if (error) {
			/*
			 * Raw protocol or connection error
			 */
			hammer2_msg_free(msg);
			if (error == EALREADY)
				error = 0;
		} else if (msg->state && msg->state->func) {
			/*
			 * Message related to state which already has a
			 * handling function installed for it.
			 */
			error = msg->state->func(msg->state, msg);
			hammer2_state_cleanuprx(msg);
		} else if ((msg->any.head.cmd & HAMMER2_MSGF_PROTOS) ==
			   HAMMER2_MSG_PROTO_LNK) {
			/*
			 * Message related to the LNK protocol set
			 */
			error = hammer2_msg_lnk_rcvmsg(msg);
			hammer2_state_cleanuprx(msg);
		} else if ((msg->any.head.cmd & HAMMER2_MSGF_PROTOS) ==
			   HAMMER2_MSG_PROTO_DBG) {
			/*
			 * Message related to the DBG protocol set
			 */
			error = hammer2_msg_dbg_rcvmsg(msg);
			hammer2_state_cleanuprx(msg);
		} else {
			/*
			 * Other higher-level messages (e.g. vnops)
			 */
			error = hammer2_msg_adhoc_input(msg);
			hammer2_state_cleanuprx(msg);
		}
		msg = NULL;
	}

	if (error)
		kprintf("hammer2: msg read failed error %d\n", error);

	lockmgr(&pmp->msglk, LK_EXCLUSIVE);
	if (msg) {
		if (msg->state && msg->state->msg == msg)
			msg->state->msg = NULL;
		hammer2_msg_free(msg);
	}

	if ((state = pmp->freerd_state) != NULL) {
		pmp->freerd_state = NULL;
		hammer2_state_free(state);
	}

	/*
	 * Shutdown the socket before waiting for the transmit side.
	 *
	 * If we are dying due to e.g. a socket disconnect verses being
	 * killed explicity we have to set KILL in order to kick the tx
	 * side when it might not have any other work to do.  KILL might
	 * already be set if we are in an unmount or reconnect.
	 */
	fp_shutdown(pmp->msg_fp, SHUT_RDWR);

	atomic_set_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_KILL);
	wakeup(&pmp->msg_ctl);

	/*
	 * Wait for the transmit side to drain remaining messages
	 * before cleaning up the rx state.  The transmit side will
	 * set KILLTX and wait for the rx side to completely finish
	 * (set msgrd_td to NULL) before cleaning up any remaining
	 * tx states.
	 */
	lockmgr(&pmp->msglk, LK_RELEASE);
	atomic_set_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_KILLRX);
	wakeup(&pmp->msg_ctl);
	while ((pmp->msg_ctl & HAMMER2_CLUSTERCTL_KILLTX) == 0) {
		wakeup(&pmp->msg_ctl);
		tsleep(pmp, 0, "clstrkw", hz);
	}

	pmp->msgrd_td = NULL;
	/* pmp can be ripped out from under us at this point */
	wakeup(pmp);
	lwkt_exit();
}

static
void
hammer2_cluster_thread_wr(void *arg)
{
	hammer2_pfsmount_t *pmp = arg;
	hammer2_msg_t *msg = NULL;
	hammer2_state_t *state;
	ssize_t res;
	size_t name_len;
	int error = 0;
	int retries = 20;

	/*
	 * Open a LNK_CONN transaction indicating that we want to take part
	 * in the spanning tree algorithm.  Filter explicitly on the PFS
	 * info in the iroot.
	 *
	 * We do not transmit our (only) LNK_SPAN until the other end has
	 * acknowledged our link connection request.
	 *
	 * The transaction remains fully open for the duration of the
	 * connection.
	 */
	msg = hammer2_msg_alloc(&pmp->router, HAMMER2_LNK_CONN |
					      HAMMER2_MSGF_CREATE,
				hammer2_msg_conn_reply, pmp);
	msg->any.lnk_conn.pfs_clid = pmp->iroot->ip_data.pfs_clid;
	msg->any.lnk_conn.pfs_fsid = pmp->iroot->ip_data.pfs_fsid;
	msg->any.lnk_conn.pfs_type = pmp->iroot->ip_data.pfs_type;
	msg->any.lnk_conn.proto_version = HAMMER2_SPAN_PROTO_1;
	msg->any.lnk_conn.peer_type = pmp->hmp->voldata.peer_type;
	msg->any.lnk_conn.peer_mask = 1LLU << HAMMER2_PEER_HAMMER2;
	name_len = pmp->iroot->ip_data.name_len;
	if (name_len >= sizeof(msg->any.lnk_conn.label))
		name_len = sizeof(msg->any.lnk_conn.label) - 1;
	bcopy(pmp->iroot->ip_data.filename, msg->any.lnk_conn.label, name_len);
	pmp->conn_state = msg->state;
	msg->any.lnk_conn.label[name_len] = 0;
	hammer2_msg_write(msg);

	/*
	 * Transmit loop
	 */
	msg = NULL;
	lockmgr(&pmp->msglk, LK_EXCLUSIVE);

	while ((pmp->msg_ctl & HAMMER2_CLUSTERCTL_KILL) == 0 && error == 0) {
		/*
		 * Sleep if no messages pending.  Interlock with flag while
		 * holding msglk.
		 */
		if (TAILQ_EMPTY(&pmp->msgq)) {
			atomic_set_int(&pmp->msg_ctl,
				       HAMMER2_CLUSTERCTL_SLEEPING);
			lksleep(&pmp->msg_ctl, &pmp->msglk, 0, "msgwr", hz);
			atomic_clear_int(&pmp->msg_ctl,
					 HAMMER2_CLUSTERCTL_SLEEPING);
		}

		while ((msg = TAILQ_FIRST(&pmp->msgq)) != NULL) {
			/*
			 * Remove msg from the transmit queue and do
			 * persist and half-closed state handling.
			 */
			TAILQ_REMOVE(&pmp->msgq, msg, qentry);
			lockmgr(&pmp->msglk, LK_RELEASE);

			error = hammer2_state_msgtx(msg);
			if (error == EALREADY) {
				error = 0;
				hammer2_msg_free(msg);
				lockmgr(&pmp->msglk, LK_EXCLUSIVE);
				continue;
			}
			if (error) {
				hammer2_msg_free(msg);
				lockmgr(&pmp->msglk, LK_EXCLUSIVE);
				break;
			}

			/*
			 * Dump the message to the pipe or socket.
			 */
			error = fp_write(pmp->msg_fp, &msg->any, msg->hdr_size,
					 &res, UIO_SYSSPACE);
			if (error || res != msg->hdr_size) {
				if (error == 0)
					error = EINVAL;
				lockmgr(&pmp->msglk, LK_EXCLUSIVE);
				break;
			}
			if (msg->aux_size) {
				error = fp_write(pmp->msg_fp,
						 msg->aux_data, msg->aux_size,
						 &res, UIO_SYSSPACE);
				if (error || res != msg->aux_size) {
					if (error == 0)
						error = EINVAL;
					lockmgr(&pmp->msglk, LK_EXCLUSIVE);
					break;
				}
			}
			hammer2_state_cleanuptx(msg);
			lockmgr(&pmp->msglk, LK_EXCLUSIVE);
		}
	}

	/*
	 * Cleanup messages pending transmission and release msgq lock.
	 */
	if (error)
		kprintf("hammer2: msg write failed error %d\n", error);

	if (msg) {
		if (msg->state && msg->state->msg == msg)
			msg->state->msg = NULL;
		hammer2_msg_free(msg);
	}

	/*
	 * Shutdown the socket.  This will cause the rx thread to get an
	 * EOF and ensure that both threads get to a termination state.
	 */
	fp_shutdown(pmp->msg_fp, SHUT_RDWR);

	/*
	 * Set KILLTX (which the rx side waits for), then wait for the RX
	 * side to completely finish before we clean out any remaining
	 * command states.
	 */
	lockmgr(&pmp->msglk, LK_RELEASE);
	atomic_set_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_KILLTX);
	wakeup(&pmp->msg_ctl);
	while (pmp->msgrd_td) {
		wakeup(&pmp->msg_ctl);
		tsleep(pmp, 0, "clstrkw", hz);
	}
	lockmgr(&pmp->msglk, LK_EXCLUSIVE);

	/*
	 * Simulate received MSGF_DELETE's for any remaining states.
	 */
cleanuprd:
	RB_FOREACH(state, hammer2_state_tree, &pmp->staterd_tree) {
		if (state->func &&
		    (state->rxcmd & HAMMER2_MSGF_DELETE) == 0) {
			lockmgr(&pmp->msglk, LK_RELEASE);
			msg = hammer2_msg_alloc(&pmp->router,
						HAMMER2_LNK_ERROR,
						NULL, NULL);
			if ((state->rxcmd & HAMMER2_MSGF_CREATE) == 0)
				msg->any.head.cmd |= HAMMER2_MSGF_CREATE;
			msg->any.head.cmd |= HAMMER2_MSGF_DELETE;
			msg->state = state;
			state->rxcmd = msg->any.head.cmd &
				       ~HAMMER2_MSGF_DELETE;
			msg->state->func(state, msg);
			hammer2_state_cleanuprx(msg);
			lockmgr(&pmp->msglk, LK_EXCLUSIVE);
			goto cleanuprd;
		}
		if (state->func == NULL) {
			state->flags &= ~HAMMER2_STATE_INSERTED;
			RB_REMOVE(hammer2_state_tree,
				  &pmp->staterd_tree, state);
			hammer2_state_free(state);
			goto cleanuprd;
		}
	}

	/*
	 * NOTE: We have to drain the msgq to handle situations
	 *	 where received states have built up output
	 *	 messages, to avoid creating messages with
	 *	 duplicate CREATE/DELETE flags.
	 */
cleanupwr:
	hammer2_drain_msgq(pmp);
	RB_FOREACH(state, hammer2_state_tree, &pmp->statewr_tree) {
		if (state->func &&
		    (state->rxcmd & HAMMER2_MSGF_DELETE) == 0) {
			lockmgr(&pmp->msglk, LK_RELEASE);
			msg = hammer2_msg_alloc(&pmp->router,
						HAMMER2_LNK_ERROR,
						NULL, NULL);
			if ((state->rxcmd & HAMMER2_MSGF_CREATE) == 0)
				msg->any.head.cmd |= HAMMER2_MSGF_CREATE;
			msg->any.head.cmd |= HAMMER2_MSGF_DELETE |
					     HAMMER2_MSGF_REPLY;
			msg->state = state;
			state->rxcmd = msg->any.head.cmd &
				       ~HAMMER2_MSGF_DELETE;
			msg->state->func(state, msg);
			hammer2_state_cleanuprx(msg);
			lockmgr(&pmp->msglk, LK_EXCLUSIVE);
			goto cleanupwr;
		}
		if (state->func == NULL) {
			state->flags &= ~HAMMER2_STATE_INSERTED;
			RB_REMOVE(hammer2_state_tree,
				  &pmp->statewr_tree, state);
			hammer2_state_free(state);
			goto cleanupwr;
		}
	}

	hammer2_drain_msgq(pmp);
	if (--retries == 0)
		panic("hammer2: comm thread shutdown couldn't drain");
	if (RB_ROOT(&pmp->statewr_tree))
		goto cleanupwr;

	if ((state = pmp->freewr_state) != NULL) {
		pmp->freewr_state = NULL;
		hammer2_state_free(state);
	}

	lockmgr(&pmp->msglk, LK_RELEASE);

	/*
	 * The state trees had better be empty now
	 */
	KKASSERT(RB_EMPTY(&pmp->staterd_tree));
	KKASSERT(RB_EMPTY(&pmp->statewr_tree));
	KKASSERT(pmp->conn_state == NULL);

	/*
	 * pmp can be ripped out from under us once msgwr_td is set to NULL.
	 */
	pmp->msgwr_td = NULL;
	wakeup(pmp);
	lwkt_exit();
}

/*
 * This cleans out the pending transmit message queue, adjusting any
 * persistent states properly in the process.
 *
 * Caller must hold pmp->msglk
 */
static
void
hammer2_drain_msgq(hammer2_pfsmount_t *pmp)
{
	hammer2_msg_t *msg;

	/*
	 * Clean out our pending transmit queue, executing the
	 * appropriate state adjustments.  If this tries to open
	 * any new outgoing transactions we have to loop up and
	 * clean them out.
	 */
	while ((msg = TAILQ_FIRST(&pmp->msgq)) != NULL) {
		TAILQ_REMOVE(&pmp->msgq, msg, qentry);
		lockmgr(&pmp->msglk, LK_RELEASE);
		if (msg->state && msg->state->msg == msg)
			msg->state->msg = NULL;
		if (hammer2_state_msgtx(msg))
			hammer2_msg_free(msg);
		else
			hammer2_state_cleanuptx(msg);
		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
	}
}

/*
 * Called with msglk held after queueing a new message, wakes up the
 * transmit thread.  We use an interlock thread to avoid unnecessary
 * wakeups.
 */
void
hammer2_clusterctl_wakeup(hammer2_pfsmount_t *pmp)
{
	if (pmp->msg_ctl & HAMMER2_CLUSTERCTL_SLEEPING) {
		atomic_clear_int(&pmp->msg_ctl, HAMMER2_CLUSTERCTL_SLEEPING);
		wakeup(&pmp->msg_ctl);
	}
}

static int
hammer2_msg_lnk_rcvmsg(hammer2_msg_t *msg)
{
	switch(msg->any.head.cmd & HAMMER2_MSGF_TRANSMASK) {
	case HAMMER2_LNK_CONN | HAMMER2_MSGF_CREATE:
		/*
		 * reply & leave trans open
		 */
		kprintf("CONN RECEIVE - (just ignore it)\n");
		hammer2_msg_result(msg, 0);
		break;
	case HAMMER2_LNK_SPAN | HAMMER2_MSGF_CREATE:
		kprintf("SPAN RECEIVE - ADDED FROM CLUSTER\n");
		break;
	case HAMMER2_LNK_SPAN | HAMMER2_MSGF_DELETE:
		kprintf("SPAN RECEIVE - DELETED FROM CLUSTER\n");
		break;
	default:
		break;
	}
	return(0);
}

/*
 * This function is called when the other end replies to our LNK_CONN
 * request.
 *
 * We transmit our (single) SPAN on the initial reply, leaving that
 * transaction open too.
 */
static int
hammer2_msg_conn_reply(hammer2_state_t *state, hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = state->any.pmp;
	hammer2_mount_t *hmp = pmp->hmp;
	hammer2_msg_t *rmsg;
	size_t name_len;
	int copyid;

	kprintf("LNK_CONN REPLY RECEIVED CMD %08x\n", msg->any.head.cmd);

	if (msg->any.head.cmd & HAMMER2_MSGF_CREATE) {
		kprintf("LNK_CONN transaction replied to, initiate SPAN\n");
		rmsg = hammer2_msg_alloc(&pmp->router, HAMMER2_LNK_SPAN |
						       HAMMER2_MSGF_CREATE,
					hammer2_msg_span_reply, pmp);
		rmsg->any.lnk_span.pfs_clid = pmp->iroot->ip_data.pfs_clid;
		rmsg->any.lnk_span.pfs_fsid = pmp->iroot->ip_data.pfs_fsid;
		rmsg->any.lnk_span.pfs_type = pmp->iroot->ip_data.pfs_type;
		rmsg->any.lnk_span.peer_type = pmp->hmp->voldata.peer_type;
		rmsg->any.lnk_span.proto_version = HAMMER2_SPAN_PROTO_1;
		name_len = pmp->iroot->ip_data.name_len;
		if (name_len >= sizeof(rmsg->any.lnk_span.label))
			name_len = sizeof(rmsg->any.lnk_span.label) - 1;
		bcopy(pmp->iroot->ip_data.filename,
		      rmsg->any.lnk_span.label,
		      name_len);
		rmsg->any.lnk_span.label[name_len] = 0;
		hammer2_msg_write(rmsg);

		/*
		 * Dump the configuration stored in the volume header
		 */
		hammer2_voldata_lock(hmp);
		for (copyid = 0; copyid < HAMMER2_COPYID_COUNT; ++copyid) {
			if (hmp->voldata.copyinfo[copyid].copyid == 0)
				continue;
			hammer2_volconf_update(pmp, copyid);
		}
		hammer2_voldata_unlock(hmp);
	}
	if ((state->txcmd & HAMMER2_MSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & HAMMER2_MSGF_DELETE)) {
		kprintf("LNK_CONN transaction terminated by remote\n");
		pmp->conn_state = NULL;
		hammer2_msg_reply(msg, 0);
	}
	return(0);
}

/*
 * Remote terminated our span transaction.  We have to terminate our side.
 */
static int
hammer2_msg_span_reply(hammer2_state_t *state, hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = state->any.pmp;

	kprintf("SPAN REPLY - Our sent span was terminated by the remote %08x state %p\n", msg->any.head.cmd, state);
	if ((state->txcmd & HAMMER2_MSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & HAMMER2_MSGF_DELETE)) {
		hammer2_msg_reply(msg, 0);
	}
	return(0);
}

/*
 * Volume configuration updates are passed onto the userland service
 * daemon via the open LNK_CONN transaction.
 */
void
hammer2_volconf_update(hammer2_pfsmount_t *pmp, int index)
{
	hammer2_mount_t *hmp = pmp->hmp;
	hammer2_msg_t *msg;

	/* XXX interlock against connection state termination */
	kprintf("volconf update %p\n", pmp->conn_state);
	if (pmp->conn_state) {
		kprintf("TRANSMIT VOLCONF VIA OPEN CONN TRANSACTION\n");
		msg = hammer2_msg_alloc(&pmp->router, HAMMER2_LNK_VOLCONF,
					NULL, NULL);
		msg->state = pmp->conn_state;
		msg->any.lnk_volconf.copy = hmp->voldata.copyinfo[index];
		msg->any.lnk_volconf.mediaid = hmp->voldata.fsid;
		msg->any.lnk_volconf.index = index;
		hammer2_msg_write(msg);
	}
}
