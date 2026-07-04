/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/* Driver for the VirtIO MMIO (legacy v1) interface. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/serialize.h>

/*
 * DragonFly note: the bus/resource accessors used here (bus_read_4(),
 * bus_alloc_resource_any(), SYS_RES_*) come from <sys/bus.h> + <sys/rman.h>;
 * unlike FreeBSD there is no <machine/bus.h>/<machine/resource.h> on x86_64.
 */
#include <dev/virtual/virtio/virtio/virtio.h>
#include <dev/virtual/virtio/virtio/virtqueue.h>
#include "virtio_mmio.h"
#include "virtio_bus_if.h"

struct vqentry {
	int		 what;
	driver_intr_t	*handler;
	void		 *arg;
	TAILQ_ENTRY(vqentry) entries;
};

struct vtmmio_virtqueue {
	struct virtqueue	*vtv_vq;
	int			 vtv_ires_idx;
};

static int	vtmmio_detach(device_t);
static int	vtmmio_suspend(device_t);
static int	vtmmio_resume(device_t);
static int	vtmmio_shutdown(device_t);
static void	vtmmio_driver_added(device_t, driver_t *);
static void	vtmmio_child_detached(device_t, device_t);
static int	vtmmio_read_ivar(device_t, device_t, int, uintptr_t *);
static int	vtmmio_write_ivar(device_t, device_t, int, uintptr_t);

static uint64_t	vtmmio_negotiate_features(device_t, uint64_t);
static int	vtmmio_with_feature(device_t, uint64_t);
static int	vtmmio_intr_count(device_t dev);
static int	vtmmio_intr_alloc(device_t dev, int *cnt, int use_config,
		    int *cpus);
static int	vtmmio_intr_release(device_t dev);
static int	vtmmio_alloc_virtqueues(device_t, int, struct vq_alloc_info *);
static int	vtmmio_setup_intr(device_t, uint irq, lwkt_serialize_t);
static int	vtmmio_teardown_intr(device_t, uint irq);
static int	vtmmio_bind_intr(device_t, uint, int, driver_intr_t, void *);
static int	vtmmio_unbind_intr(device_t, int);
static void	vtmmio_stop(device_t);
static int	vtmmio_reinit(device_t, uint64_t);
static void	vtmmio_reinit_complete(device_t);
static void	vtmmio_notify_virtqueue(device_t, uint16_t);
static uint8_t	vtmmio_get_status(device_t);
static void	vtmmio_set_status(device_t, uint8_t);
static void	vtmmio_read_dev_config(device_t, bus_size_t, void *, int);
static void	vtmmio_write_dev_config(device_t, bus_size_t, void *, int);

static void	vtmmio_describe_features(struct vtmmio_softc *, const char *,
		    uint64_t);
static void	vtmmio_probe_and_attach_child(struct vtmmio_softc *);
static int	vtmmio_reinit_virtqueue(struct vtmmio_softc *, int);
static void	vtmmio_free_interrupts(struct vtmmio_softc *);
static void	vtmmio_free_virtqueues(struct vtmmio_softc *);
static void	vtmmio_release_child_resources(struct vtmmio_softc *);
static void	vtmmio_reset(struct vtmmio_softc *);
static void	vtmmio_select_virtqueue(struct vtmmio_softc *, int);
static void	vtmmio_intr(void *);
static void	vtmmio_add_irqentry(struct vtmmio_intr_resource *, int,
		    driver_intr_t, void *);
static void	vtmmio_del_irqentry(struct vtmmio_intr_resource *, int);
static void	vtmmio_set_virtqueue(struct vtmmio_softc *, struct virtqueue *,
		    uint32_t);

/*
 * I/O port read/write wrappers.
 */
#define vtmmio_write_config_1(sc, o, v)	bus_write_1((sc)->res[0], (o), (v))
#define vtmmio_write_config_2(sc, o, v)	bus_write_2((sc)->res[0], (o), (v))
#define vtmmio_write_config_4(sc, o, v)	bus_write_4((sc)->res[0], (o), (v))

