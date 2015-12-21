/**
 * \file drm_fops.c
 * File operations for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/devfs.h>

#include <drm/drmP.h>
#include <linux/module.h>
#include "drm_legacy.h"
#include "drm_internal.h"

/* from BKL pushdown: note that nothing else serializes idr_find() */
DEFINE_MUTEX(drm_global_mutex);
EXPORT_SYMBOL(drm_global_mutex);

extern drm_pci_id_list_t *drm_find_description(int vendor, int device,
    drm_pci_id_list_t *idlist);
extern devclass_t drm_devclass;

static int drm_setup(struct drm_device *dev)
{
	drm_local_map_t *map;
	int i;

	DRM_LOCK_ASSERT(dev);

	/* prebuild the SAREA */
	i = drm_legacy_addmap(dev, 0, SAREA_MAX, _DRM_SHM,
	    _DRM_CONTAINS_LOCK, &map);
	if (i != 0)
		return i;

	if (dev->driver->firstopen)
		dev->driver->firstopen(dev);

	dev->buf_use = 0;

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA)) {
		i = drm_legacy_dma_setup(dev);
		if (i != 0)
			return i;
	}

	drm_ht_create(&dev->magiclist, DRM_MAGIC_HASH_ORDER);
	INIT_LIST_HEAD(&dev->magicfree);

	init_waitqueue_head(&dev->lock.lock_queue);
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev->irq_enabled = 0;
	dev->context_flag = 0;
	dev->last_context = 0;
	dev->if_version = 0;

	dev->buf_sigio = NULL;

	DRM_DEBUG("\n");

	return 0;
}

#define DRIVER_SOFTC(unit) \
	((struct drm_device *)devclass_get_softc(drm_devclass, unit))

int
drm_open(struct dev_open_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	int flags = ap->a_oflags;
	int fmt = 0;
	struct thread *p = curthread;
	struct drm_device *dev;
	int retcode;

	dev = DRIVER_SOFTC(minor(kdev));
	if (dev == NULL)
		return (ENXIO);

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	retcode = drm_open_helper(kdev, flags, fmt, p, dev, ap->a_fp);

	if (retcode == 0) {
		atomic_inc(&dev->counts[_DRM_STAT_OPENS]);
		DRM_LOCK(dev);
		device_busy(dev->dev);
		if (!dev->open_count++)
			retcode = drm_setup(dev);
		DRM_UNLOCK(dev);
	}

	DRM_DEBUG("return %d\n", retcode);

	return (retcode);
}

/* drm_open_helper is called whenever a process opens /dev/drm. */
int drm_open_helper(struct cdev *kdev, int flags, int fmt, DRM_STRUCTPROC *p,
		    struct drm_device *dev, struct file *fp)
{
	struct drm_file *priv;
	int retcode;

	if (flags & O_EXCL)
		return EBUSY; /* No exclusive opens */
	dev->flags = flags;

	DRM_DEBUG("pid = %d, device = %s\n", DRM_CURRENTPID, devtoname(kdev));

	priv = kmalloc(sizeof(*priv), M_DRM, M_WAITOK | M_NULLOK | M_ZERO);
	if (priv == NULL) {
		return ENOMEM;
	}
	
	DRM_LOCK(dev);
	priv->dev		= dev;
	priv->uid               = p->td_proc->p_ucred->cr_svuid;
	priv->pid		= p->td_proc->p_pid;

	/* for compatibility root is always authenticated */
	priv->authenticated = capable(CAP_SYS_ADMIN);

	INIT_LIST_HEAD(&priv->fbs);
	INIT_LIST_HEAD(&priv->event_list);
	init_waitqueue_head(&priv->event_wait);
	priv->event_space = 4096; /* set aside 4k for event buffer */

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_open(dev, priv);

	if (dev->driver->open) {
		/* shared code returns -errno */
		retcode = -dev->driver->open(dev, priv);
		if (retcode != 0) {
			drm_free(priv, M_DRM);
			DRM_UNLOCK(dev);
			return retcode;
		}
	}

	/* first opener automatically becomes master */
	priv->master = list_empty(&dev->filelist);

	list_add(&priv->lhead, &dev->filelist);
	DRM_UNLOCK(dev);
	kdev->si_drv1 = dev;

	retcode = devfs_set_cdevpriv(fp, priv, &drm_cdevpriv_dtor);
	if (retcode != 0)
		drm_cdevpriv_dtor(priv);

	return retcode;
}

/**
 * Release file.
 *
 * \param inode device inode
 * \param file_priv DRM file private.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls drm_lastclose().
 */

