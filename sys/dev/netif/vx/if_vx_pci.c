/*
 * Copyright (C) 1996 Naoki Hamada <nao@tom-yam.or.jp>
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
 * $FreeBSD: src/sys/dev/vx/if_vx_pci.c,v 1.21 2000/05/28 15:59:52 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/interrupt.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>

#include "pcidevs.h"
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include "if_vxreg.h"

static const struct vx_pci_type {
	uint16_t	 vx_vid;
	uint16_t	 vx_did;
	const char	*vx_desc;
} vx_pci_types[] = {
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C590,
		"3Com 3c590 PCI Ethernet Adapter" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595TX,
		"3Com 3c595-TX PCI Ethernet Adapter" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595T4,
		"3Com 3c595-T4 PCI Ethernet Adapter" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595MII,
		"3Com 3c595-MII PCI Ethernet Adapter" },

	/*
	 * The (Fast) Etherlink XL adapters are now supported by
	 * the xl driver, which uses bus master DMA and is much
	 * faster. (And which also supports the 3c905B.
	 */
#ifdef VORTEX_ETHERLINK_XL
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900TPO,
		"3Com 3c900-TPO Fast Etherlink XL" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900COMBO,
		"3Com 3c900-COMBO Fast Etherlink XL" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905TX,
		"3Com 3c905-TX Fast Etherlink XL" },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905T4,
		"3Com 3c905-T4 Fast Etherlink XL" },
#endif
	{ 0, 0, NULL }
};

static void vx_pci_shutdown(device_t);
static int vx_pci_probe(device_t);
static int vx_pci_attach(device_t);

static device_method_t vx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vx_pci_probe),
	DEVMETHOD(device_attach,	vx_pci_attach),
	DEVMETHOD(device_shutdown,	vx_pci_shutdown),

	DEVMETHOD_END
};

static driver_t vx_driver = {
	"vx",
	vx_methods,
	sizeof(struct vx_softc)
};

static devclass_t vx_devclass;

DRIVER_MODULE(if_vx, pci, vx_driver, vx_devclass, NULL, NULL);

static void
vx_pci_shutdown(device_t dev)
{
   struct vx_softc	*sc;

   sc = device_get_softc(dev);
   lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);
   vxstop(sc); 
   lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
}

static int
vx_pci_probe(device_t dev)
{
	const struct vx_pci_type *t;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (t = vx_pci_types; t->vx_desc != NULL; ++t) {
		if (vid == t->vx_vid && did == t->vx_did) {
			device_set_desc(dev, t->vx_desc);
			return 0;
		}
	}
	return ENXIO;
}

static int 
vx_pci_attach(device_t dev)
{
    struct vx_softc *sc = device_get_softc(dev);
    struct ifnet *ifp = &sc->arpcom.ac_if;
    int rid;

    rid = PCIR_MAPS;
    sc->vx_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
	RF_ACTIVE);

    if (sc->vx_res == NULL)
	goto bad;

    sc->vx_btag = rman_get_bustag(sc->vx_res);
    sc->vx_bhandle = rman_get_bushandle(sc->vx_res);

    rid = 0;
    sc->vx_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	RF_SHAREABLE | RF_ACTIVE);

    if (sc->vx_irq == NULL)
	goto bad;

    if (vxattach(dev) == 0) {
	goto bad;
    }

    /* defect check for 3C590 */
    if (pci_get_device(dev) == PCI_PRODUCT_3COM_3C590) {
	GO_WINDOW(0);
	if (vxbusyeeprom(sc))
	    goto bad;
	CSR_WRITE_2(sc, VX_W0_EEPROM_COMMAND,
	    EEPROM_CMD_RD | EEPROM_SOFT_INFO_2);
	if (vxbusyeeprom(sc))
	    goto bad;
	if (!(CSR_READ_2(sc, VX_W0_EEPROM_DATA) & NO_RX_OVN_ANOMALY)) {
	    kprintf("Warning! Defective early revision adapter!\n");
	}
    }

    ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->vx_irq));

    if (bus_setup_intr(dev, sc->vx_irq, INTR_MPSAFE,
		       vxintr, sc, &sc->vx_intrhand, 
		       ifp->if_serializer)
    ) {
	ether_ifdetach(&sc->arpcom.ac_if);
	goto bad;
    }

    return(0);

bad:
    if (sc->vx_res != NULL)
	bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->vx_res);
    if (sc->vx_irq != NULL)
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->vx_irq);
    return(ENXIO);
}
