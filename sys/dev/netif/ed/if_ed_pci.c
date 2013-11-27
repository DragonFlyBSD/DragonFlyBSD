/*
 *
 * Copyright (c) 1996 Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    Stefan Esser.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * $FreeBSD: src/sys/dev/ed/if_ed_pci.c,v 1.34 2003/10/31 18:31:58 brooks Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_mib.h>
#include <net/ifq_var.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "if_edvar.h"

static int ed_pci_detach(device_t dev);

static struct ed_type {
	uint16_t	 ed_vid;
	uint16_t	 ed_did;
	const char	*ed_name;
} ed_devs[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8029,
		"NE2000 PCI Ethernet (RealTek 8029)" },
	{ PCI_VENDOR_NETVIN, PCI_PRODUCT_NETVIN_5000,
		"NE2000 PCI Ethernet (NetVin 5000)" },
	{ PCI_VENDOR_PROLAN, PCI_PRODUCT_PROLAN_NE2KETHER,
		"NE2000 PCI Ethernet (ProLAN)" },
	{ PCI_VENDOR_COMPEX, PCI_PRODUCT_COMPEX_NE2KETHER,
		"NE2000 PCI Ethernet (Compex)" },
	{ PCI_VENDOR_KTI, PCI_PRODUCT_KTI_NE2KETHER,
		"NE2000 PCI Ethernet (KTI)" },
	{ PCI_VENDOR_WINBOND, PCI_PRODUCT_WINBOND_W89C940F,
		"NE2000 PCI Ethernet (Winbond W89C940)" },
	{ PCI_VENDOR_SURECOM, PCI_PRODUCT_SURECOM_NE34,
		"NE2000 PCI Ethernet (Surecom NE-34)" },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT86C926,
		"NE2000 PCI Ethernet (VIA VT86C926)" },
	{ 0, 0, NULL }
};

static int	ed_pci_probe	(device_t);
static int	ed_pci_attach	(device_t);

static int
ed_pci_probe(device_t dev)
{
	struct ed_type *t;
	uint16_t product = pci_get_device(dev);
	uint16_t vendor = pci_get_vendor(dev);

	for (t = ed_devs; t->ed_name != NULL; t++) {
		if (vendor == t->ed_vid && product == t->ed_did) {
			device_set_desc(dev, t->ed_name);
			return 0;
		}
	}
	return ENXIO;
}

static int
ed_pci_attach(device_t dev)
{
        struct	ed_softc *sc = device_get_softc(dev);
        int	flags = 0;
        int	error;

        error = ed_probe_Novell(dev, PCIR_MAPS, flags);
        if (error)
                return (error);

        error = ed_alloc_irq(dev, 0, RF_SHAREABLE);
        if (error) {
                ed_release_resources(dev);
                return (error);
        }

	error = ed_attach(dev);
	if (error == 0) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->irq_res));

		error = bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE,
				       edintr, sc, &sc->irq_handle,
				       ifp->if_serializer);
		if (error)
			ed_pci_detach(dev);
	} else {
                ed_release_resources(dev);
	}
	return (error);
}

static int
ed_pci_detach(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->gone) {
		device_printf(dev, "already unloaded\n");
		lwkt_serialize_exit(ifp->if_serializer);
		return (0);
	}
	ed_stop(sc);
	ifp->if_flags &= ~IFF_RUNNING;
	sc->gone = 1;
	bus_teardown_intr(dev, sc->irq_res, sc->irq_handle);

	lwkt_serialize_exit(ifp->if_serializer);

	ether_ifdetach(ifp);
	ed_release_resources(dev);
	return (0);
}

static device_method_t ed_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ed_pci_probe),
	DEVMETHOD(device_attach,	ed_pci_attach),
	DEVMETHOD(device_attach,	ed_pci_detach),

	DEVMETHOD_END
};

static driver_t ed_pci_driver = {
	"ed",
	ed_pci_methods,
	sizeof(struct ed_softc),
};

DRIVER_MODULE(ed, pci, ed_pci_driver, ed_devclass, NULL, NULL);
MODULE_DEPEND(ed, pci, 1, 1, 1);
