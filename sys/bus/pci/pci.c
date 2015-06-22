/*-
 * Copyright (c) 1997, Stefan Esser <se@kfreebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@kfreebsd.org>
 * Copyright (c) 2000, BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/pci/pci.c,v 1.355.2.9.2.1 2009/04/15 03:14:26 kensmith Exp $
 */

#include "opt_acpi.h"
#include "opt_compat_oldpci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/machintr.h>

#include <machine/msi_machdep.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/device.h>

#include <sys/pciio.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pci_private.h>

#include <bus/u4b/controller/xhcireg.h>
#include <bus/u4b/controller/ehcireg.h>
#include <bus/u4b/controller/ohcireg.h>
#include <bus/u4b/controller/uhcireg.h>

#include "pcib_if.h"
#include "pci_if.h"

#ifdef __HAVE_ACPI
#include <contrib/dev/acpica/acpi.h>
#include "acpi_if.h"
#else
#define	ACPI_PWR_FOR_SLEEP(x, y, z)
#endif

typedef void	(*pci_read_cap_t)(device_t, int, int, pcicfgregs *);

static uint32_t		pci_mapbase(unsigned mapreg);
static const char	*pci_maptype(unsigned mapreg);
static int		pci_mapsize(unsigned testval);
static int		pci_maprange(unsigned mapreg);
static void		pci_fixancient(pcicfgregs *cfg);

static int		pci_porten(device_t pcib, int b, int s, int f);
static int		pci_memen(device_t pcib, int b, int s, int f);
static void		pci_assign_interrupt(device_t bus, device_t dev,
			    int force_route);
static int		pci_add_map(device_t pcib, device_t bus, device_t dev,
			    int b, int s, int f, int reg,
			    struct resource_list *rl, int force, int prefetch);
static int		pci_probe(device_t dev);
static int		pci_attach(device_t dev);
static void		pci_child_detached(device_t, device_t);
static void		pci_load_vendor_data(void);
static int		pci_describe_parse_line(char **ptr, int *vendor,
			    int *device, char **desc);
static char		*pci_describe_device(device_t dev);
static int		pci_modevent(module_t mod, int what, void *arg);
static void		pci_hdrtypedata(device_t pcib, int b, int s, int f,
			    pcicfgregs *cfg);
static void		pci_read_capabilities(device_t pcib, pcicfgregs *cfg);
static int		pci_read_vpd_reg(device_t pcib, pcicfgregs *cfg,
			    int reg, uint32_t *data);
#if 0
static int		pci_write_vpd_reg(device_t pcib, pcicfgregs *cfg,
			    int reg, uint32_t data);
#endif
static void		pci_read_vpd(device_t pcib, pcicfgregs *cfg);
static void		pci_disable_msi(device_t dev);
static void		pci_enable_msi(device_t dev, uint64_t address,
			    uint16_t data);
static void		pci_setup_msix_vector(device_t dev, u_int index,
			    uint64_t address, uint32_t data);
static void		pci_mask_msix_vector(device_t dev, u_int index);
static void		pci_unmask_msix_vector(device_t dev, u_int index);
static void		pci_mask_msix_allvectors(device_t dev);
static struct msix_vector *pci_find_msix_vector(device_t dev, int rid);
static int		pci_msi_blacklisted(void);
static void		pci_resume_msi(device_t dev);
static void		pci_resume_msix(device_t dev);
static int		pcie_slotimpl(const pcicfgregs *);
static void		pci_print_verbose_expr(const pcicfgregs *);

