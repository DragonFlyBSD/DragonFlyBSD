/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * $FreeBSD: head/sys/dev/virtio/balloon/virtio_balloon.c 326255 2017-11-27 14:52:40Z pfg $
 */

/*
 * Copyright (c) 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Diederik de Groot <info@talon.nl>
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

/* Driver for VirtIO memory balloon devices. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/queue.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/virtual/virtio/virtio/virtio.h>
#include <dev/virtual/virtio/virtio/virtqueue.h>
#include <dev/virtual/virtio/balloon/virtio_balloon.h>

struct vtballoon_softc {
	device_t		 vtballoon_dev;
	struct lwkt_serialize    vtballoon_slz;
	uint64_t		 vtballoon_features;
	uint32_t		 vtballoon_flags;
#define VTBALLOON_FLAG_DETACH	 0x01

	struct virtqueue	*vtballoon_inflate_vq;
	struct virtqueue	*vtballoon_deflate_vq;

	uint32_t		 vtballoon_desired_npages;
	uint32_t		 vtballoon_current_npages;
	TAILQ_HEAD(,vm_page)	 vtballoon_pages;

	struct thread		*vtballoon_td;
	uint32_t		*vtballoon_page_frames;
	int			 vtballoon_pagereq;
	int			 vtballoon_timeout;
	int			 vtballoon_nintr;
	int			 vtballoon_debug;
#define VTBALLOON_INFO     	 0x01
#define VTBALLOON_ERROR    	 0x02
#define VTBALLOON_DEBUG    	 0x04
#define VTBALLOON_TRACE    	 0x08

	struct virtqueue	*vtballoon_stats_vq;
	struct vtballoon_stat	 vtballoon_stats[VTBALLOON_S_NR];
	bool			 vtballoon_update_stats;
};

static struct virtio_feature_desc vtballoon_feature_desc[] = {
	{ VIRTIO_BALLOON_F_MUST_TELL_HOST,	"MustTellHost"		},
	{ VIRTIO_BALLOON_F_STATS_VQ,		"StatsVq"		},
	{ VIRTIO_BALLOON_F_DEFLATE_ON_OOM,	"DeflateOnOutOfMemory"	},
	{ 0, NULL }
};

#define vtballoon_dprintf(_sc, _level, _msg, _args ...) do {	    \
	if ((_sc)->vtballoon_debug & (_level))			  \
		device_printf((_sc)->vtballoon_dev, "%s:%d: "_msg,      \
		  __FUNCTION__, __LINE__, ##_args);		     \
} while (0)

static int		vtballoon_probe(device_t);
static int		vtballoon_attach(device_t);
static int		vtballoon_detach(device_t);

static int		vtballoon_alloc_intrs(struct vtballoon_softc *sc);

static void		vtballoon_negotiate_features(struct vtballoon_softc *);
static int		vtballoon_alloc_virtqueues(struct vtballoon_softc *);

static void 		vtballoon_config_change_intr(void *);

static void		vtballoon_update_stats(struct vtballoon_softc *sc);
static void		vtballoon_stats_vq_intr(void *);

static void		vtballoon_inflate_vq_intr(void *);
static void		vtballoon_deflate_vq_intr(void *);
static void		vtballoon_inflate(struct vtballoon_softc *, int);
static void		vtballoon_deflate(struct vtballoon_softc *, int);

static void		vtballoon_send_page_frames(struct vtballoon_softc *,
			    struct virtqueue *, int);

static void		vtballoon_pop(struct vtballoon_softc *);
static void		vtballoon_stop(struct vtballoon_softc *);

static vm_page_t	vtballoon_alloc_page(struct vtballoon_softc *);
static void		vtballoon_free_page(struct vtballoon_softc *, vm_page_t);

static int		vtballoon_sleep(struct vtballoon_softc *);
static void		vtballoon_thread(void *);
static void		vtballoon_get_tunables(struct vtballoon_softc *);
static void		vtballoon_add_sysctl(struct vtballoon_softc *);

/*
 * Features desired/implemented by this driver.
 * VIRTIO_BALLOON_F_STATS_VQ | VIRTIO_BALLOON_F_MUST_TELL_HOST
 */
