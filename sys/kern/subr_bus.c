/*
 * Copyright (c) 1997,1998 Doug Rabson
 * All rights reserved.
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
 *
 * $FreeBSD: src/sys/kern/subr_bus.c,v 1.54.2.9 2002/10/10 15:13:32 jhb Exp $
 */

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kobj.h>
#include <sys/bus_private.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/device.h>
#include <sys/lock.h>
#include <sys/caps.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/event.h>
#include <sys/signalvar.h>
#include <sys/machintr.h>
#include <sys/vnode.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>	/* for device_printf() */

SYSCTL_NODE(_hw, OID_AUTO, bus, CTLFLAG_RW, NULL, NULL);
SYSCTL_NODE(, OID_AUTO, dev, CTLFLAG_RW, NULL, NULL);

MALLOC_DEFINE(M_BUS, "bus", "Bus data structures");

#ifdef BUS_DEBUG
#define PDEBUG(a)	(kprintf("%s:%d: ", __func__, __LINE__), kprintf a, kprintf("\n"))
#define DEVICENAME(d)	((d)? device_get_name(d): "no device")
#define DRIVERNAME(d)	((d)? d->name : "no driver")
#define DEVCLANAME(d)	((d)? d->name : "no devclass")

/* Produce the indenting, indent*2 spaces plus a '.' ahead of that to 
 * prevent syslog from deleting initial spaces
 */
#define indentprintf(p)	do { int iJ; kprintf("."); for (iJ=0; iJ<indent; iJ++) kprintf("  "); kprintf p ; } while(0)

static void	print_device_short(device_t dev, int indent);
static void	print_device(device_t dev, int indent);
void		print_device_tree_short(device_t dev, int indent);
void		print_device_tree(device_t dev, int indent);
static void	print_driver_short(driver_t *driver, int indent);
static void	print_driver(driver_t *driver, int indent);
static void	print_driver_list(driver_list_t drivers, int indent);
static void	print_devclass_short(devclass_t dc, int indent);
static void	print_devclass(devclass_t dc, int indent);
void		print_devclass_list_short(void);
void		print_devclass_list(void);

#else
/* Make the compiler ignore the function calls */
#define PDEBUG(a)			/* nop */
#define DEVICENAME(d)			/* nop */
#define DRIVERNAME(d)			/* nop */
#define DEVCLANAME(d)			/* nop */

#define print_device_short(d,i)		/* nop */
#define print_device(d,i)		/* nop */
#define print_device_tree_short(d,i)	/* nop */
#define print_device_tree(d,i)		/* nop */
#define print_driver_short(d,i)		/* nop */
#define print_driver(d,i)		/* nop */
#define print_driver_list(d,i)		/* nop */
#define print_devclass_short(d,i)	/* nop */
#define print_devclass(d,i)		/* nop */
#define print_devclass_list_short()	/* nop */
#define print_devclass_list()		/* nop */
#endif

/*
 * dev sysctl tree
 */

enum {
	DEVCLASS_SYSCTL_PARENT,
};

static int
devclass_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	devclass_t dc = (devclass_t)arg1;
	const char *value;

	switch (arg2) {
	case DEVCLASS_SYSCTL_PARENT:
		value = dc->parent ? dc->parent->name : "";
		break;
	default:
		return (EINVAL);
	}
	return (SYSCTL_OUT(req, value, strlen(value)));
}

static void
devclass_sysctl_init(devclass_t dc)
{

	if (dc->sysctl_tree != NULL)
		return;
	sysctl_ctx_init(&dc->sysctl_ctx);
	dc->sysctl_tree = SYSCTL_ADD_NODE(&dc->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_dev), OID_AUTO, dc->name,
	    CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_PROC(&dc->sysctl_ctx, SYSCTL_CHILDREN(dc->sysctl_tree),
	    OID_AUTO, "%parent", CTLTYPE_STRING | CTLFLAG_RD,
	    dc, DEVCLASS_SYSCTL_PARENT, devclass_sysctl_handler, "A",
	    "parent class");
}

enum {
	DEVICE_SYSCTL_DESC,
	DEVICE_SYSCTL_DRIVER,
	DEVICE_SYSCTL_LOCATION,
	DEVICE_SYSCTL_PNPINFO,
	DEVICE_SYSCTL_PARENT,
};

static int
device_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	const char *value;
	char *buf;
	int error;

	buf = NULL;
	switch (arg2) {
	case DEVICE_SYSCTL_DESC:
		value = dev->desc ? dev->desc : "";
		break;
	case DEVICE_SYSCTL_DRIVER:
		value = dev->driver ? dev->driver->name : "";
		break;
	case DEVICE_SYSCTL_LOCATION:
		value = buf = kmalloc(1024, M_BUS, M_WAITOK | M_ZERO);
		bus_child_location_str(dev, buf, 1024);
		break;
	case DEVICE_SYSCTL_PNPINFO:
		value = buf = kmalloc(1024, M_BUS, M_WAITOK | M_ZERO);
		bus_child_pnpinfo_str(dev, buf, 1024);
		break;
	case DEVICE_SYSCTL_PARENT:
		value = dev->parent ? dev->parent->nameunit : "";
		break;
	default:
		return (EINVAL);
	}
	error = SYSCTL_OUT(req, value, strlen(value));
	if (buf != NULL)
		kfree(buf, M_BUS);
	return (error);
}

static void
device_sysctl_init(device_t dev)
{
	devclass_t dc = dev->devclass;

	if (dev->sysctl_tree != NULL)
		return;
	devclass_sysctl_init(dc);
	sysctl_ctx_init(&dev->sysctl_ctx);
	dev->sysctl_tree = SYSCTL_ADD_NODE(&dev->sysctl_ctx,
	    SYSCTL_CHILDREN(dc->sysctl_tree), OID_AUTO,
	    dev->nameunit + strlen(dc->name),
	    CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%desc", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_DESC, device_sysctl_handler, "A",
	    "device description");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%driver", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_DRIVER, device_sysctl_handler, "A",
	    "device driver name");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%location", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_LOCATION, device_sysctl_handler, "A",
	    "device location relative to parent");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%pnpinfo", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_PNPINFO, device_sysctl_handler, "A",
	    "device identification");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%parent", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_PARENT, device_sysctl_handler, "A",
	    "parent device");
}

static void
device_sysctl_update(device_t dev)
{
	devclass_t dc = dev->devclass;

	if (dev->sysctl_tree == NULL)
		return;
	sysctl_rename_oid(dev->sysctl_tree, dev->nameunit + strlen(dc->name));
}

static void
device_sysctl_fini(device_t dev)
{
	if (dev->sysctl_tree == NULL)
		return;
	sysctl_ctx_free(&dev->sysctl_ctx);
	dev->sysctl_tree = NULL;
}

static void	device_attach_async(device_t dev);
static void	device_attach_thread(void *arg);
static int	device_doattach(device_t dev);

static int do_async_attach = 0;
static int numasyncthreads;
TUNABLE_INT("kern.do_async_attach", &do_async_attach);

/*
 * /dev/devctl implementation
 */

/*
 * This design allows only one reader for /dev/devctl.  This is not desirable
 * in the long run, but will get a lot of hair out of this implementation.
 * Maybe we should make this device a clonable device.
 *
 * Also note: we specifically do not attach a device to the device_t tree
 * to avoid potential chicken and egg problems.  One could argue that all
 * of this belongs to the root node.  One could also further argue that the
 * sysctl interface that we have not might more properly be an ioctl
 * interface, but at this stage of the game, I'm not inclined to rock that
 * boat.
 *
 * I'm also not sure that the SIGIO support is done correctly or not, as
 * I copied it from a driver that had SIGIO support that likely hasn't been
 * tested since 3.4 or 2.2.8!
 */

static int sysctl_devctl_disable(SYSCTL_HANDLER_ARGS);
static int devctl_disable = 0;
TUNABLE_INT("hw.bus.devctl_disable", &devctl_disable);
SYSCTL_PROC(_hw_bus, OID_AUTO, devctl_disable, CTLTYPE_INT | CTLFLAG_RW, 0, 0,
    sysctl_devctl_disable, "I", "devctl disable");

static d_open_t		devopen;
static d_close_t	devclose;
static d_read_t		devread;
static d_ioctl_t	devioctl;
static d_kqfilter_t	devkqfilter;

static struct dev_ops devctl_ops = {
	{ "devctl", 0, D_MPSAFE },
	.d_open =	devopen,
	.d_close =	devclose,
	.d_read =	devread,
	.d_ioctl =	devioctl,
	.d_kqfilter =	devkqfilter
};

struct dev_event_info
{
	char *dei_data;
	TAILQ_ENTRY(dev_event_info) dei_link;
};

TAILQ_HEAD(devq, dev_event_info);

static struct dev_softc
{
	int	inuse;
	struct lock lock;
	struct kqinfo kq;
	struct devq devq;
	struct proc *async_proc;
} devsoftc;

/*
 * Chicken-and-egg problem with devfs, get the queue operational early.
 */
static void
predevinit(void)
{
	lockinit(&devsoftc.lock, "dev mtx", 0, 0);
	TAILQ_INIT(&devsoftc.devq);
}
SYSINIT(predevinit, SI_SUB_CREATE_INIT, SI_ORDER_ANY, predevinit, 0);

static void
devinit(void)
{
	/*
	 * WARNING! make_dev() can call back into devctl_queue_data()
	 *	    immediately.
	 */
	make_dev(&devctl_ops, 0, UID_ROOT, GID_WHEEL, 0600, "devctl");
}

static int
devopen(struct dev_open_args *ap)
{
	/*
	 * Disallow access to disk volumes if RESTRICTEDROOT
	 */
	if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
		return (EPERM);

	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	if (devsoftc.inuse) {
		lockmgr(&devsoftc.lock, LK_RELEASE);
		return (EBUSY);
	}
	/* move to init */
	devsoftc.inuse = 1;
	devsoftc.async_proc = NULL;
	lockmgr(&devsoftc.lock, LK_RELEASE);

	return (0);
}

static int
devclose(struct dev_close_args *ap)
{
	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	devsoftc.inuse = 0;
	wakeup(&devsoftc);
	lockmgr(&devsoftc.lock, LK_RELEASE);

	return (0);
}

/*
 * The read channel for this device is used to report changes to
 * userland in realtime.  We are required to free the data as well as
 * the n1 object because we allocate them separately.  Also note that
 * we return one record at a time.  If you try to read this device a
 * character at a time, you will lose the rest of the data.  Listening
 * programs are expected to cope.
 */
static int
devread(struct dev_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct dev_event_info *n1;
	int rv;

	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	while (TAILQ_EMPTY(&devsoftc.devq)) {
		if (ap->a_ioflag & IO_NDELAY) {
			lockmgr(&devsoftc.lock, LK_RELEASE);
			return (EAGAIN);
		}
		tsleep_interlock(&devsoftc, PCATCH);
		lockmgr(&devsoftc.lock, LK_RELEASE);
		rv = tsleep(&devsoftc, PCATCH | PINTERLOCKED, "devctl", 0);
		lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
		if (rv) {
			/*
			 * Need to translate ERESTART to EINTR here? -- jake
			 */
			lockmgr(&devsoftc.lock, LK_RELEASE);
			return (rv);
		}
	}
	n1 = TAILQ_FIRST(&devsoftc.devq);
	TAILQ_REMOVE(&devsoftc.devq, n1, dei_link);
	lockmgr(&devsoftc.lock, LK_RELEASE);
	rv = uiomove(n1->dei_data, strlen(n1->dei_data), uio);
	kfree(n1->dei_data, M_BUS);
	kfree(n1, M_BUS);
	return (rv);
}

static	int
devioctl(struct dev_ioctl_args *ap)
{
	switch (ap->a_cmd) {

	case FIONBIO:
		return (0);
	case FIOASYNC:
		if (*(int*)ap->a_data)
			devsoftc.async_proc = curproc;
		else
			devsoftc.async_proc = NULL;
		return (0);

		/* (un)Support for other fcntl() calls. */
	case FIOCLEX:
	case FIONCLEX:
	case FIONREAD:
	case FIOSETOWN:
	case FIOGETOWN:
	default:
		break;
	}
	return (ENOTTY);
}

static void dev_filter_detach(struct knote *);
static int dev_filter_read(struct knote *, long);

static struct filterops dev_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE, NULL,
	  dev_filter_detach, dev_filter_read };

static int
devkqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;
	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &dev_filtops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		lockmgr(&devsoftc.lock, LK_RELEASE);
		return (0);
	}

	klist = &devsoftc.kq.ki_note;
	knote_insert(klist, kn);

	lockmgr(&devsoftc.lock, LK_RELEASE);

	return (0);
}

