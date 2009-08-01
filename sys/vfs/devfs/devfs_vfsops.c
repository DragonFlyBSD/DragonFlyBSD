/*
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
#include <sys/vnode.h>
#include <sys/jail.h>
#include <vfs/devfs/devfs.h>

MALLOC_DECLARE(M_DEVFS);

extern struct vop_ops devfs_vnode_norm_vops;
extern struct vop_ops devfs_vnode_dev_vops;
extern struct lock 	devfs_lock;

static int	devfs_mount (struct mount *mp, char *path, caddr_t data,
				  struct ucred *cred);
static int	devfs_statfs (struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int	devfs_unmount (struct mount *mp, int mntflags);
int			devfs_root(struct mount *mp, struct vnode **vpp);

/*
 * VFS Operations.
 *
 * mount system call
 */
/* ARGSUSED */
static int
devfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	size_t size;

	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_mount() called!\n");

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);


	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_NOSTKMNT;
	mp->mnt_data = 0;
	vfs_getnewfsid(mp);

	size = sizeof("devfs") - 1;
	bcopy("devfs", mp->mnt_stat.f_mntfromname, size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	devfs_statfs(mp, &mp->mnt_stat, cred);

	//XXX: save other mount info passed from userland or so.
	mp->mnt_data = kmalloc(sizeof(struct devfs_mnt_data), M_DEVFS, M_WAITOK);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	DEVFS_MNTDATA(mp)->jailed = jailed(cred);
	DEVFS_MNTDATA(mp)->mntonnamelen = strlen(mp->mnt_stat.f_mntonname);
	DEVFS_MNTDATA(mp)->leak_count = 0;
	DEVFS_MNTDATA(mp)->root_node = devfs_allocp(Proot, "", NULL, mp, NULL);
	KKASSERT(DEVFS_MNTDATA(mp)->root_node);
	TAILQ_INIT(DEVFS_ORPHANLIST(mp));
	lockmgr(&devfs_lock, LK_RELEASE);

	vfs_add_vnodeops(mp, &devfs_vnode_norm_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &devfs_vnode_dev_vops, &mp->mnt_vn_spec_ops);

	devfs_debug(DEVFS_DEBUG_DEBUG, "calling devfs_mount_add\n");
	devfs_mount_add(DEVFS_MNTDATA(mp));

	return (0);
}

/*
 * unmount system call
 */
static int
devfs_unmount(struct mount *mp, int mntflags)
{
	int error = 0;
	int flags = 0;

	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_unmount() called!\n");

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	error = vflush(mp, 0, flags);

	if (error)
		return (error);

	devfs_debug(DEVFS_DEBUG_DEBUG, "There were %d devfs_node orphans left\n", devfs_tracer_orphan_count(mp, 1));
	devfs_debug(DEVFS_DEBUG_DEBUG, "There are %d devfs_node orphans left\n", devfs_tracer_orphan_count(mp, 0));
	devfs_mount_del(DEVFS_MNTDATA(mp));
	kfree(mp->mnt_data, M_DEVFS);

	return (0);
}

/*
 * Sets *vpp to the root procfs vnode, referenced and exclusively locked
 */
int
devfs_root(struct mount *mp, struct vnode **vpp)
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
devfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	devfs_debug(DEVFS_DEBUG_DEBUG, "(vfsops) devfs_stat() called!\n");
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;	/* avoid divide by zero in some df's */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;

	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}

	return (0);
}

static struct vfsops devfs_vfsops = {
	.vfs_mount =    	devfs_mount,
	.vfs_unmount =    	devfs_unmount,
	.vfs_root =    		devfs_root,
	.vfs_statfs =    	devfs_statfs,
	.vfs_sync =    		vfs_stdsync
};

VFS_SET(devfs_vfsops, devfs, VFCF_SYNTHETIC);
MODULE_VERSION(devfs, 1);