#define VTBALLOON_FEATURES		VIRTIO_BALLOON_F_STATS_VQ

/* Timeout between retries when the balloon needs inflating. */
#define VTBALLOON_LOWMEM_TIMEOUT	hz * 100

/* vm_page_alloc flags */
#define VTBALLOON_REGULAR_ALLOC		VM_ALLOC_NORMAL
#define VTBALLOON_LOWMEM_ALLOC		VM_ALLOC_SYSTEM

/*
 * Maximum number of pages we'll request to inflate or deflate
 * the balloon in one virtqueue request. Both Linux and NetBSD
 * have settled on 256, doing up to 1MB at a time.
 */
#define VTBALLOON_PAGES_PER_REQUEST	256

/*
 * Default Debug Level
 * VTBALLOON_INFO | VTBALLOON_ERROR | VTBALLOON_DEBUG | VTBALLOON_TRACE
 */
#define VTBALLOON_DEFAULT_DEBUG_LEVEL   VTBALLOON_INFO | VTBALLOON_ERROR

/*
 * Maximum number of interrupts to request
 */
#define VTBALLOON_MAX_INTERRUPTS	4

/* Must be able to fix all pages frames in one page (segment). */
CTASSERT(VTBALLOON_PAGES_PER_REQUEST * sizeof(uint32_t) <= PAGE_SIZE);

#define VTBALLOON_SLZ(_sc)		&(_sc)->vtballoon_slz
#define VTBALLOON_ENTER_SLZ(_sc)	lwkt_serialize_enter(VTBALLOON_SLZ(sc));
#define VTBALLOON_EXIT_SLZ(_sc)		lwkt_serialize_exit(VTBALLOON_SLZ(sc));

static device_method_t vtballoon_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtballoon_probe),
	DEVMETHOD(device_attach,	vtballoon_attach),
	DEVMETHOD(device_detach,	vtballoon_detach),

	DEVMETHOD_END
};

static driver_t vtballoon_driver = {
	"vtballoon",
	vtballoon_methods,
	sizeof(struct vtballoon_softc)
};
static devclass_t vtballoon_devclass;

DRIVER_MODULE(virtio_balloon, virtio_pci, vtballoon_driver,
    vtballoon_devclass, NULL, NULL);
MODULE_VERSION(virtio_balloon, 1);
MODULE_DEPEND(virtio_balloon, virtio, 1, 1, 1);

static int
vtballoon_probe(device_t dev)
{
	struct vtballoon_softc *sc = device_get_softc(dev);
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");
	if (virtio_get_device_type(dev) != VIRTIO_ID_BALLOON)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Balloon Adapter");

	return (BUS_PROBE_DEFAULT);
}

struct irqmap {
	int irq;
	int idx;
	driver_intr_t *handler;
	const char * handler_name;
};

