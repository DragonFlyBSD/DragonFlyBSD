/*
 * Copyright (c) 2004, Joerg Sonnenberger <joerg@bec.de>
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
 * $DragonFly: src/sys/bus/pci/pci_pcib.c,v 1.1 2004/02/24 15:21:25 joerg Exp $
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/resource.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include "pcib_if.h"
#include "pcib_private.h"

/*
 * Attach a pci bus device to a motherboard or pci-to-pci bridge bus.
 * Due to probe recursion it is possible for pci-to-pci bridges (such as
 * on the DELL2550) to attach before all the motherboard bridges have
 * attached.  We must call device_add_child() with the secondary id
 * rather then -1 in order to ensure that we do not accidently use
 * a motherboard PCI id, otherwise the device probe will believe that
 * the later motherboard bridge bus has already been probed and refuse
 * to probe it.  The result: disappearing busses!
 *
 * Bridges will cause recursions or duplicate attach attempts.  If
 * we have already attached this bus we don't do it again!
 */

void
pcib_attach_common(device_t dev)
{
    struct pcib_softc	*sc;
    uint8_t		iolow;

    sc = device_get_softc(dev);
    sc->dev = dev;

    /*
     * Get current bridge configuration.
     */
    sc->command   = pci_read_config(dev, PCIR_COMMAND, 1);
    sc->secbus    = pci_read_config(dev, PCIR_SECBUS_1, 1);
    sc->subbus    = pci_read_config(dev, PCIR_SUBBUS_1, 1);
    sc->secstat   = pci_read_config(dev, PCIR_SECSTAT_1, 2);
    sc->bridgectl = pci_read_config(dev, PCIR_BRIDGECTL_1, 2);
    sc->seclat    = pci_read_config(dev, PCIR_SECLAT_1, 1);

    /*
     * Determine current I/O decode.
     */
    if (sc->command & PCIM_CMD_PORTEN) {
	iolow = pci_read_config(dev, PCIR_IOBASEL_1, 1);
	if ((iolow & PCIM_BRIO_MASK) == PCIM_BRIO_32) {
	    sc->iobase = PCI_PPBIOBASE(pci_read_config(dev, PCIR_IOBASEH_1, 2),
				       pci_read_config(dev, PCIR_IOBASEL_1, 1));
	} else {
	    sc->iobase = PCI_PPBIOBASE(0, pci_read_config(dev, PCIR_IOBASEL_1, 1));
	}

	iolow = pci_read_config(dev, PCIR_IOLIMITL_1, 1);
	if ((iolow & PCIM_BRIO_MASK) == PCIM_BRIO_32) {
	    sc->iolimit = PCI_PPBIOLIMIT(pci_read_config(dev, PCIR_IOLIMITH_1, 2),
					 pci_read_config(dev, PCIR_IOLIMITL_1, 1));
	} else {
	    sc->iolimit = PCI_PPBIOLIMIT(0, pci_read_config(dev, PCIR_IOLIMITL_1, 1));
	}
    }

    /*
     * Determine current memory decode.
     */
    if (sc->command & PCIM_CMD_MEMEN) {
	sc->membase   = PCI_PPBMEMBASE(0, pci_read_config(dev, PCIR_MEMBASE_1, 2));
	sc->memlimit  = PCI_PPBMEMLIMIT(0, pci_read_config(dev, PCIR_MEMLIMIT_1, 2));
	sc->pmembase  = PCI_PPBMEMBASE((pci_addr_t)pci_read_config(dev, PCIR_PMBASEH_1, 4),
				       pci_read_config(dev, PCIR_PMBASEL_1, 2));
	sc->pmemlimit = PCI_PPBMEMLIMIT((pci_addr_t)pci_read_config(dev, PCIR_PMLIMITH_1, 4),
					pci_read_config(dev, PCIR_PMLIMITL_1, 2));
    }

    /*
     * Quirk handling.
     */
    switch (pci_get_devid(dev)) {
    case 0x12258086:		/* Intel 82454KX/GX (Orion) */
	{
	    uint8_t	supbus;

	    supbus = pci_read_config(dev, 0x41, 1);
	    if (supbus != 0xff) {
		sc->secbus = supbus + 1;
		sc->subbus = supbus + 1;
	    }
	    break;
	}

    /*
     * The i82380FB mobile docking controller is a PCI-PCI bridge,
     * and it is a subtractive bridge.  However, the ProgIf is wrong
     * so the normal setting of PCIB_SUBTRACTIVE bit doesn't
     * happen.  There's also a Toshiba bridge that behaves this
     * way.
     */
    case 0x124b8086:		/* Intel 82380FB Mobile */
    case 0x060513d7:		/* Toshiba ???? */
	sc->flags |= PCIB_SUBTRACTIVE;
	break;
    }

    /*
     * Intel 815, 845 and other chipsets say they are PCI-PCI bridges,
     * but have a ProgIF of 0x80.  The 82801 family (AA, AB, BAM/CAM,
     * BA/CA/DB and E) PCI bridges are HUB-PCI bridges, in Intelese.
     * This means they act as if they were subtractively decoding
     * bridges and pass all transactions.  Mark them and real ProgIf 1
     * parts as subtractive.
     */
    if ((pci_get_devid(dev) & 0xff00ffff) == 0x24008086 ||
      pci_read_config(dev, PCIR_PROGIF, 1) == 1)
	sc->flags |= PCIB_SUBTRACTIVE;
	
    if (bootverbose) {
	device_printf(dev, "  secondary bus     %d\n", sc->secbus);
	device_printf(dev, "  subordinate bus   %d\n", sc->subbus);
	device_printf(dev, "  I/O decode        0x%x-0x%x\n", sc->iobase, sc->iolimit);
	device_printf(dev, "  memory decode     0x%x-0x%x\n", sc->membase, sc->memlimit);
	device_printf(dev, "  prefetched decode 0x%x-0x%x\n", sc->pmembase, sc->pmemlimit);
	if (sc->flags & PCIB_SUBTRACTIVE)
	    device_printf(dev, "  Subtractively decoded bridge.\n");
    }

    /*
     * XXX If the secondary bus number is zero, we should assign a bus number
     *     since the BIOS hasn't, then initialise the bridge.
     */

    /*
     * XXX If the subordinate bus number is less than the secondary bus number,
     *     we should pick a better value.  One sensible alternative would be to
     *     pick 255; the only tradeoff here is that configuration transactions
     *     would be more widely routed than absolutely necessary.
     */
}

