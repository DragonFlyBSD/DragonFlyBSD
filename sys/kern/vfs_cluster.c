/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 * Modifications/enhancements:
 * 	Copyright (c) 1995 John S. Dyson.  All rights reserved.
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
 *	@(#)vfs_cluster.c	8.7 (Berkeley) 2/13/94
 * $FreeBSD: src/sys/kern/vfs_cluster.c,v 1.92.2.9 2001/11/18 07:10:59 dillon Exp $
 * $DragonFly: src/sys/kern/vfs_cluster.c,v 1.20 2006/03/26 07:56:54 swildner Exp $
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

#if defined(CLUSTERDEBUG)
#include <sys/sysctl.h>
static int	rcluster= 0;
SYSCTL_INT(_debug, OID_AUTO, rcluster, CTLFLAG_RW, &rcluster, 0, "");
#endif

static MALLOC_DEFINE(M_SEGMENT, "cluster_save", "cluster_save buffer");

static struct cluster_save *
	cluster_collectbufs (struct vnode *vp, struct buf *last_bp,
			    int lblocksize);
static struct buf *
	cluster_rbuild (struct vnode *vp, off_t filesize, off_t loffset,
			    off_t doffset, int size, int run, struct buf *fbp);
static void cluster_callback (struct bio *);


static int write_behind = 1;
SYSCTL_INT(_vfs, OID_AUTO, write_behind, CTLFLAG_RW, &write_behind, 0, "");

extern vm_page_t	bogus_page;

extern int cluster_pbuf_freecnt;

/*
 * Maximum number of blocks for read-ahead.
 */
#define MAXRA 32

/*
 * This replaces bread.
 */
int
cluster_read(struct vnode *vp, off_t filesize, off_t loffset, 
	     int size, int totread, int seqcount, struct buf **bpp)
{
	struct buf *bp, *rbp, *reqbp;
	off_t origoffset;
	off_t doffset;
	int error;
	int i;
	int maxra, racluster;

	error = 0;

	/*
	 * Try to limit the amount of read-ahead by a few
	 * ad-hoc parameters.  This needs work!!!
	 */
	racluster = vp->v_mount->mnt_iosize_max / size;
	maxra = 2 * racluster + (totread / size);
	if (maxra > MAXRA)
		maxra = MAXRA;
	if (maxra > nbuf/8)
		maxra = nbuf/8;

	/*
	 * get the requested block
	 */
	*bpp = reqbp = bp = getblk(vp, loffset, size, 0, 0);
	origoffset = loffset;

	/*
	 * if it is in the cache, then check to see if the reads have been
	 * sequential.  If they have, then try some read-ahead, otherwise
	 * back-off on prospective read-aheads.
	 */
	if (bp->b_flags & B_CACHE) {
		if (!seqcount) {
			return 0;
		} else if ((bp->b_flags & B_RAM) == 0) {
			return 0;
		} else {
			struct buf *tbp;
			bp->b_flags &= ~B_RAM;
			/*
			 * We do the crit here so that there is no window
			 * between the findblk and the b_usecount increment
			 * below.  We opt to keep the crit out of the loop
			 * for efficiency.
			 */
			crit_enter();
			for (i = 1; i < maxra; i++) {
				if (!(tbp = findblk(vp, loffset + i * size))) {
					break;
				}

				/*
				 * Set another read-ahead mark so we know 
				 * to check again.
				 */
				if (((i % racluster) == (racluster - 1)) ||
					(i == (maxra - 1)))
					tbp->b_flags |= B_RAM;
			}
			crit_exit();
			if (i >= maxra) {
				return 0;
			}
			loffset += i * size;
		}
		reqbp = bp = NULL;
	} else {
		off_t firstread = bp->b_loffset;
		int nblks;

		KASSERT(firstread != NOOFFSET, 
			("cluster_read: no buffer offset"));
		if (firstread + totread > filesize)
			totread = (int)(filesize - firstread);
		nblks = totread / size;
		if (nblks) {
			int burstbytes;

			if (nblks > racluster)
				nblks = racluster;

	    		error = VOP_BMAP(vp, loffset, NULL,
					 &doffset, &burstbytes, NULL);
			if (error)
				goto single_block_read;
			if (doffset == NOOFFSET)
				goto single_block_read;
			if (burstbytes < size * 2)
				goto single_block_read;
			if (nblks > burstbytes / size)
				nblks = burstbytes / size;

			bp = cluster_rbuild(vp, filesize, loffset,
					    doffset, size, nblks, bp);
			loffset += bp->b_bufsize;
		} else {
single_block_read:
			/*
			 * if it isn't in the cache, then get a chunk from
			 * disk if sequential, otherwise just get the block.
			 */
			bp->b_flags |= B_READ | B_RAM;
			loffset += size;
		}
	}

	/*
	 * If we have been doing sequential I/O, then do some read-ahead.
	 */
	rbp = NULL;
	if (seqcount &&
	    loffset < origoffset + seqcount * size &&
	    loffset + size <= filesize
	) {
		rbp = getblk(vp, loffset, size, 0, 0);
		if ((rbp->b_flags & B_CACHE) == 0) {
			int nblksread;
			int ntoread;
			int burstbytes;

			error = VOP_BMAP(vp, loffset, NULL,
					 &doffset, &burstbytes, NULL);
			if (error || doffset == NOOFFSET) {
				rbp->b_flags &= ~(B_ASYNC | B_READ);
				brelse(rbp);
				rbp = NULL;
				goto no_read_ahead;
			}
			ntoread = burstbytes / size;
			nblksread = (totread + size - 1) / size;
			if (seqcount < nblksread)
				seqcount = nblksread;
			if (seqcount < ntoread)
				ntoread = seqcount;

			rbp->b_flags |= B_READ | B_ASYNC | B_RAM;
			if (burstbytes) {
				rbp = cluster_rbuild(vp, filesize, loffset,
						     doffset, size, 
						     ntoread, rbp);
			} else {
				rbp->b_bio2.bio_offset = doffset;
			}
		}
	}
no_read_ahead:

	/*
	 * Handle the synchronous read
	 */
	if (bp) {
#if defined(CLUSTERDEBUG)
		if (rcluster)
			printf("S(%lld,%d,%d) ",
			    bp->b_loffset, bp->b_bcount, seqcount);
#endif
		if ((bp->b_flags & B_CLUSTER) == 0) {
			vfs_busy_pages(bp, 0);
		}
		bp->b_flags &= ~(B_ERROR|B_INVAL);
		if ((bp->b_flags & B_ASYNC) || bp->b_bio1.bio_done != NULL)
			BUF_KERNPROC(bp);
		vn_strategy(vp, &bp->b_bio1);
		error = bp->b_error;
	}

	/*
	 * And if we have read-aheads, do them too
	 */
	if (rbp) {
		if (error) {
			rbp->b_flags &= ~(B_ASYNC | B_READ);
			brelse(rbp);
		} else if (rbp->b_flags & B_CACHE) {
			rbp->b_flags &= ~(B_ASYNC | B_READ);
			bqrelse(rbp);
		} else {
#if defined(CLUSTERDEBUG)
			if (rcluster) {
				if (bp)
					printf("A+(%lld,%d,%lld,%d) ",
					    rbp->b_loffset, rbp->b_bcount,
					    rbp->b_loffset - origoffset,
					    seqcount);
				else
					printf("A(%lld,%d,%lld,%d) ",
					    rbp->b_loffset, rbp->b_bcount,
					    rbp->b_loffset - origoffset,
					    seqcount);
			}
#endif

			if ((rbp->b_flags & B_CLUSTER) == 0) {
				vfs_busy_pages(rbp, 0);
			}
			rbp->b_flags &= ~(B_ERROR|B_INVAL);
			if ((rbp->b_flags & B_ASYNC) || rbp->b_bio1.bio_done != NULL)
				BUF_KERNPROC(rbp);
			vn_strategy(vp, &rbp->b_bio1);
		}
	}
	if (reqbp)
		return (biowait(reqbp));
	else
		return (error);
}

/*
 * If blocks are contiguous on disk, use this to provide clustered
 * read ahead.  We will read as many blocks as possible sequentially
 * and then parcel them up into logical blocks in the buffer hash table.
 */
static struct buf *
cluster_rbuild(struct vnode *vp, off_t filesize, off_t loffset, 
	off_t doffset, int size, int run, struct buf *fbp)
{
	struct buf *bp, *tbp;
	off_t boffset;
	int i, j;

	KASSERT(size == vp->v_mount->mnt_stat.f_iosize,
	    ("cluster_rbuild: size %d != filesize %ld\n",
	    size, vp->v_mount->mnt_stat.f_iosize));

	/*
	 * avoid a division
	 */
	while (loffset + run * size > filesize) {
		--run;
	}

	tbp = fbp;
	tbp->b_flags |= B_READ; 
	tbp->b_bio2.bio_offset = doffset;
	if( (tbp->b_flags & B_MALLOC) ||
		((tbp->b_flags & B_VMIO) == 0) || (run <= 1) )
		return tbp;

	bp = trypbuf(&cluster_pbuf_freecnt);
	if (bp == 0)
		return tbp;

	/*
	 * We are synthesizing a buffer out of vm_page_t's, but
	 * if the block size is not page aligned then the starting
	 * address may not be either.  Inherit the b_data offset
	 * from the original buffer.
	 */
	bp->b_data = (char *)((vm_offset_t)bp->b_data |
	    ((vm_offset_t)tbp->b_data & PAGE_MASK));
	bp->b_flags = B_ASYNC | B_READ | B_CLUSTER | B_VMIO;
	bp->b_bio1.bio_done = cluster_callback;
	bp->b_bio1.bio_caller_info1.cluster_head = NULL;
	bp->b_bio1.bio_caller_info2.cluster_tail = NULL;
	bp->b_loffset = loffset;
	bp->b_bio2.bio_offset = NOOFFSET;
	KASSERT(bp->b_loffset != NOOFFSET,
		("cluster_rbuild: no buffer offset"));
	pbgetvp(vp, bp);

	bp->b_bcount = 0;
	bp->b_bufsize = 0;
	bp->b_xio.xio_npages = 0;

	for (boffset = doffset, i = 0; i < run; ++i, boffset += size) {
		if (i != 0) {
			if ((bp->b_xio.xio_npages * PAGE_SIZE) +
			    round_page(size) > vp->v_mount->mnt_iosize_max) {
				break;
			}

			/*
			 * Shortcut some checks and try to avoid buffers that
			 * would block in the lock.  The same checks have to
			 * be made again after we officially get the buffer.
			 */
			if ((tbp = findblk(vp, loffset + i * size)) != NULL) {
				if (BUF_LOCK(tbp, LK_EXCLUSIVE | LK_NOWAIT))
					break;
				BUF_UNLOCK(tbp);

				for (j = 0; j < tbp->b_xio.xio_npages; j++) {
					if (tbp->b_xio.xio_pages[j]->valid)
						break;
				}
				
				if (j != tbp->b_xio.xio_npages)
					break;
	
				if (tbp->b_bcount != size)
					break;
			}

			tbp = getblk(vp, loffset + i * size, size, 0, 0);

			/*
			 * Stop scanning if the buffer is fuly valid 
			 * (marked B_CACHE), or locked (may be doing a
			 * background write), or if the buffer is not
			 * VMIO backed.  The clustering code can only deal
			 * with VMIO-backed buffers.
			 */
			if ((tbp->b_flags & (B_CACHE|B_LOCKED)) ||
			    (tbp->b_flags & B_VMIO) == 0) {
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
			 * Set a read-ahead mark as appropriate
			 */
			if (i == 1 || i == (run - 1))
				tbp->b_flags |= B_RAM;

			/*
			 * Set the buffer up for an async read (XXX should
			 * we do this only if we do not wind up brelse()ing?).
			 * Set the block number if it isn't set, otherwise
			 * if it is make sure it matches the block number we
			 * expect.
			 */
			tbp->b_flags |= B_READ | B_ASYNC;
			if (tbp->b_bio2.bio_offset == NOOFFSET) {
				tbp->b_bio2.bio_offset = boffset;
			} else if (tbp->b_bio2.bio_offset != boffset) {
				brelse(tbp);
				break;
			}
		}
		/*
		 * XXX fbp from caller may not be B_ASYNC, but we are going
		 * to biodone() it in cluster_callback() anyway
		 */
		BUF_KERNPROC(tbp);
		cluster_append(&bp->b_bio1, tbp);
		for (j = 0; j < tbp->b_xio.xio_npages; ++j) {
			vm_page_t m;
			m = tbp->b_xio.xio_pages[j];
			vm_page_io_start(m);
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
		if (tbp->b_bcount != size)
		    printf("warning: tbp->b_bcount wrong %d vs %d\n", tbp->b_bcount, size);
		if (tbp->b_bufsize != size)
		    printf("warning: tbp->b_bufsize wrong %d vs %d\n", tbp->b_bufsize, size);
		bp->b_bcount += size;
		bp->b_bufsize += size;
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
	if (bp->b_bufsize > bp->b_kvasize)
		panic("cluster_rbuild: b_bufsize(%d) > b_kvasize(%d)",
		    bp->b_bufsize, bp->b_kvasize);
	bp->b_kvasize = bp->b_bufsize;

	pmap_qenter(trunc_page((vm_offset_t) bp->b_data),
		(vm_page_t *)bp->b_xio.xio_pages, bp->b_xio.xio_npages);
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
	 * Must propogate errors to all the components.
	 */
	if (bp->b_flags & B_ERROR)
		error = bp->b_error;

	pmap_qremove(trunc_page((vm_offset_t) bp->b_data), bp->b_xio.xio_npages);
	/*
	 * Move memory from the large cluster buffer into the component
	 * buffers and mark IO as done on these.  Since the memory map
	 * is the same, no actual copying is required.
	 */
	while ((tbp = bio->bio_caller_info1.cluster_head) != NULL) {
		bio->bio_caller_info1.cluster_head = tbp->b_cluster_next;
		if (error) {
			tbp->b_flags |= B_ERROR;
			tbp->b_error = error;
		} else {
			tbp->b_dirtyoff = tbp->b_dirtyend = 0;
			tbp->b_flags &= ~(B_ERROR|B_INVAL);
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
 *	cluster_wbuild_wb:
 *
 *	Implement modified write build for cluster.
 *
 *		write_behind = 0	write behind disabled
 *		write_behind = 1	write behind normal (default)
 *		write_behind = 2	write behind backed-off
 */

static __inline int
cluster_wbuild_wb(struct vnode *vp, int size, off_t start_loffset, int len)
{
	int r = 0;

	switch(write_behind) {
	case 2:
		if (start_loffset < len)
			break;
		start_loffset -= len;
		/* fall through */
	case 1:
		r = cluster_wbuild(vp, size, start_loffset, len);
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
 */
void
cluster_write(struct buf *bp, off_t filesize, int seqcount)
{
	struct vnode *vp;
	off_t loffset;
	int maxclen, cursize;
	int lblocksize;
	int async;

	vp = bp->b_vp;
	if (vp->v_type == VREG) {
		async = vp->v_mount->mnt_flag & MNT_ASYNC;
		lblocksize = vp->v_mount->mnt_stat.f_iosize;
	} else {
		async = 0;
		lblocksize = bp->b_bufsize;
	}
	loffset = bp->b_loffset;
	KASSERT(bp->b_loffset != NOOFFSET, 
		("cluster_write: no buffer offset"));

	/* Initialize vnode to beginning of file. */
	if (loffset == 0)
		vp->v_lasta = vp->v_clen = vp->v_cstart = vp->v_lastw = 0;

	if (vp->v_clen == 0 || loffset != vp->v_lastw + lblocksize ||
	    bp->b_bio2.bio_offset == NOOFFSET ||
	    (bp->b_bio2.bio_offset != vp->v_lasta + lblocksize)) {
		maxclen = vp->v_mount->mnt_iosize_max;
		if (vp->v_clen != 0) {
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
			cursize = vp->v_lastw - vp->v_cstart + lblocksize;
			if (bp->b_loffset + lblocksize != filesize ||
			    loffset != vp->v_lastw + lblocksize || vp->v_clen <= cursize) {
				if (!async && seqcount > 0) {
					cluster_wbuild_wb(vp, lblocksize,
						vp->v_cstart, cursize);
				}
			} else {
				struct buf **bpp, **endbp;
				struct cluster_save *buflist;

				buflist = cluster_collectbufs(vp, bp,
							      lblocksize);
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
					 */
					for (bpp = buflist->bs_children;
					     bpp < endbp; bpp++)
						brelse(*bpp);
					free(buflist, M_SEGMENT);
					if (seqcount > 1) {
						cluster_wbuild_wb(vp, 
						    lblocksize, vp->v_cstart, 
						    cursize);
					}
				} else {
					/*
					 * Succeeded, keep building cluster.
					 */
					for (bpp = buflist->bs_children;
					     bpp <= endbp; bpp++)
						bdwrite(*bpp);
					free(buflist, M_SEGMENT);
					vp->v_lastw = loffset;
					vp->v_lasta = bp->b_bio2.bio_offset;
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
		    bp->b_loffset + lblocksize != filesize &&
		    (bp->b_bio2.bio_offset == NOOFFSET) &&
		    (VOP_BMAP(vp, loffset, NULL, &bp->b_bio2.bio_offset, &maxclen, NULL) ||
		     bp->b_bio2.bio_offset == NOOFFSET)) {
			bawrite(bp);
			vp->v_clen = 0;
			vp->v_lasta = bp->b_bio2.bio_offset;
			vp->v_cstart = loffset + lblocksize;
			vp->v_lastw = loffset;
			return;
		}
		if (maxclen > lblocksize)
			vp->v_clen = maxclen - lblocksize;
		else
			vp->v_clen = 0;
		if (!async && vp->v_clen == 0) { /* I/O not contiguous */
			vp->v_cstart = loffset + lblocksize;
			bawrite(bp);
		} else {	/* Wait for rest of cluster */
			vp->v_cstart = loffset;
			bdwrite(bp);
		}
	} else if (loffset == vp->v_cstart + vp->v_clen) {
		/*
		 * At end of cluster, write it out if seqcount tells us we
		 * are operating sequentially, otherwise let the buf or
		 * update daemon handle it.
		 */
		bdwrite(bp);
		if (seqcount > 1)
			cluster_wbuild_wb(vp, lblocksize, vp->v_cstart,
					  vp->v_clen + lblocksize);
		vp->v_clen = 0;
		vp->v_cstart = loffset + lblocksize;
	} else if (vm_page_count_severe()) {
		/*
		 * We are low on memory, get it going NOW
		 */
		bawrite(bp);
	} else {
		/*
		 * In the middle of a cluster, so just delay the I/O for now.
		 */
		bdwrite(bp);
	}
	vp->v_lastw = loffset;
	vp->v_lasta = bp->b_bio2.bio_offset;
}


/*
 * This is an awful lot like cluster_rbuild...wish they could be combined.
 * The last lbn argument is the current block on which I/O is being
 * performed.  Check to see that it doesn't fall in the middle of
 * the current block (if last_bp == NULL).
 */
int
cluster_wbuild(struct vnode *vp, int size, off_t start_loffset, int bytes)
{
	struct buf *bp, *tbp;
	int i, j;
	int totalwritten = 0;

	while (bytes > 0) {
		crit_enter();
		/*
		 * If the buffer is not delayed-write (i.e. dirty), or it 
		 * is delayed-write but either locked or inval, it cannot 
		 * partake in the clustered write.
		 */
		if (((tbp = findblk(vp, start_loffset)) == NULL) ||
		  ((tbp->b_flags & (B_LOCKED | B_INVAL | B_DELWRI)) != B_DELWRI) ||
		  BUF_LOCK(tbp, LK_EXCLUSIVE | LK_NOWAIT)) {
			start_loffset += size;
			bytes -= size;
			crit_exit();
			continue;
		}
		bremfree(tbp);
		tbp->b_flags &= ~B_DONE;
		crit_exit();

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
		  (tbp->b_bcount != size) ||
		  (bytes == size) ||
		  ((bp = getpbuf(&cluster_pbuf_freecnt)) == NULL)) {
			totalwritten += tbp->b_bufsize;
			bawrite(tbp);
			start_loffset += size;
			bytes -= size;
			continue;
		}

		/*
		 * We got a pbuf to make the cluster in.
		 * so initialise it.
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
		bp->b_flags |= B_CLUSTER |
			(tbp->b_flags & (B_VMIO | B_NEEDCOMMIT | B_NOWDRAIN));
		bp->b_bio1.bio_done = cluster_callback;
		bp->b_bio1.bio_caller_info1.cluster_head = NULL;
		bp->b_bio1.bio_caller_info2.cluster_tail = NULL;
		pbgetvp(vp, bp);
		/*
		 * From this location in the file, scan forward to see
		 * if there are buffers with adjacent data that need to
		 * be written as well.
		 */
		for (i = 0; i < bytes; (i += size), (start_loffset += size)) {
			if (i != 0) { /* If not the first buffer */
				crit_enter();
				/*
				 * If the adjacent data is not even in core it
				 * can't need to be written.
				 */
				if ((tbp = findblk(vp, start_loffset)) == NULL) {
					crit_exit();
					break;
				}

				/*
				 * If it IS in core, but has different
				 * characteristics, or is locked (which
				 * means it could be undergoing a background
				 * I/O or be in a weird state), then don't
				 * cluster with it.
				 */
				if ((tbp->b_flags & (B_VMIO | B_CLUSTEROK |
				    B_INVAL | B_DELWRI | B_NEEDCOMMIT))
				  != (B_DELWRI | B_CLUSTEROK |
				    (bp->b_flags & (B_VMIO | B_NEEDCOMMIT))) ||
				    (tbp->b_flags & B_LOCKED) ||
				    BUF_LOCK(tbp, LK_EXCLUSIVE | LK_NOWAIT)) {
					crit_exit();
					break;
				}

				/*
				 * Check that the combined cluster
				 * would make sense with regard to pages
				 * and would not be too large
				 */
				if ((tbp->b_bcount != size) ||
				  ((bp->b_bio2.bio_offset + i) !=
				    tbp->b_bio2.bio_offset) ||
				  ((tbp->b_xio.xio_npages + bp->b_xio.xio_npages) >
				    (vp->v_mount->mnt_iosize_max / PAGE_SIZE))) {
					BUF_UNLOCK(tbp);
					crit_exit();
					break;
				}
				/*
				 * Ok, it's passed all the tests,
				 * so remove it from the free list
				 * and mark it busy. We will use it.
				 */
				bremfree(tbp);
				tbp->b_flags &= ~B_DONE;
				crit_exit();
			} /* end of code for non-first buffers only */

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

				if (i != 0) { /* if not first buffer */
					for (j = 0; j < tbp->b_xio.xio_npages; ++j) {
						m = tbp->b_xio.xio_pages[j];
						if (m->flags & PG_BUSY) {
							bqrelse(tbp);
							goto finishcluster;
						}
					}
				}
					
				for (j = 0; j < tbp->b_xio.xio_npages; ++j) {
					m = tbp->b_xio.xio_pages[j];
					vm_page_io_start(m);
					vm_object_pip_add(m->object, 1);
					if ((bp->b_xio.xio_npages == 0) ||
					  (bp->b_xio.xio_pages[bp->b_xio.xio_npages - 1] != m)) {
						bp->b_xio.xio_pages[bp->b_xio.xio_npages] = m;
						bp->b_xio.xio_npages++;
					}
				}
			}
			bp->b_bcount += size;
			bp->b_bufsize += size;

			crit_enter();
			bundirty(tbp);
			tbp->b_flags &= ~(B_READ | B_DONE | B_ERROR);
			tbp->b_flags |= B_ASYNC;
			crit_exit();
			BUF_KERNPROC(tbp);
			cluster_append(&bp->b_bio1, tbp);

			/*
			 * check for latent dependencies to be handled 
			 */
			if (LIST_FIRST(&tbp->b_dep) != NULL && bioops.io_start)
				(*bioops.io_start)(tbp);

		}
	finishcluster:
		pmap_qenter(trunc_page((vm_offset_t) bp->b_data),
			(vm_page_t *) bp->b_xio.xio_pages, bp->b_xio.xio_npages);
		if (bp->b_bufsize > bp->b_kvasize)
			panic(
			    "cluster_wbuild: b_bufsize(%d) > b_kvasize(%d)\n",
			    bp->b_bufsize, bp->b_kvasize);
		bp->b_kvasize = bp->b_bufsize;
		totalwritten += bp->b_bufsize;
		bp->b_dirtyoff = 0;
		bp->b_dirtyend = bp->b_bufsize;
		bawrite(bp);

		bytes -= i;
	}
	return totalwritten;
}

/*
 * Collect together all the buffers in a cluster.
 * Plus add one additional buffer.
 */
static struct cluster_save *
cluster_collectbufs(struct vnode *vp, struct buf *last_bp, int lblocksize)
{
	struct cluster_save *buflist;
	struct buf *bp;
	off_t loffset;
	int i, len;

	len = (int)(vp->v_lastw - vp->v_cstart + lblocksize) / lblocksize;
	buflist = malloc(sizeof(struct buf *) * (len + 1) + sizeof(*buflist),
			 M_SEGMENT, M_WAITOK);
	buflist->bs_nchildren = 0;
	buflist->bs_children = (struct buf **) (buflist + 1);
	for (loffset = vp->v_cstart, i = 0; i < len; (loffset += lblocksize), i++) {
		(void) bread(vp, loffset, last_bp->b_bcount, &bp);
		buflist->bs_children[i] = bp;
		if (bp->b_bio2.bio_offset == NOOFFSET) {
			VOP_BMAP(bp->b_vp, bp->b_loffset, NULL,
				&bp->b_bio2.bio_offset, NULL, NULL);
		}
	}
	buflist->bs_children[i] = bp = last_bp;
	if (bp->b_bio2.bio_offset == NOOFFSET) {
		VOP_BMAP(bp->b_vp, bp->b_loffset, NULL,
			 &bp->b_bio2.bio_offset, NULL, NULL);
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

