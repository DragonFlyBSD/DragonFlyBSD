/*-
 * Copyright (c) 2011, Bryan Venteicher <bryanv@daemoninthecloset.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD: src/sys/dev/virtio/block/virtio_blk.c,v 1.4 2012/04/16 18:29:12 grehan Exp $
 */

/* Driver for VirtIO block devices. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/serialize.h>
#include <sys/buf2.h>
#include <sys/rman.h>
#include <sys/disk.h>
#include <sys/devicestat.h>

#include <dev/virtual/virtio/virtio/virtio.h>
#include <dev/virtual/virtio/virtio/virtqueue.h>
#include "virtio_blk.h"

struct vtblk_request {
	struct virtio_blk_outhdr	vbr_hdr;
	struct bio			*vbr_bp;
	uint8_t				vbr_ack;

	TAILQ_ENTRY(vtblk_request)	vbr_link;
};

struct vtblk_softc {
	device_t		 	vtblk_dev;
	struct lwkt_serialize		vtblk_slz;
	uint64_t		 	vtblk_features;

#define VTBLK_FLAG_READONLY		0x0002
#define VTBLK_FLAG_DETACH		0x0004
#define VTBLK_FLAG_SUSPEND		0x0008
	uint32_t			vtblk_flags;

	struct virtqueue		*vtblk_vq;
	struct sglist			*vtblk_sglist;
	struct disk			vtblk_disk;
	cdev_t				cdev;
	struct devstat			stats;

	struct bio_queue_head	 	vtblk_bioq;
	TAILQ_HEAD(, vtblk_request)	vtblk_req_free;
	TAILQ_HEAD(, vtblk_request)	vtblk_req_ready;

	int			 	vtblk_sector_size;
	int			 	vtblk_max_nsegs;
	int				vtblk_unit;
	int			 	vtblk_request_count;

	struct vtblk_request		vtblk_dump_request;
};

static struct virtio_feature_desc vtblk_feature_desc[] = {
	{ VIRTIO_BLK_F_BARRIER,		"HostBarrier"	},
	{ VIRTIO_BLK_F_SIZE_MAX,	"MaxSegSize"	},
	{ VIRTIO_BLK_F_SEG_MAX,		"MaxNumSegs"	},
	{ VIRTIO_BLK_F_GEOMETRY,	"DiskGeometry"	},
	{ VIRTIO_BLK_F_RO,		"ReadOnly"	},
	{ VIRTIO_BLK_F_BLK_SIZE,	"BlockSize"	},
	{ VIRTIO_BLK_F_SCSI,		"SCSICmds"	},
	{ VIRTIO_BLK_F_FLUSH,		"FlushCmd"	},
	{ VIRTIO_BLK_F_TOPOLOGY,	"Topology"	},

	{ 0, NULL }
};

static int	vtblk_modevent(module_t, int, void *);

static int	vtblk_probe(device_t);
static int	vtblk_attach(device_t);
static int	vtblk_detach(device_t);
static int	vtblk_suspend(device_t);
static int	vtblk_resume(device_t);
static int	vtblk_shutdown(device_t);

static void	vtblk_negotiate_features(struct vtblk_softc *);
static int	vtblk_maximum_segments(struct vtblk_softc *,
				       struct virtio_blk_config *);
static int	vtblk_alloc_virtqueue(struct vtblk_softc *);
static void	vtblk_alloc_disk(struct vtblk_softc *,
				 struct virtio_blk_config *);
/*
 * Interface to the device switch.
 */
static d_open_t		vtblk_open;
static d_strategy_t	vtblk_strategy;
static d_dump_t		vtblk_dump;

static struct dev_ops vbd_disk_ops = {
	{ "vbd", 200, D_DISK | D_MPSAFE },
	.d_open		= vtblk_open,
	.d_close	= nullclose,
	.d_read		= physread,
	.d_write	= physwrite,
	.d_strategy	= vtblk_strategy,
	.d_dump		= vtblk_dump,
};

static void vtblk_startio(struct vtblk_softc *);
static struct vtblk_request *vtblk_bio_request(struct vtblk_softc *);
static int vtblk_execute_request(struct vtblk_softc *, struct vtblk_request *);