static void		pci_read_cap_pmgt(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_ht(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_msi(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_msix(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_vpd(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_subvendor(device_t, int, int,
			    pcicfgregs *);
static void		pci_read_cap_pcix(device_t, int, int, pcicfgregs *);
static void		pci_read_cap_express(device_t, int, int, pcicfgregs *);

static device_method_t pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pci_probe),
	DEVMETHOD(device_attach,	pci_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	pci_suspend),
	DEVMETHOD(device_resume,	pci_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	pci_print_child),
	DEVMETHOD(bus_probe_nomatch,	pci_probe_nomatch),
	DEVMETHOD(bus_read_ivar,	pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	pci_write_ivar),
	DEVMETHOD(bus_driver_added,	pci_driver_added),
	DEVMETHOD(bus_child_detached,	pci_child_detached),
	DEVMETHOD(bus_setup_intr,	pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	pci_teardown_intr),

	DEVMETHOD(bus_get_resource_list,pci_get_resource_list),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_delete_resource,	pci_delete_resource),
	DEVMETHOD(bus_alloc_resource,	pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_child_pnpinfo_str, pci_child_pnpinfo_str_method),
	DEVMETHOD(bus_child_location_str, pci_child_location_str_method),

	/* PCI interface */
	DEVMETHOD(pci_read_config,	pci_read_config_method),
	DEVMETHOD(pci_write_config,	pci_write_config_method),
	DEVMETHOD(pci_enable_busmaster,	pci_enable_busmaster_method),
	DEVMETHOD(pci_disable_busmaster, pci_disable_busmaster_method),
	DEVMETHOD(pci_enable_io,	pci_enable_io_method),
	DEVMETHOD(pci_disable_io,	pci_disable_io_method),
	DEVMETHOD(pci_get_vpd_ident,	pci_get_vpd_ident_method),
	DEVMETHOD(pci_get_vpd_readonly,	pci_get_vpd_readonly_method),
	DEVMETHOD(pci_get_powerstate,	pci_get_powerstate_method),
	DEVMETHOD(pci_set_powerstate,	pci_set_powerstate_method),
	DEVMETHOD(pci_assign_interrupt,	pci_assign_interrupt_method),
	DEVMETHOD(pci_find_extcap,	pci_find_extcap_method),
	DEVMETHOD(pci_alloc_msi,	pci_alloc_msi_method),
	DEVMETHOD(pci_release_msi,	pci_release_msi_method),
	DEVMETHOD(pci_alloc_msix_vector, pci_alloc_msix_vector_method),
	DEVMETHOD(pci_release_msix_vector, pci_release_msix_vector_method),
	DEVMETHOD(pci_msi_count,	pci_msi_count_method),
	DEVMETHOD(pci_msix_count,	pci_msix_count_method),

	DEVMETHOD_END
};

DEFINE_CLASS_0(pci, pci_driver, pci_methods, 0);

static devclass_t pci_devclass;
DRIVER_MODULE(pci, pcib, pci_driver, pci_devclass, pci_modevent, NULL);
MODULE_VERSION(pci, 1);

static char	*pci_vendordata;
static size_t	pci_vendordata_size;


static const struct pci_read_cap {
	int		cap;
	pci_read_cap_t	read_cap;
} pci_read_caps[] = {
	{ PCIY_PMG,		pci_read_cap_pmgt },
	{ PCIY_HT,		pci_read_cap_ht },
	{ PCIY_MSI,		pci_read_cap_msi },
	{ PCIY_MSIX,		pci_read_cap_msix },
	{ PCIY_VPD,		pci_read_cap_vpd },
	{ PCIY_SUBVENDOR,	pci_read_cap_subvendor },
	{ PCIY_PCIX,		pci_read_cap_pcix },
	{ PCIY_EXPRESS,		pci_read_cap_express },
	{ 0, NULL } /* required last entry */
};

struct pci_quirk {
	uint32_t devid;	/* Vendor/device of the card */
	int	type;
#define	PCI_QUIRK_MAP_REG	1 /* PCI map register in weird place */
#define	PCI_QUIRK_DISABLE_MSI	2 /* MSI/MSI-X doesn't work */
	int	arg1;
	int	arg2;
};

struct pci_quirk pci_quirks[] = {
	/* The Intel 82371AB and 82443MX has a map register at offset 0x90. */
	{ 0x71138086, PCI_QUIRK_MAP_REG,	0x90,	 0 },
	{ 0x719b8086, PCI_QUIRK_MAP_REG,	0x90,	 0 },
	/* As does the Serverworks OSB4 (the SMBus mapping register) */
	{ 0x02001166, PCI_QUIRK_MAP_REG,	0x90,	 0 },

	/*
	 * MSI doesn't work with the ServerWorks CNB20-HE Host Bridge
	 * or the CMIC-SL (AKA ServerWorks GC_LE).
	 */
	{ 0x00141166, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x00171166, PCI_QUIRK_DISABLE_MSI,	0,	0 },

	/*
	 * MSI doesn't work on earlier Intel chipsets including
	 * E7500, E7501, E7505, 845, 865, 875/E7210, and 855.
	 */
	{ 0x25408086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x254c8086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x25508086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x25608086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x25708086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x25788086, PCI_QUIRK_DISABLE_MSI,	0,	0 },
	{ 0x35808086, PCI_QUIRK_DISABLE_MSI,	0,	0 },

	/*
	 * MSI doesn't work with devices behind the AMD 8131 HT-PCIX
	 * bridge.
	 */
	{ 0x74501022, PCI_QUIRK_DISABLE_MSI,	0,	0 },

	{ 0 }
};

/* map register information */
#define	PCI_MAPMEM	0x01	/* memory map */
#define	PCI_MAPMEMP	0x02	/* prefetchable memory map */
#define	PCI_MAPPORT	0x04	/* port map */

#define PCI_MSIX_RID2VEC(rid)	((rid) - 1)	/* rid -> MSI-X vector # */
#define PCI_MSIX_VEC2RID(vec)	((vec) + 1)	/* MSI-X vector # -> rid */

struct devlist pci_devq;
uint32_t pci_generation;
uint32_t pci_numdevs = 0;
static int pcie_chipset, pcix_chipset;

/* sysctl vars */
SYSCTL_NODE(_hw, OID_AUTO, pci, CTLFLAG_RD, 0, "PCI bus tuning parameters");

static int pci_enable_io_modes = 1;
TUNABLE_INT("hw.pci.enable_io_modes", &pci_enable_io_modes);
SYSCTL_INT(_hw_pci, OID_AUTO, enable_io_modes, CTLFLAG_RW,
    &pci_enable_io_modes, 1,
    "Enable I/O and memory bits in the config register.  Some BIOSes do not\n\
enable these bits correctly.  We'd like to do this all the time, but there\n\
are some peripherals that this causes problems with.");

static int pci_do_power_nodriver = 0;
TUNABLE_INT("hw.pci.do_power_nodriver", &pci_do_power_nodriver);
SYSCTL_INT(_hw_pci, OID_AUTO, do_power_nodriver, CTLFLAG_RW,
    &pci_do_power_nodriver, 0,
  "Place a function into D3 state when no driver attaches to it.  0 means\n\
disable.  1 means conservatively place devices into D3 state.  2 means\n\
aggressively place devices into D3 state.  3 means put absolutely everything\n\
in D3 state.");

static int pci_do_power_resume = 1;
TUNABLE_INT("hw.pci.do_power_resume", &pci_do_power_resume);
SYSCTL_INT(_hw_pci, OID_AUTO, do_power_resume, CTLFLAG_RW,
    &pci_do_power_resume, 1,
  "Transition from D3 -> D0 on resume.");

static int pci_do_msi = 1;
TUNABLE_INT("hw.pci.enable_msi", &pci_do_msi);
SYSCTL_INT(_hw_pci, OID_AUTO, enable_msi, CTLFLAG_RW, &pci_do_msi, 1,
    "Enable support for MSI interrupts");

static int pci_do_msix = 1;
TUNABLE_INT("hw.pci.enable_msix", &pci_do_msix);
SYSCTL_INT(_hw_pci, OID_AUTO, enable_msix, CTLFLAG_RW, &pci_do_msix, 1,
    "Enable support for MSI-X interrupts");

static int pci_honor_msi_blacklist = 1;
TUNABLE_INT("hw.pci.honor_msi_blacklist", &pci_honor_msi_blacklist);
SYSCTL_INT(_hw_pci, OID_AUTO, honor_msi_blacklist, CTLFLAG_RD,
    &pci_honor_msi_blacklist, 1, "Honor chipset blacklist for MSI");

#if defined(__x86_64__)
static int pci_usb_takeover = 1;
TUNABLE_INT("hw.pci.usb_early_takeover", &pci_usb_takeover);
SYSCTL_INT(_hw_pci, OID_AUTO, usb_early_takeover, CTLFLAG_RD,
    &pci_usb_takeover, 1, "Enable early takeover of USB controllers.\n\
Disable this if you depend on BIOS emulation of USB devices, that is\n\
you use USB devices (like keyboard or mouse) but do not load USB drivers");
#endif

static int pci_msi_cpuid;

/* Find a device_t by bus/slot/function in domain 0 */

device_t
pci_find_bsf(uint8_t bus, uint8_t slot, uint8_t func)
{

	return (pci_find_dbsf(0, bus, slot, func));
}

/* Find a device_t by domain/bus/slot/function */

device_t
pci_find_dbsf(uint32_t domain, uint8_t bus, uint8_t slot, uint8_t func)
{
	struct pci_devinfo *dinfo;

	STAILQ_FOREACH(dinfo, &pci_devq, pci_links) {
		if ((dinfo->cfg.domain == domain) &&
		    (dinfo->cfg.bus == bus) &&
		    (dinfo->cfg.slot == slot) &&
		    (dinfo->cfg.func == func)) {
			return (dinfo->cfg.dev);
		}
	}

	return (NULL);
}

/* Find a device_t by vendor/device ID */

device_t
pci_find_device(uint16_t vendor, uint16_t device)
{
	struct pci_devinfo *dinfo;

	STAILQ_FOREACH(dinfo, &pci_devq, pci_links) {
		if ((dinfo->cfg.vendor == vendor) &&
		    (dinfo->cfg.device == device)) {
			return (dinfo->cfg.dev);
		}
	}

	return (NULL);
}

device_t
pci_find_class(uint8_t class, uint8_t subclass)
{
	struct pci_devinfo *dinfo;

	STAILQ_FOREACH(dinfo, &pci_devq, pci_links) {
		if (dinfo->cfg.baseclass == class &&
		    dinfo->cfg.subclass == subclass) {
			return (dinfo->cfg.dev);
		}
	}

	return (NULL);
}

/* return base address of memory or port map */

static uint32_t
pci_mapbase(uint32_t mapreg)
{

	if (PCI_BAR_MEM(mapreg))
		return (mapreg & PCIM_BAR_MEM_BASE);
	else
		return (mapreg & PCIM_BAR_IO_BASE);
}

/* return map type of memory or port map */

static const char *
pci_maptype(unsigned mapreg)
{

	if (PCI_BAR_IO(mapreg))
		return ("I/O Port");
	if (mapreg & PCIM_BAR_MEM_PREFETCH)
		return ("Prefetchable Memory");
	return ("Memory");
}

/* return log2 of map size decoded for memory or port map */

static int
pci_mapsize(uint32_t testval)
{
	int ln2size;

	testval = pci_mapbase(testval);
	ln2size = 0;
	if (testval != 0) {
		while ((testval & 1) == 0)
		{
			ln2size++;
			testval >>= 1;
		}
	}
	return (ln2size);
}

/* return log2 of address range supported by map register */

static int
pci_maprange(unsigned mapreg)
{
	int ln2range = 0;

	if (PCI_BAR_IO(mapreg))
		ln2range = 32;
	else
		switch (mapreg & PCIM_BAR_MEM_TYPE) {
		case PCIM_BAR_MEM_32:
			ln2range = 32;
			break;
		case PCIM_BAR_MEM_1MB:
			ln2range = 20;
			break;
		case PCIM_BAR_MEM_64:
			ln2range = 64;
			break;
		}
	return (ln2range);
}

/* adjust some values from PCI 1.0 devices to match 2.0 standards ... */

static void
pci_fixancient(pcicfgregs *cfg)
{
	if (cfg->hdrtype != 0)
		return;

	/* PCI to PCI bridges use header type 1 */
	if (cfg->baseclass == PCIC_BRIDGE && cfg->subclass == PCIS_BRIDGE_PCI)
		cfg->hdrtype = 1;
}

/* extract header type specific config data */

static void
pci_hdrtypedata(device_t pcib, int b, int s, int f, pcicfgregs *cfg)
{
#define	REG(n, w)	PCIB_READ_CONFIG(pcib, b, s, f, n, w)
	switch (cfg->hdrtype) {
	case 0:
		cfg->subvendor      = REG(PCIR_SUBVEND_0, 2);
		cfg->subdevice      = REG(PCIR_SUBDEV_0, 2);
		cfg->nummaps	    = PCI_MAXMAPS_0;
		break;
	case 1:
		cfg->nummaps	    = PCI_MAXMAPS_1;
#ifdef COMPAT_OLDPCI
		cfg->secondarybus   = REG(PCIR_SECBUS_1, 1);
#endif
		break;
	case 2:
		cfg->subvendor      = REG(PCIR_SUBVEND_2, 2);
		cfg->subdevice      = REG(PCIR_SUBDEV_2, 2);
		cfg->nummaps	    = PCI_MAXMAPS_2;
#ifdef COMPAT_OLDPCI
		cfg->secondarybus   = REG(PCIR_SECBUS_2, 1);
#endif
		break;
	}
#undef REG
}

/* read configuration header into pcicfgregs structure */
struct pci_devinfo *
pci_read_device(device_t pcib, int d, int b, int s, int f, size_t size)
{
#define	REG(n, w)	PCIB_READ_CONFIG(pcib, b, s, f, n, w)
	pcicfgregs *cfg = NULL;
	struct pci_devinfo *devlist_entry;
	struct devlist *devlist_head;

	devlist_head = &pci_devq;

	devlist_entry = NULL;

	if (REG(PCIR_DEVVENDOR, 4) != -1) {
		devlist_entry = kmalloc(size, M_DEVBUF, M_WAITOK | M_ZERO);

		cfg = &devlist_entry->cfg;

		cfg->domain		= d;
		cfg->bus		= b;
		cfg->slot		= s;
		cfg->func		= f;
		cfg->vendor		= REG(PCIR_VENDOR, 2);
		cfg->device		= REG(PCIR_DEVICE, 2);
		cfg->cmdreg		= REG(PCIR_COMMAND, 2);
		cfg->statreg		= REG(PCIR_STATUS, 2);
		cfg->baseclass		= REG(PCIR_CLASS, 1);
		cfg->subclass		= REG(PCIR_SUBCLASS, 1);
		cfg->progif		= REG(PCIR_PROGIF, 1);
		cfg->revid		= REG(PCIR_REVID, 1);
		cfg->hdrtype		= REG(PCIR_HDRTYPE, 1);
		cfg->cachelnsz		= REG(PCIR_CACHELNSZ, 1);
		cfg->lattimer		= REG(PCIR_LATTIMER, 1);
		cfg->intpin		= REG(PCIR_INTPIN, 1);
		cfg->intline		= REG(PCIR_INTLINE, 1);

		cfg->mingnt		= REG(PCIR_MINGNT, 1);
		cfg->maxlat		= REG(PCIR_MAXLAT, 1);

		cfg->mfdev		= (cfg->hdrtype & PCIM_MFDEV) != 0;
		cfg->hdrtype		&= ~PCIM_MFDEV;

		pci_fixancient(cfg);
		pci_hdrtypedata(pcib, b, s, f, cfg);

		pci_read_capabilities(pcib, cfg);

		STAILQ_INSERT_TAIL(devlist_head, devlist_entry, pci_links);

		devlist_entry->conf.pc_sel.pc_domain = cfg->domain;
		devlist_entry->conf.pc_sel.pc_bus = cfg->bus;
		devlist_entry->conf.pc_sel.pc_dev = cfg->slot;
		devlist_entry->conf.pc_sel.pc_func = cfg->func;
		devlist_entry->conf.pc_hdr = cfg->hdrtype;

		devlist_entry->conf.pc_subvendor = cfg->subvendor;
		devlist_entry->conf.pc_subdevice = cfg->subdevice;
		devlist_entry->conf.pc_vendor = cfg->vendor;
		devlist_entry->conf.pc_device = cfg->device;

		devlist_entry->conf.pc_class = cfg->baseclass;
		devlist_entry->conf.pc_subclass = cfg->subclass;
		devlist_entry->conf.pc_progif = cfg->progif;
		devlist_entry->conf.pc_revid = cfg->revid;

		pci_numdevs++;
		pci_generation++;
	}
	return (devlist_entry);
#undef REG
}

static int
pci_fixup_nextptr(int *nextptr0)
{
	int nextptr = *nextptr0;

	/* "Next pointer" is only one byte */
	KASSERT(nextptr <= 0xff, ("Illegal next pointer %d", nextptr));

	if (nextptr & 0x3) {
		/*
		 * PCI local bus spec 3.0:
		 *
		 * "... The bottom two bits of all pointers are reserved
		 *  and must be implemented as 00b although software must
		 *  mask them to allow for future uses of these bits ..."
		 */
		if (bootverbose) {
			kprintf("Illegal PCI extended capability "
				"offset, fixup 0x%02x -> 0x%02x\n",
				nextptr, nextptr & ~0x3);
		}
		nextptr &= ~0x3;
	}
	*nextptr0 = nextptr;

	if (nextptr < 0x40) {
		if (nextptr != 0) {
			kprintf("Illegal PCI extended capability "
				"offset 0x%02x", nextptr);
		}
		return 0;
	}
	return 1;
}

static void
pci_read_cap_pmgt(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	struct pcicfg_pp *pp = &cfg->pp;

	if (pp->pp_cap)
		return;

	pp->pp_cap = REG(ptr + PCIR_POWER_CAP, 2);
	pp->pp_status = ptr + PCIR_POWER_STATUS;
	pp->pp_pmcsr = ptr + PCIR_POWER_PMCSR;

	if ((nextptr - ptr) > PCIR_POWER_DATA) {
		/*
		 * XXX
		 * We should write to data_select and read back from
		 * data_scale to determine whether data register is
		 * implemented.
		 */
#ifdef foo
		pp->pp_data = ptr + PCIR_POWER_DATA;
#else
		pp->pp_data = 0;
#endif
	}

#undef REG
}

static void
pci_read_cap_ht(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#if defined(__x86_64__)

#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	struct pcicfg_ht *ht = &cfg->ht;
	uint64_t addr;
	uint32_t val;

	/* Determine HT-specific capability type. */
	val = REG(ptr + PCIR_HT_COMMAND, 2);

	if ((val & 0xe000) == PCIM_HTCAP_SLAVE)
		cfg->ht.ht_slave = ptr;

	if ((val & PCIM_HTCMD_CAP_MASK) != PCIM_HTCAP_MSI_MAPPING)
		return;

	if (!(val & PCIM_HTCMD_MSI_FIXED)) {
		/* Sanity check the mapping window. */
		addr = REG(ptr + PCIR_HTMSI_ADDRESS_HI, 4);
		addr <<= 32;
		addr |= REG(ptr + PCIR_HTMSI_ADDRESS_LO, 4);
		if (addr != MSI_X86_ADDR_BASE) {
			device_printf(pcib, "HT Bridge at pci%d:%d:%d:%d "
				"has non-default MSI window 0x%llx\n",
				cfg->domain, cfg->bus, cfg->slot, cfg->func,
				(long long)addr);
		}
	} else {
		addr = MSI_X86_ADDR_BASE;
	}

	ht->ht_msimap = ptr;
	ht->ht_msictrl = val;
	ht->ht_msiaddr = addr;

#undef REG

#endif	/* __x86_64__ */
}

static void
pci_read_cap_msi(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	struct pcicfg_msi *msi = &cfg->msi;

	msi->msi_location = ptr;
	msi->msi_ctrl = REG(ptr + PCIR_MSI_CTRL, 2);
	msi->msi_msgnum = 1 << ((msi->msi_ctrl & PCIM_MSICTRL_MMC_MASK) >> 1);

#undef REG
}

static void
pci_read_cap_msix(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	struct pcicfg_msix *msix = &cfg->msix;
	uint32_t val;

	msix->msix_location = ptr;
	msix->msix_ctrl = REG(ptr + PCIR_MSIX_CTRL, 2);
	msix->msix_msgnum = (msix->msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;

	val = REG(ptr + PCIR_MSIX_TABLE, 4);
	msix->msix_table_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	msix->msix_table_offset = val & ~PCIM_MSIX_BIR_MASK;

	val = REG(ptr + PCIR_MSIX_PBA, 4);
	msix->msix_pba_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	msix->msix_pba_offset = val & ~PCIM_MSIX_BIR_MASK;

	TAILQ_INIT(&msix->msix_vectors);

#undef REG
}

static void
pci_read_cap_vpd(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
	cfg->vpd.vpd_reg = ptr;
}

static void
pci_read_cap_subvendor(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	/* Should always be true. */
	if ((cfg->hdrtype & PCIM_HDRTYPE) == 1) {
		uint32_t val;

		val = REG(ptr + PCIR_SUBVENDCAP_ID, 4);
		cfg->subvendor = val & 0xffff;
		cfg->subdevice = val >> 16;
	}

#undef REG
}

static void
pci_read_cap_pcix(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
	/*
	 * Assume we have a PCI-X chipset if we have
	 * at least one PCI-PCI bridge with a PCI-X
	 * capability.  Note that some systems with
	 * PCI-express or HT chipsets might match on
	 * this check as well.
	 */
	if ((cfg->hdrtype & PCIM_HDRTYPE) == 1)
		pcix_chipset = 1;

	cfg->pcix.pcix_ptr = ptr;
}

static int
pcie_slotimpl(const pcicfgregs *cfg)
{
	const struct pcicfg_expr *expr = &cfg->expr;
	uint16_t port_type;

	/*
	 * - Slot implemented bit is meaningful iff current port is
	 *   root port or down stream port.
	 * - Testing for root port or down stream port is meanningful
	 *   iff PCI configure has type 1 header.
	 */

	if (cfg->hdrtype != 1)
		return 0;

	port_type = expr->expr_cap & PCIEM_CAP_PORT_TYPE;
	if (port_type != PCIE_ROOT_PORT && port_type != PCIE_DOWN_STREAM_PORT)
		return 0;

	if (!(expr->expr_cap & PCIEM_CAP_SLOT_IMPL))
		return 0;

	return 1;
}

static void
pci_read_cap_express(device_t pcib, int ptr, int nextptr, pcicfgregs *cfg)
{
#define REG(n, w)	\
	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)

	struct pcicfg_expr *expr = &cfg->expr;

	/*
	 * Assume we have a PCI-express chipset if we have
	 * at least one PCI-express device.
	 */
	pcie_chipset = 1;

	expr->expr_ptr = ptr;
	expr->expr_cap = REG(ptr + PCIER_CAPABILITY, 2);

	/*
	 * Read slot capabilities.  Slot capabilities exists iff
	 * current port's slot is implemented
	 */
	if (pcie_slotimpl(cfg))
		expr->expr_slotcap = REG(ptr + PCIER_SLOTCAP, 4);

#undef REG
}

static void
pci_read_capabilities(device_t pcib, pcicfgregs *cfg)
{
#define	REG(n, w)	PCIB_READ_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, w)
#define	WREG(n, v, w)	PCIB_WRITE_CONFIG(pcib, cfg->bus, cfg->slot, cfg->func, n, v, w)

	uint32_t val;
	int nextptr, ptrptr;

	if ((REG(PCIR_STATUS, 2) & PCIM_STATUS_CAPPRESENT) == 0) {
		/* No capabilities */
		return;
	}

	switch (cfg->hdrtype & PCIM_HDRTYPE) {
	case 0:
	case 1:
		ptrptr = PCIR_CAP_PTR;
		break;
	case 2:
		ptrptr = PCIR_CAP_PTR_2;	/* cardbus capabilities ptr */
		break;
	default:
		return;				/* no capabilities support */
	}
	nextptr = REG(ptrptr, 1);	/* sanity check? */

	/*
	 * Read capability entries.
	 */
	while (pci_fixup_nextptr(&nextptr)) {
		const struct pci_read_cap *rc;
		int ptr = nextptr;

		/* Find the next entry */
		nextptr = REG(ptr + PCICAP_NEXTPTR, 1);

		/* Process this entry */
		val = REG(ptr + PCICAP_ID, 1);
		for (rc = pci_read_caps; rc->read_cap != NULL; ++rc) {
			if (rc->cap == val) {
				rc->read_cap(pcib, ptr, nextptr, cfg);
				break;
			}
		}
	}

#if defined(__x86_64__)
	/*
	 * Enable the MSI mapping window for all HyperTransport
	 * slaves.  PCI-PCI bridges have their windows enabled via
	 * PCIB_MAP_MSI().
	 */
	if (cfg->ht.ht_slave != 0 && cfg->ht.ht_msimap != 0 &&
	    !(cfg->ht.ht_msictrl & PCIM_HTCMD_MSI_ENABLE)) {
		device_printf(pcib,
	    "Enabling MSI window for HyperTransport slave at pci%d:%d:%d:%d\n",
		    cfg->domain, cfg->bus, cfg->slot, cfg->func);
		 cfg->ht.ht_msictrl |= PCIM_HTCMD_MSI_ENABLE;
		 WREG(cfg->ht.ht_msimap + PCIR_HT_COMMAND, cfg->ht.ht_msictrl,
		     2);
	}
#endif

/* REG and WREG use carry through to next functions */
}

/*
 * PCI Vital Product Data
 */

#define	PCI_VPD_TIMEOUT		1000000

static int
pci_read_vpd_reg(device_t pcib, pcicfgregs *cfg, int reg, uint32_t *data)
{
	int count = PCI_VPD_TIMEOUT;

	KASSERT((reg & 3) == 0, ("VPD register must by 4 byte aligned"));

	WREG(cfg->vpd.vpd_reg + PCIR_VPD_ADDR, reg, 2);

	while ((REG(cfg->vpd.vpd_reg + PCIR_VPD_ADDR, 2) & 0x8000) != 0x8000) {
		if (--count < 0)
			return (ENXIO);
		DELAY(1);	/* limit looping */
	}
	*data = (REG(cfg->vpd.vpd_reg + PCIR_VPD_DATA, 4));

	return (0);
}

#if 0
static int
pci_write_vpd_reg(device_t pcib, pcicfgregs *cfg, int reg, uint32_t data)
{
	int count = PCI_VPD_TIMEOUT;

	KASSERT((reg & 3) == 0, ("VPD register must by 4 byte aligned"));

	WREG(cfg->vpd.vpd_reg + PCIR_VPD_DATA, data, 4);
	WREG(cfg->vpd.vpd_reg + PCIR_VPD_ADDR, reg | 0x8000, 2);
	while ((REG(cfg->vpd.vpd_reg + PCIR_VPD_ADDR, 2) & 0x8000) == 0x8000) {
		if (--count < 0)
			return (ENXIO);
		DELAY(1);	/* limit looping */
	}

	return (0);
}
#endif

#undef PCI_VPD_TIMEOUT

struct vpd_readstate {
	device_t	pcib;
	pcicfgregs	*cfg;
	uint32_t	val;
	int		bytesinval;
	int		off;
	uint8_t		cksum;
};

static int
vpd_nextbyte(struct vpd_readstate *vrs, uint8_t *data)
{
	uint32_t reg;
	uint8_t byte;

	if (vrs->bytesinval == 0) {
		if (pci_read_vpd_reg(vrs->pcib, vrs->cfg, vrs->off, &reg))
			return (ENXIO);
		vrs->val = le32toh(reg);
		vrs->off += 4;
		byte = vrs->val & 0xff;
		vrs->bytesinval = 3;
	} else {
		vrs->val = vrs->val >> 8;
		byte = vrs->val & 0xff;
		vrs->bytesinval--;
	}

	vrs->cksum += byte;
	*data = byte;
	return (0);
}

int
pcie_slot_implemented(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);

	return pcie_slotimpl(&dinfo->cfg);
}

void
pcie_set_max_readrq(device_t dev, uint16_t rqsize)
{
	uint8_t expr_ptr;
	uint16_t val;

	rqsize &= PCIEM_DEVCTL_MAX_READRQ_MASK;
	if (rqsize > PCIEM_DEVCTL_MAX_READRQ_4096) {
		panic("%s: invalid max read request size 0x%02x",
		      device_get_nameunit(dev), rqsize);
	}

	expr_ptr = pci_get_pciecap_ptr(dev);
	if (!expr_ptr)
		panic("%s: not PCIe device", device_get_nameunit(dev));

	val = pci_read_config(dev, expr_ptr + PCIER_DEVCTRL, 2);
	if ((val & PCIEM_DEVCTL_MAX_READRQ_MASK) != rqsize) {
		if (bootverbose)
			device_printf(dev, "adjust device control 0x%04x", val);

		val &= ~PCIEM_DEVCTL_MAX_READRQ_MASK;
		val |= rqsize;
		pci_write_config(dev, expr_ptr + PCIER_DEVCTRL, val, 2);

		if (bootverbose)
			kprintf(" -> 0x%04x\n", val);
	}
}

uint16_t
pcie_get_max_readrq(device_t dev)
{
	uint8_t expr_ptr;
	uint16_t val;

	expr_ptr = pci_get_pciecap_ptr(dev);
	if (!expr_ptr)
		panic("%s: not PCIe device", device_get_nameunit(dev));

	val = pci_read_config(dev, expr_ptr + PCIER_DEVCTRL, 2);
	return (val & PCIEM_DEVCTL_MAX_READRQ_MASK);
}

static void
pci_read_vpd(device_t pcib, pcicfgregs *cfg)
{
	struct vpd_readstate vrs;
	int state;
	int name;
	int remain;
	int i;
	int alloc, off;		/* alloc/off for RO/W arrays */
	int cksumvalid;
	int dflen;
	uint8_t byte;
	uint8_t byte2;

	/* init vpd reader */
	vrs.bytesinval = 0;
	vrs.off = 0;
	vrs.pcib = pcib;
	vrs.cfg = cfg;
	vrs.cksum = 0;

	state = 0;
	name = remain = i = 0;	/* shut up stupid gcc */
	alloc = off = 0;	/* shut up stupid gcc */
	dflen = 0;		/* shut up stupid gcc */
	cksumvalid = -1;
	while (state >= 0) {
		if (vpd_nextbyte(&vrs, &byte)) {
			state = -2;
			break;
		}
#if 0
		kprintf("vpd: val: %#x, off: %d, bytesinval: %d, byte: %#hhx, " \
		    "state: %d, remain: %d, name: %#x, i: %d\n", vrs.val,
		    vrs.off, vrs.bytesinval, byte, state, remain, name, i);
#endif
		switch (state) {
		case 0:		/* item name */
			if (byte & 0x80) {
				if (vpd_nextbyte(&vrs, &byte2)) {
					state = -2;
					break;
				}
				remain = byte2;
				if (vpd_nextbyte(&vrs, &byte2)) {
					state = -2;
					break;
				}
				remain |= byte2 << 8;
				if (remain > (0x7f*4 - vrs.off)) {
					state = -1;
					kprintf(
			    "pci%d:%d:%d:%d: invalid VPD data, remain %#x\n",
					    cfg->domain, cfg->bus, cfg->slot,
					    cfg->func, remain);
				}
				name = byte & 0x7f;
			} else {
				remain = byte & 0x7;
				name = (byte >> 3) & 0xf;
			}
			switch (name) {
			case 0x2:	/* String */
				cfg->vpd.vpd_ident = kmalloc(remain + 1,
				    M_DEVBUF, M_WAITOK);
				i = 0;
				state = 1;
				break;
			case 0xf:	/* End */
				state = -1;
				break;
			case 0x10:	/* VPD-R */
				alloc = 8;
				off = 0;
				cfg->vpd.vpd_ros = kmalloc(alloc *
				    sizeof(*cfg->vpd.vpd_ros), M_DEVBUF,
				    M_WAITOK | M_ZERO);
				state = 2;
				break;
			case 0x11:	/* VPD-W */
				alloc = 8;
				off = 0;
				cfg->vpd.vpd_w = kmalloc(alloc *
				    sizeof(*cfg->vpd.vpd_w), M_DEVBUF,
				    M_WAITOK | M_ZERO);
				state = 5;
				break;
			default:	/* Invalid data, abort */
				state = -1;
				break;
			}
			break;

		case 1:	/* Identifier String */
			cfg->vpd.vpd_ident[i++] = byte;
			remain--;
			if (remain == 0)  {
				cfg->vpd.vpd_ident[i] = '\0';
				state = 0;
			}
			break;

		case 2:	/* VPD-R Keyword Header */
			if (off == alloc) {
				cfg->vpd.vpd_ros = krealloc(cfg->vpd.vpd_ros,
				    (alloc *= 2) * sizeof(*cfg->vpd.vpd_ros),
				    M_DEVBUF, M_WAITOK | M_ZERO);
			}
			cfg->vpd.vpd_ros[off].keyword[0] = byte;
			if (vpd_nextbyte(&vrs, &byte2)) {
				state = -2;
				break;
			}
			cfg->vpd.vpd_ros[off].keyword[1] = byte2;
			if (vpd_nextbyte(&vrs, &byte2)) {
				state = -2;
				break;
			}
			dflen = byte2;
			if (dflen == 0 &&
			    strncmp(cfg->vpd.vpd_ros[off].keyword, "RV",
			    2) == 0) {
				/*
				 * if this happens, we can't trust the rest
				 * of the VPD.
				 */
				kprintf(
				    "pci%d:%d:%d:%d: bad keyword length: %d\n",
				    cfg->domain, cfg->bus, cfg->slot,
				    cfg->func, dflen);
				cksumvalid = 0;
				state = -1;
				break;
			} else if (dflen == 0) {
				cfg->vpd.vpd_ros[off].value = kmalloc(1 *
				    sizeof(*cfg->vpd.vpd_ros[off].value),
				    M_DEVBUF, M_WAITOK);
				cfg->vpd.vpd_ros[off].value[0] = '\x00';
			} else
				cfg->vpd.vpd_ros[off].value = kmalloc(
				    (dflen + 1) *
				    sizeof(*cfg->vpd.vpd_ros[off].value),
				    M_DEVBUF, M_WAITOK);
			remain -= 3;
			i = 0;
			/* keep in sync w/ state 3's transistions */
			if (dflen == 0 && remain == 0)
				state = 0;
			else if (dflen == 0)
				state = 2;
			else
				state = 3;
			break;

		case 3:	/* VPD-R Keyword Value */
			cfg->vpd.vpd_ros[off].value[i++] = byte;
			if (strncmp(cfg->vpd.vpd_ros[off].keyword,
			    "RV", 2) == 0 && cksumvalid == -1) {
				if (vrs.cksum == 0)
					cksumvalid = 1;
				else {
					if (bootverbose)
						kprintf(
				"pci%d:%d:%d:%d: bad VPD cksum, remain %hhu\n",
						    cfg->domain, cfg->bus,
						    cfg->slot, cfg->func,
						    vrs.cksum);
					cksumvalid = 0;
					state = -1;
					break;
				}
			}
			dflen--;
			remain--;
			/* keep in sync w/ state 2's transistions */
			if (dflen == 0)
				cfg->vpd.vpd_ros[off++].value[i++] = '\0';
			if (dflen == 0 && remain == 0) {
				cfg->vpd.vpd_rocnt = off;
				cfg->vpd.vpd_ros = krealloc(cfg->vpd.vpd_ros,
				    off * sizeof(*cfg->vpd.vpd_ros),
				    M_DEVBUF, M_WAITOK | M_ZERO);
				state = 0;
			} else if (dflen == 0)
				state = 2;
			break;

		case 4:
			remain--;
			if (remain == 0)
				state = 0;
			break;

		case 5:	/* VPD-W Keyword Header */
			if (off == alloc) {
				cfg->vpd.vpd_w = krealloc(cfg->vpd.vpd_w,
				    (alloc *= 2) * sizeof(*cfg->vpd.vpd_w),
				    M_DEVBUF, M_WAITOK | M_ZERO);
			}
			cfg->vpd.vpd_w[off].keyword[0] = byte;
			if (vpd_nextbyte(&vrs, &byte2)) {
				state = -2;
				break;
			}
			cfg->vpd.vpd_w[off].keyword[1] = byte2;
			if (vpd_nextbyte(&vrs, &byte2)) {
				state = -2;
				break;
			}
			cfg->vpd.vpd_w[off].len = dflen = byte2;
			cfg->vpd.vpd_w[off].start = vrs.off - vrs.bytesinval;
			cfg->vpd.vpd_w[off].value = kmalloc((dflen + 1) *
			    sizeof(*cfg->vpd.vpd_w[off].value),
			    M_DEVBUF, M_WAITOK);
			remain -= 3;
			i = 0;
			/* keep in sync w/ state 6's transistions */
			if (dflen == 0 && remain == 0)
				state = 0;
			else if (dflen == 0)
				state = 5;
			else
				state = 6;
			break;

		case 6:	/* VPD-W Keyword Value */
			cfg->vpd.vpd_w[off].value[i++] = byte;
			dflen--;
			remain--;
			/* keep in sync w/ state 5's transistions */
			if (dflen == 0)
				cfg->vpd.vpd_w[off++].value[i++] = '\0';
			if (dflen == 0 && remain == 0) {
				cfg->vpd.vpd_wcnt = off;
				cfg->vpd.vpd_w = krealloc(cfg->vpd.vpd_w,
				    off * sizeof(*cfg->vpd.vpd_w),
				    M_DEVBUF, M_WAITOK | M_ZERO);
				state = 0;
			} else if (dflen == 0)
				state = 5;
			break;

		default:
			kprintf("pci%d:%d:%d:%d: invalid state: %d\n",
			    cfg->domain, cfg->bus, cfg->slot, cfg->func,
			    state);
			state = -1;
			break;
		}
	}

	if (cksumvalid == 0 || state < -1) {
		/* read-only data bad, clean up */
		if (cfg->vpd.vpd_ros != NULL) {
			for (off = 0; cfg->vpd.vpd_ros[off].value; off++)
				kfree(cfg->vpd.vpd_ros[off].value, M_DEVBUF);
			kfree(cfg->vpd.vpd_ros, M_DEVBUF);
			cfg->vpd.vpd_ros = NULL;
		}
	}
	if (state < -1) {
		/* I/O error, clean up */
		kprintf("pci%d:%d:%d:%d: failed to read VPD data.\n",
		    cfg->domain, cfg->bus, cfg->slot, cfg->func);
		if (cfg->vpd.vpd_ident != NULL) {
			kfree(cfg->vpd.vpd_ident, M_DEVBUF);
			cfg->vpd.vpd_ident = NULL;
		}
		if (cfg->vpd.vpd_w != NULL) {
			for (off = 0; cfg->vpd.vpd_w[off].value; off++)
				kfree(cfg->vpd.vpd_w[off].value, M_DEVBUF);
			kfree(cfg->vpd.vpd_w, M_DEVBUF);
			cfg->vpd.vpd_w = NULL;
		}
	}
	cfg->vpd.vpd_cached = 1;
#undef REG
#undef WREG
}

int
pci_get_vpd_ident_method(device_t dev, device_t child, const char **identptr)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;

	if (!cfg->vpd.vpd_cached && cfg->vpd.vpd_reg != 0)
		pci_read_vpd(device_get_parent(dev), cfg);

	*identptr = cfg->vpd.vpd_ident;

	if (*identptr == NULL)
		return (ENXIO);

	return (0);
}

