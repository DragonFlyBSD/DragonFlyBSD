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

#include <dev/misc/ecc/e5_imc_reg.h>
#include <dev/misc/ecc/e5_imc_var.h>

#define MEMTEMP_E5_DIMM_TEMP_HIWAT	85	/* spec default TEMPLO */
#define MEMTEMP_E5_DIMM_TEMP_STEP	5	/* spec TEMPLO/MID/HI step */

struct memtemp_e5_softc;

struct memtemp_e5_dimm {
	TAILQ_ENTRY(memtemp_e5_dimm)	dimm_link;
	struct ksensordev		dimm_sensordev;
	struct ksensor			dimm_sensor;
	struct memtemp_e5_softc		*dimm_parent;
	int				dimm_id;
	int				dimm_temp_hiwat;
	int				dimm_temp_lowat;
	int				dimm_flags;
};

#define MEMTEMP_E5_DIMM_FLAG_CRIT	0x1

struct memtemp_e5_softc {
	device_t			temp_dev;
	const struct e5_imc_chan	*temp_chan;
	int				temp_node;
	TAILQ_HEAD(, memtemp_e5_dimm)	temp_dimm;
};

static int	memtemp_e5_probe(device_t);
static int	memtemp_e5_attach(device_t);
static int	memtemp_e5_detach(device_t);

static int	memtemp_e5_tempth_adjust(int);
static void	memtemp_e5_tempth_str(int, char *, int);
static void	memtemp_e5_sensor_task(void *);

#define MEMTEMP_E5_CHAN(v, imc, c, c_ext)			\
{								\
	.did		= PCI_E5V##v##_IMC##imc##_THERMAL_CHN##c##_DID_ID, \
	.slot		= PCISLOT_E5V##v##_IMC##imc##_THERMAL_CHN##c, \
	.func		= PCIFUNC_E5V##v##_IMC##imc##_THERMAL_CHN##c, \
	.desc		= "Intel E5 v" #v " memory thermal sensor", \
								\
	E5_IMC_CHAN_FIELDS(v, imc, c, c_ext)			\
}

#define MEMTEMP_E5_CHAN_V2(c)		MEMTEMP_E5_CHAN(2, 0, c, c)
#define MEMTEMP_E5_CHAN_IMC0_V3(c)	MEMTEMP_E5_CHAN(3, 0, c, c)
#define MEMTEMP_E5_CHAN_IMC1_V3(c, c_ext) \
					MEMTEMP_E5_CHAN(3, 1, c, c_ext)
#define MEMTEMP_E5_CHAN_END		E5_IMC_CHAN_END

static const struct e5_imc_chan memtemp_e5_chans[] = {
	MEMTEMP_E5_CHAN_V2(0),
	MEMTEMP_E5_CHAN_V2(1),
	MEMTEMP_E5_CHAN_V2(2),
	MEMTEMP_E5_CHAN_V2(3),

	MEMTEMP_E5_CHAN_IMC0_V3(0),
	MEMTEMP_E5_CHAN_IMC0_V3(1),
	MEMTEMP_E5_CHAN_IMC0_V3(2),
	MEMTEMP_E5_CHAN_IMC0_V3(3),
	MEMTEMP_E5_CHAN_IMC1_V3(0, 2),	/* IMC1 chan0 -> channel2 */
	MEMTEMP_E5_CHAN_IMC1_V3(1, 3),	/* IMC1 chan1 -> channel3 */

	MEMTEMP_E5_CHAN_END
};

