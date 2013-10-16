/*-
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression)
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
#include <sys/objcache.h>

#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>

#include <sys/mutex.h>
#include <sys/mutex2.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"

#include "hammer2.h"
#include "hammer2_lz4.h"

#include "zlib/hammer2_zlib.h"

#define REPORT_REFS_ERRORS 1	/* XXX remove me */

MALLOC_DEFINE(M_OBJCACHE, "objcache", "Object Cache");

struct hammer2_sync_info {
	hammer2_trans_t trans;
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
long hammer2_iod_fmap_read;
long hammer2_iod_volu_read;
long hammer2_iod_file_write;
long hammer2_iod_meta_write;
long hammer2_iod_indr_write;
long hammer2_iod_fmap_write;
long hammer2_iod_volu_write;
long hammer2_ioa_file_read;
long hammer2_ioa_meta_read;
long hammer2_ioa_indr_read;
long hammer2_ioa_fmap_read;
long hammer2_ioa_volu_read;
long hammer2_ioa_fmap_write;
long hammer2_ioa_file_write;
long hammer2_ioa_meta_write;
long hammer2_ioa_indr_write;
long hammer2_ioa_volu_write;

MALLOC_DECLARE(C_BUFFER);
MALLOC_DEFINE(C_BUFFER, "compbuffer", "Buffer used for compression.");

MALLOC_DECLARE(D_BUFFER);
MALLOC_DEFINE(D_BUFFER, "decompbuffer", "Buffer used for decompression.");

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
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_fmap_read, CTLFLAG_RW,
	   &hammer2_iod_fmap_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_read, CTLFLAG_RW,
	   &hammer2_iod_volu_read, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_file_write, CTLFLAG_RW,
	   &hammer2_iod_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_meta_write, CTLFLAG_RW,
	   &hammer2_iod_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_indr_write, CTLFLAG_RW,
	   &hammer2_iod_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_fmap_write, CTLFLAG_RW,
	   &hammer2_iod_fmap_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, iod_volu_write, CTLFLAG_RW,
	   &hammer2_iod_volu_write, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_read, CTLFLAG_RW,
	   &hammer2_ioa_file_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_read, CTLFLAG_RW,
	   &hammer2_ioa_meta_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_read, CTLFLAG_RW,
	   &hammer2_ioa_indr_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_fmap_read, CTLFLAG_RW,
	   &hammer2_ioa_fmap_read, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_volu_read, CTLFLAG_RW,
	   &hammer2_ioa_volu_read, 0, "");

SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_file_write, CTLFLAG_RW,
	   &hammer2_ioa_file_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_meta_write, CTLFLAG_RW,
	   &hammer2_ioa_meta_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_indr_write, CTLFLAG_RW,
	   &hammer2_ioa_indr_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_fmap_write, CTLFLAG_RW,
	   &hammer2_ioa_fmap_write, 0, "");
SYSCTL_LONG(_vfs_hammer2, OID_AUTO, ioa_volu_write, CTLFLAG_RW,
	   &hammer2_ioa_volu_write, 0, "");

static int hammer2_vfs_init(struct vfsconf *conf);
static int hammer2_vfs_uninit(struct vfsconf *vfsp);
static int hammer2_vfs_mount(struct mount *mp, char *path, caddr_t data,
				struct ucred *cred);
static int hammer2_remount(hammer2_mount_t *, char *, struct vnode *,
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
static int hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

static void hammer2_write_thread(void *arg);

/* 
 * Functions for compression in threads,
 * from hammer2_vnops.c
 */
static void hammer2_write_file_core(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_inode_data_t *ipdata,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase, int ioflag, int pblksize,
				int *errorp);
static void hammer2_compress_and_write(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_inode_data_t *ipdata,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase, int ioflag,
				int pblksize, int *errorp, int comp_algo);
static void hammer2_zero_check_and_write(struct buf *bp,
				hammer2_trans_t *trans, hammer2_inode_t *ip,
				hammer2_inode_data_t *ipdata,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase,
				int ioflag, int pblksize, int *errorp);
static int test_block_zeros(const char *buf, size_t bytes);
static void zero_write(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_inode_data_t *ipdata,
				hammer2_chain_t **parentp, 
				hammer2_key_t lbase,
				int *errorp);
static void hammer2_write_bp(hammer2_chain_t *chain, struct buf *bp,
				int ioflag, int pblksize, int *errorp);

static int hammer2_rcvdmsg(kdmsg_msg_t *msg);
static void hammer2_autodmsg(kdmsg_msg_t *msg);


/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	.vfs_init	= hammer2_vfs_init,
	.vfs_uninit = hammer2_vfs_uninit,
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
	static struct objcache_malloc_args margs_read;
	static struct objcache_malloc_args margs_write;

	int error;

	error = 0;

	if (HAMMER2_BLOCKREF_BYTES != sizeof(struct hammer2_blockref))
		error = EINVAL;
	if (HAMMER2_INODE_BYTES != sizeof(struct hammer2_inode_data))
		error = EINVAL;
	if (HAMMER2_VOLUME_BYTES != sizeof(struct hammer2_volume_data))
		error = EINVAL;

	if (error)
		kprintf("HAMMER2 structure size mismatch; cannot continue.\n");
	
	margs_read.objsize = 65536;
	margs_read.mtype = D_BUFFER;
	
	margs_write.objsize = 32768;
	margs_write.mtype = C_BUFFER;
	
	cache_buffer_read = objcache_create(margs_read.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL, objcache_malloc_alloc,
				objcache_malloc_free, &margs_read);
	cache_buffer_write = objcache_create(margs_write.mtype->ks_shortdesc,
				0, 1, NULL, NULL, NULL, objcache_malloc_alloc,
				objcache_malloc_free, &margs_write);

	lockinit(&hammer2_mntlk, "mntlk", 0, 0);
	TAILQ_INIT(&hammer2_mntlist);

	return (error);
}

