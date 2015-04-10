/*
 * Copyright (c) 2007, 2008 Rui Paulo <rpaulo@FreeBSD.org>
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
 *
 * $FreeBSD: src/sys/dev/coretemp/coretemp.c,v 1.14 2011/05/05 19:15:15 delphij Exp $
 */

/*
 * Device driver for Intel's On Die thermal sensor via MSR.
 * First introduced in Intel's Core line of processors.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/cpu_topology.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/proc.h>	/* for curthread */
#include <sys/sched.h>
#include <sys/thread2.h>
#include <sys/bitops.h>

#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#define MSR_THERM_STATUS_TM_STATUS	__BIT64(0)
#define MSR_THERM_STATUS_TM_STATUS_LOG	__BIT64(1)
#define MSR_THERM_STATUS_PROCHOT	__BIT64(2)
#define MSR_THERM_STATUS_PROCHOT_LOG	__BIT64(3)
#define MSR_THERM_STATUS_CRIT		__BIT64(4)
#define MSR_THERM_STATUS_CRIT_LOG	__BIT64(5)
#define MSR_THERM_STATUS_THRESH1	__BIT64(6)
#define MSR_THERM_STATUS_THRESH1_LOG	__BIT64(7)
#define MSR_THERM_STATUS_THRESH2	__BIT64(8)
#define MSR_THERM_STATUS_THRESH2_LOG	__BIT64(9)
#define MSR_THERM_STATUS_PWRLIM		__BIT64(10)
#define MSR_THERM_STATUS_PWRLIM_LOG	__BIT64(11)
#define MSR_THERM_STATUS_READ		__BITS64(16, 22)
#define MSR_THERM_STATUS_RES		__BITS64(27, 30)
#define MSR_THERM_STATUS_READ_VALID	__BIT64(31)

#define MSR_THERM_STATUS_HAS_STATUS(msr) \
    (((msr) & (MSR_THERM_STATUS_TM_STATUS | MSR_THERM_STATUS_TM_STATUS_LOG)) ==\
     (MSR_THERM_STATUS_TM_STATUS | MSR_THERM_STATUS_TM_STATUS_LOG))

#define MSR_THERM_STATUS_IS_CRITICAL(msr) \
    (((msr) & (MSR_THERM_STATUS_CRIT | MSR_THERM_STATUS_CRIT_LOG)) == \
     (MSR_THERM_STATUS_CRIT | MSR_THERM_STATUS_CRIT_LOG))

#define MSR_PKGTM_STATUS_TM_STATUS	__BIT64(0)
#define MSR_PKGTM_STATUS_TM_STATUS_LOG	__BIT64(1)
#define MSR_PKGTM_STATUS_PROCHOT	__BIT64(2)
#define MSR_PKGTM_STATUS_PROCHOT_LOG	__BIT64(3)
#define MSR_PKGTM_STATUS_CRIT		__BIT64(4)
#define MSR_PKGTM_STATUS_CRIT_LOG	__BIT64(5)
#define MSR_PKGTM_STATUS_THRESH1	__BIT64(6)
#define MSR_PKGTM_STATUS_THRESH1_LOG	__BIT64(7)
#define MSR_PKGTM_STATUS_THRESH2	__BIT64(8)
#define MSR_PKGTM_STATUS_THRESH2_LOG	__BIT64(9)
#define MSR_PKGTM_STATUS_PWRLIM		__BIT64(10)
#define MSR_PKGTM_STATUS_PWRLIM_LOG	__BIT64(11)
#define MSR_PKGTM_STATUS_READ		__BITS64(16, 22)

#define MSR_PKGTM_STATUS_HAS_STATUS(msr) \
    (((msr) & (MSR_PKGTM_STATUS_TM_STATUS | MSR_PKGTM_STATUS_TM_STATUS_LOG)) ==\
     (MSR_PKGTM_STATUS_TM_STATUS | MSR_PKGTM_STATUS_TM_STATUS_LOG))

#define MSR_PKGTM_STATUS_IS_CRITICAL(msr) \
    (((msr) & (MSR_PKGTM_STATUS_CRIT | MSR_PKGTM_STATUS_CRIT_LOG)) == \
     (MSR_PKGTM_STATUS_CRIT | MSR_PKGTM_STATUS_CRIT_LOG))

