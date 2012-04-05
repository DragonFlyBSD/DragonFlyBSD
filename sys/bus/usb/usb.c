/*
 * $NetBSD: usb.c,v 1.68 2002/02/20 20:30:12 christos Exp $
 * $FreeBSD: src/sys/dev/usb/usb.c,v 1.106 2005/03/27 15:31:23 iedowse Exp $
 */

/* Also already merged from NetBSD:
 *	$NetBSD: usb.c,v 1.70 2002/05/09 21:54:32 augustss Exp $
 *	$NetBSD: usb.c,v 1.71 2002/06/01 23:51:04 lukem Exp $
 *	$NetBSD: usb.c,v 1.73 2002/09/23 05:51:19 simonb Exp $
 *	$NetBSD: usb.c,v 1.80 2003/11/07 17:03:25 wiz Exp $
 */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB specifications and other documentation can be found at
 * http://www.usb.org/developers/docs/ and
 * http://www.usb.org/developers/devclass_docs/
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/unistd.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/uio.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/event.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#define USBUNIT(d)	(minor(d))	/* usb_discover device nodes, kthread */
#define USB_DEV_MINOR	255		/* event queue device */

MALLOC_DEFINE(M_USB, "USB", "USB");
MALLOC_DEFINE(M_USBDEV, "USBdev", "USB device");
MALLOC_DEFINE(M_USBHC, "USBHC", "USB host controller");

#include "usb_if.h"

#include <bus/usb/usbdivar.h>
#include <bus/usb/usb_quirks.h>

/* Define this unconditionally in case a kernel module is loaded that
 * has been compiled with debugging options.
 */
SYSCTL_NODE(_hw, OID_AUTO, usb, CTLFLAG_RW, 0, "USB debugging");

/*
 * XXX: This is a hack! If your USB keyboard doesn't work
 * early at boot, try setting this tunable to 0 from
 * bootleader:     
 *
 *      set hw.usb.hack_defer_exploration=0
 */ 
static int	hack_defer_exploration = 1;

#ifdef USB_DEBUG
#define DPRINTF(x)	if (usbdebug) kprintf x
#define DPRINTFN(n,x)	if (usbdebug>(n)) kprintf x
int	usbdebug = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, debug, CTLFLAG_RW,
	   &usbdebug, 0, "usb debug level");

/*
 * 0  - do usual exploration
 * 1  - do not use timeout exploration
 * >1 - do no exploration
 */
int	usb_noexplore = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct usb_softc {
	cdev_t		sc_usbdev;
	TAILQ_ENTRY(usb_softc) sc_coldexplist; /* cold needs-explore list */
	usbd_bus_handle sc_bus;		/* USB controller */
	struct usbd_port sc_port;	/* dummy port for root hub */

	struct thread	*sc_event_thread;

	char		sc_dying;
};

struct usb_taskq {
	TAILQ_HEAD(, usb_task)	tasks;
	struct thread		*task_thread_proc;
	const char		*name;
	int			taskcreated;	/* task thread exists. */
};
static struct usb_taskq usb_taskq[USB_NUM_TASKQS];

d_open_t  usbopen;
d_close_t usbclose;
d_read_t usbread;
d_ioctl_t usbioctl;
d_kqfilter_t usbkqfilter;

static void usbfilt_detach(struct knote *);
static int usbfilt(struct knote *, long);

static struct dev_ops usb_ops = {
	{ "usb", 0, 0 },
	.d_open =	usbopen,
	.d_close =	usbclose,
	.d_read =	usbread,
	.d_ioctl =	usbioctl,
	.d_kqfilter = 	usbkqfilter
};

static void	usb_discover(device_t);
static bus_child_detached_t usb_child_detached;
static void	usb_create_event_thread(device_t);
static void	usb_event_thread(void *);
static void	usb_task_thread(void *);

