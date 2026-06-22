/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * $FreeBSD: head/sys/dev/asmc/asmc.c$
 *
 * Ported from FreeBSD's asmc(4) driver.
 *
 * DragonFlyBSD port: adapted for kmalloc/kfree, lockmgr 2-arg form,
 * taskqueue_start_threads ncpu arg, sys/bus_resource.h, acpica paths.
 * Added MMIO (T2) backend support.
 */

/*
 * Driver for Apple's System Management Controller (SMC).
 * Supports ISA I/O port (Intel Macs) and MMIO (T2 Macs).
 *
 * Inspired by the Linux applesmc driver.
 *
 * This file: probe/attach/detach, ACPI device methods, init,
 * capability/sensor detection, module glue (DRIVER_MODULE, MODULE_DEPEND).
 */

#include "smc.h"

#include <machine/specialreg.h>
#include <machine/cpufunc.h>

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("APPLE_SMC")

static int	apple_smc_probe(device_t);
static int	apple_smc_attach(device_t);
static int	apple_smc_detach(device_t);
static int	apple_smc_resume(device_t);

static int	apple_smc_init(device_t);
static void	apple_smc_detect_capabilities(device_t);
static int	apple_smc_detect_sensors(device_t);

static const struct {
	const char	*key;
	const char	*desc;
} apple_smc_temp_descs[] = {
	{ "TA0P", "Ambient" },		{ "TA0S", "PCIe Slot 1 Ambient" },
	{ "TA0p", "Ambient Air" },	{ "TA0V", "Ambient" },
	{ "TA1P", "Ambient 2" },	{ "TA1S", "PCIe Slot 1 PCB" },
	{ "TA1p", "Ambient Air 2" },	{ "TA2P", "Ambient 3" },
	{ "TA2S", "PCIe Slot 2 Ambient" }, { "TA3S", "PCIe Slot 2 PCB" },
	{ "TALP", "Ambient Light Proximity" },
	{ "TaLC", "Airflow Left" },	{ "TaRC", "Airflow Right" },
	{ "Ta0P", "Airflow Proximity" },
	{ "TB0T", "Enclosure Bottom" },	{ "TB1T", "Battery 1" },
	{ "TB2T", "Battery 2" },	{ "TB3T", "Battery 3" },
	{ "TBXT", "Battery" },		{ "Tb0P", "BLC Proximity" },
	{ "TC0C", "CPU Core 1" },	{ "TC0D", "CPU Die" },
	{ "TC0H", "CPU Heatsink" },	{ "TC0h", "CPU Heatsink" },
	{ "TC0P", "CPU Proximity" },	{ "TC0c", "CPU Core 1 PECI" },
	{ "TC0d", "CPU Die PECI" },	{ "TC0p", "CPU Proximity" },
	{ "TC1C", "CPU Core 2" },	{ "TC1c", "CPU Core 2 PECI" },
	{ "TC2C", "CPU Core 3" },	{ "TC2c", "CPU Core 3 PECI" },
	{ "TC3C", "CPU Core 4" },	{ "TC3c", "CPU Core 4 PECI" },
	{ "TC4C", "CPU Core 5" },	{ "TC5C", "CPU Core 6" },
	{ "TC6C", "CPU Core 7" },	{ "TC7C", "CPU Core 8" },
	{ "TC8C", "CPU Core 9" },
	{ "TCGC", "PECI GPU" },		{ "TCGc", "PECI GPU" },
	{ "TCSA", "PECI SA" },		{ "TCSc", "PECI SA" },
	{ "TCXC", "PECI CPU" },		{ "TCXc", "PECI CPU" },
	{ "TCPG", "CPU Package GPU" },
	{ "TCAG", "CPU A Package" },	{ "TCAH", "CPU A Heatsink" },
	{ "TCBG", "CPU B Package" },	{ "TCBH", "CPU B Heatsink" },
	{ "TG0D", "GPU Diode" },	{ "TG0H", "GPU Heatsink" },
	{ "TG0P", "GPU Proximity" },	{ "TG0d", "GPU Die" },
	{ "TG0h", "GPU Heatsink" },	{ "TG0p", "GPU Proximity" },
	{ "TH0O", "HDD" },		{ "TH0P", "HDD Proximity" },
	{ "TH1P", "HDD Bay 2" },	{ "TH2P", "HDD Bay 3" },
	{ "TH3P", "HDD Bay 4" },
	{ "TI0P", "Thunderbolt 1" },	{ "TI1P", "Thunderbolt 2" },
	{ "TL0P", "LCD Proximity" },	{ "TL0p", "LCD Proximity" },
	{ "TM0P", "Memory Proximity" },	{ "TM0S", "Memory Slot 1" },
	{ "TM1S", "Memory Slot 2" },
	{ "TN0D", "Northbridge Diode" }, { "TN0H", "MCH Heatsink" },
	{ "TN0P", "Northbridge Proximity" },
	{ "TP0P", "PCH Proximity" },	{ "TPCD", "PCH Die" },
	{ "TO0P", "Optical Drive" },
	{ "Tp0C", "Power Supply" },	{ "Tp0P", "Power Supply Proximity" },
	{ "TW0P", "Wireless Proximity" },
	{ "TTF0", "Fan" },
};

