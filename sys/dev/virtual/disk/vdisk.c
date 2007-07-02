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
 * $DragonFly: src/sys/dev/virtual/disk/vdisk.c,v 1.7 2007/07/02 17:15:10 dillon Exp $
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
#include <sys/bus.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <machine/md_var.h>

#include <sys/buf2.h>

#include <sys/stat.h>
#include <unistd.h>

struct vkd_softc {
	struct bio_queue_head bio_queue;
	struct devstat stats;
	struct disk disk;
	struct spinlock spin;
	thread_t iotd;		/* dedicated io thread */
	cdev_t dev;
	int unit;
	int fd;
};

#define CDEV_MAJOR	97

static void vkd_io_thread(void *arg);
static void vkd_doio(struct vkd_softc *sc, struct bio *bio);

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

	for (i = 0; i < DiskNum; i++) {
		/* check that the 'bus device' has been initialized */
		dsk = &DiskInfo[i];
		if (dsk == NULL || dsk->type != VKD_DISK)
			continue;
		if (dsk->fd < 0 || fstat(dsk->fd, &st) < 0)
			continue;

		/* and create a new device */
		sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
		sc->unit = dsk->unit;
		sc->fd = dsk->fd;
		spin_init(&sc->spin);
		bioq_init(&sc->bio_queue);
		devstat_add_entry(&sc->stats, "vkd", sc->unit, DEV_BSIZE,
				  DEVSTAT_NO_ORDERED_TAGS,
				  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
				  DEVSTAT_PRIORITY_DISK);
		sc->dev = disk_create(sc->unit, &sc->disk, &vkd_ops);
		sc->dev->si_drv1 = sc;
		sc->dev->si_iosize_max = 256 * 1024;
		if (ncpus > 2) {
			int xcpu = ncpus - 1;
			lwkt_create(vkd_io_thread, sc, &sc->iotd, NULL, 
				    0, xcpu, "vkd%d-io", sc->unit);
			usched_mastermask &= ~(1 << xcpu);
		}
	}
}

SYSINIT(vkdisk, SI_SUB_DRIVERS, SI_ORDER_FIRST, vkdinit, NULL);

static int
vkdopen(struct dev_open_args *ap)
{
	struct vkd_softc *sc;
	struct disk_info info;
	struct stat st;
	cdev_t dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	if (fstat(sc->fd, &st) < 0 || st.st_size == 0)
		return(ENXIO);

	bzero(&info, sizeof(info));
	info.d_media_blksize = DEV_BSIZE;
	info.d_media_blocks = st.st_size / info.d_media_blksize;

	info.d_nheads = 1;
	info.d_ncylinders = 1;
	info.d_secpertrack = info.d_media_blocks;
	info.d_secpercyl = info.d_secpertrack * info.d_nheads;

	disk_setdiskinfo(&sc->disk, &info);
	return(0);
}

static int
vkdstrategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct vkd_softc *sc;
	cdev_t dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	if (sc->iotd) {
		spin_lock_wr(&sc->spin);
		bioqdisksort(&sc->bio_queue, bio);
		spin_unlock_wr(&sc->spin);
		wakeup(sc->iotd);
	} else {
		bioqdisksort(&sc->bio_queue, bio);
		while ((bio = bioq_first(&sc->bio_queue)) != NULL) {
			bioq_remove(&sc->bio_queue, bio);
			vkd_doio(sc, bio);
			biodone(bio);
		}
	}
	return(0);
}

static
void
vkd_io_thread(void *arg)
{
	struct bio *bio;
	struct vkd_softc *sc;
	int count = 0;

	rel_mplock();
	sc = arg;

	kprintf("vkd%d I/O helper on cpu %d\n", sc->unit, mycpu->gd_cpuid);

	spin_lock_wr(&sc->spin);
	for (;;) {
		while ((bio = bioq_first(&sc->bio_queue)) != NULL) {
			bioq_remove(&sc->bio_queue, bio);
			spin_unlock_wr(&sc->spin);
			vkd_doio(sc, bio);
			get_mplock();
			biodone(bio);
			rel_mplock();
			if ((++count & 3) == 0)
				lwkt_switch();
			spin_lock_wr(&sc->spin);
		}
		msleep(sc->iotd, &sc->spin, 0, "bioq", 0);
	}
	/* not reached */
	spin_unlock_wr(&sc->spin);
}

static
void
vkd_doio(struct vkd_softc *sc, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	int n;

	devstat_start_transaction(&sc->stats);

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		n = pread(sc->fd, bp->b_data, bp->b_bcount, bio->bio_offset);
		break;
	case BUF_CMD_WRITE:
		/* XXX HANDLE SHORT WRITE XXX */
		n = pwrite(sc->fd, bp->b_data, bp->b_bcount, bio->bio_offset);
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
}

