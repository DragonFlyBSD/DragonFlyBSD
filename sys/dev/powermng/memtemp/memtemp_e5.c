/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitops.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>
#include <bus/pci/pcib_private.h>

#include "pcib_if.h"

#include <dev/misc/ecc/ecc_e5_reg.h>

#define UBOX_READ(dev, ofs, w)				\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_UBOX0, PCIFUNC_E5_UBOX0, (ofs), w)
#define UBOX_READ_2(dev, ofs)		UBOX_READ((dev), (ofs), 2)
#define UBOX_READ_4(dev, ofs)		UBOX_READ((dev), (ofs), 4)

#define IMC_CPGC_READ(dev, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_IMC_CPGC, PCIFUNC_E5_IMC_CPGC, (ofs), w)
#define IMC_CPGC_READ_2(dev, ofs)	IMC_CPGC_READ((dev), (ofs), 2)
#define IMC_CPGC_READ_4(dev, ofs)	IMC_CPGC_READ((dev), (ofs), 4)

#define IMC_CTAD_READ(dev, c, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_IMC_CTAD, PCIFUNC_E5_IMC_CTAD((c)), (ofs), w)
#define IMC_CTAD_READ_2(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 2)
#define IMC_CTAD_READ_4(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 4)

struct memtemp_e5_type {
	uint16_t	did;
	int		slot;
	int		func;
	int		chan;
	const char	*desc;
};

struct memtemp_e5_softc;

struct memtemp_e5_dimm {
	TAILQ_ENTRY(memtemp_e5_dimm) dimm_link;
	struct ksensordev	dimm_sensordev;
	struct ksensor		dimm_sensor;
	struct memtemp_e5_softc	*dimm_parent;
	int			dimm_id;
	int			dimm_extid;
};

struct memtemp_e5_softc {
	device_t		temp_dev;
	int			temp_chan;
	int			temp_node;
	TAILQ_HEAD(, memtemp_e5_dimm) temp_dimm;
};

static int	memtemp_e5_probe(device_t);
static int	memtemp_e5_attach(device_t);
static int	memtemp_e5_detach(device_t);

static void	memtemp_e5_sensor_task(void *);

#define MEMTEMP_E5_TYPE_V2(c) \
{ \
	.did	= PCI_E5_IMC_THERMAL_CHN##c##_DID_ID, \
	.slot	= PCISLOT_E5_IMC_THERMAL, \
	.func	= PCIFUNC_E5_IMC_THERMAL_CHN##c, \
	.chan	= c, \
	.desc	= "Intel E5 v2 memory thermal sensor" \
}

#define MEMTEMP_E5_TYPE_END		{ 0, 0, 0, 0, NULL }

static const struct memtemp_e5_type memtemp_types[] = {
	MEMTEMP_E5_TYPE_V2(0),
	MEMTEMP_E5_TYPE_V2(1),
	MEMTEMP_E5_TYPE_V2(2),
	MEMTEMP_E5_TYPE_V2(3),

	MEMTEMP_E5_TYPE_END
};

#undef MEMTEMP_E5_TYPE_V2
#undef MEMTEMP_E5_TYPE_END

