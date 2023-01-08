/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, 2009 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2009 Norikatsu Shigemura <nork@FreeBSD.org>
 * Copyright (c) 2009-2012 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 * Copyright (c) 2017-2020 Conrad Meyer <cem@FreeBSD.org>. All rights reserved.
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
 * $FreeBSD: head/sys/dev/amdtemp/amdtemp.c 366136 2020-09-25 04:16:28Z cem $
 */

/*
 * Driver for the AMD CPU on-die thermal sensors.
 * Initially based on the k8temp Linux driver.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pci_cfgreg.h>

#include <dev/powermng/amdsmn/amdsmn.h>

typedef enum {
	CORE0_SENSOR0,
	CORE0_SENSOR1,
	CORE1_SENSOR0,
	CORE1_SENSOR1,
	CORE0,
	CORE1,
	CCD1,
	CCD_BASE = CCD1,
	CCD2,
	CCD3,
	CCD4,
	CCD5,
	CCD6,
	CCD7,
	CCD8,
	CCD9,
	CCD10,
	CCD11,
	CCD12,
	MAXSENSORS,

	CCD_MAX = CCD12,
	NUM_CCDS = CCD_MAX - CCD_BASE + 1,
} amdsensor_t;

struct amdtemp_softc {
	int		sc_ncores;
	int		sc_ntemps;
	int		sc_flags;
	int		sc_ccd_display;
#define	AMDTEMP_FLAG_CS_SWAP	0x01	/* ThermSenseCoreSel is inverted. */
#define	AMDTEMP_FLAG_CT_10BIT	0x02	/* CurTmp is 10-bit wide. */
#define	AMDTEMP_FLAG_ALT_OFFSET	0x04	/* CurTmp starts at -28C. */
	int32_t		sc_offset;
	int32_t		sc_ccd_offset;
	int32_t		(*sc_gettemp)(device_t, amdsensor_t);
	struct sysctl_oid *sc_sysctl_cpu[MAXCPU];
	struct intr_config_hook sc_ich;
	device_t	sc_smn;
	uint32_t	sc_probed_regmask;

	/*
	 * NOTE: We put common sensors like the CCDs on cpu0.  Remaining
	 *	 cores are only applicable if ntemps == 2 (with no CCDs).
	 *	 When ntemps == 1 the temp sensors are CCD-based and shared.
	 */
	struct sensorcpu {
		device_t dev;
		struct amdtemp_softc *sc;
		struct ksensordev    sensordev;
		struct ksensor	     *sensors;
		struct sensor_task   *senstask;
		uint32_t regmask;
	} *sc_sensorcpus;
};

/*
 * N.B. The numbers in macro names below are significant and represent CPU
 * family and model numbers.  Do not make up fictitious family or model numbers
 * when adding support for new devices.
 */
#define	VENDORID_AMD			0x1022

#define	DEVICEID_AMD_MISC0F		0x1103
#define	DEVICEID_AMD_MISC10		0x1203
#define	DEVICEID_AMD_MISC11		0x1303
#define	DEVICEID_AMD_MISC14		0x1703
#define	DEVICEID_AMD_MISC15		0x1603
#define	DEVICEID_AMD_MISC15_M10H	0x1403
#define	DEVICEID_AMD_MISC15_M30H	0x141d
#define	DEVICEID_AMD_MISC15_M60H_ROOT	0x1576
#define	DEVICEID_AMD_MISC16		0x1533
#define	DEVICEID_AMD_MISC16_M30H	0x1583
#define	DEVICEID_AMD_HOSTB17H_ROOT	0x1450
#define	DEVICEID_AMD_HOSTB17H_M10H_ROOT	0x15d0
#define	DEVICEID_AMD_HOSTB17H_M30H_ROOT	0x1480	/* Also M70h. */
#define	DEVICEID_AMD_HOSTB17H_M60H_ROOT	0x1630
#define DEVICEID_AMD_HOSTB17H_M70H_ROOT 0x1443
#define DEVICEID_AMD_HOSTB17H_MA0H_ROOT 0x1727

#if 0
#define DEVICEID_AMD_HOSTB19H_M10H_ROOT 0x14b0
#define DEVICEID_AMD_HOSTB19H_M40H_ROOT 0x167c
#define DEVICEID_AMD_HOSTB19H_M50H_ROOT 0x166d
#define DEVICEID_AMD_HOSTB19H_M60H_ROOT 0x14e3
#define DEVICEID_AMD_HOSTB19H_M70H_ROOT 0x14f3
#endif
#define DEVICEID_AMD_HOSTB19H_M10H_ROOT 0x14a4
#define DEVICEID_AMD_HOSTB19H_M60H_ROOT 0x14d8
#define DEVICEID_AMD_HOSTB19H_M70H_ROOT 0x14e8