static int
vtballoon_attach(device_t dev)
{
	struct vtballoon_softc *sc;
	int error, i;

	sc = device_get_softc(dev);
	sc->vtballoon_dev = dev;
	sc->vtballoon_debug = VTBALLOON_DEFAULT_DEBUG_LEVEL;

	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");

	lwkt_serialize_init(VTBALLOON_SLZ(sc));
	TAILQ_INIT(&sc->vtballoon_pages);

	vtballoon_get_tunables(sc);
	vtballoon_add_sysctl(sc);

	virtio_set_feature_desc(dev, vtballoon_feature_desc);
	vtballoon_negotiate_features(sc);

	sc->vtballoon_page_frames = contigmalloc(VTBALLOON_PAGES_PER_REQUEST *
	    sizeof(uint32_t), M_DEVBUF, M_NOWAIT | M_ZERO, 0, BUS_SPACE_MAXADDR, 16, 0);
	if (sc->vtballoon_page_frames == NULL) {
		error = ENOMEM;
		vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot allocate page frame request array (error:%d)\n", error);
		goto fail;
	}
	error = vtballoon_alloc_intrs(sc);
	if (error) {
		vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot allocate interrupts (error:%d)\n", error);
		goto fail;
	}

	error = vtballoon_alloc_virtqueues(sc);
	if (error) {
		vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot allocate virtqueues (error:%d)\n", error);
		goto fail;
	}

	int nrhandlers = virtio_with_feature(sc->vtballoon_dev, VIRTIO_BALLOON_F_STATS_VQ) ? 4 : 3;
	struct irqmap info[4];

	/* Possible "Virtqueue <-> IRQ" configurations */
	switch (sc->vtballoon_nintr) {
	case 1:
		info[2] = (struct irqmap){0, -1, vtballoon_config_change_intr, "config"};
		info[0] = (struct irqmap){0, 0, vtballoon_inflate_vq_intr, "inflate"};
		info[1] = (struct irqmap){0, 1, vtballoon_deflate_vq_intr, "deflate"};
		info[3] = (struct irqmap){0, 2, vtballoon_stats_vq_intr, "stats"};
		break;
	case 2:
		info[2] = (struct irqmap){1, -1, vtballoon_config_change_intr, "config"};
		info[0] = (struct irqmap){0, 0, vtballoon_inflate_vq_intr, "inflate"};
		info[1] = (struct irqmap){0, 1, vtballoon_deflate_vq_intr, "deflate"};
		info[3] = (struct irqmap){0, 2, vtballoon_stats_vq_intr, "stats"};
		break;
	case 3:
		info[2] = (struct irqmap){2, -1, vtballoon_config_change_intr, "config"};
		info[0] = (struct irqmap){0, 0, vtballoon_inflate_vq_intr, "inflate"};
		info[1] = (struct irqmap){1, 1, vtballoon_deflate_vq_intr, "deflate"};
		info[3] = (struct irqmap){2, 2, vtballoon_stats_vq_intr, "stats"};
		break;
	case 4:
		info[2] = (struct irqmap){3, -1, vtballoon_config_change_intr, "config"};
		info[0] = (struct irqmap){0, 0, vtballoon_inflate_vq_intr, "inflate"};
		info[1] = (struct irqmap){1, 1, vtballoon_deflate_vq_intr, "deflate"};
		info[3] = (struct irqmap){2, 2, vtballoon_stats_vq_intr, "stats"};
		break;
	default:
		vtballoon_dprintf(sc, VTBALLOON_ERROR, "Invalid interrupt vector count: %d\n", sc->vtballoon_nintr);
		goto fail;
	}
	for (i = 0; i < nrhandlers; i++) {
		error = virtio_bind_intr(sc->vtballoon_dev, info[i].irq, info[i].idx,
		    info[i].handler, sc);
		if (error) {
			vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot bind virtqueue '%s' handler to IRQ:%d/%d\n", 
				info[i].handler_name, info[i].irq, sc->vtballoon_nintr);
			goto fail;
		}
	}

	for (i = 0; i < sc->vtballoon_nintr; i++) {
		error = virtio_setup_intr(dev, i, VTBALLOON_SLZ(sc));
		if (error) {
			vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot setup virtqueue interrupt:%d (error:%d)\n", i, error);
			goto fail;
		}
	}

	error = kthread_create(vtballoon_thread, sc, &sc->vtballoon_td, "virtio_balloon");
	if (error) {
		vtballoon_dprintf(sc, VTBALLOON_ERROR, "cannot create balloon kthread (error:%d)\n", error);
		goto fail;
	}

	virtqueue_enable_intr(sc->vtballoon_inflate_vq);
	virtqueue_enable_intr(sc->vtballoon_deflate_vq);

	if (virtio_with_feature(sc->vtballoon_dev, VIRTIO_BALLOON_F_STATS_VQ)) {
		virtqueue_enable_intr(sc->vtballoon_stats_vq);
#if 0		/* enabling this causes a panic, on asserting ASSERT_SERIALIZED(sc) in vtballoon_update_stats */
		/*
		 * Prime this stats virtqueue with one buffer so the hypervisor can
		 * use it to signal us later.
		 */
		VTBALLOON_ENTER_SLZ(sc);
		vtballoon_update_stats(sc);
		VTBALLOON_EXIT_SLZ(sc);
#endif
	}

