/*	$OpenBSD: aps.c,v 1.19 2009/05/24 16:40:18 jsg Exp $	*/
/*
 * Copyright (c) 2005 Jonathan Gray <jsg@openbsd.org>
 * Copyright (c) 2008 Can Erkin Acar <canacar@openbsd.org>
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
 * A driver for the ThinkPad Active Protection System based on notes from
 * http://www.almaden.ibm.com/cs/people/marksmith/tpaps.html
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sensors.h>

#include <bus/isa/isavar.h>

#if defined(APSDEBUG)
#define DPRINTF(x)		do { kprintf x; } while (0)
#else
#define DPRINTF(x)
#endif


/*
 * EC interface on Thinkpad Laptops, from Linux HDAPS driver notes.
 * From Renesans H8S/2140B Group Hardware Manual
 * http://documentation.renesas.com/eng/products/mpumcu/rej09b0300_2140bhm.pdf
 *
 * EC uses LPC Channel 3 registers TWR0..15
 */

/* STR3 status register */
#define APS_STR3		0x04

#define APS_STR3_IBF3B	0x80	/* Input buffer full (host->slave) */
#define APS_STR3_OBF3B	0x40	/* Output buffer full (slave->host)*/
#define APS_STR3_MWMF	0x20	/* Master write mode */
#define APS_STR3_SWMF	0x10	/* Slave write mode */


/* Base address of TWR registers */
#define APS_TWR_BASE		0x10
#define APS_TWR_RET		0x1f

/* TWR registers */
#define APS_CMD			0x00
#define APS_ARG1		0x01
#define APS_ARG2		0x02
#define APS_ARG3		0x03
#define APS_RET			0x0f

/* Sensor values */
#define APS_STATE		0x01
#define	APS_XACCEL		0x02
#define APS_YACCEL		0x04
#define APS_TEMP		0x06
#define	APS_XVAR		0x07
#define APS_YVAR		0x09
#define APS_TEMP2		0x0b
#define APS_UNKNOWN		0x0c
#define APS_INPUT		0x0d

/* write masks for I/O, send command + 0-3 arguments*/
#define APS_WRITE_0		0x0001
#define APS_WRITE_1		0x0003
#define APS_WRITE_2		0x0007
#define APS_WRITE_3		0x000f

/* read masks for I/O, read 0-3 values (skip command byte) */
#define APS_READ_0		0x0000
#define APS_READ_1		0x0002
#define APS_READ_2		0x0006
#define APS_READ_3		0x000e

#define APS_READ_RET		0x8000
#define APS_READ_ALL		0xffff

/* Bit definitions for APS_INPUT value */
#define APS_INPUT_KB		(1 << 5)
#define APS_INPUT_MS		(1 << 6)
#define APS_INPUT_LIDOPEN	(1 << 7)

#define APS_ADDR_BASE		0x1600
#define APS_ADDR_SIZE		0x1f

struct aps_sensor_rec {
	u_int8_t	state;
	u_int16_t	x_accel;
	u_int16_t	y_accel;
	u_int8_t	temp1;
	u_int16_t	x_var;
	u_int16_t	y_var;
	u_int8_t	temp2;
	u_int8_t	unk;
	u_int8_t	input;
};

#define APS_NUM_SENSORS		9

#define APS_SENSOR_XACCEL	0
#define APS_SENSOR_YACCEL	1
#define APS_SENSOR_XVAR		2
#define APS_SENSOR_YVAR		3
#define APS_SENSOR_TEMP1	4
#define APS_SENSOR_TEMP2	5
#define APS_SENSOR_KBACT	6
#define APS_SENSOR_MSACT	7
#define APS_SENSOR_LIDOPEN	8

struct aps_softc {
	struct device		*sc_dev;

	struct resource		*sc_iores;
	int			sc_iorid;

	struct ksensor		sensors[APS_NUM_SENSORS];
	struct ksensordev	sensordev;