#undef MEMTEMP_E5_CHAN_END
#undef MEMTEMP_E5_CHAN_V2
#undef MEMTEMP_E5_CHAN

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
	const struct e5_imc_chan *c;
	uint16_t vid, did;
	int slot, func;

	vid = pci_get_vendor(dev);
	if (vid != PCI_E5_IMC_VID_ID)
		return ENXIO;

	did = pci_get_device(dev);
	slot = pci_get_slot(dev);
	func = pci_get_function(dev);

	for (c = memtemp_e5_chans; c->desc != NULL; ++c) {
		if (c->did == did && c->slot == slot && c->func == func) {
			struct memtemp_e5_softc *sc = device_get_softc(dev);
			char desc[128];
			uint32_t cfg;
			int node;

			node = e5_imc_node_probe(dev, c);
			if (node < 0)
				break;

			/*
			 * XXX
			 * It seems that memory thermal sensor is available,
			 * only if CLTT is set (OLTT_EN does not seem matter).
			 */
			cfg = pci_read_config(dev,
			    PCI_E5_IMC_THERMAL_CHN_TEMP_CFG, 4);
			if ((cfg & PCI_E5_IMC_THERMAL_CHN_TEMP_CFG_CLTT) == 0)
				break;

			ksnprintf(desc, sizeof(desc), "%s node%d channel%d",
			    c->desc, node, c->chan_ext);
			device_set_desc_copy(dev, desc);

			sc->temp_chan = c;
			sc->temp_node = node;

			return 0;
		}
	}
	return ENXIO;
}

static int
memtemp_e5_tempth_adjust(int temp)
{
	if (temp == PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_DISABLE)
		return 0;
	else if (temp < PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_TEMPMIN ||
	    temp >= PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_TEMPMAX)
		return -1;
	return temp;
}

static void
memtemp_e5_tempth_str(int temp, char *temp_str, int temp_strlen)
{
	if (temp < 0)
		strlcpy(temp_str, "reserved", temp_strlen);
	else if (temp == 0)
		strlcpy(temp_str, "disabled", temp_strlen);
	else
		ksnprintf(temp_str, temp_strlen, "%dC", temp);
}

