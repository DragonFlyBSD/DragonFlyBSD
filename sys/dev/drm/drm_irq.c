/*-
 * Copyright 2003 Eric Anholt
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
 * ERIC ANHOLT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <anholt@FreeBSD.org>
 *
 * $DragonFly: src/sys/dev/drm/drm_irq.c,v 1.1 2008/04/05 18:12:29 hasso Exp $
 */

/** @file drm_irq.c
 * Support code for handling setup/teardown of interrupt handlers and
 * handing interrupt handlers off to the drivers.
 */

#include "drmP.h"
#include "drm.h"

static void drm_locked_task(void *context, int pending __unused);

int drm_irq_by_busid(drm_device_t *dev, void *data, struct drm_file *file_priv)
{
	drm_irq_busid_t *irq = data;

	if ((irq->busnum >> 8) != dev->pci_domain ||
	    (irq->busnum & 0xff) != dev->pci_bus ||
	    irq->devnum != dev->pci_slot ||
	    irq->funcnum != dev->pci_func)
		return EINVAL;

	irq->irq = dev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n",
		  irq->busnum, irq->devnum, irq->funcnum, irq->irq);

	return 0;
}

#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
static irqreturn_t
drm_irq_handler_wrap(DRM_IRQ_ARGS)
{
	drm_device_t *dev = (drm_device_t *)arg;

	DRM_SPINLOCK(&dev->irq_lock);
	dev->driver.irq_handler(arg);
	DRM_SPINUNLOCK(&dev->irq_lock);
}
#endif

int drm_irq_install(drm_device_t *dev)
{
	int retcode;
#ifdef __NetBSD__
	pci_intr_handle_t ih;
#endif

	if (dev->irq == 0 || dev->dev_private == NULL)
		return EINVAL;

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, dev->irq );

	DRM_LOCK();
	if (dev->irq_enabled) {
		DRM_UNLOCK();
		return EBUSY;
	}
	dev->irq_enabled = 1;

	dev->context_flag = 0;

#ifndef __DragonFly__
	DRM_SPININIT(&dev->irq_lock, "DRM IRQ lock");
#else
	lwkt_serialize_init(&dev->irq_lock);
#endif

				/* Before installing handler */
	dev->driver.irq_preinstall(dev);
	DRM_UNLOCK();

				/* Install handler */
#if defined(__FreeBSD__) || defined(__DragonFly__)
	dev->irqrid = 0;
	dev->irqr = bus_alloc_resource_any(dev->device, SYS_RES_IRQ, 
				      &dev->irqrid, RF_SHAREABLE);
	if (!dev->irqr) {
		retcode = ENOENT;
		goto err;
	}
#if __FreeBSD_version >= 700031
	retcode = bus_setup_intr(dev->device, dev->irqr,
				 INTR_TYPE_TTY | INTR_MPSAFE,
				 NULL, drm_irq_handler_wrap, dev, &dev->irqh);
#elif defined(__DragonFly__)
	retcode = bus_setup_intr(dev->device, dev->irqr,
				 INTR_MPSAFE,
				 dev->driver.irq_handler, dev, &dev->irqh,
				 &dev->irq_lock);
#else
	retcode = bus_setup_intr(dev->device, dev->irqr,
				 INTR_TYPE_TTY | INTR_MPSAFE,
				 drm_irq_handler_wrap, dev, &dev->irqh);
#endif
	if (retcode != 0)
		goto err;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	if (pci_intr_map(&dev->pa, &ih) != 0) {
		retcode = ENOENT;
		goto err;
	}
	dev->irqh = pci_intr_establish(&dev->pa.pa_pc, ih, IPL_TTY,
	    (irqreturn_t (*)(void *))dev->irq_handler, dev);
	if (!dev->irqh) {
		retcode = ENOENT;
		goto err;
	}
#endif

				/* After installing handler */
	DRM_LOCK();
	dev->driver.irq_postinstall(dev);
	DRM_UNLOCK();

	TASK_INIT(&dev->locked_task, 0, drm_locked_task, dev);
	return 0;
err:
	DRM_LOCK();
	dev->irq_enabled = 0;
#if defined(___FreeBSD__) || defined(__DragonFly__)
	if (dev->irqrid != 0) {
		bus_release_resource(dev->device, SYS_RES_IRQ, dev->irqrid,
		    dev->irqr);
		dev->irqrid = 0;
	}
#endif
#ifndef __DragonFly__
	DRM_SPINUNINIT(&dev->irq_lock);
#endif
	DRM_UNLOCK();
	return retcode;
}

int drm_irq_uninstall(drm_device_t *dev)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	int irqrid;
#endif

	if (!dev->irq_enabled)
		return EINVAL;

	dev->irq_enabled = 0;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	irqrid = dev->irqrid;
	dev->irqrid = 0;
#endif

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, dev->irq );

	dev->driver.irq_uninstall(dev);

#if defined(__FreeBSD__) || defined(__DragonFly__)
	DRM_UNLOCK();
	bus_teardown_intr(dev->device, dev->irqr, dev->irqh);
	bus_release_resource(dev->device, SYS_RES_IRQ, irqrid, dev->irqr);
	DRM_LOCK();
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	pci_intr_disestablish(&dev->pa.pa_pc, dev->irqh);
#endif
#ifndef __DragonFly__
	DRM_SPINUNINIT(&dev->irq_lock);
#endif

	return 0;
}

