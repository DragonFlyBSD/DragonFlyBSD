/*	$NetBSD: wbsio.c,v 1.1 2010/02/21 05:16:29 cnst Exp $	*/
/*	$OpenBSD: wbsio.c,v 1.5 2009/03/29 21:53:52 sthen Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2010 Constantine A. Murenin <cnst++@dragonflybsd.org>
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
 * Winbond LPC Super I/O driver.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <bus/isa/isavar.h>
#include <bus/isa/isa_common.h>

#include "wbsioreg.h"
#include "wbsiovar.h"

static void	wbsio_identify(driver_t *, device_t);
static int	wbsio_probe(device_t);
static int	wbsio_attach(device_t);
static int	wbsio_detach(device_t);

static device_method_t wbsio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	wbsio_identify),
	DEVMETHOD(device_probe,		wbsio_probe),
	DEVMETHOD(device_attach, 	wbsio_attach),
	DEVMETHOD(device_detach,	wbsio_detach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_set_resource,	bus_generic_set_resource),
	DEVMETHOD(bus_alloc_resource,	isa_alloc_resource),
	DEVMETHOD(bus_release_resource,	isa_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),

	DEVMETHOD_END
};

static driver_t wbsio_driver = {
	"wbsio",
	wbsio_methods,
	sizeof(struct wbsio_softc)
};

static devclass_t wbsio_devclass;

DRIVER_MODULE(wbsio, isa, wbsio_driver, wbsio_devclass, NULL, NULL);


static __inline void
wbsio_conf_enable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_EN_MAGIC);
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_EN_MAGIC);
}

static __inline void
wbsio_conf_disable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_DS_MAGIC);
}

static __inline u_int8_t
wbsio_conf_read(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, index);
	return (bus_space_read_1(iot, ioh, WBSIO_DATA));
}

static __inline void
wbsio_conf_write(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index,
    u_int8_t data)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, index);
	bus_space_write_1(iot, ioh, WBSIO_DATA, data);
}

static void
wbsio_identify(driver_t *driver, device_t parent)
{
#ifdef KLD_MODULE
	device_t child[2];
	const int port[2] = { 0x2e, 0x4e };

	for (int i = 0; i < 2; i++) {
		child[i] = device_find_child(parent, driver->name, i);
		if (child[i] == NULL) {
			child[i] = BUS_ADD_CHILD(parent, parent, ISA_ORDER_PNP,
			    driver->name, i);
			if (child[i] == NULL) {
				kprintf("%s: cannot add child[%i]\n",
				    __func__, i);
				continue;
			}
		} else
			continue;
		if (bus_set_resource(child[i], SYS_RES_IOPORT, 0,
			port[i], WBSIO_IOSIZE, -1))
			kprintf("%s: cannot set resource for child[%i]\n",
			    __func__, i);
	}
#endif
}

static int
wbsio_probe(device_t dev)
{
	struct resource *iores;
	int iorid = 0;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	uint8_t reg_id, reg_rev;
	const char *desc = NULL;
	char fulldesc[64];

	/* Match by device ID */

	iores = bus_alloc_resource(dev, SYS_RES_IOPORT, &iorid,
	    0ul, ~0ul, WBSIO_IOSIZE,
	    RF_ACTIVE);
	if (iores == NULL)
		return ENXIO;
	iot = rman_get_bustag(iores);
	ioh = rman_get_bushandle(iores);

	wbsio_conf_enable(iot, ioh);
	/* Read device ID */
	reg_id = wbsio_conf_read(iot, ioh, WBSIO_ID);
	/* Read device revision */
	reg_rev = wbsio_conf_read(iot, ioh, WBSIO_REV);
	wbsio_conf_disable(iot, ioh);
	bus_release_resource(dev, SYS_RES_IOPORT, iorid, iores);

	switch (reg_id) {
	case WBSIO_ID_W83627HF:
		desc = "W83627HF";
		break;
	case WBSIO_ID_W83627THF:
		desc = "W83627THF";
		break;
	case WBSIO_ID_W83627EHF:
		desc = "W83627EHF";
		break;
	case WBSIO_ID_W83627DHG:
		desc = "W83627DHG";
		break;
	case WBSIO_ID_W83627DHGP:
		desc = "W83627DHG-P";
		break;
	case WBSIO_ID_W83627UHG:
		desc = "W83627UHG";
		break;
	case WBSIO_ID_W83637HF:
		desc = "W83637HF";
		break;
	case WBSIO_ID_W83667HG:
		desc = "W83667HG";
		break;
	case WBSIO_ID_W83687THF:
		desc = "W83687THF";
		break;
	case WBSIO_ID_W83697HF:
		desc = "W83697HF";
		break;
	case WBSIO_ID_NCT6776F:
		desc = "NCT6776F";
		break;
	}

	if (desc == NULL) {
#ifndef KLD_MODULE
		if (bootverbose)
#endif
			if (!(reg_id == 0xff && reg_rev == 0xff))
				device_printf(dev, "%s port 0x%02x: "
				    "Device ID 0x%02x, Rev 0x%02x\n",
				    __func__, isa_get_port(dev),
				    reg_id, reg_rev);
		return ENXIO;
	}

	ksnprintf(fulldesc, sizeof(fulldesc),
	    "Winbond LPC Super I/O %s rev 0x%02x", desc, reg_rev);
	device_set_desc_copy(dev, fulldesc);
	return 0;
}

static int
wbsio_attach(device_t dev)
{
	struct wbsio_softc *sc = device_get_softc(dev);
	uint8_t reg0, reg1;
	uint16_t iobase;
	device_t child;

	/* Map ISA I/O space */
	sc->sc_iores = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->sc_iorid,
	    0ul, ~0ul, WBSIO_IOSIZE,
	    RF_ACTIVE);
	if (sc->sc_iores == NULL) {
		device_printf(dev, "can't map i/o space\n");
		return ENXIO;
	}
	sc->sc_iot = rman_get_bustag(sc->sc_iores);
	sc->sc_ioh = rman_get_bushandle(sc->sc_iores);

	/* Enter configuration mode */
	wbsio_conf_enable(sc->sc_iot, sc->sc_ioh);

	/* Read device ID */
	sc->sc_devid = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_ID);

	/* Select HM logical device */
	wbsio_conf_write(sc->sc_iot, sc->sc_ioh, WBSIO_LDN, WBSIO_LDN_HM);

	/*
	 * The address should be 8-byte aligned, but it seems some
	 * BIOSes ignore this.  They get away with it, because
	 * Apparently the hardware simply ignores the lower three
	 * bits.  We do the same here.
	 */
	reg0 = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_HM_ADDR_LSB);
	reg1 = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_HM_ADDR_MSB);
	iobase = (reg1 << 8) | (reg0 & ~0x7);
	device_printf(dev, "hardware monitor iobase is 0x%x\n", iobase);

	/* Escape from configuration mode */
	wbsio_conf_disable(sc->sc_iot, sc->sc_ioh);

	if (iobase == 0) {
		device_printf(dev, "no hardware monitor configured\n");
		return 0;
	}

	child = BUS_ADD_CHILD(dev, dev, 0, "lm", -1);
	if (child == NULL) {
		device_printf(dev, "cannot add child\n");
		return ENXIO;
	}
	if (bus_set_resource(child, SYS_RES_IOPORT, 0, iobase, 8, -1)) {
		device_printf(dev, "cannot set resource\n");
		return ENXIO;
	}
	return device_probe_and_attach(child);
}

static int
wbsio_detach(device_t dev)
{
	struct wbsio_softc *sc = device_get_softc(dev);

	return bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->sc_iorid, sc->sc_iores);
}
