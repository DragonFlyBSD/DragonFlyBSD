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
static const char *nvme_status_string(nvme_status_buf_t *buf,
			int type, int code);

static d_open_t nvme_open;
static d_close_t nvme_close;
static d_ioctl_t nvme_ioctl;
static d_strategy_t nvme_strategy;
static d_dump_t nvme_dump;

static struct dev_ops nvme_ops = {
	{ "nvme", 0, D_DISK | D_MPSAFE | D_CANFREE | D_TRACKCLOSE | D_KVABIO },
	.d_open =       nvme_open,
	.d_close =      nvme_close,
	.d_read =       physread,
	.d_dump =       nvme_dump,
	.d_write =      physwrite,
	.d_ioctl =      nvme_ioctl,
	.d_strategy =   nvme_strategy,
};

static struct krate krate_nvmeio = { .freq = 1 };

__read_mostly static int nvme_sync_delay = 0;
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
	if (nsc->cdev) {
		disk_destroy(&nsc->disk);
		devstat_remove_entry(&nsc->stats);
	}
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
	cdev_t dev = ap->a_head.a_dev;
	nvme_softns_t *nsc = dev->si_drv1;
	nvme_softc_t *sc = nsc->sc;
	int error;

	switch(ap->a_cmd) {
	case NVMEIOCGETLOG:
		error = nvme_getlog_ioctl(sc, (void *)ap->a_data);
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return error;
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
 *
 * WARNING! We are using the KVABIO API and must not access memory
 *	    through bp->b_data without first calling bkvasync(bp).
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
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_RD]];
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
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WR]];
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
		if (nlba > 65536) {
			/* will cause INVAL error */
			break;
		}
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WR]];
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
		subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WR]];
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
		 *
		 * NOTE: subq lock does not protect the I/O (completion
		 *	 only needs the comq lock).
		 */
		if (delay == 0)
			req->callback = nvme_disk_callback;
		req->nsc = nsc;
		req->bio = bio;
		BUF_KERNPROC(bp);		/* do before submit */
		lockmgr(&subq->lk, LK_EXCLUSIVE);
		nvme_submit_request(req);	/* needs subq lock */
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
	 * No requests were available, requeue the bio.
	 *
	 * The nvme_get_request() call armed the requeue signal but
	 * it is possible that it was picked up too quickly.  If it
	 * was, signal the admin thread ourselves.  This case will occur
	 * relatively rarely and only under heavy I/O conditions so we
	 * don't have to be entirely efficient about dealing with it.
	 */
requeue:
	BUF_KERNPROC(bp);
	lockmgr(&nsc->lk, LK_EXCLUSIVE);
	bioqdisksort(&nsc->bioq, bio);
	lockmgr(&nsc->lk, LK_RELEASE);
	if (atomic_swap_int(&subq->signal_requeue, 1) == 0) {
		atomic_swap_int(&subq->signal_requeue, 0);
                atomic_set_int(&subq->sc->admin_signal, ADMIN_SIG_REQUEUE);
                wakeup(&subq->sc->admin_signal);
	}
	return 1;
}

static
void
nvme_disk_callback(nvme_request_t *req, struct lock *lk)
{
	nvme_softns_t *nsc = req->nsc;
	struct bio *bio;
	struct buf *bp;
	int code;
	int type;

	code = NVME_COMQ_STATUS_CODE_GET(req->res.tail.status);
	type = NVME_COMQ_STATUS_TYPE_GET(req->res.tail.status);
	bio = req->bio;
	bp = bio->bio_buf;

	if (lk)					/* comq lock */
		lockmgr(lk, LK_RELEASE);
	nvme_put_request(req);			/* does not need subq lock */
	devstat_end_transaction_buf(&nsc->stats, bp);

	if (code) {
		nvme_status_buf_t sb;

		krateprintf(&krate_nvmeio,
			    "%s%d: %s error nvme-code %s\n",
			    device_get_name(nsc->sc->dev),
			    device_get_unit(nsc->sc->dev),
			    buf_cmd_name(bp),
			    nvme_status_string(&sb, type, code));
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	} else {
		bp->b_resid = 0;
		biodone(bio);
	}
	if (lk)					/* comq lock */
		lockmgr(lk, LK_EXCLUSIVE);
}

int
nvme_alloc_disk_unit(void)
{
	static int unit_counter = 0;
	int unit;

	unit = atomic_fetchadd_int(&unit_counter, 1);

	return unit;
}

