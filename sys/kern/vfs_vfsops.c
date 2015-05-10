/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
/*
 * Implement mp->mnt_ops function call wrappers.
 *
 * These wrappers are responsible for handling all MPSAFE issues related to
 * a mount.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/domain.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/vfsops.h>
#include <sys/sysmsg.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

/*
 * MPSAFE
 */
int
vfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_mount)(mp, path, data, cred);
	VFS_MPUNLOCK(mp);

	return (error);
}

/*
 * MPSAFE
 */
int
vfs_start(struct mount *mp, int flags)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK_FLAG(mp, MNTK_ST_MPSAFE);
	error = (mp->mnt_op->vfs_start)(mp, flags);
	if (error == 0) {
		/* do not call vfs_acinit on mount updates */
		if ((mp->mnt_flag & MNT_UPDATE) == 0)
			VFS_ACINIT(mp,error);
	}
	VFS_MPUNLOCK(mp);
	if (error == EMOUNTEXIT)
		error = 0;
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_unmount(struct mount *mp, int mntflags)
{
	VFS_MPLOCK_DECLARE;
	int error;
	int flags;

	VFS_MPLOCK(mp);
	VFS_ACDONE(mp);
	flags = mp->mnt_kern_flag;
	error = (mp->mnt_op->vfs_unmount)(mp, mntflags);
	if (error == 0)
		vn_syncer_thr_stop(mp);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_root(struct mount *mp, struct vnode **vpp)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_root)(mp, vpp);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
	     struct ucred *cred)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_quotactl)(mp, cmds, uid, arg, cred);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_statfs)(mp, sbp, cred);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_statvfs)(mp, sbp, cred);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_sync(struct mount *mp, int waitfor)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_sync)(mp, waitfor);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_vget(struct mount *mp, struct vnode *dvp, ino_t ino, struct vnode **vpp)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_vget)(mp, dvp, ino, vpp);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	   struct fid *fhp, struct vnode **vpp)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_fhtovp)(mp, rootvp, fhp, vpp);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_checkexp(struct mount *mp, struct sockaddr *nam,
	     int *extflagsp, struct ucred **credanonp)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_checkexp)(mp, nam, extflagsp, credanonp);
	VFS_MPUNLOCK(mp);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(vp->v_mount);
	error = (vp->v_mount->mnt_op->vfs_vptofh)(vp, fhp);
	VFS_MPUNLOCK(vp->v_mount);
	return (error);
}

/*
 * MPSAFE
 */
int
vfs_init(struct vfsconf *vfc)
{
	int error;

	get_mplock();
	error = (vfc->vfc_vfsops->vfs_init)(vfc);
	rel_mplock();

	return (error);
}

/*
 * MPSAFE
 */
int
vfs_uninit(struct vfsconf *vfc, struct vfsconf *vfsp)
{
	int error;

	get_mplock();
	error = (vfc->vfc_vfsops->vfs_uninit)(vfsp);
	rel_mplock();

	return (error);
}

/*
 * MPSAFE
 */
int
vfs_extattrctl(struct mount *mp, int cmd, struct vnode *vp,
		 int attrnamespace, const char *attrname,
		 struct ucred *cred)
{
	VFS_MPLOCK_DECLARE;
	int error;

	VFS_MPLOCK(mp);
	error = (mp->mnt_op->vfs_extattrctl)(mp, cmd, vp,
					     attrnamespace, attrname,
					     cred);
	VFS_MPUNLOCK(mp);
	return (error);
}
