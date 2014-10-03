/*-
 * Copyright (c) 1999 Jonathan Lemon
 * Copyright (c) 1999, 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
/*-
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
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
 * $FreeBSD: src/sys/dev/amr/amr_disk.c,v 1.39 2006/10/31 21:19:25 pjd Exp $
 */

/*
 * Disk driver for AMI MegaRaid controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/dtype.h>

#include <sys/rman.h>

#include <dev/raid/amr/amrio.h>
#include <dev/raid/amr/amrreg.h>
#include <dev/raid/amr/amrvar.h>
#include <dev/raid/amr/amr_tables.h>

/* prototypes */
static int amrd_probe(device_t dev);
static int amrd_attach(device_t dev);
static int amrd_detach(device_t dev);

static	d_open_t	amrd_open;
static	d_strategy_t	amrd_strategy;
static	d_dump_t	amrd_dump;

static struct dev_ops amrd_ops = {
	{ "amrd", 0, D_DISK },
	.d_open = amrd_open,
	.d_strategy = amrd_strategy,
	.d_dump = amrd_dump,
};

static devclass_t	amrd_devclass;

static device_method_t amrd_methods[] = {
    DEVMETHOD(device_probe,	amrd_probe),
    DEVMETHOD(device_attach,	amrd_attach),
    DEVMETHOD(device_detach,	amrd_detach),
    DEVMETHOD_END
};

static driver_t amrd_driver = {
    "amrd",
    amrd_methods,
    sizeof(struct amrd_softc)
};

DRIVER_MODULE_ORDERED(amrd, amr, amrd_driver, amrd_devclass, NULL, NULL,
    SI_ORDER_THIRD);

static int
amrd_open(struct dev_open_args *ap)
{
    struct amrd_softc	*sc = ap->a_head.a_dev->si_drv1;

    debug_called(1);

    if (sc == NULL)
	return (ENXIO);

    /* controller not active? */
    if (sc->amrd_controller->amr_state & AMR_STATE_SHUTDOWN)
	return(ENXIO);

    return (0);
}
/********************************************************************************
 * System crashdump support
 */

static int
amrd_dump(struct dev_dump_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    off_t offset = ap->a_offset;
    void *virtual = ap->a_virtual;
    size_t length = ap->a_length;
    struct amrd_softc	*amrd_sc;
    struct amr_softc	*amr_sc;
    int			error = 0;

    amrd_sc = (struct amrd_softc *)dev->si_drv1;
    if (amrd_sc == NULL)
	return(ENXIO);
    amr_sc  = (struct amr_softc *)amrd_sc->amrd_controller;

    if (length > 0) {
	int	driveno = amrd_sc->amrd_drive - amr_sc->amr_drive;

	error = amr_dump_blocks(amr_sc, driveno, offset / AMR_BLKSIZE,
	    virtual, (int)length / AMR_BLKSIZE);
    }
    return(error);
}

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static int
amrd_strategy(struct dev_strategy_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct bio *bio = ap->a_bio;
    struct buf *bp = bio->bio_buf;
    struct amrd_softc *sc = (struct amrd_softc *)dev->si_drv1;

    /* bogus disk? */
    if (sc == NULL) {
	bp->b_error = EINVAL;
	goto bad;
    }
    bio->bio_driver_info = sc;

    devstat_start_transaction(&sc->amrd_stats);
    amr_submit_bio(sc->amrd_controller, bio);
    return (0);

 bad:
    bp->b_flags |= B_ERROR;

    /*
     * Correctly set the buf to indicate a completed transfer
     */
    bp->b_resid = bp->b_bcount;
    biodone(bio);
    return (0);
}

void
amrd_intr(void *data)
{
    struct bio *bio = (struct bio *)data;
    struct buf *bp = bio->bio_buf;
    struct amrd_softc *sc = (struct amrd_softc *)bio->bio_driver_info;

    debug_called(2);

    devstat_end_transaction_buf(&sc->amrd_stats, bp);
    if (bp->b_flags & B_ERROR) {
	bp->b_error = EIO;
	debug(1, "i/o error\n");
    } else {
	bp->b_resid = 0;
    }

    biodone(bio);
}

static int
amrd_probe(device_t dev)
{

    debug_called(1);

    device_set_desc(dev, "LSILogic MegaRAID logical drive");
    return (0);
}

static int
amrd_attach(device_t dev)
{
    struct amrd_softc	*sc = (struct amrd_softc *)device_get_softc(dev);
    device_t		parent;
    struct disk_info info;

    debug_called(1);

    parent = device_get_parent(dev);
    sc->amrd_controller = (struct amr_softc *)device_get_softc(parent);
    sc->amrd_unit = device_get_unit(dev);
    sc->amrd_drive = device_get_ivars(dev);
    sc->amrd_dev = dev;

    device_printf(dev, "%uMB (%u sectors) RAID %d (%s)\n",
		  sc->amrd_drive->al_size / ((1024 * 1024) / AMR_BLKSIZE),
		  sc->amrd_drive->al_size, sc->amrd_drive->al_properties & AMR_DRV_RAID_MASK, 
		  amr_describe_code(amr_table_drvstate, AMR_DRV_CURSTATE(sc->amrd_drive->al_state)));

    devstat_add_entry(&sc->amrd_stats, "amrd", sc->amrd_unit, AMR_BLKSIZE,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_STORARRAY | DEVSTAT_TYPE_IF_OTHER, 
		      DEVSTAT_PRIORITY_ARRAY);

    sc->amrd_dev_t = disk_create(sc->amrd_unit, &sc->amrd_disk, &amrd_ops);
    sc->amrd_dev_t->si_drv1 = sc;

    /* set maximum I/O size to match the maximum s/g size */
    sc->amrd_dev_t->si_iosize_max = (AMR_NSEG - 1) * PAGE_SIZE;

    /*
     * Set disk info, as it appears that all needed data is available already.
     * Setting the disk info will also cause the probing to start.
     */
    bzero(&info, sizeof(info));
    info.d_media_blksize = AMR_BLKSIZE;			/* optional */
    info.d_media_blocks = sc->amrd_drive->al_size;

    info.d_type = DTYPE_SCSI;				/* mandatory */
    info.d_secpertrack = sc->amrd_drive->al_sectors;
    info.d_nheads = sc->amrd_drive->al_heads;
    info.d_ncylinders = sc->amrd_drive->al_cylinders;
    info.d_secpercyl = sc->amrd_drive->al_sectors * sc->amrd_drive->al_heads;

    disk_setdiskinfo(&sc->amrd_disk, &info);

    return (0);
}

static int
amrd_detach(device_t dev)
{
    struct amrd_softc *sc = (struct amrd_softc *)device_get_softc(dev);

    debug_called(1);

#if 0 /* XXX swildner */
    if (sc->amrd_disk->d_flags & DISKFLAG_OPEN)
	return(EBUSY);
#endif

    devstat_remove_entry(&sc->amrd_stats);
    disk_destroy(&sc->amrd_disk);

    return(0);
}
