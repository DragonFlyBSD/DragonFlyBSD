/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)procfs_vfsops.c	8.7 (Berkeley) 5/10/95
 *
 * $FreeBSD: src/sys/miscfs/procfs/procfs_vfsops.c,v 1.32.2.1 2001/10/15 20:42:01 des Exp $
 */

/*
 * procfs VFS interface
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <vfs/procfs/procfs.h>

extern struct vop_ops procfs_vnode_vops;

static int	procfs_mount (struct mount *mp, char *path, caddr_t data,
				  struct ucred *cred);
static int	procfs_statfs (struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int	procfs_unmount (struct mount *mp, int mntflags);

/*
 * VFS Operations.
 *
 * mount system call
 */
/* ARGSUSED */
static int
procfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	size_t size;
	int error;

	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	if (mp->mnt_vfc->vfc_refcount == 1 && (error = at_exit(procfs_exit))) {
		kprintf("procfs:  cannot register procfs_exit with at_exit\n");
		return(error);
	}

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_NOSTKMNT;
	mp->mnt_data = NULL;
	vfs_getnewfsid(mp);

	size = sizeof("procfs") - 1;
	bcopy("procfs", mp->mnt_stat.f_mntfromname, size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	procfs_statfs(mp, &mp->mnt_stat, cred);
	vfs_add_vnodeops(mp, &procfs_vnode_vops, &mp->mnt_vn_norm_ops);

	return (0);
}

/*
 * unmount system call
 */
static int
procfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	error = vflush(mp, 0, flags);
	if (error)
		return (error);

	if (mp->mnt_vfc->vfc_refcount == 1)
		rm_at_exit(procfs_exit);

	return (0);
}

/*
 * Sets *vpp to the root procfs vnode, referenced and exclusively locked.
 */
int
procfs_root(struct mount *mp, struct vnode **vpp)
{
	return (procfs_allocvp(mp, vpp, 0, Proot));
}

/*
 * Get file system statistics.
 */
static int
procfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	sbp->f_bsize = PAGE_SIZE;
	sbp->f_iosize = PAGE_SIZE;
	sbp->f_blocks = 1;	/* avoid divide by zero in some df's */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = maxproc;			/* approx */
	sbp->f_ffree = maxproc - nprocs;	/* approx */

	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}

	return (0);
}

static struct vfsops procfs_vfsops = {
	.vfs_mount =    	procfs_mount,
	.vfs_unmount =    	procfs_unmount,
	.vfs_root =    		procfs_root,
	.vfs_statfs =    	procfs_statfs,
};

VFS_SET(procfs_vfsops, procfs, VFCF_SYNTHETIC | VFCF_MPSAFE);
MODULE_VERSION(procfs, 1);
