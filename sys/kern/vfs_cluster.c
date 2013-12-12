/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 * Modifications/enhancements:
 * 	Copyright (c) 1995 John S. Dyson.  All rights reserved.
 *	Copyright (c) 2012-2013 Matthew Dillon.  All rights reserved.
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
 */

#include "opt_debug_cluster.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/resourcevar.h>
#include <sys/vmmeter.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <sys/sysctl.h>

#include <sys/buf2.h>
#include <vm/vm_page2.h>

#include <machine/limits.h>

/*
 * Cluster tracking cache - replaces the original vnode v_* fields which had
 * limited utility and were not MP safe.
 *
 * The cluster tracking cache is a simple 4-way set-associative non-chained
 * cache.  It is capable of tracking up to four zones separated by 1MB or
 * more per vnode.
 *
 * NOTE: We want this structure to be cache-line friendly so the iterator
 *	 is embedded rather than in a separate array.
 *
 * NOTE: A cluster cache entry can become stale when a vnode is recycled.
 *	 For now we treat the values as heuristical but also self-consistent.
 *	 i.e. the values cannot be completely random and cannot be SMP unsafe
 *	 or the cluster code might end-up clustering non-contiguous buffers
 *	 at the wrong offsets.
 */
struct cluster_cache {
	struct vnode *vp;
	u_int	locked;
	off_t	v_lastw;		/* last write (write cluster) */
	off_t	v_cstart;		/* start block of cluster */
	off_t	v_lasta;		/* last allocation */
	u_int	v_clen;			/* length of current cluster */
	u_int	iterator;
} __cachealign;

typedef struct cluster_cache cluster_cache_t;

#define CLUSTER_CACHE_SIZE	512
#define CLUSTER_CACHE_MASK	(CLUSTER_CACHE_SIZE - 1)

#define CLUSTER_ZONE		((off_t)(1024 * 1024))

cluster_cache_t cluster_array[CLUSTER_CACHE_SIZE];

#if defined(CLUSTERDEBUG)
#include <sys/sysctl.h>
static int	rcluster= 0;
SYSCTL_INT(_debug, OID_AUTO, rcluster, CTLFLAG_RW, &rcluster, 0, "");
#endif

static MALLOC_DEFINE(M_SEGMENT, "cluster_save", "cluster_save buffer");

static struct cluster_save *
	cluster_collectbufs (cluster_cache_t *cc, struct vnode *vp,
				struct buf *last_bp, int blksize);
static struct buf *
	cluster_rbuild (struct vnode *vp, off_t filesize, off_t loffset,
			    off_t doffset, int blksize, int run, 
			    struct buf *fbp);
static void cluster_callback (struct bio *);
static void cluster_setram (struct buf *);
static int cluster_wbuild(struct vnode *vp, struct buf **bpp, int blksize,
			    off_t start_loffset, int bytes);

static int write_behind = 1;
SYSCTL_INT(_vfs, OID_AUTO, write_behind, CTLFLAG_RW, &write_behind, 0,
    "Cluster write-behind setting");
static quad_t write_behind_minfilesize = 10 * 1024 * 1024;
SYSCTL_QUAD(_vfs, OID_AUTO, write_behind_minfilesize, CTLFLAG_RW,
    &write_behind_minfilesize, 0, "Cluster write-behind setting");
static int max_readahead = 2 * 1024 * 1024;
SYSCTL_INT(_vfs, OID_AUTO, max_readahead, CTLFLAG_RW, &max_readahead, 0,
    "Limit in bytes for desired cluster read-ahead");

extern vm_page_t	bogus_page;

extern int cluster_pbuf_freecnt;

/*
 * Acquire/release cluster cache (can return dummy entry)
 */
static
cluster_cache_t *
cluster_getcache(cluster_cache_t *dummy, struct vnode *vp, off_t loffset)
{
	cluster_cache_t *cc;
	size_t hv;
	int i;
	int xact;

	hv = (size_t)(intptr_t)vp ^ (size_t)(intptr_t)vp / sizeof(*vp);
	hv &= CLUSTER_CACHE_MASK & ~3;
	cc = &cluster_array[hv];

	xact = -1;
	for (i = 0; i < 4; ++i) {
		if (cc[i].vp != vp)
			continue;
		if (((cc[i].v_cstart ^ loffset) & ~(CLUSTER_ZONE - 1)) == 0) {
			xact = i;
			break;
		}
	}
	if (xact >= 0 && atomic_swap_int(&cc[xact].locked, 1) == 0) {
		if (cc[xact].vp == vp &&
		    ((cc[i].v_cstart ^ loffset) & ~(CLUSTER_ZONE - 1)) == 0) {
			return(&cc[xact]);
		}
		atomic_swap_int(&cc[xact].locked, 0);
	}

	/*
	 * New entry.  If we can't acquire the cache line then use the
	 * passed-in dummy element and reset all fields.
	 *
	 * When we are able to acquire the cache line we only clear the
	 * fields if the vp does not match.  This allows us to multi-zone
	 * a vp and for excessive zones / partial clusters to be retired.
	 */
	i = cc->iterator++ & 3;
	cc += i;
	if (atomic_swap_int(&cc->locked, 1) != 0) {
		cc = dummy;
		cc->locked = 1;
		cc->vp = NULL;
	}
	if (cc->vp != vp) {
		cc->vp = vp;
		cc->v_lasta = 0;
		cc->v_clen = 0;
		cc->v_cstart = 0;
		cc->v_lastw = 0;
	}
	return(cc);
}

static
void
cluster_putcache(cluster_cache_t *cc)
{
	atomic_swap_int(&cc->locked, 0);
}

/*
 * This replaces bread(), providing a synchronous read of the requested
 * buffer plus asynchronous read-ahead within the specified bounds.
 *
 * The caller may pre-populate *bpp if it already has the requested buffer
 * in-hand, else must set *bpp to NULL.  Note that the cluster_read() inline
 * sets *bpp to NULL and then calls cluster_readx() for compatibility.
 *
 * filesize	- read-ahead @ blksize will not cross this boundary
 * loffset	- loffset for returned *bpp
 * blksize	- blocksize for returned *bpp and read-ahead bps
 * minreq	- minimum (not a hard minimum) in bytes, typically reflects
 *		  a higher level uio resid.
 * maxreq	- maximum (sequential heuristic) in bytes (highet typ ~2MB)
 * bpp		- return buffer (*bpp) for (loffset,blksize)
 */
