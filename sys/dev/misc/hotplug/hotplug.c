/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Device attachment and detachment notifications.
 */

#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/device.h>
#include <sys/lock.h>
#include <sys/event.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/hotplug.h>

#define HOTPLUG_MAXEVENTS	16

#define CDEV_MAJOR		82

static d_open_t		hotplugopen;
static d_close_t	hotplugclose;
static d_read_t		hotplugread;
static d_kqfilter_t	hotplugkqfilter;

static void hotplugfiltdetach(struct knote *);
static int hotplugfilt(struct knote *, long);

static struct dev_ops hotplug_ops = {
	{ "hotplug", CDEV_MAJOR, D_KQFILTER },
	.d_open =	hotplugopen,
	.d_close =	hotplugclose,
	.d_read =	hotplugread,
	.d_kqfilter =	hotplugkqfilter
};

struct hotplug_event_info {
	struct hotplug_event *he;
	TAILQ_ENTRY(hotplug_event_info) hei_link;
};

TAILQ_HEAD(hpq, hotplug_event_info);

static struct hotplug_softc
{
	cdev_t dev;
	struct lock lock;
	int opened;
	int qcount;
	struct hpq queue;
	struct kqinfo kq;
	void (*old_devfs_node_added)(struct hotplug_device *hpdev);
	void (*old_devfs_node_removed)(struct hotplug_device *hpdev);
} hpsc;

extern void (*devfs_node_added)(struct hotplug_device *hpdev);
extern void (*devfs_node_removed)(struct hotplug_device *hpdev);

void hotplug_devfs_node_added(struct hotplug_device *hpdev);
void hotplug_devfs_node_removed(struct hotplug_device *hpdev);

static int hotplug_get_event(struct hotplug_event *he);
static int hotplug_put_event(struct hotplug_event *he);

static int hotplug_uninit(void);
static int hotplug_init(void);

static int
hotplugopen(struct dev_open_args *ap)
{
	if (hpsc.opened)
		return (EBUSY);
	hpsc.opened = 1;
	return 0;
}

static int
hotplugclose(struct dev_close_args *ap)
{
	hpsc.opened = 0;
	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	wakeup(&hpsc);
	lockmgr(&hpsc.lock, LK_RELEASE);
	return 0;
}

static struct filterops hotplugfiltops =
	{ FILTEROP_ISFD, NULL, hotplugfiltdetach, hotplugfilt };

static int
hotplugkqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &hotplugfiltops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	klist = &hpsc.kq.ki_note;
	knote_insert(klist, kn);
	lockmgr(&hpsc.lock, LK_RELEASE);

	return (0);
}

static void
hotplugfiltdetach(struct knote *kn)
{
	struct klist *klist;

	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	klist = &hpsc.kq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&hpsc.lock, LK_RELEASE);
}

static int
hotplugfilt(struct knote *kn, long hint)
{
	int ready = 0;

	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	if (!TAILQ_EMPTY(&hpsc.queue))
		ready = 1;
	lockmgr(&hpsc.lock, LK_RELEASE);

	return (ready);
}

int
hotplug_get_event(struct hotplug_event *he)
{
	struct hotplug_event_info *hei;

	/* shouldn't get there */
	if(TAILQ_EMPTY(&hpsc.queue))
		return EINVAL;
	hpsc.qcount--;
	/* we are under hotplugread() lock here */
	hei = TAILQ_FIRST(&hpsc.queue);
	memcpy(he, hei->he, sizeof(struct hotplug_event));
	TAILQ_REMOVE(&hpsc.queue, hei, hei_link);
	kfree(hei->he, M_DEVBUF);
	kfree(hei, M_DEVBUF);
	return (0);
}

static int
hotplugread(struct dev_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct hotplug_event *he;
	int rv = EINVAL;

	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	while(TAILQ_EMPTY(&hpsc.queue)) {
		tsleep_interlock(&hpsc, PCATCH);
		lockmgr(&hpsc.lock, LK_RELEASE);
		rv = tsleep(&hpsc, PCATCH | PINTERLOCKED, "hotplug", 0);
		if(rv) {
			lockmgr(&hpsc.lock, LK_RELEASE);
			return (rv);
		}
	}
	he = kmalloc(sizeof(struct hotplug_event), M_DEVBUF, M_WAITOK);
	if(hotplug_get_event(he) == 0) {
		rv = uiomove((caddr_t)he, sizeof(struct hotplug_event), uio);
		kfree(he, M_DEVBUF);
	}
	lockmgr(&hpsc.lock, LK_RELEASE);
	return (rv);
}

