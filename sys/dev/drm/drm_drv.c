/*-
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 * $FreeBSD: head/sys/dev/drm2/drm_drv.c 247835 2013-03-05 09:49:34Z kib $
 */

/** @file drm_drv.c
 * The catch-all file for DRM device support, including module setup/teardown,
 * open/close, and ioctl dispatch.
 */

#include <sys/devfs.h>
#include <machine/limits.h>

#include <drm/drmP.h>
#include <drm/drm_core.h>

#ifdef DRM_DEBUG_DEFAULT_ON
int drm_debug = (DRM_DEBUGBITS_DEBUG | DRM_DEBUGBITS_KMS |
    DRM_DEBUGBITS_FAILED_IOCTL);
#else
int drm_debug = 0;
#endif
int drm_notyet_flag = 0;

unsigned int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */
unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */

static int drm_load(struct drm_device *dev);
static void drm_unload(struct drm_device *dev);
drm_pci_id_list_t *drm_find_description(int vendor, int device,
    drm_pci_id_list_t *idlist);

#define DRIVER_SOFTC(unit) \
	((struct drm_device *)devclass_get_softc(drm_devclass, unit))

static int
drm_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		TUNABLE_INT_FETCH("drm.debug", &drm_debug);
		TUNABLE_INT_FETCH("drm.notyet", &drm_notyet_flag);
		break;
	}
	return (0);
}

