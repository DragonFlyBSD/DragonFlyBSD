/*
 * Copyright (c) 1989, 1991, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)mount.h	8.21 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/sys/mount.h,v 1.89.2.7 2003/04/04 20:35:57 tegge Exp $
 */

#ifndef _SYS_MOUNT_H_
#define _SYS_MOUNT_H_

#include <sys/ucred.h>
#include <sys/tree.h>

#ifndef _KERNEL
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
#include <sys/stat.h>
#endif /* !_POSIX_C_SOURCE */
#endif /* !_KERNEL */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_NAMECACHE_H_
#include <sys/namecache.h>
#endif
#ifndef _SYS_STATVFS_H_
#include <sys/statvfs.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#include <sys/vfs_quota.h>
#endif

struct thread;
struct journal;
struct vop_ops;
struct vop_mountctl_args;
struct statvfs;
struct vmntvnodescan_info;

typedef struct fsid { int32_t val[2]; } fsid_t;	/* file system id type */

/*
 * File identifier.  These are unique per filesystem on a single machine.
 *
 * fix_ext is also used by HAMMER.
 */
#define	MAXFIDSZ	16

struct fid {
	u_short		fid_len;		/* length of data in bytes */
	u_short		fid_ext;		/* extended data 	   */
	char		fid_data[MAXFIDSZ];	/* data (variable length) */
};

/*
 * file system statistics
 */

#define MFSNAMELEN	16	/* length of fs type name, including null */
#define	MNAMELEN	80	/* length of buffer for returned name */

struct statfs {
	long	f_spare2;		/* placeholder */
	long	f_bsize;		/* fundamental file system block size */
	long	f_iosize;		/* optimal transfer block size */
	long	f_blocks;		/* total data blocks in file system */
	long	f_bfree;		/* free blocks in fs */
	long	f_bavail;		/* free blocks avail to non-superuser */
	long	f_files;		/* total file nodes in file system */
	long	f_ffree;		/* free file nodes in fs */
	fsid_t	f_fsid;			/* file system id */
	uid_t	f_owner;		/* user that mounted the filesystem */
	int	f_type;			/* type of filesystem */
	int	f_flags;		/* copy of mount exported flags */
	long    f_syncwrites;		/* count of sync writes since mount */
	long    f_asyncwrites;		/* count of async writes since mount */
	char	f_fstypename[MFSNAMELEN]; /* fs type name */
	char	f_mntonname[MNAMELEN];	/* directory on which mounted */
	long    f_syncreads;		/* count of sync reads since mount */
	long    f_asyncreads;		/* count of async reads since mount */
	short	f_spares1;		/* unused spare */
	char	f_mntfromname[MNAMELEN];/* mounted filesystem */
	short	f_spares2;		/* unused spare */
	long    f_spare[2];		/* unused spare */
};

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * bio_ops are associated with the mount structure and used in conjuction
 * with the b_dep field in a buffer.  Currently softupdates and HAMMER
 * utilize this field.
 *
 * All bio_ops callbacks must be MPSAFE and could be called without
 * the mplock.
 */
struct buf;

struct bio_ops {
	TAILQ_ENTRY(bio_ops) entry;
	void	(*io_start) (struct buf *);
	void	(*io_complete) (struct buf *);
	void	(*io_deallocate) (struct buf *);
	int	(*io_fsync) (struct vnode *);
	int	(*io_sync) (struct mount *);
	void	(*io_movedeps) (struct buf *, struct buf *);
	int	(*io_countdeps) (struct buf *, int);
	int	(*io_checkread) (struct buf *);
	int	(*io_checkwrite) (struct buf *);
};

#endif

/*
 * vfs quota accounting
 *
 * uids and gids often come in contiguous blocks; use a small linear
 * array as the basic in-memory accounting allocation unit
 */
#define ACCT_CHUNK_BITS	5
#define ACCT_CHUNK_NIDS	(1<<ACCT_CHUNK_BITS)
#define ACCT_CHUNK_MASK	(ACCT_CHUNK_NIDS - 1)

struct ac_counters {
	uint64_t space;
	uint64_t limit;
};

struct ac_unode {
	RB_ENTRY(ac_unode)	rb_entry;
	uid_t			left_bits;
	struct ac_counters	uid_chunk[ACCT_CHUNK_NIDS];
};

