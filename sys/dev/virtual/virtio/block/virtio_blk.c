/*-
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
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
 * $FreeBSD: head/sys/dev/virtio/block/virtio_blk.c 252707 2013-07-04 17:57:26Z bryanv $
 */

/* Driver for VirtIO block devices. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>
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
	struct virtio_blk_outhdr	 vbr_hdr __aligned(16);
	struct bio			*vbr_bio;
	uint8_t				 vbr_ack;

	SLIST_ENTRY(vtblk_request)	 vbr_link;
};

enum vtblk_cache_mode {
	VTBLK_CACHE_WRITETHROUGH,
	VTBLK_CACHE_WRITEBACK,
	VTBLK_CACHE_MAX
};

struct vtblk_queue {
	struct vtblk_softc	*vtblk_sc;
	struct virtqueue	*vtblk_vq;
	struct sglist		*vtblk_sglist;
	struct bio_queue_head	 vtblk_bioq;
	SLIST_HEAD(, vtblk_request)
				 vtblk_req_free;
	struct lwkt_serialize	 vtblk_slz;
};

struct vtblk_softc {
	device_t		 vtblk_dev;
	uint64_t		 vtblk_features;
	uint32_t		 vtblk_flags;
#define VTBLK_FLAG_INDIRECT	0x0001
#define VTBLK_FLAG_READONLY	0x0002
#define VTBLK_FLAG_DETACH	0x0004
#define VTBLK_FLAG_SUSPEND	0x0008
#define VTBLK_FLAG_DUMPING	0x0010
#define VTBLK_FLAG_WC_CONFIG	0x0020

	struct disk		 vtblk_disk;
	cdev_t			 cdev;
	struct devstat		 stats;

	struct vtblk_queue	 vtblk_queues[SMP_MAXCPU];
	u_int			 vtblk_nqs;
	u_int			 vtblk_nintrs;
	int			 vtblk_vqmap[SMP_MAXCPU];

	int			 vtblk_sector_size;
	int			 vtblk_max_nsegs;
	int			 vtblk_request_count;
	enum vtblk_cache_mode	 vtblk_write_cache;

	struct vtblk_request	 vtblk_dump_request;
};

static struct virtio_feature_desc vtblk_feature_desc[] = {
	{ VIRTIO_BLK_F_BARRIER,		"HostBarrier"	},
	{ VIRTIO_BLK_F_SIZE_MAX,	"MaxSegSize"	},
	{ VIRTIO_BLK_F_SEG_MAX,		"MaxNumSegs"	},
	{ VIRTIO_BLK_F_GEOMETRY,	"DiskGeometry"	},
	{ VIRTIO_BLK_F_RO,		"ReadOnly"	},
	{ VIRTIO_BLK_F_BLK_SIZE,	"BlockSize"	},
	{ VIRTIO_BLK_F_SCSI,		"SCSICmds"	},
	{ VIRTIO_BLK_F_FLUSH,		"FlushCommand"	},
	{ VIRTIO_BLK_F_TOPOLOGY,	"Topology"	},
	{ VIRTIO_BLK_F_CONFIG_WCE,	"ConfigWCE"	},
	{ VIRTIO_BLK_F_MQ,		"MultiQueue"	},
	{ VIRTIO_BLK_F_DISCARD,		"Discard"	},
	{ VIRTIO_BLK_F_WRITE_ZEROES,	"WriteZeroes"	},

	{ 0, NULL }
};

static int	vtblk_probe(device_t);
static int	vtblk_attach(device_t);
static int	vtblk_detach(device_t);
static int	vtblk_suspend(device_t);
static int	vtblk_resume(device_t);
static int	vtblk_shutdown(device_t);

static void	vtblk_negotiate_features(struct vtblk_softc *);
static int	vtblk_alloc_intrs(struct vtblk_softc *);
static int	vtblk_maximum_segments(struct vtblk_softc *,
		    struct virtio_blk_config *);
static int	vtblk_alloc_virtqueues(struct vtblk_softc *);
static void	vtblk_set_write_cache(struct vtblk_softc *, int);
static int	vtblk_write_cache_enabled(struct vtblk_softc *,
		    struct virtio_blk_config *);
static int	vtblk_write_cache_sysctl(SYSCTL_HANDLER_ARGS);
static void	vtblk_alloc_disk(struct vtblk_softc *,
		    struct virtio_blk_config *);
/*
 * Interface to the device switch.
 */