	struct aps_sensor_rec	aps_data;
};

static void	aps_identify(driver_t *, struct device *);
static int	aps_probe(struct device *);
static int	aps_attach(struct device *);
static int	aps_detach(struct device *);

static int	aps_resume(struct device *);
static int	aps_suspend(struct device *);

static int	aps_init(struct resource *);
static int	aps_read_data(struct aps_softc *);
static void	aps_refresh_sensor_data(struct aps_softc *);
static void	aps_refresh(void *);
static int	aps_do_io(struct resource *, unsigned char *, int, int);

static device_method_t aps_methods[] = {
	DEVMETHOD(device_identify,	aps_identify),
	DEVMETHOD(device_probe,		aps_probe),
	DEVMETHOD(device_attach,	aps_attach),
	DEVMETHOD(device_detach,	aps_detach),

	DEVMETHOD(device_resume,	aps_resume),
	DEVMETHOD(device_suspend,	aps_suspend),
	{ NULL, NULL }
};

static driver_t aps_driver = {
	"aps",
	aps_methods,
	sizeof(struct aps_softc)
};

static devclass_t aps_devclass;

DRIVER_MODULE(aps, isa, aps_driver, aps_devclass, NULL, NULL);



/* properly communicate with the controller, writing a set of memory
 * locations and reading back another set  */
static int
aps_do_io(struct resource *iores, unsigned char *buf, int wmask, int rmask)
{
	bus_space_tag_t iot = rman_get_bustag(iores);
	bus_space_handle_t ioh = rman_get_bushandle(iores);
	int bp, stat, n;

	DPRINTF(("aps_do_io: CMD: 0x%02x, wmask: 0x%04x, rmask: 0x%04x\n",
	       buf[0], wmask, rmask));

	/* write init byte using arbitration */
	for (n = 0; n < 100; n++) {
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_OBF3B | APS_STR3_SWMF)) {
			bus_space_read_1(iot, ioh, APS_TWR_RET);
			continue;
		}
		bus_space_write_1(iot, ioh, APS_TWR_BASE, buf[0]);
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_MWMF))
			break;
		DRIVERSLEEP(1);
	}

	if (n == 100) {
		DPRINTF(("aps_do_io: Failed to get bus\n"));
		return (1);
	}

	/* write data bytes, init already sent */
	/* make sure last bye is always written as this will trigger slave */
	wmask |= APS_READ_RET;
	buf[APS_RET] = 0x01;

	for (n = 1, bp = 2; n < 16; bp <<= 1, n++) {
		if (wmask & bp) {
			bus_space_write_1(iot, ioh, APS_TWR_BASE + n, buf[n]);
			DPRINTF(("aps_do_io:  write %2d 0x%02x\n", n, buf[n]));
		}
	}

	for (n = 0; n < 100; n++) {
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_OBF3B))
			break;
		DRIVERSLEEP(500);
	}

	if (n == 100) {
		DPRINTF(("aps_do_io: timeout waiting response\n"));
		return (1);
	}
	/* wait for data available */
	/* make sure to read the final byte to clear status */
	rmask |= APS_READ_RET;

	/* read cmd and data bytes */
	for (n = 0, bp = 1; n < 16; bp <<= 1, n++) {
		if (rmask & bp) {
			buf[n] = bus_space_read_1(iot, ioh, APS_TWR_BASE + n);
			DPRINTF(("aps_do_io:  read %2d 0x%02x\n", n, buf[n]));
		}
	}

	return (0);
}

/* for hints, see /sys/bus/isa/isahint.c */

