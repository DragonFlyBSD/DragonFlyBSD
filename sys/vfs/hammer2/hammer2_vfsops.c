/*
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
/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

static int	hammer2_init(struct vfsconf *conf);
static int	hammer2_mount(struct mount *mp, char *path, caddr_t data,
			      struct ucred *cred);
static int	hammer2_remount(struct mount *, char *, struct vnode *,
				struct ucred *);
static int	hammer2_unmount(struct mount *mp, int mntflags);
static int	hammer2_root(struct mount *mp, struct vnode **vpp);
static int	hammer2_statfs(struct mount *mp, struct statfs *sbp,
			       struct ucred *cred);
static int	hammer2_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int	hammer2_sync(struct mount *mp, int waitfor);
static int	hammer2_vget(struct mount *mp, struct vnode *dvp,
			     ino_t ino, struct vnode **vpp);
static int	hammer2_fhtovp(struct mount *mp, struct vnode *rootvp,
			       struct fid *fhp, struct vnode **vpp);
static int	hammer2_vptofh(struct vnode *vp, struct fid *fhp);
static int	hammer2_checkexp(struct mount *mp, struct sockaddr *nam,
				 int *exflagsp, struct ucred **credanonp);

static int	tmpfs_unmount(struct mount *, int);
static int	tmpfs_root(struct mount *, struct vnode **);

/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	/* From tmpfs */
	.vfs_root =			tmpfs_root,

	/* From  HAMMER2 */
	.vfs_init	= hammer2_init,
	.vfs_sync	= hammer2_sync,
	.vfs_mount	= hammer2_mount,
	.vfs_unmount	= hammer2_unmount,
#ifdef notyet
	.vfs_root 	= hammer2_root,
#endif
	.vfs_statfs	= hammer2_statfs,
	/* If we enable statvfs, we disappear in df, till we implement it. */
	/* That makes debugging difficult :) */
//	.vfs_statvfs	= hammer2_statvfs,
	.vfs_vget	= hammer2_vget,
	.vfs_vptofh	= hammer2_vptofh,
	.vfs_fhtovp	= hammer2_fhtovp,
	.vfs_checkexp	= hammer2_checkexp
};


MALLOC_DEFINE(M_HAMMER2, "HAMMER2-mount", "");

VFS_SET(hammer2_vfsops, hammer2, 0);
MODULE_VERSION(hammer2, 1);

static int
hammer2_init(struct vfsconf *conf)
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
static int
hammer2_mount(struct mount *mp, char *path, caddr_t data,
	      struct ucred *cred)
{
	struct hammer2_mount_info info;
	struct hammer2_mount *hmp;
	struct vnode *devvp;
	struct nlookupdata nd;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev, *label;
	int ronly;
	int error;
	int rc;

	hmp = NULL;
	dev = label = NULL;
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
		    ((label + 1) - dev) > done)
			return (EINVAL);
		*label = '\0';
		label++;
		if (*label == '\0')
			return (EINVAL);

		if (mp->mnt_flag & MNT_UPDATE) {
			/* Update mount */
			/* HAMMER2 implements NFS export via mountctl */
			hmp = MPTOH2(mp);
			devvp = hmp->hm_devvp;
			return hammer2_remount(mp, path, devvp, cred);
		}
	}

	kprintf("hammer2_mount2\n");
	/*
	 * New non-root mount
	 */
	/* Lookup name and verify it refers to a block device */
	error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
	if (error)
		return (error);
	error = nlookup(&nd);
	if (error)
		return (error);
	error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	if (error)
		return (error);
	nlookup_done(&nd);

	kprintf("hammer2_mount3\n");
	if (!vn_isdisk(devvp, &error)) {
		vrele(devvp);
		return (error);
	}

	kprintf("hammer2_mount4\n");
	/*
	 * Common path for new root/non-root mounts;
	 * devvp is a ref-ed by not locked vnode referring to the fs device
	 */

	kprintf("hammer2_mount5\n");
	error = vfs_mountedon(devvp);
	if (error) {
		vrele(devvp);
		return (error);
	}

	kprintf("hammer2_mount6\n");
	if (vcount(devvp) > 0) {
		vrele(devvp);
		return (EBUSY);
	}

	kprintf("hammer2_mount7\n");
	/*
	 * Open the fs device
	 */
	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	kprintf("hammer2_mount8\n");
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
	if (error) {
		vn_unlock(devvp);
		vrele(devvp);
		return (error);
	}
	/* This is correct; however due to an NFS quirk of my setup, FREAD
	 * is required... */

	kprintf("hammer2_mount9\n");
	/*
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD | FWRITE, FSCRED, NULL);
	 */
	error = VOP_OPEN(devvp, FREAD, FSCRED, NULL);
	vn_unlock(devvp);
	if (error) {
		vrele(devvp);
		return (error);
	}

