/*
 * Copyright (c) 1997-2001 Granch, Ltd. All rights reserved.
 * Author: Denis I.Timofeev <timofeev@granch.ru>
 *
 * Redistributon and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/sbni/if_sbni_pci.c,v 1.6 2002/09/28 20:59:59 phk Exp $
 */

 
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_arp.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include "if_sbnireg.h"
#include "if_sbnivar.h"

static int	sbni_pci_probe(device_t);
static int	sbni_pci_attach(device_t);

static device_method_t sbni_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	sbni_pci_probe),
	DEVMETHOD(device_attach, sbni_pci_attach),
	{ 0, 0 }
};

static driver_t sbni_pci_driver = {
	"sbni",
	sbni_pci_methods,
	sizeof(struct sbni_softc)
};

static devclass_t sbni_pci_devclass;

DRIVER_MODULE(if_sbni, pci, sbni_pci_driver, sbni_pci_devclass, NULL, NULL);


static int
sbni_pci_probe(device_t dev)
{
	struct sbni_softc  *sc;
	u_int32_t  ports;
   
	ports = SBNI_PORTS;
	if (pci_get_vendor(dev) != SBNI_PCI_VENDOR ||
	    pci_get_device(dev) != SBNI_PCI_DEVICE)
		return (ENXIO);

	sc = device_get_softc(dev);

	if (pci_get_subdevice(dev) == 2) {
		ports <<= 1;
		sc->slave_sc = kmalloc(sizeof(struct sbni_softc),
				      M_DEVBUF, M_INTWAIT | M_ZERO);
		device_set_desc(dev, "Granch SBNI12/PCI Dual adapter");
	} else {
		device_set_desc(dev, "Granch SBNI12/PCI adapter");
	}

	sc->io_rid = PCIR_MAPS;
 	sc->io_res = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->io_rid,
					0ul, ~0ul, ports, RF_ACTIVE);
	if (!sc->io_res) {
		kprintf("sbni: cannot allocate io ports!\n");
		if (sc->slave_sc)
			kfree(sc->slave_sc, M_DEVBUF);
		return (ENOENT);
	}

	if (sc->slave_sc) {
		sc->slave_sc->io_res = sc->io_res;
		sc->slave_sc->io_off = 4;
	}
	if (sbni_probe(sc) != 0) {
		bus_release_resource(dev, SYS_RES_IOPORT,
				     sc->io_rid, sc->io_res);
		if (sc->slave_sc)
			kfree(sc->slave_sc, M_DEVBUF);
		return (ENXIO);
	}

	device_quiet(dev);
	return (0);
}

static int
sbni_pci_attach(device_t dev)
{
	struct sbni_softc *sc;
	struct sbni_flags flags;
	int error;

	sc = device_get_softc(dev);

	kprintf("sbni%d: <Granch SBNI12/PCI%sadapter> port 0x%lx",
	       next_sbni_unit, sc->slave_sc ? " Dual " : " ",
	       rman_get_start(sc->io_res));
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_SHAREABLE);

	*(u_int32_t*)&flags = 0;

	sbni_attach(sc, next_sbni_unit++, flags);
	if (sc->slave_sc)
		sbni_attach(sc->slave_sc, next_sbni_unit++, flags);

	if (sc->irq_res) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		error = bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE,
				       sbni_intr, sc, &sc->irq_handle,
				       ifp->if_serializer);
		if (error) {
			kprintf("sbni%d: bus_setup_intr\n", next_sbni_unit);
		} else {
			ifp->if_cpuid =
				ithread_cpuid(rman_get_start(sc->irq_res));
			KKASSERT(ifp->if_cpuid >= 0 && ifp->if_cpuid < ncpus);
		}
	} else {
		kprintf("\nsbni%d: cannot claim irq!\n", next_sbni_unit);
	}

	return (0);

#if 0
	bus_release_resource(dev, SYS_RES_IOPORT, sc->io_rid, sc->io_res);
	if (sc->irq_res) {
		bus_release_resource(
		    dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	}
	if (sc->slave_sc)
		kfree(sc->slave_sc, M_DEVBUF);
	return (error);
#endif
}
