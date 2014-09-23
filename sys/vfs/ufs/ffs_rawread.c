/*-
 * Copyright (c) 2000-2003 Tor Egge
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/ufs/ffs/ffs_rawread.c,v 1.3.2.2 2003/05/29 06:15:35 alc Exp $
 * $DragonFly: src/sys/vfs/ufs/ffs_rawread.c,v 1.28 2008/06/19 23:27:39 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/buf.h>
#include "quota.h"
#include "inode.h"
#include "fs.h"

#include <machine/limits.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

static int ffs_rawread_readahead(struct vnode *vp, caddr_t udata, off_t offset,
				 size_t len, struct buf *bp);
static int ffs_rawread_main(struct vnode *vp,
			    struct uio *uio);

static int ffs_rawread_sync(struct vnode *vp);

int ffs_rawread(struct vnode *vp, struct uio *uio, int *workdone);

void ffs_rawread_setup(void);

SYSCTL_DECL(_vfs_ffs);

static int ffsrawbufcnt = 4;
SYSCTL_INT(_vfs_ffs, OID_AUTO, ffsrawbufcnt, CTLFLAG_RD, &ffsrawbufcnt, 0,
	   "Buffers available for raw reads");

static int allowrawread = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, allowrawread, CTLFLAG_RW, &allowrawread, 0,
	   "Flag to enable raw reads");

static int rawreadahead = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, rawreadahead, CTLFLAG_RW, &rawreadahead, 0,
	   "Flag to enable readahead for long raw reads");


void
ffs_rawread_setup(void)
{
	ffsrawbufcnt = (nswbuf > 100 ) ? (nswbuf - (nswbuf >> 4)) : nswbuf - 8;
}


static int
ffs_rawread_sync(struct vnode *vp)
{
	int error;

	/*
	 * Check for dirty mmap, pending writes and dirty buffers
	 */
	lwkt_gettoken(&vp->v_token);
	if (bio_track_active(&vp->v_track_write) ||
	    !RB_EMPTY(&vp->v_rbdirty_tree) ||
	    (vp->v_flag & VOBJDIRTY) != 0) {
		/* Attempt to msync mmap() regions to clean dirty mmap */ 
		if ((vp->v_flag & VOBJDIRTY) != 0) {
			struct vm_object *obj;
			if ((obj = vp->v_object) != NULL)
				vm_object_page_clean(obj, 0, 0, OBJPC_SYNC);
		}

		/* Wait for pending writes to complete */
		error = bio_track_wait(&vp->v_track_write, 0, 0);
		if (error != 0) {
			goto done;
		}
		/* Flush dirty buffers */
		if (!RB_EMPTY(&vp->v_rbdirty_tree)) {
			if ((error = VOP_FSYNC(vp, MNT_WAIT, 0)) != 0) {
				goto done;
			}
			if (bio_track_active(&vp->v_track_write) ||
			    !RB_EMPTY(&vp->v_rbdirty_tree))
				panic("ffs_rawread_sync: dirty bufs");
		}
	} else {
		error = 0;
	}
done:
	lwkt_reltoken(&vp->v_token);
	return error;
}


static int
ffs_rawread_readahead(struct vnode *vp, caddr_t udata, off_t loffset,
		      size_t len, struct buf *bp)
{
	int error;
	int iolen;
	int blockoff;
	int bsize;
	struct vnode *dp;
	int bforwards;
	
	bsize = vp->v_mount->mnt_stat.f_iosize;

	/*
	 * Make sure it fits into the pbuf
	 */
	iolen = (int)(intptr_t)udata & PAGE_MASK;
	if (len + iolen > bp->b_kvasize) {
		len = bp->b_kvasize;
		if (iolen != 0)
			len -= PAGE_SIZE;
	}

	/*
	 * Raw disk address is in bio2, but we wait for it to
	 * chain to bio1.
	 */
	bp->b_flags &= ~B_ERROR;
	bp->b_loffset = loffset;
	bp->b_bio2.bio_offset = NOOFFSET;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;

	blockoff = (loffset % bsize) / DEV_BSIZE;

	error = VOP_BMAP(vp, bp->b_loffset, &bp->b_bio2.bio_offset,
			 &bforwards, NULL, BUF_CMD_READ);
	if (error != 0)
		return error;
	dp = VTOI(vp)->i_devvp;
	if (bp->b_bio2.bio_offset == NOOFFSET) {
		/* 
		 * Fill holes with NULs to preserve semantics 
		 */
		if (len + blockoff * DEV_BSIZE > bsize)
			len = bsize - blockoff * DEV_BSIZE;
		
		if (vmapbuf(bp, udata, len) < 0)
			return EFAULT;
		
		lwkt_user_yield();
		bzero(bp->b_data, bp->b_bcount);

		/* Mark operation completed (similar to bufdone()) */

		bp->b_resid = 0;
		return 0;
	}
	
	if (len + blockoff * DEV_BSIZE > bforwards)
		len = bforwards - blockoff * DEV_BSIZE;
	bp->b_bio2.bio_offset += blockoff * DEV_BSIZE;
	
	if (vmapbuf(bp, udata, len) < 0)
		return EFAULT;
	
	/*
	 * Access the block device layer using the device vnode (dp) and
	 * the translated block number (bio2) instead of the logical block
	 * number (bio1).
	 *
	 * Even though we are bypassing the vnode layer, we still
	 * want the vnode state to indicate that an I/O on its behalf
	 * is in progress.
	 */
	bp->b_cmd = BUF_CMD_READ;
	bio_start_transaction(&bp->b_bio1, &vp->v_track_read);
	vn_strategy(dp, &bp->b_bio2);
	return 0;
}