static int		vtblk_vq_intr(void *);
static void		vtblk_complete(void *);

static void		vtblk_stop(struct vtblk_softc *);

static void		vtblk_drain_vq(struct vtblk_softc *, int);
static void		vtblk_drain(struct vtblk_softc *);

static int		vtblk_alloc_requests(struct vtblk_softc *);
static void		vtblk_free_requests(struct vtblk_softc *);
static struct vtblk_request *vtblk_dequeue_request(struct vtblk_softc *);
static void		vtblk_enqueue_request(struct vtblk_softc *,
					      struct vtblk_request *);

static struct vtblk_request *vtblk_dequeue_ready(struct vtblk_softc *);
static void		vtblk_enqueue_ready(struct vtblk_softc *,
					    struct vtblk_request *);

static void		vtblk_bio_error(struct bio *, int);

/* Tunables. */
static int vtblk_no_ident = 0;
TUNABLE_INT("hw.vtblk.no_ident", &vtblk_no_ident);

/* Features desired/implemented by this driver. */
#define VTBLK_FEATURES \
    (VIRTIO_BLK_F_BARRIER		| \
     VIRTIO_BLK_F_SIZE_MAX		| \
     VIRTIO_BLK_F_SEG_MAX		| \
     VIRTIO_BLK_F_GEOMETRY		| \
     VIRTIO_BLK_F_RO			| \
     VIRTIO_BLK_F_BLK_SIZE		| \
     VIRTIO_BLK_F_FLUSH)

/*
 * Each block request uses at least two segments - one for the header
 * and one for the status.
 */
#define VTBLK_MIN_SEGMENTS	2

static device_method_t vtblk_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtblk_probe),
	DEVMETHOD(device_attach,	vtblk_attach),
	DEVMETHOD(device_detach,	vtblk_detach),
	DEVMETHOD(device_suspend,	vtblk_suspend),
	DEVMETHOD(device_resume,	vtblk_resume),
	DEVMETHOD(device_shutdown,	vtblk_shutdown),

	DEVMETHOD_END
};

static driver_t vtblk_driver = {
	"vtblk",
	vtblk_methods,
	sizeof(struct vtblk_softc)
};
static devclass_t vtblk_devclass;

DRIVER_MODULE(virtio_blk, virtio_pci, vtblk_driver, vtblk_devclass,
	      vtblk_modevent, NULL);
MODULE_VERSION(virtio_blk, 1);
MODULE_DEPEND(virtio_blk, virtio, 1, 1, 1);

static int
vtblk_modevent(module_t mod, int type, void *unused)
{
	int error;

	error = 0;

	switch (type) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtblk_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_BLOCK)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Block Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtblk_attach(device_t dev)
{
	struct vtblk_softc *sc;
	struct virtio_blk_config blkcfg;
	int error;

	sc = device_get_softc(dev);
	sc->vtblk_dev = dev;
	sc->vtblk_unit = device_get_unit(dev);

	lwkt_serialize_init(&sc->vtblk_slz);

	bioq_init(&sc->vtblk_bioq);
	TAILQ_INIT(&sc->vtblk_req_free);
	TAILQ_INIT(&sc->vtblk_req_ready);

	virtio_set_feature_desc(dev, vtblk_feature_desc);
	vtblk_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_BLK_F_RO))
		sc->vtblk_flags |= VTBLK_FLAG_READONLY;

	/* Get local copy of config. */
	virtio_read_device_config(dev, 0, &blkcfg,
				  sizeof(struct virtio_blk_config));

	/*
	 * With the current sglist(9) implementation, it is not easy
	 * for us to support a maximum segment size as adjacent
	 * segments are coalesced. For now, just make sure it's larger
	 * than the maximum supported transfer size.
	 */
	if (virtio_with_feature(dev, VIRTIO_BLK_F_SIZE_MAX)) {
		if (blkcfg.size_max < MAXPHYS) {
			error = ENOTSUP;
			device_printf(dev, "host requires unsupported "
			    "maximum segment size feature\n");
			goto fail;
		}
	}

	sc->vtblk_max_nsegs = vtblk_maximum_segments(sc, &blkcfg);
        if (sc->vtblk_max_nsegs <= VTBLK_MIN_SEGMENTS) {
		error = EINVAL;
		device_printf(dev, "fewer than minimum number of segments "
		    "allowed: %d\n", sc->vtblk_max_nsegs);
		goto fail;
	}

	/*
	 * Allocate working sglist. The number of segments may be too
	 * large to safely store on the stack.
	 */
	sc->vtblk_sglist = sglist_alloc(sc->vtblk_max_nsegs, M_NOWAIT);
	if (sc->vtblk_sglist == NULL) {
		error = ENOMEM;
		device_printf(dev, "cannot allocate sglist\n");
		goto fail;
	}

	error = vtblk_alloc_virtqueue(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueue\n");
		goto fail;
	}

	error = vtblk_alloc_requests(sc);
	if (error) {
		device_printf(dev, "cannot preallocate requests\n");
		goto fail;
	}

	vtblk_alloc_disk(sc, &blkcfg);

	error = virtio_setup_intr(dev, &sc->vtblk_slz);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupt\n");
		goto fail;
	}

	virtqueue_enable_intr(sc->vtblk_vq);