static const struct amdtemp_product {
	uint16_t	amdtemp_vendorid;
	uint16_t	amdtemp_deviceid;
	/*
	 * 0xFC register is only valid on the D18F3 PCI device; SMN temp
	 * drivers do not attach to that device.
	 */
	bool		amdtemp_has_cpuid;
} amdtemp_products[] = {
	{ VENDORID_AMD,	DEVICEID_AMD_MISC0F, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC10, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC11, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC14, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M10H, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M30H, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M60H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC16, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC16_M30H, true },

	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_M10H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_M30H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_M60H_ROOT, false },
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB17H_M70H_ROOT, false },
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB17H_MA0H_ROOT, false },

	{ VENDORID_AMD, DEVICEID_AMD_HOSTB19H_M10H_ROOT, false },
#if 0
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB19H_M40H_ROOT, false },
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB19H_M50H_ROOT, false },
#endif
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB19H_M60H_ROOT, false },
	{ VENDORID_AMD, DEVICEID_AMD_HOSTB19H_M70H_ROOT, false },
};

/*
 * Reported Temperature Control Register, family 0Fh-15h (some models), 16h.
 */
#define	AMDTEMP_REPTMP_CTRL	0xa4

#define	AMDTEMP_REPTMP10H_CURTMP_MASK	0x7ff
#define	AMDTEMP_REPTMP10H_CURTMP_SHIFT	21
#define	AMDTEMP_REPTMP10H_TJSEL_MASK	0x3
#define	AMDTEMP_REPTMP10H_TJSEL_SHIFT	16

/*
 * Reported Temperature, Family 15h, M60+
 *
 * Same register bit definitions as other Family 15h CPUs, but access is
 * indirect via SMN, like Family 17h.
 */
#define	AMDTEMP_15H_M60H_REPTMP_CTRL	0xd8200ca4

/*
 * Reported Temperature, Family 17h
 *
 * According to AMD OSRR for 17H, section 4.2.1, bits 31-21 of this register
 * provide the current temp.  bit 19, when clear, means the temp is reported in
 * a range 0.."225C" (probable typo for 255C), and when set changes the range
 * to -49..206C.
 *
 * Family 17H and 19H
 */
#define	AMDTEMP_17H_CUR_TMP		0x59800
#define	AMDTEMP_17H_CUR_TMP_RANGE_SEL	(1u << 19)
/*
 *
 */
#define	AMDTEMP_17H_CCD_TMP_VALID	(1u << 11)

/*
 * AMD temperature range adjustment, in deciKelvins (i.e., 49.0 Celsius).
 */
#define	AMDTEMP_CURTMP_RANGE_ADJUST	490

/*
 * Thermaltrip Status Register (Family 0Fh only)
 */
#define	AMDTEMP_THERMTP_STAT	0xe4
#define	AMDTEMP_TTSR_SELCORE	0x04
#define	AMDTEMP_TTSR_SELSENSOR	0x40

/*
 * DRAM Configuration High Register
 */
#define	AMDTEMP_DRAM_CONF_HIGH	0x94	/* Function 2 */
#define	AMDTEMP_DRAM_MODE_DDR3	0x0100

/*
 * CPU Family/Model Register
 */
#define	AMDTEMP_CPUID		0xfc

/*
 * Device methods.
 */
static void 	amdtemp_identify(driver_t *driver, device_t parent);
static int	amdtemp_probe(device_t dev);
static int	amdtemp_attach(device_t dev);
static void	amdtemp_intrhook(void *arg);
static int	amdtemp_detach(device_t dev);
static int32_t	amdtemp_gettemp0f(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp15hm60h(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp17to19h(device_t dev, amdsensor_t sensor);
static void	amdtemp_probe_ccd_sensors17h(device_t dev, uint32_t model);
static void	amdtemp_probe_ccd_sensors19h(device_t dev, uint32_t model);
static int	amdtemp_sysctl(SYSCTL_HANDLER_ARGS);
static void	amdtemp_sensor_task(void *);

static device_method_t amdtemp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	amdtemp_identify),
	DEVMETHOD(device_probe,		amdtemp_probe),
	DEVMETHOD(device_attach,	amdtemp_attach),
	DEVMETHOD(device_detach,	amdtemp_detach),

	DEVMETHOD_END
};

static driver_t amdtemp_driver = {
	"amdtemp",
	amdtemp_methods,
	sizeof(struct amdtemp_softc),
};

static devclass_t amdtemp_devclass;
DRIVER_MODULE_ORDERED(amdtemp, hostb, amdtemp_driver,
		      &amdtemp_devclass, NULL, NULL, SI_ORDER_LATER);