static const char *
apple_smc_temp_desc(const char *key)
{
	unsigned int i;

	for (i = 0; i < nitems(apple_smc_temp_descs); i++) {
		if (strcmp(apple_smc_temp_descs[i].key, key) == 0)
			return (apple_smc_temp_descs[i].desc);
	}
	return ("Temperature");
}

static device_method_t apple_smc_methods[] = {
	DEVMETHOD(device_probe,		apple_smc_probe),
	DEVMETHOD(device_attach,	apple_smc_attach),
	DEVMETHOD(device_detach,	apple_smc_detach),
	DEVMETHOD(device_resume,	apple_smc_resume),
	DEVMETHOD_END
};

static driver_t apple_smc_driver = {
	"apple_smc",
	apple_smc_methods,
	sizeof(struct apple_smc_softc),
	.gpri = KOBJ_GPRI_ACPI
};

static devclass_t apple_smc_devclass;

static char *apple_smc_ids[] = { "APP0001", NULL };

DRIVER_MODULE(apple_smc, acpi, apple_smc_driver, apple_smc_devclass,
    NULL, NULL);
MODULE_DEPEND(apple_smc, acpi, 1, 1, 1);
MODULE_VERSION(apple_smc, 1);

static int
apple_smc_probe(device_t dev)
{
	char *product;
	char buf[64];

	if (acpi_disabled("apple_smc"))
		return (ENXIO);
	if (ACPI_ID_PROBE(device_get_parent(dev), dev, apple_smc_ids) == NULL)
		return (ENXIO);

	product = kgetenv("smbios.system.product");
	ksnprintf(buf, sizeof(buf), "Apple %s",
	    product ? product : "SMC");
	device_set_desc_copy(dev, buf);
	if (product)
		kfreeenv(product);

	return (0);
}

