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

/* from BKL pushdown */
DEFINE_MUTEX(drm_global_mutex);

/**
 * DOC: file operations
 *
 * Drivers must define the file operations structure that forms the DRM
 * userspace API entry point, even though most of those operations are
 * implemented in the DRM core. The mandatory functions are drm_open(),
 * drm_read(), drm_ioctl() and drm_compat_ioctl if CONFIG_COMPAT is enabled.
 * Drivers which implement private ioctls that require 32/64 bit compatibility
 * support must provided their onw .compat_ioctl() handler that processes
 * private ioctls and calls drm_compat_ioctl() for core ioctls.
 *
 * In addition drm_read() and drm_poll() provide support for DRM events. DRM
 * events are a generic and extensible means to send asynchronous events to
 * userspace through the file descriptor. They are used to send vblank event and
 * page flip completions by the KMS API. But drivers can also use it for their
 * own needs, e.g. to signal completion of rendering.
 *
 * The memory mapping implementation will vary depending on how the driver
 * manages memory. Legacy drivers will use the deprecated drm_legacy_mmap()
 * function, modern drivers should use one of the provided memory-manager
 * specific implementations. For GEM-based drivers this is drm_gem_mmap().
 *
 * No other file operations are supported by the DRM userspace API. Overall the
 * following is an example #file_operations structure:
 *
 *     static const example_drm_fops = {
 *             .owner = THIS_MODULE,
 *             .open = drm_open,
 *             .release = drm_release,
 *             .unlocked_ioctl = drm_ioctl,
 *     #ifdef CONFIG_COMPAT
 *             .compat_ioctl = drm_compat_ioctl,
 *     #endif
 *             .poll = drm_poll,
 *             .read = drm_read,
 *             .llseek = no_llseek,
 *             .mmap = drm_gem_mmap,
 *     };
 */

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

/**
 * drm_open - open method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their .open() #file_operations
 * method. It looks up the correct DRM device and instantiates all the per-file
 * resources for it.
 *
 * RETURNS:
 *
 * 0 on success or negative errno value on falure.
 */
int drm_open(struct dev_open_args *ap)
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
		device_busy(dev->dev->bsddev);
		if (!dev->open_count++)
			retcode = drm_setup(dev);
		DRM_UNLOCK(dev);
	}

	DRM_DEBUG("return %d\n", retcode);

	return (retcode);
}
EXPORT_SYMBOL(drm_open);

/*
 * Check whether DRI will run on this CPU.
 *
 * \return non-zero if the DRI will run on this CPU, or zero otherwise.
 */

/*
 * drm_new_set_master - Allocate a new master object and become master for the
 * associated master realm.
 *
 * @dev: The associated device.
 * @fpriv: File private identifying the client.
 *
 * This function must be called with dev::struct_mutex held.
 * Returns negative error code on failure. Zero on success.
 */