fail:
	if (error)
		vtballoon_detach(dev);

	return (error);
}

static int
vtballoon_detach(device_t dev)
{
	struct vtballoon_softc *sc;
	int i;

	sc = device_get_softc(dev);
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");

	if (sc->vtballoon_td != NULL) {
		VTBALLOON_ENTER_SLZ(sc);
		sc->vtballoon_flags |= VTBALLOON_FLAG_DETACH;

		/* drain */
		wakeup_one(sc);
		zsleep(sc->vtballoon_td, VTBALLOON_SLZ(sc), 0, "vtbdth", 0);
		VTBALLOON_EXIT_SLZ(sc);
		sc->vtballoon_td = NULL;
	}

	lwkt_serialize_handler_disable(VTBALLOON_SLZ(sc));

	for (i = 0; i < sc->vtballoon_nintr; i++)
		virtio_teardown_intr(dev, i);

	if (device_is_attached(dev)) {
		vtballoon_pop(sc);
		vtballoon_stop(sc);
	}

	if (sc->vtballoon_page_frames != NULL) {
		contigfree(sc->vtballoon_page_frames, VTBALLOON_PAGES_PER_REQUEST *
			sizeof(uint32_t), M_DEVBUF);
		sc->vtballoon_page_frames = NULL;
	}
	return (0);
}

static void
vtballoon_negotiate_features(struct vtballoon_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtballoon_dev;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");
	features = virtio_negotiate_features(dev, VTBALLOON_FEATURES);
	sc->vtballoon_features = features;
}

static int vtballoon_alloc_intrs(struct vtballoon_softc *sc)
{
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");
	int cnt, error;
	int intrcount = virtio_intr_count(sc->vtballoon_dev);
	int use_config = 1;

	intrcount = imin(intrcount, VTBALLOON_MAX_INTERRUPTS);
	if (intrcount < 1)
		return (ENXIO);

	cnt = intrcount;
	error = virtio_intr_alloc(sc->vtballoon_dev, &cnt, use_config, NULL);
	if (error != 0) {
		virtio_intr_release(sc->vtballoon_dev);
		return (error);
	}
	sc->vtballoon_nintr = cnt;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "%d Interrupts Allocated\n", sc->vtballoon_nintr);
	return (0);
}

static int
vtballoon_alloc_virtqueues(struct vtballoon_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info[3];
	int nvqs;

	dev = sc->vtballoon_dev;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");
	nvqs = 2;

	VQ_ALLOC_INFO_INIT(&vq_info[0], 0, &sc->vtballoon_inflate_vq,
		"%s inflate", device_get_nameunit(dev));

	VQ_ALLOC_INFO_INIT(&vq_info[1], 0, &sc->vtballoon_deflate_vq,
		"%s deflate", device_get_nameunit(dev));

	if (virtio_with_feature(sc->vtballoon_dev, VIRTIO_BALLOON_F_STATS_VQ)) {
		VQ_ALLOC_INFO_INIT(&vq_info[2], 0, &sc->vtballoon_stats_vq,
			"%s stats", device_get_nameunit(dev));
		nvqs = 3;
	}
	return (virtio_alloc_virtqueues(dev, nvqs, vq_info));
}