int
cluster_readx(struct vnode *vp, off_t filesize, off_t loffset,
	     int blksize, size_t minreq, size_t maxreq, struct buf **bpp)
{
	struct buf *bp, *rbp, *reqbp;
	off_t origoffset;
	off_t doffset;
	int error;
	int i;
	int maxra;
	int maxrbuild;

	error = 0;

	/*
	 * Calculate the desired read-ahead in blksize'd blocks (maxra).
	 * To do this we calculate maxreq.
	 *
	 * maxreq typically starts out as a sequential heuristic.  If the
	 * high level uio/resid is bigger (minreq), we pop maxreq up to
	 * minreq.  This represents the case where random I/O is being
	 * performed by the userland is issuing big read()'s.
	 *
	 * Then we limit maxreq to max_readahead to ensure it is a reasonable
	 * value.
	 *
	 * Finally we must ensure that (loffset + maxreq) does not cross the
	 * boundary (filesize) for the current blocksize.  If we allowed it
	 * to cross we could end up with buffers past the boundary with the
	 * wrong block size (HAMMER large-data areas use mixed block sizes).
	 * minreq is also absolutely limited to filesize.
	 */
	if (maxreq < minreq)
		maxreq = minreq;
	/* minreq not used beyond this point */

	if (maxreq > max_readahead) {
		maxreq = max_readahead;
		if (maxreq > 16 * 1024 * 1024)
			maxreq = 16 * 1024 * 1024;
	}
	if (maxreq < blksize)
		maxreq = blksize;
	if (loffset + maxreq > filesize) {
		if (loffset > filesize)
			maxreq = 0;
		else
			maxreq = filesize - loffset;
	}

	maxra = (int)(maxreq / blksize);

	/*
	 * Get the requested block.
	 */
	if (*bpp)
		reqbp = bp = *bpp;
	else
		*bpp = reqbp = bp = getblk(vp, loffset, blksize, 0, 0);
	origoffset = loffset;

	/*
	 * Calculate the maximum cluster size for a single I/O, used
	 * by cluster_rbuild().
	 */
	maxrbuild = vmaxiosize(vp) / blksize;

	/*
	 * if it is in the cache, then check to see if the reads have been
	 * sequential.  If they have, then try some read-ahead, otherwise
	 * back-off on prospective read-aheads.
	 */
	if (bp->b_flags & B_CACHE) {
		/*
		 * Not sequential, do not do any read-ahead
		 */
		if (maxra <= 1)
			return 0;

		/*
		 * No read-ahead mark, do not do any read-ahead
		 * yet.
		 */
		if ((bp->b_flags & B_RAM) == 0)
			return 0;

		/*
		 * We hit a read-ahead-mark, figure out how much read-ahead
		 * to do (maxra) and where to start (loffset).
		 *
		 * Shortcut the scan.  Typically the way this works is that
		 * we've built up all the blocks inbetween except for the
		 * last in previous iterations, so if the second-to-last
		 * block is present we just skip ahead to it.
		 *
		 * This algorithm has O(1) cpu in the steady state no
		 * matter how large maxra is.
		 */
		bp->b_flags &= ~B_RAM;

		if (findblk(vp, loffset + (maxra - 2) * blksize, FINDBLK_TEST))
			i = maxra - 1;
		else
			i = 1;
		while (i < maxra) {
			if (findblk(vp, loffset + i * blksize,
				    FINDBLK_TEST) == NULL) {
				break;
			}
			++i;
		}

		/*
		 * We got everything or everything is in the cache, no
		 * point continuing.
		 */
		if (i >= maxra)
			return 0;

		/*
		 * Calculate where to start the read-ahead and how much
		 * to do.  Generally speaking we want to read-ahead by
		 * (maxra) when we've found a read-ahead mark.  We do
		 * not want to reduce maxra here as it will cause
		 * successive read-ahead I/O's to be smaller and smaller.
		 *
		 * However, we have to make sure we don't break the
		 * filesize limitation for the clustered operation.
		 */
		loffset += i * blksize;
		reqbp = bp = NULL;

		if (loffset >= filesize)
			return 0;
		if (loffset + maxra * blksize > filesize) {
			maxreq = filesize - loffset;
			maxra = (int)(maxreq / blksize);
		}
	} else {
		__debugvar off_t firstread = bp->b_loffset;
		int nblks;

		/*
		 * Set-up synchronous read for bp.
		 */
		bp->b_cmd = BUF_CMD_READ;
		bp->b_bio1.bio_done = biodone_sync;
		bp->b_bio1.bio_flags |= BIO_SYNC;

		KASSERT(firstread != NOOFFSET, 
			("cluster_read: no buffer offset"));

		/*
		 * nblks is our cluster_rbuild request size, limited
		 * primarily by the device.
		 */
		if ((nblks = maxra) > maxrbuild)
			nblks = maxrbuild;

		if (nblks > 1) {
			int burstbytes;

	    		error = VOP_BMAP(vp, loffset, &doffset,
					 &burstbytes, NULL, BUF_CMD_READ);
			if (error)
				goto single_block_read;
			if (nblks > burstbytes / blksize)
				nblks = burstbytes / blksize;
			if (doffset == NOOFFSET)
				goto single_block_read;
			if (nblks <= 1)
				goto single_block_read;

			bp = cluster_rbuild(vp, filesize, loffset,
					    doffset, blksize, nblks, bp);
			loffset += bp->b_bufsize;
			maxra -= bp->b_bufsize / blksize;
		} else {
single_block_read:
			/*
			 * If it isn't in the cache, then get a chunk from
			 * disk if sequential, otherwise just get the block.
			 */
			cluster_setram(bp);
			loffset += blksize;
			--maxra;
		}
	}

	/*
	 * If B_CACHE was not set issue bp.  bp will either be an
	 * asynchronous cluster buf or a synchronous single-buf.
	 * If it is a single buf it will be the same as reqbp.
	 *
	 * NOTE: Once an async cluster buf is issued bp becomes invalid.
	 */
	if (bp) {
#if defined(CLUSTERDEBUG)
		if (rcluster)
			kprintf("S(%012jx,%d,%d)\n",
			    (intmax_t)bp->b_loffset, bp->b_bcount, maxra);
#endif
		if ((bp->b_flags & B_CLUSTER) == 0)
			vfs_busy_pages(vp, bp);
		bp->b_flags &= ~(B_ERROR|B_INVAL);
		vn_strategy(vp, &bp->b_bio1);
		error = 0;
		/* bp invalid now */
		bp = NULL;
	}

	/*
	 * If we have been doing sequential I/O, then do some read-ahead.
	 * The code above us should have positioned us at the next likely
	 * offset.
	 *
	 * Only mess with buffers which we can immediately lock.  HAMMER
	 * will do device-readahead irrespective of what the blocks
	 * represent.
	 */
	while (error == 0 && maxra > 0) {
		int burstbytes;
		int tmp_error;
		int nblks;

		rbp = getblk(vp, loffset, blksize,
			     GETBLK_SZMATCH|GETBLK_NOWAIT, 0);
		if (rbp == NULL)
			goto no_read_ahead;
		if ((rbp->b_flags & B_CACHE)) {
			bqrelse(rbp);
			goto no_read_ahead;
		}

		/*
		 * An error from the read-ahead bmap has nothing to do
		 * with the caller's original request.
		 */
		tmp_error = VOP_BMAP(vp, loffset, &doffset,
				     &burstbytes, NULL, BUF_CMD_READ);
		if (tmp_error || doffset == NOOFFSET) {
			rbp->b_flags |= B_INVAL;
			brelse(rbp);
			rbp = NULL;
			goto no_read_ahead;
		}
		if ((nblks = maxra) > maxrbuild)
			nblks = maxrbuild;
		if (nblks > burstbytes / blksize)
			nblks = burstbytes / blksize;

		/*
		 * rbp: async read
		 */
		rbp->b_cmd = BUF_CMD_READ;
		/*rbp->b_flags |= B_AGE*/;
		cluster_setram(rbp);

		if (nblks > 1) {
			rbp = cluster_rbuild(vp, filesize, loffset,
					     doffset, blksize, 
					     nblks, rbp);
		} else {
			rbp->b_bio2.bio_offset = doffset;
		}

		rbp->b_flags &= ~(B_ERROR|B_INVAL);

		if ((rbp->b_flags & B_CLUSTER) == 0)
			vfs_busy_pages(vp, rbp);
		BUF_KERNPROC(rbp);
		loffset += rbp->b_bufsize;
		maxra -= rbp->b_bufsize / blksize;
		vn_strategy(vp, &rbp->b_bio1);
		/* rbp invalid now */
	}

	/*
	 * Wait for our original buffer to complete its I/O.  reqbp will
	 * be NULL if the original buffer was B_CACHE.  We are returning
	 * (*bpp) which is the same as reqbp when reqbp != NULL.
	 */
no_read_ahead:
	if (reqbp) {
		KKASSERT(reqbp->b_bio1.bio_flags & BIO_SYNC);
		error = biowait(&reqbp->b_bio1, "clurd");
	}
	return (error);
}

