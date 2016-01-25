/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ufs_extern.h	8.10 (Berkeley) 5/14/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_extern.h,v 1.27.2.1 2000/12/28 11:01:46 ps Exp $
 * $DragonFly: src/sys/vfs/ufs/ufs_extern.h,v 1.16 2008/09/17 21:44:25 dillon Exp $
 */

#ifndef _VFS_UFS_EXTERN_H_
#define	_VFS_UFS_EXTERN_H_

struct componentname;
struct direct;
struct indir;
struct inode;
struct mount;
struct proc;
struct sockaddr;
struct ucred;
struct ufid;
struct vfsconf;
struct vnode;
struct vop_bmap_args;
struct vop_old_lookup_args;
struct vop_generic_args;
struct vop_inactive_args;
struct vop_reclaim_args;

int	ufs_vnoperate(struct vop_generic_args *);
int	ufs_vnoperatefifo(struct vop_generic_args *);
int	ufs_vnoperatespec(struct vop_generic_args *);

int	 ufs_bmap(struct vop_bmap_args *);
int	 ufs_bmaparray(struct vnode *, daddr_t, daddr_t *, struct indir *,
		int *, int *, int *);
int	 ufs_check_export(struct mount *, struct sockaddr *, 
			       int *, struct ucred **);
int	 ufs_fhtovp(struct mount *, struct vnode *,
				struct ufid *, struct vnode **);
int	 ufs_checkpath(struct inode *, struct inode *, struct ucred *);
void	 ufs_dirbad(struct inode *, doff_t, char *);
int	 ufs_dirbadentry(struct vnode *, struct direct *, int);
int	 ufs_dirempty(struct inode *, ino_t, struct ucred *);
void	 ufs_makedirentry(struct inode *, struct componentname *,
	    struct direct *);
int	 ufs_direnter(struct vnode *, struct vnode *, struct direct *,
	    struct componentname *, struct buf *);
int	 ufs_dirremove(struct vnode *, struct inode *, int, int);
int	 ufs_dirrewrite(struct inode *, struct inode *, ino_t, int, int);
int	 ufs_getlbns(struct vnode *, ufs_daddr_t, struct indir *, int *);
struct vnode *
	 ufs_ihashget(struct ufsmount *, cdev_t, ino_t);
int	 ufs_ihashcheck(struct ufsmount *, cdev_t, ino_t);
void	 ufs_ihashinit(struct ufsmount *);
void	 ufs_ihashuninit(struct ufsmount *);
int	 ufs_ihashins(struct ufsmount *, struct inode *);
struct vnode *
	 ufs_ihashlookup(struct ufsmount *, cdev_t, ino_t);
void	 ufs_ihashrem(struct ufsmount *, struct inode *);
int	 ufs_inactive(struct vop_inactive_args *);
int	 ufs_init(struct vfsconf *);
void	 ufs_itimes(struct vnode *vp);
int	 ufs_lookup(struct vop_old_lookup_args *);
int	 ufs_reclaim(struct vop_reclaim_args *);
int	 ufs_root(struct mount *, struct vnode **);
int	 ufs_start(struct mount *, int, struct thread *);
int	 ufs_vinit(struct mount *, struct vnode **);

/*
 * Soft update function prototypes.
 */
void	softdep_setup_directory_add(struct buf *, struct inode *, off_t,
	    ino_t, struct buf *);
void	softdep_change_directoryentry_offset(struct inode *, caddr_t,
	    caddr_t, caddr_t, int);
void	softdep_setup_remove(struct buf *,struct inode *, struct inode *,
	    int);
void	softdep_setup_directory_change(struct buf *, struct inode *,
	    struct inode *, ino_t, int);
void	softdep_change_linkcnt(struct inode *);
int	softdep_slowdown(struct vnode *);

#endif /* !_VFS_UFS_EXTERN_H_ */