#define CORETEMP_TEMP_INVALID	-1
#define CORETEMP_TEMP_NOUPDATE	-2

struct coretemp_sensor {
	struct ksensordev	c_sensdev;
	struct ksensor		c_sens;
};

struct coretemp_softc {
	device_t		sc_dev;
	int			sc_tjmax;

	int			sc_nsens;
	struct coretemp_sensor	*sc_sens;
	struct coretemp_sensor	*sc_pkg_sens;

	struct globaldata	*sc_gd;
	int			sc_cpu;
	volatile uint32_t	sc_flags;	/* CORETEMP_FLAG_ */
	volatile uint64_t	sc_msr;
	volatile uint64_t	sc_pkg_msr;
};

#define CORETEMP_FLAG_INITED	0x1
#define CORETEMP_FLAG_PENDING	0x2
#define CORETEMP_FLAG_CRIT	0x4
#define CORETEMP_FLAG_PKGCRIT	0x8

#define CORETEMP_HAS_PKGSENSOR(sc)	((sc)->sc_pkg_sens != NULL)

/*
 * Device methods.
 */
static void	coretemp_identify(driver_t *driver, device_t parent);
static int	coretemp_probe(device_t dev);
static int	coretemp_attach(device_t dev);
static int	coretemp_detach(device_t dev);

static void	coretemp_ipi_func(void *sc);
static void	coretemp_ipi_send(struct coretemp_softc *sc, uint32_t flags);
static boolean_t coretemp_msr_fetch(struct coretemp_softc *sc, uint64_t *msr,
		    uint64_t *pkg_msr);
static int	coretemp_msr_temp(struct coretemp_softc *sc, uint64_t msr);
static void	coretemp_sensor_update(struct coretemp_softc *sc, int temp);
static void	coretemp_sensor_task(void *arg);

static void	coretemp_pkg_sensor_task(void *arg);
static void	coretemp_pkg_sensor_update(struct coretemp_softc *sc, int temp);
static int	coretemp_pkg_msr_temp(struct coretemp_softc *sc, uint64_t msr);

static device_method_t coretemp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	coretemp_identify),
	DEVMETHOD(device_probe,		coretemp_probe),
	DEVMETHOD(device_attach,	coretemp_attach),
	DEVMETHOD(device_detach,	coretemp_detach),

	DEVMETHOD_END
};

static driver_t coretemp_driver = {
	"coretemp",
	coretemp_methods,
	sizeof(struct coretemp_softc),
};

static devclass_t coretemp_devclass;
DRIVER_MODULE(coretemp, cpu, coretemp_driver, coretemp_devclass, NULL, NULL);
MODULE_VERSION(coretemp, 1);

static __inline void
coretemp_sensor_set_invalid(struct ksensor *sens)
{
	sens->status = SENSOR_S_UNSPEC;
	sens->flags &= ~SENSOR_FUNKNOWN;
	sens->flags |= SENSOR_FINVALID;
	sens->value = 0;
}

static __inline void
coretemp_sensor_set_unknown(struct ksensor *sens)
{
	sens->status = SENSOR_S_UNSPEC;
	sens->flags &= ~SENSOR_FINVALID;
	sens->flags |= SENSOR_FUNKNOWN;
	sens->value = 0;
}

static __inline void
coretemp_sensor_set(struct ksensor *sens, const struct coretemp_softc *sc,
    uint32_t crit_flag, int temp)
{
	if (sc->sc_flags & crit_flag)
		sens->status = SENSOR_S_CRIT;
	else
		sens->status = SENSOR_S_OK;
	sens->flags &= ~(SENSOR_FINVALID | SENSOR_FUNKNOWN);
	sens->value = temp * 1000000 + 273150000;
}

static void
coretemp_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "coretemp", -1) != NULL)
		return;

	/* Check that the vendor is Intel. */
	if (cpu_vendor_id != CPU_VENDOR_INTEL)
		return;

	/*
	 * Some Intel CPUs, namely the PIII, don't have thermal sensors,
	 * but report them in cpu_thermal_feature.  This leads to a later
	 * GPF when the sensor is queried via a MSR, so we stop here.
	 */
	if (CPUID_TO_MODEL(cpu_id) < 0xe)
		return;

	if ((cpu_thermal_feature & CPUID_THERMAL_SENSOR) == 0)
		return;

	/*
	 * We add a child for each CPU since settings must be performed
	 * on each CPU in the SMP case.
	 */
	child = device_add_child(parent, "coretemp", -1);
	if (child == NULL)
		device_printf(parent, "add coretemp child failed\n");
}

