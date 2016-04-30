/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Imre Vadász <imre@vdsz.com>
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
/*
 * Intel SoC gpio driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <sys/rman.h>

#include "opt_acpi.h"
#include "acpi.h"
#include <dev/acpica/acpivar.h>

#include <bus/pci/pcivar.h>

#include <bus/gpio/gpio_acpi/gpio_acpivar.h>

#include "gpio_intel_var.h"

#include "gpio_if.h"

static int	gpio_intel_probe(device_t dev);
static int	gpio_intel_attach(device_t dev);
static int	gpio_intel_detach(device_t dev);
static int	gpio_intel_alloc_intr(device_t dev, u_int pin, int trigger,
		    int polarity, int termination, void **cookiep);
static void	gpio_intel_setup_intr(device_t dev, void *cookie, void *arg,
		    driver_intr_t *handler);
static void	gpio_intel_teardown_intr(device_t dev, void *cookie);
static void	gpio_intel_free_intr(device_t dev, void *cookie);
static int	gpio_intel_alloc_io_pin(device_t dev, u_int pin, int flags,
		    void **cookiep);
static void	gpio_intel_release_io_pin(device_t dev, void *cookie);
static int	gpio_intel_read_pin(device_t dev, void *cookie);
static void	gpio_intel_write_pin(device_t dev, void *cookie, int value);

static void	gpio_intel_intr(void *arg);
static int	gpio_intel_pin_exists(struct gpio_intel_softc *sc,
		    uint16_t pin);

static char *cherryview_ids[] = { "INT33FF", NULL };

static int
gpio_intel_probe(device_t dev)
{

        if (acpi_disabled("gpio_intel") ||
            ACPI_ID_PROBE(device_get_parent(dev), dev, cherryview_ids) == NULL)
                return (ENXIO);

	device_set_desc(dev, "Intel Cherry Trail GPIO Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
gpio_intel_attach(device_t dev)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	int error, i, rid;

	lockinit(&sc->lk, "gpio_intel", 0, LK_CANRECURSE);

	sc->dev = dev;

        if (ACPI_ID_PROBE(device_get_parent(dev), dev, cherryview_ids)
	    != NULL) {
		error = gpio_cherryview_matchuid(sc);
		if (error) {
			error = ENXIO;
			goto err;
		}
	} else {
		error = ENXIO;
		goto err;
	}

	for (i = 0; i < NELEM(sc->intrmaps); i++) {
		sc->intrmaps[i].pin = -1;
	}
	for (i = 0; i < NELEM(sc->iomaps); i++) {
		sc->iomaps[i].pin = -1;
	}

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "unable to map registers");
		error = ENXIO;
		goto err;
	}
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "unable to map interrupt");
		error = ENXIO;
		goto err;
	}

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	/* Activate the interrupt */
	error = bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE,
	    gpio_intel_intr, sc, &sc->intrhand, NULL);
	if (error)
		device_printf(dev, "Can't setup IRQ\n");

	/* power up the controller */
	pci_set_powerstate(dev, PCI_POWERSTATE_D0);

	sc->fns->init(sc);
	lockmgr(&sc->lk, LK_RELEASE);

	sc->acpireg = gpio_acpi_register(dev);

	return (0);

err:
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		sc->irq_res = NULL;
	}
	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
		sc->mem_res = NULL;
	}
	return (error);
}

static int
gpio_intel_detach(device_t dev)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);

	gpio_acpi_unregister(dev, sc->acpireg);

	bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		sc->irq_res = NULL;
	}
	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
		sc->mem_res = NULL;
	}
	lockuninit(&sc->lk);

	pci_set_powerstate(dev, PCI_POWERSTATE_D3);

	return 0;
}

/*
 * The trigger, polarity and termination parameters are only used for
 * sanity checking. The gpios should already be configured correctly by
 * the firmware.
 */
static int
gpio_intel_alloc_intr(device_t dev, u_int pin, int trigger, int polarity,
    int termination, void **cookiep)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_intr_map *map = NULL;
	int i, ret;

	if (cookiep == NULL)
		return (EINVAL);

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	if (gpio_intel_pin_exists(sc, pin)) {
		/* Make sure this pin isn't mapped yet */
		for (i = 0; i < NELEM(sc->intrmaps); i++) {
			if (sc->intrmaps[i].pin == pin) {
				lockmgr(&sc->lk, LK_RELEASE);
				return (EBUSY);
			}
		}
		ret = sc->fns->map_intr(sc, pin, trigger, polarity,
		    termination);
		if (ret == 0) {
			/* XXX map_intr should return the pin_intr_map */
			for (i = 0; i < NELEM(sc->intrmaps); i++) {
				if (sc->intrmaps[i].pin == pin)
					map = &sc->intrmaps[i];
			}
			if (map != NULL) {
				*cookiep = map;
				map->arg = NULL;
				map->handler = NULL;
			}
		}
	} else {
		device_printf(sc->dev, "%s: Invalid pin %d\n", __func__, pin);
		ret = ENOENT;
	}

	lockmgr(&sc->lk, LK_RELEASE);

	return (ret);
}

