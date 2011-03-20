/*-
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD @169221
 */

/*
 * Host to PCI and PCI to PCI bridge drivers that use the MP Table to route
 * interrupts from PCI devices to I/O APICs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcib_private.h>

#include <machine/smp.h>

#include "legacyvar.h"
#include "pci_cfgreg.h"

#include "pcib_if.h"

static int
mptable_pci_route_interrupt(device_t pcib, device_t dev, int pin)
{
	int line, bus, slot;

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);

	if (ioapic_use_old) {
		line = pci_apic_irq(bus, slot, pin);
		if (line >= 0) {
			goto done;
		} else {
			int irq = pci_get_irq(dev);

			/* 
			 * PCI interrupts might be redirected to the
			 * ISA bus according to some MP tables.  Use the
			 * same methods as used by the ISA devices
			 * devices to find the proper IOAPIC int pin.
			 */
			kprintf("MPTable: Try routing through ISA bus for "
				"bus %d slot %d INT%c irq %d\n",
				bus, slot, 'A' + pin - 1, irq);
			line = isa_apic_irq(irq);
			if (line >= 0)
				goto done;
		}
	} else {
		int irq = pci_get_irq(dev);

		line = mptable_pci_int_route(bus, slot, pin, irq);
		if (line >= 0)
			goto done;
	}

	kprintf("MPTABLE: Unable to route for bus %d slot %d INT%c\n",
		bus, slot, 'A' + pin - 1);
	return PCI_INVALID_IRQ;

done:
	BUS_CONFIG_INTR(dev, dev, line, INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
	return line;
}

/* Host to PCI bridge driver. */

static int
mptable_hostb_probe(device_t dev)
{
	if (!apic_io_enable)
		return (ENXIO);

	if (pci_cfgregopen() == 0)
		return (ENXIO);
#ifdef notyet
	if (mptable_pci_probe_table(pcib_get_bus(dev)) != 0)
		return (ENXIO);
#endif
	if (!ioapic_use_old) {
		if (bootverbose)
			mptable_pci_int_dump();
	}

	device_set_desc(dev, "MPTABLE Host-PCI bridge");
	return (0);
}

static int
mptable_hostb_attach(device_t dev)
{

	device_add_child(dev, "pci", pcib_get_bus(dev));
	return (bus_generic_attach(dev));
}

/* Pass MSI requests up to the nexus. */
static int
mptable_hostb_alloc_msi(device_t pcib, device_t dev, int count, int maxcount,
    int *irqs)
{
	device_t bus;

	bus = device_get_parent(pcib);
	return (PCIB_ALLOC_MSI(device_get_parent(bus), dev, count, maxcount,
	    irqs));
}

static int
mptable_hostb_alloc_msix(device_t pcib, device_t dev, int *irq)
{
	device_t bus;

	bus = device_get_parent(pcib);
	return (PCIB_ALLOC_MSIX(device_get_parent(bus), dev, irq));
}

static int
mptable_hostb_map_msi(device_t pcib, device_t dev, int irq, uint64_t *addr,
    uint32_t *data)
{
	device_t bus;

	bus = device_get_parent(pcib);
	return (PCIB_MAP_MSI(device_get_parent(bus), dev, irq, addr, data));
}

static device_method_t mptable_hostb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mptable_hostb_probe),
	DEVMETHOD(device_attach,	mptable_hostb_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_read_ivar,	legacy_pcib_read_ivar),
	DEVMETHOD(bus_write_ivar,	legacy_pcib_write_ivar),
	DEVMETHOD(bus_alloc_resource,	legacy_pcib_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	legacy_pcib_maxslots),
	DEVMETHOD(pcib_read_config,	legacy_pcib_read_config),
	DEVMETHOD(pcib_write_config,	legacy_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,	mptable_pci_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	mptable_hostb_alloc_msi),
	DEVMETHOD(pcib_release_msi,	pcib_release_msi),
	DEVMETHOD(pcib_alloc_msix,	mptable_hostb_alloc_msix),
	DEVMETHOD(pcib_release_msix,	pcib_release_msix),
	DEVMETHOD(pcib_map_msi,		mptable_hostb_map_msi),

	{ 0, 0 }
};

static devclass_t hostb_devclass;

DEFINE_CLASS_0(pcib, mptable_hostb_driver, mptable_hostb_methods, 1);
DRIVER_MODULE(mptable_pcib, legacy, mptable_hostb_driver, hostb_devclass, 0, 0);

/* PCI to PCI bridge driver. */

static int
mptable_pcib_probe(device_t dev)
{
	int bus;

	if (!apic_io_enable)
		return (ENXIO);

	if ((pci_get_class(dev) != PCIC_BRIDGE) ||
	    (pci_get_subclass(dev) != PCIS_BRIDGE_PCI))
		return (ENXIO);
	bus = pci_read_config(dev, PCIR_SECBUS_1, 1);
	if (bus == 0)
		return (ENXIO);
#ifdef notyet
	if (mptable_pci_probe_table(bus) != 0)
		return (ENXIO);
#endif
	device_set_desc(dev, "MPTABLE PCI-PCI bridge");
	return (-500);
}

static device_method_t mptable_pcib_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mptable_pcib_probe),
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
	DEVMETHOD(pcib_route_interrupt,	mptable_pci_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	pcib_alloc_msi),
	DEVMETHOD(pcib_release_msi,	pcib_release_msi),
	DEVMETHOD(pcib_alloc_msix,	pcib_alloc_msix),
	DEVMETHOD(pcib_release_msix,	pcib_release_msix),
	DEVMETHOD(pcib_map_msi,		pcib_map_msi),

	{0, 0}
};

static devclass_t pcib_devclass;

DEFINE_CLASS_0(pcib, mptable_pcib_driver, mptable_pcib_pci_methods,
    sizeof(struct pcib_softc));
DRIVER_MODULE(mptable_pcib, pci, mptable_pcib_driver, pcib_devclass, 0, 0);