static int
apple_smc_attach(device_t dev)
{
	int i, j;
	int ret;
	char name[2];
	struct apple_smc_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *sysctlctx;
	struct sysctl_oid *sysctlnode;

	/* Lock must be initialized before any backend probe */
	lockinit(&sc->sc_lock, "apple_smc", 0, LK_CANRECURSE);

	/* Try MMIO first */
	sc->sc_rid_mem = 0;
	sc->sc_iomem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_rid_mem, RF_ACTIVE);
	if (sc->sc_iomem != NULL) {
		if (apple_smc_try_enable_mmio(dev) == 0) {
			sc->sc_is_mmio = 1;
			device_printf(dev, "using MMIO backend%s\n",
			    sc->sc_is_t2 ? " (T2)" : "");
		} else {
			bus_release_resource(dev, SYS_RES_MEMORY,
			    sc->sc_rid_mem, sc->sc_iomem);
			sc->sc_iomem = NULL;
		}
	}

	if (!sc->sc_is_mmio) {
		sc->sc_ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		    &sc->sc_rid_port, RF_ACTIVE);
		if (sc->sc_ioport == NULL) {
			device_printf(dev, "unable to allocate IO port\n");
			ret = ENOMEM;
			goto err;
		}
	}

	sysctlctx  = device_get_sysctl_ctx(dev);
	sysctlnode = device_get_sysctl_tree(dev);

	ret = apple_smc_init(dev);
	if (ret != 0) {
		device_printf(dev, "SMC not responding\n");
		goto err;
	}

	/* Read Tj,max from MSR 0x1A2 bits[23:16] (Intel Sandy Bridge+). */
	sc->sc_tj_max = 100;	/* safe default */
	{
		uint64_t msr;
		if (rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &msr) == 0) {
			int tj = (int)((msr >> 16) & 0xff);
			if (tj > 0)
				sc->sc_tj_max = tj;
		}
	}

	apple_smc_detect_capabilities(dev);
	apple_smc_detect_sensors(dev);

	/* fan tree */
	sc->sc_fan_tree[0] = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "fan",
	    CTLFLAG_RD, 0, "Fan Root Tree");

	{
		/*
		 * Per-fan sysctl table.  arg2_key encodes the key index
		 * for apple_smc_mb_sysctl_fanrw in bits [15:8]; the fan
		 * number is OR'd into bits [7:0] at registration time.
		 * Entries with arg2_key == -1 pass fan index directly.
		 */
		static const struct {
			const char	*name;
			int		type;
			int		arg2_key;
			int		(*handler)(SYSCTL_HANDLER_ARGS);
			const char	*desc;
			int		need_safespeed;
		} fan_sysctls[] = {
			{ "id", CTLTYPE_STRING | CTLFLAG_RD, -1,
			    apple_smc_mb_sysctl_fanid,
			    "Fan ID", 0 },
			{ "speed", CTLTYPE_INT | CTLFLAG_RD, -1,
			    apple_smc_mb_sysctl_fanspeed,
			    "Fan speed in RPM", 0 },
			{ "safespeed", CTLTYPE_INT | CTLFLAG_RD, -1,
			    apple_smc_mb_sysctl_fansafespeed,
			    "Fan safe speed in RPM", 1 },
			{ "minspeed", CTLTYPE_INT | CTLFLAG_RW, 0 << 8,
			    apple_smc_mb_sysctl_fanrw,
			    "Fan minimum speed in RPM", 0 },
			{ "maxspeed", CTLTYPE_INT | CTLFLAG_RW, 1 << 8,
			    apple_smc_mb_sysctl_fanrw,
			    "Fan maximum speed in RPM", 0 },
			{ "targetspeed", CTLTYPE_INT | CTLFLAG_RW, 2 << 8,
			    apple_smc_mb_sysctl_fanrw,
			    "Fan target speed in RPM", 0 },
			{ "manual", CTLTYPE_INT | CTLFLAG_RW, -1,
			    apple_smc_mb_sysctl_fanmanual,
			    "Fan manual mode (0=auto, 1=manual)", 0 },
		};
		unsigned int fi;

		for (i = 1; i <= sc->sc_nfan; i++) {
			j = i - 1;
			name[0] = '0' + j;
			name[1] = 0;
			sc->sc_fan_tree[i] = SYSCTL_ADD_NODE(sysctlctx,
			    SYSCTL_CHILDREN(sc->sc_fan_tree[0]), OID_AUTO,
			    name, CTLFLAG_RD, 0, "Fan Subtree");

			for (fi = 0; fi < nitems(fan_sysctls); fi++) {
				if (fan_sysctls[fi].need_safespeed &&
				    !sc->sc_has_safespeed)
					continue;
				SYSCTL_ADD_PROC(sysctlctx,
				    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
				    OID_AUTO, fan_sysctls[fi].name,
				    fan_sysctls[fi].type, dev,
				    fan_sysctls[fi].arg2_key == -1 ?
				        j : (fan_sysctls[fi].arg2_key | j),
				    fan_sysctls[fi].handler, "I",
				    fan_sysctls[fi].desc);
			}
		}
	}

	/* temp tree */
	sc->sc_temp_tree = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "temp",
	    CTLFLAG_RD, 0, "Temperature sensors");

	for (i = 0; i < sc->sc_temp_count; i++) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_temp_tree),
		    OID_AUTO, sc->sc_temp_sensors[i],
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, i, apple_smc_temp_sysctl, "I",
		    apple_smc_temp_desc(sc->sc_temp_sensors[i]));
	}

	/* DTS (distance-to-Tj,max) sensors, decoded as absolute temps */
	if (sc->sc_dts_count > 0) {
		struct sysctl_oid *dts_tree;

		dts_tree = SYSCTL_ADD_NODE(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_temp_tree), OID_AUTO, "dts",
		    CTLFLAG_RD, 0, "CPU DTS temperature sensors");
		for (i = 0; i < sc->sc_dts_count; i++) {
			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(dts_tree),
			    OID_AUTO, sc->sc_dts_sensors[i],
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, i, apple_smc_dts_sysctl, "I",
			    apple_smc_temp_desc(sc->sc_dts_sensors[i]));
		}
	}

	/* light tree */
	if (sc->sc_has_light) {
		sc->sc_light_tree = SYSCTL_ADD_NODE(sysctlctx,
		    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "light",
		    CTLFLAG_RD, 0, "Keyboard backlight sensors");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_light_tree),
		    OID_AUTO, "left", CTLTYPE_INT | CTLFLAG_RD, dev, 0,
		    sc->sc_light_len == ASMC_LIGHT_LONGLEN ?
		        apple_smc_mbp_sysctl_light_left_10byte :
		        apple_smc_mbp_sysctl_light,
		    "I", "Keyboard backlight left sensor");

		if (sc->sc_light_len != ASMC_LIGHT_LONGLEN &&
		    apple_smc_key_getinfo(dev, ASMC_KEY_LIGHTRIGHT,
		    NULL, NULL) == 0) {
			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sc->sc_light_tree),
			    OID_AUTO, "right", CTLTYPE_INT | CTLFLAG_RD,
			    dev, 1, apple_smc_mbp_sysctl_light,
			    "I", "Keyboard backlight right sensor");
		}

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_light_tree),
		    OID_AUTO, "control",
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY,
		    dev, 0, apple_smc_mbp_sysctl_light_control,
		    "I", "Keyboard backlight brightness control");
	}