static
int
hammer2_vfs_uninit(struct vfsconf *vfsp __unused)
{
	objcache_destroy(cache_buffer_read);
	objcache_destroy(cache_buffer_write);
	return 0;
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
	hammer2_key_t key_next;
	hammer2_key_t key_dummy;
	hammer2_key_t lhc;
	struct vnode *devvp;
	struct nlookupdata nd;
	hammer2_chain_t *parent;
	hammer2_chain_t *schain;
	hammer2_chain_t *rchain;
	struct file *fp;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev;
	char *label;
	int ronly = 1;
	int error;
	int cache_index;
	int i;

	hmp = NULL;
	pmp = NULL;
	dev = NULL;
	label = NULL;
	devvp = NULL;
	cache_index = -1;

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
			pmp = MPTOPMP(mp);
			for (i = 0; i < pmp->cluster.nchains; ++i) {
				hmp = pmp->cluster.chains[i]->hmp;
				devvp = hmp->devvp;
				error = hammer2_remount(hmp, path, devvp, cred);
				if (error)
					break;
			}
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
	 * Open the device if this isn't a secondary mount and construct
	 * the H2 device mount (hmp).
	 */
	if (hmp == NULL) {
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
		hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
		hmp->ronly = ronly;
		hmp->devvp = devvp;
		kmalloc_create(&hmp->mchain, "HAMMER2-chains");
		TAILQ_INSERT_TAIL(&hammer2_mntlist, hmp, mntentry);

		lockinit(&hmp->alloclk, "h2alloc", 0, 0);
		lockinit(&hmp->voldatalk, "voldata", 0, LK_CANRECURSE);
		TAILQ_INIT(&hmp->transq);

		/*
		 * vchain setup. vchain.data is embedded.
		 * vchain.refs is initialized and will never drop to 0.
		 */
		hmp->vchain.hmp = hmp;
		hmp->vchain.refs = 1;
		hmp->vchain.data = (void *)&hmp->voldata;
		hmp->vchain.bref.type = HAMMER2_BREF_TYPE_VOLUME;
		hmp->vchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
		hmp->vchain.delete_tid = HAMMER2_MAX_TID;
		hammer2_chain_core_alloc(NULL, &hmp->vchain, NULL);
		/* hmp->vchain.u.xxx is left NULL */

		/*
		 * fchain setup.  fchain.data is embedded.
		 * fchain.refs is initialized and will never drop to 0.
		 *
		 * The data is not used but needs to be initialized to
		 * pass assertion muster.  We use this chain primarily
		 * as a placeholder for the freemap's top-level RBTREE
		 * so it does not interfere with the volume's topology
		 * RBTREE.
		 */
		hmp->fchain.hmp = hmp;
		hmp->fchain.refs = 1;
		hmp->fchain.data = (void *)&hmp->voldata.freemap_blockset;
		hmp->fchain.bref.type = HAMMER2_BREF_TYPE_FREEMAP;
		hmp->fchain.bref.data_off = 0 | HAMMER2_PBUFRADIX;
		hmp->fchain.bref.methods =
			HAMMER2_ENC_CHECK(HAMMER2_CHECK_FREEMAP) |
			HAMMER2_ENC_COMP(HAMMER2_COMP_NONE);
		hmp->fchain.delete_tid = HAMMER2_MAX_TID;

		hammer2_chain_core_alloc(NULL, &hmp->fchain, NULL);
		/* hmp->fchain.u.xxx is left NULL */

		/*
		 * Install the volume header
		 */
		error = hammer2_install_volume_header(hmp);
		if (error) {
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return error;
		}

		/*
		 * First locate the super-root inode, which is key 0
		 * relative to the volume header's blockset.
		 *
		 * Then locate the root inode by scanning the directory keyspace
		 * represented by the label.
		 */
		parent = hammer2_chain_lookup_init(&hmp->vchain, 0);
		schain = hammer2_chain_lookup(&parent, &key_dummy,
				      HAMMER2_SROOT_KEY, HAMMER2_SROOT_KEY,
				      &cache_index, 0);
		hammer2_chain_lookup_done(parent);
		if (schain == NULL) {
			kprintf("hammer2_mount: invalid super-root\n");
			hammer2_vfs_unmount(mp, MNT_FORCE);
			return EINVAL;
		}

		/*
		 * NOTE: inode_get sucks up schain's lock.
		 */
		atomic_set_int(&schain->flags, HAMMER2_CHAIN_PFSROOT);
		hmp->sroot = hammer2_inode_get(NULL, NULL, schain);
		hammer2_inode_ref(hmp->sroot);
		hammer2_inode_unlock_ex(hmp->sroot, schain);
		schain = NULL;
		/* leave hmp->sroot with one ref */
	}

	/*
	 * Block device opened successfully, finish initializing the
	 * mount structure.
	 *
	 * From this point on we have to call hammer2_unmount() on failure.
	 */
	pmp = kmalloc(sizeof(*pmp), M_HAMMER2, M_WAITOK | M_ZERO);

	kmalloc_create(&pmp->minode, "HAMMER2-inodes");
	kmalloc_create(&pmp->mmsg, "HAMMER2-pfsmsg");

	spin_init(&pmp->inum_spin);
	RB_INIT(&pmp->inum_tree);

	kdmsg_iocom_init(&pmp->iocom, pmp,
			 KDMSG_IOCOMF_AUTOCONN |
			 KDMSG_IOCOMF_AUTOSPAN |
			 KDMSG_IOCOMF_AUTOCIRC,
			 pmp->mmsg, hammer2_rcvdmsg);

	ccms_domain_init(&pmp->ccms_dom);
	++hmp->pmp_count;
	lockmgr(&hammer2_mntlk, LK_RELEASE);
	kprintf("hammer2_mount hmp=%p pmp=%p pmpcnt=%d\n",
		hmp, pmp, hmp->pmp_count);

	mp->mnt_flag = MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;	/* all entry pts are SMP */
	mp->mnt_kern_flag |= MNTK_THR_SYNC;	/* new vsyncscan semantics */

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
	mp->mnt_data = (qaddr_t)pmp;
	pmp->mp = mp;

	/*
	 * Lookup mount point under the media-localized super-root.
	 */
	parent = hammer2_inode_lock_ex(hmp->sroot);
	lhc = hammer2_dirhash(label, strlen(label));
	rchain = hammer2_chain_lookup(&parent, &key_next,
				      lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				      &cache_index, 0);
	while (rchain) {
		if (rchain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    strcmp(label, rchain->data->ipdata.filename) == 0) {
			break;
		}
		rchain = hammer2_chain_next(&parent, rchain, &key_next,
					    key_next,
					    lhc + HAMMER2_DIRHASH_LOMASK,
					    &cache_index, 0);
	}
	hammer2_inode_unlock_ex(hmp->sroot, parent);

	if (rchain == NULL) {
		kprintf("hammer2_mount: PFS label not found\n");
		--hmp->pmp_count;
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EINVAL;
	}
	if (rchain->flags & HAMMER2_CHAIN_MOUNTED) {
		hammer2_chain_unlock(rchain);
		kprintf("hammer2_mount: PFS label already mounted!\n");
		--hmp->pmp_count;
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EBUSY;
	}
#if 0
	if (rchain->flags & HAMMER2_CHAIN_RECYCLE) {
		kprintf("hammer2_mount: PFS label currently recycling\n");
		--hmp->pmp_count;
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EBUSY;
	}
#endif

	atomic_set_int(&rchain->flags, HAMMER2_CHAIN_MOUNTED);

	/*
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 */
	hammer2_chain_ref(rchain);		/* for pmp->rchain */
	pmp->cluster.nchains = 1;
	pmp->cluster.chains[0] = rchain;
	pmp->iroot = hammer2_inode_get(pmp, NULL, rchain);
	hammer2_inode_ref(pmp->iroot);		/* ref for pmp->iroot */

	KKASSERT(rchain->pmp == NULL);		/* tracking pmp for rchain */
	rchain->pmp = pmp;
	atomic_add_long(&pmp->inmem_chains, 1);

	hammer2_inode_unlock_ex(pmp->iroot, rchain);

	kprintf("iroot %p\n", pmp->iroot);

	/*
	 * The logical file buffer bio write thread handles things
	 * like physical block assignment and compression.
	 */
	mtx_init(&pmp->wthread_mtx);
	bioq_init(&pmp->wthread_bioq);
	pmp->wthread_destroy = 0;
	lwkt_create(hammer2_write_thread, pmp,
		    &pmp->wthread_td, NULL, 0, -1, "hwrite-%s", label);

	/*
	 * Ref the cluster management messaging descriptor.  The mount
	 * program deals with the other end of the communications pipe.
	 */
	fp = holdfp(curproc->p_fd, info.cluster_fd, -1);
	if (fp == NULL) {
		kprintf("hammer2_mount: bad cluster_fd!\n");
		hammer2_vfs_unmount(mp, MNT_FORCE);
		return EBADF;
	}
	hammer2_cluster_reconnect(pmp, fp);

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

/*
 * Handle bioq for strategy write
 */
static
void
hammer2_write_thread(void *arg)
{
	hammer2_pfsmount_t *pmp;
	struct bio *bio;
	struct buf *bp;
	hammer2_trans_t trans;
	struct vnode *vp;
	hammer2_inode_t *last_ip;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	hammer2_chain_t **parentp;
	hammer2_inode_data_t *ipdata;
	hammer2_key_t lbase;
	int lblksize;
	int pblksize;
	int error;
	
	pmp = arg;
	
	mtx_lock(&pmp->wthread_mtx);
	while (pmp->wthread_destroy == 0) {
		if (bioq_first(&pmp->wthread_bioq) == NULL) {
			mtxsleep(&pmp->wthread_bioq, &pmp->wthread_mtx,
				 0, "h2bioqw", 0);
		}
		last_ip = NULL;
		parent = NULL;
		parentp = &parent;

		while ((bio = bioq_takefirst(&pmp->wthread_bioq)) != NULL) {
			mtx_unlock(&pmp->wthread_mtx);
			
			error = 0;
			bp = bio->bio_buf;
			vp = bp->b_vp;
			ip = VTOI(vp);

			/*
			 * Cache transaction for multi-buffer flush efficiency.
			 * Lock the ip separately for each buffer to allow
			 * interleaving with frontend writes.
			 */
			if (last_ip != ip) {
				if (last_ip)
					hammer2_trans_done(&trans);
				hammer2_trans_init(&trans, ip->pmp,
						   HAMMER2_TRANS_BUFCACHE);
				last_ip = ip;
			}
			parent = hammer2_inode_lock_ex(ip);

			/*
			 * Inode is modified, flush size and mtime changes
			 * to ensure that the file size remains consistent
			 * with the buffers being flushed.
			 */
			if (ip->flags & (HAMMER2_INODE_RESIZED |
					 HAMMER2_INODE_MTIME)) {
				hammer2_inode_fsync(&trans, ip, parentp);
			}
			ipdata = hammer2_chain_modify_ip(&trans, ip,
							 parentp, 0);
			lblksize = hammer2_calc_logical(ip, bio->bio_offset,
							&lbase, NULL);
			pblksize = hammer2_calc_physical(ip, lbase);
			hammer2_write_file_core(bp, &trans, ip, ipdata,
						parentp,
						lbase, IO_ASYNC,
						pblksize, &error);
			hammer2_inode_unlock_ex(ip, parent);
			if (error) {
				kprintf("hammer2: error in buffer write\n");
				bp->b_flags |= B_ERROR;
				bp->b_error = EIO;
			}
			biodone(bio);
			mtx_lock(&pmp->wthread_mtx);
		}

		/*
		 * Clean out transaction cache
		 */
		if (last_ip)
			hammer2_trans_done(&trans);
	}
	pmp->wthread_destroy = -1;
	wakeup(&pmp->wthread_destroy);
	
	mtx_unlock(&pmp->wthread_mtx);
}

/* 
 * Return a chain suitable for I/O, creating the chain if necessary
 * and assigning its physical block.
 */
static
hammer2_chain_t *
hammer2_assign_physical(hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_chain_t **parentp,
			hammer2_key_t lbase, int pblksize, int *errorp)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_off_t pbase;
	hammer2_key_t key_dummy;
	int pradix = hammer2_getradix(pblksize);
	int cache_index = -1;

	/*
	 * Locate the chain associated with lbase, return a locked chain.
	 * However, do not instantiate any data reference (which utilizes a
	 * device buffer) because we will be using direct IO via the
	 * logical buffer cache buffer.
	 */
	*errorp = 0;
	KKASSERT(pblksize >= HAMMER2_MIN_ALLOC);
retry:
	parent = *parentp;
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS); /* extra lock */
	chain = hammer2_chain_lookup(&parent, &key_dummy,
				     lbase, lbase,
				     &cache_index, HAMMER2_LOOKUP_NODATA);

	if (chain == NULL) {
		/*
		 * We found a hole, create a new chain entry.
		 *
		 * NOTE: DATA chains are created without device backing
		 *	 store (nor do we want any).
		 */
		*errorp = hammer2_chain_create(trans, &parent, &chain,
					       lbase, HAMMER2_PBUFRADIX,
					       HAMMER2_BREF_TYPE_DATA,
					       pblksize);
		if (chain == NULL) {
			hammer2_chain_lookup_done(parent);
			panic("hammer2_chain_create: par=%p error=%d\n",
				parent, *errorp);
			goto retry;
		}

		pbase = chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX;
		/*ip->delta_dcount += pblksize;*/
	} else {
		switch (chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * The data is embedded in the inode.  The
			 * caller is responsible for marking the inode
			 * modified and copying the data to the embedded
			 * area.
			 */
			pbase = NOOFFSET;
			break;
		case HAMMER2_BREF_TYPE_DATA:
			if (chain->bytes != pblksize) {
				hammer2_chain_resize(trans, ip,
						     parent, &chain,
						     pradix,
						     HAMMER2_MODIFY_OPTDATA);
			}
			hammer2_chain_modify(trans, &chain,
					     HAMMER2_MODIFY_OPTDATA);
			pbase = chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX;
			break;
		default:
			panic("hammer2_assign_physical: bad type");
			/* NOT REACHED */
			pbase = NOOFFSET;
			break;
		}
	}

	/*
	 * Cleanup.  If chain wound up being the inode (i.e. DIRECTDATA),
	 * we might have to replace *parentp.
	 */
	hammer2_chain_lookup_done(parent);
	if (chain) {
		if (*parentp != chain &&
		    (*parentp)->core == chain->core) {
			parent = *parentp;
			*parentp = chain;		/* eats lock */
			hammer2_chain_unlock(parent);
			hammer2_chain_lock(chain, 0);	/* need another */
		}
		/* else chain already locked for return */
	}
	return (chain);
}