/*
 * This replaces breadcb(), providing an asynchronous read of the requested
 * buffer with a callback, plus an asynchronous read-ahead within the
 * specified bounds.
 *
 * The callback must check whether BIO_DONE is set in the bio and issue
 * the bpdone(bp, 0) if it isn't.  The callback is responsible for clearing
 * BIO_DONE and disposing of the I/O (bqrelse()ing it).
 *
 * filesize	- read-ahead @ blksize will not cross this boundary
 * loffset	- loffset for returned *bpp
 * blksize	- blocksize for returned *bpp and read-ahead bps
 * minreq	- minimum (not a hard minimum) in bytes, typically reflects
 *		  a higher level uio resid.
 * maxreq	- maximum (sequential heuristic) in bytes (highet typ ~2MB)
 * bpp		- return buffer (*bpp) for (loffset,blksize)
 */
void
cluster_readcb(struct vnode *vp, off_t filesize, off_t loffset,
	     int blksize, size_t minreq, size_t maxreq,
	     void (*func)(struct bio *), void *arg)
{
	struct buf *bp, *rbp, *reqbp;
	off_t origoffset;
	off_t doffset;
	int i;
	int maxra;
	int maxrbuild;

	/*
	 * Calculate the desired read-ahead in blksize'd blocks (maxra).
	 * To do this we calculate maxreq.
	 *
	 * maxreq typically starts out as a sequential heuristic.  If the
	 * high level uio/resid is bigger (minreq), we pop maxreq up to
	 * minreq.  This represents the case where random I/O is being
	 * performed by the userland is issuing big read()'s.
	 *
	 * Then we limit maxreq to max_readahead to ensure it is a reasonable
	 * value.
	 *
	 * Finally we must ensure that (loffset + maxreq) does not cross the
	 * boundary (filesize) for the current blocksize.  If we allowed it
	 * to cross we could end up with buffers past the boundary with the
	 * wrong block size (HAMMER large-data areas use mixed block sizes).
	 * minreq is also absolutely limited to filesize.
	 */
	if (maxreq < minreq)
		maxreq = minreq;
	/* minreq not used beyond this point */

	if (maxreq > max_readahead) {
		maxreq = max_readahead;
		if (maxreq > 16 * 1024 * 1024)
			maxreq = 16 * 1024 * 1024;
	}
	if (maxreq < blksize)
		maxreq = blksize;
	if (loffset + maxreq > filesize) {
		if (loffset > filesize)
			maxreq = 0;
		else
			maxreq = filesize - loffset;
	}

	maxra = (int)(maxreq / blksize);

	/*
	 * Get the requested block.
	 */
	reqbp = bp = getblk(vp, loffset, blksize, 0, 0);
	origoffset = loffset;

	/*
	 * Calculate the maximum cluster size for a single I/O, used
	 * by cluster_rbuild().
	 */
	maxrbuild = vmaxiosize(vp) / blksize;

	/*
	 * if it is in the cache, then check to see if the reads have been
	 * sequential.  If they have, then try some read-ahead, otherwise
	 * back-off on prospective read-aheads.
	 */
	if (bp->b_flags & B_CACHE) {
		/*
		 * Setup for func() call whether we do read-ahead or not.
		 */
		bp->b_bio1.bio_caller_info1.ptr = arg;
		bp->b_bio1.bio_flags |= BIO_DONE;

		/*
		 * Not sequential, do not do any read-ahead
		 */
		if (maxra <= 1)
			goto no_read_ahead;

		/*
		 * No read-ahead mark, do not do any read-ahead
		 * yet.
		 */
		if ((bp->b_flags & B_RAM) == 0)
			goto no_read_ahead;
		bp->b_flags &= ~B_RAM;

		/*
		 * We hit a read-ahead-mark, figure out how much read-ahead
		 * to do (maxra) and where to start (loffset).
		 *
		 * Shortcut the scan.  Typically the way this works is that
		 * we've built up all the blocks inbetween except for the
		 * last in previous iterations, so if the second-to-last
		 * block is present we just skip ahead to it.
		 *
		 * This algorithm has O(1) cpu in the steady state no
		 * matter how large maxra is.
		 */
		if (findblk(vp, loffset + (maxra - 2) * blksize, FINDBLK_TEST))
			i = maxra - 1;
		else
			i = 1;
		while (i < maxra) {
			if (findblk(vp, loffset + i * blksize,
				    FINDBLK_TEST) == NULL) {
				break;
			}
			++i;
		}

		/*
		 * We got everything or everything is in the cache, no
		 * point continuing.
		 */
		if (i >= maxra)
			goto no_read_ahead;

		/*
		 * Calculate where to start the read-ahead and how much
		 * to do.  Generally speaking we want to read-ahead by
		 * (maxra) when we've found a read-ahead mark.  We do
		 * not want to reduce maxra here as it will cause
		 * successive read-ahead I/O's to be smaller and smaller.
		 *
		 * However, we have to make sure we don't break the
		 * filesize limitation for the clustered operation.
		 */
		loffset += i * blksize;
		bp = NULL;
		/* leave reqbp intact to force function callback */

		if (loffset >= filesize)
			goto no_read_ahead;
		if (loffset + maxra * blksize > filesize) {
			maxreq = filesize - loffset;
			maxra = (int)(maxreq / blksize);
		}
	} else {
		__debugvar off_t firstread = bp->b_loffset;
		int nblks;
		int tmp_error;

		/*
		 * Set-up synchronous read for bp.
		 */
		bp->b_flags &= ~(B_ERROR | B_EINTR | B_INVAL);
		bp->b_cmd = BUF_CMD_READ;
		bp->b_bio1.bio_done = func;
		bp->b_bio1.bio_caller_info1.ptr = arg;
		BUF_KERNPROC(bp);
		reqbp = NULL;	/* don't func() reqbp, it's running async */

		KASSERT(firstread != NOOFFSET,
			("cluster_read: no buffer offset"));

		/*
		 * nblks is our cluster_rbuild request size, limited
		 * primarily by the device.
		 */
		if ((nblks = maxra) > maxrbuild)
			nblks = maxrbuild;

		if (nblks > 1) {
			int burstbytes;

			tmp_error = VOP_BMAP(vp, loffset, &doffset,
					     &burstbytes, NULL, BUF_CMD_READ);
			if (tmp_error)
				goto single_block_read;
			if (nblks > burstbytes / blksize)
				nblks = burstbytes / blksize;
			if (doffset == NOOFFSET)
				goto single_block_read;
			if (nblks <= 1)
				goto single_block_read;

			bp = cluster_rbuild(vp, filesize, loffset,
					    doffset, blksize, nblks, bp);
			loffset += bp->b_bufsize;
			maxra -= bp->b_bufsize / blksize;
		} else {
single_block_read:
			/*
			 * If it isn't in the cache, then get a chunk from
			 * disk if sequential, otherwise just get the block.
			 */
			cluster_setram(bp);
			loffset += blksize;
			--maxra;
		}
	}

	/*
	 * If bp != NULL then B_CACHE was *NOT* set and bp must be issued.
	 * bp will either be an asynchronous cluster buf or an asynchronous
	 * single-buf.
	 *
	 * NOTE: Once an async cluster buf is issued bp becomes invalid.
	 */
	if (bp) {
#if defined(CLUSTERDEBUG)
		if (rcluster)
			kprintf("S(%012jx,%d,%d)\n",
			    (intmax_t)bp->b_loffset, bp->b_bcount, maxra);
#endif
		if ((bp->b_flags & B_CLUSTER) == 0)
			vfs_busy_pages(vp, bp);
		bp->b_flags &= ~(B_ERROR|B_INVAL);
		vn_strategy(vp, &bp->b_bio1);
		/* bp invalid now */
		bp = NULL;
	}

	/*
	 * If we have been doing sequential I/O, then do some read-ahead.
	 * The code above us should have positioned us at the next likely
	 * offset.
	 *
	 * Only mess with buffers which we can immediately lock.  HAMMER
	 * will do device-readahead irrespective of what the blocks
	 * represent.
	 */
	while (maxra > 0) {
		int burstbytes;
		int tmp_error;
		int nblks;

		rbp = getblk(vp, loffset, blksize,
			     GETBLK_SZMATCH|GETBLK_NOWAIT, 0);
		if (rbp == NULL)
			goto no_read_ahead;
		if ((rbp->b_flags & B_CACHE)) {
			bqrelse(rbp);
			goto no_read_ahead;
		}

		/*
		 * An error from the read-ahead bmap has nothing to do
		 * with the caller's original request.
		 */
		tmp_error = VOP_BMAP(vp, loffset, &doffset,
				     &burstbytes, NULL, BUF_CMD_READ);
		if (tmp_error || doffset == NOOFFSET) {
			rbp->b_flags |= B_INVAL;
			brelse(rbp);
			rbp = NULL;
			goto no_read_ahead;
		}
		if ((nblks = maxra) > maxrbuild)
			nblks = maxrbuild;
		if (nblks > burstbytes / blksize)
			nblks = burstbytes / blksize;

		/*
		 * rbp: async read
		 */
		rbp->b_cmd = BUF_CMD_READ;
		/*rbp->b_flags |= B_AGE*/;
		cluster_setram(rbp);

		if (nblks > 1) {
			rbp = cluster_rbuild(vp, filesize, loffset,
					     doffset, blksize,
					     nblks, rbp);
		} else {
			rbp->b_bio2.bio_offset = doffset;
		}

		rbp->b_flags &= ~(B_ERROR|B_INVAL);

		if ((rbp->b_flags & B_CLUSTER) == 0)
			vfs_busy_pages(vp, rbp);
		BUF_KERNPROC(rbp);
		loffset += rbp->b_bufsize;
		maxra -= rbp->b_bufsize / blksize;
		vn_strategy(vp, &rbp->b_bio1);
		/* rbp invalid now */
	}

	/*
	 * If reqbp is non-NULL it had B_CACHE set and we issue the
	 * function callback synchronously.
	 *
	 * Note that we may start additional asynchronous I/O before doing
	 * the func() callback for the B_CACHE case
	 */
no_read_ahead:
	if (reqbp)
		func(&reqbp->b_bio1);
}