fail:
	if (error)
		vtblk_detach(dev);

	return (error);
}

static int
vtblk_detach(device_t dev)
{
	struct vtblk_softc *sc;

	sc = device_get_softc(dev);

	lwkt_serialize_enter(&sc->vtblk_slz);
	sc->vtblk_flags |= VTBLK_FLAG_DETACH;
	if (device_is_attached(dev))
		vtblk_stop(sc);
	lwkt_serialize_exit(&sc->vtblk_slz);

	vtblk_drain(sc);

	if (sc->vtblk_sglist != NULL) {
		sglist_free(sc->vtblk_sglist);
		sc->vtblk_sglist = NULL;
	}

	return (0);
}

static int
vtblk_suspend(device_t dev)
{
	struct vtblk_softc *sc;

	sc = device_get_softc(dev);

	lwkt_serialize_enter(&sc->vtblk_slz);
	sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
	/* TODO Wait for any inflight IO to complete? */
	lwkt_serialize_exit(&sc->vtblk_slz);

	return (0);
}

static int
vtblk_resume(device_t dev)
{
	struct vtblk_softc *sc;

	sc = device_get_softc(dev);

	lwkt_serialize_enter(&sc->vtblk_slz);
	sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
	/* TODO Resume IO? */
	lwkt_serialize_exit(&sc->vtblk_slz);

	return (0);
}

static int
vtblk_shutdown(device_t dev)
{
	return (0);
}

static int
vtblk_open(struct dev_open_args *ap)
{
	struct vtblk_softc *sc;
	cdev_t dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENXIO);

	return (sc->vtblk_flags & VTBLK_FLAG_DETACH ? ENXIO : 0);
}

static int
vtblk_dump(struct dev_dump_args *ap)
{
	/* XXX */
	return (ENXIO);
}

static int
vtblk_strategy(struct dev_strategy_args *ap)
{
	struct vtblk_softc *sc;
	cdev_t dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;

	if (sc == NULL) {
		vtblk_bio_error(bio, EINVAL);
		return EINVAL;
	}

	/*
	 * Fail any write if RO. Unfortunately, there does not seem to
	 * be a better way to report our readonly'ness to GEOM above.
	 *
	 * XXX: Is that true in DFly?
	 */
	if (sc->vtblk_flags & VTBLK_FLAG_READONLY &&
	    (bp->b_cmd == BUF_CMD_READ || bp->b_cmd == BUF_CMD_FLUSH)) {
		vtblk_bio_error(bio, EROFS);
		return (EINVAL);
	}

	lwkt_serialize_enter(&sc->vtblk_slz);
	if ((sc->vtblk_flags & VTBLK_FLAG_DETACH) == 0) {
		devstat_start_transaction(&sc->stats);
		bioqdisksort(&sc->vtblk_bioq, bio);
		vtblk_startio(sc);
	} else {
		vtblk_bio_error(bio, ENXIO);
	}
	lwkt_serialize_exit(&sc->vtblk_slz);
	return 0;
}