#define vtmmio_read_config_1(sc, o)	bus_read_1((sc)->res[0], (o))
#define vtmmio_read_config_2(sc, o)	bus_read_2((sc)->res[0], (o))
#define vtmmio_read_config_4(sc, o)	bus_read_4((sc)->res[0], (o))

/*
 * The legacy QueuePFN register stores a virtqueue's physical address as a
 * 32-bit page-frame number (paddr >> PAGE_SHIFT).  Bound the ring allocation
 * so that page-frame number cannot overflow 32 bits and silently truncate
 * when written to the device.
 */
#define VTMMIO_QUEUE_PFN_MAX		((((vm_paddr_t)1) << (32 + PAGE_SHIFT)) - 1)

static device_method_t vtmmio_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,			  vtmmio_probe),
	DEVMETHOD(device_attach,		  vtmmio_attach),
	DEVMETHOD(device_detach,		  vtmmio_detach),
	DEVMETHOD(device_suspend,		  vtmmio_suspend),
	DEVMETHOD(device_resume,		  vtmmio_resume),
	DEVMETHOD(device_shutdown,		  vtmmio_shutdown),

	/* Bus interface. */
	DEVMETHOD(bus_driver_added,		  vtmmio_driver_added),
	DEVMETHOD(bus_child_detached,		  vtmmio_child_detached),
	DEVMETHOD(bus_read_ivar,		  vtmmio_read_ivar),
	DEVMETHOD(bus_write_ivar,		  vtmmio_write_ivar),

	/* VirtIO bus interface. */
	DEVMETHOD(virtio_bus_negotiate_features,  vtmmio_negotiate_features),
	DEVMETHOD(virtio_bus_with_feature,	  vtmmio_with_feature),
	DEVMETHOD(virtio_bus_intr_count,	  vtmmio_intr_count),
	DEVMETHOD(virtio_bus_intr_alloc,	  vtmmio_intr_alloc),
	DEVMETHOD(virtio_bus_intr_release,	  vtmmio_intr_release),
	DEVMETHOD(virtio_bus_alloc_virtqueues,	  vtmmio_alloc_virtqueues),
	DEVMETHOD(virtio_bus_setup_intr,	  vtmmio_setup_intr),
	DEVMETHOD(virtio_bus_teardown_intr,	  vtmmio_teardown_intr),
	DEVMETHOD(virtio_bus_bind_intr,		  vtmmio_bind_intr),
	DEVMETHOD(virtio_bus_unbind_intr,	  vtmmio_unbind_intr),
	DEVMETHOD(virtio_bus_stop,		  vtmmio_stop),
	DEVMETHOD(virtio_bus_reinit,		  vtmmio_reinit),
	DEVMETHOD(virtio_bus_reinit_complete,	  vtmmio_reinit_complete),
	DEVMETHOD(virtio_bus_notify_vq,		  vtmmio_notify_virtqueue),
	DEVMETHOD(virtio_bus_read_device_config,  vtmmio_read_dev_config),
	DEVMETHOD(virtio_bus_write_device_config, vtmmio_write_dev_config),

	DEVMETHOD_END
};

DEFINE_CLASS_0(virtio_mmio, vtmmio_driver, vtmmio_methods,
    sizeof(struct vtmmio_softc));

static driver_t vtmmio_root_driver = {
	"virtio_mmio",
	vtmmio_methods,
	sizeof(struct vtmmio_softc)
};

static devclass_t vtmmio_devclass;

DRIVER_MODULE(virtio_mmio, nexus, vtmmio_root_driver, vtmmio_devclass, NULL,
    NULL);
MODULE_VERSION(virtio_mmio, 1);
MODULE_DEPEND(virtio_mmio, virtio, 1, 1, 1);