static int
nvme_dump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	nvme_softns_t *nsc = dev->si_drv1;
	nvme_softc_t *sc = nsc->sc;
	uint64_t nlba;
	uint64_t secno;
	nvme_subqueue_t *subq;
	nvme_comqueue_t *comq;
	nvme_request_t *req;
	int didlock;

	/*
	 * Calculate sector/extent
	 */
	secno = ap->a_offset / nsc->blksize;
	nlba = ap->a_length / nsc->blksize;

	subq = &sc->subqueues[sc->qmap[mycpuid][NVME_QMAP_WR]];

	if (nlba) {
		/*
		 * Issue a WRITE
		 *
		 * get_request does not need the subq lock.
		 */
		req = nvme_get_dump_request(subq, NVME_IOCMD_WRITE,
				       ap->a_virtual, nlba * nsc->blksize);
		req->cmd.write.head.nsid = nsc->nsid;
		req->cmd.write.start_lba = secno;
		req->cmd.write.count_lba = nlba - 1;	/* 0's based */
	} else {
		/*
		 * Issue a FLUSH
		 *
		 * get_request does not need the subq lock.
		 */
		req = nvme_get_dump_request(subq, NVME_IOCMD_FLUSH, NULL, 0);
		req->cmd.flush.head.nsid = nsc->nsid;
	}

	/*
	 * Prevent callback from occurring if the synchronous
	 * delay optimization is enabled.
	 */
	req->callback = NULL;
	req->nsc = nsc;

	/*
	 * 500 x 1uS poll wait on lock.  We might be the idle thread, so
	 * we can't safely block during a dump.
	 */
	didlock = 500;
	while (lockmgr(&subq->lk, LK_EXCLUSIVE | LK_NOWAIT) != 0) {
		if (--didlock == 0)
			break;
		tsc_delay(1000);	/* 1uS */
		lwkt_switch();
	}
	nvme_submit_request(req);	/* needs subq lock */
	if (didlock)
		lockmgr(&subq->lk, LK_RELEASE);

	comq = req->comq;
	nvme_poll_request(req);
	nvme_put_dump_request(req);		/* does not need subq lock */

	/*
	 * Shut the nvme controller down nicely when we finish the dump.
	 * We should to do this whether we are in a panic or not because
	 * frankly the dump is overwriting swap space, thus the system is
	 * probably not stable.
	 */
	if (nlba == 0)
		nvme_issue_shutdown(sc, 1);
	return 0;
}