/* 
 * From hammer2_vnops.c.
 * The core write function which determines which path to take
 * depending on compression settings.
 */
static
void
hammer2_write_file_core(struct buf *bp, hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_inode_data_t *ipdata,
			hammer2_chain_t **parentp,
			hammer2_key_t lbase, int ioflag, int pblksize,
			int *errorp)
{
	hammer2_chain_t *chain;

	switch(HAMMER2_DEC_COMP(ipdata->comp_algo)) {
	case HAMMER2_COMP_NONE:
		/*
		 * We have to assign physical storage to the buffer
		 * we intend to dirty or write now to avoid deadlocks
		 * in the strategy code later.
		 *
		 * This can return NOOFFSET for inode-embedded data.
		 * The strategy code will take care of it in that case.
		 */
		chain = hammer2_assign_physical(trans, ip, parentp,
						lbase, pblksize,
						errorp);
		hammer2_write_bp(chain, bp, ioflag, pblksize, errorp);
		if (chain)
			hammer2_chain_unlock(chain);
		break;
	case HAMMER2_COMP_AUTOZERO:
		/*
		 * Check for zero-fill only
		 */
		hammer2_zero_check_and_write(bp, trans, ip,
				    ipdata, parentp, lbase,
				    ioflag, pblksize, errorp);
		break;
	case HAMMER2_COMP_LZ4:
	case HAMMER2_COMP_ZLIB:
	default:
		/*
		 * Check for zero-fill and attempt compression.
		 */
		hammer2_compress_and_write(bp, trans, ip,
					   ipdata, parentp,
					   lbase, ioflag,
					   pblksize, errorp,
					   ipdata->comp_algo);
		break;
	}
	ipdata = &ip->chain->data->ipdata;	/* reload */
}