int
vtmmio_probe(device_t dev)
{
	struct vtmmio_softc *sc;
	int rid;
	uint32_t magic, version;

	sc = device_get_softc(dev);

	rid = 0;
	sc->res[0] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->res[0] == NULL) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	magic = vtmmio_read_config_4(sc, VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != VIRTIO_MMIO_MAGIC_VIRT) {
		if (bootverbose)
			device_printf(dev, "bad magic value %#x\n", magic);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res[0]);
		sc->res[0] = NULL;
		return (ENXIO);
	}

	version = vtmmio_read_config_4(sc, VIRTIO_MMIO_VERSION);
	if (version != 1) {
		device_printf(dev, "unsupported version: %#x\n", version);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res[0]);
		sc->res[0] = NULL;
		return (ENXIO);
	}

	if (vtmmio_read_config_4(sc, VIRTIO_MMIO_DEVICE_ID) == 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res[0]);
		sc->res[0] = NULL;
		return (ENXIO);
	}

	bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res[0]);
	sc->res[0] = NULL;

	device_set_desc(dev, "VirtIO MMIO adapter");
	return (BUS_PROBE_DEFAULT);
}

int
vtmmio_attach(device_t dev)
{
	struct vtmmio_softc *sc;
	device_t child;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->vtmmio_config_irq = -1;

	rid = 0;
	sc->res[0] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->res[0] == NULL) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	sc->vtmmio_version = vtmmio_read_config_4(sc, VIRTIO_MMIO_VERSION);
	if (sc->vtmmio_version != 1) {
		device_printf(dev, "unsupported version: %#x\n",
		    sc->vtmmio_version);
		vtmmio_detach(dev);
		return (ENXIO);
	}

	vtmmio_reset(sc);

	/* Tell the host we've noticed this device. */
	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);

	if ((child = device_add_child(dev, NULL, -1)) == NULL) {
		device_printf(dev, "cannot create child device\n");
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_FAILED);
		vtmmio_detach(dev);
		return (ENOMEM);
	}

	sc->vtmmio_child_dev = child;
	vtmmio_probe_and_attach_child(sc);

	return (0);
}

static int
vtmmio_detach(device_t dev)
{
	struct vtmmio_softc *sc;
	device_t child;
	int error;

	sc = device_get_softc(dev);

	if ((child = sc->vtmmio_child_dev) != NULL) {
		error = device_delete_child(dev, child);
		if (error)
			return (error);
		sc->vtmmio_child_dev = NULL;
	}

	vtmmio_reset(sc);
	vtmmio_release_child_resources(sc);

	if (sc->res[0] != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->res[0]);
		sc->res[0] = NULL;
	}

	return (0);
}

static int
vtmmio_suspend(device_t dev)
{

	return (bus_generic_suspend(dev));
}

static int
vtmmio_resume(device_t dev)
{

	return (bus_generic_resume(dev));
}

static int
vtmmio_shutdown(device_t dev)
{

	(void) bus_generic_shutdown(dev);
	vtmmio_stop(dev);

	return (0);
}

static void
vtmmio_driver_added(device_t dev, driver_t *driver)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);
	vtmmio_probe_and_attach_child(sc);
}

static void
vtmmio_child_detached(device_t dev, device_t child)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);
	vtmmio_reset(sc);
	vtmmio_release_child_resources(sc);
}

