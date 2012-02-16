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
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/udev.h>
#include <sys/devfs.h>
#include <libprop/proplib.h>

#include <sys/thread2.h>

MALLOC_DEFINE(M_UDEV, "udev", "udev allocs");

/* XXX: use UUIDs for identification; would need help from devfs */

static cdev_t		udev_dev;
static d_open_t		udev_dev_open;
static d_close_t	udev_dev_close;
static d_read_t		udev_dev_read;
static d_kqfilter_t	udev_dev_kqfilter;
static d_ioctl_t	udev_dev_ioctl;

static int _udev_dict_set_cstr(prop_dictionary_t, const char *, char *);
static int _udev_dict_set_int(prop_dictionary_t, const char *, int64_t);
static int _udev_dict_set_uint(prop_dictionary_t, const char *, uint64_t);
static int _udev_dict_delete_key(prop_dictionary_t, const char *);
static prop_dictionary_t udev_init_dict_event(cdev_t, const char *);
static int udev_init_dict(cdev_t);
static int udev_destroy_dict(cdev_t);
static void udev_event_insert(int, prop_dictionary_t);
static struct udev_event_kernel *udev_event_remove(void);
static void udev_event_free(struct udev_event_kernel *);
static char *udev_event_externalize(struct udev_event_kernel *);
static void udev_getdevs_scan_callback(cdev_t, void *);
static int udev_getdevs_ioctl(struct plistref *, u_long, prop_dictionary_t);
static void udev_dev_filter_detach(struct knote *);
static int udev_dev_filter_read(struct knote *, long);

struct cmd_function {
	const char *cmd;
	int  (*fn)(struct plistref *, u_long, prop_dictionary_t);
};

struct udev_prop_ctx {
	prop_array_t cdevs;
	int error;
};

struct udev_event_kernel {
	struct udev_event ev;
	TAILQ_ENTRY(udev_event_kernel)	link;
};

struct udev_softc {
	int opened;
	int initiated;

	struct kqinfo kq;

	int qlen;
	struct lock lock;
	TAILQ_HEAD(, udev_event_kernel) ev_queue;	/* list of thread_io */
} udevctx;

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

	if ((error = _udev_dict_set_int(dict, "major", umajor(dev->si_inode))))
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

	/* Only start queing events after client has initiated properly */
	if (!udevctx.initiated)
		return;

	/* XXX: use objcache eventually */
	ev = kmalloc(sizeof(*ev), M_UDEV, M_WAITOK);
	ev->ev.ev_dict = prop_dictionary_copy(dict);
	if (ev->ev.ev_dict == NULL) {
		kfree(ev, M_UDEV);
		return;
	}
	ev->ev.ev_type = ev_type;

	lockmgr(&udevctx.lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&udevctx.ev_queue, ev, link);
	++udevctx.qlen;
	lockmgr(&udevctx.lock, LK_RELEASE);

	wakeup(&udevctx);
	KNOTE(&udevctx.kq.ki_note, 0);
}

static struct udev_event_kernel *
udev_event_remove(void)
{
	struct udev_event_kernel *ev;

	lockmgr(&udevctx.lock, LK_EXCLUSIVE);
	if (TAILQ_EMPTY(&udevctx.ev_queue)) {
		lockmgr(&udevctx.lock, LK_RELEASE);
		return NULL;
	}

	ev = TAILQ_FIRST(&udevctx.ev_queue);
	TAILQ_REMOVE(&udevctx.ev_queue, ev, link);
	--udevctx.qlen;
	lockmgr(&udevctx.lock, LK_RELEASE);

	return ev;
}

static void
udev_event_free(struct udev_event_kernel *ev)
{
	/* XXX: use objcache eventually */
	kfree(ev, M_UDEV);
}

static char *
udev_event_externalize(struct udev_event_kernel *ev)
{
	prop_dictionary_t	dict;
	char *xml;
	int error;


	dict = prop_dictionary_create();
	if (dict == NULL) {
		log(LOG_DEBUG, "udev_event_externalize: prop_dictionary_create() failed\n");
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
 * dev stuff
 */
static int
udev_dev_open(struct dev_open_args *ap)
{
	if (udevctx.opened)
		return EBUSY;

	udevctx.opened = 1;

	return 0;
}

static int
udev_dev_close(struct dev_close_args *ap)
{
	udevctx.opened = 0;
	udevctx.initiated = 0;
	wakeup(&udevctx);

	return 0;
}

static struct filterops udev_dev_read_filtops =
	{ FILTEROP_ISFD, NULL, udev_dev_filter_detach, udev_dev_filter_read };

static int
udev_dev_kqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;
	lockmgr(&udevctx.lock, LK_EXCLUSIVE);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &udev_dev_read_filtops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
	        lockmgr(&udevctx.lock, LK_RELEASE);
		return (0);
	}

	klist = &udevctx.kq.ki_note;
	knote_insert(klist, kn);

        lockmgr(&udevctx.lock, LK_RELEASE);

	return (0);
}