static void
dev_filter_detach(struct knote *kn)
{
	struct klist *klist;

	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	klist = &devsoftc.kq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&devsoftc.lock, LK_RELEASE);
}

static int
dev_filter_read(struct knote *kn, long hint)
{
	int ready = 0;

	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	if (!TAILQ_EMPTY(&devsoftc.devq))
		ready = 1;
	lockmgr(&devsoftc.lock, LK_RELEASE);

	return (ready);
}


/**
 * @brief Return whether the userland process is running
 */
boolean_t
devctl_process_running(void)
{
	return (devsoftc.inuse == 1);
}

/**
 * @brief Queue data to be read from the devctl device
 *
 * Generic interface to queue data to the devctl device.  It is
 * assumed that @p data is properly formatted.  It is further assumed
 * that @p data is allocated using the M_BUS malloc type.
 */
void
devctl_queue_data(char *data)
{
	struct dev_event_info *n1 = NULL;
	struct proc *p;

	n1 = kmalloc(sizeof(*n1), M_BUS, M_NOWAIT);
	if (n1 == NULL)
		return;
	n1->dei_data = data;
	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&devsoftc.devq, n1, dei_link);
	wakeup(&devsoftc);
	lockmgr(&devsoftc.lock, LK_RELEASE);
	KNOTE(&devsoftc.kq.ki_note, 0);
	p = devsoftc.async_proc;
	if (p != NULL)
		ksignal(p, SIGIO);
}

/**
 * @brief Send a 'notification' to userland, using standard ways
 */
void
devctl_notify(const char *system, const char *subsystem, const char *type,
    const char *data)
{
	int len = 0;
	char *msg;

	if (system == NULL)
		return;		/* BOGUS!  Must specify system. */
	if (subsystem == NULL)
		return;		/* BOGUS!  Must specify subsystem. */
	if (type == NULL)
		return;		/* BOGUS!  Must specify type. */
	len += strlen(" system=") + strlen(system);
	len += strlen(" subsystem=") + strlen(subsystem);
	len += strlen(" type=") + strlen(type);
	/* add in the data message plus newline. */
	if (data != NULL)
		len += strlen(data);
	len += 3;	/* '!', '\n', and NUL */
	msg = kmalloc(len, M_BUS, M_NOWAIT);
	if (msg == NULL)
		return;		/* Drop it on the floor */
	if (data != NULL)
		ksnprintf(msg, len, "!system=%s subsystem=%s type=%s %s\n",
		    system, subsystem, type, data);
	else
		ksnprintf(msg, len, "!system=%s subsystem=%s type=%s\n",
		    system, subsystem, type);
	devctl_queue_data(msg);
}

/*
 * Common routine that tries to make sending messages as easy as possible.
 * We allocate memory for the data, copy strings into that, but do not
 * free it unless there's an error.  The dequeue part of the driver should
 * free the data.  We don't send data when the device is disabled.  We do
 * send data, even when we have no listeners, because we wish to avoid
 * races relating to startup and restart of listening applications.
 *
 * devaddq is designed to string together the type of event, with the
 * object of that event, plus the plug and play info and location info
 * for that event.  This is likely most useful for devices, but less
 * useful for other consumers of this interface.  Those should use
 * the devctl_queue_data() interface instead.
 */
static void
devaddq(const char *type, const char *what, device_t dev)
{
	char *data = NULL;
	char *loc = NULL;
	char *pnp = NULL;
	const char *parstr;

	if (devctl_disable)
		return;
	data = kmalloc(1024, M_BUS, M_NOWAIT);
	if (data == NULL)
		goto bad;

	/* get the bus specific location of this device */
	loc = kmalloc(1024, M_BUS, M_NOWAIT);
	if (loc == NULL)
		goto bad;
	*loc = '\0';
	bus_child_location_str(dev, loc, 1024);

	/* Get the bus specific pnp info of this device */
	pnp = kmalloc(1024, M_BUS, M_NOWAIT);
	if (pnp == NULL)
		goto bad;
	*pnp = '\0';
	bus_child_pnpinfo_str(dev, pnp, 1024);

	/* Get the parent of this device, or / if high enough in the tree. */
	if (device_get_parent(dev) == NULL)
		parstr = ".";	/* Or '/' ? */
	else
		parstr = device_get_nameunit(device_get_parent(dev));
	/* String it all together. */
	ksnprintf(data, 1024, "%s%s at %s %s on %s\n", type, what, loc, pnp,
	  parstr);
	kfree(loc, M_BUS);
	kfree(pnp, M_BUS);
	devctl_queue_data(data);
	return;
bad:
	if (pnp != NULL)
		kfree(pnp, M_BUS);
	if (loc != NULL)
		kfree(loc, M_BUS);
	if (loc != NULL)
		kfree(data, M_BUS);
	return;
}

/*
 * A device was added to the tree.  We are called just after it successfully
 * attaches (that is, probe and attach success for this device).  No call
 * is made if a device is merely parented into the tree.  See devnomatch
 * if probe fails.  If attach fails, no notification is sent (but maybe
 * we should have a different message for this).
 */
static void
devadded(device_t dev)
{
	char *pnp = NULL;
	char *tmp = NULL;

	pnp = kmalloc(1024, M_BUS, M_NOWAIT);
	if (pnp == NULL)
		goto fail;
	tmp = kmalloc(1024, M_BUS, M_NOWAIT);
	if (tmp == NULL)
		goto fail;
	*pnp = '\0';
	bus_child_pnpinfo_str(dev, pnp, 1024);
	ksnprintf(tmp, 1024, "%s %s", device_get_nameunit(dev), pnp);
	devaddq("+", tmp, dev);
fail:
	if (pnp != NULL)
		kfree(pnp, M_BUS);
	if (tmp != NULL)
		kfree(tmp, M_BUS);
	return;
}

/*
 * A device was removed from the tree.  We are called just before this
 * happens.
 */
static void
devremoved(device_t dev)
{
	char *pnp = NULL;
	char *tmp = NULL;

	pnp = kmalloc(1024, M_BUS, M_NOWAIT);
	if (pnp == NULL)
		goto fail;
	tmp = kmalloc(1024, M_BUS, M_NOWAIT);
	if (tmp == NULL)
		goto fail;
	*pnp = '\0';
	bus_child_pnpinfo_str(dev, pnp, 1024);
	ksnprintf(tmp, 1024, "%s %s", device_get_nameunit(dev), pnp);
	devaddq("-", tmp, dev);
fail:
	if (pnp != NULL)
		kfree(pnp, M_BUS);
	if (tmp != NULL)
		kfree(tmp, M_BUS);
	return;
}

/*
 * Called when there's no match for this device.  This is only called
 * the first time that no match happens, so we don't keep getitng this
 * message.  Should that prove to be undesirable, we can change it.
 * This is called when all drivers that can attach to a given bus
 * decline to accept this device.  Other errrors may not be detected.
 */
static void
devnomatch(device_t dev)
{
	devaddq("?", "", dev);
}

static int
sysctl_devctl_disable(SYSCTL_HANDLER_ARGS)
{
	struct dev_event_info *n1;
	int dis, error;

	dis = devctl_disable;
	error = sysctl_handle_int(oidp, &dis, 0, req);
	if (error || !req->newptr)
		return (error);
	lockmgr(&devsoftc.lock, LK_EXCLUSIVE);
	devctl_disable = dis;
	if (dis) {
		while (!TAILQ_EMPTY(&devsoftc.devq)) {
			n1 = TAILQ_FIRST(&devsoftc.devq);
			TAILQ_REMOVE(&devsoftc.devq, n1, dei_link);
			kfree(n1->dei_data, M_BUS);
			kfree(n1, M_BUS);
		}
	}
	lockmgr(&devsoftc.lock, LK_RELEASE);
	return (0);
}

/* End of /dev/devctl code */

TAILQ_HEAD(,bsd_device)	bus_data_devices;
static int bus_data_generation = 1;

kobj_method_t null_methods[] = {
	{ 0, 0 }
};

DEFINE_CLASS(null, null_methods, 0);

/*
 * Devclass implementation
 */

static devclass_list_t devclasses = TAILQ_HEAD_INITIALIZER(devclasses);

static devclass_t
devclass_find_internal(const char *classname, const char *parentname,
		       int create)
{
	devclass_t dc;

	PDEBUG(("looking for %s", classname));
	if (classname == NULL)
		return(NULL);

	TAILQ_FOREACH(dc, &devclasses, link)
		if (!strcmp(dc->name, classname))
			break;

	if (create && !dc) {
		PDEBUG(("creating %s", classname));
		dc = kmalloc(sizeof(struct devclass) + strlen(classname) + 1,
			    M_BUS, M_INTWAIT | M_ZERO);
		dc->parent = NULL;
		dc->name = (char*) (dc + 1);
		strcpy(dc->name, classname);
		dc->devices = NULL;
		dc->maxunit = 0;
		TAILQ_INIT(&dc->drivers);
		TAILQ_INSERT_TAIL(&devclasses, dc, link);

		bus_data_generation_update();

	}

	/*
	 * If a parent class is specified, then set that as our parent so
	 * that this devclass will support drivers for the parent class as
	 * well.  If the parent class has the same name don't do this though
	 * as it creates a cycle that can trigger an infinite loop in
	 * device_probe_child() if a device exists for which there is no
	 * suitable driver.
	 */
	if (parentname && dc && !dc->parent &&
	    strcmp(classname, parentname) != 0)
		dc->parent = devclass_find_internal(parentname, NULL, FALSE);

	return(dc);
}

devclass_t
devclass_create(const char *classname)
{
	return(devclass_find_internal(classname, NULL, TRUE));
}

devclass_t
devclass_find(const char *classname)
{
	return(devclass_find_internal(classname, NULL, FALSE));
}

device_t
devclass_find_unit(const char *classname, int unit)
{
	devclass_t dc;

	if ((dc = devclass_find(classname)) != NULL)
	    return(devclass_get_device(dc, unit));
	return (NULL);
}

int
devclass_add_driver(devclass_t dc, driver_t *driver)
{
	driverlink_t dl;
	device_t dev;
	int i;

	PDEBUG(("%s", DRIVERNAME(driver)));

	dl = kmalloc(sizeof *dl, M_BUS, M_INTWAIT | M_ZERO);

	/*
	 * Compile the driver's methods. Also increase the reference count
	 * so that the class doesn't get freed when the last instance
	 * goes. This means we can safely use static methods and avoids a
	 * double-free in devclass_delete_driver.
	 */
	kobj_class_instantiate(driver);

	/*
	 * Make sure the devclass which the driver is implementing exists.
	 */
	devclass_find_internal(driver->name, NULL, TRUE);

	dl->driver = driver;
	TAILQ_INSERT_TAIL(&dc->drivers, dl, link);

	/*
	 * Call BUS_DRIVER_ADDED for any existing busses in this class,
	 * but only if the bus has already been attached (otherwise we
	 * might probe too early).
	 *
	 * This is what will cause a newly loaded module to be associated
	 * with hardware.  bus_generic_driver_added() is typically what ends
	 * up being called.
	 */
	for (i = 0; i < dc->maxunit; i++) {
		if ((dev = dc->devices[i]) != NULL) {
			if (dev->state >= DS_ATTACHED)
				BUS_DRIVER_ADDED(dev, driver);
		}
	}

	bus_data_generation_update();
	return(0);
}

int
devclass_delete_driver(devclass_t busclass, driver_t *driver)
{
	devclass_t dc = devclass_find(driver->name);
	driverlink_t dl;
	device_t dev;
	int i;
	int error;

	PDEBUG(("%s from devclass %s", driver->name, DEVCLANAME(busclass)));

	if (!dc)
		return(0);

	/*
	 * Find the link structure in the bus' list of drivers.
	 */
	TAILQ_FOREACH(dl, &busclass->drivers, link)
		if (dl->driver == driver)
			break;

	if (!dl) {
		PDEBUG(("%s not found in %s list", driver->name, busclass->name));
		return(ENOENT);
	}

	/*
	 * Disassociate from any devices.  We iterate through all the
	 * devices in the devclass of the driver and detach any which are
	 * using the driver and which have a parent in the devclass which
	 * we are deleting from.
	 *
	 * Note that since a driver can be in multiple devclasses, we
	 * should not detach devices which are not children of devices in
	 * the affected devclass.
	 */
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i]) {
			dev = dc->devices[i];
			if (dev->driver == driver && dev->parent &&
			    dev->parent->devclass == busclass) {
				if ((error = device_detach(dev)) != 0)
					return(error);
				device_set_driver(dev, NULL);
		    	}
		}

	TAILQ_REMOVE(&busclass->drivers, dl, link);
	kfree(dl, M_BUS);

	kobj_class_uninstantiate(driver);

	bus_data_generation_update();
	return(0);
}