int
pci_get_vpd_readonly_method(device_t dev, device_t child, const char *kw,
	const char **vptr)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;
	int i;

	if (!cfg->vpd.vpd_cached && cfg->vpd.vpd_reg != 0)
		pci_read_vpd(device_get_parent(dev), cfg);

	for (i = 0; i < cfg->vpd.vpd_rocnt; i++)
		if (memcmp(kw, cfg->vpd.vpd_ros[i].keyword,
		    sizeof(cfg->vpd.vpd_ros[i].keyword)) == 0) {
			*vptr = cfg->vpd.vpd_ros[i].value;
		}

	if (i != cfg->vpd.vpd_rocnt)
		return (0);

	*vptr = NULL;
	return (ENXIO);
}

/*
 * Return the offset in configuration space of the requested extended
 * capability entry or 0 if the specified capability was not found.
 */
int
pci_find_extcap_method(device_t dev, device_t child, int capability,
    int *capreg)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;
	u_int32_t status;
	u_int8_t ptr;

	/*
	 * Check the CAP_LIST bit of the PCI status register first.
	 */
	status = pci_read_config(child, PCIR_STATUS, 2);
	if (!(status & PCIM_STATUS_CAPPRESENT))
		return (ENXIO);

	/*
	 * Determine the start pointer of the capabilities list.
	 */
	switch (cfg->hdrtype & PCIM_HDRTYPE) {
	case 0:
	case 1:
		ptr = PCIR_CAP_PTR;
		break;
	case 2:
		ptr = PCIR_CAP_PTR_2;
		break;
	default:
		/* XXX: panic? */
		return (ENXIO);		/* no extended capabilities support */
	}
	ptr = pci_read_config(child, ptr, 1);

	/*
	 * Traverse the capabilities list.
	 */
	while (ptr != 0) {
		if (pci_read_config(child, ptr + PCICAP_ID, 1) == capability) {
			if (capreg != NULL)
				*capreg = ptr;
			return (0);
		}
		ptr = pci_read_config(child, ptr + PCICAP_NEXTPTR, 1);
	}

	return (ENOENT);
}

/*
 * Support for MSI-X message interrupts.
 */
static void
pci_setup_msix_vector(device_t dev, u_int index, uint64_t address,
    uint32_t data)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	uint32_t offset;

	KASSERT(msix->msix_msgnum > index, ("bogus index"));
	offset = msix->msix_table_offset + index * 16;
	bus_write_4(msix->msix_table_res, offset, address & 0xffffffff);
	bus_write_4(msix->msix_table_res, offset + 4, address >> 32);
	bus_write_4(msix->msix_table_res, offset + 8, data);

	/* Enable MSI -> HT mapping. */
	pci_ht_map_msi(dev, address);
}

static void
pci_mask_msix_vector(device_t dev, u_int index)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	uint32_t offset, val;

	KASSERT(msix->msix_msgnum > index, ("bogus index"));
	offset = msix->msix_table_offset + index * 16 + 12;
	val = bus_read_4(msix->msix_table_res, offset);
	if (!(val & PCIM_MSIX_VCTRL_MASK)) {
		val |= PCIM_MSIX_VCTRL_MASK;
		bus_write_4(msix->msix_table_res, offset, val);
	}
}

static void
pci_unmask_msix_vector(device_t dev, u_int index)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	uint32_t offset, val;

	KASSERT(msix->msix_msgnum > index, ("bogus index"));
	offset = msix->msix_table_offset + index * 16 + 12;
	val = bus_read_4(msix->msix_table_res, offset);
	if (val & PCIM_MSIX_VCTRL_MASK) {
		val &= ~PCIM_MSIX_VCTRL_MASK;
		bus_write_4(msix->msix_table_res, offset, val);
	}
}

int
pci_pending_msix_vector(device_t dev, u_int index)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	uint32_t offset, bit;

	KASSERT(msix->msix_table_res != NULL && msix->msix_pba_res != NULL,
	    ("MSI-X is not setup yet"));

	KASSERT(msix->msix_msgnum > index, ("bogus index"));
	offset = msix->msix_pba_offset + (index / 32) * 4;
	bit = 1 << index % 32;
	return (bus_read_4(msix->msix_pba_res, offset) & bit);
}

/*
 * Restore MSI-X registers and table during resume.  If MSI-X is
 * enabled then walk the virtual table to restore the actual MSI-X
 * table.
 */
static void
pci_resume_msix(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;

	if (msix->msix_table_res != NULL) {
		const struct msix_vector *mv;

		pci_mask_msix_allvectors(dev);

		TAILQ_FOREACH(mv, &msix->msix_vectors, mv_link) {
			u_int vector;

			if (mv->mv_address == 0)
				continue;

			vector = PCI_MSIX_RID2VEC(mv->mv_rid);
			pci_setup_msix_vector(dev, vector,
			    mv->mv_address, mv->mv_data);
			pci_unmask_msix_vector(dev, vector);
		}
	}
	pci_write_config(dev, msix->msix_location + PCIR_MSIX_CTRL,
	    msix->msix_ctrl, 2);
}

/*
 * Attempt to allocate one MSI-X message at the specified vector on cpuid.
 *
 * After this function returns, the MSI-X's rid will be saved in rid0.
 */
int
pci_alloc_msix_vector_method(device_t dev, device_t child, u_int vector,
    int *rid0, int cpuid)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	struct msix_vector *mv;
	struct resource_list_entry *rle;
	int error, irq, rid;

	KASSERT(msix->msix_table_res != NULL &&
	    msix->msix_pba_res != NULL, ("MSI-X is not setup yet"));
	KASSERT(cpuid >= 0 && cpuid < ncpus, ("invalid cpuid %d", cpuid));
	KASSERT(vector < msix->msix_msgnum,
	    ("invalid MSI-X vector %u, total %d", vector, msix->msix_msgnum));

	if (bootverbose) {
		device_printf(child,
		    "attempting to allocate MSI-X #%u vector (%d supported)\n",
		    vector, msix->msix_msgnum);
	}

	/* Set rid according to vector number */
	rid = PCI_MSIX_VEC2RID(vector);

	/* Vector has already been allocated */
	mv = pci_find_msix_vector(child, rid);
	if (mv != NULL)
		return EBUSY;

	/* Allocate a message. */
	error = PCIB_ALLOC_MSIX(device_get_parent(dev), child, &irq, cpuid);
	if (error)
		return error;
	resource_list_add(&dinfo->resources, SYS_RES_IRQ, rid,
	    irq, irq, 1, cpuid);

	if (bootverbose) {
		rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ, rid);
		device_printf(child, "using IRQ %lu for MSI-X on cpu%d\n",
		    rle->start, cpuid);
	}

	/* Update counts of alloc'd messages. */
	msix->msix_alloc++;

	mv = kmalloc(sizeof(*mv), M_DEVBUF, M_WAITOK | M_ZERO);
	mv->mv_rid = rid;
	TAILQ_INSERT_TAIL(&msix->msix_vectors, mv, mv_link);

	*rid0 = rid;
	return 0;
}

int
pci_release_msix_vector_method(device_t dev, device_t child, int rid)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	struct resource_list_entry *rle;
	struct msix_vector *mv;
	int irq, cpuid;

	KASSERT(msix->msix_table_res != NULL &&
	    msix->msix_pba_res != NULL, ("MSI-X is not setup yet"));
	KASSERT(msix->msix_alloc > 0, ("No MSI-X allocated"));
	KASSERT(rid > 0, ("invalid rid %d", rid));

	mv = pci_find_msix_vector(child, rid);
	KASSERT(mv != NULL, ("MSI-X rid %d is not allocated", rid));
	KASSERT(mv->mv_address == 0, ("MSI-X rid %d not teardown", rid));

	/* Make sure resource is no longer allocated. */
	rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ, rid);
	KASSERT(rle != NULL, ("missing MSI-X resource, rid %d", rid));
	KASSERT(rle->res == NULL,
	    ("MSI-X resource is still allocated, rid %d", rid));

	irq = rle->start;
	cpuid = rle->cpuid;

	/* Free the resource list entries. */
	resource_list_delete(&dinfo->resources, SYS_RES_IRQ, rid);

	/* Release the IRQ. */
	PCIB_RELEASE_MSIX(device_get_parent(dev), child, irq, cpuid);

	TAILQ_REMOVE(&msix->msix_vectors, mv, mv_link);
	kfree(mv, M_DEVBUF);

	msix->msix_alloc--;
	return (0);
}

/*
 * Return the max supported MSI-X messages this device supports.
 * Basically, assuming the MD code can alloc messages, this function
 * should return the maximum value that pci_alloc_msix() can return.
 * Thus, it is subject to the tunables, etc.
 */
int
pci_msix_count_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;

	if (pci_do_msix && msix->msix_location != 0)
		return (msix->msix_msgnum);
	return (0);
}

int
pci_setup_msix(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	pcicfgregs *cfg = &dinfo->cfg;
	struct resource_list_entry *rle;
	struct resource *table_res, *pba_res;

	KASSERT(cfg->msix.msix_table_res == NULL &&
	    cfg->msix.msix_pba_res == NULL, ("MSI-X has been setup yet"));

	/* If rid 0 is allocated, then fail. */
	rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ, 0);
	if (rle != NULL && rle->res != NULL)
		return (ENXIO);

	/* Already have allocated MSIs? */
	if (cfg->msi.msi_alloc != 0)
		return (ENXIO);

	/* If MSI is blacklisted for this system, fail. */
	if (pci_msi_blacklisted())
		return (ENXIO);

	/* MSI-X capability present? */
	if (cfg->msix.msix_location == 0 || cfg->msix.msix_msgnum == 0 ||
	    !pci_do_msix)
		return (ENODEV);

	KASSERT(cfg->msix.msix_alloc == 0 &&
	    TAILQ_EMPTY(&cfg->msix.msix_vectors),
	    ("MSI-X vector has been allocated"));

	/* Make sure the appropriate BARs are mapped. */
	rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY,
	    cfg->msix.msix_table_bar);
	if (rle == NULL || rle->res == NULL ||
	    !(rman_get_flags(rle->res) & RF_ACTIVE))
		return (ENXIO);
	table_res = rle->res;
	if (cfg->msix.msix_pba_bar != cfg->msix.msix_table_bar) {
		rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY,
		    cfg->msix.msix_pba_bar);
		if (rle == NULL || rle->res == NULL ||
		    !(rman_get_flags(rle->res) & RF_ACTIVE))
			return (ENXIO);
	}
	pba_res = rle->res;

	cfg->msix.msix_table_res = table_res;
	cfg->msix.msix_pba_res = pba_res;

	pci_mask_msix_allvectors(dev);

	return 0;
}

void
pci_teardown_msix(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;

	KASSERT(msix->msix_table_res != NULL &&
	    msix->msix_pba_res != NULL, ("MSI-X is not setup yet"));
	KASSERT(msix->msix_alloc == 0 && TAILQ_EMPTY(&msix->msix_vectors),
	    ("MSI-X vector is still allocated"));

	pci_mask_msix_allvectors(dev);

	msix->msix_table_res = NULL;
	msix->msix_pba_res = NULL;
}

void
pci_enable_msix(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;

	KASSERT(msix->msix_table_res != NULL &&
	    msix->msix_pba_res != NULL, ("MSI-X is not setup yet"));

	/* Update control register to enable MSI-X. */
	msix->msix_ctrl |= PCIM_MSIXCTRL_MSIX_ENABLE;
	pci_write_config(dev, msix->msix_location + PCIR_MSIX_CTRL,
	    msix->msix_ctrl, 2);
}

void
pci_disable_msix(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;

	KASSERT(msix->msix_table_res != NULL &&
	    msix->msix_pba_res != NULL, ("MSI-X is not setup yet"));

	/* Disable MSI -> HT mapping. */
	pci_ht_map_msi(dev, 0);

	/* Update control register to disable MSI-X. */
	msix->msix_ctrl &= ~PCIM_MSIXCTRL_MSIX_ENABLE;
	pci_write_config(dev, msix->msix_location + PCIR_MSIX_CTRL,
	    msix->msix_ctrl, 2);
}

static void
pci_mask_msix_allvectors(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	u_int i;

	for (i = 0; i < dinfo->cfg.msix.msix_msgnum; ++i)
		pci_mask_msix_vector(dev, i);
}