static void
aps_identify(driver_t *driver, struct device *parent)
{
	struct device *child;

	child = device_find_child(parent, driver->name, -1);
	if (child != NULL) {
		if (isa_get_portsize(child) == 0) {
			// aps(4) must have been compiled into the kernel
			if (bootverbose)
				kprintf("%s: will specify the port\n",
				    __func__);
		} else if (isa_get_port(child) != APS_ADDR_BASE)
			kprintf("%s: will overwrite specified port\n",
			    __func__);
		else {
			if (isa_get_portsize(child) == APS_ADDR_SIZE) {
				// aps.ko must have been reloaded
				kprintf("%s: already have been invoked\n",
				    __func__);
				return;
			} else
				kprintf("%s: will amend the portsize\n",
				    __func__);
		}
	} else {
		// first invocation of `kldload aps.ko`
		kprintf("%s: creating a new %s\n",
		    __func__, driver->name);
		child = BUS_ADD_CHILD(parent, parent, ISA_ORDER_PNP,
		    driver->name, -1);
		if (child == NULL) {
			kprintf("%s: cannot add child\n", __func__);
			return;
		}
	}
	if (bus_set_resource(child, SYS_RES_IOPORT, 0,
		APS_ADDR_BASE, APS_ADDR_SIZE, -1))
		kprintf("%s: cannot set resource\n", __func__);
}

static int
aps_probe(struct device *dev)
{
	struct resource *iores;
	int iorid = 0;
	u_int8_t cr;
	unsigned char iobuf[16];

#if defined(APSDEBUG) || defined(KLD_MODULE)
	device_printf(dev, "%s: port 0x%x\n", __func__, isa_get_port(dev));
#endif

	if (device_get_unit(dev) != 0)
		return ENXIO;

	iores = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &iorid, RF_ACTIVE);
	if (iores == NULL) {
		DPRINTF(("aps: can't map i/o space\n"));
		return ENXIO;
	}


	/* See if this machine has APS */

	/* get APS mode */
	iobuf[APS_CMD] = 0x13;
	if (aps_do_io(iores, iobuf, APS_WRITE_0, APS_READ_1)) {
		bus_release_resource(dev, SYS_RES_IOPORT, iorid, iores);
		return ENXIO;
	}

	/*
	 * Observed values from Linux driver:
	 * 0x01: T42
	 * 0x02: chip already initialised
	 * 0x03: T41
	 * 0x05: T61
	 */

	cr = iobuf[APS_ARG1];

	bus_release_resource(dev, SYS_RES_IOPORT, iorid, iores);
	DPRINTF(("aps: state register 0x%x\n", cr));

	if (iobuf[APS_RET] != 0 || cr < 1 || cr > 5) {
		DPRINTF(("aps: unsupported state %d\n", cr));
		return ENXIO;
	}
	device_set_desc(dev, "ThinkPad Active Protection System");
	return 0;
}

