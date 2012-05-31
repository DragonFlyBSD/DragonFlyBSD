/*	$NetBSD: puffs_vnops.c,v 1.154 2011/07/04 08:07:30 manu Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program and the Ulla Tuominen Foundation.
 * The Google SoC project was mentored by Bill Studenmund.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/thread2.h>

#include <vfs/puffs/puffs_msgif.h>
#include <vfs/puffs/puffs_sys.h>

#define RWARGS(cont, iofl, move, offset, creds)				\
	(cont)->pvnr_ioflag = (iofl);					\
	(cont)->pvnr_resid = (move);					\
	(cont)->pvnr_offset = (offset);					\
	puffs_credcvt(&(cont)->pvnr_cred, creds)

int
puffs_directread(struct vnode *vp, struct uio *uio, int ioflag,
    struct ucred *cred)
{
	PUFFS_MSG_VARS(vn, read);
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	size_t tomove, argsize;
	int error;

	KKASSERT(vp->v_type == VREG);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	read_msg = NULL;
	error = 0;

	/* std sanity */
	if (uio->uio_resid == 0)
		return 0;
	if (uio->uio_offset < 0)
		return EINVAL;

	/*
	 * in case it's not a regular file or we're operating
	 * uncached, do read in the old-fashioned style,
	 * i.e. explicit read operations
	 */

	tomove = PUFFS_TOMOVE(uio->uio_resid, pmp);
	argsize = sizeof(struct puffs_vnmsg_read);
	puffs_msgmem_alloc(argsize + tomove, &park_read,
	    (void *)&read_msg, 1);

	error = 0;
	while (uio->uio_resid > 0) {
		tomove = PUFFS_TOMOVE(uio->uio_resid, pmp);
		memset(read_msg, 0, argsize); /* XXX: touser KASSERT */
		RWARGS(read_msg, ioflag, tomove,
		    uio->uio_offset, cred);
		puffs_msg_setinfo(park_read, PUFFSOP_VN,
		    PUFFS_VN_READ, VPTOPNC(vp));
		puffs_msg_setdelta(park_read, tomove);

		PUFFS_MSG_ENQUEUEWAIT2(pmp, park_read, vp->v_data,
		    NULL, error);
		error = checkerr(pmp, error, __func__);
		if (error)
			break;

		if (read_msg->pvnr_resid > tomove) {
			puffs_senderr(pmp, PUFFS_ERR_READ,
			    E2BIG, "resid grew", VPTOPNC(vp));
			error = EPROTO;
			break;
		}

		error = uiomove(read_msg->pvnr_data,
		    tomove - read_msg->pvnr_resid, uio);

		/*
		 * in case the file is out of juice, resid from
		 * userspace is != 0.  and the error-case is
		 * quite obvious
		 */
		if (error || read_msg->pvnr_resid)
			break;
	}

	puffs_msgmem_release(park_read);

	return error;
}

int
puffs_directwrite(struct vnode *vp, struct uio *uio, int ioflag,
    struct ucred *cred)
{
	PUFFS_MSG_VARS(vn, write);
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	size_t tomove, argsize;
	int error, uflags;

	KKASSERT(vp->v_type == VREG);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	error = uflags = 0;
	write_msg = NULL;

	/* tomove is non-increasing */
	tomove = PUFFS_TOMOVE(uio->uio_resid, pmp);
	argsize = sizeof(struct puffs_vnmsg_write) + tomove;
	puffs_msgmem_alloc(argsize, &park_write, (void *)&write_msg,1);

	while (uio->uio_resid > 0) {
		/* move data to buffer */
		tomove = PUFFS_TOMOVE(uio->uio_resid, pmp);
		memset(write_msg, 0, argsize); /* XXX: touser KASSERT */
		RWARGS(write_msg, ioflag, tomove,
		    uio->uio_offset, cred);
		error = uiomove(write_msg->pvnr_data, tomove, uio);
		if (error)
			break;

		/* move buffer to userspace */
		puffs_msg_setinfo(park_write, PUFFSOP_VN,
		    PUFFS_VN_WRITE, VPTOPNC(vp));
		PUFFS_MSG_ENQUEUEWAIT2(pmp, park_write, vp->v_data,
		    NULL, error);
		error = checkerr(pmp, error, __func__);
		if (error)
			break;

		if (write_msg->pvnr_resid > tomove) {
			puffs_senderr(pmp, PUFFS_ERR_WRITE,
			    E2BIG, "resid grew", VPTOPNC(vp));
			error = EPROTO;
			break;
		}

		if (PUFFS_USE_PAGECACHE(pmp))
			KKASSERT(vp->v_filesize >= uio->uio_offset);

		/* didn't move everything?  bad userspace.  bail */
		if (write_msg->pvnr_resid != 0) {
			error = EIO;
			break;
		}
	}
	puffs_msgmem_release(park_write);