MODULE_VERSION(amdtemp, 1);
MODULE_DEPEND(amdtemp, amdsmn, 1, 1, 1);
#if !defined(__DragonFly__)
MODULE_PNP_INFO("U16:vendor;U16:device", pci, amdtemp, amdtemp_products,
    nitems(amdtemp_products));
#endif

static bool
amdtemp_match(device_t dev, const struct amdtemp_product **product_out)
{
	int i;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);

	for (i = 0; i < nitems(amdtemp_products); i++) {
		if (vendor == amdtemp_products[i].amdtemp_vendorid &&
		    devid == amdtemp_products[i].amdtemp_deviceid) {
			if (product_out != NULL)
				*product_out = &amdtemp_products[i];
			return (true);
		}
	}
	return (false);
}

static void
amdtemp_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "amdtemp", -1) != NULL)
		return;

	if (amdtemp_match(parent, NULL)) {
		child = device_add_child(parent, "amdtemp", -1);
		if (child == NULL)
			device_printf(parent, "add amdtemp child failed\n");
	}
}

static int
amdtemp_probe(device_t dev)
{
	uint32_t family, model;

	if (resource_disabled("amdtemp", 0))
		return (ENXIO);
	if (!amdtemp_match(device_get_parent(dev), NULL))
		return (ENXIO);

	family = CPUID_TO_FAMILY(cpu_id);
	model = CPUID_TO_MODEL(cpu_id);

	switch (family) {
	case 0x0f:
		if ((model == 0x04 && (cpu_id & CPUID_STEPPING) == 0) ||
		    (model == 0x05 && (cpu_id & CPUID_STEPPING) <= 1))
			return (ENXIO);
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x19:
		break;
	default:
		return (ENXIO);
	}
	device_set_desc(dev, "AMD CPU On-Die Thermal Sensors");

	return (BUS_PROBE_GENERIC);
}

