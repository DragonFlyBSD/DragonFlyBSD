/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/udev.h>
#include <sys/devfs.h>
#include <libprop/proplib.h>

#include <sys/thread2.h>

MALLOC_DEFINE(M_UDEV, "udev", "udev allocs");
static struct objcache *udev_event_kernel_cache;

/* XXX: use UUIDs for identification; would need help from devfs */

static cdev_t		udev_dev;
static d_open_t		udev_dev_open;
static d_close_t	udev_dev_close;
static d_read_t		udev_dev_read;
static d_kqfilter_t	udev_dev_kqfilter;
static d_ioctl_t	udev_dev_ioctl;
static d_clone_t	udev_dev_clone;

struct udev_prop_ctx {
	prop_array_t cdevs;
	int error;
};

struct udev_event_kernel {
	struct udev_event ev;
	TAILQ_ENTRY(udev_event_kernel)	link;
};

struct udev_softc {
	TAILQ_ENTRY(udev_softc) entry;
	int opened;
	int initiated;
	int unit;
	cdev_t dev;

	struct udev_event_kernel marker;	/* udev_evq marker */
};

struct cmd_function {
	const char *cmd;
	int  (*fn)(struct udev_softc *, struct plistref *,
		   u_long, prop_dictionary_t);
};


static int _udev_dict_set_cstr(prop_dictionary_t, const char *, char *);
static int _udev_dict_set_int(prop_dictionary_t, const char *, int64_t);
static int _udev_dict_set_uint(prop_dictionary_t, const char *, uint64_t);
static int _udev_dict_delete_key(prop_dictionary_t, const char *);
static prop_dictionary_t udev_init_dict_event(cdev_t, const char *);
static int udev_init_dict(cdev_t);
static int udev_destroy_dict(cdev_t);
static void udev_event_insert(int, prop_dictionary_t);
static void udev_clean_events_locked(void);
static char *udev_event_externalize(struct udev_event_kernel *);
static void udev_getdevs_scan_callback(char *, cdev_t, bool, void *);
static int udev_getdevs_ioctl(struct udev_softc *, struct plistref *,
					u_long, prop_dictionary_t);
static void udev_dev_filter_detach(struct knote *);
static int udev_dev_filter_read(struct knote *, long);

static struct dev_ops udev_dev_ops = {
	{ "udev", 0, 0 },
	.d_open = udev_dev_open,
	.d_close = udev_dev_close,
	.d_read = udev_dev_read,
	.d_kqfilter = udev_dev_kqfilter,
	.d_ioctl = udev_dev_ioctl
};

static struct cmd_function cmd_fn[] = {
		{ .cmd = "getdevs", .fn = udev_getdevs_ioctl},
		{NULL, NULL}
};

DEVFS_DECLARE_CLONE_BITMAP(udev);

static TAILQ_HEAD(, udev_softc) udevq;
static TAILQ_HEAD(, udev_event_kernel) udev_evq;
static struct kqinfo udev_kq;
static struct lock udev_lk;
static int udev_evqlen;
static int udev_initiated_count;
static int udev_open_count;
static int udev_seqwait;
static int udev_seq;

static int
_udev_dict_set_cstr(prop_dictionary_t dict, const char *key, char *str)
{
	prop_string_t	ps;

	KKASSERT(dict != NULL);

	ps = prop_string_create_cstring(str);
	if (ps == NULL) {
		return ENOMEM;
	}

	if (prop_dictionary_set(dict, key, ps) == false) {
		prop_object_release(ps);
		return ENOMEM;
	}

	prop_object_release(ps);
	return 0;
}