static moduledata_t drm_mod = {
	"drm",
	drm_modevent,
	0
};
DECLARE_MODULE(drm, drm_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(drm, 1);
MODULE_DEPEND(drm, agp, 1, 1, 1);
MODULE_DEPEND(drm, pci, 1, 1, 1);
MODULE_DEPEND(drm, iicbus, 1, 1, 1);

static drm_ioctl_desc_t		  drm_ioctls[256] = {
	DRM_IOCTL_DEF(DRM_IOCTL_VERSION, drm_version, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_UNIQUE, drm_getunique, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAGIC, drm_getmagic, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_IRQ_BUSID, drm_irq_by_busid, DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAP, drm_getmap, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CLIENT, drm_getclient, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_STATS, drm_getstats, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CAP, drm_getcap, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_VERSION, drm_setversion, DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_UNIQUE, drm_setunique, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_BLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_UNBLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AUTH_MAGIC, drm_authmagic, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_MAP, drm_addmap_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_MAP, drm_rmmap_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_SAREA_CTX, drm_setsareactx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_SAREA_CTX, drm_getsareactx, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_MASTER, drm_setmaster_ioctl, DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_DROP_MASTER, drm_dropmaster_ioctl, DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_CTX, drm_addctx, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_CTX, drm_rmctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_MOD_CTX, drm_modctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CTX, drm_getctx, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_SWITCH_CTX, drm_switchctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_NEW_CTX, drm_newctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RES_CTX, drm_resctx, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_LOCK, drm_lock, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_UNLOCK, drm_unlock, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_FINISH, drm_noop, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_BUFS, drm_addbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_MARK_BUFS, drm_markbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_INFO_BUFS, drm_infobufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_MAP_BUFS, drm_mapbufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_FREE_BUFS, drm_freebufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_DMA, drm_dma, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_CONTROL, drm_control, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ACQUIRE, drm_agp_acquire_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_RELEASE, drm_agp_release_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ENABLE, drm_agp_enable_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_INFO, drm_agp_info_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ALLOC, drm_agp_alloc_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_FREE, drm_agp_free_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_BIND, drm_agp_bind_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_UNBIND, drm_agp_unbind_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_SG_ALLOC, drm_sg_alloc_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_SG_FREE, drm_sg_free, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_WAIT_VBLANK, drm_wait_vblank, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODESET_CTL, drm_modeset_ctl, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_UPDATE_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_GEM_CLOSE, drm_gem_close_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_FLINK, drm_gem_flink_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_OPEN, drm_gem_open_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETRESOURCES, drm_mode_getresources, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANERESOURCES, drm_mode_getplane_res, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCRTC, drm_mode_getcrtc, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETCRTC, drm_mode_setcrtc, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANE, drm_mode_getplane, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPLANE, drm_mode_setplane, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CURSOR, drm_mode_cursor_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETGAMMA, drm_mode_gamma_get_ioctl, DRM_MASTER|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETGAMMA, drm_mode_gamma_set_ioctl, DRM_MASTER|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETENCODER, drm_mode_getencoder, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCONNECTOR, drm_mode_getconnector, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ATTACHMODE, drm_mode_attachmode_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DETACHMODE, drm_mode_detachmode_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPERTY, drm_mode_getproperty_ioctl, DRM_MASTER | DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPROPERTY, drm_mode_connector_property_set_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPBLOB, drm_mode_getblob_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETFB, drm_mode_getfb, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB, drm_mode_addfb, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB2, drm_mode_addfb2, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_RMFB, drm_mode_rmfb, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_PAGE_FLIP, drm_mode_page_flip_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DIRTYFB, drm_mode_dirtyfb_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATE_DUMB, drm_mode_create_dumb_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_MAP_DUMB, drm_mode_mmap_dumb_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DESTROY_DUMB, drm_mode_destroy_dumb_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
};

static struct dev_ops drm_cdevsw = {
	{ "drm", 0, D_TRACKCLOSE | D_MPSAFE },
	.d_open =	drm_open,
	.d_close =	drm_close,
	.d_read =	drm_read,
	.d_ioctl =	drm_ioctl,
	.d_kqfilter =	drm_kqfilter,
	.d_mmap =	drm_mmap,
	.d_mmap_single = drm_mmap_single,
};

static int drm_msi = 1;	/* Enable by default. */
TUNABLE_INT("hw.drm.msi", &drm_msi);
SYSCTL_NODE(_hw, OID_AUTO, drm, CTLFLAG_RW, NULL, "DRM device");
SYSCTL_INT(_hw_drm, OID_AUTO, msi, CTLFLAG_RD, &drm_msi, 1,
    "Enable MSI interrupts for drm devices");

static struct drm_msi_blacklist_entry drm_msi_blacklist[] = {
	{0x8086, 0x2772}, /* Intel i945G	*/ \
	{0x8086, 0x27A2}, /* Intel i945GM	*/ \
	{0x8086, 0x27AE}, /* Intel i945GME	*/ \
	{0, 0}
};

static int drm_msi_is_blacklisted(struct drm_device *dev, unsigned long flags)
{
	int i = 0;

	if (dev->driver->use_msi != NULL) {
		int use_msi;

		use_msi = dev->driver->use_msi(dev, flags);

		return (!use_msi);
	}

	/* TODO: Maybe move this to a callback in i915? */
	for (i = 0; drm_msi_blacklist[i].vendor != 0; i++) {
		if ((drm_msi_blacklist[i].vendor == dev->pci_vendor) &&
		    (drm_msi_blacklist[i].device == dev->pci_device)) {
			return 1;
		}
	}

	return 0;
}

int drm_probe(device_t kdev, drm_pci_id_list_t *idlist)
{
	drm_pci_id_list_t *id_entry;
	int vendor, device;

	vendor = pci_get_vendor(kdev);
	device = pci_get_device(kdev);

	if (pci_get_class(kdev) != PCIC_DISPLAY)
		return ENXIO;

	id_entry = drm_find_description(vendor, device, idlist);
	if (id_entry != NULL) {
		if (!device_get_desc(kdev)) {
			DRM_DEBUG("desc : %s\n", device_get_desc(kdev));
			device_set_desc(kdev, id_entry->name);
		}
		return 0;
	}

	return ENXIO;
}

int drm_attach(device_t kdev, drm_pci_id_list_t *idlist)
{
	struct drm_device *dev;
	drm_pci_id_list_t *id_entry;
	int unit, error, msicount;
	int rid = 0;

	unit = device_get_unit(kdev);
	dev = device_get_softc(kdev);

	if (!strcmp(device_get_name(kdev), "drmsub"))
		dev->dev = device_get_parent(kdev);
	else
		dev->dev = kdev;

	dev->pci_domain = pci_get_domain(dev->dev);
	dev->pci_bus = pci_get_bus(dev->dev);
	dev->pci_slot = pci_get_slot(dev->dev);
	dev->pci_func = pci_get_function(dev->dev);

	dev->pci_vendor = pci_get_vendor(dev->dev);
	dev->pci_device = pci_get_device(dev->dev);
	dev->pci_subvendor = pci_get_subvendor(dev->dev);
	dev->pci_subdevice = pci_get_subdevice(dev->dev);

	id_entry = drm_find_description(dev->pci_vendor,
	    dev->pci_device, idlist);
	dev->id_entry = id_entry;

	if (drm_core_check_feature(dev, DRIVER_HAVE_IRQ)) {
		if (drm_msi &&
		    !drm_msi_is_blacklisted(dev, dev->id_entry->driver_private)) {
			msicount = pci_msi_count(dev->dev);
			DRM_DEBUG("MSI count = %d\n", msicount);
			if (msicount > 1)
				msicount = 1;

			if (pci_alloc_msi(dev->dev, &rid, msicount, -1) == 0) {
				DRM_INFO("MSI enabled %d message(s)\n",
				    msicount);
				dev->msi_enabled = 1;
				dev->irqrid = rid;
			}
		}

		dev->irqr = bus_alloc_resource_any(dev->dev, SYS_RES_IRQ,
		    &dev->irqrid, RF_SHAREABLE);
		if (!dev->irqr) {
			return (ENOENT);
		}

		dev->irq = (int) rman_get_start(dev->irqr);
	}

	lockinit(&dev->dev_lock, "drmdev", 0, LK_CANRECURSE);
	lwkt_serialize_init(&dev->irq_lock);
	lockinit(&dev->vbl_lock, "drmvbl", 0, LK_CANRECURSE);
	lockinit(&dev->event_lock, "drmev", 0, LK_CANRECURSE);
	lockinit(&dev->dev_struct_lock, "drmslk", 0, LK_CANRECURSE);

	error = drm_load(dev);
	if (error)
		goto error;

	error = drm_create_cdevs(kdev);
	if (error)
		goto error;

	return (error);
error:
	if (dev->irqr) {
		bus_release_resource(dev->dev, SYS_RES_IRQ,
		    dev->irqrid, dev->irqr);
	}
	if (dev->msi_enabled) {
		pci_release_msi(dev->dev);
	}
	return (error);
}

int
drm_create_cdevs(device_t kdev)
{
	struct drm_device *dev;
	int error, unit;

	unit = device_get_unit(kdev);
	dev = device_get_softc(kdev);

	dev->devnode = make_dev(&drm_cdevsw, unit, DRM_DEV_UID, DRM_DEV_GID,
				DRM_DEV_MODE, "dri/card%d", unit);
	error = 0;
	if (error == 0)
		dev->devnode->si_drv1 = dev;
	return (error);
}

int drm_detach(device_t kdev)
{
	struct drm_device *dev;

	dev = device_get_softc(kdev);
	drm_unload(dev);
	if (dev->irqr) {
		bus_release_resource(dev->dev, SYS_RES_IRQ, dev->irqrid,
		    dev->irqr);
		if (dev->msi_enabled) {
			pci_release_msi(dev->dev);
			DRM_INFO("MSI released\n");
		}
	}
	return (0);
}

#ifndef DRM_DEV_NAME
#define DRM_DEV_NAME "drm"
#endif

devclass_t drm_devclass;

drm_pci_id_list_t *drm_find_description(int vendor, int device,
    drm_pci_id_list_t *idlist)
{
	int i = 0;

	for (i = 0; idlist[i].vendor != 0; i++) {
		if ((idlist[i].vendor == vendor) &&
		    ((idlist[i].device == device) ||
		    (idlist[i].device == 0))) {
			return &idlist[i];
		}
	}
	return NULL;
}

/**
 * Take down the DRM device.
 *
 * \param dev DRM device structure.
 *
 * Frees every resource in \p dev.
 *
 * \sa drm_device
 */
static int drm_lastclose(struct drm_device *dev)
{
	drm_magic_entry_t *pt, *next;
	int i;

	DRM_DEBUG("\n");

	if (dev->driver->lastclose != NULL)
		dev->driver->lastclose(dev);

	if (!drm_core_check_feature(dev, DRIVER_MODESET) && dev->irq_enabled)
		drm_irq_uninstall(dev);

	DRM_LOCK(dev);
	if (dev->unique) {
		drm_free(dev->unique, DRM_MEM_DRIVER);
		dev->unique = NULL;
		dev->unique_len = 0;
	}
	/* Clear pid list */
	for (i = 0; i < DRM_HASH_SIZE; i++) {
		for (pt = dev->magiclist[i].head; pt; pt = next) {
			next = pt->next;
			drm_free(pt, DRM_MEM_MAGIC);
		}
		dev->magiclist[i].head = dev->magiclist[i].tail = NULL;
	}

	/* Clear AGP information */
	if (dev->agp) {
		drm_agp_mem_t *entry;
		drm_agp_mem_t *nexte;

		/* Remove AGP resources, but leave dev->agp intact until
		 * drm_unload is called.
		 */
		for (entry = dev->agp->memory; entry; entry = nexte) {
			nexte = entry->next;
			if (entry->bound)
				drm_agp_unbind_memory(entry->handle);
			drm_agp_free_memory(entry->handle);
			drm_free(entry, DRM_MEM_AGPLISTS);
		}
		dev->agp->memory = NULL;

		if (dev->agp->acquired)
			drm_agp_release(dev);

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}
	if (dev->sg != NULL) {
		drm_sg_cleanup(dev->sg);
		dev->sg = NULL;
	}

	drm_dma_takedown(dev);
	if (dev->lock.hw_lock) {
		dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.file_priv = NULL;
		DRM_WAKEUP_INT((void *)&dev->lock.lock_queue);
	}
	DRM_UNLOCK(dev);

	return 0;
}

static int drm_load(struct drm_device *dev)
{
	int i, retcode;

	DRM_DEBUG("\n");

	INIT_LIST_HEAD(&dev->maplist);
	dev->map_unrhdr = new_unrhdr(1, ((1 << DRM_MAP_HANDLE_BITS) - 1), NULL);
	if (dev->map_unrhdr == NULL) {
		DRM_ERROR("Couldn't allocate map number allocator\n");
		return EINVAL;
	}


	drm_mem_init();
	drm_sysctl_init(dev);
	INIT_LIST_HEAD(&dev->filelist);

	dev->counters  = 6;
	dev->types[0]  = _DRM_STAT_LOCK;
	dev->types[1]  = _DRM_STAT_OPENS;
	dev->types[2]  = _DRM_STAT_CLOSES;
	dev->types[3]  = _DRM_STAT_IOCTLS;
	dev->types[4]  = _DRM_STAT_LOCKS;
	dev->types[5]  = _DRM_STAT_UNLOCKS;

	for (i = 0; i < DRM_ARRAY_SIZE(dev->counts); i++)
		atomic_set(&dev->counts[i], 0);

	INIT_LIST_HEAD(&dev->vblank_event_list);

	if (drm_core_has_AGP(dev)) {
		if (drm_device_is_agp(dev))
			dev->agp = drm_agp_init();
		if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP) &&
		    dev->agp == NULL) {
			DRM_ERROR("Card isn't AGP, or couldn't initialize "
			    "AGP.\n");
			retcode = ENOMEM;
			goto error;
		}
		if (dev->agp != NULL && dev->agp->agp_info.ai_aperture_base != 0) {
			if (drm_mtrr_add(dev->agp->agp_info.ai_aperture_base,
			    dev->agp->agp_info.ai_aperture_size, DRM_MTRR_WC) == 0)
				dev->agp->agp_mtrr = 1;
		}
	}

	retcode = drm_ctxbitmap_init(dev);
	if (retcode != 0) {
		DRM_ERROR("Cannot allocate memory for context bitmap.\n");
		goto error;
	}

	if (dev->driver->driver_features & DRIVER_GEM) {
		retcode = drm_gem_init(dev);
		if (retcode != 0) {
			DRM_ERROR("Cannot initialize graphics execution "
				  "manager (GEM)\n");
			goto error1;
		}
	}

	if (dev->driver->load != NULL) {
		DRM_LOCK(dev);
		/* Shared code returns -errno. */
		retcode = -dev->driver->load(dev,
		    dev->id_entry->driver_private);
		if (pci_enable_busmaster(dev->dev))
			DRM_ERROR("Request to enable bus-master failed.\n");
		DRM_UNLOCK(dev);
		if (retcode != 0)
			goto error1;
	}

	DRM_INFO("Initialized %s %d.%d.%d %s\n",
	    dev->driver->name,
	    dev->driver->major,
	    dev->driver->minor,
	    dev->driver->patchlevel,
	    dev->driver->date);

	return 0;

error1:
	drm_gem_destroy(dev);
error:
	drm_ctxbitmap_cleanup(dev);
	drm_sysctl_cleanup(dev);
	DRM_LOCK(dev);
	drm_lastclose(dev);
	DRM_UNLOCK(dev);
	if (dev->devnode != NULL)
		destroy_dev(dev->devnode);

	lockuninit(&dev->vbl_lock);
	lockuninit(&dev->dev_lock);
	lockuninit(&dev->event_lock);
	lockuninit(&dev->dev_struct_lock);

	return retcode;
}