static int
vtmmio_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vtmmio_child_dev != child)
		return (ENOENT);

	switch (index) {
	case VIRTIO_IVAR_DEVTYPE:
		*result = vtmmio_read_config_4(sc, VIRTIO_MMIO_DEVICE_ID);
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static int
vtmmio_write_ivar(device_t dev, device_t child, int index, uintptr_t value)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vtmmio_child_dev != child)
		return (ENOENT);

	switch (index) {
	case VIRTIO_IVAR_FEATURE_DESC:
		sc->vtmmio_child_feat_desc = (void *) value;
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static uint64_t
vtmmio_negotiate_features(device_t dev, uint64_t child_features)
{
	struct vtmmio_softc *sc;
	uint64_t host_features, features;

	sc = device_get_softc(dev);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_HOST_FEATURES_SEL, 1);
	host_features = vtmmio_read_config_4(sc, VIRTIO_MMIO_HOST_FEATURES);
	host_features <<= 32;

	vtmmio_write_config_4(sc, VIRTIO_MMIO_HOST_FEATURES_SEL, 0);
	host_features |= vtmmio_read_config_4(sc, VIRTIO_MMIO_HOST_FEATURES);

	vtmmio_describe_features(sc, "host", host_features);

	features = host_features & child_features;
	features = virtqueue_filter_features(features);
	sc->vtmmio_features = features;

	vtmmio_describe_features(sc, "negotiated", features);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_FEATURES_SEL, 1);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_FEATURES,
	    (uint32_t)(features >> 32));

	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_FEATURES,
	    (uint32_t)features);

	return (features);
}

static int
vtmmio_with_feature(device_t dev, uint64_t feature)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	return ((sc->vtmmio_features & feature) != 0);
}

static int
vtmmio_intr_count(device_t dev)
{
	(void)dev;

	return (1);
}

static int
vtmmio_intr_alloc(device_t dev, int *cnt, int use_config, int *cpus)
{
	struct vtmmio_softc *sc;
	struct vtmmio_intr_resource *ires;

	(void)use_config;

	sc = device_get_softc(dev);

	if (sc->vtmmio_nintr_res > 0)
		return (EINVAL);
	if (*cnt <= 0)
		return (EINVAL);

	*cnt = 1;
	ires = &sc->vtmmio_intr_res[0];
	ires->ires_sc = sc;
	ires->rid = 0;
	TAILQ_INIT(&ires->ls);

	ires->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &ires->rid,
	    RF_ACTIVE);
	if (ires->irq == NULL)
		return (ENXIO);

	if (cpus != NULL)
		cpus[0] = rman_get_cpuid(ires->irq);

	sc->vtmmio_nintr_res = 1;
	return (0);
}

static int
vtmmio_intr_release(device_t dev)
{
	struct vtmmio_softc *sc;
	struct vtmmio_intr_resource *ires;

	sc = device_get_softc(dev);

	if (sc->vtmmio_nintr_res == 0)
		return (EINVAL);

	ires = &sc->vtmmio_intr_res[0];
	KKASSERT(TAILQ_EMPTY(&ires->ls));

	if (ires->irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, ires->rid, ires->irq);
		ires->irq = NULL;
	}

	sc->vtmmio_nintr_res = 0;
	return (0);
}

static int
vtmmio_alloc_virtqueues(device_t dev, int nvqs, struct vq_alloc_info *vq_info)
{
	struct vtmmio_softc *sc;
	struct vtmmio_virtqueue *vqx;
	struct vq_alloc_info *info;
	struct virtqueue *vq;
	uint32_t size;
	int idx, error;

	sc = device_get_softc(dev);

	if (sc->vtmmio_nvqs != 0)
		return (EALREADY);
	if (nvqs <= 0 || nvqs > VIRTIO_MAX_VIRTQUEUES)
		return (EINVAL);

	sc->vtmmio_vqs = kmalloc(nvqs * sizeof(struct vtmmio_virtqueue),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc->vtmmio_vqs == NULL)
		return (ENOMEM);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);

	for (idx = 0; idx < nvqs; idx++) {
		vqx = &sc->vtmmio_vqs[idx];
		info = &vq_info[idx];

		vqx->vtv_ires_idx = -1;
		vtmmio_select_virtqueue(sc, idx);
		size = vtmmio_read_config_4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX);

		error = virtqueue_alloc(dev, idx, size, VIRTIO_MMIO_VRING_ALIGN,
		    VTMMIO_QUEUE_PFN_MAX, info, &vq);
		if (error) {
			device_printf(dev,
			    "cannot allocate virtqueue %d: %d\n", idx, error);
			break;
		}

		vtmmio_set_virtqueue(sc, vq, size);
		vqx->vtv_vq = *info->vqai_vq = vq;
		sc->vtmmio_nvqs++;
	}

	if (error)
		vtmmio_free_virtqueues(sc);

	return (error);
}