#ifdef APPLE_SMC_DEBUG
	sc->sc_raw_tree = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "raw", CTLFLAG_RD, 0, "Raw SMC key access");

	SYSCTL_ADD_PROC(sysctlctx, SYSCTL_CHILDREN(sc->sc_raw_tree),
	    OID_AUTO, "key", CTLTYPE_STRING | CTLFLAG_RW,
	    dev, 0, apple_smc_raw_key_sysctl, "A", "SMC key name (4 chars)");

	SYSCTL_ADD_PROC(sysctlctx, SYSCTL_CHILDREN(sc->sc_raw_tree),
	    OID_AUTO, "value", CTLTYPE_STRING | CTLFLAG_RW,
	    dev, 0, apple_smc_raw_value_sysctl, "A", "SMC key value (hex)");

	SYSCTL_ADD_PROC(sysctlctx, SYSCTL_CHILDREN(sc->sc_raw_tree),
	    OID_AUTO, "len", CTLTYPE_INT | CTLFLAG_RD,
	    dev, 0, apple_smc_raw_len_sysctl, "I", "SMC key value length");

	SYSCTL_ADD_PROC(sysctlctx, SYSCTL_CHILDREN(sc->sc_raw_tree),
	    OID_AUTO, "type", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, 0, apple_smc_raw_type_sysctl, "A", "SMC key type (4 chars)");