struct ac_gnode {
	RB_ENTRY(ac_gnode)	rb_entry;
	gid_t			left_bits;
	struct ac_counters	gid_chunk[ACCT_CHUNK_NIDS];
};

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct vfs_acct {
	RB_HEAD(ac_utree,ac_unode)	ac_uroot;
	RB_HEAD(ac_gtree,ac_gnode)	ac_groot;
	uint64_t			ac_bytes;	/* total bytes used */
	uint64_t			ac_limit;
	struct spinlock			ac_spin;	/* protective spinlock */
};


/*
 * Structure per mounted file system.  Each mounted file system has an
 * array of operations and an instance record.  The file systems are
 * put on a doubly linked list.
 *
 * NOTE: mnt_nvnodelist and mnt_reservedvnlist.  At the moment vnodes
 * are linked into mnt_nvnodelist.  At some point in the near future the
 * vnode list will be split into a 'dirty' and 'clean' list. mnt_nvnodelist
 * will become the dirty list and mnt_reservedvnlist will become the 'clean'
 * list.  Filesystem kld's syncing code should remain compatible since
 * they only need to scan the dirty vnode list (nvnodelist -> dirtyvnodelist).
 *
 * NOTE: All VFSs must at least populate mnt_vn_ops or those VOP ops that
 * only take namecache pointers will not be able to find their operations
 * vector via namecache->nc_mount.
 *
 * MPSAFE NOTES: mnt_lock interlocks mounting and unmounting operations.
 *
 *		 mnt_token interlocks operations which adjust the mount
 *		 structure and will also be held through VFS operations
 *		 for VFSes not flagged MPSAFE.
 */
TAILQ_HEAD(vnodelst, vnode);
TAILQ_HEAD(journallst, journal);

struct mount {
	TAILQ_ENTRY(mount) mnt_list;		/* mount list */
	struct vfsops	*mnt_op;		/* operations on fs */
	struct vfsconf	*mnt_vfc;		/* configuration info */
	long		mnt_namecache_gen;	/* ++ to clear negative hits */
	struct vnode	*mnt_syncer;		/* syncer vnode */
	struct syncer_ctx *mnt_syncer_ctx;	/* syncer process context */
	struct vnodelst	mnt_nvnodelist;		/* list of vnodes this mount */
	TAILQ_HEAD(,vmntvnodescan_info) mnt_vnodescan_list;
	struct lock	mnt_lock;		/* mount structure lock */
	int		mnt_flag;		/* flags shared with user */
	int		mnt_kern_flag;		/* kernel only flags */
	int		mnt_maxsymlinklen;	/* max size of short symlink */
	struct statfs	mnt_stat;		/* cache of filesystem stats */
	struct statvfs	mnt_vstat;		/* extended stats */
	qaddr_t		mnt_data;		/* private data */
	time_t		mnt_time;		/* last time written*/
	u_int		mnt_iosize_max;		/* max IO request size */
	struct vnodelst	mnt_reservedvnlist;	/* (future) dirty vnode list */
	int		mnt_nvnodelistsize;	/* # of vnodes on this mount */

	/*
	 * ops vectors have a fixed stacking order.  All primary calls run
	 * through mnt_vn_ops.  This field is typically assigned to 
	 * mnt_vn_norm_ops.  If journaling has been enabled this field is
	 * usually assigned to mnt_vn_journal_ops.
	 */
	struct vop_ops	*mnt_vn_use_ops;	/* current ops set */

	struct vop_ops	*mnt_vn_coherency_ops;	/* cache coherency ops */
	struct vop_ops	*mnt_vn_journal_ops;	/* journaling ops */
	struct vop_ops  *mnt_vn_norm_ops;	/* for use by the VFS */
	struct vop_ops	*mnt_vn_spec_ops;	/* for use by the VFS */
	struct vop_ops	*mnt_vn_fifo_ops;	/* for use by the VFS */
	struct nchandle mnt_ncmountpt;		/* mount point */
	struct nchandle mnt_ncmounton;		/* mounted on */
	int		mnt_refs;		/* nchandle references */
	struct lwkt_token mnt_token;		/* token lock if not MPSAFE */
	struct journallst mnt_jlist;		/* list of active journals */
	u_int8_t	*mnt_jbitmap;		/* streamid bitmap */
	int16_t		mnt_streamid;		/* last streamid */