static struct msix_vector *
pci_find_msix_vector(device_t dev, int rid)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msix *msix = &dinfo->cfg.msix;
	struct msix_vector *mv;

	TAILQ_FOREACH(mv, &msix->msix_vectors, mv_link) {
		if (mv->mv_rid == rid)
			return mv;
	}
	return NULL;
}

/*
 * HyperTransport MSI mapping control
 */
void
pci_ht_map_msi(device_t dev, uint64_t addr)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_ht *ht = &dinfo->cfg.ht;

	if (!ht->ht_msimap)
		return;

	if (addr && !(ht->ht_msictrl & PCIM_HTCMD_MSI_ENABLE) &&
	    ht->ht_msiaddr >> 20 == addr >> 20) {
		/* Enable MSI -> HT mapping. */
		ht->ht_msictrl |= PCIM_HTCMD_MSI_ENABLE;
		pci_write_config(dev, ht->ht_msimap + PCIR_HT_COMMAND,
		    ht->ht_msictrl, 2);
	}

	if (!addr && (ht->ht_msictrl & PCIM_HTCMD_MSI_ENABLE)) {
		/* Disable MSI -> HT mapping. */
		ht->ht_msictrl &= ~PCIM_HTCMD_MSI_ENABLE;
		pci_write_config(dev, ht->ht_msimap + PCIR_HT_COMMAND,
		    ht->ht_msictrl, 2);
	}
}

/*
 * Support for MSI message signalled interrupts.
 */
void
pci_enable_msi(device_t dev, uint64_t address, uint16_t data)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;

	/* Write data and address values. */
	pci_write_config(dev, msi->msi_location + PCIR_MSI_ADDR,
	    address & 0xffffffff, 4);
	if (msi->msi_ctrl & PCIM_MSICTRL_64BIT) {
		pci_write_config(dev, msi->msi_location + PCIR_MSI_ADDR_HIGH,
		    address >> 32, 4);
		pci_write_config(dev, msi->msi_location + PCIR_MSI_DATA_64BIT,
		    data, 2);
	} else
		pci_write_config(dev, msi->msi_location + PCIR_MSI_DATA, data,
		    2);

	/* Enable MSI in the control register. */
	msi->msi_ctrl |= PCIM_MSICTRL_MSI_ENABLE;
	pci_write_config(dev, msi->msi_location + PCIR_MSI_CTRL, msi->msi_ctrl,
	    2);

	/* Enable MSI -> HT mapping. */
	pci_ht_map_msi(dev, address);
}

void
pci_disable_msi(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;

	/* Disable MSI -> HT mapping. */
	pci_ht_map_msi(dev, 0);

	/* Disable MSI in the control register. */
	msi->msi_ctrl &= ~PCIM_MSICTRL_MSI_ENABLE;
	pci_write_config(dev, msi->msi_location + PCIR_MSI_CTRL, msi->msi_ctrl,
	    2);
}

/*
 * Restore MSI registers during resume.  If MSI is enabled then
 * restore the data and address registers in addition to the control
 * register.
 */
static void
pci_resume_msi(device_t dev)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;
	uint64_t address;
	uint16_t data;

	if (msi->msi_ctrl & PCIM_MSICTRL_MSI_ENABLE) {
		address = msi->msi_addr;
		data = msi->msi_data;
		pci_write_config(dev, msi->msi_location + PCIR_MSI_ADDR,
		    address & 0xffffffff, 4);
		if (msi->msi_ctrl & PCIM_MSICTRL_64BIT) {
			pci_write_config(dev, msi->msi_location +
			    PCIR_MSI_ADDR_HIGH, address >> 32, 4);
			pci_write_config(dev, msi->msi_location +
			    PCIR_MSI_DATA_64BIT, data, 2);
		} else
			pci_write_config(dev, msi->msi_location + PCIR_MSI_DATA,
			    data, 2);
	}
	pci_write_config(dev, msi->msi_location + PCIR_MSI_CTRL, msi->msi_ctrl,
	    2);
}

/*
 * Returns true if the specified device is blacklisted because MSI
 * doesn't work.
 */
int
pci_msi_device_blacklisted(device_t dev)
{
	struct pci_quirk *q;

	if (!pci_honor_msi_blacklist)
		return (0);

	for (q = &pci_quirks[0]; q->devid; q++) {
		if (q->devid == pci_get_devid(dev) &&
		    q->type == PCI_QUIRK_DISABLE_MSI)
			return (1);
	}
	return (0);
}

/*
 * Determine if MSI is blacklisted globally on this sytem.  Currently,
 * we just check for blacklisted chipsets as represented by the
 * host-PCI bridge at device 0:0:0.  In the future, it may become
 * necessary to check other system attributes, such as the kenv values
 * that give the motherboard manufacturer and model number.
 */
static int
pci_msi_blacklisted(void)
{
	device_t dev;

	if (!pci_honor_msi_blacklist)
		return (0);

	/* Blacklist all non-PCI-express and non-PCI-X chipsets. */
	if (!(pcie_chipset || pcix_chipset))
		return (1);

	dev = pci_find_bsf(0, 0, 0);
	if (dev != NULL)
		return (pci_msi_device_blacklisted(dev));
	return (0);
}

/*
 * Attempt to allocate count MSI messages on start_cpuid.
 *
 * If start_cpuid < 0, then the MSI messages' target CPU will be
 * selected automaticly.
 *
 * If the caller explicitly specified the MSI messages' target CPU,
 * i.e. start_cpuid >= 0, then we will try to allocate the count MSI
 * messages on the specified CPU, if the allocation fails due to MD
 * does not have enough vectors (EMSGSIZE), then we will try next
 * available CPU, until the allocation fails on all CPUs.
 *
 * EMSGSIZE will be returned, if all available CPUs does not have
 * enough vectors for the requested amount of MSI messages.  Caller
 * should either reduce the amount of MSI messages to be requested,
 * or simply giving up using MSI.
 *
 * The available SYS_RES_IRQ resources' rids, which are >= 1, are
 * returned in 'rid' array, if the allocation succeeds.
 */
int
pci_alloc_msi_method(device_t dev, device_t child, int *rid, int count,
    int start_cpuid)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;
	struct resource_list_entry *rle;
	int error, i, irqs[32], cpuid = 0;
	uint16_t ctrl;

	KASSERT(count != 0 && count <= 32 && powerof2(count),
	    ("invalid MSI count %d", count));
	KASSERT(start_cpuid < ncpus, ("invalid cpuid %d", start_cpuid));

	/* If rid 0 is allocated, then fail. */
	rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ, 0);
	if (rle != NULL && rle->res != NULL)
		return (ENXIO);

	/* Already have allocated messages? */
	if (cfg->msi.msi_alloc != 0 || cfg->msix.msix_table_res != NULL)
		return (ENXIO);

	/* If MSI is blacklisted for this system, fail. */
	if (pci_msi_blacklisted())
		return (ENXIO);

	/* MSI capability present? */
	if (cfg->msi.msi_location == 0 || cfg->msi.msi_msgnum == 0 ||
	    !pci_do_msi)
		return (ENODEV);

	KASSERT(count <= cfg->msi.msi_msgnum, ("large MSI count %d, max %d",
	    count, cfg->msi.msi_msgnum));

	if (bootverbose) {
		device_printf(child,
		    "attempting to allocate %d MSI vector%s (%d supported)\n",
		    count, count > 1 ? "s" : "", cfg->msi.msi_msgnum);
	}

	if (start_cpuid < 0)
		start_cpuid = atomic_fetchadd_int(&pci_msi_cpuid, 1) % ncpus;

	error = EINVAL;
	for (i = 0; i < ncpus; ++i) {
		cpuid = (start_cpuid + i) % ncpus;

		error = PCIB_ALLOC_MSI(device_get_parent(dev), child, count,
		    cfg->msi.msi_msgnum, irqs, cpuid);
		if (error == 0)
			break;
		else if (error != EMSGSIZE)
			return error;
	}
	if (error)
		return error;

	/*
	 * We now have N messages mapped onto SYS_RES_IRQ resources in
	 * the irqs[] array, so add new resources starting at rid 1.
	 */
	for (i = 0; i < count; i++) {
		rid[i] = i + 1;
		resource_list_add(&dinfo->resources, SYS_RES_IRQ, i + 1,
		    irqs[i], irqs[i], 1, cpuid);
	}

	if (bootverbose) {
		if (count == 1) {
			device_printf(child, "using IRQ %d on cpu%d for MSI\n",
			    irqs[0], cpuid);
		} else {
			int run;

			/*
			 * Be fancy and try to print contiguous runs
			 * of IRQ values as ranges.  'run' is true if
			 * we are in a range.
			 */
			device_printf(child, "using IRQs %d", irqs[0]);
			run = 0;
			for (i = 1; i < count; i++) {

				/* Still in a run? */
				if (irqs[i] == irqs[i - 1] + 1) {
					run = 1;
					continue;
				}

				/* Finish previous range. */
				if (run) {
					kprintf("-%d", irqs[i - 1]);
					run = 0;
				}

				/* Start new range. */
				kprintf(",%d", irqs[i]);
			}

			/* Unfinished range? */
			if (run)
				kprintf("-%d", irqs[count - 1]);
			kprintf(" for MSI on cpu%d\n", cpuid);
		}
	}

	/* Update control register with count. */
	ctrl = cfg->msi.msi_ctrl;
	ctrl &= ~PCIM_MSICTRL_MME_MASK;
	ctrl |= (ffs(count) - 1) << 4;
	cfg->msi.msi_ctrl = ctrl;
	pci_write_config(child, cfg->msi.msi_location + PCIR_MSI_CTRL, ctrl, 2);

	/* Update counts of alloc'd messages. */
	cfg->msi.msi_alloc = count;
	cfg->msi.msi_handlers = 0;
	return (0);
}

/* Release the MSI messages associated with this device. */
int
pci_release_msi_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;
	struct resource_list_entry *rle;
	int i, irqs[32], cpuid = -1;

	/* Do we have any messages to release? */
	if (msi->msi_alloc == 0)
		return (ENODEV);
	KASSERT(msi->msi_alloc <= 32, ("more than 32 alloc'd messages"));

	/* Make sure none of the resources are allocated. */
	if (msi->msi_handlers > 0)
		return (EBUSY);
	for (i = 0; i < msi->msi_alloc; i++) {
		rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ, i + 1);
		KASSERT(rle != NULL, ("missing MSI resource"));
		if (rle->res != NULL)
			return (EBUSY);
		if (i == 0) {
			cpuid = rle->cpuid;
			KASSERT(cpuid >= 0 && cpuid < ncpus,
			    ("invalid MSI target cpuid %d", cpuid));
		} else {
			KASSERT(rle->cpuid == cpuid,
			    ("MSI targets different cpus, "
			     "was cpu%d, now cpu%d", cpuid, rle->cpuid));
		}
		irqs[i] = rle->start;
	}

	/* Update control register with 0 count. */
	KASSERT(!(msi->msi_ctrl & PCIM_MSICTRL_MSI_ENABLE),
	    ("%s: MSI still enabled", __func__));
	msi->msi_ctrl &= ~PCIM_MSICTRL_MME_MASK;
	pci_write_config(child, msi->msi_location + PCIR_MSI_CTRL,
	    msi->msi_ctrl, 2);

	/* Release the messages. */
	PCIB_RELEASE_MSI(device_get_parent(dev), child, msi->msi_alloc, irqs,
	    cpuid);
	for (i = 0; i < msi->msi_alloc; i++)
		resource_list_delete(&dinfo->resources, SYS_RES_IRQ, i + 1);

	/* Update alloc count. */
	msi->msi_alloc = 0;
	msi->msi_addr = 0;
	msi->msi_data = 0;
	return (0);
}

/*
 * Return the max supported MSI messages this device supports.
 * Basically, assuming the MD code can alloc messages, this function
 * should return the maximum value that pci_alloc_msi() can return.
 * Thus, it is subject to the tunables, etc.
 */
int
pci_msi_count_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;

	if (pci_do_msi && msi->msi_location != 0)
		return (msi->msi_msgnum);
	return (0);
}

/* kfree pcicfgregs structure and all depending data structures */

int
pci_freecfg(struct pci_devinfo *dinfo)
{
	struct devlist *devlist_head;
	int i;

	devlist_head = &pci_devq;

	if (dinfo->cfg.vpd.vpd_reg) {
		kfree(dinfo->cfg.vpd.vpd_ident, M_DEVBUF);
		for (i = 0; i < dinfo->cfg.vpd.vpd_rocnt; i++)
			kfree(dinfo->cfg.vpd.vpd_ros[i].value, M_DEVBUF);
		kfree(dinfo->cfg.vpd.vpd_ros, M_DEVBUF);
		for (i = 0; i < dinfo->cfg.vpd.vpd_wcnt; i++)
			kfree(dinfo->cfg.vpd.vpd_w[i].value, M_DEVBUF);
		kfree(dinfo->cfg.vpd.vpd_w, M_DEVBUF);
	}
	STAILQ_REMOVE(devlist_head, dinfo, pci_devinfo, pci_links);
	kfree(dinfo, M_DEVBUF);

	/* increment the generation count */
	pci_generation++;

	/* we're losing one device */
	pci_numdevs--;
	return (0);
}

/*
 * PCI power manangement
 */
int
pci_set_powerstate_method(device_t dev, device_t child, int state)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;
	uint16_t status;
	int oldstate, highest, delay;

	if (cfg->pp.pp_cap == 0)
		return (EOPNOTSUPP);

	/*
	 * Optimize a no state change request away.  While it would be OK to
	 * write to the hardware in theory, some devices have shown odd
	 * behavior when going from D3 -> D3.
	 */
	oldstate = pci_get_powerstate(child);
	if (oldstate == state)
		return (0);

	/*
	 * The PCI power management specification states that after a state
	 * transition between PCI power states, system software must
	 * guarantee a minimal delay before the function accesses the device.
	 * Compute the worst case delay that we need to guarantee before we
	 * access the device.  Many devices will be responsive much more
	 * quickly than this delay, but there are some that don't respond
	 * instantly to state changes.  Transitions to/from D3 state require
	 * 10ms, while D2 requires 200us, and D0/1 require none.  The delay
	 * is done below with DELAY rather than a sleeper function because
	 * this function can be called from contexts where we cannot sleep.
	 */
	highest = (oldstate > state) ? oldstate : state;
	if (highest == PCI_POWERSTATE_D3)
	    delay = 10000;
	else if (highest == PCI_POWERSTATE_D2)
	    delay = 200;
	else
	    delay = 0;
	status = PCI_READ_CONFIG(dev, child, cfg->pp.pp_status, 2)
	    & ~PCIM_PSTAT_DMASK;
	switch (state) {
	case PCI_POWERSTATE_D0:
		status |= PCIM_PSTAT_D0;
		break;
	case PCI_POWERSTATE_D1:
		if ((cfg->pp.pp_cap & PCIM_PCAP_D1SUPP) == 0)
			return (EOPNOTSUPP);
		status |= PCIM_PSTAT_D1;
		break;
	case PCI_POWERSTATE_D2:
		if ((cfg->pp.pp_cap & PCIM_PCAP_D2SUPP) == 0)
			return (EOPNOTSUPP);
		status |= PCIM_PSTAT_D2;
		break;
	case PCI_POWERSTATE_D3:
		status |= PCIM_PSTAT_D3;
		break;
	default:
		return (EINVAL);
	}

	if (bootverbose)
		kprintf(
		    "pci%d:%d:%d:%d: Transition from D%d to D%d\n",
		    dinfo->cfg.domain, dinfo->cfg.bus, dinfo->cfg.slot,
		    dinfo->cfg.func, oldstate, state);

	PCI_WRITE_CONFIG(dev, child, cfg->pp.pp_status, status, 2);
	if (delay)
		DELAY(delay);
	return (0);
}

int
pci_get_powerstate_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;
	uint16_t status;
	int result;

	if (cfg->pp.pp_cap != 0) {
		status = PCI_READ_CONFIG(dev, child, cfg->pp.pp_status, 2);
		switch (status & PCIM_PSTAT_DMASK) {
		case PCIM_PSTAT_D0:
			result = PCI_POWERSTATE_D0;
			break;
		case PCIM_PSTAT_D1:
			result = PCI_POWERSTATE_D1;
			break;
		case PCIM_PSTAT_D2:
			result = PCI_POWERSTATE_D2;
			break;
		case PCIM_PSTAT_D3:
			result = PCI_POWERSTATE_D3;
			break;
		default:
			result = PCI_POWERSTATE_UNKNOWN;
			break;
		}
	} else {
		/* No support, device is always at D0 */
		result = PCI_POWERSTATE_D0;
	}
	return (result);
}

/*
 * Some convenience functions for PCI device drivers.
 */

static __inline void
pci_set_command_bit(device_t dev, device_t child, uint16_t bit)
{
	uint16_t	command;

	command = PCI_READ_CONFIG(dev, child, PCIR_COMMAND, 2);
	command |= bit;
	PCI_WRITE_CONFIG(dev, child, PCIR_COMMAND, command, 2);
}

static __inline void
pci_clear_command_bit(device_t dev, device_t child, uint16_t bit)
{
	uint16_t	command;

	command = PCI_READ_CONFIG(dev, child, PCIR_COMMAND, 2);
	command &= ~bit;
	PCI_WRITE_CONFIG(dev, child, PCIR_COMMAND, command, 2);
}

int
pci_enable_busmaster_method(device_t dev, device_t child)
{
	pci_set_command_bit(dev, child, PCIM_CMD_BUSMASTEREN);
	return (0);
}

int
pci_disable_busmaster_method(device_t dev, device_t child)
{
	pci_clear_command_bit(dev, child, PCIM_CMD_BUSMASTEREN);
	return (0);
}

int
pci_enable_io_method(device_t dev, device_t child, int space)
{
	uint16_t command;
	uint16_t bit;
	char *error;

	bit = 0;
	error = NULL;

	switch(space) {
	case SYS_RES_IOPORT:
		bit = PCIM_CMD_PORTEN;
		error = "port";
		break;
	case SYS_RES_MEMORY:
		bit = PCIM_CMD_MEMEN;
		error = "memory";
		break;
	default:
		return (EINVAL);
	}
	pci_set_command_bit(dev, child, bit);
	/* Some devices seem to need a brief stall here, what do to? */
	command = PCI_READ_CONFIG(dev, child, PCIR_COMMAND, 2);
	if (command & bit)
		return (0);
	device_printf(child, "failed to enable %s mapping!\n", error);
	return (ENXIO);
}