static void drm_unload(struct drm_device *dev)
{
	int i;

	DRM_DEBUG("\n");

	drm_sysctl_cleanup(dev);
	if (dev->devnode != NULL)
		destroy_dev(dev->devnode);

	drm_ctxbitmap_cleanup(dev);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);

	if (dev->agp && dev->agp->agp_mtrr) {
		int __unused retcode;

		retcode = drm_mtrr_del(0, dev->agp->agp_info.ai_aperture_base,
		    dev->agp->agp_info.ai_aperture_size, DRM_MTRR_WC);
		DRM_DEBUG("mtrr_del = %d", retcode);
	}

	drm_vblank_cleanup(dev);

	DRM_LOCK(dev);
	drm_lastclose(dev);
	DRM_UNLOCK(dev);

	/* Clean up PCI resources allocated by drm_bufs.c.  We're not really
	 * worried about resource consumption while the DRM is inactive (between
	 * lastclose and firstopen or unload) because these aren't actually
	 * taking up KVA, just keeping the PCI resource allocated.
	 */
	for (i = 0; i < DRM_MAX_PCI_RESOURCE; i++) {
		if (dev->pcir[i] == NULL)
			continue;
		bus_release_resource(dev->dev, SYS_RES_MEMORY,
		    dev->pcirid[i], dev->pcir[i]);
		dev->pcir[i] = NULL;
	}

	if (dev->agp) {
		drm_free(dev->agp, DRM_MEM_AGPLISTS);
		dev->agp = NULL;
	}

	if (dev->driver->unload != NULL) {
		DRM_LOCK(dev);
		dev->driver->unload(dev);
		DRM_UNLOCK(dev);
	}

	delete_unrhdr(dev->map_unrhdr);

	drm_mem_uninit();

	if (pci_disable_busmaster(dev->dev))
		DRM_ERROR("Request to disable bus-master failed.\n");

	lockuninit(&dev->vbl_lock);
	lockuninit(&dev->dev_lock);
	lockuninit(&dev->event_lock);
	lockuninit(&dev->dev_struct_lock);
}

