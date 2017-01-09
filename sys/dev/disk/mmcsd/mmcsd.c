/*-
 * Copyright (c) 2006 Bernd Walter.  All rights reserved.
 * Copyright (c) 2006 M. Warner Losh.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this software may have been developed with reference to
 * the SD Simplified Specification.  The following disclaimer may apply:
 *
 * The following conditions apply to the release of the simplified
 * specification ("Simplified Specification") by the SD Card Association and
 * the SD Group. The Simplified Specification is a subset of the complete SD
 * Specification which is owned by the SD Card Association and the SD
 * Group. This Simplified Specification is provided on a non-confidential
 * basis subject to the disclaimers below. Any implementation of the
 * Simplified Specification may require a license from the SD Card
 * Association, SD Group, SD-3C LLC or other third parties.
 *
 * Disclaimers:
 *
 * The information contained in the Simplified Specification is presented only
 * as a standard specification for SD Cards and SD Host/Ancillary products and
 * is provided "AS-IS" without any representations or warranties of any
 * kind. No responsibility is assumed by the SD Group, SD-3C LLC or the SD
 * Card Association for any damages, any infringements of patents or other
 * right of the SD Group, SD-3C LLC, the SD Card Association or any third
 * parties, which may result from its use. No license is granted by
 * implication, estoppel or otherwise under any patent or other rights of the
 * SD Group, SD-3C LLC, the SD Card Association or any third party. Nothing
 * herein shall be construed as an obligation by the SD Group, the SD-3C LLC
 * or the SD Card Association to disclose or distribute any technical
 * information, know-how or other confidential information to any third party.
 *
 * $FreeBSD: src/sys/dev/mmc/mmcsd.c,v 1.20 2009/02/17 19:17:25 mav Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/device.h>
#include <sys/devicestat.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/spinlock.h>

#include <sys/buf2.h>

#include <bus/mmc/mmcvar.h>
#include <bus/mmc/mmcreg.h>

#include "mmcbus_if.h"

struct mmcsd_softc {
	device_t dev;
	cdev_t dev_t;
	struct lock sc_lock;
	struct disk disk;
	struct devstat device_stats;
	struct thread *td;
	struct bio_queue_head bio_queue;
	daddr_t eblock, eend;	/* Range remaining after the last erase. */
	int running;
	int suspend;
};

/* bus entry points */
static int mmcsd_probe(device_t dev);
static int mmcsd_attach(device_t dev);
static int mmcsd_detach(device_t dev);

/* disk routines */
static d_open_t mmcsd_open;
static d_close_t mmcsd_close;
static d_strategy_t mmcsd_strategy;
static d_dump_t mmcsd_dump;

static void mmcsd_task(void *arg);

static const char *mmcsd_card_name(device_t dev);
static int mmcsd_bus_bit_width(device_t dev);

#define MMCSD_LOCK(_sc)		lockmgr(&(_sc)->sc_lock, LK_EXCLUSIVE)
#define	MMCSD_UNLOCK(_sc)	lockmgr(&(_sc)->sc_lock, LK_RELEASE)
#define MMCSD_LOCK_INIT(_sc)	lockinit(&(_sc)->sc_lock, "mmcsd", 0, LK_CANRECURSE)
#define MMCSD_LOCK_DESTROY(_sc)	lockuninit(&(_sc)->sc_lock);
#define MMCSD_ASSERT_LOCKED(_sc) KKASSERT(lockstatus(&(_sc)->sc_lock, curthread) != 0);
#define MMCSD_ASSERT_UNLOCKED(_sc) KKASSERT(lockstatus(&(_sc)->sc_lock, curthread) == 0);

static struct dev_ops mmcsd_ops = {
	{ "mmcsd", 0, D_DISK | D_MPSAFE },
	.d_open = mmcsd_open,
	.d_close = mmcsd_close,
	.d_strategy = mmcsd_strategy,
	.d_dump = mmcsd_dump,
};

static int
mmcsd_probe(device_t dev)
{

	device_quiet(dev);
	device_set_desc(dev, "MMC/SD Memory Card");
	return (0);
}

