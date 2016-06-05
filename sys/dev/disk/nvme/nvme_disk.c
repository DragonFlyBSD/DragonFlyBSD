/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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

#include "nvme.h"

static void nvme_disk_callback(nvme_request_t *req, struct lock *lk);
static int nvme_strategy_core(nvme_softns_t *nsc, struct bio *bio, int delay);

static d_open_t nvme_open;
static d_close_t nvme_close;
static d_ioctl_t nvme_ioctl;
static d_strategy_t nvme_strategy;

static struct dev_ops nvme_ops = {
	{ "nvme", 0, D_DISK | D_MPSAFE | D_CANFREE | D_TRACKCLOSE},
	.d_open =       nvme_open,
	.d_close =      nvme_close,
	.d_read =       physread,
	.d_write =      physwrite,
	.d_ioctl =      nvme_ioctl,
	.d_strategy =   nvme_strategy,
};

static int nvme_sync_delay = 0;
SYSCTL_INT(_debug, OID_AUTO, nvme_sync_delay, CTLFLAG_RW, &nvme_sync_delay, 0,
	   "Enable synchronous delay/completion-check, uS");

/*
 * Attach a namespace as a disk, making the disk available to the system.
 */
void
nvme_disk_attach(nvme_softns_t *nsc)
{
	nvme_softc_t *sc;
	struct disk_info info;
	char serial[20+16];
	size_t len;
	uint64_t cap_gb;

	sc = nsc->sc;
	devstat_add_entry(&nsc->stats, "nvme", nsc->unit, nsc->blksize,
			  DEVSTAT_NO_ORDERED_TAGS,
			  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
			  DEVSTAT_PRIORITY_OTHER);
	nsc->cdev = disk_create(nsc->unit, &nsc->disk, &nvme_ops);
	nsc->cdev->si_drv1 = nsc;
	nsc->cdev->si_iosize_max = MAXPHYS;	/* XXX */
	disk_setdisktype(&nsc->disk, "ssd");

	bzero(&info, sizeof(info));
	info.d_media_blksize = nsc->blksize;
	info.d_media_blocks = nsc->idns.size;
	info.d_secpertrack = 1024;
	info.d_nheads = 1;
	info.d_secpercyl = info.d_secpertrack * info.d_nheads;
	info.d_ncylinders =  (u_int)(info.d_media_blocks / info.d_secpercyl);

	KKASSERT(sizeof(sc->idctlr.serialno) == 20);
	bzero(serial, sizeof(serial));
	bcopy(sc->idctlr.serialno, serial, sizeof(sc->idctlr.serialno));
	len = string_cleanup(serial, 1);

	ksnprintf(serial + len, sizeof(serial) - len, "-%u", nsc->nsid);

	info.d_serialno = serial;

	cap_gb = nsc->idns.size / (1024 * 1024 * 1024 / nsc->blksize);
	device_printf(sc->dev,
		"Disk nvme%d ns=%u "
		"blksize=%u lbacnt=%ju cap=%juGB serno=%s\n",
		nsc->unit, nsc->nsid,
		nsc->blksize, nsc->idns.size, cap_gb, serial);

	disk_setdiskinfo(&nsc->disk, &info);
	/* serial is copied and does not have to be persistent */
}

void
nvme_disk_detach(nvme_softns_t *nsc)
{
	disk_destroy(&nsc->disk);
	devstat_remove_entry(&nsc->stats);
}

static
int
nvme_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	nvme_softns_t *nsc = dev->si_drv1;
	nvme_softc_t *sc = nsc->sc;

	if (sc->flags & NVME_SC_UNLOADING)
		return ENXIO;

	atomic_add_long(&sc->opencnt, 1);

	return 0;
}

static
int
nvme_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	nvme_softns_t *nsc = dev->si_drv1;
	nvme_softc_t *sc = nsc->sc;

	atomic_add_long(&sc->opencnt, -1);

	return 0;
}

static int
nvme_ioctl(struct dev_ioctl_args *ap)
{
	return ENOIOCTL;
}

static int
nvme_strategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	nvme_softns_t *nsc = dev->si_drv1;

	nvme_strategy_core(nsc, ap->a_bio, nvme_sync_delay);

	return 0;
}

/*
 * Called from admin thread to requeue BIOs.  We must call
 * nvme_strategy_core() with delay = 0 to disable synchronous
 * optimizations to avoid deadlocking the admin thread.
 */
void
nvme_disk_requeues(nvme_softc_t *sc)
{
	nvme_softns_t *nsc;
	struct bio *bio;
	int i;

	for (i = 0; i < sc->nscmax; ++i) {
		nsc = sc->nscary[i];
		if (nsc == NULL || nsc->sc == NULL)
			continue;
		if (bioq_first(&nsc->bioq)) {
			lockmgr(&nsc->lk, LK_EXCLUSIVE);
			while ((bio = bioq_first(&nsc->bioq)) != NULL) {
				bioq_remove(&nsc->bioq, bio);
				lockmgr(&nsc->lk, LK_RELEASE);
				if (nvme_strategy_core(nsc, bio, 0))
					goto next;
				lockmgr(&nsc->lk, LK_EXCLUSIVE);
			}
			lockmgr(&nsc->lk, LK_RELEASE);
		}
next:
		;
	}
}


/*
 * Returns non-zero if no requests are available.
 */
