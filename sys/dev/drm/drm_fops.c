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
 *
 * $FreeBSD: src/sys/dev/drm2/drm_fops.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/devfs.h>

#include <drm/drmP.h>

extern drm_pci_id_list_t *drm_find_description(int vendor, int device,
    drm_pci_id_list_t *idlist);
extern devclass_t drm_devclass;

static int drm_setup(struct drm_device *dev)
{
	drm_local_map_t *map;
	int i;

	DRM_LOCK_ASSERT(dev);

	/* prebuild the SAREA */
	i = drm_addmap(dev, 0, SAREA_MAX, _DRM_SHM,
	    _DRM_CONTAINS_LOCK, &map);
	if (i != 0)
		return i;

	if (dev->driver->firstopen)
		dev->driver->firstopen(dev);

	dev->buf_use = 0;

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA)) {
		i = drm_dma_setup(dev);
		if (i != 0)
			return i;
	}

	for (i = 0; i < DRM_HASH_SIZE; i++) {
		dev->magiclist[i].head = NULL;
		dev->magiclist[i].tail = NULL;
	}

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
	priv->ioctl_count 	= 0;

	/* for compatibility root is always authenticated */
	priv->authenticated	= DRM_SUSER(p);

	INIT_LIST_HEAD(&priv->fbs);
	INIT_LIST_HEAD(&priv->event_list);
	init_waitqueue_head(&priv->event_wait);
	priv->event_space = 4096; /* set aside 4k for event buffer */

	if (dev->driver->driver_features & DRIVER_GEM)
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

static bool
drm_dequeue_event(struct drm_device *dev, struct drm_file *file_priv,
    struct uio *uio, struct drm_pending_event **out)
{
	struct drm_pending_event *e;

	if (list_empty(&file_priv->event_list))
		return (false);
	e = list_first_entry(&file_priv->event_list,
	    struct drm_pending_event, link);
	if (e->event->length > uio->uio_resid)
		return (false);

	file_priv->event_space += e->event->length;
	list_del(&e->link);
	*out = e;
	return (true);
}

int
drm_read(struct dev_read_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct drm_file *file_priv;
	struct drm_device *dev;
	struct drm_pending_event *e;
	int error;

	error = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}
	dev = drm_get_device_from_kdev(kdev);
	lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	while (list_empty(&file_priv->event_list)) {
		if ((ioflag & O_NONBLOCK) != 0) {
			error = EAGAIN;
			goto out;
		}
		error = lksleep(&file_priv->event_space, &dev->event_lock,
	           PCATCH, "drmrea", 0);
	       if (error != 0)
		       goto out;
	}
	while (drm_dequeue_event(dev, file_priv, uio, &e)) {
		lockmgr(&dev->event_lock, LK_RELEASE);
		error = uiomove((caddr_t)e->event, e->event->length, uio);
		e->destroy(e);
		if (error != 0)
			return (error);
		lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	}
out:
	lockmgr(&dev->event_lock, LK_RELEASE);
	return (error);
}

void
drm_event_wakeup(struct drm_pending_event *e)
{
	struct drm_file *file_priv;
	struct drm_device *dev;

	file_priv = e->file_priv;
	dev = file_priv->dev;
	KKASSERT(lockstatus(&dev->event_lock, curthread) != 0);

	wakeup(&file_priv->event_space);
	KNOTE(&file_priv->dkq.ki_note, 0);
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

	lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	klist = &file_priv->dkq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&dev->event_lock, LK_RELEASE);
}

static struct filterops drmfiltops =
        { FILTEROP_ISFD, NULL, drmfilt_detach, drmfilt };

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

	lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	klist = &file_priv->dkq.ki_note;
	knote_insert(klist, kn);
	lockmgr(&dev->event_lock, LK_RELEASE);

	return (0);
}
