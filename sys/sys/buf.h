/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)buf.h	8.9 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/sys/buf.h,v 1.88.2.10 2003/01/25 19:02:23 dillon Exp $
 * $DragonFly: src/sys/sys/buf.h,v 1.54 2008/08/29 20:08:37 dillon Exp $
 */

#ifndef _SYS_BUF_H_
#define	_SYS_BUF_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_DEVICE_H_
#include <sys/device.h>
#endif

#ifndef _SYS_XIO_H_
#include <sys/xio.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_BIO_H_
#include <sys/bio.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

struct buf;
struct bio;
struct mount;
struct vnode;
struct xio;

#define NBUF_BIO	6

struct buf_rb_tree;
struct buf_rb_hash;
RB_PROTOTYPE2(buf_rb_tree, buf, b_rbnode, rb_buf_compare, off_t);
RB_PROTOTYPE2(buf_rb_hash, buf, b_rbhash, rb_buf_compare, off_t);

/*
 * To avoid including <ufs/ffs/softdep.h> 
 */   
LIST_HEAD(workhead, worklist);

#endif

typedef enum buf_cmd {
	BUF_CMD_DONE = 0,
	BUF_CMD_READ,
	BUF_CMD_WRITE,
	BUF_CMD_FREEBLKS,
	BUF_CMD_FORMAT,
	BUF_CMD_FLUSH
} buf_cmd_t;

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * The buffer header describes an I/O operation in the kernel.
 *
 * NOTES:
 *	b_bufsize represents the filesystem block size (for this particular
 *	block) and/or the allocation size or original request size.  This
 *	field is NOT USED by lower device layers.  VNode and device
 *	strategy routines WILL NEVER ACCESS THIS FIELD.
 *
 *	b_bcount represents the I/O request size.  Unless B_NOBCLIP is set,
 *	the device chain is allowed to clip b_bcount to accomodate the device
 *	EOF.  Note that this is different from the byte oriented file EOF.
 *	If B_NOBCLIP is set, the device chain is required to generate an
 *	error if it would othrewise have to clip the request.  Buffers 
 *	obtained via getblk() automatically set B_NOBCLIP.  It is important
 *	to note that EOF clipping via b_bcount is different from EOF clipping
 *	via returning a b_actual < b_bcount.  B_NOBCLIP only effects block
 *	oriented EOF clipping (b_bcount modifications).
 *
 *	b_actual represents the number of bytes of I/O that actually occured,
 *	whether an error occured or not.  b_actual must be initialized to 0
 *	prior to initiating I/O as the device drivers will assume it to
 *	start at 0.
 *
 *	b_dirtyoff, b_dirtyend.  Buffers support piecemeal, unaligned
 *	ranges of dirty data that need to be written to backing store.
 *	The range is typically clipped at b_bcount (not b_bufsize).
 *
 *	b_bio1 and b_bio2 represent the two primary I/O layers.  Additional
 *	I/O layers are allocated out of the object cache and may also exist.
 *
 *	b_bio1 is the logical layer and contains offset or block number 
 *	data for the primary vnode, b_vp.  I/O operations are almost
 *	universally initiated from the logical layer, so you will often
 *	see things like:  vn_strategy(bp->b_vp, &bp->b_bio1).
 *
 *	b_bio2 is the first physical layer (typically the slice-relative
 *	layer) and contains the translated offset or block number for
 *	the block device underlying a filesystem.   Filesystems such as UFS
 *	will maintain cached translations and you may see them initiate
 *	a 'physical' I/O using vn_strategy(devvp, &bp->b_bio2).  BUT, 
 *	remember that the layering is relative to bp->b_vp, so the
 *	device-relative block numbers for buffer cache operations that occur
 *	directly on a block device will be in the first BIO layer.
 *
 *	b_ops - initialized if a buffer has a bio_ops
 *
 *	NOTE!!! Only the BIO subsystem accesses b_bio1 and b_bio2 directly.
 *	ALL STRATEGY LAYERS FOR BOTH VNODES AND DEVICES ONLY ACCESS THE BIO
 *	PASSED TO THEM, AND WILL PUSH ANOTHER BIO LAYER IF FORWARDING THE
 *	I/O DEEPER.  In particular, a vn_strategy() or dev_dstrategy()
 *	call should not ever access buf->b_vp as this vnode may be totally
 *	unrelated to the vnode/device whos strategy routine was called.
 */