static int
vtmmio_setup_intr(device_t dev, uint irq, lwkt_serialize_t slz)
{
	struct vtmmio_softc *sc;
	struct vtmmio_intr_resource *ires;

	sc = device_get_softc(dev);

	if ((int)irq >= sc->vtmmio_nintr_res)
		return (EINVAL);

	ires = &sc->vtmmio_intr_res[irq];

	return (bus_setup_intr(dev, ires->irq, INTR_MPSAFE, vtmmio_intr,
	    ires, &ires->intrhand, slz));
}

static int
vtmmio_teardown_intr(device_t dev, uint irq)
{
	struct vtmmio_softc *sc;
	struct vtmmio_intr_resource *ires;

	sc = device_get_softc(dev);
	if ((int)irq >= sc->vtmmio_nintr_res)
		return (EINVAL);

	ires = &sc->vtmmio_intr_res[irq];
	if (ires->intrhand == NULL)
		return (ENXIO);

	bus_teardown_intr(dev, ires->irq, ires->intrhand);
	ires->intrhand = NULL;
	return (0);
}

static void
vtmmio_add_irqentry(struct vtmmio_intr_resource *intr_res, int what,
    driver_intr_t handler, void *arg)
{
	struct vqentry *e;

	TAILQ_FOREACH(e, &intr_res->ls, entries) {
		if (e->what == what)
			return;
	}

	e = kmalloc(sizeof(*e), M_DEVBUF, M_WAITOK | M_ZERO);
	e->what = what;
	e->handler = handler;
	e->arg = arg;
	TAILQ_INSERT_TAIL(&intr_res->ls, e, entries);
}

static void
vtmmio_del_irqentry(struct vtmmio_intr_resource *intr_res, int what)
{
	struct vqentry *e;

	TAILQ_FOREACH(e, &intr_res->ls, entries) {
		if (e->what == what)
			break;
	}
	if (e != NULL) {
		TAILQ_REMOVE(&intr_res->ls, e, entries);
		kfree(e, M_DEVBUF);
	}
}

static int
vtmmio_bind_intr(device_t dev, uint irq, int what, driver_intr_t handler,
    void *arg)
{
	struct vtmmio_softc *sc;
	struct vtmmio_virtqueue *vqx;

	sc = device_get_softc(dev);
	if (irq >= sc->vtmmio_nintr_res)
		return (EINVAL);

	if (what == -1) {
		if (sc->vtmmio_config_irq != -1)
			return (EINVAL);
		sc->vtmmio_config_irq = irq;
		vtmmio_add_irqentry(&sc->vtmmio_intr_res[irq], what, handler, arg);
		return (0);
	}

	if (sc->vtmmio_nvqs <= what || what < 0)
		return (EINVAL);

	vqx = &sc->vtmmio_vqs[what];
	if (vqx->vtv_ires_idx != -1)
		return (EINVAL);

	vqx->vtv_ires_idx = irq;
	vtmmio_add_irqentry(&sc->vtmmio_intr_res[irq], what, handler, arg);
	return (0);
}

static int
vtmmio_unbind_intr(device_t dev, int what)
{
	struct vtmmio_softc *sc;
	struct vtmmio_virtqueue *vqx;
	uint irq;

	sc = device_get_softc(dev);

	if (what == -1) {
		if (sc->vtmmio_config_irq == -1)
			return (EINVAL);
		irq = sc->vtmmio_config_irq;
		sc->vtmmio_config_irq = -1;
		goto done;
	}

	if (sc->vtmmio_nvqs <= what || what < 0)
		return (EINVAL);

	vqx = &sc->vtmmio_vqs[what];
	if (vqx->vtv_ires_idx == -1)
		return (EINVAL);

	irq = vqx->vtv_ires_idx;
	vqx->vtv_ires_idx = -1;

done:
	KKASSERT(irq < sc->vtmmio_nintr_res);
	vtmmio_del_irqentry(&sc->vtmmio_intr_res[irq], what);
	return (0);
}