#endif

	/* battery charge limit (T2) */
	if (sc->sc_is_t2 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_BCLM, NULL, NULL) == 0) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sysctlnode), OID_AUTO,
		    "battery_charge_limit", CTLTYPE_INT | CTLFLAG_RW,
		    dev, 0, apple_smc_bclm_sysctl, "I",
		    "Battery charge limit (0-100)");
	}

	/* system state subtree */
	{
		static const struct {
			const char	*smc_key;
			const char	*name;
			int		type;
			int		arg2;
			int		(*handler)(SYSCTL_HANDLER_ARGS);
			const char	*fmt;
			const char	*desc;
		} sys_sysctls[] = {
			{ ASMC_KEY_MSSD, "shutdown_cause",
			    CTLTYPE_STRING | CTLFLAG_RD, 0,
			    apple_smc_cause_sysctl, "A",
			    "Last shutdown cause (MSSD)" },
			{ ASMC_KEY_MSSP, "sleep_cause",
			    CTLTYPE_STRING | CTLFLAG_RD, 1,
			    apple_smc_cause_sysctl, "A",
			    "Last sleep cause (MSSP)" },
			{ ASMC_KEY_MSAL, "thermal_status",
			    CTLTYPE_STRING | CTLFLAG_RD, 0,
			    apple_smc_msal_sysctl, "A",
			    "Thermal subsystem status (MSAL)" },
			{ ASMC_KEY_CLKT, "time_of_day",
			    CTLTYPE_UINT | CTLFLAG_RD, 0,
			    apple_smc_clkt_sysctl, "IU",
			    "Seconds since midnight (CLKT)" },
			{ ASMC_KEY_MSPS, "power_state",
			    CTLTYPE_UINT | CTLFLAG_RD, 0,
			    apple_smc_msps_sysctl, "IU",
			    "SMC power state index (MSPS)" },
			{ ASMC_KEY_RPLT, "board_id",
			    CTLTYPE_STRING | CTLFLAG_RD, 0,
			    apple_smc_rplt_sysctl, "A",
			    "Apple board codename (RPlt)" },
			{ ASMC_KEY_RGEN, "chip_gen",
			    CTLTYPE_UINT | CTLFLAG_RD, 0,
			    apple_smc_rgen_sysctl, "IU",
			    "Apple chip generation (RGEN; 3=T2)" },
		};
		struct sysctl_oid *sys_tree;
		unsigned int si;

		sys_tree = SYSCTL_ADD_NODE(sysctlctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    "system", CTLFLAG_RD, 0, "System state and board identity");

		for (si = 0; si < nitems(sys_sysctls); si++) {
			if (apple_smc_key_getinfo(dev,
			    sys_sysctls[si].smc_key, NULL, NULL) != 0)
				continue;
			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sys_tree), OID_AUTO,
			    sys_sysctls[si].name, sys_sysctls[si].type,
			    dev, sys_sysctls[si].arg2,
			    sys_sysctls[si].handler, sys_sysctls[si].fmt,
			    sys_sysctls[si].desc);
		}
	}

	if (!sc->sc_has_sms)
		goto nosms;

	apple_smc_sms_init(dev);

	sc->sc_sms_tree = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "sms",
	    CTLFLAG_RD, 0, "Sudden Motion Sensor");

	{
		static const char *sms_names[] = { "x", "y", "z" };
		static const char *sms_descs[] = {
			"Sudden Motion Sensor X value",
			"Sudden Motion Sensor Y value",
			"Sudden Motion Sensor Z value",
		};
		unsigned int si;

		for (si = 0; si < nitems(sms_names); si++) {
			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sc->sc_sms_tree),
			    OID_AUTO, sms_names[si],
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, si, apple_smc_mb_sysctl_sms, "I",
			    sms_descs[si]);
		}
	}

	sc->sc_sms_tq = NULL;
	TASK_INIT(&sc->sc_sms_task, 0, apple_smc_sms_task, sc);
	sc->sc_sms_tq = taskqueue_create("apple_smc_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->sc_sms_tq);
	taskqueue_start_threads(&sc->sc_sms_tq, 1, TDPRI_INT_HIGH, -1,
	    "%s sms taskq", device_get_nameunit(dev));

	sc->sc_rid_irq = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_rid_irq, RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		ret = ENXIO;
		goto err;
	}

	ret = bus_setup_intr(dev, sc->sc_irq, INTR_MPSAFE,
	    apple_smc_sms_intr, dev, &sc->sc_cookie, NULL);
	if (ret) {
		device_printf(dev, "unable to setup SMS IRQ\n");
		goto err;
	}