static cdev_t usb_dev;		/* The /dev/usb device. */
static int usb_ndevs;		/* Number of /dev/usbN devices. */
/* Busses to explore at the end of boot-time device configuration */
static TAILQ_HEAD(, usb_softc) usb_coldexplist =
    TAILQ_HEAD_INITIALIZER(usb_coldexplist);
 
#define USB_MAX_EVENTS 100
struct usb_event_q {
	struct usb_event ue;
	TAILQ_ENTRY(usb_event_q) next;
};
static TAILQ_HEAD(, usb_event_q) usb_events =
	TAILQ_HEAD_INITIALIZER(usb_events);
static int usb_nevents = 0;
static struct kqinfo usb_kqevent;
static struct proc *usb_async_proc;  /* process that wants USB SIGIO */
static int usb_dev_open = 0;
static void usb_add_event(int, struct usb_event *);

static int usb_get_next_event(struct usb_event *);

static const char *usbrev_str[] = USBREV_STR;

static device_probe_t usb_match;
static device_attach_t usb_attach;
static device_detach_t usb_detach;

static devclass_t usb_devclass;

static kobj_method_t usb_methods[] = {
	DEVMETHOD(device_probe, usb_match),
	DEVMETHOD(device_attach, usb_attach),
	DEVMETHOD(device_detach, usb_detach),
	DEVMETHOD(bus_child_detached, usb_child_detached),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	{0,0}
};

static driver_t usb_driver = {
	"usb",
	usb_methods,
	sizeof(struct usb_softc)
};

MODULE_DEPEND(usb, usb, 1, 1, 1);
MODULE_VERSION(usb, 1);

static int
usb_match(device_t self)
{
	DPRINTF(("usb_match\n"));
	return (UMATCH_GENERIC);
}

static int
usb_attach(device_t self)
{
	struct usb_softc *sc = device_get_softc(self);
	void *aux = device_get_ivars(self);
	cdev_t tmp_dev;
	usbd_device_handle dev;
	usbd_status err;
	int usbrev;
	int speed;
	struct usb_event ue;

	TUNABLE_INT_FETCH("hw.usb.hack_defer_exploration",
	    &hack_defer_exploration);

	DPRINTF(("usb_attach\n"));

	usbd_init();
	sc->sc_bus = aux;
	sc->sc_bus->usbctl = sc;
	sc->sc_port.power = USB_MAX_POWER;

	usbrev = sc->sc_bus->usbrev;
	device_printf(self, "USB revision %s", usbrev_str[usbrev]);
	switch (usbrev) {
	case USBREV_1_0:
	case USBREV_1_1:
		speed = USB_SPEED_FULL;
		break;
	case USBREV_2_0:
		speed = USB_SPEED_HIGH;
		break;
	default:
		kprintf(", not supported\n");
		sc->sc_dying = 1;
		return ENXIO;
	}
	kprintf("\n");

	/* Make sure not to use tsleep() if we are cold booting. */
	if (hack_defer_exploration && cold)
		sc->sc_bus->use_polling++;

	ue.u.ue_ctrlr.ue_bus = device_get_unit(self);
	usb_add_event(USB_EVENT_CTRLR_ATTACH, &ue);

#ifdef USB_USE_SOFTINTR
	callout_init(&sc->sc_bus->softi);
#endif

	err = usbd_new_device(self, sc->sc_bus, 0, speed, 0, &sc->sc_port);
	if (!err) {
		dev = sc->sc_port.device;
		if (dev->hub == NULL) {
			sc->sc_dying = 1;
			device_printf(self,
			    "root device is not a hub\n");
			return ENXIO;
		}
		sc->sc_bus->root_hub = dev;
#if 1
		/*
		 * Turning this code off will delay attachment of USB devices
		 * until the USB event thread is running, which means that
		 * the keyboard will not work until after cold boot.
		 */
		if (cold) {
			if (hack_defer_exploration) {
				/* Explore high-speed busses before others. */
				if (speed == USB_SPEED_HIGH)
					dev->hub->explore(sc->sc_bus->root_hub);
				else
					TAILQ_INSERT_TAIL(&usb_coldexplist, sc,
					    sc_coldexplist);
			} else {
				/*
				 * XXX Exploring high speed devices here will
				 * hang the system.
				 */
				if (speed != USB_SPEED_HIGH)
					dev->hub->explore(sc->sc_bus->root_hub);
			}
		}
#endif
	} else {
		device_printf(self,
		    "root hub problem, error=%d\n", err);
		sc->sc_dying = 1;
	}
	if (hack_defer_exploration && cold)
		sc->sc_bus->use_polling--;

	usb_create_event_thread(self);

	/*
	 * The per controller devices (used for usb_discover)
	 * XXX This is redundant now, but old usbd's will want it
	 */
	tmp_dev = make_dev(&usb_ops, device_get_unit(self),
			   UID_ROOT, GID_OPERATOR, 0660,
			   "usb%d", device_get_unit(self));
	sc->sc_usbdev = reference_dev(tmp_dev);
	if (usb_ndevs++ == 0) {
		/* The device spitting out events */
		tmp_dev = make_dev(&usb_ops, USB_DEV_MINOR,
				   UID_ROOT, GID_OPERATOR, 0660, "usb");
		usb_dev = reference_dev(tmp_dev);
	}

	return 0;
}