static driverlink_t
devclass_find_driver_internal(devclass_t dc, const char *classname)
{
	driverlink_t dl;

	PDEBUG(("%s in devclass %s", classname, DEVCLANAME(dc)));

	TAILQ_FOREACH(dl, &dc->drivers, link)
		if (!strcmp(dl->driver->name, classname))
			return(dl);

	PDEBUG(("not found"));
	return(NULL);
}

kobj_class_t
devclass_find_driver(devclass_t dc, const char *classname)
{
	driverlink_t dl;

	dl = devclass_find_driver_internal(dc, classname);
	if (dl)
		return(dl->driver);
	else
		return(NULL);
}

const char *
devclass_get_name(devclass_t dc)
{
	return(dc->name);
}

device_t
devclass_get_device(devclass_t dc, int unit)
{
	if (dc == NULL || unit < 0 || unit >= dc->maxunit)
		return(NULL);
	return(dc->devices[unit]);
}

void *
devclass_get_softc(devclass_t dc, int unit)
{
	device_t dev;

	dev = devclass_get_device(dc, unit);
	if (!dev)
		return(NULL);

	return(device_get_softc(dev));
}

int
devclass_get_devices(devclass_t dc, device_t **devlistp, int *devcountp)
{
	int i;
	int count;
	device_t *list;
    
	count = 0;
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i])
			count++;

	list = kmalloc(count * sizeof(device_t), M_TEMP, M_INTWAIT | M_ZERO);

	count = 0;
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i]) {
			list[count] = dc->devices[i];
			count++;
		}

	*devlistp = list;
	*devcountp = count;

	return(0);
}

/**
 * @brief Get a list of drivers in the devclass
 *
 * An array containing a list of pointers to all the drivers in the
 * given devclass is allocated and returned in @p *listp.  The number
 * of drivers in the array is returned in @p *countp. The caller should
 * free the array using @c free(p, M_TEMP).
 *
 * @param dc            the devclass to examine
 * @param listp         gives location for array pointer return value
 * @param countp        gives location for number of array elements
 *                      return value
 *
 * @retval 0            success
 * @retval ENOMEM       the array allocation failed
 */
int
devclass_get_drivers(devclass_t dc, driver_t ***listp, int *countp)
{
        driverlink_t dl;
        driver_t **list;
        int count;

        count = 0;
        TAILQ_FOREACH(dl, &dc->drivers, link)
                count++;
        list = kmalloc(count * sizeof(driver_t *), M_TEMP, M_NOWAIT);
        if (list == NULL)
                return (ENOMEM);

        count = 0;
        TAILQ_FOREACH(dl, &dc->drivers, link) {
                list[count] = dl->driver;
                count++;
        }
        *listp = list;
        *countp = count;

        return (0);
}

/**
 * @brief Get the number of devices in a devclass
 *
 * @param dc		the devclass to examine
 */
int
devclass_get_count(devclass_t dc)
{
	int count, i;

	count = 0;
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i])
			count++;
	return (count);
}

int
devclass_get_maxunit(devclass_t dc)
{
	return(dc->maxunit);
}

void
devclass_set_parent(devclass_t dc, devclass_t pdc)
{
        dc->parent = pdc;
}

devclass_t
devclass_get_parent(devclass_t dc)
{
	return(dc->parent);
}

static int
devclass_alloc_unit(devclass_t dc, int *unitp)
{
	int unit = *unitp;

	PDEBUG(("unit %d in devclass %s", unit, DEVCLANAME(dc)));

	/* If we have been given a wired unit number, check for existing device */
	if (unit != -1) {
		if (unit >= 0 && unit < dc->maxunit &&
		    dc->devices[unit] != NULL) {
			if (bootverbose)
				kprintf("%s-: %s%d exists, using next available unit number\n",
				       dc->name, dc->name, unit);
			/* find the next available slot */
			while (++unit < dc->maxunit && dc->devices[unit] != NULL)
				;
		}
	} else {
		/* Unwired device, find the next available slot for it */
		unit = 0;
		while (unit < dc->maxunit && dc->devices[unit] != NULL)
			unit++;
	}

	/*
	 * We've selected a unit beyond the length of the table, so let's
	 * extend the table to make room for all units up to and including
	 * this one.
	 */
	if (unit >= dc->maxunit) {
		device_t *newlist;
		int newsize;

		newsize = (unit + 1);
		newlist = kmalloc(sizeof(device_t) * newsize, M_BUS,
				 M_INTWAIT | M_ZERO);
		if (newlist == NULL)
			return(ENOMEM);
		/*
		 * WARNING: Due to gcc builtin optimization,
		 *	    calling bcopy causes gcc to assume
		 *	    that the source and destination args
		 *	    cannot be NULL and optimize-away later
		 *	    conditional tests to determine if dc->devices
		 *	    is NULL.  In this situation, in fact,
		 *	    dc->devices CAN be NULL w/ maxunit == 0.
		 */
		if (dc->devices) {
			bcopy(dc->devices,
			      newlist,
			      sizeof(device_t) * dc->maxunit);
			kfree(dc->devices, M_BUS);
		}
		dc->devices = newlist;
		dc->maxunit = newsize;
	}
	PDEBUG(("now: unit %d in devclass %s", unit, DEVCLANAME(dc)));

	*unitp = unit;
	return(0);
}

static int
devclass_add_device(devclass_t dc, device_t dev)
{
	int buflen, error;

	PDEBUG(("%s in devclass %s", DEVICENAME(dev), DEVCLANAME(dc)));

	buflen = strlen(dc->name) + 5;
	dev->nameunit = kmalloc(buflen, M_BUS, M_INTWAIT | M_ZERO);
	if (dev->nameunit == NULL)
		return(ENOMEM);

	if ((error = devclass_alloc_unit(dc, &dev->unit)) != 0) {
		kfree(dev->nameunit, M_BUS);
		dev->nameunit = NULL;
		return(error);
	}
	dc->devices[dev->unit] = dev;
	dev->devclass = dc;
	ksnprintf(dev->nameunit, buflen, "%s%d", dc->name, dev->unit);

	return(0);
}

static int
devclass_delete_device(devclass_t dc, device_t dev)
{
	if (!dc || !dev)
		return(0);

	PDEBUG(("%s in devclass %s", DEVICENAME(dev), DEVCLANAME(dc)));

	if (dev->devclass != dc || dc->devices[dev->unit] != dev) {
		panic("devclass_delete_device: inconsistent device class: "
		      "%p/%p %d %p/%p\n", dev->devclass, dc, dev->unit,
		      dc->devices[dev->unit], dev);
	}
	dc->devices[dev->unit] = NULL;
	if (dev->flags & DF_WILDCARD)
		dev->unit = -1;
	dev->devclass = NULL;
	kfree(dev->nameunit, M_BUS);
	dev->nameunit = NULL;

	return(0);
}

static device_t
make_device(device_t parent, const char *name, int unit)
{
	device_t dev;
	devclass_t dc;

	PDEBUG(("%s at %s as unit %d", name, DEVICENAME(parent), unit));

	if (name != NULL) {
		dc = devclass_find_internal(name, NULL, TRUE);
		if (!dc) {
			kprintf("make_device: can't find device class %s\n", name);
			return(NULL);
		}
	} else
		dc = NULL;

	dev = kmalloc(sizeof(struct bsd_device), M_BUS, M_INTWAIT | M_ZERO);
	if (!dev)
		return(0);

	dev->parent = parent;
	TAILQ_INIT(&dev->children);
	kobj_init((kobj_t) dev, &null_class);
	dev->driver = NULL;
	dev->devclass = NULL;
	dev->unit = unit;
	dev->nameunit = NULL;
	dev->desc = NULL;
	dev->busy = 0;
	dev->devflags = 0;
	dev->flags = DF_ENABLED;
	dev->order = 0;
	if (unit == -1)
		dev->flags |= DF_WILDCARD;
	if (name) {
		dev->flags |= DF_FIXEDCLASS;
		if (devclass_add_device(dc, dev) != 0) {
			kobj_delete((kobj_t)dev, M_BUS);
			return(NULL);
		}
    	}
	dev->ivars = NULL;
	dev->softc = NULL;

	dev->state = DS_NOTPRESENT;

	TAILQ_INSERT_TAIL(&bus_data_devices, dev, devlink);
	bus_data_generation_update();

	return(dev);
}

static int
device_print_child(device_t dev, device_t child)
{
	int retval = 0;

	if (device_is_alive(child))
		retval += BUS_PRINT_CHILD(dev, child);
	else
		retval += device_printf(child, " not found\n");

	return(retval);
}

device_t
device_add_child(device_t dev, const char *name, int unit)
{
	return device_add_child_ordered(dev, 0, name, unit);
}

device_t
device_add_child_ordered(device_t dev, int order, const char *name, int unit)
{
	device_t child;
	device_t place;

	PDEBUG(("%s at %s with order %d as unit %d", name, DEVICENAME(dev),
		order, unit));

	child = make_device(dev, name, unit);
	if (child == NULL)
		return child;
	child->order = order;

	TAILQ_FOREACH(place, &dev->children, link) {
		if (place->order > order)
			break;
	}

	if (place) {
		/*
		 * The device 'place' is the first device whose order is
		 * greater than the new child.
		 */
		TAILQ_INSERT_BEFORE(place, child, link);
	} else {
		/*
		 * The new child's order is greater or equal to the order of
		 * any existing device. Add the child to the tail of the list.
		 */
		TAILQ_INSERT_TAIL(&dev->children, child, link);
    	}

	bus_data_generation_update();
	return(child);
}

int
device_delete_child(device_t dev, device_t child)
{
	int error;
	device_t grandchild;

	PDEBUG(("%s from %s", DEVICENAME(child), DEVICENAME(dev)));

	/* remove children first */
	while ( (grandchild = TAILQ_FIRST(&child->children)) ) {
        	error = device_delete_child(child, grandchild);
		if (error)
			return(error);
	}

	if ((error = device_detach(child)) != 0)
		return(error);
	if (child->devclass)
		devclass_delete_device(child->devclass, child);
	TAILQ_REMOVE(&dev->children, child, link);
	TAILQ_REMOVE(&bus_data_devices, child, devlink);
	kobj_delete((kobj_t)child, M_BUS);

	bus_data_generation_update();
	return(0);
}

/**
 * @brief Delete all children devices of the given device, if any.
 *
 * This function deletes all children devices of the given device, if
 * any, using the device_delete_child() function for each device it
 * finds. If a child device cannot be deleted, this function will
 * return an error code.
 * 
 * @param dev		the parent device
 *
 * @retval 0		success
 * @retval non-zero	a device would not detach
 */
int
device_delete_children(device_t dev)
{
	device_t child;
	int error;

	PDEBUG(("Deleting all children of %s", DEVICENAME(dev)));

	error = 0;

	while ((child = TAILQ_FIRST(&dev->children)) != NULL) {
		error = device_delete_child(dev, child);
		if (error) {
			PDEBUG(("Failed deleting %s", DEVICENAME(child)));
			break;
		}
	}
	return (error);
}

/**
 * @brief Find a device given a unit number
 *
 * This is similar to devclass_get_devices() but only searches for
 * devices which have @p dev as a parent.
 *
 * @param dev		the parent device to search
 * @param unit		the unit number to search for.  If the unit is -1,
 *			return the first child of @p dev which has name
 *			@p classname (that is, the one with the lowest unit.)
 *
 * @returns		the device with the given unit number or @c
 *			NULL if there is no such device
 */
device_t
device_find_child(device_t dev, const char *classname, int unit)
{
	devclass_t dc;
	device_t child;

	dc = devclass_find(classname);
	if (!dc)
		return(NULL);

	if (unit != -1) {
		child = devclass_get_device(dc, unit);
		if (child && child->parent == dev)
			return (child);
	} else {
		for (unit = 0; unit < devclass_get_maxunit(dc); unit++) {
			child = devclass_get_device(dc, unit);
			if (child && child->parent == dev)
				return (child);
		}
	}
	return(NULL);
}

static driverlink_t
first_matching_driver(devclass_t dc, device_t dev)
{
	if (dev->devclass)
		return(devclass_find_driver_internal(dc, dev->devclass->name));
	else
		return(TAILQ_FIRST(&dc->drivers));
}

