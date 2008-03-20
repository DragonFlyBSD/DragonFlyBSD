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
 * $DragonFly: src/sys/dev/virtual/disk/vdisk.c,v 1.8 2008/03/20 02:14:56 dillon Exp $
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
#include <machine/cothread.h>
#include <machine/md_var.h>

#include <sys/buf2.h>

#include <sys/stat.h>
#include <unistd.h>

struct vkd_softc {
	struct bio_queue_head bio_queue;
	struct devstat stats;
	struct disk disk;
	cothread_t	cotd;
	TAILQ_HEAD(, bio) cotd_queue;
	TAILQ_HEAD(, bio) cotd_done;
	cdev_t dev;
	int unit;
	int fd;
};

#define CDEV_MAJOR	97

static void vkd_io_thread(cothread_t cotd);
static void vkd_io_intr(cothread_t cotd);
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
		bioq_init(&sc->bio_queue);
		devstat_add_entry(&sc->stats, "vkd", sc->unit, DEV_BSIZE,
				  DEVSTAT_NO_ORDERED_TAGS,
				  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
				  DEVSTAT_PRIORITY_DISK);
		sc->dev = disk_create(sc->unit, &sc->disk, &vkd_ops);
		sc->dev->si_drv1 = sc;
		sc->dev->si_iosize_max = 256 * 1024;

		TAILQ_INIT(&sc->cotd_queue);
		TAILQ_INIT(&sc->cotd_done);
		sc->cotd = cothread_create(vkd_io_thread, vkd_io_intr, sc, 
					   "vkd");
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

	devstat_start_transaction(&sc->stats);
	cothread_lock(sc->cotd);
	TAILQ_INSERT_TAIL(&sc->cotd_queue, bio, bio_act);
	cothread_signal(sc->cotd);
	cothread_unlock(sc->cotd);

	return(0);
}

static
void
vkd_io_intr(cothread_t cotd)
{
	struct vkd_softc *sc;
	struct bio *bio;

	sc = cotd->arg;

	cothread_lock(cotd);
	while (!TAILQ_EMPTY(&sc->cotd_done)) {
		bio = TAILQ_FIRST(&sc->cotd_done);
		TAILQ_REMOVE(&sc->cotd_done, bio, bio_act);
		devstat_end_transaction_buf(&sc->stats, bio->bio_buf);
		biodone(bio);
	}
	cothread_unlock(sc->cotd);
}

/*
 * WARNING!  This runs as a cothread and has no access to mycpu nor can it
 * make vkernel-specific calls other then cothread_*() calls.
 */
static
void
vkd_io_thread(cothread_t cotd)
{
	struct bio *bio;
	struct vkd_softc *sc = cotd->arg;
	int count;

	cothread_lock(cotd);
	for (;;) {
		cothread_wait(cotd);	/* interlocks cothread lock */
		count = 0;
		while ((bio = TAILQ_FIRST(&sc->cotd_queue)) != NULL) {
			TAILQ_REMOVE(&sc->cotd_queue, bio, bio_act);
			cothread_unlock(cotd);
			vkd_doio(sc, bio);
			cothread_lock(cotd);
			TAILQ_INSERT_TAIL(&sc->cotd_done, bio, bio_act);
			if (++count == 8) {
				cothread_intr(cotd);
				count = 0;
			}
		}
		if (count)
			cothread_intr(cotd);
	}
	/* NOT REACHED */
	cothread_unlock(cotd);
}

static
void
vkd_doio(struct vkd_softc *sc, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	int n;

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
}

