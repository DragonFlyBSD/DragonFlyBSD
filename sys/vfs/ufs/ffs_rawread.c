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
 * $DragonFly: src/sys/vfs/ufs/ffs_rawread.c,v 1.15 2006/03/24 18:35:34 dillon Exp $
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
				 size_t len, struct thread *td, struct buf *bp,
				 caddr_t sa, int *baseticks);
static int ffs_rawread_main(struct vnode *vp,
			    struct uio *uio);

static int ffs_rawread_sync(struct vnode *vp, struct thread *td);

int ffs_rawread(struct vnode *vp, struct uio *uio, int *workdone);

void ffs_rawread_setup(void);

static void ffs_rawreadwakeup(struct bio *bio);


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
ffs_rawread_sync(struct vnode *vp, struct thread *td)
{
	int error;
	int upgraded;

	/* Check for dirty mmap, pending writes and dirty buffers */
	crit_enter();
	if (vp->v_track_write.bk_active > 0 ||
	    !RB_EMPTY(&vp->v_rbdirty_tree) ||
	    (vp->v_flag & VOBJDIRTY) != 0) {
		crit_exit();

		if (VOP_ISLOCKED(vp, td) != LK_EXCLUSIVE) {
			upgraded = 1;
			/* Upgrade to exclusive lock, this might block */
			VOP_LOCK(vp, LK_UPGRADE | LK_NOPAUSE, td);
		} else
			upgraded = 0;
		
		/* Attempt to msync mmap() regions to clean dirty mmap */ 
		if ((vp->v_flag & VOBJDIRTY) != 0) {
			struct vm_object *obj;
			if (VOP_GETVOBJECT(vp, &obj) == 0)
				vm_object_page_clean(obj, 0, 0, OBJPC_SYNC);
		}

		/* Wait for pending writes to complete */
		crit_enter();
		while (vp->v_track_write.bk_active) {
			vp->v_track_write.bk_waitflag = 1;
			error = tsleep(&vp->v_track_write, 0, "rawrdfls", 0);
			if (error != 0) {
				crit_exit();
				if (upgraded != 0)
					VOP_LOCK(vp, LK_DOWNGRADE, td);
				return (error);
			}
		}
		/* Flush dirty buffers */
		if (!RB_EMPTY(&vp->v_rbdirty_tree)) {
			crit_exit();
			if ((error = VOP_FSYNC(vp, MNT_WAIT, td)) != 0) {
				if (upgraded != 0)
					VOP_LOCK(vp, LK_DOWNGRADE, td);
				return (error);
			}
			crit_enter();
			if (vp->v_track_write.bk_active > 0 ||
			    !RB_EMPTY(&vp->v_rbdirty_tree))
				panic("ffs_rawread_sync: dirty bufs");
		}
		crit_exit();
		if (upgraded != 0)
			VOP_LOCK(vp, LK_DOWNGRADE, td);
	} else {
		crit_exit();
	}
	return 0;
}


static int
ffs_rawread_readahead(struct vnode *vp, caddr_t udata, off_t loffset,
		      size_t len, struct thread *td, struct buf *bp,
		      caddr_t sa, int *baseticks)
{
	int error;
	uint iolen;
	off_t blockno;
	int blockoff;
	int bsize;
	struct vnode *dp;
	int bforwards;
	
	bsize = vp->v_mount->mnt_stat.f_iosize;
	
	iolen = ((vm_offset_t) udata) & PAGE_MASK;
	bp->b_bcount = len;
	if (bp->b_bcount + iolen > bp->b_kvasize) {
		bp->b_bcount = bp->b_kvasize;
		if (iolen != 0)
			bp->b_bcount -= PAGE_SIZE;
	}
	bp->b_flags = B_PHYS | B_READ;
	bp->b_data = udata;
	bp->b_saveaddr = sa;
	bp->b_loffset = loffset;
	bp->b_bio2.bio_offset = NOOFFSET;
	bp->b_bio2.bio_done = ffs_rawreadwakeup;

	error = VOP_BMAP(vp, bp->b_loffset, &dp, &bp->b_bio2.bio_offset,
			 &bforwards, NULL);
	if (error != 0) {
		return error;
	}
	if (bp->b_bio2.bio_offset == NOOFFSET) {
		/* 
		 * Fill holes with NULs to preserve semantics 
		 */
		if (bp->b_bcount + blockoff * DEV_BSIZE > bsize)
			bp->b_bcount = bsize - blockoff * DEV_BSIZE;
		bp->b_bufsize = bp->b_bcount;
		
		if (vmapbuf(bp) < 0)
			return EFAULT;
		
		if (ticks - *baseticks >= hogticks) {
			*baseticks = ticks;
			uio_yield();
		}
		bzero(bp->b_data, bp->b_bufsize);

		/* Mark operation completed (similar to bufdone()) */

		bp->b_resid = 0;
		bp->b_flags |= B_DONE;
		return 0;
	}
	
	if (bp->b_bcount + blockoff * DEV_BSIZE > bforwards)
		bp->b_bcount = bforwards - blockoff * DEV_BSIZE;
	bp->b_bufsize = bp->b_bcount;
	bp->b_bio2.bio_offset += blockoff * DEV_BSIZE;
	
	if (vmapbuf(bp) < 0)
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
	bio_start_transaction(&bp->b_bio1, &vp->v_track_read);
	vn_strategy(dp, &bp->b_bio2);
	return 0;
}