/*
 * From hammer2_vnops.c
 * Generic function that will perform the compression in compression
 * write path. The compression algorithm is determined by the settings
 * obtained from inode.
 */
static
void
hammer2_compress_and_write(struct buf *bp, hammer2_trans_t *trans,
	hammer2_inode_t *ip, hammer2_inode_data_t *ipdata,
	hammer2_chain_t **parentp,
	hammer2_key_t lbase, int ioflag, int pblksize,
	int *errorp, int comp_algo)
{
	hammer2_chain_t *chain;
	int comp_size;
	int comp_block_size;
	char *comp_buffer;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, trans, ip, ipdata, parentp, lbase, errorp);
		return;
	}

	comp_size = 0;
	comp_buffer = NULL;

	KKASSERT(pblksize / 2 <= 32768);
		
	if (ip->comp_heuristic < 8 || (ip->comp_heuristic & 7) == 0) {
		z_stream strm_compress;
		int comp_level;
		int ret;

		switch(HAMMER2_DEC_COMP(comp_algo)) {
		case HAMMER2_COMP_LZ4:
			comp_buffer = objcache_get(cache_buffer_write,
						   M_INTWAIT);
			comp_size = LZ4_compress_limitedOutput(
					bp->b_data,
					&comp_buffer[sizeof(int)],
					pblksize,
					pblksize / 2 - sizeof(int));
			/*
			 * We need to prefix with the size, LZ4
			 * doesn't do it for us.  Add the related
			 * overhead.
			 */
			*(int *)comp_buffer = comp_size;
			if (comp_size)
				comp_size += sizeof(int);
			break;
		case HAMMER2_COMP_ZLIB:
			comp_level = HAMMER2_DEC_LEVEL(comp_algo);
			if (comp_level == 0)
				comp_level = 6;	/* default zlib compression */
			else if (comp_level < 6)
				comp_level = 6;
			else if (comp_level > 9)
				comp_level = 9;
			ret = deflateInit(&strm_compress, comp_level);
			if (ret != Z_OK) {
				kprintf("HAMMER2 ZLIB: fatal error "
					"on deflateInit.\n");
			}

			comp_buffer = objcache_get(cache_buffer_write,
						   M_INTWAIT);
			strm_compress.next_in = bp->b_data;
			strm_compress.avail_in = pblksize;
			strm_compress.next_out = comp_buffer;
			strm_compress.avail_out = pblksize / 2;
			ret = deflate(&strm_compress, Z_FINISH);
			if (ret == Z_STREAM_END) {
				comp_size = pblksize / 2 -
					    strm_compress.avail_out;
			} else {
				comp_size = 0;
			}
			ret = deflateEnd(&strm_compress);
			break;
		default:
			kprintf("Error: Unknown compression method.\n");
			kprintf("Comp_method = %d.\n", comp_algo);
			break;
		}
	}

	if (comp_size == 0) {
		/*
		 * compression failed or turned off
		 */
		comp_block_size = pblksize;	/* safety */
		if (++ip->comp_heuristic > 128)
			ip->comp_heuristic = 8;
	} else {
		/*
		 * compression succeeded
		 */
		ip->comp_heuristic = 0;
		if (comp_size <= 1024) {
			comp_block_size = 1024;
		} else if (comp_size <= 2048) {
			comp_block_size = 2048;
		} else if (comp_size <= 4096) {
			comp_block_size = 4096;
		} else if (comp_size <= 8192) {
			comp_block_size = 8192;
		} else if (comp_size <= 16384) {
			comp_block_size = 16384;
		} else if (comp_size <= 32768) {
			comp_block_size = 32768;
		} else {
			panic("hammer2: WRITE PATH: "
			      "Weird comp_size value.");
			/* NOT REACHED */
			comp_block_size = pblksize;
		}
	}

	chain = hammer2_assign_physical(trans, ip, parentp,
					lbase, comp_block_size,
					errorp);
	ipdata = &ip->chain->data->ipdata;	/* RELOAD */

	if (*errorp) {
		kprintf("WRITE PATH: An error occurred while "
			"assigning physical space.\n");
		KKASSERT(chain == NULL);
	} else {
		/* Get device offset */
		hammer2_off_t pbase;
		hammer2_off_t pmask;
		hammer2_off_t peof;
		size_t boff;
		size_t psize;
		struct buf *dbp;
		int temp_check;

		KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);

		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(chain->data->ipdata.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bcopy(bp->b_data, chain->data->ipdata.u.data,
			      HAMMER2_EMBEDDED_BYTES);
			break;
		case HAMMER2_BREF_TYPE_DATA:
			psize = hammer2_devblksize(chain->bytes);
			pmask = (hammer2_off_t)psize - 1;
			pbase = chain->bref.data_off & ~pmask;
			boff = chain->bref.data_off &
			       (HAMMER2_OFF_MASK & pmask);
			peof = (pbase + HAMMER2_SEGMASK64) &
			       ~HAMMER2_SEGMASK64;
			temp_check = HAMMER2_DEC_CHECK(chain->bref.methods);

			/*
			 * Optimize out the read-before-write
			 * if possible.
			 */
			if (comp_block_size == psize) {
				dbp = getblk(chain->hmp->devvp, pbase,
					     psize, 0, 0);
			} else {
				*errorp = bread(chain->hmp->devvp,
						pbase, psize, &dbp);
				if (*errorp) {
					kprintf("hammer2: WRITE PATH: "
						"dbp bread error\n");
					break;
				}
			}

			/*
			 * When loading the block make sure we don't
			 * leave garbage after the compressed data.
			 */
			if (comp_size) {
				chain->bref.methods =
					HAMMER2_ENC_COMP(comp_algo) +
					HAMMER2_ENC_CHECK(temp_check);
				bcopy(comp_buffer, dbp->b_data + boff,
				      comp_size);
				if (comp_size != comp_block_size) {
					bzero(dbp->b_data + boff +
						comp_size,
					      comp_block_size -
						comp_size);
				}
			} else {
				chain->bref.methods =
					HAMMER2_ENC_COMP(
						HAMMER2_COMP_NONE) +
					HAMMER2_ENC_CHECK(temp_check);
				bcopy(bp->b_data, dbp->b_data + boff,
				      pblksize);
			}

			/*
			 * Device buffer is now valid, chain is no
			 * longer in the initial state.
			 */
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_INITIAL);

			/* Now write the related bdp. */
			if (ioflag & IO_SYNC) {
				/*
				 * Synchronous I/O requested.
				 */
				bwrite(dbp);
			/*
			} else if ((ioflag & IO_DIRECT) &&
				   loff + n == pblksize) {
				bdwrite(dbp);
			*/
			} else if (ioflag & IO_ASYNC) {
				bawrite(dbp);
			} else if (hammer2_cluster_enable) {
				cluster_write(dbp, peof,
					      HAMMER2_PBUFSIZE,
					      4/*XXX*/);
			} else {
				bdwrite(dbp);
			}
			break;
		default:
			panic("hammer2_write_bp: bad chain type %d\n",
				chain->bref.type);
			/* NOT REACHED */
			break;
		}

		hammer2_chain_unlock(chain);
	}
	if (comp_buffer)
		objcache_put(cache_buffer_write, comp_buffer);
}