static int
amdtemp_attach(device_t dev)
{
	char tn[32];
	u_int regs[4];
	const struct amdtemp_product *product;
	struct amdtemp_softc *sc;
	struct sysctl_ctx_list *sysctlctx;
	struct sysctl_oid *sysctlnode;
	uint32_t cpuid, family, model;
	u_int bid;
	int erratum319, unit;
	bool needsmn;

	sc = device_get_softc(dev);
	erratum319 = 0;
	needsmn = false;

	if (!amdtemp_match(device_get_parent(dev), &product))
		return (ENXIO);

	cpuid = cpu_id;
	family = CPUID_TO_FAMILY(cpuid);
	model = CPUID_TO_MODEL(cpuid);

	/*
	 * This checks for the byzantine condition of running a heterogenous
	 * revision multi-socket system where the attach thread is potentially
	 * probing a remote socket's PCI device.
	 *
	 * Currently, such scenarios are unsupported on models using the SMN
	 * (because on those models, amdtemp(4) attaches to a different PCI
	 * device than the one that contains AMDTEMP_CPUID).
	 *
	 * The ancient 0x0F family of devices only supports this register from
	 * models 40h+.
	 */
	if (product->amdtemp_has_cpuid && (family > 0x0f ||
	    (family == 0x0f && model >= 0x40))) {
		cpuid = pci_read_config(device_get_parent(dev), AMDTEMP_CPUID,
		    4);
		family = CPUID_TO_FAMILY(cpuid);
		model = CPUID_TO_MODEL(cpuid);
	}

	switch (family) {
	case 0x0f:
		/*
		 * Thermaltrip Status Register
		 *
		 * - ThermSenseCoreSel
		 *
		 * Revision F & G:	0 - Core1, 1 - Core0
		 * Other:		0 - Core0, 1 - Core1
		 *
		 * - CurTmp
		 *
		 * Revision G:		bits 23-14
		 * Other:		bits 23-16
		 *
		 * XXX According to the BKDG, CurTmp, ThermSenseSel and
		 * ThermSenseCoreSel bits were introduced in Revision F
		 * but CurTmp seems working fine as early as Revision C.
		 * However, it is not clear whether ThermSenseSel and/or
		 * ThermSenseCoreSel work in undocumented cases as well.
		 * In fact, the Linux driver suggests it may not work but
		 * we just assume it does until we find otherwise.
		 *
		 * XXX According to Linux, CurTmp starts at -28C on
		 * Socket AM2 Revision G processors, which is not
		 * documented anywhere.
		 */
		if (model >= 0x40)
			sc->sc_flags |= AMDTEMP_FLAG_CS_SWAP;
		if (model >= 0x60 && model != 0xc1) {
			do_cpuid(0x80000001, regs);
			bid = (regs[1] >> 9) & 0x1f;
			switch (model) {
			case 0x68: /* Socket S1g1 */
			case 0x6c:
			case 0x7c:
				break;
			case 0x6b: /* Socket AM2 and ASB1 (2 cores) */
				if (bid != 0x0b && bid != 0x0c)
					sc->sc_flags |=
					    AMDTEMP_FLAG_ALT_OFFSET;
				break;
			case 0x6f: /* Socket AM2 and ASB1 (1 core) */
			case 0x7f:
				if (bid != 0x07 && bid != 0x09 &&
				    bid != 0x0c)
					sc->sc_flags |=
					    AMDTEMP_FLAG_ALT_OFFSET;
				break;
			default:
				sc->sc_flags |= AMDTEMP_FLAG_ALT_OFFSET;
			}
			sc->sc_flags |= AMDTEMP_FLAG_CT_10BIT;
		}

		/*
		 * There are two sensors per core.
		 */
		sc->sc_ntemps = 2;
		sc->sc_ccd_display = 0;

		sc->sc_gettemp = amdtemp_gettemp0f;
		break;
	case 0x10:
		/*
		 * Erratum 319 Inaccurate Temperature Measurement
		 *
		 * http://support.amd.com/us/Processor_TechDocs/41322.pdf
		 */
		do_cpuid(0x80000001, regs);
		switch ((regs[1] >> 28) & 0xf) {
		case 0:	/* Socket F */
			erratum319 = 1;
			break;
		case 1:	/* Socket AM2+ or AM3 */
			if ((pci_cfgregread(pci_get_bus(dev),
			    pci_get_slot(dev), 2, AMDTEMP_DRAM_CONF_HIGH, 2) &
			    AMDTEMP_DRAM_MODE_DDR3) != 0 || model > 0x04 ||
			    (model == 0x04 && (cpuid & CPUID_STEPPING) >= 3))
				break;
			/* XXX 00100F42h (RB-C2) exists in both formats. */
			erratum319 = 1;
			break;
		}
		/* FALLTHROUGH */
	case 0x11:
	case 0x12:
	case 0x14:
	case 0x15:
	case 0x16:
		sc->sc_ntemps = 1;
		sc->sc_ccd_display = 1;
		/*
		 * Some later (60h+) models of family 15h use a similar SMN
		 * network as family 17h.  (However, the register index differs
		 * from 17h and the decoding matches other 10h-15h models,
		 * which differ from 17h.)
		 */
		if (family == 0x15 && model >= 0x60) {
			sc->sc_gettemp = amdtemp_gettemp15hm60h;
			needsmn = true;
		} else
			sc->sc_gettemp = amdtemp_gettemp;
		break;
	case 0x17:
	case 0x19:
		sc->sc_ntemps = 1;
		sc->sc_ccd_display = 1;
		sc->sc_gettemp = amdtemp_gettemp17to19h;
		switch(model) {
		case 0x10 ... 0x1f:
		case 0xa0 ... 0xaf:
		case 0x40 ... 0x4f:
			sc->sc_ccd_offset = 0x300;
			break;
		case 0x60 ... 0x6f:
		case 0x70 ... 0x7f:
			sc->sc_ccd_offset = 0x308;
			break;
		default:
			sc->sc_ccd_offset = 0x154;
			break;
		}
		needsmn = true;
		device_printf(dev, "sc_ccd_offset = %08x\n", sc->sc_ccd_offset);
		break;
	default:
		device_printf(dev, "Bogus family 0x%x\n", family);
		return (ENXIO);
	}

	if (needsmn) {
		sc->sc_smn = device_find_child(
		    device_get_parent(dev), "amdsmn", -1);
		if (sc->sc_smn == NULL) {
			device_printf(dev, "No SMN device found\n");
			return (ENXIO);
		}
	}

	/*
	 * Find number of cores per package.  XXX this does not work
	 * properly, it appears to be calculating the total number of cores.
	 */

	sc->sc_ncores = (amd_feature2 & AMDID2_CMP) != 0 ?
	    (cpu_procinfo2 & AMDID_CMP_CORES) + 1 : 1;
	if (sc->sc_ncores > MAXCPU)
		return (ENXIO);

	if (erratum319)
		device_printf(dev,
		    "Erratum 319: temperature measurement may be inaccurate\n");
	if (bootverbose)
		device_printf(dev, "Found %d cores and %d sensors.\n",
		    sc->sc_ncores,
		    sc->sc_ntemps > 1 ? sc->sc_ntemps * sc->sc_ncores : 1);

	/*
	 * dev.amdtemp.N tree.
	 */
	unit = device_get_unit(dev);
	ksnprintf(tn, sizeof(tn), "dev.amdtemp.%d.sensor_offset", unit);
	TUNABLE_INT_FETCH(tn, &sc->sc_offset);

	sysctlctx = device_get_sysctl_ctx(dev);
	SYSCTL_ADD_INT(sysctlctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "sensor_offset", CTLFLAG_RW, &sc->sc_offset, 0,
	    "Temperature sensor offset");
	sysctlnode = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "core0", CTLFLAG_RD, 0, "Core 0");

	SYSCTL_ADD_PROC(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode),
	    OID_AUTO, "sensor0",
	    CTLTYPE_INT | CTLFLAG_RD,
	    dev, CORE0_SENSOR0, amdtemp_sysctl, "IK",
	    "Core 0 / Sensor 0 temperature");

	sc->sc_probed_regmask |= 1U << CORE0_SENSOR0;

	if (family == 0x17)
		amdtemp_probe_ccd_sensors17h(dev, model);
	else if (family == 0x19)
		amdtemp_probe_ccd_sensors19h(dev, model);
	else if (sc->sc_ntemps > 1) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sysctlnode),
		    OID_AUTO, "sensor1",
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, CORE0_SENSOR1, amdtemp_sysctl, "IK",
		    "Core 0 / Sensor 1 temperature");

		sc->sc_probed_regmask |= 1U << CORE0_SENSOR1;

		if (sc->sc_ncores > 1) {
			sysctlnode = SYSCTL_ADD_NODE(sysctlctx,
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    OID_AUTO, "core1", CTLFLAG_RD,
			    0, "Core 1");

			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sysctlnode),
			    OID_AUTO, "sensor0",
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, CORE1_SENSOR0, amdtemp_sysctl, "IK",
			    "Core 1 / Sensor 0 temperature");

			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sysctlnode),
			    OID_AUTO, "sensor1",
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, CORE1_SENSOR1, amdtemp_sysctl, "IK",
			    "Core 1 / Sensor 1 temperature");

			sc->sc_probed_regmask |= 1U << CORE1_SENSOR0;
			sc->sc_probed_regmask |= 1U << CORE1_SENSOR1;
		}
	}

	/*
	 * Try to create dev.cpu sysctl entries and setup intrhook function.
	 * This is needed because the cpu driver may be loaded late on boot,
	 * after us.
	 */
	amdtemp_intrhook(dev);
	sc->sc_ich.ich_func = amdtemp_intrhook;
	sc->sc_ich.ich_arg = dev;
	if (config_intrhook_establish(&sc->sc_ich) != 0) {
		device_printf(dev, "config_intrhook_establish failed!\n");
		return (ENXIO);
	}

	return (0);
}