	struct bio_ops	*mnt_bioops;		/* BIO ops (hammer, softupd) */

	struct vfs_acct	mnt_acct;		/* vfs space accounting */
};

#endif /* _KERNEL || _KERNEL_STRUCTURES */

/*
 * User specifiable flags.
 */
#define	MNT_RDONLY	0x00000001	/* read only filesystem */
#define	MNT_SYNCHRONOUS	0x00000002	/* file system written synchronously */
#define	MNT_NOEXEC	0x00000004	/* can't exec from filesystem */
#define	MNT_NOSUID	0x00000008	/* don't honor setuid bits on fs */
#define	MNT_NODEV	0x00000010	/* don't interpret special files */
#define	MNT_UNION	0x00000020	/* union with underlying filesystem */
#define	MNT_ASYNC	0x00000040	/* file system written asynchronously */
#define	MNT_SUIDDIR	0x00100000	/* special handling of SUID on dirs */
#define	MNT_SOFTDEP	0x00200000	/* soft updates being done */
#define	MNT_NOSYMFOLLOW	0x00400000	/* do not follow symlinks */
#define	MNT_TRIM	0x01000000	/* Enable online FS trimming */
#define	MNT_NOATIME	0x10000000	/* disable update of file access time */
#define	MNT_NOCLUSTERR	0x40000000	/* disable cluster read */
#define	MNT_NOCLUSTERW	0x80000000	/* disable cluster write */

/*
 * NFS export related mount flags.
 */
#define	MNT_EXRDONLY	0x00000080	/* exported read only */
#define	MNT_EXPORTED	0x00000100	/* file system is exported */
#define	MNT_DEFEXPORTED	0x00000200	/* exported to the world */
#define	MNT_EXPORTANON	0x00000400	/* use anon uid mapping for everyone */
#define	MNT_EXKERB	0x00000800	/* exported with Kerberos uid mapping */
#define	MNT_EXPUBLIC	0x20000000	/* public export (WebNFS) */

/*
 * Flags set by internal operations,
 * but visible to the user.
 * XXX some of these are not quite right.. (I've never seen the root flag set)
 */
#define	MNT_LOCAL	0x00001000	/* filesystem is stored locally */
#define	MNT_QUOTA	0x00002000	/* quotas are enabled on filesystem */
#define	MNT_ROOTFS	0x00004000	/* identifies the root filesystem */
#define	MNT_USER	0x00008000	/* mounted by a user */
#define	MNT_IGNORE	0x00800000	/* do not show entry in df */

/*
 * Mask of flags that are visible to statfs()
 * XXX I think that this could now become (~(MNT_CMDFLAGS))
 * but the 'mount' program may need changing to handle this.
 */
#define	MNT_VISFLAGMASK	(MNT_RDONLY	| MNT_SYNCHRONOUS | MNT_NOEXEC	| \
			MNT_NOSUID	| MNT_NODEV	| MNT_UNION	| \
			MNT_ASYNC	| MNT_EXRDONLY	| MNT_EXPORTED	| \
			MNT_DEFEXPORTED	| MNT_EXPORTANON| MNT_EXKERB	| \
			MNT_LOCAL	| MNT_USER	| MNT_QUOTA	| \
			MNT_ROOTFS	| MNT_NOATIME	| MNT_NOCLUSTERR| \
			MNT_NOCLUSTERW	| MNT_SUIDDIR	| MNT_SOFTDEP	| \
			MNT_IGNORE	| MNT_NOSYMFOLLOW | MNT_EXPUBLIC| \
			MNT_TRIM)
/*
 * External filesystem command modifier flags.
 * Unmount can use the MNT_FORCE flag.
 * XXX These are not STATES and really should be somewhere else.
 */
