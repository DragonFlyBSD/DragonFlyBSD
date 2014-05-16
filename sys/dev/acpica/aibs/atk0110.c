/*	$OpenBSD: atk0110.c,v 1.1 2009/07/23 01:38:16 cnst Exp $	*/

/*
 * Copyright (c) 2009 Constantine A. Murenin <cnst+dfly@bugmail.mojo.ru>
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

#include <machine/inttypes.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/malloc.h>

#include <sys/sensors.h>

#include "acpi.h"
#include "acpivar.h"

/*
 * ASUSTeK AI Booster (ACPI ATK0110).
 *
 * This code was originally written for OpenBSD after the techniques
 * described in the Linux's asus_atk0110.c and FreeBSD's acpi_aiboost.c
 * were verified to be accurate on the actual hardware kindly provided by
 * Sam Fourman Jr.  It was subsequently ported from OpenBSD to DragonFly BSD.
 *
 *				  -- Constantine A. Murenin <http://cnst.su/>
 */

#define AIBS_MORE_SENSORS
#define AIBS_VERBOSE

struct aibs_sensor {
	struct ksensor	s;
	UINT64		i;
	UINT64		l;
	UINT64		h;
};

struct aibs_softc {
	struct device		*sc_dev;
	ACPI_HANDLE		sc_ah;

	struct aibs_sensor	*sc_asens_volt;
	struct aibs_sensor	*sc_asens_temp;
	struct aibs_sensor	*sc_asens_fan;

	struct ksensordev	sc_sensordev;
};


static int aibs_probe(struct device *);
static int aibs_attach(struct device *);
static int aibs_detach(struct device *);
static void aibs_refresh(void *);

static void aibs_attach_sif(struct aibs_softc *, enum sensor_type);
static void aibs_refresh_r(struct aibs_softc *, enum sensor_type);


static device_method_t aibs_methods[] = {
	DEVMETHOD(device_probe,		aibs_probe),
	DEVMETHOD(device_attach,	aibs_attach),
	DEVMETHOD(device_detach,	aibs_detach),
	{ NULL, NULL }
};

static driver_t aibs_driver = {
	"aibs",
	aibs_methods,
	sizeof(struct aibs_softc)
};

static devclass_t aibs_devclass;

DRIVER_MODULE(aibs, acpi, aibs_driver, aibs_devclass, NULL, NULL);
MODULE_DEPEND(aibs, acpi, 1, 1, 1);

static char* aibs_hids[] = {
	"ATK0110",
	NULL
};

static int
aibs_probe(struct device *dev)
{

	if (acpi_disabled("aibs") ||
	    ACPI_ID_PROBE(device_get_parent(dev), dev, aibs_hids) == NULL)
		return ENXIO;

	device_set_desc(dev, "ASUSTeK AI Booster (ACPI ASOC ATK0110)");
	return 0;
}

static int
aibs_attach(struct device *dev)
{
	struct aibs_softc	*sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_ah = acpi_get_handle(dev);

	strlcpy(sc->sc_sensordev.xname, device_get_nameunit(dev),
	    sizeof(sc->sc_sensordev.xname));

	aibs_attach_sif(sc, SENSOR_VOLTS_DC);
	aibs_attach_sif(sc, SENSOR_TEMP);
	aibs_attach_sif(sc, SENSOR_FANRPM);

	if (sc->sc_sensordev.sensors_count == 0) {
		device_printf(dev, "no sensors found\n");
		return ENXIO;
	}

	if (sensor_task_register(sc, aibs_refresh, 5)) {
		device_printf(dev, "unable to register update task\n");
		return ENXIO;
	}

	sensordev_install(&sc->sc_sensordev);
	return 0;
}

