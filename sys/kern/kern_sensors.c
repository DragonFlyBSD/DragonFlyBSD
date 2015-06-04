/* $OpenBSD: kern_sensors.c,v 1.19 2007/06/04 18:42:05 deraadt Exp $ */

/*
 * (MPSAFE)
 *
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2006 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/lock.h>

#include <sys/sysctl.h>
#include <sys/sensors.h>

#include <sys/mplock2.h>

/*
 * Default to the last cpu's sensor thread
 */
#define SENSOR_TASK_DEFCPU	(ncpus - 1)

static int		sensordev_idmax;
static TAILQ_HEAD(sensordev_list, ksensordev) sensordev_list =
    TAILQ_HEAD_INITIALIZER(sensordev_list);

static struct ksensordev *sensordev_get(int);
static struct ksensor	*sensor_find(struct ksensordev *, enum sensor_type,
			    int);

struct sensor_task {
	void				*arg;
	void				(*func)(void *);

	int				period;
	time_t				nextrun;	/* time_uptime */
	int				running;
	int				cpuid;
	TAILQ_ENTRY(sensor_task)	entry;
};
TAILQ_HEAD(sensor_tasklist, sensor_task);

struct sensor_taskthr {
	struct sensor_tasklist		list;
	struct lock			lock;
};

static void		sensor_task_thread(void *);
static void		sensor_task_schedule(struct sensor_taskthr *,
			    struct sensor_task *);

static void		sensordev_sysctl_install(struct ksensordev *);
static void		sensordev_sysctl_deinstall(struct ksensordev *);
static void		sensor_sysctl_install(struct ksensordev *,
			    struct ksensor *);
static void		sensor_sysctl_deinstall(struct ksensordev *,
			    struct ksensor *);

static struct sensor_taskthr sensor_task_threads[MAXCPU];

void
sensordev_install(struct ksensordev *sensdev)
{
	struct ksensordev *v, *after = NULL;
	int num = 0;

	SYSCTL_XLOCK();

	TAILQ_FOREACH(v, &sensordev_list, list) {
		if (v->num == num) {
			++num;
			after = v;
		} else if (v->num > num) {
			break;
		}
	}

	sensdev->num = num;
	if (after == NULL) {
		KKASSERT(sensdev->num == 0);
		TAILQ_INSERT_HEAD(&sensordev_list, sensdev, list);
	} else {
		TAILQ_INSERT_AFTER(&sensordev_list, after, sensdev, list);
	}

	/* Save max sensor device id */
	sensordev_idmax = TAILQ_LAST(&sensordev_list, sensordev_list)->num + 1;

	/* Install sysctl node for this sensor device */
	sensordev_sysctl_install(sensdev);

	SYSCTL_XUNLOCK();
}

void
sensor_attach(struct ksensordev *sensdev, struct ksensor *sens)
{
	struct ksensor *v, *nv;
	struct ksensors_head *sh;
	int i;

	SYSCTL_XLOCK();

	sh = &sensdev->sensors_list;
	if (sensdev->sensors_count == 0) {
		for (i = 0; i < SENSOR_MAX_TYPES; i++)
			sensdev->maxnumt[i] = 0;
		sens->numt = 0;
		SLIST_INSERT_HEAD(sh, sens, list);
	} else {
		for (v = SLIST_FIRST(sh);
		    (nv = SLIST_NEXT(v, list)) != NULL; v = nv)
			if (v->type == sens->type && (v->type != nv->type || 
			    (v->type == nv->type && nv->numt - v->numt > 1)))
				break;
		/* sensors of the same type go after each other */
		if (v->type == sens->type)
			sens->numt = v->numt + 1;
		else
			sens->numt = 0;
		SLIST_INSERT_AFTER(v, sens, list);
	}
	/*
	 * We only increment maxnumt[] if the sensor was added
	 * to the last position of sensors of this type
	 */
	if (sensdev->maxnumt[sens->type] == sens->numt)
		sensdev->maxnumt[sens->type]++;
	sensdev->sensors_count++;

	/* Install sysctl node for this sensor */
	sensor_sysctl_install(sensdev, sens);

	SYSCTL_XUNLOCK();
}