#define	MNT_UPDATE	0x00010000	/* not a real mount, just an update */
#define	MNT_DELEXPORT	0x00020000	/* delete export host lists */
#define	MNT_RELOAD	0x00040000	/* reload filesystem data */
#define	MNT_FORCE	0x00080000	/* force unmount or readonly change */
#define MNT_CMDFLAGS	(MNT_UPDATE|MNT_DELEXPORT|MNT_RELOAD|MNT_FORCE)
/*
 * Internal filesystem control flags stored in mnt_kern_flag.
 *
 * MNTK_UNMOUNT locks the mount entry so that name lookup cannot proceed
 * past the mount point.  This keeps the subtree stable during mounts
 * and unmounts.
 *
 * MNTK_UNMOUNTF permits filesystems to detect a forced unmount while
 * dounmount() is still waiting to lock the mountpoint. This allows
 * the filesystem to cancel operations that might otherwise deadlock
 * with the unmount attempt (used by NFS).
 *
 * MNTK_NOSTKMNT prevents mounting another filesystem inside the flagged one.
 */
#define MNTK_UNMOUNTF	0x00000001	/* forced unmount in progress */
#define MNTK_MPSAFE	0x00010000	/* call vops without mnt_token lock */
#define MNTK_RD_MPSAFE	0x00020000	/* vop_read is MPSAFE */
#define MNTK_WR_MPSAFE	0x00040000	/* vop_write is MPSAFE */
#define MNTK_GA_MPSAFE	0x00080000	/* vop_getattr is MPSAFE */
#define MNTK_IN_MPSAFE	0x00100000	/* vop_inactive is MPSAFE */
#define MNTK_SG_MPSAFE	0x00200000	/* vop_strategy is MPSAFE */
#define MNTK_NCALIASED	0x00800000	/* namecached aliased */
#define MNTK_UNMOUNT	0x01000000	/* unmount in progress */
#define	MNTK_MWAIT	0x02000000	/* waiting for unmount to finish */
#define MNTK_WANTRDWR	0x04000000	/* upgrade to read/write requested */
#define MNTK_FSMID	0x08000000	/* getattr supports FSMIDs */
#define MNTK_NOSTKMNT	0x10000000	/* no stacked mount point allowed */
#define MNTK_NOMSYNC	0x20000000	/* used by tmpfs */
#define MNTK_THR_SYNC	0x40000000	/* fs sync thread requested */
#define MNTK_ST_MPSAFE	0x80000000	/* (mfs) vfs_start is MPSAFE */

#define MNTK_ALL_MPSAFE	(MNTK_MPSAFE | MNTK_RD_MPSAFE | MNTK_WR_MPSAFE | \
			 MNTK_GA_MPSAFE | MNTK_IN_MPSAFE | MNTK_SG_MPSAFE | \
			 MNTK_ST_MPSAFE)

/*
 * mountlist_*() defines
 */
#define MNTSCAN_FORWARD		0x0001
#define MNTSCAN_REVERSE		0x0002
#define MNTSCAN_NOBUSY		0x0004

#define MNTINS_FIRST		0x0001
#define MNTINS_LAST		0x0002

/*
 * Sysctl CTL_VFS definitions.
 *
 * Second level identifier specifies which filesystem. Second level
 * identifier VFS_VFSCONF returns information about all filesystems.
 * Second level identifier VFS_GENERIC is non-terminal.
 */
#define	VFS_VFSCONF		0	/* get configured filesystems */
#define	VFS_GENERIC		0	/* generic filesystem information */
/*
 * Third level identifiers for VFS_GENERIC are given below; third
 * level identifiers for specific filesystems are given in their
 * mount specific header files.
 */
#define VFS_MAXTYPENUM	1	/* int: highest defined filesystem type */
#define VFS_CONF	2	/* struct: vfsconf for filesystem given
				   as next argument */

/*
 * VFS MPLOCK helper.
 */
#define VFS_MPLOCK_DECLARE	int xlock_mpsafe

#define VFS_MPLOCK1(mp)		VFS_MPLOCK_FLAG(mp, MNTK_MPSAFE)

#define VFS_MPLOCK2(mp)							\
		do {							\
			if (xlock_mpsafe) {				\
				get_mplock();	/* TEMPORARY */		\
				lwkt_gettoken(&mp->mnt_token);		\
				xlock_mpsafe = 0;			\
			}						\
		} while(0)

#define VFS_MPLOCK_FLAG(mp, flag)					\
		do {							\
			if (mp->mnt_kern_flag & flag) {			\
				xlock_mpsafe = 1;			\
			} else {					\
				get_mplock();	/* TEMPORARY */		\
				lwkt_gettoken(&mp->mnt_token);		\
				xlock_mpsafe = 0;			\
			}						\
		} while(0)