static void
vtblk_negotiate_features(struct vtblk_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtblk_dev;
	features = VTBLK_FEATURES;

	sc->vtblk_features = virtio_negotiate_features(dev, features);
}

static int
vtblk_maximum_segments(struct vtblk_softc *sc, struct virtio_blk_config *blkcfg)
{
	device_t dev;
	int nsegs;

	dev = sc->vtblk_dev;
	nsegs = VTBLK_MIN_SEGMENTS;

	if (virtio_with_feature(dev, VIRTIO_BLK_F_SEG_MAX)) {
		nsegs += MIN(blkcfg->seg_max, MAXPHYS / PAGE_SIZE + 1);
	} else {
		nsegs += 1;
	}

	return (nsegs);
}

static int
vtblk_alloc_virtqueue(struct vtblk_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info;

	dev = sc->vtblk_dev;

	VQ_ALLOC_INFO_INIT(&vq_info, sc->vtblk_max_nsegs,
			   vtblk_vq_intr, sc, &sc->vtblk_vq,
			   "%s request", device_get_nameunit(dev));

	return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}

static void
vtblk_alloc_disk(struct vtblk_softc *sc, struct virtio_blk_config *blkcfg)
{

	struct disk_info info;

	/* construct the disk_info */
	bzero(&info, sizeof(info));

	if (virtio_with_feature(sc->vtblk_dev, VIRTIO_BLK_F_BLK_SIZE))
		sc->vtblk_sector_size = blkcfg->blk_size;
	else
		sc->vtblk_sector_size = DEV_BSIZE;

	info.d_media_blksize = sc->vtblk_sector_size;
	info.d_media_blocks = blkcfg->capacity;

	info.d_ncylinders = blkcfg->geometry.cylinders;
	info.d_nheads = blkcfg->geometry.heads;
	info.d_secpertrack = blkcfg->geometry.sectors;

	info.d_secpercyl = info.d_secpertrack * info.d_nheads;

	devstat_add_entry(&sc->stats, "vbd", device_get_unit(sc->vtblk_dev),
			  DEV_BSIZE, DEVSTAT_ALL_SUPPORTED,
			  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
			  DEVSTAT_PRIORITY_DISK);

	/* attach a generic disk device to ourselves */
	sc->cdev = disk_create(device_get_unit(sc->vtblk_dev), &sc->vtblk_disk,
			       &vbd_disk_ops);

	sc->cdev->si_drv1 = sc;
	disk_setdiskinfo(&sc->vtblk_disk, &info);
}

static void
vtblk_startio(struct vtblk_softc *sc)
{
	struct virtqueue *vq;
	struct vtblk_request *req;
	int enq;

	vq = sc->vtblk_vq;
	enq = 0;

	ASSERT_SERIALIZED(&sc->vtblk_slz);
	
	if (sc->vtblk_flags & VTBLK_FLAG_SUSPEND)
		return;

	while (!virtqueue_full(vq)) {
		if ((req = vtblk_dequeue_ready(sc)) == NULL)
			req = vtblk_bio_request(sc);
		if (req == NULL)
			break;

		if (vtblk_execute_request(sc, req) != 0) {
			vtblk_enqueue_ready(sc, req);
			break;
		}

		enq++;
	}

	if (enq > 0)
		virtqueue_notify(vq, &sc->vtblk_slz);
}

