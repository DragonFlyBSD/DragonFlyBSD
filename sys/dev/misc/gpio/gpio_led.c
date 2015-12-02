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
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <dev/misc/gpio/gpio.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/devfs.h>

struct ledsc {
	LIST_ENTRY(ledsc)	list;
	struct gpio *gp;
	int		pin;
	cdev_t	dev;
	int		unit;
	int		opened;
	char	*name;
	struct gpio_mapping *gp_map;
};

DEVFS_DEFINE_CLONE_BITMAP(nled);
static struct lock led_lock;
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(&led_list);
static MALLOC_DEFINE(M_LED, "LED", "LED driver");


static int
led_open(struct dev_open_args *ap)
{
	struct ledsc *sc;
	cdev_t	dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	if (sc->opened)
		return EBUSY;

	sc->opened = 1;

	return 0;
}

static int
led_close(struct dev_close_args *ap)
{
	struct ledsc *sc;
	cdev_t	dev;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	if (sc->opened)
		sc->opened = 0;

	return 0;
}

static int
led_write(struct dev_write_args *ap)
{
	struct ledsc *sc;
	cdev_t	dev;
	int		error;
	int		data = 0;
	int		len;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	if (ap->a_uio->uio_resid > sizeof(int))
		return EINVAL;

	len = ap->a_uio->uio_resid;

	error = uiomove((void *)&data, ap->a_uio->uio_resid, ap->a_uio);
	if (error)
		return error;

	if (len > 1)
		data = ((char *)&data)[0];

	if (data >= '0')
		data -= '0';

	gpio_pin_write(sc->gp, sc->gp_map, 0, data);

	return 0;
}

static int
led_read(struct dev_read_args *ap)
{
	struct ledsc *sc;
	cdev_t	dev;
	int		error;
	int		data = 0;

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;

	if (ap->a_uio->uio_resid < sizeof(int))
		return EINVAL;

	data = gpio_pin_read(sc->gp, sc->gp_map, 0);

	error = uiomove((void *)&data,
	    (ap->a_uio->uio_resid > sizeof(int))?(sizeof(int)):(ap->a_uio->uio_resid),
		ap->a_uio);

	return error;
}

static int
led_ioctl(struct dev_ioctl_args *ap)
{
	/* XXX: set a name */
	return 0;
}

static struct dev_ops nled_ops = {
	{ "gpio", 0, 0 },
	.d_open  =	led_open,
	.d_close =	led_close,
	.d_write = 	led_write,
	.d_read  =	led_read,
	.d_ioctl =	led_ioctl,
};


static int
led_attach(struct gpio *gp, void *arg, int pin, u_int32_t mask)
{
	struct ledsc *sc;

	if (arg == NULL)
		return 1;

	lockmgr(&led_lock, LK_EXCLUSIVE);
	sc = kmalloc(sizeof(struct ledsc), M_LED, M_WAITOK);

	/* XXX: check for name collisions */
	sc->name = kstrdup((char *)arg, M_LED);
	sc->pin = pin;
	sc->gp = gp;

	sc->gp_map = gpio_map(gp, NULL, pin, 1);
	if (sc->gp_map == NULL) {
		lockmgr(&led_lock, LK_RELEASE);
		return 2;
	}

	sc->unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(nled), 0);

	LIST_INSERT_HEAD(&led_list, sc, list);
	sc->dev = make_dev(&nled_ops, sc->unit,
	    UID_ROOT, GID_WHEEL, 0600, "led/%s", sc->name);
	sc->dev->si_drv1 = sc;
	lockmgr(&led_lock, LK_RELEASE);

	kprintf("gpio_led: Attached led '%s' to gpio %s, pin %d\n",
	    sc->name, sc->gp->driver_name, pin);

	return 0;
}

static int
led_detach(struct gpio *gp, void *arg, int pin)
{
	/* XXX: implement */
	return 0;
}

void
led_switch(const char *name, int on_off)
{
	struct ledsc *sc;

	if (name == NULL)
		return;

	lockmgr(&led_lock, LK_EXCLUSIVE);
	LIST_FOREACH(sc, &led_list, list) {
		if (strcmp(name, sc->name) != 0)
			continue;

		gpio_pin_write(sc->gp, sc->gp_map, 0, on_off);
		break;
	}

	lockmgr(&led_lock, LK_RELEASE);
}

struct gpio_consumer led_gpio_cons = {
	.consumer_name = "led",
	.consumer_attach = led_attach,
	.consumer_detach = led_detach,
};

static void
led_drvinit(void *unused)
{
	lockinit(&led_lock, "led_lock", 0, 0);
	devfs_clone_bitmap_init(&DEVFS_CLONE_BITMAP(nled));
	gpio_consumer_register(&led_gpio_cons);
}

SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
