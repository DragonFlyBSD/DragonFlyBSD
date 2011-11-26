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
 * $DragonFly: src/sys/vfs/userfs/userfs.h,v 1.2 2007/09/07 21:42:59 dillon Exp $
 */

#include <sys/syslink_msg.h>
#include <sys/tree.h>
#ifdef _KERNEL
#include <sys/lockf.h>
#endif

#include <sys/syslink_msg2.h>

/*
 * userfs mount information structure, passed to mount(2).  The userfs
 * will return a syslink messaging descriptor representing the user<->kernel
 * fs interface.  The user filesystem usually creates multiple LWPs to
 * be able to handle requests in parallel.
 */
struct userfs_mount_info {
	int	cfd;		/* returned syslink descriptor */
	void	*mmbase;	/* FUTURE - memory mapped BIO interface */
	size_t	mmsize;		/* FUTURE - memory mapped BIO interface */
	ino_t	root_ino;	/* root inode information */
	void	*root_uptr;	/* root inode information */
};

#define USERFS_BSIZE	16384	/* used to break uio's into bufs */
#define USERFS_BMASK	(USERFS_BSIZE-1)

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct userfs_ino_rb_tree;
struct user_inode;
RB_PROTOTYPE2(userfs_ino_rb_tree, user_inode, rb_node,
	     userfs_ino_rb_compare, ino_t);

struct user_mount {
	struct sldesc *sldesc;	/* syslink communications descriptor */
	struct vnode *rootvp;	/* root vnode for filesystem */
	struct mount *mp;
	RB_HEAD(userfs_ino_rb_tree, user_inode) rb_root;
};

struct user_inode {
	RB_ENTRY(user_inode) rb_node;
	struct user_mount *ump;
	struct vnode *vp;
	off_t	filesize;	/* current file size */
	void	*uptr;		/* userland supplied pointer */
	ino_t	inum;		/* inode number */
	struct lockf lockf;
};

#endif

#if defined(_KERNEL)

extern struct vop_ops userfs_vnode_vops;
int	user_vop_inactive(struct vop_inactive_args *);
int	user_vop_reclaim(struct vop_reclaim_args *);
int	user_vfs_vget(struct mount *mp, struct vnode *dvp,
			ino_t ino, struct vnode **vpp);

void	user_elm_push_vnode(syslink_elm_t par, struct vnode *vp);
void	user_elm_push_offset(syslink_elm_t par, off_t offset);
void	user_elm_push_bio(syslink_elm_t par, int cmd, int bcount);
void	user_elm_push_mode(struct syslink_elm *par, mode_t mode);
void	user_elm_push_cred(struct syslink_elm *par, struct ucred *cred);
void	user_elm_push_vattr(struct syslink_elm *par, struct vattr *vattr);
void	user_elm_push_nch(struct syslink_elm *par, struct nchandle *nch);


int	user_getnewvnode(struct mount *mp, struct vnode **vpp, ino_t ino,
			 enum vtype vtype);

int	user_elm_parse_vattr(struct syslink_elm *par, struct vattr *vat);

#endif