static int
nvme_strategy_core(nvme_softns_t *nsc, struct bio *bio, int delay)
{
	nvme_softc_t *sc = nsc->sc;
	struct buf *bp = bio->bio_buf;
	uint64_t nlba;
	uint64_t secno;
	nvme_subqueue_t *subq;
	nvme_request_t *req;
	int nobytes;

	/*
	 * Calculate sector/extent
	 */
	secno = bio->bio_offset / nsc->blksize;
	nlba = bp->b_bcount / nsc->blksize;

	devstat_start_transaction(&nsc->stats);

	subq = NULL;
	req = NULL;
	nobytes = 0;

	/*
	 * Convert bio to low-level request
	 */
	switch (bp->b_cmd) {
	case BUF_CMD_READ:
		if (nlba == 0) {
			nobytes = 1;
			break;
		}
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_RDHIGH]];
		/* get_request does not need the subq lock */
		req = nvme_get_request(subq, NVME_IOCMD_READ,
				       bp->b_data, nlba * nsc->blksize);
		if (req == NULL)
			goto requeue;

		req->cmd.read.head.nsid = nsc->nsid;
		req->cmd.read.start_lba = secno;
		req->cmd.read.count_lba = nlba - 1;	/* 0's based */
		req->cmd.read.ioflags = 0; /* NVME_IOFLG_LR, NVME_IOFLG_FUA */
		req->cmd.read.dsm = 0;	   /* NVME_DSM_INCOMPRESSIBLE */
					   /* NVME_DSM_SEQREQ */
		break;
	case BUF_CMD_WRITE:
		if (nlba == 0) {
			nobytes = 1;
			break;
		}
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WRHIGH]];
		/* get_request does not need the subq lock */
		req = nvme_get_request(subq, NVME_IOCMD_WRITE,
				       bp->b_data, nlba * nsc->blksize);
		if (req == NULL)
			goto requeue;
		req->cmd.write.head.nsid = nsc->nsid;
		req->cmd.write.start_lba = secno;
		req->cmd.write.count_lba = nlba - 1;	/* 0's based */
		break;
	case BUF_CMD_FREEBLKS:
		if (nlba == 0) {
			nobytes = 1;
			break;
		}
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WRHIGH]];
		/* get_request does not need the subq lock */
		req = nvme_get_request(subq, NVME_IOCMD_WRITEZ, NULL, 0);
		if (req == NULL)
			goto requeue;
		req->cmd.writez.head.nsid = nsc->nsid;
		req->cmd.writez.start_lba = secno;
		req->cmd.writez.count_lba = nlba - 1;	/* 0's based */
		req->cmd.read.ioflags = 0; /* NVME_IOFLG_LR, NVME_IOFLG_FUA */
		req->cmd.read.dsm = 0;	   /* NVME_DSM_INCOMPRESSIBLE */
					   /* NVME_DSM_SEQREQ */
		break;
	case BUF_CMD_FLUSH:
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WRHIGH]];
		/* get_request does not need the subq lock */
		req = nvme_get_request(subq, NVME_IOCMD_FLUSH, NULL, 0);
		if (req == NULL)
			goto requeue;
		req->cmd.flush.head.nsid = nsc->nsid;
		break;
	default:
		break;
	}

	/*
	 * Submit the request
	 */
	if (req) {
		nvme_comqueue_t *comq;

		/* HACK OPTIMIZATIONS - TODO NEEDS WORK */

		/*
		 * Prevent callback from occurring if the synchronous
		 * delay optimization is enabled.
		 */
		if (delay == 0)
			req->callback = nvme_disk_callback;
		req->nsc = nsc;
		req->bio = bio;
		lockmgr(&subq->lk, LK_EXCLUSIVE);
		nvme_submit_request(req);	/* needs subq lock */
		BUF_KERNPROC(bp);		/* do before lock release */
		lockmgr(&subq->lk, LK_RELEASE);
		if (delay) {
			comq = req->comq;
			DELAY(delay);		/* XXX */
			lockmgr(&comq->lk, LK_EXCLUSIVE);
			nvme_poll_completions(comq, &comq->lk);
			if (req->state == NVME_REQ_SUBMITTED) {
				/*
				 * Didn't finish, do it the slow way
				 * (restore async completion).
				 */
				req->callback = nvme_disk_callback;
				lockmgr(&comq->lk, LK_RELEASE);
			} else {
				/*
				 * Jeeze, that was fast.
				 */
				nvme_disk_callback(req, &comq->lk);
				lockmgr(&comq->lk, LK_RELEASE);
			}
		} /* else async completion */
	} else if (nobytes) {
		devstat_end_transaction_buf(&nsc->stats, bp);
		biodone(bio);
	} else {
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		devstat_end_transaction_buf(&nsc->stats, bp);
		biodone(bio);
	}
	return 0;

	/*
	 * No requests were available, requeue the bio
	 */
requeue:
	BUF_KERNPROC(bp);
	lockmgr(&nsc->lk, LK_EXCLUSIVE);
	bioqdisksort(&nsc->bioq, bio);
	lockmgr(&nsc->lk, LK_RELEASE);
	return 1;
}

static
void
nvme_disk_callback(nvme_request_t *req, struct lock *lk)
{
	nvme_softns_t *nsc = req->nsc;
	struct bio *bio;
	struct buf *bp;
	int status;

	status = NVME_COMQ_STATUS_CODE_GET(req->res.tail.status);
	bio = req->bio;
	bp = bio->bio_buf;

	if (lk)					/* comq lock */
		lockmgr(lk, LK_RELEASE);
	nvme_put_request(req);			/* does not need subq lock */
	devstat_end_transaction_buf(&nsc->stats, bp);
	if (status) {
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	} else {
		biodone(bio);
	}
	if (lk)					/* comq lock */
		lockmgr(lk, LK_EXCLUSIVE);
}