nosms:
	return (0);

err:
	apple_smc_detach(dev);
	return (ret);
}

static void
apple_smc_free_sensors(char **sensors, int count)
{
	int i;

	for (i = 0; i < count; i++)
		kfree(sensors[i], M_DEVBUF);
}

static int
apple_smc_detach(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);

	apple_smc_free_sensors(sc->sc_temp_sensors,    sc->sc_temp_count);
	apple_smc_free_sensors(sc->sc_dts_sensors,     sc->sc_dts_count);
	apple_smc_free_sensors(sc->sc_voltage_sensors, sc->sc_voltage_count);
	apple_smc_free_sensors(sc->sc_current_sensors, sc->sc_current_count);
	apple_smc_free_sensors(sc->sc_power_sensors,   sc->sc_power_count);
	apple_smc_free_sensors(sc->sc_light_sensors,   sc->sc_light_count);

	/*
	 * Detach order matters: tear down the IRQ handler first so no new
	 * tasks can be enqueued, then drain any in-flight task, then free
	 * the taskqueue.  Reversing drain and teardown would allow the ISR
	 * to re-enqueue while we are draining.
	 */
	if (sc->sc_cookie) {
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_cookie);
		sc->sc_cookie = NULL;
	}
	if (sc->sc_irq) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_rid_irq,
		    sc->sc_irq);
		sc->sc_irq = NULL;
	}
	if (sc->sc_sms_tq) {
		taskqueue_drain(sc->sc_sms_tq, &sc->sc_sms_task);
		taskqueue_free(sc->sc_sms_tq);
		sc->sc_sms_tq = NULL;
	}
	if (sc->sc_ioport) {
		bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_rid_port,
		    sc->sc_ioport);
		sc->sc_ioport = NULL;
	}
	if (sc->sc_iomem) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid_mem,
		    sc->sc_iomem);
		sc->sc_iomem = NULL;
	}
	lockuninit(&sc->sc_lock);

	return (0);
}

static int
apple_smc_resume(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t buf[2];
	unsigned int lc;
	int error;

	/* Restore keyboard backlight level from before sleep. */
	lc = apple_smc_get_light_control();
	buf[0] = (uint8_t)lc;
	buf[1] = 0x00;
	error = apple_smc_key_write(dev, ASMC_KEY_LIGHTVALUE, buf, sizeof(buf));
	if (error != 0)
		device_printf(dev, "resume: failed to restore backlight: %d\n",
		    error);

	/* Re-initialize the Sudden Motion Sensor after resume. */
	if (sc->sc_has_sms)
		apple_smc_sms_init(dev);

	return (0);
}

static int
apple_smc_init(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *sysctlctx;
	uint8_t buf[6];
	int error;

	sysctlctx = device_get_sysctl_ctx(dev);

	error = apple_smc_key_read(dev, ASMC_KEY_REV, buf, 6);
	if (error != 0) {
		if (sc->sc_is_t2) {
			error = apple_smc_key_read(dev, ASMC_NKEYS, buf, 4);
			if (error != 0)
				goto out;
			device_printf(dev, "T2 SMC: %d keys\n", be32dec(buf));
		} else {
			goto out;
		}
	} else {
		device_printf(dev, "SMC revision: %x.%x%x\n",
		    buf[0], buf[1], buf[2]);
	}

	if (apple_smc_key_read(dev, ASMC_KEY_AUPO, buf, 1) == 0) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "auto_poweron",
		    CTLTYPE_INT | CTLFLAG_RW, dev, 0,
		    apple_smc_aupo_sysctl, "I",
		    "Auto power-on after AC power loss");
	}

	sc->sc_nfan = apple_smc_fan_count(dev);
	if (sc->sc_nfan > ASMC_MAXFANS) {
		device_printf(dev, "clamping fan count to %d\n", ASMC_MAXFANS);
		sc->sc_nfan = ASMC_MAXFANS;
	}

	if (apple_smc_key_read(dev, ASMC_NKEYS, buf, 4) == 0)
		sc->sc_nkeys = be32dec(buf);

