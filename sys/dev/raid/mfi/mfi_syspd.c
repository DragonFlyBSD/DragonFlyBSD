/*-
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *            Copyright 1994-2009 The FreeBSD Project.
 *            All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE FREEBSD PROJECT``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FREEBSD PROJECT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY,OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD: src/sys/dev/mfi/mfi_pddisk.c,v 1.2.2.6 2007/08/24 17:29:18 jhb Exp $
 */

#include "opt_mfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/uio.h>

#include <sys/bio.h>
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

static int	mfi_syspd_probe(device_t dev);
static int	mfi_syspd_attach(device_t dev);
static int	mfi_syspd_detach(device_t dev);

static d_open_t		mfi_syspd_open;
static d_close_t	mfi_syspd_close;
static d_strategy_t	mfi_syspd_strategy;
static d_dump_t		mfi_syspd_dump;

static struct dev_ops mfi_syspd_ops = {
	{ "mfisyspd", 0, D_DISK },
	.d_open = mfi_syspd_open,
	.d_close = mfi_syspd_close,
	.d_strategy = mfi_syspd_strategy,
	.d_dump = mfi_syspd_dump,
};

static devclass_t	mfi_syspd_devclass;

static device_method_t mfi_syspd_methods[] = {
	DEVMETHOD(device_probe,		mfi_syspd_probe),
	DEVMETHOD(device_attach,	mfi_syspd_attach),
	DEVMETHOD(device_detach,	mfi_syspd_detach),
	{ 0, 0 }
};

static driver_t mfi_syspd_driver = {
	"mfisyspd",
	mfi_syspd_methods,
	sizeof(struct mfi_system_pd)
};

DRIVER_MODULE(mfisyspd, mfi, mfi_syspd_driver, mfi_syspd_devclass, 0, 0);

static int
mfi_syspd_probe(device_t dev)
{

	return (0);
}

static int
mfi_syspd_attach(device_t dev)
{
	struct mfi_system_pd *sc;
	struct mfi_pd_info *pd_info;
	struct disk_info info;
	uint64_t sectors;
	uint32_t secsize;

	sc = device_get_softc(dev);
	pd_info = device_get_ivars(dev);

	sc->pd_dev = dev;
	sc->pd_id = pd_info->ref.v.device_id;
	sc->pd_unit = device_get_unit(dev);
	sc->pd_info = pd_info;
	sc->pd_controller = device_get_softc(device_get_parent(dev));
	sc->pd_flags = MFI_DISK_FLAGS_SYSPD;

	sectors = pd_info->raw_size;
	secsize = MFI_SECTOR_LEN;
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&sc->pd_controller->mfi_syspd_tqh, sc, pd_link);
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);
	device_printf(dev, "%juMB (%ju sectors) SYSPD volume\n",
		      sectors / (1024 * 1024 / secsize), sectors);

	devstat_add_entry(&sc->pd_devstat, "mfisyspd", device_get_unit(dev),
	    MFI_SECTOR_LEN, DEVSTAT_NO_ORDERED_TAGS,
	    DEVSTAT_TYPE_STORARRAY | DEVSTAT_TYPE_IF_OTHER,
	    DEVSTAT_PRIORITY_ARRAY);

	sc->pd_dev_t = disk_create(sc->pd_unit, &sc->pd_disk, &mfi_syspd_ops);
	sc->pd_dev_t->si_drv1 = sc;
	sc->pd_dev_t->si_iosize_max = sc->pd_controller->mfi_max_io *
	    secsize;

	bzero(&info, sizeof(info));
	info.d_media_blksize = secsize; /* mandatory */
	info.d_media_blocks = sectors;

	if (info.d_media_blocks >= (1 * 1024 * 1024)) {
		info.d_nheads = 255;
		info.d_secpertrack = 63;
	} else {
		info.d_nheads = 64;
		info.d_secpertrack = 32;
	}

	disk_setdiskinfo(&sc->pd_disk, &info);

	return (0);
}