static struct vtblk_request *
vtblk_bio_request(struct vtblk_softc *sc)
{
	struct bio_queue_head *bioq;
	struct vtblk_request *req;
	struct bio *bio;
	struct buf *bp;

	bioq = &sc->vtblk_bioq;

	if (bioq_first(bioq) == NULL)
		return (NULL);

	req = vtblk_dequeue_request(sc);
	if (req == NULL)
		return (NULL);

	bio = bioq_takefirst(bioq);
	req->vbr_bp = bio;
	req->vbr_ack = -1;
	req->vbr_hdr.ioprio = 1;
	bp = bio->bio_buf;

	switch (bp->b_cmd) {
	case BUF_CMD_FLUSH:
		req->vbr_hdr.type = VIRTIO_BLK_T_FLUSH;
		break;
	case BUF_CMD_READ:
		req->vbr_hdr.type = VIRTIO_BLK_T_IN;
		req->vbr_hdr.sector = bio->bio_offset / DEV_BSIZE;
		break;
	case BUF_CMD_WRITE:
		req->vbr_hdr.type = VIRTIO_BLK_T_OUT;
		req->vbr_hdr.sector = bio->bio_offset / DEV_BSIZE;
		break;
	default:
		KASSERT(0, ("bio with unhandled cmd: %d", bp->b_cmd));
		req->vbr_hdr.type = -1;
		break;
	}

	if (bp->b_flags & B_ORDERED)
		req->vbr_hdr.type |= VIRTIO_BLK_T_BARRIER;

	return (req);
}

static int
vtblk_execute_request(struct vtblk_softc *sc, struct vtblk_request *req)
{
	struct sglist *sg;
	struct bio *bio;
	struct buf *bp;
	int writable, error;

	sg = sc->vtblk_sglist;
	bio = req->vbr_bp;
	bp = bio->bio_buf;
	writable = 0;

	/*
	 * sglist is live throughout this subroutine.
	 */
	sglist_reset(sg);
	
	error = sglist_append(sg, &req->vbr_hdr,
			      sizeof(struct virtio_blk_outhdr));
	KASSERT(error == 0, ("error adding header to sglist"));
	KASSERT(sg->sg_nseg == 1,
	    ("header spanned multiple segments: %d", sg->sg_nseg));

	if (bp->b_cmd == BUF_CMD_READ || bp->b_cmd == BUF_CMD_WRITE) {
		error = sglist_append(sg, bp->b_data, bp->b_bcount);
		KASSERT(error == 0, ("error adding buffer to sglist"));

		/* BUF_CMD_READ means the host writes into our buffer. */
		if (bp->b_cmd == BUF_CMD_READ)
			writable += sg->sg_nseg - 1;
	}

	error = sglist_append(sg, &req->vbr_ack, sizeof(uint8_t));
	KASSERT(error == 0, ("error adding ack to sglist"));
	writable++;

	KASSERT(sg->sg_nseg >= VTBLK_MIN_SEGMENTS,
	    ("fewer than min segments: %d", sg->sg_nseg));

	error = virtqueue_enqueue(sc->vtblk_vq, req, sg,
				  sg->sg_nseg - writable, writable);

	sglist_reset(sg);

	return (error);
}

static int
vtblk_vq_intr(void *xsc)
{
	vtblk_complete(xsc);

	return (1);
}

static void
vtblk_complete(void *arg)
{
	struct vtblk_softc *sc;
	struct vtblk_request *req;
	struct virtqueue *vq;
	struct bio *bio;
	struct buf *bp;
	
	sc = arg;
	vq = sc->vtblk_vq;

	lwkt_serialize_handler_disable(&sc->vtblk_slz);
	virtqueue_disable_intr(sc->vtblk_vq);
	ASSERT_SERIALIZED(&sc->vtblk_slz);

retry:
	if (sc->vtblk_flags & VTBLK_FLAG_DETACH)
		return;

	while ((req = virtqueue_dequeue(vq, NULL)) != NULL) {
		bio = req->vbr_bp;
		bp = bio->bio_buf;

		if (req->vbr_ack == VIRTIO_BLK_S_OK)
			bp->b_resid = 0;
		else {
			bp->b_flags |= B_ERROR;
			if (req->vbr_ack == VIRTIO_BLK_S_UNSUPP) {
				bp->b_error = ENOTSUP;
			} else {
				bp->b_error = EIO;
			}
		}

		devstat_end_transaction_buf(&sc->stats, bio->bio_buf);

		lwkt_serialize_exit(&sc->vtblk_slz);
		/*
		 * Unlocking the controller around biodone() does not allow
		 * processing further device interrupts; when we queued
		 * vtblk_complete, we disabled interrupts. It will allow
		 * concurrent vtblk_strategy/_startio command dispatches.
		 */
		biodone(bio);
		lwkt_serialize_enter(&sc->vtblk_slz);

		vtblk_enqueue_request(sc, req);
	}

	vtblk_startio(sc);

	if (virtqueue_enable_intr(vq) != 0) {
		/* 
		 * If new virtqueue entries appeared immediately after
		 * enabling interrupts, process them now. Release and
		 * retake softcontroller lock to try to avoid blocking
		 * I/O dispatch for too long.
		 */
		virtqueue_disable_intr(vq);
		goto retry;
	}
	lwkt_serialize_handler_enable(&sc->vtblk_slz);
}

