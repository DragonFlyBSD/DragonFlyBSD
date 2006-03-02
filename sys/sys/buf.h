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
 * $DragonFly: src/sys/sys/buf.h,v 1.23 2006/03/02 19:26:17 dillon Exp $
 */

#ifndef _SYS_BUF_H_
#define	_SYS_BUF_H_

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

#define NBUF_BIO	4

struct buf_rb_tree;
RB_PROTOTYPE(buf_rb_tree, buf, b_rbnode, rb_buf_compare);

/*
 * To avoid including <ufs/ffs/softdep.h> 
 */   
LIST_HEAD(workhead, worklist);
/*
 * These are currently used only by the soft dependency code, hence
 * are stored once in a global variable. If other subsystems wanted
 * to use these hooks, a pointer to a set of bio_ops could be added
 * to each buffer.
 */
extern struct bio_ops {
	void	(*io_start) (struct buf *);
	void	(*io_complete) (struct buf *);
	void	(*io_deallocate) (struct buf *);
	int 	(*io_fsync) (struct vnode *);
	int 	(*io_sync) (struct mount *);
	void	(*io_movedeps) (struct buf *, struct buf *);
	int 	(*io_countdeps) (struct buf *, int);
} bioops;

/*
 * The buffer header describes an I/O operation in the kernel.
 *
 * NOTES:
 *	b_bufsize, b_bcount.  b_bufsize is the allocation size of the
 *	buffer, either DEV_BSIZE or PAGE_SIZE aligned.  b_bcount is the
 *	originally requested buffer size and can serve as a bounds check
 *	against EOF.  For most, but not all uses, b_bcount == b_bufsize.
 *
 *	b_dirtyoff, b_dirtyend.  Buffers support piecemeal, unaligned
 *	ranges of dirty data that need to be written to backing store.
 *	The range is typically clipped at b_bcount ( not b_bufsize ).
 *
 *	b_resid.  Number of bytes remaining in I/O.  After an I/O operation
 *	completes, b_resid is usually 0 indicating 100% success.
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
 *	NOTE!!! Only the BIO subsystem accesses b_bio1 and b_bio2 directly.
 *	ALL STRATEGY LAYERS FOR BOTH VNODES AND DEVICES ONLY ACCESS THE BIO
 *	PASSED TO THEM, AND WILL PUSH ANOTHER BIO LAYER IF FORWARDING THE
 *	I/O DEEPER.  In particular, a vn_strategy() or dev_dstrategy()
 *	call should not ever access buf->b_vp as this vnode may be totally
 *	unrelated to the vnode/device whos strategy routine was called.
 */
struct buf {
	LIST_ENTRY(buf) b_hash;		/* Hash chain. */
	RB_ENTRY(buf) b_rbnode;		/* Red-Black node in vnode RB tree */
	TAILQ_ENTRY(buf) b_freelist;	/* Free list position if not active. */
	struct buf *b_cluster_next;	/* Next buffer (cluster code) */
	struct vnode *b_vp;		/* (vp, lblkno) index */
	struct bio b_bio_array[NBUF_BIO]; /* BIO translation layers */ 
	long	b_flags;		/* B_* flags. */
	unsigned short b_qindex;	/* buffer queue index */
	unsigned char b_xflags;		/* extra flags */
	struct lock b_lock;		/* Buffer lock */
	long	b_bufsize;		/* Allocated buffer size. */
	long	b_runningbufspace;	/* when I/O is running, pipelining */
	long	b_bcount;		/* Valid bytes in buffer. */
	long	b_resid;		/* Remaining I/O */
	int	b_error;		/* Error return */
	caddr_t	b_data;			/* Memory, superblocks, indirect etc. */
	caddr_t	b_kvabase;		/* base kva for buffer */
	int	b_kvasize;		/* size of kva for buffer */
	int	b_dirtyoff;		/* Offset in buffer of dirty region. */
	int	b_dirtyend;		/* Offset of end of dirty region. */
	void	*b_saveaddr;		/* Original b_addr for physio. */
	struct	xio b_xio;  		/* data buffer page list management */
	struct	workhead b_dep;		/* List of filesystem dependencies. */
};