/*
 * If blocks are contiguous on disk, use this to provide clustered
 * read ahead.  We will read as many blocks as possible sequentially
 * and then parcel them up into logical blocks in the buffer hash table.
 *
 * This function either returns a cluster buf or it returns fbp.  fbp is
 * already expected to be set up as a synchronous or asynchronous request.
 *
 * If a cluster buf is returned it will always be async.
 */
static struct buf *
cluster_rbuild(struct vnode *vp, off_t filesize, off_t loffset, off_t doffset,
	       int blksize, int run, struct buf *fbp)
{
	struct buf *bp, *tbp;
	off_t boffset;
	int i, j;
	int maxiosize = vmaxiosize(vp);

	/*
	 * avoid a division
	 */
	while (loffset + run * blksize > filesize) {
		--run;
	}

	tbp = fbp;
	tbp->b_bio2.bio_offset = doffset;
	if((tbp->b_flags & B_MALLOC) ||
	    ((tbp->b_flags & B_VMIO) == 0) || (run <= 1)) {
		return tbp;
	}

	bp = trypbuf_kva(&cluster_pbuf_freecnt);
	if (bp == NULL) {
		return tbp;
	}

	/*
	 * We are synthesizing a buffer out of vm_page_t's, but
	 * if the block size is not page aligned then the starting
	 * address may not be either.  Inherit the b_data offset
	 * from the original buffer.
	 */
	bp->b_data = (char *)((vm_offset_t)bp->b_data |
	    ((vm_offset_t)tbp->b_data & PAGE_MASK));
	bp->b_flags |= B_CLUSTER | B_VMIO;
	bp->b_cmd = BUF_CMD_READ;
	bp->b_bio1.bio_done = cluster_callback;		/* default to async */
	bp->b_bio1.bio_caller_info1.cluster_head = NULL;
	bp->b_bio1.bio_caller_info2.cluster_tail = NULL;
	bp->b_loffset = loffset;
	bp->b_bio2.bio_offset = doffset;
	KASSERT(bp->b_loffset != NOOFFSET,
		("cluster_rbuild: no buffer offset"));

	bp->b_bcount = 0;
	bp->b_bufsize = 0;
	bp->b_xio.xio_npages = 0;

	for (boffset = doffset, i = 0; i < run; ++i, boffset += blksize) {
		if (i) {
			if ((bp->b_xio.xio_npages * PAGE_SIZE) +
			    round_page(blksize) > maxiosize) {
				break;
			}

			/*
			 * Shortcut some checks and try to avoid buffers that
			 * would block in the lock.  The same checks have to
			 * be made again after we officially get the buffer.
			 */
			tbp = getblk(vp, loffset + i * blksize, blksize,
				     GETBLK_SZMATCH|GETBLK_NOWAIT, 0);
			if (tbp == NULL)
				break;
			for (j = 0; j < tbp->b_xio.xio_npages; j++) {
				if (tbp->b_xio.xio_pages[j]->valid)
					break;
			}
			if (j != tbp->b_xio.xio_npages) {
				bqrelse(tbp);
				break;
			}

			/*
			 * Stop scanning if the buffer is fuly valid 
			 * (marked B_CACHE), or locked (may be doing a
			 * background write), or if the buffer is not
			 * VMIO backed.  The clustering code can only deal
			 * with VMIO-backed buffers.
			 */
			if ((tbp->b_flags & (B_CACHE|B_LOCKED)) ||
			    (tbp->b_flags & B_VMIO) == 0 ||
			    (LIST_FIRST(&tbp->b_dep) != NULL &&
			     buf_checkread(tbp))
			) {
				bqrelse(tbp);
				break;
			}

			/*
			 * The buffer must be completely invalid in order to
			 * take part in the cluster.  If it is partially valid
			 * then we stop.
			 */
			for (j = 0;j < tbp->b_xio.xio_npages; j++) {
				if (tbp->b_xio.xio_pages[j]->valid)
					break;
			}
			if (j != tbp->b_xio.xio_npages) {
				bqrelse(tbp);
				break;
			}

			/*
			 * Set a read-ahead mark as appropriate.  Always
			 * set the read-ahead mark at (run - 1).  It is
			 * unclear why we were also setting it at i == 1.
			 */
			if (/*i == 1 ||*/ i == (run - 1))
				cluster_setram(tbp);

			/*
			 * Depress the priority of buffers not explicitly
			 * requested.
			 */
			/* tbp->b_flags |= B_AGE; */

			/*
			 * Set the block number if it isn't set, otherwise
			 * if it is make sure it matches the block number we
			 * expect.
			 */
			if (tbp->b_bio2.bio_offset == NOOFFSET) {
				tbp->b_bio2.bio_offset = boffset;
			} else if (tbp->b_bio2.bio_offset != boffset) {
				brelse(tbp);
				break;
			}
		}

		/*
		 * The passed-in tbp (i == 0) will already be set up for
		 * async or sync operation.  All other tbp's acquire in
		 * our loop are set up for async operation.
		 */
		tbp->b_cmd = BUF_CMD_READ;
		BUF_KERNPROC(tbp);
		cluster_append(&bp->b_bio1, tbp);
		for (j = 0; j < tbp->b_xio.xio_npages; ++j) {
			vm_page_t m;

			m = tbp->b_xio.xio_pages[j];
			vm_page_busy_wait(m, FALSE, "clurpg");
			vm_page_io_start(m);
			vm_page_wakeup(m);
			vm_object_pip_add(m->object, 1);
			if ((bp->b_xio.xio_npages == 0) ||
				(bp->b_xio.xio_pages[bp->b_xio.xio_npages-1] != m)) {
				bp->b_xio.xio_pages[bp->b_xio.xio_npages] = m;
				bp->b_xio.xio_npages++;
			}
			if ((m->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL)
				tbp->b_xio.xio_pages[j] = bogus_page;
		}
		/*
		 * XXX shouldn't this be += size for both, like in 
		 * cluster_wbuild()?
		 *
		 * Don't inherit tbp->b_bufsize as it may be larger due to
		 * a non-page-aligned size.  Instead just aggregate using
		 * 'size'.
		 */
		if (tbp->b_bcount != blksize)
		    kprintf("warning: tbp->b_bcount wrong %d vs %d\n", tbp->b_bcount, blksize);
		if (tbp->b_bufsize != blksize)
		    kprintf("warning: tbp->b_bufsize wrong %d vs %d\n", tbp->b_bufsize, blksize);
		bp->b_bcount += blksize;
		bp->b_bufsize += blksize;
	}

	/*
	 * Fully valid pages in the cluster are already good and do not need
	 * to be re-read from disk.  Replace the page with bogus_page
	 */
	for (j = 0; j < bp->b_xio.xio_npages; j++) {
		if ((bp->b_xio.xio_pages[j]->valid & VM_PAGE_BITS_ALL) ==
		    VM_PAGE_BITS_ALL) {
			bp->b_xio.xio_pages[j] = bogus_page;
		}
	}
	if (bp->b_bufsize > bp->b_kvasize) {
		panic("cluster_rbuild: b_bufsize(%d) > b_kvasize(%d)",
		    bp->b_bufsize, bp->b_kvasize);
	}
	pmap_qenter(trunc_page((vm_offset_t) bp->b_data),
		(vm_page_t *)bp->b_xio.xio_pages, bp->b_xio.xio_npages);
	BUF_KERNPROC(bp);
	return (bp);
}

/*
 * Cleanup after a clustered read or write.
 * This is complicated by the fact that any of the buffers might have
 * extra memory (if there were no empty buffer headers at allocbuf time)
 * that we will need to shift around.
 *
 * The returned bio is &bp->b_bio1
 */
void
cluster_callback(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct buf *tbp;
	int error = 0;

	/*
	 * Must propogate errors to all the components.  A short read (EOF)
	 * is a critical error.
	 */
	if (bp->b_flags & B_ERROR) {
		error = bp->b_error;
	} else if (bp->b_bcount != bp->b_bufsize) {
		panic("cluster_callback: unexpected EOF on cluster %p!", bio);
	}

	pmap_qremove(trunc_page((vm_offset_t) bp->b_data), bp->b_xio.xio_npages);
	/*
	 * Move memory from the large cluster buffer into the component
	 * buffers and mark IO as done on these.  Since the memory map
	 * is the same, no actual copying is required.
	 */
	while ((tbp = bio->bio_caller_info1.cluster_head) != NULL) {
		bio->bio_caller_info1.cluster_head = tbp->b_cluster_next;
		if (error) {
			tbp->b_flags |= B_ERROR | B_IODEBUG;
			tbp->b_error = error;
		} else {
			tbp->b_dirtyoff = tbp->b_dirtyend = 0;
			tbp->b_flags &= ~(B_ERROR|B_INVAL);
			tbp->b_flags |= B_IODEBUG;
			/*
			 * XXX the bdwrite()/bqrelse() issued during
			 * cluster building clears B_RELBUF (see bqrelse()
			 * comment).  If direct I/O was specified, we have
			 * to restore it here to allow the buffer and VM
			 * to be freed.
			 */
			if (tbp->b_flags & B_DIRECT)
				tbp->b_flags |= B_RELBUF;
		}
		biodone(&tbp->b_bio1);
	}
	relpbuf(bp, &cluster_pbuf_freecnt);
}

/*
 * Implement modified write build for cluster.
 *
 * 	write_behind = 0	write behind disabled
 *	write_behind = 1	write behind normal (default)
 *	write_behind = 2	write behind backed-off
 *
 * In addition, write_behind is only activated for files that have
 * grown past a certain size (default 10MB).  Otherwise temporary files
 * wind up generating a lot of unnecessary disk I/O.
 */
static __inline int
cluster_wbuild_wb(struct vnode *vp, int blksize, off_t start_loffset, int len)
{
	int r = 0;

	switch(write_behind) {
	case 2:
		if (start_loffset < len)
			break;
		start_loffset -= len;
		/* fall through */
	case 1:
		if (vp->v_filesize >= write_behind_minfilesize) {
			r = cluster_wbuild(vp, NULL, blksize,
					   start_loffset, len);
		}
		/* fall through */
	default:
		/* fall through */
		break;
	}
	return(r);
}

/*
 * Do clustered write for FFS.
 *
 * Three cases:
 *	1. Write is not sequential (write asynchronously)
 *	Write is sequential:
 *	2.	beginning of cluster - begin cluster
 *	3.	middle of a cluster - add to cluster
 *	4.	end of a cluster - asynchronously write cluster
 *
 * WARNING! vnode fields are not locked and must ONLY be used heuristically.
 */
void
cluster_write(struct buf *bp, off_t filesize, int blksize, int seqcount)
{
	struct vnode *vp;
	off_t loffset;
	int maxclen, cursize;
	int async;
	cluster_cache_t dummy;
	cluster_cache_t *cc;

	vp = bp->b_vp;
	if (vp->v_type == VREG)
		async = vp->v_mount->mnt_flag & MNT_ASYNC;
	else
		async = 0;
	loffset = bp->b_loffset;
	KASSERT(bp->b_loffset != NOOFFSET, 
		("cluster_write: no buffer offset"));

	cc = cluster_getcache(&dummy, vp, loffset);

	/*
	 * Initialize vnode to beginning of file.
	 */
	if (loffset == 0)
		cc->v_lasta = cc->v_clen = cc->v_cstart = cc->v_lastw = 0;

	if (cc->v_clen == 0 || loffset != cc->v_lastw + blksize ||
	    bp->b_bio2.bio_offset == NOOFFSET ||
	    (bp->b_bio2.bio_offset != cc->v_lasta + blksize)) {
		maxclen = vmaxiosize(vp);
		if (cc->v_clen != 0) {
			/*
			 * Next block is not sequential.
			 *
			 * If we are not writing at end of file, the process
			 * seeked to another point in the file since its last
			 * write, or we have reached our maximum cluster size,
			 * then push the previous cluster. Otherwise try
			 * reallocating to make it sequential.
			 *
			 * Change to algorithm: only push previous cluster if
			 * it was sequential from the point of view of the
			 * seqcount heuristic, otherwise leave the buffer 
			 * intact so we can potentially optimize the I/O
			 * later on in the buf_daemon or update daemon
			 * flush.
			 */
			cursize = cc->v_lastw - cc->v_cstart + blksize;
			if (bp->b_loffset + blksize < filesize ||
			    loffset != cc->v_lastw + blksize ||
			    cc->v_clen <= cursize) {
				if (!async && seqcount > 0) {
					cluster_wbuild_wb(vp, blksize,
						cc->v_cstart, cursize);
				}
			} else {
				struct buf **bpp, **endbp;
				struct cluster_save *buflist;

				buflist = cluster_collectbufs(cc, vp,
							      bp, blksize);
				endbp = &buflist->bs_children
				    [buflist->bs_nchildren - 1];
				if (VOP_REALLOCBLKS(vp, buflist)) {
					/*
					 * Failed, push the previous cluster
					 * if *really* writing sequentially
					 * in the logical file (seqcount > 1),
					 * otherwise delay it in the hopes that
					 * the low level disk driver can
					 * optimize the write ordering.
					 *
					 * NOTE: We do not brelse the last
					 *	 element which is bp, and we
					 *	 do not return here.
					 */
					for (bpp = buflist->bs_children;
					     bpp < endbp; bpp++)
						brelse(*bpp);
					kfree(buflist, M_SEGMENT);
					if (seqcount > 1) {
						cluster_wbuild_wb(vp, 
						    blksize, cc->v_cstart,
						    cursize);
					}
				} else {
					/*
					 * Succeeded, keep building cluster.
					 */
					for (bpp = buflist->bs_children;
					     bpp <= endbp; bpp++)
						bdwrite(*bpp);
					kfree(buflist, M_SEGMENT);
					cc->v_lastw = loffset;
					cc->v_lasta = bp->b_bio2.bio_offset;
					cluster_putcache(cc);
					return;
				}
			}
		}
		/*
		 * Consider beginning a cluster. If at end of file, make
		 * cluster as large as possible, otherwise find size of
		 * existing cluster.
		 */
		if ((vp->v_type == VREG) &&
		    bp->b_loffset + blksize < filesize &&
		    (bp->b_bio2.bio_offset == NOOFFSET) &&
		    (VOP_BMAP(vp, loffset, &bp->b_bio2.bio_offset, &maxclen, NULL, BUF_CMD_WRITE) ||
		     bp->b_bio2.bio_offset == NOOFFSET)) {
			bdwrite(bp);
			cc->v_clen = 0;
			cc->v_lasta = bp->b_bio2.bio_offset;
			cc->v_cstart = loffset + blksize;
			cc->v_lastw = loffset;
			cluster_putcache(cc);
			return;
		}
		if (maxclen > blksize)
			cc->v_clen = maxclen - blksize;
		else
			cc->v_clen = 0;
		if (!async && cc->v_clen == 0) { /* I/O not contiguous */
			cc->v_cstart = loffset + blksize;
			bdwrite(bp);
		} else {	/* Wait for rest of cluster */
			cc->v_cstart = loffset;
			bdwrite(bp);
		}
	} else if (loffset == cc->v_cstart + cc->v_clen) {
		/*
		 * At end of cluster, write it out if seqcount tells us we
		 * are operating sequentially, otherwise let the buf or
		 * update daemon handle it.
		 */
		bdwrite(bp);
		if (seqcount > 1)
			cluster_wbuild_wb(vp, blksize, cc->v_cstart,
					  cc->v_clen + blksize);
		cc->v_clen = 0;
		cc->v_cstart = loffset + blksize;
	} else if (vm_page_count_severe() &&
		   bp->b_loffset + blksize < filesize) {
		/*
		 * We are low on memory, get it going NOW.  However, do not
		 * try to push out a partial block at the end of the file
		 * as this could lead to extremely non-optimal write activity.
		 */
		bawrite(bp);
	} else {
		/*
		 * In the middle of a cluster, so just delay the I/O for now.
		 */
		bdwrite(bp);
	}
	cc->v_lastw = loffset;
	cc->v_lasta = bp->b_bio2.bio_offset;
	cluster_putcache(cc);
}

/*
 * This is the clustered version of bawrite().  It works similarly to
 * cluster_write() except I/O on the buffer is guaranteed to occur.
 */
int
cluster_awrite(struct buf *bp)
{
	int total;

	/*
	 * Don't bother if it isn't clusterable.
	 */
	if ((bp->b_flags & B_CLUSTEROK) == 0 ||
	    bp->b_vp == NULL ||
	    (bp->b_vp->v_flag & VOBJBUF) == 0) {
		total = bp->b_bufsize;
		bawrite(bp);
		return (total);
	}

	total = cluster_wbuild(bp->b_vp, &bp, bp->b_bufsize,
			       bp->b_loffset, vmaxiosize(bp->b_vp));
	if (bp)
		bawrite(bp);

	return total;
}

/*
 * This is an awful lot like cluster_rbuild...wish they could be combined.
 * The last lbn argument is the current block on which I/O is being
 * performed.  Check to see that it doesn't fall in the middle of
 * the current block (if last_bp == NULL).
 *
 * cluster_wbuild() normally does not guarantee anything.  If bpp is
 * non-NULL and cluster_wbuild() is able to incorporate it into the
 * I/O it will set *bpp to NULL, otherwise it will leave it alone and
 * the caller must dispose of *bpp.
 */
static int
cluster_wbuild(struct vnode *vp, struct buf **bpp,
	       int blksize, off_t start_loffset, int bytes)
{
	struct buf *bp, *tbp;
	int i, j;
	int totalwritten = 0;
	int must_initiate;
	int maxiosize = vmaxiosize(vp);

	while (bytes > 0) {
		/*
		 * If the buffer matches the passed locked & removed buffer
		 * we used the passed buffer (which might not be B_DELWRI).
		 *
		 * Otherwise locate the buffer and determine if it is
		 * compatible.
		 */
		if (bpp && (*bpp)->b_loffset == start_loffset) {
			tbp = *bpp;
			*bpp = NULL;
			bpp = NULL;
		} else {
			tbp = findblk(vp, start_loffset, FINDBLK_NBLOCK);
			if (tbp == NULL ||
			    (tbp->b_flags & (B_LOCKED | B_INVAL | B_DELWRI)) !=
			     B_DELWRI ||
			    (LIST_FIRST(&tbp->b_dep) && buf_checkwrite(tbp))) {
				if (tbp)
					BUF_UNLOCK(tbp);
				start_loffset += blksize;
				bytes -= blksize;
				continue;
			}
			bremfree(tbp);
		}
		KKASSERT(tbp->b_cmd == BUF_CMD_DONE);

		/*
		 * Extra memory in the buffer, punt on this buffer.
		 * XXX we could handle this in most cases, but we would
		 * have to push the extra memory down to after our max
		 * possible cluster size and then potentially pull it back
		 * up if the cluster was terminated prematurely--too much
		 * hassle.
		 */
		if (((tbp->b_flags & (B_CLUSTEROK|B_MALLOC)) != B_CLUSTEROK) ||
		    (tbp->b_bcount != tbp->b_bufsize) ||
		    (tbp->b_bcount != blksize) ||
		    (bytes == blksize) ||
		    ((bp = getpbuf_kva(&cluster_pbuf_freecnt)) == NULL)) {
			totalwritten += tbp->b_bufsize;
			bawrite(tbp);
			start_loffset += blksize;
			bytes -= blksize;
			continue;
		}

		/*
		 * Set up the pbuf.  Track our append point with b_bcount
		 * and b_bufsize.  b_bufsize is not used by the device but
		 * our caller uses it to loop clusters and we use it to
		 * detect a premature EOF on the block device.
		 */
		bp->b_bcount = 0;
		bp->b_bufsize = 0;
		bp->b_xio.xio_npages = 0;
		bp->b_loffset = tbp->b_loffset;
		bp->b_bio2.bio_offset = tbp->b_bio2.bio_offset;

		/*
		 * We are synthesizing a buffer out of vm_page_t's, but
		 * if the block size is not page aligned then the starting
		 * address may not be either.  Inherit the b_data offset
		 * from the original buffer.
		 */
		bp->b_data = (char *)((vm_offset_t)bp->b_data |
		    ((vm_offset_t)tbp->b_data & PAGE_MASK));
		bp->b_flags &= ~B_ERROR;
		bp->b_flags |= B_CLUSTER | B_BNOCLIP |
			(tbp->b_flags & (B_VMIO | B_NEEDCOMMIT));
		bp->b_bio1.bio_caller_info1.cluster_head = NULL;
		bp->b_bio1.bio_caller_info2.cluster_tail = NULL;

		/*
		 * From this location in the file, scan forward to see
		 * if there are buffers with adjacent data that need to
		 * be written as well.
		 *
		 * IO *must* be initiated on index 0 at this point
		 * (particularly when called from cluster_awrite()).
		 */
		for (i = 0; i < bytes; (i += blksize), (start_loffset += blksize)) {
			if (i == 0) {
				must_initiate = 1;
			} else {
				/*
				 * Not first buffer.
				 */
				must_initiate = 0;
				tbp = findblk(vp, start_loffset,
					      FINDBLK_NBLOCK);
				/*
				 * Buffer not found or could not be locked
				 * non-blocking.
				 */
				if (tbp == NULL)
					break;

				/*
				 * If it IS in core, but has different
				 * characteristics, then don't cluster
				 * with it.
				 */
				if ((tbp->b_flags & (B_VMIO | B_CLUSTEROK |
				     B_INVAL | B_DELWRI | B_NEEDCOMMIT))
				    != (B_DELWRI | B_CLUSTEROK |
				     (bp->b_flags & (B_VMIO | B_NEEDCOMMIT))) ||
				    (tbp->b_flags & B_LOCKED)
				) {
					BUF_UNLOCK(tbp);
					break;
				}

				/*
				 * Check that the combined cluster
				 * would make sense with regard to pages
				 * and would not be too large
				 *
				 * WARNING! buf_checkwrite() must be the last
				 *	    check made.  If it returns 0 then
				 *	    we must initiate the I/O.
				 */
				if ((tbp->b_bcount != blksize) ||
				  ((bp->b_bio2.bio_offset + i) !=
				    tbp->b_bio2.bio_offset) ||
				  ((tbp->b_xio.xio_npages + bp->b_xio.xio_npages) >
				    (maxiosize / PAGE_SIZE)) ||
				  (LIST_FIRST(&tbp->b_dep) &&
				   buf_checkwrite(tbp))
				) {
					BUF_UNLOCK(tbp);
					break;
				}
				if (LIST_FIRST(&tbp->b_dep))
					must_initiate = 1;
				/*
				 * Ok, it's passed all the tests,
				 * so remove it from the free list
				 * and mark it busy. We will use it.
				 */
				bremfree(tbp);
				KKASSERT(tbp->b_cmd == BUF_CMD_DONE);
			}

			/*
			 * If the IO is via the VM then we do some
			 * special VM hackery (yuck).  Since the buffer's
			 * block size may not be page-aligned it is possible
			 * for a page to be shared between two buffers.  We
			 * have to get rid of the duplication when building
			 * the cluster.
			 */
			if (tbp->b_flags & B_VMIO) {
				vm_page_t m;

				/*
				 * Try to avoid deadlocks with the VM system.
				 * However, we cannot abort the I/O if
				 * must_initiate is non-zero.
				 */
				if (must_initiate == 0) {
					for (j = 0;
					     j < tbp->b_xio.xio_npages;
					     ++j) {
						m = tbp->b_xio.xio_pages[j];
						if (m->flags & PG_BUSY) {
							bqrelse(tbp);
							goto finishcluster;
						}
					}
				}
					
				for (j = 0; j < tbp->b_xio.xio_npages; ++j) {
					m = tbp->b_xio.xio_pages[j];
					vm_page_busy_wait(m, FALSE, "clurpg");
					vm_page_io_start(m);
					vm_page_wakeup(m);
					vm_object_pip_add(m->object, 1);
					if ((bp->b_xio.xio_npages == 0) ||
					  (bp->b_xio.xio_pages[bp->b_xio.xio_npages - 1] != m)) {
						bp->b_xio.xio_pages[bp->b_xio.xio_npages] = m;
						bp->b_xio.xio_npages++;
					}
				}
			}
			bp->b_bcount += blksize;
			bp->b_bufsize += blksize;

			bundirty(tbp);
			tbp->b_flags &= ~B_ERROR;
			tbp->b_cmd = BUF_CMD_WRITE;
			BUF_KERNPROC(tbp);
			cluster_append(&bp->b_bio1, tbp);

			/*
			 * check for latent dependencies to be handled 
			 */
			if (LIST_FIRST(&tbp->b_dep) != NULL)
				buf_start(tbp);
		}
	finishcluster:
		pmap_qenter(trunc_page((vm_offset_t)bp->b_data),
			    (vm_page_t *)bp->b_xio.xio_pages,
			    bp->b_xio.xio_npages);
		if (bp->b_bufsize > bp->b_kvasize) {
			panic("cluster_wbuild: b_bufsize(%d) "
			      "> b_kvasize(%d)\n",
			      bp->b_bufsize, bp->b_kvasize);
		}
		totalwritten += bp->b_bufsize;
		bp->b_dirtyoff = 0;
		bp->b_dirtyend = bp->b_bufsize;
		bp->b_bio1.bio_done = cluster_callback;
		bp->b_cmd = BUF_CMD_WRITE;

		vfs_busy_pages(vp, bp);
		bsetrunningbufspace(bp, bp->b_bufsize);
		BUF_KERNPROC(bp);
		vn_strategy(vp, &bp->b_bio1);

		bytes -= i;
	}
	return totalwritten;
}

/*
 * Collect together all the buffers in a cluster, plus add one
 * additional buffer passed-in.
 *
 * Only pre-existing buffers whos block size matches blksize are collected.
 * (this is primarily because HAMMER1 uses varying block sizes and we don't
 * want to override its choices).
 *
 * This code will not try to collect buffers that it cannot lock, otherwise
 * it might deadlock against SMP-friendly filesystems.
 */
static struct cluster_save *
cluster_collectbufs(cluster_cache_t *cc, struct vnode *vp,
		    struct buf *last_bp, int blksize)
{
	struct cluster_save *buflist;
	struct buf *bp;
	off_t loffset;
	int i, len;
	int j;
	int k;

	len = (int)(cc->v_lastw - cc->v_cstart + blksize) / blksize;
	KKASSERT(len > 0);
	buflist = kmalloc(sizeof(struct buf *) * (len + 1) + sizeof(*buflist),
			 M_SEGMENT, M_WAITOK);
	buflist->bs_nchildren = 0;
	buflist->bs_children = (struct buf **) (buflist + 1);
	for (loffset = cc->v_cstart, i = 0, j = 0;
	     i < len;
	     (loffset += blksize), i++) {
		bp = getcacheblk(vp, loffset,
				 last_bp->b_bcount, GETBLK_SZMATCH |
						    GETBLK_NOWAIT);
		buflist->bs_children[i] = bp;
		if (bp == NULL) {
			j = i + 1;
		} else if (bp->b_bio2.bio_offset == NOOFFSET) {
			VOP_BMAP(bp->b_vp, bp->b_loffset,
				 &bp->b_bio2.bio_offset,
				 NULL, NULL, BUF_CMD_WRITE);
		}
	}

	/*
	 * Get rid of gaps
	 */
	for (k = 0; k < j; ++k) {
		if (buflist->bs_children[k]) {
			bqrelse(buflist->bs_children[k]);
			buflist->bs_children[k] = NULL;
		}
	}
	if (j != 0) {
		if (j != i) {
			bcopy(buflist->bs_children + j,
			      buflist->bs_children + 0,
			      sizeof(buflist->bs_children[0]) * (i - j));
		}
		i -= j;
	}
	buflist->bs_children[i] = bp = last_bp;
	if (bp->b_bio2.bio_offset == NOOFFSET) {
		VOP_BMAP(bp->b_vp, bp->b_loffset, &bp->b_bio2.bio_offset,
			 NULL, NULL, BUF_CMD_WRITE);
	}
	buflist->bs_nchildren = i + 1;
	return (buflist);
}

void
cluster_append(struct bio *bio, struct buf *tbp)
{
	tbp->b_cluster_next = NULL;
	if (bio->bio_caller_info1.cluster_head == NULL) {
		bio->bio_caller_info1.cluster_head = tbp;
		bio->bio_caller_info2.cluster_tail = tbp;
	} else {
		bio->bio_caller_info2.cluster_tail->b_cluster_next = tbp;
		bio->bio_caller_info2.cluster_tail = tbp;
	}
}

static
void
cluster_setram (struct buf *bp)
{
	bp->b_flags |= B_RAM;
	if (bp->b_xio.xio_npages)
		vm_page_flag_set(bp->b_xio.xio_pages[0], PG_RAM);
}
