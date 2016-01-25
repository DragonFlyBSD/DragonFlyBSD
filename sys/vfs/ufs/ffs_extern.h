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
 *	@(#)ffs_extern.h	8.6 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_extern.h,v 1.30 2000/01/09 22:40:02 mckusick Exp $
 * $DragonFly: src/sys/vfs/ufs/ffs_extern.h,v 1.15 2008/09/17 21:44:25 dillon Exp $
 */

#ifndef _VFS_UFS_EXTERN_H
#define	_VFS_UFS_EXTERN_H

/*
 * Sysctl values for the fast filesystem.
 */
#define FFS_REALLOCBLKS		3	/* block reallocation enabled */
#define FFS_ASYNCFREE		4	/* asynchronous block freeing enabled */
#define	FFS_MAXID		5	/* number of valid ffs ids */

#define FFS_NAMES { \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "doreallocblks", CTLTYPE_INT }, \
	{ "doasyncfree", CTLTYPE_INT }, \
}

struct buf;
struct fid;
struct fs;
struct inode;
struct malloc_type;
struct mount;
struct proc;
struct thread;
struct sockaddr;
struct statfs;
struct ucred;
struct vnode;
struct vop_balloc_args;
struct vop_bmap_args;
struct vop_fsync_args;
struct vop_reallocblks_args;

int	ffs_alloc(struct inode *,
	    ufs_daddr_t, ufs_daddr_t, int, struct ucred *, ufs_daddr_t *);
int	ffs_balloc(struct vop_balloc_args *);
int	ffs_blkatoff(struct vnode *, off_t, char **, struct buf **);
int	ffs_blkatoff_ra(struct vnode *, off_t, char **, struct buf **, int);
void	ffs_blkfree(struct inode *, ufs_daddr_t, long);
ufs_daddr_t ffs_blkpref(struct inode *, ufs_daddr_t, int, ufs_daddr_t *);
int	ffs_bmap(struct vop_bmap_args *);
void	ffs_clrblock(struct fs *, u_char *, ufs_daddr_t);
int	ffs_fhtovp(struct mount *, struct vnode *,
		   struct fid *, struct vnode **);
int	ffs_flushfiles(struct mount *, int);
void	ffs_fragacct(struct fs *, int, int32_t [], int);
int	ffs_freefile( struct vnode *, ino_t, int );
int	ffs_isblock(struct fs *, u_char *, ufs_daddr_t);
int	ffs_isfreeblock(struct fs *, unsigned char *, ufs_daddr_t);
int	ffs_mountfs(struct vnode *, struct mount *, struct malloc_type *);
int	ffs_mountroot(void);
int	ffs_reallocblks(struct vop_reallocblks_args *);
int	ffs_realloccg(struct inode *,
	    ufs_daddr_t, ufs_daddr_t, int, int, struct ucred *, struct buf **);
void	ffs_setblock(struct fs *, u_char *, ufs_daddr_t);
int	ffs_statfs(struct mount *, struct statfs *, struct ucred *);
int	ffs_sync(struct mount *, int);
int	ffs_truncate(struct vnode *, off_t, int, struct ucred *);
int	ffs_unmount(struct mount *, int);
int	ffs_update(struct vnode *, int);
int	ffs_valloc(struct vnode *, int, struct ucred *, struct vnode **);

int	ffs_vfree(struct vnode *, ino_t, int);
int	ffs_vget(struct mount *, struct vnode *, ino_t, struct vnode **);
int	ffs_vptofh(struct vnode *, struct fid *);

/*
 * Soft update function prototypes.
 */
void	softdep_initialize(void);
int	softdep_mount(struct vnode *, struct mount *, struct fs *);
int	softdep_flushfiles(struct mount *, int);
void	softdep_update_inodeblock(struct inode *, struct buf *, int);
void	softdep_load_inodeblock(struct inode *);
void	softdep_freefile(struct vnode *, ino_t, int);
void	softdep_setup_freeblocks(struct inode *, off_t);
void	softdep_setup_inomapdep(struct buf *, struct inode *, ino_t);
void	softdep_setup_blkmapdep(struct buf *, struct fs *, ufs_daddr_t);
void	softdep_setup_allocdirect(struct inode *, ufs_lbn_t, ufs_daddr_t,
	    ufs_daddr_t, long, long, struct buf *);
void	softdep_setup_allocindir_meta(struct buf *, struct inode *,
	    struct buf *, int, ufs_daddr_t);
void	softdep_setup_allocindir_page(struct inode *, ufs_lbn_t,
	    struct buf *, int, ufs_daddr_t, ufs_daddr_t, struct buf *);
void	softdep_fsync_mountdev(struct vnode *);
int	softdep_sync_metadata(struct vnode *, struct thread *);

#endif /* !_VFS_UFS_EXTERN_H */
