/*
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
#include <sys/uio.h>	/* must come first to avoid kfree() macros issues */

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <drm/drm_client.h>
#include <drm/drm_file.h>
#include <drm/drmP.h>

#include <sys/ktr.h>

#include "drm_legacy.h"
#include "drm_internal.h"
#include "drm_crtc_internal.h"

static void drm_events_release(struct drm_file *file_priv);

#ifndef KTR_DRM
#define KTR_DRM KTR_ALL
#endif

KTR_INFO_MASTER(drm);
KTR_INFO(KTR_DRM, drm, file_close, 0,
    "file close stage=%u file=%p dev=%p open=%d fbs_empty=%u event_bytes=%d",
    u_int stage, void *file, void *dev, int open_count, u_int fbs_empty,
    int event_bytes);
KTR_INFO(KTR_DRM, drm, event_send, 1,
    "event send stage=%u file=%p event=%p len=%u event_bytes=%d pending_bytes=%ju",
    u_int stage, void *file, void *event, u_int length, int event_bytes,
    uintmax_t pending_bytes);
KTR_INFO(KTR_DRM, drm, kqfilter, 2,
    "kqfilter stage=%u file=%p kn=%p filter=%d event_bytes=%d event_space=%d",
    u_int stage, void *file, void *kn, int filter, int event_bytes, int event_space);

/* from BKL pushdown */
DEFINE_MUTEX(drm_global_mutex);

#ifdef __DragonFly__
SYSCTL_DECL(_hw_dri);
static uint64_t drm_event_send_count;
static uint64_t drm_event_send_bytes;
static uint64_t drm_event_read_count;
static uint64_t drm_event_read_bytes;
static uint64_t drm_event_pending_bytes;
static uint64_t drm_event_kqfilter_read_count;
static uint64_t drm_event_kqfilter_read_ready_count;
static uint64_t drm_event_kqfilter_write_count;

SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_send_count, CTLFLAG_RD,
    &drm_event_send_count, 0, "DRM event send count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_send_bytes, CTLFLAG_RD,
    &drm_event_send_bytes, 0, "DRM event send bytes");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_read_count, CTLFLAG_RD,
    &drm_event_read_count, 0, "DRM event read count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_read_bytes, CTLFLAG_RD,
    &drm_event_read_bytes, 0, "DRM event read bytes");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_pending_bytes, CTLFLAG_RD,
    &drm_event_pending_bytes, 0, "DRM event queued bytes");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_kqfilter_read_count, CTLFLAG_RD,
    &drm_event_kqfilter_read_count, 0, "DRM kqueue read filter count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_kqfilter_read_ready_count, CTLFLAG_RD,
    &drm_event_kqfilter_read_ready_count, 0, "DRM kqueue read ready count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, drm_event_kqfilter_write_count, CTLFLAG_RD,
    &drm_event_kqfilter_write_count, 0, "DRM kqueue write filter count");
#endif

/**
 * DOC: file operations
 *
 * Drivers must define the file operations structure that forms the DRM
 * userspace API entry point, even though most of those operations are
 * implemented in the DRM core. The resulting &struct file_operations must be
 * stored in the &drm_driver.fops field. The mandatory functions are drm_open(),
 * drm_read(), drm_ioctl() and drm_compat_ioctl() if CONFIG_COMPAT is enabled
 * Note that drm_compat_ioctl will be NULL if CONFIG_COMPAT=n, so there's no
 * need to sprinkle #ifdef into the code. Drivers which implement private ioctls
 * that require 32/64 bit compatibility support must provide their own
 * &file_operations.compat_ioctl handler that processes private ioctls and calls
 * drm_compat_ioctl() for core ioctls.
 *
 * In addition drm_read() and drm_poll() provide support for DRM events. DRM
 * events are a generic and extensible means to send asynchronous events to
 * userspace through the file descriptor. They are used to send vblank event and
 * page flip completions by the KMS API. But drivers can also use it for their
 * own needs, e.g. to signal completion of rendering.
 *
 * For the driver-side event interface see drm_event_reserve_init() and
 * drm_send_event() as the main starting points.
 *
 * The memory mapping implementation will vary depending on how the driver
 * manages memory. Legacy drivers will use the deprecated drm_legacy_mmap()
 * function, modern drivers should use one of the provided memory-manager
 * specific implementations. For GEM-based drivers this is drm_gem_mmap(), and
 * for drivers which use the CMA GEM helpers it's drm_gem_cma_mmap().
 *
 * No other file operations are supported by the DRM userspace API. Overall the
 * following is an example &file_operations structure::
 *
 *     static const example_drm_fops = {
 *             .owner = THIS_MODULE,
 *             .open = drm_open,
 *             .release = drm_release,
 *             .unlocked_ioctl = drm_ioctl,
 *             .compat_ioctl = drm_compat_ioctl, // NULL if CONFIG_COMPAT=n
 *             .poll = drm_poll,
 *             .read = drm_read,
 *             .llseek = no_llseek,
 *             .mmap = drm_gem_mmap,
 *     };
 *
 * For plain GEM based drivers there is the DEFINE_DRM_GEM_FOPS() macro, and for
 * CMA based drivers there is the DEFINE_DRM_GEM_CMA_FOPS() macro to make this
 * simpler.
 *
 * The driver's &file_operations must be stored in &drm_driver.fops.
 *
 * For driver-private IOCTL handling see the more detailed discussion in
 * :ref:`IOCTL support in the userland interfaces chapter<drm_driver_ioctl>`.
 */

