/*
 * Copyright (c) 1989, 1993
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
 *	@(#)vnode.h	8.7 (Berkeley) 2/4/94
 * $FreeBSD: src/sys/sys/vnode.h,v 1.111.2.19 2002/12/29 18:19:53 dillon Exp $
 */

#ifndef _SYS_VNODE_H_
#define	_SYS_VNODE_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_EVENT_H_
#include <sys/event.h>
#endif
#ifndef _SYS_BIOTRACK_H_
#include <sys/biotrack.h>
#endif
#ifndef _SYS_UIO_H_
#include <sys/uio.h>
#endif
#ifndef _SYS_ACL_H_
#include <sys/acl.h>
#endif
#ifndef _SYS_NAMECACHE_H_
#include <sys/namecache.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_VFSOPS_H_
#include <sys/vfsops.h>
#endif
#ifndef _SYS_VFSCACHE_H_
#include <sys/vfscache.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_SYSLINK_RPC_H_
#include <sys/syslink_rpc.h>
#endif
#ifndef _SYS_SYSREF_H_
#include <sys/sysref.h>
#endif
#ifndef _MACHINE_LOCK_H_
#include <machine/lock.h>
#endif

/*
 * The vnode is the focus of all file activity in UNIX.  There is a
 * unique vnode allocated for each active file, each current directory,
 * each mounted-on file, text file, and the root.
 */

/*
 * Each underlying filesystem allocates its own private area and hangs
 * it from v_data.  If non-null, this area is freed in getnewvnode().
 */
TAILQ_HEAD(buflists, buf);

/*
 * Struct for mount options to printable formats.
 */
struct mountctl_opt {
        int             o_opt;
        const char      *o_name;
};

/*
 * The vnode infrastructure is being reorgranized.  Most reference-related
 * fields are locked by the BGL, and most file I/O related operations and
 * vnode teardown functions are locked by the vnode lock.
 *
 * File read operations require a shared lock, file write operations require
 * an exclusive lock.  Most directory operations (read or write) currently
 * require an exclusive lock due to the side effects stored in the directory
 * inode (which we intend to fix).
 *
 * File reads and writes are further protected by a range lock.  The intention
 * is to be able to break I/O operations down into more easily managed pieces
 * so vm_page arrays can be passed through rather then UIOs.  This work will
 * occur in multiple stages.  The range locks will also eventually be used to
 * deal with clustered cache coherency issues and, more immediately, to
 * protect operations associated with the kernel-managed journaling module.
 *
 * NOTE: Certain fields within the vnode structure requires v_token to be
 *	 held.  The vnode's normal lock need not be held when accessing
 *	 these fields as long as the vnode is deterministically referenced
 *	 (i.e. can't be ripped out from under the caller).  This is typical
 *	 for code paths based on descriptors or file pointers, but not for
 *	 backdoor code paths that come in via the buffer cache.
 *
 *	v_rbclean_tree
 *	v_rbdirty_tree
 *	v_rbhash_tree
 *	v_pollinfo
 *
 * NOTE: The vnode operations vector, v_ops, is a double-indirect that
 *	 typically points to &v_mount->mnt_vn_use_ops.  We use a double
 *	 pointer because mnt_vn_use_ops may change dynamically when e.g.
 *	 journaling is turned on or off.
 *
 * NOTE: v_filesize is currently only applicable when a VM object is
 *	 associated with the vnode.  Otherwise it will be set to NOOFFSET.
 *
 * NOTE: The following fields require a spin or token lock.  Note that
 *	 additional subsystems may use v_token or v_spin for other
 *	 purposes, e.g. vfs/fifofs/fifo_vnops.c
 *
 *	 v_namecache	v_spin
 *	 v_rb*		v_token
 */
RB_HEAD(buf_rb_tree, buf);
RB_HEAD(buf_rb_hash, buf);