static
const char *
nvme_status_string(nvme_status_buf_t *sb, int type, int code)
{
	const char *cstr = NULL;

	switch(type) {
	case NVME_STATUS_TYPE_GENERIC:
		switch(code) {
		case NVME_CODE_SUCCESS:
			cstr = "success";
			break;
		case NVME_CODE_BADOP:
			cstr = "badop";
			break;
		case NVME_CODE_BADFIELD:
			cstr = "badfield";
			break;
		case NVME_CODE_IDCONFLICT:
			cstr = "idconflict";
			break;
		case NVME_CODE_BADXFER:
			cstr = "badxfer";
			break;
		case NVME_CODE_ABORTED_PWRLOSS:
			cstr = "aborted-powerloss";
			break;
		case NVME_CODE_INTERNAL:
			cstr = "internal";
			break;
		case NVME_CODE_ABORTED_ONREQ:
			cstr = "aborted-onreq";
			break;
		case NVME_CODE_ABORTED_SQDEL:
			cstr = "aborted-sqdel";
			break;
		case NVME_CODE_ABORTED_FUSEFAIL:
			cstr = "aborted-fusefail";
			break;
		case NVME_CODE_ABORTED_FUSEMISSING:
			cstr = "aborted-fusemissing";
			break;
		case NVME_CODE_BADNAMESPACE:
			cstr = "badnamespace";
			break;
		case NVME_CODE_SEQERROR:
			cstr = "seqerror";
			break;
		case NVME_CODE_BADSGLSEG:
			cstr = "badsgl-seg";
			break;
		case NVME_CODE_BADSGLCNT:
			cstr = "badsgl-cnt";
			break;
		case NVME_CODE_BADSGLLEN:
			cstr = "badsgl-len";
			break;
		case NVME_CODE_BADSGLMLEN:
			cstr = "badsgl-mlen";
			break;
		case NVME_CODE_BADSGLTYPE:
			cstr = "badsgl-type";
			break;
		case NVME_CODE_BADMEMBUFUSE:
			cstr = "badmem-bufuse";
			break;
		case NVME_CODE_BADPRPOFF:
			cstr = "bad-prpoff";
			break;

		case NVME_CODE_ATOMICWUOVFL:
			cstr = "atomic-wuovfl";
			break;
		case NVME_CODE_LBA_RANGE:
			cstr = "lba-range";
			break;
		case NVME_CODE_CAP_EXCEEDED:
			cstr = "cap-exceeded";
			break;
		case NVME_CODE_NAM_NOT_READY:
			cstr = "nam-not-ready";
			break;
		case NVME_CODE_RSV_CONFLICT:
			cstr = "rsv-conflict";
			break;
		case NVME_CODE_FMT_IN_PROG:
			cstr = "fmt-in-prog";
			break;
		default:
			cstr = "unknown";
			break;
		}
		ksnprintf(sb->buf, sizeof(sb->buf),
			  "type=generic code=%s(%04x)", cstr, code);
		break;
	case NVME_STATUS_TYPE_SPECIFIC:
		switch(code) {
		case NVME_CSSCODE_BADCOMQ:
			cstr = "bad-comq";
			break;
		case NVME_CSSCODE_BADQID:
			cstr = "bad-qid";
			break;
		case NVME_CSSCODE_BADQSIZE:
			cstr = "bad-qsize";
			break;
		case NVME_CSSCODE_ABORTLIM:
			cstr = "abort-lim";
			break;
		case NVME_CSSCODE_RESERVED04 :
			cstr = "unknown";
			break;
		case NVME_CSSCODE_ASYNCEVENTLIM:
			cstr = "async-event-lim";
			break;
		case NVME_CSSCODE_BADFWSLOT:
			cstr = "bad-fwslot";
			break;
		case NVME_CSSCODE_BADFWIMAGE:
			cstr = "bad-fwimage";
			break;
		case NVME_CSSCODE_BADINTRVECT:
			cstr = "bad-intrvect";
			break;
		case NVME_CSSCODE_BADLOGPAGE:
			cstr = "bad-logpage";
			break;
		case NVME_CSSCODE_BADFORMAT:
			cstr = "bad-format";
			break;
		case NVME_CSSCODE_FW_NEEDSCONVRESET:
			cstr = "needs-convreset";
			break;
		case NVME_CSSCODE_BADQDELETE:
			cstr = "bad-qdelete";
			break;
		case NVME_CSSCODE_FEAT_NOT_SAVEABLE:
			cstr = "feat-not-saveable";
			break;
		case NVME_CSSCODE_FEAT_NOT_CHGABLE:
			cstr = "feat-not-changeable";
			break;
		case NVME_CSSCODE_FEAT_NOT_NSSPEC:
			cstr = "feat-not-nsspec";
			break;
		case NVME_CSSCODE_FW_NEEDSSUBRESET:
			cstr = "fw-needs-subreset";
			break;
		case NVME_CSSCODE_FW_NEEDSRESET:
			cstr = "fw-needs-reset";
			break;
		case NVME_CSSCODE_FW_NEEDSMAXTVIOLATE:
			cstr = "fw-needsmaxviolate";
			break;
		case NVME_CSSCODE_FW_PROHIBITED:
			cstr = "fw-prohibited";
			break;
		case NVME_CSSCODE_RANGE_OVERLAP:
			cstr = "range-overlap";
			break;
		case NVME_CSSCODE_NAM_INSUFF_CAP:
			cstr = "name-insufficient-cap";
			break;
		case NVME_CSSCODE_NAM_ID_UNAVAIL:
			cstr = "name-id-unavail";
			break;
		case NVME_CSSCODE_RESERVED17:
			cstr = "unknown";
			break;
		case NVME_CSSCODE_NAM_ALREADY_ATT:
			cstr = "name-already-att";
			break;
		case NVME_CSSCODE_NAM_IS_PRIVATE:
			cstr = "name-is-private";
			break;
		case NVME_CSSCODE_NAM_NOT_ATT:
			cstr = "name-not-att";
			break;
		case NVME_CSSCODE_NO_THIN_PROVISION:
			cstr = "no-thin-provision";
			break;
		case NVME_CSSCODE_CTLR_LIST_INVALID:
			cstr = "controller-list-invalid";
			break;

		case NVME_CSSCODE_ATTR_CONFLICT:
			cstr = "attr-conflict";
			break;
		case NVME_CSSCODE_BADPROTINFO:
			cstr = "bad-prot-info";
			break;
		case NVME_CSSCODE_WRITE_TO_RDONLY:
			cstr = "write-to-readonly";
			break;
		default:
			cstr = "unknown";
			break;
		}
		ksnprintf(sb->buf, sizeof(sb->buf),
			  "type=specific code=%s(%04x)", cstr, code);
		break;
	case NVME_STATUS_TYPE_MEDIA:
		switch(code) {
		case NVME_MEDCODE_WRITE_FAULT:
			cstr = "write-fault";
			break;
		case NVME_MEDCODE_UNRECOV_READ_ERROR:
			cstr = "unrecoverable-read-error";
			break;
		case NVME_MEDCODE_ETOE_GUARD_CHK:
			cstr = "etoe-guard-check";
			break;
		case NVME_MEDCODE_ETOE_APPTAG_CHK:
			cstr = "etoe-apptag-check";
			break;
		case NVME_MEDCODE_ETOE_REFTAG_CHK:
			cstr = "etoe-reftag-check";
			break;
		case NVME_MEDCODE_COMPARE_FAILURE:
			cstr = "compare-failure";
			break;
		case NVME_MEDCODE_ACCESS_DENIED:
			cstr = "access-denied";
			break;
		case NVME_MEDCODE_UNALLOCATED:
			cstr = "unallocated";
			break;
		default:
			cstr = "unknown";
			break;
		}
		ksnprintf(sb->buf, sizeof(sb->buf),
			  "type=media code=%s(%04x)", cstr, code);
		break;
	case NVME_STATUS_TYPE_VENDOR:
		ksnprintf(sb->buf, sizeof(sb->buf),
			  "type=vendor code=%04x", code);
		break;
	default:
		ksnprintf(sb->buf, sizeof(sb->buf),
			  "type=%02x code=%04x", type, code);
		break;
	}
	return sb->buf;
}
