/*
 * Copyright (c) 2026 Imre Vadász <imre@vdsz.com>
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
 * Device driver for Intel's C-state residency counters.
 * Supports Nehalem and later Core CPUs.
 * Currently supports models up to CoffeeLake generation.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/cpu_topology.h>
#include <sys/kernel.h>
#include <sys/thread2.h>
#include <sys/sensors.h>
#include <sys/bitops.h>

#include <machine/clock.h>
#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#include "cpu_if.h"

#define ATOM_MSR_PKG_CSTATE_COUNTER_HZ	(1000 * 1000)
#define ATOM_MSR_PKG_C2_RESIDENCY	MSR_PKG_C3_RESIDENCY
#define ATOM_MSR_PKG_C4_RESIDENCY	MSR_PKG_C6_RESIDENCY
#define ATOM_MSR_PKG_C6_RESIDENCY	MSR_PKG_C7_RESIDENCY

#define ATOM_MSR_CORE_C1_RESIDENCY	0x660
#define ATOM_MSR_CORE_C6_RESIDENCY	MSR_CORE_C6_RESIDENCY

struct corecstat_sensor {
	uint64_t	tsc_count;
	uint64_t	cst_count;
	u_int		msr;
	int		bits;
	uint64_t	mask;
	struct ksensor	sensor;
};

struct corecstat_softc {
	device_t		sc_dev;

	int			sc_is_atom;
	/*
	 * sc_is_atom == 0
	 *   0 ==> Nehalem/Westmere
	 *   1 ==> Sandy Bridge / Ivy Bridge / Haswell / Broadwell / Skylake
	 *   2 ==> Haswell/Broadwell - low-power
	 *
	 * sc_is_atom == 1
	 *   0 ==> Original Atom
	 *   1 ==> Airmont Atom
	 */
	int			sc_version;

	struct corecstat_sensor	sc_core_sens[3];
	u_int			sc_core_sens_cnt;

#define PC2_IDX  0
#define PC3_IDX  1
#define PC6_IDX  2
#define PC7_IDX  3
#define PC8_IDX  4
#define PC9_IDX  5
#define PC10_IDX 6
	struct corecstat_sensor	*sc_pkg_sens;
	u_int			sc_pkg_sens_cnt;

	struct sensor_task	*sc_senstask;
	struct ksensordev	*sc_cpu_sensordev;

	struct ksensordev	*sc_sensordev;
};

/*
 * Device methods.
 */
static void	corecstat_identify(driver_t *driver, device_t parent);
static int	corecstat_probe(device_t dev);
static int	corecstat_attach(device_t dev);
static int	corecstat_detach(device_t dev);
static void	corecstat_init_atom_pkg(struct corecstat_softc *sc, int cpu);
static void	corecstat_init_atom_cpu(struct corecstat_softc *sc, int cpu);
static void	corecstat_init_core_pkg(struct corecstat_softc *sc, int cpu);
static void	corecstat_init_core_cpu(struct corecstat_softc *sc, int cpu);
static void	corecstat_refresh(void *arg);
static void	corecstat_pkg_sens_init(struct corecstat_softc *sc,
					int cpu, char *desc, u_int msr,
					int bits);
static void	corecstat_core_sens_init(struct corecstat_softc *sc,
					int cpu, char *desc, u_int msr,
					int bits);
static void	corecstat_sens_init(struct corecstat_sensor *sens,
				    char *desc, u_int msr, int bits);
static void	corecstat_sens_update(struct corecstat_softc *sc,
				      struct corecstat_sensor *sens);
static int	corecstat_try(u_int msr, char *name);

static device_method_t corecstat_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	corecstat_identify),
	DEVMETHOD(device_probe,		corecstat_probe),
	DEVMETHOD(device_attach,	corecstat_attach),
	DEVMETHOD(device_detach,	corecstat_detach),

	DEVMETHOD_END
};

static driver_t corecstat_driver = {
	"corecstat",
	corecstat_methods,
	sizeof(struct corecstat_softc),
};

static devclass_t corecstat_devclass;
DRIVER_MODULE(corecstat, cpu, corecstat_driver, corecstat_devclass, NULL, NULL);
MODULE_VERSION(corecstat, 1);

static void
corecstat_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "corecstat", -1) != NULL)
		return;

	/* Check that the vendor is Intel. */
	if (cpu_vendor_id != CPU_VENDOR_INTEL)
		return;

	/*
	 * We add a child for each CPU since settings must be performed
	 * on each CPU in the SMP case.
	 */
	child = device_add_child(parent, "corecstat", -1);
	if (child == NULL)
		device_printf(parent, "add corecstat child failed\n");
}