static int
_udev_dict_set_int(prop_dictionary_t dict, const char *key, int64_t val)
{
	prop_number_t	pn;

	KKASSERT(dict != NULL);

	pn = prop_number_create_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

static int
_udev_dict_set_uint(prop_dictionary_t dict, const char *key, uint64_t val)
{
	prop_number_t	pn;

	KKASSERT(dict != NULL);

	pn = prop_number_create_unsigned_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

static int
_udev_dict_delete_key(prop_dictionary_t dict, const char *key)
{
	KKASSERT(dict != NULL);

	prop_dictionary_remove(dict, key);

	return 0;
}

/*
 * Initialize an event dictionary, which contains three parameters to
 * identify the device referred to (name, devnum, kptr) and the affected key.
 */
static prop_dictionary_t
udev_init_dict_event(cdev_t dev, const char *key)
{
	prop_dictionary_t	dict;
	uint64_t	kptr;
	int error;

	kptr = (uint64_t)(uintptr_t)dev;
	KKASSERT(dev != NULL);

	dict = prop_dictionary_create();
	if (dict == NULL) {
		log(LOG_DEBUG, "udev_init_dict_event: prop_dictionary_create() failed\n");
		return NULL;
	}

	if ((error = _udev_dict_set_cstr(dict, "name", dev->si_name)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "devnum", dev->si_inode)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "devtype", (dev_dflags(dev) & D_TYPEMASK))))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "kptr", kptr)))
		goto error_out;
	if ((error = _udev_dict_set_cstr(dict, "key", __DECONST(char *, key))))
		goto error_out;

	return dict;

error_out:
	prop_object_release(dict);
	return NULL;
}

int
udev_dict_set_cstr(cdev_t dev, const char *key, char *str)
{
	prop_dictionary_t	dict;
	int error;

	KKASSERT(dev != NULL);

	if (dev->si_dict == NULL) {
		error = udev_init_dict(dev);
		if (error)
			return -1;
	}

	/* Queue a key update event */
	dict = udev_init_dict_event(dev, key);
	if (dict == NULL)
		return ENOMEM;

	if ((error = _udev_dict_set_cstr(dict, "value", str))) {
		prop_object_release(dict);
		return error;
	}
	udev_event_insert(UDEV_EV_KEY_UPDATE, dict);
	prop_object_release(dict);

	error = _udev_dict_set_cstr(dev->si_dict, key, str);
	return error;
}

int
udev_dict_set_int(cdev_t dev, const char *key, int64_t val)
{
	prop_dictionary_t	dict;
	int error;

	KKASSERT(dev != NULL);

	if (dev->si_dict == NULL) {
		error = udev_init_dict(dev);
		if (error)
			return -1;
	}

	/* Queue a key update event */
	dict = udev_init_dict_event(dev, key);
	if (dict == NULL)
		return ENOMEM;
	if ((error = _udev_dict_set_int(dict, "value", val))) {
		prop_object_release(dict);
		return error;
	}
	udev_event_insert(UDEV_EV_KEY_UPDATE, dict);
	prop_object_release(dict);

	return _udev_dict_set_int(dev->si_dict, key, val);
}

int
udev_dict_set_uint(cdev_t dev, const char *key, uint64_t val)
{
	prop_dictionary_t	dict;
	int error;

	KKASSERT(dev != NULL);

	if (dev->si_dict == NULL) {
		error = udev_init_dict(dev);
		if (error)
			return -1;
	}

	/* Queue a key update event */
	dict = udev_init_dict_event(dev, key);
	if (dict == NULL)
		return ENOMEM;
	if ((error = _udev_dict_set_uint(dict, "value", val))) {
		prop_object_release(dict);
		return error;
	}
	udev_event_insert(UDEV_EV_KEY_UPDATE, dict);
	prop_object_release(dict);

	return _udev_dict_set_uint(dev->si_dict, key, val);
}

int
udev_dict_delete_key(cdev_t dev, const char *key)
{
	prop_dictionary_t	dict;

	KKASSERT(dev != NULL);

	/* Queue a key removal event */
	dict = udev_init_dict_event(dev, key);
	if (dict == NULL)
		return ENOMEM;
	udev_event_insert(UDEV_EV_KEY_REMOVE, dict);
	prop_object_release(dict);

	return _udev_dict_delete_key(dev->si_dict, key);
}