out:
	return (error);
}

static void
apple_smc_detect_capabilities(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	uint8_t len;
	char type[ASMC_TYPELEN + 1];

	/*
	 * SMS detection issues 10 separate key_getinfo calls.  Each call
	 * acquires the lock and performs a full SMC round-trip, so this
	 * runs about 10x slower than a single lookup.  Acceptable at attach
	 * time; not on a hot path.
	 */
	sc->sc_has_sms =
	    (apple_smc_key_getinfo(dev, ASMC_KEY_SMS, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_X, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_Y, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_Z, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_LOW, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_HIGH, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_LOW_INT, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_HIGH_INT, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_SMS_FLAG, &len, type) == 0 &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_INTOK, &len, type) == 0);

	if (apple_smc_key_getinfo(dev, ASMC_KEY_LIGHTLEFT, &len, type) == 0 &&
	    (len == ASMC_LIGHT_SHORTLEN || len == ASMC_LIGHT_LONGLEN) &&
	    apple_smc_key_getinfo(dev, ASMC_KEY_LIGHTVALUE, NULL, NULL) == 0) {
		sc->sc_has_light = 1;
		sc->sc_light_len = len;
	}

	sc->sc_has_safespeed =
	    (apple_smc_key_getinfo(dev, ASMC_KEY_FANSAFESPEED0,
	    &len, type) == 0);

	sc->sc_has_alsl =
	    (apple_smc_key_getinfo(dev, ASMC_KEY_LIGHTSRC,
	    &len, type) == 0);
}

static void
apple_smc_register_sensor_tree(struct sysctl_ctx_list *ctx, device_t dev,
    const char *name, const char *desc, int sensor_char,
    char **sensors, int count)
{
	struct sysctl_oid *tree;
	int i;

	if (count <= 0)
		return;
	tree = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    name, CTLFLAG_RD, 0, desc);
	for (i = 0; i < count; i++) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    sensors[i], CTLTYPE_INT | CTLFLAG_RD,
		    dev, (sensor_char << 8) | i,
		    apple_smc_sensor_sysctl, "I", desc);
	}
}