struct vnode {
	struct spinlock v_spin;
	int	v_flag;				/* vnode flags (see below) */
	int	v_writecount;
	int	v_opencount;			/* number of explicit opens */
	int	v_auxrefs;			/* auxiliary references */
	int	v_refcnt;
	struct bio_track v_track_read;		/* track I/O's in progress */
	struct bio_track v_track_write;		/* track I/O's in progress */
	struct mount *v_mount;			/* ptr to vfs we are in */
	struct vop_ops **v_ops;			/* vnode operations vector */
	TAILQ_ENTRY(vnode) v_list;		/* vnode act/inact/cache/free */
	TAILQ_ENTRY(vnode) v_nmntvnodes;	/* vnodes for mount point */
	LIST_ENTRY(vnode) v_synclist;		/* vnodes with dirty buffers */
	struct buf_rb_tree v_rbclean_tree;	/* RB tree of clean bufs */
	struct buf_rb_tree v_rbdirty_tree;	/* RB tree of dirty bufs */
	struct buf_rb_hash v_rbhash_tree;	/* RB tree general lookup */
	enum	vtype v_type;			/* vnode type */
	int16_t		v_act;			/* use heuristic */
	int16_t		v_state;		/* active/free/cached */
	union {
		struct socket	*vu_socket;	/* unix ipc (VSOCK) */
		struct {
			int	vu_umajor;	/* device number for attach */
			int	vu_uminor;
			struct cdev	*vu_cdevinfo; /* device (VCHR, VBLK) */
			SLIST_ENTRY(vnode) vu_cdevnext;
		} vu_cdev;
		struct fifoinfo	*vu_fifoinfo;	/* fifo (VFIFO) */
	} v_un;
	off_t	v_filesize;			/* file EOF or NOOFFSET */
	off_t	v_lazyw;			/* lazy write iterator */
	struct vm_object *v_object;		/* Place to store VM object */
	struct	lock v_lock;			/* file/dir ops lock */
	struct	lwkt_token v_token;		/* (see above) */
	enum	vtagtype v_tag;			/* type of underlying data */
	void	*v_data;			/* private data for fs */
	struct namecache_list v_namecache;	/* (S) associated nc entries */
	struct	{
		struct	kqinfo vpi_kqinfo;	/* identity of poller(s) */
	} v_pollinfo;
	struct vmresident *v_resident;		/* optional vmresident */
	struct mount *v_pfsmp;			/* real mp for pfs/nullfs mt */
#ifdef	DEBUG_LOCKS
	const char *filename;			/* Source file doing locking */
	int line;				/* Line number doing locking */
#endif
};
#define	v_socket	v_un.vu_socket
#define v_umajor	v_un.vu_cdev.vu_umajor
#define v_uminor	v_un.vu_cdev.vu_uminor
#define	v_rdev		v_un.vu_cdev.vu_cdevinfo
#define	v_cdevnext	v_un.vu_cdev.vu_cdevnext
#define	v_fifoinfo	v_un.vu_fifoinfo

/*
 * Vnode flags.
 */
#define	VROOT		0x00000001	/* root of its file system */
#define	VTEXT		0x00000002	/* vnode is a pure text prototype */
#define	VSYSTEM		0x00000004	/* vnode being used by kernel */
#define	VISTTY		0x00000008	/* vnode represents a tty */
#define VCTTYISOPEN	0x00000010	/* controlling terminal tty is open */
#define VCKPT		0x00000020	/* checkpoint-restored vnode */
/* open for business	0x00000040 */
#define VMAYHAVELOCKS	0x00000080	/* maybe posix or flock locks on vp */
#define VPFSROOT	0x00000100	/* may be a pseudo filesystem root */
/* open for business    0x00000200 */
#define VAGE0		0x00000400	/* Age count for recycling - 2 bits */
#define VAGE1		0x00000800	/* Age count for recycling - 2 bits */
/* open for business	0x00001000 */
#define	VOBJBUF		0x00002000	/* Allocate buffers in VM object */
#define	VINACTIVE	0x00004000	/* ran VOP_INACTIVE */
/* open for business    0x00008000 */
/* open for business    0x00010000 */
/* open for business    0x00020000 */
#define	VRECLAIMED	0x00040000	/* This vnode has been destroyed */
/* open for business	0x00080000 */
#define VNOTSEEKABLE	0x00100000	/* rd/wr ignores file offset */
#define	VONWORKLST	0x00200000	/* On syncer work-list */
#define VISDIRTY	0x00400000	/* inode dirty from VFS */
#define	VOBJDIRTY	0x00800000	/* object might be dirty */
#define VSWAPCACHE	0x01000000	/* enable swapcache */
/* open for business	0x02000000 */
/* open for business	0x04000000 */

