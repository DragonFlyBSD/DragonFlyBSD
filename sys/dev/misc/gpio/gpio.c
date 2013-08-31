/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 *
 *
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2004, 2006 Alexander Yurchenko <grange@openbsd.org>
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
 * XXX: consumer_detach stuff.
 * XXX: userland stuff.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <dev/misc/gpio/gpio.h>
#include <sys/devfs.h>

struct gpio_driver {
	char	*name;
	struct devfs_bitmap	unit_bitmap;
	LIST_ENTRY(gpio_driver)	link;
};

static LIST_HEAD(, gpio_consumer) gpio_conslist = LIST_HEAD_INITIALIZER(&gpio_conslist);
static LIST_HEAD(, gpio_driver) gpio_driverlist = LIST_HEAD_INITIALIZER(&gpio_driverlist);
DEVFS_DECLARE_CLONE_BITMAP(gpio);
static struct lock gpio_lock;

void
gpio_consumer_register(struct gpio_consumer *gcp)
{
	lockmgr(&gpio_lock, LK_EXCLUSIVE);
	LIST_INSERT_HEAD(&gpio_conslist, gcp, link);
	lockmgr(&gpio_lock, LK_RELEASE);
}

void
gpio_consumer_unregister(struct gpio_consumer *gcp)
{
	lockmgr(&gpio_lock, LK_EXCLUSIVE);
	LIST_REMOVE(gcp, link);
	lockmgr(&gpio_lock, LK_RELEASE);
}

