/*
 * Copyright (c) 2015 Imre Vadász <imre@vdsz.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for Intel's On Die power usage estimation via MSR.
 * Supported by Sandy Bridge and later CPUs, and also by Atom CPUs
 * of the Silvermont and later architectures.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/cpu_topology.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/bitops.h>

#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#include "cpu_if.h"

#define MSR_RAPL_POWER_UNIT_POWER	__BITS64(0, 3)
#define MSR_RAPL_POWER_UNIT_ENERGY	__BITS64(8, 12)
#define MSR_RAPL_POWER_UNIT_TIME	__BITS64(16, 19)

struct corepower_sensor {
	uint64_t	energy;
	u_int		msr;
	struct ksensor	sensor;
};

struct corepower_softc {
	device_t		sc_dev;

	uint32_t		sc_watt_unit;
	uint32_t		sc_joule_unit;
	uint32_t		sc_second_unit;

	int			sc_have_sens;
	int			sc_is_atom;

	struct corepower_sensor	sc_pkg_sens;
	struct corepower_sensor	sc_dram_sens;
	struct corepower_sensor	sc_pp0_sens;
	struct corepower_sensor	sc_pp1_sens;

	struct ksensordev	sc_sensordev;
	struct sensor_task	*sc_senstask;
};

/*
 * Device methods.
 */
static void	corepower_identify(driver_t *driver, device_t parent);
static int	corepower_probe(device_t dev);
static int	corepower_attach(device_t dev);
static int	corepower_detach(device_t dev);
static uint32_t	corepower_energy_to_uwatts(struct corepower_softc *sc,
					   uint32_t units, uint32_t secs);
static void	corepower_refresh(void *arg);
static void	corepower_sens_init(struct corepower_sensor *sens,
				    char *desc, u_int msr, int cpu);
static void	corepower_sens_update(struct corepower_softc *sc,
				      struct corepower_sensor *sens);
static int	corepower_try(u_int msr, char *name);

static device_method_t corepower_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	corepower_identify),
	DEVMETHOD(device_probe,		corepower_probe),
	DEVMETHOD(device_attach,	corepower_attach),
	DEVMETHOD(device_detach,	corepower_detach),

	DEVMETHOD_END
};

static driver_t corepower_driver = {
	"corepower",
	corepower_methods,
	sizeof(struct corepower_softc),
};

static devclass_t corepower_devclass;
DRIVER_MODULE(corepower, cpu, corepower_driver, corepower_devclass, NULL, NULL);
MODULE_VERSION(corepower, 1);

static void
corepower_identify(driver_t *driver, device_t parent)
{
	device_t child;
	const struct cpu_node *node;
	int cpu, master_cpu;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "corepower", -1) != NULL)
		return;

	/* Check that the vendor is Intel. */
	if (cpu_vendor_id != CPU_VENDOR_INTEL)
		return;

	/* We only want one child per CPU package */
	cpu = device_get_unit(parent);
	node = get_cpu_node_by_cpuid(cpu);
	while (node != NULL) {
		if (node->type == CHIP_LEVEL) {
			if (node->child_no == 0)
				node = NULL;
			break;
		}
		node = node->parent_node;
	}
	if (node == NULL)
		return;

	master_cpu = BSRCPUMASK(node->members);
	if (cpu != master_cpu)
		return;

	child = device_add_child(parent, "corepower", -1);
	if (child == NULL)
		device_printf(parent, "add corepower child failed\n");
}

