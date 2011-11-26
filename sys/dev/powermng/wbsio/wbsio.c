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

/* ISA bus registers */
#define WBSIO_INDEX		0x00	/* Configuration Index Register */
#define WBSIO_DATA		0x01	/* Configuration Data Register */

#define WBSIO_IOSIZE		0x02	/* ISA I/O space size */

#define WBSIO_CONF_EN_MAGIC	0x87	/* enable configuration mode */
#define WBSIO_CONF_DS_MAGIC	0xaa	/* disable configuration mode */

/* Configuration Space Registers */
#define WBSIO_LDN		0x07	/* Logical Device Number */
#define WBSIO_ID		0x20	/* Device ID */
#define WBSIO_REV		0x21	/* Device Revision */

#define WBSIO_ID_W83627HF	0x52
#define WBSIO_ID_W83627THF	0x82
#define WBSIO_ID_W83627EHF	0x88
#define WBSIO_ID_W83627DHG	0xa0
#define WBSIO_ID_W83627DHGP	0xb0
#define WBSIO_ID_W83627SF	0x59
#define WBSIO_ID_W83627UHG	0xa2
#define WBSIO_ID_W83637HF	0x70
#define WBSIO_ID_W83667HG	0xa5
#define WBSIO_ID_W83687THF	0x85
#define WBSIO_ID_W83697HF	0x60

/* Logical Device Number (LDN) Assignments */
#define WBSIO_LDN_HM		0x0b

/* Hardware Monitor Control Registers (LDN B) */
#define WBSIO_HM_ADDR_MSB	0x60	/* Address [15:8] */
#define WBSIO_HM_ADDR_LSB	0x61	/* Address [7:0] */

struct wbsio_softc {
	struct device		*sc_dev;

	struct resource		*sc_iores;
	int			sc_iorid;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

static void	wbsio_identify(driver_t *, struct device *);
static int	wbsio_probe(struct device *);
static int	wbsio_attach(struct device *);
static int	wbsio_detach(struct device *);

static device_method_t wbsio_methods[] = {
	DEVMETHOD(device_identify,	wbsio_identify),
	DEVMETHOD(device_probe,		wbsio_probe),
	DEVMETHOD(device_attach, 	wbsio_attach),
	DEVMETHOD(device_detach,	wbsio_detach),

	{ NULL, NULL}
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
wbsio_identify(driver_t *driver, struct device *parent)
{
#ifdef KLD_MODULE
	struct device *child[2];
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
wbsio_probe(struct device *dev)
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
wbsio_attach(struct device *dev)
{
	struct wbsio_softc *sc = device_get_softc(dev);
	uint8_t reg0, reg1;
	uint16_t iobase;
	struct device *parent = device_get_parent(dev);
	struct device *child;
	struct devclass *c_dc;
	int c_maxunit;

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

	child = NULL;
	c_dc = devclass_find("lm");
	if (c_dc == NULL) {
		device_printf(dev, "lm devclass not found\n");
		return ENXIO;
	}
	c_maxunit = devclass_get_maxunit(c_dc);
	for (int u = 0; u < c_maxunit; u++) {
		child = devclass_get_device(c_dc, u);
		if (child == NULL)
			continue;
		if (isa_get_port(child) == iobase) {
			if (device_is_attached(child)) {
				device_printf(dev,
				    "%s is already attached at 0x%x\n",
				    device_get_nameunit(child), iobase);
				return 0;
			}
			break;
		}
		if (device_is_attached(child)) {
			child = NULL;
			continue;
		}
		device_printf(dev,
		    "found unused %s at 0x%x with state %i, reusing at 0x%x\n",
		    device_get_nameunit(child), isa_get_port(child),
		    device_get_state(child), iobase);
		break;
	}
	if (child == NULL)
		child = BUS_ADD_CHILD(parent, parent, ISA_ORDER_PNP,
		    "lm", -1);
//	child = BUS_ADD_CHILD(parent, parent, ISA_ORDER_PNP,
//	    "lm", 3 + device_get_unit(dev));
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
wbsio_detach(struct device *dev)
{
	struct wbsio_softc *sc = device_get_softc(dev);

	return bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->sc_iorid, sc->sc_iores);
}