/*
 * v_state flags (v_state is interlocked by v_spin and vfs_spin)
 */
#define VS_CACHED	0
#define VS_ACTIVE	1
#define VS_INACTIVE	2
#define VS_DYING	3

/*
 * v_refcnt uses bit 30 to flag that 1->0 transitions require finalization
 * (actual deactivation) and bit 31 to indicate that deactivation is in
 * progress.
 *
 * The VREFCNT() macro returns a negative number if the vnode is undergoing
 * termination (value should not be interpreted beyond being negative),
 * zero if it is cached and has no references, or a positive number
 * indicating the number of refs.
 */
#define VREF_TERMINATE	0x80000000	/* termination in progress */
#define VREF_FINALIZE	0x40000000	/* deactivate on last vrele */
#define VREF_MASK	0xBFFFFFFF	/* includes VREF_TERMINATE */

#define VREFCNT(vp)	((int)((vp)->v_refcnt & VREF_MASK))

/*
 * vmntvnodescan() flags
 */
#define VMSC_GETVP	0x01
#define VMSC_GETVX	0x02
#define VMSC_NOWAIT	0x10
#define VMSC_ONEPASS	0x20

/*
 * Flags for ioflag. (high 16 bits used to ask for read-ahead and
 * help with write clustering)
 */
#define	IO_UNIT		0x0001		/* do I/O as atomic unit */
#define	IO_APPEND	0x0002		/* append write to end */
#define	IO_SYNC		0x0004		/* do I/O synchronously */
#define	IO_NODELOCKED	0x0008		/* underlying node already locked */
#define	IO_NDELAY	0x0010		/* FNDELAY flag set in file table */
#define	IO_VMIO		0x0020		/* data already in VMIO space */
#define	IO_INVAL	0x0040		/* invalidate after I/O */
#define IO_ASYNC	0x0080		/* bawrite rather then bdwrite */
#define	IO_DIRECT	0x0100		/* attempt to bypass buffer cache */
#define	IO_RECURSE	0x0200		/* possibly device-recursive (vn) */
#define	IO_CORE		0x0400		/* I/O is part of core dump */
#define	IO_NRDELAY	0x0800		/* do not block on disk reads */

#define	IO_SEQMAX	0x7F		/* seq heuristic max value */
#define	IO_SEQSHIFT	16		/* seq heuristic in upper 16 bits */

/*
 * Modes.  Note that these V-modes must match file S_I*USR, SUID, SGID,
 * and SVTX flag bits.
 */
#define	VSUID	04000		/* set user id on execution */
#define	VSGID	02000		/* set group id on execution */
#define	VSVTX	01000		/* save swapped text even after use */
#define	VREAD	00400		/* read, write, execute permissions */
#define	VWRITE	00200
#define	VEXEC	00100

/*
 * Token indicating no attribute value yet assigned.
 */
#define	VNOVAL	(-1)

/*
 * LK_TIMELOCK timeout for vnode locks (used mainly by the pageout daemon)
 */
#define	VLKTIMEOUT     (hz / 20 + 1)

#ifdef _KERNEL

/*
 * Convert between vnode types and inode formats (since POSIX.1
 * defines mode word of stat structure in terms of inode formats).
 */
extern	enum vtype	iftovt_tab[];
extern	int		vttoif_tab[];
#define	IFTOVT(mode)	(iftovt_tab[((mode) & S_IFMT) >> 12])
#define	VTTOIF(indx)	(vttoif_tab[(int)(indx)])
#define	MAKEIMODE(indx, mode)	(int)(VTTOIF(indx) | (mode))

/*
 * Flags to various vnode functions.
 */
#define	SKIPSYSTEM	0x0001		/* vflush: skip vnodes marked VSYSTEM */
#define	FORCECLOSE	0x0002		/* vflush: force file closure */
#define	WRITECLOSE	0x0004		/* vflush: only close writable files */
#define	DOCLOSE		0x0008		/* vclean: close active files */
#define	V_SAVE		0x0001		/* vinvalbuf: sync file first */