static const char *taskq_names[] = USB_TASKQ_NAMES;

void
usb_create_event_thread(device_t self)
{
	struct usb_softc *sc = device_get_softc(self);
	int i;

	if (kthread_create(usb_event_thread, self, &sc->sc_event_thread,
			   "%s", device_get_nameunit(self))) {
		device_printf(self,
		    "unable to create event thread for\n");
		panic("usb_create_event_thread");
	}

	for (i = 0; i < USB_NUM_TASKQS; i++) {
		struct usb_taskq *taskq = &usb_taskq[i];

		if (taskq->taskcreated == 0) {
			taskq->taskcreated = 1;
			taskq->name = taskq_names[i];
			TAILQ_INIT(&taskq->tasks);
			if (kthread_create(usb_task_thread, taskq,
			    &taskq->task_thread_proc, "%s", taskq->name)) {
				kprintf("unable to create task thread\n");
				panic("usb_create_event_thread task");
			}
		}
	}
}

/*
 * Add a task to be performed by the task thread.  This function can be
 * called from any context and the task will be executed in a process
 * context ASAP.
 */
void
usb_add_task(usbd_device_handle dev, struct usb_task *task, int queue)
{
	struct usb_taskq *taskq;

	crit_enter();

	taskq = &usb_taskq[queue];
	if (task->queue == -1) {
		DPRINTFN(2,("usb_add_task: task=%p\n", task));
		TAILQ_INSERT_TAIL(&taskq->tasks, task, next);
		task->queue = queue;
	} else {
		DPRINTFN(3,("usb_add_task: task=%p on q\n", task));
	}
	wakeup(&taskq->tasks);

	crit_exit();
}

void
usb_do_task(usbd_device_handle dev, struct usb_task *task, int queue,
	    int time_out)
{
	struct usb_taskq *taskq;

	crit_enter();

	taskq = &usb_taskq[queue];
	if (task->queue == -1) {
		DPRINTFN(2,("usb_add_task: task=%p\n", task));
		TAILQ_INSERT_TAIL(&taskq->tasks, task, next);
		task->queue = queue;
	} else {
		DPRINTFN(3,("usb_add_task: task=%p on q\n", task));
	}
	wakeup(&taskq->tasks);

	/* Wait until task is finished */
	tsleep((&taskq->tasks + 1), 0, "usbdotsk", time_out);

	crit_exit();
}