static driverlink_t
next_matching_driver(devclass_t dc, device_t dev, driverlink_t last)
{
	if (dev->devclass) {
		driverlink_t dl;
		for (dl = TAILQ_NEXT(last, link); dl; dl = TAILQ_NEXT(dl, link))
			if (!strcmp(dev->devclass->name, dl->driver->name))
				return(dl);
		return(NULL);
	} else
		return(TAILQ_NEXT(last, link));
}

int
device_probe_child(device_t dev, device_t child)
{
	devclass_t dc;
	driverlink_t best = NULL;
	driverlink_t dl;
	int result, pri = 0;
	int hasclass = (child->devclass != NULL);

	dc = dev->devclass;
	if (!dc)
		panic("device_probe_child: parent device has no devclass");

	if (child->state == DS_ALIVE)
		return(0);

	for (; dc; dc = dc->parent) {
		for (dl = first_matching_driver(dc, child); dl;
		     dl = next_matching_driver(dc, child, dl)) {
			PDEBUG(("Trying %s", DRIVERNAME(dl->driver)));
			device_set_driver(child, dl->driver);
			if (!hasclass)
				device_set_devclass(child, dl->driver->name);
			result = DEVICE_PROBE(child);
			if (!hasclass)
				device_set_devclass(child, 0);

			/*
			 * If the driver returns SUCCESS, there can be
			 * no higher match for this device.
			 */
			if (result == 0) {
				best = dl;
				pri = 0;
				break;
			}

			/*
			 * The driver returned an error so it
			 * certainly doesn't match.
			 */
			if (result > 0) {
				device_set_driver(child, NULL);
				continue;
			}

			/*
			 * A priority lower than SUCCESS, remember the
			 * best matching driver. Initialise the value
			 * of pri for the first match.
			 */
			if (best == NULL || result > pri) {
				best = dl;
				pri = result;
				continue;
			}
		}
		/*
	         * If we have unambiguous match in this devclass,
	         * don't look in the parent.
	         */
	        if (best && pri == 0)
			break;
	}

	/*
	 * If we found a driver, change state and initialise the devclass.
	 */
	if (best) {
		if (!child->devclass)
			device_set_devclass(child, best->driver->name);
		device_set_driver(child, best->driver);
		if (pri < 0) {
			/*
			 * A bit bogus. Call the probe method again to make
			 * sure that we have the right description.
			 */
			DEVICE_PROBE(child);
		}

		bus_data_generation_update();
		child->state = DS_ALIVE;
		return(0);
	}

	return(ENXIO);
}

int
device_probe_child_gpri(device_t dev, device_t child, u_int gpri)
{
	devclass_t dc;
	driverlink_t best = NULL;
	driverlink_t dl;
	int result, pri = 0;
	int hasclass = (child->devclass != NULL);

	dc = dev->devclass;
	if (!dc)
		panic("device_probe_child: parent device has no devclass");

	if (child->state == DS_ALIVE)
		return(0);

	for (; dc; dc = dc->parent) {
		for (dl = first_matching_driver(dc, child); dl;
			dl = next_matching_driver(dc, child, dl)) {
			/*
			 * GPRI handling, only probe drivers with the
			 * specific GPRI.
			 */
			if (dl->driver->gpri != gpri)
				continue;

			PDEBUG(("Trying %s", DRIVERNAME(dl->driver)));
			device_set_driver(child, dl->driver);
			if (!hasclass)
				device_set_devclass(child, dl->driver->name);
			result = DEVICE_PROBE(child);
			if (!hasclass)
				device_set_devclass(child, 0);

			/*
			 * If the driver returns SUCCESS, there can be
			 * no higher match for this device.
			 */
			if (result == 0) {
				best = dl;
				pri = 0;
				break;
			}

			/*
			 * The driver returned an error so it
			 * certainly doesn't match.
			 */
			if (result > 0) {
				device_set_driver(child, NULL);
				continue;
			}

			/*
			 * A priority lower than SUCCESS, remember the
			 * best matching driver. Initialise the value
			 * of pri for the first match.
			 */
			if (best == NULL || result > pri) {
				best = dl;
				pri = result;
				continue;
			}
	        }
		/*
	         * If we have unambiguous match in this devclass,
	         * don't look in the parent.
	         */
	        if (best && pri == 0)
			break;
	}

	/*
	 * If we found a driver, change state and initialise the devclass.
	 */
	if (best) {
		if (!child->devclass)
			device_set_devclass(child, best->driver->name);
		device_set_driver(child, best->driver);
		if (pri < 0) {
			/*
			 * A bit bogus. Call the probe method again to make
			 * sure that we have the right description.
			 */
			DEVICE_PROBE(child);
		}

		bus_data_generation_update();
		child->state = DS_ALIVE;
		return(0);
	}

	return(ENXIO);
}

device_t
device_get_parent(device_t dev)
{
	return dev->parent;
}

int
device_get_children(device_t dev, device_t **devlistp, int *devcountp)
{
	int count;
	device_t child;
	device_t *list;
    
	count = 0;
	TAILQ_FOREACH(child, &dev->children, link)
		count++;

	list = kmalloc(count * sizeof(device_t), M_TEMP, M_INTWAIT | M_ZERO);

	count = 0;
	TAILQ_FOREACH(child, &dev->children, link) {
		list[count] = child;
		count++;
	}

	*devlistp = list;
	*devcountp = count;

	return(0);
}

driver_t *
device_get_driver(device_t dev)
{
	return(dev->driver);
}

devclass_t
device_get_devclass(device_t dev)
{
	return(dev->devclass);
}

const char *
device_get_name(device_t dev)
{
	if (dev->devclass)
		return devclass_get_name(dev->devclass);
	return(NULL);
}

const char *
device_get_nameunit(device_t dev)
{
	return(dev->nameunit);
}

int
device_get_unit(device_t dev)
{
	return(dev->unit);
}

const char *
device_get_desc(device_t dev)
{
	return(dev->desc);
}

uint32_t
device_get_flags(device_t dev)
{
	return(dev->devflags);
}

struct sysctl_ctx_list *
device_get_sysctl_ctx(device_t dev)
{
	return (&dev->sysctl_ctx);
}

struct sysctl_oid *
device_get_sysctl_tree(device_t dev)
{
	return (dev->sysctl_tree);
}

int
device_print_prettyname(device_t dev)
{
	const char *name = device_get_name(dev);

	if (name == NULL)
		return kprintf("unknown: ");
	else
		return kprintf("%s%d: ", name, device_get_unit(dev));
}

int
device_printf(device_t dev, const char * fmt, ...)
{
	__va_list ap;
	int retval;

	retval = device_print_prettyname(dev);
	__va_start(ap, fmt);
	retval += kvprintf(fmt, ap);
	__va_end(ap);
	return retval;
}

/**
 * @brief Print the name of the device followed by a colon, a space
 * and the result of calling log() with the value of @p fmt and
 * the following arguments.
 *
 * @returns the number of characters printed
 */
int
device_log(device_t dev, int pri, const char * fmt, ...)
{
	char buf[128];
	struct sbuf sb;
	const char *name;
	__va_list ap;
	size_t retval;

	retval = 0;

	sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);

	name = device_get_name(dev);

	if (name == NULL)
		sbuf_cat(&sb, "unknown: ");
	else
		sbuf_printf(&sb, "%s%d: ", name, device_get_unit(dev));

	__va_start(ap, fmt);
	sbuf_vprintf(&sb, fmt, ap);
	__va_end(ap);

	sbuf_finish(&sb);

	log(pri, "%.*s", (int) sbuf_len(&sb), sbuf_data(&sb));
	retval = sbuf_len(&sb);

	sbuf_delete(&sb);

	return (retval);
}

static void
device_set_desc_internal(device_t dev, const char* desc, int copy)
{
	if (dev->desc && (dev->flags & DF_DESCMALLOCED)) {
		kfree(dev->desc, M_BUS);
		dev->flags &= ~DF_DESCMALLOCED;
		dev->desc = NULL;
	}

	if (copy && desc) {
		dev->desc = kmalloc(strlen(desc) + 1, M_BUS, M_INTWAIT);
		if (dev->desc) {
			strcpy(dev->desc, desc);
			dev->flags |= DF_DESCMALLOCED;
		}
	} else {
		/* Avoid a -Wcast-qual warning */
		dev->desc = (char *)(uintptr_t) desc;
	}

	bus_data_generation_update();
}

void
device_set_desc(device_t dev, const char* desc)
{
	device_set_desc_internal(dev, desc, FALSE);
}

void
device_set_desc_copy(device_t dev, const char* desc)
{
	device_set_desc_internal(dev, desc, TRUE);
}

void
device_set_flags(device_t dev, uint32_t flags)
{
	dev->devflags = flags;
}

void *
device_get_softc(device_t dev)
{
	return dev->softc;
}

void
device_set_softc(device_t dev, void *softc)
{
	if (dev->softc && !(dev->flags & DF_EXTERNALSOFTC))
		kfree(dev->softc, M_BUS);
	dev->softc = softc;
	if (dev->softc)
		dev->flags |= DF_EXTERNALSOFTC;
	else
		dev->flags &= ~DF_EXTERNALSOFTC;
}

void
device_set_async_attach(device_t dev, int enable)
{
	if (enable)
		dev->flags |= DF_ASYNCPROBE;
	else
		dev->flags &= ~DF_ASYNCPROBE;
}

void *
device_get_ivars(device_t dev)
{
	return dev->ivars;
}

void
device_set_ivars(device_t dev, void * ivars)
{
	if (!dev)
		return;

	dev->ivars = ivars;
}

device_state_t
device_get_state(device_t dev)
{
	return(dev->state);
}

void
device_enable(device_t dev)
{
	dev->flags |= DF_ENABLED;
}

void
device_disable(device_t dev)
{
	dev->flags &= ~DF_ENABLED;
}

/*
 * YYY cannot block
 */
void
device_busy(device_t dev)
{
	if (dev->state < DS_ATTACHED)
		panic("device_busy: called for unattached device");
	if (dev->busy == 0 && dev->parent)
		device_busy(dev->parent);
	dev->busy++;
	dev->state = DS_BUSY;
}

/*
 * YYY cannot block
 */
void
device_unbusy(device_t dev)
{
	if (dev->state != DS_BUSY)
		panic("device_unbusy: called for non-busy device");
	dev->busy--;
	if (dev->busy == 0) {
		if (dev->parent)
			device_unbusy(dev->parent);
		dev->state = DS_ATTACHED;
	}
}

void
device_quiet(device_t dev)
{
	dev->flags |= DF_QUIET;
}

void
device_verbose(device_t dev)
{
	dev->flags &= ~DF_QUIET;
}

int
device_is_quiet(device_t dev)
{
	return((dev->flags & DF_QUIET) != 0);
}

int
device_is_enabled(device_t dev)
{
	return((dev->flags & DF_ENABLED) != 0);
}

int
device_is_alive(device_t dev)
{
	return(dev->state >= DS_ALIVE);
}

int
device_is_attached(device_t dev)
{
	return(dev->state >= DS_ATTACHED);
}

int
device_set_devclass(device_t dev, const char *classname)
{
	devclass_t dc;
	int error;

	if (!classname) {
		if (dev->devclass)
			devclass_delete_device(dev->devclass, dev);
		return(0);
	}

	if (dev->devclass) {
		kprintf("device_set_devclass: device class already set\n");
		return(EINVAL);
	}

	dc = devclass_find_internal(classname, NULL, TRUE);
	if (!dc)
		return(ENOMEM);

	error = devclass_add_device(dc, dev);

	bus_data_generation_update();
	return(error);
}

int
device_set_driver(device_t dev, driver_t *driver)
{
	if (dev->state >= DS_ATTACHED)
		return(EBUSY);

	if (dev->driver == driver)
		return(0);

	if (dev->softc && !(dev->flags & DF_EXTERNALSOFTC)) {
		kfree(dev->softc, M_BUS);
		dev->softc = NULL;
	}
	device_set_desc(dev, NULL);
	kobj_delete((kobj_t) dev, 0);
	dev->driver = driver;
	if (driver) {
		kobj_init((kobj_t) dev, (kobj_class_t) driver);
		if (!(dev->flags & DF_EXTERNALSOFTC))
			dev->softc = kmalloc(driver->size, M_BUS,
					    M_INTWAIT | M_ZERO);
	} else {
		kobj_init((kobj_t) dev, &null_class);
	}

	bus_data_generation_update();
	return(0);
}

