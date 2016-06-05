/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * Primary device interface for NVME driver, for DragonFlyBSD
 */

#include "nvme.h"

/*
 * Device bus methods
 */
static int	nvme_probe (device_t dev);
static int	nvme_attach (device_t dev);
static int	nvme_detach (device_t dev);
#if 0
static int	nvme_shutdown (device_t dev);
static int	nvme_suspend (device_t dev);
static int	nvme_resume (device_t dev);
#endif

static device_method_t nvme_methods[] = {
	DEVMETHOD(device_probe,		nvme_probe),
	DEVMETHOD(device_attach,	nvme_attach),
	DEVMETHOD(device_detach,	nvme_detach),
#if 0
	DEVMETHOD(device_shutdown,	nvme_shutdown),
	DEVMETHOD(device_suspend,	nvme_suspend),
	DEVMETHOD(device_resume,	nvme_resume),
#endif

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	DEVMETHOD_END
};

static devclass_t	nvme_devclass;

static driver_t nvme_driver = {
	"nvme",
	nvme_methods,
	sizeof(nvme_softc_t)
};

DRIVER_MODULE(nvme, pci, nvme_driver, nvme_devclass, NULL, NULL);
MODULE_VERSION(nvme, 1);

/*
 * Device bus method procedures
 */
static int
nvme_probe (device_t dev)
{
	const nvme_device_t *ad;

	if (kgetenv("hint.nvme.disabled"))
		return(ENXIO);

	ad = nvme_lookup_device(dev);
	if (ad) {
		device_set_desc(dev, ad->name);
		return(-5);	/* higher priority the NATA */
	}
	return(ENXIO);
}

static int
nvme_attach (device_t dev)
{
	nvme_softc_t *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->ad = nvme_lookup_device(dev);
	if (sc->ad == NULL)
		return(ENXIO);

	/* sanity check critical structure sizes */
	KKASSERT(sizeof(nvme_admin_data_t) == NVME_MAX_ADMIN_BUFFER);
	KKASSERT(sizeof(nvme_allres_t) == 16);
	KKASSERT(sizeof(nvme_allcmd_t) == 64);

	error = sc->ad->attach(dev);

	return error;
}

static int
nvme_detach (device_t dev)
{
	nvme_softc_t *sc = device_get_softc(dev);
	int error = 0;

	if (sc->ad) {
		error = sc->ad->detach(dev);
		sc->ad = NULL;
	}
	return(error);
}

#if 0

static int
nvme_shutdown (device_t dev)
{
	return (0);
}

static int
nvme_suspend (device_t dev)
{
	return (0);
}

static int
nvme_resume (device_t dev)
{
	return (0);
}

#endif

/*
 * Chipset register accesses (NVME_REG_*)
 */
u_int32_t
nvme_read(nvme_softc_t *sc, bus_size_t r)
{
	bus_space_barrier(sc->iot, sc->ioh, r, 4,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->iot, sc->ioh, r));
}

u_int64_t
nvme_read8(nvme_softc_t *sc, bus_size_t r)
{
	bus_space_barrier(sc->iot, sc->ioh, r, 8,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_8(sc->iot, sc->ioh, r));
}

void
nvme_write(nvme_softc_t *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->iot, sc->ioh, r, v);
	bus_space_barrier(sc->iot, sc->ioh, r, 4,
			  BUS_SPACE_BARRIER_WRITE);
}

void
nvme_write8(nvme_softc_t *sc, bus_size_t r, u_int64_t v)
{
	bus_space_write_8(sc->iot, sc->ioh, r, v);
	bus_space_barrier(sc->iot, sc->ioh, r, 8,
			  BUS_SPACE_BARRIER_WRITE);
}

/*
 * Sleep (ms) milliseconds, error on the side of caution.
 */
void
nvme_os_sleep(int ms)
{
	int slpticks;

	slpticks = hz * ms / 1000 + 1;
	tsleep(&slpticks, 0, "nvslp", slpticks);
}

/*
 * Sleep for a minimum interval and return the number of milliseconds
 * that was.  The minimum value returned is 1
 */
int
nvme_os_softsleep(void)
{
	int slpticks = 0;

	if (hz >= 1000) {
		tsleep(&slpticks, 0, "nvslp", hz / 1000);
		return(1);
	} else {
		tsleep(&slpticks, 0, "nvslp", 1);
		return(1000 / hz);
	}
}

void
nvme_os_hardsleep(int us)
{
	DELAY(us);
}