int drm_control(drm_device_t *dev, void *data, struct drm_file *file_priv)
{
	drm_control_t *ctl = data;
	int err;

	switch ( ctl->func ) {
	case DRM_INST_HANDLER:
		/* Handle drivers whose DRM used to require IRQ setup but the
		 * no longer does.
		 */
		if (!dev->driver.use_irq)
			return 0;
		if (dev->if_version < DRM_IF_VERSION(1, 2) &&
		    ctl->irq != dev->irq)
			return EINVAL;
		return drm_irq_install(dev);
	case DRM_UNINST_HANDLER:
		if (!dev->driver.use_irq)
			return 0;
		DRM_LOCK();
		err = drm_irq_uninstall(dev);
		DRM_UNLOCK();
		return err;
	default:
		return EINVAL;
	}
}

int drm_wait_vblank(drm_device_t *dev, void *data, struct drm_file *file_priv)
{
	drm_wait_vblank_t *vblwait = data;
	struct timeval now;
	int ret = 0;
	int flags, seq;

	if (!dev->irq_enabled)
		return EINVAL;

	if (vblwait->request.type &
	    ~(_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK)) {
		DRM_ERROR("Unsupported type value 0x%x, supported mask 0x%x\n",
		    vblwait->request.type,
		    (_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK));
		return EINVAL;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;

	if ((flags & _DRM_VBLANK_SECONDARY) && !dev->driver.use_vbl_irq2)
		return EINVAL;
	
	seq = atomic_read((flags & _DRM_VBLANK_SECONDARY) ?
	    &dev->vbl_received2 : &dev->vbl_received);

	switch (vblwait->request.type & _DRM_VBLANK_TYPES_MASK) {
	case _DRM_VBLANK_RELATIVE:
		vblwait->request.sequence += seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
	case _DRM_VBLANK_ABSOLUTE:
		break;
	default:
		return EINVAL;
	}

	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    (seq - vblwait->request.sequence) <= (1<<23)) {
		vblwait->request.sequence = seq + 1;
	}

	if (flags & _DRM_VBLANK_SIGNAL) {
#if 0 /* disabled */
		drm_vbl_sig_t *vbl_sig = malloc(sizeof(drm_vbl_sig_t), M_DRM,
		    M_NOWAIT | M_ZERO);
		if (vbl_sig == NULL)
			return ENOMEM;

		vbl_sig->sequence = vblwait->request.sequence;
		vbl_sig->signo = vblwait->request.signal;
		vbl_sig->pid = DRM_CURRENTPID;

		vblwait->reply.sequence = atomic_read(&dev->vbl_received);
		
		DRM_SPINLOCK(&dev->irq_lock);
		TAILQ_INSERT_HEAD(&dev->vbl_sig_list, vbl_sig, link);
		DRM_SPINUNLOCK(&dev->irq_lock);
		ret = 0;
#endif
		ret = EINVAL;
	} else {
		DRM_LOCK();
		/* shared code returns -errno */
		if (flags & _DRM_VBLANK_SECONDARY) {
			if (dev->driver.vblank_wait2)
				ret = -dev->driver.vblank_wait2(dev,
				    &vblwait->request.sequence);
		} else if (dev->driver.vblank_wait)
			ret = -dev->driver.vblank_wait(dev,
			    &vblwait->request.sequence);

		DRM_UNLOCK();

		microtime(&now);
		vblwait->reply.tval_sec = now.tv_sec;
		vblwait->reply.tval_usec = now.tv_usec;
	}

	return ret;
}

void drm_vbl_send_signals(drm_device_t *dev)
{
}

#if 0 /* disabled */
void drm_vbl_send_signals( drm_device_t *dev )
{
	drm_vbl_sig_t *vbl_sig;
	unsigned int vbl_seq = atomic_read( &dev->vbl_received );
	struct proc *p;

	vbl_sig = TAILQ_FIRST(&dev->vbl_sig_list);
	while (vbl_sig != NULL) {
		drm_vbl_sig_t *next = TAILQ_NEXT(vbl_sig, link);

		if ( ( vbl_seq - vbl_sig->sequence ) <= (1<<23) ) {
			p = pfind(vbl_sig->pid);
			if (p != NULL)
				psignal(p, vbl_sig->signo);

			TAILQ_REMOVE(&dev->vbl_sig_list, vbl_sig, link);
			DRM_FREE(vbl_sig,sizeof(*vbl_sig));
		}
		vbl_sig = next;
	}
}
#endif

static void drm_locked_task(void *context, int pending __unused)
{
	drm_device_t *dev = context;

	DRM_LOCK();
	for (;;) {
		int ret;

		if (drm_lock_take(&dev->lock.hw_lock->lock,
		    DRM_KERNEL_CONTEXT))
		{
			dev->lock.file_priv = NULL; /* kernel owned */
			dev->lock.lock_time = jiffies;
			atomic_inc(&dev->counts[_DRM_STAT_LOCKS]);
			break;  /* Got lock */
		}

		/* Contention */
#if defined(__FreeBSD__) && __FreeBSD_version > 500000
		ret = mtx_sleep((void *)&dev->lock.lock_queue, &dev->dev_lock,
		    PZERO | PCATCH, "drmlk2", 0);
#elif defined(__DragonFly__)
		crit_enter();
		tsleep_interlock((void *)&dev->lock.lock_queue);
		DRM_UNLOCK();
		ret = tsleep((void *)&dev->lock.lock_queue, PCATCH,
		    "drmlk2", 0);
		crit_exit();
		DRM_LOCK();
#else
		ret = tsleep((void *)&dev->lock.lock_queue, PZERO | PCATCH,
		    "drmlk2", 0);
#endif
		if (ret != 0)
			return;
	}
	DRM_UNLOCK();

	dev->locked_task_call(dev);

	drm_lock_free(dev, &dev->lock.hw_lock->lock, DRM_KERNEL_CONTEXT);
}

void
drm_locked_tasklet(drm_device_t *dev, void (*tasklet)(drm_device_t *dev))
{
	dev->locked_task_call = tasklet;
	taskqueue_enqueue(taskqueue_swi, &dev->locked_task);
}