/*
 * Function that performs zero-checking and writing without compression,
 * it corresponds to default zero-checking path.
 */
static
void
hammer2_zero_check_and_write(struct buf *bp, hammer2_trans_t *trans,
	hammer2_inode_t *ip, hammer2_inode_data_t *ipdata,
	hammer2_chain_t **parentp,
	hammer2_key_t lbase, int ioflag, int pblksize, int *errorp)
{
	hammer2_chain_t *chain;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, trans, ip, ipdata, parentp, lbase, errorp);
	} else {
		chain = hammer2_assign_physical(trans, ip, parentp,
						lbase, pblksize, errorp);
		hammer2_write_bp(chain, bp, ioflag, pblksize, errorp);
		if (chain)
			hammer2_chain_unlock(chain);
	}
}

/*
 * A function to test whether a block of data contains only zeros,
 * returns TRUE (non-zero) if the block is all zeros.
 */
static
int
test_block_zeros(const char *buf, size_t bytes)
{
	size_t i;

	for (i = 0; i < bytes; i += sizeof(long)) {
		if (*(const long *)(buf + i) != 0)
			return (0);
	}
	return (1);
}

/*
 * Function to "write" a block that contains only zeros.
 */
static
void
zero_write(struct buf *bp, hammer2_trans_t *trans, hammer2_inode_t *ip,
	hammer2_inode_data_t *ipdata, hammer2_chain_t **parentp,
	hammer2_key_t lbase, int *errorp __unused)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_dummy;
	int cache_index = -1;

	parent = hammer2_chain_lookup_init(*parentp, 0);

	chain = hammer2_chain_lookup(&parent, &key_dummy, lbase, lbase,
				     &cache_index, HAMMER2_LOOKUP_NODATA);
	if (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			bzero(chain->data->ipdata.u.data,
			      HAMMER2_EMBEDDED_BYTES);
		} else {
			hammer2_chain_delete(trans, chain, 0);
		}
		hammer2_chain_unlock(chain);
	}
	hammer2_chain_lookup_done(parent);
}

/*
 * Function to write the data as it is, without performing any sort of
 * compression. This function is used in path without compression and
 * default zero-checking path.
 */
static
void
hammer2_write_bp(hammer2_chain_t *chain, struct buf *bp, int ioflag,
				int pblksize, int *errorp)
{
	hammer2_off_t pbase;
	hammer2_off_t pmask;
	hammer2_off_t peof;
	struct buf *dbp;
	size_t boff;
	size_t psize;
	int error;
	int temp_check = HAMMER2_DEC_CHECK(chain->bref.methods);

	KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(chain->data->ipdata.op_flags &
			 HAMMER2_OPFLAG_DIRECTDATA);
		KKASSERT(bp->b_loffset == 0);
		bcopy(bp->b_data, chain->data->ipdata.u.data,
		      HAMMER2_EMBEDDED_BYTES);
		error = 0;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		psize = hammer2_devblksize(chain->bytes);
		pmask = (hammer2_off_t)psize - 1;
		pbase = chain->bref.data_off & ~pmask;
		boff = chain->bref.data_off & (HAMMER2_OFF_MASK & pmask);
		peof = (pbase + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;

		if (psize == pblksize) {
			dbp = getblk(chain->hmp->devvp, pbase,
				     psize, 0, 0);
			error = 0;
		} else {
			error = bread(chain->hmp->devvp, pbase, psize, &dbp);
			if (error) {
				kprintf("hammer2: WRITE PATH: "
					"dbp bread error\n");
				break;
			}
		}

		chain->bref.methods = HAMMER2_ENC_COMP(HAMMER2_COMP_NONE) +
				      HAMMER2_ENC_CHECK(temp_check);
		bcopy(bp->b_data, dbp->b_data + boff, chain->bytes);
		
		/*
		 * Device buffer is now valid, chain is no
		 * longer in the initial state.
		 */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

		if (ioflag & IO_SYNC) {
			/*
			 * Synchronous I/O requested.
			 */
			bwrite(dbp);
		/*
		} else if ((ioflag & IO_DIRECT) && loff + n == pblksize) {
			bdwrite(dbp);
		*/
		} else if (ioflag & IO_ASYNC) {
			bawrite(dbp);
		} else if (hammer2_cluster_enable) {
			cluster_write(dbp, peof, HAMMER2_PBUFSIZE, 4/*XXX*/);
		} else {
			bdwrite(dbp);
		}
		break;
	default:
		panic("hammer2_write_bp: bad chain type %d\n",
		      chain->bref.type);
		/* NOT REACHED */
		error = 0;
		break;
	}
	*errorp = error;
}