static int drm_open_helper(struct cdev *kdev, int flags,
			   struct file *filp, struct drm_minor *minor);

/**
 * drm_file_alloc - allocate file context
 * @minor: minor to allocate on
 *
 * This allocates a new DRM file context. It is not linked into any context and
 * can be used by the caller freely. Note that the context keeps a pointer to
 * @minor, so it must be freed before @minor is.
 *
 * RETURNS:
 * Pointer to newly allocated context, ERR_PTR on failure.
 */
struct drm_file *drm_file_alloc(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct drm_file *file;
	int ret;

	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return ERR_PTR(-ENOMEM);

#ifdef __DragonFly__
	file->pid = curproc->p_pid;
#else
	file->pid = get_pid(task_pid(current));
#endif
	file->minor = minor;

	/* for compatibility root is always authenticated */
	file->authenticated = capable(CAP_SYS_ADMIN);
	file->lock_count = 0;

	INIT_LIST_HEAD(&file->lhead);
	INIT_LIST_HEAD(&file->fbs);
	lockinit(&file->fbs_lock, "dpfl", 0, LK_CANRECURSE);
	INIT_LIST_HEAD(&file->blobs);
	INIT_LIST_HEAD(&file->pending_event_list);
	INIT_LIST_HEAD(&file->event_list);
	init_waitqueue_head(&file->event_wait);
	file->event_space = 4096; /* set aside 4k for event buffer */

	lockinit(&file->event_read_lock, "dperl", 0, LK_CANRECURSE);
	spin_init(&file->master_lookup_lock, "drmflm");

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_open(dev, file);

	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_open(file);

	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_init_file_private(&file->prime);

	if (dev->driver->open) {
		ret = dev->driver->open(dev, file);
		if (ret < 0)
			goto out_prime_destroy;
	}

	return file;

out_prime_destroy:
	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_destroy_file_private(&file->prime);
	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_release(file);
	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_release(dev, file);
	spin_uninit(&file->master_lookup_lock);
	put_pid(file->pid);
	kfree(file);

	return ERR_PTR(ret);
}

