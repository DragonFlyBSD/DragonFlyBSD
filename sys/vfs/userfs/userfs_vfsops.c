/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/vfs/userfs/userfs_vfsops.c,v 1.1 2007/08/13 17:49:17 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/syslink.h>
#include <sys/syslink_msg2.h>
#include <sys/syslink_vfs.h>
#include "userfs.h"

/*
 * Red-Black tree support - used to index inode numbers
 */

static int
user_ino_rb_compare(struct user_inode *ip1, struct user_inode *ip2)
{
	if (ip1->inum < ip2->inum)
		return(-1);
	if (ip1->inum > ip2->inum)
		return(1);
	return(0);
}

RB_GENERATE2(userfs_ino_rb_tree, user_inode, rb_node,
             user_ino_rb_compare, ino_t, inum);

/*
 * VFS ABI
 */
static void	user_free_ump(struct mount *mp);

static int	user_vfs_mount(struct mount *mp, char *path, caddr_t data,
				struct ucred *cred);
static int	user_vfs_unmount(struct mount *mp, int mntflags);
static int	user_vfs_root(struct mount *mp, struct vnode **vpp);
static int	user_vfs_statfs(struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int	user_vfs_sync(struct mount *mp, int waitfor);

static struct vfsops userfs_vfsops = {
	.vfs_mount	= user_vfs_mount,
	.vfs_unmount	= user_vfs_unmount,
	.vfs_root 	= user_vfs_root,
	.vfs_statfs	= user_vfs_statfs,
	.vfs_sync	= user_vfs_sync,
	.vfs_vget	= user_vfs_vget
};

static MALLOC_DEFINE(M_USERFSMNT, "userfs-mount", "userfs mount");

VFS_SET(userfs_vfsops, userfs, 0);
MODULE_VERSION(userfs, 1);

static int
user_vfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct userfs_mount_info info;
	struct user_mount *ump;
	int error;

	if ((error = copyin(data, &info, sizeof(info))) != 0)
		return (error);

	ump = kmalloc(sizeof(*ump), M_USERFSMNT, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)ump;
	ump->mp = mp;
	RB_INIT(&ump->rb_root);

	error = syslink_ukbackend(&info.cfd, &ump->sldesc);
	if (error == 0) {
		error = user_getnewvnode(mp, &ump->rootvp, 0, VDIR);
		if (ump->rootvp)
			vn_unlock(ump->rootvp);
	}

	/*
	 * Allocate the syslink pipe
	 */
	if (error == 0)
		error = copyout(&info, data, sizeof(info));

	/*
	 * Setup the rest of the mount or clean up after an error.
	 */
	if (error) {
		user_free_ump(mp);
	} else {
		mp->mnt_iosize_max = MAXPHYS;
		mp->mnt_kern_flag |= MNTK_FSMID;
		mp->mnt_stat.f_fsid.val[0] = 0;	/* XXX */
		mp->mnt_stat.f_fsid.val[1] = 0;	/* XXX */
		vfs_getnewfsid(mp);		/* XXX */
		mp->mnt_maxsymlinklen = 255;
		mp->mnt_flag |= MNT_LOCAL;

		vfs_add_vnodeops(mp, &userfs_vnode_vops, &mp->mnt_vn_norm_ops);
	}
	return (error);
}

static int
user_vfs_unmount(struct mount *mp, int mntflags)
{
#if 0
	struct user_mount *ump = (void *)mp->mnt_data;
#endif
	int flags;
	int error;

	/*
	 * Clean out the vnodes
	 */
	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 1, flags);
	if (error)
		return (error);

	/*
	 * Clean up the syslink descriptor
	 */
	user_free_ump(mp);
	return(0);
}

static void
user_free_ump(struct mount *mp)
{
	struct user_mount *ump = (void *)mp->mnt_data;

	if (ump->rootvp) {
		vrele(ump->rootvp);
		ump->rootvp = NULL;
	}
	if (ump->sldesc) {
		syslink_kclose(ump->sldesc);
		ump->sldesc = NULL;
	}
	mp->mnt_data = NULL;
	ump->mp = NULL;
	kfree(ump, M_USERFSMNT);
}

/*
 * VFS_ROOT() is called during the mount sequence and since the process
 * is stuck in the kernel doing the mount, we will deadlock if we try
 * to talk to it now.  Just allocate and return a vnode.
 */
static int
user_vfs_root(struct mount *mp, struct vnode **vpp)
{
	struct user_mount *ump = (void *)mp->mnt_data;
	struct vnode *vp;

	vp = ump->rootvp;
	vref(vp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	*vpp = vp;
	return (0);
}

static int
user_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct user_mount *ump = (void *)mp->mnt_data;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS, SLVFS_CMD_VFS_STATFS);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_root\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VFS_STATFS|SE_CMDF_REPLY) &&
		    par->se_bytes == sizeof(*par) + sizeof(struct statfs)
		) {
			*sbp = *(struct statfs *)(par + 1);
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

static int
user_vfs_sync(struct mount *mp, int waitfor)
{
	return(0);
}