static int
ffs_rawread_main(struct vnode *vp, struct uio *uio)
{
	int error, nerror;
	struct buf *bp, *nbp, *tbp;
	caddr_t sa, nsa, tsa;
	uint iolen;
	int baseticks = ticks;
	caddr_t udata;
	int resid;
	off_t offset;
	struct thread *td;
	
	td = uio->uio_td ? uio->uio_td : curthread;
	udata = uio->uio_iov->iov_base;
	resid = uio->uio_resid;
	offset = uio->uio_offset;

	error = 0;
	nerror = 0;
	
	bp = NULL;
	nbp = NULL;
	sa = NULL;
	nsa = NULL;
	
	while (resid > 0) {
		
		if (bp == NULL) { /* Setup first read */
			/* XXX: Leave some bufs for swap */
			bp = getpbuf(&ffsrawbufcnt);
			sa = bp->b_data;
			bp->b_vp = vp; 
			error = ffs_rawread_readahead(vp, udata, offset, resid,
				    td, bp, sa, &baseticks);
			if (error != 0)
				break;
			
			if (resid > bp->b_bufsize) { /* Setup fist readahead */
				/* XXX: Leave bufs for swap */
				if (rawreadahead != 0) 
					nbp = trypbuf(&ffsrawbufcnt);
				else
					nbp = NULL;
				if (nbp != NULL) {
					nsa = nbp->b_data;
					nbp->b_vp = vp;
					
					nerror = ffs_rawread_readahead(
							vp, 
							udata + bp->b_bufsize,
							offset + bp->b_bufsize,
							resid - bp->b_bufsize,
							td, nbp, nsa,
							&baseticks);
					if (nerror) {
						relpbuf(nbp, &ffsrawbufcnt);
						nbp = NULL;
					}
				}
			}
		}
		
		crit_enter();
		while ((bp->b_flags & B_DONE) == 0) {
			tsleep((caddr_t)&bp->b_bio2, 0, "rawrd", 0);
		}
		crit_exit();
		
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
				    bp->b_bufsize - iolen, td, bp,
				    sa, &baseticks);
			if (error != 0)
				break;
		} else if (nbp != NULL) { /* Complete read with readahead */
			
			tbp = bp;
			bp = nbp;
			nbp = tbp;
			
			tsa = sa;
			sa = nsa;
			nsa = tsa;

			clearbiocache(&nbp->b_bio2);
			
			if (resid <= bp->b_bufsize) { /* No more readaheads */
				relpbuf(nbp, &ffsrawbufcnt);
				nbp = NULL;
			} else { /* Setup next readahead */
				nerror = ffs_rawread_readahead(
						vp, udata + bp->b_bufsize,
				   		offset + bp->b_bufsize,
						resid - bp->b_bufsize,
						td, nbp, nsa, &baseticks);
				if (nerror != 0) {
					relpbuf(nbp, &ffsrawbufcnt);
					nbp = NULL;
				}
			}
		} else if (nerror != 0) {/* Deferred Readahead error */
			break;		
		}  else if (resid > 0) { /* More to read, no readahead */
			error = ffs_rawread_readahead(vp, udata, offset,
						      resid, td, bp, sa,
						      &baseticks);
			if (error != 0)
				break;
		}
	}
	
	if (bp != NULL)
		relpbuf(bp, &ffsrawbufcnt);
	if (nbp != NULL) {			/* Run down readahead buffer */
		crit_enter();
		while ((nbp->b_flags & B_DONE) == 0) {
			tsleep(&nbp->b_bio2, 0, "rawrd", 0);
		}
		crit_exit();
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
	    (((uio->uio_td != NULL) ? uio->uio_td : curthread)->td_flags &
	     TDF_DEADLKTREAT) == 0) {
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
			error = ffs_rawread_sync(vp,
						 (uio->uio_td != NULL) ?
						 uio->uio_td : curthread);
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


static void
ffs_rawreadwakeup(struct bio *bio)
{
	wakeup(bio);
}