int drm_version(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_version *version = data;
	int len;

#define DRM_COPY( name, value )						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( DRM_COPY_TO_USER( name, value, len ) )		\
			return EFAULT;				\
	}

	version->version_major		= dev->driver->major;
	version->version_minor		= dev->driver->minor;
	version->version_patchlevel	= dev->driver->patchlevel;

	DRM_COPY(version->name, dev->driver->name);
	DRM_COPY(version->date, dev->driver->date);
	DRM_COPY(version->desc, dev->driver->desc);

	return 0;
}

/*
 * Stub is needed for devfs
 */
int drm_close(struct dev_close_args *ap)
{
	return 0;
}

void drm_cdevpriv_dtor(void *cd)
{
	struct drm_file *file_priv = cd;
	struct drm_device *dev = file_priv->dev;
	int retcode = 0;

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	DRM_LOCK(dev);

	if (dev->driver->preclose != NULL)
		dev->driver->preclose(dev, file_priv);

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
	    DRM_CURRENTPID, (long)dev->dev, dev->open_count);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_release(dev, file_priv);

	if (dev->lock.hw_lock && _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)
	    && dev->lock.file_priv == file_priv) {
		DRM_DEBUG("Process %d dead, freeing lock for context %d\n",
			  DRM_CURRENTPID,
			  _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock));
		if (dev->driver->reclaim_buffers_locked != NULL)
			dev->driver->reclaim_buffers_locked(dev, file_priv);

		drm_lock_free(&dev->lock,
		    _DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock));

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	} else if (dev->driver->reclaim_buffers_locked != NULL &&
	    dev->lock.hw_lock != NULL) {
		/* The lock is required to reclaim buffers */
		for (;;) {
			if (!dev->lock.hw_lock) {
				/* Device has been unregistered */
				retcode = EINTR;
				break;
			}
			if (drm_lock_take(&dev->lock, DRM_KERNEL_CONTEXT)) {
				dev->lock.file_priv = file_priv;
				dev->lock.lock_time = jiffies;
				atomic_inc(&dev->counts[_DRM_STAT_LOCKS]);
				break;	/* Got lock */
			}
			/* Contention */
			retcode = DRM_LOCK_SLEEP(dev, &dev->lock.lock_queue,
			    PCATCH, "drmlk2", 0);
			if (retcode)
				break;
		}
		if (retcode == 0) {
			dev->driver->reclaim_buffers_locked(dev, file_priv);
			drm_lock_free(&dev->lock, DRM_KERNEL_CONTEXT);
		}
	}

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA) &&
	    !dev->driver->reclaim_buffers_locked)
		drm_reclaim_buffers(dev, file_priv);

	funsetown(&dev->buf_sigio);

	if (dev->driver->postclose != NULL)
		dev->driver->postclose(dev, file_priv);
	list_del(&file_priv->lhead);


	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc(&dev->counts[_DRM_STAT_CLOSES]);
	device_unbusy(dev->dev);
	if (--dev->open_count == 0) {
		retcode = drm_lastclose(dev);
	}

	DRM_UNLOCK(dev);
}