static int
udev_init_dict(cdev_t dev)
{
	prop_dictionary_t dict;
	uint64_t	kptr;
	int error;

	kptr = (uint64_t)(uintptr_t)dev;

	KKASSERT(dev != NULL);

	if (dev->si_dict != NULL) {
#if 0
		log(LOG_DEBUG,
		    "udev_init_dict: new dict for %s, but has dict already (%p)!\n",
		    dev->si_name, dev->si_dict);
#endif
		return 0;
	}

	dict = prop_dictionary_create();
	if (dict == NULL) {
		log(LOG_DEBUG, "udev_init_dict: prop_dictionary_create() failed\n");
		return ENOMEM;
	}

	if ((error = _udev_dict_set_cstr(dict, "name", dev->si_name)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "devnum", dev->si_inode)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "kptr", kptr)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "devtype", (dev_dflags(dev) & D_TYPEMASK))))
		goto error_out;

	/* XXX: The next 3 are marginallly useful, if at all */
	if ((error = _udev_dict_set_uint(dict, "uid", dev->si_uid)))
		goto error_out;
	if ((error = _udev_dict_set_uint(dict, "gid", dev->si_gid)))
		goto error_out;
	if ((error = _udev_dict_set_int(dict, "mode", dev->si_perms)))
		goto error_out;

	if ((error = _udev_dict_set_int(dict, "major", dev->si_umajor)))
		goto error_out;
	if ((error = _udev_dict_set_int(dict, "minor", dev->si_uminor)))
		goto error_out;
	if (dev->si_ops->head.name != NULL) {
		if ((error = _udev_dict_set_cstr(dict, "driver",
		    __DECONST(char *, dev->si_ops->head.name))))
			goto error_out;
	}

	dev->si_dict = dict;
	return 0;

error_out:
	dev->si_dict = NULL;
	prop_object_release(dict);
	return error;
}

static int
udev_destroy_dict(cdev_t dev)
{
	KKASSERT(dev != NULL);

	if (dev->si_dict != NULL) {
		prop_object_release(dev->si_dict);
		dev->si_dict = NULL;
	}

	return 0;
}

static void
udev_event_insert(int ev_type, prop_dictionary_t dict)
{
	struct udev_event_kernel *ev;
	prop_dictionary_t dict_copy;

	/* Only start queing events after client has initiated properly */
	if (udev_initiated_count) {
		dict_copy = prop_dictionary_copy(dict);
		if (dict_copy == NULL)
			return;
		ev = objcache_get(udev_event_kernel_cache, M_WAITOK);
		ev->ev.ev_dict = dict_copy;
		ev->ev.ev_type = ev_type;

		lockmgr(&udev_lk, LK_EXCLUSIVE);
		TAILQ_INSERT_TAIL(&udev_evq, ev, link);
		++udev_evqlen;
		++udev_seq;
		if (udev_seqwait)
			wakeup(&udev_seqwait);
		lockmgr(&udev_lk, LK_RELEASE);
		wakeup(&udev_evq);
		KNOTE(&udev_kq.ki_note, 0);
	} else if (udev_open_count) {
		lockmgr(&udev_lk, LK_EXCLUSIVE);
		++udev_seq;
		if (udev_seqwait)
			wakeup(&udev_seqwait);
		lockmgr(&udev_lk, LK_RELEASE);
		KNOTE(&udev_kq.ki_note, 0);
	}
}

static void
udev_clean_events_locked(void)
{
	struct udev_event_kernel *ev;

	while ((ev = TAILQ_FIRST(&udev_evq)) &&
	       ev->ev.ev_dict != NULL) {
		TAILQ_REMOVE(&udev_evq, ev, link);
		objcache_put(udev_event_kernel_cache, ev);
		--udev_evqlen;
	}
}

static char *
udev_event_externalize(struct udev_event_kernel *ev)
{
	prop_dictionary_t	dict;
	char *xml;
	int error;

	dict = prop_dictionary_create();
	if (dict == NULL) {
		log(LOG_DEBUG,
		    "udev_event_externalize: prop_dictionary_create() failed\n");
		return NULL;
	}

	if ((error = _udev_dict_set_int(dict, "evtype", ev->ev.ev_type))) {
		prop_object_release(dict);
		return NULL;
	}

	if (prop_dictionary_set(dict, "evdict", ev->ev.ev_dict) == false) {
		prop_object_release(dict);
		return NULL;
	}

	prop_object_release(ev->ev.ev_dict);

	xml = prop_dictionary_externalize(dict);

	prop_object_release(dict);

	return xml;
}