static const char*
pcib_match(device_t dev)
{
	switch (pci_get_devid(dev)) {
	/* Intel -- vendor 0x8086 */
	case 0x71818086:
		return ("Intel 82443LX (440 LX) PCI-PCI (AGP) bridge");
	case 0x71918086:
		return ("Intel 82443BX (440 BX) PCI-PCI (AGP) bridge");
	case 0x71A18086:
		return ("Intel 82443GX (440 GX) PCI-PCI (AGP) bridge");
	case 0x84cb8086:
		return ("Intel 82454NX PCI Expander Bridge");
	case 0x11318086:
		return ("Intel 82801BA/BAM (ICH2) PCI-PCI (AGP) bridge");
	case 0x124b8086:
		return ("Intel 82380FB mobile PCI to PCI bridge");
	case 0x24188086:
		return ("Intel 82801AA (ICH) Hub to PCI bridge");
	case 0x24288086:
		return ("Intel 82801AB (ICH0) Hub to PCI bridge");
	case 0x244e8086:
		return ("Intel 82801BA/BAM (ICH2) Hub to PCI bridge");
	case 0x1a318086:
		return ("Intel 82845 PCI-PCI (AGP) bridge");
	
	/* VLSI -- vendor 0x1004 */
	case 0x01021004:
		return ("VLSI 82C534 Eagle II PCI Bus bridge");
	case 0x01031004:
		return ("VLSI 82C538 Eagle II PCI Docking bridge");

	/* VIA Technologies -- vendor 0x1106 */
	case 0x83051106:
		return ("VIA 8363 (Apollo KT133) PCI-PCI (AGP) bridge");
	case 0x85981106:
		return ("VIA 82C598MVP (Apollo MVP3) PCI-PCI (AGP) bridge");
	/* Exclude the ACPI function of VT82Cxxx series */
	case 0x30401106:
	case 0x30501106:
	case 0x30571106:
		return NULL;

	/* AcerLabs -- vendor 0x10b9 */
	/* Funny : The datasheet told me vendor id is "10b8",sub-vendor */
	/* id is '10b9" but the register always shows "10b9". -Foxfair  */
	case 0x524710b9:
		return ("AcerLabs M5247 PCI-PCI(AGP Supported) bridge");
	case 0x524310b9:/* 5243 seems like 5247, need more info to divide*/
		return ("AcerLabs M5243 PCI-PCI bridge");

	/* AMD -- vendor 0x1022 */
	case 0x70071022:
		return ("AMD-751 PCI-PCI (1x/2x AGP) bridge");
	case 0x700f1022:
		return ("AMD-761 PCI-PCI (4x AGP) bridge");

	/* DEC -- vendor 0x1011 */
	case 0x00011011:
		return ("DEC 21050 PCI-PCI bridge");
	case 0x00211011:
		return ("DEC 21052 PCI-PCI bridge");
	case 0x00221011:
		return ("DEC 21150 PCI-PCI bridge");
	case 0x00241011:
		return ("DEC 21152 PCI-PCI bridge");
	case 0x00251011:
		return ("DEC 21153 PCI-PCI bridge");
	case 0x00261011:
		return ("DEC 21154 PCI-PCI bridge");

	/* NVIDIA -- vendor 0x10de */
	case 0x006c10de:
	case 0x01e810de:
		return ("NVIDIA nForce2 PCI-PCI bridge");

	/* Others */
	case 0x00221014:
		return ("IBM 82351 PCI-PCI bridge");
	/* UMC United Microelectronics 0x1060 */
	case 0x88811060:
		return ("UMC UM8881 HB4 486 PCI Chipset");
	};

	if (pci_get_class(dev) == PCIC_BRIDGE
	    && pci_get_subclass(dev) == PCIS_BRIDGE_PCI)
		return pci_bridge_type(dev);

	return NULL;
}

