/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/an/if_an_pci.c,v 1.2.2.8 2003/02/11 03:32:48 ambrisko Exp $
 */

/*
 * This is a PCI shim for the Aironet PC4500/4800 wireless network
 * driver. Aironet makes PCMCIA, ISA and PCI versions of these devices,
 * which all have basically the same interface. The ISA and PCI cards
 * are actually bridge adapters with PCMCIA cards inserted into them,
 * however they appear as normal PCI or ISA devices to the host.
 *
 * All we do here is handle the PCI probe and attach and set up an
 * interrupt handler entry point. The PCI version of the card uses
 * a PLX 9050 PCI to "dumb bus" bridge chip, which provides us with
 * multiple PCI address space mappings. The primary mapping at PCI
 * register 0x14 is for the PLX chip itself, *NOT* the Aironet card.
 * The I/O address of the Aironet is actually at register 0x18, which
 * is the local bus mapping register for bus space 0. There are also
 * registers for additional register spaces at registers 0x1C and
 * 0x20, but these are unused in the Aironet devices. To find out
 * more, you need a datasheet for the 9050 from PLX, but you have
 * to go through their sales office to get it. Bleh.
 */

#include "opt_inet.h"

#ifdef INET
#define ANCACHE
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "if_aironet_ieee.h"
#include "if_anreg.h"

struct an_type {
	uint16_t		 an_vid;
	uint16_t		 an_did;
	int			 an_port_rid;
	const char		*an_name;
};

static const struct an_type an_devs[] = {
	{ PCI_VENDOR_AIRONET, PCI_PRODUCT_AIRONET_350,
	  PCIR_BAR(2), "Cisco Aironet 350 Series" },
	{ PCI_VENDOR_AIRONET, PCI_PRODUCT_AIRONET_PC4500,
	  PCIR_BAR(2), "Aironet PCI4500" },
	{ PCI_VENDOR_AIRONET, PCI_PRODUCT_AIRONET_PC4800,
	  PCIR_BAR(2), "Aironet PCI4800" },
	{ PCI_VENDOR_AIRONET, PCI_PRODUCT_AIRONET_PC4xxx,
	  PCIR_BAR(2), "Aironet PCI4500/PCI4800" },
	{ PCI_VENDOR_AIRONET, PCI_PRODUCT_AIRONET_MPI350,
	  PCIR_BAR(0), "Cisco Aironet MPI350" },
	{ 0, 0, 0, NULL }
};

static int an_probe_pci		(device_t);
static int an_attach_pci	(device_t);
static int an_suspend_pci	(device_t);
static int an_resume_pci	(device_t);

static int
an_probe_pci(device_t dev)
{
	const struct an_type *t;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (t = an_devs; t->an_name != NULL; ++t) {
		if (vid == t->an_vid && did == t->an_did) {
			struct an_softc *sc;

			sc = device_get_softc(dev);
			sc->port_rid = t->an_port_rid;
			if (vid == PCI_VENDOR_AIRONET &&
			    did == PCI_PRODUCT_AIRONET_MPI350)
				sc->mpi350 = 1;

			device_set_desc(dev, t->an_name);
			return(0);
		}
	}
	return(ENXIO);
}

static int
an_attach_pci(device_t dev)
{
	struct an_softc		*sc;
	struct ifnet		*ifp;
	int 			flags, error;

	sc = device_get_softc(dev);
	ifp = &sc->arpcom.ac_if;
	flags = device_get_flags(dev);

	error = an_alloc_port(dev, sc->port_rid, 1);
	if (error) {
		device_printf(dev, "couldn't map ports\n");
		goto fail;
	}

	sc->an_btag = rman_get_bustag(sc->port_res);
	sc->an_bhandle = rman_get_bushandle(sc->port_res);

	/* Allocate memory for MPI350 */
	if (sc->mpi350) {
		/* Allocate memory */
		sc->mem_rid = PCIR_MAPS + 4;
		error = an_alloc_memory(dev, sc->mem_rid, 1);
		if (error) {
			device_printf(dev, "couldn't map memory\n");
			goto fail;
		}
		sc->an_mem_btag = rman_get_bustag(sc->mem_res);
		sc->an_mem_bhandle = rman_get_bushandle(sc->mem_res);

		/* Allocate aux. memory */
		sc->mem_aux_rid = PCIR_MAPS + 8;
		error = an_alloc_aux_memory(dev, sc->mem_aux_rid, 
		    AN_AUXMEMSIZE);
		if (error) {
			device_printf(dev, "couldn't map aux memory\n");
			goto fail;
		}
		sc->an_mem_aux_btag = rman_get_bustag(sc->mem_aux_res);
		sc->an_mem_aux_bhandle = rman_get_bushandle(sc->mem_aux_res);

		/* Allocate DMA region */
		error = bus_dma_tag_create(NULL,	/* parent */
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       0x3ffff,			/* maxsize XXX */
			       1,			/* nsegments */
			       0xffff,			/* maxsegsize XXX */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       &sc->an_dtag);
		if (error) {
			device_printf(dev, "couldn't get DMA region\n");
			goto fail;
		}
	}

	/* Allocate interrupt */
	error = an_alloc_irq(dev, 0, RF_SHAREABLE);
	if (error)
		goto fail;

	error = an_attach(sc, dev, flags);
	if (error)
		goto fail;

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->irq_res));

	error = bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE,
			       an_intr, sc, &sc->irq_handle, 
			       sc->arpcom.ac_if.if_serializer);
	if (error) {
    		ifmedia_removeall(&sc->an_ifmedia);
		ether_ifdetach(&sc->arpcom.ac_if);
		goto fail;
	}

	return(0);

fail:
	an_release_resources(dev);
	return(error);
}

static int
an_suspend_pci(device_t dev)
{
	an_shutdown(dev);
	
	return (0);
}

static int
an_resume_pci(device_t dev)
{
	an_resume(dev);

	return (0);
}

static device_method_t an_pci_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         an_probe_pci),
        DEVMETHOD(device_attach,        an_attach_pci),
	DEVMETHOD(device_detach,	an_detach),
	DEVMETHOD(device_shutdown,	an_shutdown),
	DEVMETHOD(device_suspend,	an_suspend_pci),
	DEVMETHOD(device_resume,	an_resume_pci),
	DEVMETHOD_END
};

static driver_t an_pci_driver = {
        "an",
        an_pci_methods,
        sizeof(struct an_softc),
};

static devclass_t an_devclass;

DRIVER_MODULE(if_an, pci, an_pci_driver, an_devclass, NULL, NULL);
