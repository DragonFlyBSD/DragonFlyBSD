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
 * $DragonFly: src/sys/sys/vnode.h,v 1.30 2004/12/29 02:40:03 dillon Exp $
 */

#ifndef _SYS_VNODE_H_
#define	_SYS_VNODE_H_

#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/acl.h>
#include <sys/namecache.h>
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/thread.h>
#endif
#include <sys/vfsops.h>
#include <sys/vfscache.h>

#include <machine/lock.h>

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
 * Range locks protect offset ranges in files and directories at a high
 * level, allowing the actual I/O to be broken down into smaller pieces.
 * Range locks will eventually be integrated into the clustered cache
 * coherency infrastructure.
 *
 * We use a simple data structure for now, but eventually this should 
 * probably be a btree or red-black tree.
 */
struct vrangelock;

TAILQ_HEAD(vrangelock_list, vrangelock);

struct vrangehead {
	struct vrangelock_list	vh_list;
};

struct vrangelock {
	TAILQ_ENTRY(vrangelock) vr_node;
	int		vr_flags;
	off_t		vr_offset;
	off_t		vr_length;
};

#define RNGL_WAITING	0x0001		/* waiting for lock, else has lock */
#define RNGL_CHECK	0x0002		/* check for work on unlock */
#define RNGL_SHARED	0x0004		/* shared lock, else exclusive */
#define RNGL_ONLIST	0x0008		/* sanity check */

static __inline
void
vrange_init(struct vrangelock *vr, int flags, off_t offset, off_t length)
{
	vr->vr_flags = flags;
	vr->vr_offset = offset;
	vr->vr_length = length;
}

#ifdef _KERNEL

void	vrange_lock(struct vnode *vp, struct vrangelock *vr);
void	vrange_unlock(struct vnode *vp, struct vrangelock *vr);

static __inline
void
vrange_lock_shared(struct vnode *vp, struct vrangelock *vr, 
		    off_t offset, off_t length)
{
	vrange_init(vr, RNGL_SHARED, offset, length);
	vrange_lock(vp, vr);
}

static __inline
void
vrange_lock_excl(struct vnode *vp, struct vrangelock *vr,
		    off_t offset, off_t length)
{
	vrange_init(vr, 0, offset, length);
	vrange_lock(vp, vr);
}

#endif

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
 * NOTE: XXX v_opencount currently only used by specfs.  It should be used
 *	 universally.  
 *
 * NOTE: The vnode operations vector, v_ops, is a double-indirect that
 *	 typically points to &v_mount->mnt_vn_use_ops.  We use a double
 *	 pointer because mnt_vn_use_ops may change dynamically when e.g.
 *	 journaling is turned on or off.
 */