static int
corepower_probe(device_t dev)
{
	int cpu_family, cpu_model;

	if (resource_disabled("corepower", 0))
		return (ENXIO);

	cpu_model = CPUID_TO_MODEL(cpu_id);
	cpu_family = CPUID_TO_FAMILY(cpu_id);

	if (cpu_family == 0x06) {
		switch (cpu_model) {
		/* Core CPUs */
		case 0x2a:
		case 0x3a:
		/* Xeon CPUs */
		case 0x2d:
		case 0x3e:
		case 0x3f:
		case 0x4f:
		case 0x56:
		/* Haswell, Broadwell, Skylake */
		case 0x3c:
		case 0x3d:
		case 0x45:
		case 0x46:
		case 0x47:
		case 0x4e:
		case 0x5e:
		/* Atom CPUs */
		case 0x37:
		case 0x4a:
		case 0x4c:
		case 0x4d:
		case 0x5a:
		case 0x5d:
			break;
		default:
			return (ENXIO);
		}
	}

	if (corepower_try(MSR_RAPL_POWER_UNIT, "MSR_RAPL_POWER_UNIT") == 0)
		return (ENXIO);

	device_set_desc(dev, "CPU On-Die Power Usage Estimation");

	return (BUS_PROBE_GENERIC);
}

static int
corepower_attach(device_t dev)
{
	struct corepower_softc *sc = device_get_softc(dev);
	uint64_t val;
	uint32_t power_units;
	uint32_t energy_units;
	uint32_t time_units;
	int cpu_family, cpu_model;
	int cpu;

	sc->sc_dev = dev;
	sc->sc_have_sens = 0;
	sc->sc_is_atom = 0;

	cpu_family = CPUID_TO_FAMILY(cpu_id);
	cpu_model = CPUID_TO_MODEL(cpu_id);

	/* Check CPU model */
	if (cpu_family == 0x06) {
		switch (cpu_model) {
		/* Core CPUs */
		case 0x2a:
		case 0x3a:
			sc->sc_have_sens = 0xd;
			break;
		/* Xeon CPUs */
		case 0x2d: /* Only Xeon branded, Core i version should probably be 0x5 */
		case 0x3e:
		case 0x3f:
		case 0x4f:
		case 0x56:
			sc->sc_have_sens = 0x7;
			break;
		/* Haswell, Broadwell, Skylake */
		case 0x3c:
		case 0x3d:
		case 0x45:
		case 0x46:
		case 0x47:
		case 0x4e:
		case 0x5e:
			/* Check if Core or Xeon (Xeon CPUs might be 0x7) */
			sc->sc_have_sens = 0xf;
			break;
		/* Atom CPUs */
		case 0x37:
		case 0x4a:
		case 0x4c:
		case 0x4d:
		case 0x5a:
		case 0x5d:
			sc->sc_have_sens = 0x5;
			/* use quirk for Valleyview Atom CPUs */
			sc->sc_is_atom = 1;
			break;
		default:
			return (ENXIO);
		}
	}

	val = rdmsr(MSR_RAPL_POWER_UNIT);

	power_units = __SHIFTOUT(val, MSR_RAPL_POWER_UNIT_POWER);
	energy_units = __SHIFTOUT(val, MSR_RAPL_POWER_UNIT_ENERGY);
	time_units = __SHIFTOUT(val, MSR_RAPL_POWER_UNIT_TIME);

	sc->sc_watt_unit = (1 << power_units);
	sc->sc_joule_unit = (1 << energy_units);
	sc->sc_second_unit = (1 << time_units);

	/*
	 * Add hw.sensors.cpu_nodeN MIB.
	 */
	cpu = device_get_unit(device_get_parent(dev));
	ksnprintf(sc->sc_sensordev.xname, sizeof(sc->sc_sensordev.xname),
	    "cpu_node%d", get_chip_ID(cpu));
	if ((sc->sc_have_sens & 1) &&
	    corepower_try(MSR_PKG_ENERGY_STATUS, "MSR_PKG_ENERGY_STATUS")) {
		corepower_sens_init(&sc->sc_pkg_sens, "Package Power",
		    MSR_PKG_ENERGY_STATUS, cpu);
		sensor_attach(&sc->sc_sensordev, &sc->sc_pkg_sens.sensor);
	} else {
		sc->sc_have_sens &= ~1;
	}
	if ((sc->sc_have_sens & 2) &&
	    corepower_try(MSR_DRAM_ENERGY_STATUS, "MSR_DRAM_ENERGY_STATUS")) {
		corepower_sens_init(&sc->sc_dram_sens, "DRAM Power",
		    MSR_DRAM_ENERGY_STATUS, cpu);
		sensor_attach(&sc->sc_sensordev, &sc->sc_dram_sens.sensor);
	} else {
		sc->sc_have_sens &= ~2;
	}
	if ((sc->sc_have_sens & 4) &&
	    corepower_try(MSR_PP0_ENERGY_STATUS, "MSR_PP0_ENERGY_STATUS")) {
		corepower_sens_init(&sc->sc_pp0_sens, "Cores Power",
		    MSR_PP0_ENERGY_STATUS, cpu);
		sensor_attach(&sc->sc_sensordev, &sc->sc_pp0_sens.sensor);
	} else {
		sc->sc_have_sens &= ~4;
	}
	if ((sc->sc_have_sens & 8) &&
	    corepower_try(MSR_PP1_ENERGY_STATUS, "MSR_PP1_ENERGY_STATUS")) {
		corepower_sens_init(&sc->sc_pp1_sens, "Graphics Power",
		    MSR_PP1_ENERGY_STATUS, cpu);
		sensor_attach(&sc->sc_sensordev, &sc->sc_pp1_sens.sensor);
	} else {
		sc->sc_have_sens &= ~8;
	}

	if (sc->sc_have_sens == 0)
		return (ENXIO);

	sc->sc_senstask = sensor_task_register2(sc, corepower_refresh, 1, cpu);

	sensordev_install(&sc->sc_sensordev);

	return (0);
}