int
pci_disable_io_method(device_t dev, device_t child, int space)
{
	uint16_t command;
	uint16_t bit;
	char *error;

	bit = 0;
	error = NULL;

	switch(space) {
	case SYS_RES_IOPORT:
		bit = PCIM_CMD_PORTEN;
		error = "port";
		break;
	case SYS_RES_MEMORY:
		bit = PCIM_CMD_MEMEN;
		error = "memory";
		break;
	default:
		return (EINVAL);
	}
	pci_clear_command_bit(dev, child, bit);
	command = PCI_READ_CONFIG(dev, child, PCIR_COMMAND, 2);
	if (command & bit) {
		device_printf(child, "failed to disable %s mapping!\n", error);
		return (ENXIO);
	}
	return (0);
}

/*
 * New style pci driver.  Parent device is either a pci-host-bridge or a
 * pci-pci-bridge.  Both kinds are represented by instances of pcib.
 */

void
pci_print_verbose(struct pci_devinfo *dinfo)
{

	if (bootverbose) {
		pcicfgregs *cfg = &dinfo->cfg;

		kprintf("found->\tvendor=0x%04x, dev=0x%04x, revid=0x%02x\n",
		    cfg->vendor, cfg->device, cfg->revid);
		kprintf("\tdomain=%d, bus=%d, slot=%d, func=%d\n",
		    cfg->domain, cfg->bus, cfg->slot, cfg->func);
		kprintf("\tclass=%02x-%02x-%02x, hdrtype=0x%02x, mfdev=%d\n",
		    cfg->baseclass, cfg->subclass, cfg->progif, cfg->hdrtype,
		    cfg->mfdev);
		kprintf("\tcmdreg=0x%04x, statreg=0x%04x, cachelnsz=%d (dwords)\n",
		    cfg->cmdreg, cfg->statreg, cfg->cachelnsz);
		kprintf("\tlattimer=0x%02x (%d ns), mingnt=0x%02x (%d ns), maxlat=0x%02x (%d ns)\n",
		    cfg->lattimer, cfg->lattimer * 30, cfg->mingnt,
		    cfg->mingnt * 250, cfg->maxlat, cfg->maxlat * 250);
		if (cfg->intpin > 0)
			kprintf("\tintpin=%c, irq=%d\n",
			    cfg->intpin +'a' -1, cfg->intline);
		if (cfg->pp.pp_cap) {
			uint16_t status;

			status = pci_read_config(cfg->dev, cfg->pp.pp_status, 2);
			kprintf("\tpowerspec %d  supports D0%s%s D3  current D%d\n",
			    cfg->pp.pp_cap & PCIM_PCAP_SPEC,
			    cfg->pp.pp_cap & PCIM_PCAP_D1SUPP ? " D1" : "",
			    cfg->pp.pp_cap & PCIM_PCAP_D2SUPP ? " D2" : "",
			    status & PCIM_PSTAT_DMASK);
		}
		if (cfg->msi.msi_location) {
			int ctrl;

			ctrl = cfg->msi.msi_ctrl;
			kprintf("\tMSI supports %d message%s%s%s\n",
			    cfg->msi.msi_msgnum,
			    (cfg->msi.msi_msgnum == 1) ? "" : "s",
			    (ctrl & PCIM_MSICTRL_64BIT) ? ", 64 bit" : "",
			    (ctrl & PCIM_MSICTRL_VECTOR) ? ", vector masks":"");
		}
		if (cfg->msix.msix_location) {
			kprintf("\tMSI-X supports %d message%s ",
			    cfg->msix.msix_msgnum,
			    (cfg->msix.msix_msgnum == 1) ? "" : "s");
			if (cfg->msix.msix_table_bar == cfg->msix.msix_pba_bar)
				kprintf("in map 0x%x\n",
				    cfg->msix.msix_table_bar);
			else
				kprintf("in maps 0x%x and 0x%x\n",
				    cfg->msix.msix_table_bar,
				    cfg->msix.msix_pba_bar);
		}
		pci_print_verbose_expr(cfg);
	}
}

static void
pci_print_verbose_expr(const pcicfgregs *cfg)
{
	const struct pcicfg_expr *expr = &cfg->expr;
	const char *port_name;
	uint16_t port_type;

	if (!bootverbose)
		return;

	if (expr->expr_ptr == 0) /* No PCI Express capability */
		return;

	kprintf("\tPCI Express ver.%d cap=0x%04x",
		expr->expr_cap & PCIEM_CAP_VER_MASK, expr->expr_cap);

	port_type = expr->expr_cap & PCIEM_CAP_PORT_TYPE;

	switch (port_type) {
	case PCIE_END_POINT:
		port_name = "DEVICE";
		break;
	case PCIE_LEG_END_POINT:
		port_name = "LEGDEV";
		break;
	case PCIE_ROOT_PORT:
		port_name = "ROOT";
		break;
	case PCIE_UP_STREAM_PORT:
		port_name = "UPSTREAM";
		break;
	case PCIE_DOWN_STREAM_PORT:
		port_name = "DOWNSTRM";
		break;
	case PCIE_PCIE2PCI_BRIDGE:
		port_name = "PCIE2PCI";
		break;
	case PCIE_PCI2PCIE_BRIDGE:
		port_name = "PCI2PCIE";
		break;
	case PCIE_ROOT_END_POINT:
		port_name = "ROOTDEV";
		break;
	case PCIE_ROOT_EVT_COLL:
		port_name = "ROOTEVTC";
		break;
	default:
		port_name = NULL;
		break;
	}
	if ((port_type == PCIE_ROOT_PORT ||
	     port_type == PCIE_DOWN_STREAM_PORT) &&
	    !(expr->expr_cap & PCIEM_CAP_SLOT_IMPL))
		port_name = NULL;
	if (port_name != NULL)
		kprintf("[%s]", port_name);

	if (pcie_slotimpl(cfg)) {
		kprintf(", slotcap=0x%08x", expr->expr_slotcap);
		if (expr->expr_slotcap & PCIEM_SLTCAP_HP_CAP)
			kprintf("[HOTPLUG]");
	}
	kprintf("\n");
}

static int
pci_porten(device_t pcib, int b, int s, int f)
{
	return (PCIB_READ_CONFIG(pcib, b, s, f, PCIR_COMMAND, 2)
		& PCIM_CMD_PORTEN) != 0;
}

static int
pci_memen(device_t pcib, int b, int s, int f)
{
	return (PCIB_READ_CONFIG(pcib, b, s, f, PCIR_COMMAND, 2)
		& PCIM_CMD_MEMEN) != 0;
}

/*
 * Add a resource based on a pci map register. Return 1 if the map
 * register is a 32bit map register or 2 if it is a 64bit register.
 */
static int
pci_add_map(device_t pcib, device_t bus, device_t dev,
    int b, int s, int f, int reg, struct resource_list *rl, int force,
    int prefetch)
{
	uint32_t map;
	uint16_t old_cmd;
	pci_addr_t base;
	pci_addr_t start, end, count;
	uint8_t ln2size;
	uint8_t ln2range;
	uint32_t testval;
	uint16_t cmd;
	int type;
	int barlen;
	struct resource *res;

	map = PCIB_READ_CONFIG(pcib, b, s, f, reg, 4);

        /* Disable access to device memory */
	old_cmd = 0;
	if (PCI_BAR_MEM(map)) {
		old_cmd = PCIB_READ_CONFIG(pcib, b, s, f, PCIR_COMMAND, 2);
		cmd = old_cmd & ~PCIM_CMD_MEMEN;
		PCIB_WRITE_CONFIG(pcib, b, s, f, PCIR_COMMAND, cmd, 2);
	}

	PCIB_WRITE_CONFIG(pcib, b, s, f, reg, 0xffffffff, 4);
	testval = PCIB_READ_CONFIG(pcib, b, s, f, reg, 4);
	PCIB_WRITE_CONFIG(pcib, b, s, f, reg, map, 4);

        /* Restore memory access mode */
	if (PCI_BAR_MEM(map)) {
		PCIB_WRITE_CONFIG(pcib, b, s, f, PCIR_COMMAND, old_cmd, 2);
	}

	if (PCI_BAR_MEM(map)) {
		type = SYS_RES_MEMORY;
		if (map & PCIM_BAR_MEM_PREFETCH)
			prefetch = 1;
	} else
		type = SYS_RES_IOPORT;
	ln2size = pci_mapsize(testval);
	ln2range = pci_maprange(testval);
	base = pci_mapbase(map);
	barlen = ln2range == 64 ? 2 : 1;

	/*
	 * For I/O registers, if bottom bit is set, and the next bit up
	 * isn't clear, we know we have a BAR that doesn't conform to the
	 * spec, so ignore it.  Also, sanity check the size of the data
	 * areas to the type of memory involved.  Memory must be at least
	 * 16 bytes in size, while I/O ranges must be at least 4.
	 */
	if (PCI_BAR_IO(testval) && (testval & PCIM_BAR_IO_RESERVED) != 0)
		return (barlen);
	if ((type == SYS_RES_MEMORY && ln2size < 4) ||
	    (type == SYS_RES_IOPORT && ln2size < 2))
		return (barlen);

	if (ln2range == 64)
		/* Read the other half of a 64bit map register */
		base |= (uint64_t) PCIB_READ_CONFIG(pcib, b, s, f, reg + 4, 4) << 32;
	if (bootverbose) {
		kprintf("\tmap[%02x]: type %s, range %2d, base %#jx, size %2d",
		    reg, pci_maptype(map), ln2range, (uintmax_t)base, ln2size);
		if (type == SYS_RES_IOPORT && !pci_porten(pcib, b, s, f))
			kprintf(", port disabled\n");
		else if (type == SYS_RES_MEMORY && !pci_memen(pcib, b, s, f))
			kprintf(", memory disabled\n");
		else
			kprintf(", enabled\n");
	}

	/*
	 * If base is 0, then we have problems.  It is best to ignore
	 * such entries for the moment.  These will be allocated later if
	 * the driver specifically requests them.  However, some
	 * removable busses look better when all resources are allocated,
	 * so allow '0' to be overriden.
	 *
	 * Similarly treat maps whose values is the same as the test value
	 * read back.  These maps have had all f's written to them by the
	 * BIOS in an attempt to disable the resources.
	 */
	if (!force && (base == 0 || map == testval))
		return (barlen);
	if ((u_long)base != base) {
		device_printf(bus,
		    "pci%d:%d:%d:%d bar %#x too many address bits",
		    pci_get_domain(dev), b, s, f, reg);
		return (barlen);
	}

	/*
	 * This code theoretically does the right thing, but has
	 * undesirable side effects in some cases where peripherals
	 * respond oddly to having these bits enabled.  Let the user
	 * be able to turn them off (since pci_enable_io_modes is 1 by
	 * default).
	 */
	if (pci_enable_io_modes) {
		/* Turn on resources that have been left off by a lazy BIOS */
		if (type == SYS_RES_IOPORT && !pci_porten(pcib, b, s, f)) {
			cmd = PCIB_READ_CONFIG(pcib, b, s, f, PCIR_COMMAND, 2);
			cmd |= PCIM_CMD_PORTEN;
			PCIB_WRITE_CONFIG(pcib, b, s, f, PCIR_COMMAND, cmd, 2);
		}
		if (type == SYS_RES_MEMORY && !pci_memen(pcib, b, s, f)) {
			cmd = PCIB_READ_CONFIG(pcib, b, s, f, PCIR_COMMAND, 2);
			cmd |= PCIM_CMD_MEMEN;
			PCIB_WRITE_CONFIG(pcib, b, s, f, PCIR_COMMAND, cmd, 2);
		}
	} else {
		if (type == SYS_RES_IOPORT && !pci_porten(pcib, b, s, f))
			return (barlen);
		if (type == SYS_RES_MEMORY && !pci_memen(pcib, b, s, f))
			return (barlen);
	}

	count = 1 << ln2size;
	if (base == 0 || base == pci_mapbase(testval)) {
		start = 0;	/* Let the parent decide. */
		end = ~0ULL;
	} else {
		start = base;
		end = base + (1 << ln2size) - 1;
	}
	resource_list_add(rl, type, reg, start, end, count, -1);

	/*
	 * Try to allocate the resource for this BAR from our parent
	 * so that this resource range is already reserved.  The
	 * driver for this device will later inherit this resource in
	 * pci_alloc_resource().
	 */
	res = resource_list_alloc(rl, bus, dev, type, &reg, start, end, count,
	    prefetch ? RF_PREFETCHABLE : 0, -1);
	if (res == NULL) {
		/*
		 * If the allocation fails, delete the resource list
		 * entry to force pci_alloc_resource() to allocate
		 * resources from the parent.
		 */
		resource_list_delete(rl, type, reg);
#ifdef PCI_BAR_CLEAR
		/* Clear the BAR */
		start = 0;
#else	/* !PCI_BAR_CLEAR */
		/*
		 * Don't clear BAR here.  Some BIOS lists HPET as a
		 * PCI function, clearing the BAR causes HPET timer
		 * stop ticking.
		 */
		if (bootverbose) {
			kprintf("pci:%d:%d:%d: resource reservation failed "
				"%#jx - %#jx\n", b, s, f,
				(intmax_t)start, (intmax_t)end);
		}
		return (barlen);
#endif	/* PCI_BAR_CLEAR */
	} else {
		start = rman_get_start(res);
	}
	pci_write_config(dev, reg, start, 4);
	if (ln2range == 64)
		pci_write_config(dev, reg + 4, start >> 32, 4);
	return (barlen);
}

/*
 * For ATA devices we need to decide early what addressing mode to use.
 * Legacy demands that the primary and secondary ATA ports sits on the
 * same addresses that old ISA hardware did. This dictates that we use
 * those addresses and ignore the BAR's if we cannot set PCI native
 * addressing mode.
 */
static void
pci_ata_maps(device_t pcib, device_t bus, device_t dev, int b,
    int s, int f, struct resource_list *rl, int force, uint32_t prefetchmask)
{
	int rid, type, progif;
#if 0
	/* if this device supports PCI native addressing use it */
	progif = pci_read_config(dev, PCIR_PROGIF, 1);
	if ((progif & 0x8a) == 0x8a) {
		if (pci_mapbase(pci_read_config(dev, PCIR_BAR(0), 4)) &&
		    pci_mapbase(pci_read_config(dev, PCIR_BAR(2), 4))) {
			kprintf("Trying ATA native PCI addressing mode\n");
			pci_write_config(dev, PCIR_PROGIF, progif | 0x05, 1);
		}
	}
#endif
	progif = pci_read_config(dev, PCIR_PROGIF, 1);
	type = SYS_RES_IOPORT;
	if (progif & PCIP_STORAGE_IDE_MODEPRIM) {
		pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(0), rl, force,
		    prefetchmask & (1 << 0));
		pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(1), rl, force,
		    prefetchmask & (1 << 1));
	} else {
		rid = PCIR_BAR(0);
		resource_list_add(rl, type, rid, 0x1f0, 0x1f7, 8, -1);
		resource_list_alloc(rl, bus, dev, type, &rid, 0x1f0, 0x1f7, 8,
		    0, -1);
		rid = PCIR_BAR(1);
		resource_list_add(rl, type, rid, 0x3f6, 0x3f6, 1, -1);
		resource_list_alloc(rl, bus, dev, type, &rid, 0x3f6, 0x3f6, 1,
		    0, -1);
	}
	if (progif & PCIP_STORAGE_IDE_MODESEC) {
		pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(2), rl, force,
		    prefetchmask & (1 << 2));
		pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(3), rl, force,
		    prefetchmask & (1 << 3));
	} else {
		rid = PCIR_BAR(2);
		resource_list_add(rl, type, rid, 0x170, 0x177, 8, -1);
		resource_list_alloc(rl, bus, dev, type, &rid, 0x170, 0x177, 8,
		    0, -1);
		rid = PCIR_BAR(3);
		resource_list_add(rl, type, rid, 0x376, 0x376, 1, -1);
		resource_list_alloc(rl, bus, dev, type, &rid, 0x376, 0x376, 1,
		    0, -1);
	}
	pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(4), rl, force,
	    prefetchmask & (1 << 4));
	pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(5), rl, force,
	    prefetchmask & (1 << 5));
}

static void
pci_assign_interrupt(device_t bus, device_t dev, int force_route)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	pcicfgregs *cfg = &dinfo->cfg;
	char tunable_name[64];
	int irq;

	/* Has to have an intpin to have an interrupt. */
	if (cfg->intpin == 0)
		return;

	/* Let the user override the IRQ with a tunable. */
	irq = PCI_INVALID_IRQ;
	ksnprintf(tunable_name, sizeof(tunable_name),
	    "hw.pci%d.%d.%d.%d.INT%c.irq",
	    cfg->domain, cfg->bus, cfg->slot, cfg->func, cfg->intpin + 'A' - 1);
	if (TUNABLE_INT_FETCH(tunable_name, &irq)) {
		if (irq >= 255 || irq <= 0) {
			irq = PCI_INVALID_IRQ;
		} else {
			if (machintr_legacy_intr_find(irq,
			    INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW) < 0) {
				device_printf(dev,
				    "hw.pci%d.%d.%d.%d.INT%c.irq=%d, invalid\n",
				    cfg->domain, cfg->bus, cfg->slot, cfg->func,
				    cfg->intpin + 'A' - 1, irq);
				irq = PCI_INVALID_IRQ;
			} else {
				BUS_CONFIG_INTR(bus, dev, irq,
				    INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
			}
		}
	}

	/*
	 * If we didn't get an IRQ via the tunable, then we either use the
	 * IRQ value in the intline register or we ask the bus to route an
	 * interrupt for us.  If force_route is true, then we only use the
	 * value in the intline register if the bus was unable to assign an
	 * IRQ.
	 */
	if (!PCI_INTERRUPT_VALID(irq)) {
		if (!PCI_INTERRUPT_VALID(cfg->intline) || force_route)
			irq = PCI_ASSIGN_INTERRUPT(bus, dev);
		if (!PCI_INTERRUPT_VALID(irq))
			irq = cfg->intline;
	}

	/* If after all that we don't have an IRQ, just bail. */
	if (!PCI_INTERRUPT_VALID(irq))
		return;

	/* Update the config register if it changed. */
	if (irq != cfg->intline) {
		cfg->intline = irq;
		pci_write_config(dev, PCIR_INTLINE, irq, 1);
	}

	/* Add this IRQ as rid 0 interrupt resource. */
	resource_list_add(&dinfo->resources, SYS_RES_IRQ, 0, irq, irq, 1,
	    machintr_legacy_intr_cpuid(irq));
}

