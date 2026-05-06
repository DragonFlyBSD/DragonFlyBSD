/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * Apple EFI Firmware Properties driver for DragonFly BSD.
 *
 * Discovers Apple-specific ACPI structures on Macs:
 * - Enumerates Apple SSDTs (SmcDppt, PcieTbt, Sdxc, etc.)
 * - Evaluates OSDW (macOS detection flag)
 * - Walks namespace for _DSM methods using Apple's DTGP UUID
 * - Extracts device properties and exposes via sysctl
 */

#include "apple_fw.h"

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("APPLE_FW")

MALLOC_DEFINE(M_APPLEFW, "apple_fw", "Apple EFI firmware driver");

static int	apple_fw_identify(driver_t *, device_t);
static int	apple_fw_probe(device_t);
static int	apple_fw_attach(device_t);
static int	apple_fw_detach(device_t);

static void	apple_fw_enumerate_ssdts(device_t);
static void	apple_fw_eval_osdw(device_t);

static device_method_t apple_fw_methods[] = {
	DEVMETHOD(device_identify,	apple_fw_identify),
	DEVMETHOD(device_probe,		apple_fw_probe),
	DEVMETHOD(device_attach,	apple_fw_attach),
	DEVMETHOD(device_detach,	apple_fw_detach),
	DEVMETHOD_END
};

static driver_t apple_fw_driver = {
	"apple_fw",
	apple_fw_methods,
	sizeof(struct apple_fw_softc),
	.gpri = KOBJ_GPRI_ACPI
};

static devclass_t apple_fw_devclass;

DRIVER_MODULE(apple_fw, acpi, apple_fw_driver, apple_fw_devclass,
    NULL, NULL);
MODULE_DEPEND(apple_fw, acpi, 1, 1, 1);
MODULE_VERSION(apple_fw, 1);

/*
 * Check if running on Apple EFI firmware by examining the FADT OEM ID.
 */
static int
apple_fw_is_apple(void)
{
	ACPI_TABLE_HEADER hdr;
	ACPI_STATUS status;

	status = AcpiGetTableHeader(ACPI_SIG_FADT, 0, &hdr);
	if (ACPI_FAILURE(status))
		return (0);
	return (strncmp(hdr.OemId, "APPLE ", ACPI_OEM_ID_SIZE) == 0);
}

/*
 * Tag value for our synthetic child — used by probe to distinguish
 * the device we create from real ACPI-enumerated devices.
 */
#define	DEV_APPLE_FW(x)	\
    (acpi_get_magic(x) == (uintptr_t)&apple_fw_devclass)

static int
apple_fw_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Don't re-add on bus rescan */
	if (device_find_child(parent, "apple_fw", -1) != NULL)
		return (0);

	if (!apple_fw_is_apple())
		return (ENXIO);

	child = BUS_ADD_CHILD(parent, parent, 0, "apple_fw", -1);
	if (child != NULL)
		acpi_set_magic(child, (uintptr_t)&apple_fw_devclass);
	return (0);
}

static int
apple_fw_probe(device_t dev)
{
	char *product, *biosver;
	char desc[128];

	if (acpi_disabled("apple_fw"))
		return (ENXIO);

	/* Only match the synthetic child we tagged in identify */
	if (!DEV_APPLE_FW(dev))
		return (ENXIO);

	product = kgetenv("smbios.system.product");
	biosver = kgetenv("smbios.bios.version");
	ksnprintf(desc, sizeof(desc), "Apple %s EFI Firmware %s",
	    product, biosver);
	device_set_desc_copy(dev, desc);
	kfreeenv(product);
	kfreeenv(biosver);

	return (0);
}

static int
apple_fw_attach(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;
	lockinit(&sc->sc_lock, "apple_fw", 0, LK_CANRECURSE);

	/* Enumerate Apple-specific SSDTs */
	apple_fw_enumerate_ssdts(dev);

	/* Check OSDW (macOS detection) state */
	apple_fw_eval_osdw(dev);

	/* Discover _DSM device properties via DTGP */
	apple_fw_discover_dsm_devices(dev);

	/* Register sysctl tree */
	apple_fw_sysctl_init(dev);

	return (0);
}

static int
apple_fw_detach(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);

	lockuninit(&sc->sc_lock);
	return (0);
}

/*
 * Enumerate all SSDT tables with OEM ID "APPLE".
 */
static void
apple_fw_enumerate_ssdts(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);
	ACPI_TABLE_HEADER *table;
	ACPI_STATUS status;
	uint32_t instance;
	int n;

	sc->sc_ssdt_count = 0;

	for (instance = 1; instance <= 128; instance++) {
		status = AcpiGetTable(ACPI_SIG_SSDT, instance, &table);
		if (ACPI_FAILURE(status))
			break;

		/* Only catalog Apple SSDTs */
		if (strncmp(table->OemId, "APPLE ", ACPI_OEM_ID_SIZE) != 0)
			continue;

		n = sc->sc_ssdt_count;
		if (n >= APPLE_FW_MAX_SSDTS)
			break;

		memcpy(sc->sc_ssdts[n].oem_table_id, table->OemTableId,
		    ACPI_OEM_TABLE_ID_SIZE);
		sc->sc_ssdts[n].oem_table_id[ACPI_OEM_TABLE_ID_SIZE] = '\0';
		sc->sc_ssdt_count++;
	}
}

/*
 * Evaluate the OSDW method.  Returns 1 if the firmware thinks macOS is
 * running (OSYS == 0x2710), 0 otherwise.
 */
static void
apple_fw_eval_osdw(device_t dev)
{
	struct apple_fw_softc *sc = device_get_softc(dev);
	UINT32 val;

	sc->sc_osdw = 0;

	if (ACPI_SUCCESS(acpi_GetInteger(NULL, "OSDW", &val)))
		sc->sc_osdw = (int)val;
}