static void
vtmmio_stop(device_t dev)
{

	vtmmio_reset(device_get_softc(dev));
}

static int
vtmmio_reinit(device_t dev, uint64_t features)
{
	struct vtmmio_softc *sc;
	int idx, error;

	sc = device_get_softc(dev);

	if (vtmmio_get_status(dev) != VIRTIO_CONFIG_STATUS_RESET)
		vtmmio_stop(dev);

	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);
	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER);

	vtmmio_negotiate_features(dev, features);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);

	for (idx = 0; idx < sc->vtmmio_nvqs; idx++) {
		error = vtmmio_reinit_virtqueue(sc, idx);
		if (error)
			return (error);
	}

	return (0);
}

static void
vtmmio_reinit_complete(device_t dev)
{

	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
}

static void
vtmmio_notify_virtqueue(device_t dev, uint16_t queue)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_NOTIFY, queue);
}

static uint8_t
vtmmio_get_status(device_t dev)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);
	return (vtmmio_read_config_4(sc, VIRTIO_MMIO_STATUS));
}

static void
vtmmio_set_status(device_t dev, uint8_t status)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (status != VIRTIO_CONFIG_STATUS_RESET)
		status |= vtmmio_get_status(dev);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_STATUS, status);
}

static void
vtmmio_read_dev_config(device_t dev, bus_size_t offset, void *dst, int length)
{
	struct vtmmio_softc *sc;
	bus_size_t off;
	uint8_t *d;
	int size;

	sc = device_get_softc(dev);
	off = VIRTIO_MMIO_CONFIG + offset;

	for (d = dst; length > 0; d += size, off += size, length -= size) {
		if (length >= 4) {
			size = 4;
			*(uint32_t *)d = vtmmio_read_config_4(sc, off);
		} else if (length >= 2) {
			size = 2;
			*(uint16_t *)d = vtmmio_read_config_2(sc, off);
		} else {
			size = 1;
			*d = vtmmio_read_config_1(sc, off);
		}
	}
}

static void
vtmmio_write_dev_config(device_t dev, bus_size_t offset, void *src, int length)
{
	struct vtmmio_softc *sc;
	bus_size_t off;
	uint8_t *s;
	int size;

	sc = device_get_softc(dev);
	off = VIRTIO_MMIO_CONFIG + offset;

	for (s = src; length > 0; s += size, off += size, length -= size) {
		if (length >= 4) {
			size = 4;
			vtmmio_write_config_4(sc, off, *(uint32_t *)s);
		} else if (length >= 2) {
			size = 2;
			vtmmio_write_config_2(sc, off, *(uint16_t *)s);
		} else {
			size = 1;
			vtmmio_write_config_1(sc, off, *s);
		}
	}
}

static void
vtmmio_describe_features(struct vtmmio_softc *sc, const char *msg,
    uint64_t features)
{
	device_t dev, child;

	dev = sc->dev;
	child = sc->vtmmio_child_dev;

	if (device_is_attached(child) && bootverbose == 0)
		return;

	virtio_describe(dev, msg, features, sc->vtmmio_child_feat_desc);
}

static void
vtmmio_probe_and_attach_child(struct vtmmio_softc *sc)
{
	device_t dev, child;
	int error;

	dev = sc->dev;
	child = sc->vtmmio_child_dev;

	if (child == NULL)
		return;

	if (device_get_state(child) != DS_NOTPRESENT)
		return;

	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER);
	error = device_probe_and_attach(child);
	if (error != 0 || device_get_state(child) == DS_NOTPRESENT) {
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_FAILED);
		vtmmio_reset(sc);
		vtmmio_release_child_resources(sc);

		/* Reset status for future attempt. */
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);
	} else
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
}