/* Perform early OHCI takeover from SMM. */
static void
ohci_early_takeover(device_t self)
{
	struct resource *res;
	uint32_t ctl;
	int rid;
	int i;

	rid = PCIR_BAR(0);
	res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res == NULL)
		return;

	ctl = bus_read_4(res, OHCI_CONTROL);
	if (ctl & OHCI_IR) {
		if (bootverbose)
			kprintf("ohci early: "
			    "SMM active, request owner change\n");
		bus_write_4(res, OHCI_COMMAND_STATUS, OHCI_OCR);
		for (i = 0; (i < 100) && (ctl & OHCI_IR); i++) {
			DELAY(1000);
			ctl = bus_read_4(res, OHCI_CONTROL);
		}
		if (ctl & OHCI_IR) {
			if (bootverbose)
				kprintf("ohci early: "
				    "SMM does not respond, resetting\n");
			bus_write_4(res, OHCI_CONTROL, OHCI_HCFS_RESET);
		}
		/* Disable interrupts */
		bus_write_4(res, OHCI_INTERRUPT_DISABLE, OHCI_ALL_INTRS);
	}

	bus_release_resource(self, SYS_RES_MEMORY, rid, res);
}

/* Perform early UHCI takeover from SMM. */
static void
uhci_early_takeover(device_t self)
{
	struct resource *res;
	int rid;

	/*
	 * Set the PIRQD enable bit and switch off all the others. We don't
	 * want legacy support to interfere with us XXX Does this also mean
	 * that the BIOS won't touch the keyboard anymore if it is connected
	 * to the ports of the root hub?
	 */
	pci_write_config(self, PCI_LEGSUP, PCI_LEGSUP_USBPIRQDEN, 2);

	/* Disable interrupts */
	rid = PCI_UHCI_BASE_REG;
	res = bus_alloc_resource_any(self, SYS_RES_IOPORT, &rid, RF_ACTIVE);
	if (res != NULL) {
		bus_write_2(res, UHCI_INTR, 0);
		bus_release_resource(self, SYS_RES_IOPORT, rid, res);
	}
}

/* Perform early EHCI takeover from SMM. */
static void
ehci_early_takeover(device_t self)
{
	struct resource *res;
	uint32_t cparams;
	uint32_t eec;
	uint32_t eecp;
	uint32_t bios_sem;
	uint32_t offs;
	int rid;
	int i;

	rid = PCIR_BAR(0);
	res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res == NULL)
		return;

	cparams = bus_read_4(res, EHCI_HCCPARAMS);

	/* Synchronise with the BIOS if it owns the controller. */
	for (eecp = EHCI_HCC_EECP(cparams); eecp != 0;
	    eecp = EHCI_EECP_NEXT(eec)) {
		eec = pci_read_config(self, eecp, 4);
		if (EHCI_EECP_ID(eec) != EHCI_EC_LEGSUP) {
			continue;
		}
		bios_sem = pci_read_config(self, eecp +
		    EHCI_LEGSUP_BIOS_SEM, 1);
		if (bios_sem == 0) {
			continue;
		}
		if (bootverbose)
			kprintf("ehci early: "
			    "SMM active, request owner change\n");

		pci_write_config(self, eecp + EHCI_LEGSUP_OS_SEM, 1, 1);

		for (i = 0; (i < 100) && (bios_sem != 0); i++) {
			DELAY(1000);
			bios_sem = pci_read_config(self, eecp +
			    EHCI_LEGSUP_BIOS_SEM, 1);
		}

		if (bios_sem != 0) {
			if (bootverbose)
				kprintf("ehci early: "
				    "SMM does not respond\n");
		}
		/* Disable interrupts */
		offs = EHCI_CAPLENGTH(bus_read_4(res, EHCI_CAPLEN_HCIVERSION));
		bus_write_4(res, offs + EHCI_USBINTR, 0);
	}
	bus_release_resource(self, SYS_RES_MEMORY, rid, res);
}

/* Perform early XHCI takeover from SMM. */
static void
xhci_early_takeover(device_t self)
{
	struct resource *res;
	uint32_t cparams;
	uint32_t eec;
	uint32_t eecp;
	uint32_t bios_sem;
	uint32_t offs;
	int rid;
	int i;

	rid = PCIR_BAR(0);
	res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res == NULL)
		return;

	cparams = bus_read_4(res, XHCI_HCSPARAMS0);

	eec = -1;

	/* Synchronise with the BIOS if it owns the controller. */
	for (eecp = XHCI_HCS0_XECP(cparams) << 2; eecp != 0 && XHCI_XECP_NEXT(eec);
	    eecp += XHCI_XECP_NEXT(eec) << 2) {
		eec = bus_read_4(res, eecp);

		if (XHCI_XECP_ID(eec) != XHCI_ID_USB_LEGACY)
			continue;

		bios_sem = bus_read_1(res, eecp + XHCI_XECP_BIOS_SEM);

		if (bios_sem == 0) {
			if (bootverbose) 
				kprintf("xhci early: xhci is not owned by SMM\n");
			
			continue;
		}

		if (bootverbose)
			kprintf("xhci early: "
			    "SMM active, request owner change\n");

		bus_write_1(res, eecp + XHCI_XECP_OS_SEM, 1);

		/* wait a maximum of 5 seconds */

		for (i = 0; (i < 5000) && (bios_sem != 0); i++) {
			DELAY(1000);

			bios_sem = bus_read_1(res, eecp +
			    XHCI_XECP_BIOS_SEM);
		}

		if (bios_sem != 0) {
			if (bootverbose) {
				kprintf("xhci early: "
				    "SMM does not respond\n");
				kprintf("xhci early: "
				    "taking xhci by force\n");
			}
			bus_write_1(res, eecp + XHCI_XECP_BIOS_SEM, 0x00);
		} else {
			if (bootverbose) 
				kprintf("xhci early:"
				    "handover successful\n");
		}
	
		/* Disable interrupts */
		offs = bus_read_1(res, XHCI_CAPLENGTH);
		bus_write_4(res, offs + XHCI_USBCMD, 0);
		bus_read_4(res, offs + XHCI_USBSTS);
	}
	bus_release_resource(self, SYS_RES_MEMORY, rid, res);
}

void
pci_add_resources(device_t pcib, device_t bus, device_t dev, int force, uint32_t prefetchmask)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	pcicfgregs *cfg = &dinfo->cfg;
	struct resource_list *rl = &dinfo->resources;
	struct pci_quirk *q;
	int b, i, f, s;

	b = cfg->bus;
	s = cfg->slot;
	f = cfg->func;

	/* ATA devices needs special map treatment */
	if ((pci_get_class(dev) == PCIC_STORAGE) &&
	    (pci_get_subclass(dev) == PCIS_STORAGE_IDE) &&
	    ((pci_get_progif(dev) & PCIP_STORAGE_IDE_MASTERDEV) ||
	     (!pci_read_config(dev, PCIR_BAR(0), 4) &&
	      !pci_read_config(dev, PCIR_BAR(2), 4))) )
		pci_ata_maps(pcib, bus, dev, b, s, f, rl, force, prefetchmask);
	else
		for (i = 0; i < cfg->nummaps;)
			i += pci_add_map(pcib, bus, dev, b, s, f, PCIR_BAR(i),
			    rl, force, prefetchmask & (1 << i));

	/*
	 * Add additional, quirked resources.
	 */
	for (q = &pci_quirks[0]; q->devid; q++) {
		if (q->devid == ((cfg->device << 16) | cfg->vendor)
		    && q->type == PCI_QUIRK_MAP_REG)
			pci_add_map(pcib, bus, dev, b, s, f, q->arg1, rl,
			  force, 0);
	}

	if (cfg->intpin > 0 && PCI_INTERRUPT_VALID(cfg->intline)) {
		/*
		 * Try to re-route interrupts. Sometimes the BIOS or
		 * firmware may leave bogus values in these registers.
		 * If the re-route fails, then just stick with what we
		 * have.
		 */
		pci_assign_interrupt(bus, dev, 1);
	}

	if (pci_usb_takeover && pci_get_class(dev) == PCIC_SERIALBUS &&
	    pci_get_subclass(dev) == PCIS_SERIALBUS_USB) {
		if (pci_get_progif(dev) == PCIP_SERIALBUS_USB_XHCI)
			xhci_early_takeover(dev);
		else if (pci_get_progif(dev) == PCIP_SERIALBUS_USB_EHCI)
			ehci_early_takeover(dev);
		else if (pci_get_progif(dev) == PCIP_SERIALBUS_USB_OHCI)
			ohci_early_takeover(dev);
		else if (pci_get_progif(dev) == PCIP_SERIALBUS_USB_UHCI)
			uhci_early_takeover(dev);
	}
}

void
pci_add_children(device_t dev, int domain, int busno, size_t dinfo_size)
{
#define	REG(n, w)	PCIB_READ_CONFIG(pcib, busno, s, f, n, w)
	device_t pcib = device_get_parent(dev);
	struct pci_devinfo *dinfo;
	int maxslots;
	int s, f, pcifunchigh;
	uint8_t hdrtype;

	KASSERT(dinfo_size >= sizeof(struct pci_devinfo),
	    ("dinfo_size too small"));
	maxslots = PCIB_MAXSLOTS(pcib);
	for (s = 0; s <= maxslots; s++) {
		pcifunchigh = 0;
		f = 0;
		DELAY(1);
		hdrtype = REG(PCIR_HDRTYPE, 1);
		if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
			continue;
		if (hdrtype & PCIM_MFDEV)
			pcifunchigh = PCI_FUNCMAX;
		for (f = 0; f <= pcifunchigh; f++) {
			dinfo = pci_read_device(pcib, domain, busno, s, f,
			    dinfo_size);
			if (dinfo != NULL) {
				pci_add_child(dev, dinfo);
			}
		}
	}
#undef REG
}

void
pci_add_child(device_t bus, struct pci_devinfo *dinfo)
{
	device_t pcib;

	pcib = device_get_parent(bus);
	dinfo->cfg.dev = device_add_child(bus, NULL, -1);
	device_set_ivars(dinfo->cfg.dev, dinfo);
	resource_list_init(&dinfo->resources);
	pci_cfg_save(dinfo->cfg.dev, dinfo, 0);
	pci_cfg_restore(dinfo->cfg.dev, dinfo);
	pci_print_verbose(dinfo);
	pci_add_resources(pcib, bus, dinfo->cfg.dev, 0, 0);
}

static int
pci_probe(device_t dev)
{
	device_set_desc(dev, "PCI bus");

	/* Allow other subclasses to override this driver. */
	return (-1000);
}

static int
pci_attach(device_t dev)
{
	int busno, domain;

	/*
	 * Since there can be multiple independantly numbered PCI
	 * busses on systems with multiple PCI domains, we can't use
	 * the unit number to decide which bus we are probing. We ask
	 * the parent pcib what our domain and bus numbers are.
	 */
	domain = pcib_get_domain(dev);
	busno = pcib_get_bus(dev);
	if (bootverbose)
		device_printf(dev, "domain=%d, physical bus=%d\n",
		    domain, busno);

	pci_add_children(dev, domain, busno, sizeof(struct pci_devinfo));

	return (bus_generic_attach(dev));
}

int
pci_suspend(device_t dev)
{
	int dstate, error, i, numdevs;
	device_t acpi_dev, child, *devlist;
	struct pci_devinfo *dinfo;

	/*
	 * Save the PCI configuration space for each child and set the
	 * device in the appropriate power state for this sleep state.
	 */
	acpi_dev = NULL;
	if (pci_do_power_resume)
		acpi_dev = devclass_get_device(devclass_find("acpi"), 0);
	device_get_children(dev, &devlist, &numdevs);
	for (i = 0; i < numdevs; i++) {
		child = devlist[i];
		dinfo = (struct pci_devinfo *) device_get_ivars(child);
		pci_cfg_save(child, dinfo, 0);
	}

	/* Suspend devices before potentially powering them down. */
	error = bus_generic_suspend(dev);
	if (error) {
		kfree(devlist, M_TEMP);
		return (error);
	}

	/*
	 * Always set the device to D3.  If ACPI suggests a different
	 * power state, use it instead.  If ACPI is not present, the
	 * firmware is responsible for managing device power.  Skip
	 * children who aren't attached since they are powered down
	 * separately.  Only manage type 0 devices for now.
	 */
	for (i = 0; acpi_dev && i < numdevs; i++) {
		child = devlist[i];
		dinfo = (struct pci_devinfo *) device_get_ivars(child);
		if (device_is_attached(child) && dinfo->cfg.hdrtype == 0) {
			dstate = PCI_POWERSTATE_D3;
			ACPI_PWR_FOR_SLEEP(acpi_dev, child, &dstate);
			pci_set_powerstate(child, dstate);
		}
	}
	kfree(devlist, M_TEMP);
	return (0);
}

int
pci_resume(device_t dev)
{
	int i, numdevs;
	device_t acpi_dev, child, *devlist;
	struct pci_devinfo *dinfo;

	/*
	 * Set each child to D0 and restore its PCI configuration space.
	 */
	acpi_dev = NULL;
	if (pci_do_power_resume)
		acpi_dev = devclass_get_device(devclass_find("acpi"), 0);
	device_get_children(dev, &devlist, &numdevs);
	for (i = 0; i < numdevs; i++) {
		/*
		 * Notify ACPI we're going to D0 but ignore the result.  If
		 * ACPI is not present, the firmware is responsible for
		 * managing device power.  Only manage type 0 devices for now.
		 */
		child = devlist[i];
		dinfo = (struct pci_devinfo *) device_get_ivars(child);
		if (acpi_dev && device_is_attached(child) &&
		    dinfo->cfg.hdrtype == 0) {
			ACPI_PWR_FOR_SLEEP(acpi_dev, child, NULL);
			pci_set_powerstate(child, PCI_POWERSTATE_D0);
		}

		/* Now the device is powered up, restore its config space. */
		pci_cfg_restore(child, dinfo);
	}
	kfree(devlist, M_TEMP);
	return (bus_generic_resume(dev));
}

static void
pci_load_vendor_data(void)
{
	caddr_t vendordata, info;

	if ((vendordata = preload_search_by_type("pci_vendor_data")) != NULL) {
		info = preload_search_info(vendordata, MODINFO_ADDR);
		pci_vendordata = *(char **)info;
		info = preload_search_info(vendordata, MODINFO_SIZE);
		pci_vendordata_size = *(size_t *)info;
		/* terminate the database */
		pci_vendordata[pci_vendordata_size] = '\n';
	}
}

void
pci_driver_added(device_t dev, driver_t *driver)
{
	int numdevs;
	device_t *devlist;
	device_t child;
	struct pci_devinfo *dinfo;
	int i;

	if (bootverbose)
		device_printf(dev, "driver added\n");
	DEVICE_IDENTIFY(driver, dev);
	device_get_children(dev, &devlist, &numdevs);
	for (i = 0; i < numdevs; i++) {
		child = devlist[i];
		if (device_get_state(child) != DS_NOTPRESENT)
			continue;
		dinfo = device_get_ivars(child);
		pci_print_verbose(dinfo);
		if (bootverbose)
			kprintf("pci%d:%d:%d:%d: reprobing on driver added\n",
			    dinfo->cfg.domain, dinfo->cfg.bus, dinfo->cfg.slot,
			    dinfo->cfg.func);
		pci_cfg_restore(child, dinfo);
		if (device_probe_and_attach(child) != 0)
			pci_cfg_save(child, dinfo, 1);
	}
	kfree(devlist, M_TEMP);
}

static void
pci_child_detached(device_t parent __unused, device_t child)
{
	/* Turn child's power off */
	pci_cfg_save(child, device_get_ivars(child), 1);
}

int
pci_setup_intr(device_t dev, device_t child, struct resource *irq, int flags,
    driver_intr_t *intr, void *arg, void **cookiep,
    lwkt_serialize_t serializer, const char *desc)
{
	int rid, error;
	void *cookie;

	error = bus_generic_setup_intr(dev, child, irq, flags, intr,
	    arg, &cookie, serializer, desc);
	if (error)
		return (error);

	/* If this is not a direct child, just bail out. */
	if (device_get_parent(child) != dev) {
		*cookiep = cookie;
		return(0);
	}

	rid = rman_get_rid(irq);
	if (rid == 0) {
		/* Make sure that INTx is enabled */
		pci_clear_command_bit(dev, child, PCIM_CMD_INTxDIS);
	} else {
		struct pci_devinfo *dinfo = device_get_ivars(child);
		uint64_t addr;
		uint32_t data;

		/*
		 * Check to see if the interrupt is MSI or MSI-X.
		 * Ask our parent to map the MSI and give
		 * us the address and data register values.
		 * If we fail for some reason, teardown the
		 * interrupt handler.
		 */
		if (dinfo->cfg.msi.msi_alloc > 0) {
			struct pcicfg_msi *msi = &dinfo->cfg.msi;

			if (msi->msi_addr == 0) {
				KASSERT(msi->msi_handlers == 0,
			    ("MSI has handlers, but vectors not mapped"));
				error = PCIB_MAP_MSI(device_get_parent(dev),
				    child, rman_get_start(irq), &addr, &data,
				    rman_get_cpuid(irq));
				if (error)
					goto bad;
				msi->msi_addr = addr;
				msi->msi_data = data;
				pci_enable_msi(child, addr, data);
			}
			msi->msi_handlers++;
		} else {
			struct msix_vector *mv;
			u_int vector;

			KASSERT(dinfo->cfg.msix.msix_alloc > 0,
			    ("No MSI-X or MSI rid %d allocated", rid));

			mv = pci_find_msix_vector(child, rid);
			KASSERT(mv != NULL,
			    ("MSI-X rid %d is not allocated", rid));
			KASSERT(mv->mv_address == 0,
			    ("MSI-X rid %d has been setup", rid));

			error = PCIB_MAP_MSI(device_get_parent(dev),
			    child, rman_get_start(irq), &addr, &data,
			    rman_get_cpuid(irq));
			if (error)
				goto bad;
			mv->mv_address = addr;
			mv->mv_data = data;

			vector = PCI_MSIX_RID2VEC(rid);
			pci_setup_msix_vector(child, vector,
			    mv->mv_address, mv->mv_data);
			pci_unmask_msix_vector(child, vector);
		}

		/* Make sure that INTx is disabled if we are using MSI/MSIX */
		pci_set_command_bit(dev, child, PCIM_CMD_INTxDIS);
	bad:
		if (error) {
			(void)bus_generic_teardown_intr(dev, child, irq,
			    cookie);
			return (error);
		}
	}
	*cookiep = cookie;
	return (0);
}