static
int
hammer2_remount(hammer2_mount_t *hmp, char *path, struct vnode *devvp,
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
	hammer2_chain_t *rchain;
	int flags;
	int error = 0;
	int ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
	int dumpcnt;
	int i;
	struct vnode *devvp;

	pmp = MPTOPMP(mp);

	ccms_domain_uninit(&pmp->ccms_dom);
	kdmsg_iocom_uninit(&pmp->iocom);	/* XXX chain dependency */

	lockmgr(&hammer2_mntlk, LK_EXCLUSIVE);

	/*
	 * If mount initialization proceeded far enough we must flush
	 * its vnodes.
	 */
	if (mntflags & MNT_FORCE)
		flags = FORCECLOSE;
	else
		flags = 0;
	if (pmp->iroot) {
		error = vflush(mp, 0, flags);
		if (error)
			goto failed;
	}

	if (pmp->wthread_td) {
		mtx_lock(&pmp->wthread_mtx);
		pmp->wthread_destroy = 1;
		wakeup(&pmp->wthread_bioq);
		while (pmp->wthread_destroy != -1) {
			mtxsleep(&pmp->wthread_destroy,
				&pmp->wthread_mtx, 0,
				"umount-sleep",	0);
		}
		mtx_unlock(&pmp->wthread_mtx);
		pmp->wthread_td = NULL;
	}

	for (i = 0; i < pmp->cluster.nchains; ++i) {
		hmp = pmp->cluster.chains[i]->hmp;

		hammer2_mount_exlock(hmp);

		--hmp->pmp_count;
		kprintf("hammer2_unmount hmp=%p pmpcnt=%d\n",
			hmp, hmp->pmp_count);

		/*
		 * Flush any left over chains.  The voldata lock is only used
		 * to synchronize against HAMMER2_CHAIN_MODIFIED_AUX.
		 */
		hammer2_voldata_lock(hmp);
		if ((hmp->vchain.flags | hmp->fchain.flags) &
		    (HAMMER2_CHAIN_MODIFIED | HAMMER2_CHAIN_SUBMODIFIED)) {
			hammer2_voldata_unlock(hmp, 0);
			hammer2_vfs_sync(mp, MNT_WAIT);
			hammer2_vfs_sync(mp, MNT_WAIT);
		} else {
			hammer2_voldata_unlock(hmp, 0);
		}
		if (hmp->pmp_count == 0) {
			if (hmp->vchain.flags & (HAMMER2_CHAIN_MODIFIED |
						 HAMMER2_CHAIN_SUBMODIFIED)) {
				kprintf("hammer2_unmount: chains left over "
					"after final sync\n");
				if (hammer2_debug & 0x0010)
					Debugger("entered debugger");
			}
		}

		/*
		 * Cleanup the root and super-root chain elements
		 * (which should be clean).
		 */
		if (pmp->iroot) {
#if REPORT_REFS_ERRORS
			if (pmp->iroot->refs != 1)
				kprintf("PMP->IROOT %p REFS WRONG %d\n",
					pmp->iroot, pmp->iroot->refs);
#else
			KKASSERT(pmp->iroot->refs == 1);
#endif
			/* ref for pmp->iroot */
			hammer2_inode_drop(pmp->iroot);
			pmp->iroot = NULL;
		}

		rchain = pmp->cluster.chains[i];
		if (rchain) {
			atomic_clear_int(&rchain->flags, HAMMER2_CHAIN_MOUNTED);
#if REPORT_REFS_ERRORS
			if (rchain->refs != 1)
				kprintf("PMP->RCHAIN %p REFS WRONG %d\n",
					rchain, rchain->refs);
#else
			KKASSERT(rchain->refs == 1);
#endif
			hammer2_chain_drop(rchain);
			pmp->cluster.chains[i] = NULL;
		}

		/*
		 * If no PFS's left drop the master hammer2_mount for the
		 * device.
		 */
		if (hmp->pmp_count == 0) {
			if (hmp->sroot) {
				hammer2_inode_drop(hmp->sroot);
				hmp->sroot = NULL;
			}

			/*
			 * Finish up with the device vnode
			 */
			if ((devvp = hmp->devvp) != NULL) {
				vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
				hmp->devvp = NULL;
				VOP_CLOSE(devvp,
					  (ronly ? FREAD : FREAD|FWRITE));
				vrele(devvp);
				devvp = NULL;
			}

			/*
			 * Final drop of embedded freemap root chain to
			 * clean up fchain.core (fchain structure is not
			 * flagged ALLOCATED so it is cleaned out and then
			 * left to rot).
			 */
			hammer2_chain_drop(&hmp->fchain);

			/*
			 * Final drop of embedded volume root chain to clean
			 * up vchain.core (vchain structure is not flagged
			 * ALLOCATED so it is cleaned out and then left to
			 * rot).
			 */
			dumpcnt = 50;
			hammer2_dump_chain(&hmp->vchain, 0, &dumpcnt);
			hammer2_mount_unlock(hmp);
			hammer2_chain_drop(&hmp->vchain);

			TAILQ_REMOVE(&hammer2_mntlist, hmp, mntentry);
			kmalloc_destroy(&hmp->mchain);
			kfree(hmp, M_HAMMER2);
		} else {
			hammer2_mount_unlock(hmp);
		}
	}

	pmp->mp = NULL;
	mp->mnt_data = NULL;

	kmalloc_destroy(&pmp->mmsg);
	kmalloc_destroy(&pmp->minode);

	kfree(pmp, M_HAMMER2);
	error = 0;

failed:
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
	hammer2_chain_t *parent;
	int error;
	struct vnode *vp;

	pmp = MPTOPMP(mp);
	if (pmp->iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		parent = hammer2_inode_lock_sh(pmp->iroot);
		vp = hammer2_igetv(pmp->iroot, &error);
		hammer2_inode_unlock_sh(pmp->iroot, parent);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}

	return (error);
}

/*
 * Filesystem status
 *
 * XXX incorporate ipdata->inode_quota and data_quota
 */
static
int
hammer2_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;

	pmp = MPTOPMP(mp);
	KKASSERT(pmp->cluster.nchains >= 1);
	hmp = pmp->cluster.chains[0]->hmp;	/* XXX */

	mp->mnt_stat.f_files = pmp->inode_count;
	mp->mnt_stat.f_ffree = 0;
	mp->mnt_stat.f_blocks = hmp->voldata.allocator_size / HAMMER2_PBUFSIZE;
	mp->mnt_stat.f_bfree =  hmp->voldata.allocator_free / HAMMER2_PBUFSIZE;
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
	KKASSERT(pmp->cluster.nchains >= 1);
	hmp = pmp->cluster.chains[0]->hmp;	/* XXX */

	mp->mnt_vstat.f_bsize = HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_files = pmp->inode_count;
	mp->mnt_vstat.f_ffree = 0;
	mp->mnt_vstat.f_blocks = hmp->voldata.allocator_size / HAMMER2_PBUFSIZE;
	mp->mnt_vstat.f_bfree =  hmp->voldata.allocator_free / HAMMER2_PBUFSIZE;
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
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;
	int flags;
	int error;
	int total_error;
	int i;

	pmp = MPTOPMP(mp);

	/*
	 * We can't acquire locks on existing vnodes while in a transaction
	 * without risking a deadlock.  This assumes that vfsync() can be
	 * called without the vnode locked (which it can in DragonFly).
	 * Otherwise we'd have to implement a multi-pass or flag the lock
	 * failures and retry.
	 */
	/*flags = VMSC_GETVP;*/
	flags = 0;
	if (waitfor & MNT_LAZY)
		flags |= VMSC_ONEPASS;

	hammer2_trans_init(&info.trans, pmp, HAMMER2_TRANS_ISFLUSH);

	/*
	 * vfsync the vnodes. XXX
	 */
	info.error = 0;
	info.waitfor = MNT_NOWAIT;
	vsyncscan(mp, flags | VMSC_NOWAIT, hammer2_sync_scan2, &info);
	if (info.error == 0 && (waitfor & MNT_WAIT)) {
		info.waitfor = waitfor;
		    vsyncscan(mp, flags, hammer2_sync_scan2, &info);

	}