struct buf {
	RB_ENTRY(buf) b_rbnode;		/* RB node in vnode clean/dirty tree */
	RB_ENTRY(buf) b_rbhash;		/* RB node in vnode hash tree */
	TAILQ_ENTRY(buf) b_freelist;	/* Free list position if not active. */
	struct buf *b_cluster_next;	/* Next buffer (cluster code) */
	struct vnode *b_vp;		/* (vp, loffset) index */
	struct bio b_bio_array[NBUF_BIO]; /* BIO translation layers */ 
	u_int32_t b_flags;		/* B_* flags. */
	unsigned int b_qindex;		/* buffer queue index */
	unsigned int b_qcpu;		/* buffer queue cpu */
	unsigned char b_act_count;	/* similar to vm_page act_count */
	unsigned char b_unused01;
	struct lock b_lock;		/* Buffer lock */
	void	*b_iosched;		/* I/O scheduler priv data */
	buf_cmd_t b_cmd;		/* I/O command */
	int	b_bufsize;		/* Allocated buffer size. */
	int	b_runningbufspace;	/* when I/O is running, pipelining */
	int	b_bcount;		/* Valid bytes in buffer. */
	int	b_resid;		/* Remaining I/O */
	int	b_error;		/* Error return */
	caddr_t	b_data;			/* Memory, superblocks, indirect etc. */
	caddr_t	b_kvabase;		/* base kva for buffer */
	int	b_kvasize;		/* size of kva for buffer */
	int	b_dirtyoff;		/* Offset in buffer of dirty region. */
	int	b_dirtyend;		/* Offset of end of dirty region. */
	int	b_refs;			/* FINDBLK_REF/bqhold()/bqdrop() */
	struct	xio b_xio;  		/* data buffer page list management */
	struct  bio_ops *b_ops;		/* bio_ops used w/ b_dep */
	struct	workhead b_dep;		/* List of filesystem dependencies. */
};

/*
 * XXX temporary
 */
#define b_bio1		b_bio_array[0]	/* logical layer */
#define b_bio2		b_bio_array[1]	/* (typically) the disk layer */
#define b_loffset	b_bio1.bio_offset


/*
 * Flags passed to getblk()
 *
 * GETBLK_PCATCH - Allow signals to be caught.  getblk() is allowed to return
 *		   NULL if this flag is passed.
 *
 * GETBLK_BHEAVY - This is a heavy weight buffer, meaning that resolving
 *		   writes can require additional buffers.
 *
 * GETBLK_SZMATCH- blksize must match pre-existing b_bcount.  getblk() can
 *		   return NULL.
 *
 * GETBLK_NOWAIT - Do not use a blocking lock.  getblk() can return NULL.
 */
#define GETBLK_PCATCH	0x0001	/* catch signals */
#define GETBLK_BHEAVY	0x0002	/* heavy weight buffer */
#define GETBLK_SZMATCH	0x0004	/* pre-existing buffer must match */
#define GETBLK_NOWAIT	0x0008	/* non-blocking */

#define FINDBLK_TEST	0x0010	/* test only, do not lock */
#define FINDBLK_NBLOCK	0x0020	/* use non-blocking lock, can return NULL */
#define FINDBLK_REF	0x0040	/* ref the buf to prevent reuse */