#ifdef notyet
	/* VOP_IOCTL(EXTENDED_DISK_INFO, devvp); */
	/* if vn device, never use bdwrite(); */
	/* check if device supports BUF_CMD_READALL; */
	/* check if device supports BUF_CMD_WRITEALL; */
#endif

	kprintf("hammer2_mount10\n");
	hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
	/*mp->mnt_data = (qaddr_t) hmp;*/
	hmp->hm_mp = mp;
	/*hmp->hm_ronly = ronly;*/
	/*hmp->hm_devvp = devvp;*/
	lockinit(&hmp->hm_lk, "h2mp", 0, 0);
	kmalloc_create(&hmp->hm_inodes, "HAMMER2-inodes");
	kmalloc_create(&hmp->hm_ipstacks, "HAMMER2-ipstacks");
	
	kprintf("hammer2_mount11\n");

	int valid = 0;
	struct buf *bp;
	struct hammer2_volume_data *vd;
	do {
		rc = bread(devvp, 0, HAMMER2_PBUFSIZE, &bp);
		if (rc != 0) 
			break;
		
		vd = bp->b_data;
		if (vd->magic != HAMMER2_VOLUME_ID_HBO)
			break;
	} while(0);
	brelse(bp);
	vd = NULL;
	if (!valid) {
		/* XXX: close in the correct mode */
		VOP_CLOSE(devvp, FREAD);
		kfree(hmp, M_HAMMER2);
		return (EINVAL);
	}


	/*
	 * Filesystem subroutines are self-synchronized
	 */
	/*mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;*/

	kprintf("hammer2_mount 20\n");

	/* Setup root inode */
	hmp->hm_iroot = alloci(hmp);
	hmp->hm_iroot->type = HAMMER2_INODE_TYPE_DIR | HAMMER2_INODE_TYPE_ROOT;
	hmp->hm_iroot->inum = 1;

	/* currently rely on tmpfs routines */
	/*vfs_getnewfsid(mp);*/
	/*vfs_add_vnodeops(mp, &hammer2_vnode_vops, &mp->mnt_vn_norm_ops);*/
	/*vfs_add_vnodeops(mp, &hammer2_spec_vops, &mp->mnt_vn_spec_ops);*/
	/*vfs_add_vnodeops(mp, &hammer2_fifo_vops, &mp->mnt_vn_fifo_ops);*/

	copystr("hammer2", mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	copyinstr(path, mp->mnt_stat.f_mntonname,
		  sizeof(mp->mnt_stat.f_mntonname) - 1,
		  &size);

	kprintf("hammer2_mount 21\n");
	hammer2_statfs(mp, &mp->mnt_stat, cred);

	hammer2_inode_unlock_ex(hmp->hm_iroot);

	kprintf("hammer2_mount 22\n");
	return (tmpfs_mount(hmp, mp, path, data, cred));
}

static int
hammer2_remount(struct mount *mp, char *path, struct vnode *devvp,
                struct ucred *cred)
{
	return (0);
}

