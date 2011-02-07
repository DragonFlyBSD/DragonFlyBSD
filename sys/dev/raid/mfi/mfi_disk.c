/*-
 * Copyright (c) 2006 IronPort Systems
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
 * $FreeBSD: src/sys/dev/mfi/mfi_disk.c,v 1.8 2008/11/17 23:30:19 jhb Exp $
 */

#include "opt_mfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/uio.h>

#include <sys/buf2.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <sys/rman.h>

#include <dev/raid/mfi/mfireg.h>
#include <dev/raid/mfi/mfi_ioctl.h>
#include <dev/raid/mfi/mfivar.h>

static int	mfi_disk_probe(device_t dev);
static int	mfi_disk_attach(device_t dev);
static int	mfi_disk_detach(device_t dev);

static d_open_t		mfi_disk_open;
static d_close_t	mfi_disk_close;
static d_strategy_t	mfi_disk_strategy;
static d_dump_t		mfi_disk_dump;

static struct dev_ops mfi_disk_ops = {
	{ "mfid", 0, D_DISK },
	.d_open = mfi_disk_open,
	.d_close = mfi_disk_close,
	.d_strategy = mfi_disk_strategy,
	.d_dump = mfi_disk_dump,
};

static devclass_t	mfi_disk_devclass;

static device_method_t mfi_disk_methods[] = {
	DEVMETHOD(device_probe,		mfi_disk_probe),
	DEVMETHOD(device_attach,	mfi_disk_attach),
	DEVMETHOD(device_detach,	mfi_disk_detach),
	{ 0, 0 }
};

static driver_t mfi_disk_driver = {
	"mfid",
	mfi_disk_methods,
	sizeof(struct mfi_disk)
};

DRIVER_MODULE(mfid, mfi, mfi_disk_driver, mfi_disk_devclass, 0, 0);

static int
mfi_disk_probe(device_t dev)
{

	return (0);
}

static int
mfi_disk_attach(device_t dev)
{
	struct mfi_disk *sc;
	struct mfi_ld_info *ld_info;
	struct disk_info info;
	uint64_t sectors;
	uint32_t secsize;
	char *state;

	sc = device_get_softc(dev);
	ld_info = device_get_ivars(dev);

	sc->ld_dev = dev;
	sc->ld_id = ld_info->ld_config.properties.ld.v.target_id;
	sc->ld_unit = device_get_unit(dev);
	sc->ld_info = ld_info;
	sc->ld_controller = device_get_softc(device_get_parent(dev));
	sc->ld_flags = 0;

	sectors = ld_info->size;
	secsize = MFI_SECTOR_LEN;
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&sc->ld_controller->mfi_ld_tqh, sc, ld_link);
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);

	switch (ld_info->ld_config.params.state) {
	case MFI_LD_STATE_OFFLINE:
		state = "offline";
		break;
	case MFI_LD_STATE_PARTIALLY_DEGRADED:
		state = "partially degraded";
		break;
	case MFI_LD_STATE_DEGRADED:
		state = "degraded";
		break;
	case MFI_LD_STATE_OPTIMAL:
		state = "optimal";
		break;
	default:
		state = "unknown";
		break;
	}
	device_printf(dev, "%juMB (%ju sectors) RAID volume '%s' is %s\n",
		      sectors / (1024 * 1024 / secsize), sectors,
		      ld_info->ld_config.properties.name,
		      state);

	devstat_add_entry(&sc->ld_devstat, "mfid", device_get_unit(dev),
	    MFI_SECTOR_LEN, DEVSTAT_NO_ORDERED_TAGS,
	    DEVSTAT_TYPE_STORARRAY | DEVSTAT_TYPE_IF_OTHER,
	    DEVSTAT_PRIORITY_ARRAY);

	sc->ld_disk.d_cdev = disk_create(sc->ld_unit, &sc->ld_disk,
	    &mfi_disk_ops);
	sc->ld_disk.d_cdev->si_drv1 = sc;
	sc->ld_disk.d_cdev->si_iosize_max =
	    min(sc->ld_controller->mfi_max_io * secsize,
		(sc->ld_controller->mfi_max_sge - 1) * PAGE_SIZE);

	bzero(&info, sizeof(info));
	info.d_media_blksize = secsize;	/* mandatory */
	info.d_media_blocks = sectors;

	if (info.d_media_blocks >= (1 * 1024 * 1024)) {
		info.d_nheads = 255;
		info.d_secpertrack = 63;
	} else {
		info.d_nheads = 64;
		info.d_secpertrack = 32;
	}

	disk_setdiskinfo(&sc->ld_disk, &info);

	return (0);
}