static int
mfi_syspd_detach(device_t dev)
{
	struct mfi_system_pd *sc;

	sc = device_get_softc(dev);
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_EXCLUSIVE);
	if ((sc->pd_flags & MFI_DISK_FLAGS_OPEN) &&
	    (sc->pd_controller->mfi_keep_deleted_volumes ||
	    sc->pd_controller->mfi_detaching)) {
		lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);
		return (EBUSY);
	}
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);

	disk_destroy(&sc->pd_disk);
	devstat_remove_entry(&sc->pd_devstat);
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_REMOVE(&sc->pd_controller->mfi_syspd_tqh, sc, pd_link);
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);
	kfree(sc->pd_info, M_MFIBUF);
	return (0);
}

static int
mfi_syspd_open(struct dev_open_args *ap)
{
	struct mfi_system_pd *sc = ap->a_head.a_dev->si_drv1;
	int error;

	lockmgr(&sc->pd_controller->mfi_io_lock, LK_EXCLUSIVE);
	if (sc->pd_flags & MFI_DISK_FLAGS_DISABLED)
		error = ENXIO;
	else {
		sc->pd_flags |= MFI_DISK_FLAGS_OPEN;
		error = 0;
	}
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);
	return (error);
}

static int
mfi_syspd_close(struct dev_close_args *ap)
{
	struct mfi_system_pd *sc = ap->a_head.a_dev->si_drv1;

	lockmgr(&sc->pd_controller->mfi_io_lock, LK_EXCLUSIVE);
	sc->pd_flags &= ~MFI_DISK_FLAGS_OPEN;
	lockmgr(&sc->pd_controller->mfi_io_lock, LK_RELEASE);

	return (0);
}

int
mfi_syspd_disable(struct mfi_system_pd *sc)
{

	KKASSERT(lockstatus(&sc->pd_controller->mfi_io_lock, curthread) != 0);
	if (sc->pd_flags & MFI_DISK_FLAGS_OPEN) {
		if (sc->pd_controller->mfi_delete_busy_volumes)
			return (0);
		device_printf(sc->pd_dev, "Unable to delete busy syspd device\n");
		return (EBUSY);
	}
	sc->pd_flags |= MFI_DISK_FLAGS_DISABLED;
	return (0);
}

void
mfi_syspd_enable(struct mfi_system_pd *sc)
{

	KKASSERT(lockstatus(&sc->pd_controller->mfi_io_lock, curthread) != 0);
	sc->pd_flags &= ~MFI_DISK_FLAGS_DISABLED;
}

static int
mfi_syspd_strategy(struct dev_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct mfi_system_pd *sc = ap->a_head.a_dev->si_drv1;
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

	controller = sc->pd_controller;
	bio->bio_driver_info = sc;
	lockmgr(&controller->mfi_io_lock, LK_EXCLUSIVE);
	mfi_enqueue_bio(controller, bio);
	devstat_start_transaction(&sc->pd_devstat);
	mfi_startio(controller);
	lockmgr(&controller->mfi_io_lock, LK_RELEASE);
	return (0);
}

#if 0
void
mfi_disk_complete(struct bio *bio)
{
	struct mfi_system_pd *sc = bio->bio_driver_info;
	struct buf *bp = bio->bio_buf;

	devstat_end_transaction_buf(&sc->pd_devstat, bp);
	if (bio->b_flags & B_ERROR) {
		if (bp->b_error == 0)
			bp->b_error = EIO;
		diskerr(bio, sc->pd_disk.d_cdev, "hard error", -1, 1);
		kprintf("\n");
	} else {
		bp->b_resid = 0;
	}
	biodone(bio);
}
#endif

static int
mfi_syspd_dump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	off_t offset = ap->a_offset;
	void *virt = ap->a_virtual;
	size_t len = ap->a_length;
	struct mfi_system_pd *sc;
	struct mfi_softc *parent_sc;
	int error;

	sc = dev->si_drv1;
	parent_sc = sc->pd_controller;

	if (len > 0) {
		if ((error = mfi_dump_syspd_blocks(parent_sc, sc->pd_id, offset /
		    MFI_SECTOR_LEN, virt, len)) != 0)
			return (error);
	} else {
		/* mfi_sync_cache(parent_sc, sc->ld_id); */
	}
	return (0);
}