#if 0
	if (waitfor == MNT_WAIT) {
		/* XXX */
	} else {
		/* XXX */
	}
#endif

	total_error = 0;
	for (i = 0; i < pmp->cluster.nchains; ++i) {
		hmp = pmp->cluster.chains[i]->hmp;

		/*
		 * Media mounts have two 'roots', vchain for the topology
		 * and fchain for the free block table.  Flush both.
		 *
		 * Note that the topology and free block table are handled
		 * independently, so the free block table can wind up being
		 * ahead of the topology.  We depend on the bulk free scan
		 * code to deal with any loose ends.
		 */
		hammer2_chain_lock(&hmp->vchain, HAMMER2_RESOLVE_ALWAYS);
		if (hmp->vchain.flags & (HAMMER2_CHAIN_MODIFIED |
					  HAMMER2_CHAIN_SUBMODIFIED)) {
			hammer2_chain_flush(&info.trans, &hmp->vchain);
		}
		hammer2_chain_unlock(&hmp->vchain);

		hammer2_chain_lock(&hmp->fchain, HAMMER2_RESOLVE_ALWAYS);
		if (hmp->fchain.flags & (HAMMER2_CHAIN_MODIFIED |
					 HAMMER2_CHAIN_SUBMODIFIED)) {
			/* this will also modify vchain as a side effect */
			hammer2_chain_flush(&info.trans, &hmp->fchain);
		}
		hammer2_chain_unlock(&hmp->fchain);

		error = 0;

		/*
		 * We can't safely flush the volume header until we have
		 * flushed any device buffers which have built up.
		 *
		 * XXX this isn't being incremental
		 */
		vn_lock(hmp->devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(hmp->devvp, MNT_WAIT, 0);
		vn_unlock(hmp->devvp);

		/*
		 * The flush code sets CHAIN_VOLUMESYNC to indicate that the
		 * volume header needs synchronization via hmp->volsync.
		 *
		 * XXX synchronize the flag & data with only this flush XXX
		 */
		if (error == 0 &&
		    (hmp->vchain.flags & HAMMER2_CHAIN_VOLUMESYNC)) {
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
			 * Then we can safely flush the version of the
			 * volume header synchronized by the flush code.
			 */
			i = hmp->volhdrno + 1;
			if (i >= HAMMER2_NUM_VOLHDRS)
				i = 0;
			if (i * HAMMER2_ZONE_BYTES64 + HAMMER2_SEGSIZE >
			    hmp->volsync.volu_size) {
				i = 0;
			}
			kprintf("sync volhdr %d %jd\n",
				i, (intmax_t)hmp->volsync.volu_size);
			bp = getblk(hmp->devvp, i * HAMMER2_ZONE_BYTES64,
				    HAMMER2_PBUFSIZE, 0, 0);
			atomic_clear_int(&hmp->vchain.flags,
					 HAMMER2_CHAIN_VOLUMESYNC);
			bcopy(&hmp->volsync, bp->b_data, HAMMER2_PBUFSIZE);
			bawrite(bp);
			hmp->volhdrno = i;
		}
		if (error)
			total_error = error;
	}

	hammer2_trans_done(&info.trans);
	return (total_error);
}

/*
 * Sync passes.
 *
 * NOTE: We don't test SUBMODIFIED or MOVED here because the fsync code
 *	 won't flush on those flags.  The syncer code above will do a
 *	 general meta-data flush globally that will catch these flags.
 */

static int
hammer2_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct hammer2_sync_info *info = data;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	int error;

	ip = VTOI(vp);
	if (ip == NULL)
		return(0);
	if (vp->v_type == VNON || vp->v_type == VBAD) {
		vclrisdirty(vp);
		return(0);
	}
	if ((ip->flags & HAMMER2_INODE_MODIFIED) == 0 &&
	    RB_EMPTY(&vp->v_rbdirty_tree)) {
		vclrisdirty(vp);
		return(0);
	}

	/*
	 * VOP_FSYNC will start a new transaction so replicate some code
	 * here to do it inline (see hammer2_vop_fsync()).
	 *
	 * WARNING: The vfsync interacts with the buffer cache and might
	 *          block, we can't hold the inode lock at that time.
	 */
	atomic_clear_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	if (ip->vp)
		vfsync(ip->vp, MNT_NOWAIT, 1, NULL, NULL);
	parent = hammer2_inode_lock_ex(ip);
	hammer2_chain_flush(&info->trans, parent);
	hammer2_inode_unlock_ex(ip, parent);
	error = 0;
#if 0
	error = VOP_FSYNC(vp, MNT_NOWAIT, 0);
#endif
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
				"mismatch copy #%d %08x/%08x\n",
				i, crc0, crc);
			error_reported = 1;
			brelse(bp);
			bp = NULL;
			continue;
		}
		if (valid == 0 || hmp->voldata.mirror_tid < vd->mirror_tid) {
			valid = 1;
			hmp->voldata = *vd;
			hmp->volhdrno = i;
		}
		brelse(bp);
		bp = NULL;
	}
	if (valid) {
		hmp->volsync = hmp->voldata;
		error = 0;
		if (error_reported || bootverbose || 1) { /* 1/DEBUG */
			kprintf("hammer2: using volume header #%d\n",
				hmp->volhdrno);
		}
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
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *parent;
	hammer2_mount_t *hmp;
	size_t name_len;

	hmp = pmp->cluster.chains[0]->hmp;	/* XXX */

	/*
	 * Closes old comm descriptor, kills threads, cleans up
	 * states, then installs the new descriptor and creates
	 * new threads.
	 */
	kdmsg_iocom_reconnect(&pmp->iocom, fp, "hammer2");

	/*
	 * Setup LNK_CONN fields for autoinitiated state machine
	 */
	parent = hammer2_inode_lock_ex(pmp->iroot);
	ipdata = &parent->data->ipdata;
	pmp->iocom.auto_lnk_conn.pfs_clid = ipdata->pfs_clid;
	pmp->iocom.auto_lnk_conn.pfs_fsid = ipdata->pfs_fsid;
	pmp->iocom.auto_lnk_conn.pfs_type = ipdata->pfs_type;
	pmp->iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	pmp->iocom.auto_lnk_conn.peer_type = hmp->voldata.peer_type;

	/*
	 * Filter adjustment.  Clients do not need visibility into other
	 * clients (otherwise millions of clients would present a serious
	 * problem).  The fs_label also serves to restrict the namespace.
	 */
	pmp->iocom.auto_lnk_conn.peer_mask = 1LLU << HAMMER2_PEER_HAMMER2;
	pmp->iocom.auto_lnk_conn.pfs_mask = (uint64_t)-1;
	switch (ipdata->pfs_type) {
	case DMSG_PFSTYPE_CLIENT:
		pmp->iocom.auto_lnk_conn.peer_mask &=
				~(1LLU << DMSG_PFSTYPE_CLIENT);
		break;
	default:
		break;
	}

	name_len = ipdata->name_len;
	if (name_len >= sizeof(pmp->iocom.auto_lnk_conn.fs_label))
		name_len = sizeof(pmp->iocom.auto_lnk_conn.fs_label) - 1;
	bcopy(ipdata->filename,
	      pmp->iocom.auto_lnk_conn.fs_label,
	      name_len);
	pmp->iocom.auto_lnk_conn.fs_label[name_len] = 0;

	/*
	 * Setup LNK_SPAN fields for autoinitiated state machine
	 */
	pmp->iocom.auto_lnk_span.pfs_clid = ipdata->pfs_clid;
	pmp->iocom.auto_lnk_span.pfs_fsid = ipdata->pfs_fsid;
	pmp->iocom.auto_lnk_span.pfs_type = ipdata->pfs_type;
	pmp->iocom.auto_lnk_span.peer_type = hmp->voldata.peer_type;
	pmp->iocom.auto_lnk_span.proto_version = DMSG_SPAN_PROTO_1;
	name_len = ipdata->name_len;
	if (name_len >= sizeof(pmp->iocom.auto_lnk_span.fs_label))
		name_len = sizeof(pmp->iocom.auto_lnk_span.fs_label) - 1;
	bcopy(ipdata->filename,
	      pmp->iocom.auto_lnk_span.fs_label,
	      name_len);
	pmp->iocom.auto_lnk_span.fs_label[name_len] = 0;
	hammer2_inode_unlock_ex(pmp->iroot, parent);

	kdmsg_iocom_autoinitiate(&pmp->iocom, hammer2_autodmsg);
}