static void
aibs_attach_sif(struct aibs_softc *sc, enum sensor_type st)
{
	ACPI_STATUS		s;
	ACPI_BUFFER		b;
	ACPI_OBJECT		*bp, *o;
	int			i, n;
	char			name[] = "?SIF";
	struct aibs_sensor	*as;

	switch (st) {
	case SENSOR_TEMP:
		name[0] = 'T';
		break;
	case SENSOR_FANRPM:
		name[0] = 'F';
		break;
	case SENSOR_VOLTS_DC:
		name[0] = 'V';
		break;
	default:
		return;
	}

	b.Length = ACPI_ALLOCATE_BUFFER;
	s = AcpiEvaluateObjectTyped(sc->sc_ah, name, NULL, &b,
	    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->sc_dev, "%s not found\n", name);
		return;
	}

	bp = b.Pointer;
	o = bp->Package.Elements;
	if (o[0].Type != ACPI_TYPE_INTEGER) {
		device_printf(sc->sc_dev, "%s[0]: invalid type\n", name);
		AcpiOsFree(b.Pointer);
		return;
	}

	n = o[0].Integer.Value;
	if (bp->Package.Count - 1 < n) {
		device_printf(sc->sc_dev, "%s: invalid package\n", name);
		AcpiOsFree(b.Pointer);
		return;
	} else if (bp->Package.Count - 1 > n) {
		int on = n;

#ifdef AIBS_MORE_SENSORS
		n = bp->Package.Count - 1;
#endif
		device_printf(sc->sc_dev, "%s: malformed package: %i/%i"
		    ", assume %i\n", name, on, bp->Package.Count - 1, n);
	}
	if (n < 1) {
		device_printf(sc->sc_dev, "%s: no members in the package\n",
		    name);
		AcpiOsFree(b.Pointer);
		return;
	}

	as = kmalloc(sizeof(*as) * n, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (as == NULL) {
		device_printf(sc->sc_dev, "%s: malloc fail\n", name);
		AcpiOsFree(b.Pointer);
		return;
	}

	switch (st) {
	case SENSOR_TEMP:
		sc->sc_asens_temp = as;
		break;
	case SENSOR_FANRPM:
		sc->sc_asens_fan = as;
		break;
	case SENSOR_VOLTS_DC:
		sc->sc_asens_volt = as;
		break;
	default:
		/* NOTREACHED */
		return;
	}

	for (i = 0, o++; i < n; i++, o++) {
		ACPI_OBJECT	*oi;

		/* acpica automatically evaluates the referenced package */
		if (o[0].Type != ACPI_TYPE_PACKAGE) {
			device_printf(sc->sc_dev,
			    "%s: %i: not a package: %i type\n",
			    name, i, o[0].Type);
			continue;
		}
		oi = o[0].Package.Elements;
		if (o[0].Package.Count != 5 ||
		    oi[0].Type != ACPI_TYPE_INTEGER ||
		    oi[1].Type != ACPI_TYPE_STRING ||
		    oi[2].Type != ACPI_TYPE_INTEGER ||
		    oi[3].Type != ACPI_TYPE_INTEGER ||
		    oi[4].Type != ACPI_TYPE_INTEGER) {
			device_printf(sc->sc_dev,
			    "%s: %i: invalid package\n",
			    name, i);
			continue;
		}
		as[i].i = oi[0].Integer.Value;
		strlcpy(as[i].s.desc, oi[1].String.Pointer,
		    sizeof(as[i].s.desc));
		as[i].l = oi[2].Integer.Value;
		as[i].h = oi[3].Integer.Value;
		as[i].s.type = st;
#ifdef AIBS_VERBOSE
		device_printf(sc->sc_dev, "%c%i: "
		    "0x%08"PRIx64" %20s %5"PRIi64" / %5"PRIi64"  "
		    "0x%"PRIx64"\n",
		    name[0], i,
		    as[i].i, as[i].s.desc, (int64_t)as[i].l, (int64_t)as[i].h,
		    oi[4].Integer.Value);
#endif
		sensor_attach(&sc->sc_sensordev, &as[i].s);
	}

	AcpiOsFree(b.Pointer);
	return;
}