static d_open_t		vtblk_open;
static d_strategy_t	vtblk_strategy;
static d_dump_t		vtblk_dump;

static struct dev_ops vbd_disk_ops = {
	{ "vbd", 200, D_DISK | D_MPSAFE | D_KVABIO },
	.d_open		= vtblk_open,
	.d_close	= nullclose,
	.d_read		= physread,
	.d_write	= physwrite,
	.d_strategy	= vtblk_strategy,
	.d_dump		= vtblk_dump,
};

static void	vtblk_vq_startio(struct vtblk_queue *);
static struct vtblk_request * vtblk_bio_request(struct vtblk_queue *);
static int	vtblk_execute_request(struct vtblk_queue *,
		    struct vtblk_request *);
static void	vtblk_vq_intr(void *);

static void	vtblk_prepare_dump(struct vtblk_softc *);
static int	vtblk_write_dump(struct vtblk_softc *, void *, off_t, size_t);
static int	vtblk_flush_dump(struct vtblk_softc *);
static int	vtblk_poll_request(struct vtblk_softc *,
		    struct vtblk_request *);

static void	vtblk_drain_vq(struct vtblk_queue *, int);
static void	vtblk_drain(struct vtblk_softc *);

static int	vtblk_alloc_requests(struct vtblk_queue *);
static void	vtblk_free_requests(struct vtblk_softc *);
static struct vtblk_request * vtblk_dequeue_request(struct vtblk_queue *);
static void	vtblk_enqueue_request(struct vtblk_queue *,
		    struct vtblk_request *);

static int	vtblk_request_error(struct vtblk_request *);
static void	vtblk_finish_bio(struct bio *, int);

static void	vtblk_setup_sysctl(struct vtblk_softc *);
static int	vtblk_tunable_int(struct vtblk_softc *, const char *, int);

/* Tunables. */
static int vtblk_writecache_mode = -1;
TUNABLE_INT("hw.vtblk.writecache_mode", &vtblk_writecache_mode);
static int vtblk_max_queues = SMP_MAXCPU;
TUNABLE_INT("hw.vtblk.max_queues", &vtblk_max_queues);

/* Features desired/implemented by this driver. */
#define VTBLK_FEATURES \
    (VIRTIO_BLK_F_SIZE_MAX		| \
     VIRTIO_BLK_F_SEG_MAX		| \
     VIRTIO_BLK_F_GEOMETRY		| \
     VIRTIO_BLK_F_RO			| \
     VIRTIO_BLK_F_BLK_SIZE		| \
     VIRTIO_BLK_F_FLUSH			| \
     VIRTIO_BLK_F_CONFIG_WCE		| \
     VIRTIO_BLK_F_MQ			| \
     VIRTIO_RING_F_INDIRECT_DESC)

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