#define VFS_MPUNLOCK(mp)						\
		do {							\
			if (xlock_mpsafe == 0) {			\
				lwkt_reltoken(&mp->mnt_token);		\
				rel_mplock();	/* TEMPORARY */		\
			}						\
		} while(0)

/*
 * Flags for various system call interfaces.
 *
 * waitfor flags to vfs_sync() and getfsstat()
 */
#define MNT_WAIT	0x0001	/* synchronously wait for I/O to complete */
#define MNT_NOWAIT	0x0002	/* start all I/O, but do not wait for it */
#define MNT_LAZY	0x0004	/* be lazy and do not necessarily push it all */

#define VOP_FSYNC_SYSCALL	0x0001	/* from system call */

/*
 * Generic file handle
 */
struct fhandle {
	fsid_t	fh_fsid;	/* File system id of mount point */
	struct	fid fh_fid;	/* File sys specific id */
};
typedef struct fhandle	fhandle_t;

/*
 * Export arguments for local filesystem mount calls.
 */
struct export_args {
	int	ex_flags;		/* export related flags */
	uid_t	ex_root;		/* mapping for root uid */
	struct	ucred ex_anon;		/* mapping for anonymous user */
	struct	sockaddr *ex_addr;	/* net address to which exported */
	int	ex_addrlen;		/* and the net address length */
	struct	sockaddr *ex_mask;	/* mask of valid bits in saddr */
	int	ex_masklen;		/* and the smask length */
	char	*ex_indexfile;		/* index file for WebNFS URLs */
};

/*
 * Structure holding information for a publicly exported filesystem
 * (WebNFS). Currently the specs allow just for one such filesystem.
 */
struct nfs_public {
	int		np_valid;	/* Do we hold valid information */
	fhandle_t	np_handle;	/* Filehandle for pub fs (internal) */
	struct mount	*np_mount;	/* Mountpoint of exported fs */
	char		*np_index;	/* Index file */
};

/*
 * Filesystem configuration information. One of these exists for each
 * type of filesystem supported by the kernel. These are searched at
 * mount time to identify the requested filesystem.
 */
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
struct vfsconf {
	struct	vfsops *vfc_vfsops;	/* filesystem operations vector */
	char	vfc_name[MFSNAMELEN];	/* filesystem type name */
	int	vfc_typenum;		/* historic filesystem type number */
	int	vfc_refcount;		/* number mounted of this type */
	int	vfc_flags;		/* permanent flags */
	STAILQ_ENTRY(vfsconf) vfc_next;	/* next in list */
};

struct ovfsconf {
	void	*vfc_vfsops;
	char	vfc_name[32];
	int	vfc_index;
	int	vfc_refcount;
	int	vfc_flags;
};

/*
 * NB: these flags refer to IMPLEMENTATION properties, not properties of
 * any actual mounts; i.e., it does not make sense to change the flags.
 */
#define	VFCF_STATIC	0x00010000	/* statically compiled into kernel */
#define	VFCF_NETWORK	0x00020000	/* may get data over the network */
#define	VFCF_READONLY	0x00040000	/* writes are not implemented */
#define VFCF_SYNTHETIC	0x00080000	/* data does not represent real files */
#define	VFCF_LOOPBACK	0x00100000	/* aliases some other mounted FS */
#define	VFCF_UNICODE	0x00200000	/* stores file names as Unicode*/

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_MOUNT);
#endif
extern int nfs_mount_type;	/* vfc_typenum for nfs, or -1 */

struct vfsconf *vfsconf_find_by_name(const char *);
struct vfsconf *vfsconf_find_by_typenum(int);
int vfsconf_get_maxtypenum(void);
int vfsconf_each(int (*)(struct vfsconf *, void *), void *);

#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

TAILQ_HEAD(mntlist, mount);	/* struct mntlist */

/*
 * Operations supported on mounted file system.
 */
struct nlookupdata;
struct nlookupdata;
struct mbuf;

typedef int vfs_mount_t(struct mount *mp, char *path, caddr_t data,
				    struct ucred *cred);