int
udev_event_attach(cdev_t dev, char *name, int alias)
{
	prop_dictionary_t	dict;
	int error;

	KKASSERT(dev != NULL);

	error = ENOMEM;

	if (alias) {
		dict = prop_dictionary_copy(dev->si_dict);
		if (dict == NULL)
			goto error_out;

		if ((error = _udev_dict_set_cstr(dict, "name", name))) {
			prop_object_release(dict);
			goto error_out;
		}

		_udev_dict_set_int(dict, "alias", 1);

		udev_event_insert(UDEV_EVENT_ATTACH, dict);
		prop_object_release(dict);
	} else {
		error = udev_init_dict(dev);
		if (error)
			goto error_out;

		_udev_dict_set_int(dev->si_dict, "alias", 0);
		udev_event_insert(UDEV_EVENT_ATTACH, dev->si_dict);
	}

error_out:
	return error;
}

int
udev_event_detach(cdev_t dev, char *name, int alias)
{
	prop_dictionary_t	dict;

	KKASSERT(dev != NULL);

	if (alias) {
		dict = prop_dictionary_copy(dev->si_dict);
		if (dict == NULL)
			goto error_out;

		if (_udev_dict_set_cstr(dict, "name", name)) {
			prop_object_release(dict);
			goto error_out;
		}

		_udev_dict_set_int(dict, "alias", 1);

		udev_event_insert(UDEV_EVENT_DETACH, dict);
		prop_object_release(dict);
	} else {
		udev_event_insert(UDEV_EVENT_DETACH, dev->si_dict);
	}

error_out:
	udev_destroy_dict(dev);

	return 0;
}

/*
 * Allow multiple opens.  Each opener gets a different device.
 * Messages are replicated to all devices using a marker system.
 */
static int
udev_dev_clone(struct dev_clone_args *ap)
{
	struct udev_softc *softc;
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(udev), 1000);
	if (unit < 0) {
		ap->a_dev = NULL;
		return 1;
	}

	softc = kmalloc(sizeof(*softc), M_UDEV, M_WAITOK | M_ZERO);
	softc->unit = unit;
	lockmgr(&udev_lk, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&udevq, softc, entry);
	lockmgr(&udev_lk, LK_RELEASE);

	softc->dev = make_only_dev(&udev_dev_ops, unit, ap->a_cred->cr_ruid,
				   0, 0600, "udev/%d", unit);
	softc->dev->si_drv1 = softc;
	ap->a_dev = softc->dev;
	return 0;
}

/*
 * dev stuff
 */
static int
udev_dev_open(struct dev_open_args *ap)
{
	struct udev_softc *softc = ap->a_head.a_dev->si_drv1;

	lockmgr(&udev_lk, LK_EXCLUSIVE);
	if (softc == NULL || softc->opened) {
		lockmgr(&udev_lk, LK_RELEASE);
		return EBUSY;
	}
	softc->opened = 1;
	++udev_open_count;
	lockmgr(&udev_lk, LK_RELEASE);

	return 0;
}

static int
udev_dev_close(struct dev_close_args *ap)
{
	struct udev_softc *softc = ap->a_head.a_dev->si_drv1;

	KKASSERT(softc->dev == ap->a_head.a_dev);
	KKASSERT(softc->opened == 1);

	lockmgr(&udev_lk, LK_EXCLUSIVE);
	TAILQ_REMOVE(&udevq, softc, entry);
	devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(udev), softc->unit);

	if (softc->initiated) {
		TAILQ_REMOVE(&udev_evq, &softc->marker, link);
		softc->initiated = 0;
		--udev_initiated_count;
		udev_clean_events_locked();
	}
	softc->opened = 0;
	softc->dev = NULL;
	ap->a_head.a_dev->si_drv1 = NULL;
	--udev_open_count;
	lockmgr(&udev_lk, LK_RELEASE);

	destroy_dev(ap->a_head.a_dev);
	wakeup(&udev_evq);

	kfree(softc, M_UDEV);

	return 0;
}

static struct filterops udev_dev_read_filtops =
	{ FILTEROP_ISFD, NULL, udev_dev_filter_detach, udev_dev_filter_read };

static int
udev_dev_kqfilter(struct dev_kqfilter_args *ap)
{
	struct udev_softc *softc = ap->a_head.a_dev->si_drv1;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;
	lockmgr(&udev_lk, LK_EXCLUSIVE);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &udev_dev_read_filtops;
		kn->kn_hook = (caddr_t)softc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
	        lockmgr(&udev_lk, LK_RELEASE);
		return (0);
	}

	klist = &udev_kq.ki_note;
	knote_insert(klist, kn);

        lockmgr(&udev_lk, LK_RELEASE);

	return (0);
}

