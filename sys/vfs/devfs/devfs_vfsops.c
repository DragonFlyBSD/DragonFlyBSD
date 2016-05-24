/*
 * (MPSAFE)
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/mount.h>
#include <sys/namecache.h>
#include <sys/vnode.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/devfs.h>

MALLOC_DECLARE(M_DEVFS);

extern struct vop_ops devfs_vnode_norm_vops;
extern struct vop_ops devfs_vnode_dev_vops;
extern struct lock 	devfs_lock;

static int	devfs_vfs_mount (struct mount *mp, char *path, caddr_t data,
				  struct ucred *cred);
static int	devfs_vfs_statfs (struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int	devfs_vfs_unmount (struct mount *mp, int mntflags);
int		devfs_vfs_root(struct mount *mp, struct vnode **vpp);
static int	devfs_vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
static int	devfs_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
static int	devfs_vfs_vptofh(struct vnode *vp, struct fid *fhp);


/*
 * VFS Operations.
 *
 * mount system call
 */
static int
devfs_vfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct devfs_mount_info info;
	struct devfs_mnt_data *mnt;
	size_t size;
	int error;

	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_mount() called!\n");

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	if (data == NULL) {
		bzero(&info, sizeof(info));
	} else {
		if ((error = copyin(data, &info, sizeof(info))) != 0)
			return (error);
	}

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_NOSTKMNT | MNTK_ALL_MPSAFE;
	mp->mnt_data = NULL;
	vfs_getnewfsid(mp);

	size = sizeof("devfs") - 1;
	bcopy("devfs", mp->mnt_stat.f_mntfromname, size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	copyinstr(path, mp->mnt_stat.f_mntonname,
	    sizeof(mp->mnt_stat.f_mntonname) -1, &size);
	devfs_vfs_statfs(mp, &mp->mnt_stat, cred);

	/*
	 * XXX: save other mount info passed from userland or so.
	 */
	mnt = kmalloc(sizeof(*mnt), M_DEVFS, M_WAITOK | M_ZERO);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	mp->mnt_data = (qaddr_t)mnt;

	if (info.flags & DEVFS_MNT_JAIL)
		mnt->jailed = 1;
	else
		mnt->jailed = jailed(cred);

	mnt->leak_count = 0;
	mnt->file_count = 0;
	mnt->mp = mp;
	TAILQ_INIT(&mnt->orphan_list);
	mnt->root_node = devfs_allocp(Nroot, "", NULL, mp, NULL);
	KKASSERT(mnt->root_node);
	lockmgr(&devfs_lock, LK_RELEASE);

	vfs_add_vnodeops(mp, &devfs_vnode_norm_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &devfs_vnode_dev_vops, &mp->mnt_vn_spec_ops);

	devfs_debug(DEVFS_DEBUG_DEBUG, "calling devfs_mount_add\n");
	devfs_mount_add(mnt);

	return (0);
}

/*
 * unmount system call
 */
static int
devfs_vfs_unmount(struct mount *mp, int mntflags)
{
	int error = 0;
	int flags = 0;

	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_unmount() called!\n");

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	error = vflush(mp, 0, flags);

	if (error)
		return (error);
	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	devfs_tracer_orphan_count(mp, 1);
	lockmgr(&devfs_lock, LK_RELEASE);
	devfs_mount_del(DEVFS_MNTDATA(mp));
	kfree(mp->mnt_data, M_DEVFS);
	mp->mnt_data = NULL;

	return (0);
}

/*
 * Sets *vpp to the root procfs vnode, referenced and exclusively locked
 */
int
devfs_vfs_root(struct mount *mp, struct vnode **vpp)
{
	int ret;

	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_root() called!\n");
	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	ret = devfs_allocv(vpp, DEVFS_MNTDATA(mp)->root_node);
	lockmgr(&devfs_lock, LK_RELEASE);

	return ret;
}

/*
 * Get file system statistics.
 */
static int
devfs_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_stat() called!\n");
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;	/* avoid divide by zero in some df's */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = (DEVFS_MNTDATA(mp))?(DEVFS_MNTDATA(mp)->file_count):0;
	sbp->f_ffree = 0;

	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}

	return (0);
}

static int
devfs_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
		 struct fid *fhp, struct vnode **vpp)
{
	struct vnode		*vp;
	struct devfs_fid	*dfhp;

	dfhp = (struct devfs_fid *)fhp;

	if (dfhp->fid_gen != boottime.tv_sec)
		return EINVAL;

	vp = devfs_inode_to_vnode(mp, dfhp->fid_ino);

	if (vp == NULL)
		return ENOENT;

	*vpp = vp;
	return 0;
}

/*
 * Vnode pointer to File handle
 */
static int
devfs_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct devfs_node	*node;
	struct devfs_fid	*dfhp;

	if ((node = DEVFS_NODE(vp)) != NULL) {
		dfhp = (struct devfs_fid *)fhp;
		dfhp->fid_len = sizeof(struct devfs_fid);
		dfhp->fid_ino = node->d_dir.d_ino;
		dfhp->fid_gen = boottime.tv_sec;
	} else {
		return ENOENT;
	}

	return (0);
}

static int
devfs_vfs_vget(struct mount *mp, struct vnode *dvp,
	       ino_t ino, struct vnode **vpp)
{
	struct vnode *vp;
	vp = devfs_inode_to_vnode(mp, ino);

	if (vp == NULL)
		return ENOENT;

	*vpp = vp;
	return 0;
}

static void
devfs_vfs_ncpgen_set(struct mount *mp, struct namecache *ncp)
{
	ncp->nc_namecache_gen = mp->mnt_namecache_gen;
}

static int
devfs_vfs_ncpgen_test(struct mount *mp, struct namecache *ncp)
{
	return (ncp->nc_namecache_gen != mp->mnt_namecache_gen);
}

static struct vfsops devfs_vfsops = {
	.vfs_mount 	= devfs_vfs_mount,
	.vfs_unmount	= devfs_vfs_unmount,
	.vfs_root 	= devfs_vfs_root,
	.vfs_statfs	= devfs_vfs_statfs,
	.vfs_vget	= devfs_vfs_vget,
	.vfs_vptofh	= devfs_vfs_vptofh,
	.vfs_fhtovp	= devfs_vfs_fhtovp,
	.vfs_ncpgen_set	= devfs_vfs_ncpgen_set,
	.vfs_ncpgen_test	= devfs_vfs_ncpgen_test
};

VFS_SET(devfs_vfsops, devfs, VFCF_SYNTHETIC);
MODULE_VERSION(devfs, 1);