void
sensordev_deinstall(struct ksensordev *sensdev)
{
	struct ksensordev *last;

	SYSCTL_XLOCK();

	TAILQ_REMOVE(&sensordev_list, sensdev, list);

	/* Adjust max sensor device id */
	last = TAILQ_LAST(&sensordev_list, sensordev_list);
	if (last != NULL)
		sensordev_idmax = last->num + 1;
	else
		sensordev_idmax = 0;

	/*
	 * Deinstall sensor device's sysctl node; this also
	 * removes all attached sensors' sysctl nodes.
	 */
	sensordev_sysctl_deinstall(sensdev);

	SYSCTL_XUNLOCK();
}

void
sensor_detach(struct ksensordev *sensdev, struct ksensor *sens)
{
	struct ksensors_head *sh;

	SYSCTL_XLOCK();

	sh = &sensdev->sensors_list;
	sensdev->sensors_count--;
	SLIST_REMOVE(sh, sens, ksensor, list);
	/*
	 * We only decrement maxnumt[] if this is the tail 
	 * sensor of this type
	 */
	if (sens->numt == sensdev->maxnumt[sens->type] - 1)
		sensdev->maxnumt[sens->type]--;

	/* Deinstall sensor's sysctl node */
	sensor_sysctl_deinstall(sensdev, sens);

	SYSCTL_XUNLOCK();
}

static struct ksensordev *
sensordev_get(int num)
{
	struct ksensordev *sd;

	SYSCTL_ASSERT_XLOCKED();

	TAILQ_FOREACH(sd, &sensordev_list, list) {
		if (sd->num == num)
			return (sd);
	}
	return (NULL);
}

static struct ksensor *
sensor_find(struct ksensordev *sensdev, enum sensor_type type, int numt)
{
	struct ksensor *s;
	struct ksensors_head *sh;

	SYSCTL_ASSERT_XLOCKED();

	sh = &sensdev->sensors_list;
	SLIST_FOREACH(s, sh, list) {
		if (s->type == type && s->numt == numt)
			return (s);
	}
	return (NULL);
}

void
sensor_task_register(void *arg, void (*func)(void *), int period)
{
	sensor_task_register2(arg, func, period, SENSOR_TASK_DEFCPU);
}

void
sensor_task_unregister(void *arg)
{
	struct sensor_taskthr	*thr = &sensor_task_threads[SENSOR_TASK_DEFCPU];
	struct sensor_task	*st;

	lockmgr(&thr->lock, LK_EXCLUSIVE);
	TAILQ_FOREACH(st, &thr->list, entry)
		if (st->arg == arg)
			st->running = 0;
	lockmgr(&thr->lock, LK_RELEASE);
}

void
sensor_task_unregister2(struct sensor_task *st)
{
	struct sensor_taskthr *thr;

	KASSERT(st->cpuid >= 0 && st->cpuid < ncpus,
	    ("invalid task cpuid %d", st->cpuid));
	thr = &sensor_task_threads[st->cpuid];

	/*
	 * Hold the lock then zero-out running, so upon returning
	 * to the caller, the task will no longer run.
	 */
	lockmgr(&thr->lock, LK_EXCLUSIVE);
	st->running = 0;
	lockmgr(&thr->lock, LK_RELEASE);
}

struct sensor_task *
sensor_task_register2(void *arg, void (*func)(void *), int period, int cpu)
{
	struct sensor_taskthr	*thr;
	struct sensor_task	*st;

	KASSERT(cpu >= 0 && cpu < ncpus, ("invalid cpuid %d", cpu));
	thr = &sensor_task_threads[cpu];

	st = kmalloc(sizeof(struct sensor_task), M_DEVBUF, M_WAITOK);

	lockmgr(&thr->lock, LK_EXCLUSIVE);
	st->arg = arg;
	st->func = func;
	st->period = period;
	st->cpuid = cpu;

	st->running = 1;

	st->nextrun = 0;
	TAILQ_INSERT_HEAD(&thr->list, st, entry);

	wakeup(&thr->list);

	lockmgr(&thr->lock, LK_RELEASE);

	return st;
}