static int
hotplug_put_event(struct hotplug_event *he)
{
	struct hotplug_event_info *hei = NULL;

	if (hpsc.qcount == HOTPLUG_MAXEVENTS && hpsc.opened) {
		kprintf("hotplug: event lost, queue full\n");
		return (1);
	}
	hei = kmalloc(sizeof(struct hotplug_event_info), M_DEVBUF, M_WAITOK);
	hei->he = kmalloc(sizeof(struct hotplug_event), M_DEVBUF, M_WAITOK);
	memcpy(hei->he, he, sizeof(struct hotplug_event));
	lockmgr(&hpsc.lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&hpsc.queue, hei, hei_link);
	hpsc.qcount++;
	wakeup(&hpsc);
	lockmgr(&hpsc.lock, LK_RELEASE);
	KNOTE(&hpsc.kq.ki_note, 0);
	return (0);
}

void
hotplug_devfs_node_added(struct hotplug_device *hpdev) {
	struct hotplug_event he;
	u_int class;
	char *name;

	if(!hpdev->dev || !hpsc.opened)
		return;
	class = hpdev->dev->si_ops->head.flags;
	name = hpdev->name;
	he.he_type = HOTPLUG_DEVAT;
	he.he_devclass = ((class == D_TTY) ? DV_TTY : ((class == D_TAPE) ? DV_TAPE : ((class == D_DISK) ? DV_DISK : DV_DULL)));
	strlcpy(he.he_devname, name, sizeof(he.he_devname));
	hotplug_put_event(&he);
}

void
hotplug_devfs_node_removed(struct hotplug_device *hpdev) {
	struct hotplug_event he;
	u_int class;
	char *name;

	if(!hpdev->dev || !hpsc.opened)
		return;
	class = hpdev->dev->si_ops->head.flags;
	name = hpdev->name;
	he.he_type = HOTPLUG_DEVDT;
	he.he_devclass = ((class == D_TTY) ? DV_TTY : ((class == D_TAPE) ? DV_TAPE : ((class == D_DISK) ? DV_DISK : DV_DULL)));
	strlcpy(he.he_devname, name, sizeof(he.he_devname));
	hotplug_put_event(&he);
}

static int
hotplug_init()
{
	hpsc.dev = make_dev(&hotplug_ops, 0, UID_ROOT, GID_WHEEL, 0600, "hotplug");
	hpsc.qcount = 0;
	lockinit(&hpsc.lock, "hotplug mtx", 0, 0);
	TAILQ_INIT(&hpsc.queue);
	/* setup handlers */
	hpsc.old_devfs_node_added = devfs_node_added;
	hpsc.old_devfs_node_removed = devfs_node_removed;
	devfs_node_added = hotplug_devfs_node_added;
	devfs_node_removed = hotplug_devfs_node_removed;
	return 0;
}

static int
hotplug_uninit()
{
	struct hotplug_event_info *hei;

	if(hpsc.opened)
		return EBUSY;
	devfs_node_added = hpsc.old_devfs_node_added;
	devfs_node_removed = hpsc.old_devfs_node_removed;
	/* Free the entire tail queue. */
	while ((hei = TAILQ_FIRST(&hpsc.queue))) {
		TAILQ_REMOVE(&hpsc.queue, hei, hei_link);
		kfree(hei->he, M_DEVBUF);
		kfree(hei, M_DEVBUF);
	}

	/* The tail queue should now be empty. */
	if (!TAILQ_EMPTY(&hpsc.queue))
		kprintf("hotplug: queue not empty!\n");
	destroy_dev(hpsc.dev);
	return 0;
}

static int
hotplug_modevh(struct module *m, int what, void *arg __unused)
{
	int		error;

	switch (what) {
	case MOD_LOAD:
		error = hotplug_init();
		break;
	case MOD_UNLOAD:
		error = hotplug_uninit();
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static moduledata_t hotplug_mod = {
	"hotplug",
	hotplug_modevh,
	NULL,
};

DECLARE_MODULE(hotplug, hotplug_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