static void
vtballoon_config_change_intr(void *arg)
{
	struct vtballoon_softc *sc = arg;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");
	ASSERT_SERIALIZED(VTBALLOON_SLZ(sc));
	wakeup_one(sc);
}

static inline void
vtballoon_update_stat(struct vtballoon_softc *sc, int idx,
	uint16_t tag, uint64_t val)
{
	KASSERT(idx >= VTBALLOON_S_NR, ("Stats index out of bounds"));
	/*
	 * XXX: Required for endianess in the future
	 * sc->vtballoon_stats[idx].tag = virtio_is_little_endian(sc->vtballoon_dev) ? le16toh(tag) : tag;
	 * sc->vtballoon_stats[idx].val = virtio_is_little_endian(sc->vtballoon_dev) ? le64toh(val) : val;
	 * at the moment virtio balloon is always little endian.
	 * 
	 */
	sc->vtballoon_stats[idx].tag = le16toh(tag);
	sc->vtballoon_stats[idx].val = le64toh(val);

}

/*
 * collect guest side statistics
 *
 * XXX: am i using the correct memory and pagefault values
 */
static unsigned int collect_balloon_stats(struct vtballoon_softc *sc)
{
	#define pages_to_bytes(x) ((uint64_t)(x) << PAGE_SHIFT)
	unsigned int idx = 0;
	struct vmtotal total;
	struct vmmeter vmm;
	struct vmstats vms;
	size_t vmt_size = sizeof(total);
	size_t vmm_size = sizeof(vmm);
	size_t vms_size = sizeof(vms);

	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Updating Stats Buffer\n");
	if (!kernel_sysctlbyname("vm.vmtotal", &total, &vmt_size, NULL, 0, NULL)) {
		/* Total amount of free memory )*/
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_MEMFREE,
					pages_to_bytes(total.t_rm - total.t_arm));
		/* Total amount of memory */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_MEMTOT,
					pages_to_bytes(total.t_rm));
		/* Available memory as in /proc	*/
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_AVAIL,
					pages_to_bytes(total.t_arm));
	}
	if (!kernel_sysctlbyname("vm.vmstats", &vms, &vms_size, NULL, 0, NULL)) {
		/* Disk caches */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_CACHES,
					pages_to_bytes(vms.v_cache_count));
	}
	if (!kernel_sysctlbyname("vm.vmmeter", &vmm, &vmm_size, NULL, 0, NULL)) {
		/* Amount of memory swapped in */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_SWAP_IN,
					pages_to_bytes(vmm.v_swappgsin));
		/* Amount of memory swapped out */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_SWAP_OUT,
					pages_to_bytes(vmm.v_swappgsout));
		/* Number of major faults */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_MAJFLT,
					vmm.v_vm_faults);
		/* Number of minor faults */
		vtballoon_update_stat(sc, idx++, VTBALLOON_S_MINFLT,
					vmm.v_intrans);
	}

	if (sc->vtballoon_debug & VTBALLOON_TRACE)  {
		static const char *vt_balloon_names[]=VTBALLOON_S_NAMES;
		int i;
		for (i=0; i < idx; i++) {
			kprintf("\t%s = %lu\n", vt_balloon_names[sc->vtballoon_stats[i].tag], sc->vtballoon_stats[i].val);
		}
	}

	return idx;
}

static void
vtballoon_update_stats(struct vtballoon_softc *sc)
{
	struct virtqueue *vq = sc->vtballoon_stats_vq;

	ASSERT_SERIALIZED(VTBALLOON_SLZ(sc));

	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Stats Requested\n");

	struct sglist sg;
	struct sglist_seg segs[1];
	unsigned int num_stats;
	int error;

	num_stats = collect_balloon_stats(sc);

	sglist_init(&sg, 1, segs);
	error = sglist_append(&sg, sc->vtballoon_stats, sizeof(sc->vtballoon_stats[0]) * num_stats);
	KASSERT(error == 0, ("error adding page frames to sglist"));

	error = virtqueue_enqueue(vq, vq, &sg, 1, 0);
	KASSERT(error == 0, ("error enqueuing page frames to virtqueue"));
	virtqueue_notify(sc->vtballoon_stats_vq, NULL);
}

