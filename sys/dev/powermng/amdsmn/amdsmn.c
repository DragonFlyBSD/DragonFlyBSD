/*-
 * Copyright (c) 2017-2020 Conrad Meyer <cem@FreeBSD.org>
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
 * $FreeBSD: head/sys/dev/amdsmn/amdsmn.c 366136 2020-09-25 04:16:28Z cem $
 */

/*
 * Driver for the AMD Family 15h, 17h, 19h CPU System Management Network.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pci_cfgreg.h>

#include <dev/powermng/amdsmn/amdsmn.h>

#define	F15H_SMN_ADDR_REG	0xb8
#define	F15H_SMN_DATA_REG	0xbc

#define	F17H_SMN_ADDR_REG	0x60
#define	F17H_SMN_DATA_REG	0x64

#define	F19H_SMN_ADDR_REG	0x60
#define	F19H_SMN_DATA_REG	0x64

#define	PCI_DEVICE_ID_AMD_15H_M60H_ROOT		0x1576
#define	PCI_DEVICE_ID_AMD_17H_ROOT		0x1450
#define	PCI_DEVICE_ID_AMD_17H_M10H_ROOT		0x15d0
#define	PCI_DEVICE_ID_AMD_17H_M30H_ROOT		0x1480	/* Also M70H. */
#define	PCI_DEVICE_ID_AMD_17H_M60H_ROOT		0x1630
#define	PCI_DEVICE_ID_AMD_17H_MA0H_ROOT		0x14b5

#if 0
#define	PCI_DEVICE_ID_AMD_19H_M10H_ROOT		0x14b1
#define	PCI_DEVICE_ID_AMD_19H_M40H_ROOT		0x14b5
#define	PCI_DEVICE_ID_AMD_19H_M50H_ROOT		0x166e
#define	PCI_DEVICE_ID_AMD_19H_M60H_ROOT		0x14e4
#define	PCI_DEVICE_ID_AMD_19H_M70H_ROOT		0x14f4
#endif
#define	PCI_DEVICE_ID_AMD_19H_M10H_ROOT		0x14a4
#define	PCI_DEVICE_ID_AMD_19H_M60H_ROOT		0x14d8
#define	PCI_DEVICE_ID_AMD_19H_M70H_ROOT		0x14e8

struct pciid;
struct amdsmn_softc {
	struct lock smn_lock;
	const struct pciid *smn_pciid;
};

static const struct pciid {
	uint16_t	amdsmn_vendorid;
	uint16_t	amdsmn_deviceid;
	uint8_t		amdsmn_addr_reg;
	uint8_t		amdsmn_data_reg;
} amdsmn_ids[] = {
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_15H_M60H_ROOT,
		.amdsmn_addr_reg = F15H_SMN_ADDR_REG,
		.amdsmn_data_reg = F15H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_17H_ROOT,
		.amdsmn_addr_reg = F17H_SMN_ADDR_REG,
		.amdsmn_data_reg = F17H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_17H_M10H_ROOT,
		.amdsmn_addr_reg = F17H_SMN_ADDR_REG,
		.amdsmn_data_reg = F17H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_17H_M30H_ROOT,
		.amdsmn_addr_reg = F17H_SMN_ADDR_REG,
		.amdsmn_data_reg = F17H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_17H_M60H_ROOT,
		.amdsmn_addr_reg = F17H_SMN_ADDR_REG,
		.amdsmn_data_reg = F17H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_17H_MA0H_ROOT,
		.amdsmn_addr_reg = F17H_SMN_ADDR_REG,
		.amdsmn_data_reg = F17H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_19H_M10H_ROOT,
		.amdsmn_addr_reg = F19H_SMN_ADDR_REG,
		.amdsmn_data_reg = F19H_SMN_DATA_REG,
	},
#if 0
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_19H_M40H_ROOT,
		.amdsmn_addr_reg = F19H_SMN_ADDR_REG,
		.amdsmn_data_reg = F19H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_19H_M50H_ROOT,
		.amdsmn_addr_reg = F19H_SMN_ADDR_REG,
		.amdsmn_data_reg = F19H_SMN_DATA_REG,
	},
#endif
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_19H_M60H_ROOT,
		.amdsmn_addr_reg = F19H_SMN_ADDR_REG,
		.amdsmn_data_reg = F19H_SMN_DATA_REG,
	},
	{
		.amdsmn_vendorid = CPU_VENDOR_AMD,
		.amdsmn_deviceid = PCI_DEVICE_ID_AMD_19H_M70H_ROOT,
		.amdsmn_addr_reg = F19H_SMN_ADDR_REG,
		.amdsmn_data_reg = F19H_SMN_DATA_REG,
	},
};