static void drm_events_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_pending_event *e, *et;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	/* Unlink pending events */
	list_for_each_entry_safe(e, et, &file_priv->pending_event_list,
				 pending_link) {
		list_del(&e->pending_link);
		e->file_priv = NULL;
	}

	/* Remove unconsumed events */
	list_for_each_entry_safe(e, et, &file_priv->event_list, link) {
		list_del(&e->link);
#ifdef __DragonFly__
		file_priv->event_bytes -= e->event->length;
		drm_event_pending_bytes -= e->event->length;
#endif
		kfree(e);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

/**
 * drm_file_free - free file context
 * @file: context to free, or NULL
 *
 * This destroys and deallocates a DRM file context previously allocated via
 * drm_file_alloc(). The caller must make sure to unlink it from any contexts
 * before calling this.
 *
 * If NULL is passed, this is a no-op.
 *
 * RETURNS:
 * 0 on success, or error code on failure.
 */
void drm_file_free(struct drm_file *file)
{
	struct drm_device *dev;

	if (!file)
		return;

	dev = file->minor->dev;

	DRM_DEBUG("pid = %d, device = 0x%p, open_count = %d\n",
		  curproc->p_pid,
		  dev,
		  dev->open_count);

	if (drm_core_check_feature(dev, DRIVER_LEGACY) &&
	    dev->driver->preclose)
		dev->driver->preclose(dev, file);

	if (drm_core_check_feature(dev, DRIVER_LEGACY))
		drm_legacy_lock_release(dev, file->filp);

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		drm_legacy_reclaim_buffers(dev, file);

	drm_events_release(file);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		drm_fb_release(file);
		drm_property_destroy_user_blobs(dev, file);
	}

	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_release(file);

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_release(dev, file);

	drm_legacy_ctxbitmap_flush(dev, file);

	if (drm_is_primary_client(file))
		drm_master_release(file);

	if (dev->driver->postclose)
		dev->driver->postclose(dev, file);

	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_destroy_file_private(&file->prime);

	WARN_ON(!list_empty(&file->event_list));

	spin_uninit(&file->master_lookup_lock);
	put_pid(file->pid);
	kfree(file);
}

void drm_file_close_counted(struct drm_file *file)
{
	struct drm_device *dev;
	struct drm_minor *minor;

	if (!file)
		return;

	minor = file->minor;
	dev = minor->dev;

	mutex_lock(&drm_global_mutex);
	mutex_lock(&dev->filelist_mutex);
	if (!list_empty(&file->lhead))
		list_del_init(&file->lhead);
	mutex_unlock(&dev->filelist_mutex);

	drm_file_free(file);

	if (!--dev->open_count)
		drm_lastclose(dev);
	mutex_unlock(&drm_global_mutex);

	drm_minor_release(minor);
}

static int drm_setup(struct drm_device * dev)
{
	int ret;

	if (dev->driver->firstopen &&
	    drm_core_check_feature(dev, DRIVER_LEGACY)) {
		ret = dev->driver->firstopen(dev);
		if (ret != 0)
			return ret;
	}

	ret = drm_legacy_dma_setup(dev);
	if (ret < 0)
		return ret;


	DRM_DEBUG("\n");
	return 0;
}

/**
 * drm_open - open method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their &file_operations.open method.
 * It looks up the correct DRM device and instantiates all the per-file
 * resources for it. It also calls the &drm_driver.open driver callback.
 *
 * RETURNS:
 *
 * 0 on success or negative errno value on falure.
 */
// drm_open() is a file_operations function, not a dev_ops function
#if 0
int drm_open(struct inode *inode, struct file *filp)
#endif
int drm_open(struct dev_open_args *ap)
{
#ifdef __DragonFly__
	struct file *filp = ap->a_fpp ? *ap->a_fpp : NULL;
	struct inode *inode = filp->f_data;	/* A Linux inode is a Unix vnode */
	struct cdev *kdev = ap->a_head.a_dev;
	int flags = ap->a_oflags;
#endif
	struct drm_device *dev;
	struct drm_minor *minor;
	int retcode;
	int need_setup = 0;

	minor = drm_minor_acquire(iminor(inode));
	if (IS_ERR(minor))
		return PTR_ERR(minor);

	dev = minor->dev;
	mutex_lock(&drm_global_mutex);
	if (!dev->open_count++)
		need_setup = 1;

	/* share address_space across all char-devs of a single device */
#if 0
	filp->f_mapping = dev->anon_inode->i_mapping;
#endif

#ifdef __DragonFly__
	retcode = drm_open_helper(kdev, flags, filp, minor);
#else
	retcode = drm_open_helper(filp, minor);
#endif
	if (retcode)
		goto err_undo;
	if (need_setup) {
		retcode = drm_setup(dev);
		if (retcode)
			goto err_undo;
	}
#ifdef __DragonFly__
	device_busy(dev->dev->bsddev);
#endif
	mutex_unlock(&drm_global_mutex);
	return 0;

err_undo:
	dev->open_count--;
	mutex_unlock(&drm_global_mutex);
	drm_minor_release(minor);
	return retcode;
}
EXPORT_SYMBOL(drm_open);

