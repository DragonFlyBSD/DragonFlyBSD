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
 * $DragonFly: src/sys/kern/kern_physio.c,v 1.27 2008/08/22 08:47:56 swildner Exp $
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

static int
physio(cdev_t dev, struct uio *uio, int ioflag)
{
	int i;
	int error;
	int saflags;
	int iolen;
	int bcount;
	int bounceit;
	caddr_t ubase;
	struct buf *bp;

	bp = getpbuf_kva(NULL);
	saflags = bp->b_flags;
	error = 0;

	/* XXX: sanity check */
	if (dev->si_iosize_max < PAGE_SIZE) {
		kprintf("WARNING: %s si_iosize_max=%d, using MAXPHYS.\n",
		    devtoname(dev), dev->si_iosize_max);
		dev->si_iosize_max = MAXPHYS;
	}

	/* Must be a real uio */
	KKASSERT(uio->uio_segflg != UIO_NOCOPY);

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
			bp->b_bio1.bio_done = biodone_sync;
			bp->b_bio1.bio_flags |= BIO_SYNC;

			/* 
			 * Setup for mapping the request into kernel memory.
			 *
			 * We can only write as much as fits in a pbuf,
			 * which is MAXPHYS, and no larger then the device's
			 * ability.
			 *
			 * If not using bounce pages the base address of the
			 * user mapping into the pbuf may be offset, further
			 * reducing how much will actually fit in the pbuf.
			 */
			if (bcount > dev->si_iosize_max)
				bcount = dev->si_iosize_max;

			ubase = uio->uio_iov[i].iov_base;
			bounceit = (int)(((vm_offset_t)ubase) & 15);
			iolen = ((vm_offset_t)ubase) & PAGE_MASK;
			if (bounceit) {
				if (bcount > bp->b_kvasize)
					bcount = bp->b_kvasize;
			} else {
				if ((bcount + iolen) > bp->b_kvasize) {
					bcount = bp->b_kvasize;
					if (iolen != 0)
						bcount -= PAGE_SIZE;
				}
			}

			/*
			 * If we have to use a bounce buffer allocate kernel
			 * memory and copyin/copyout.  Otherwise map the
			 * user buffer directly into kernel memory without
			 * copying.
			 */
			if (uio->uio_segflg == UIO_USERSPACE) {
				if (bounceit) {
					bp->b_data = bp->b_kvabase;
					bp->b_bcount = bcount;
					vm_hold_load_pages(bp, (vm_offset_t)bp->b_data, (vm_offset_t)bp->b_data + bcount);
					if (uio->uio_rw == UIO_WRITE) {
						error = copyin(ubase, bp->b_data, bcount);
						if (error) {
							vm_hold_free_pages(bp, (vm_offset_t)bp->b_data, (vm_offset_t)bp->b_data + bcount);
							goto doerror;
						}
					}
				} else if (vmapbuf(bp, ubase, bcount) < 0) {
					error = EFAULT;
					goto doerror;
				}
			} else {
				bp->b_data = uio->uio_iov[i].iov_base;
				bp->b_bcount = bcount;
			}
			dev_dstrategy(dev, &bp->b_bio1);
			biowait(&bp->b_bio1, "physstr");

			iolen = bp->b_bcount - bp->b_resid;
			if (uio->uio_segflg == UIO_USERSPACE) {
				if (bounceit) {
					if (uio->uio_rw == UIO_READ && iolen) {
						error = copyout(bp->b_data, ubase, iolen);
						if (error) {
							bp->b_flags |= B_ERROR;
							bp->b_error = error;
						}
					}
					vm_hold_free_pages(bp, (vm_offset_t)bp->b_data, (vm_offset_t)bp->b_data + bcount);
				} else {
					vunmapbuf(bp);
				}
			}
			if (iolen == 0 && !(bp->b_flags & B_ERROR))
				goto doerror;	/* EOF */
			uio->uio_iov[i].iov_len -= iolen;
			uio->uio_iov[i].iov_base = (char *)uio->uio_iov[i].iov_base + iolen;
			uio->uio_resid -= iolen;
			uio->uio_offset += iolen;
			if (bp->b_flags & B_ERROR) {
				error = bp->b_error;
				goto doerror;
			}
		}
	}
doerror:
	relpbuf(bp, NULL);
	return (error);
}

int
physread(struct dev_read_args *ap)
{
	return(physio(ap->a_head.a_dev, ap->a_uio, ap->a_ioflag));
}

int
physwrite(struct dev_write_args *ap)
{
	return(physio(ap->a_head.a_dev, ap->a_uio, ap->a_ioflag));
}