static int
hammer2_unmount(struct mount *mp, int mntflags)
{
	struct hammer2_mount *hmp;
	int flags;
	int error;

	kprintf("hammer2_unmount\n");

	hmp = MPTOH2(mp);
	flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	hammer2_mount_exlock(hmp);

	error = vflush(mp, 0, flags);

	/*
	 * Work to do:
	 *	1) Wait on the flusher having no work; heat up if needed
	 *	2) Scan inode RB tree till all the inodes are free
	 *	3) Destroy the kmalloc inode zone
	 *	4) Free the mount point
	 */

	kmalloc_destroy(&hmp->hm_inodes);
	kmalloc_destroy(&hmp->hm_ipstacks);

	hammer2_mount_unlock(hmp);

	// Tmpfs does this
	//kfree(hmp, M_HAMMER2);

	return (tmpfs_unmount(mp, mntflags));

	return (error);
}

static int
hammer2_vget(struct mount *mp, struct vnode *dvp,
	     ino_t ino, struct vnode **vpp)
{
	kprintf("hammer2_vget\n");
	return (EOPNOTSUPP);
}

static int
hammer2_root(struct mount *mp, struct vnode **vpp)
{
	struct hammer2_mount *hmp;
	int error;
	struct vnode *vp;

	kprintf("hammer2_root\n");

	hmp = MPTOH2(mp);
	hammer2_mount_lock_ex(hmp);
	if (hmp->hm_iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		vp = igetv(hmp->hm_iroot, &error);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}
	hammer2_mount_unlock(hmp);

	return (error);
}

static int
hammer2_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct hammer2_mount *hmp;

	kprintf("hammer2_statfs\n");

	hmp = MPTOH2(mp);

	sbp->f_iosize = PAGE_SIZE;
	sbp->f_bsize = PAGE_SIZE;

	sbp->f_blocks = 10;
	sbp->f_bavail = 10;
	sbp->f_bfree = 10;

	sbp->f_files = 10;
	sbp->f_ffree = 10;
	sbp->f_owner = 0;

	return (0);
}

static int
hammer2_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	kprintf("hammer2_statvfs\n");
	return (EOPNOTSUPP);
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
static int
hammer2_sync(struct mount *mp, int waitfor)
{
	struct hammer2_mount *hmp;
	struct hammer2_inode *ip;

	kprintf("hammer2_sync \n");

//	hmp = MPTOH2(mp);

	return (0);
}

static int
hammer2_vptofh(struct vnode *vp, struct fid *fhp)
{
	return (0);
}

static int
hammer2_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	return (0);
}

static int
hammer2_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	return (0);
}

/*
 * Efficient memory file system.
 *
 * tmpfs is a file system that uses NetBSD's virtual memory sub-system
 * (the well-known UVM) to store file data and metadata in an efficient
 * way.  This means that it does not follow the structure of an on-disk
 * file system because it simply does not need to.  Instead, it uses
 * memory-specific data structures and algorithms to automatically
 * allocate and release resources.
 */

#include <sys/conf.h>
#include <sys/param.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>

#include "hammer2.h"

/*
 * Default permission for root node
 */
#define TMPFS_DEFAULT_ROOT_MODE	(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

/* --------------------------------------------------------------------- */
int
tmpfs_node_ctor(void *obj, void *privdata, int flags)
{
	struct hammer2_node *node = (struct hammer2_node *)obj;

	node->tn_gen++;
	node->tn_size = 0;
	node->tn_status = 0;
	node->tn_flags = 0;
	node->tn_links = 0;
	node->tn_vnode = NULL;
	node->tn_vpstate = TMPFS_VNODE_WANT;
	bzero(&node->tn_spec, sizeof(node->tn_spec));

	return (1);
}

static void
tmpfs_node_dtor(void *obj, void *privdata)
{
	struct hammer2_node *node = (struct hammer2_node *)obj;
	node->tn_type = VNON;
	node->tn_vpstate = TMPFS_VNODE_DOOMED;
}

static void*
tmpfs_node_init(void *args, int flags)
{
	struct hammer2_node *node = (struct hammer2_node *)objcache_malloc_alloc(args, flags);
	if (node == NULL)
		return (NULL);
	node->tn_id = 0;

	lockinit(&node->tn_interlock, "tmpfs node interlock", 0, LK_CANRECURSE);
	node->tn_gen = karc4random();

	return node;
}