int
pci_teardown_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie)
{
	int rid, error;

	if (irq == NULL || !(rman_get_flags(irq) & RF_ACTIVE))
		return (EINVAL);

	/* If this isn't a direct child, just bail out */
	if (device_get_parent(child) != dev)
		return(bus_generic_teardown_intr(dev, child, irq, cookie));

	rid = rman_get_rid(irq);
	if (rid == 0) {
		/* Mask INTx */
		pci_set_command_bit(dev, child, PCIM_CMD_INTxDIS);
	} else {
		struct pci_devinfo *dinfo = device_get_ivars(child);

		/*
		 * Check to see if the interrupt is MSI or MSI-X.  If so,
		 * decrement the appropriate handlers count and mask the
		 * MSI-X message, or disable MSI messages if the count
		 * drops to 0.
		 */
		if (dinfo->cfg.msi.msi_alloc > 0) {
			struct pcicfg_msi *msi = &dinfo->cfg.msi;

			KASSERT(rid <= msi->msi_alloc,
			    ("MSI-X index too high"));
			KASSERT(msi->msi_handlers > 0,
			    ("MSI rid %d is not setup", rid));

			msi->msi_handlers--;
			if (msi->msi_handlers == 0)
				pci_disable_msi(child);
		} else {
			struct msix_vector *mv;

			KASSERT(dinfo->cfg.msix.msix_alloc > 0,
			    ("No MSI or MSI-X rid %d allocated", rid));

			mv = pci_find_msix_vector(child, rid);
			KASSERT(mv != NULL,
			    ("MSI-X rid %d is not allocated", rid));
			KASSERT(mv->mv_address != 0,
			    ("MSI-X rid %d has not been setup", rid));

			pci_mask_msix_vector(child, PCI_MSIX_RID2VEC(rid));
			mv->mv_address = 0;
			mv->mv_data = 0;
		}
	}
	error = bus_generic_teardown_intr(dev, child, irq, cookie);
	if (rid > 0)
		KASSERT(error == 0,
		    ("%s: generic teardown failed for MSI/MSI-X", __func__));
	return (error);
}

int
pci_print_child(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo;
	struct resource_list *rl;
	int retval = 0;

	dinfo = device_get_ivars(child);
	rl = &dinfo->resources;

	retval += bus_print_child_header(dev, child);

	retval += resource_list_print_type(rl, "port", SYS_RES_IOPORT, "%#lx");
	retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#lx");
	retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%ld");
	if (device_get_flags(dev))
		retval += kprintf(" flags %#x", device_get_flags(dev));

	retval += kprintf(" at device %d.%d", pci_get_slot(child),
	    pci_get_function(child));

	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static struct
{
	int	class;
	int	subclass;
	char	*desc;
} pci_nomatch_tab[] = {
	{PCIC_OLD,		-1,			"old"},
	{PCIC_OLD,		PCIS_OLD_NONVGA,	"non-VGA display device"},
	{PCIC_OLD,		PCIS_OLD_VGA,		"VGA-compatible display device"},
	{PCIC_STORAGE,		-1,			"mass storage"},
	{PCIC_STORAGE,		PCIS_STORAGE_SCSI,	"SCSI"},
	{PCIC_STORAGE,		PCIS_STORAGE_IDE,	"ATA"},
	{PCIC_STORAGE,		PCIS_STORAGE_FLOPPY,	"floppy disk"},
	{PCIC_STORAGE,		PCIS_STORAGE_IPI,	"IPI"},
	{PCIC_STORAGE,		PCIS_STORAGE_RAID,	"RAID"},
	{PCIC_STORAGE,		PCIS_STORAGE_ATA_ADMA,	"ATA (ADMA)"},
	{PCIC_STORAGE,		PCIS_STORAGE_SATA,	"SATA"},
	{PCIC_STORAGE,		PCIS_STORAGE_SAS,	"SAS"},
	{PCIC_NETWORK,		-1,			"network"},
	{PCIC_NETWORK,		PCIS_NETWORK_ETHERNET,	"ethernet"},
	{PCIC_NETWORK,		PCIS_NETWORK_TOKENRING,	"token ring"},
	{PCIC_NETWORK,		PCIS_NETWORK_FDDI,	"fddi"},
	{PCIC_NETWORK,		PCIS_NETWORK_ATM,	"ATM"},
	{PCIC_NETWORK,		PCIS_NETWORK_ISDN,	"ISDN"},
	{PCIC_DISPLAY,		-1,			"display"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_VGA,	"VGA"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_XGA,	"XGA"},
	{PCIC_DISPLAY,		PCIS_DISPLAY_3D,	"3D"},
	{PCIC_MULTIMEDIA,	-1,			"multimedia"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_VIDEO,	"video"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_AUDIO,	"audio"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_TELE,	"telephony"},
	{PCIC_MULTIMEDIA,	PCIS_MULTIMEDIA_HDA,	"HDA"},
	{PCIC_MEMORY,		-1,			"memory"},
	{PCIC_MEMORY,		PCIS_MEMORY_RAM,	"RAM"},
	{PCIC_MEMORY,		PCIS_MEMORY_FLASH,	"flash"},
	{PCIC_BRIDGE,		-1,			"bridge"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_HOST,	"HOST-PCI"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_ISA,	"PCI-ISA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_EISA,	"PCI-EISA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_MCA,	"PCI-MCA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_PCI,	"PCI-PCI"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_PCMCIA,	"PCI-PCMCIA"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_NUBUS,	"PCI-NuBus"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_CARDBUS,	"PCI-CardBus"},
	{PCIC_BRIDGE,		PCIS_BRIDGE_RACEWAY,	"PCI-RACEway"},
	{PCIC_SIMPLECOMM,	-1,			"simple comms"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_UART,	"UART"},	/* could detect 16550 */
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_PAR,	"parallel port"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_MULSER,	"multiport serial"},
	{PCIC_SIMPLECOMM,	PCIS_SIMPLECOMM_MODEM,	"generic modem"},
	{PCIC_BASEPERIPH,	-1,			"base peripheral"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_PIC,	"interrupt controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_DMA,	"DMA controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_TIMER,	"timer"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_RTC,	"realtime clock"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_PCIHOT,	"PCI hot-plug controller"},
	{PCIC_BASEPERIPH,	PCIS_BASEPERIPH_SDHC,	"SD host controller"},
	{PCIC_INPUTDEV,		-1,			"input device"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_KEYBOARD,	"keyboard"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_DIGITIZER,"digitizer"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_MOUSE,	"mouse"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_SCANNER,	"scanner"},
	{PCIC_INPUTDEV,		PCIS_INPUTDEV_GAMEPORT,	"gameport"},
	{PCIC_DOCKING,		-1,			"docking station"},
	{PCIC_PROCESSOR,	-1,			"processor"},
	{PCIC_SERIALBUS,	-1,			"serial bus"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_FW,	"FireWire"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_ACCESS,	"AccessBus"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_SSA,	"SSA"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_USB,	"USB"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_FC,	"Fibre Channel"},
	{PCIC_SERIALBUS,	PCIS_SERIALBUS_SMBUS,	"SMBus"},
	{PCIC_WIRELESS,		-1,			"wireless controller"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_IRDA,	"iRDA"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_IR,	"IR"},
	{PCIC_WIRELESS,		PCIS_WIRELESS_RF,	"RF"},
	{PCIC_INTELLIIO,	-1,			"intelligent I/O controller"},
	{PCIC_INTELLIIO,	PCIS_INTELLIIO_I2O,	"I2O"},
	{PCIC_SATCOM,		-1,			"satellite communication"},
	{PCIC_SATCOM,		PCIS_SATCOM_TV,		"sat TV"},
	{PCIC_SATCOM,		PCIS_SATCOM_AUDIO,	"sat audio"},
	{PCIC_SATCOM,		PCIS_SATCOM_VOICE,	"sat voice"},
	{PCIC_SATCOM,		PCIS_SATCOM_DATA,	"sat data"},
	{PCIC_CRYPTO,		-1,			"encrypt/decrypt"},
	{PCIC_CRYPTO,		PCIS_CRYPTO_NETCOMP,	"network/computer crypto"},
	{PCIC_CRYPTO,		PCIS_CRYPTO_ENTERTAIN,	"entertainment crypto"},
	{PCIC_DASP,		-1,			"dasp"},
	{PCIC_DASP,		PCIS_DASP_DPIO,		"DPIO module"},
	{0, 0,		NULL}
};

void
pci_probe_nomatch(device_t dev, device_t child)
{
	int	i;
	char	*cp, *scp, *device;

	/*
	 * Look for a listing for this device in a loaded device database.
	 */
	if ((device = pci_describe_device(child)) != NULL) {
		device_printf(dev, "<%s>", device);
		kfree(device, M_DEVBUF);
	} else {
		/*
		 * Scan the class/subclass descriptions for a general
		 * description.
		 */
		cp = "unknown";
		scp = NULL;
		for (i = 0; pci_nomatch_tab[i].desc != NULL; i++) {
			if (pci_nomatch_tab[i].class == pci_get_class(child)) {
				if (pci_nomatch_tab[i].subclass == -1) {
					cp = pci_nomatch_tab[i].desc;
				} else if (pci_nomatch_tab[i].subclass ==
				    pci_get_subclass(child)) {
					scp = pci_nomatch_tab[i].desc;
				}
			}
		}
		device_printf(dev, "<%s%s%s>",
		    cp ? cp : "",
		    ((cp != NULL) && (scp != NULL)) ? ", " : "",
		    scp ? scp : "");
	}
	kprintf(" (vendor 0x%04x, dev 0x%04x) at device %d.%d",
		pci_get_vendor(child), pci_get_device(child),
		pci_get_slot(child), pci_get_function(child));
	if (pci_get_intpin(child) > 0) {
		int irq;

		irq = pci_get_irq(child);
		if (PCI_INTERRUPT_VALID(irq))
			kprintf(" irq %d", irq);
	}
	kprintf("\n");

	pci_cfg_save(child, (struct pci_devinfo *)device_get_ivars(child), 1);
}

/*
 * Parse the PCI device database, if loaded, and return a pointer to a
 * description of the device.
 *
 * The database is flat text formatted as follows:
 *
 * Any line not in a valid format is ignored.
 * Lines are terminated with newline '\n' characters.
 *
 * A VENDOR line consists of the 4 digit (hex) vendor code, a TAB, then
 * the vendor name.
 *
 * A DEVICE line is entered immediately below the corresponding VENDOR ID.
 * - devices cannot be listed without a corresponding VENDOR line.
 * A DEVICE line consists of a TAB, the 4 digit (hex) device code,
 * another TAB, then the device name.
 */

/*
 * Assuming (ptr) points to the beginning of a line in the database,
 * return the vendor or device and description of the next entry.
 * The value of (vendor) or (device) inappropriate for the entry type
 * is set to -1.  Returns nonzero at the end of the database.
 *
 * Note that this is slightly unrobust in the face of corrupt data;
 * we attempt to safeguard against this by spamming the end of the
 * database with a newline when we initialise.
 */
static int
pci_describe_parse_line(char **ptr, int *vendor, int *device, char **desc)
{
	char	*cp = *ptr;
	int	left;

	*device = -1;
	*vendor = -1;
	**desc = '\0';
	for (;;) {
		left = pci_vendordata_size - (cp - pci_vendordata);
		if (left <= 0) {
			*ptr = cp;
			return(1);
		}

		/* vendor entry? */
		if (*cp != '\t' &&
		    ksscanf(cp, "%x\t%80[^\n]", vendor, *desc) == 2)
			break;
		/* device entry? */
		if (*cp == '\t' &&
		    ksscanf(cp, "%x\t%80[^\n]", device, *desc) == 2)
			break;

		/* skip to next line */
		while (*cp != '\n' && left > 0) {
			cp++;
			left--;
		}
		if (*cp == '\n') {
			cp++;
			left--;
		}
	}
	/* skip to next line */
	while (*cp != '\n' && left > 0) {
		cp++;
		left--;
	}
	if (*cp == '\n' && left > 0)
		cp++;
	*ptr = cp;
	return(0);
}

static char *
pci_describe_device(device_t dev)
{
	int	vendor, device;
	char	*desc, *vp, *dp, *line;

	desc = vp = dp = NULL;

	/*
	 * If we have no vendor data, we can't do anything.
	 */
	if (pci_vendordata == NULL)
		goto out;

	/*
	 * Scan the vendor data looking for this device
	 */
	line = pci_vendordata;
	if ((vp = kmalloc(80, M_DEVBUF, M_NOWAIT)) == NULL)
		goto out;
	for (;;) {
		if (pci_describe_parse_line(&line, &vendor, &device, &vp))
			goto out;
		if (vendor == pci_get_vendor(dev))
			break;
	}
	if ((dp = kmalloc(80, M_DEVBUF, M_NOWAIT)) == NULL)
		goto out;
	for (;;) {
		if (pci_describe_parse_line(&line, &vendor, &device, &dp)) {
			*dp = 0;
			break;
		}
		if (vendor != -1) {
			*dp = 0;
			break;
		}
		if (device == pci_get_device(dev))
			break;
	}
	if (dp[0] == '\0')
		ksnprintf(dp, 80, "0x%x", pci_get_device(dev));
	if ((desc = kmalloc(strlen(vp) + strlen(dp) + 3, M_DEVBUF, M_NOWAIT)) !=
	    NULL)
		ksprintf(desc, "%s, %s", vp, dp);
 out:
	if (vp != NULL)
		kfree(vp, M_DEVBUF);
	if (dp != NULL)
		kfree(dp, M_DEVBUF);
	return(desc);
}

int
pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct pci_devinfo *dinfo;
	pcicfgregs *cfg;

	dinfo = device_get_ivars(child);
	cfg = &dinfo->cfg;

	switch (which) {
	case PCI_IVAR_ETHADDR:
		/*
		 * The generic accessor doesn't deal with failure, so
		 * we set the return value, then return an error.
		 */
		*((uint8_t **) result) = NULL;
		return (EINVAL);
	case PCI_IVAR_SUBVENDOR:
		*result = cfg->subvendor;
		break;
	case PCI_IVAR_SUBDEVICE:
		*result = cfg->subdevice;
		break;
	case PCI_IVAR_VENDOR:
		*result = cfg->vendor;
		break;
	case PCI_IVAR_DEVICE:
		*result = cfg->device;
		break;
	case PCI_IVAR_DEVID:
		*result = (cfg->device << 16) | cfg->vendor;
		break;
	case PCI_IVAR_CLASS:
		*result = cfg->baseclass;
		break;
	case PCI_IVAR_SUBCLASS:
		*result = cfg->subclass;
		break;
	case PCI_IVAR_PROGIF:
		*result = cfg->progif;
		break;
	case PCI_IVAR_REVID:
		*result = cfg->revid;
		break;
	case PCI_IVAR_INTPIN:
		*result = cfg->intpin;
		break;
	case PCI_IVAR_IRQ:
		*result = cfg->intline;
		break;
	case PCI_IVAR_DOMAIN:
		*result = cfg->domain;
		break;
	case PCI_IVAR_BUS:
		*result = cfg->bus;
		break;
	case PCI_IVAR_SLOT:
		*result = cfg->slot;
		break;
	case PCI_IVAR_FUNCTION:
		*result = cfg->func;
		break;
	case PCI_IVAR_CMDREG:
		*result = cfg->cmdreg;
		break;
	case PCI_IVAR_CACHELNSZ:
		*result = cfg->cachelnsz;
		break;
	case PCI_IVAR_MINGNT:
		*result = cfg->mingnt;
		break;
	case PCI_IVAR_MAXLAT:
		*result = cfg->maxlat;
		break;
	case PCI_IVAR_LATTIMER:
		*result = cfg->lattimer;
		break;
	case PCI_IVAR_PCIXCAP_PTR:
		*result = cfg->pcix.pcix_ptr;
		break;
	case PCI_IVAR_PCIECAP_PTR:
		*result = cfg->expr.expr_ptr;
		break;
	case PCI_IVAR_VPDCAP_PTR:
		*result = cfg->vpd.vpd_reg;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

int
pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct pci_devinfo *dinfo;

	dinfo = device_get_ivars(child);

	switch (which) {
	case PCI_IVAR_INTPIN:
		dinfo->cfg.intpin = value;
		return (0);
	case PCI_IVAR_ETHADDR:
	case PCI_IVAR_SUBVENDOR:
	case PCI_IVAR_SUBDEVICE:
	case PCI_IVAR_VENDOR:
	case PCI_IVAR_DEVICE:
	case PCI_IVAR_DEVID:
	case PCI_IVAR_CLASS:
	case PCI_IVAR_SUBCLASS:
	case PCI_IVAR_PROGIF:
	case PCI_IVAR_REVID:
	case PCI_IVAR_IRQ:
	case PCI_IVAR_DOMAIN:
	case PCI_IVAR_BUS:
	case PCI_IVAR_SLOT:
	case PCI_IVAR_FUNCTION:
		return (EINVAL);	/* disallow for now */

	default:
		return (ENOENT);
	}
}
#ifdef notyet
#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#include <sys/cons.h>

/*
 * List resources based on pci map registers, used for within ddb
 */

DB_SHOW_COMMAND(pciregs, db_pci_dump)
{
	struct pci_devinfo *dinfo;
	struct devlist *devlist_head;
	struct pci_conf *p;
	const char *name;
	int i, error, none_count;

	none_count = 0;
	/* get the head of the device queue */
	devlist_head = &pci_devq;

	/*
	 * Go through the list of devices and print out devices
	 */
	for (error = 0, i = 0,
	     dinfo = STAILQ_FIRST(devlist_head);
	     (dinfo != NULL) && (error == 0) && (i < pci_numdevs) && !db_pager_quit;
	     dinfo = STAILQ_NEXT(dinfo, pci_links), i++) {

		/* Populate pd_name and pd_unit */
		name = NULL;
		if (dinfo->cfg.dev)
			name = device_get_name(dinfo->cfg.dev);

		p = &dinfo->conf;
		db_kprintf("%s%d@pci%d:%d:%d:%d:\tclass=0x%06x card=0x%08x "
			"chip=0x%08x rev=0x%02x hdr=0x%02x\n",
			(name && *name) ? name : "none",
			(name && *name) ? (int)device_get_unit(dinfo->cfg.dev) :
			none_count++,
			p->pc_sel.pc_domain, p->pc_sel.pc_bus, p->pc_sel.pc_dev,
			p->pc_sel.pc_func, (p->pc_class << 16) |
			(p->pc_subclass << 8) | p->pc_progif,
			(p->pc_subdevice << 16) | p->pc_subvendor,
			(p->pc_device << 16) | p->pc_vendor,
			p->pc_revid, p->pc_hdr);
	}
}
#endif /* DDB */
#endif

static struct resource *
pci_alloc_map(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct resource_list *rl = &dinfo->resources;
	struct resource_list_entry *rle;
	struct resource *res;
	pci_addr_t map, testval;
	int mapsize;

	/*
	 * Weed out the bogons, and figure out how large the BAR/map
	 * is.  Bars that read back 0 here are bogus and unimplemented.
	 * Note: atapci in legacy mode are special and handled elsewhere
	 * in the code.  If you have a atapci device in legacy mode and
	 * it fails here, that other code is broken.
	 */
	res = NULL;
	map = pci_read_config(child, *rid, 4);
	pci_write_config(child, *rid, 0xffffffff, 4);
	testval = pci_read_config(child, *rid, 4);
	if (pci_maprange(testval) == 64)
		map |= (pci_addr_t)pci_read_config(child, *rid + 4, 4) << 32;
	if (pci_mapbase(testval) == 0)
		goto out;

	/*
	 * Restore the original value of the BAR.  We may have reprogrammed
	 * the BAR of the low-level console device and when booting verbose,
	 * we need the console device addressable.
	 */
	pci_write_config(child, *rid, map, 4);

	if (PCI_BAR_MEM(testval)) {
		if (type != SYS_RES_MEMORY) {
			if (bootverbose)
				device_printf(dev,
				    "child %s requested type %d for rid %#x,"
				    " but the BAR says it is an memio\n",
				    device_get_nameunit(child), type, *rid);
			goto out;
		}
	} else {
		if (type != SYS_RES_IOPORT) {
			if (bootverbose)
				device_printf(dev,
				    "child %s requested type %d for rid %#x,"
				    " but the BAR says it is an ioport\n",
				    device_get_nameunit(child), type, *rid);
			goto out;
		}
	}
	/*
	 * For real BARs, we need to override the size that
	 * the driver requests, because that's what the BAR
	 * actually uses and we would otherwise have a
	 * situation where we might allocate the excess to
	 * another driver, which won't work.
	 */
	mapsize = pci_mapsize(testval);
	count = 1UL << mapsize;
	if (RF_ALIGNMENT(flags) < mapsize)
		flags = (flags & ~RF_ALIGNMENT_MASK) | RF_ALIGNMENT_LOG2(mapsize);
	if (PCI_BAR_MEM(testval) && (testval & PCIM_BAR_MEM_PREFETCH))
		flags |= RF_PREFETCHABLE;

	/*
	 * Allocate enough resource, and then write back the
	 * appropriate bar for that resource.
	 */
	res = BUS_ALLOC_RESOURCE(device_get_parent(dev), child, type, rid,
	    start, end, count, flags, -1);
	if (res == NULL) {
		device_printf(child,
		    "%#lx bytes of rid %#x res %d failed (%#lx, %#lx).\n",
		    count, *rid, type, start, end);
		goto out;
	}
	resource_list_add(rl, type, *rid, start, end, count, -1);
	rle = resource_list_find(rl, type, *rid);
	if (rle == NULL)
		panic("pci_alloc_map: unexpectedly can't find resource.");
	rle->res = res;
	rle->start = rman_get_start(res);
	rle->end = rman_get_end(res);
	rle->count = count;
	if (bootverbose)
		device_printf(child,
		    "Lazy allocation of %#lx bytes rid %#x type %d at %#lx\n",
		    count, *rid, type, rman_get_start(res));
	map = rman_get_start(res);
out:;
	pci_write_config(child, *rid, map, 4);
	if (pci_maprange(testval) == 64)
		pci_write_config(child, *rid + 4, map >> 32, 4);
	return (res);
}


struct resource *
pci_alloc_resource(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags, int cpuid)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct resource_list *rl = &dinfo->resources;
	struct resource_list_entry *rle;
	pcicfgregs *cfg = &dinfo->cfg;

	/*
	 * Perform lazy resource allocation
	 */
	if (device_get_parent(child) == dev) {
		switch (type) {
		case SYS_RES_IRQ:
			/*
			 * Can't alloc legacy interrupt once MSI messages
			 * have been allocated.
			 */
			if (*rid == 0 && (cfg->msi.msi_alloc > 0 ||
			    cfg->msix.msix_alloc > 0))
				return (NULL);
			/*
			 * If the child device doesn't have an
			 * interrupt routed and is deserving of an
			 * interrupt, try to assign it one.
			 */
			if (*rid == 0 && !PCI_INTERRUPT_VALID(cfg->intline) &&
			    (cfg->intpin != 0))
				pci_assign_interrupt(dev, child, 0);
			break;
		case SYS_RES_IOPORT:
		case SYS_RES_MEMORY:
			if (*rid < PCIR_BAR(cfg->nummaps)) {
				/*
				 * Enable the I/O mode.  We should
				 * also be assigning resources too
				 * when none are present.  The
				 * resource_list_alloc kind of sorta does
				 * this...
				 */
				if (PCI_ENABLE_IO(dev, child, type))
					return (NULL);
			}
			rle = resource_list_find(rl, type, *rid);
			if (rle == NULL)
				return (pci_alloc_map(dev, child, type, rid,
				    start, end, count, flags));
			break;
		}
		/*
		 * If we've already allocated the resource, then
		 * return it now.  But first we may need to activate
		 * it, since we don't allocate the resource as active
		 * above.  Normally this would be done down in the
		 * nexus, but since we short-circuit that path we have
		 * to do its job here.  Not sure if we should kfree the
		 * resource if it fails to activate.
		 */
		rle = resource_list_find(rl, type, *rid);
		if (rle != NULL && rle->res != NULL) {
			if (bootverbose)
				device_printf(child,
			    "Reserved %#lx bytes for rid %#x type %d at %#lx\n",
				    rman_get_size(rle->res), *rid, type,
				    rman_get_start(rle->res));
			if ((flags & RF_ACTIVE) &&
			    bus_generic_activate_resource(dev, child, type,
			    *rid, rle->res) != 0)
				return (NULL);
			return (rle->res);
		}
	}
	return (resource_list_alloc(rl, dev, child, type, rid,
	    start, end, count, flags, cpuid));
}

