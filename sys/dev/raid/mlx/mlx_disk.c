/*-
 * Copyright (c) 1999 Jonathan Lemon
 * Copyright (c) 1999 Michael Smith
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
 * $FreeBSD: src/sys/dev/mlx/mlx_disk.c,v 1.8.2.4 2001/06/25 04:37:51 msmith Exp $
 */

/*
 * Disk driver for Mylex DAC960 RAID adapters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/dtype.h>
#include <sys/rman.h>

#include "mlx_compat.h"
#include "mlxio.h"
#include "mlxvar.h"
#include "mlxreg.h"

/* prototypes */
static int mlxd_probe(device_t dev);
static int mlxd_attach(device_t dev);
static int mlxd_detach(device_t dev);

static	d_open_t	mlxd_open;
static	d_close_t	mlxd_close;
static	d_strategy_t	mlxd_strategy;
static	d_ioctl_t	mlxd_ioctl;

static struct dev_ops mlxd_ops = {
		{ "mlxd", 0, D_DISK },
		.d_open =	mlxd_open,
		.d_close =	mlxd_close,
		.d_read =	physread,
		.d_write =	physwrite,
		.d_ioctl =	mlxd_ioctl,
		.d_strategy =	mlxd_strategy,
};

devclass_t		mlxd_devclass;

static device_method_t mlxd_methods[] = {
    DEVMETHOD(device_probe,	mlxd_probe),
    DEVMETHOD(device_attach,	mlxd_attach),
    DEVMETHOD(device_detach,	mlxd_detach),
    DEVMETHOD_END
};

static driver_t mlxd_driver = {
    "mlxd",
    mlxd_methods,
    sizeof(struct mlxd_softc)
};

DRIVER_MODULE(mlxd, mlx, mlxd_driver, mlxd_devclass, NULL, NULL);

static int
mlxd_open(struct dev_open_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct mlxd_softc	*sc = (struct mlxd_softc *)dev->si_drv1;

    debug_called(1);
	
    if (sc == NULL)
	return (ENXIO);

    /* controller not active? */
    if (sc->mlxd_controller->mlx_state & MLX_STATE_SHUTDOWN)
	return(ENXIO);
#if 0
    bzero(&info, sizeof(info));
    info.d_media_blksize= MLX_BLKSIZE;		/* mandatory */
    info.d_media_blocks	= sc->mlxd_drive->ms_size;

    info.d_type		= DTYPE_SCSI;		/* optional */
    info.d_secpertrack	= sc->mlxd_drive->ms_sectors;
    info.d_nheads	= sc->mlxd_drive->ms_heads;
    info.d_ncylinders	= sc->mlxd_drive->ms_cylinders;
    info.d_secpercyl	= sc->mlxd_drive->ms_sectors * sc->mlxd_drive->ms_heads;

    disk_setdiskinfo(&sc->mlxd_disk, &info);
#endif
    sc->mlxd_flags |= MLXD_OPEN;
    return (0);
}

static int
mlxd_close(struct dev_close_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct mlxd_softc	*sc = (struct mlxd_softc *)dev->si_drv1;

    debug_called(1);
	
    if (sc == NULL)
	return (ENXIO);
    sc->mlxd_flags &= ~MLXD_OPEN;
    return (0);
}

static int
mlxd_ioctl(struct dev_ioctl_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct mlxd_softc	*sc = (struct mlxd_softc *)dev->si_drv1;
    int error;

    debug_called(1);
	
    if (sc == NULL)
	return (ENXIO);

    if ((error = mlx_submit_ioctl(sc->mlxd_controller, sc->mlxd_drive, ap->a_cmd, ap->a_data, ap->a_fflag)) != ENOIOCTL) {
	debug(0, "mlx_submit_ioctl returned %d\n", error);
	return(error);
    }
    return (ENOTTY);
}

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static int
mlxd_strategy(struct dev_strategy_args *ap)
{
    struct bio *bio = ap->a_bio;
    struct buf *bp = bio->bio_buf;
    struct mlxd_softc	*sc = (struct mlxd_softc *)bio->bio_driver_info;

    debug_called(1);

    /* bogus disk? */
    if (sc == NULL) {
	bp->b_error = EINVAL;
	bp->b_flags |= B_ERROR;
	goto bad;
    }

    /* XXX may only be temporarily offline - sleep? */
    if (sc->mlxd_drive->ms_state == MLX_SYSD_OFFLINE) {
	bp->b_error = ENXIO;
	bp->b_flags |= B_ERROR;
	goto bad;
    }

    devstat_start_transaction(&sc->mlxd_stats);
    mlx_submit_bio(sc->mlxd_controller, bio);
    return(0);

 bad:
    /*
     * Correctly set the bio to indicate a failed tranfer.
     */
    bp->b_resid = bp->b_bcount;
    biodone(bio);
    return(0);
}