static void drm_unload(struct drm_device *dev)
{
	int i;

	DRM_DEBUG("\n");

	drm_sysctl_cleanup(dev);
	if (dev->devnode != NULL)
		destroy_dev(dev->devnode);

	if (drm_core_check_feature(dev, DRIVER_GEM))
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
		drm_free(dev->agp, M_DRM);
		dev->agp = NULL;
	}

	if (dev->driver->unload != NULL) {
		DRM_LOCK(dev);
		dev->driver->unload(dev);
		DRM_UNLOCK(dev);
	}

	drm_mem_uninit();

	if (pci_disable_busmaster(dev->dev))
		DRM_ERROR("Request to disable bus-master failed.\n");

	lockuninit(&dev->vbl_lock);
	lockuninit(&dev->dev_lock);
	lockuninit(&dev->event_lock);
	lockuninit(&dev->struct_mutex);
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
int drm_lastclose(struct drm_device * dev)
{
	DRM_DEBUG("\n");

	if (dev->driver->lastclose)
		dev->driver->lastclose(dev);
	DRM_DEBUG("driver lastclose completed\n");

	if (dev->irq_enabled && !drm_core_check_feature(dev, DRIVER_MODESET))
		drm_irq_uninstall(dev);

	mutex_lock(&dev->struct_mutex);

	if (dev->unique) {
		drm_free(dev->unique, M_DRM);
		dev->unique = NULL;
		dev->unique_len = 0;
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
			drm_free(entry, M_DRM);
		}
		dev->agp->memory = NULL;

		if (dev->agp->acquired)
			drm_agp_release(dev);

		dev->agp->acquired = 0;
		dev->agp->enabled  = 0;
	}
	if (dev->sg != NULL) {
		drm_legacy_sg_cleanup(dev->sg);
		dev->sg = NULL;
	}

	drm_legacy_dma_takedown(dev);

	if (dev->lock.hw_lock) {
		dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.file_priv = NULL;
		wakeup(&dev->lock.lock_queue);
	}

	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("lastclose completed\n");
	return 0;
}

/**
 * Release file.
 *
 * \param inode device inode
 * \param file_priv DRM file private.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls drm_lastclose().
 */
int drm_release(device_t kdev)
{
	struct drm_device *dev = device_get_softc(kdev);
	struct drm_magic_entry *pt, *next;

	mutex_lock(&drm_global_mutex);

	/* Clear pid list */
	if (dev->magicfree.next) {
		list_for_each_entry_safe(pt, next, &dev->magicfree, head) {
			list_del(&pt->head);
			drm_ht_remove_item(&dev->magiclist, &pt->hash_item);
			kfree(pt);
		}
		drm_ht_remove(&dev->magiclist);
	}

	drm_unload(dev);
	if (dev->irqr) {
		bus_release_resource(dev->dev, SYS_RES_IRQ, dev->irqrid,
		    dev->irqr);
		if (dev->irq_type == PCI_INTR_TYPE_MSI) {
			pci_release_msi(dev->dev);
			DRM_INFO("MSI released\n");
		}
	}

	mutex_unlock(&drm_global_mutex);

	return (0);
}

static bool
drm_dequeue_event(struct drm_device *dev, struct drm_file *file_priv,
    struct uio *uio, struct drm_pending_event **out)
{
	struct drm_pending_event *e;
	bool ret = false;

	lockmgr(&dev->event_lock, LK_EXCLUSIVE);

	*out = NULL;
	if (list_empty(&file_priv->event_list))
		goto out;
	e = list_first_entry(&file_priv->event_list,
	    struct drm_pending_event, link);
	if (e->event->length > uio->uio_resid)
		goto out;

	file_priv->event_space += e->event->length;
	list_del(&e->link);
	*out = e;
	ret = true;

out:
	lockmgr(&dev->event_lock, LK_RELEASE);
	return ret;
}

int
drm_read(struct dev_read_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct drm_file *file_priv;
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_pending_event *e;
	int error;
	int ret;

	error = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}

	ret = wait_event_interruptible(file_priv->event_wait,
				       !list_empty(&file_priv->event_list));
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
	if (ret < 0)
		return -ret;

	while (drm_dequeue_event(dev, file_priv, uio, &e)) {
		error = uiomove((caddr_t)e->event, e->event->length, uio);
		e->destroy(e);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
drmfilt(struct knote *kn, long hint)
{
	struct drm_file *file_priv;
	struct drm_device *dev;
	int ready = 0;

	file_priv = (struct drm_file *)kn->kn_hook;
	dev = file_priv->dev;
	lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	if (!list_empty(&file_priv->event_list))
		ready = 1;
	lockmgr(&dev->event_lock, LK_RELEASE);

	return (ready);
}

static void
drmfilt_detach(struct knote *kn)
{
	struct drm_file *file_priv;
	struct drm_device *dev;
	struct klist *klist;

	file_priv = (struct drm_file *)kn->kn_hook;
	dev = file_priv->dev;

	klist = &file_priv->dkq.ki_note;
	knote_remove(klist, kn);
}

static struct filterops drmfiltops =
        { FILTEROP_MPSAFE | FILTEROP_ISFD, NULL, drmfilt_detach, drmfilt };

int
drm_kqfilter(struct dev_kqfilter_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	struct drm_file *file_priv;
	struct drm_device *dev;
	struct knote *kn = ap->a_kn;
	struct klist *klist;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}
	dev = drm_get_device_from_kdev(kdev);

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
	case EVFILT_WRITE:
		kn->kn_fop = &drmfiltops;
		kn->kn_hook = (caddr_t)file_priv;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &file_priv->dkq.ki_note;
	knote_insert(klist, kn);

	return (0);
}