	return error;
}

static void
puffs_iodone(struct bio *bio)
{
	bio->bio_flags = 0;
	bpdone(bio->bio_buf, 0);
}

int
puffs_bioread(struct vnode *vp, struct uio *uio, int ioflag,
    struct ucred *cred)
{
	int biosize = vp->v_mount->mnt_stat.f_iosize;
	struct buf *bp;
	struct vattr vattr;
	off_t lbn, loffset, fsize;
	size_t n;
	int boff, seqcount;
	int error = 0;

	KKASSERT(uio->uio_rw == UIO_READ);
	KKASSERT(vp->v_type == VREG);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	seqcount = (int)((off_t)(ioflag >> IO_SEQSHIFT) * biosize / BKVASIZE);

	/*
	 * Cache consistency can only be maintained approximately.
	 *
	 * GETATTR is called to synchronize the file size.
	 *
	 * NOTE: In the normal case the attribute cache is not
	 * cleared which means GETATTR may use cached data and
	 * not immediately detect changes made on the server.
	 */

	error = VOP_GETATTR(vp, &vattr);
	if (error)
		return error;

	/*
	 * Loop until uio exhausted or we hit EOF
	 */
	do {
		bp = NULL;

		lbn = uio->uio_offset / biosize;
		boff = uio->uio_offset & (biosize - 1);
		loffset = lbn * biosize;
		fsize = puffs_meta_getsize(vp);

		if (loffset + boff >= fsize) {
			n = 0;
			break;
		}
		bp = getblk(vp, loffset, biosize, 0, 0);

		if (bp == NULL)
			return EINTR;

		/*
		 * If B_CACHE is not set, we must issue the read.  If this
		 * fails, we return an error.
		 */
		if ((bp->b_flags & B_CACHE) == 0) {
			bp->b_cmd = BUF_CMD_READ;
			bp->b_bio2.bio_done = puffs_iodone;
			bp->b_bio2.bio_flags |= BIO_SYNC;
			vfs_busy_pages(vp, bp);
			error = puffs_doio(vp, &bp->b_bio2, uio->uio_td);
			if (error) {
				brelse(bp);
				return error;
			}
		}

		/*
		 * on is the offset into the current bp.  Figure out how many
		 * bytes we can copy out of the bp.  Note that bcount is
		 * NOT DEV_BSIZE aligned.
		 *
		 * Then figure out how many bytes we can copy into the uio.
		 */
		n = biosize - boff;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (loffset + boff + n > fsize)
			n = fsize - loffset - boff;

		if (n > 0)
			error = uiomove(bp->b_data + boff, n, uio);
		if (bp)
			brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n > 0);

	return error;
}

int
puffs_biowrite(struct vnode *vp, struct uio *uio, int ioflag,
    struct ucred *cred)
{
	int biosize = vp->v_mount->mnt_stat.f_iosize;
	struct buf *bp;
	struct vattr vattr;
	off_t loffset, fsize;
	int boff, bytes;
	int error = 0;
	int bcount;
	int trivial;

	KKASSERT(uio->uio_rw == UIO_WRITE);
	KKASSERT(vp->v_type == VREG);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	/*
	 * If IO_APPEND then load uio_offset.  We restart here if we cannot
	 * get the append lock.
	 *
	 * We need to obtain exclusize lock if we intend to modify file size
	 * in order to guarentee the append point with multiple contending
	 * writers.
	 */
	if (ioflag & IO_APPEND) {
		/* XXXDF relock if necessary */
		KKASSERT(vn_islocked(vp) == LK_EXCLUSIVE);
		error = VOP_GETATTR(vp, &vattr);
		if (error)
			return error;
		uio->uio_offset = puffs_meta_getsize(vp);
	}