/*
 * These flags are kept in b_flags.
 *
 * Notes:
 *
 *	B_PAGING	Indicates that bp is being used by the paging system or
 *			some paging system and that the bp is not linked into
 *			the b_vp's clean/dirty linked lists or ref counts.
 *			Buffer vp reassignments are illegal in this case.
 *
 *	B_CACHE		This may only be set if the buffer is entirely valid.
 *			The situation where B_DELWRI is set and B_CACHE is
 *			clear MUST be committed to disk by getblk() so 
 *			B_DELWRI can also be cleared.  See the comments for
 *			getblk() in kern/vfs_bio.c.  If B_CACHE is clear,
 *			the caller is expected to clear B_ERROR|B_INVAL,
 *			set BUF_CMD_READ, and initiate an I/O.
 *
 *			The 'entire buffer' is defined to be the range from
 *			0 through b_bcount.
 *
 *	B_MALLOC	Request that the buffer be allocated from the malloc
 *			pool, DEV_BSIZE aligned instead of PAGE_SIZE aligned.
 *
 *	B_CLUSTEROK	This flag is typically set for B_DELWRI buffers
 *			by filesystems that allow clustering when the buffer
 *			is fully dirty and indicates that it may be clustered
 *			with other adjacent dirty buffers.  Note the clustering
 *			may not be used with the stage 1 data write under NFS
 *			but may be used for the commit rpc portion.
 *
 *	B_VMIO		Indicates that the buffer is tied into an VM object.
 *			The buffer's data is always PAGE_SIZE aligned even
 *			if b_bufsize and b_bcount are not.  ( b_bufsize is 
 *			always at least DEV_BSIZE aligned, though ).
 *	
 *	B_DIRECT	Hint that we should attempt to completely free
 *			the pages underlying the buffer.   B_DIRECT is 
 *			sticky until the buffer is released and typically
 *			only has an effect when B_RELBUF is also set.
 *
 *	B_LOCKED	The buffer will be released to the locked queue
 *			regardless of its current state.  Note that
 *			if B_DELWRI is set, no I/O occurs until the caller
 *			acquires the buffer, clears B_LOCKED, then releases
 *			it again.
 *
 *	B_AGE		When allocating a new buffer any buffer encountered
 *			with B_AGE set will be reallocated more quickly then
 *			buffers encountered without it set.  B_AGE is set
 *			as part of the loop so idle buffers should eventually
 *			wind up with B_AGE set.  B_AGE explicitly does NOT
 *			cause the buffer to be instantly reallocated for
 *			other purposes.
 *
 *			Standard buffer flushing routines leave B_AGE intact
 *			through the DIRTY queue and into the CLEAN queue.
 *			Setting B_AGE on a dirty buffer will not cause it
 *			to be flushed more quickly but will cause it to be
 *			reallocated more quickly after having been flushed.
 *
 *	B_NOCACHE	Request that the buffer and backing store be
 *			destroyed on completion.  If B_DELWRI is set and the
 *			write fails, the buffer remains intact.
 *
 *	B_NOTMETA	May be set on block device buffers representing
 *			file data (i.e. which aren't really meta-data),
 *			which will cause the buffer cache to set PG_NOTMETA
 *			in the VM pages when releasing them and the
 *			swapcache to not try to cache them.
 *
 *	B_MARKER	Special marker buf, always skip.
 */