static int
mmcsd_attach(device_t dev)
{
	struct mmcsd_softc *sc;
	struct disk_info info;
	cdev_t dsk;
	intmax_t mb;
	char unit;
	int sector_size;

	sc = device_get_softc(dev);
	sc->dev = dev;
	MMCSD_LOCK_INIT(sc);

	sector_size = mmc_get_sector_size(dev);
	devstat_add_entry(&sc->device_stats, "mmcsd", device_get_unit(dev),
	    sector_size, DEVSTAT_NO_ORDERED_TAGS,
	    DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
	    DEVSTAT_PRIORITY_DISK);

	dsk = disk_create(device_get_unit(dev), &sc->disk, &mmcsd_ops);
	dsk->si_drv1 = sc;
	sc->dev_t = dsk;

	dsk->si_iosize_max = 4*1024*1024;	/* Maximum defined SD card AU size. */

	bzero(&info, sizeof(info));
	info.d_media_blksize = sector_size;
	info.d_media_blocks = mmc_get_media_size(dev);
	disk_setdiskinfo(&sc->disk, &info);

	/*
	 * Display in most natural units.  There's no cards < 1MB.
	 * The SD standard goes to 2GiB, but the data format supports
	 * up to 4GiB and some card makers push it up to this limit.
	 * The SDHC standard only goes to 32GiB (the data format in
	 * SDHC is good to 2TiB however, which isn't too ugly at
	 * 2048GiBm, so we note it in passing here and don't add the
	 * code to print TiB).
	 */
	mb = (info.d_media_blksize * info.d_media_blocks) >> 20;	/* 1MiB == 1 << 20 */
	unit = 'M';
	if (mb >= 10240) {		/* 1GiB = 1024 MiB */
		unit = 'G';
		mb /= 1024;
	}
	device_printf(dev, "%ju%cB <%s Memory Card>%s at %s %dMHz/%dbit\n",
	    mb, unit, mmcsd_card_name(dev),
	    mmc_get_read_only(dev) ? " (read-only)" : "",
	    device_get_nameunit(device_get_parent(dev)),
	    mmc_get_tran_speed(dev) / 1000000, mmcsd_bus_bit_width(dev));

	bioq_init(&sc->bio_queue);

	sc->running = 1;
	sc->suspend = 0;
	sc->eblock = sc->eend = 0;
	kthread_create(mmcsd_task, sc, &sc->td, "mmc/sd card task");

	return (0);
}

static int
mmcsd_detach(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);
	struct buf *q_bp;
	struct bio *q_bio;

	MMCSD_LOCK(sc);
	sc->suspend = 0;
	if (sc->running > 0) {
		/* kill thread */
		sc->running = 0;
		wakeup(sc);
		/* wait for thread to finish. */
		while (sc->running != -1)
			lksleep(sc, &sc->sc_lock, 0, "detach", 0);
	}
	MMCSD_UNLOCK(sc);

	/*
	 * Flush the request queue.
	 *
	 * XXX: Return all queued I/O with ENXIO. Is this correct?
	 */
	while ((q_bio = bioq_takefirst(&sc->bio_queue)) != NULL) {
		q_bp = q_bio->bio_buf;
		q_bp->b_resid = q_bp->b_bcount;
		q_bp->b_error = ENXIO;
		q_bp->b_flags |= B_ERROR;
		biodone(q_bio);
	}

	/* kill disk */
	disk_destroy(&sc->disk);
	devstat_remove_entry(&sc->device_stats);

	MMCSD_LOCK_DESTROY(sc);

	return (0);
}

static int
mmcsd_suspend(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);

	MMCSD_LOCK(sc);
	sc->suspend = 1;
	if (sc->running > 0) {
		/* kill thread */
		sc->running = 0;
		wakeup(sc);
		/* wait for thread to finish. */
		while (sc->running != -1)
			lksleep(sc, &sc->sc_lock, 0, "detach", 0);
	}
	MMCSD_UNLOCK(sc);
	return (0);
}

static int
mmcsd_resume(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);

	MMCSD_LOCK(sc);
	sc->suspend = 0;
	if (sc->running <= 0) {
		sc->running = 1;
		MMCSD_UNLOCK(sc);
		kthread_create(mmcsd_task, sc, &sc->td, "mmc/sd card task");
	} else
		MMCSD_UNLOCK(sc);
	return (0);
}

static int
mmcsd_open(struct dev_open_args *ap)
{
	return (0);
}

static int
mmcsd_close(struct dev_close_args *ap)
{
	return (0);
}

static int
mmcsd_strategy(struct dev_strategy_args *ap)
{
	struct mmcsd_softc *sc;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;

	sc = (struct mmcsd_softc *)ap->a_head.a_dev->si_drv1;
	MMCSD_LOCK(sc);
	if (sc->running > 0 || sc->suspend > 0) {
		bioqdisksort(&sc->bio_queue, bio);
		MMCSD_UNLOCK(sc);
		wakeup(sc);
	} else {
		MMCSD_UNLOCK(sc);
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		bp->b_resid = bp->b_bcount;
		biodone(bio);
	}
	return (0);
}

