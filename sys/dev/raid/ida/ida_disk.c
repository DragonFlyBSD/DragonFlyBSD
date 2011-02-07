/*-
 * Copyright (c) 1999,2000 Jonathan Lemon
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
 * $FreeBSD: src/sys/dev/ida/ida_disk.c,v 1.12.2.6 2001/11/27 20:21:02 ps Exp $
 */

/*
 * Disk driver for Compaq SMART RAID adapters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/dtype.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <machine/clock.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "idareg.h"
#include "idavar.h"

/* prototypes */
static int idad_probe(device_t dev);
static int idad_attach(device_t dev);
static int idad_detach(device_t dev);

static	d_open_t	idad_open;
static	d_close_t	idad_close;
static	d_strategy_t	idad_strategy;
static	d_dump_t	idad_dump;

static struct dev_ops id_ops = {
	{ "idad", 0, D_DISK },
	.d_open =	idad_open,
	.d_close =	idad_close,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_strategy =	idad_strategy,
	.d_dump =	idad_dump,
};

static devclass_t	idad_devclass;

static device_method_t idad_methods[] = {
	DEVMETHOD(device_probe,		idad_probe),
	DEVMETHOD(device_attach,	idad_attach),
	DEVMETHOD(device_detach,	idad_detach),
	{ 0, 0 }
};

static driver_t idad_driver = {
	"idad",
	idad_methods,
	sizeof(struct idad_softc)
};

DRIVER_MODULE(idad, ida, idad_driver, idad_devclass, 0, 0);

static __inline struct idad_softc *
idad_getsoftc(cdev_t dev)
{

	return ((struct idad_softc *)dev->si_drv1);
}

static int
idad_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct idad_softc *drv;

	drv = idad_getsoftc(dev);
	if (drv == NULL)
		return (ENXIO);
#if 0
	bzero(&info, sizeof(info));
	info.d_media_blksize = drv->secsize;		/* mandatory */
	info.d_media_blocks = drv->secperunit;

	info.d_secpertrack = drv->sectors;		/* optional */
	info.d_type = DTYPE_SCSI;
	info.d_nheads = drv->heads;
	info.d_ncylinders = drv->cylinders;
	info.d_secpercyl = drv->sectors * drv->heads;

	disk_setdiskinfo(&drv->disk, &info);
#endif
	return (0);
}

static int
idad_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct idad_softc *drv;

	drv = idad_getsoftc(dev);
	if (drv == NULL)
		return (ENXIO);
	return (0);
}

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static int
idad_strategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct idad_softc *drv;

	drv = idad_getsoftc(dev);
	if (drv == NULL) {
    		bp->b_error = EINVAL;
		goto bad;
	}

	/*
	 * software write protect check
	 */
	if ((drv->flags & DRV_WRITEPROT) && bp->b_cmd != BUF_CMD_READ) {
		bp->b_error = EROFS;
		goto bad;
	}

	/*
	 * If it's a null transfer, return immediately
	 */
	if (bp->b_bcount == 0)
		goto done;

	bio->bio_driver_info = drv;
	crit_enter();
	devstat_start_transaction(&drv->stats);
	ida_submit_buf(drv->controller, bio);
	crit_exit();
	return(0);

bad:
	bp->b_flags |= B_ERROR;

done:
	/*
	 * Correctly set the buf to indicate a completed transfer
	 */
	bp->b_resid = bp->b_bcount;
	biodone(bio);
	return(0);
}

static int
idad_dump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct idad_softc *drv;
	int error = 0;

	drv = idad_getsoftc(dev);
	if (drv == NULL)
		return (ENXIO);

	drv->controller->flags &= ~IDA_INTERRUPTS;

	if (ap->a_length > 0) {
		error = ida_command(drv->controller, CMD_WRITE, ap->a_virtual,
		    ap->a_length, drv->drive, ap->a_offset / DEV_BSIZE, DMA_DATA_OUT);
	}
	drv->controller->flags |= IDA_INTERRUPTS;
	return (error);

}

void
idad_intr(struct bio *bio)
{
	struct idad_softc *drv = (struct idad_softc *)bio->bio_driver_info;
	struct buf *bp = bio->bio_buf;

	if (bp->b_flags & B_ERROR)
		bp->b_error = EIO;
	else
		bp->b_resid = 0;

	devstat_end_transaction_buf(&drv->stats, bp);
	biodone(bio);
}

static int
idad_probe(device_t dev)
{

	device_set_desc(dev, "Compaq Logical Drive");
	return (0);
}

static int
idad_attach(device_t dev)
{
	struct ida_drive_info dinfo;
	struct disk_info info;
	struct idad_softc *drv;
	device_t parent;
	cdev_t dsk;
	int error;

	drv = (struct idad_softc *)device_get_softc(dev);
	parent = device_get_parent(dev);
	drv->controller = (struct ida_softc *)device_get_softc(parent);
	drv->unit = device_get_unit(dev);
	drv->drive = drv->controller->num_drives;
	drv->controller->num_drives++;

	error = ida_command(drv->controller, CMD_GET_LOG_DRV_INFO,
	    &dinfo, sizeof(dinfo), drv->drive, 0, DMA_DATA_IN);
	if (error) {
		device_printf(dev, "CMD_GET_LOG_DRV_INFO failed\n");
		return (ENXIO);
	}

	drv->cylinders = dinfo.ncylinders;
	drv->heads = dinfo.nheads;
	drv->sectors = dinfo.nsectors;
	drv->secsize = dinfo.secsize == 0 ? 512 : dinfo.secsize;
	drv->secperunit = dinfo.secperunit;

	/* XXX
	 * other initialization
	 */
	device_printf(dev, "%uMB (%u sectors), blocksize=%d\n",
	    drv->secperunit / ((1024 * 1024) / drv->secsize),
	    drv->secperunit, drv->secsize);

	devstat_add_entry(&drv->stats, "idad", drv->unit, drv->secsize,
	    DEVSTAT_NO_ORDERED_TAGS,
	    DEVSTAT_TYPE_STORARRAY| DEVSTAT_TYPE_IF_OTHER,
	    DEVSTAT_PRIORITY_ARRAY);

	dsk = disk_create(drv->unit, &drv->disk, &id_ops);

	dsk->si_drv1 = drv;
	dsk->si_iosize_max = DFLTPHYS;		/* XXX guess? */

	/*
	 * Set disk info, as it appears that all needed data is available already.
	 * Setting the disk info will also cause the probing to start.
	 */
	bzero(&info, sizeof(info));
	info.d_media_blksize = drv->secsize;		/* mandatory */
	info.d_media_blocks = drv->secperunit;

	info.d_secpertrack = drv->sectors;		/* optional */
	info.d_type = DTYPE_SCSI;
	info.d_nheads = drv->heads;
	info.d_ncylinders = drv->cylinders;
	info.d_secpercyl = drv->sectors * drv->heads;

	disk_setdiskinfo(&drv->disk, &info);

	return (0);
}

static int
idad_detach(device_t dev)
{
	struct idad_softc *drv;

	drv = (struct idad_softc *)device_get_softc(dev);
	devstat_remove_entry(&drv->stats);
	disk_destroy(&drv->disk);
	return (0);
}