static int pcib_probe(device_t dev)
{
	const char *desc;

	desc = pcib_match(dev);
	if (desc) {
		device_set_desc_copy(dev, desc);
		return -1000;
	}

	return ENXIO;
}

int
pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct pcib_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		*result = sc->secbus;
		return (0);
	}
	return (ENOENT);
}

int
pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct pcib_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->secbus = value;
		return (0);
	}
	return (ENOENT);
}

int
pcib_attach(device_t dev)
{
	struct pcib_softc *sc;
	device_t child;

	pcib_attach_common(dev);
	sc = device_get_softc(dev);
	/*chipset_attach(dev, device_get_unit(dev));*/

	if (sc->secbus != 0) {
		child = device_add_child(dev, "pci", sc->secbus);
		if (child != NULL)
		    return bus_generic_attach(dev);
	} 
	return 0;
}

/*
 * Is the prefetch window open (eg, can we allocate memory in it?)
 */
static int
pcib_is_prefetch_open(struct pcib_softc *sc)
{
	return (sc->pmembase > 0 && sc->pmembase < sc->pmemlimit);
}

/*
 * Is the nonprefetch window open (eg, can we allocate memory in it?)
 */
static int
pcib_is_nonprefetch_open(struct pcib_softc *sc)
{
	return (sc->membase > 0 && sc->membase < sc->memlimit);
}

/*
 * Is the io window open (eg, can we allocate ports in it?)
 */
static int
pcib_is_io_open(struct pcib_softc *sc)
{
	return (sc->iobase > 0 && sc->iobase < sc->iolimit);
}

/*
 * We have to trap resource allocation requests and ensure that the bridge
 * is set up to, or capable of handling them.
 */