DRIVER_MODULE(virtio_blk, virtio_pci, vtblk_driver, vtblk_devclass, NULL, NULL);
MODULE_VERSION(virtio_blk, 1);
MODULE_DEPEND(virtio_blk, virtio, 1, 1, 1);

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
	int i;

	sc = device_get_softc(dev);
	sc->vtblk_dev = dev;

	virtio_set_feature_desc(dev, vtblk_feature_desc);
	vtblk_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_RING_F_INDIRECT_DESC))
		sc->vtblk_flags |= VTBLK_FLAG_INDIRECT;
	if (virtio_with_feature(dev, VIRTIO_BLK_F_RO))
		sc->vtblk_flags |= VTBLK_FLAG_READONLY;
	if (virtio_with_feature(dev, VIRTIO_BLK_F_CONFIG_WCE))
		sc->vtblk_flags |= VTBLK_FLAG_WC_CONFIG;

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
			return error;
		}
	}

	sc->vtblk_max_nsegs = vtblk_maximum_segments(sc, &blkcfg);
	if (sc->vtblk_max_nsegs <= VTBLK_MIN_SEGMENTS) {
		error = EINVAL;
		device_printf(dev, "fewer than minimum number of segments "
		    "allowed: %d\n", sc->vtblk_max_nsegs);
		return error;
	}

	if (virtio_with_feature(dev, VIRTIO_BLK_F_MQ)) {
		int max_queues =
		    vtblk_tunable_int(sc, "max_queues", vtblk_max_queues);
		sc->vtblk_nqs = min(blkcfg.num_queues, ncpus);
		sc->vtblk_nqs = min(sc->vtblk_nqs, VIRTIO_MAX_VIRTQUEUES);
		sc->vtblk_nqs = min(sc->vtblk_nqs, max_queues);
		sc->vtblk_nintrs =
		    min(virtio_intr_count(sc->vtblk_dev), sc->vtblk_nqs);
		// Limit to a 1:1 mapping of IRQs to Virtqueue for now.
		sc->vtblk_nqs = min(sc->vtblk_nqs, sc->vtblk_nintrs);
	} else {
		sc->vtblk_nqs = 1;
		sc->vtblk_nintrs = 1;
	}

	for (i = 0; i < sc->vtblk_nqs; i++) {
		struct vtblk_queue *vq = &sc->vtblk_queues[i];

		vq->vtblk_sc = sc;
		lwkt_serialize_init(&vq->vtblk_slz);
		bioq_init(&vq->vtblk_bioq);
		SLIST_INIT(&vq->vtblk_req_free);
		/*
		 * Allocate working sglist. The number of segments may be too
		 * large to safely store on the stack.
		 */
		vq->vtblk_sglist = sglist_alloc(sc->vtblk_max_nsegs, M_INTWAIT);
		if (vq->vtblk_sglist == NULL) {
			error = ENOMEM;
			device_printf(dev, "cannot allocate sglist\n");
			goto fail;
		}
	}

	for (i = 0; i < ncpus; i++) {
		// Could be improved to take the CPU topology into account.
		sc->vtblk_vqmap[i] = i % sc->vtblk_nqs;
	}

	error = vtblk_alloc_intrs(sc);
	if (error) {
		device_printf(dev, "cannot allocate interrupts\n");
		goto fail;
	}

	error = vtblk_alloc_virtqueues(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueues\n");
		goto fail;
	}

	for (i = 0; i < sc->vtblk_nqs; i++) {
		error = virtio_bind_intr(sc->vtblk_dev, i, i,
					 vtblk_vq_intr, &sc->vtblk_queues[i]);
		if (error) {
			device_printf(dev,
			    "cannot assign virtqueue to interrupt\n");
			goto fail;
		}
	}

	for (i = 0; i < sc->vtblk_nqs; i++) {
		error = vtblk_alloc_requests(&sc->vtblk_queues[i]);
		if (error) {
			device_printf(dev, "cannot preallocate requests\n");
			goto fail;
		}
	}

	for (i = 0; i < sc->vtblk_nintrs; i++) {
		error = virtio_setup_intr(dev, i,
					  &sc->vtblk_queues[i].vtblk_slz);
		if (error) {
			device_printf(dev,
				      "cannot setup virtqueue interrupt\n");
			goto fail;
		}
	}

	for (i = 0; i < sc->vtblk_nqs; i++)
		virtqueue_enable_intr(sc->vtblk_queues[i].vtblk_vq);

	vtblk_alloc_disk(sc, &blkcfg);
	vtblk_setup_sysctl(sc);

fail:
	if (error)
		vtblk_detach(dev);

	return (error);
}

static int
vtblk_detach(device_t dev)
{
	struct vtblk_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->vtblk_nintrs; i++)
		virtio_teardown_intr(dev, i);

	sc->vtblk_flags |= VTBLK_FLAG_DETACH;
	// Once VTBLK_FLAG_DETACH is set, we just need to take the virtqueue
	// serializers once, to make sure that any pending d_strategy call is
	// finished, or will return ENXIO.
	for (i = 0; i < sc->vtblk_nqs; i++) {
		struct vtblk_queue *vq = &sc->vtblk_queues[i];

		lwkt_serialize_enter(&vq->vtblk_slz);
		lwkt_serialize_exit(&vq->vtblk_slz);
	}
	virtio_stop(sc->vtblk_dev);

	// Now the device is fully stopped, and we can clean up everything
	// safely.
	vtblk_drain(sc);
	if (sc->cdev != NULL) {
		disk_destroy(&sc->vtblk_disk);
		sc->cdev = NULL;
	}

	for (i = 0; i < sc->vtblk_nqs; i++) {
		struct vtblk_queue *vq = &sc->vtblk_queues[i];

		if (vq->vtblk_sglist != NULL) {
			sglist_free(vq->vtblk_sglist);
			vq->vtblk_sglist = NULL;
		}
	}

	return (0);
}

static int
vtblk_suspend(device_t dev)
{
	struct vtblk_softc *sc = device_get_softc(dev);
	int i;

	sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
	for (i = 0; i < sc->vtblk_nqs; i++) {
		lwkt_serialize_enter(&sc->vtblk_queues[i].vtblk_slz);
		/* XXX BMV: virtio_stop(), etc needed here? */
		lwkt_serialize_exit(&sc->vtblk_queues[i].vtblk_slz);
	}

	return (0);
}