#define	B_AGE		0x00000001	/* Reuse more quickly */
#define	B_NEEDCOMMIT	0x00000002	/* Append-write in progress. */
#define	B_NOTMETA	0x00000004	/* This really isn't metadata */
#define	B_DIRECT	0x00000008	/* direct I/O flag (pls free vmio) */
#define	B_DEFERRED	0x00000010	/* vfs-controlled deferment */
#define	B_CACHE		0x00000020	/* Bread found us in the cache. */
#define	B_HASHED 	0x00000040 	/* Indexed via v_rbhash_tree */
#define	B_DELWRI	0x00000080	/* Delay I/O until buffer reused. */
#define	B_BNOCLIP	0x00000100	/* EOF clipping b_bcount not allowed */
#define	B_HASBOGUS	0x00000200	/* Contains bogus pages */
#define	B_EINTR		0x00000400	/* I/O was interrupted */
#define	B_ERROR		0x00000800	/* I/O error occurred. */
#define	B_IODEBUG	0x00001000	/* (Debugging only bread) */
#define	B_INVAL		0x00002000	/* Does not contain valid info. */
#define	B_LOCKED	0x00004000	/* Locked in core (not reusable). */
#define	B_NOCACHE	0x00008000	/* Destroy buffer AND backing store */
#define	B_MALLOC	0x00010000	/* malloced b_data */
#define	B_CLUSTEROK	0x00020000	/* Pagein op, so swap() can count it. */
#define	B_MARKER	0x00040000	/* Special marker buf in queue */
#define	B_RAW		0x00080000	/* Set by physio for raw transfers. */
#define	B_HEAVY		0x00100000	/* Heavy-weight buffer */
#define	B_DIRTY		0x00200000	/* Needs writing later. */
#define	B_RELBUF	0x00400000	/* Release VMIO buffer. */
#define	B_UNUSED23	0x00800000	/* Request wakeup on done */
#define	B_VNCLEAN	0x01000000	/* On vnode clean list */
#define	B_VNDIRTY	0x02000000	/* On vnode dirty list */
#define	B_PAGING	0x04000000	/* volatile paging I/O -- bypass VMIO */
#define	B_ORDERED	0x08000000	/* Must guarantee I/O ordering */
#define B_RAM		0x10000000	/* Read ahead mark (flag) */
#define B_VMIO		0x20000000	/* VMIO flag */
#define B_CLUSTER	0x40000000	/* pagein op, so swap() can count it */
#define B_VFSFLAG1	0x80000000	/* VFSs can set this flag */

#define PRINT_BUF_FLAGS "\20"	\
	"\40unused31\37cluster\36vmio\35ram\34ordered" \
	"\33paging\32vndirty\31vnclean\30unused23\27relbuf\26dirty" \
	"\25unused20\24raw\23unused18\22clusterok\21malloc\20nocache" \
	"\17locked\16inval\15unused12\14error\13eintr\12unused9\11bnoclip" \
	"\10delwri\7hashed\6cache\5deferred\4direct\3unused2\2needcommit\1age"

#define	NOOFFSET	(-1LL)		/* No buffer offset calculated yet */

#ifdef _KERNEL
/*
 * Buffer locking.  See sys/buf2.h for inline functions.
 */
extern char *buf_wmesg;			/* Default buffer lock message */
#define BUF_WMESG "bufwait"

#endif /* _KERNEL */

struct bio_queue_head {
	TAILQ_HEAD(bio_queue, bio) queue;
	off_t	off_unused;
	int	reorder;
	struct	bio *transition;
	struct	bio *bio_unused;
};

/*
 * This structure describes a clustered I/O.
 */
struct cluster_save {
	int	bs_nchildren;		/* Number of associated buffers. */
	struct buf **bs_children;	/* List of associated buffers. */
};

/*
 * Zero out the buffer's data area.
 */
#define	clrbuf(bp) {							\
	bzero((bp)->b_data, (u_int)(bp)->b_bcount);			\
	(bp)->b_resid = 0;						\
}

/*
 * Flags to low-level bitmap allocation routines (balloc).
 *
 * Note: sequential_heuristic() in kern/vfs_vnops.c limits the count
 * to 127.
 */
#define B_SEQMASK	0x7F000000	/* Sequential heuristic mask. */
#define B_SEQSHIFT	24		/* Sequential heuristic shift. */
#define B_SEQMAX	0x7F
#define B_CLRBUF	0x01		/* Cleared invalid areas of buffer. */
#define B_SYNC		0x02		/* Do all allocations synchronously. */

#ifdef _KERNEL
extern long	nbuf;			/* The number of buffer headers */
extern long	maxswzone;		/* Max KVA for swap structures */
extern long	maxbcache;		/* Max KVA for buffer cache */
extern long	hidirtybufspace;
extern int      buf_maxio;              /* nominal maximum I/O for buffer */
extern struct buf *buf;			/* The buffer headers. */
extern char	*buffers;		/* The buffer contents. */
extern int	bufpages;		/* Number of memory pages in the buffer pool. */
extern struct	buf *swbuf;		/* Swap I/O buffer headers. */
extern long	nswbuf;			/* Number of swap I/O buffer headers. */
extern int	bioq_reorder_burst_interval;
extern int	bioq_reorder_burst_bytes;
extern int	bioq_reorder_minor_interval;
extern int	bioq_reorder_minor_bytes;