struct resource *
pcib_alloc_resource(device_t dev, device_t child, int type, int *rid, 
		    u_long start, u_long end, u_long count, u_int flags)
{
	struct pcib_softc *sc = device_get_softc(dev);
	int ok;

	/*
	 * Fail the allocation for this range if it's not supported.
	 */
	switch (type) {
	case SYS_RES_IOPORT:
		ok = 0;
		if (!pcib_is_io_open(sc))
			break;
		ok = (start >= sc->iobase && end <= sc->iolimit);
		if ((sc->flags & PCIB_SUBTRACTIVE) == 0) {
			if (!ok) {
				if (start < sc->iobase)
					start = sc->iobase;
				if (end > sc->iolimit)
					end = sc->iolimit;
			}
		} else {
			ok = 1;
#if 0
			if (start < sc->iobase && end > sc->iolimit) {
				start = sc->iobase;
				end = sc->iolimit;
			}
#endif			
		}
		if (end < start) {
			device_printf(dev, "ioport: end (%lx) < start (%lx)\n", end, start);
			start = 0;
			end = 0;
			ok = 0;
		}
		if (!ok) {
			device_printf(dev, "device %s requested unsupported I/O "
			    "range 0x%lx-0x%lx (decoding 0x%x-0x%x)\n",
			    device_get_nameunit(child), start, end,
			    sc->iobase, sc->iolimit);
			return (NULL);
		}
		if (bootverbose)
			device_printf(dev, "device %s requested decoded I/O range 0x%lx-0x%lx\n",
			    device_get_nameunit(child), start, end);
		break;

	case SYS_RES_MEMORY:
		ok = 0;
		if (pcib_is_nonprefetch_open(sc))
			ok = ok || (start >= sc->membase && end <= sc->memlimit);
		if (pcib_is_prefetch_open(sc))
			ok = ok || (start >= sc->pmembase && end <= sc->pmemlimit);
		if ((sc->flags & PCIB_SUBTRACTIVE) == 0) {
			if (!ok) {
				ok = 1;
				if (flags & RF_PREFETCHABLE) {
					if (pcib_is_prefetch_open(sc)) {
						if (start < sc->pmembase)
							start = sc->pmembase;
						if (end > sc->pmemlimit)
							end = sc->pmemlimit;
					} else {
						ok = 0;
					}
				} else {	/* non-prefetchable */
					if (pcib_is_nonprefetch_open(sc)) {
						if (start < sc->membase)
							start = sc->membase;
						if (end > sc->memlimit)
							end = sc->memlimit;
					} else {
						ok = 0;
					}
				}
			}
		} else if (!ok) {
			ok = 1;	/* subtractive bridge: always ok */
#if 0
			if (pcib_is_nonprefetch_open(sc)) {
				if (start < sc->membase && end > sc->memlimit) {
					start = sc->membase;
					end = sc->memlimit;
				}
			}
			if (pcib_is_prefetch_open(sc)) {
				if (start < sc->pmembase && end > sc->pmemlimit) {
					start = sc->pmembase;
					end = sc->pmemlimit;
				}
			}
#endif
		}
		if (end < start) {
			device_printf(dev, "memory: end (%lx) < start (%lx)\n", end, start);
			start = 0;
			end = 0;
			ok = 0;
		}
		if (!ok && bootverbose)
			device_printf(dev,
			    "device %s requested unsupported memory range "
			    "0x%lx-0x%lx (decoding 0x%x-0x%x, 0x%x-0x%x)\n",
			    device_get_nameunit(child), start, end,
			    sc->membase, sc->memlimit, sc->pmembase,
			    sc->pmemlimit);
		if (!ok)
			return (NULL);
		if (bootverbose)
			device_printf(dev,"device %s requested decoded memory range 0x%lx-0x%lx\n",
			    device_get_nameunit(child), start, end);
		break;

	default:
		break;
	}
	/*
	 * Bridge is OK decoding this resource, so pass it up.
	 */
	return (bus_generic_alloc_resource(dev, child, type, rid, start, end, count, flags));
}


int
pcib_maxslots(device_t dev)
{
	return 31;
}

u_int32_t
pcib_read_config(device_t dev, int b, int s, int f,
		 int reg, int width)
{
	/*
	 * Pass through to the next ppb up the chain (i.e. our
	 * grandparent).
	 */
	return PCIB_READ_CONFIG(device_get_parent(device_get_parent(dev)),
				b, s, f, reg, width);
}

void
pcib_write_config(device_t dev, int b, int s, int f,
		  int reg, uint32_t val, int width)
{
	/*
	 * Pass through to the next ppb up the chain (i.e. our
	 * grandparent).
	 */
	PCIB_WRITE_CONFIG(device_get_parent(device_get_parent(dev)),
				b, s, f, reg, val, width);	
}

/*
 * Route an interrupt across a PCI bridge.
 */