static int
vtmmio_reinit_virtqueue(struct vtmmio_softc *sc, int idx)
{
	struct vtmmio_virtqueue *vqx;
	struct virtqueue *vq;
	int error;
	uint16_t size;

	vqx = &sc->vtmmio_vqs[idx];
	vq = vqx->vtv_vq;

	KASSERT(vq != NULL, ("%s: vq %d not allocated", __func__, idx));

	vtmmio_select_virtqueue(sc, idx);
	size = vtmmio_read_config_4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX);

	error = virtqueue_reinit(vq, size);
	if (error)
		return (error);

	vtmmio_set_virtqueue(sc, vq, size);
	return (0);
}

static void
vtmmio_free_interrupts(struct vtmmio_softc *sc)
{
	struct vtmmio_intr_resource *ires;
	int i;

	if (sc->vtmmio_nintr_res == 0)
		return;

	if (sc->vtmmio_config_irq != -1)
		vtmmio_unbind_intr(sc->dev, -1);
	for (i = 0; i < sc->vtmmio_nvqs; i++) {
		if (sc->vtmmio_vqs[i].vtv_ires_idx != -1)
			vtmmio_unbind_intr(sc->dev, i);
	}

	ires = &sc->vtmmio_intr_res[0];
	if (ires->intrhand != NULL) {
		bus_teardown_intr(sc->dev, ires->irq, ires->intrhand);
		ires->intrhand = NULL;
	}
	if (ires->irq != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, ires->rid, ires->irq);
		ires->irq = NULL;
	}

	sc->vtmmio_nintr_res = 0;
}

static void
vtmmio_free_virtqueues(struct vtmmio_softc *sc)
{
	struct vtmmio_virtqueue *vqx;
	int idx;

	for (idx = 0; idx < sc->vtmmio_nvqs; idx++) {
		vqx = &sc->vtmmio_vqs[idx];
		vtmmio_select_virtqueue(sc, idx);
		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_PFN, 0);
		virtqueue_free(vqx->vtv_vq);
		vqx->vtv_vq = NULL;
	}

	if (sc->vtmmio_vqs != NULL)
		kfree(sc->vtmmio_vqs, M_DEVBUF);
	sc->vtmmio_vqs = NULL;
	sc->vtmmio_nvqs = 0;
}

static void
vtmmio_release_child_resources(struct vtmmio_softc *sc)
{

	vtmmio_free_interrupts(sc);
	vtmmio_free_virtqueues(sc);
}

static void
vtmmio_reset(struct vtmmio_softc *sc)
{

	vtmmio_set_status(sc->dev, VIRTIO_CONFIG_STATUS_RESET);
}

static void
vtmmio_select_virtqueue(struct vtmmio_softc *sc, int idx)
{

	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_SEL, idx);
}

static void
vtmmio_set_virtqueue(struct vtmmio_softc *sc, struct virtqueue *vq,
    uint32_t size)
{
	vm_paddr_t paddr;

	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_NUM, size);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_ALIGN,
	    VIRTIO_MMIO_VRING_ALIGN);
	paddr = virtqueue_paddr(vq);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_PFN,
	    paddr >> PAGE_SHIFT);
}

static void
vtmmio_intr(void *arg)
{
	struct vtmmio_intr_resource *ires;
	struct vtmmio_softc *sc;
	struct vqentry *e;
	uint32_t status;

	ires = arg;
	sc = ires->ires_sc;

	status = vtmmio_read_config_4(sc, VIRTIO_MMIO_INTERRUPT_STATUS);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_INTERRUPT_ACK, status);

	TAILQ_FOREACH(e, &ires->ls, entries) {
		if (e->what == -1) {
			if (status & VIRTIO_MMIO_INT_CONFIG)
				e->handler(e->arg);
		} else if (status & VIRTIO_MMIO_INT_VRING) {
			e->handler(e->arg);
		}
	}
}