static int
aps_attach(struct device *dev)
{
	struct aps_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;
	sc->sc_iores = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->sc_iorid, RF_ACTIVE);
	if (sc->sc_iores == NULL) {
		device_printf(dev, "can't map i/o space\n");
		return ENXIO;
	}

	if (aps_init(sc->sc_iores)) {
		device_printf(dev, "failed to initialise\n");
		bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_iorid, sc->sc_iores);
		return ENXIO;
	}

	sc->sensors[APS_SENSOR_XACCEL].type = SENSOR_INTEGER;
	ksnprintf(sc->sensors[APS_SENSOR_XACCEL].desc,
	    sizeof(sc->sensors[APS_SENSOR_XACCEL].desc), "X_ACCEL");

	sc->sensors[APS_SENSOR_YACCEL].type = SENSOR_INTEGER;
	ksnprintf(sc->sensors[APS_SENSOR_YACCEL].desc,
	    sizeof(sc->sensors[APS_SENSOR_YACCEL].desc), "Y_ACCEL");

	sc->sensors[APS_SENSOR_TEMP1].type = SENSOR_TEMP;
	sc->sensors[APS_SENSOR_TEMP2].type = SENSOR_TEMP;

	sc->sensors[APS_SENSOR_XVAR].type = SENSOR_INTEGER;
	ksnprintf(sc->sensors[APS_SENSOR_XVAR].desc,
	    sizeof(sc->sensors[APS_SENSOR_XVAR].desc), "X_VAR");

	sc->sensors[APS_SENSOR_YVAR].type = SENSOR_INTEGER;
	ksnprintf(sc->sensors[APS_SENSOR_YVAR].desc,
	    sizeof(sc->sensors[APS_SENSOR_YVAR].desc), "Y_VAR");

	sc->sensors[APS_SENSOR_KBACT].type = SENSOR_INDICATOR;
	ksnprintf(sc->sensors[APS_SENSOR_KBACT].desc,
	    sizeof(sc->sensors[APS_SENSOR_KBACT].desc), "Keyboard Active");

	sc->sensors[APS_SENSOR_MSACT].type = SENSOR_INDICATOR;
	ksnprintf(sc->sensors[APS_SENSOR_MSACT].desc,
	    sizeof(sc->sensors[APS_SENSOR_MSACT].desc), "Mouse Active");

	sc->sensors[APS_SENSOR_LIDOPEN].type = SENSOR_INDICATOR;
	ksnprintf(sc->sensors[APS_SENSOR_LIDOPEN].desc,
	    sizeof(sc->sensors[APS_SENSOR_LIDOPEN].desc), "Lid Open");

	/* stop hiding and report to the authorities */
	strlcpy(sc->sensordev.xname, device_get_nameunit(dev),
	    sizeof(sc->sensordev.xname));
	for (int i = 0; i < APS_NUM_SENSORS ; i++)
		sensor_attach(&sc->sensordev, &sc->sensors[i]);

	/* Refresh sensor data every 1 second */
	/* XXX: a more frequent refresh might be appropriate */
	sensor_task_register(sc, aps_refresh, 1);

	sensordev_install(&sc->sensordev);
	return 0;
}

static int
aps_detach(struct device *dev)
{
	struct aps_softc *sc = device_get_softc(dev);

	sensordev_deinstall(&sc->sensordev);
	sensor_task_unregister(sc);
	return bus_release_resource(dev, SYS_RES_IOPORT,
	    sc->sc_iorid, sc->sc_iores);
}

static int
aps_init(struct resource *iores)
{
	unsigned char iobuf[16];

	/* command 0x17/0x81: check EC */
	iobuf[APS_CMD] = 0x17;
	iobuf[APS_ARG1] = 0x81;

	if (aps_do_io(iores, iobuf, APS_WRITE_1, APS_READ_3))
		return (1);

	if (iobuf[APS_RET] != 0 ||iobuf[APS_ARG3] != 0)
		return (1);

	/* Test values from the Linux driver */
	if ((iobuf[APS_ARG1] != 0 || iobuf[APS_ARG2] != 0x60) &&
	    (iobuf[APS_ARG1] != 1 || iobuf[APS_ARG2] != 0))
		return (1);

	/* command 0x14: set power */
	iobuf[APS_CMD] = 0x14;
	iobuf[APS_ARG1] = 0x01;

	if (aps_do_io(iores, iobuf, APS_WRITE_1, APS_READ_0))
		return (1);

	if (iobuf[APS_RET] != 0)
		return (1);

	/* command 0x10: set config (sample rate and order) */
	iobuf[APS_CMD] = 0x10;
	iobuf[APS_ARG1] = 0xc8;
	iobuf[APS_ARG2] = 0x00;
	iobuf[APS_ARG3] = 0x02;

	if (aps_do_io(iores, iobuf, APS_WRITE_3, APS_READ_0))
		return (1);

	if (iobuf[APS_RET] != 0)
		return (1);

	/* command 0x11: refresh data */
	iobuf[APS_CMD] = 0x11;
	if (aps_do_io(iores, iobuf, APS_WRITE_0, APS_READ_1))
		return (1);
	if (iobuf[APS_ARG1] != 0)
		return (1);

	return (0);
}

