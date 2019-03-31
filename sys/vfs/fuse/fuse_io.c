/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
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
 */

#include "fuse.h"

#include <sys/uio.h>
#include <sys/buf2.h>

static void
fuse_brelse(struct buf *bp)
{
	bp->b_flags |= B_INVAL | B_RELBUF;
	brelse(bp);
}

static void
fuse_fix_size(struct fuse_node *fnp, bool fixsize, size_t oldsize)
{
	if (fixsize)
		fuse_node_truncate(fnp, fnp->size, oldsize);
}

int
fuse_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);
	bool need_reopen = !curproc || fnp->closed; /* XXX */
	int error = 0;

	while (uio->uio_resid > 0 && uio->uio_offset < fnp->size) {
		struct file *fp;
		struct buf *bp;
		struct fuse_ipc *fip;
		struct fuse_read_in *fri;
		off_t base_offset, buf_offset;
		size_t len;
		uint64_t fh;

		fh = fuse_nfh(VTOI(vp));
		if (ap->a_fp)
			fh = fuse_fh(ap->a_fp);

		buf_offset = (off_t)uio->uio_offset & FUSE_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - buf_offset;

		fuse_dbg("uio_offset=%ju uio_resid=%ju base_offset=%ju "
		    "buf_offset=%ju\n",
		    uio->uio_offset, uio->uio_resid, base_offset, buf_offset);

		bp = getblk(vp, base_offset, FUSE_BLKSIZE, 0, 0);
		KKASSERT(bp);
		if ((bp->b_flags & (B_INVAL | B_CACHE | B_RAM)) == B_CACHE) {
			bp->b_flags &= ~B_AGE;
			goto skip;
		}
		if (ap->a_ioflag & IO_NRDELAY) {
			bqrelse(bp);
			return EWOULDBLOCK;
		}

		error = breadnx(vp, base_offset, FUSE_BLKSIZE, B_NOTMETA, NULL,
		    NULL, 0, &bp);
		KKASSERT(!error);

		fuse_dbg("b_loffset=%ju b_bcount=%d b_flags=%x\n",
		    bp->b_loffset, bp->b_bcount, bp->b_flags);

		if (need_reopen) {
			error = falloc(NULL, &fp, NULL);
			if (error) {
				fuse_brelse(bp);
				break;
			}
			error = VOP_OPEN(vp, FREAD | FWRITE, ap->a_cred, fp);
			if (error) {
				fuse_brelse(bp);
				break;
			}
		}

		fip = fuse_ipc_get(fmp, sizeof(*fri));
		fri = fuse_ipc_fill(fip, FUSE_READ, fnp->ino, ap->a_cred);
		fri->offset = bp->b_loffset;
		fri->size = bp->b_bcount;
		if (need_reopen)
			fri->fh = fuse_nfh(VTOI(vp));
		else
			fri->fh = fh;

		fuse_dbg("fuse_read_in offset=%ju size=%u fh=%jx\n",
		    fri->offset, fri->size, fri->fh);

		error = fuse_ipc_tx(fip);
		if (error) {
			fuse_brelse(bp);
			break;
		}
		memcpy(bp->b_data, fuse_out_data(fip), fuse_out_data_size(fip));
		fuse_ipc_put(fip);

		if (need_reopen) {
			error = fdrop(fp); /* calls VOP_CLOSE() */
			if (error) {
				fuse_brelse(bp);
				break;
			}
		}
skip:
		len = FUSE_BLKSIZE - buf_offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		if (uio->uio_offset + len > fnp->size)
			len = (size_t)(fnp->size - uio->uio_offset);
		fuse_dbg("size=%ju len=%ju\n", fnp->size, len);

		error = uiomovebp(bp, bp->b_data + buf_offset, len, uio);
		bqrelse(bp);
		if (error)
			break;
	}

	fuse_dbg("uio_offset=%ju uio_resid=%ju error=%d done\n",
	    uio->uio_offset, uio->uio_resid, error);

	return error;
}

int
fuse_write(struct vop_write_args *ap)
{
	return fuse_dio_write(ap);
}