/* drm_ioctl is called whenever a process performs an ioctl on /dev/drm.
 */
int drm_ioctl(struct dev_ioctl_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	caddr_t data = ap->a_data;
	struct thread *p = curthread;
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	int retcode = 0;
	drm_ioctl_desc_t *ioctl;
	int (*func)(struct drm_device *dev, void *data, struct drm_file *file_priv);
	int nr = DRM_IOCTL_NR(cmd);
	int is_driver_ioctl = 0;
	struct drm_file *file_priv;

	retcode = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (retcode !=0) {
		DRM_ERROR("can't find authenticator\n");
		return EINVAL;
	}

	atomic_inc(&dev->counts[_DRM_STAT_IOCTLS]);
	++file_priv->ioctl_count;

	DRM_DEBUG("pid=%d, cmd=0x%02lx, nr=0x%02x, dev 0x%lx, auth=%d\n",
	    DRM_CURRENTPID, cmd, nr, (long)dev->dev,
	    file_priv->authenticated);

	switch (cmd) {
	case FIONBIO:
	case FIOASYNC:
		return 0;

	case FIOSETOWN:
		return fsetown(*(int *)data, &dev->buf_sigio);

	case FIOGETOWN:
		*(int *) data = fgetown(&dev->buf_sigio);
		return 0;
	}

	if (IOCGROUP(cmd) != DRM_IOCTL_BASE) {
		DRM_DEBUG("Bad ioctl group 0x%x\n", (int)IOCGROUP(cmd));
		return EINVAL;
	}

	ioctl = &drm_ioctls[nr];
	/* It's not a core DRM ioctl, try driver-specific. */
	if (ioctl->func == NULL && nr >= DRM_COMMAND_BASE) {
		/* The array entries begin at DRM_COMMAND_BASE ioctl nr */
		nr -= DRM_COMMAND_BASE;
		if (nr > dev->driver->num_ioctls) {
			return EINVAL;
		}
		ioctl = &dev->driver->ioctls[nr];
		is_driver_ioctl = 1;
	}
	func = ioctl->func;

	if (func == NULL) {
		DRM_DEBUG("no function\n");
		return EINVAL;
	}

	if (((ioctl->flags & DRM_ROOT_ONLY) && !DRM_SUSER(p)) ||
	    ((ioctl->flags & DRM_AUTH) && !file_priv->authenticated) ||
	    ((ioctl->flags & DRM_MASTER) && !file_priv->master))
		return EACCES;

	if (is_driver_ioctl) {
		if ((ioctl->flags & DRM_UNLOCKED) == 0)
			DRM_LOCK(dev);
		/* shared code returns -errno */
		retcode = -func(dev, data, file_priv);
		if ((ioctl->flags & DRM_UNLOCKED) == 0)
			DRM_UNLOCK(dev);
	} else {
		retcode = func(dev, data, file_priv);
	}

	if (retcode != 0)
		DRM_DEBUG("    returning %d\n", retcode);
	if (retcode != 0 &&
	    (drm_debug & DRM_DEBUGBITS_FAILED_IOCTL) != 0) {
		kprintf(
"pid %d, cmd 0x%02lx, nr 0x%02x/%1d, dev 0x%lx, auth %d, res %d\n",
		    DRM_CURRENTPID, cmd, nr, is_driver_ioctl, (long)dev->dev,
		    file_priv->authenticated, retcode);
	}

	return retcode;
}

