/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/dev/virtual/disk/vdisk.c,v 1.3 2007/03/16 13:41:40 swildner Exp $
 */

/*
 * Virtual disk driver
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <machine/md_var.h>

#include <sys/buf2.h>

#include <sys/stat.h>
#include <unistd.h>

struct vkd_softc {
	struct bio_queue_head bio_queue;
	struct devstat stats;
	struct disk disk;
	cdev_t dev;
	int unit;
	int fd;
};

#define CDEV_MAJOR	97

static d_strategy_t	vkdstrategy;
static d_open_t		vkdopen;

static struct dev_ops vkd_ops = {
	{ "vkd", CDEV_MAJOR, D_DISK },
        .d_open =	vkdopen,
        .d_close =	nullclose,
        .d_read =	physread,
        .d_write =	physwrite,
        .d_strategy =	vkdstrategy,
};

static void
vkdinit(void *dummy __unused)
{
	struct vkdisk_info *dsk;
	struct vkd_softc *sc;
	struct stat st;
	int i;

	KASSERT(DiskNum <= VKDISK_MAX, ("too many disks: %d\n", DiskNum));
	
	for (i = 0; i < DiskNum; i++) {

		/* check that the 'bus device' has been initialized */
		dsk = &DiskInfo[i];
		if (dsk == NULL)
			continue;
		if (dsk->fd < 0 || fstat(dsk->fd, &st) < 0)
			continue;

		/* and create a new device */
		sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
		sc->unit = dsk->unit;
		sc->fd = dsk->fd;
		bioq_init(&sc->bio_queue);
		devstat_add_entry(&sc->stats, "vkd", sc->unit, DEV_BSIZE,
				  DEVSTAT_NO_ORDERED_TAGS,
				  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
				  DEVSTAT_PRIORITY_DISK);
		sc->dev = disk_create(sc->unit, &sc->disk, 0, &vkd_ops);
		sc->dev->si_drv1 = sc;
		sc->dev->si_iosize_max = 256 * 1024;

#if 0
		dl = &sc->disk.d_label;
		bzero(dl, sizeof(*dl));
		dl->d_secsize = DEV_BSIZE;
		dl->d_nsectors = st.st_size / dl->d_secsize;
		dl->d_ntracks = 1;
		dl->d_secpercyl = dl->d_nsectors;
		dl->d_ncylinders = 1;
#endif

	}
}

SYSINIT(vkdisk, SI_SUB_DRIVERS, SI_ORDER_FIRST, vkdinit, NULL);

static int
vkdopen(struct dev_open_args *ap)
{
	struct vkd_softc *sc;
	struct disklabel *dl;
	struct stat st;
	cdev_t dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	if (fstat(sc->fd, &st) < 0 || st.st_size == 0)
		return(ENXIO);

	dl = &sc->disk.d_label;
	bzero(dl, sizeof(*dl));
	dl->d_secsize = DEV_BSIZE;
	dl->d_nsectors = st.st_size / dl->d_secsize;
	dl->d_ntracks = 1;
	dl->d_secpercyl = dl->d_nsectors;
	dl->d_ncylinders = 1;
	dl->d_secperunit = dl->d_nsectors;
	return(0);
}

static int
vkdstrategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp;
	struct vkd_softc *sc;
	cdev_t dev;
	int n;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	bioqdisksort(&sc->bio_queue, bio);
	while ((bio = bioq_first(&sc->bio_queue)) != NULL) {
		bioq_remove(&sc->bio_queue, bio);
		bp = bio->bio_buf;

		devstat_start_transaction(&sc->stats);

		switch(bp->b_cmd) {
		case BUF_CMD_READ:
			lseek(sc->fd, bio->bio_offset, 0);
			n = read(sc->fd, bp->b_data, bp->b_bcount);
			break;
		case BUF_CMD_WRITE:
			/* XXX HANDLE SHORT WRITE XXX */
			lseek(sc->fd, bio->bio_offset, 0);
			n = write(sc->fd, bp->b_data, bp->b_bcount);
			break;
		default:
			panic("vkd: bad b_cmd %d", bp->b_cmd);
			break; /* not reached */
		}
		if (n != bp->b_bcount) {
			bp->b_error = EIO;
			bp->b_flags |= B_ERROR;
		}
			
		bp->b_resid = bp->b_bcount - n;
		devstat_end_transaction_buf(&sc->stats, bp);
		biodone(bio);
	}
	return(0);
}

