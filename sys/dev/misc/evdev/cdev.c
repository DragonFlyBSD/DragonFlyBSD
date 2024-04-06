/*-
 * Copyright (c) 2014 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015-2016 Vladimir Kondratyev <wulf@FreeBSD.org>
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
 * $FreeBSD$
 */

#include "opt_evdev.h"

#include <sys/types.h>
#include <sys/module.h>
#include <sys/devfs.h>

#include <sys/param.h>
#include <sys/caps.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/vnode.h> /* IO_NDELAY in read() */
#include <sys/uio.h>

#include <sys/errno.h>

#include <sys/device.h>
#include <sys/bus.h>

/* Use FreeBSD bitstring locally. */
#include "freebsd-bitstring.h"

#include <dev/misc/evdev/evdev.h>
#include <dev/misc/evdev/evdev_private.h>
#include <dev/misc/evdev/input.h>

#ifdef EVDEV_DEBUG
#define	debugf(client, fmt, args...)	kprintf("evdev cdev: "fmt"\n", ##args)
#else
#define	debugf(client, fmt, args...)
#endif

#define	DEF_RING_REPORTS	8

static d_open_t		evdev_open;
static d_read_t		evdev_read;
static d_write_t	evdev_write;
static d_ioctl_t	evdev_ioctl;
static d_kqfilter_t	evdev_kqfilter;
static d_priv_dtor_t	evdev_dtor;

static int evdev_kqread(struct knote *kn, long hint);
static void evdev_kqdetach(struct knote *kn);
static int evdev_ioctl_eviocgbit(struct evdev_dev *, int, int, caddr_t);
static void evdev_client_filter_queue(struct evdev_client *, uint16_t);

static struct dev_ops evdev_cdevsw = {
	{ "evdev", 0, 0 },
	.d_open = evdev_open,
	.d_read = evdev_read,
	.d_write = evdev_write,
	.d_ioctl = evdev_ioctl,
	.d_kqfilter = evdev_kqfilter,
};

static struct filterops evdev_cdev_filterops = {
	.f_flags = FILTEROP_ISFD,
	.f_attach = NULL,
	.f_detach = evdev_kqdetach,
	.f_event = evdev_kqread,
};

static int
evdev_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	size_t buffer_size;
	int ret;

	/*
	 * Disallow access to disk volumes if RESTRICTEDROOT
	 */
	if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
		return (EPERM);

	if (evdev == NULL)
		return (ENODEV);

	/* Initialize client structure */
	buffer_size = evdev->ev_report_size * DEF_RING_REPORTS;
	client = kmalloc(offsetof(struct evdev_client, ec_buffer) +
	    sizeof(struct input_event) * buffer_size,
	    M_EVDEV, M_WAITOK | M_ZERO);

	/* Initialize ring buffer */
	client->ec_buffer_size = buffer_size;
	client->ec_buffer_head = 0;
	client->ec_buffer_tail = 0;
	client->ec_buffer_ready = 0;

	client->ec_evdev = evdev;
	lockinit(&client->ec_buffer_mtx, "evclient", 0, LK_CANRECURSE);

	/* Avoid race with evdev_unregister */
	EVDEV_LOCK(evdev);
	if (dev->si_drv1 == NULL)
		ret = ENODEV;
	else
		ret = evdev_register_client(evdev, client);

	if (ret != 0)
		evdev_revoke_client(client);
	/*
	 * Unlock evdev here because non-sleepable lock held
	 * while calling devfs_set_cdevpriv upsets WITNESS
	 */
	EVDEV_UNLOCK(evdev);

	if (ret == 0) {
		struct file *fp;

		fp = (ap->a_fpp) ? *ap->a_fpp : NULL;
		ret = devfs_set_cdevpriv(fp, client, &evdev_dtor);
	}

	if (ret != 0) {
		debugf(client, "cannot register evdev client");
	}

	return (ret);
}