/*
 * close() function
 */
int
drm_close(struct dev_close_args *ap)
{
#ifdef __DragonFly__
	struct file *filp = ap->a_fp;
#endif
	struct drm_file *file_priv = filp->private_data;
	/*
	 * Use the minor reference the file has held since drm_open();
	 * re-acquiring here leaked one drm_device reference per
	 * open/close cycle, so drm_dev_release() could never run.  The
	 * drm_minor_release() at the end returns the open's reference,
	 * matching Linux drm_release() semantics.
	 */
	struct drm_minor *minor = file_priv->minor;
	struct drm_device *dev = minor->dev;

	KTR_LOG(drm_file_close, 0u, file_priv, dev, dev->open_count,
	    list_empty(&file_priv->fbs) ? 1u : 0u, file_priv->event_bytes);
	mutex_lock(&drm_global_mutex);

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	mutex_lock(&dev->filelist_mutex);
	list_del(&file_priv->lhead);
	mutex_unlock(&dev->filelist_mutex);

	if (drm_core_check_feature(dev, DRIVER_LEGACY) &&
	    dev->driver->preclose)
		dev->driver->preclose(dev, file_priv);

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG("pid = %d, device = 0x%p, open_count = %d\n",
		  curproc->p_pid,
		  dev,
		  dev->open_count);

	if (drm_core_check_feature(dev, DRIVER_LEGACY))
		drm_legacy_lock_release(dev, filp);

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		drm_legacy_reclaim_buffers(dev, file_priv);

	drm_events_release(file_priv);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		drm_fb_release(file_priv);
		KTR_LOG(drm_file_close, 1u, file_priv, dev, dev->open_count,
		    list_empty(&file_priv->fbs) ? 1u : 0u, file_priv->event_bytes);
		drm_property_destroy_user_blobs(dev, file_priv);
	}

	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_release(file_priv);

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_release(dev, file_priv);

	drm_legacy_ctxbitmap_flush(dev, file_priv);

	if (drm_is_primary_client(file_priv))
		drm_master_release(file_priv);

	if (dev->driver->postclose)
		dev->driver->postclose(dev, file_priv);
	KTR_LOG(drm_file_close, 2u, file_priv, dev, dev->open_count,
	    list_empty(&file_priv->fbs) ? 1u : 0u, file_priv->event_bytes);

	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_destroy_file_private(&file_priv->prime);

	WARN_ON(!list_empty(&file_priv->event_list));
	KTR_LOG(drm_file_close, 3u, file_priv, dev, dev->open_count,
	    list_empty(&file_priv->fbs) ? 1u : 0u, file_priv->event_bytes);

	put_pid(file_priv->pid);
	kfree(file_priv);

	/* ========================================================
	 * End inline drm_release
	 */

#ifdef __DragonFly__
	/*
	 * Pair the device_busy() taken by drm_open().  Without this the
	 * bus-level busy count only ever grows, the device stays DS_BUSY
	 * forever after the first open, and device_detach() can never
	 * distinguish "has live users" from "ever had users".
	 */
	device_unbusy(dev->dev->bsddev);
#endif
	if (!--dev->open_count) {
		KTR_LOG(drm_file_close, 4u, file_priv, dev, dev->open_count, 1u, 0);
		drm_lastclose(dev);
#if 0	/* XXX: drm_put_dev() not implemented */
		if (drm_dev_is_unplugged(dev))
			drm_put_dev(dev);
#endif
	}
	mutex_unlock(&drm_global_mutex);

	drm_minor_release(minor);

	return 0;
}
EXPORT_SYMBOL(drm_close);

/*
 * Check whether DRI will run on this CPU.
 *
 * \return non-zero if the DRI will run on this CPU, or zero otherwise.
 */
static int drm_cpu_valid(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
	return 0;		/* No cmpxchg before v9 sparc. */
#endif
	return 1;
}

/*
 * Called whenever a process opens a drm node
 *
 * \param filp file pointer.
 * \param minor acquired minor-object.
 * \return zero on success or a negative number on failure.
 *
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
static int drm_open_helper(struct cdev *kdev, int flags,
		    struct file *filp, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct drm_file *priv;
	int ret;

	if (flags & O_EXCL)
		return -EBUSY;	/* No exclusive opens */
	if (!drm_cpu_valid())
		return -EINVAL;