static daddr_t
mmcsd_rw(struct mmcsd_softc *sc, struct bio *bio)
{
	daddr_t block, end;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_request req;
	struct mmc_data data;
	device_t dev = sc->dev;
	int sz = sc->disk.d_info.d_media_blksize;
	struct buf *bp = bio->bio_buf;

	block = bio->bio_offset / sz;
	end = block + (bp->b_bcount / sz);
	while (block < end) {
		char *vaddr = bp->b_data +
		    (block - (bio->bio_offset / sz)) * sz;
		int numblocks = min(end - block, mmc_get_max_data(dev));
		memset(&req, 0, sizeof(req));
		memset(&cmd, 0, sizeof(cmd));
		memset(&stop, 0, sizeof(stop));
		req.cmd = &cmd;
		cmd.data = &data;
		if (bp->b_cmd == BUF_CMD_READ) {
			if (numblocks > 1)
				cmd.opcode = MMC_READ_MULTIPLE_BLOCK;
			else
				cmd.opcode = MMC_READ_SINGLE_BLOCK;
		} else {
			if (numblocks > 1)
				cmd.opcode = MMC_WRITE_MULTIPLE_BLOCK;
			else
				cmd.opcode = MMC_WRITE_BLOCK;
		}
		cmd.arg = block;
		if (!mmc_get_high_cap(dev))
			cmd.arg <<= 9;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		data.data = vaddr;
		data.mrq = &req;
		if (bp->b_cmd == BUF_CMD_READ)
			data.flags = MMC_DATA_READ;
		else
			data.flags = MMC_DATA_WRITE;
		data.len = numblocks * sz;
		if (numblocks > 1) {
			data.flags |= MMC_DATA_MULTI;
			stop.opcode = MMC_STOP_TRANSMISSION;
			stop.arg = 0;
			stop.flags = MMC_RSP_R1B | MMC_CMD_AC;
			req.stop = &stop;
		}
//		kprintf("Len %d  %lld-%lld flags %#x sz %d\n",
//		    (int)data.len, (long long)block, (long long)end, data.flags, sz);
		MMCBUS_WAIT_FOR_REQUEST(device_get_parent(dev), dev, &req);
		if (req.cmd->error != MMC_ERR_NONE)
			break;
		block += numblocks;
	}
	return (block);
}

static daddr_t
mmcsd_delete(struct mmcsd_softc *sc, struct bio *bio)
{
	daddr_t block, end, start, stop;
	struct mmc_command cmd;
	struct mmc_request req;
	device_t dev = sc->dev;
	int sz = sc->disk.d_info.d_media_blksize;
	int erase_sector;
	struct buf *bp = bio->bio_buf;

	block = bio->bio_offset / sz;
	end = block + (bp->b_bcount / sz);
	/* Coalesce with part remaining from previous request. */
	if (block > sc->eblock && block <= sc->eend)
		block = sc->eblock;
	if (end >= sc->eblock && end < sc->eend)
		end = sc->eend;
	/* Safe round to the erase sector boundaries. */
	erase_sector = mmc_get_erase_sector(dev);
	start = block + erase_sector - 1;	 /* Round up. */
	start -= start % erase_sector;
	stop = end;				/* Round down. */
	stop -= end % erase_sector;
	/* We can't erase area smaller then sector, store it for later. */
	if (start >= stop) {
		sc->eblock = block;
		sc->eend = end;
		return (end);
	}

	/* Set erase start position. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	req.cmd = &cmd;
	if (mmc_get_card_type(dev) == mode_sd)
		cmd.opcode = SD_ERASE_WR_BLK_START;
	else
		cmd.opcode = MMC_ERASE_GROUP_START;
	cmd.arg = start;
	if (!mmc_get_high_cap(dev))
		cmd.arg <<= 9;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(device_get_parent(dev), dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
	    kprintf("erase err1: %d\n", req.cmd->error);
	    return (block);
	}
	/* Set erase stop position. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	req.cmd = &cmd;
	if (mmc_get_card_type(dev) == mode_sd)
		cmd.opcode = SD_ERASE_WR_BLK_END;
	else
		cmd.opcode = MMC_ERASE_GROUP_END;
	cmd.arg = stop;
	if (!mmc_get_high_cap(dev))
		cmd.arg <<= 9;
	cmd.arg--;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(device_get_parent(dev), dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
	    kprintf("erase err2: %d\n", req.cmd->error);
	    return (block);
	}
	/* Erase range. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	req.cmd = &cmd;
	cmd.opcode = MMC_ERASE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1B | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(device_get_parent(dev), dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
	    kprintf("erase err3 %d\n", req.cmd->error);
	    return (block);
	}
	/* Store one of remaining parts for the next call. */
	if ((bio->bio_offset / sz) >= sc->eblock || block == start) {
		sc->eblock = stop;	/* Predict next forward. */
		sc->eend = end;
	} else {
		sc->eblock = block;	/* Predict next backward. */
		sc->eend = start;
	}
	return (end);
}