static void
udev_dev_filter_detach(struct knote *kn)
{
	struct klist *klist;

	lockmgr(&udev_lk, LK_EXCLUSIVE);
	klist = &udev_kq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&udev_lk, LK_RELEASE);
}

static int
udev_dev_filter_read(struct knote *kn, long hint)
{
	struct udev_softc *softc = (void *)kn->kn_hook;
	struct udev_event_kernel *ev;
	int ready = 0;

	lockmgr(&udev_lk, LK_EXCLUSIVE);
	if (softc->initiated) {
		ev = TAILQ_NEXT(&softc->marker, link);
		while (ev && ev->ev.ev_dict == NULL)
			ev = TAILQ_NEXT(ev, link);
		if (ev)
			ready = 1;
	}
	lockmgr(&udev_lk, LK_RELEASE);

	return (ready);
}

static int
udev_dev_read(struct dev_read_args *ap)
{
	struct udev_softc *softc = ap->a_head.a_dev->si_drv1;
	struct udev_event_kernel *ev;
	struct uio *uio = ap->a_uio;
	char	*xml;
	size_t	len;
	int	error;

	lockmgr(&udev_lk, LK_EXCLUSIVE);

	/*
	 * Automatically enable message collection if it has not already
	 * been enabled.
	 */
	if (softc->initiated == 0) {
		softc->initiated = 1;
		++udev_initiated_count;
		TAILQ_INSERT_HEAD(&udev_evq, &softc->marker, link);
	}

	/*
	 * Loop, sleep interruptably until we get an event or signal.
	 */
	error = 0;
	for (;;) {
		if (softc->initiated) {
			ev = TAILQ_NEXT(&softc->marker, link);
			while (ev && ev->ev.ev_dict == NULL)
				ev = TAILQ_NEXT(ev, link);
			if (ev) {
				if ((xml = udev_event_externalize(ev)) == NULL) {
					error = ENOMEM;
					break;
				}
				len = strlen(xml) + 1; /* include terminator */
				if (uio->uio_resid < len)
					error = ENOMEM;
				else
					error = uiomove((caddr_t)xml, len, uio);
				kfree(xml, M_TEMP);

				/*
				 * Move the marker
				 */
				TAILQ_REMOVE(&udev_evq, &softc->marker, link);
				TAILQ_INSERT_AFTER(&udev_evq,
						   ev, &softc->marker, link);
				udev_clean_events_locked();
				break;
			}
		}
		if (ap->a_ioflag & IO_NDELAY) {
			error = EWOULDBLOCK;
			break;
		}
		if ((error = lksleep(&udev_evq, &udev_lk, PCATCH, "udevq", 0)))
			break;
	}

	lockmgr(&udev_lk, LK_RELEASE);
	return error;
}

static int
udev_dev_ioctl(struct dev_ioctl_args *ap)
{
	struct udev_softc *softc = ap->a_head.a_dev->si_drv1;
	prop_dictionary_t dict;
	prop_object_t	po;
	prop_string_t	ps;
	struct plistref *pref;
	int i, error;
	int seq;

	error = 0;

	switch(ap->a_cmd) {
	case UDEVPROP:
		/* Use proplib(3) for userspace/kernel communication */
		pref = (struct plistref *)ap->a_data;
		error = prop_dictionary_copyin_ioctl(pref, ap->a_cmd, &dict);
		if (error)
			return error;

		po = prop_dictionary_get(dict, "command");
		if (po == NULL || prop_object_type(po) != PROP_TYPE_STRING) {
			log(LOG_DEBUG, "udev: prop_dictionary_get() failed\n");
			prop_object_release(dict);
			return EINVAL;
		}

		ps = po;
		/* Handle cmd */
		for(i = 0; cmd_fn[i].cmd != NULL; i++) {
			if (prop_string_equals_cstring(ps, cmd_fn[i].cmd))
				break;
		}

		if (cmd_fn[i].cmd != NULL) {
			error = cmd_fn[i].fn(softc, pref, ap->a_cmd, dict);
		} else {
			error = EINVAL;
		}

		//prop_object_release(po);
		prop_object_release(dict);
		break;
	case UDEVWAIT:
		/*
		 * Wait for events based on sequence number.  Updates
		 * sequence number for loop.
		 */
		lockmgr(&udev_lk, LK_EXCLUSIVE);
		seq = *(int *)ap->a_data;
		++udev_seqwait;
		while (seq == udev_seq) {
			error = lksleep(&udev_seqwait, &udev_lk,
					PCATCH, "udevw", 0);
			if (error)
				break;
		}
		--udev_seqwait;
		*(int *)ap->a_data = udev_seq;
		lockmgr(&udev_lk, LK_RELEASE);
		break;
	default:
		error = ENOTTY; /* Inappropriate ioctl for device */
		break;
	}

	return(error);
}