static void
udev_dev_filter_detach(struct knote *kn)
{
	struct klist *klist;

	lockmgr(&udevctx.lock, LK_EXCLUSIVE);
	klist = &udevctx.kq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&udevctx.lock, LK_RELEASE);
}

static int
udev_dev_filter_read(struct knote *kn, long hint)
{
	int ready = 0;

	lockmgr(&udevctx.lock, LK_EXCLUSIVE);
	if (!TAILQ_EMPTY(&udevctx.ev_queue))
		ready = 1;
	lockmgr(&udevctx.lock, LK_RELEASE);

	return (ready);
}

static int
udev_dev_read(struct dev_read_args *ap)
{
	struct udev_event_kernel *ev;
	struct uio *uio = ap->a_uio;
	char	*xml;
	size_t	len;
	int	error;


	lockmgr(&udevctx.lock, LK_EXCLUSIVE);

	for (;;) {
		if ((ev = udev_event_remove()) != NULL) {
			if ((xml = udev_event_externalize(ev)) == NULL) {
				lockmgr(&udevctx.lock, LK_RELEASE);
				return ENOMEM;
			}

			len = strlen(xml) + 1; /* account for NULL-termination */
			if (uio->uio_resid < len) {
				error = ENOMEM;
			} else {
				error = uiomove((caddr_t)xml, len, uio);
			}

			kfree(xml, M_TEMP);
			udev_event_free(ev);
			lockmgr(&udevctx.lock, LK_RELEASE);
			return error;
		}

		if ((error = lksleep(&udevctx, &udevctx.lock, 0, "udevq", 0))) {
			lockmgr(&udevctx.lock, LK_RELEASE);
			return error;
		}
	}

	lockmgr(&udevctx.lock, LK_RELEASE);

}

static int
udev_dev_ioctl(struct dev_ioctl_args *ap)
{
	prop_dictionary_t dict;
	prop_object_t	po;
	prop_string_t	ps;
	struct plistref *pref;
	int i, error;

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
			error = cmd_fn[i].fn(pref, ap->a_cmd, dict);
		} else {
			error = EINVAL;
		}

		//prop_object_release(po);
		prop_object_release(dict);
		break;
	default:
		error = ENOTTY; /* Inappropriate ioctl for device */
		break;
	}

	return(error);
}

static void
udev_getdevs_scan_callback(cdev_t cdev, void *arg)
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
udev_getdevs_ioctl(struct plistref *pref, u_long cmd, prop_dictionary_t dict)
{
	prop_dictionary_t odict;
	struct udev_prop_ctx ctx;
	int error;

	ctx.error = 0;
	ctx.cdevs = prop_array_create();
	if (ctx.cdevs == NULL) {
		log(LOG_DEBUG, "udev_getdevs_ioctl: prop_array_create() failed\n");
		return EINVAL;
	}

	/* XXX: need devfs_scan_alias_callback() */
	devfs_scan_callback(udev_getdevs_scan_callback, &ctx);

	if (ctx.error != 0) {
		prop_object_release(ctx.cdevs);
		return (ctx.error);
	}
	udevctx.initiated = 1;

	odict = prop_dictionary_create();
	if (odict == NULL) {
		return ENOMEM;
	}

	if ((prop_dictionary_set(odict, "array", ctx.cdevs)) == 0) {
		log(LOG_DEBUG, "udev_getdevs_ioctl: prop_dictionary_set failed\n");
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
	lockinit(&udevctx.lock, "udevevq", 0, LK_CANRECURSE);
	TAILQ_INIT(&udevctx.ev_queue);
}

static void
udev_uninit(void)
{
}

static void
udev_dev_init(void)
{
	udev_dev = make_dev(&udev_dev_ops,
            0,
            UID_ROOT,
            GID_WHEEL,
            0600,
            "udev");
}

static void
udev_dev_uninit(void)
{
	destroy_dev(udev_dev);
}

SYSINIT(subr_udev_register, SI_SUB_CREATE_INIT, SI_ORDER_ANY, udev_init, NULL);
SYSUNINIT(subr_udev_register, SI_SUB_CREATE_INIT, SI_ORDER_ANY, udev_uninit, NULL);
SYSINIT(subr_udev_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY, udev_dev_init, NULL);
SYSUNINIT(subr_udev_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY, udev_dev_uninit, NULL);