void
pci_delete_resource(device_t dev, device_t child, int type, int rid)
{
	struct pci_devinfo *dinfo;
	struct resource_list *rl;
	struct resource_list_entry *rle;

	if (device_get_parent(child) != dev)
		return;

	dinfo = device_get_ivars(child);
	rl = &dinfo->resources;
	rle = resource_list_find(rl, type, rid);
	if (rle) {
		if (rle->res) {
			if (rman_get_device(rle->res) != dev ||
			    rman_get_flags(rle->res) & RF_ACTIVE) {
				device_printf(dev, "delete_resource: "
				    "Resource still owned by child, oops. "
				    "(type=%d, rid=%d, addr=%lx)\n",
				    rle->type, rle->rid,
				    rman_get_start(rle->res));
				return;
			}
			bus_release_resource(dev, type, rid, rle->res);
		}
		resource_list_delete(rl, type, rid);
	}
	/*
	 * Why do we turn off the PCI configuration BAR when we delete a
	 * resource? -- imp
	 */
	pci_write_config(child, rid, 0, 4);
	BUS_DELETE_RESOURCE(device_get_parent(dev), child, type, rid);
}

struct resource_list *
pci_get_resource_list (device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);

	if (dinfo == NULL)
		return (NULL);

	return (&dinfo->resources);
}

uint32_t
pci_read_config_method(device_t dev, device_t child, int reg, int width)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;

	return (PCIB_READ_CONFIG(device_get_parent(dev),
	    cfg->bus, cfg->slot, cfg->func, reg, width));
}

void
pci_write_config_method(device_t dev, device_t child, int reg,
    uint32_t val, int width)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;

	PCIB_WRITE_CONFIG(device_get_parent(dev),
	    cfg->bus, cfg->slot, cfg->func, reg, val, width);
}

int
pci_child_location_str_method(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	ksnprintf(buf, buflen, "slot=%d function=%d", pci_get_slot(child),
	    pci_get_function(child));
	return (0);
}

int
pci_child_pnpinfo_str_method(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct pci_devinfo *dinfo;
	pcicfgregs *cfg;

	dinfo = device_get_ivars(child);
	cfg = &dinfo->cfg;
	ksnprintf(buf, buflen, "vendor=0x%04x device=0x%04x subvendor=0x%04x "
	    "subdevice=0x%04x class=0x%02x%02x%02x", cfg->vendor, cfg->device,
	    cfg->subvendor, cfg->subdevice, cfg->baseclass, cfg->subclass,
	    cfg->progif);
	return (0);
}

int
pci_assign_interrupt_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	pcicfgregs *cfg = &dinfo->cfg;

	return (PCIB_ROUTE_INTERRUPT(device_get_parent(dev), child,
	    cfg->intpin));
}

static int
pci_modevent(module_t mod, int what, void *arg)
{
	static struct cdev *pci_cdev;

	switch (what) {
	case MOD_LOAD:
		STAILQ_INIT(&pci_devq);
		pci_generation = 0;
		pci_cdev = make_dev(&pci_ops, 0, UID_ROOT, GID_WHEEL, 0644,
				    "pci");
		pci_load_vendor_data();
		break;

	case MOD_UNLOAD:
		destroy_dev(pci_cdev);
		break;
	}

	return (0);
}

void
pci_cfg_restore(device_t dev, struct pci_devinfo *dinfo)
{
	int i;

	/*
	 * Only do header type 0 devices.  Type 1 devices are bridges,
	 * which we know need special treatment.  Type 2 devices are
	 * cardbus bridges which also require special treatment.
	 * Other types are unknown, and we err on the side of safety
	 * by ignoring them.
	 */
	if (dinfo->cfg.hdrtype != 0)
		return;

	/*
	 * Restore the device to full power mode.  We must do this
	 * before we restore the registers because moving from D3 to
	 * D0 will cause the chip's BARs and some other registers to
	 * be reset to some unknown power on reset values.  Cut down
	 * the noise on boot by doing nothing if we are already in
	 * state D0.
	 */
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	}
	for (i = 0; i < dinfo->cfg.nummaps; i++)
		pci_write_config(dev, PCIR_BAR(i), dinfo->cfg.bar[i], 4);
	pci_write_config(dev, PCIR_BIOS, dinfo->cfg.bios, 4);
	pci_write_config(dev, PCIR_COMMAND, dinfo->cfg.cmdreg, 2);
	pci_write_config(dev, PCIR_INTLINE, dinfo->cfg.intline, 1);
	pci_write_config(dev, PCIR_INTPIN, dinfo->cfg.intpin, 1);
	pci_write_config(dev, PCIR_MINGNT, dinfo->cfg.mingnt, 1);
	pci_write_config(dev, PCIR_MAXLAT, dinfo->cfg.maxlat, 1);
	pci_write_config(dev, PCIR_CACHELNSZ, dinfo->cfg.cachelnsz, 1);
	pci_write_config(dev, PCIR_LATTIMER, dinfo->cfg.lattimer, 1);
	pci_write_config(dev, PCIR_PROGIF, dinfo->cfg.progif, 1);
	pci_write_config(dev, PCIR_REVID, dinfo->cfg.revid, 1);

	/* Restore MSI and MSI-X configurations if they are present. */
	if (dinfo->cfg.msi.msi_location != 0)
		pci_resume_msi(dev);
	if (dinfo->cfg.msix.msix_location != 0)
		pci_resume_msix(dev);
}

void
pci_cfg_save(device_t dev, struct pci_devinfo *dinfo, int setstate)
{
	int i;
	uint32_t cls;
	int ps;

	/*
	 * Only do header type 0 devices.  Type 1 devices are bridges, which
	 * we know need special treatment.  Type 2 devices are cardbus bridges
	 * which also require special treatment.  Other types are unknown, and
	 * we err on the side of safety by ignoring them.  Powering down
	 * bridges should not be undertaken lightly.
	 */
	if (dinfo->cfg.hdrtype != 0)
		return;
	for (i = 0; i < dinfo->cfg.nummaps; i++)
		dinfo->cfg.bar[i] = pci_read_config(dev, PCIR_BAR(i), 4);
	dinfo->cfg.bios = pci_read_config(dev, PCIR_BIOS, 4);

	/*
	 * Some drivers apparently write to these registers w/o updating our
	 * cached copy.  No harm happens if we update the copy, so do so here
	 * so we can restore them.  The COMMAND register is modified by the
	 * bus w/o updating the cache.  This should represent the normally
	 * writable portion of the 'defined' part of type 0 headers.  In
	 * theory we also need to save/restore the PCI capability structures
	 * we know about, but apart from power we don't know any that are
	 * writable.
	 */
	dinfo->cfg.subvendor = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	dinfo->cfg.subdevice = pci_read_config(dev, PCIR_SUBDEV_0, 2);
	dinfo->cfg.vendor = pci_read_config(dev, PCIR_VENDOR, 2);
	dinfo->cfg.device = pci_read_config(dev, PCIR_DEVICE, 2);
	dinfo->cfg.cmdreg = pci_read_config(dev, PCIR_COMMAND, 2);
	dinfo->cfg.intline = pci_read_config(dev, PCIR_INTLINE, 1);
	dinfo->cfg.intpin = pci_read_config(dev, PCIR_INTPIN, 1);
	dinfo->cfg.mingnt = pci_read_config(dev, PCIR_MINGNT, 1);
	dinfo->cfg.maxlat = pci_read_config(dev, PCIR_MAXLAT, 1);
	dinfo->cfg.cachelnsz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	dinfo->cfg.lattimer = pci_read_config(dev, PCIR_LATTIMER, 1);
	dinfo->cfg.baseclass = pci_read_config(dev, PCIR_CLASS, 1);
	dinfo->cfg.subclass = pci_read_config(dev, PCIR_SUBCLASS, 1);
	dinfo->cfg.progif = pci_read_config(dev, PCIR_PROGIF, 1);
	dinfo->cfg.revid = pci_read_config(dev, PCIR_REVID, 1);

	/*
	 * don't set the state for display devices, base peripherals and
	 * memory devices since bad things happen when they are powered down.
	 * We should (a) have drivers that can easily detach and (b) use
	 * generic drivers for these devices so that some device actually
	 * attaches.  We need to make sure that when we implement (a) we don't
	 * power the device down on a reattach.
	 */
	cls = pci_get_class(dev);
	if (!setstate)
		return;
	switch (pci_do_power_nodriver)
	{
		case 0:		/* NO powerdown at all */
			return;
		case 1:		/* Conservative about what to power down */
			if (cls == PCIC_STORAGE)
				return;
			/*FALLTHROUGH*/
		case 2:		/* Agressive about what to power down */
			if (cls == PCIC_DISPLAY || cls == PCIC_MEMORY ||
			    cls == PCIC_BASEPERIPH)
				return;
			/*FALLTHROUGH*/
		case 3:		/* Power down everything */
			break;
	}
	/*
	 * PCI spec says we can only go into D3 state from D0 state.
	 * Transition from D[12] into D0 before going to D3 state.
	 */
	ps = pci_get_powerstate(dev);
	if (ps != PCI_POWERSTATE_D0 && ps != PCI_POWERSTATE_D3)
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D3)
		pci_set_powerstate(dev, PCI_POWERSTATE_D3);
}

#ifdef COMPAT_OLDPCI

/*
 * Locate the parent of a PCI device by scanning the PCI devlist
 * and return the entry for the parent.
 * For devices on PCI Bus 0 (the host bus), this is the PCI Host.
 * For devices on secondary PCI busses, this is that bus' PCI-PCI Bridge.
 */
pcicfgregs *
pci_devlist_get_parent(pcicfgregs *cfg)
{
	struct devlist *devlist_head;
	struct pci_devinfo *dinfo;
	pcicfgregs *bridge_cfg;
	int i;

	dinfo = STAILQ_FIRST(devlist_head = &pci_devq);

	/* If the device is on PCI bus 0, look for the host */
	if (cfg->bus == 0) {
		for (i = 0; (dinfo != NULL) && (i < pci_numdevs);
		dinfo = STAILQ_NEXT(dinfo, pci_links), i++) {
			bridge_cfg = &dinfo->cfg;
			if (bridge_cfg->baseclass == PCIC_BRIDGE
				&& bridge_cfg->subclass == PCIS_BRIDGE_HOST
		    		&& bridge_cfg->bus == cfg->bus) {
				return bridge_cfg;
			}
		}
	}

	/* If the device is not on PCI bus 0, look for the PCI-PCI bridge */
	if (cfg->bus > 0) {
		for (i = 0; (dinfo != NULL) && (i < pci_numdevs);
		dinfo = STAILQ_NEXT(dinfo, pci_links), i++) {
			bridge_cfg = &dinfo->cfg;
			if (bridge_cfg->baseclass == PCIC_BRIDGE
				&& bridge_cfg->subclass == PCIS_BRIDGE_PCI
				&& bridge_cfg->secondarybus == cfg->bus) {
				return bridge_cfg;
			}
		}
	}

	return NULL; 
}

#endif	/* COMPAT_OLDPCI */

int
pci_alloc_1intr(device_t dev, int msi_enable, int *rid0, u_int *flags0)
{
	int rid, type;
	u_int flags;

	rid = 0;
	type = PCI_INTR_TYPE_LEGACY;
	flags = RF_SHAREABLE | RF_ACTIVE;

	msi_enable = device_getenv_int(dev, "msi.enable", msi_enable);
	if (msi_enable) {
		int cpu;

		cpu = device_getenv_int(dev, "msi.cpu", -1);
		if (cpu >= ncpus)
			cpu = ncpus - 1;

		if (pci_alloc_msi(dev, &rid, 1, cpu) == 0) {
			flags &= ~RF_SHAREABLE;
			type = PCI_INTR_TYPE_MSI;
		}
	}

	*rid0 = rid;
	*flags0 = flags;

	return type;
}

/* Wrapper APIs suitable for device driver use. */
void
pci_save_state(device_t dev)
{
	struct pci_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	pci_cfg_save(dev, dinfo, 0);
}

void
pci_restore_state(device_t dev)
{
	struct pci_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	pci_cfg_restore(dev, dinfo);
}
