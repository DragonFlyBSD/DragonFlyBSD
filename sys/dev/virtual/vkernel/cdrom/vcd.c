/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 */

/*
 * Virtual CDROM driver
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <machine/md_var.h>

#include <sys/buf2.h>

#include <sys/stat.h>
#include <unistd.h>

struct vcd_softc {
	struct bio_queue_head bio_queue;
	struct devstat stats;
	struct disk disk;
	cdev_t dev;
	int unit;
	int fd;
};

static d_strategy_t	vcdstrategy;
static d_open_t		vcdopen;

static struct dev_ops vcd_ops = {
	{ "vcd", 0, D_DISK },
        .d_open =	vcdopen,
        .d_close =	nullclose,
        .d_read =	physread,
        .d_write =	physwrite,
        .d_strategy =	vcdstrategy,
};

static void
vcdinit(void *dummy __unused)
{
	struct vkdisk_info *dsk;
	struct vcd_softc *sc;
	struct disk_info info;
	struct stat st;
	int i;

	for (i = 0; i < DiskNum; i++) {
		/* check that the 'bus device' has been initialized */
		dsk = &DiskInfo[i];
		if (dsk == NULL || dsk->type != VKD_CD)
			continue;
		if (dsk->fd < 0 || fstat(dsk->fd, &st) < 0)
			continue;

		/* and create a new device */
		sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
		sc->unit = dsk->unit;
		sc->fd = dsk->fd;
		bioq_init(&sc->bio_queue);
		devstat_add_entry(&sc->stats, "vcd", sc->unit, 2048,
				  DEVSTAT_NO_ORDERED_TAGS,
				  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
				  DEVSTAT_PRIORITY_DISK);
		sc->dev = disk_create(sc->unit, &sc->disk, &vcd_ops);
		sc->dev->si_drv1 = sc;
		sc->dev->si_iosize_max = 256 * 1024;

		bzero(&info, sizeof(info));
		info.d_media_blksize = 2048;
		info.d_media_blocks = st.st_size / info.d_media_blksize;
		info.d_dsflags = DSO_ONESLICE | DSO_COMPATLABEL | DSO_COMPATPARTA |
		    DSO_RAWEXTENSIONS;
		info.d_nheads = 1;
		info.d_ncylinders = 1;
		info.d_secpertrack = info.d_media_blocks;
		info.d_secpercyl = info.d_secpertrack * info.d_nheads;

		disk_setdiskinfo(&sc->disk, &info);
	}
}

SYSINIT(vcdisk, SI_SUB_DRIVERS, SI_ORDER_FIRST, vcdinit, NULL);

static int
vcdopen(struct dev_open_args *ap)
{
	struct vcd_softc *sc;
	struct stat st;
	cdev_t dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	if (fstat(sc->fd, &st) < 0 || st.st_size == 0)
		return(ENXIO);

	return(0);
}

static int
vcdstrategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp;
	struct vcd_softc *sc;
	cdev_t dev;
	int n;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	bioqdisksort(&sc->bio_queue, bio);
	while ((bio = bioq_takefirst(&sc->bio_queue)) != NULL) {
		bp = bio->bio_buf;

		devstat_start_transaction(&sc->stats);

		switch(bp->b_cmd) {
		case BUF_CMD_READ:
			n = pread(sc->fd, bp->b_data,
				  bp->b_bcount, bio->bio_offset);
			break;
		case BUF_CMD_WRITE:
			/* XXX HANDLE SHORT WRITE XXX */
			n = pwrite(sc->fd, bp->b_data,
				   bp->b_bcount, bio->bio_offset);
			break;
		default:
			panic("vcd: bad b_cmd %d", bp->b_cmd);
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