static int
memtemp_e5_attach(device_t dev)
{
	struct memtemp_e5_softc *sc = device_get_softc(dev);
	int dimm;

	sc->temp_dev = dev;
	TAILQ_INIT(&sc->temp_dimm);

	for (dimm = 0; dimm < PCI_E5_IMC_CHN_DIMM_MAX; ++dimm) {
		char temp_lostr[16], temp_midstr[16], temp_histr[16];
		struct memtemp_e5_dimm *dimm_sc;
		int dimm_extid, temp_lo, temp_mid, temp_hi;
		uint32_t dimmmtr, temp_th;

		dimmmtr = IMC_CTAD_READ_4(dev, sc->temp_chan,
		    PCI_E5_IMC_CTAD_DIMMMTR(dimm));

		if ((dimmmtr & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP) == 0)
			continue;

		dimm_sc = kmalloc(sizeof(*dimm_sc), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		dimm_sc->dimm_id = dimm;
		dimm_sc->dimm_parent = sc;

		temp_th = pci_read_config(dev,
		    PCI_E5_IMC_THERMAL_DIMM_TEMP_TH(dimm), 4);

		temp_lo = __SHIFTOUT(temp_th,
		    PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_TEMPLO);
		temp_lo = memtemp_e5_tempth_adjust(temp_lo);
		memtemp_e5_tempth_str(temp_lo, temp_lostr, sizeof(temp_lostr));

		temp_mid = __SHIFTOUT(temp_th,
		    PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_TEMPMID);
		temp_mid = memtemp_e5_tempth_adjust(temp_mid);
		memtemp_e5_tempth_str(temp_mid, temp_midstr,
		    sizeof(temp_midstr));

		temp_hi = __SHIFTOUT(temp_th,
		    PCI_E5_IMC_THERMAL_DIMM_TEMP_TH_TEMPHI);
		temp_hi = memtemp_e5_tempth_adjust(temp_hi);
		memtemp_e5_tempth_str(temp_hi, temp_histr, sizeof(temp_histr));

		/*
		 * NOTE:
		 * - TEMPHI initiates THRTCRIT.
		 * - TEMPMID initiates THRTHI, so it is also taken into
		 *   consideration.
		 * - Some BIOSes program temp_lo to a rediculous low value,
		 *   so ignore TEMPLO here.
		 */
		if (temp_mid <= 0) {
			if (temp_hi <= 0)
				dimm_sc->dimm_temp_hiwat = MEMTEMP_E5_DIMM_TEMP_HIWAT;
			else
				dimm_sc->dimm_temp_hiwat = temp_hi;
		} else {
			dimm_sc->dimm_temp_hiwat = temp_mid;
		}
		if (dimm_sc->dimm_temp_hiwat < MEMTEMP_E5_DIMM_TEMP_STEP)
			dimm_sc->dimm_temp_hiwat = MEMTEMP_E5_DIMM_TEMP_HIWAT;
		dimm_sc->dimm_temp_lowat = dimm_sc->dimm_temp_hiwat -
		    MEMTEMP_E5_DIMM_TEMP_STEP;

		device_printf(dev, "DIMM%d "
		    "temp_hi %s, temp_mid %s, temp_lo %s\n", dimm,
		    temp_histr, temp_midstr, temp_lostr);
		device_printf(dev, "DIMM%d hiwat %dC, lowat %dC\n", dimm,
		    dimm_sc->dimm_temp_hiwat, dimm_sc->dimm_temp_lowat);

		dimm_extid =
		(sc->temp_node * PCI_E5_IMC_CHN_MAX * PCI_E5_IMC_CHN_DIMM_MAX) +
		(sc->temp_chan->chan_ext * PCI_E5_IMC_CHN_DIMM_MAX) + dimm;
		ksnprintf(dimm_sc->dimm_sensordev.xname,
		    sizeof(dimm_sc->dimm_sensordev.xname),
		    "dimm%d", dimm_extid);
		dimm_sc->dimm_sensor.type = SENSOR_TEMP;
		sensor_attach(&dimm_sc->dimm_sensordev, &dimm_sc->dimm_sensor);
		if (sensor_task_register(dimm_sc, memtemp_e5_sensor_task, 2)) {
			device_printf(dev, "DIMM%d sensor task register "
			    "failed\n", dimm);
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
	device_t dev = dimm_sc->dimm_parent->temp_dev;
	int dimm = dimm_sc->dimm_id;
	uint32_t val;
	int temp, reg;

	reg = PCI_E5_IMC_THERMAL_DIMMTEMPSTAT(dimm);

	val = pci_read_config(dev, reg, 4);
	if (val & (PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMPHI |
	    PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMPMID |
	    PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMPLO))
		pci_write_config(dev, reg, val, 4);

	temp = __SHIFTOUT(val, PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMP);
	if (temp < PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMPMIN ||
	    temp >= PCI_E5_IMC_THERMAL_DIMMTEMPSTAT_TEMPMAX) {
		sensor->status = SENSOR_S_UNSPEC;
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
		return;
	}

	/*
	 * Some BIOSes will always turn on TEMPMID, so we rely on
	 * our own hiwat/lowat to send the notification.
	 */
	if (temp >= dimm_sc->dimm_temp_hiwat &&
	    (dimm_sc->dimm_flags & MEMTEMP_E5_DIMM_FLAG_CRIT) == 0) {
		int node, chan;
		char temp_str[16], data[64];

		node = dimm_sc->dimm_parent->temp_node;
		chan = dimm_sc->dimm_parent->temp_chan->chan_ext;

		ksnprintf(temp_str, sizeof(temp_str), "%d", temp);
		ksnprintf(data, sizeof(data),
		    "node=%d channel=%d dimm=%d", node, chan, dimm);
		devctl_notify("memtemp", "Thermal", temp_str, data);

		device_printf(dev, "node%d channel%d DIMM%d "
		    "temperature (%dC) is too high (>= %d)\n",
		    node, chan, dimm, temp, dimm_sc->dimm_temp_hiwat);

		dimm_sc->dimm_flags |= MEMTEMP_E5_DIMM_FLAG_CRIT;
	} else if ((dimm_sc->dimm_flags & MEMTEMP_E5_DIMM_FLAG_CRIT) &&
	     temp < dimm_sc->dimm_temp_lowat) {
		dimm_sc->dimm_flags &= ~MEMTEMP_E5_DIMM_FLAG_CRIT;
	}

	if (dimm_sc->dimm_flags & MEMTEMP_E5_DIMM_FLAG_CRIT)
		sensor->status = SENSOR_S_CRIT;
	else
		sensor->status = SENSOR_S_OK;
	sensor->flags &= ~SENSOR_FINVALID;
	sensor->value = (temp * 1000000) + 273150000;
}