void
usb_rem_task(usbd_device_handle dev, struct usb_task *task)
{
	crit_enter();
	if (task->queue != -1) {
		TAILQ_REMOVE(&usb_taskq[task->queue].tasks, task, next);
		task->queue = -1;
	}
	crit_exit();
}

void
usb_event_thread(void *arg)
{
	device_t self = arg;
	struct usb_softc *sc = device_get_softc(self);

	DPRINTF(("usb_event_thread: start\n"));

	/*
	 * In case this controller is a companion controller to an
	 * EHCI controller we need to wait until the EHCI controller
	 * has grabbed the port.
	 * XXX It would be nicer to do this with a tsleep(), but I don't
	 * know how to synchronize the creation of the threads so it
	 * will work.
	 */
	usb_delay_ms(sc->sc_bus, 500);

	get_mplock();
	crit_enter();

	/* Make sure first discover does something. */
	sc->sc_bus->needs_explore = 1;
	usb_discover(self);

	while (!sc->sc_dying) {
#ifdef USB_DEBUG
		if (usb_noexplore < 2)
#endif
		usb_discover(self);
#ifdef USB_DEBUG
		tsleep(&sc->sc_bus->needs_explore, 0, "usbevt",
		       usb_noexplore ? 0 : hz * 60);
#else
		tsleep(&sc->sc_bus->needs_explore, 0, "usbevt", hz * 60);
#endif
		DPRINTFN(2,("usb_event_thread: woke up\n"));
	}
	sc->sc_event_thread = NULL;

	crit_exit();
	rel_mplock();

	/* In case parent is waiting for us to exit. */
	wakeup(sc);

	DPRINTF(("usb_event_thread: exit\n"));
}

void
usb_task_thread(void *arg)
{
	struct usb_task *task;
	struct usb_taskq *taskq;

	get_mplock();
	crit_enter();

	taskq = arg;
	DPRINTF(("usb_task_thread: start taskq %s\n", taskq->name));

	while (usb_ndevs > 0) {
		task = TAILQ_FIRST(&taskq->tasks);
		if (task == NULL) {
			tsleep(&taskq->tasks, 0, "usbtsk", 0);
			task = TAILQ_FIRST(&taskq->tasks);
		}
		DPRINTFN(2,("usb_task_thread: woke up task=%p\n", task));
		if (task != NULL) {
			TAILQ_REMOVE(&taskq->tasks, task, next);
			task->queue = -1;
			crit_exit();
			task->fun(task->arg);
			crit_enter();
			wakeup((&taskq->tasks + 1));
		}
	}

	crit_exit();
	rel_mplock();

	taskq->taskcreated = 0;
	wakeup(&taskq->taskcreated);

	DPRINTF(("usb_event_thread: exit\n"));
}

int
usbopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = USBUNIT(dev);
	struct usb_softc *sc;

	if (unit == USB_DEV_MINOR) {
		if (usb_dev_open)
			return (EBUSY);
		usb_dev_open = 1;
		usb_async_proc = NULL;
		return (0);
	}

	sc = devclass_get_softc(usb_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_dying)
		return (EIO);

	return (0);
}

int
usbread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct usb_event ue;
	int unit = USBUNIT(dev);
	int error, n;

	if (unit != USB_DEV_MINOR)
		return (ENODEV);

	if (uio->uio_resid != sizeof(struct usb_event))
		return (EINVAL);

	error = 0;
	crit_enter();
	for (;;) {
		n = usb_get_next_event(&ue);
		if (n != 0)
			break;
		if (ap->a_ioflag & IO_NDELAY) {
			error = EWOULDBLOCK;
			break;
		}
		error = tsleep(&usb_events, PCATCH, "usbrea", 0);
		if (error)
			break;
	}
	crit_exit();
	if (!error)
		error = uiomove((void *)&ue, uio->uio_resid, uio);

	return (error);
}