drm_local_map_t *drm_getsarea(struct drm_device *dev)
{
	struct drm_map_list *entry;

	list_for_each_entry(entry, &dev->maplist, head) {
		if (entry->map && entry->map->type == _DRM_SHM &&
		    (entry->map->flags & _DRM_CONTAINS_LOCK)) {
			return entry->map;
		}
	}

	return NULL;
}

int
drm_add_busid_modesetting(struct drm_device *dev, struct sysctl_ctx_list *ctx,
    struct sysctl_oid *top)
{
	struct sysctl_oid *oid;

	ksnprintf(dev->busid_str, sizeof(dev->busid_str),
	     "pci:%04x:%02x:%02x.%d", dev->pci_domain, dev->pci_bus,
	     dev->pci_slot, dev->pci_func);
	oid = SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(top), OID_AUTO, "busid",
	    CTLFLAG_RD, dev->busid_str, 0, NULL);
	if (oid == NULL)
		return (ENOMEM);
	dev->modesetting = (dev->driver->driver_features & DRIVER_MODESET) != 0;
	oid = SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(top), OID_AUTO,
	    "modesetting", CTLFLAG_RD, &dev->modesetting, 0, NULL);
	if (oid == NULL)
		return (ENOMEM);

	return (0);
}

int
drm_mmap_single(struct dev_mmap_single_args *ap)
{
	struct drm_device *dev;
	struct cdev *kdev = ap->a_head.a_dev;
	vm_ooffset_t *offset = ap->a_offset;
	vm_size_t size = ap->a_size;
	struct vm_object **obj_res = ap->a_object;
	int nprot = ap->a_nprot;

	dev = drm_get_device_from_kdev(kdev);
	if (dev->drm_ttm_bdev != NULL) {
		return (ttm_bo_mmap_single(dev->drm_ttm_bdev, offset, size,
		    obj_res, nprot));
	} else if ((dev->driver->driver_features & DRIVER_GEM) != 0) {
		return (drm_gem_mmap_single(dev, offset, size, obj_res, nprot));
	} else {
		return (ENODEV);
	}
}