static void
sensor_task_thread(void *xthr)
{
	struct sensor_taskthr	*thr = xthr;
	struct sensor_task	*st, *nst;
	time_t			now;

	lockmgr(&thr->lock, LK_EXCLUSIVE);

	for (;;) {
		while (TAILQ_EMPTY(&thr->list))
			lksleep(&thr->list, &thr->lock, 0, "waittask", 0);

		while ((nst = TAILQ_FIRST(&thr->list))->nextrun >
		    (now = time_uptime)) {
			lksleep(&thr->list, &thr->lock, 0,
			    "timeout", (nst->nextrun - now) * hz);
		}

		while ((st = nst) != NULL) {
			nst = TAILQ_NEXT(st, entry);

			if (st->nextrun > now)
				break;

			/* take it out while we work on it */
			TAILQ_REMOVE(&thr->list, st, entry);

			if (!st->running) {
				kfree(st, M_DEVBUF);
				continue;
			}

			/* run the task */
			st->func(st->arg);
			/* stick it back in the tasklist */
			sensor_task_schedule(thr, st);
		}
	}

	lockmgr(&thr->lock, LK_RELEASE);
}

static void
sensor_task_schedule(struct sensor_taskthr *thr, struct sensor_task *st)
{
	struct sensor_task 	*cst;

	KASSERT(lockstatus(&thr->lock, curthread) == LK_EXCLUSIVE,
	    ("sensor task lock is not held"));

	st->nextrun = time_uptime + st->period;

	TAILQ_FOREACH(cst, &thr->list, entry) {
		if (cst->nextrun > st->nextrun) {
			TAILQ_INSERT_BEFORE(cst, st, entry);
			return;
		}
	}

	/* must be an empty list, or at the end of the list */
	TAILQ_INSERT_TAIL(&thr->list, st, entry);
}

/*
 * sysctl glue code
 */
static int	sysctl_handle_sensordev(SYSCTL_HANDLER_ARGS);
static int	sysctl_handle_sensor(SYSCTL_HANDLER_ARGS);
static int	sysctl_sensors_handler(SYSCTL_HANDLER_ARGS);

SYSCTL_NODE(_hw, OID_AUTO, sensors, CTLFLAG_RD, NULL,
    "Hardware Sensors sysctl internal magic");
SYSCTL_NODE(_hw, HW_SENSORS, _sensors, CTLFLAG_RD, sysctl_sensors_handler,
    "Hardware Sensors XP MIB interface");

SYSCTL_INT(_hw_sensors, OID_AUTO, dev_idmax, CTLFLAG_RD,
    &sensordev_idmax, 0, "Max sensor device id");

static void
sensordev_sysctl_install(struct ksensordev *sensdev)
{
	struct sysctl_ctx_list *cl = &sensdev->clist;
	struct ksensor *s;
	struct ksensors_head *sh = &sensdev->sensors_list;

	SYSCTL_ASSERT_XLOCKED();

	KASSERT(sensdev->oid == NULL,
	    ("sensor device %s sysctl node already installed", sensdev->xname));

	sysctl_ctx_init(cl);
	sensdev->oid = SYSCTL_ADD_NODE(cl, SYSCTL_STATIC_CHILDREN(_hw_sensors),
	    sensdev->num, sensdev->xname, CTLFLAG_RD, NULL, "");
	if (sensdev->oid == NULL) {
		kprintf("sensor: add sysctl tree for %s failed\n",
		    sensdev->xname);
		return;
	}

	/* Install sysctl nodes for sensors attached to this sensor device */
	SLIST_FOREACH(s, sh, list)
		sensor_sysctl_install(sensdev, s);
}