int
device_probe_and_attach(device_t dev)
{
	device_t bus = dev->parent;
	int error = 0;

	if (dev->state >= DS_ALIVE)
		return(0);

	if ((dev->flags & DF_ENABLED) == 0) {
		if (bootverbose) {
			device_print_prettyname(dev);
			kprintf("not probed (disabled)\n");
		}
		return(0);
	}

	error = device_probe_child(bus, dev);
	if (error) {
		if (!(dev->flags & DF_DONENOMATCH)) {
			BUS_PROBE_NOMATCH(bus, dev);
			devnomatch(dev);
			dev->flags |= DF_DONENOMATCH;
		}
		return(error);
	}

	/*
	 * Output the exact device chain prior to the attach in case the  
	 * system locks up during attach, and generate the full info after
	 * the attach so correct irq and other information is displayed.
	 */
	if (bootverbose && !device_is_quiet(dev)) {
		device_t tmp;

		kprintf("%s", device_get_nameunit(dev));
		for (tmp = dev->parent; tmp; tmp = tmp->parent)
			kprintf(".%s", device_get_nameunit(tmp));
		kprintf("\n");
	}
	if (!device_is_quiet(dev))
		device_print_child(bus, dev);
	if ((dev->flags & DF_ASYNCPROBE) && do_async_attach) {
		kprintf("%s: probing asynchronously\n",
			device_get_nameunit(dev));
		dev->state = DS_INPROGRESS;
		device_attach_async(dev);
		error = 0;
	} else {
		error = device_doattach(dev);
	}
	return(error);
}

int
device_probe_and_attach_gpri(device_t dev, u_int gpri)
{
	device_t bus = dev->parent;
	int error = 0;

	if (dev->state >= DS_ALIVE)
		return(0);

	if ((dev->flags & DF_ENABLED) == 0) {
		if (bootverbose) {
			device_print_prettyname(dev);
			kprintf("not probed (disabled)\n");
		}
		return(0);
	}

	error = device_probe_child_gpri(bus, dev, gpri);
	if (error) {
#if 0
		if (!(dev->flags & DF_DONENOMATCH)) {
			BUS_PROBE_NOMATCH(bus, dev);
			devnomatch(dev);
			dev->flags |= DF_DONENOMATCH;
		}
#endif
		return(error);
	}

	/*
	 * Output the exact device chain prior to the attach in case the
	 * system locks up during attach, and generate the full info after
	 * the attach so correct irq and other information is displayed.
	 */
	if (bootverbose && !device_is_quiet(dev)) {
		device_t tmp;

		kprintf("%s", device_get_nameunit(dev));
		for (tmp = dev->parent; tmp; tmp = tmp->parent)
			kprintf(".%s", device_get_nameunit(tmp));
		kprintf("\n");
	}
	if (!device_is_quiet(dev))
		device_print_child(bus, dev);
	if ((dev->flags & DF_ASYNCPROBE) && do_async_attach) {
		kprintf("%s: probing asynchronously\n",
			device_get_nameunit(dev));
		dev->state = DS_INPROGRESS;
		device_attach_async(dev);
		error = 0;
	} else {
		error = device_doattach(dev);
	}
	return(error);
}

/*
 * Device is known to be alive, do the attach asynchronously.
 * However, serialize the attaches with the mp lock.
 */
static void
device_attach_async(device_t dev)
{
	thread_t td;

	atomic_add_int(&numasyncthreads, 1);
	lwkt_create(device_attach_thread, dev, &td, NULL,
		    0, 0, "%s", (dev->desc ? dev->desc : "devattach"));
}

static void
device_attach_thread(void *arg)
{
	device_t dev = arg;

	(void)device_doattach(dev);
	atomic_subtract_int(&numasyncthreads, 1);
	wakeup(&numasyncthreads);
}

/*
 * Device is known to be alive, do the attach (synchronous or asynchronous)
 */
static int
device_doattach(device_t dev)
{
	device_t bus = dev->parent;
	int hasclass = (dev->devclass != NULL);
	int error;

	device_sysctl_init(dev);
	error = DEVICE_ATTACH(dev);
	if (error == 0) {
		dev->state = DS_ATTACHED;
		if (bootverbose && !device_is_quiet(dev))
			device_print_child(bus, dev);
		device_sysctl_update(dev);
		devadded(dev);
	} else {
		kprintf("device_probe_and_attach: %s%d attach returned %d\n",
		       dev->driver->name, dev->unit, error);
		/* Unset the class that was set in device_probe_child */
		if (!hasclass)
			device_set_devclass(dev, 0);
		device_set_driver(dev, NULL);
		dev->state = DS_NOTPRESENT;
		device_sysctl_fini(dev);
	}
	return(error);
}

int
device_detach(device_t dev)
{
	int error;

	PDEBUG(("%s", DEVICENAME(dev)));
	if (dev->state == DS_BUSY)
		return(EBUSY);
	if (dev->state != DS_ATTACHED)
		return(0);

	if ((error = DEVICE_DETACH(dev)) != 0)
		return(error);
	devremoved(dev);
	device_printf(dev, "detached\n");
	if (dev->parent)
		BUS_CHILD_DETACHED(dev->parent, dev);

	if (!(dev->flags & DF_FIXEDCLASS))
		devclass_delete_device(dev->devclass, dev);

	dev->state = DS_NOTPRESENT;
	device_set_driver(dev, NULL);
	device_sysctl_fini(dev);

	return(0);
}

int
device_shutdown(device_t dev)
{
	if (dev->state < DS_ATTACHED)
		return 0;
	PDEBUG(("%s", DEVICENAME(dev)));
	return DEVICE_SHUTDOWN(dev);
}

int
device_set_unit(device_t dev, int unit)
{
	devclass_t dc;
	int err;

	dc = device_get_devclass(dev);
	if (unit < dc->maxunit && dc->devices[unit])
		return(EBUSY);
	err = devclass_delete_device(dc, dev);
	if (err)
		return(err);
	dev->unit = unit;
	err = devclass_add_device(dc, dev);
	if (err)
		return(err);

	bus_data_generation_update();
	return(0);
}

/*======================================*/
/*
 * Access functions for device resources.
 */

/* Supplied by config(8) in ioconf.c */
extern struct config_device config_devtab[];
extern int devtab_count;

/* Runtime version */
struct config_device *devtab = config_devtab;

static int
resource_new_name(const char *name, int unit)
{
	struct config_device *new;

	new = kmalloc((devtab_count + 1) * sizeof(*new), M_TEMP,
		     M_INTWAIT | M_ZERO);
	if (devtab && devtab_count > 0)
		bcopy(devtab, new, devtab_count * sizeof(*new));
	new[devtab_count].name = kmalloc(strlen(name) + 1, M_TEMP, M_INTWAIT);
	if (new[devtab_count].name == NULL) {
		kfree(new, M_TEMP);
		return(-1);
	}
	strcpy(new[devtab_count].name, name);
	new[devtab_count].unit = unit;
	new[devtab_count].resource_count = 0;
	new[devtab_count].resources = NULL;
	if (devtab && devtab != config_devtab)
		kfree(devtab, M_TEMP);
	devtab = new;
	return devtab_count++;
}

static int
resource_new_resname(int j, const char *resname, resource_type type)
{
	struct config_resource *new;
	int i;

	i = devtab[j].resource_count;
	new = kmalloc((i + 1) * sizeof(*new), M_TEMP, M_INTWAIT | M_ZERO);
	if (devtab[j].resources && i > 0)
		bcopy(devtab[j].resources, new, i * sizeof(*new));
	new[i].name = kmalloc(strlen(resname) + 1, M_TEMP, M_INTWAIT);
	if (new[i].name == NULL) {
		kfree(new, M_TEMP);
		return(-1);
	}
	strcpy(new[i].name, resname);
	new[i].type = type;
	if (devtab[j].resources)
		kfree(devtab[j].resources, M_TEMP);
	devtab[j].resources = new;
	devtab[j].resource_count = i + 1;
	return(i);
}

static int
resource_match_string(int i, const char *resname, const char *value)
{
	int j;
	struct config_resource *res;

	for (j = 0, res = devtab[i].resources;
	     j < devtab[i].resource_count; j++, res++)
		if (!strcmp(res->name, resname)
		    && res->type == RES_STRING
		    && !strcmp(res->u.stringval, value))
			return(j);
	return(-1);
}

static int
resource_find(const char *name, int unit, const char *resname, 
	      struct config_resource **result)
{
	int i, j;
	struct config_resource *res;

	/*
	 * First check specific instances, then generic.
	 */
	for (i = 0; i < devtab_count; i++) {
		if (devtab[i].unit < 0)
			continue;
		if (!strcmp(devtab[i].name, name) && devtab[i].unit == unit) {
			res = devtab[i].resources;
			for (j = 0; j < devtab[i].resource_count; j++, res++)
				if (!strcmp(res->name, resname)) {
					*result = res;
					return(0);
				}
		}
	}
	for (i = 0; i < devtab_count; i++) {
		if (devtab[i].unit >= 0)
			continue;
		/* XXX should this `&& devtab[i].unit == unit' be here? */
		/* XXX if so, then the generic match does nothing */
		if (!strcmp(devtab[i].name, name) && devtab[i].unit == unit) {
			res = devtab[i].resources;
			for (j = 0; j < devtab[i].resource_count; j++, res++)
				if (!strcmp(res->name, resname)) {
					*result = res;
					return(0);
				}
		}
	}
	return(ENOENT);
}

static int
resource_kenv(const char *name, int unit, const char *resname, long *result)
{
	const char *env;
	char buf[64];

	/*
	 * DragonFly style loader.conf hinting
	 */
	ksnprintf(buf, sizeof(buf), "%s%d.%s", name, unit, resname);
	if ((env = kgetenv(buf)) != NULL) {
		*result = strtol(env, NULL, 0);
		return(0);
	}

	/*
	 * Also support FreeBSD style loader.conf hinting
	 */
	ksnprintf(buf, sizeof(buf), "hint.%s.%d.%s", name, unit, resname);
	if ((env = kgetenv(buf)) != NULL) {
		*result = strtol(env, NULL, 0);
		return(0);
	}

	return (ENOENT);
}

int
resource_int_value(const char *name, int unit, const char *resname, int *result)
{
	struct config_resource *res;
	long kvalue = 0;
	int error;

	if (resource_kenv(name, unit, resname, &kvalue) == 0) {
		*result = (int)kvalue;
		return 0;
	}
	if ((error = resource_find(name, unit, resname, &res)) != 0)
		return(error);
	if (res->type != RES_INT)
		return(EFTYPE);
	*result = res->u.intval;
	return(0);
}

int
resource_long_value(const char *name, int unit, const char *resname,
		    long *result)
{
	struct config_resource *res;
	long kvalue;
	int error;

	if (resource_kenv(name, unit, resname, &kvalue) == 0) {
		*result = kvalue;
		return 0;
	}
	if ((error = resource_find(name, unit, resname, &res)) != 0)
		return(error);
	if (res->type != RES_LONG)
		return(EFTYPE);
	*result = res->u.longval;
	return(0);
}

int
resource_string_value(const char *name, int unit, const char *resname,
    const char **result)
{
	int error;
	struct config_resource *res;
	char buf[64];
	const char *env;

	/*
	 * DragonFly style loader.conf hinting
	 */
	ksnprintf(buf, sizeof(buf), "%s%d.%s", name, unit, resname);
	if ((env = kgetenv(buf)) != NULL) {
		*result = env;
		return 0;
	}

	/*
	 * Also support FreeBSD style loader.conf hinting
	 */
	ksnprintf(buf, sizeof(buf), "hint.%s.%d.%s", name, unit, resname);
	if ((env = kgetenv(buf)) != NULL) {
		*result = env;
		return 0;
	}

	if ((error = resource_find(name, unit, resname, &res)) != 0)
		return(error);
	if (res->type != RES_STRING)
		return(EFTYPE);
	*result = res->u.stringval;
	return(0);
}

int
resource_query_string(int i, const char *resname, const char *value)
{
	if (i < 0)
		i = 0;
	else
		i = i + 1;
	for (; i < devtab_count; i++)
		if (resource_match_string(i, resname, value) >= 0)
			return(i);
	return(-1);
}

int
resource_locate(int i, const char *resname)
{
	if (i < 0)
		i = 0;
	else
		i = i + 1;
	for (; i < devtab_count; i++)
		if (!strcmp(devtab[i].name, resname))
			return(i);
	return(-1);
}

int
resource_count(void)
{
	return(devtab_count);
}

char *
resource_query_name(int i)
{
	return(devtab[i].name);
}

int
resource_query_unit(int i)
{
	return(devtab[i].unit);
}