	do {
		boff = uio->uio_offset & (biosize-1);
		loffset = uio->uio_offset - boff;
		bytes = (int)szmin((unsigned)(biosize - boff), uio->uio_resid);
again:
		/*
		 * Handle direct append and file extension cases, calculate
		 * unaligned buffer size.  When extending B_CACHE will be
		 * set if possible.  See UIO_NOCOPY note below.
		 */
		fsize = puffs_meta_getsize(vp);
		if (uio->uio_offset + bytes > fsize) {
			trivial = (uio->uio_segflg != UIO_NOCOPY &&
			    uio->uio_offset <= fsize);
			puffs_meta_setsize(vp, uio->uio_offset + bytes,
			    trivial);
		}
		bp = getblk(vp, loffset, biosize, 0, 0);
		if (bp == NULL) {
			error = EINTR;
			break;
		}

		/*
		 * Actual bytes in buffer which we care about
		 */
		if (loffset + biosize < fsize)
			bcount = biosize;
		else
			bcount = (int)(fsize - loffset);

		/*
		 * Avoid a read by setting B_CACHE where the data we
		 * intend to write covers the entire buffer.  Note
		 * that the buffer may have been set to B_CACHE by
		 * puffs_meta_setsize() above or otherwise inherited the
		 * flag, but if B_CACHE isn't set the buffer may be
		 * uninitialized and must be zero'd to accomodate
		 * future seek+write's.
		 *
		 * See the comments in kern/vfs_bio.c's getblk() for
		 * more information.
		 *
		 * When doing a UIO_NOCOPY write the buffer is not
		 * overwritten and we cannot just set B_CACHE unconditionally
		 * for full-block writes.
		 */
		if (boff == 0 && bytes == biosize &&
		    uio->uio_segflg != UIO_NOCOPY) {
			bp->b_flags |= B_CACHE;
			bp->b_flags &= ~(B_ERROR | B_INVAL);
		}

		/*
		 * b_resid may be set due to file EOF if we extended out.
		 * The NFS bio code will zero the difference anyway so
		 * just acknowledged the fact and set b_resid to 0.
		 */
		if ((bp->b_flags & B_CACHE) == 0) {
			bp->b_cmd = BUF_CMD_READ;
			bp->b_bio2.bio_done = puffs_iodone;
			bp->b_bio2.bio_flags |= BIO_SYNC;
			vfs_busy_pages(vp, bp);
			error = puffs_doio(vp, &bp->b_bio2, uio->uio_td);
			if (error) {
				brelse(bp);
				break;
			}
			bp->b_resid = 0;
		}

		/*
		 * If dirtyend exceeds file size, chop it down.  This should
		 * not normally occur but there is an append race where it
		 * might occur XXX, so we log it.
		 *
		 * If the chopping creates a reverse-indexed or degenerate
		 * situation with dirtyoff/end, we 0 both of them.
		 */
		if (bp->b_dirtyend > bcount) {
			kprintf("PUFFS append race @%08llx:%d\n",
			    (long long)bp->b_bio2.bio_offset,
			    bp->b_dirtyend - bcount);
			bp->b_dirtyend = bcount;
		}

		if (bp->b_dirtyoff >= bp->b_dirtyend)
			bp->b_dirtyoff = bp->b_dirtyend = 0;

		/*
		 * If the new write will leave a contiguous dirty
		 * area, just update the b_dirtyoff and b_dirtyend,
		 * otherwise force a write rpc of the old dirty area.
		 *
		 * While it is possible to merge discontiguous writes due to
		 * our having a B_CACHE buffer ( and thus valid read data
		 * for the hole), we don't because it could lead to
		 * significant cache coherency problems with multiple clients,
		 * especially if locking is implemented later on.
		 *
		 * as an optimization we could theoretically maintain
		 * a linked list of discontinuous areas, but we would still
		 * have to commit them separately so there isn't much
		 * advantage to it except perhaps a bit of asynchronization.
		 */
		if (bp->b_dirtyend > 0 &&
		    (boff > bp->b_dirtyend ||
		    (boff + bytes) < bp->b_dirtyoff)
		   ) {
			if (bwrite(bp) == EINTR) {
				error = EINTR;
				break;
			}
			goto again;
		}

		error = uiomove(bp->b_data + boff, bytes, uio);

		/*
		 * Since this block is being modified, it must be written
		 * again and not just committed.  Since write clustering does
		 * not work for the stage 1 data write, only the stage 2
		 * commit rpc, we have to clear B_CLUSTEROK as well.
		 */
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);

		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Only update dirtyoff/dirtyend if not a degenerate
		 * condition.
		 *
		 * The underlying VM pages have been marked valid by
		 * virtue of acquiring the bp.  Because the entire buffer
		 * is marked dirty we do not have to worry about cleaning
		 * out the related dirty bits (and wouldn't really know
		 * how to deal with byte ranges anyway)
		 */
		if (bytes) {
			if (bp->b_dirtyend > 0) {
				bp->b_dirtyoff = imin(boff, bp->b_dirtyoff);
				bp->b_dirtyend = imax(boff + bytes,
				    bp->b_dirtyend);
			} else {
				bp->b_dirtyoff = boff;
				bp->b_dirtyend = boff + bytes;
			}
		}