/*
 * XXX temporary
 */
#define b_bio1		b_bio_array[0]	/* logical layer */
#define b_bio2		b_bio_array[1]	/* (typically) the disk layer */
#define b_loffset	b_bio1.bio_offset
#define b_lblkno	b_bio1.bio_blkno

/*
 * These flags are kept in b_flags.
 *
 * Notes:
 *
 *	B_ASYNC		VOP calls on bp's are usually async whether or not
 *			B_ASYNC is set, but some subsystems, such as NFS, like 
 *			to know what is best for the caller so they can
 *			optimize the I/O.
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
 *			set B_READ, and initiate an I/O.
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
 *	B_NOWDRAIN	This flag should be set when a device (like VN)
 *			does a turn-around VOP_WRITE from its strategy
 *			routine.  This flag prevents bwrite() from blocking
 *			in wdrain, avoiding a deadlock situation.
 */

#define	B_AGE		0x00000001	/* Move to age queue when I/O done. */
#define	B_NEEDCOMMIT	0x00000002	/* Append-write in progress. */
#define	B_ASYNC		0x00000004	/* Start I/O, do not wait. */
#define	B_DIRECT	0x00000008	/* direct I/O flag (pls free vmio) */
#define	B_DEFERRED	0x00000010	/* Skipped over for cleaning */
#define	B_CACHE		0x00000020	/* Bread found us in the cache. */
#define	B_UNUSED40 	0x00000040 	/* Unused */
#define	B_DELWRI	0x00000080	/* Delay I/O until buffer reused. */
#define	B_FREEBUF	0x00000100	/* Instruct driver: free blocks */
#define	B_DONE		0x00000200	/* I/O completed. */
#define	B_EINTR		0x00000400	/* I/O was interrupted */
#define	B_ERROR		0x00000800	/* I/O error occurred. */
#define	B_UNUSED1000	0x00001000	/* Unused */
#define	B_INVAL		0x00002000	/* Does not contain valid info. */
#define	B_LOCKED	0x00004000	/* Locked in core (not reusable). */
#define	B_NOCACHE	0x00008000	/* Do not cache block after use. */
#define	B_MALLOC	0x00010000	/* malloced b_data */
#define	B_CLUSTEROK	0x00020000	/* Pagein op, so swap() can count it. */
#define	B_PHYS		0x00040000	/* I/O to user memory. */
#define	B_RAW		0x00080000	/* Set by physio for raw transfers. */
#define	B_READ		0x00100000	/* Read buffer. */
#define	B_DIRTY		0x00200000	/* Needs writing later. */
#define	B_RELBUF	0x00400000	/* Release VMIO buffer. */
#define	B_WANT		0x00800000	/* Used by vm_pager.c */
#define	B_WRITE		0x00000000	/* Write buffer (pseudo flag). */
#define	B_UNUSED1000000	0x01000000	/* Unused */
#define	B_XXX		0x02000000	/* Debugging flag. */
#define	B_PAGING	0x04000000	/* volatile paging I/O -- bypass VMIO */
#define	B_ORDERED	0x08000000	/* Must guarantee I/O ordering */
#define B_RAM		0x10000000	/* Read ahead mark (flag) */
#define B_VMIO		0x20000000	/* VMIO flag */
#define B_CLUSTER	0x40000000	/* pagein op, so swap() can count it */
#define B_NOWDRAIN	0x80000000	/* Avoid wdrain deadlock */

#define PRINT_BUF_FLAGS "\20\40nowdrain\37cluster\36vmio\35ram\34ordered" \
	"\33paging\32xxx\31writeinprog\30want\27relbuf\26dirty" \
	"\25read\24raw\23phys\22clusterok\21malloc\20nocache" \
	"\17locked\16inval\15scanned\14error\13eintr\12done\11freebuf" \
	"\10delwri\7call\6cache\4direct\3async\2needcommit\1age"

/*
 * These flags are kept in b_xflags.
 */
