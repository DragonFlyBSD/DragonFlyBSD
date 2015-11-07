/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
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

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <dev/acpica/acpi_pvpanic/panic_notifier.h>

#include "acpi.h"

#include <dev/acpica/acpivar.h>

#define PVPANIC_GUESTPANIC	(1 << 0)

ACPI_MODULE_NAME("pvpanic")

struct acpi_pvpanic_softc {
	device_t		dev;
	ACPI_HANDLE		handle;
	int			pvpanic_port_rid;
	struct resource		*pvpanic_port_res;
	bus_space_tag_t		pvpanic_iot;
	bus_space_handle_t	pvpanic_ioh;
	struct panicerinfo	pvpanic_info;
};

static int	acpi_pvpanic_probe(device_t dev);
static int	acpi_pvpanic_attach(device_t dev);
static int	acpi_pvpanic_detach(device_t dev);
static void	acpi_pvpanic_panic(void *arg);

static device_method_t acpi_pvpanic_methods[] = {
	DEVMETHOD(device_probe,		acpi_pvpanic_probe),
	DEVMETHOD(device_attach,	acpi_pvpanic_attach),
	DEVMETHOD(device_detach,	acpi_pvpanic_detach),

	DEVMETHOD_END
};

static driver_t acpi_pvpanic_driver = {
	"acpi_pvpanic",
	acpi_pvpanic_methods,
	sizeof(struct acpi_pvpanic_softc),
};

static devclass_t acpi_pvpanic_devclass;
DRIVER_MODULE(acpi_pvpanic, acpi, acpi_pvpanic_driver, acpi_pvpanic_devclass,
    NULL, NULL);
MODULE_DEPEND(acpi_pvpanic, acpi, 1, 1, 1);
MODULE_VERSION(acpi_pvpanic, 1);

static int
acpi_pvpanic_probe(device_t dev)
{
	static char *pvpanic_ids[] = { "QEMU0001", NULL };

	if (acpi_disabled("pvpanic") ||
	    ACPI_ID_PROBE(device_get_parent(dev), dev, pvpanic_ids) == NULL ||
	    device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "Qemu pvpanic device");
	return (0);
}

static int
acpi_pvpanic_attach(device_t dev)
{
	struct	acpi_pvpanic_softc *sc;
	uint8_t val;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->handle = acpi_get_handle(dev);

	sc->pvpanic_port_rid = 0;
	sc->pvpanic_port_res = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->pvpanic_port_rid, RF_ACTIVE);
	if (sc->pvpanic_port_res == NULL) {
		device_printf(dev, "can't allocate pvpanic port\n");
		return ENXIO;
	}
	sc->pvpanic_iot = rman_get_bustag(sc->pvpanic_port_res);
	sc->pvpanic_ioh = rman_get_bushandle(sc->pvpanic_port_res);

	/* Check for guest panic notification support */
	val = bus_space_read_1(sc->pvpanic_iot, sc->pvpanic_ioh, 0);
	if (!(val & PVPANIC_GUESTPANIC)) {
		device_printf(dev, "No PVPANIC_GUESTPANIC support\n");
		goto fail;
	}

	device_printf(dev, "Registering panic callback\n");
	sc->pvpanic_info.notifier = acpi_pvpanic_panic;
	sc->pvpanic_info.arg = sc;
	if (set_panic_notifier(&sc->pvpanic_info) != 0) {
		device_printf(dev, "Failed to register panic notifier\n");
		goto fail;
	}
	return (0);

fail:
	bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->pvpanic_port_rid, sc->pvpanic_port_res);
	return ENXIO;
}

static int
acpi_pvpanic_detach(device_t dev)
{
	struct	acpi_pvpanic_softc *sc = device_get_softc(dev);

	set_panic_notifier(NULL);

	bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->pvpanic_port_rid, sc->pvpanic_port_res);

	return (0);
}

static void
acpi_pvpanic_panic(void *arg)
{
	struct acpi_pvpanic_softc *sc = (struct acpi_pvpanic_softc *)arg;

	bus_space_write_1(sc->pvpanic_iot, sc->pvpanic_ioh, 0,
	    PVPANIC_GUESTPANIC);
}