static int
ffs_rawread_main(struct vnode *vp, struct uio *uio)
{
	int error, nerror;
	struct buf *bp, *nbp, *tbp;
	int iolen;
	caddr_t udata;
	int resid;
	off_t offset;
	
	udata = uio->uio_iov->iov_base;
	resid = uio->uio_resid;
	offset = uio->uio_offset;

	error = 0;
	nerror = 0;
	
	bp = NULL;
	nbp = NULL;
	
	while (resid > 0) {
		
		if (bp == NULL) { /* Setup first read */
			/* XXX: Leave some bufs for swap */
			bp = getpbuf_kva(&ffsrawbufcnt);
			error = ffs_rawread_readahead(vp, udata, offset,
						      resid, bp);
			if (error != 0)
				break;
			
			if (resid > bp->b_bufsize) { /* Setup fist readahead */
				/* XXX: Leave bufs for swap */
				if (rawreadahead != 0) 
					nbp = trypbuf_kva(&ffsrawbufcnt);
				else
					nbp = NULL;
				if (nbp != NULL) {
					nerror = ffs_rawread_readahead(
							vp, 
							udata + bp->b_bufsize,
							offset + bp->b_bufsize,
							resid - bp->b_bufsize,
							nbp);
					if (nerror) {
						relpbuf(nbp, &ffsrawbufcnt);
						nbp = NULL;
					}
				}
			}
		}
		
		biowait(&bp->b_bio1, "rawrd");
		
		vunmapbuf(bp);
		
		iolen = bp->b_bcount - bp->b_resid;
		if (iolen == 0 && (bp->b_flags & B_ERROR) == 0) {
			nerror = 0;	/* Ignore possible beyond EOF error */
			break; /* EOF */
		}
		
		if ((bp->b_flags & B_ERROR) != 0) {
			error = bp->b_error;
			break;
		}
		clearbiocache(&bp->b_bio2);
		resid -= iolen;
		udata += iolen;
		offset += iolen;
		if (iolen < bp->b_bufsize) {
			/* Incomplete read.  Try to read remaining part */
			error = ffs_rawread_readahead(
				    vp, udata, offset,
				    bp->b_bufsize - iolen, bp);
			if (error != 0)
				break;
		} else if (nbp != NULL) { /* Complete read with readahead */
			
			tbp = bp;
			bp = nbp;
			nbp = tbp;
			
			clearbiocache(&nbp->b_bio2);
			
			if (resid <= bp->b_bufsize) { /* No more readaheads */
				relpbuf(nbp, &ffsrawbufcnt);
				nbp = NULL;
			} else { /* Setup next readahead */
				nerror = ffs_rawread_readahead(
						vp, udata + bp->b_bufsize,
				   		offset + bp->b_bufsize,
						resid - bp->b_bufsize,
						nbp);
				if (nerror != 0) {
					relpbuf(nbp, &ffsrawbufcnt);
					nbp = NULL;
				}
			}
		} else if (nerror != 0) {/* Deferred Readahead error */
			break;		
		}  else if (resid > 0) { /* More to read, no readahead */
			error = ffs_rawread_readahead(vp, udata, offset,
						      resid, bp);
			if (error != 0)
				break;
		}
	}
	
	if (bp != NULL)
		relpbuf(bp, &ffsrawbufcnt);
	if (nbp != NULL) {			/* Run down readahead buffer */
		biowait(&nbp->b_bio1, "rawrd");
		vunmapbuf(nbp);
		relpbuf(nbp, &ffsrawbufcnt);
	}
	
	if (error == 0)
		error = nerror;
	uio->uio_iov->iov_base = udata;
	uio->uio_resid = resid;
	uio->uio_offset = offset;
	return error;
}


int
ffs_rawread(struct vnode *vp,
	    struct uio *uio,
	    int *workdone)
{
	if (allowrawread != 0 &&
	    uio->uio_iovcnt == 1 && 
	    uio->uio_segflg == UIO_USERSPACE &&
	    uio->uio_resid == uio->uio_iov->iov_len &&
	    (curthread->td_flags & TDF_DEADLKTREAT) == 0) {
		int secsize;		/* Media sector size */
		off_t filebytes;	/* Bytes left of file */
		int blockbytes;		/* Bytes left of file in full blocks */
		int partialbytes;	/* Bytes in last partial block */
		int skipbytes;		/* Bytes not to read in ffs_rawread */
		struct inode *ip;
		int error;
		

		/* Only handle sector aligned reads */
		ip = VTOI(vp);
		secsize = ip->i_devvp->v_rdev->si_bsize_phys;
		if ((uio->uio_offset & (secsize - 1)) == 0 &&
		    (uio->uio_resid & (secsize - 1)) == 0) {
			
			/* Sync dirty pages and buffers if needed */
			error = ffs_rawread_sync(vp);
			if (error != 0)
				return error;
			
			/* Check for end of file */
			if (ip->i_size > uio->uio_offset) {
				filebytes = ip->i_size - uio->uio_offset;

				/* No special eof handling needed ? */
				if (uio->uio_resid <= filebytes) {
					*workdone = 1;
					return ffs_rawread_main(vp, uio);
				}
				
				partialbytes = ((unsigned int) ip->i_size) %
					ip->i_fs->fs_bsize;
				blockbytes = (int) filebytes - partialbytes;
				if (blockbytes > 0) {
					skipbytes = uio->uio_resid -
						blockbytes;
					uio->uio_resid = blockbytes;
					error = ffs_rawread_main(vp, uio);
					uio->uio_resid += skipbytes;
					if (error != 0)
						return error;
					/* Read remaining part using buffer */
				}
			}
		}
	}
	*workdone = 0;
	return 0;
}