static int
apple_smc_detect_sensors(device_t dev)
{
	struct apple_smc_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *sysctlctx;
	char key[ASMC_KEYLEN + 1];
	char type[ASMC_TYPELEN + 1];
	uint8_t len;
	unsigned int start, end, i;
	int error;
	char *sensor_key;

	if (sc->sc_nkeys == 0)
		return (0);

	/* Temperature: T..U range, sp78 only.
	 * Probe-read each key: values consistently below -10000 millideg are
	 * Intel DTS (distance-to-Tj,max) readings, not absolute temperatures.
	 * Values at or below -120000 millideg are "not connected" sentinels
	 * (Apple encodes these as 0x8000/0x8100/0x8200 in sp78) and are
	 * dropped entirely.  DTS sensors are stored separately and decoded in
	 * the sysctl handler as: actual = sc_tj_max*1000 + dts_millideg. */
	error = apple_smc_key_search(dev, "T\0\0\0", &start);
	if (error == 0)
		error = apple_smc_key_search(dev, "U\0\0\0", &end);
	if (error == 0) {
		for (i = start; i < end; i++) {
			int probe;

			if (apple_smc_key_dump_by_index(dev, i,
			    key, type, &len))
				continue;
			if (len != 2 || strncmp(type, "sp78", 4) != 0)
				continue;

			/* Probe read to classify the sensor. */
			if (apple_smc_sensor_read(dev, key, &probe) != 0)
				continue;

			/* Disconnected sentinel: drop. */
			if (probe <= -120000)
				continue;

			sensor_key = kmalloc(ASMC_KEYLEN + 1,
			    M_DEVBUF, M_WAITOK);
			memcpy(sensor_key, key, ASMC_KEYLEN + 1);

			if (probe < -10000) {
				/* DTS sensor. */
				if (sc->sc_dts_count < ASMC_TEMP_MAX)
					sc->sc_dts_sensors[sc->sc_dts_count++] =
					    sensor_key;
				else
					kfree(sensor_key, M_DEVBUF);
			} else {
				/* Absolute temperature sensor. */
				if (sc->sc_temp_count < ASMC_TEMP_MAX)
					sc->sc_temp_sensors[sc->sc_temp_count++] =
					    sensor_key;
				else
					kfree(sensor_key, M_DEVBUF);
			}
		}
	}

	/* Voltage: V..W */
	error = apple_smc_key_search(dev, "V\0\0\0", &start);
	if (error == 0) error = apple_smc_key_search(dev, "W\0\0\0", &end);
	if (error == 0)
		apple_smc_scan_sensor_range(dev, start, end, 'V',
		    &sc->sc_voltage_count, sc->sc_voltage_sensors,
		    ASMC_MAX_SENSORS);

	/* Current: I..J */
	error = apple_smc_key_search(dev, "I\0\0\0", &start);
	if (error == 0) error = apple_smc_key_search(dev, "J\0\0\0", &end);
	if (error == 0)
		apple_smc_scan_sensor_range(dev, start, end, 'I',
		    &sc->sc_current_count, sc->sc_current_sensors,
		    ASMC_MAX_SENSORS);

	/* Power: P..Q */
	error = apple_smc_key_search(dev, "P\0\0\0", &start);
	if (error == 0) error = apple_smc_key_search(dev, "Q\0\0\0", &end);
	if (error == 0)
		apple_smc_scan_sensor_range(dev, start, end, 'P',
		    &sc->sc_power_count, sc->sc_power_sensors,
		    ASMC_MAX_SENSORS);

	/* Ambient light: ALV/ALS keys in A..B */
	error = apple_smc_key_search(dev, "A\0\0\0", &start);
	if (error == 0) error = apple_smc_key_search(dev, "B\0\0\0", &end);
	if (error == 0) {
		for (i = start; i < end; i++) {
			if (apple_smc_key_dump_by_index(dev, i,
			    key, type, &len))
				continue;
			if (key[0] != 'A' || key[1] != 'L' ||
			    (key[2] != 'V' && key[2] != 'S') || len != 2)
				continue;
			if (!apple_smc_sensor_type_supported(type))
				continue;
			if (sc->sc_light_count >= ASMC_MAX_SENSORS)
				break;
			sensor_key = kmalloc(ASMC_KEYLEN + 1,
			    M_DEVBUF, M_WAITOK);
			memcpy(sensor_key, key, ASMC_KEYLEN + 1);
			sc->sc_light_sensors[sc->sc_light_count++] = sensor_key;
		}
	}

	sysctlctx = device_get_sysctl_ctx(dev);

	apple_smc_register_sensor_tree(sysctlctx, dev, "voltage",
	    "Voltage sensors (millivolts)", 'V',
	    sc->sc_voltage_sensors, sc->sc_voltage_count);
	apple_smc_register_sensor_tree(sysctlctx, dev, "current",
	    "Current sensors (milliamps)", 'I',
	    sc->sc_current_sensors, sc->sc_current_count);
	apple_smc_register_sensor_tree(sysctlctx, dev, "power",
	    "Power sensors (milliwatts)", 'P',
	    sc->sc_power_sensors, sc->sc_power_count);
	apple_smc_register_sensor_tree(sysctlctx, dev, "ambient",
	    "Ambient light sensors", 'L',
	    sc->sc_light_sensors, sc->sc_light_count);

	return (0);
}