/*
 * While most virtqueues communicate guest-initiated requests to the hypervisor,
 * the stats queue operates in reverse.  The driver(host) initializes the virtqueue
 * with a single buffer. From that point forward, all conversations consist of
 * a hypervisor request (a call to this function) which directs us to refill
 * the virtqueue with a fresh stats buffer. Since stats collection can sleep,
 * we delegate the job to the vtballoon_thread which will do the actual stats
 * collecting work.
 */
static void
vtballoon_stats_vq_intr(void *arg)
{
	struct vtballoon_softc *sc = arg;
	struct virtqueue *vq = sc->vtballoon_stats_vq;

	ASSERT_SERIALIZED(VTBALLOON_SLZ(sc));
	if (sc->vtballoon_update_stats || !virtqueue_pending(vq))
		return;

	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Ballooon Stats Requested\n");
	sc->vtballoon_update_stats = true;
	wakeup_one(sc);
	virtqueue_dequeue(vq, NULL);
}

static void
vtballoon_inflate_vq_intr(void *arg)
{
	struct vtballoon_softc *sc = arg;
	struct virtqueue *vq = sc->vtballoon_inflate_vq;
	ASSERT_SERIALIZED(VTBALLOON_SLZ(sc));
	if (!virtqueue_pending(vq))
		return;
	wakeup_one(sc);
}

static void
vtballoon_deflate_vq_intr(void *arg)
{
	struct vtballoon_softc *sc = arg;
	struct virtqueue *vq = sc->vtballoon_deflate_vq;
	ASSERT_SERIALIZED(VTBALLOON_SLZ(sc));
	if (!virtqueue_pending(vq))
		return;
	wakeup_one(sc);
}

static void
vtballoon_inflate(struct vtballoon_softc *sc, int npages)
{
	struct virtqueue *vq;

	vm_page_t m;
	int i;

	vq = sc->vtballoon_inflate_vq;

	if (npages > VTBALLOON_PAGES_PER_REQUEST)
		npages = VTBALLOON_PAGES_PER_REQUEST;

	for (i = 0; i < npages; i++) {
		if ((m = vtballoon_alloc_page(sc)) == NULL) {
			/* First allocate usign VTBALLOON_REGULAR_ALLOC and fall back to VTBALLOON_LOWMEM_ALLOC
			 * when the guest is under severe memory pressure. Quickly decrease the
			 * allocation rate, allowing the system to swap out pages.
			 */
			sc->vtballoon_pagereq = VM_ALLOC_SYSTEM | VM_ALLOC_INTERRUPT;
			sc->vtballoon_timeout = VTBALLOON_LOWMEM_TIMEOUT;
			break;
		}

		sc->vtballoon_page_frames[i] =
		    VM_PAGE_TO_PHYS(m) >> VIRTIO_BALLOON_PFN_SHIFT;

		KASSERT(m->queue == PQ_NONE,
		    ("%s: allocated page %p on queue", __func__, m));
		TAILQ_INSERT_TAIL(&sc->vtballoon_pages, m, pageq);
	}

	if (i > 0)
		vtballoon_send_page_frames(sc, vq, i);
}