int
fuse_dio_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_mount *fmp = VFSTOFUSE(vp->v_mount);
	struct fuse_node *fnp = VTOI(vp);
	bool need_reopen = !curproc || fnp->closed; /* XXX */
	int kflags = 0;
	int error = 0;

	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = fnp->size;

	while (uio->uio_resid > 0) {
		struct file *fp;
		struct buf *bp;
		struct fuse_ipc *fip;
		struct fuse_read_in *fri;
		struct fuse_write_in *fwi;
		struct fuse_write_out *fwo;
		off_t base_offset, buf_offset;
		size_t len, oldsize;
		uint64_t fh;
		bool fixsize = false;
		bool need_read = false;

		fh = fuse_nfh(VTOI(vp));
		if (ap->a_fp)
			fh = fuse_fh(ap->a_fp);

		buf_offset = (off_t)uio->uio_offset & FUSE_BLKMASK64;
		base_offset = (off_t)uio->uio_offset - buf_offset;

		fuse_dbg("uio_offset=%ju uio_resid=%ju base_offset=%ju "
		    "buf_offset=%ju\n",
		    uio->uio_offset, uio->uio_resid, base_offset, buf_offset);

		oldsize = fnp->size;
		len = FUSE_BLKSIZE - buf_offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		if (uio->uio_offset + len > fnp->size) {
			/* XXX trivial flag */
			error = fuse_node_truncate(fnp, fnp->size,
			    uio->uio_offset + len);
			if (error)
				break;
			fixsize = true;
			kflags |= NOTE_EXTEND;
		}
		fuse_dbg("size=%ju len=%ju\n", fnp->size, len);

		bp = NULL;
		if (uio->uio_segflg == UIO_NOCOPY) {
			bp = getblk(ap->a_vp, base_offset, FUSE_BLKSIZE,
			    GETBLK_BHEAVY, 0);
			if (!(bp->b_flags & B_CACHE)) {
				bqrelse(bp);
				need_read = true;
			}
		} else if (!buf_offset && uio->uio_resid >= FUSE_BLKSIZE) {
			bp = getblk(ap->a_vp, base_offset, FUSE_BLKSIZE,
			    GETBLK_BHEAVY, 0);
			if (!(bp->b_flags & B_CACHE))
				vfs_bio_clrbuf(bp);
		} else if (base_offset >= fnp->size) {
			bp = getblk(ap->a_vp, base_offset, FUSE_BLKSIZE,
			    GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			need_read = true;
		}

		if (bp)
			fuse_dbg("b_loffset=%ju b_bcount=%d b_flags=%x\n",
			    bp->b_loffset, bp->b_bcount, bp->b_flags);

		if (need_reopen) {
			error = falloc(NULL, &fp, NULL);
			if (error) {
				fuse_brelse(bp);
				fuse_fix_size(fnp, fixsize, oldsize);
				break;
			}
			/* XXX can panic at vref() in vop_stdopen() */
			error = VOP_OPEN(vp, FREAD | FWRITE, ap->a_cred, fp);
			if (error) {
				fuse_brelse(bp);
				fuse_fix_size(fnp, fixsize, oldsize);
				break;
			}
		}

		if (need_read) {
			error = bread(ap->a_vp, base_offset, FUSE_BLKSIZE, &bp);
			KKASSERT(!error);

			fuse_dbg("b_loffset=%ju b_bcount=%d b_flags=%x\n",
			    bp->b_loffset, bp->b_bcount, bp->b_flags);

			if (bp->b_loffset + (buf_offset + len) > oldsize) {
				memset(bp->b_data, 0, FUSE_BLKSIZE); /* XXX */
				goto skip; /* prevent EBADF */
			}

			fip = fuse_ipc_get(fmp, sizeof(*fri));
			fri = fuse_ipc_fill(fip, FUSE_READ, fnp->ino,
			    ap->a_cred);
			fri->offset = bp->b_loffset;
			fri->size = buf_offset + len;
			if (need_reopen)
				fri->fh = fuse_nfh(VTOI(vp));
			else
				fri->fh = fh;

			fuse_dbg("fuse_read_in offset=%ju size=%u fh=%jx\n",
			    fri->offset, fri->size, fri->fh);

			error = fuse_ipc_tx(fip);
			if (error) {
				fuse_brelse(bp);
				fuse_fix_size(fnp, fixsize, oldsize);
				break;
			}
			memcpy(bp->b_data, fuse_out_data(fip),
			    fuse_out_data_size(fip));
			fuse_ipc_put(fip);
		}
skip:
		error = uiomovebp(bp, bp->b_data + buf_offset, len, uio);
		if (error) {
			bqrelse(bp);
			fuse_fix_size(fnp, fixsize, oldsize);
			break;
		}
		kflags |= NOTE_WRITE;

		fip = fuse_ipc_get(fmp, sizeof(*fwi) + len);
		fwi = fuse_ipc_fill(fip, FUSE_WRITE, fnp->ino, ap->a_cred);
		fwi->offset = bp->b_loffset + buf_offset;
		fwi->size = len;
		if (need_reopen)
			fwi->fh = fuse_nfh(VTOI(vp));
		else
			fwi->fh = fh;
		memcpy((void*)(fwi + 1), bp->b_data + buf_offset, len);

		fuse_dbg("fuse_write_in offset=%ju size=%u fh=%jx\n",
		    fwi->offset, fwi->size, fwi->fh);

		error = fuse_ipc_tx(fip);
		if (error) {
			fuse_brelse(bp);
			fuse_fix_size(fnp, fixsize, oldsize);
			break;
		}
		fwo = fuse_out_data(fip);
		if (fwo->size != len) {
			fuse_ipc_put(fip);
			fuse_brelse(bp);
			fuse_fix_size(fnp, fixsize, oldsize);
			break;
		}
		fuse_ipc_put(fip);

		if (need_reopen) {
			error = fdrop(fp); /* calls VOP_CLOSE() */
			if (error) {
				fuse_brelse(bp);
				fuse_fix_size(fnp, fixsize, oldsize);
				break;
			}
		}

		error = bwrite(bp);
		KKASSERT(!error);
	}

	fuse_knote(ap->a_vp, kflags);

	fuse_dbg("uio_offset=%ju uio_resid=%ju error=%d done\n",
	    uio->uio_offset, uio->uio_resid, error);

	return error;
}