struct vnode {
	u_long	v_flag;				/* vnode flags (see below) */
	int	v_usecount;			/* reference count of users */
	int	v_writecount;			/* reference count of writers */
	int	v_holdcnt;			/* page & buffer references */
	int	v_opencount;			/* number of explicit opens */
	u_long	v_id;				/* capability identifier */
	struct	mount *v_mount;			/* ptr to vfs we are in */
	struct  vop_ops **v_ops;		/* vnode operations vector */
	TAILQ_ENTRY(vnode) v_freelist;		/* vnode freelist */
	TAILQ_ENTRY(vnode) v_nmntvnodes;	/* vnodes for mount point */
	struct	buflists v_cleanblkhd;		/* clean blocklist head */
	struct	buflists v_dirtyblkhd;		/* dirty blocklist head */
	LIST_ENTRY(vnode) v_synclist;		/* vnodes with dirty buffers */
	long	v_numoutput;			/* num of writes in progress */
	enum	vtype v_type;			/* vnode type */
	union {
		struct mount	*vu_mountedhere;/* ptr to mounted vfs (VDIR) */
		struct socket	*vu_socket;	/* unix ipc (VSOCK) */
		struct {
			udev_t	vu_udev;	/* device number for attach */
			struct specinfo	*vu_specinfo; /* device (VCHR, VBLK) */
			SLIST_ENTRY(vnode) vu_specnext;
		} vu_spec;
		struct fifoinfo	*vu_fifoinfo;	/* fifo (VFIFO) */
	} v_un;
	struct	nqlease *v_lease;		/* Soft reference to lease */
	daddr_t	v_lastw;			/* last write (write cluster) */
	daddr_t	v_cstart;			/* start block of cluster */
	daddr_t	v_lasta;			/* last allocation */
	int	v_clen;				/* length of current cluster */
	struct vm_object *v_object;		/* Place to store VM object */
	struct	lock v_lock;			/* file/dir ops lock */
	enum	vtagtype v_tag;			/* type of underlying data */
	void	*v_data;			/* private data for fs */
	struct namecache_list v_namecache;	/* associated nc entries */
	struct	{
		struct	lwkt_token vpi_token;	/* lock to protect below */
		struct	selinfo vpi_selinfo;	/* identity of poller(s) */
		short	vpi_events;		/* what they are looking for */
		short	vpi_revents;		/* what has happened */
	} v_pollinfo;
	struct vmresident *v_resident;		/* optional vmresident */
	struct vrangehead v_range;		/* range lock */
#ifdef	DEBUG_LOCKS
	const char *filename;			/* Source file doing locking */
	int line;				/* Line number doing locking */
#endif
	void	*v_xaddr;
};
#define	v_mountedhere	v_un.vu_mountedhere
#define	v_socket	v_un.vu_socket
#define v_udev		v_un.vu_spec.vu_udev
#define	v_rdev		v_un.vu_spec.vu_specinfo
#define	v_specnext	v_un.vu_spec.vu_specnext
#define	v_fifoinfo	v_un.vu_fifoinfo

#define	VN_POLLEVENT(vp, events)				\
	do {							\
		if ((vp)->v_pollinfo.vpi_events & (events))	\
			vn_pollevent((vp), (events));		\
	} while (0)

/*
 * Vnode flags.
 */
#define	VROOT		0x00001	/* root of its file system */
#define	VTEXT		0x00002	/* vnode is a pure text prototype */
#define	VSYSTEM		0x00004	/* vnode being used by kernel */
#define	VISTTY		0x00008	/* vnode represents a tty */
#define VCTTYISOPEN	0x00010	/* controlling terminal tty is open */
#define VCKPT		0x00020	/* checkpoint-restored vnode */
/* open for business    0x00040 */
/* open for business    0x00080 */
/* open for business    0x00100 */
/* open for business    0x00200 */
#define	VBWAIT		0x00400	/* waiting for output to complete */
/* open for business    0x00800 */
/* open for business    0x01000 */
#define	VOBJBUF		0x02000	/* Allocate buffers in VM object */
#define	VINACTIVE	0x04000	/* The vnode is inactive */
#define	VAGE		0x08000	/* Insert vnode at head of free list */
#define	VOLOCK		0x10000	/* vnode is locked waiting for an object */
#define	VOWANT		0x20000	/* a process is waiting for VOLOCK */
#define	VRECLAIMED	0x40000	/* This vnode has been destroyed */
#define	VFREE		0x80000	/* This vnode is on the freelist */
/* open for business    0x100000 */
#define	VONWORKLST	0x200000 /* On syncer work-list */
#define	VMOUNT		0x400000 /* Mount in progress */
#define	VOBJDIRTY	0x800000 /* object might be dirty */
#define	VPLACEMARKER	0x1000000 /* dummy vnode placemarker */

/*
 * vmntvnodescan() flags
 */
#define VMSC_GETVP	1
#define VMSC_GETVX	2
#define VMSC_REFVP	3
#define VMSC_NOWAIT	0x10

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
#define	IO_NOWDRAIN	0x0200		/* do not block on wdrain */
#define	IO_CORE		0x0400		/* I/O is part of core dump */