static int
resource_create(const char *name, int unit, const char *resname,
		resource_type type, struct config_resource **result)
{
	int i, j;
	struct config_resource *res = NULL;

	for (i = 0; i < devtab_count; i++)
		if (!strcmp(devtab[i].name, name) && devtab[i].unit == unit) {
			res = devtab[i].resources;
			break;
		}
	if (res == NULL) {
		i = resource_new_name(name, unit);
		if (i < 0)
			return(ENOMEM);
		res = devtab[i].resources;
	}
	for (j = 0; j < devtab[i].resource_count; j++, res++)
		if (!strcmp(res->name, resname)) {
			*result = res;
			return(0);
		}
	j = resource_new_resname(i, resname, type);
	if (j < 0)
		return(ENOMEM);
	res = &devtab[i].resources[j];
	*result = res;
	return(0);
}

int
resource_set_int(const char *name, int unit, const char *resname, int value)
{
	int error;
	struct config_resource *res;

	error = resource_create(name, unit, resname, RES_INT, &res);
	if (error)
		return(error);
	if (res->type != RES_INT)
		return(EFTYPE);
	res->u.intval = value;
	return(0);
}

int
resource_set_long(const char *name, int unit, const char *resname, long value)
{
	int error;
	struct config_resource *res;

	error = resource_create(name, unit, resname, RES_LONG, &res);
	if (error)
		return(error);
	if (res->type != RES_LONG)
		return(EFTYPE);
	res->u.longval = value;
	return(0);
}

int
resource_set_string(const char *name, int unit, const char *resname,
		    const char *value)
{
	int error;
	struct config_resource *res;

	error = resource_create(name, unit, resname, RES_STRING, &res);
	if (error)
		return(error);
	if (res->type != RES_STRING)
		return(EFTYPE);
	if (res->u.stringval)
		kfree(res->u.stringval, M_TEMP);
	res->u.stringval = kmalloc(strlen(value) + 1, M_TEMP, M_INTWAIT);
	if (res->u.stringval == NULL)
		return(ENOMEM);
	strcpy(res->u.stringval, value);
	return(0);
}

static void
resource_cfgload(void *dummy __unused)
{
	struct config_resource *res, *cfgres;
	int i, j;
	int error;
	char *name, *resname;
	int unit;
	resource_type type;
	char *stringval;
	int config_devtab_count;

	config_devtab_count = devtab_count;
	devtab = NULL;
	devtab_count = 0;

	for (i = 0; i < config_devtab_count; i++) {
		name = config_devtab[i].name;
		unit = config_devtab[i].unit;

		for (j = 0; j < config_devtab[i].resource_count; j++) {
			cfgres = config_devtab[i].resources;
			resname = cfgres[j].name;
			type = cfgres[j].type;
			error = resource_create(name, unit, resname, type,
						&res);
			if (error) {
				kprintf("create resource %s%d: error %d\n",
					name, unit, error);
				continue;
			}
			if (res->type != type) {
				kprintf("type mismatch %s%d: %d != %d\n",
					name, unit, res->type, type);
				continue;
			}
			switch (type) {
			case RES_INT:
				res->u.intval = cfgres[j].u.intval;
				break;
			case RES_LONG:
				res->u.longval = cfgres[j].u.longval;
				break;
			case RES_STRING:
				if (res->u.stringval)
					kfree(res->u.stringval, M_TEMP);
				stringval = cfgres[j].u.stringval;
				res->u.stringval = kmalloc(strlen(stringval) + 1,
							  M_TEMP, M_INTWAIT);
				if (res->u.stringval == NULL)
					break;
				strcpy(res->u.stringval, stringval);
				break;
			default:
				panic("unknown resource type %d", type);
			}
		}
	}
}
SYSINIT(cfgload, SI_BOOT1_POST, SI_ORDER_ANY + 50, resource_cfgload, 0);


/*======================================*/
/*
 * Some useful method implementations to make life easier for bus drivers.
 */

void
resource_list_init(struct resource_list *rl)
{
	SLIST_INIT(rl);
}

void
resource_list_free(struct resource_list *rl)
{
	struct resource_list_entry *rle;

	while ((rle = SLIST_FIRST(rl)) != NULL) {
		if (rle->res)
			panic("resource_list_free: resource entry is busy");
		SLIST_REMOVE_HEAD(rl, link);
		kfree(rle, M_BUS);
	}
}

void
resource_list_add(struct resource_list *rl, int type, int rid,
    u_long start, u_long end, u_long count, int cpuid)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(rl, type, rid);
	if (rle == NULL) {
		rle = kmalloc(sizeof(struct resource_list_entry), M_BUS,
			     M_INTWAIT);
		SLIST_INSERT_HEAD(rl, rle, link);
		rle->type = type;
		rle->rid = rid;
		rle->res = NULL;
		rle->cpuid = -1;
	}

	if (rle->res)
		panic("resource_list_add: resource entry is busy");

	rle->start = start;
	rle->end = end;
	rle->count = count;

	if (cpuid != -1) {
		if (rle->cpuid != -1 && rle->cpuid != cpuid) {
			panic("resource_list_add: moving from cpu%d -> cpu%d",
			    rle->cpuid, cpuid);
		}
		rle->cpuid = cpuid;
	}
}

struct resource_list_entry*
resource_list_find(struct resource_list *rl,
		   int type, int rid)
{
	struct resource_list_entry *rle;

	SLIST_FOREACH(rle, rl, link)
		if (rle->type == type && rle->rid == rid)
			return(rle);
	return(NULL);
}

void
resource_list_delete(struct resource_list *rl,
		     int type, int rid)
{
	struct resource_list_entry *rle = resource_list_find(rl, type, rid);

	if (rle) {
		if (rle->res != NULL)
			panic("resource_list_delete: resource has not been released");
		SLIST_REMOVE(rl, rle, resource_list_entry, link);
		kfree(rle, M_BUS);
	}
}

struct resource *
resource_list_alloc(struct resource_list *rl,
		    device_t bus, device_t child,
		    int type, int *rid,
		    u_long start, u_long end,
		    u_long count, u_int flags, int cpuid)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);
	int isdefault = (start == 0UL && end == ~0UL);

	if (passthrough) {
		return(BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
					  type, rid,
					  start, end, count, flags, cpuid));
	}

	rle = resource_list_find(rl, type, *rid);

	if (!rle)
		return(0);		/* no resource of that type/rid */

	if (rle->res)
		panic("resource_list_alloc: resource entry is busy");

	if (isdefault) {
		start = rle->start;
		count = ulmax(count, rle->count);
		end = ulmax(rle->end, start + count - 1);
	}
	cpuid = rle->cpuid;

	rle->res = BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
				      type, rid, start, end, count,
				      flags, cpuid);

	/*
	 * Record the new range.
	 */
	if (rle->res) {
		rle->start = rman_get_start(rle->res);
		rle->end = rman_get_end(rle->res);
		rle->count = count;
	}

	return(rle->res);
}

int
resource_list_release(struct resource_list *rl,
		      device_t bus, device_t child,
		      int type, int rid, struct resource *res)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);
	int error;

	if (passthrough) {
		return(BUS_RELEASE_RESOURCE(device_get_parent(bus), child,
					    type, rid, res));
	}

	rle = resource_list_find(rl, type, rid);

	if (!rle)
		panic("resource_list_release: can't find resource");
	if (!rle->res)
		panic("resource_list_release: resource entry is not busy");

	error = BUS_RELEASE_RESOURCE(device_get_parent(bus), child,
				     type, rid, res);
	if (error)
		return(error);

	rle->res = NULL;
	return(0);
}

int
resource_list_print_type(struct resource_list *rl, const char *name, int type,
			 const char *format)
{
	struct resource_list_entry *rle;
	int printed, retval;

	printed = 0;
	retval = 0;
	/* Yes, this is kinda cheating */
	SLIST_FOREACH(rle, rl, link) {
		if (rle->type == type) {
			if (printed == 0)
				retval += kprintf(" %s ", name);
			else
				retval += kprintf(",");
			printed++;
			retval += kprintf(format, rle->start);
			if (rle->count > 1) {
				retval += kprintf("-");
				retval += kprintf(format, rle->start +
						 rle->count - 1);
			}
		}
	}
	return(retval);
}

/*
 * Generic driver/device identify functions.  These will install a device
 * rendezvous point under the parent using the same name as the driver
 * name, which will at a later time be probed and attached.
 *
 * These functions are used when the parent does not 'scan' its bus for
 * matching devices, or for the particular devices using these functions,
 * or when the device is a pseudo or synthesized device (such as can be
 * found under firewire and ppbus).
 */
int
bus_generic_identify(driver_t *driver, device_t parent)
{
	if (parent->state == DS_ATTACHED)
		return (0);
	BUS_ADD_CHILD(parent, parent, 0, driver->name, -1);
	return (0);
}

int
bus_generic_identify_sameunit(driver_t *driver, device_t parent)
{
	if (parent->state == DS_ATTACHED)
		return (0);
	BUS_ADD_CHILD(parent, parent, 0, driver->name, device_get_unit(parent));
	return (0);
}

/*
 * Call DEVICE_IDENTIFY for each driver.
 */
int
bus_generic_probe(device_t dev)
{
	devclass_t dc = dev->devclass;
	driverlink_t dl;

	TAILQ_FOREACH(dl, &dc->drivers, link) {
		DEVICE_IDENTIFY(dl->driver, dev);
	}

	return(0);
}

/*
 * This is an aweful hack due to the isa bus and autoconf code not
 * probing the ISA devices until after everything else has configured.
 * The ISA bus did a dummy attach long ago so we have to set it back
 * to an earlier state so the probe thinks its the initial probe and
 * not a bus rescan.
 *
 * XXX remove by properly defering the ISA bus scan.
 */
int
bus_generic_probe_hack(device_t dev)
{
	if (dev->state == DS_ATTACHED) {
		dev->state = DS_ALIVE;
		bus_generic_probe(dev);
		dev->state = DS_ATTACHED;
	}
	return (0);
}

int
bus_generic_attach(device_t dev)
{
	device_t child;

	TAILQ_FOREACH(child, &dev->children, link) {
		device_probe_and_attach(child);
	}

	return(0);
}

int
bus_generic_attach_gpri(device_t dev, u_int gpri)
{
	device_t child;

	TAILQ_FOREACH(child, &dev->children, link) {
		device_probe_and_attach_gpri(child, gpri);
	}

	return(0);
}

int
bus_generic_detach(device_t dev)
{
	device_t child;
	int error;

	if (dev->state != DS_ATTACHED)
		return(EBUSY);

	TAILQ_FOREACH(child, &dev->children, link)
		if ((error = device_detach(child)) != 0)
			return(error);

	return 0;
}

int
bus_generic_shutdown(device_t dev)
{
	device_t child;

	TAILQ_FOREACH(child, &dev->children, link)
		device_shutdown(child);

	return(0);
}

int
bus_generic_suspend(device_t dev)
{
	int error;
	device_t child, child2;

	TAILQ_FOREACH(child, &dev->children, link) {
		error = DEVICE_SUSPEND(child);
		if (error) {
			for (child2 = TAILQ_FIRST(&dev->children);
			     child2 && child2 != child; 
			     child2 = TAILQ_NEXT(child2, link))
				DEVICE_RESUME(child2);
			return(error);
		}
	}
	return(0);
}

int
bus_generic_resume(device_t dev)
{
	device_t child;

	TAILQ_FOREACH(child, &dev->children, link)
		DEVICE_RESUME(child);
		/* if resume fails, there's nothing we can usefully do... */

	return(0);
}

int
bus_print_child_header(device_t dev, device_t child)
{
	int retval = 0;

	if (device_get_desc(child))
		retval += device_printf(child, "<%s>", device_get_desc(child));
	else
		retval += kprintf("%s", device_get_nameunit(child));
	if (bootverbose) {
		if (child->state != DS_ATTACHED)
			kprintf(" [tentative]");
		else
			kprintf(" [attached!]");
	}
	return(retval);
}

int
bus_print_child_footer(device_t dev, device_t child)
{
	return(kprintf(" on %s\n", device_get_nameunit(dev)));
}

device_t
bus_generic_add_child(device_t dev, device_t child, int order,
		      const char *name, int unit)
{
	if (dev->parent)
		dev = BUS_ADD_CHILD(dev->parent, child, order, name, unit);
	else
		dev = device_add_child_ordered(child, order, name, unit);
	return(dev);
		
}

int
bus_generic_print_child(device_t dev, device_t child)
{
	int retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += bus_print_child_footer(dev, child);

	return(retval);
}