#define	BX_VNDIRTY	0x00000001	/* On vnode dirty list */
#define	BX_VNCLEAN	0x00000002	/* On vnode clean list */
#define	BX_BKGRDWRITE	0x00000004	/* Do writes in background */
#define	BX_BKGRDINPROG	0x00000008	/* Background write in progress */
#define	BX_BKGRDWAIT	0x00000010	/* Background write waiting */
#define BX_AUTOCHAINDONE 0x00000020	/* pager I/O chain auto mode */

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
	daddr_t	last_blkno;
	struct	bio *insert_point;
	struct	bio *switch_point;
};

/*
 * This structure describes a clustered I/O.  It is stored in the b_saveaddr
 * field of the buffer on which I/O is done.  At I/O completion, cluster
 * callback uses the structure to parcel I/O's to individual buffers, and
 * then free's this structure.
 */
struct cluster_save {
	long	bs_bcount;		/* Saved b_bcount. */
	long	bs_bufsize;		/* Saved b_bufsize. */
	void	*bs_saveaddr;		/* Saved b_addr. */
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
extern int	nbuf;			/* The number of buffer headers */
extern int	maxswzone;		/* Max KVA for swap structures */
extern int	maxbcache;		/* Max KVA for buffer cache */
extern int	runningbufspace;
extern int      buf_maxio;              /* nominal maximum I/O for buffer */
extern struct	buf *buf;		/* The buffer headers. */
extern char	*buffers;		/* The buffer contents. */
extern int	bufpages;		/* Number of memory pages in the buffer pool. */
extern struct	buf *swbuf;		/* Swap I/O buffer headers. */
extern int	nswbuf;			/* Number of swap I/O buffer headers. */

struct uio;

caddr_t bufhashinit (caddr_t);
void	bufinit (void);
void	bwillwrite (void);
int	buf_dirty_count_severe (void);
void	initbufbio(struct buf *);
void	reinitbufbio(struct buf *);
void	clearbiocache(struct bio *);
void	bremfree (struct buf *);
int	bread (struct vnode *, daddr_t, int, struct buf **);
int	breadn (struct vnode *, daddr_t, int, daddr_t *, int *, int,
	    struct buf **);
int	bwrite (struct buf *);
void	bdwrite (struct buf *);
void	bawrite (struct buf *);
void	bdirty (struct buf *);
void	bundirty (struct buf *);
int	bowrite (struct buf *);
void	brelse (struct buf *);
void	bqrelse (struct buf *);
int	vfs_bio_awrite (struct buf *);
struct buf *getpbuf (int *);
struct buf *incore (struct vnode *, daddr_t);
struct buf *gbincore (struct vnode *, daddr_t);
int	inmem (struct vnode *, daddr_t);
struct buf *getblk (struct vnode *, daddr_t, int, int, int);
struct buf *geteblk (int);
struct bio *push_bio(struct bio *);
void pop_bio(struct bio *);
int	biowait (struct buf *);
void	biodone (struct bio *);

void	cluster_append(struct bio *, struct buf *);
int	cluster_read (struct vnode *, u_quad_t, daddr_t, long,
	    long, int, struct buf **);
int	cluster_wbuild (struct vnode *, long, daddr_t, int);
void	cluster_write (struct buf *, u_quad_t, int);
int	physio (dev_t dev, struct uio *uio, int ioflag);
#define physread physio
#define physwrite physio
void	vfs_bio_set_validclean (struct buf *, int base, int size);
void	vfs_bio_clrbuf (struct buf *);
void	vfs_busy_pages (struct buf *, int clear_modify);
void	vfs_unbusy_pages (struct buf *);
int	vmapbuf (struct buf *);
void	vunmapbuf (struct buf *);
void	relpbuf (struct buf *, int *);
void	brelvp (struct buf *);
void	bgetvp (struct vnode *, struct buf *);
void	pbgetvp (struct vnode *, struct buf *);
void	pbrelvp (struct buf *);
int	allocbuf (struct buf *bp, int size);
void	reassignbuf (struct buf *, struct vnode *);
void	pbreassignbuf (struct buf *, struct vnode *);
struct	buf *trypbuf (int *);

#endif /* _KERNEL */

#endif /* !_SYS_BUF_H_ */