#if 0
	if (dev->switch_power_state != DRM_SWITCH_POWER_ON && dev->switch_power_state != DRM_SWITCH_POWER_DYNAMIC_OFF)
		return -EINVAL;
#endif

	DRM_DEBUG("pid = %d, minor = %d\n", DRM_CURRENTPID, minor->index);

	priv = drm_file_alloc(minor);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

#ifdef __DragonFly__
	kdev->si_drv1 = dev;
#endif

	if (drm_is_primary_client(priv)) {
		ret = drm_master_open(priv);
		if (ret) {
			drm_file_free(priv);
			return ret;
		}
	}

	filp->private_data = priv;
#if 0
	filp->f_mode |= FMODE_UNSIGNED_OFFSET;
#endif
	priv->filp = filp;

	mutex_lock(&dev->filelist_mutex);
	list_add(&priv->lhead, &dev->filelist);
	mutex_unlock(&dev->filelist_mutex);

#ifdef __alpha__
	/*
	 * Default the hose
	 */
	if (!dev->hose) {
		struct pci_dev *pci_dev;
		pci_dev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
		if (pci_dev) {
			dev->hose = pci_dev->sysdata;
			pci_dev_put(pci_dev);
		}
		if (!dev->hose) {
			struct pci_bus *b = list_entry(pci_root_buses.next,
				struct pci_bus, node);
			if (b)
				dev->hose = b->sysdata;
		}
	}
#endif

	return 0;
}

static void drm_legacy_dev_reinit(struct drm_device *dev)
{
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	mutex_lock(&dev->struct_mutex);

	drm_legacy_agp_clear(dev);

	drm_legacy_sg_cleanup(dev);
	drm_legacy_vma_flush(dev);
	drm_legacy_dma_takedown(dev);

	mutex_unlock(&dev->struct_mutex);

	dev->sigdata.lock = NULL;

	dev->context_flag = 0;
	dev->last_context = 0;
	dev->if_version = 0;

	DRM_DEBUG("lastclose completed\n");
}

void drm_lastclose(struct drm_device * dev)
{
	DRM_DEBUG("\n");

	if (dev->driver->lastclose)
		dev->driver->lastclose(dev);
	DRM_DEBUG("driver lastclose completed\n");

	if (drm_core_check_feature(dev, DRIVER_LEGACY))
		drm_legacy_dev_reinit(dev);

#if 0 /* drm client implementation GPL'ed until 5.7 */
	drm_client_dev_restore(dev);
#endif
}

/**
 * drm_release - release method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their &file_operations.release
 * method. It frees any resources associated with the open file, and calls the
 * &drm_driver.postclose driver callback. If this is the last open file for the
 * DRM device also proceeds to call the &drm_driver.lastclose driver callback.
 *
 * RETURNS:
 *
 * Always succeeds and returns 0.
 */
int drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_minor *minor = file_priv->minor;
	struct drm_device *dev = minor->dev;

#ifdef __DragonFly__
	/* dev is not correctly set yet */
	return 0;
#endif

	mutex_lock(&drm_global_mutex);

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	mutex_lock(&dev->filelist_mutex);
	list_del(&file_priv->lhead);
	mutex_unlock(&dev->filelist_mutex);

	drm_file_free(file_priv);

	if (!--dev->open_count) {
		drm_lastclose(dev);
		if (drm_dev_is_unplugged(dev))
			drm_put_dev(dev);
	}
	mutex_unlock(&drm_global_mutex);

	drm_minor_release(minor);

	return 0;
}
EXPORT_SYMBOL(drm_release);