static int
corecstat_probe(device_t dev)
{
	int cpu_family, cpu_model;

	if (resource_disabled("corecstat", 0))
		return (ENXIO);

	cpu_model = CPUID_TO_MODEL(cpu_id);
	cpu_family = CPUID_TO_FAMILY(cpu_id);

	if (cpu_family == 0x06) {
		switch (cpu_model) {
		/* ATOM */
		case 0x1c:
		case 0x26:
		case 0x27:
		case 0x35:
		case 0x36:
			break;
		/* Silvermont Atom */
		case 0x37:
		case 0x4a:
		case 0x4c: /* Airmont Atom */
		case 0x4d:
		case 0x5a:
		case 0x5d:
			break;
		/* Nehalem */
		case 0x1a:
		case 0x1e:
		case 0x1f:
		case 0x25: /* Westmere */
		case 0x2c: /* Westmere */
		case 0x2e:
		case 0x2f: /* Westmere */
			break;
		/* Sandy Bridge */
		case 0x2a:
		case 0x2d:
		case 0x3a: /* Ivy Bridge */
		case 0x3e: /* Ivy Bridge-E */
			break;
		/* Haswell */
		case 0x3c:
		case 0x3f: /* Haswell-E */
		case 0x45:
		case 0x46:
			break;
		/* Broadwell */
		case 0x3d:
		case 0x47:
		case 0x4f:
		case 0x56:
			break;
		/* Skylake */
		case 0x4e:
		case 0x5e:
		/* Kabylake/Coffeelake */
		case 0x8e:
			break;
		default:
			return (ENXIO);
		}
	}

	device_set_desc(dev, "CPU Package C-State residency counters");

	return (BUS_PROBE_GENERIC);
}

static int
corecstat_attach(device_t dev)
{
	struct corecstat_softc *sc = device_get_softc(dev);
	const struct cpu_node *node;
	int cpu = device_get_unit(device_get_parent(dev));
	int cpu_family, cpu_model;

	sc->sc_dev = dev;
	sc->sc_is_atom = 0;
	sc->sc_core_sens_cnt = 0;
	sc->sc_pkg_sens_cnt = 0;

	cpu_model = CPUID_TO_MODEL(cpu_id);
	cpu_family = CPUID_TO_FAMILY(cpu_id);

	if (cpu_family == 0x06) {
		switch (cpu_model) {
		/* ATOM */
		case 0x1c:
		case 0x26:
		case 0x27:
		case 0x35:
		case 0x36:
			/* Only Package C-States available, no Core C-states */
			sc->sc_is_atom = 1;
			sc->sc_version = 0;
			break;
		/* Silvermont Atom */
		case 0x37:
		case 0x4a:
		case 0x4c: /* Airmont Atom */
		case 0x4d:
		case 0x5a:
		case 0x5d:
			sc->sc_is_atom = 1;
			sc->sc_version = 1;
			break;
		/* Nehalem */
		case 0x1a:
		case 0x1e:
		case 0x1f:
		case 0x25: /* Westmere */
		case 0x2c: /* Westmere */
		case 0x2e:
		case 0x2f: /* Westmere */
			/* PKG_C3, PKG_C6, PKG_C7 */
			/* CORE_C3, CORE_C6 */
			sc->sc_version = 0;
			break;
		/* Sandy Bridge */
		case 0x2a:
		case 0x2d:
		/* Ivy Bridge */
		case 0x3a:
		case 0x3e: /* Ivy Bridge-E */
		/* Haswell */
		case 0x3c:
		case 0x3f: /* Haswell-E */
		case 0x46:
		/* Broadwell */
		case 0x47:
		case 0x4f:
		case 0x56:
		/* Skylake */
		case 0x4e:
		case 0x5e:
			/* PKG_C2, PKG_C3, PKG_C6, PKG_C7 */
			/* CORE_C3, CORE_C6, CORE_C7 */
			sc->sc_version = 1;
			break;
		case 0x45:	/* Haswell low-power CPUs */
		case 0x3d:	/* Broadwell low-power CPUs */
		case 0x8e:	/* Kabylake/Coffeelake CPUs */
			/* PKG_C2, PKG_C3, PKG_C6, PKG_C7, PKG_C8, PKG_C9, PKG_C10 */
			/* CORE_C3, CORE_C6, CORE_C7 */
			sc->sc_version = 2;
			break;
		default:
			return (ENXIO);
		}
	}

	sc->sc_cpu_sensordev = CPU_GET_SENSDEV(devclass_find_unit("cpu", cpu));
	if (sc->sc_cpu_sensordev == NULL)
		return (1);

	node = get_cpu_node_by_cpuid(cpu);
	while (node != NULL) {
		if (node->type == CORE_LEVEL &&
		    !(cpu == BSRCPUMASK(node->members)))
			return (ENXIO);
		if (node->type == CHIP_LEVEL) {
			if (node->child_no == 0)
				node = NULL;
			break;
		}
		node = node->parent_node;
	}
	if (node != NULL && cpu == BSRCPUMASK(node->members)) {
		sc->sc_sensordev = kmalloc(sizeof(struct ksensordev),
		    M_DEVBUF, M_WAITOK | M_ZERO);

		ksnprintf(sc->sc_sensordev->xname, sizeof(
		    sc->sc_sensordev->xname), "cpu_node%d", get_chip_ID(cpu));

		if (sc->sc_is_atom)
			corecstat_init_atom_pkg(sc, cpu);
		else
			corecstat_init_core_pkg(sc, cpu);

		sensordev_install(sc->sc_sensordev);
	} else if (sc->sc_is_atom && sc->sc_version == 0) {
		return (ENXIO);
	}

	if (sc->sc_is_atom)
		corecstat_init_atom_cpu(sc, cpu);
	else
		corecstat_init_core_cpu(sc, cpu);

	sc->sc_senstask = sensor_task_register2(sc, corecstat_refresh, 1,
	    device_get_unit(dev));

	return (0);
}