static int
coretemp_probe(device_t dev)
{
	if (resource_disabled("coretemp", 0))
		return (ENXIO);

	device_set_desc(dev, "CPU On-Die Thermal Sensors");

	return (BUS_PROBE_GENERIC);
}

static int
coretemp_attach(device_t dev)
{
	struct coretemp_softc *sc = device_get_softc(dev);
	const struct cpu_node *node, *start_node;
	cpumask_t cpu_mask;
	device_t pdev;
	uint64_t msr;
	int cpu_model, cpu_stepping;
	int ret, tjtarget, cpu, sens_idx;
	int master_cpu;
	struct coretemp_sensor *csens;

	sc->sc_dev = dev;
	pdev = device_get_parent(dev);
	cpu_model = CPUID_TO_MODEL(cpu_id);
	cpu_stepping = cpu_id & CPUID_STEPPING;

#if 0
	/*
	 * XXXrpaulo: I have this CPU model and when it returns from C3
	 * coretemp continues to function properly.
	 */

	/*
	 * Check for errata AE18.
	 * "Processor Digital Thermal Sensor (DTS) Readout stops
	 *  updating upon returning from C3/C4 state."
	 *
	 * Adapted from the Linux coretemp driver.
	 */
	if (cpu_model == 0xe && cpu_stepping < 0xc) {
		msr = rdmsr(MSR_BIOS_SIGN);
		msr = msr >> 32;
		if (msr < 0x39) {
			device_printf(dev, "not supported (Intel errata "
			    "AE18), try updating your BIOS\n");
			return (ENXIO);
		}
	}
#endif

	/*
	 * Use 100C as the initial value.
	 */
	sc->sc_tjmax = 100;

	if ((cpu_model == 0xf && cpu_stepping >= 2) || cpu_model == 0xe) {
		/*
		 * On some Core 2 CPUs, there's an undocumented MSR that
		 * can tell us if Tj(max) is 100 or 85.
		 *
		 * The if-clause for CPUs having the MSR_IA32_EXT_CONFIG
		 * was adapted from the Linux coretemp driver.
		 */
		msr = rdmsr(MSR_IA32_EXT_CONFIG);
		if (msr & (1 << 30))
			sc->sc_tjmax = 85;
	} else if (cpu_model == 0x17) {
		switch (cpu_stepping) {
		case 0x6:	/* Mobile Core 2 Duo */
			sc->sc_tjmax = 105;
			break;
		default:	/* Unknown stepping */
			break;
		}
	} else if (cpu_model == 0x1c) {
		switch (cpu_stepping) {
		case 0xa:	/* 45nm Atom D400, N400 and D500 series */
			sc->sc_tjmax = 100;
			break;
		default:
			sc->sc_tjmax = 90;
			break;
		}
	} else {
		/*
		 * Attempt to get Tj(max) from MSR IA32_TEMPERATURE_TARGET.
		 *
		 * This method is described in Intel white paper "CPU
		 * Monitoring With DTS/PECI". (#322683)
		 */
		ret = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &msr);
		if (ret == 0) {
			tjtarget = (msr >> 16) & 0xff;

			/*
			 * On earlier generation of processors, the value
			 * obtained from IA32_TEMPERATURE_TARGET register is
			 * an offset that needs to be summed with a model
			 * specific base.  It is however not clear what
			 * these numbers are, with the publicly available
			 * documents from Intel.
			 *
			 * For now, we consider [70, 110]C range, as
			 * described in #322683, as "reasonable" and accept
			 * these values whenever the MSR is available for
			 * read, regardless the CPU model.
			 */
			if (tjtarget >= 70 && tjtarget <= 110)
				sc->sc_tjmax = tjtarget;
			else
				device_printf(dev, "Tj(target) value %d "
				    "does not seem right.\n", tjtarget);
		} else
			device_printf(dev, "Can not get Tj(target) "
			    "from your CPU, using 100C.\n");
	}

	if (bootverbose)
		device_printf(dev, "Setting TjMax=%d\n", sc->sc_tjmax);

	sc->sc_cpu = device_get_unit(device_get_parent(dev));
	sc->sc_gd = globaldata_find(sc->sc_cpu);

	start_node = get_cpu_node_by_cpuid(sc->sc_cpu);

	node = start_node;
	while (node != NULL) {
		if (node->type == CORE_LEVEL) {
			if (node->child_no == 0)
				node = NULL;
			break;
		}
		node = node->parent_node;
	}
	if (node != NULL) {
		master_cpu = BSRCPUMASK(node->members);
		if (bootverbose) {
			device_printf(dev, "master cpu%d, count %u\n",
			    master_cpu, node->child_no);
		}
		if (sc->sc_cpu != master_cpu)
			return (0);

		KKASSERT(node->child_no > 0);
		sc->sc_nsens = node->child_no;
		cpu_mask = node->members;
	} else {
		sc->sc_nsens = 1;
		CPUMASK_ASSBIT(cpu_mask, sc->sc_cpu);
	}
	sc->sc_sens = kmalloc(sizeof(struct coretemp_sensor) * sc->sc_nsens,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sens_idx = 0;
	CPUSET_FOREACH(cpu, cpu_mask) {
		KKASSERT(sens_idx < sc->sc_nsens);
		csens = &sc->sc_sens[sens_idx];

		/*
		 * Add hw.sensors.cpuN.temp0 MIB.
		 */
		ksnprintf(csens->c_sensdev.xname,
		    sizeof(csens->c_sensdev.xname), "cpu%d", cpu);
		ksnprintf(csens->c_sens.desc, sizeof(csens->c_sens.desc),
		    "node%d core%d", get_chip_ID(cpu),
		    get_core_number_within_chip(cpu));
		csens->c_sens.type = SENSOR_TEMP;
		coretemp_sensor_set_unknown(&csens->c_sens);
		sensor_attach(&csens->c_sensdev, &csens->c_sens);
		sensordev_install(&csens->c_sensdev);

		++sens_idx;
	}

	if (cpu_thermal_feature & CPUID_THERMAL_PTM) {
		boolean_t pkg_sens = TRUE;

		/*
		 * Package thermal sensor
		 */

		node = start_node;
		while (node != NULL) {
			if (node->type == CHIP_LEVEL) {
				if (node->child_no == 0)
					node = NULL;
				break;
			}
			node = node->parent_node;
		}
		if (node != NULL) {
			master_cpu = BSRCPUMASK(node->members);
			if (bootverbose) {
				device_printf(dev, "pkg master cpu%d\n",
				    master_cpu);
			}
			if (sc->sc_cpu != master_cpu)
				pkg_sens = FALSE;
		}

		if (pkg_sens) {
			csens = sc->sc_pkg_sens =
			    kmalloc(sizeof(struct coretemp_sensor), M_DEVBUF,
			    M_WAITOK | M_ZERO);

			/*
			 * Add hw.sensors.cpu_nodeN.temp0 MIB.
			 */
			ksnprintf(csens->c_sensdev.xname,
			    sizeof(csens->c_sensdev.xname), "cpu_node%d",
			    get_chip_ID(sc->sc_cpu));
			ksnprintf(csens->c_sens.desc,
			    sizeof(csens->c_sens.desc), "node%d",
			    get_chip_ID(sc->sc_cpu));
			csens->c_sens.type = SENSOR_TEMP;
			coretemp_sensor_set_unknown(&csens->c_sens);
			sensor_attach(&csens->c_sensdev, &csens->c_sens);
			sensordev_install(&csens->c_sensdev);
		}
	}

	if (CORETEMP_HAS_PKGSENSOR(sc))
		sensor_task_register(sc, coretemp_pkg_sensor_task, 2);
	else
		sensor_task_register(sc, coretemp_sensor_task, 2);

	return (0);
}