static void
evdev_dtor(void *data)
{
	struct evdev_client *client = (struct evdev_client *)data;

	EVDEV_LOCK(client->ec_evdev);
	if (!client->ec_revoked)
		evdev_dispose_client(client->ec_evdev, client);
	EVDEV_UNLOCK(client->ec_evdev);

	funsetown(&client->ec_sigio);
	lockuninit(&client->ec_buffer_mtx);
	kfree(client, M_EVDEV);
}

static int
evdev_read(struct dev_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct evdev_client *client;
	struct input_event event;
	int ret = 0;
	int remaining;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&client);
	if (ret != 0)
		return (ret);

	debugf(client, "read %zd bytes by thread %d", uio->uio_resid, 0);

	if (client->ec_revoked)
		return (ENODEV);

	/* Zero-sized reads are allowed for error checking */
	if (uio->uio_resid != 0 && uio->uio_resid < sizeof(struct input_event))
		return (EINVAL);

	remaining = uio->uio_resid / sizeof(struct input_event);

	EVDEV_CLIENT_LOCKQ(client);

	if (EVDEV_CLIENT_EMPTYQ(client)) {
		if (ioflag & IO_NDELAY) {
			ret = EWOULDBLOCK;
		} else {
			if (remaining != 0) {
				client->ec_blocked = true;
				ret = lksleep(client, &client->ec_buffer_mtx,
				    PCATCH, "evread", 0);
				if (ret == 0 && client->ec_revoked)
					ret = ENODEV;
			}
		}
	}

	while (ret == 0 && !EVDEV_CLIENT_EMPTYQ(client) && remaining > 0) {
		memcpy(&event, &client->ec_buffer[client->ec_buffer_head],
		    sizeof(struct input_event));
		client->ec_buffer_head =
		    (client->ec_buffer_head + 1) % client->ec_buffer_size;
		remaining--;

		EVDEV_CLIENT_UNLOCKQ(client);
		ret = uiomove((void *)&event, sizeof(struct input_event), uio);
		EVDEV_CLIENT_LOCKQ(client);
	}

	EVDEV_CLIENT_UNLOCKQ(client);

	return (ret);
}

static int
evdev_write(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	struct input_event event;
	int ret = 0;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&client);
	if (ret != 0)
		return (ret);

	debugf(client, "write %zd bytes by thread %d", uio->uio_resid, 0);

	if (client->ec_revoked || evdev == NULL)
		return (ENODEV);

	if (uio->uio_resid % sizeof(struct input_event) != 0) {
		debugf(client, "write size not multiple of input_event size");
		return (EINVAL);
	}

	while (uio->uio_resid > 0 && ret == 0) {
		ret = uiomove((void *)&event, sizeof(struct input_event), uio);
		if (ret == 0)
			ret = evdev_inject_event(evdev, event.type, event.code,
			    event.value);
	}

	return (ret);
}

static int
evdev_kqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist;
	struct evdev_client *client;
	int ret;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&client);
	if (ret != 0)
		return (ret);

	if (client->ec_revoked)
		return (ENODEV);

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &evdev_cdev_filterops;
		break;
	default:
		return(EINVAL);
	}
	kn->kn_hook = (caddr_t)client;

	klist = &client->kqinfo.ki_note;
	knote_insert(klist, kn);
	return (0);
}

static int
evdev_kqread(struct knote *kn, long hint)
{
	struct evdev_client *client;
	int ret;
	int locked = 0;

	client = (struct evdev_client *)kn->kn_hook;

	/* NOTE on DragonFly v FreeBSD.
	 * FreeBSD locks the klist when calling f_event, i.e. evdev_kqread().
	 * That's why the plain assertion EVDEV_CLIENT_LOCKQ_ASSERT(client)
	 * fails on DragonFly: DragonFly does not ensure the lock associated
	 * with the klist is locked.
	 * To mimic FreeBSD's behavior, we will lock ec_buffer_mtx if
	 * it was not locked, and unlock when leaving.
	 */
	locked = lockowned(&(client)->ec_buffer_mtx);
	if (!locked)
		EVDEV_CLIENT_LOCKQ(client);

	EVDEV_CLIENT_LOCKQ_ASSERT(client);

	if (client->ec_revoked) {
		kn->kn_flags |= EV_EOF;
		ret = 1;
	} else {
		kn->kn_data = EVDEV_CLIENT_SIZEQ(client) *
		    sizeof(struct input_event);
		ret = !EVDEV_CLIENT_EMPTYQ(client);
	}

	/* Unlock if ec_buffer_mtx was not locked. */
	if (!locked) {
		EVDEV_CLIENT_UNLOCKQ(client);
	}

	return (ret);
}