static int
aibs_detach(struct device *dev)
{
	struct aibs_softc	*sc = device_get_softc(dev);

	sensordev_deinstall(&sc->sc_sensordev);
	sensor_task_unregister(sc);
	if (sc->sc_asens_volt != NULL)
		kfree(sc->sc_asens_volt, M_DEVBUF);
	if (sc->sc_asens_temp != NULL)
		kfree(sc->sc_asens_temp, M_DEVBUF);
	if (sc->sc_asens_fan != NULL)
		kfree(sc->sc_asens_fan, M_DEVBUF);
	return 0;
}

#ifdef AIBS_VERBOSE
#define ddevice_printf(x...) device_printf(x)
#else
#define ddevice_printf(x...)
#endif

static void
aibs_refresh(void *arg)
{
	struct aibs_softc *sc = arg;

	aibs_refresh_r(sc, SENSOR_VOLTS_DC);
	aibs_refresh_r(sc, SENSOR_TEMP);
	aibs_refresh_r(sc, SENSOR_FANRPM);
}

static void
aibs_refresh_r(struct aibs_softc *sc, enum sensor_type st)
{
	ACPI_STATUS		rs;
	ACPI_HANDLE		rh;
	int			i, n = sc->sc_sensordev.maxnumt[st];
	char			*name;
	struct aibs_sensor	*as;

	switch (st) {
	case SENSOR_TEMP:
		name = "RTMP";
		as = sc->sc_asens_temp;
		break;
	case SENSOR_FANRPM:
		name = "RFAN";
		as = sc->sc_asens_fan;
		break;
	case SENSOR_VOLTS_DC:
		name = "RVLT";
		as = sc->sc_asens_volt;
		break;
	default:
		return;
	}

	if (as == NULL)
		return;

	rs = AcpiGetHandle(sc->sc_ah, name, &rh);
	if (ACPI_FAILURE(rs)) {
		ddevice_printf(sc->sc_dev, "%s: method handle not found\n",
		    name);
		for (i = 0; i < n; i++)
			as[i].s.flags |= SENSOR_FINVALID;
		return;
	}

	for (i = 0; i < n; i++) {
		ACPI_OBJECT		p, *bp;
		ACPI_OBJECT_LIST	mp;
		ACPI_BUFFER		b;
		UINT64			v;
		struct ksensor		*s = &as[i].s;
		const UINT64		l = as[i].l, h = as[i].h;

		p.Type = ACPI_TYPE_INTEGER;
		p.Integer.Value = as[i].i;
		mp.Count = 1;
		mp.Pointer = &p;
		b.Length = ACPI_ALLOCATE_BUFFER;
		rs = AcpiEvaluateObjectTyped(rh, NULL, &mp, &b,
		    ACPI_TYPE_INTEGER);
		if (ACPI_FAILURE(rs)) {
			ddevice_printf(sc->sc_dev,
			    "%s: %i: evaluation failed\n",
			    name, i);
			s->flags |= SENSOR_FINVALID;
			continue;
		}
		bp = b.Pointer;
		v = bp->Integer.Value;
		AcpiOsFree(b.Pointer);

		switch (st) {
		case SENSOR_TEMP:
			s->value = v * 100 * 1000 + 273150000;
			if (v == 0) {
				s->status = SENSOR_S_UNKNOWN;
				s->flags |= SENSOR_FINVALID;
			} else {
				if (v > h)
					s->status = SENSOR_S_CRIT;
				else if (v > l)
					s->status = SENSOR_S_WARN;
				else
					s->status = SENSOR_S_OK;
				s->flags &= ~SENSOR_FINVALID;
			}
			break;
		case SENSOR_FANRPM:
			s->value = v;
			/* some boards have strange limits for fans */
			if ((l != 0 && l < v && v < h) ||
			    (l == 0 && v > h))
				s->status = SENSOR_S_OK;
			else
				s->status = SENSOR_S_WARN;
			s->flags &= ~SENSOR_FINVALID;
			break;
		case SENSOR_VOLTS_DC:
			s->value = v * 1000;
			if (l < v && v < h)
				s->status = SENSOR_S_OK;
			else
				s->status = SENSOR_S_WARN;
			s->flags &= ~SENSOR_FINVALID;
			break;
		default:
			/* NOTREACHED */
			break;
		}
	}

	return;
}