int
pcib_route_interrupt(device_t pcib, device_t dev, int pin)
{
	device_t	bus;
	int		parent_intpin;
	int		intnum;

	device_printf(pcib, "Hi!\n");

	/*	
	 *
	 * The PCI standard defines a swizzle of the child-side device/intpin
	 * to the parent-side intpin as follows.
	 *
	 * device = device on child bus
	 * child_intpin = intpin on child bus slot (0-3)
	 * parent_intpin = intpin on parent bus slot (0-3)
	 *
	 * parent_intpin = (device + child_intpin) % 4
	 */
	parent_intpin = (pci_get_slot(pcib) + (pin - 1)) % 4;

	/*
	 * Our parent is a PCI bus.  Its parent must export the pci interface
	 * which includes the ability to route interrupts.
	 */
	bus = device_get_parent(pcib);
	intnum = PCI_ROUTE_INTERRUPT(device_get_parent(bus), pcib,
	    parent_intpin + 1);
	device_printf(pcib, "routed slot %d INT%c to irq %d\n",
	    pci_get_slot(dev), 'A' + pin - 1, intnum);
	return(intnum);
}

/*
 * Try to read the bus number of a host-PCI bridge using appropriate config
 * registers.
 */
int
host_pcib_get_busno(pci_read_config_fn read_config, int bus, int slot, int func,
    uint8_t *busnum)
{
	uint32_t id;

	id = read_config(bus, slot, func, PCIR_DEVVENDOR, 4);
	if (id == 0xffffffff)
		return (0);

	switch (id) {
	case 0x12258086:
		/* Intel 824?? */
		/* XXX This is a guess */
		/* *busnum = read_config(bus, slot, func, 0x41, 1); */
		*busnum = bus;
		break;
	case 0x84c48086:
		/* Intel 82454KX/GX (Orion) */
		*busnum = read_config(bus, slot, func, 0x4a, 1);
		break;
	case 0x84ca8086:
		/*
		 * For the 450nx chipset, there is a whole bundle of
		 * things pretending to be host bridges. The MIOC will 
		 * be seen first and isn't really a pci bridge (the
		 * actual busses are attached to the PXB's). We need to 
		 * read the registers of the MIOC to figure out the
		 * bus numbers for the PXB channels.
		 *
		 * Since the MIOC doesn't have a pci bus attached, we
		 * pretend it wasn't there.
		 */
		return (0);
	case 0x84cb8086:
		switch (slot) {
		case 0x12:
			/* Intel 82454NX PXB#0, Bus#A */
			*busnum = read_config(bus, 0x10, func, 0xd0, 1);
			break;
		case 0x13:
			/* Intel 82454NX PXB#0, Bus#B */
			*busnum = read_config(bus, 0x10, func, 0xd1, 1) + 1;
			break;
		case 0x14:
			/* Intel 82454NX PXB#1, Bus#A */
			*busnum = read_config(bus, 0x10, func, 0xd3, 1);
			break;
		case 0x15:
			/* Intel 82454NX PXB#1, Bus#B */
			*busnum = read_config(bus, 0x10, func, 0xd4, 1) + 1;
			break;
		}
		break;

		/* ServerWorks -- vendor 0x1166 */
	case 0x00051166:
	case 0x00061166:
	case 0x00081166:
	case 0x00091166:
	case 0x00101166:
	case 0x00111166:
	case 0x00171166:
	case 0x01011166:
	case 0x010f1014:
	case 0x02011166:
	case 0x03021014:
		*busnum = read_config(bus, slot, func, 0x44, 1);
		break;
	default:
		/* Don't know how to read bus number. */
		return 0;
	}

	return 1;
}

static device_method_t pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pcib_probe),
	DEVMETHOD(device_attach,	pcib_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_read_ivar,	pcib_read_ivar),
	DEVMETHOD(bus_write_ivar,	pcib_write_ivar),
	DEVMETHOD(bus_alloc_resource,	pcib_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	pcib_maxslots),
	DEVMETHOD(pcib_read_config,	pcib_read_config),
	DEVMETHOD(pcib_write_config,	pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,	pcib_route_interrupt),

	{ 0, 0 }
};

static driver_t pcib_driver = {
	"pcib",
	pcib_methods,
	sizeof(struct pcib_softc)
};

devclass_t pcib_devclass;

DRIVER_MODULE(pcib, pci, pcib_driver, pcib_devclass, 0, 0);