struct uio;
struct devstat;

void	bufinit (void);
long	bd_heatup (void);
void	bd_wait (long count);
void	waitrunningbufspace(void);
int	buf_dirty_count_severe (void);
int	buf_runningbufspace_severe (void);
void	initbufbio(struct buf *);
void	uninitbufbio(struct buf *);
void	reinitbufbio(struct buf *);
void	clearbiocache(struct bio *);
void	bremfree (struct buf *);
int	breadx (struct vnode *, off_t, int, struct buf **);
int	breadnx (struct vnode *, off_t, int, off_t *, int *, int,
		struct buf **);
void	breadcb(struct vnode *, off_t, int,
		void (*)(struct bio *), void *);
int	bwrite (struct buf *);
void	bdwrite (struct buf *);
void	buwrite (struct buf *);
void	bawrite (struct buf *);
void	bdirty (struct buf *);
void	bheavy (struct buf *);
void	bundirty (struct buf *);
int	bowrite (struct buf *);
void	brelse (struct buf *);
void	bqrelse (struct buf *);
int	cluster_awrite (struct buf *);
struct buf *getpbuf (int *);
struct buf *getpbuf_kva (int *);
int	inmem (struct vnode *, off_t);
struct buf *findblk (struct vnode *, off_t, int);
struct buf *getblk (struct vnode *, off_t, int, int, int);
struct buf *getcacheblk (struct vnode *, off_t, int, int);
struct buf *geteblk (int);
struct buf *getnewbuf(int, int, int, int);
void	bqhold(struct buf *bp);
void	bqdrop(struct buf *bp);
void	regetblk(struct buf *bp);
struct bio *push_bio(struct bio *);
struct bio *pop_bio(struct bio *);
int	biowait (struct bio *, const char *);
int	biowait_timeout (struct bio *, const char *, int);
void	bpdone (struct buf *, int);
void	biodone (struct bio *);
void	biodone_sync (struct bio *);
void	pbuf_adjcount(int *pfreecnt, int n);

void	cluster_append(struct bio *, struct buf *);
int	cluster_readx (struct vnode *, off_t, off_t, int,
	    size_t, size_t, struct buf **);
void	cluster_readcb (struct vnode *, off_t, off_t, int,
	    size_t, size_t, void (*func)(struct bio *), void *arg);
void	cluster_write (struct buf *, off_t, int, int);
int	physread (struct dev_read_args *);
int	physwrite (struct dev_write_args *);
void	vfs_bio_clrbuf (struct buf *);
void	vfs_busy_pages (struct vnode *, struct buf *);
void	vfs_unbusy_pages (struct buf *);
int	vmapbuf (struct buf *, caddr_t, int);
void	vunmapbuf (struct buf *);
void	relpbuf (struct buf *, int *);
void	brelvp (struct buf *);
int	bgetvp (struct vnode *, struct buf *, int);
void	bsetrunningbufspace(struct buf *, int);
int	allocbuf (struct buf *bp, int size);
int	scan_all_buffers (int (*)(struct buf *, void *), void *);
void	reassignbuf (struct buf *);
struct	buf *trypbuf (int *);
struct	buf *trypbuf_kva (int *);
void	bio_ops_sync(struct mount *mp);
void	vm_hold_free_pages(struct buf *bp, vm_offset_t from, vm_offset_t to);
void	vm_hold_load_pages(struct buf *bp, vm_offset_t from, vm_offset_t to);
void	nestiobuf_done(struct bio *mbio, int donebytes, int error, struct devstat *stats);
void	nestiobuf_init(struct bio *mbio);
void	nestiobuf_add(struct bio *mbio, struct buf *bp, int off, size_t size, struct devstat *stats);
void	nestiobuf_start(struct bio *mbio);
void	nestiobuf_error(struct bio *mbio, int error);
#endif	/* _KERNEL */
#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_BUF_H_ */