static void
vtballoon_deflate(struct vtballoon_softc *sc, int npages)
{
	TAILQ_HEAD(, vm_page) free_pages;
	struct virtqueue *vq;
	vm_page_t m;
	int i;

	vq = sc->vtballoon_deflate_vq;
	TAILQ_INIT(&free_pages);

	if (npages > VTBALLOON_PAGES_PER_REQUEST)
		npages = VTBALLOON_PAGES_PER_REQUEST;

	for (i = 0; i < npages; i++) {
		m = TAILQ_FIRST(&sc->vtballoon_pages);
		KASSERT(m != NULL, ("%s: no more pages to deflate", __func__));

		sc->vtballoon_page_frames[i] =
		    VM_PAGE_TO_PHYS(m) >> VIRTIO_BALLOON_PFN_SHIFT;

		TAILQ_REMOVE(&sc->vtballoon_pages, m, pageq);
		TAILQ_INSERT_TAIL(&free_pages, m, pageq);
	}

	if (i > 0) {
		/*
		 * Note that if virtio VIRTIO_BALLOON_F_MUST_TELL_HOST
		 * feature is true, we *have* to tell host first
		 * before freeing the pages.
		 */
		vtballoon_send_page_frames(sc, vq, i);

		while ((m = TAILQ_FIRST(&free_pages)) != NULL) {
			TAILQ_REMOVE(&free_pages, m, pageq);
			vtballoon_free_page(sc, m);
		}
	}

	KASSERT((TAILQ_EMPTY(&sc->vtballoon_pages) &&
	    sc->vtballoon_current_npages == 0) ||
	    (!TAILQ_EMPTY(&sc->vtballoon_pages) &&
	    sc->vtballoon_current_npages != 0),
	    ("%s: bogus page count %d", __func__,
	    sc->vtballoon_current_npages));
}

static void
vtballoon_send_page_frames(struct vtballoon_softc *sc, struct virtqueue *vq,
    int npages)
{
	struct sglist sg;
	struct sglist_seg segs[1];
	void *c;
	int error;

	sglist_init(&sg, 1, segs);

	error = sglist_append(&sg, sc->vtballoon_page_frames,
	    npages * sizeof(uint32_t));
	KASSERT(error == 0, ("error adding page frames to sglist"));

	error = virtqueue_enqueue(vq, vq, &sg, 1, 0);
	KASSERT(error == 0, ("error enqueuing page frames to virtqueue"));
	virtqueue_notify(vq, NULL);

	/*
	 * Inflate and deflate operations are done synchronously. The
	 * interrupt handler will wake us up.
	 */
	VTBALLOON_ENTER_SLZ(sc);
	while ((c = virtqueue_dequeue(vq, NULL)) == NULL) {
		zsleep(sc, VTBALLOON_SLZ(sc), 0, "vtbspf", 0);
	}
	VTBALLOON_EXIT_SLZ(sc);

	KASSERT(c == vq, ("unexpected balloon operation response"));
}

static void
vtballoon_pop(struct vtballoon_softc *sc)
{
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Popping\n");

	while (!TAILQ_EMPTY(&sc->vtballoon_pages))
		vtballoon_deflate(sc, sc->vtballoon_current_npages);
}

static void
vtballoon_stop(struct vtballoon_softc *sc)
{
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Stopping\n");

	virtqueue_disable_intr(sc->vtballoon_inflate_vq);
	virtqueue_disable_intr(sc->vtballoon_deflate_vq);
/*
	if (virtio_with_feature(sc->vtballoon_dev, VIRTIO_BALLOON_F_STATS_VQ)) {
		virtqueue_disable_intr(sc->vtballoon_stats_vq);
	}
*/
	virtio_stop(sc->vtballoon_dev);
}

static vm_page_t
vtballoon_alloc_page(struct vtballoon_softc *sc)
{
	vm_page_t m;

	m = vm_page_alloc(NULL, 0, sc->vtballoon_pagereq);
	if (m != NULL)
		sc->vtballoon_current_npages++;

	return (m);
}

static void
vtballoon_free_page(struct vtballoon_softc *sc, vm_page_t m)
{
	vm_page_free_toq(m);
	sc->vtballoon_current_npages--;
}