static void
sensor_sysctl_install(struct ksensordev *sensdev, struct ksensor *sens)
{
	char n[32];

	SYSCTL_ASSERT_XLOCKED();

	if (sensdev->oid == NULL) {
		/* Sensor device sysctl node is not installed yet */
		return;
	}

	ksnprintf(n, sizeof(n), "%s%d", sensor_type_s[sens->type], sens->numt);
	KASSERT(sens->oid == NULL,
	    ("sensor %s:%s sysctl node already installed", sensdev->xname, n));

	sens->oid = SYSCTL_ADD_PROC(&sensdev->clist,
	    SYSCTL_CHILDREN(sensdev->oid), OID_AUTO, n,
	    CTLTYPE_STRUCT | CTLFLAG_RD, sens, 0, sysctl_handle_sensor,
	    "S,sensor", "");
}

static void
sensordev_sysctl_deinstall(struct ksensordev *sensdev)
{
	SYSCTL_ASSERT_XLOCKED();

	if (sensdev->oid != NULL) {
		sysctl_ctx_free(&sensdev->clist);
		sensdev->oid = NULL;
	}
}

static void
sensor_sysctl_deinstall(struct ksensordev *sensdev, struct ksensor *sens)
{
	SYSCTL_ASSERT_XLOCKED();

	if (sensdev->oid != NULL && sens->oid != NULL) {
		sysctl_ctx_entry_del(&sensdev->clist, sens->oid);
		sysctl_remove_oid(sens->oid, 1, 0);
	}
	sens->oid = NULL;
}

static int
sysctl_handle_sensordev(SYSCTL_HANDLER_ARGS)
{
	struct ksensordev *ksd = arg1;
	struct sensordev *usd;
	int error;

	if (req->newptr)
		return (EPERM);

	/* Grab a copy, to clear the kernel pointers */
	usd = kmalloc(sizeof(*usd), M_TEMP, M_WAITOK | M_ZERO);
	usd->num = ksd->num;
	strlcpy(usd->xname, ksd->xname, sizeof(usd->xname));
	memcpy(usd->maxnumt, ksd->maxnumt, sizeof(usd->maxnumt));
	usd->sensors_count = ksd->sensors_count;

	error = SYSCTL_OUT(req, usd, sizeof(struct sensordev));

	kfree(usd, M_TEMP);
	return (error);
}

static int
sysctl_handle_sensor(SYSCTL_HANDLER_ARGS)
{
	struct ksensor *ks = arg1;
	struct sensor *us;
	int error;

	if (req->newptr)
		return (EPERM);

	/* Grab a copy, to clear the kernel pointers */
	us = kmalloc(sizeof(*us), M_TEMP, M_WAITOK | M_ZERO);
	memcpy(us->desc, ks->desc, sizeof(ks->desc));
	us->tv = ks->tv;
	us->value = ks->value;
	us->type = ks->type;
	us->status = ks->status;
	us->numt = ks->numt;
	us->flags = ks->flags;

	error = SYSCTL_OUT(req, us, sizeof(struct sensor));

	kfree(us, M_TEMP);
	return (error);
}

static int
sysctl_sensors_handler(SYSCTL_HANDLER_ARGS)
{
	int *name = arg1;
	u_int namelen = arg2;
	struct ksensordev *ksd;
	struct ksensor *ks;
	int dev, numt;
	enum sensor_type type;

	if (namelen != 1 && namelen != 3)
		return (ENOTDIR);

	dev = name[0];
	if ((ksd = sensordev_get(dev)) == NULL)
		return (ENOENT);

	if (namelen == 1)
		return (sysctl_handle_sensordev(NULL, ksd, 0, req));

	type = name[1];
	numt = name[2];

	if ((ks = sensor_find(ksd, type, numt)) == NULL)
		return (ENOENT);
	return (sysctl_handle_sensor(NULL, ks, 0, req));
}

static void
sensor_sysinit(void *arg __unused)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct sensor_taskthr *thr = &sensor_task_threads[cpu];
		int error;

		TAILQ_INIT(&thr->list);
		lockinit(&thr->lock, "sensorthr", 0, LK_CANRECURSE);

		error = kthread_create_cpu(sensor_task_thread, thr, NULL, cpu,
		    "sensors %d", cpu);
		if (error)
			panic("sensors kthread on cpu%d failed: %d", cpu, error);
	}
}
SYSINIT(sensor, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, sensor_sysinit, NULL);