int
gpio_consumer_attach(const char *consumer, void *arg, struct gpio *gp,
	    int pin, u_int32_t mask)
{
	struct gpio_consumer *gcp;
	int error = -1;
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the lock */
	if ((lockstatus(&gpio_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&gpio_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	LIST_FOREACH(gcp, &gpio_conslist, link) {
		if (strcmp(gcp->consumer_name, consumer) != 0)
			continue;

		if (gcp->consumer_attach)
			error = gcp->consumer_attach(gp, arg, pin, mask);
		if (error) {
			kprintf("gpio: Attach of consumer %s to gpio %s%d pin %d failed "
			    "(consumer error %d)\n", consumer, gp->driver_name,
			    gp->driver_unit, pin, error);
			goto end;
		}

		kprintf("gpio: Attached consumer %s to gpio %s%d pin %d\n",
		    consumer, gp->driver_name, gp->driver_unit, pin);
		goto end;
	}

	kprintf("gpio: Attach of consumer %s to gpio %s%d pin %d failed "
	    "(unknown consumer)\n", consumer, gp->driver_name, gp->driver_unit, pin);

end:
	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&gpio_lock, LK_RELEASE);
	return error;
}

int
gpio_consumer_detach(const char *consumer, struct gpio *gp,
		int pin)
{
	struct gpio_consumer *gcp;
	int error = -1;
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the lock */
	if ((lockstatus(&gpio_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&gpio_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	LIST_FOREACH(gcp, &gpio_conslist, link) {
		if (strcmp(gcp->consumer_name, consumer) != 0)
			continue;

		if (gcp->consumer_detach)
			error = gcp->consumer_detach(gp, NULL, pin);
		if (error) {
			kprintf("gpio: Detach of consumer %s from gpio %s%d pin %d failed "
			    "(consumer error %d)\n", consumer, gp->driver_name,
			    gp->driver_unit, pin, error);
			goto end;
		}

		kprintf("gpio: Detached consumer %s from gpio %s%d pin %d\n",
		    consumer, gp->driver_name, gp->driver_unit, pin);
		goto end;
	}

	kprintf("gpio: Detach of consumer %s from gpio %s%d pin %d failed "
	    "(unknown consumer)\n", consumer, gp->driver_name, gp->driver_unit, pin);

end:
	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&gpio_lock, LK_RELEASE);
	return error;
}

struct gpio_mapping *
gpio_map(struct gpio *gp, int *map, int offset, u_int32_t mask)
{
	struct gpio_mapping *gmp;
	int npins, pin, i;
	int locked = 0;

	npins = gpio_npins(mask);
	if (npins > gp->npins)
		return NULL;
	if (npins == 0)
		return NULL;

	/* Check if it is locked already. if not, we acquire the lock */
	if ((lockstatus(&gpio_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&gpio_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	gmp = kmalloc(sizeof(struct gpio_mapping), M_TEMP, M_WAITOK);
	gmp->gp = gp;
	if (map) {
		gmp->map = map;
		gmp->map_alloced = 0;
	} else {
		gmp->map = kmalloc(sizeof(int) * npins, M_TEMP, M_WAITOK);
		gmp->map_alloced = 1;
	}

	for (npins = 0, i = 0; i < 32; i++)
		if (mask & (1 << i)) {
			pin = offset + i;
			if (pin < 0 || pin >= gp->npins ||
				gp->pins[pin].pin_mapped || gp->pins[pin].pin_opened) {
				if (map == NULL)
					kfree(gmp->map, M_TEMP);
				kfree(gmp, M_TEMP);
				/* If we acquired the lock, we also get rid of it */
				if (locked)
					lockmgr(&gpio_lock, LK_RELEASE);
				return NULL;
			}
			gp->pins[pin].pin_mapped = 1;
			gmp->map[npins++] = pin;
		}
	gmp->size = npins;

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&gpio_lock, LK_RELEASE);

	return gmp;
}

void
gpio_unmap(struct gpio_mapping *gmp)
{
	int pin, i;
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the lock */
	if ((lockstatus(&gpio_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&gpio_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	for (i = 0; i < gmp->size; i++) {
		pin = gmp->map[i];
		gmp->gp->pins[pin].pin_mapped = 0;
	}

	if (gmp->map_alloced)
		kfree(gmp->map, M_TEMP);
	kfree(gmp, M_TEMP);

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&gpio_lock, LK_RELEASE);
}

int
gpio_npins(u_int32_t mask)
{
	int npins, i;

	for (npins = 0, i = 0; i < 32; i++)
		if (mask & (1 << i))
			npins++;

	return (npins);
}

int
gpio_pin_read(struct gpio *gp, struct gpio_mapping *map, int pin)
{
	return gp->pin_read(gp->arg, map->map[pin]);
}

void
gpio_pin_write(struct gpio *gp, struct gpio_mapping *map, int pin, int data)
{
	gp->pin_write(gp->arg, map->map[pin], data);
}

void
gpio_pin_ctl(struct gpio *gp, struct gpio_mapping *map, int pin, int flags)
{
	gp->pin_ctl(gp->arg, map->map[pin], flags);
}

int
gpio_pin_caps(struct gpio *gp, struct gpio_mapping *map, int pin)
{
	return (gp->pins[map->map[pin]].pin_caps);
}

static int
gpio_open(struct dev_open_args *ap)
{
	gpio_pin_t	*pin;
	cdev_t	dev;

	dev = ap->a_head.a_dev;
	pin = dev->si_drv2;

	if (pin->pin_opened || pin->pin_mapped)
		return EBUSY;

	pin->pin_opened = 1;

	return 0;
}

static int
gpio_close(struct dev_close_args *ap)
{
	gpio_pin_t	*pin;
	cdev_t	dev;

	dev = ap->a_head.a_dev;
	pin = dev->si_drv2;

	if (pin->pin_opened)
		pin->pin_opened = 0;

	return 0;
}

static int
gpio_write(struct dev_write_args *ap)
{
	struct gpio	*gp;
	gpio_pin_t	*pin;
	cdev_t		dev;
	int		error;
	int		data = 0;

	dev = ap->a_head.a_dev;
	gp = dev->si_drv1;
	pin = dev->si_drv2;

	if (ap->a_uio->uio_resid > sizeof(int))
		return EINVAL;

	error = uiomove((void *)&data, ap->a_uio->uio_resid, ap->a_uio);
	if (error)
		return error;

	if (data != GPIO_PIN_LOW && data != GPIO_PIN_HIGH)
		return EINVAL;

	gp->pin_write(gp->arg, pin->pin_num, data);
	pin->pin_state = data;

	return 0;
}

static int
gpio_read(struct dev_read_args *ap)
{
	struct gpio	*gp;
	gpio_pin_t	*pin;
	cdev_t		dev;
	int		error;
	int		data = 0;

	dev = ap->a_head.a_dev;
	gp = dev->si_drv1;
	pin = dev->si_drv2;

	if (ap->a_uio->uio_resid < sizeof(char))
		return EINVAL;

	data = gp->pin_read(gp->arg, pin->pin_num);

	error = uiomove((void *)&data,
	    (ap->a_uio->uio_resid > sizeof(int))?(sizeof(int)):(ap->a_uio->uio_resid),
		ap->a_uio);

	return error;
}

static int
gpio_ioctl(struct dev_ioctl_args *ap)
{
	struct gpio_pin_set_args *gpsa;
	struct gpio	*gp;
	gpio_pin_t	*pin;
	cdev_t		dev;

	dev = ap->a_head.a_dev;
	gpsa = (struct gpio_pin_set_args *)ap->a_data;
	gp = dev->si_drv1;
	pin = dev->si_drv2;

	switch(ap->a_cmd) {
	case GPIOPINSET:
		if (pin->pin_opened || pin->pin_mapped)
			return EBUSY;

		gpsa->caps = pin->pin_caps;
		gpsa->flags = pin->pin_flags;

		if ((gpsa->flags & pin->pin_caps) != gpsa->flags)
			return ENODEV;

		if (gpsa->flags > 0) {
			gp->pin_ctl(gp->arg, pin->pin_num, gpsa->flags);
			pin->pin_flags = gpsa->flags | GPIO_PIN_SET;
		}
		break;

	case GPIOPINUNSET:
		return EINVAL;
		break;

	default:
		return EINVAL;
	}
	return 0;
}

static int
gpio_master_ioctl(struct dev_ioctl_args *ap)
{
	struct gpio_pin_set_args *gpsa;
	struct gpio_info	*gpi;
	struct gpio_attach_args	*gpaa;
	struct gpio	*gp;
	cdev_t		dev;
	gpio_pin_t	*pin;
	int		error = 0;

	dev = ap->a_head.a_dev;
	gp = dev->si_drv1;

	switch(ap->a_cmd) {
	case GPIOINFO:
		gpi = (struct gpio_info *)ap->a_data;
		gpi->npins = gp->npins;
		if (gpi->pins != NULL) {
			error = copyout(gp->pins, gpi->pins,
			    sizeof(struct gpio_pin)*gp->npins);
		}
		break;

	case GPIOATTACH:
		gpaa = (struct gpio_attach_args *)ap->a_data;
		error = gpio_consumer_attach(gpaa->consumer_name,
		    (gpaa->arg_type == GPIO_TYPE_INT)?
		    ((void *)gpaa->consumer_arg.lint):
		    (gpaa->consumer_arg.string),
		    gp, gpaa->pin_offset, gpaa->pin_mask);
		break;

	case GPIODETACH:
		gpaa = (struct gpio_attach_args *)ap->a_data;
		error = gpio_consumer_detach(gpaa->consumer_name, gp,
		    gpaa->pin_offset);
		break;

	case GPIOPINSET:
		gpsa = (struct gpio_pin_set_args *)ap->a_data;
		if (gpsa->pin < 0 || gpsa->pin >= gp->npins)
			return EINVAL;

		pin = &gp->pins[gpsa->pin];

		if (pin->pin_opened || pin->pin_mapped)
			return EBUSY;

		gpsa->caps = pin->pin_caps;
		gpsa->flags = pin->pin_flags;

		if ((gpsa->flags & pin->pin_caps) != gpsa->flags)
			return ENODEV;

		if (gpsa->flags > 0) {
			gp->pin_ctl(gp->arg, gpsa->pin, gpsa->flags);
			pin->pin_flags = gpsa->flags | GPIO_PIN_SET;
		}
		break;

	case GPIOPINUNSET:
		gpsa = (struct gpio_pin_set_args *)ap->a_data;
		error = EINVAL;
		break;

	default:
		return EINVAL;
	}

	return error;
}

static struct dev_ops gpio_ops = {
	{ "gpio", 0, 0 },
	.d_open  =	gpio_open,
	.d_close =	gpio_close,
	.d_write = 	gpio_write,
	.d_read  =	gpio_read,
	.d_ioctl =	gpio_ioctl,
};

static struct dev_ops gpio_master_ops = {
	{ "gpio", 0, 0 },
	.d_ioctl =	gpio_master_ioctl,
};

void
gpio_register(struct gpio *gp)
{
	struct gpio_driver *gpd;
	int i, unit, master_unit = -1;

	KKASSERT(gp->npins > 0);
	KKASSERT(gp->pins);

	lockmgr(&gpio_lock, LK_EXCLUSIVE);
	LIST_FOREACH(gpd, &gpio_driverlist, link) {
		if (strcmp(gpd->name, gp->driver_name) != 0)
			continue;

		master_unit = devfs_clone_bitmap_get(&gpd->unit_bitmap, 0);
		break;
	}
	if (master_unit == -1) {
		gpd = kmalloc(sizeof(struct gpio_driver),
		    M_TEMP, M_WAITOK | M_ZERO);
		gpd->name = kstrdup(gp->driver_name, M_TEMP);
		devfs_clone_bitmap_init(&gpd->unit_bitmap);
		master_unit = devfs_clone_bitmap_get(&gpd->unit_bitmap, 0);
		LIST_INSERT_HEAD(&gpio_driverlist, gpd, link);
	}
	lockmgr(&gpio_lock, LK_RELEASE);

	gp->driver_unit = master_unit;
	kprintf("gpio: GPIO driver %s%d registered, npins = %d\n",
	    gp->driver_name, master_unit, gp->npins);

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(gpio), 0);
	gp->master_dev = make_dev(&gpio_master_ops, unit, UID_ROOT, GID_WHEEL, 0600,
	    "gpio/%s%d/master", gp->driver_name, master_unit);
	gp->master_dev->si_drv1 = gp;

	for (i = 0; i < gp->npins; i++) {
		unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(gpio), 0);
		gp->pins[i].dev = make_dev(&gpio_ops, unit, UID_ROOT, GID_WHEEL, 0600,
		    "gpio/%s%d/%d", gp->driver_name, master_unit, gp->pins[i].pin_num);
		gp->pins[i].dev->si_drv1 = gp;
		gp->pins[i].dev->si_drv2 = &gp->pins[i];
	}
}

void
gpio_unregister(struct gpio *gp)
{
	struct gpio_driver *gpd;
	int i;

	KKASSERT(gp->npins > 0);
	KKASSERT(gp->pins);

	for (i = 0; i < gp->npins; i++) {
		devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(gpio),
		    minor(gp->pins[i].dev));
		destroy_dev(gp->pins[i].dev);
	}

	destroy_dev(gp->master_dev);

	lockmgr(&gpio_lock, LK_EXCLUSIVE);
	LIST_FOREACH(gpd, &gpio_driverlist, link) {
		if (strcmp(gpd->name, gp->driver_name) != 0)
			continue;

		devfs_clone_bitmap_put(&gpd->unit_bitmap, gp->driver_unit);
		LIST_REMOVE(gpd, link);
		break;
	}
	lockmgr(&gpio_lock, LK_RELEASE);

	kprintf("gpio: GPIO driver %s%d unregistered\n",
	    gp->driver_name, gp->driver_unit);
}

static void
gpio_drvinit(void *unused)
{
	lockinit(&gpio_lock, "gpio_lock", 0, 0);
	devfs_clone_bitmap_init(&DEVFS_CLONE_BITMAP(gpio));
}

SYSINIT(gpio, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, gpio_drvinit, NULL);