static int
coretemp_detach(device_t dev)
{
	struct coretemp_softc *sc = device_get_softc(dev);

	if (sc->sc_nsens > 0) {
		int i;

		sensor_task_unregister(sc);
		lwkt_synchronize_ipiqs("coretemp");

		for (i = 0; i < sc->sc_nsens; ++i)
			sensordev_deinstall(&sc->sc_sens[i].c_sensdev);
		kfree(sc->sc_sens, M_DEVBUF);

		if (sc->sc_pkg_sens != NULL) {
			sensordev_deinstall(&sc->sc_pkg_sens->c_sensdev);
			kfree(sc->sc_pkg_sens, M_DEVBUF);
		}
	}
	return (0);
}

static void
coretemp_ipi_func(void *xsc)
{
	struct coretemp_softc *sc = xsc; 

	sc->sc_msr = rdmsr(MSR_THERM_STATUS);
	if (CORETEMP_HAS_PKGSENSOR(sc))
		sc->sc_pkg_msr = rdmsr(MSR_PKG_THERM_STATUS);
	cpu_sfence();
	atomic_clear_int(&sc->sc_flags, CORETEMP_FLAG_PENDING);
}

static void
coretemp_ipi_send(struct coretemp_softc *sc, uint32_t flags)
{
	KASSERT((sc->sc_flags & CORETEMP_FLAG_PENDING) == 0,
	    ("coretemp: cpu%d ipi is still pending", sc->sc_cpu));
	atomic_set_int(&sc->sc_flags, flags | CORETEMP_FLAG_PENDING);
	cpu_mfence();
	lwkt_send_ipiq_passive(sc->sc_gd, coretemp_ipi_func, sc);
}