static int
aps_read_data(struct aps_softc *sc)
{
	unsigned char iobuf[16];

	/* command 0x11: refresh data */
	iobuf[APS_CMD] = 0x11;
	if (aps_do_io(sc->sc_iores, iobuf, APS_WRITE_0, APS_READ_ALL))
		return (1);

	sc->aps_data.state = iobuf[APS_STATE];
	sc->aps_data.x_accel = iobuf[APS_XACCEL] + 256 * iobuf[APS_XACCEL + 1];
	sc->aps_data.y_accel = iobuf[APS_YACCEL] + 256 * iobuf[APS_YACCEL + 1];
	sc->aps_data.temp1 = iobuf[APS_TEMP];
	sc->aps_data.x_var = iobuf[APS_XVAR] + 256 * iobuf[APS_XVAR + 1];
	sc->aps_data.y_var = iobuf[APS_YVAR] + 256 * iobuf[APS_YVAR + 1];
	sc->aps_data.temp2 = iobuf[APS_TEMP2];
	sc->aps_data.input = iobuf[APS_INPUT];

	return (0);
}

static void
aps_refresh_sensor_data(struct aps_softc *sc)
{
	int64_t temp;

	if (aps_read_data(sc)) {
		for (int i = 0; i < APS_NUM_SENSORS; i++)
			sc->sensors[i].flags |= SENSOR_FINVALID;
		return;
	}

	sc->sensors[APS_SENSOR_XACCEL].value = sc->aps_data.x_accel;
	sc->sensors[APS_SENSOR_YACCEL].value = sc->aps_data.y_accel;

	/* convert to micro (mu) degrees */
	temp = sc->aps_data.temp1 * 1000000;
	/* convert to kelvin */
	temp += 273150000;
	sc->sensors[APS_SENSOR_TEMP1].value = temp;

	/* convert to micro (mu) degrees */
	temp = sc->aps_data.temp2 * 1000000;
	/* convert to kelvin */
	temp += 273150000;
	sc->sensors[APS_SENSOR_TEMP2].value = temp;

	sc->sensors[APS_SENSOR_XVAR].value = sc->aps_data.x_var;
	sc->sensors[APS_SENSOR_YVAR].value = sc->aps_data.y_var;
	sc->sensors[APS_SENSOR_KBACT].value =
	    (sc->aps_data.input &  APS_INPUT_KB) ? 1 : 0;
	sc->sensors[APS_SENSOR_MSACT].value =
	    (sc->aps_data.input & APS_INPUT_MS) ? 1 : 0;
	sc->sensors[APS_SENSOR_LIDOPEN].value =
	    (sc->aps_data.input & APS_INPUT_LIDOPEN) ? 1 : 0;

	for (int i = 0; i < APS_NUM_SENSORS; i++)
		sc->sensors[i].flags &= ~SENSOR_FINVALID;
}

static void
aps_refresh(void *arg)
{
	struct aps_softc *sc = (struct aps_softc *)arg;

	aps_refresh_sensor_data(sc);
}

static int
aps_resume(struct device *dev)
{
	struct aps_softc *sc = device_get_softc(dev);
	unsigned char iobuf[16];

	/*
	 * Redo the init sequence on resume, because APS is
	 * as forgetful as it is deaf.
	 */

	/* get APS mode */
	iobuf[APS_CMD] = 0x13;
	if (aps_do_io(sc->sc_iores, iobuf, APS_WRITE_0, APS_READ_1)
	    || aps_init(sc->sc_iores)) {
		device_printf(sc->sc_dev, "failed to wake up\n");
		return EIO;
	}

	sensor_task_register(sc, aps_refresh, 1);
	return 0;
}

static int
aps_suspend(struct device *dev)
{
	struct aps_softc *sc = device_get_softc(dev);

	for (int i = 0; i < APS_NUM_SENSORS; i++)
		sc->sensors[i].flags |= SENSOR_FINVALID;
	sensor_task_unregister(sc);
	return 0;
}