static void
tmpfs_node_fini(void *obj, void *args)
{
	struct hammer2_node *node = (struct hammer2_node *)obj;
	lockuninit(&node->tn_interlock);
	objcache_malloc_free(obj, args);
}

int
tmpfs_mount(struct hammer2_mount *hmp,
	    struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
//	struct tmpfs_mount *tmp;
	struct hammer2_node *root;
//	struct tmpfs_args args;
	vm_pindex_t pages;
	vm_pindex_t pages_limit;
	ino_t nodes;
	u_int64_t	maxfsize;
	int error;
	/* Size counters. */
	ino_t	nodes_max;
	off_t	size_max;
	size_t	maxfsize_max;
	size_t	size;

	/* Root node attributes. */
	uid_t	root_uid = cred->cr_uid;
	gid_t	root_gid = cred->cr_gid;
	mode_t	root_mode = (VREAD | VWRITE);

	if (mp->mnt_flag & MNT_UPDATE) {
		/* XXX: There is no support yet to update file system
		 * settings.  Should be added. */

		return EOPNOTSUPP;
	}

	kprintf("tmpfs_mount\n");

	/*
	 * mount info
	 */
//	bzero(&args, sizeof(args));
	size_max  = 0;
	nodes_max = 0;
	maxfsize_max = 0;

	if (path) {
		if (data) {
//			error = copyin(data, &args, sizeof(args));
//			if (error)
//				return (error);
		}
		/*
		size_max = args.ta_size_max;
		nodes_max = args.ta_nodes_max;
		maxfsize_max = args.ta_maxfsize_max;
		root_uid = args.ta_root_uid;
		root_gid = args.ta_root_gid;
		root_mode = args.ta_root_mode;
		*/
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	if (cred->cr_uid != 0) {
		root_mode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			root_mode |= VWRITE;
	}

	pages_limit = vm_swap_max + vmstats.v_page_count / 2;

	if (size_max == 0)
		pages = pages_limit / 2;
	else if (size_max < PAGE_SIZE)
		pages = 1;
	else if (OFF_TO_IDX(size_max) > pages_limit)
		pages = pages_limit;
	else
		pages = OFF_TO_IDX(size_max);

	if (nodes_max == 0)
		nodes = 3 + pages * PAGE_SIZE / 1024;
	else if (nodes_max < 3)
		nodes = 3;
	else if (nodes_max > pages)
		nodes = pages;
	else
		nodes = nodes_max;

	maxfsize = IDX_TO_OFF(pages_limit);
	if (maxfsize_max != 0 && maxfsize > maxfsize_max)
		maxfsize = maxfsize_max;

	/* Allocate the tmpfs mount structure and fill it. */
//	tmp = kmalloc(sizeof(*tmp), M_HAMMER2, M_WAITOK | M_ZERO);

	struct hammer2_mount *tmp = hmp;
	lockinit(&(tmp->allnode_lock), "tmpfs allnode lock", 0, LK_CANRECURSE);
	tmp->tm_nodes_max = nodes;
	tmp->tm_nodes_inuse = 0;
	tmp->tm_maxfilesize = maxfsize;
	LIST_INIT(&tmp->tm_nodes_used);

	tmp->tm_pages_max = pages;
	tmp->tm_pages_used = 0;

	kmalloc_create(&tmp->tm_node_zone, "tmpfs node");
	kmalloc_create(&tmp->tm_dirent_zone, "tmpfs dirent");
	kmalloc_create(&tmp->tm_name_zone, "tmpfs name zone");

	kmalloc_raise_limit(tmp->tm_node_zone, sizeof(struct hammer2_node) *
			    tmp->tm_nodes_max);

	tmp->tm_node_zone_malloc_args.objsize = sizeof(struct hammer2_node);
	tmp->tm_node_zone_malloc_args.mtype = tmp->tm_node_zone;

	tmp->tm_dirent_zone_malloc_args.objsize = sizeof(struct hammer2_dirent);
	tmp->tm_dirent_zone_malloc_args.mtype = tmp->tm_dirent_zone;

	tmp->tm_dirent_pool =  objcache_create( "tmpfs dirent cache",
	    0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc, objcache_malloc_free,
	    &tmp->tm_dirent_zone_malloc_args);
	tmp->tm_node_pool = objcache_create( "tmpfs node cache",
	    0, 0,
	    tmpfs_node_ctor, tmpfs_node_dtor, NULL,
	    tmpfs_node_init, tmpfs_node_fini,
	    &tmp->tm_node_zone_malloc_args);

	/* Allocate the root node. */
	error = tmpfs_alloc_node(tmp, VDIR, root_uid, root_gid,
				 root_mode & ALLPERMS, NULL, NULL,
				 VNOVAL, VNOVAL, &root);

	/*
	 * We are backed by swap, set snocache chflags flag so we
	 * don't trip over swapcache.
	 */
	root->tn_flags = SF_NOCACHE;

	if (error != 0 || root == NULL) {
	    objcache_destroy(tmp->tm_node_pool);
	    objcache_destroy(tmp->tm_dirent_pool);
	    kfree(tmp, M_HAMMER2);
	    return error;
	}
	KASSERT(root->tn_id >= 0, ("tmpfs root with invalid ino: %d", (int)root->tn_id));
	tmp->tm_root = root;

	mp->mnt_flag |= MNT_LOCAL;
#if 0
	mp->mnt_kern_flag |= MNTK_RD_MPSAFE | MNTK_WR_MPSAFE | MNTK_GA_MPSAFE  |
			     MNTK_IN_MPSAFE | MNTK_SG_MPSAFE;
#endif
	mp->mnt_kern_flag |= MNTK_RD_MPSAFE | MNTK_GA_MPSAFE | MNTK_SG_MPSAFE;
	mp->mnt_kern_flag |= MNTK_WR_MPSAFE;
	mp->mnt_kern_flag |= MNTK_NOMSYNC;
	mp->mnt_kern_flag |= MNTK_THR_SYNC;
	mp->mnt_data = (qaddr_t)tmp;
	vfs_getnewfsid(mp);

	vfs_add_vnodeops(mp, &tmpfs_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &tmpfs_fifo_vops, &mp->mnt_vn_fifo_ops);

	hammer2_statfs(mp, &mp->mnt_stat, cred);

	return 0;
}

/* --------------------------------------------------------------------- */

/* ARGSUSED2 */
static int
tmpfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;
	int found;
	struct hammer2_mount *tmp;
	struct hammer2_node *node;

	kprintf("tmpfs_umount\n");

	/* Handle forced unmounts. */
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	tmp = VFS_TO_TMPFS(mp);

	/*
	 * Finalize all pending I/O.  In the case of tmpfs we want
	 * to throw all the data away so clean out the buffer cache
	 * and vm objects before calling vflush().
	 */
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_type == VREG && node->tn_vnode) {
			++node->tn_links;
			TMPFS_NODE_LOCK(node);
			vx_get(node->tn_vnode);
			tmpfs_truncate(node->tn_vnode, 0);
			vx_put(node->tn_vnode);
			TMPFS_NODE_UNLOCK(node);
			--node->tn_links;
		}
	}
	error = vflush(mp, 0, flags);
	if (error != 0)
		return error;

	/*
	 * First pass get rid of all the directory entries and
	 * vnode associations.  The directory structure will
	 * remain via the extra link count representing tn_dir.tn_parent.
	 *
	 * No vnodes should remain after the vflush above.
	 */
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		++node->tn_links;
		TMPFS_NODE_LOCK(node);
		if (node->tn_type == VDIR) {
			struct tmpfs_dirent *de;

			while (!TAILQ_EMPTY(&node->tn_dir.tn_dirhead)) {
				de = TAILQ_FIRST(&node->tn_dir.tn_dirhead);
				tmpfs_dir_detach(node, de);
				tmpfs_free_dirent(tmp, de);
				node->tn_size -= sizeof(struct hammer2_dirent);
			}
		}
		KKASSERT(node->tn_vnode == NULL);
