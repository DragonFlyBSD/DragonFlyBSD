/*	$OpenBSD: km.c,v 1.2 2008/08/29 03:38:31 cnst Exp $	*/

/*
 * Copyright (c) 2008/2010 Constantine A. Murenin <cnst+dfly@bugmail.mojo.ru>
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
#include <sys/bus.h>
#include <sys/sensors.h>

#include <bus/pci/pcivar.h>
#include "pcidevs.h"


/*
 * AMD Family 10h Processors, Function 3 -- Miscellaneous Control
 */

/* Function 3 Registers */
#define KM_REP_TEMP_CONTR_R	0xa4
#define KM_THERMTRIP_STAT_R	0xe4
#define KM_NORTHBRIDGE_CAP_R	0xe8
#define KM_CPUID_FAMILY_MODEL_R	0xfc

/* Operations on Reported Temperature Control Register */
#define KM_GET_CURTMP(r)	(((r) >> 21) & 0x7ff)

/* Operations on Thermtrip Status Register */
#define KM_GET_DIODEOFFSET(r)	(((r) >> 8) & 0x7f)


struct km_softc {
	struct device		*sc_dev;
	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

static void	km_identify(driver_t *, struct device *);
static int	km_probe(struct device *);
static int	km_attach(struct device *);
static int	km_detach(struct device *);
static void	km_refresh(void *);

static device_method_t km_methods[] = {
	DEVMETHOD(device_identify,	km_identify),
	DEVMETHOD(device_probe,		km_probe),
	DEVMETHOD(device_attach,	km_attach),
	DEVMETHOD(device_detach,	km_detach),
	{ NULL, NULL }
};

static driver_t km_driver = {
	"km",
	km_methods,
	sizeof(struct km_softc)
};

static devclass_t km_devclass;

DRIVER_MODULE(km, hostb, km_driver, km_devclass, NULL, NULL);


static void
km_identify(driver_t *driver, struct device *parent)
{
	if (km_probe(parent) == ENXIO)
		return;
	if (device_find_child(parent, driver->name, -1) != NULL)
		return;
	device_add_child(parent, driver->name, -1);
}

static int
km_probe(struct device *dev)
{
	int ten = 0;

	if (pci_get_vendor(dev) != PCI_VENDOR_AMD)
		return ENXIO;

	switch (pci_get_device(dev)) {
	case PCI_PRODUCT_AMD_AMD64_F10_MISC:
		ten = 1;
		/* FALLTHROUGH */
	case PCI_PRODUCT_AMD_AMD64_F11_MISC:
		if (device_get_desc(dev) == NULL)
			device_set_desc(dev, ten ?
			    "AMD Family 10h temperature sensor" :
			    "AMD Family 11h temperature sensor");
		return 0;
	default:
		return ENXIO;
	}
}

static int
km_attach(struct device *dev)
{
	struct km_softc	*sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	strlcpy(sc->sc_sensordev.xname, device_get_nameunit(dev),
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TEMP;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	if (sensor_task_register(sc, km_refresh, 5)) {
		device_printf(dev, "unable to register update task\n");
		return ENXIO;
	}

	sensordev_install(&sc->sc_sensordev);
	return 0;
}

static int
km_detach(struct device *dev)
{
	struct km_softc	*sc = device_get_softc(dev);

	sensordev_deinstall(&sc->sc_sensordev);
	sensor_task_unregister(sc);
	return 0;
}

static void
km_refresh(void *arg)
{
	struct km_softc	*sc = arg;
	struct ksensor	*s = &sc->sc_sensor;
	uint32_t	r;
	int		c;

	r = pci_read_config(sc->sc_dev, KM_REP_TEMP_CONTR_R, 4);
	c = KM_GET_CURTMP(r);
	s->value = c * 125000 + 273150000;
}
