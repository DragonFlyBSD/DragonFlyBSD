/*-
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
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Daryll Strauss <daryll@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 * $FreeBSD: src/sys/dev/drm2/drm_fops.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

/** @file drm_fops.c
 * Support code for dealing with the file privates associated with each
 * open of the DRM device.
 */

#include <dev/drm2/drmP.h>
#include <sys/types.h>
#include <sys/conf.h>

struct drm_file *drm_find_file_by_proc(struct drm_device *dev, DRM_STRUCTPROC *p)
{
	uid_t uid = p->td_proc->p_ucred->cr_svuid;
	pid_t pid = p->td_proc->p_pid;
	struct drm_file *priv;

	TAILQ_FOREACH(priv, &dev->files, link)
		if (priv->pid == pid && priv->uid == uid)
			return priv;
	return NULL;
}

/* drm_open_helper is called whenever a process opens /dev/drm. */
int drm_open_helper(struct cdev *kdev, int flags, int fmt, DRM_STRUCTPROC *p,
		    struct drm_device *dev)
{
	struct drm_file *priv;
	int retcode;

	if (flags & O_EXCL)
		return EBUSY; /* No exclusive opens */
	dev->flags = flags;

	DRM_DEBUG("pid = %d, device = %s\n", DRM_CURRENTPID, devtoname(kdev));

	priv = kmalloc(sizeof(*priv), DRM_MEM_FILES, M_NOWAIT | M_ZERO);
	if (priv == NULL) {
		return ENOMEM;
	}

	retcode = devfs_set_cdevpriv(priv, drm_close);
	if (retcode != 0) {
		kfree(priv, DRM_MEM_FILES);
		return retcode;
	}

	DRM_LOCK(dev);
	priv->dev		= dev;
	priv->uid		= p->td_ucred->cr_svuid;
	priv->pid		= p->td_proc->p_pid;
	priv->ioctl_count 	= 0;

	/* for compatibility root is always authenticated */
	priv->authenticated	= DRM_SUSER(p);

	INIT_LIST_HEAD(&priv->fbs);
	INIT_LIST_HEAD(&priv->event_list);
	priv->event_space = 4096; /* set aside 4k for event buffer */

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_open(dev, priv);

	if (dev->driver->open) {
		/* shared code returns -errno */
		retcode = -dev->driver->open(dev, priv);
		if (retcode != 0) {
			devfs_clear_cdevpriv();
			kfree(priv, DRM_MEM_FILES);
			DRM_UNLOCK(dev);
			return retcode;
		}
	}

	/* first opener automatically becomes master */
	priv->master = TAILQ_EMPTY(&dev->files);

	TAILQ_INSERT_TAIL(&dev->files, priv, link);
	DRM_UNLOCK(dev);
	kdev->si_drv1 = dev;
	return 0;
}

#if 0
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
#endif

int
/* drm_read(struct cdev *kdev, struct uio *uio, int ioflag) */
drm_read(struct dev_read_args *ap)
{
	kprintf("drm_read(): not implemented\n");
#if 0
	struct cdev *kdev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct drm_file *file_priv;
	struct drm_device *dev;
	struct drm_pending_event *e;
	int error;

	error = devfs_get_cdevpriv((void **)&file_priv);
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
		error = uiomove(e->event, e->event->length, uio);
		e->destroy(e);
		if (error != 0)
			return (error);
		lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	}
out:
	lockmgr(&dev->event_lock, LK_RELEASE);
	return (error);
#endif
	return 0;
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
//	selwakeup(&file_priv->event_poll);	/* XXX */
}

static int
drmfilt(struct knote *kn, long hint)
{
	return (0);
}

static void
drmfilt_detach(struct knote *kn) {}

static struct filterops drmfiltops =
        { FILTEROP_ISFD, NULL, drmfilt_detach, drmfilt };

int
drm_kqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
	case EVFILT_WRITE:
		kn->kn_fop = &drmfiltops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	return (0);
}

#if 0	/* XXX: poll kqueue */
int
drm_poll(struct cdev *kdev, int events, struct thread *td)
	struct drm_file *file_priv;
	struct drm_device *dev;
	int error, revents;

	/* FIXME */
/*	error = devfs_get_cdevpriv((void **)&file_priv);*/
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}
	dev = drm_get_device_from_kdev(kdev);

	revents = 0;
	lockmgr(&dev->event_lock, LK_EXCLUSIVE);
	if ((events & (POLLIN | POLLRDNORM)) != 0) {
		if (list_empty(&file_priv->event_list)) {
			selrecord(td, &file_priv->event_poll);
		} else {
			revents |= events & (POLLIN | POLLRDNORM);
		}
	}
	lockmgr(&dev->event_lock, LK_RELEASE);
	return (revents);
}
#endif