static int
vtblk_resume(device_t dev)
{
	struct vtblk_softc *sc = device_get_softc(dev);
	int i;

	sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
	for (i = 0; i < sc->vtblk_nqs; i++) {
		lwkt_serialize_enter(&sc->vtblk_queues[i].vtblk_slz);
		/* XXX BMV: virtio_reinit(), etc needed here? */
#if 0 /* XXX Resume IO? */
		vtblk_vq_startio(&sc->vtblk_queues[i]);
#endif
		lwkt_serialize_exit(&sc->vtblk_queues[i].vtblk_slz);
	}

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

	if ((ap->a_oflags & FWRITE) && (sc->vtblk_flags & VTBLK_FLAG_READONLY))
		return (EACCES);

	return (sc->vtblk_flags & VTBLK_FLAG_DETACH ? ENXIO : 0);
}

static int
vtblk_dump(struct dev_dump_args *ap)
{
	struct vtblk_softc *sc;
	cdev_t dev = ap->a_head.a_dev;
        uint64_t buf_start, buf_len;
        int error;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENXIO);

        buf_start = ap->a_offset;
        buf_len = ap->a_length;

//	lwkt_serialize_enter(&sc->vtblk_queues[0].vtblk_slz);

	if ((sc->vtblk_flags & VTBLK_FLAG_DUMPING) == 0) {
		vtblk_prepare_dump(sc);
		sc->vtblk_flags |= VTBLK_FLAG_DUMPING;
	}

	if (buf_len > 0)
		error = vtblk_write_dump(sc, ap->a_virtual, buf_start,
		    buf_len);
	else if (buf_len == 0)
		error = vtblk_flush_dump(sc);
	else {
		error = EINVAL;
		sc->vtblk_flags &= ~VTBLK_FLAG_DUMPING;
	}

//	lwkt_serialize_exit(&sc->vtblk_queues[0].vtblk_slz);

	return (error);
}

/*
 * WARNING! We are using the KVABIO API and must not access memory
 *          through bp->b_data without first calling bkvasync(bp).
 */