void
amdtemp_intrhook(void *arg)
{
	struct amdtemp_softc *sc;
	struct sysctl_ctx_list *sysctlctx;
	device_t dev = (device_t)arg;
	device_t acpi, cpu, nexus;
	amdsensor_t sensor;
	int i;
	int j;

	sc = device_get_softc(dev);
	if (sc->sc_ich.ich_arg == NULL)
		return;

	/*
	 * dev.cpu.N.temperature.
	 */
	nexus = device_find_child(root_bus, "nexus", 0);
	acpi = device_find_child(nexus, "acpi", 0);

	for (i = 0; i < sc->sc_ncores; i++) {
		if (sc->sc_sysctl_cpu[i] != NULL)
			continue;
		cpu = device_find_child(acpi, "cpu",
		    device_get_unit(dev) * sc->sc_ncores + i);
		if (cpu != NULL) {
			sysctlctx = device_get_sysctl_ctx(cpu);

			sensor = sc->sc_ntemps > 1 ?
			    (i == 0 ? CORE0 : CORE1) : CORE0_SENSOR0;
			sc->sc_sysctl_cpu[i] = SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(device_get_sysctl_tree(cpu)),
			    OID_AUTO, "temperature",
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, sensor, amdtemp_sysctl, "IK",
			    "Current temparature");
		}
	}
	config_intrhook_disestablish(&sc->sc_ich);

	/*
	 * sensor infrastructure.  Use [ncpus] for globally shared sensors
	 */
	sc->sc_sensorcpus = kmalloc(sizeof(*sc->sc_sensorcpus) *
				   (sc->sc_ncores + 1),
				   M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i <= sc->sc_ncores; i++) {
		struct sensorcpu *scpu = &sc->sc_sensorcpus[i];

		if (i == 0)
			scpu->regmask = sc->sc_probed_regmask & 0x0003U;
		else if (i == 1)
			scpu->regmask = sc->sc_probed_regmask & 0x000CU;
		else if (i != sc->sc_ncores)
			scpu->regmask = 0;
		else
			scpu->regmask = sc->sc_probed_regmask & ~0xFU;

		if (scpu->regmask == 0)
			continue;

		if (sc->sc_ccd_display) {
			ksnprintf(scpu->sensordev.xname,
				  sizeof(scpu->sensordev.xname),
				  "die%d", device_get_unit(dev));
		} else {
			ksnprintf(scpu->sensordev.xname,
				  sizeof(scpu->sensordev.xname),
				  "cpu%d", i);
		}

		scpu->dev = dev;
		scpu->sc = sc;
		scpu->sensors = kmalloc(sizeof(*scpu->sensors) * MAXSENSORS,
					M_DEVBUF, M_WAITOK | M_ZERO);
		for (j = 0; j < MAXSENSORS; ++j) {
			if ((scpu->regmask & (1U << j)) == 0)
				continue;

			switch(j) {
			case CORE0_SENSOR0:
			case CORE0_SENSOR1:
			case CORE1_SENSOR0:
			case CORE1_SENSOR1:
				if (sc->sc_ccd_display) {
					ksnprintf(scpu->sensors[j].desc,
						  sizeof(scpu->sensors[0].desc),
						  "high temp");
				} else {
					ksnprintf(scpu->sensors[j].desc,
						  sizeof(scpu->sensors[0].desc),
						  "temp%d", j & 1);
				}
				break;
			case CORE0:
				ksnprintf(scpu->sensors[j].desc,
					  sizeof(scpu->sensors[0].desc),
					  "core0 rollup temp");
				break;
			case CORE1:
				ksnprintf(scpu->sensors[j].desc,
					  sizeof(scpu->sensors[0].desc),
					  "core1 rollup temp");
				break;
			case CCD_BASE ... CCD_MAX:
				ksnprintf(scpu->sensors[j].desc,
					  sizeof(scpu->sensors[0].desc),
					  "ccd%u temp", j - CCD_BASE);
				break;
			}
			scpu->sensors[j].type = SENSOR_TEMP;
			sensor_set_unknown(&scpu->sensors[j]);
			sensor_attach(&scpu->sensordev, &scpu->sensors[j]);
		}
		scpu->senstask = sensor_task_register2(scpu,
						       amdtemp_sensor_task,
						       2,
						       ((i < sc->sc_ncores) ?
							i : -1));
		sensordev_install(&scpu->sensordev);
	}
}