static void
vtblk_stop(struct vtblk_softc *sc)
{
	virtqueue_disable_intr(sc->vtblk_vq);
	virtio_stop(sc->vtblk_dev);
}

static void
vtblk_drain_vq(struct vtblk_softc *sc, int skip_done)
{
	struct virtqueue *vq;
	struct vtblk_request *req;
	int last;

	vq = sc->vtblk_vq;
	last = 0;

	while ((req = virtqueue_drain(vq, &last)) != NULL) {
		if (!skip_done)
			vtblk_bio_error(req->vbr_bp, ENXIO);

		vtblk_enqueue_request(sc, req);
	}

	KASSERT(virtqueue_empty(vq), ("virtqueue not empty"));
}

static void
vtblk_drain(struct vtblk_softc *sc)
{
	struct bio_queue_head *bioq;
	struct vtblk_request *req;
	struct bio *bp;

	bioq = &sc->vtblk_bioq;

	if (sc->vtblk_vq != NULL)
		vtblk_drain_vq(sc, 0);

	while ((req = vtblk_dequeue_ready(sc)) != NULL) {
		vtblk_bio_error(req->vbr_bp, ENXIO);
		vtblk_enqueue_request(sc, req);
	}

	while (bioq_first(bioq) != NULL) {
		bp = bioq_takefirst(bioq);
		vtblk_bio_error(bp, ENXIO);
	}

	vtblk_free_requests(sc);
}

static int
vtblk_alloc_requests(struct vtblk_softc *sc)
{
	struct vtblk_request *req;
	int i, nreqs;

	nreqs = virtqueue_size(sc->vtblk_vq);

	/*
	 * Preallocate sufficient requests to keep the virtqueue full. Each
	 * request consumes VTBLK_MIN_SEGMENTS or more descriptors so reduce
	 * the number allocated when indirect descriptors are not available.
	 */
	nreqs /= VTBLK_MIN_SEGMENTS;

	for (i = 0; i < nreqs; i++) {
		req = kmalloc(sizeof(struct vtblk_request), M_DEVBUF, M_WAITOK);

		sc->vtblk_request_count++;
		vtblk_enqueue_request(sc, req);
	}

	return (0);
}

static void
vtblk_free_requests(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	while ((req = vtblk_dequeue_request(sc)) != NULL) {
		sc->vtblk_request_count--;
		kfree(req, M_DEVBUF);
	}

	KASSERT(sc->vtblk_request_count == 0, ("leaked requests"));
}

static struct vtblk_request *
vtblk_dequeue_request(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	req = TAILQ_FIRST(&sc->vtblk_req_free);
	if (req != NULL)
		TAILQ_REMOVE(&sc->vtblk_req_free, req, vbr_link);

	return (req);
}

static void
vtblk_enqueue_request(struct vtblk_softc *sc, struct vtblk_request *req)
{
	bzero(req, sizeof(struct vtblk_request));
	TAILQ_INSERT_HEAD(&sc->vtblk_req_free, req, vbr_link);
}

static struct vtblk_request *
vtblk_dequeue_ready(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	req = TAILQ_FIRST(&sc->vtblk_req_ready);
	if (req != NULL)
		TAILQ_REMOVE(&sc->vtblk_req_ready, req, vbr_link);

	return (req);
}

static void
vtblk_enqueue_ready(struct vtblk_softc *sc, struct vtblk_request *req)
{
	TAILQ_INSERT_HEAD(&sc->vtblk_req_ready, req, vbr_link);
}

static void
vtblk_bio_error(struct bio *bp, int error)
{
	biodone(bp);
}