int
usbclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = USBUNIT(dev);

	if (unit == USB_DEV_MINOR) {
		usb_async_proc = NULL;
		usb_dev_open = 0;
	}

	return (0);
}

int
usbioctl(struct dev_ioctl_args *ap)
{
	cdev_t devt = ap->a_head.a_dev;
	struct usb_softc *sc;
	int unit = USBUNIT(devt);

	if (unit == USB_DEV_MINOR) {
		switch (ap->a_cmd) {
		case FIOASYNC:
			if (*(int *)ap->a_data)
				usb_async_proc = curproc;
			else
				usb_async_proc = NULL;
			return (0);

		default:
			return (EINVAL);
		}
	}

	sc = devclass_get_softc(usb_devclass, unit);

	if (sc->sc_dying)
		return (EIO);

	switch (ap->a_cmd) {
	/* This part should be deleted */
  	case USB_DISCOVER:
  		break;
	case USB_REQUEST:
	{
		struct usb_ctl_request *ur = (void *)ap->a_data;
		size_t len = UGETW(ur->ucr_request.wLength);
		struct iovec iov;
		struct uio uio;
		void *ptr = NULL;
		int addr = ur->ucr_addr;
		usbd_status err;
		int error = 0;

		DPRINTF(("usbioctl: USB_REQUEST addr=%d len=%zu\n", addr, len));
		if (len > 32768)
			return (EINVAL);
		if (addr < 0 || addr >= USB_MAX_DEVICES ||
		    sc->sc_bus->devices[addr] == 0)
			return (EINVAL);
		if (len != 0) {
			iov.iov_base = (caddr_t)ur->ucr_data;
			iov.iov_len = len;
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_resid = len;
			uio.uio_offset = 0;
			uio.uio_segflg = UIO_USERSPACE;
			uio.uio_rw =
				ur->ucr_request.bmRequestType & UT_READ ?
				UIO_READ : UIO_WRITE;
			uio.uio_td = curthread;
			ptr = kmalloc(len, M_TEMP, M_WAITOK);
			if (uio.uio_rw == UIO_WRITE) {
				error = uiomove(ptr, len, &uio);
				if (error)
					goto ret;
			}
		}
		err = usbd_do_request_flags(sc->sc_bus->devices[addr],
			  &ur->ucr_request, ptr, ur->ucr_flags, &ur->ucr_actlen,
			  USBD_DEFAULT_TIMEOUT);
		if (err) {
			error = EIO;
			goto ret;
		}
		if (len != 0) {
			if (uio.uio_rw == UIO_READ) {
				error = uiomove(ptr, len, &uio);
				if (error)
					goto ret;
			}
		}
	ret:
		if (ptr)
			kfree(ptr, M_TEMP);
		return (error);
	}

	case USB_DEVICEINFO:
	{
		struct usb_device_info *di = (void *)ap->a_data;
		int addr = di->udi_addr;
		usbd_device_handle dev;

		if (addr < 1 || addr >= USB_MAX_DEVICES)
			return (EINVAL);
		dev = sc->sc_bus->devices[addr];
		if (dev == NULL)
			return (ENXIO);
		usbd_fill_deviceinfo(dev, di, 1);
		break;
	}

	case USB_DEVICESTATS:
		*(struct usb_device_stats *)ap->a_data = sc->sc_bus->stats;
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

static struct filterops usbfiltops =
	{ FILTEROP_ISFD, NULL, usbfilt_detach, usbfilt };

int
usbkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &usbfiltops;
		kn->kn_hook = (caddr_t)dev;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &usb_kqevent.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
usbfilt_detach(struct knote *kn)
{
	struct klist *klist;

	klist = &usb_kqevent.ki_note;
	knote_remove(klist, kn);
}

static int
usbfilt(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	int unit = USBUNIT(dev);
	int ready = 0;

	if (unit == USB_DEV_MINOR) {
		crit_enter();
		if (usb_nevents > 0)
			ready = 1;
		crit_exit();
	}

	return (ready);
}

/* Explore device tree from the root. */
static void
usb_discover(device_t self)
{
	struct usb_softc *sc = device_get_softc(self);

	DPRINTFN(2,("usb_discover\n"));
#ifdef USB_DEBUG
	if (usb_noexplore > 1)
		return;
#endif

	/*
	 * We need mutual exclusion while traversing the device tree,
	 * but this is guaranteed since this function is only called
	 * from the event thread for the controller.
	 */
	crit_enter();
	while (sc->sc_bus->needs_explore && !sc->sc_dying) {
		sc->sc_bus->needs_explore = 0;

		crit_exit();
		sc->sc_bus->root_hub->hub->explore(sc->sc_bus->root_hub);
		crit_enter();

	}
	crit_exit();
}

void
usb_needs_explore(usbd_device_handle dev)
{
	DPRINTFN(2,("usb_needs_explore\n"));
	dev->bus->needs_explore = 1;
	wakeup(&dev->bus->needs_explore);
}

/* Called from a critical section */
int
usb_get_next_event(struct usb_event *ue)
{
	struct usb_event_q *ueq;

	if (usb_nevents <= 0)
		return (0);
	ueq = TAILQ_FIRST(&usb_events);
#ifdef DIAGNOSTIC
	if (ueq == NULL) {
		kprintf("usb: usb_nevents got out of sync! %d\n", usb_nevents);
		usb_nevents = 0;
		return (0);
	}
#endif
	if (ue)
		*ue = ueq->ue;
	TAILQ_REMOVE(&usb_events, ueq, next);
	kfree(ueq, M_USBDEV);
	usb_nevents--;
	return (1);
}

void
usbd_add_dev_event(int type, usbd_device_handle udev)
{
	struct usb_event ue;

	usbd_fill_deviceinfo(udev, &ue.u.ue_device, USB_EVENT_IS_ATTACH(type));
	usb_add_event(type, &ue);
}

void
usbd_add_drv_event(int type, usbd_device_handle udev, device_t dev)
{
	struct usb_event ue;

	ue.u.ue_driver.ue_cookie = udev->cookie;
	strncpy(ue.u.ue_driver.ue_devname, device_get_nameunit(dev),
		sizeof ue.u.ue_driver.ue_devname);
	usb_add_event(type, &ue);
}

void
usb_add_event(int type, struct usb_event *uep)
{
	struct usb_event_q *ueq;
	struct timeval thetime;

	ueq = kmalloc(sizeof *ueq, M_USBDEV, M_INTWAIT);
	ueq->ue = *uep;
	ueq->ue.ue_type = type;
	microtime(&thetime);
	TIMEVAL_TO_TIMESPEC(&thetime, &ueq->ue.ue_time);

	crit_enter();
	if (USB_EVENT_IS_DETACH(type)) {
		struct usb_event_q *ueqi, *ueqi_next;

		for (ueqi = TAILQ_FIRST(&usb_events); ueqi; ueqi = ueqi_next) {
			ueqi_next = TAILQ_NEXT(ueqi, next);
			if (ueqi->ue.u.ue_driver.ue_cookie.cookie ==
			    uep->u.ue_device.udi_cookie.cookie) {
				TAILQ_REMOVE(&usb_events, ueqi, next);
				kfree(ueqi, M_USBDEV);
				usb_nevents--;
				ueqi_next = TAILQ_FIRST(&usb_events);
			}
		}
	}
	if (usb_nevents >= USB_MAX_EVENTS) {
		/* Too many queued events, drop an old one. */
		DPRINTF(("usb: event dropped\n"));
		usb_get_next_event(NULL);
	}
	TAILQ_INSERT_TAIL(&usb_events, ueq, next);
	usb_nevents++;
	wakeup(&usb_events);
	KNOTE(&usb_kqevent.ki_note, 0);
	if (usb_async_proc != NULL) {
		ksignal(usb_async_proc, SIGIO);
	}
	crit_exit();
}

void
usb_schedsoftintr(usbd_bus_handle bus)
{
	DPRINTFN(10,("usb_schedsoftintr: polling=%d\n", bus->use_polling));
#ifdef USB_USE_SOFTINTR
	if (bus->use_polling) {
		bus->methods->soft_intr(bus);
	} else {
		if (!callout_pending(&bus->softi))
			callout_reset(&bus->softi, 0, bus->methods->soft_intr,
			    bus);
	}
#else
       bus->methods->soft_intr(bus);
#endif /* USB_USE_SOFTINTR */
}

static int
usb_detach(device_t self)
{
	struct usb_softc *sc = device_get_softc(self);
	struct usb_event ue;

	DPRINTF(("usb_detach: start\n"));

	sc->sc_dying = 1;

	/* Make all devices disconnect. */
	if (sc->sc_port.device != NULL)
		usb_disconnect_port(&sc->sc_port, self);

	/* Kill off event thread. */
	if (sc->sc_event_thread != NULL) {
		wakeup(&sc->sc_bus->needs_explore);
		if (tsleep(sc, 0, "usbdet", hz * 60))
			device_printf(self,
			    "event thread didn't die\n");
		DPRINTF(("usb_detach: event thread dead\n"));
	}

	release_dev(sc->sc_usbdev);

	if (--usb_ndevs == 0) {
		int i;

		release_dev(usb_dev);
		dev_ops_remove_minor(&usb_ops, USB_DEV_MINOR);
		usb_dev = NULL;

		for (i = 0; i < USB_NUM_TASKQS; i++) {
			struct usb_taskq *taskq = &usb_taskq[i];
			wakeup(&taskq->tasks);
			if (tsleep(&taskq->taskcreated, 0, "usbtdt",
			    hz * 60)) {
				kprintf("usb task thread %s didn't die\n",
				    taskq->name);
			}
		}
	}

	usbd_finish();

#ifdef USB_USE_SOFTINTR
	callout_stop(&sc->sc_bus->softi);
#endif

	ue.u.ue_ctrlr.ue_bus = device_get_unit(self);
	usb_add_event(USB_EVENT_CTRLR_DETACH, &ue);

	return (0);
}

static void
usb_child_detached(device_t self, device_t child)
{
	struct usb_softc *sc = device_get_softc(self);

	/* XXX, should check it is the right device. */
	sc->sc_port.device = NULL;
}

/* Explore USB busses at the end of device configuration */
static void
usb_cold_explore(void *arg)
{
	struct usb_softc *sc;

	TUNABLE_INT_FETCH("hw.usb.hack_defer_exploration",
	    &hack_defer_exploration);

	if (!hack_defer_exploration)
		return;

	KASSERT(cold || TAILQ_EMPTY(&usb_coldexplist),
	    ("usb_cold_explore: busses to explore when !cold"));
	while (!TAILQ_EMPTY(&usb_coldexplist)) {
		sc = TAILQ_FIRST(&usb_coldexplist);
		TAILQ_REMOVE(&usb_coldexplist, sc, sc_coldexplist);

		sc->sc_bus->use_polling++;
		sc->sc_port.device->hub->explore(sc->sc_bus->root_hub);
		sc->sc_bus->use_polling--;
	}
}

struct usbd_bus *
usb_getbushandle(struct usb_softc *sc)
{
	return (sc->sc_bus);
}


SYSINIT(usb_cold_explore, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE,
    usb_cold_explore, NULL);

DRIVER_MODULE(usb, ohci, usb_driver, usb_devclass, NULL, NULL);
DRIVER_MODULE(usb, uhci, usb_driver, usb_devclass, NULL, NULL);
DRIVER_MODULE(usb, ehci, usb_driver, usb_devclass, NULL, NULL);