static device_method_t memtemp_e5_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		memtemp_e5_probe),
	DEVMETHOD(device_attach,	memtemp_e5_attach),
	DEVMETHOD(device_detach,	memtemp_e5_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t memtemp_e5_driver = {
	"memtemp",
	memtemp_e5_methods,
	sizeof(struct memtemp_e5_softc)
};
static devclass_t memtemp_devclass;
DRIVER_MODULE(memtemp_e5, pci, memtemp_e5_driver, memtemp_devclass, NULL, NULL);
MODULE_DEPEND(memtemp_e5, pci, 1, 1, 1);

static int
memtemp_e5_probe(device_t dev)
{
	const struct memtemp_e5_type *t;
	uint16_t vid, did;
	int slot, func;

	vid = pci_get_vendor(dev);
	if (vid != PCI_E5_VID_ID)
		return ENXIO;

	did = pci_get_device(dev);
	slot = pci_get_slot(dev);
	func = pci_get_function(dev);

	for (t = memtemp_types; t->desc != NULL; ++t) {
		if (t->did == did && t->slot == slot && t->func == func) {
			struct memtemp_e5_softc *sc = device_get_softc(dev);
			char desc[128];
			uint32_t val;
			int node, dimm;

			/* Check CPGC vid/did */
			if (IMC_CPGC_READ_2(dev, PCIR_VENDOR) !=
			    PCI_E5_VID_ID ||
			    IMC_CPGC_READ_2(dev, PCIR_DEVICE) !=
			    PCI_E5_IMC_CPGC_DID_ID)
				break;

			/* Is this channel disabled */
			val = IMC_CPGC_READ_4(dev, PCI_E5_IMC_CPGC_MCMTR);
			if (val & PCI_E5_IMC_CPGC_MCMTR_CHN_DISABLE(t->chan))
				break;

			/* Check CTAD vid/did */
			if (IMC_CTAD_READ_2(dev, t->chan, PCIR_VENDOR) !=
			    PCI_E5_VID_ID ||
			    IMC_CTAD_READ_2(dev, t->chan, PCIR_DEVICE) !=
			    PCI_E5_IMC_CTAD_DID_ID(t->chan))
				break;

			/* Are there any DIMMs populated? */
			for (dimm = 0; dimm < PCI_E5_IMC_DIMM_MAX; ++dimm) {
				val = IMC_CTAD_READ_4(dev, t->chan,
				    PCI_E5_IMC_CTAD_DIMMMTR(dimm));
				if (val & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP)
					break;
			}
			if (dimm == PCI_E5_IMC_DIMM_MAX)
				break;

			/* Check UBOX vid/did */
			if (UBOX_READ_2(dev, PCIR_VENDOR) != PCI_E5_VID_ID ||
			    UBOX_READ_2(dev, PCIR_DEVICE) !=
			    PCI_E5_UBOX0_DID_ID)
				break;

			val = UBOX_READ_4(dev, PCI_E5_UBOX0_CPUNODEID);
			node = __SHIFTOUT(val,
			    PCI_E5_UBOX0_CPUNODEID_LCLNODEID);

			ksnprintf(desc, sizeof(desc), "%s node%d channel%d",
			    t->desc, node, t->chan);
			device_set_desc_copy(dev, desc);

			sc->temp_chan = t->chan;
			sc->temp_node = node;

			return 0;
		}
	}
	return ENXIO;
}

static int
memtemp_e5_attach(device_t dev)
{
	struct memtemp_e5_softc *sc = device_get_softc(dev);
	int dimm;

	sc->temp_dev = dev;
	TAILQ_INIT(&sc->temp_dimm);

	for (dimm = 0; dimm < PCI_E5_IMC_DIMM_MAX; ++dimm) {
		struct memtemp_e5_dimm *dimm_sc;
		uint32_t dimmmtr;

		dimmmtr = IMC_CTAD_READ_4(sc->temp_dev, sc->temp_chan,
		    PCI_E5_IMC_CTAD_DIMMMTR(dimm));

		if ((dimmmtr & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP) == 0)
			continue;

		dimm_sc = kmalloc(sizeof(*dimm_sc), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		dimm_sc->dimm_id = dimm;
		dimm_sc->dimm_parent = sc;
		dimm_sc->dimm_extid =
		    (sc->temp_node * PCI_E5_IMC_CHN_MAX * PCI_E5_IMC_DIMM_MAX) +
		    (sc->temp_chan * PCI_E5_IMC_DIMM_MAX) + dimm;

		ksnprintf(dimm_sc->dimm_sensordev.xname,
		    sizeof(dimm_sc->dimm_sensordev.xname),
		    "dimm%d", dimm_sc->dimm_extid);
		dimm_sc->dimm_sensor.type = SENSOR_TEMP;
		sensor_attach(&dimm_sc->dimm_sensordev, &dimm_sc->dimm_sensor);
		if (sensor_task_register(dimm_sc, memtemp_e5_sensor_task, 2)) {
			device_printf(sc->temp_dev, "DIMM%d sensor task "
			    "register failed\n", dimm);
			kfree(dimm_sc, M_DEVBUF);
			continue;
		}
		sensordev_install(&dimm_sc->dimm_sensordev);

		TAILQ_INSERT_TAIL(&sc->temp_dimm, dimm_sc, dimm_link);
	}
	return 0;
}

static int
memtemp_e5_detach(device_t dev)
{
	struct memtemp_e5_softc *sc = device_get_softc(dev);
	struct memtemp_e5_dimm *dimm_sc;

	while ((dimm_sc = TAILQ_FIRST(&sc->temp_dimm)) != NULL) {
		TAILQ_REMOVE(&sc->temp_dimm, dimm_sc, dimm_link);

		sensordev_deinstall(&dimm_sc->dimm_sensordev);
		sensor_task_unregister(dimm_sc);

		kfree(dimm_sc, M_DEVBUF);
	}
	return 0;
}

static void
memtemp_e5_sensor_task(void *xdimm_sc)
{
	struct memtemp_e5_dimm *dimm_sc = xdimm_sc;
	struct ksensor *sensor = &dimm_sc->dimm_sensor;
	uint32_t val;
	int temp;

	val = pci_read_config(dimm_sc->dimm_parent->temp_dev,
	    PCI_E5_IMC_THERMAL_DIMMTEMPSTAT(dimm_sc->dimm_id), 4);
	temp = __SHIFTOUT(val, PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMP);

	sensor->flags &= ~SENSOR_FINVALID;
	sensor->value = (temp * 1000000) + 273150000;
}