int
amdtemp_detach(device_t dev)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	int i;
	int j;

	for (i = 0; i < sc->sc_ncores; i++) {
		if (sc->sc_sysctl_cpu[i] != NULL)
			sysctl_remove_oid(sc->sc_sysctl_cpu[i], 1, 0);
	}

	if (sc->sc_sensorcpus) {
		for (i = 0; i <= sc->sc_ncores; i++) {
			struct sensorcpu *scpu = &sc->sc_sensorcpus[i];

			if (scpu->sensors) {
				for (j = 0; j < MAXSENSORS; ++j) {
					if ((scpu->regmask & (1U << j)) == 0)
						continue;
					sensor_detach(&scpu->sensordev,
						      &scpu->sensors[j]);
				}
				if (scpu->senstask) {
					sensor_task_unregister2(scpu->senstask);
					scpu->senstask = NULL;
				}
				sensordev_deinstall(&scpu->sensordev);
				kfree(scpu->sensors, M_DEVBUF);
				scpu->sensors = NULL;
			}
		}
		kfree(sc->sc_sensorcpus, M_DEVBUF);
		sc->sc_sensorcpus = NULL;
	}

	/* NewBus removes the dev.amdtemp.N tree by itself. */

	return (0);
}

static int
amdtemp_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct amdtemp_softc *sc = device_get_softc(dev);
	amdsensor_t sensor = (amdsensor_t)arg2;
	int32_t auxtemp[2], temp;
	int error;

	switch (sensor) {
	case CORE0:
		auxtemp[0] = sc->sc_gettemp(dev, CORE0_SENSOR0);
		auxtemp[1] = sc->sc_gettemp(dev, CORE0_SENSOR1);
		temp = imax(auxtemp[0], auxtemp[1]);
		break;
	case CORE1:
		auxtemp[0] = sc->sc_gettemp(dev, CORE1_SENSOR0);
		auxtemp[1] = sc->sc_gettemp(dev, CORE1_SENSOR1);
		temp = imax(auxtemp[0], auxtemp[1]);
		break;
	default:
		temp = sc->sc_gettemp(dev, sensor);
		break;
	}
	error = sysctl_handle_int(oidp, &temp, 0, req);

	return (error);
}

#define	AMDTEMP_ZERO_C_TO_K	2731

