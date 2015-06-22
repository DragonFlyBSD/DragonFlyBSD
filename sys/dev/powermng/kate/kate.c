/*	$OpenBSD: kate.c,v 1.2 2008/03/27 04:52:03 cnst Exp $	*/

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
 * AMD NPT Family 0Fh Processors, Function 3 -- Miscellaneous Control
 */

/* Function 3 Registers */
#define K_THERMTRIP_STAT_R	0xe4
#define K_NORTHBRIDGE_CAP_R	0xe8
#define K_CPUID_FAMILY_MODEL_R	0xfc

/* Bits within Thermtrip Status Register */
#define K_THERM_SENSE_SEL	(1 << 6)
#define K_THERM_SENSE_CORE_SEL	(1 << 2)

/* Flip core and sensor selection bits */
#define K_T_SEL_C0(v)		(v |= K_THERM_SENSE_CORE_SEL)
#define K_T_SEL_C1(v)		(v &= ~(K_THERM_SENSE_CORE_SEL))
#define K_T_SEL_S0(v)		(v &= ~(K_THERM_SENSE_SEL))
#define K_T_SEL_S1(v)		(v |= K_THERM_SENSE_SEL)


/*
 * Revision Guide for AMD NPT Family 0Fh Processors,
 * Publication # 33610, Revision 3.30, February 2008
 */
static const struct {
	const char	rev[5];
	const uint32_t	cpuid[5];
} kate_proc[] = {
	{ "BH-F", { 0x00040FB0, 0x00040F80, 0, 0, 0 } },	/* F2 */
	{ "DH-F", { 0x00040FF0, 0x00050FF0, 0x00040FC0, 0, 0 } }, /* F2, F3 */
	{ "JH-F", { 0x00040F10, 0x00040F30, 0x000C0F10, 0, 0 } }, /* F2, F3 */
	{ "BH-G", { 0x00060FB0, 0x00060F80, 0, 0, 0 } },	/* G1, G2 */
	{ "DH-G", { 0x00070FF0, 0x00060FF0,
	    0x00060FC0, 0x00070FC0, 0 } }	/* G1, G2 */
};


struct kate_softc {
	struct device		*sc_dev;

	struct ksensor		sc_sensors[4];
	struct ksensordev	sc_sensordev;

	char			sc_rev;
	int8_t			sc_ii;
	int8_t			sc_in;
};

static void	kate_identify(driver_t *, struct device *);
static int	kate_probe(struct device *);
static int	kate_attach(struct device *);
static int	kate_detach(struct device *);
static void	kate_refresh(void *);

static device_method_t kate_methods[] = {
	DEVMETHOD(device_identify,	kate_identify),
	DEVMETHOD(device_probe,		kate_probe),
	DEVMETHOD(device_attach,	kate_attach),
	DEVMETHOD(device_detach,	kate_detach),
	{ NULL, NULL }
};

static driver_t kate_driver = {
	"kate",
	kate_methods,
	sizeof(struct kate_softc)
};

static devclass_t kate_devclass;

DRIVER_MODULE(kate, hostb, kate_driver, kate_devclass, NULL, NULL);


static void
kate_identify(driver_t *driver, struct device *parent)
{
	if (kate_probe(parent) == ENXIO)
		return;
	if (device_find_child(parent, driver->name, -1) != NULL)
		return;
	device_add_child(parent, driver->name, -1);
}

static int
kate_probe(struct device *dev)
{
#ifndef KATE_STRICT
	struct kate_softc	ks;
	struct kate_softc	*sc = &ks;
#endif
	uint32_t		c;
	int			i, j;

	if (pci_get_vendor(dev) != PCI_VENDOR_AMD ||
	    pci_get_device(dev) != PCI_PRODUCT_AMD_AMD64_MISC)
		return ENXIO;

	/* just in case we probe successfully, set the description */
	if (device_get_desc(dev) == NULL)
		device_set_desc(dev,
		    "AMD Family 0Fh temperature sensors");

	/*
	 * First, let's probe for chips at or after Revision F, which is
	 * when the temperature readings were officially introduced.
	 */
	c = pci_read_config(dev, K_CPUID_FAMILY_MODEL_R, 4);
	for (i = 0; i < NELEM(kate_proc); i++)
		for (j = 0; kate_proc[i].cpuid[j] != 0; j++)
			if ((c & ~0xf) == kate_proc[i].cpuid[j])
				return 0;

#ifndef KATE_STRICT
	/*
	 * If the probe above was not successful, let's try to actually
	 * read the sensors from the chip, and see if they make any sense.
	 */
	sc->sc_ii = 0;
	sc->sc_in = 4;
	sc->sc_dev = dev;
	kate_refresh(sc);
	for (i = 0; i < 4; i++)
		if (!(sc->sc_sensors[i].flags & SENSOR_FINVALID))
			return 0;
#endif /* !KATE_STRICT */

	return ENXIO;
}