static int
hammer2_rcvdmsg(kdmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * (non-transaction)
		 * Execute shell command (not supported atm)
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * (non-transaction)
		 */
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("HAMMER2 DBG: %s\n", msg->aux_data);
		}
		break;
	default:
		/*
		 * Unsupported message received.  We only need to
		 * reply if it's a transaction in order to close our end.
		 * Ignore any one-way messages are any further messages
		 * associated with the transaction.
		 *
		 * NOTE: This case also includes DMSG_LNK_ERROR messages
		 *	 which might be one-way, replying to those would
		 *	 cause an infinite ping-pong.
		 */
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	return(0);
}

/*
 * This function is called after KDMSG has automatically handled processing
 * of a LNK layer message (typically CONN, SPAN, or CIRC).
 *
 * We tag off the LNK_CONN to trigger our LNK_VOLCONF messages which
 * advertises all available hammer2 super-root volumes.
 */
static void
hammer2_autodmsg(kdmsg_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->iocom->handle;
	hammer2_mount_t *hmp = pmp->cluster.chains[0]->hmp; /* XXX */
	int copyid;

	/*
	 * We only care about replies to our LNK_CONN auto-request.  kdmsg
	 * has already processed the reply, we use this calback as a shim
	 * to know when we can advertise available super-root volumes.
	 */
	if ((msg->any.head.cmd & DMSGF_TRANSMASK) !=
	    (DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_REPLY) ||
	    msg->state == NULL) {
		return;
	}

	kprintf("LNK_CONN REPLY RECEIVED CMD %08x\n", msg->any.head.cmd);

	if (msg->any.head.cmd & DMSGF_CREATE) {
		kprintf("HAMMER2: VOLDATA DUMP\n");

		/*
		 * Dump the configuration stored in the volume header
		 */
		hammer2_voldata_lock(hmp);
		for (copyid = 0; copyid < HAMMER2_COPYID_COUNT; ++copyid) {
			if (hmp->voldata.copyinfo[copyid].copyid == 0)
				continue;
			hammer2_volconf_update(pmp, copyid);
		}
		hammer2_voldata_unlock(hmp, 0);
	}
	if ((msg->any.head.cmd & DMSGF_DELETE) &&
	    msg->state && (msg->state->txcmd & DMSGF_DELETE) == 0) {
		kprintf("HAMMER2: CONN WAS TERMINATED\n");
	}
}

/*
 * Volume configuration updates are passed onto the userland service
 * daemon via the open LNK_CONN transaction.
 */
void
hammer2_volconf_update(hammer2_pfsmount_t *pmp, int index)
{
	hammer2_mount_t *hmp = pmp->cluster.chains[0]->hmp;	/* XXX */
	kdmsg_msg_t *msg;

	/* XXX interlock against connection state termination */
	kprintf("volconf update %p\n", pmp->iocom.conn_state);
	if (pmp->iocom.conn_state) {
		kprintf("TRANSMIT VOLCONF VIA OPEN CONN TRANSACTION\n");
		msg = kdmsg_msg_alloc_state(pmp->iocom.conn_state,
					    DMSG_LNK_VOLCONF, NULL, NULL);
		msg->any.lnk_volconf.copy = hmp->voldata.copyinfo[index];
		msg->any.lnk_volconf.mediaid = hmp->voldata.fsid;
		msg->any.lnk_volconf.index = index;
		kdmsg_msg_write(msg);
	}
}

void
hammer2_dump_chain(hammer2_chain_t *chain, int tab, int *countp)
{
	hammer2_chain_layer_t *layer;
	hammer2_chain_t *scan;
	hammer2_chain_t *first_parent;

	--*countp;
	if (*countp == 0) {
		kprintf("%*.*s...\n", tab, tab, "");
		return;
	}
	if (*countp < 0)
		return;
	first_parent = chain->core ? TAILQ_FIRST(&chain->core->ownerq) : NULL;
	kprintf("%*.*schain %p.%d [%08x][core=%p fp=%p] (%s) np=%p dt=%s refs=%d",
		tab, tab, "",
		chain, chain->bref.type, chain->flags,
		chain->core,
		first_parent,
		((chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		chain->data) ?  (char *)chain->data->ipdata.filename : "?"),
		(first_parent ? TAILQ_NEXT(chain, core_entry) : NULL),
		(chain->delete_tid == HAMMER2_MAX_TID ? "max" : "fls"),
		chain->refs);
	if (first_parent)
		kprintf(" [fpflags %08x fprefs %d\n",
			first_parent->flags,
			first_parent->refs);
	if (chain->core == NULL || TAILQ_EMPTY(&chain->core->layerq))
		kprintf("\n");
	else
		kprintf(" {\n");
	TAILQ_FOREACH(layer, &chain->core->layerq, entry) {
		RB_FOREACH(scan, hammer2_chain_tree, &layer->rbtree) {
			hammer2_dump_chain(scan, tab + 4, countp);
		}
	}
	if (chain->core && !TAILQ_EMPTY(&chain->core->layerq)) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE && chain->data)
			kprintf("%*.*s}(%s)\n", tab, tab, "",
				chain->data->ipdata.filename);
		else
			kprintf("%*.*s}\n", tab, tab, "");
	}
}