static int
mfi_disk_detach(device_t dev)
{
	struct mfi_disk *sc;

	sc = device_get_softc(dev);

	lockmgr(&sc->ld_controller->mfi_io_lock, LK_EXCLUSIVE);
	if (((sc->ld_flags & MFI_DISK_FLAGS_OPEN)) &&
	    (sc->ld_controller->mfi_keep_deleted_volumes ||
	    sc->ld_controller->mfi_detaching)) {
		lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);
		return (EBUSY);
	}
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);

	disk_destroy(&sc->ld_disk);
	devstat_remove_entry(&sc->ld_devstat);
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_REMOVE(&sc->ld_controller->mfi_ld_tqh, sc, ld_link);
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);
	kfree(sc->ld_info, M_MFIBUF);
	return (0);
}

static int
mfi_disk_open(struct dev_open_args *ap)
{
	struct mfi_disk *sc = ap->a_head.a_dev->si_drv1;
	int error;

	lockmgr(&sc->ld_controller->mfi_io_lock, LK_EXCLUSIVE);
	if (sc->ld_flags & MFI_DISK_FLAGS_DISABLED)
		error = ENXIO;
	else {
		sc->ld_flags |= MFI_DISK_FLAGS_OPEN;
		error = 0;
	}
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);

	return (error);
}

static int
mfi_disk_close(struct dev_close_args *ap)
{
	struct mfi_disk *sc = ap->a_head.a_dev->si_drv1;

	lockmgr(&sc->ld_controller->mfi_io_lock, LK_EXCLUSIVE);
	sc->ld_flags &= ~MFI_DISK_FLAGS_OPEN;
	lockmgr(&sc->ld_controller->mfi_io_lock, LK_RELEASE);

	return (0);
}

int
mfi_disk_disable(struct mfi_disk *sc)
{

	KKASSERT(lockstatus(&sc->ld_controller->mfi_io_lock, curthread) != 0);
	if (sc->ld_flags & MFI_DISK_FLAGS_OPEN) {
		if (sc->ld_controller->mfi_delete_busy_volumes)
			return (0);
		device_printf(sc->ld_dev, "Unable to delete busy device\n");
		return (EBUSY);
	}
	sc->ld_flags |= MFI_DISK_FLAGS_DISABLED;
	return (0);
}

void
mfi_disk_enable(struct mfi_disk *sc)
{

	KKASSERT(lockstatus(&sc->ld_controller->mfi_io_lock, curthread) != 0);
	sc->ld_flags &= ~MFI_DISK_FLAGS_DISABLED;
}

static int
mfi_disk_strategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct mfi_disk *sc = ap->a_head.a_dev->si_drv1;
	struct mfi_softc *controller;

	if (sc == NULL) {
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		bp->b_resid = bp->b_bcount;
		biodone(bio);
		return (0);
	}

	/*
	 * XXX swildner
	 *
	 * If it's a null transfer, do nothing. FreeBSD's original driver
	 * doesn't have this, but that caused hard error messages (even
	 * though everything else continued to work fine). Interestingly,
	 * only when HAMMER was used.
	 *
	 * Several others of our RAID drivers have this check, such as
	 * aac(4) and ida(4), so we insert it here, too.
	 *
	 * The cause of null transfers is yet unknown.
	 */
	if (bp->b_bcount == 0) {
		bp->b_resid = bp->b_bcount;
		biodone(bio);
		return (0);
	}

	controller = sc->ld_controller;
	bio->bio_driver_info = sc;
	lockmgr(&controller->mfi_io_lock, LK_EXCLUSIVE);
	mfi_enqueue_bio(controller, bio);
	devstat_start_transaction(&sc->ld_devstat);
	mfi_startio(controller);
	lockmgr(&controller->mfi_io_lock, LK_RELEASE);
	return (0);
}

void
mfi_disk_complete(struct bio *bio)
{
	struct mfi_disk *sc = bio->bio_driver_info;
	struct buf *bp = bio->bio_buf;

	devstat_end_transaction_buf(&sc->ld_devstat, bp);
	if (bp->b_flags & B_ERROR) {
		if (bp->b_error == 0)
			bp->b_error = EIO;
		diskerr(bio, sc->ld_disk.d_cdev, "hard error", -1, 1);
		kprintf("\n");
	} else {
		bp->b_resid = 0;
	}
	biodone(bio);
}

static int
mfi_disk_dump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	off_t offset = ap->a_offset;
	void *virt = ap->a_virtual;
	size_t len = ap->a_length;
	struct mfi_disk *sc;
	struct mfi_softc *parent_sc;
	int error;

	sc = dev->si_drv1;
	parent_sc = sc->ld_controller;

	if (len > 0) {
		if ((error = mfi_dump_blocks(parent_sc, sc->ld_id, offset /
		    MFI_SECTOR_LEN, virt, len)) != 0)
			return (error);
	} else {
		/* mfi_sync_cache(parent_sc, sc->ld_id); */
	}

	return (0);
}