/**
 * drm_read - read method for DRM file
 * @filp: file pointer
 * @buffer: userspace destination pointer for the read
 * @count: count in bytes to read
 * @offset: offset to read
 *
 * This function must be used by drivers as their &file_operations.read
 * method iff they use DRM events for asynchronous signalling to userspace.
 * Since events are used by the KMS API for vblank and page flip completion this
 * means all modern display drivers must use it.
 *
 * @offset is ignored, DRM events are read like a pipe. Therefore drivers also
 * must set the &file_operation.llseek to no_llseek(). Polling support is
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
/*
ssize_t drm_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset)
*/
int drm_read(struct dev_read_args *ap)
{
	struct file *filp = ap->a_fp;
	struct uio *uio = ap->a_uio;
	size_t count = uio->uio_resid;
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	int ret = 0;	/* drm_read() returns int in DragonFly */

	ret = mutex_lock_interruptible(&file_priv->event_read_lock);
	if (ret)
		return ret;

	for (;;) {
		struct drm_pending_event *e = NULL;

		spin_lock_irq(&dev->event_lock);
		if (!list_empty(&file_priv->event_list)) {
			e = list_first_entry(&file_priv->event_list,
					struct drm_pending_event, link);
			file_priv->event_space += e->event->length;
#ifdef __DragonFly__
			file_priv->event_bytes -= e->event->length;
			drm_event_pending_bytes -= e->event->length;
#endif
			list_del(&e->link);
		}
		spin_unlock_irq(&dev->event_lock);

		if (e == NULL) {
			if (ret) {
				ret = 0;	/* DragonFly expects a zero return value on success */
				break;
			}

			if (filp->f_flag & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}

			mutex_unlock(&file_priv->event_read_lock);
			ret = wait_event_interruptible(file_priv->event_wait,
						       !list_empty(&file_priv->event_list));
			if (ret >= 0)
				ret = mutex_lock_interruptible(&file_priv->event_read_lock);
			if (ret)
				return ret;
		} else {
			unsigned length = e->event->length;

			if (length > count - ret) {
put_back_event:
				spin_lock_irq(&dev->event_lock);
				file_priv->event_space -= length;
#ifdef __DragonFly__
				file_priv->event_bytes += length;
				drm_event_pending_bytes += length;
#endif
				list_add(&e->link, &file_priv->event_list);
				spin_unlock_irq(&dev->event_lock);
				break;
			}

			if (uiomove((caddr_t)e->event, length, uio)) {
				if (ret == 0)
					ret = -EFAULT;
				goto put_back_event;
			}

			ret += length;
#ifdef __DragonFly__
			drm_event_read_count++;
			drm_event_read_bytes += length;
#endif
			kfree(e);
		}
	}
	mutex_unlock(&file_priv->event_read_lock);

	return ret;
}
EXPORT_SYMBOL(drm_read);

/**
 * drm_poll - poll method for DRM file
 * @filp: file pointer
 * @wait: poll waiter table
 *
 * This function must be used by drivers as their &file_operations.read method
 * iff they use DRM events for asynchronous signalling to userspace.  Since
 * events are used by the KMS API for vblank and page flip completion this means
 * all modern display drivers must use it.
 *
 * See also drm_read().
 *
 * RETURNS:
 *
 * Mask of POLL flags indicating the current status of the file.
 */
static int
drmfilt_read(struct knote *kn, long hint)
{
	struct drm_file *file_priv = (struct drm_file *)kn->kn_hook;

	(void)hint;

	/*
	 * Ownership: this filter borrows file_priv from kn_hook; the open DRM
	 * file owns it until the knote is detached by drm_kqfilter cleanup.
	 * Lifetime: event_bytes is updated while event_list entries are queued or
	 * removed; it never owns the events themselves.
	 * Threading: do not take event_lock here. drm_send_event_locked() calls
	 * KNOTE() while holding event_lock, so taking it here would self-deadlock
	 * on the first delivered DRM event.
	 */
	kn->kn_data = file_priv->event_bytes;
	drm_event_kqfilter_read_count++;
	if (file_priv->event_bytes != 0)
		drm_event_kqfilter_read_ready_count++;

	return (file_priv->event_bytes != 0);
}

static int
drmfilt_write(struct knote *kn, long hint)
{
	struct drm_file *file_priv = (struct drm_file *)kn->kn_hook;

	(void)hint;

	/*
	 * Ownership: this filter only observes file_priv; userspace cannot write
	 * DRM events through this fd, so there is no per-writer buffer to own.
	 * Lifetime: file_priv outlives the knote registration as above.
	 * Threading: Linux drm_poll() always reports EPOLLOUT/EPOLLWRNORM for DRM
	 * fds. Mirror that contract for DragonFly poll/kqueue users instead of
	 * tying write readiness to the asynchronous event queue.
	 */
	kn->kn_data = file_priv->event_space;
	drm_event_kqfilter_write_count++;
	return (1);
}