#if DRM_LINUX

#include <sys/sysproto.h>

MODULE_DEPEND(DRIVER_NAME, linux, 1, 1, 1);

#define LINUX_IOCTL_DRM_MIN		0x6400
#define LINUX_IOCTL_DRM_MAX		0x64ff

static linux_ioctl_function_t drm_linux_ioctl;
static struct linux_ioctl_handler drm_handler = {drm_linux_ioctl,
    LINUX_IOCTL_DRM_MIN, LINUX_IOCTL_DRM_MAX};

/* The bits for in/out are switched on Linux */
#define LINUX_IOC_IN	IOC_OUT
#define LINUX_IOC_OUT	IOC_IN

static int
drm_linux_ioctl(DRM_STRUCTPROC *p, struct linux_ioctl_args* args)
{
	int error;
	int cmd = args->cmd;

	args->cmd &= ~(LINUX_IOC_IN | LINUX_IOC_OUT);
	if (cmd & LINUX_IOC_IN)
		args->cmd |= IOC_IN;
	if (cmd & LINUX_IOC_OUT)
		args->cmd |= IOC_OUT;
	
	error = ioctl(p, (struct ioctl_args *)args);

	return error;
}
#endif /* DRM_LINUX */

static int
drm_core_init(void *arg)
{

	drm_global_init();

#if DRM_LINUX
	linux_ioctl_register_handler(&drm_handler);
#endif /* DRM_LINUX */

	DRM_INFO("Initialized %s %d.%d.%d %s\n",
		 CORE_NAME, CORE_MAJOR, CORE_MINOR, CORE_PATCHLEVEL, CORE_DATE);
	return 0;
}