static uint32_t
vtballoon_desired_size(struct vtballoon_softc *sc)
{
	uint32_t desired;

	desired = virtio_read_dev_config_4(sc->vtballoon_dev,
	    offsetof(struct virtio_balloon_config, num_pages));

	return (le32toh(desired));
}

static void
vtballoon_update_size(struct vtballoon_softc *sc)
{
	virtio_write_dev_config_4(sc->vtballoon_dev,
	    offsetof(struct virtio_balloon_config, actual),
	    htole32(sc->vtballoon_current_npages));
}

static int
vtballoon_sleep(struct vtballoon_softc *sc)
{
	int rc, timeout;
	uint32_t current, desired;

	rc = 0;
	current = sc->vtballoon_current_npages;
	sc->vtballoon_pagereq = VM_ALLOC_NORMAL | VM_ALLOC_INTERRUPT;

	VTBALLOON_ENTER_SLZ(sc);
	for (;;) {
		if (sc->vtballoon_flags & VTBALLOON_FLAG_DETACH) {
			rc = 1;
			break;
		}

		desired = vtballoon_desired_size(sc);
		if (desired != sc->vtballoon_desired_npages)
			vtballoon_dprintf(sc, VTBALLOON_DEBUG, "balloon %s %d -> %d (4K pages)\n",
				desired < sc->vtballoon_desired_npages ? "deflating" : "inflating",
				current, desired);

		sc->vtballoon_desired_npages = desired;

		/*
		 * If given, use non-zero timeout on the first time through
		 * the loop. On subsequent times, timeout will be zero so
		 * we will reevaluate the desired size of the balloon and
		 * break out to retry if needed.
		 */
		timeout = sc->vtballoon_timeout;
		sc->vtballoon_timeout = 0;

		if (current > desired)
			break;
		else if (current < desired && timeout == 0)
			break;
		else if (sc->vtballoon_update_stats)
			break;
		else if (!timeout)
			vtballoon_dprintf(sc, VTBALLOON_TRACE, "balloon %d (4K pages) reached\n", current);

		zsleep(sc, VTBALLOON_SLZ(sc), 0, "vtbslp", timeout);
	}
	VTBALLOON_EXIT_SLZ(sc);

	return (rc);
}

static void
vtballoon_thread(void *arg)
{
	struct vtballoon_softc *sc = arg;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "Thread started.\n");

	uint32_t current, desired;
	for (;;) {
		if (vtballoon_sleep(sc) != 0)
			break;

		current = sc->vtballoon_current_npages;
		desired = sc->vtballoon_desired_npages;

		if (desired != current) {
			if (desired > current)
				vtballoon_inflate(sc, desired - current);
			else
				vtballoon_deflate(sc, current - desired);

			vtballoon_update_size(sc);
		}
		if (sc->vtballoon_update_stats) {
			vtballoon_update_stats(sc);
			sc->vtballoon_update_stats = false;
		}
	}

	kthread_exit();
}

static void
vtballoon_get_tunables(struct vtballoon_softc *sc)
{
	char tmpstr[64];
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");

	TUNABLE_INT_FETCH("hw.vtballoon.debug_level", &sc->vtballoon_debug);

	ksnprintf(tmpstr, sizeof(tmpstr), "dev.vtballoon.%d.debug_level",
	    device_get_unit(sc->vtballoon_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->vtballoon_debug);
}

static void
vtballoon_add_sysctl(struct vtballoon_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtballoon_dev;
	vtballoon_dprintf(sc, VTBALLOON_TRACE, "\n");

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "debug_level",
	    CTLFLAG_RW, &sc->vtballoon_debug, 0,
	    "Debug level");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "desired",
	    CTLFLAG_RD, &sc->vtballoon_desired_npages, sizeof(uint32_t),
	    "Desired balloon size in pages");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "current",
	    CTLFLAG_RD, &sc->vtballoon_current_npages, sizeof(uint32_t),
	    "Current balloon size in pages");
}