#ifdef DIAGNOSTIC
#define	VATTR_NULL(vap)	vattr_null(vap)
#else
#define	VATTR_NULL(vap)	(*(vap) = va_null)	/* initialize a vattr */
#endif /* DIAGNOSTIC */

#define	NULLVP	((struct vnode *)NULL)

#define	VNODEOP_SET(f) \
	SYSINIT(f##init, SI_SUB_VFS, SI_ORDER_SECOND, vfs_nadd_vnodeops_sysinit, &f); \
	SYSUNINIT(f##uninit, SI_SUB_VFS, SI_ORDER_SECOND,vfs_nrm_vnodeops_sysinit, &f);

/*
 * Global vnode data.
 */
struct objcache;

extern	struct vnode *rootvnode;	/* root (i.e. "/") vnode */
extern  struct nchandle rootnch;	/* root (i.e. "/") namecache */
extern	int desiredvnodes;		/* number of vnodes desired */
extern	time_t syncdelay;		/* max time to delay syncing data */
extern	time_t filedelay;		/* time to delay syncing files */
extern	time_t dirdelay;		/* time to delay syncing directories */
extern	time_t metadelay;		/* time to delay syncing metadata */
extern	struct objcache *namei_oc;
extern	int prtactive;			/* nonzero to call vprint() */
extern	struct vattr va_null;		/* predefined null vattr structure */
extern	int numvnodes;
extern	int inactivevnodes;
extern	int activevnodes;
extern	int cachedvnodes;

/*
 * This macro is very helpful in defining those offsets in the vdesc struct.
 *
 * This is stolen from X11R4.  I ignored all the fancy stuff for
 * Crays, so if you decide to port this to such a serious machine,
 * you might want to consult Intrinsic.h's XtOffset{,Of,To}.
 */
#define	VOPARG_OFFSET(p_type,field) \
        ((int) (((char *) (&(((p_type)NULL)->field))) - ((char *) NULL)))
#define	VOPARG_OFFSETOF(s_type,field) \
	VOPARG_OFFSET(s_type*,field)
#define	VOPARG_OFFSETTO(S_TYPE,S_OFFSET,STRUCT_P) \
	((S_TYPE)(((char*)(STRUCT_P))+(S_OFFSET)))

typedef int (*vnodeopv_entry_t)(struct vop_generic_args *);

/*
 * VOCALL calls an op given an ops vector.  We break it out because BSD's
 * vclean changes the ops vector and then wants to call ops with the old
 * vector.
 */

typedef int (*vocall_func_t)(struct vop_generic_args *);

/*
 * This call executes the vops vector for the offset stored in the ap's
 * descriptor of the passed vops rather then the one related to the
 * ap's vop_ops structure.  It is used to chain VOPS calls on behalf of
 * filesystems from a VFS's context ONLY (that is, from a VFS's own vops
 * vector function).
 */
#define VOCALL(vops, ap)		\
	(*(vocall_func_t *)((char *)(vops)+((ap)->a_desc->sd_offset)))(ap)

#define	VDESC(OP) (& __CONCAT(OP,_desc))

/*
 * Public vnode manipulation functions.
 */
struct file;
struct mount;
struct nlookupdata;
struct proc;
struct thread;
struct stat;
struct ucred;
struct uio;
struct vattr;
struct vnode;
struct syncer_ctx;

struct vnode *getsynthvnode(const char *devname);
void	addaliasu (struct vnode *vp, int x, int y);
int	v_associate_rdev(struct vnode *vp, cdev_t dev);
void	v_release_rdev(struct vnode *vp);
int 	bdevvp (cdev_t dev, struct vnode **vpp);
struct vnode *allocvnode(int lktimeout, int lkflags);
void	allocvnode_gc(void);
int	freesomevnodes(int count);
int	getnewvnode (enum vtagtype tag, struct mount *mp, 
		    struct vnode **vpp, int timo, int lkflags);
int	getspecialvnode (enum vtagtype tag, struct mount *mp, 
		    struct vop_ops **ops, struct vnode **vpp, int timo, 
		    int lkflags);
void	speedup_syncer (struct mount *mp);
int	vaccess(enum vtype, mode_t, uid_t, gid_t, mode_t, struct ucred *);
void	vattr_null (struct vattr *vap);
int	vcount (struct vnode *vp);
int	vfinddev (cdev_t dev, enum vtype type, struct vnode **vpp);
void	vfs_nadd_vnodeops_sysinit (void *);
void	vfs_nrm_vnodeops_sysinit (void *);
void	vfs_add_vnodeops(struct mount *, struct vop_ops *, struct vop_ops **);
void	vfs_rm_vnodeops(struct mount *, struct vop_ops *, struct vop_ops **);
int	vflush (struct mount *mp, int rootrefs, int flags);

int	vmntvnodescan(struct mount *mp, int flags,
	    int (*fastfunc)(struct mount *mp, struct vnode *vp, void *data),
	    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
	    void *data);
int	vsyncscan(struct mount *mp, int flags,
	    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
	    void *data);
void	vclrisdirty(struct vnode *vp);
void	vclrobjdirty(struct vnode *vp);
void	vsetisdirty(struct vnode *vp);
void	vsetobjdirty(struct vnode *vp);

void	insmntque(struct vnode *vp, struct mount *mp);

void	vclean_vxlocked (struct vnode *vp, int flags);
void	vclean_unlocked (struct vnode *vp);
void	vgone_vxlocked (struct vnode *vp);
int	vrevoke (struct vnode *vp, struct ucred *cred);
int	vinvalbuf (struct vnode *vp, int save, int slpflag, int slptimeo);
int	vtruncbuf (struct vnode *vp, off_t length, int blksize);
void	vnode_pager_setsize (struct vnode *, vm_ooffset_t);
int	nvtruncbuf (struct vnode *vp, off_t length, int blksize, int boff,
		int trivial);
int	nvextendbuf(struct vnode *vp, off_t olength, off_t nlength,
		int oblksize, int nblksize,
		int oboff, int nboff, int trivial);
void	nvnode_pager_setsize (struct vnode *vp, off_t length,
		int blksize, int boff);
int	vfsync(struct vnode *vp, int waitfor, int passes,
		int (*checkdef)(struct buf *),
		int (*waitoutput)(struct vnode *, struct thread *));
int	vinitvmio(struct vnode *vp, off_t filesize, int blksize, int boff);
void	vprint (char *label, struct vnode *vp);
int	vrecycle (struct vnode *vp);
int	vmaxiosize (struct vnode *vp);
void	vn_strategy(struct vnode *vp, struct bio *bio);
int	vn_cache_strategy(struct vnode *vp, struct bio *bio);
int	vn_close (struct vnode *vp, int flags, struct file *fp);
void	vn_gone (struct vnode *vp);
int	vn_isdisk (struct vnode *vp, int *errp);
int	vn_islocked (struct vnode *vp);
int	vn_islocked_unlock (struct vnode *vp);
void	vn_islocked_relock (struct vnode *vp, int vpls);
int	vn_lock (struct vnode *vp, int flags);
void	vn_unlock (struct vnode *vp);

#ifdef	DEBUG_LOCKS
int	debug_vn_lock (struct vnode *vp, int flags,
		const char *filename, int line);
#define vn_lock(vp,flags)	debug_vn_lock(vp, flags, __FILE__, __LINE__)
#endif

/*#define DEBUG_VN_UNLOCK*/
#ifdef DEBUG_VN_UNLOCK
void	debug_vn_unlock (struct vnode *vp,
		const char *filename, int line);
#define vn_unlock(vp)		debug_vn_unlock(vp, __FILE__, __LINE__)
#endif

#define VOP_NULL	((void*)(uintptr_t)vop_null)
#define VOP_EBADF	((void*)(uintptr_t)vop_ebadf)
#define VOP_ENOTTY	((void*)(uintptr_t)vop_enotty)
#define VOP_EINVAL	((void*)(uintptr_t)vop_einval)
#define VOP_EOPNOTSUPP	((void*)(uintptr_t)vop_eopnotsupp)

int	vn_get_namelen(struct vnode *, int *);
void	vn_setspecops (struct file *fp);
int	vn_fullpath (struct proc *p, struct vnode *vn, char **retbuf, char **freebuf, int guess);
int	vn_open (struct nlookupdata *ndp, struct file *fp, int fmode, int cmode);
int	vn_opendisk (const char *devname, int fmode, struct vnode **vpp);
int 	vn_rdwr (enum uio_rw rw, struct vnode *vp, caddr_t base,
	    int len, off_t offset, enum uio_seg segflg, int ioflg,
	    struct ucred *cred, int *aresid);
int	vn_rdwr_inchunks (enum uio_rw rw, struct vnode *vp, caddr_t base,
	    int len, off_t offset, enum uio_seg segflg, int ioflg,
	    struct ucred *cred, int *aresid);
int	vn_stat (struct vnode *vp, struct stat *sb, struct ucred *cred);
cdev_t	vn_todev (struct vnode *vp);
void	vfs_timestamp (struct timespec *);
size_t	vfs_flagstostr(int flags, const struct mountctl_opt *optp, char *buf, size_t len, int *errorp);
void	vn_mark_atime(struct vnode *vp, struct thread *td);
int	vn_writechk (struct vnode *vp, struct nchandle *nch);
int	ncp_writechk(struct nchandle *nch);
int	vop_stdopen (struct vop_open_args *ap);
int	vop_stdclose (struct vop_close_args *ap);
int	vop_stdmountctl(struct vop_mountctl_args *ap);
int	vop_stdgetpages(struct vop_getpages_args *ap);
int	vop_stdputpages(struct vop_putpages_args *ap);
int	vop_stdmarkatime(struct vop_markatime_args *ap);
int	vop_stdnoread(struct vop_read_args *ap);
int	vop_stdnowrite(struct vop_write_args *ap);
int	vop_stdpathconf (struct vop_pathconf_args *ap);
int	vop_eopnotsupp (struct vop_generic_args *ap);
int	vop_ebadf (struct vop_generic_args *ap);
int	vop_einval (struct vop_generic_args *ap);
int	vop_enotty (struct vop_generic_args *ap);
int	vop_defaultop (struct vop_generic_args *ap);
int	vop_null (struct vop_generic_args *ap);
int	vop_write_dirent(int *, struct uio *, ino_t, uint8_t, uint16_t,
			 const char *);

int	vop_compat_nresolve(struct vop_nresolve_args *ap);
int	vop_compat_nlookupdotdot(struct vop_nlookupdotdot_args *ap);
int	vop_compat_ncreate(struct vop_ncreate_args *ap);
int	vop_compat_nmkdir(struct vop_nmkdir_args *ap);
int	vop_compat_nmknod(struct vop_nmknod_args *ap);
int	vop_compat_nlink(struct vop_nlink_args *ap);
int	vop_compat_nsymlink(struct vop_nsymlink_args *ap);
int	vop_compat_nwhiteout(struct vop_nwhiteout_args *ap);
int	vop_compat_nremove(struct vop_nremove_args *ap);
int	vop_compat_nrmdir(struct vop_nrmdir_args *ap);
int	vop_compat_nrename(struct vop_nrename_args *ap);

void	vx_lock (struct vnode *vp);
void	vx_unlock (struct vnode *vp);
void	vx_get (struct vnode *vp);
int	vx_get_nonblock (struct vnode *vp);
void	vx_put (struct vnode *vp);
int	vget (struct vnode *vp, int lockflag);
void	vput (struct vnode *vp);
void	vhold (struct vnode *);
void	vdrop (struct vnode *);
void	vref (struct vnode *vp);
void	vrele (struct vnode *vp);
void	vsetflags (struct vnode *vp, int flags);
void	vclrflags (struct vnode *vp, int flags);

/*#define DEBUG_VPUT*/
#ifdef DEBUG_VPUT
void	debug_vput (struct vnode *vp, const char *filename, int line);
#define vput(vp)		debug_vput(vp, __FILE__, __LINE__)
#endif

void	vfs_subr_init(void);
void	vfs_mount_init(void);
void	vfs_lock_init(void);
void	mount_init(struct mount *mp);

void	vn_syncer_add(struct vnode *, int);
void	vn_syncer_remove(struct vnode *);
void	vn_syncer_thr_create(struct mount *);
void	vn_syncer_thr_stop(struct mount *);

extern	struct vop_ops default_vnode_vops;
extern	struct vop_ops dead_vnode_vops;

extern	struct vop_ops *default_vnode_vops_p;
extern	struct vop_ops *dead_vnode_vops_p;

#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_VNODE_H_ */