static int32_t
amdtemp_gettemp0f(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t mask, offset, temp;

	/* Set Sensor/Core selector. */
	temp = pci_read_config(dev, AMDTEMP_THERMTP_STAT, 1);
	temp &= ~(AMDTEMP_TTSR_SELCORE | AMDTEMP_TTSR_SELSENSOR);
	switch (sensor) {
	case CORE0_SENSOR1:
		temp |= AMDTEMP_TTSR_SELSENSOR;
		/* FALLTHROUGH */
	case CORE0_SENSOR0:
	case CORE0:
		if ((sc->sc_flags & AMDTEMP_FLAG_CS_SWAP) != 0)
			temp |= AMDTEMP_TTSR_SELCORE;
		break;
	case CORE1_SENSOR1:
		temp |= AMDTEMP_TTSR_SELSENSOR;
		/* FALLTHROUGH */
	case CORE1_SENSOR0:
	case CORE1:
		if ((sc->sc_flags & AMDTEMP_FLAG_CS_SWAP) == 0)
			temp |= AMDTEMP_TTSR_SELCORE;
		break;
	default:
		__assert_unreachable();
	}
	pci_write_config(dev, AMDTEMP_THERMTP_STAT, temp, 1);

	mask = (sc->sc_flags & AMDTEMP_FLAG_CT_10BIT) != 0 ? 0x3ff : 0x3fc;
	offset = (sc->sc_flags & AMDTEMP_FLAG_ALT_OFFSET) != 0 ? 28 : 49;
	temp = pci_read_config(dev, AMDTEMP_THERMTP_STAT, 4);
	temp = ((temp >> 14) & mask) * 5 / 2;
	temp += AMDTEMP_ZERO_C_TO_K + (sc->sc_offset - offset) * 10;

	return (temp);
}

static uint32_t
amdtemp_decode_fam10h_to_17h(int32_t sc_offset, uint32_t val, bool minus49)
{
	uint32_t temp;

	/* Convert raw register subfield units (0.125C) to units of 0.1C. */
	temp = (val & AMDTEMP_REPTMP10H_CURTMP_MASK) * 5 / 4;

	if (minus49)
		temp -= AMDTEMP_CURTMP_RANGE_ADJUST;

	temp += AMDTEMP_ZERO_C_TO_K + sc_offset * 10;
	return (temp);
}

static uint32_t
amdtemp_decode_fam10h_to_16h(int32_t sc_offset, uint32_t val)
{
	bool minus49;

	/*
	 * On Family 15h and higher, if CurTmpTjSel is 11b, the range is
	 * adjusted down by 49.0 degrees Celsius.  (This adjustment is not
	 * documented in BKDGs prior to family 15h model 00h.)
	 */
	minus49 = (CPUID_TO_FAMILY(cpu_id) >= 0x15 &&
	    ((val >> AMDTEMP_REPTMP10H_TJSEL_SHIFT) &
	    AMDTEMP_REPTMP10H_TJSEL_MASK) == 0x3);

	return (amdtemp_decode_fam10h_to_17h(sc_offset,
	    val >> AMDTEMP_REPTMP10H_CURTMP_SHIFT, minus49));
}

static uint32_t
amdtemp_decode_fam17h_tctl(int32_t sc_offset, uint32_t val)
{
	bool minus49;

	minus49 = ((val & AMDTEMP_17H_CUR_TMP_RANGE_SEL) != 0);
	return (amdtemp_decode_fam10h_to_17h(sc_offset,
	    val >> AMDTEMP_REPTMP10H_CURTMP_SHIFT, minus49));
}

static int32_t
amdtemp_gettemp(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t temp;

	temp = pci_read_config(dev, AMDTEMP_REPTMP_CTRL, 4);
	return (amdtemp_decode_fam10h_to_16h(sc->sc_offset, temp));
}

static int32_t
amdtemp_gettemp15hm60h(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t val;
	int error;

	error = amdsmn_read(sc->sc_smn, AMDTEMP_15H_M60H_REPTMP_CTRL, &val);
	KASSERT(error == 0, ("amdsmn_read"));
	return (amdtemp_decode_fam10h_to_16h(sc->sc_offset, val));
}

static int32_t
amdtemp_gettemp17to19h(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t val;
	int error;

	switch (sensor) {
	case CORE0_SENSOR0:
		/* Tctl */
		error = amdsmn_read(sc->sc_smn, AMDTEMP_17H_CUR_TMP, &val);
		KASSERT(error == 0, ("amdsmn_read"));
		return (amdtemp_decode_fam17h_tctl(sc->sc_offset, val));
	case CCD_BASE ... CCD_MAX:
		/* Tccd<N> */
		error = amdsmn_read(sc->sc_smn,
				    AMDTEMP_17H_CUR_TMP +
				    sc->sc_ccd_offset +
				    (((int)sensor - CCD_BASE) * sizeof(val)),
				    &val);
		KASSERT(error == 0, ("amdsmn_read2"));
		KASSERT((val & AMDTEMP_17H_CCD_TMP_VALID) != 0,
		    ("sensor %d: not valid", (int)sensor));
		return (amdtemp_decode_fam10h_to_17h(sc->sc_offset, val, true));
	default:
		__assert_unreachable();
	}
}