static int
corecstat_detach(device_t dev)
{
	struct corecstat_softc *sc = device_get_softc(dev);

	if (sc->sc_senstask != NULL) {
		sensor_task_unregister2(sc->sc_senstask);
		sc->sc_senstask = NULL;
	}
	if (sc->sc_cpu_sensordev != NULL) {
		int i;

		for (i = 0; i < sc->sc_core_sens_cnt; i++) {
			sensor_detach(sc->sc_cpu_sensordev,
			    &sc->sc_core_sens[i].sensor);
		}
	}
	if (sc->sc_sensordev != NULL) {
		sensordev_deinstall(sc->sc_sensordev);
		kfree(sc->sc_sensordev, M_DEVBUF);
		sc->sc_sensordev = NULL;
	}
	if (sc->sc_pkg_sens != NULL) {
		kfree(sc->sc_pkg_sens, M_DEVBUF);
		sc->sc_pkg_sens = NULL;
	}

	return (0);
}

static void
corecstat_init_atom_pkg(struct corecstat_softc *sc, int cpu)
{
	if (sc->sc_version == 0) {
		sc->sc_pkg_sens = kmalloc(3 * sizeof(struct corecstat_sensor),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC2 residency",
		    ATOM_MSR_PKG_C2_RESIDENCY, 64);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC4 residency",
		    ATOM_MSR_PKG_C4_RESIDENCY, 64);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC6 residency",
		    ATOM_MSR_PKG_C6_RESIDENCY, 64);
	} else {
		sc->sc_pkg_sens = kmalloc(1 * sizeof(struct corecstat_sensor),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC6 residency",
		    ATOM_MSR_PKG_C6_RESIDENCY, 64);
	}
}

static void
corecstat_init_atom_cpu(struct corecstat_softc *sc, int cpu)
{
	/* No Core C-state residency counters on old Atoms */
	KKASSERT(sc->sc_version != 0);

	corecstat_core_sens_init(sc, cpu, "node%d core%d C1 res.",
	    ATOM_MSR_CORE_C1_RESIDENCY, 64);
	corecstat_core_sens_init(sc, cpu, "node%d core%d C6 res.",
	    ATOM_MSR_CORE_C6_RESIDENCY, 64);
}

static void
corecstat_init_core_pkg(struct corecstat_softc *sc, int cpu)
{
	sc->sc_pkg_sens = kmalloc(7 * sizeof(struct corecstat_sensor),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc->sc_version == 1 || sc->sc_version == 2) {
		corecstat_pkg_sens_init(sc, cpu, "node%d PC2 residency",
		    MSR_PKG_C2_RESIDENCY, 64);
	}

	corecstat_pkg_sens_init(sc, cpu, "node%d PC3 residency",
	    MSR_PKG_C3_RESIDENCY, 64);
	corecstat_pkg_sens_init(sc, cpu, "node%d PC6 residency",
	    MSR_PKG_C6_RESIDENCY, 64);
	corecstat_pkg_sens_init(sc, cpu, "node%d PC7 residency",
	    MSR_PKG_C7_RESIDENCY, 64);

	if (sc->sc_version == 2) {
		corecstat_pkg_sens_init(sc, cpu, "node%d PC8 residency",
		    MSR_PKG_C8_RESIDENCY, 60);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC9 residency",
		    MSR_PKG_C9_RESIDENCY, 60);
		corecstat_pkg_sens_init(sc, cpu, "node%d PC10 residency",
		    MSR_PKG_C10_RESIDENCY, 60);
	}
}