static void
gpio_intel_free_intr(device_t dev, void *cookie)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_intr_map *map = (struct pin_intr_map *)cookie;

	KKASSERT(gpio_intel_pin_exists(sc, map->pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	map->arg = NULL;
	map->handler = NULL;
	sc->fns->unmap_intr(sc, map);
	lockmgr(&sc->lk, LK_RELEASE);
}

static void
gpio_intel_setup_intr(device_t dev, void *cookie, void *arg,
    driver_intr_t *handler)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_intr_map *map = (struct pin_intr_map *)cookie;

	KKASSERT(gpio_intel_pin_exists(sc, map->pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	map->arg = arg;
	map->handler = handler;
	sc->fns->enable_intr(sc, map);
	lockmgr(&sc->lk, LK_RELEASE);
}

static void
gpio_intel_teardown_intr(device_t dev, void *cookie)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_intr_map *map = (struct pin_intr_map *)cookie;

	KKASSERT(gpio_intel_pin_exists(sc, map->pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	sc->fns->disable_intr(sc, map);
	map->arg = NULL;
	map->handler = NULL;
	lockmgr(&sc->lk, LK_RELEASE);
}

static int
gpio_intel_alloc_io_pin(device_t dev, u_int pin, int flags, void **cookiep)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_io_map *map = NULL;
	int i, ret;

	if (cookiep == NULL)
		return (EINVAL);

	if (!gpio_intel_pin_exists(sc, pin))
		return (ENOENT);

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	for (i = 0; i < NELEM(sc->iomaps); i++) {
		if (sc->iomaps[i].pin == pin) {
			lockmgr(&sc->lk, LK_RELEASE);
			return (EBUSY);
		}
	}
	for (i = 0; i < NELEM(sc->iomaps); i++) {
		if (sc->iomaps[i].pin == -1) {
			map = &sc->iomaps[i];
			break;
		}
	}
	if (map == NULL) {
		ret = EBUSY;
	} else if (sc->fns->check_io_pin(sc, pin, flags)) {
		/*
		 * XXX It's possible that RX gets disabled when the interrupt
		 *     on this pin gets released.
		 */
		map->pin = pin;
		map->flags = flags;
		*cookiep = map;
		ret = 0;
	} else {
		ret = EBUSY;
	}

	lockmgr(&sc->lk, LK_RELEASE);

	return (ret);
}

static void
gpio_intel_release_io_pin(device_t dev, void *cookie)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_io_map *map = (struct pin_io_map *)cookie;

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	map->pin = -1;
	map->flags = 0;
	lockmgr(&sc->lk, LK_RELEASE);
}

static int
gpio_intel_read_pin(device_t dev, void *cookie)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_io_map *map = (struct pin_io_map *)cookie;
	int val;

	KKASSERT(map->pin >= 0);
	KKASSERT(gpio_intel_pin_exists(sc, map->pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	val = sc->fns->read_pin(sc, map->pin);
	lockmgr(&sc->lk, LK_RELEASE);

	return (val);
}

static void
gpio_intel_write_pin(device_t dev, void *cookie, int value)
{
	struct gpio_intel_softc *sc = device_get_softc(dev);
	struct pin_io_map *map = (struct pin_io_map *)cookie;

	KKASSERT(map->pin >= 0);
	KKASSERT(gpio_intel_pin_exists(sc, map->pin));

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	sc->fns->write_pin(sc, map->pin, value);
	lockmgr(&sc->lk, LK_RELEASE);
}

static void
gpio_intel_intr(void *arg)
{
	struct gpio_intel_softc *sc = (struct gpio_intel_softc *)arg;

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	sc->fns->intr(arg);
	lockmgr(&sc->lk, LK_RELEASE);
}

static int
gpio_intel_pin_exists(struct gpio_intel_softc *sc, uint16_t pin)
{
	struct pinrange *r;

	for (r = sc->ranges; r->start != -1 && r->end != -1; r++) {
		if (r->start <= (int)pin && r->end >= (int)pin)
			return (TRUE);
	}

	return (FALSE);
}

static device_method_t gpio_intel_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, gpio_intel_probe),
	DEVMETHOD(device_attach, gpio_intel_attach),
	DEVMETHOD(device_detach, gpio_intel_detach),

	/* GPIO methods */
	DEVMETHOD(gpio_alloc_intr, gpio_intel_alloc_intr),
	DEVMETHOD(gpio_free_intr, gpio_intel_free_intr),
	DEVMETHOD(gpio_setup_intr, gpio_intel_setup_intr),
	DEVMETHOD(gpio_teardown_intr, gpio_intel_teardown_intr),
	DEVMETHOD(gpio_alloc_io_pin, gpio_intel_alloc_io_pin),
	DEVMETHOD(gpio_release_io_pin, gpio_intel_release_io_pin),
	DEVMETHOD(gpio_read_pin, gpio_intel_read_pin),
	DEVMETHOD(gpio_write_pin, gpio_intel_write_pin),

	DEVMETHOD_END
};

static driver_t gpio_intel_driver = {
        "gpio_intel",
        gpio_intel_methods,
        sizeof(struct gpio_intel_softc)
};

static devclass_t gpio_intel_devclass;

DRIVER_MODULE(gpio_intel, acpi, gpio_intel_driver, gpio_intel_devclass,
    NULL, NULL);
MODULE_DEPEND(gpio_intel, acpi, 1, 1, 1);
MODULE_DEPEND(gpio_intel, gpio_acpi, 1, 1, 1);
MODULE_VERSION(gpio_intel, 1);