typedef int vfs_start_t(struct mount *mp, int flags);
typedef int vfs_unmount_t(struct mount *mp, int mntflags);
typedef int vfs_root_t(struct mount *mp, struct vnode **vpp);
typedef int vfs_quotactl_t(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
				    struct ucred *cred);
typedef int vfs_statfs_t(struct mount *mp, struct statfs *sbp,
				    struct ucred *cred);
typedef int vfs_statvfs_t(struct mount *mp, struct statvfs *sbp,
				    struct ucred *cred);
typedef int vfs_sync_t(struct mount *mp, int waitfor);
typedef int vfs_vget_t(struct mount *mp, struct vnode *dvp,
				    ino_t ino, struct vnode **vpp);
typedef int vfs_fhtovp_t(struct mount *mp, struct vnode *rootvp,
				    struct fid *fhp, struct vnode **vpp);
typedef int vfs_checkexp_t(struct mount *mp, struct sockaddr *nam,
				    int *extflagsp, struct ucred **credanonp);
typedef int vfs_vptofh_t(struct vnode *vp, struct fid *fhp);
typedef int vfs_init_t(struct vfsconf *);
typedef int vfs_uninit_t(struct vfsconf *);
typedef int vfs_extattrctl_t(struct mount *mp, int cmd, struct vnode *vp,
		    int attrnamespace, const char *attrname,
		    struct ucred *cred);
typedef int vfs_acinit_t(struct mount *mp);
typedef void vfs_acdone_t(struct mount *mp);
typedef void vfs_account_t(struct mount *mp,
			uid_t uid, gid_t gid, int64_t delta);
typedef void vfs_ncpgen_set_t(struct mount *mp, struct namecache *ncp);
typedef int vfs_ncpgen_test_t(struct mount *mp, struct namecache *ncp);

int vfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred);
int vfs_start(struct mount *mp, int flags);
int vfs_unmount(struct mount *mp, int mntflags);
int vfs_root(struct mount *mp, struct vnode **vpp);
int vfs_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
				struct ucred *cred);
int vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred);
int vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred);
int vfs_sync(struct mount *mp, int waitfor);
int vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
int vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
int vfs_checkexp(struct mount *mp, struct sockaddr *nam,
				int *extflagsp, struct ucred **credanonp);
int vfs_vptofh(struct vnode *vp, struct fid *fhp);
int vfs_init(struct vfsconf *vfc);
int vfs_uninit(struct vfsconf *vfc, struct vfsconf *vfsp);
int vfs_extattrctl(struct mount *mp, int cmd, struct vnode *vp,
		    int attrnamespace, const char *attrname,
		    struct ucred *cred);


struct vfsops {
	vfs_mount_t 	*vfs_mount;
	vfs_start_t 	*vfs_start;
	vfs_unmount_t 	*vfs_unmount;
	vfs_root_t   	*vfs_root;
	vfs_quotactl_t 	*vfs_quotactl;
	vfs_statfs_t 	*vfs_statfs;
	vfs_sync_t   	*vfs_sync;
	vfs_vget_t  	*vfs_vget;
	vfs_fhtovp_t 	*vfs_fhtovp;
	vfs_checkexp_t 	*vfs_checkexp;
	vfs_vptofh_t 	*vfs_vptofh;
	vfs_init_t  	*vfs_init;
	vfs_uninit_t 	*vfs_uninit;
	vfs_extattrctl_t *vfs_extattrctl;
	vfs_statvfs_t 	*vfs_statvfs;
	vfs_acinit_t	*vfs_acinit;
	vfs_acdone_t	*vfs_acdone;
	vfs_account_t	*vfs_account;
	vfs_ncpgen_set_t	*vfs_ncpgen_set;
	vfs_ncpgen_test_t	*vfs_ncpgen_test;
};

#define VFS_MOUNT(MP, PATH, DATA, CRED)		\
	vfs_mount(MP, PATH, DATA, CRED)
#define VFS_START(MP, FLAGS)			\
	vfs_start(MP, FLAGS)
#define VFS_UNMOUNT(MP, FORCE)			\
	vfs_unmount(MP, FORCE)
#define VFS_ROOT(MP, VPP)			\
	vfs_root(MP, VPP)
#define VFS_QUOTACTL(MP, C, U, A, CRED)		\
	vfs_quotactl(MP, C, U, A, CRED)
#define VFS_STATFS(MP, SBP, CRED)		\
	vfs_statfs(MP, SBP, CRED)