/*
 * Called whenever a process opens /dev/drm.
 *
 * \param filp file pointer.
 * \param minor acquired minor-object.
 * \return zero on success or a negative number on failure.
 *
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
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

	INIT_LIST_HEAD(&priv->lhead);
	INIT_LIST_HEAD(&priv->fbs);
	lockinit(&priv->fbs_lock, "dpfl", 0, LK_CANRECURSE);
	INIT_LIST_HEAD(&priv->blobs);
	INIT_LIST_HEAD(&priv->pending_event_list);
	INIT_LIST_HEAD(&priv->event_list);
	init_waitqueue_head(&priv->event_wait);
	priv->event_space = 4096; /* set aside 4k for event buffer */

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_open(dev, priv);

	if (dev->driver->open) {
		/* shared code returns -errno */
		retcode = -dev->driver->open(dev, priv);
		if (retcode != 0) {
			kfree(priv);
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

/*
 * drm_legacy_dev_reinit
 *
 * Reinitializes a legacy/ums drm device in it's lastclose function.
 */

/*
 * Take down the DRM device.
 *
 * \param dev DRM device structure.
 *
 * Frees every resource in \p dev.
 *
 * \sa drm_device
 */
void drm_lastclose(struct drm_device * dev)
{
	DRM_DEBUG("\n");

	if (dev->driver->lastclose)
		dev->driver->lastclose(dev);
	DRM_DEBUG("driver lastclose completed\n");

	if (dev->irq_enabled && !drm_core_check_feature(dev, DRIVER_MODESET))
		drm_irq_uninstall(dev);

	mutex_lock(&dev->struct_mutex);

	if (dev->unique) {
		kfree(dev->unique);
		dev->unique = NULL;
		dev->unique_len = 0;
	}

	drm_legacy_agp_clear(dev);

	drm_legacy_sg_cleanup(dev);
	drm_legacy_dma_takedown(dev);

	if (dev->lock.hw_lock) {
		dev->lock.hw_lock = NULL; /* SHM removed */
		dev->lock.file_priv = NULL;
		wakeup(&dev->lock.lock_queue);
	}

	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("lastclose completed\n");
}

/**
 * drm_release - release method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their .release() #file_operations
 * method. It frees any resources associated with the open file, and if this is
 * the last open file for the DRM device also proceeds to call drm_lastclose().
 *
 * RETURNS:
 *
 * Always succeeds and returns 0.
 */
int drm_release(device_t kdev)
{
	struct drm_device *dev = device_get_softc(kdev);
	struct drm_magic_entry *pt, *next;
	int i;

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

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG("\n");

	drm_sysctl_cleanup(dev);
	if (dev->devnode != NULL)
		destroy_dev(dev->devnode);

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_destroy(dev);

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
		bus_release_resource(dev->dev->bsddev, SYS_RES_MEMORY,
		    dev->pcirid[i], dev->pcir[i]);
		dev->pcir[i] = NULL;
	}

	if (dev->agp) {
		kfree(dev->agp);
		dev->agp = NULL;
	}

	if (dev->driver->unload != NULL) {
		DRM_LOCK(dev);
		dev->driver->unload(dev);
		DRM_UNLOCK(dev);
	}

	if (pci_disable_busmaster(dev->dev->bsddev))
		DRM_ERROR("Request to disable bus-master failed.\n");

	lockuninit(&dev->vbl_lock);
	lockuninit(&dev->dev_lock);
	lockuninit(&dev->event_lock);
	lockuninit(&dev->struct_mutex);

	/* ========================================================
	 * End inline drm_release
	 */

	if (dev->irqr) {
		bus_release_resource(dev->dev->bsddev, SYS_RES_IRQ, dev->irqrid,
		    dev->irqr);
		if (dev->irq_type == PCI_INTR_TYPE_MSI) {
			pci_release_msi(dev->dev->bsddev);
			DRM_INFO("MSI released\n");
		}
	}

	mutex_unlock(&drm_global_mutex);

	return (0);
}
EXPORT_SYMBOL(drm_release);

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

/**
 * drm_read - read method for DRM file
 * @filp: file pointer
 * @buffer: userspace destination pointer for the read
 * @count: count in bytes to read
 * @offset: offset to read
 *
 * This function must be used by drivers as their .read() #file_operations
 * method iff they use DRM events for asynchronous signalling to userspace.
 * Since events are used by the KMS API for vblank and page flip completion this
 * means all modern display drivers must use it.
 *
 * @offset is ignore, DRM events are read like a pipe. Therefore drivers also
 * must set the .llseek() #file_operation to no_llseek(). Polling support is
 * provided by drm_poll().
 *
 * This function will only ever read a full event. Therefore userspace must
 * supply a big enough buffer to fit any event to ensure forward progress. Since
 * the maximum event space is currently 4K it's recommended to just use that for
 * safety.
 *
 * RETURNS:
 *
 * Number of bytes read (always aligned to full events, and can be 0) or a
 * negative error code on failure.
 */
int drm_read(struct dev_read_args *ap)
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

/**
 * drm_poll - poll method for DRM file
 * @filp: file pointer
 * @wait: poll waiter table
 *
 * This function must be used by drivers as their .read() #file_operations
 * method iff they use DRM events for asynchronous signalling to userspace.
 * Since events are used by the KMS API for vblank and page flip completion this
 * means all modern display drivers must use it.
 *
 * See also drm_read().
 *
 * RETURNS:
 *
 * Mask of POLL flags indicating the current status of the file.
 */

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

#ifdef __DragonFly__
/*
 * The Linux layer version of kfree() is a macro and can't be called
 * directly via a function pointer
 */
static void
drm_event_destroy(struct drm_pending_event *e)
{
	kfree(e);
}
#endif

/**
 * drm_event_reserve_init_locked - init a DRM event and reserve space for it
 * @dev: DRM device
 * @file_priv: DRM file private data
 * @p: tracking structure for the pending event
 * @e: actual event data to deliver to userspace
 *
 * This function prepares the passed in event for eventual delivery. If the event
 * doesn't get delivered (because the IOCTL fails later on, before queuing up
 * anything) then the even must be cancelled and freed using
 * drm_event_cancel_free(). Successfully initialized events should be sent out
 * using drm_send_event() or drm_send_event_locked() to signal completion of the
 * asynchronous event to userspace.
 *
 * If callers embedded @p into a larger structure it must be allocated with
 * kmalloc and @p must be the first member element.
 *
 * This is the locked version of drm_event_reserve_init() for callers which
 * already hold dev->event_lock.
 *
 * RETURNS:
 *
 * 0 on success or a negative error code on failure.
 */
int drm_event_reserve_init_locked(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_pending_event *p,
				  struct drm_event *e)
{
	if (file_priv->event_space < e->length)
		return -ENOMEM;

	file_priv->event_space -= e->length;

	p->event = e;
	p->file_priv = file_priv;

	/* we *could* pass this in as arg, but everyone uses kfree: */
#ifdef __DragonFly__
	p->destroy = drm_event_destroy;
#else
	p->destroy = (void (*) (struct drm_pending_event *)) kfree;
#endif

	return 0;
}
EXPORT_SYMBOL(drm_event_reserve_init_locked);

/**
 * drm_event_reserve_init - init a DRM event and reserve space for it
 * @dev: DRM device
 * @file_priv: DRM file private data
 * @p: tracking structure for the pending event
 * @e: actual event data to deliver to userspace
 *
 * This function prepares the passed in event for eventual delivery. If the event
 * doesn't get delivered (because the IOCTL fails later on, before queuing up
 * anything) then the even must be cancelled and freed using
 * drm_event_cancel_free(). Successfully initialized events should be sent out
 * using drm_send_event() or drm_send_event_locked() to signal completion of the
 * asynchronous event to userspace.
 *
 * If callers embedded @p into a larger structure it must be allocated with
 * kmalloc and @p must be the first member element.
 *
 * Callers which already hold dev->event_lock should use
 * drm_event_reserve_init() instead.
 *
 * RETURNS:
 *
 * 0 on success or a negative error code on failure.
 */
int drm_event_reserve_init(struct drm_device *dev,
			   struct drm_file *file_priv,
			   struct drm_pending_event *p,
			   struct drm_event *e)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->event_lock, flags);
	ret = drm_event_reserve_init_locked(dev, file_priv, p, e);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return ret;
}
EXPORT_SYMBOL(drm_event_reserve_init);

/**
 * drm_event_cancel_free - free a DRM event and release it's space
 * @dev: DRM device
 * @p: tracking structure for the pending event
 *
 * This function frees the event @p initialized with drm_event_reserve_init()
 * and releases any allocated space.
 */
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p)
{
	unsigned long flags;
	spin_lock_irqsave(&dev->event_lock, flags);
	p->file_priv->event_space += p->event->length;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	p->destroy(p);
}
EXPORT_SYMBOL(drm_event_cancel_free);

/**
 * drm_send_event_locked - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. Callers must already hold
 * dev->event_lock, see drm_send_event() for the unlocked version.
 */
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e)
{
	assert_spin_locked(&dev->event_lock);

	list_add_tail(&e->link,
		      &e->file_priv->event_list);
	wake_up_interruptible(&e->file_priv->event_wait);
#ifdef __DragonFly__
	KNOTE(&e->file_priv->dkq.ki_note, 0);
#endif

}
EXPORT_SYMBOL(drm_send_event_locked);

/**
 * drm_send_event - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. This function acquires dev->event_lock,
 * see drm_send_event_locked() for callers which already hold this lock.
 */
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);
	drm_send_event_locked(dev, e);
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}
EXPORT_SYMBOL(drm_send_event);