static void
evdev_kqdetach(struct knote *kn)
{
	struct evdev_client *client;

	client = (struct evdev_client *)kn->kn_hook;
	knote_remove(&client->kqinfo.ki_note, kn);
}

static int
evdev_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	caddr_t data = ap->a_data;
	struct evdev_dev *evdev = dev->si_drv1;
	struct evdev_client *client;
	struct input_keymap_entry *ke;
	int ret, len, limit, type_num;
	uint32_t code;
	size_t nvalues;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&client);
	if (ret != 0)
		return (ret);

	if (client->ec_revoked || evdev == NULL)
		return (ENODEV);

	/* file I/O ioctl handling */
	switch (cmd) {
	case FIOSETOWN:
		return (fsetown(*(int *)data, &client->ec_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(&client->ec_sigio);
		return (0);

	case FIONBIO:
		return (0);

	case FIOASYNC:
		if (*(int *)data)
			client->ec_async = true;
		else
			client->ec_async = false;

		return (0);

	case FIONREAD:
		EVDEV_CLIENT_LOCKQ(client);
		*(int *)data =
		    EVDEV_CLIENT_SIZEQ(client) * sizeof(struct input_event);
		EVDEV_CLIENT_UNLOCKQ(client);
		return (0);
	}

	len = IOCPARM_LEN(cmd);
	debugf(client, "ioctl called: cmd=0x%08lx, data=%p", cmd, data);

	/* evdev fixed-length ioctls handling */
	switch (cmd) {
	case EVIOCGVERSION:
		*(int *)data = EV_VERSION;
		return (0);

	case EVIOCGID:
		debugf(client, "EVIOCGID: bus=%d vendor=0x%04x product=0x%04x",
		    evdev->ev_id.bustype, evdev->ev_id.vendor,
		    evdev->ev_id.product);
		memcpy(data, &evdev->ev_id, sizeof(struct input_id));
		return (0);

	case EVIOCGREP:
		if (!evdev_event_supported(evdev, EV_REP))
			return (ENOTSUP);

		memcpy(data, evdev->ev_rep, sizeof(evdev->ev_rep));
		return (0);

	case EVIOCSREP:
		if (!evdev_event_supported(evdev, EV_REP))
			return (ENOTSUP);

		evdev_inject_event(evdev, EV_REP, REP_DELAY, ((int *)data)[0]);
		evdev_inject_event(evdev, EV_REP, REP_PERIOD,
		    ((int *)data)[1]);
		return (0);

	case EVIOCGKEYCODE:
		/* Fake unsupported ioctl */
		return (0);

	case EVIOCGKEYCODE_V2:
		if (evdev->ev_methods == NULL ||
		    evdev->ev_methods->ev_get_keycode == NULL)
			return (ENOTSUP);

		ke = (struct input_keymap_entry *)data;
		evdev->ev_methods->ev_get_keycode(evdev, ke);
		return (0);

	case EVIOCSKEYCODE:
		/* Fake unsupported ioctl */
		return (0);

	case EVIOCSKEYCODE_V2:
		if (evdev->ev_methods == NULL ||
		    evdev->ev_methods->ev_set_keycode == NULL)
			return (ENOTSUP);

		ke = (struct input_keymap_entry *)data;
		evdev->ev_methods->ev_set_keycode(evdev, ke);
		return (0);

	case EVIOCGABS(0) ... EVIOCGABS(ABS_MAX):
		if (evdev->ev_absinfo == NULL)
			return (EINVAL);

		memcpy(data, &evdev->ev_absinfo[cmd - EVIOCGABS(0)],
		    sizeof(struct input_absinfo));
		return (0);

	case EVIOCSABS(0) ... EVIOCSABS(ABS_MAX):
		if (evdev->ev_absinfo == NULL)
			return (EINVAL);

		code = cmd - EVIOCSABS(0);
		/* mt-slot number can not be changed */
		if (code == ABS_MT_SLOT)
			return (EINVAL);

		EVDEV_LOCK(evdev);
		evdev_set_absinfo(evdev, code, (struct input_absinfo *)data);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCSFF:
	case EVIOCRMFF:
	case EVIOCGEFFECTS:
		/* Fake unsupported ioctls */
		return (0);

	case EVIOCGRAB:
		EVDEV_LOCK(evdev);
		if (*(int *)data)
			ret = evdev_grab_client(evdev, client);
		else
			ret = evdev_release_client(evdev, client);
		EVDEV_UNLOCK(evdev);
		return (ret);

	case EVIOCREVOKE:
		if (*(int *)data != 0)
			return (EINVAL);

		EVDEV_LOCK(evdev);
		if (dev->si_drv1 != NULL && !client->ec_revoked) {
			evdev_dispose_client(evdev, client);
			evdev_revoke_client(client);
		}
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCSCLOCKID:
		switch (*(int *)data) {
		case CLOCK_REALTIME:
			client->ec_clock_id = EV_CLOCK_REALTIME;
			return (0);
		case CLOCK_MONOTONIC:
			client->ec_clock_id = EV_CLOCK_MONOTONIC;
			return (0);
		default:
			return (EINVAL);
		}
	}

	/* evdev variable-length ioctls handling */
	switch (IOCBASECMD(cmd)) {
	case EVIOCGNAME(0):
		strlcpy(data, evdev->ev_name, len);
		return (0);

	case EVIOCGPHYS(0):
		if (evdev->ev_shortname[0] == 0)
			return (ENOENT);

		strlcpy(data, evdev->ev_shortname, len);
		return (0);

	case EVIOCGUNIQ(0):
		if (evdev->ev_serial[0] == 0)
			return (ENOENT);

		strlcpy(data, evdev->ev_serial, len);
		return (0);

	case EVIOCGPROP(0):
		limit = MIN(len, bitstr_size(INPUT_PROP_CNT));
		memcpy(data, evdev->ev_prop_flags, limit);
		return (0);

	case EVIOCGMTSLOTS(0):
		if (evdev->ev_mt == NULL)
			return (EINVAL);
		if (len < sizeof(uint32_t))
			return (EINVAL);
		code = *(uint32_t *)data;
		if (!ABS_IS_MT(code))
			return (EINVAL);

		nvalues =
		    MIN(len / sizeof(int32_t) - 1, MAXIMAL_MT_SLOT(evdev) + 1);
		for (int i = 0; i < nvalues; i++)
			((int32_t *)data)[i + 1] =
			    evdev_mt_get_value(evdev, i, code);
		return (0);

	case EVIOCGKEY(0):
		limit = MIN(len, bitstr_size(KEY_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_KEY);
		memcpy(data, evdev->ev_key_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGLED(0):
		limit = MIN(len, bitstr_size(LED_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_LED);
		memcpy(data, evdev->ev_led_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGSND(0):
		limit = MIN(len, bitstr_size(SND_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_SND);
		memcpy(data, evdev->ev_snd_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGSW(0):
		limit = MIN(len, bitstr_size(SW_CNT));
		EVDEV_LOCK(evdev);
		evdev_client_filter_queue(client, EV_SW);
		memcpy(data, evdev->ev_sw_states, limit);
		EVDEV_UNLOCK(evdev);
		return (0);

	case EVIOCGBIT(0, 0) ... EVIOCGBIT(EV_MAX, 0):
		type_num = IOCBASECMD(cmd) - EVIOCGBIT(0, 0);
		debugf(client, "EVIOCGBIT(%d): data=%p, len=%d", type_num,
		    data, len);
		return (evdev_ioctl_eviocgbit(evdev, type_num, len, data));
	}

	return (EINVAL);
}

static int
evdev_ioctl_eviocgbit(struct evdev_dev *evdev, int type, int len, caddr_t data)
{
	/*
	 * We will use freebsd-bitstring.h locally. This ensures bitmap
	 * is of type (unsigned long *). DragonFly's original bitmap
	 * is (unsigned char *).
	 */
	unsigned long *bitmap;
	int limit;

	switch (type) {
	case 0:
		bitmap = evdev->ev_type_flags;
		limit = EV_CNT;
		break;
	case EV_KEY:
		bitmap = evdev->ev_key_flags;
		limit = KEY_CNT;
		break;
	case EV_REL:
		bitmap = evdev->ev_rel_flags;
		limit = REL_CNT;
		break;
	case EV_ABS:
		bitmap = evdev->ev_abs_flags;
		limit = ABS_CNT;
		break;
	case EV_MSC:
		bitmap = evdev->ev_msc_flags;
		limit = MSC_CNT;
		break;
	case EV_LED:
		bitmap = evdev->ev_led_flags;
		limit = LED_CNT;
		break;
	case EV_SND:
		bitmap = evdev->ev_snd_flags;
		limit = SND_CNT;
		break;
	case EV_SW:
		bitmap = evdev->ev_sw_flags;
		limit = SW_CNT;
		break;
	case EV_FF:
		/*
		 * We don't support EV_FF now, so let's
		 * just fake it returning only zeros.
		 */
		bzero(data, len);
		return (0);
	default:
		return (ENOTTY);
	}

	/*
	 * Clear ioctl data buffer in case it's bigger than
	 * bitmap size
	 */
	bzero(data, len);

	limit = bitstr_size(limit);
	len = MIN(limit, len);
	memcpy(data, bitmap, len);
	return (0);
}

void
evdev_revoke_client(struct evdev_client *client)
{

	EVDEV_LOCK_ASSERT(client->ec_evdev);

	client->ec_revoked = true;
}

void
evdev_notify_event(struct evdev_client *client)
{

	EVDEV_CLIENT_LOCKQ_ASSERT(client);

	if (client->ec_blocked) {
		client->ec_blocked = false;
		wakeup(client);
	}
	if (client->ec_selected) {
		client->ec_selected = false;
		wakeup(&client->kqinfo);
	}

	KNOTE(&client->kqinfo.ki_note, 0);

	if (client->ec_async && client->ec_sigio != NULL)
		pgsigio(client->ec_sigio, SIGIO, 0);
}

int
evdev_cdev_create(struct evdev_dev *evdev)
{
	cdev_t dev;
	int ret, unit;

	/*
	 * Iterate over devices input/eventX until we find a non-existing
	 * one and record its number in unit.
	 */
	unit = 0;
	while (devfs_find_device_by_name("input/event%d", unit) != NULL) {
	    unit++;
	}

	/*
	 * Put unit as minor. Minor and major will determine st_rdev of
	 * eventX. Ensuring that all eventX have different major and minor
	 * will make st_rdev unique. This is needed by libinput, which
	 * determines eventX from st_rdev.
	 */
	dev = make_dev(&evdev_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600,
	    "input/event%d", unit);

	if (dev != NULL) {
		dev->si_drv1 = evdev;
		evdev->ev_cdev = dev;
		evdev->ev_unit = unit;
		ret = 0;
	} else {
		ret = ENODEV;
		goto err;
	}

	reference_dev(evdev->ev_cdev);

err:
	return (ret);
}

int
evdev_cdev_destroy(struct evdev_dev *evdev)
{

	if (evdev->ev_cdev) {
		dev_ops_remove_minor(&evdev_cdevsw, evdev->ev_unit);
	}

	return (0);
}

static void
evdev_client_gettime(struct evdev_client *client, struct timeval *tv)
{

	switch (client->ec_clock_id) {
	case EV_CLOCK_BOOTTIME:
		/*
		 * XXX: FreeBSD does not support true POSIX monotonic clock.
		 *      So aliase EV_CLOCK_BOOTTIME to EV_CLOCK_MONOTONIC.
		 */
	case EV_CLOCK_MONOTONIC:
		microuptime(tv);
		break;

	case EV_CLOCK_REALTIME:
	default:
		microtime(tv);
		break;
	}
}

void
evdev_client_push(struct evdev_client *client, uint16_t type, uint16_t code,
    int32_t value)
{
	struct timeval time;
	size_t count, head, tail, ready;

	EVDEV_CLIENT_LOCKQ_ASSERT(client);
	head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	ready = client->ec_buffer_ready;
	count = client->ec_buffer_size;

	/* If queue is full drop its content and place SYN_DROPPED event */
	if ((tail + 1) % count == head) {
		debugf(client, "client %p: buffer overflow", client);

		head = (tail + count - 1) % count;
		client->ec_buffer[head] = (struct input_event) {
			.type = EV_SYN,
			.code = SYN_DROPPED,
			.value = 0
		};
		/*
		 * XXX: Here is a small race window from now till the end of
		 *      report. The queue is empty but client has been already
		 *      notified of data readyness. Can be fixed in two ways:
		 * 1. Implement bulk insert so queue lock would not be dropped
		 *    till the SYN_REPORT event.
		 * 2. Insert SYN_REPORT just now and skip remaining events
		 */
		client->ec_buffer_head = head;
		client->ec_buffer_ready = head;
	}

	client->ec_buffer[tail].type = type;
	client->ec_buffer[tail].code = code;
	client->ec_buffer[tail].value = value;
	client->ec_buffer_tail = (tail + 1) % count;

	/* Allow users to read events only after report has been completed */
	if (type == EV_SYN && code == SYN_REPORT) {
		evdev_client_gettime(client, &time);
		for (; ready != client->ec_buffer_tail;
		    ready = (ready + 1) % count)
			client->ec_buffer[ready].time = time;
		client->ec_buffer_ready = client->ec_buffer_tail;
	}
}

void
evdev_client_dumpqueue(struct evdev_client *client)
{
	struct input_event *event;
	size_t i, head, tail, ready, size;

	head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	ready = client->ec_buffer_ready;
	size = client->ec_buffer_size;

	kprintf("evdev client: %p\n", client);
	kprintf("event queue: head=%zu ready=%zu tail=%zu size=%zu\n",
	    head, ready, tail, size);

	kprintf("queue contents:\n");

	for (i = 0; i < size; i++) {
		event = &client->ec_buffer[i];
		kprintf("%zu: ", i);

		if (i < head || i > tail)
			kprintf("unused\n");
		else
			kprintf("type=%d code=%d value=%d ", event->type,
			    event->code, event->value);

		if (i == head)
			kprintf("<- head\n");
		else if (i == tail)
			kprintf("<- tail\n");
		else if (i == ready)
			kprintf("<- ready\n");
		else
			kprintf("\n");
	}
}

static void
evdev_client_filter_queue(struct evdev_client *client, uint16_t type)
{
	struct input_event *event;
	size_t head, tail, count, i;
	bool last_was_syn = false;

	EVDEV_CLIENT_LOCKQ(client);

	i = head = client->ec_buffer_head;
	tail = client->ec_buffer_tail;
	count = client->ec_buffer_size;
	client->ec_buffer_ready = client->ec_buffer_tail;

	while (i != client->ec_buffer_tail) {
		event = &client->ec_buffer[i];
		i = (i + 1) % count;

		/* Skip event of given type */
		if (event->type == type)
			continue;

		/* Remove empty SYN_REPORT events */
		if (event->type == EV_SYN && event->code == SYN_REPORT) {
			if (last_was_syn)
				continue;
			else
				client->ec_buffer_ready = (tail + 1) % count;
		}

		/* Rewrite entry */
		memcpy(&client->ec_buffer[tail], event,
		    sizeof(struct input_event));

		last_was_syn = (event->type == EV_SYN &&
		    event->code == SYN_REPORT);

		tail = (tail + 1) % count;
	}

	client->ec_buffer_head = i;
	client->ec_buffer_tail = tail;

	EVDEV_CLIENT_UNLOCKQ(client);
}