static void
drmfilt_detach(struct knote *kn)
{
	struct drm_file *file_priv;
 
	file_priv = (struct drm_file *)kn->kn_hook;
	KTR_LOG(drm_kqfilter, 1u, file_priv, kn, kn->kn_filter,
	    file_priv->event_bytes, file_priv->event_space);

	knote_remove(&file_priv->dkq.ki_note, kn);
}

static struct filterops drmread_filtops =
	{ FILTEROP_MPSAFE | FILTEROP_ISFD, NULL, drmfilt_detach, drmfilt_read };

static struct filterops drmwrite_filtops =
	{ FILTEROP_MPSAFE | FILTEROP_ISFD, NULL, drmfilt_detach, drmfilt_write };

int
drm_kqfilter(struct dev_kqfilter_args *ap)
{
	struct file *filp = ap->a_fp;
	struct drm_file *file_priv = filp->private_data;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &drmread_filtops;
		kn->kn_hook = (caddr_t)file_priv;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &drmwrite_filtops;
		kn->kn_hook = (caddr_t)file_priv;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	KTR_LOG(drm_kqfilter, 0u, file_priv, kn, kn->kn_filter,
	    file_priv->event_bytes, file_priv->event_space);
	klist = &file_priv->dkq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

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
 * already hold &drm_device.event_lock.
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
	list_add(&p->pending_link, &file_priv->pending_event_list);
	p->file_priv = file_priv;

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
 * Callers which already hold &drm_device.event_lock should use
 * drm_event_reserve_init_locked() instead.
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
 * and releases any allocated space. It is used to cancel an event when the
 * nonblocking operation could not be submitted and needed to be aborted.
 */
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p)
{
	unsigned long flags;
	spin_lock_irqsave(&dev->event_lock, flags);
	if (p->file_priv) {
		p->file_priv->event_space += p->event->length;
		list_del(&p->pending_link);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (p->fence)
		dma_fence_put(p->fence);

	kfree(p);
}
EXPORT_SYMBOL(drm_event_cancel_free);

/**
 * drm_send_event_locked - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. Callers must already hold
 * &drm_device.event_lock, see drm_send_event() for the unlocked version.
 *
 * Note that the core will take care of unlinking and disarming events when the
 * corresponding DRM file is closed. Drivers need not worry about whether the
 * DRM file for this event still exists and can call this function upon
 * completion of the asynchronous work unconditionally.
 */
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e)
{
	unsigned int event_length __unused = e->event != NULL ? e->event->length : 0;

	assert_spin_locked(&dev->event_lock);

	if (e->completion) {
		complete_all(e->completion);
		e->completion_release(e->completion);
		e->completion = NULL;
	}

	if (e->fence) {
		dma_fence_signal(e->fence);
		dma_fence_put(e->fence);
	}

	if (!e->file_priv) {
		KTR_LOG(drm_event_send, 0u, NULL, e, event_length, 0,
		    (uintmax_t)drm_event_pending_bytes);
		kfree(e);
		return;
	}

	KTR_LOG(drm_event_send, 1u, e->file_priv, e, event_length,
	    e->file_priv->event_bytes, (uintmax_t)drm_event_pending_bytes);
	list_del(&e->pending_link);
	list_add_tail(&e->link,
		      &e->file_priv->event_list);
#ifdef __DragonFly__
	e->file_priv->event_bytes += e->event->length;
	drm_event_send_count++;
	drm_event_send_bytes += e->event->length;
	drm_event_pending_bytes += e->event->length;
	KTR_LOG(drm_event_send, 2u, e->file_priv, e, event_length,
	    e->file_priv->event_bytes, (uintmax_t)drm_event_pending_bytes);
#endif
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
 * to its associated userspace DRM file. This function acquires
 * &drm_device.event_lock, see drm_send_event_locked() for callers which already
 * hold this lock.
 *
 * Note that the core will take care of unlinking and disarming events when the
 * corresponding DRM file is closed. Drivers need not worry about whether the
 * DRM file for this event still exists and can call this function upon
 * completion of the asynchronous work unconditionally.
 */
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);
	drm_send_event_locked(dev, e);
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}
EXPORT_SYMBOL(drm_send_event);