#if 0
		vp = node->tn_vnode;
		if (vp != NULL) {
			tmpfs_free_vp(vp);
			vrecycle(vp);
			node->tn_vnode = NULL;
		}
#endif
		TMPFS_NODE_UNLOCK(node);
		--node->tn_links;
	}

	/*
	 * Now get rid of all nodes.  We can remove any node with a
	 * link count of 0 or any directory node with a link count of
	 * 1.  The parents will not be destroyed until all their children
	 * have been destroyed.
	 *
	 * Recursion in tmpfs_free_node() can further modify the list so
	 * we cannot use a next pointer here.
	 *
	 * The root node will be destroyed by this loop (it will be last).
	 */
	while (!LIST_EMPTY(&tmp->tm_nodes_used)) {
		found = 0;
		LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
			if (node->tn_links == 0 ||
			    (node->tn_links == 1 && node->tn_type == VDIR)) {
				TMPFS_NODE_LOCK(node);
				tmpfs_free_node(tmp, node);
				/* eats lock */
				found = 1;
				break;
			}
		}
		if (found == 0) {
			kprintf("tmpfs: Cannot free entire node tree!");
			break;
		}
	}

	KKASSERT(tmp->tm_root == NULL);

	objcache_destroy(tmp->tm_dirent_pool);
	objcache_destroy(tmp->tm_node_pool);

	kmalloc_destroy(&tmp->tm_name_zone);
	kmalloc_destroy(&tmp->tm_dirent_zone);
	kmalloc_destroy(&tmp->tm_node_zone);

	tmp->tm_node_zone = tmp->tm_dirent_zone = NULL;

	lockuninit(&tmp->allnode_lock);
	KKASSERT(tmp->tm_pages_used == 0);
	KKASSERT(tmp->tm_nodes_inuse == 0);

	/* Throw away the hammer2_mount structure. */
	kfree(tmp, M_HAMMER2);
	mp->mnt_data = NULL;

	mp->mnt_flag &= ~MNT_LOCAL;
	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_root(struct mount *mp, struct vnode **vpp)
{
	struct hammer2_mount *tmp;
	int error;

	kprintf("tmpfs_root\n");

	tmp = VFS_TO_TMPFS(mp);
	if (tmp->tm_root == NULL) {
		kprintf("tmpfs_root: called without root node %p\n", mp);
		print_backtrace(-1);
		*vpp = NULL;
		error = EINVAL;
	} else {
		error = tmpfs_alloc_vp(mp, tmp->tm_root, LK_EXCLUSIVE, vpp);
		(*vpp)->v_flag |= VROOT;
		(*vpp)->v_type = VDIR;
	}
	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_fhtovp(struct mount *mp, struct vnode *rootvp, struct fid *fhp, struct vnode **vpp)
{
	boolean_t found;
	struct tmpfs_fid *tfhp;
	struct hammer2_mount *tmp;
	struct hammer2_node *node;

	tmp = VFS_TO_TMPFS(mp);

	tfhp = (struct tmpfs_fid *)fhp;
	if (tfhp->tf_len != sizeof(struct tmpfs_fid))
		return EINVAL;

	if (tfhp->tf_id >= tmp->tm_nodes_max)
		return EINVAL;

	found = FALSE;

	TMPFS_LOCK(tmp);
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_id == tfhp->tf_id &&
		    node->tn_gen == tfhp->tf_gen) {
			found = TRUE;
			break;
		}
	}
	TMPFS_UNLOCK(tmp);

	if (found)
		return (tmpfs_alloc_vp(mp, node, LK_EXCLUSIVE, vpp));

	return (EINVAL);
}

/* --------------------------------------------------------------------- */

static int
tmpfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct hammer2_node *node;
	struct tmpfs_fid tfh;
	node = VP_TO_TMPFS_NODE(vp);
	memset(&tfh, 0, sizeof(tfh));
	tfh.tf_len = sizeof(struct tmpfs_fid);
	tfh.tf_gen = node->tn_gen;
	tfh.tf_id = node->tn_id;
	memcpy(fhp, &tfh, sizeof(tfh));
	return (0);
}