/*
 * Device methods.
 */
static void 	amdsmn_identify(driver_t *driver, device_t parent);
static int	amdsmn_probe(device_t dev);
static int	amdsmn_attach(device_t dev);
static int	amdsmn_detach(device_t dev);

static device_method_t amdsmn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	amdsmn_identify),
	DEVMETHOD(device_probe,		amdsmn_probe),
	DEVMETHOD(device_attach,	amdsmn_attach),
	DEVMETHOD(device_detach,	amdsmn_detach),
	DEVMETHOD_END
};

static driver_t amdsmn_driver = {
	"amdsmn",
	amdsmn_methods,
	sizeof(struct amdsmn_softc),
};

static devclass_t amdsmn_devclass;
DRIVER_MODULE_ORDERED(amdsmn, hostb, amdsmn_driver,
			&amdsmn_devclass, NULL, NULL, SI_ORDER_EARLIER);
MODULE_VERSION(amdsmn, 1);
#if !defined(__DragonFly__)
MODULE_PNP_INFO("U16:vendor;U16:device", pci, amdsmn, amdsmn_ids,
    nitems(amdsmn_ids));
#endif

static bool
amdsmn_match(device_t parent, const struct pciid **pciid_out)
{
	uint16_t vendor, device;
	size_t i;

	vendor = pci_get_vendor(parent);
	device = pci_get_device(parent);

	for (i = 0; i < nitems(amdsmn_ids); i++) {
		if (vendor == amdsmn_ids[i].amdsmn_vendorid &&
		    device == amdsmn_ids[i].amdsmn_deviceid) {
			if (pciid_out != NULL)
				*pciid_out = &amdsmn_ids[i];
			return (true);
		}
	}
	return (false);
}

static void
amdsmn_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "amdsmn", -1) != NULL)
		return;
	if (!amdsmn_match(parent, NULL))
		return;

	child = device_add_child(parent, "amdsmn", -1);
	if (child == NULL)
		device_printf(parent, "add amdsmn child failed\n");
}

static int
amdsmn_probe(device_t dev)
{
	uint32_t family;
	char buf[64];

	if (resource_disabled("amdsmn", 0))
		return (ENXIO);
	if (!amdsmn_match(device_get_parent(dev), NULL))
		return (ENXIO);

	family = CPUID_TO_FAMILY(cpu_id);

	switch (family) {
	case 0x15:
	case 0x17:
	case 0x19:
		break;
	default:
		return (ENXIO);
	}
	ksnprintf(buf, sizeof(buf), "AMD Family %xh System Management Network",
	    family);
	device_set_desc_copy(dev, buf);

	return (BUS_PROBE_GENERIC);
}

static int
amdsmn_attach(device_t dev)
{
	struct amdsmn_softc *sc = device_get_softc(dev);

	if (!amdsmn_match(device_get_parent(dev), &sc->smn_pciid))
		return (ENXIO);

	lockinit(&sc->smn_lock, device_get_nameunit(dev), 0, 0);
	return (0);
}

int
amdsmn_detach(device_t dev)
{
	struct amdsmn_softc *sc = device_get_softc(dev);

	lockuninit(&sc->smn_lock);
	return (0);
}

int
amdsmn_read(device_t dev, uint32_t addr, uint32_t *value)
{
	struct amdsmn_softc *sc = device_get_softc(dev);
	device_t parent;

	parent = device_get_parent(dev);

	lockmgr(&sc->smn_lock, LK_EXCLUSIVE);
	pci_write_config(parent, sc->smn_pciid->amdsmn_addr_reg, addr, 4);
	*value = pci_read_config(parent, sc->smn_pciid->amdsmn_data_reg, 4);
	lockmgr(&sc->smn_lock, LK_RELEASE);

	return (0);
}

int
amdsmn_write(device_t dev, uint32_t addr, uint32_t value)
{
	struct amdsmn_softc *sc = device_get_softc(dev);
	device_t parent;

	parent = device_get_parent(dev);

	lockmgr(&sc->smn_lock, LK_EXCLUSIVE);
	pci_write_config(parent, sc->smn_pciid->amdsmn_addr_reg, addr, 4);
	pci_write_config(parent, sc->smn_pciid->amdsmn_data_reg, value, 4);
	lockmgr(&sc->smn_lock, LK_RELEASE);

	return (0);
}