int
bus_generic_read_ivar(device_t dev, device_t child, int index, 
		      uintptr_t * result)
{
	int error;

	if (dev->parent)
		error = BUS_READ_IVAR(dev->parent, child, index, result);
	else
		error = ENOENT;
	return (error);
}

int
bus_generic_write_ivar(device_t dev, device_t child, int index, 
		       uintptr_t value)
{
	int error;

	if (dev->parent)
		error = BUS_WRITE_IVAR(dev->parent, child, index, value);
	else
		error = ENOENT;
	return (error);
}

/*
 * Resource list are used for iterations, do not recurse.
 */
struct resource_list *
bus_generic_get_resource_list(device_t dev, device_t child)
{
	return (NULL);
}

void
bus_generic_driver_added(device_t dev, driver_t *driver)
{
	device_t child;

	DEVICE_IDENTIFY(driver, dev);
	TAILQ_FOREACH(child, &dev->children, link) {
		if (child->state == DS_NOTPRESENT)
			device_probe_and_attach(child);
	}
}

int
bus_generic_setup_intr(device_t dev, device_t child, struct resource *irq,
    int flags, driver_intr_t *intr, void *arg, void **cookiep,
    lwkt_serialize_t serializer, const char *desc)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent) {
		return BUS_SETUP_INTR(dev->parent, child, irq, flags,
		    intr, arg, cookiep, serializer, desc);
	} else {
		return EINVAL;
	}
}

int
bus_generic_teardown_intr(device_t dev, device_t child, struct resource *irq,
			  void *cookie)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_TEARDOWN_INTR(dev->parent, child, irq, cookie));
	else
		return(EINVAL);
}

int
bus_generic_disable_intr(device_t dev, device_t child, void *cookie)
{
	if (dev->parent)
		return(BUS_DISABLE_INTR(dev->parent, child, cookie));
	else
		return(0);
}

void
bus_generic_enable_intr(device_t dev, device_t child, void *cookie)
{
	if (dev->parent)
		BUS_ENABLE_INTR(dev->parent, child, cookie);
}

int
bus_generic_config_intr(device_t dev, device_t child, int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_CONFIG_INTR(dev->parent, child, irq, trig, pol));
	else
		return(EINVAL);
}

struct resource *
bus_generic_alloc_resource(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags, int cpuid)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_ALLOC_RESOURCE(dev->parent, child, type, rid, 
					   start, end, count, flags, cpuid));
	else
		return(NULL);
}

int
bus_generic_release_resource(device_t dev, device_t child, int type, int rid,
			     struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_RELEASE_RESOURCE(dev->parent, child, type, rid, r));
	else
		return(EINVAL);
}

int
bus_generic_activate_resource(device_t dev, device_t child, int type, int rid,
			      struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_ACTIVATE_RESOURCE(dev->parent, child, type, rid, r));
	else
		return(EINVAL);
}

int
bus_generic_deactivate_resource(device_t dev, device_t child, int type,
				int rid, struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return(BUS_DEACTIVATE_RESOURCE(dev->parent, child, type, rid,
					       r));
	else
		return(EINVAL);
}

int
bus_generic_get_resource(device_t dev, device_t child, int type, int rid,
			 u_long *startp, u_long *countp)
{
	int error;

	error = ENOENT;
	if (dev->parent) {
		error = BUS_GET_RESOURCE(dev->parent, child, type, rid, 
					 startp, countp);
	}
	return (error);
}

int
bus_generic_set_resource(device_t dev, device_t child, int type, int rid,
			u_long start, u_long count, int cpuid)
{
	int error;

	error = EINVAL;
	if (dev->parent) {
		error = BUS_SET_RESOURCE(dev->parent, child, type, rid, 
					 start, count, cpuid);
	}
	return (error);
}

void
bus_generic_delete_resource(device_t dev, device_t child, int type, int rid)
{
	if (dev->parent)
		BUS_DELETE_RESOURCE(dev, child, type, rid);
}

/**
 * @brief Helper function for implementing BUS_GET_DMA_TAG().
 *
 * This simple implementation of BUS_GET_DMA_TAG() simply calls the
 * BUS_GET_DMA_TAG() method of the parent of @p dev.
 */
bus_dma_tag_t
bus_generic_get_dma_tag(device_t dev, device_t child)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent != NULL)
		return (BUS_GET_DMA_TAG(dev->parent, child));
	return (NULL);
}

int
bus_generic_rl_get_resource(device_t dev, device_t child, int type, int rid,
    u_long *startp, u_long *countp)
{
	struct resource_list *rl = NULL;
	struct resource_list_entry *rle = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return(EINVAL);

	rle = resource_list_find(rl, type, rid);
	if (!rle)
		return(ENOENT);

	if (startp)
		*startp = rle->start;
	if (countp)
		*countp = rle->count;

	return(0);
}

int
bus_generic_rl_set_resource(device_t dev, device_t child, int type, int rid,
    u_long start, u_long count, int cpuid)
{
	struct resource_list *rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return(EINVAL);

	resource_list_add(rl, type, rid, start, (start + count - 1), count,
	    cpuid);

	return(0);
}

void
bus_generic_rl_delete_resource(device_t dev, device_t child, int type, int rid)
{
	struct resource_list *rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return;

	resource_list_delete(rl, type, rid);
}

int
bus_generic_rl_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct resource_list *rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return(EINVAL);

	return(resource_list_release(rl, dev, child, type, rid, r));
}

struct resource *
bus_generic_rl_alloc_resource(device_t dev, device_t child, int type,
    int *rid, u_long start, u_long end, u_long count, u_int flags, int cpuid)
{
	struct resource_list *rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return(NULL);

	return(resource_list_alloc(rl, dev, child, type, rid,
	    start, end, count, flags, cpuid));
}

int
bus_generic_child_present(device_t bus, device_t child)
{
	return(BUS_CHILD_PRESENT(device_get_parent(bus), bus));
}


/*
 * Some convenience functions to make it easier for drivers to use the
 * resource-management functions.  All these really do is hide the
 * indirection through the parent's method table, making for slightly
 * less-wordy code.  In the future, it might make sense for this code
 * to maintain some sort of a list of resources allocated by each device.
 */
int
bus_alloc_resources(device_t dev, struct resource_spec *rs,
    struct resource **res)
{
	int i;

	for (i = 0; rs[i].type != -1; i++)
	        res[i] = NULL;
	for (i = 0; rs[i].type != -1; i++) {
		res[i] = bus_alloc_resource_any(dev,
		    rs[i].type, &rs[i].rid, rs[i].flags);
		if (res[i] == NULL) {
			bus_release_resources(dev, rs, res);
			return (ENXIO);
		}
	}
	return (0);
}

void
bus_release_resources(device_t dev, const struct resource_spec *rs,
    struct resource **res)
{
	int i;

	for (i = 0; rs[i].type != -1; i++)
		if (res[i] != NULL) {
			bus_release_resource(
			    dev, rs[i].type, rs[i].rid, res[i]);
			res[i] = NULL;
		}
}

struct resource *
bus_alloc_resource(device_t dev, int type, int *rid, u_long start, u_long end,
		   u_long count, u_int flags)
{
	if (dev->parent == NULL)
		return(0);
	return(BUS_ALLOC_RESOURCE(dev->parent, dev, type, rid, start, end,
				  count, flags, -1));
}

struct resource *
bus_alloc_legacy_irq_resource(device_t dev, int *rid, u_long irq, u_int flags)
{
	if (dev->parent == NULL)
		return(0);
	return BUS_ALLOC_RESOURCE(dev->parent, dev, SYS_RES_IRQ, rid,
	    irq, irq, 1, flags, machintr_legacy_intr_cpuid(irq));
}

int
bus_activate_resource(device_t dev, int type, int rid, struct resource *r)
{
	if (dev->parent == NULL)
		return(EINVAL);
	return(BUS_ACTIVATE_RESOURCE(dev->parent, dev, type, rid, r));
}

int
bus_deactivate_resource(device_t dev, int type, int rid, struct resource *r)
{
	if (dev->parent == NULL)
		return(EINVAL);
	return(BUS_DEACTIVATE_RESOURCE(dev->parent, dev, type, rid, r));
}

int
bus_release_resource(device_t dev, int type, int rid, struct resource *r)
{
	if (dev->parent == NULL)
		return(EINVAL);
	return(BUS_RELEASE_RESOURCE(dev->parent, dev, type, rid, r));
}

int
bus_setup_intr_descr(device_t dev, struct resource *r, int flags,
    driver_intr_t handler, void *arg, void **cookiep,
    lwkt_serialize_t serializer, const char *desc)
{
	if (dev->parent == NULL)
		return EINVAL;
	return BUS_SETUP_INTR(dev->parent, dev, r, flags, handler, arg,
	    cookiep, serializer, desc);
}

int
bus_setup_intr(device_t dev, struct resource *r, int flags,
    driver_intr_t handler, void *arg, void **cookiep,
    lwkt_serialize_t serializer)
{
	return bus_setup_intr_descr(dev, r, flags, handler, arg, cookiep,
	    serializer, NULL);
}

int
bus_teardown_intr(device_t dev, struct resource *r, void *cookie)
{
	if (dev->parent == NULL)
		return(EINVAL);
	return(BUS_TEARDOWN_INTR(dev->parent, dev, r, cookie));
}

void
bus_enable_intr(device_t dev, void *cookie)
{
	if (dev->parent)
		BUS_ENABLE_INTR(dev->parent, dev, cookie);
}

int
bus_disable_intr(device_t dev, void *cookie)
{
	if (dev->parent)
		return(BUS_DISABLE_INTR(dev->parent, dev, cookie));
	else
		return(0);
}

int
bus_set_resource(device_t dev, int type, int rid,
		 u_long start, u_long count, int cpuid)
{
	return(BUS_SET_RESOURCE(device_get_parent(dev), dev, type, rid,
				start, count, cpuid));
}

int
bus_get_resource(device_t dev, int type, int rid,
		 u_long *startp, u_long *countp)
{
	return(BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
				startp, countp));
}

u_long
bus_get_resource_start(device_t dev, int type, int rid)
{
	u_long start, count;
	int error;

	error = BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
				 &start, &count);
	if (error)
		return(0);
	return(start);
}

u_long
bus_get_resource_count(device_t dev, int type, int rid)
{
	u_long start, count;
	int error;

	error = BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
				 &start, &count);
	if (error)
		return(0);
	return(count);
}

void
bus_delete_resource(device_t dev, int type, int rid)
{
	BUS_DELETE_RESOURCE(device_get_parent(dev), dev, type, rid);
}

int
bus_child_present(device_t child)
{
	return (BUS_CHILD_PRESENT(device_get_parent(child), child));
}

int
bus_child_pnpinfo_str(device_t child, char *buf, size_t buflen)
{
	device_t parent;

	parent = device_get_parent(child);
	if (parent == NULL) {
		*buf = '\0';
		return (0);
	}
	return (BUS_CHILD_PNPINFO_STR(parent, child, buf, buflen));
}

int
bus_child_location_str(device_t child, char *buf, size_t buflen)
{
	device_t parent;

	parent = device_get_parent(child);
	if (parent == NULL) {
		*buf = '\0';
		return (0);
	}
	return (BUS_CHILD_LOCATION_STR(parent, child, buf, buflen));
}

/**
 * @brief Wrapper function for BUS_GET_DMA_TAG().
 *
 * This function simply calls the BUS_GET_DMA_TAG() method of the
 * parent of @p dev.
 */
bus_dma_tag_t
bus_get_dma_tag(device_t dev)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return (NULL);
	return (BUS_GET_DMA_TAG(parent, dev));
}

static int
root_print_child(device_t dev, device_t child)
{
	return(0);
}

static int
root_setup_intr(device_t dev, device_t child, driver_intr_t *intr, void *arg,
		void **cookiep, lwkt_serialize_t serializer, const char *desc)
{
	/*
	 * If an interrupt mapping gets to here something bad has happened.
	 */
	panic("root_setup_intr");
}

/*
 * If we get here, assume that the device is permanant and really is
 * present in the system.  Removable bus drivers are expected to intercept
 * this call long before it gets here.  We return -1 so that drivers that
 * really care can check vs -1 or some ERRNO returned higher in the food
 * chain.
 */
static int
root_child_present(device_t dev, device_t child)
{
	return(-1);
}

/*
 * XXX NOTE! other defaults may be set in bus_if.m
 */