static int
vtblk_strategy(struct dev_strategy_args *ap)
{
	struct vtblk_softc *sc;
	cdev_t dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	struct bio *bio = ap->a_bio;
	struct vtblk_queue *q = &sc->vtblk_queues[sc->vtblk_vqmap[mycpuid]];

	if (sc == NULL) {
		vtblk_finish_bio(bio, EINVAL);
		return EINVAL;
	}

	lwkt_serialize_enter(&q->vtblk_slz);
	if ((sc->vtblk_flags & VTBLK_FLAG_DETACH) == 0) {
		bioqdisksort(&q->vtblk_bioq, bio);
		vtblk_vq_startio(q);
		lwkt_serialize_exit(&q->vtblk_slz);
	} else {
		lwkt_serialize_exit(&q->vtblk_slz);
		vtblk_finish_bio(bio, ENXIO);
	}
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

/*
 * Calculate the maximum number of DMA segment supported.  Note
 * that the in/out header is encoded in the segment list.  We
 * assume that VTBLK_MIN_SEGMENTS covers that part of it so
 * we add it into the desired total.  If the SEG_MAX feature
 * is not specified we have to just assume that the host can
 * handle the maximum number of segments required for a MAXPHYS
 * sized request.
 *
 * The additional + 1 is in case a MAXPHYS-sized buffer crosses
 * a page boundary.
 */
static int
vtblk_maximum_segments(struct vtblk_softc *sc,
    struct virtio_blk_config *blkcfg)
{
	device_t dev;
	int nsegs;

	dev = sc->vtblk_dev;
	nsegs = VTBLK_MIN_SEGMENTS;

	if (virtio_with_feature(dev, VIRTIO_BLK_F_SEG_MAX)) {
		nsegs = MIN(blkcfg->seg_max, MAXPHYS / PAGE_SIZE + 1 + nsegs);
	} else {
		nsegs = MAXPHYS / PAGE_SIZE + 1 + nsegs;
	}
	if (sc->vtblk_flags & VTBLK_FLAG_INDIRECT)
		nsegs = MIN(nsegs, VIRTIO_MAX_INDIRECT);

	return (nsegs);
}

static int
vtblk_alloc_intrs(struct vtblk_softc *sc)
{
	int cnt = sc->vtblk_nintrs;
	int *cpus = NULL;
	int error;

	if (cnt > 1) {
		// We can re-use the cpuid-to-virtqueue mapping here.
		// TODO: Actually take CPU topology into account, for a better
		//       distribution.
		cpus = sc->vtblk_vqmap;
	}

	error = virtio_intr_alloc(sc->vtblk_dev, &cnt, 0, cpus);
	if (error != 0)
		return (error);
	else if (cnt != sc->vtblk_nqs)
		return (ENXIO);

	return (0);
}

static int
vtblk_alloc_virtqueues(struct vtblk_softc *sc)
{
	device_t dev = sc->vtblk_dev;
	struct vq_alloc_info *vq_info =
	    kmalloc(sc->vtblk_nqs * sizeof(struct vq_alloc_info),
	    M_TEMP, M_WAITOK | M_ZERO);
	int i;
	int error;

	for (i = 0; i < sc->vtblk_nqs; i++) {
		VQ_ALLOC_INFO_INIT(&vq_info[i], sc->vtblk_max_nsegs,
		    &sc->vtblk_queues[i].vtblk_vq, "%s request %d",
		    device_get_nameunit(dev), i);
	}

	error = virtio_alloc_virtqueues(dev, sc->vtblk_nqs, vq_info);
	kfree(vq_info, M_TEMP);
	return error;
}

static void
vtblk_set_write_cache(struct vtblk_softc *sc, int wc)
{

	/* Set either writeback (1) or writethrough (0) mode. */
	virtio_write_dev_config_1(sc->vtblk_dev,
	    offsetof(struct virtio_blk_config, writeback), wc);
}

static int
vtblk_write_cache_enabled(struct vtblk_softc *sc,
    struct virtio_blk_config *blkcfg)
{
	int wc;

	if (sc->vtblk_flags & VTBLK_FLAG_WC_CONFIG) {
		wc = vtblk_tunable_int(sc, "writecache_mode",
		    vtblk_writecache_mode);
		if (wc >= 0 && wc < VTBLK_CACHE_MAX)
			vtblk_set_write_cache(sc, wc);
		else
			wc = blkcfg->writeback;
	} else
		wc = virtio_with_feature(sc->vtblk_dev, VIRTIO_BLK_F_FLUSH);

	return (wc);
}

static int
vtblk_write_cache_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct vtblk_softc *sc;
	int oldwc, wc, error;

	sc = oidp->oid_arg1;
	oldwc = wc = sc->vtblk_write_cache;

	error = sysctl_handle_int(oidp, &wc, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if ((sc->vtblk_flags & VTBLK_FLAG_WC_CONFIG) == 0)
		return (EPERM);
	if (wc < 0 || wc >= VTBLK_CACHE_MAX)
		return (EINVAL);

	if (oldwc == wc)
		return (0);

	// Not used outside this SYSCTL right now, so no locking needed.
	//lwkt_serialize_enter(&sc->vtblk_queues[0].vtblk_slz);
	sc->vtblk_write_cache = wc;
	vtblk_set_write_cache(sc, sc->vtblk_write_cache);
	//lwkt_serialize_exit(&sc->vtblk_queues[0].vtblk_slz);

	return (0);
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
		sc->vtblk_sector_size = 512;

	/* blkcfg->capacity is always expressed in 512 byte sectors. */
	info.d_media_blksize = sc->vtblk_sector_size;
	info.d_media_blocks = blkcfg->capacity * 512 / info.d_media_blksize;

	if (virtio_with_feature(sc->vtblk_dev, VIRTIO_BLK_F_GEOMETRY)) {
		info.d_ncylinders = blkcfg->geometry.cylinders;
		info.d_nheads = blkcfg->geometry.heads;
		info.d_secpertrack = blkcfg->geometry.sectors;
		info.d_secpercyl = info.d_secpertrack * info.d_nheads;

		/*
		 * If the virtio device is reporting a legacy cylinder count,
		 * recalculate ncylinders based on the media size
		 */
		if (info.d_ncylinders == 16383) {
			info.d_ncylinders = info.d_media_blocks /
			    (info.d_nheads * info.d_secpertrack);
			device_printf(sc->vtblk_dev,
				      "Virtio: ncylinders at legacy maximum "
				      "(16383), recalculating to %d\n",
				      info.d_ncylinders);
		}
	} else {
		/* Fabricate a geometry */
		info.d_secpertrack = 1024;
		info.d_nheads = 1;
		info.d_secpercyl = info.d_secpertrack * info.d_nheads;
		info.d_ncylinders =
		    (u_int)(info.d_media_blocks / info.d_secpercyl);
	}

	if (vtblk_write_cache_enabled(sc, blkcfg) != 0)
		sc->vtblk_write_cache = VTBLK_CACHE_WRITEBACK;
	else
		sc->vtblk_write_cache = VTBLK_CACHE_WRITETHROUGH;

	devstat_add_entry(&sc->stats, "vbd", device_get_unit(sc->vtblk_dev),
			  DEV_BSIZE, DEVSTAT_ALL_SUPPORTED,
			  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
			  DEVSTAT_PRIORITY_DISK);

	/* attach a generic disk device to ourselves */
	sc->cdev = disk_create(device_get_unit(sc->vtblk_dev), &sc->vtblk_disk,
			       &vbd_disk_ops);

	sc->cdev->si_drv1 = sc;
	sc->cdev->si_iosize_max = MAXPHYS;
	disk_setdiskinfo(&sc->vtblk_disk, &info);
	if (virtio_with_feature(sc->vtblk_dev, VIRTIO_BLK_F_BLK_SIZE)) {
		device_printf(sc->vtblk_dev, "Block size: %u\n",
		    sc->vtblk_sector_size);
	}
	device_printf(sc->vtblk_dev,
	    "%juMB (%ju %d byte sectors: %dH %dS/T %dC)\n",
	    ((uintmax_t)info.d_media_blocks * info.d_media_blksize) >> 20,
	    (uintmax_t)info.d_media_blocks, info.d_media_blksize,
	    blkcfg->geometry.heads, blkcfg->geometry.sectors,
	    blkcfg->geometry.cylinders);
}

static void
vtblk_vq_startio(struct vtblk_queue *q)
{
	struct vtblk_softc *sc = q->vtblk_sc;
	struct virtqueue *vq;
	struct vtblk_request *req;
	int enq;

	vq = q->vtblk_vq;
	enq = 0;

	ASSERT_SERIALIZED(&q->vtblk_slz);

	if (sc->vtblk_flags & VTBLK_FLAG_SUSPEND)
		return;

	while (!virtqueue_full(vq)) {
		req = vtblk_bio_request(q);
		if (req == NULL)
			break;

		if (vtblk_execute_request(q, req) != 0) {
			bioqdisksort(&q->vtblk_bioq, req->vbr_bio);
			vtblk_enqueue_request(q, req);
			break;
		}
		devstat_start_transaction(&sc->stats);

		enq++;
	}

	if (enq > 0)
		virtqueue_notify(vq, &q->vtblk_slz);
}

static struct vtblk_request *
vtblk_bio_request(struct vtblk_queue *q)
{
	struct bio_queue_head *bioq;
	struct vtblk_request *req;
	struct bio *bio;
	struct buf *bp;

	bioq = &q->vtblk_bioq;

	if (bioq_first(bioq) == NULL)
		return (NULL);

	req = vtblk_dequeue_request(q);
	if (req == NULL)
		return (NULL);

	bio = bioq_takefirst(bioq);
	req->vbr_bio = bio;
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

	return (req);
}

static int
vtblk_execute_request(struct vtblk_queue *q, struct vtblk_request *req)
{
	struct sglist *sg;
	struct bio *bio;
	struct buf *bp;
	int writable, error;

	sg = q->vtblk_sglist;
	bio = req->vbr_bio;
	bp = bio->bio_buf;
	writable = 0;

	/*
	 * sglist is live throughout this subroutine.
	 */
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

	error = virtqueue_enqueue(q->vtblk_vq, req, sg,
				  sg->sg_nseg - writable, writable);

	sglist_reset(sg);

	return (error);
}

static void
vtblk_vq_intr(void *arg)
{
	struct vtblk_queue *q = arg;
	struct vtblk_softc *sc = q->vtblk_sc;
	struct virtqueue *vq = q->vtblk_vq;
	struct vtblk_request *req;
	struct bio *bio;
	struct buf *bp;

	ASSERT_SERIALIZED(&q->vtblk_slz);

	if (!virtqueue_pending(vq))
		return;

	lwkt_serialize_handler_disable(&q->vtblk_slz);
	virtqueue_disable_intr(q->vtblk_vq);

retry:
	if (sc->vtblk_flags & VTBLK_FLAG_DETACH)
		return;

	while ((req = virtqueue_dequeue(vq, NULL)) != NULL) {
		bio = req->vbr_bio;
		bp = bio->bio_buf;

		if (req->vbr_ack == VIRTIO_BLK_S_OK) {
			bp->b_resid = 0;
		} else {
			bp->b_flags |= B_ERROR;
			if (req->vbr_ack == VIRTIO_BLK_S_UNSUPP) {
				bp->b_error = ENOTSUP;
			} else {
				bp->b_error = EIO;
			}
		}

		devstat_end_transaction_buf(&sc->stats, bio->bio_buf);

		lwkt_serialize_exit(&q->vtblk_slz);
		/*
		 * Unlocking the controller around biodone() does not allow
		 * processing further device interrupts; when we queued
		 * vtblk_vq_intr, we disabled interrupts. It will allow
		 * concurrent vtblk_strategy/_startio command dispatches.
		 */
		biodone(bio);
		lwkt_serialize_enter(&q->vtblk_slz);

		vtblk_enqueue_request(q, req);
	}

	vtblk_vq_startio(q);

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
	lwkt_serialize_handler_enable(&q->vtblk_slz);
}

static void
vtblk_prepare_dump(struct vtblk_softc *sc)
{
	device_t dev;
	int i;

	dev = sc->vtblk_dev;

	for (i = 0 ; i < sc->vtblk_nqs; i++)
		virtqueue_disable_intr(sc->vtblk_queues[i].vtblk_vq);

	virtio_stop(sc->vtblk_dev);

	/*
	 * Drain all requests caught in-flight in the virtqueues,
	 * skipping biodone(). When dumping, only one request is
	 * outstanding at a time, and we just poll the virtqueue
	 * for the response.
	 */
	for (i = 0 ; i < sc->vtblk_nqs; i++)
		vtblk_drain_vq(&sc->vtblk_queues[i], 1);

	if (virtio_reinit(dev, sc->vtblk_features) != 0) {
		panic("%s: cannot reinit VirtIO block device during dump",
		    device_get_nameunit(dev));
	}

	for (i = 0 ; i < sc->vtblk_nqs; i++)
		virtqueue_disable_intr(sc->vtblk_queues[i].vtblk_vq);
	virtio_reinit_complete(dev);
}

static int
vtblk_write_dump(struct vtblk_softc *sc, void *virtual, off_t offset,
    size_t length)
{
	struct bio bio;
	struct buf bp;
	struct vtblk_request *req;

	req = &sc->vtblk_dump_request;
	req->vbr_ack = -1;
	req->vbr_hdr.type = VIRTIO_BLK_T_OUT;
	req->vbr_hdr.ioprio = 1;
	req->vbr_hdr.sector = offset / 512;

	req->vbr_bio = &bio;
	bzero(&bio, sizeof(struct bio));
	bzero(&bp, sizeof(struct buf));

	bio.bio_buf = &bp;
	bp.b_cmd = BUF_CMD_WRITE;
	bp.b_data = virtual;
	bp.b_bcount = length;

	return (vtblk_poll_request(sc, req));
}

static int
vtblk_flush_dump(struct vtblk_softc *sc)
{
	struct bio bio;
	struct buf bp;
	struct vtblk_request *req;

	req = &sc->vtblk_dump_request;
	req->vbr_ack = -1;
	req->vbr_hdr.type = VIRTIO_BLK_T_FLUSH;
	req->vbr_hdr.ioprio = 1;
	req->vbr_hdr.sector = 0;

	req->vbr_bio = &bio;
	bzero(&bio, sizeof(struct bio));
	bzero(&bp, sizeof(struct buf));

	bio.bio_buf = &bp;
	bp.b_cmd = BUF_CMD_FLUSH;

	return (vtblk_poll_request(sc, req));
}

static int
vtblk_poll_request(struct vtblk_softc *sc, struct vtblk_request *req)
{
	struct virtqueue *vq;
	int error;

	vq = sc->vtblk_queues[0].vtblk_vq;

	if (!virtqueue_empty(vq))
		return (EBUSY);

	error = vtblk_execute_request(&sc->vtblk_queues[0], req);
	if (error)
		return (error);

	virtqueue_notify(vq, NULL);
	virtqueue_poll(vq, NULL);

	error = vtblk_request_error(req);
	if (error && bootverbose) {
		device_printf(sc->vtblk_dev,
		    "%s: IO error: %d\n", __func__, error);
	}

	return (error);
}

static void
vtblk_drain_vq(struct vtblk_queue *q, int skip_done)
{
	struct virtqueue *vq = q->vtblk_vq;
	struct vtblk_request *req;
	int last;

	last = 0;

	while ((req = virtqueue_drain(vq, &last)) != NULL) {
		if (!skip_done)
			vtblk_finish_bio(req->vbr_bio, ENXIO);

		vtblk_enqueue_request(q, req);
	}

	KASSERT(virtqueue_empty(vq), ("virtqueue not empty"));
}

static void
vtblk_drain(struct vtblk_softc *sc)
{
	struct vtblk_queue *q;
	struct bio_queue_head *bioq;
	struct bio *bio;
	int i;

	for (i = 0; i < sc->vtblk_nqs; i++) {
		q = &sc->vtblk_queues[i];
		bioq = &q->vtblk_bioq;

		if (q->vtblk_vq != NULL)
			vtblk_drain_vq(q, 0);

		while (bioq_first(bioq) != NULL) {
			bio = bioq_takefirst(bioq);
			vtblk_finish_bio(bio, ENXIO);
		}
	}

	vtblk_free_requests(sc);
}

static int
vtblk_alloc_requests(struct vtblk_queue *vq)
{
	struct vtblk_softc *sc = vq->vtblk_sc;
	struct vtblk_request *req;
	int i, nreqs;

	nreqs = virtqueue_size(vq->vtblk_vq);

	/*
	 * Preallocate sufficient requests to keep the virtqueue full. Each
	 * request consumes VTBLK_MIN_SEGMENTS or more descriptors so reduce
	 * the number allocated when indirect descriptors are not available.
	 */
	if ((sc->vtblk_flags & VTBLK_FLAG_INDIRECT) == 0)
		nreqs /= VTBLK_MIN_SEGMENTS;

	for (i = 0; i < nreqs; i++) {
		req = contigmalloc(sizeof(struct vtblk_request), M_DEVBUF,
		    M_WAITOK, 0, BUS_SPACE_MAXADDR, 16, 0);
		if (req == NULL)
			return (ENOMEM);

		KKASSERT(sglist_count(&req->vbr_hdr, sizeof(req->vbr_hdr))
		    == 1);
		KKASSERT(sglist_count(&req->vbr_ack, sizeof(req->vbr_ack))
		    == 1);

		sc->vtblk_request_count++;
		vtblk_enqueue_request(vq, req);
	}

	return (0);
}

static void
vtblk_free_requests(struct vtblk_softc *sc)
{
	struct vtblk_request *req;
	int i;

	for (i = 0; i < sc->vtblk_nqs; i++) {
		struct vtblk_queue *q = &sc->vtblk_queues[i];
		while ((req = vtblk_dequeue_request(q)) != NULL) {
			sc->vtblk_request_count--;
			contigfree(req, sizeof(struct vtblk_request), M_DEVBUF);
		}
	}

	KASSERT(sc->vtblk_request_count == 0, ("leaked requests"));
}

static struct vtblk_request *
vtblk_dequeue_request(struct vtblk_queue *q)
{
	struct vtblk_request *req;

	req = SLIST_FIRST(&q->vtblk_req_free);
	if (req != NULL)
		SLIST_REMOVE_HEAD(&q->vtblk_req_free, vbr_link);

	return (req);
}

static void
vtblk_enqueue_request(struct vtblk_queue *vq, struct vtblk_request *req)
{

	bzero(req, sizeof(struct vtblk_request));
	SLIST_INSERT_HEAD(&vq->vtblk_req_free, req, vbr_link);
}

static int
vtblk_request_error(struct vtblk_request *req)
{
	int error;

	switch (req->vbr_ack) {
	case VIRTIO_BLK_S_OK:
		error = 0;
		break;
	case VIRTIO_BLK_S_UNSUPP:
		error = ENOTSUP;
		break;
	default:
		error = EIO;
		break;
	}

	return (error);
}

static void
vtblk_finish_bio(struct bio *bio, int error)
{

	biodone(bio);
}

static void
vtblk_setup_sysctl(struct vtblk_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtblk_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "writecache_mode",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, vtblk_write_cache_sysctl,
	    "I", "Write cache mode (writethrough (0) or writeback (1))");
}

static int
vtblk_tunable_int(struct vtblk_softc *sc, const char *knob, int def)
{
	char path[64];

	ksnprintf(path, sizeof(path),
	    "hw.vtblk.%d.%s", device_get_unit(sc->vtblk_dev), knob);
	TUNABLE_INT_FETCH(path, &def);

	return (def);
}
