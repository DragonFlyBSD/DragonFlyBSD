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
 * $DragonFly: src/sys/vfs/userfs/userfs_inode.c,v 1.1 2007/08/13 17:49:17 dillon Exp $
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

static MALLOC_DEFINE(M_USERFSINODE, "userfs-inode", "userfs inode");

int
user_vop_inactive(struct vop_inactive_args *ap)
{
	return(0);
}

int
user_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	if ((ip = vp->v_data) != NULL) {
		ump = ip->ump;

		userfs_ino_rb_tree_RB_REMOVE(&ump->rb_root, ip);
		vp->v_data = NULL;
		ip->vp = NULL;
		kfree(ip, M_USERFSINODE);
	}
	return(0);
}

int
user_vfs_vget(struct mount *mp, struct vnode *dvp,
	      ino_t ino, struct vnode **vpp)
{
	struct user_mount *ump = (void *)mp->mnt_data;
	struct user_inode *ip;
	struct vnode *vp;
	int error;

again:
	kprintf("LOOKUP ump %p ino %lld\n", ump, ino);
	ip = userfs_ino_rb_tree_RB_LOOKUP(&ump->rb_root, ino);
	if (ip) {
		vp = ip->vp;
		vref(vp);
		vn_lock(vp, LK_EXCLUSIVE|LK_RETRY);
		if (vp->v_flag & VRECLAIMED) {
			vput(vp);
			goto again;
		}
		*vpp = vp;
		return(0);
	}

	/*
	 * Locate the inode on-disk
	 */

	/*
	 * Create a vnode and in-memory inode.
	 */
	error = user_getnewvnode(mp, vpp, ino, VDIR);
	if (error == EAGAIN)
		goto again;
	return (error);
}

/*
 * Create a vnode and in-memory inode.  EAGAIN is returned if we race the
 * creation of the in-memory inode.
 *
 * A locked, referenced vnode is returned in *vpp if no error occurs.
 */
int
user_getnewvnode(struct mount *mp, struct vnode **vpp, ino_t ino,
		 enum vtype vtype)
{
	struct user_inode *ip;
	struct user_mount *ump;
	struct vnode *vp;
	int error;

	error = getnewvnode(VT_USERFS, mp, vpp, 0, LK_CANRECURSE);
	if (error)
		return (error);
	vp = *vpp;
	ip = kmalloc(sizeof(*ip), M_USERFSINODE, M_WAITOK|M_ZERO);
	vinitvmio(vp, 0, PAGE_SIZE, 0);

	ump = (void *)mp->mnt_data;

	/*
	 * Snap together the new vnode/inode and determine if we have
	 * raced the tree.
	 */
	if (userfs_ino_rb_tree_RB_LOOKUP(&ump->rb_root, ino)) {
		kfree(ip, M_USERFSINODE);
		vp->v_type = VBAD;
		vput(*vpp);
		return (EAGAIN);
	}
	ip->inum = ino;
	ip->vp = vp;
	ip->ump = ump;
	vp->v_data = ip;
	vp->v_type = vtype;
	userfs_ino_rb_tree_RB_INSERT(&ump->rb_root, ip);
	return(0);
}

