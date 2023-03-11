/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Emmanuel Vadot <manu@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/lock.h>

#include <dev/misc/backlight/backlight.h>

#include "backlight_if.h"

static struct lock backlight_lock;
static MALLOC_DEFINE(M_BACKLIGHT, "BACKLIGHT", "Backlight driver");
static struct unrhdr *backlight_unit;

struct backlight_softc {
	struct cdev *cdev;
	struct cdev *alias;
	int unit;
	device_t dev;
	uint32_t cached_brightness;
};

static int
backlight_ioctl(struct dev_ioctl_args *ap)
{
	struct backlight_softc *sc;
	struct backlight_props props;
	struct backlight_info info;
	cdev_t dev = ap->a_head.a_dev;
	int error = 0;

	sc = dev->si_drv1;

	switch (ap->a_cmd) {
	case BACKLIGHTGETSTATUS:
		/* Call the driver function so it fills up the props */
		bcopy(ap->a_data, &props, sizeof(struct backlight_props));
		error = BACKLIGHT_GET_STATUS(sc->dev, &props);
		if (error == 0) {
			bcopy(&props, ap->a_data, sizeof(struct backlight_props));
			sc->cached_brightness = props.brightness;
		}
		break;
	case BACKLIGHTUPDATESTATUS:
		bcopy(ap->a_data, &props, sizeof(struct backlight_props));
		if (props.brightness == sc->cached_brightness)
			return (0);
		error = BACKLIGHT_UPDATE_STATUS(sc->dev, &props);
		if (error == 0) {
			bcopy(&props, ap->a_data, sizeof(struct backlight_props));
			sc->cached_brightness = props.brightness;
		}
		break;
	case BACKLIGHTGETINFO:
		memset(&info, 0, sizeof(info));
		error = BACKLIGHT_GET_INFO(sc->dev, &info);
		if (error == 0)
			bcopy(&info, ap->a_data, sizeof(struct backlight_info));
		break;
	}

	return (error);
}

static struct dev_ops backlight_cdevsw = {
	.head = { .name = "backlight", .flags = D_MPSAFE },
	.d_ioctl =	backlight_ioctl,
};

struct cdev *
backlight_register(const char *name, device_t dev)
{
	struct backlight_softc *sc;
	struct backlight_props props;
	int error;

	sc = kmalloc(sizeof(*sc), M_BACKLIGHT, M_WAITOK | M_ZERO);

	lockmgr(&backlight_lock, LK_EXCLUSIVE);
	sc->unit = alloc_unr(backlight_unit);
	sc->dev = dev;
	make_dev(&backlight_cdevsw, sc->unit, UID_ROOT, GID_VIDEO,
	    0660, "backlight/backlight%d", sc->unit);
	sc->cdev->si_drv1 = sc;
	make_dev_alias(sc->cdev, "backlight/%s%d", name, sc->unit);

	lockmgr(&backlight_lock, LK_RELEASE);

	error = BACKLIGHT_GET_STATUS(sc->dev, &props);
	sc->cached_brightness = props.brightness;

	return (sc->cdev);
}

int
backlight_destroy(struct cdev *dev)
{
	struct backlight_softc *sc;

	sc = dev->si_drv1;
	lockmgr(&backlight_lock, LK_EXCLUSIVE);
	free_unr(backlight_unit, sc->unit);
	destroy_dev(dev);
	lockmgr(&backlight_lock, LK_RELEASE);
	return (0);
}

static void
backlight_drvinit(void *unused)
{

	backlight_unit = new_unrhdr(0, INT_MAX, NULL);
	lockuninit(&backlight_lock);
}

SYSINIT(backlightdev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, backlight_drvinit, NULL);
MODULE_VERSION(backlight, 1);