#define VFS_STATVFS(MP, SBP, CRED)		\
	vfs_statvfs(MP, SBP, CRED)
#define VFS_SYNC(MP, WAIT)			\
	vfs_sync(MP, WAIT)
#define VFS_VGET(MP, DVP, INO, VPP)		\
	vfs_vget(MP, DVP, INO, VPP)
#define VFS_FHTOVP(MP, ROOTVP, FIDP, VPP) 	\
	vfs_fhtovp(MP, ROOTVP, FIDP, VPP)
#define	VFS_VPTOFH(VP, FIDP)			\
	vfs_vptofh(VP, FIDP)
#define VFS_CHECKEXP(MP, NAM, EXFLG, CRED)	\
	vfs_checkexp(MP, NAM, EXFLG, CRED)
#define VFS_EXTATTRCTL(MP, C, FVP, NS, N, CRED)	\
	vfs_extattrctl(MP, C, FVP, NS, N, CRED)
#define VFS_ACCOUNT(MP, U, G, D) \
	if ((MP->mnt_op->vfs_account != NULL) && (D != 0)) \
		MP->mnt_op->vfs_account(MP, U, G, D);
#define VFS_ACINIT(MP, ERROR) \
	if (vfs_quota_enabled && MP->mnt_op->vfs_acinit != NULL) \
		ERROR = MP->mnt_op->vfs_acinit(MP);
#define VFS_ACDONE(MP) \
	if (vfs_quota_enabled && MP->mnt_op->vfs_acdone != NULL) \
		MP->mnt_op->vfs_acdone(MP);
#define VFS_NCPGEN_SET(MP, NCP) \
	MP->mnt_op->vfs_ncpgen_set(MP, NCP)
#define VFS_NCPGEN_TEST(MP, NCP) \
	MP->mnt_op->vfs_ncpgen_test(MP, NCP)

#endif

#ifdef _KERNEL

#include <sys/module.h>