static void
udev_getdevs_scan_callback(char *name, cdev_t cdev, bool is_alias, void *arg)
{
	struct udev_prop_ctx *ctx = arg;

	KKASSERT(arg != NULL);

	if (cdev->si_dict == NULL)
		return;

	if (prop_array_add(ctx->cdevs, cdev->si_dict) == false) {
		ctx->error = EINVAL;
		return;
	}
}

static int
udev_getdevs_ioctl(struct udev_softc *softc, struct plistref *pref,
		   u_long cmd, prop_dictionary_t dict)
{
	prop_dictionary_t odict;
	struct udev_prop_ctx ctx;
	int error;

	/*
	 * Ensure event notification is enabled before doing the devfs
	 * scan so nothing gets missed.
	 */
	lockmgr(&udev_lk, LK_EXCLUSIVE);
	if (softc->initiated == 0) {
		softc->initiated = 1;
		++udev_initiated_count;
		TAILQ_INSERT_HEAD(&udev_evq, &softc->marker, link);
	}
	lockmgr(&udev_lk, LK_RELEASE);

	/*
	 * Devfs scan to build full dictionary.
	 */
	ctx.error = 0;
	ctx.cdevs = prop_array_create();
	if (ctx.cdevs == NULL) {
		log(LOG_DEBUG,
		    "udev_getdevs_ioctl: prop_array_create() failed\n");
		return EINVAL;
	}

	devfs_scan_callback(udev_getdevs_scan_callback, &ctx);

	if (ctx.error != 0) {
		prop_object_release(ctx.cdevs);
		return (ctx.error);
	}

	odict = prop_dictionary_create();
	if (odict == NULL) {
		return ENOMEM;
	}

	if ((prop_dictionary_set(odict, "array", ctx.cdevs)) == 0) {
		log(LOG_DEBUG,
		    "udev_getdevs_ioctl: prop_dictionary_set failed\n");
		prop_object_release(odict);
		return ENOMEM;
	}

	error = prop_dictionary_copyout_ioctl(pref, cmd, odict);

	prop_object_release(odict);
	return error;
}


/*
 * SYSINIT stuff
 */
static void
udev_init(void)
{
	lockinit(&udev_lk, "udevlk", 0, LK_CANRECURSE);
	TAILQ_INIT(&udevq);
	TAILQ_INIT(&udev_evq);
	udev_event_kernel_cache = objcache_create_simple(M_UDEV, sizeof(struct udev_event_kernel));
}

static void
udev_uninit(void)
{
	objcache_destroy(udev_event_kernel_cache);
}

static void
udev_dev_init(void)
{
	udev_dev = make_autoclone_dev(&udev_dev_ops, &DEVFS_CLONE_BITMAP(udev),
				      udev_dev_clone,
				      UID_ROOT, GID_WHEEL, 0600, "udev");
}

static void
udev_dev_uninit(void)
{
	destroy_dev(udev_dev);
}

SYSINIT(subr_udev_register, SI_SUB_CREATE_INIT, SI_ORDER_ANY,
	udev_init, NULL);
SYSUNINIT(subr_udev_register, SI_SUB_CREATE_INIT, SI_ORDER_ANY,
	udev_uninit, NULL);
SYSINIT(subr_udev_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY,
	udev_dev_init, NULL);
SYSUNINIT(subr_udev_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY,
	udev_dev_uninit, NULL);
