/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2022 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hammer2.h"
#include "makefs.h"

struct m_buf *
getblkx(struct m_vnode *vp, off_t loffset, int size, int blkflags, int slptimeo)
{
	struct m_buf *bp;
	makefs_daddr_t blkno = loffset / DEV_BSIZE;

	bp = getblk(vp, blkno, size, 0, 0, 0);
	assert(bp);
	assert(bp->b_data);

	bp->b_loffset = loffset;

	return (bp);
}

int
breadx(struct m_vnode *vp, off_t loffset, int size, struct m_buf **bpp)
{
	struct m_buf *bp;
	ssize_t ret;

	assert(bpp != NULL);

	bp = getblkx(vp, loffset, size, 0, 0);
	if (bp->b_fs == NULL)
		errx(1, "buf %p for vp %p has no makefs_fsinfo", bp, vp);

	assert(bp->b_blkno * DEV_BSIZE == bp->b_loffset);
	assert(bp->b_bcount == size);
	assert(bp->b_vp);
	assert(!bp->b_vp->v_logical);
	*bpp = bp;

	if (lseek(bp->b_fs->fd, bp->b_loffset, SEEK_SET) == -1)
		err(1, "%s: lseek vp %p offset 0x%016jx",
			__func__, vp, (intmax_t)bp->b_loffset);

	ret = read(bp->b_fs->fd, bp->b_data, bp->b_bcount);
	if (debug & DEBUG_BUF_BREAD)
		printf("%s: read vp %p offset 0x%016jx size 0x%jx -> 0x%jx\n",
			__func__, vp, (intmax_t)bp->b_loffset, bp->b_bcount, ret);

	if (ret == -1) {
		err(1, "%s: read vp %p offset 0x%016jx size 0x%jx",
			__func__, vp, (intmax_t)bp->b_loffset, bp->b_bcount);
	} else if (ret != bp->b_bcount) {
		if (debug)
			printf("%s: read vp %p offset 0x%016jx size 0x%jx -> "
				"0x%jx != 0x%jx\n",
				__func__, vp, (intmax_t)bp->b_loffset, bp->b_bcount, ret,
				bp->b_bcount);
		return (EINVAL);
	}

	return (0);
}

int
bread_kvabio(struct m_vnode *vp, off_t loffset, int size, struct m_buf **bpp)
{
	return (breadx(vp, loffset, size, bpp));
}

void
bqrelse(struct m_buf *bp)
{
	brelse(bp);
}

int
bawrite(struct m_buf *bp)
{
	return (bwrite(bp));
}

static int
uiomove(caddr_t cp, size_t n, struct uio *uio)
{
	struct iovec *iov;
	size_t cnt;
	int error = 0;

	KASSERT(uio->uio_rw == UIO_READ || uio->uio_rw == UIO_WRITE,
	    ("uiomove: mode"));

	while (n > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (cnt > n)
			cnt = n;

		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
			/* emulate copyout/copyin */
			if (uio->uio_rw == UIO_READ)
				bcopy(cp, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, cp, cnt);
			break;
		case UIO_SYSSPACE:
			if (uio->uio_rw == UIO_READ)
				bcopy(cp, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, cp, cnt);
			break;
		case UIO_NOCOPY:
			assert(0); /* no UIO_NOCOPY in makefs */
			break;
		}

		if (error)
			break;
		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		cp += cnt;
		n -= cnt;
	}

	return (error);
}

int
uiomovebp(struct m_buf *bp, caddr_t cp, size_t n, struct uio *uio)
{
	return (uiomove(cp, n, uio));
}