#define	IO_SEQMAX	0x7F		/* seq heuristic max value */
#define	IO_SEQSHIFT	16		/* seq heuristic in upper 16 bits */

/*
 * Modes.  Note that these V-modes must match file S_I*USR, SUID, SGID,
 * and SVTX flag bits.
 *
 * VCREATE, VDELETE, and VEXCL may only be used in naccess() calls.
 */
#define VDELETE	040000		/* delete if the file/dir exists */
#define VCREATE	020000		/* create if the file/dir does not exist */
#define VEXCL	010000		/* error if the file/dir already exists */

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
#define	REVOKEALL	0x0001		/* vop_revoke: revoke all aliases */

#ifdef DIAGNOSTIC
#define	VATTR_NULL(vap)	vattr_null(vap)
#else
#define	VATTR_NULL(vap)	(*(vap) = va_null)	/* initialize a vattr */
#endif /* DIAGNOSTIC */

#define	NULLVP	((struct vnode *)NULL)

#define	VNODEOP_SET(f) \
	C_SYSINIT(f##init, SI_SUB_VFS, SI_ORDER_SECOND, vfs_add_vnodeops_sysinit, &f); \
	C_SYSUNINIT(f##uninit, SI_SUB_VFS, SI_ORDER_SECOND,vfs_rm_vnodeops_sysinit, &f);

/*
 * Global vnode data.
 */
extern	struct vnode *rootvnode;	/* root (i.e. "/") vnode */
extern  struct namecache *rootncp;	/* root (i.e. "/") namecache */
extern	int desiredvnodes;		/* number of vnodes desired */
extern	time_t syncdelay;		/* max time to delay syncing data */
extern	time_t filedelay;		/* time to delay syncing files */
extern	time_t dirdelay;		/* time to delay syncing directories */
extern	time_t metadelay;		/* time to delay syncing metadata */
extern	struct vm_zone *namei_zone;
extern	int prtactive;			/* nonzero to call vprint() */
extern	struct vattr va_null;		/* predefined null vattr structure */
extern	int vfs_ioopt;
extern	int numvnodes;
extern	int freevnodes;
extern  int vfs_fastdev;		/* fast specfs device access */

/*
 * Macro/function to check for client cache inconsistency w.r.t. leasing.
 */
#define	LEASE_READ	0x1		/* Check lease for readers */
#define	LEASE_WRITE	0x2		/* Check lease for modifiers */


extern void	(*lease_updatetime) (int deltat);

#endif /* _KERNEL */

/*
 * Mods for extensibility.
 */

/*
 * Flags for vdesc_flags:
 */
#define	VDESC_MAX_VPS		8
/* Low order 8 flag bits are reserved for willrele flags for vp arguments. */
#define	VDESC_VP0_WILLRELE	0x00000001
#define	VDESC_VP1_WILLRELE	0x00000002
#define	VDESC_VP2_WILLRELE	0x00000004
#define	VDESC_VP3_WILLRELE	0x00000008
#define	VDESC_VP4_WILLRELE	0x00000010
#define	VDESC_VP5_WILLRELE	0x00000020
#define	VDESC_VP6_WILLRELE	0x00000040
#define	VDESC_VP7_WILLRELE	0x00000080
#define	VDESC_NOMAP_VPP		0x00000100
#define	VDESC_VPP_WILLRELE	0x00000200
#define VDESC_VP0_WILLUNLOCK	0x00010000
#define VDESC_VP1_WILLUNLOCK	0x00020000
#define VDESC_VP2_WILLUNLOCK	0x00040000
#define VDESC_VP3_WILLUNLOCK	0x00080000
#define VDESC_VP4_WILLUNLOCK	0x00100000
#define VDESC_VP5_WILLUNLOCK	0x00200000
#define VDESC_VP6_WILLUNLOCK	0x00400000
#define VDESC_VP7_WILLUNLOCK	0x00800000

/*
 * VDESC_NO_OFFSET is used to identify the end of the offset list
 * and in places where no such field exists.
 */
#define VDESC_NO_OFFSET -1

/*
 * This structure describes the vnode operation taking place.
 */
struct vnodeop_desc {
	int	vdesc_offset;		/* offset in vector--first for speed */
	char    *vdesc_name;		/* a readable name for debugging */
	int	vdesc_flags;		/* VDESC_* flags */

	/*
	 * These ops are used by bypass routines to map and locate arguments.
	 * Creds and procs are not needed in bypass routines, but sometimes
	 * they are useful to (for example) transport layers.
	 * Nameidata is useful because it has a cred in it.
	 */
	int	*vdesc_vp_offsets;	/* list ended by VDESC_NO_OFFSET */
	int	vdesc_vpp_offset;	/* return vpp location */
	int	vdesc_cred_offset;	/* cred location, if any */
	int	vdesc_proc_offset;	/* proc location, if any */
	int	vdesc_componentname_offset; /* if any */
};

#ifdef _KERNEL
/*
 * A list of all the operation descs.
 */
extern struct vnodeop_desc *vnodeop_descs[];

/*
 * Interlock for scanning list of vnodes attached to a mountpoint
 */
extern struct lwkt_token mntvnode_token;

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


/*
 * This structure is used to configure the new vnodeops vector.  The entry
 * descriptor describes a patricular VOP function while the operations
 * vector descriptor recursively describes arrays of entry descriptors.
 */
struct vnodeopv_entry_desc {
	struct vnodeop_desc *opve_op;
	int	(*opve_func)(struct vop_generic_args *ap);
};

struct vnodeopv_desc {
	struct vop_ops **opv_desc_vector;	    /* vect to allocate/fill*/
	struct vnodeopv_entry_desc *opv_desc_ops;   /* null terminated list */
};

struct vnodeopv_node {
	TAILQ_ENTRY(vnodeopv_node)	entry;
	struct vop_ops			*ops;	    /* allocated vector */
	struct vnodeopv_entry_desc	*descs;	    /* null terminated list */
};

#ifdef DEBUG_VFS_LOCKS
/*
 * Macros to aid in tracing VFS locking problems.  Not totally
 * reliable since if the process sleeps between changing the lock
 * state and checking it with the assert, some other process could
 * change the state.  They are good enough for debugging a single
 * filesystem using a single-threaded test.  I find that 'cvs co src'
 * is a pretty good test.
 */

/*
 * [dfr] Kludge until I get around to fixing all the vfs locking.
 */
#define IS_LOCKING_VFS(vp)	((vp)->v_tag == VT_UFS		\
				 || (vp)->v_tag == VT_MFS	\
				 || (vp)->v_tag == VT_NFS	\
				 || (vp)->v_tag == VT_LFS	\
				 || (vp)->v_tag == VT_ISOFS	\
				 || (vp)->v_tag == VT_MSDOSFS)

#define	ASSERT_VOP_LOCKED(vp, str) assert_vop_locked(vp, str)
#define	ASSERT_VOP_UNLOCKED(vp, str) assert_vop_unlocked(vp, str);

#define ASSERT_VOP_ELOCKED(vp, str)					\
do {									\
	struct vnode *_vp = (vp);					\
									\
	if (_vp && IS_LOCKING_VFS(_vp) &&				\
	    VOP_ISLOCKED(_vp, curthread) != LK_EXCLUSIVE)		\
		panic("%s: %p is not exclusive locked but should be",	\
		    str, _vp);						\
} while (0)

#define ASSERT_VOP_ELOCKED_OTHER(vp, str)				\
do {									\
	struct vnode *_vp = (vp);					\
									\
	if (_vp && IS_LOCKING_VFS(_vp) &&				\
	    VOP_ISLOCKED(_vp, curthread) != LK_EXCLOTHER)		\
		panic("%s: %p is not exclusive locked by another proc",	\
		    str, _vp);						\
} while (0)

#define ASSERT_VOP_SLOCKED(vp, str)					\
do {									\
	struct vnode *_vp = (vp);					\
									\
	if (_vp && IS_LOCKING_VFS(_vp) &&				\
	    VOP_ISLOCKED(_vp, NULL) != LK_SHARED)			\
		panic("%s: %p is not locked shared but should be",	\
		    str, _vp);						\
} while (0)

void	assert_vop_locked(struct vnode *vp, const char *str);
void	assert_vop_unlocked(struct vnode *vp, const char *str);

#else

#define	ASSERT_VOP_LOCKED(vp, str)
#define	ASSERT_VOP_UNLOCKED(vp, str)

#endif /* DEBUG_VFS_LOCKS */

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
	(*(vocall_func_t *)((char *)(vops)+((ap)->a_desc->vdesc_offset)))(ap)

#define	VDESC(OP) (& __CONCAT(OP,_desc))
#define	VOFFSET(OP) (VDESC(OP)->vdesc_offset)

/*
 * VMIO support inline
 */

extern int vmiodirenable;
 
static __inline int
vn_canvmio(struct vnode *vp) 
{
    if (vp && (vp->v_type == VREG || (vmiodirenable && vp->v_type == VDIR)))
        return(TRUE); 
    return(FALSE); 
}

/*
 * Public vnode manipulation functions.
 */
struct file;
struct mount;
struct nlookupdata;
struct ostat;
struct proc;
struct thread;
struct stat;
struct nstat;
struct ucred;
struct uio;
struct vattr;
struct vnode;
struct vop_bwrite_args;

extern int	(*lease_check_hook) (struct vop_lease_args *);

void	addaliasu (struct vnode *vp, udev_t nvp_udev);
int	v_associate_rdev(struct vnode *vp, dev_t dev);
void	v_release_rdev(struct vnode *vp);
int 	bdevvp (dev_t dev, struct vnode **vpp);
void	cvtstat (struct stat *st, struct ostat *ost);
void	cvtnstat (struct stat *sb, struct nstat *nsb);
struct vnode *allocvnode(int lktimeout, int lkflags);
struct vnode *allocvnode_placemarker(void);
void freevnode_placemarker(struct vnode *);
int	getnewvnode (enum vtagtype tag, struct mount *mp, 
		    struct vnode **vpp, int timo, int lkflags);
int	getspecialvnode (enum vtagtype tag, struct mount *mp, 
		    struct vop_ops **ops, struct vnode **vpp, int timo, 
		    int lkflags);
int	lease_check (struct vop_lease_args *ap);
int	spec_vnoperate (struct vop_generic_args *);
int	speedup_syncer (void);
void	vattr_null (struct vattr *vap);
int	vcount (struct vnode *vp);
int	vfinddev (dev_t dev, enum vtype type, struct vnode **vpp);
void	vfs_add_vnodeops_sysinit (const void *);
void	vfs_rm_vnodeops_sysinit (const void *);
void	vfs_add_vnodeops(struct mount *, struct vop_ops **,
			struct vnodeopv_entry_desc *);
void	vfs_rm_vnodeops(struct vop_ops **);
int	vflush (struct mount *mp, int rootrefs, int flags);
int	vmntvnodescan(struct mount *mp, int flags,
	    int (*fastfunc)(struct mount *mp, struct vnode *vp, void *data),
	    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
	    void *data);
void	insmntque(struct vnode *vp, struct mount *mp);

void	vclean (struct vnode *vp, int flags, struct thread *td);
void	vgone (struct vnode *vp);
int	vinvalbuf (struct vnode *vp, int save, 
	    struct thread *td, int slpflag, int slptimeo);
int	vtruncbuf (struct vnode *vp, struct thread *td,
		off_t length, int blksize);
void	vprint (char *label, struct vnode *vp);
int	vrecycle (struct vnode *vp, struct thread *td);
int	vn_close (struct vnode *vp, int flags, struct thread *td);
int	vn_isdisk (struct vnode *vp, int *errp);
int	vn_lock (struct vnode *vp, int flags, struct thread *td);
#ifdef	DEBUG_LOCKS
int	debug_vn_lock (struct vnode *vp, int flags, struct thread *td,
	    const char *filename, int line);
#define vn_lock(vp,flags,p) debug_vn_lock(vp,flags,p,__FILE__,__LINE__)
#endif

void	vn_setspecops (struct file *fp);
int	vn_fullpath (struct proc *p, struct vnode *vn, char **retbuf, char **freebuf);
int	vn_open (struct nlookupdata *ndp, struct file *fp, int fmode, int cmode);
void	vn_pollevent (struct vnode *vp, int events);
void	vn_pollgone (struct vnode *vp);
int	vn_pollrecord (struct vnode *vp, struct thread *td, int events);
int 	vn_rdwr (enum uio_rw rw, struct vnode *vp, caddr_t base,
	    int len, off_t offset, enum uio_seg segflg, int ioflg,
	    struct ucred *cred, int *aresid, struct thread *td);
int	vn_rdwr_inchunks (enum uio_rw rw, struct vnode *vp, caddr_t base,
	    int len, off_t offset, enum uio_seg segflg, int ioflg,
	    struct ucred *cred, int *aresid, struct thread *td);
int	vn_stat (struct vnode *vp, struct stat *sb, struct thread *td);
dev_t	vn_todev (struct vnode *vp);
int	vfs_object_create (struct vnode *vp, struct thread *td);
void	vfs_timestamp (struct timespec *);
int	vn_writechk (struct vnode *vp);
int	vop_stdbwrite (struct vop_bwrite_args *ap);
int	vop_stdislocked (struct vop_islocked_args *ap);
int	vop_stdlock (struct vop_lock_args *ap);
int	vop_stdrlock (struct vop_lock_args *ap);
int	vop_stdunlock (struct vop_unlock_args *ap);
int	vop_nopoll (struct vop_poll_args *ap);
int	vop_stdpathconf (struct vop_pathconf_args *ap);
int	vop_stdpoll (struct vop_poll_args *ap);
int	vop_stdrevoke (struct vop_revoke_args *ap);
int	vop_eopnotsupp (struct vop_generic_args *ap);
int	vop_ebadf (struct vop_generic_args *ap);
int	vop_einval (struct vop_generic_args *ap);
int	vop_enotty (struct vop_generic_args *ap);
int	vop_defaultop (struct vop_generic_args *ap);
int	vop_null (struct vop_generic_args *ap);
int	vop_panic (struct vop_generic_args *ap);
int	vop_stdcreatevobject (struct vop_createvobject_args *ap);
int	vop_stddestroyvobject (struct vop_destroyvobject_args *ap);
int	vop_stdgetvobject (struct vop_getvobject_args *ap);

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

int	vx_lock (struct vnode *vp);
void	vx_unlock (struct vnode *vp);
int	vx_get (struct vnode *vp);
int	vx_get_nonblock (struct vnode *vp);
void	vx_put (struct vnode *vp);
int	vget (struct vnode *vp, int lockflag, struct thread *td);
void	vput (struct vnode *vp);
void	vhold (struct vnode *);
void	vdrop (struct vnode *);
void	vref (struct vnode *vp);
void	vrele (struct vnode *vp);
void	vsetflags (struct vnode *vp, int flags);
void	vclrflags (struct vnode *vp, int flags);

void	vfs_subr_init(void);
void	vfs_mount_init(void);
void	vfs_lock_init(void);
void	vfs_sync_init(void);

void	vn_syncer_add_to_worklist(struct vnode *, int);
void	vnlru_proc_wait(void);

extern	struct vop_ops *default_vnode_vops;
extern	struct vop_ops *spec_vnode_vops;
extern	struct vop_ops *dead_vnode_vops;

#endif /* _KERNEL */

#endif /* !_SYS_VNODE_H_ */