static int
coretemp_msr_temp(struct coretemp_softc *sc, uint64_t msr)
{
	int temp;

	/*
	 * Check for Thermal Status and Thermal Status Log.
	 */
	if (MSR_THERM_STATUS_HAS_STATUS(msr))
		device_printf(sc->sc_dev, "PROCHOT asserted\n");

	if (msr & MSR_THERM_STATUS_READ_VALID)
		temp = sc->sc_tjmax - __SHIFTOUT(msr, MSR_THERM_STATUS_READ);
	else
		temp = CORETEMP_TEMP_INVALID;

	/*
	 * Check for Critical Temperature Status and Critical
	 * Temperature Log.
	 * It doesn't really matter if the current temperature is
	 * invalid because the "Critical Temperature Log" bit will
	 * tell us if the Critical Temperature has been reached in
	 * past. It's not directly related to the current temperature.
	 *
	 * If we reach a critical level, allow devctl(4) to catch this
	 * and shutdown the system.
	 */
	if (MSR_THERM_STATUS_IS_CRITICAL(msr)) {
		if ((sc->sc_flags & CORETEMP_FLAG_CRIT) == 0) {
			char stemp[16];

			device_printf(sc->sc_dev,
			    "critical temperature detected, "
			    "suggest system shutdown\n");
			ksnprintf(stemp, sizeof(stemp), "%d", temp);
			devctl_notify("coretemp", "Thermal", stemp,
			    "notify=0xcc"); /* TODO: add node and core */
			atomic_set_int(&sc->sc_flags, CORETEMP_FLAG_CRIT);
		}
	} else if (sc->sc_flags & CORETEMP_FLAG_CRIT) {
		atomic_clear_int(&sc->sc_flags, CORETEMP_FLAG_CRIT);
	}

	return temp;
}

static int
coretemp_pkg_msr_temp(struct coretemp_softc *sc, uint64_t msr)
{
	int temp;

	/*
	 * Check for Thermal Status and Thermal Status Log.
	 */
	if (MSR_PKGTM_STATUS_HAS_STATUS(msr))
		device_printf(sc->sc_dev, "package PROCHOT asserted\n");

	temp = sc->sc_tjmax - __SHIFTOUT(msr, MSR_PKGTM_STATUS_READ);

	/*
	 * Check for Critical Temperature Status and Critical
	 * Temperature Log.
	 * It doesn't really matter if the current temperature is
	 * invalid because the "Critical Temperature Log" bit will
	 * tell us if the Critical Temperature has been reached in
	 * past. It's not directly related to the current temperature.
	 *
	 * If we reach a critical level, allow devctl(4) to catch this
	 * and shutdown the system.
	 */
	if (MSR_PKGTM_STATUS_IS_CRITICAL(msr)) {
		if ((sc->sc_flags & CORETEMP_FLAG_PKGCRIT) == 0) {
			char stemp[16];

			device_printf(sc->sc_dev,
			    "critical temperature detected, "
			    "suggest system shutdown\n");
			ksnprintf(stemp, sizeof(stemp), "%d", temp);
			devctl_notify("coretemp", "Thermal", stemp,
			    "notify=0xcc"); /* TODO: add node */
			atomic_set_int(&sc->sc_flags, CORETEMP_FLAG_PKGCRIT);
		}
	} else if (sc->sc_flags & CORETEMP_FLAG_PKGCRIT) {
		atomic_clear_int(&sc->sc_flags, CORETEMP_FLAG_PKGCRIT);
	}

	return temp;
}