static void
corecstat_init_core_cpu(struct corecstat_softc *sc, int cpu)
{
	corecstat_core_sens_init(sc, cpu, "node%d core%d C3 res.",
	    MSR_CORE_C3_RESIDENCY, 64);
	corecstat_core_sens_init(sc, cpu, "node%d core%d C6 res.",
	    MSR_CORE_C6_RESIDENCY, 64);

	if (sc->sc_version == 1 || sc->sc_version == 2) {
		corecstat_core_sens_init(sc, cpu, "node%d core%d C7 res.",
		    MSR_CORE_C7_RESIDENCY, 64);
	}
}

static void
corecstat_refresh(void *arg)
{
	struct corecstat_softc *sc = (struct corecstat_softc *)arg;
	int i;

	for (i = 0; i < sc->sc_core_sens_cnt; i++)
		corecstat_sens_update(sc, &sc->sc_core_sens[i]);
	for (i = 0; i < sc->sc_pkg_sens_cnt; i++)
		corecstat_sens_update(sc, &sc->sc_pkg_sens[i]);
}

static void
corecstat_core_sens_init(struct corecstat_softc *sc,
    int cpu, char *desc, u_int msr, int bits)
{
	struct corecstat_sensor *sens = &sc->sc_core_sens[sc->sc_core_sens_cnt];
	char buf[sizeof(sens->sensor.desc)];

	ksnprintf(buf, sizeof(buf), desc, get_chip_ID(cpu),
	    get_core_number_within_chip(cpu));
	if (corecstat_try(msr, buf)) {
		sc->sc_core_sens_cnt++;
		corecstat_sens_init(sens, buf, msr, bits);
		sensor_attach(sc->sc_cpu_sensordev, &sens->sensor);
	}
}

static void
corecstat_pkg_sens_init(struct corecstat_softc *sc,
    int cpu, char *desc, u_int msr, int bits)
{
	struct corecstat_sensor *sens = &sc->sc_pkg_sens[sc->sc_pkg_sens_cnt];
	char buf[sizeof(sens->sensor.desc)];

	ksnprintf(buf, sizeof(buf), desc, get_chip_ID(cpu));
	if (corecstat_try(msr, buf)) {
		sc->sc_pkg_sens_cnt++;
		corecstat_sens_init(sens, buf, msr, bits);
		sensor_attach(sc->sc_sensordev, &sens->sensor);
	}
}

static void
corecstat_sens_init(struct corecstat_sensor *sens, char *desc, u_int msr,
    int bits)
{
	uint64_t a, b;

	KKASSERT(bits > 0 && bits <= 64);
	sens->sensor.type = SENSOR_PERCENT;
	sens->sensor.flags |= SENSOR_FUNKNOWN;
	sens->msr = msr;
	sens->bits = bits;
	sens->mask = __BITS64(0, bits-1);

	crit_enter();
	a = rdtsc();
	b = rdmsr(msr);
	crit_exit();
	sens->tsc_count = a;
	sens->cst_count = b & sens->mask;

	strlcpy(sens->sensor.desc, desc, sizeof(sens->sensor.desc));
}

static void
corecstat_sens_update(struct corecstat_softc *sc,
    struct corecstat_sensor *sens)
{
	uint64_t a, b, msr;
	uint64_t tscdiff, cstdiff;

	msr = sens->msr;
	crit_enter();
	a = rdtsc();
	b = rdmsr(msr);
	crit_exit();

	b &= sens->mask;

	if (a < sens->tsc_count)
		tscdiff = a + (~sens->tsc_count);
	else
		tscdiff = a - sens->tsc_count;

	if (b < sens->cst_count)
		cstdiff = b + ((~sens->cst_count) & sens->mask);
	else
		cstdiff = b - sens->cst_count;

	sens->tsc_count = a;
	sens->cst_count = b;

	/*
	 * On old Atom CPUs (before Silvermont) the Package-cstate counters
	 * run at 1 MHz.
	 */
	if (sc->sc_is_atom && sc->sc_version == 0) {
		if (cstdiff < (1ULL << 44)) {
			cstdiff *= ATOM_MSR_PKG_CSTATE_COUNTER_HZ;
			cstdiff /= tsc_frequency;
		} else {
			cstdiff /= tsc_frequency;
			cstdiff *= ATOM_MSR_PKG_CSTATE_COUNTER_HZ;
		}
	}

	if (tscdiff > 0) {
		/* Make sure we don't calculate total garbage here */
		if (cstdiff < (1ULL << 44)) {
			sens->sensor.value = (cstdiff * 100000) / tscdiff;
			sens->sensor.flags &=
			    ~(SENSOR_FUNKNOWN | SENSOR_FINVALID);
		} else {
			sens->sensor.flags |= SENSOR_FINVALID;
		}
	}
}

static int
corecstat_try(u_int msr, char *name)
{
	uint64_t val;

	if (rdmsr_safe(msr, &val) != 0) {
		kprintf("msr %s (0x%08x) not available\n", name, msr);
		return 0;
	}
	return 1;
}