static int
corepower_detach(device_t dev)
{
	struct corepower_softc *sc = device_get_softc(dev);

	sensordev_deinstall(&sc->sc_sensordev);
	sensor_task_unregister2(sc->sc_senstask);

	return (0);
}

static uint32_t
corepower_energy_to_uwatts(struct corepower_softc *sc, uint32_t units,
    uint32_t secs)
{
	uint64_t val;

	if (sc->sc_is_atom) {
		val = ((uint64_t)units) * sc->sc_joule_unit;
	} else {
		val = ((uint64_t)units) * 1000ULL * 1000ULL;
		val /= sc->sc_joule_unit;
	}

	return val / secs;
}

static void
corepower_refresh(void *arg)
{
	struct corepower_softc *sc = (struct corepower_softc *)arg;

	if (sc->sc_have_sens & 1)
		corepower_sens_update(sc, &sc->sc_pkg_sens);
	if (sc->sc_have_sens & 2)
		corepower_sens_update(sc, &sc->sc_dram_sens);
	if (sc->sc_have_sens & 4)
		corepower_sens_update(sc, &sc->sc_pp0_sens);
	if (sc->sc_have_sens & 8)
		corepower_sens_update(sc, &sc->sc_pp1_sens);
}

static void
corepower_sens_init(struct corepower_sensor *sens, char *desc, u_int msr,
    int cpu)
{
	ksnprintf(sens->sensor.desc, sizeof(sens->sensor.desc), "node%d %s",
	    get_chip_ID(cpu), desc);
	sens->sensor.type = SENSOR_WATTS;
	sens->msr = msr;
	sens->energy = rdmsr(sens->msr) & 0xffffffffU;
}

static void
corepower_sens_update(struct corepower_softc *sc,
    struct corepower_sensor *sens)
{
	uint64_t a, res;

	a = rdmsr(sens->msr) & 0xffffffffU;
	if (sens->energy > a) {
		res = (0x100000000ULL - sens->energy) + a;
	} else {
		res = a - sens->energy;
	}
	sens->energy = a;
	sens->sensor.value = corepower_energy_to_uwatts(sc, res, 1);
}

static int
corepower_try(u_int msr, char *name)
{
	uint64_t val;

	if (rdmsr_safe(msr, &val) != 0) {
		kprintf("msr %s (0x%08x) not available\n", name, msr);
		return 0;
	}
	return 1;
}