static int
mmcsd_dump(struct dev_dump_args *ap)
{
#if 0
	cdev_t cdev = ap->a_head.a_dev;
	struct mmcsd_softc *sc = (struct mmcsd_softc *)cdev->si_drv1;
	device_t dev = sc->dev;
	struct bio bp;
	daddr_t block, end;
	int length = ap->a_length;

	/* length zero is special and really means flush buffers to media */
	if (!length)
		return (0);

	bzero(&bp, sizeof(struct bio));
	bp.bio_driver_info = cdev;
	bp.bio_pblkno = offset / sc->disk->d_sectorsize;
	bp.bio_bcount = length;
	bp.bio_data = virtual;
	bp.bio_cmd = BIO_WRITE;
	end = bp.bio_pblkno + bp.bio_bcount / sc->disk.d_info.d_media_blksize;
	MMCBUS_ACQUIRE_BUS(device_get_parent(dev), dev);
	block = mmcsd_rw(sc, &bp);
	MMCBUS_RELEASE_BUS(device_get_parent(dev), dev);
	return ((end < block) ? EIO : 0);
#endif
	return EIO;
}

static void
mmcsd_task(void *arg)
{
	struct mmcsd_softc *sc = (struct mmcsd_softc*)arg;
	struct bio *bio;
	struct buf *bp;
	int sz;
	daddr_t block, end;
	device_t dev;

	dev = sc->dev;

	while (1) {
		MMCSD_LOCK(sc);
		do {
			if (sc->running == 0)
				goto out;
			bio = bioq_takefirst(&sc->bio_queue);
			if (bio == NULL)
				lksleep(sc, &sc->sc_lock, 0, "jobqueue", 0);
		} while (bio == NULL);
		MMCSD_UNLOCK(sc);
		bp = bio->bio_buf;
		devstat_start_transaction(&sc->device_stats);
		if (bp->b_cmd != BUF_CMD_READ && mmc_get_read_only(dev)) {
			bp->b_error = EROFS;
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
			devstat_end_transaction_buf(&sc->device_stats, bp);
			biodone(bio);
			continue;
		}
		MMCBUS_ACQUIRE_BUS(device_get_parent(dev), dev);
		sz = sc->disk.d_info.d_media_blksize;
		block = bio->bio_offset / sz;
		end = block + (bp->b_bcount / sz);
		if (bp->b_cmd == BUF_CMD_READ ||
		    bp->b_cmd == BUF_CMD_WRITE) {
			/* Access to the remaining erase block obsoletes it. */
			if (block < sc->eend && end > sc->eblock)
				sc->eblock = sc->eend = 0;
			block = mmcsd_rw(sc, bio);
		} else if (bp->b_cmd == BUF_CMD_FREEBLKS) {
			block = mmcsd_delete(sc, bio);
		}
		MMCBUS_RELEASE_BUS(device_get_parent(dev), dev);
		if (block < end) {
			bp->b_error = EIO;
			bp->b_resid = (end - block) * sz;
			bp->b_flags |= B_ERROR;
		} else {
			bp->b_resid = 0;
		}
		devstat_end_transaction_buf(&sc->device_stats, bp);
		biodone(bio);
	}
out:
	/* tell parent we're done */
	sc->running = -1;
	MMCSD_UNLOCK(sc);
	wakeup(sc);
}

static const char *
mmcsd_card_name(device_t dev)
{
	if (mmc_get_card_type(dev) == mode_mmc)
		return ("MMC");
	if (mmc_get_high_cap(dev))
		return ("SDHC");
	return ("SD");
}

static int
mmcsd_bus_bit_width(device_t dev)
{
	if (mmc_get_bus_width(dev) == bus_width_1)
		return (1);
	if (mmc_get_bus_width(dev) == bus_width_4)
		return (4);
	return (8);
}

static device_method_t mmcsd_methods[] = {
	DEVMETHOD(device_probe, mmcsd_probe),
	DEVMETHOD(device_attach, mmcsd_attach),
	DEVMETHOD(device_detach, mmcsd_detach),
	DEVMETHOD(device_suspend, mmcsd_suspend),
	DEVMETHOD(device_resume, mmcsd_resume),
	DEVMETHOD_END
};

static driver_t mmcsd_driver = {
	"mmcsd",
	mmcsd_methods,
	sizeof(struct mmcsd_softc),
};
static devclass_t mmcsd_devclass;

DRIVER_MODULE(mmcsd, mmc, mmcsd_driver, mmcsd_devclass, NULL, NULL);