void
mlxd_intr(struct bio *bio)
{
    struct buf *bp = bio->bio_buf;
    struct mlxd_softc	*sc = (struct mlxd_softc *)bio->bio_driver_info;

    debug_called(1);

    if (bp->b_flags & B_ERROR)
	bp->b_error = EIO;
    else
	bp->b_resid = 0;
    devstat_end_transaction_buf(&sc->mlxd_stats, bp);
    biodone(bio);
}

static int
mlxd_probe(device_t dev)
{

    debug_called(1);
	
    device_set_desc(dev, "Mylex System Drive");
    return (0);
}

static int
mlxd_attach(device_t dev)
{
    struct mlxd_softc	*sc = (struct mlxd_softc *)device_get_softc(dev);
	struct disk_info info;
    device_t		parent;
    char		*state;
    cdev_t		dsk;
    int			s1, s2;
    
    debug_called(1);

    parent = device_get_parent(dev);
    sc->mlxd_controller = (struct mlx_softc *)device_get_softc(parent);
    sc->mlxd_unit = device_get_unit(dev);
    sc->mlxd_drive = device_get_ivars(dev);
    sc->mlxd_dev = dev;

    switch(sc->mlxd_drive->ms_state) {
    case MLX_SYSD_ONLINE:
	state = "online";
	break;
    case MLX_SYSD_CRITICAL:
	state = "critical";
	break;
    case MLX_SYSD_OFFLINE:
	state = "offline";
	break;
    default:
	state = "unknown state";
    }

    device_printf(dev, "%uMB (%u sectors) RAID %d (%s)\n",
		  sc->mlxd_drive->ms_size / ((1024 * 1024) / MLX_BLKSIZE),
		  sc->mlxd_drive->ms_size, sc->mlxd_drive->ms_raidlevel, state);

    devstat_add_entry(&sc->mlxd_stats, "mlxd", sc->mlxd_unit, MLX_BLKSIZE,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_STORARRAY | DEVSTAT_TYPE_IF_OTHER, 
		      DEVSTAT_PRIORITY_ARRAY);

    dsk = disk_create(sc->mlxd_unit, &sc->mlxd_disk, &mlxd_ops);
    dsk->si_drv1 = sc;
    sc->mlxd_dev_t = dsk;

    /* 
     * Set maximum I/O size to the lesser of the recommended maximum and the practical
     * maximum.
     */
    s1 = sc->mlxd_controller->mlx_enq2->me_maxblk * MLX_BLKSIZE;
    s2 = (sc->mlxd_controller->mlx_enq2->me_max_sg - 1) * PAGE_SIZE;
    dsk->si_iosize_max = imin(s1, s2);

	/*
	 * Set disk info, as it appears that all needed data is available already.
	 * Setting the disk info will also cause the probing to start.
	 */
	bzero(&info, sizeof(info));
    info.d_media_blksize= MLX_BLKSIZE;		/* mandatory */
    info.d_media_blocks	= sc->mlxd_drive->ms_size;

    info.d_type		= DTYPE_SCSI;		/* optional */
    info.d_secpertrack	= sc->mlxd_drive->ms_sectors;
    info.d_nheads	= sc->mlxd_drive->ms_heads;
    info.d_ncylinders	= sc->mlxd_drive->ms_cylinders;
    info.d_secpercyl	= sc->mlxd_drive->ms_sectors * sc->mlxd_drive->ms_heads;

    disk_setdiskinfo(&sc->mlxd_disk, &info);

    return (0);
}

static int
mlxd_detach(device_t dev)
{
    struct mlxd_softc *sc = (struct mlxd_softc *)device_get_softc(dev);

    debug_called(1);

    devstat_remove_entry(&sc->mlxd_stats);
    disk_destroy(&sc->mlxd_disk);

    return(0);
}