static int
kate_attach(struct device *dev)
{
	struct kate_softc	*sc;
	uint32_t		c, d;
	int			i, j, cmpcap;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	c = pci_read_config(dev, K_CPUID_FAMILY_MODEL_R, 4);
	for (i = 0; i < NELEM(kate_proc) && sc->sc_rev == '\0'; i++)
		for (j = 0; kate_proc[i].cpuid[j] != 0; j++)
			if ((c & ~0xf) == kate_proc[i].cpuid[j]) {
				sc->sc_rev = kate_proc[i].rev[3];
				device_printf(dev, "core rev %.4s%.1x\n",
				    kate_proc[i].rev, c & 0xf);
				break;
			}

	if (c != 0x0 && sc->sc_rev == '\0') {
		/* CPUID Family Model Register was introduced in Revision F */
		sc->sc_rev = 'G';	/* newer than E, assume G */
		device_printf(dev, "cpuid 0x%x\n", c);
	}

	d = pci_read_config(dev, K_NORTHBRIDGE_CAP_R, 4);
	cmpcap = (d >> 12) & 0x3;

#ifndef KATE_STRICT
	sc->sc_ii = 0;
	sc->sc_in = 4;
	kate_refresh(sc);
	if (cmpcap == 0) {
		if ((sc->sc_sensors[0].flags & SENSOR_FINVALID) &&
		    (sc->sc_sensors[1].flags & SENSOR_FINVALID))
			sc->sc_ii = 2;
		if ((sc->sc_sensors[4].flags & SENSOR_FINVALID))
			sc->sc_in = 3;
	}
#else
	sc->sc_ii = cmpcap ? 0 : 2;
	sc->sc_in = 4;
#endif /* !KATE_STRICT */

	strlcpy(sc->sc_sensordev.xname, device_get_nameunit(dev),
	    sizeof(sc->sc_sensordev.xname));

	for (i = sc->sc_ii; i < sc->sc_in; i++) {
		sc->sc_sensors[i].type = SENSOR_TEMP;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	sensor_task_register(sc, kate_refresh, 5);

	sensordev_install(&sc->sc_sensordev);
	return 0;
}

static int
kate_detach(struct device *dev)
{
	struct kate_softc	*sc = device_get_softc(dev);

	sensordev_deinstall(&sc->sc_sensordev);
	sensor_task_unregister(sc);
	return 0;
}

void
kate_refresh(void *arg)
{
	struct kate_softc	*sc = arg;
	struct ksensor		*s = sc->sc_sensors;
	uint32_t		t, m;
	int			i, v;

	t = pci_read_config(sc->sc_dev, K_THERMTRIP_STAT_R, 4);

	for (i = sc->sc_ii; i < sc->sc_in; i++) {
		switch(i) {
		case 0:
			K_T_SEL_C0(t);
			K_T_SEL_S0(t);
			break;
		case 1:
			K_T_SEL_C0(t);
			K_T_SEL_S1(t);
			break;
		case 2:
			K_T_SEL_C1(t);
			K_T_SEL_S0(t);
			break;
		case 3:
			K_T_SEL_C1(t);
			K_T_SEL_S1(t);
			break;
		}
		m = t & (K_THERM_SENSE_CORE_SEL | K_THERM_SENSE_SEL);
		pci_write_config(sc->sc_dev, K_THERMTRIP_STAT_R, t, 4);
		t = pci_read_config(sc->sc_dev, K_THERMTRIP_STAT_R, 4);
		v = 0x3ff & (t >> 14);
#ifdef KATE_STRICT
		if (sc->sc_rev != 'G')
			v &= ~0x3;
#endif /* KATE_STRICT */
		if ((t & (K_THERM_SENSE_CORE_SEL | K_THERM_SENSE_SEL)) == m &&
		    (v & ~0x3) != 0)
			s[i].flags &= ~SENSOR_FINVALID;
		else
			s[i].flags |= SENSOR_FINVALID;
		s[i].value = (v * 250000 - 49000000) + 273150000;
	}
}