static void
amdtemp_probe_ccd_sensors17h(device_t dev, uint32_t model)
{
	char sensor_name[16], sensor_descr[32];
	struct amdtemp_softc *sc;
	uint32_t maxreg, i, val;
	int error;

	switch (model) {
	case 0x00 ... 0x1f: /* Zen1, Zen+ */
		maxreg = 4;
		break;
	case 0x30 ... 0x3f: /* Zen2 TR/Epyc */
	case 0x60:	    /* Renoir */
	case 0x68:	    /* Lucienne */
	case 0x70 ... 0x7f: /* Zen2 Ryzen */
		maxreg = 8;
		_Static_assert((int)NUM_CCDS >= 8, "");
		break;
	case 0xa0 ... 0xaf: /* Zen3 ? */
		maxreg = 8;
		_Static_assert((int)NUM_CCDS >= 8, "");
		break;
	default:
		device_printf(dev,
		    "Unrecognized Family 17h Model: %02xh\n", model);
		return;
	}

	sc = device_get_softc(dev);
	for (i = 0; i < maxreg; i++) {
		error = amdsmn_read(sc->sc_smn,
				    AMDTEMP_17H_CUR_TMP +
				    sc->sc_ccd_offset +
				    (i * sizeof(val)),
				    &val);
		if (error != 0)
			continue;
		if ((val & AMDTEMP_17H_CCD_TMP_VALID) == 0)
			continue;

		ksnprintf(sensor_name, sizeof(sensor_name), "ccd%u", i);
		ksnprintf(sensor_descr, sizeof(sensor_descr),
		    "CCD %u temperature (Tccd%u)", i, i);

		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    sensor_name, CTLTYPE_INT | CTLFLAG_RD,
		    dev, CCD_BASE + i, amdtemp_sysctl, "IK", sensor_descr);

		sc->sc_probed_regmask |= 1U << (CCD_BASE + i);
	}
}

static void
amdtemp_probe_ccd_sensors19h(device_t dev, uint32_t model)
{
	char sensor_name[16], sensor_descr[32];
	struct amdtemp_softc *sc;
	uint32_t maxreg, i, val;
	int error;

	device_printf(dev, "probe ccd sensors 19h %02x\n", model);

        switch (model) {
        case 0x00 ... 0x0f: /* Zen3 EPYC "Milan" */
        case 0x20 ... 0x2f: /* Zen3 Ryzen "Vermeer" */
        case 0x40 ... 0x4f:
        case 0x50 ... 0x5f:
        case 0x60 ... 0x6f:
        case 0x70 ... 0x7f:
                maxreg = 8;
                _Static_assert((int)NUM_CCDS >= 8, "");
                break;
        case 0x10 ... 0x1f:
        case 0xa0 ... 0xaf:
                maxreg = 12;
                _Static_assert((int)NUM_CCDS >= 12, "");
		break;
        default:
                device_printf(dev,
                    "Unrecognized Family 19h Model: %02xh\n", model);
                return;
        }

	sc = device_get_softc(dev);
	for (i = 0; i < maxreg; i++) {
		error = amdsmn_read(sc->sc_smn,
				    AMDTEMP_17H_CUR_TMP +
				    sc->sc_ccd_offset +
				    (i * sizeof(val)),
				    &val);
		device_printf(dev, "probe ccd%d error %d val=%08x\n",
			      i, error, val);
		if (error != 0)
			continue;
		if ((val & AMDTEMP_17H_CCD_TMP_VALID) == 0)
			continue;

		ksnprintf(sensor_name, sizeof(sensor_name), "ccd%u", i);
		ksnprintf(sensor_descr, sizeof(sensor_descr),
		    "CCD %u temperature (Tccd%u)", i, i);

		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    sensor_name, CTLTYPE_INT | CTLFLAG_RD,
		    dev, CCD_BASE + i, amdtemp_sysctl, "IK", sensor_descr);

		sc->sc_probed_regmask |= 1U << (CCD_BASE + i);
	}
}

static void
amdtemp_sensor_task(void *sc_arg)
{
	struct sensorcpu *scpu = sc_arg;
	struct amdtemp_softc *sc;
	uint32_t mask;
	int32_t temp;
	int j;

	sc = scpu->sc;
	if (sc->sc_ich.ich_arg == NULL)
		return;
	mask = scpu->regmask;

	for (j = 0; mask; ++j) {
		if ((mask & (1U << j)) == 0)
			continue;
		temp = sc->sc_gettemp(scpu->dev, j);
		sensor_set(&scpu->sensors[j], temp * 100000L, 0);
		mask &= ~(1U << j);
	}
}