#define VFS_SET(vfsops, fsname, flags) \
	static struct vfsconf fsname ## _vfsconf = {		\
		&vfsops,					\
		#fsname,					\
		-1,						\
		0,						\
		flags,						\
		{ NULL },					\
	};							\
	static moduledata_t fsname ## _mod = {			\
		#fsname,					\
		vfs_modevent,					\
		& fsname ## _vfsconf				\
	};							\
	DECLARE_MODULE(fsname, fsname ## _mod, SI_SUB_VFS, SI_ORDER_MIDDLE)

#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#include <net/radix.h>

#define	AF_MAX		36	/* XXX */

/*
 * Network address lookup element
 */
struct netcred {
	struct	radix_node netc_rnodes[2];
	int	netc_exflags;
	struct	ucred netc_anon;
};

/*
 * Network export information
 */
struct netexport {
	struct	netcred ne_defexported;		      /* Default export */
	struct	radix_node_head *ne_rtable[AF_MAX+1]; /* Individual exports */
};

#endif

#ifdef _KERNEL

extern	char *mountrootfsname;

/*
 * exported vnode operations
 */
int	dounmount (struct mount *, int);
int	vfs_setpublicfs			    /* set publicly exported fs */
	  (struct mount *, struct netexport *, const struct export_args *);
int	vfs_lock (struct mount *);         /* lock a vfs */
void	vfs_msync (struct mount *, int);
void	vfs_unlock (struct mount *);       /* unlock a vfs */
int	vfs_busy (struct mount *, int);
int	vfs_export			    /* process mount export info */
	  (struct mount *, struct netexport *, const struct export_args *);
struct	netcred *vfs_export_lookup	    /* lookup host in fs export list */
	  (struct mount *, struct netexport *, struct sockaddr *);
int	vfs_allocate_syncvnode (struct mount *);
void	vfs_getnewfsid (struct mount *);
int	vfs_setfsid(struct mount *mp, fsid_t *template);
cdev_t	vfs_getrootfsid (struct mount *);
struct	mount *vfs_getvfs (fsid_t *);      /* return vfs given fsid */
int	vfs_modevent (module_t, int, void *);
int	vfs_mountedon (struct vnode *);    /* is a vfs mounted on vp */
int	vfs_rootmountalloc (char *, char *, struct mount **);
void	vfs_unbusy (struct mount *);
void	vfs_unmountall (void);
int	vfs_register (struct vfsconf *);
int	vfs_unregister (struct vfsconf *);
extern	struct nfs_public nfs_pub;

/* 
 * Declarations for these vfs default operations are located in 
 * kern/vfs_default.c, they should be used instead of making "dummy" 
 * functions or casting entries in the VFS op table to "enopnotsupp()".
 */ 
vfs_start_t 	vfs_stdstart;
vfs_root_t  	vfs_stdroot;
vfs_quotactl_t 	vfs_stdquotactl;
vfs_statfs_t  	vfs_stdstatfs;
vfs_statvfs_t  	vfs_stdstatvfs;
vfs_sync_t   	vfs_stdsync;
vfs_sync_t   	vfs_stdnosync;
vfs_vget_t  	vfs_stdvget;
vfs_fhtovp_t 	vfs_stdfhtovp;
vfs_checkexp_t 	vfs_stdcheckexp;
vfs_vptofh_t 	vfs_stdvptofh;
vfs_init_t  	vfs_stdinit;
vfs_uninit_t 	vfs_stduninit;
vfs_extattrctl_t vfs_stdextattrctl;
vfs_acinit_t	vfs_stdac_init;
vfs_acdone_t	vfs_stdac_done;
vfs_account_t	vfs_stdaccount;
vfs_account_t	vfs_noaccount;
vfs_ncpgen_set_t	vfs_stdncpgen_set;
vfs_ncpgen_test_t	vfs_stdncpgen_test;

struct vop_access_args;
int vop_helper_access(struct vop_access_args *ap, uid_t ino_uid, gid_t ino_gid,
			mode_t ino_mode, u_int32_t ino_flags);
int vop_helper_setattr_flags(u_int32_t *ino_flags, u_int32_t vaflags,
			uid_t uid, struct ucred *cred);
uid_t vop_helper_create_uid(struct mount *mp, mode_t dmode, uid_t duid,
			struct ucred *cred, mode_t *modep);
int vop_helper_chmod(struct vnode *vp, mode_t new_mode, struct ucred *cred,
			uid_t cur_uid, gid_t cur_gid, mode_t *cur_modep);
int vop_helper_chown(struct vnode *vp, uid_t new_uid, gid_t new_gid,
			struct ucred *cred,
			uid_t *cur_uidp, gid_t *cur_gidp, mode_t *cur_modep);
int vop_helper_read_shortcut(struct vop_read_args *ap);

void	add_bio_ops(struct bio_ops *ops);
void	rem_bio_ops(struct bio_ops *ops);

int     journal_mountctl(struct vop_mountctl_args *ap);
void	journal_remove_all_journals(struct mount *mp, int flags);

void	mountlist_insert(struct mount *, int);
int	mountlist_interlock(int (*callback)(struct mount *), struct mount *);
struct mount *mountlist_boot_getfirst(void);
void	mountlist_remove(struct mount *mp);
int	mountlist_exists(struct mount *mp);
int	mountlist_scan(int (*callback)(struct mount *, void *), void *, int);
struct mount *mount_get_by_nc(struct namecache *ncp);
#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
int	fstatfs (int, struct statfs *);
int	getfh (const char *, fhandle_t *);
int	getfsstat (struct statfs *, long, int);
int	getmntinfo (struct statfs **, int);
int	getmntvinfo (struct statfs **, struct statvfs **, int);
int	mount (const char *, const char *, int, void *);
int	statfs (const char *, struct statfs *);
int	unmount (const char *, int);
int	fhopen (const struct fhandle *, int);
int	fhstat (const struct fhandle *, struct stat *);
int	fhstatfs (const struct fhandle *, struct statfs *);

/* C library stuff */
void	endvfsent (void);
struct	ovfsconf *getvfsbyname (const char *);
struct	ovfsconf *getvfsbytype (int);
struct	ovfsconf *getvfsent (void);
#define	getvfsbyname	new_getvfsbyname
int	new_getvfsbyname (const char *, struct vfsconf *);
void	setvfsent (int);
int	vfsisloadable (const char *);
int	vfsload (const char *);
__END_DECLS

#endif /* _KERNEL */

#endif /* !_SYS_MOUNT_H_ */