static void
drm_core_exit(void *arg)
{

#if DRM_LINUX
	linux_ioctl_unregister_handler(&drm_handler);
#endif /* DRM_LINUX */

	drm_global_release();
}

SYSINIT(drm_register, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    drm_core_init, NULL);
SYSUNINIT(drm_unregister, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    drm_core_exit, NULL);

/*
 * Check if dmi_system_id structure matches system DMI data
 */
static bool
dmi_found(const struct dmi_system_id *dsi)
{
	int i, slot;
	bool found = false;
	char *sys_vendor, *board_vendor, *product_name, *board_name;

	sys_vendor = kgetenv("smbios.system.maker");
	board_vendor = kgetenv("smbios.planar.maker");
	product_name = kgetenv("smbios.system.product");
	board_name = kgetenv("smbios.planar.product");

	for (i = 0; i < NELEM(dsi->matches); i++) {
		slot = dsi->matches[i].slot;
		switch (slot) {
		case DMI_NONE:
			break;
		case DMI_SYS_VENDOR:
			if (sys_vendor != NULL &&
			    !strcmp(sys_vendor, dsi->matches[i].substr))
				break;
			else
				goto done;
		case DMI_BOARD_VENDOR:
			if (board_vendor != NULL &&
			    !strcmp(board_vendor, dsi->matches[i].substr))
				break;
			else
				goto done;
		case DMI_PRODUCT_NAME:
			if (product_name != NULL &&
			    !strcmp(product_name, dsi->matches[i].substr))
				break;
			else
				goto done;
		case DMI_BOARD_NAME:
			if (board_name != NULL &&
			    !strcmp(board_name, dsi->matches[i].substr))
				break;
			else
				goto done;
		default:
			goto done;
		}
	}
	found = true;

done:
	if (sys_vendor != NULL)
		kfreeenv(sys_vendor);
	if (board_vendor != NULL)
		kfreeenv(board_vendor);
	if (product_name != NULL)
		kfreeenv(product_name);
	if (board_name != NULL)
		kfreeenv(board_name);

	return found;
}

bool
dmi_check_system(const struct dmi_system_id *sysid)
{
	const struct dmi_system_id *dsi;
	int num = 0;

	for (dsi = sysid; dsi->matches[0].slot != 0 ; dsi++) {
		if (dmi_found(dsi)) {
			num++;
			if (dsi->callback && dsi->callback(dsi))
				break;
		}
	}
	return (num);
}