static kobj_method_t root_methods[] = {
	/* Device interface */
	KOBJMETHOD(device_shutdown,	bus_generic_shutdown),
	KOBJMETHOD(device_suspend,	bus_generic_suspend),
	KOBJMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	KOBJMETHOD(bus_add_child,	bus_generic_add_child),
	KOBJMETHOD(bus_print_child,	root_print_child),
	KOBJMETHOD(bus_read_ivar,	bus_generic_read_ivar),
	KOBJMETHOD(bus_write_ivar,	bus_generic_write_ivar),
	KOBJMETHOD(bus_setup_intr,	root_setup_intr),
	KOBJMETHOD(bus_child_present,   root_child_present),

	KOBJMETHOD_END
};

static driver_t root_driver = {
	"root",
	root_methods,
	1,			/* no softc */
};

device_t	root_bus;
devclass_t	root_devclass;

static int
root_bus_module_handler(module_t mod, int what, void* arg)
{
	switch (what) {
	case MOD_LOAD:
		TAILQ_INIT(&bus_data_devices);
		root_bus = make_device(NULL, "root", 0);
		root_bus->desc = "System root bus";
		kobj_init((kobj_t) root_bus, (kobj_class_t) &root_driver);
		root_bus->driver = &root_driver;
		root_bus->state = DS_ALIVE;
		root_devclass = devclass_find_internal("root", NULL, FALSE);
		devinit();
		return(0);

	case MOD_SHUTDOWN:
		device_shutdown(root_bus);
		return(0);
	default:
		return(0);
	}
}

static moduledata_t root_bus_mod = {
	"rootbus",
	root_bus_module_handler,
	0
};
DECLARE_MODULE(rootbus, root_bus_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);

void
root_bus_configure(void)
{
	int warncount;
	device_t dev;

	PDEBUG(("."));

	/*
	 * handle device_identify based device attachments to the root_bus
	 * (typically nexus).
	 */
	bus_generic_probe(root_bus);

	/*
	 * Probe and attach the devices under root_bus.
	 */
	TAILQ_FOREACH(dev, &root_bus->children, link) {
		device_probe_and_attach(dev);
	}

	/*
	 * Wait for all asynchronous attaches to complete.  If we don't
	 * our legacy ISA bus scan could steal device unit numbers or
	 * even I/O ports.
	 */
	warncount = 10;
	if (numasyncthreads)
		kprintf("Waiting for async drivers to attach\n");
	while (numasyncthreads > 0) {
		if (tsleep(&numasyncthreads, 0, "rootbus", hz) == EWOULDBLOCK)
			--warncount;
		if (warncount == 0) {
			kprintf("Warning: Still waiting for %d "
				"drivers to attach\n", numasyncthreads);
		} else if (warncount == -30) {
			kprintf("Giving up on %d drivers\n", numasyncthreads);
			break;
		}
	}
	root_bus->state = DS_ATTACHED;
}

int
driver_module_handler(module_t mod, int what, void *arg)
{
	int error;
	struct driver_module_data *dmd;
	devclass_t bus_devclass;
	kobj_class_t driver;
        const char *parentname;

	dmd = (struct driver_module_data *)arg;
	bus_devclass = devclass_find_internal(dmd->dmd_busname, NULL, TRUE);
	error = 0;

	switch (what) {
	case MOD_LOAD:
		if (dmd->dmd_chainevh)
			error = dmd->dmd_chainevh(mod,what,dmd->dmd_chainarg);

		driver = dmd->dmd_driver;
		PDEBUG(("Loading module: driver %s on bus %s",
		        DRIVERNAME(driver), dmd->dmd_busname));

		/*
		 * If the driver has any base classes, make the
		 * devclass inherit from the devclass of the driver's
		 * first base class. This will allow the system to
		 * search for drivers in both devclasses for children
		 * of a device using this driver.
		 */
		if (driver->baseclasses)
			parentname = driver->baseclasses[0]->name;
		else
			parentname = NULL;
		*dmd->dmd_devclass = devclass_find_internal(driver->name,
							    parentname, TRUE);

		error = devclass_add_driver(bus_devclass, driver);
		if (error)
			break;
		break;

	case MOD_UNLOAD:
		PDEBUG(("Unloading module: driver %s from bus %s",
			DRIVERNAME(dmd->dmd_driver), dmd->dmd_busname));
		error = devclass_delete_driver(bus_devclass, dmd->dmd_driver);

		if (!error && dmd->dmd_chainevh)
			error = dmd->dmd_chainevh(mod,what,dmd->dmd_chainarg);
		break;
	}

	return (error);
}

#ifdef BUS_DEBUG

/*
 * The _short versions avoid iteration by not calling anything that prints
 * more than oneliners. I love oneliners.
 */

static void
print_device_short(device_t dev, int indent)
{
	if (!dev)
		return;

	indentprintf(("device %d: <%s> %sparent,%schildren,%s%s%s%s,%sivars,%ssoftc,busy=%d\n",
		      dev->unit, dev->desc,
		      (dev->parent? "":"no "),
		      (TAILQ_EMPTY(&dev->children)? "no ":""),
		      (dev->flags&DF_ENABLED? "enabled,":"disabled,"),
		      (dev->flags&DF_FIXEDCLASS? "fixed,":""),
		      (dev->flags&DF_WILDCARD? "wildcard,":""),
		      (dev->flags&DF_DESCMALLOCED? "descmalloced,":""),
		      (dev->ivars? "":"no "),
		      (dev->softc? "":"no "),
		      dev->busy));
}

static void
print_device(device_t dev, int indent)
{
	if (!dev)
		return;

	print_device_short(dev, indent);

	indentprintf(("Parent:\n"));
	print_device_short(dev->parent, indent+1);
	indentprintf(("Driver:\n"));
	print_driver_short(dev->driver, indent+1);
	indentprintf(("Devclass:\n"));
	print_devclass_short(dev->devclass, indent+1);
}

/*
 * Print the device and all its children (indented).
 */
void
print_device_tree_short(device_t dev, int indent)
{
	device_t child;

	if (!dev)
		return;

	print_device_short(dev, indent);

	TAILQ_FOREACH(child, &dev->children, link)
		print_device_tree_short(child, indent+1);
}

/*
 * Print the device and all its children (indented).
 */
void
print_device_tree(device_t dev, int indent)
{
	device_t child;

	if (!dev)
		return;

	print_device(dev, indent);

	TAILQ_FOREACH(child, &dev->children, link)
		print_device_tree(child, indent+1);
}

static void
print_driver_short(driver_t *driver, int indent)
{
	if (!driver)
		return;

	indentprintf(("driver %s: softc size = %zu\n",
		      driver->name, driver->size));
}

static void
print_driver(driver_t *driver, int indent)
{
	if (!driver)
		return;

	print_driver_short(driver, indent);
}


static void
print_driver_list(driver_list_t drivers, int indent)
{
	driverlink_t driver;

	TAILQ_FOREACH(driver, &drivers, link)
		print_driver(driver->driver, indent);
}

static void
print_devclass_short(devclass_t dc, int indent)
{
	if (!dc)
		return;

	indentprintf(("devclass %s: max units = %d\n", dc->name, dc->maxunit));
}

static void
print_devclass(devclass_t dc, int indent)
{
	int i;

	if (!dc)
		return;

	print_devclass_short(dc, indent);
	indentprintf(("Drivers:\n"));
	print_driver_list(dc->drivers, indent+1);

	indentprintf(("Devices:\n"));
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i])
			print_device(dc->devices[i], indent+1);
}

void
print_devclass_list_short(void)
{
	devclass_t dc;

	kprintf("Short listing of devclasses, drivers & devices:\n");
	TAILQ_FOREACH(dc, &devclasses, link) {
		print_devclass_short(dc, 0);
	}
}

void
print_devclass_list(void)
{
	devclass_t dc;

	kprintf("Full listing of devclasses, drivers & devices:\n");
	TAILQ_FOREACH(dc, &devclasses, link) {
		print_devclass(dc, 0);
	}
}

#endif

/*
 * Check to see if a device is disabled via a disabled hint.
 */
int
resource_disabled(const char *name, int unit)
{
	int error, value;

	error = resource_int_value(name, unit, "disabled", &value);
	if (error)
	       return(0);
	return(value);
}

/*
 * User-space access to the device tree.
 *
 * We implement a small set of nodes:
 *
 * hw.bus			Single integer read method to obtain the
 *				current generation count.
 * hw.bus.devices		Reads the entire device tree in flat space.
 * hw.bus.rman			Resource manager interface
 *
 * We might like to add the ability to scan devclasses and/or drivers to
 * determine what else is currently loaded/available.
 */

static int
sysctl_bus(SYSCTL_HANDLER_ARGS)
{
	struct u_businfo	ubus;

	ubus.ub_version = BUS_USER_VERSION;
	ubus.ub_generation = bus_data_generation;

	return (SYSCTL_OUT(req, &ubus, sizeof(ubus)));
}
SYSCTL_NODE(_hw_bus, OID_AUTO, info, CTLFLAG_RW, sysctl_bus,
    "bus-related data");

static int
sysctl_devices(SYSCTL_HANDLER_ARGS)
{
	int			*name = (int *)arg1;
	u_int			namelen = arg2;
	int			index;
	device_t		dev;
	struct u_device		udev;	/* XXX this is a bit big */
	int			error;

	if (namelen != 2)
		return (EINVAL);

	if (bus_data_generation_check(name[0]))
		return (EINVAL);

	index = name[1];

	/*
	 * Scan the list of devices, looking for the requested index.
	 */
	TAILQ_FOREACH(dev, &bus_data_devices, devlink) {
		if (index-- == 0)
			break;
	}
	if (dev == NULL)
		return (ENOENT);

	/*
	 * Populate the return array.
	 */
	bzero(&udev, sizeof(udev));
	udev.dv_handle = (uintptr_t)dev;
	udev.dv_parent = (uintptr_t)dev->parent;
	if (dev->nameunit != NULL)
		strlcpy(udev.dv_name, dev->nameunit, sizeof(udev.dv_name));
	if (dev->desc != NULL)
		strlcpy(udev.dv_desc, dev->desc, sizeof(udev.dv_desc));
	if (dev->driver != NULL && dev->driver->name != NULL)
		strlcpy(udev.dv_drivername, dev->driver->name,
		    sizeof(udev.dv_drivername));
	bus_child_pnpinfo_str(dev, udev.dv_pnpinfo, sizeof(udev.dv_pnpinfo));
	bus_child_location_str(dev, udev.dv_location, sizeof(udev.dv_location));
	udev.dv_devflags = dev->devflags;
	udev.dv_flags = dev->flags;
	udev.dv_state = dev->state;
	error = SYSCTL_OUT(req, &udev, sizeof(udev));
	return (error);
}

SYSCTL_NODE(_hw_bus, OID_AUTO, devices, CTLFLAG_RD, sysctl_devices,
    "system device tree");

int
bus_data_generation_check(int generation)
{
	if (generation != bus_data_generation)
		return (1);

	/* XXX generate optimised lists here? */
	return (0);
}

void
bus_data_generation_update(void)
{
	bus_data_generation++;
}

const char *
intr_str_polarity(enum intr_polarity pola)
{
	switch (pola) {
	case INTR_POLARITY_LOW:
		return "low";

	case INTR_POLARITY_HIGH:
		return "high";

	case INTR_POLARITY_CONFORM:
		return "conform";
	}
	return "unknown";
}

const char *
intr_str_trigger(enum intr_trigger trig)
{
	switch (trig) {
	case INTR_TRIGGER_EDGE:
		return "edge";

	case INTR_TRIGGER_LEVEL:
		return "level";

	case INTR_TRIGGER_CONFORM:
		return "conform";
	}
	return "unknown";
}

int
device_getenv_int(device_t dev, const char *knob, int def)
{
	char env[128];

	/* Deprecated; for compat */
	ksnprintf(env, sizeof(env), "hw.%s.%s", device_get_nameunit(dev), knob);
	kgetenv_int(env, &def);

	/* Prefer dev.driver.unit.knob */
	ksnprintf(env, sizeof(env), "dev.%s.%d.%s",
	    device_get_name(dev), device_get_unit(dev), knob);
	kgetenv_int(env, &def);

	return def;
}

void
device_getenv_string(device_t dev, const char *knob, char * __restrict data,
    int dlen, const char * __restrict def)
{
	char env[128];

	strlcpy(data, def, dlen);

	/* Deprecated; for compat */
	ksnprintf(env, sizeof(env), "hw.%s.%s", device_get_nameunit(dev), knob);
	kgetenv_string(env, data, dlen);

	/* Prefer dev.driver.unit.knob */
	ksnprintf(env, sizeof(env), "dev.%s.%d.%s",
	    device_get_name(dev), device_get_unit(dev), knob);
	kgetenv_string(env, data, dlen);
}
