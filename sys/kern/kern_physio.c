/*
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * $FreeBSD: src/sys/kern/kern_physio.c,v 1.46.2.4 2003/11/14 09:51:47 simokawa Exp $
 * $DragonFly: src/sys/kern/kern_physio.c,v 1.19 2006/04/30 20:23:23 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/device.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

static void
physwakeup(struct bio *bio)
{
	bio->bio_buf->b_cmd = BUF_CMD_DONE;
	wakeup(bio);
}

int
physio(dev_t dev, struct uio *uio, int ioflag)
{
	int i;
	int error;
	int chk_blockno;
	int saflags;
	int iolen;
	int bcount;
	struct buf *bp;

	bp = getpbuf(NULL);
	saflags = bp->b_flags;
	error = 0;

	/* XXX: sanity check */
	if(dev->si_iosize_max < PAGE_SIZE) {
		printf("WARNING: %s si_iosize_max=%d, using DFLTPHYS.\n",
		    devtoname(dev), dev->si_iosize_max);
		dev->si_iosize_max = DFLTPHYS;
	}

	/* Don't check block number overflow for D_MEM */
	if ((dev_dflags(dev) & D_TYPEMASK) == D_MEM)
		chk_blockno = 0;
	else
		chk_blockno = 1;

	for (i = 0; i < uio->uio_iovcnt; i++) {
		while (uio->uio_iov[i].iov_len) {
			if (uio->uio_rw == UIO_READ)
				bp->b_cmd = BUF_CMD_READ;
			else 
				bp->b_cmd = BUF_CMD_WRITE;
			bp->b_flags = saflags;
			bcount = uio->uio_iov[i].iov_len;

			reinitbufbio(bp);	/* clear translation cache */
			bp->b_bio1.bio_offset = uio->uio_offset;
			bp->b_bio1.bio_done = physwakeup;

			/* Don't exceed drivers iosize limit */
			if (bcount > dev->si_iosize_max)
				bcount = dev->si_iosize_max;

			/* 
			 * Make sure the pbuf can map the request
			 * XXX: The pbuf has kvasize = MAXPHYS so a request
			 * XXX: larger than MAXPHYS - PAGE_SIZE must be
			 * XXX: page aligned or it will be fragmented.
			 */
			iolen = ((vm_offset_t) bp->b_data) & PAGE_MASK;
			if ((bcount + iolen) > bp->b_kvasize) {
				bcount = bp->b_kvasize;
				if (iolen != 0)
					bcount -= PAGE_SIZE;
			}
			if (uio->uio_segflg == UIO_USERSPACE) {
				if (vmapbuf(bp, uio->uio_iov[i].iov_base, bcount) < 0) {
					error = EFAULT;
					goto doerror;
				}
			} else {
				bp->b_data = uio->uio_iov[i].iov_base;
				bp->b_bcount = bcount;
				bp->b_bufsize = bcount;
			}
			dev_dstrategy(dev, &bp->b_bio1);
			crit_enter();
			while (bp->b_cmd != BUF_CMD_DONE)
				tsleep(&bp->b_bio1, 0, "physstr", 0);
			crit_exit();

			if (uio->uio_segflg == UIO_USERSPACE)
				vunmapbuf(bp);
			iolen = bp->b_bcount - bp->b_resid;
			if (iolen == 0 && !(bp->b_flags & B_ERROR))
				goto doerror;	/* EOF */
			uio->uio_iov[i].iov_len -= iolen;
			uio->uio_iov[i].iov_base += iolen;
			uio->uio_resid -= iolen;
			uio->uio_offset += iolen;
			if( bp->b_flags & B_ERROR) {
				error = bp->b_error;
				goto doerror;
			}
		}
	}
doerror:
	relpbuf(bp, NULL);
	return (error);
}