static boolean_t
coretemp_msr_fetch(struct coretemp_softc *sc, uint64_t *msr, uint64_t *pkg_msr)
{
	/*
	 * Send IPI to the specific CPU to read the correct temperature.
	 * If the IPI does not complete yet, i.e. CORETEMP_FLAG_PENDING,
	 * return false.
	 */
	if (sc->sc_cpu != mycpuid) {
		if ((sc->sc_flags & CORETEMP_FLAG_INITED) == 0) {
			coretemp_ipi_send(sc, CORETEMP_FLAG_INITED);
			return FALSE;
		} else {
			if (sc->sc_flags & CORETEMP_FLAG_PENDING) {
				/* IPI does not complete yet */
				return FALSE;
			}
			*msr = sc->sc_msr;
			if (pkg_msr != NULL)
				*pkg_msr = sc->sc_pkg_msr;
			coretemp_ipi_send(sc, 0);
		}
	} else {
		*msr = rdmsr(MSR_THERM_STATUS);
		if (pkg_msr != NULL)
			*pkg_msr = rdmsr(MSR_PKG_THERM_STATUS);
	}
	return TRUE;
}

static void
coretemp_sensor_update(struct coretemp_softc *sc, int temp)
{
	int i;

	if (temp == CORETEMP_TEMP_NOUPDATE) {
		/* No updates; keep the previous value */
	} else if (temp == CORETEMP_TEMP_INVALID) {
		for (i = 0; i < sc->sc_nsens; ++i)
			coretemp_sensor_set_invalid(&sc->sc_sens[i].c_sens);
	} else {
		for (i = 0; i < sc->sc_nsens; ++i) {
			coretemp_sensor_set(&sc->sc_sens[i].c_sens, sc,
			    CORETEMP_FLAG_CRIT, temp);
		}
	}
}

static void
coretemp_pkg_sensor_update(struct coretemp_softc *sc, int temp)
{
	KKASSERT(sc->sc_pkg_sens != NULL);
	if (temp == CORETEMP_TEMP_NOUPDATE) {
		/* No updates; keep the previous value */
	} else if (temp == CORETEMP_TEMP_INVALID) {
		coretemp_sensor_set_invalid(&sc->sc_pkg_sens->c_sens);
	} else {
		coretemp_sensor_set(&sc->sc_pkg_sens->c_sens, sc,
		    CORETEMP_FLAG_PKGCRIT, temp);
	}
}

static void
coretemp_sensor_task(void *arg)
{
	struct coretemp_softc *sc = arg;
	uint64_t msr;
	int temp;

	if (!coretemp_msr_fetch(sc, &msr, NULL))
		temp = CORETEMP_TEMP_NOUPDATE;
	else
		temp = coretemp_msr_temp(sc, msr);

	coretemp_sensor_update(sc, temp);
}

static void
coretemp_pkg_sensor_task(void *arg)
{
	struct coretemp_softc *sc = arg;
	uint64_t msr, pkg_msr;
	int temp, pkg_temp;

	if (!coretemp_msr_fetch(sc, &msr, &pkg_msr)) {
		temp = CORETEMP_TEMP_NOUPDATE;
		pkg_temp = CORETEMP_TEMP_NOUPDATE;
	} else {
		temp = coretemp_msr_temp(sc, msr);
		pkg_temp = coretemp_pkg_msr_temp(sc, pkg_msr);
	}

	coretemp_sensor_update(sc, temp);
	coretemp_pkg_sensor_update(sc, pkg_temp);
}