		if (ioflag & IO_SYNC) {
			if (ioflag & IO_INVAL)
				bp->b_flags |= B_NOCACHE;
			error = bwrite(bp);
			if (error)
				break;
		} else {
			bdwrite(bp);
		}
	} while (uio->uio_resid > 0 && bytes > 0);

	return error;
}

int
puffs_doio(struct vnode *vp, struct bio *bio, struct thread *td)
{
	struct buf *bp = bio->bio_buf;
	struct ucred *cred;
	struct uio *uiop;
	struct uio uio;
	struct iovec io;
	size_t n;
	int error = 0;

	if (td != NULL && td->td_proc != NULL)
		cred = td->td_proc->p_ucred;
	else
		cred = proc0.p_ucred;

	uiop = &uio;
	uiop->uio_iov = &io;
	uiop->uio_iovcnt = 1;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = td;

	/*
	 * clear B_ERROR and B_INVAL state prior to initiating the I/O.  We
	 * do this here so we do not have to do it in all the code that
	 * calls us.
	 */
	bp->b_flags &= ~(B_ERROR | B_INVAL);

	KASSERT(bp->b_cmd != BUF_CMD_DONE,
	    ("puffs_doio: bp %p already marked done!", bp));

	if (bp->b_cmd == BUF_CMD_READ) {
		io.iov_len = uiop->uio_resid = (size_t)bp->b_bcount;
		io.iov_base = bp->b_data;
		uiop->uio_rw = UIO_READ;

		uiop->uio_offset = bio->bio_offset;
		error = puffs_directread(vp, uiop, 0, cred);
		if (error == 0 && uiop->uio_resid) {
			n = (size_t)bp->b_bcount - uiop->uio_resid;
			bzero(bp->b_data + n, bp->b_bcount - n);
			uiop->uio_resid = 0;
		}
		if (error) {
			bp->b_flags |= B_ERROR;
			bp->b_error = error;
		}
		bp->b_resid = uiop->uio_resid;
	} else {
		KKASSERT(bp->b_cmd == BUF_CMD_WRITE);
		if (bio->bio_offset + bp->b_dirtyend > puffs_meta_getsize(vp))
			bp->b_dirtyend = puffs_meta_getsize(vp) -
			    bio->bio_offset;

		if (bp->b_dirtyend > bp->b_dirtyoff) {
			io.iov_len = uiop->uio_resid = bp->b_dirtyend
			    - bp->b_dirtyoff;
			uiop->uio_offset = bio->bio_offset + bp->b_dirtyoff;
			io.iov_base = (char *)bp->b_data + bp->b_dirtyoff;
			uiop->uio_rw = UIO_WRITE;

			error = puffs_directwrite(vp, uiop, 0, cred);

			if (error == EINTR
			    || (!error && (bp->b_flags & B_NEEDCOMMIT))) {
				crit_enter();
				bp->b_flags &= ~(B_INVAL|B_NOCACHE);
				if ((bp->b_flags & B_PAGING) == 0)
					bdirty(bp);
				if (error)
					bp->b_flags |= B_EINTR;
				crit_exit();
			} else {
				if (error) {
					bp->b_flags |= B_ERROR;
					bp->b_error = error;
				}
				bp->b_dirtyoff = bp->b_dirtyend = 0;
			}
			bp->b_resid = uiop->uio_resid;
		} else {
			bp->b_resid = 0;
		}
	}

	biodone(bio);
	KKASSERT(bp->b_cmd == BUF_CMD_DONE);
	if (bp->b_flags & B_EINTR)
		return (EINTR);
	if (bp->b_flags & B_ERROR)
		return (bp->b_error ? bp->b_error : EIO);
	return (0);
}
