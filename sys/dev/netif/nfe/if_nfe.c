/*	$OpenBSD: if_nfe.c,v 1.63 2006/06/17 18:00:43 brad Exp $	*/
/*	$DragonFly: src/sys/dev/netif/nfe/if_nfe.c,v 1.1 2006/08/27 03:28:21 sephe Exp $	*/

/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com> and
 * Matthew Dillon <dillon@apollo.backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2005, 2006 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Driver for NVIDIA nForce MCP Fast Ethernet and Gigabit Ethernet */

#include "opt_polling.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/vlan/if_vlan_var.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcidevs.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "miibus_if.h"

#include "if_nfereg.h"
#include "if_nfevar.h"

static int	nfe_probe(device_t);
static int	nfe_attach(device_t);
static int	nfe_detach(device_t);
static void	nfe_shutdown(device_t);
static int	nfe_resume(device_t);
static int	nfe_suspend(device_t);

static int	nfe_miibus_readreg(device_t, int, int);
static void	nfe_miibus_writereg(device_t, int, int, int);
static void	nfe_miibus_statchg(device_t);

#ifdef DEVICE_POLLING
static void	nfe_poll(struct ifnet *, enum poll_cmd, int);
#endif
static void	nfe_intr(void *);
static int	nfe_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	nfe_rxeof(struct nfe_softc *);
static void	nfe_txeof(struct nfe_softc *);
static int	nfe_encap(struct nfe_softc *, struct nfe_tx_ring *,
			  struct mbuf *);
static void	nfe_start(struct ifnet *);
static void	nfe_watchdog(struct ifnet *);
static void	nfe_init(void *);
static void	nfe_stop(struct nfe_softc *);
static struct nfe_jbuf *nfe_jalloc(struct nfe_softc *);
static void	nfe_jfree(void *);
static void	nfe_jref(void *);
static int	nfe_jpool_alloc(struct nfe_softc *, struct nfe_rx_ring *);
static void	nfe_jpool_free(struct nfe_softc *, struct nfe_rx_ring *);
static int	nfe_alloc_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static void	nfe_reset_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static int	nfe_init_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static void	nfe_free_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static int	nfe_alloc_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static void	nfe_reset_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static int	nfe_init_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static void	nfe_free_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static int	nfe_ifmedia_upd(struct ifnet *);
static void	nfe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	nfe_setmulti(struct nfe_softc *);
static void	nfe_get_macaddr(struct nfe_softc *, uint8_t *);
static void	nfe_set_macaddr(struct nfe_softc *, const uint8_t *);
static void	nfe_tick(void *);
static void	nfe_ring_dma_addr(void *, bus_dma_segment_t *, int, int);
static void	nfe_buf_dma_addr(void *, bus_dma_segment_t *, int, bus_size_t,
				 int);
static void	nfe_set_paddr_rxdesc(struct nfe_softc *, struct nfe_rx_ring *,
				     int, bus_addr_t);
static void	nfe_set_ready_rxdesc(struct nfe_softc *, struct nfe_rx_ring *,
				     int);
static int	nfe_newbuf_std(struct nfe_softc *, struct nfe_rx_ring *, int,
			       int);
static int	nfe_newbuf_jumbo(struct nfe_softc *, struct nfe_rx_ring *, int,
				 int);

#define NFE_DEBUG
#ifdef NFE_DEBUG

static int	nfe_debug = 0;

SYSCTL_NODE(_hw, OID_AUTO, nfe, CTLFLAG_RD, 0, "nVidia GigE parameters");
SYSCTL_INT(_hw_nfe, OID_AUTO, debug, CTLFLAG_RW, &nfe_debug, 0,
	   "control debugging printfs");

#define DPRINTF(sc, fmt, ...) do {		\
	if (nfe_debug) {			\
		if_printf(&(sc)->arpcom.ac_if,	\
			  fmt, __VA_ARGS__);	\
	}					\
} while (0)

#define DPRINTFN(sc, lv, fmt, ...) do {		\
	if (nfe_debug >= (lv)) {		\
		if_printf(&(sc)->arpcom.ac_if,	\
			  fmt, __VA_ARGS__);	\
	}					\
} while (0)

#else	/* !NFE_DEBUG */

#define DPRINTF(sc, fmt, ...)
#define DPRINTFN(sc, lv, fmt, ...)

#endif	/* NFE_DEBUG */

struct nfe_dma_ctx {
	int			nsegs;
	bus_dma_segment_t	*segs;
};

static const struct nfe_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} nfe_devices[] = {
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_LAN,
	  "NVIDIA nForce Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_LAN,
	  "NVIDIA nForce2 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN1,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN2,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN3,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN4,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN5,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN1,
	  "NVIDIA CK804 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN2,
	  "NVIDIA CK804 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN1,
	  "NVIDIA MCP04 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN2,
	  "NVIDIA MCP04 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN1,
	  "NVIDIA MCP51 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN2,
	  "NVIDIA MCP51 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN1,
	  "NVIDIA MCP55 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN2,
	  "NVIDIA MCP55 Gigabit Ethernet" }
};

static device_method_t nfe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nfe_probe),
	DEVMETHOD(device_attach,	nfe_attach),
	DEVMETHOD(device_detach,	nfe_detach),
	DEVMETHOD(device_suspend,	nfe_suspend),
	DEVMETHOD(device_resume,	nfe_resume),
	DEVMETHOD(device_shutdown,	nfe_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	nfe_miibus_readreg),
	DEVMETHOD(miibus_writereg,	nfe_miibus_writereg),
	DEVMETHOD(miibus_statchg,	nfe_miibus_statchg),

	{ 0, 0 }
};

static driver_t nfe_driver = {
	"nfe",
	nfe_methods,
	sizeof(struct nfe_softc)
};

static devclass_t	nfe_devclass;

DECLARE_DUMMY_MODULE(if_nfe);
MODULE_DEPEND(if_nfe, miibus, 1, 1, 1);
DRIVER_MODULE(if_nfe, pci, nfe_driver, nfe_devclass, 0, 0);
DRIVER_MODULE(miibus, nfe, miibus_driver, miibus_devclass, 0, 0);

static int
nfe_probe(device_t dev)
{
	const struct nfe_dev *n;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (n = nfe_devices; n->desc != NULL; ++n) {
		if (vid == n->vid && did == n->did) {
			struct nfe_softc *sc = device_get_softc(dev);

			switch (did) {
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN2:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN3:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN4:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN5:
				sc->sc_flags = NFE_JUMBO_SUP |
					       NFE_HW_CSUM;
				break;
			case PCI_PRODUCT_NVIDIA_MCP51_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP51_LAN2:
				sc->sc_flags = NFE_40BIT_ADDR;
				break;
			case PCI_PRODUCT_NVIDIA_CK804_LAN1:
			case PCI_PRODUCT_NVIDIA_CK804_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP04_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP04_LAN2:
				sc->sc_flags = NFE_JUMBO_SUP |
					       NFE_40BIT_ADDR |
					       NFE_HW_CSUM;
				break;
			case PCI_PRODUCT_NVIDIA_MCP55_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP55_LAN2:
				sc->sc_flags = NFE_JUMBO_SUP |
					       NFE_40BIT_ADDR |
					       NFE_HW_CSUM |
					       NFE_HW_VLAN;
				break;
			}

			/* Enable jumbo frames for adapters that support it */
			if (sc->sc_flags & NFE_JUMBO_SUP)
				sc->sc_flags |= NFE_USE_JUMBO;

			device_set_desc(dev, n->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
nfe_attach(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int error;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	lwkt_serialize_init(&sc->sc_jbuf_serializer);

	sc->sc_mem_rid = PCIR_BAR(0);

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t mem, irq;

		mem = pci_read_config(dev, sc->sc_mem_rid, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, sc->sc_mem_rid, mem, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif	/* !BURN_BRIDGE */

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/* Allocate IO memory */
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						&sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "cound not allocate io memory\n");
		return ENXIO;
	}
	sc->sc_memh = rman_get_bushandle(sc->sc_mem_res);
	sc->sc_memt = rman_get_bustag(sc->sc_mem_res);

	/* Allocate IRQ */
	sc->sc_irq_rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->sc_irq_rid,
						RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "could not allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	nfe_get_macaddr(sc, eaddr);

	/*
	 * Allocate Tx and Rx rings.
	 */
	error = nfe_alloc_tx_ring(sc, &sc->txq);
	if (error) {
		device_printf(dev, "could not allocate Tx ring\n");
		goto fail;
	}

	error = nfe_alloc_rx_ring(sc, &sc->rxq);
	if (error) {
		device_printf(dev, "could not allocate Rx ring\n");
		goto fail;
	}

	error = mii_phy_probe(dev, &sc->sc_miibus, nfe_ifmedia_upd,
			      nfe_ifmedia_sts);
	if (error) {
		device_printf(dev, "MII without any phy\n");
		goto fail;
	}

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nfe_ioctl;
	ifp->if_start = nfe_start;
#ifdef DEVICE_POLLING
	ifp->if_poll = nfe_poll;
#endif
	ifp->if_watchdog = nfe_watchdog;
	ifp->if_init = nfe_init;
	ifq_set_maxlen(&ifp->if_snd, NFE_IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if 0
	if (sc->sc_flags & NFE_USE_JUMBO)
		ifp->if_hardmtu = NFE_JUMBO_MTU;
#endif

	if (sc->sc_flags & NFE_HW_VLAN)
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;

#ifdef NFE_CSUM
	if (sc->sc_flags & NFE_HW_CSUM) {
#if 0
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
		    IFCAP_CSUM_UDPv4;
#else
		ifp->if_capabilities = IFCAP_HWCSUM;
		ifp->if_hwassist = CSUM_IP | CSUM_TCP | CSUM_UDP;
#endif
	}
#endif
	ifp->if_capenable = ifp->if_capabilities;

	callout_init(&sc->sc_tick_ch);

	ether_ifattach(ifp, eaddr, NULL);

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, nfe_intr, sc,
			       &sc->sc_ih, ifp->if_serializer);
	if (error) {
		device_printf(dev, "could not setup intr\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	return 0;
fail:
	nfe_detach(dev);
	return error;
}

static int
nfe_detach(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		nfe_stop(sc);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_ih);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->sc_miibus != NULL)
		device_delete_child(dev, sc->sc_miibus);
	bus_generic_detach(dev);

	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq_res);
	}

	if (sc->sc_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid,
				     sc->sc_mem_res);
	}

	nfe_free_tx_ring(sc, &sc->txq);
	nfe_free_rx_ring(sc, &sc->rxq);

	return 0;
}

static void
nfe_shutdown(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	nfe_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
nfe_suspend(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	nfe_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static int
nfe_resume(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	if (ifp->if_flags & IFF_UP) {
		nfe_init(ifp);
		if (ifp->if_flags & IFF_RUNNING)
			ifp->if_start(ifp);
	}
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static void
nfe_miibus_statchg(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct mii_data *mii = device_get_softc(sc->sc_miibus);
	uint32_t phy, seed, misc = NFE_MISC1_MAGIC, link = NFE_MEDIA_SET;

	phy = NFE_READ(sc, NFE_PHY_IFACE);
	phy &= ~(NFE_PHY_HDX | NFE_PHY_100TX | NFE_PHY_1000T);

	seed = NFE_READ(sc, NFE_RNDSEED);
	seed &= ~NFE_SEED_MASK;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_HDX) {
		phy  |= NFE_PHY_HDX;	/* half-duplex */
		misc |= NFE_MISC1_HDX;
	}

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:	/* full-duplex only */
		link |= NFE_MEDIA_1000T;
		seed |= NFE_SEED_1000T;
		phy  |= NFE_PHY_1000T;
		break;
	case IFM_100_TX:
		link |= NFE_MEDIA_100TX;
		seed |= NFE_SEED_100TX;
		phy  |= NFE_PHY_100TX;
		break;
	case IFM_10_T:
		link |= NFE_MEDIA_10T;
		seed |= NFE_SEED_10T;
		break;
	}

	NFE_WRITE(sc, NFE_RNDSEED, seed);	/* XXX: gigabit NICs only? */

	NFE_WRITE(sc, NFE_PHY_IFACE, phy);
	NFE_WRITE(sc, NFE_MISC1, misc);
	NFE_WRITE(sc, NFE_LINKSPEED, link);
}

static int
nfe_miibus_readreg(device_t dev, int phy, int reg)
{
	struct nfe_softc *sc = device_get_softc(dev);
	uint32_t val;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_CTL, (phy << NFE_PHYADD_SHIFT) | reg);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
	if (ntries == 1000) {
		DPRINTFN(sc, 2, "timeout waiting for PHY %s\n", "");
		return 0;
	}

	if (NFE_READ(sc, NFE_PHY_STATUS) & NFE_PHY_ERROR) {
		DPRINTFN(sc, 2, "could not read PHY %s\n", "");
		return 0;
	}

	val = NFE_READ(sc, NFE_PHY_DATA);
	if (val != 0xffffffff && val != 0)
		sc->mii_phyaddr = phy;

	DPRINTFN(sc, 2, "mii read phy %d reg 0x%x ret 0x%x\n", phy, reg, val);

	return val;
}

static void
nfe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct nfe_softc *sc = device_get_softc(dev);
	uint32_t ctl;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_DATA, val);
	ctl = NFE_PHY_WRITE | (phy << NFE_PHYADD_SHIFT) | reg;
	NFE_WRITE(sc, NFE_PHY_CTL, ctl);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}

#ifdef NFE_DEBUG
	if (ntries == 1000)
		DPRINTFN(sc, 2, "could not write to PHY %s\n", "");
#endif
}

#ifdef DEVICE_POLLING

static void
nfe_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct nfe_softc *sc = ifp->if_softc;

	switch(cmd) {
	case POLL_REGISTER:
		/* Disable interrupts */
		NFE_WRITE(sc, NFE_IRQ_MASK, 0);
		break;
	case POLL_DEREGISTER:
		/* enable interrupts */
		NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);
		break;
	case POLL_AND_CHECK_STATUS:
		/* fall through */
	case POLL_ONLY:
		if (ifp->if_flags & IFF_RUNNING) {
			nfe_rxeof(sc);
			nfe_txeof(sc);
		}
		break;
	}
}

#endif

static void
nfe_intr(void *arg)
{
	struct nfe_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t r;

	r = NFE_READ(sc, NFE_IRQ_STATUS);
	if (r == 0)
		return;	/* not for us */
	NFE_WRITE(sc, NFE_IRQ_STATUS, r);

	DPRINTFN(sc, 5, "%s: interrupt register %x\n", __func__, r);

	if (r & NFE_IRQ_LINK) {
		NFE_READ(sc, NFE_PHY_STATUS);
		NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
		DPRINTF(sc, "link state changed %s\n", "");
	}

	if (ifp->if_flags & IFF_RUNNING) {
		/* check Rx ring */
		nfe_rxeof(sc);

		/* check Tx ring */
		nfe_txeof(sc);
	}
}

static int
nfe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0, mask;

	switch (cmd) {
	case SIOCSIFMTU:
		/* XXX NFE_USE_JUMBO should be set here */
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->sc_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
				nfe_setmulti(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					nfe_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				nfe_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			nfe_setmulti(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->sc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
        case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_HWCSUM) {
			if (IFCAP_HWCSUM & ifp->if_capenable)
				ifp->if_capenable &= ~IFCAP_HWCSUM;
			else
				ifp->if_capenable |= IFCAP_HWCSUM;
		}
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static void
nfe_rxeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nfe_rx_ring *ring = &sc->rxq;
	int reap;

	reap = 0;
	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_POSTREAD);

	for (;;) {
		struct nfe_rx_data *data = &ring->data[ring->cur];
		struct mbuf *m;
		uint16_t flags;
		int len, error;

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			struct nfe_desc64 *desc64 = &ring->desc64[ring->cur];

			flags = le16toh(desc64->flags);
			len = le16toh(desc64->length) & 0x3fff;
		} else {
			struct nfe_desc32 *desc32 = &ring->desc32[ring->cur];

			flags = le16toh(desc32->flags);
			len = le16toh(desc32->length) & 0x3fff;
		}

		if (flags & NFE_RX_READY)
			break;

		reap = 1;

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_RX_VALID_V1))
				goto skip;

			if ((flags & NFE_RX_FIXME_V1) == NFE_RX_FIXME_V1) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		} else {
			if (!(flags & NFE_RX_VALID_V2))
				goto skip;

			if ((flags & NFE_RX_FIXME_V2) == NFE_RX_FIXME_V2) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		}

		if (flags & NFE_RX_ERROR) {
			ifp->if_ierrors++;
			goto skip;
		}

		m = data->m;

		if (sc->sc_flags & NFE_USE_JUMBO)
			error = nfe_newbuf_jumbo(sc, ring, ring->cur, 0);
		else
			error = nfe_newbuf_std(sc, ring, ring->cur, 0);
		if (error) {
			ifp->if_ierrors++;
			goto skip;
		}

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = len;
		m->m_pkthdr.rcvif = ifp;

#ifdef notyet
		if (sc->sc_flags & NFE_HW_CSUM) {
			if (flags & NFE_RX_IP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
			if (flags & NFE_RX_UDP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
			if (flags & NFE_RX_TCP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		}
#elif defined(NFE_CSUM)
		if ((sc->sc_flags & NFE_HW_CSUM) && (flags & NFE_RX_CSUMOK))
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;
#endif

		ifp->if_ipackets++;
		ifp->if_input(ifp, m);
skip:
		nfe_set_ready_rxdesc(sc, ring, ring->cur);
		sc->rxq.cur = (sc->rxq.cur + 1) % NFE_RX_RING_COUNT;
	}

	if (reap)
		bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_PREWRITE);
}

static void
nfe_txeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nfe_tx_ring *ring = &sc->txq;
	struct nfe_tx_data *data = NULL;

	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_POSTREAD);
	while (ring->next != ring->cur) {
		uint16_t flags;

		if (sc->sc_flags & NFE_40BIT_ADDR)
			flags = le16toh(ring->desc64[ring->next].flags);
		else
			flags = le16toh(ring->desc32[ring->next].flags);

		if (flags & NFE_TX_VALID)
			break;

		data = &ring->data[ring->next];

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_TX_LASTFRAG_V1) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V1) != 0) {
				if_printf(ifp, "tx v1 error 0x%4b\n", flags,
					  NFE_V1_TXERR);
				ifp->if_oerrors++;
			} else {
				ifp->if_opackets++;
			}
		} else {
			if (!(flags & NFE_TX_LASTFRAG_V2) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V2) != 0) {
				if_printf(ifp, "tx v2 error 0x%4b\n", flags,
					  NFE_V2_TXERR);
				ifp->if_oerrors++;
			} else {
				ifp->if_opackets++;
			}
		}

		if (data->m == NULL) {	/* should not get there */
			if_printf(ifp,
				  "last fragment bit w/o associated mbuf!\n");
			goto skip;
		}

		/* last fragment of the mbuf chain transmitted */
		bus_dmamap_sync(ring->data_tag, data->map,
				BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_tag, data->map);
		m_freem(data->m);
		data->m = NULL;

		ifp->if_timer = 0;
skip:
		ring->queued--;
		KKASSERT(ring->queued >= 0);
		ring->next = (ring->next + 1) % NFE_TX_RING_COUNT;
	}

	if (data != NULL) {	/* at least one slot freed */
		ifp->if_flags &= ~IFF_OACTIVE;
		ifp->if_start(ifp);
	}
}

static int
nfe_encap(struct nfe_softc *sc, struct nfe_tx_ring *ring, struct mbuf *m0)
{
	struct nfe_dma_ctx ctx;
	bus_dma_segment_t segs[NFE_MAX_SCATTER];
	struct nfe_tx_data *data, *data_map;
	bus_dmamap_t map;
	struct nfe_desc64 *desc64 = NULL;
	struct nfe_desc32 *desc32 = NULL;
	uint16_t flags = 0;
	uint32_t vtag = 0;
	int error, i, j;

	data = &ring->data[ring->cur];
	map = data->map;
	data_map = data;	/* Remember who owns the DMA map */

	ctx.nsegs = NFE_MAX_SCATTER;
	ctx.segs = segs;
	error = bus_dmamap_load_mbuf(ring->data_tag, map, m0,
				     nfe_buf_dma_addr, &ctx, BUS_DMA_NOWAIT);
	if (error && error != EFBIG) {
		if_printf(&sc->arpcom.ac_if, "could not map TX mbuf\n");
		goto back;
	}

	if (error) {	/* error == EFBIG */
		struct mbuf *m_new;

		m_new = m_defrag(m0, MB_DONTWAIT);
		if (m_new == NULL) {
			if_printf(&sc->arpcom.ac_if,
				  "could not defrag TX mbuf\n");
			error = ENOBUFS;
			goto back;
		} else {
			m0 = m_new;
		}

		ctx.nsegs = NFE_MAX_SCATTER;
		ctx.segs = segs;
		error = bus_dmamap_load_mbuf(ring->data_tag, map, m0,
					     nfe_buf_dma_addr, &ctx,
					     BUS_DMA_NOWAIT);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not map defraged TX mbuf\n");
			goto back;
		}
	}

	error = 0;

	if (ring->queued + ctx.nsegs >= NFE_TX_RING_COUNT - 1) {
		bus_dmamap_unload(ring->data_tag, map);
		error = ENOBUFS;
		goto back;
	}

	/* setup h/w VLAN tagging */
	if ((m0->m_flags & (M_PROTO1 | M_PKTHDR)) == (M_PROTO1 | M_PKTHDR) &&
	    m0->m_pkthdr.rcvif != NULL &&
	    m0->m_pkthdr.rcvif->if_type == IFT_L2VLAN) {
		struct ifvlan *ifv = m0->m_pkthdr.rcvif->if_softc;

		if (ifv != NULL)
			vtag = NFE_TX_VTAG | htons(ifv->ifv_tag);
	}

#ifdef NFE_CSUM
	if (m0->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
		flags |= NFE_TX_IP_CSUM;
	if (m0->m_pkthdr.csum_flags & (M_TCPV4_CSUM_OUT | M_UDPV4_CSUM_OUT))
		flags |= NFE_TX_TCP_CSUM;
#endif

	/*
	 * XXX urm. somebody is unaware of how hardware works.  You 
	 * absolutely CANNOT set NFE_TX_VALID on the next descriptor in
	 * the ring until the entire chain is actually *VALID*.  Otherwise
	 * the hardware may encounter a partially initialized chain that
	 * is marked as being ready to go when it in fact is not ready to
	 * go.
	 */

	for (i = 0; i < ctx.nsegs; i++) {
		j = (ring->cur + i) % NFE_TX_RING_COUNT;
		data = &ring->data[j];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &ring->desc64[j];
#if defined(__LP64__)
			desc64->physaddr[0] =
			    htole32(segs[i].ds_addr >> 32);
#endif
			desc64->physaddr[1] =
			    htole32(segs[i].ds_addr & 0xffffffff);
			desc64->length = htole16(segs[i].ds_len - 1);
			desc64->vtag = htole32(vtag);
			desc64->flags = htole16(flags);
		} else {
			desc32 = &ring->desc32[j];
			desc32->physaddr = htole32(segs[i].ds_addr);
			desc32->length = htole16(segs[i].ds_len - 1);
			desc32->flags = htole16(flags);
		}

		/* csum flags and vtag belong to the first fragment only */
		flags &= ~(NFE_TX_IP_CSUM | NFE_TX_TCP_CSUM);
		vtag = 0;

		ring->queued++;
		KKASSERT(ring->queued <= NFE_TX_RING_COUNT);
	}

	/* the whole mbuf chain has been DMA mapped, fix last descriptor */
	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc64->flags |= htole16(NFE_TX_LASTFRAG_V2);
	} else {
		if (sc->sc_flags & NFE_JUMBO_SUP)
			flags = NFE_TX_LASTFRAG_V2;
		else
			flags = NFE_TX_LASTFRAG_V1;
		desc32->flags |= htole16(flags);
	}

	/*
	 * Set NFE_TX_VALID backwards so the hardware doesn't see the
	 * whole mess until the first descriptor in the map is flagged.
	 */
	for (i = ctx.nsegs - 1; i >= 0; --i) {
		j = (ring->cur + i) % NFE_TX_RING_COUNT;
		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &ring->desc64[j];
			desc64->flags |= htole16(NFE_TX_VALID);
		} else {
			desc32 = &ring->desc32[j];
			desc32->flags |= htole16(NFE_TX_VALID);
		}
	}
	ring->cur = (ring->cur + ctx.nsegs) % NFE_TX_RING_COUNT;

	/* Exchange DMA map */
	data_map->map = data->map;
	data->map = map;
	data->m = m0;

	bus_dmamap_sync(ring->data_tag, map, BUS_DMASYNC_PREWRITE);
back:
	if (error)
		m_freem(m0);
	return error;
}

static void
nfe_start(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct nfe_tx_ring *ring = &sc->txq;
	int count = 0;
	struct mbuf *m0;

	if (ifp->if_flags & IFF_OACTIVE)
		return;

	if (ifq_is_empty(&ifp->if_snd))
		return;

	for (;;) {
		m0 = ifq_dequeue(&ifp->if_snd, NULL);
		if (m0 == NULL)
			break;

		BPF_MTAP(ifp, m0);

		if (nfe_encap(sc, ring, m0) != 0) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}
		++count;

		/*
		 * NOTE:
		 * `m0' may be freed in nfe_encap(), so
		 * it should not be touched any more.
		 */
	}
	if (count == 0)	/* nothing sent */
		return;

	/* Sync TX descriptor ring */
	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_PREWRITE);

	/* Kick Tx */
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_KICKTX | sc->rxtxctl);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
nfe_watchdog(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;

	if (ifp->if_flags & IFF_RUNNING) {
		if_printf(ifp, "watchdog timeout - lost interrupt recovered\n");
		nfe_txeof(sc);
		return;
	}

	if_printf(ifp, "watchdog timeout\n");

	nfe_init(ifp->if_softc);

	ifp->if_oerrors++;

	if (!ifq_is_empty(&ifp->if_snd))
		ifp->if_start(ifp);
}

static void
nfe_init(void *xsc)
{
	struct nfe_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t tmp;
	int error;

	nfe_stop(sc);

	error = nfe_init_tx_ring(sc, &sc->txq);
	if (error) {
		nfe_stop(sc);
		return;
	}

	error = nfe_init_rx_ring(sc, &sc->rxq);
	if (error) {
		nfe_stop(sc);
		return;
	}

	NFE_WRITE(sc, NFE_TX_UNK, 0);
	NFE_WRITE(sc, NFE_STATUS, 0);

	sc->rxtxctl = NFE_RXTX_BIT2;
	if (sc->sc_flags & NFE_40BIT_ADDR)
		sc->rxtxctl |= NFE_RXTX_V3MAGIC;
	else if (sc->sc_flags & NFE_JUMBO_SUP)
		sc->rxtxctl |= NFE_RXTX_V2MAGIC;
#ifdef NFE_CSUM
	if (sc->sc_flags & NFE_HW_CSUM)
		sc->rxtxctl |= NFE_RXTX_RXCSUM;
#endif

	/*
	 * Although the adapter is capable of stripping VLAN tags from received
	 * frames (NFE_RXTX_VTAG_STRIP), we do not enable this functionality on
	 * purpose.  This will be done in software by our network stack.
	 */
	if (sc->sc_flags & NFE_HW_VLAN)
		sc->rxtxctl |= NFE_RXTX_VTAG_INSERT;

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);

	if (sc->sc_flags & NFE_HW_VLAN)
		NFE_WRITE(sc, NFE_VTAG_CTL, NFE_VTAG_ENABLE);

	NFE_WRITE(sc, NFE_SETUP_R6, 0);

	/* set MAC address */
	nfe_set_macaddr(sc, sc->arpcom.ac_enaddr);

	/* tell MAC where rings are in memory */
#ifdef __LP64__
	NFE_WRITE(sc, NFE_RX_RING_ADDR_HI, sc->rxq.physaddr >> 32);
#endif
	NFE_WRITE(sc, NFE_RX_RING_ADDR_LO, sc->rxq.physaddr & 0xffffffff);
#ifdef __LP64__
	NFE_WRITE(sc, NFE_TX_RING_ADDR_HI, sc->txq.physaddr >> 32);
#endif
	NFE_WRITE(sc, NFE_TX_RING_ADDR_LO, sc->txq.physaddr & 0xffffffff);

	NFE_WRITE(sc, NFE_RING_SIZE,
	    (NFE_RX_RING_COUNT - 1) << 16 |
	    (NFE_TX_RING_COUNT - 1));

	NFE_WRITE(sc, NFE_RXBUFSZ, sc->rxq.bufsz);

	/* force MAC to wakeup */
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_WAKEUP);
	DELAY(10);
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_VALID);

#if 1
	/* configure interrupts coalescing/mitigation */
	NFE_WRITE(sc, NFE_IMTIMER, NFE_IM_DEFAULT);
#else
	/* no interrupt mitigation: one interrupt per packet */
	NFE_WRITE(sc, NFE_IMTIMER, 970);
#endif

	NFE_WRITE(sc, NFE_SETUP_R1, NFE_R1_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R6, NFE_R6_MAGIC);

	/* update MAC knowledge of PHY; generates a NFE_IRQ_LINK interrupt */
	NFE_WRITE(sc, NFE_STATUS, sc->mii_phyaddr << 24 | NFE_STATUS_MAGIC);

	NFE_WRITE(sc, NFE_SETUP_R4, NFE_R4_MAGIC);
	NFE_WRITE(sc, NFE_WOL_CTL, NFE_WOL_MAGIC);

	sc->rxtxctl &= ~NFE_RXTX_BIT2;
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT1 | sc->rxtxctl);

	/* set Rx filter */
	nfe_setmulti(sc);

	nfe_ifmedia_upd(ifp);

	/* enable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, NFE_RX_START);

	/* enable Tx */
	NFE_WRITE(sc, NFE_TX_CTL, NFE_TX_START);

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

#ifdef DEVICE_POLLING
	if ((ifp->if_flags & IFF_POLLING) == 0)
#endif
	/* enable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);

	callout_reset(&sc->sc_tick_ch, hz, nfe_tick, sc);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;
}

static void
nfe_stop(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	callout_stop(&sc->sc_tick_ch);

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	/* Abort Tx */
	NFE_WRITE(sc, NFE_TX_CTL, 0);

	/* Disable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, 0);

	/* Disable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, 0);

	/* Reset Tx and Rx rings */
	nfe_reset_tx_ring(sc, &sc->txq);
	nfe_reset_rx_ring(sc, &sc->rxq);
}

static int
nfe_alloc_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i, j, error, descsize;
	void **desc;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof(struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof(struct nfe_desc32);
	}

	ring->bufsz = MCLBYTES;
	ring->cur = ring->next = 0;

	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   NFE_RX_RING_COUNT * descsize, 1,
				   NFE_RX_RING_COUNT * descsize,
				   0, &ring->tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create desc RX DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(ring->tag, desc, BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &ring->map);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not allocate RX desc DMA memory\n");
		bus_dma_tag_destroy(ring->tag);
		ring->tag = NULL;
		return error;
	}

	error = bus_dmamap_load(ring->tag, ring->map, *desc,
				NFE_RX_RING_COUNT * descsize,
				nfe_ring_dma_addr, &ring->physaddr,
				BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not load RX desc DMA map\n");
		bus_dmamem_free(ring->tag, *desc, ring->map);
		bus_dma_tag_destroy(ring->tag);
		ring->tag = NULL;
		return error;
	}

	if (sc->sc_flags & NFE_USE_JUMBO) {
		ring->bufsz = NFE_JBYTES;

		error = nfe_jpool_alloc(sc, ring);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not allocate jumbo frames\n");
			return error;
		}
	}

	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MCLBYTES, 1, MCLBYTES,
				   0, &ring->data_tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create RX mbuf DMA tag\n");
		return error;
	}

	/* Create a spare RX mbuf DMA map */
	error = bus_dmamap_create(ring->data_tag, 0, &ring->data_tmpmap);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create spare RX mbuf DMA map\n");
		bus_dma_tag_destroy(ring->data_tag);
		ring->data_tag = NULL;
		return error;
	}

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		error = bus_dmamap_create(ring->data_tag, 0,
					  &ring->data[i].map);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not create %dth RX mbuf DMA mapn", i);
			goto fail;
		}
	}
	return 0;
fail:
	for (j = 0; j < i; ++j)
		bus_dmamap_destroy(ring->data_tag, ring->data[i].map);
	bus_dmamap_destroy(ring->data_tag, ring->data_tmpmap);
	bus_dma_tag_destroy(ring->data_tag);
	ring->data_tag = NULL;
	return error;
}

static void
nfe_reset_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i;

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		struct nfe_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_unload(ring->data_tag, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}
	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

static int
nfe_init_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i;

	for (i = 0; i < NFE_RX_RING_COUNT; ++i) {
		int error;

		/* XXX should use a function pointer */
		if (sc->sc_flags & NFE_USE_JUMBO)
			error = nfe_newbuf_jumbo(sc, ring, i, 1);
		else
			error = nfe_newbuf_std(sc, ring, i, 1);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not allocate RX buffer\n");
			return error;
		}

		nfe_set_ready_rxdesc(sc, ring, i);
	}
	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_PREWRITE);

	return 0;
}

static void
nfe_free_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	if (ring->data_tag != NULL) {
		struct nfe_rx_data *data;
		int i;

		for (i = 0; i < NFE_RX_RING_COUNT; i++) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_unload(ring->data_tag, data->map);
				m_freem(data->m);
			}
			bus_dmamap_destroy(ring->data_tag, data->map);
		}
		bus_dmamap_destroy(ring->data_tag, ring->data_tmpmap);
		bus_dma_tag_destroy(ring->data_tag);
	}

	nfe_jpool_free(sc, ring);

	if (ring->tag != NULL) {
		void *desc;

		if (sc->sc_flags & NFE_40BIT_ADDR)
			desc = ring->desc64;
		else
			desc = ring->desc32;

		bus_dmamap_unload(ring->tag, ring->map);
		bus_dmamem_free(ring->tag, desc, ring->map);
		bus_dma_tag_destroy(ring->tag);
	}
}

static struct nfe_jbuf *
nfe_jalloc(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nfe_jbuf *jbuf;

	lwkt_serialize_enter(&sc->sc_jbuf_serializer);

	jbuf = SLIST_FIRST(&sc->rxq.jfreelist);
	if (jbuf != NULL) {
		SLIST_REMOVE_HEAD(&sc->rxq.jfreelist, jnext);
		jbuf->inuse = 1;
	} else {
		if_printf(ifp, "no free jumbo buffer\n");
	}

	lwkt_serialize_exit(&sc->sc_jbuf_serializer);

	return jbuf;
}

static void
nfe_jfree(void *arg)
{
	struct nfe_jbuf *jbuf = arg;
	struct nfe_softc *sc = jbuf->sc;
	struct nfe_rx_ring *ring = jbuf->ring;

	if (&ring->jbuf[jbuf->slot] != jbuf)
		panic("%s: free wrong jumbo buffer\n", __func__);
	else if (jbuf->inuse == 0)
		panic("%s: jumbo buffer already freed\n", __func__);

	lwkt_serialize_enter(&sc->sc_jbuf_serializer);
	atomic_subtract_int(&jbuf->inuse, 1);
	if (jbuf->inuse == 0)
		SLIST_INSERT_HEAD(&ring->jfreelist, jbuf, jnext);
	lwkt_serialize_exit(&sc->sc_jbuf_serializer);
}

static void
nfe_jref(void *arg)
{
	struct nfe_jbuf *jbuf = arg;
	struct nfe_rx_ring *ring = jbuf->ring;

	if (&ring->jbuf[jbuf->slot] != jbuf)
		panic("%s: ref wrong jumbo buffer\n", __func__);
	else if (jbuf->inuse == 0)
		panic("%s: jumbo buffer already freed\n", __func__);

	atomic_subtract_int(&jbuf->inuse, 1);
}

static int
nfe_jpool_alloc(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_jbuf *jbuf;
	bus_addr_t physaddr;
	caddr_t buf;
	int i, error;

	/*
	 * Allocate a big chunk of DMA'able memory.
	 */
	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   NFE_JPOOL_SIZE, 1, NFE_JPOOL_SIZE,
				   0, &ring->jtag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create jumbo DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(ring->jtag, (void **)&ring->jpool,
				 BUS_DMA_WAITOK, &ring->jmap);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not allocate jumbo DMA memory\n");
		bus_dma_tag_destroy(ring->jtag);
		ring->jtag = NULL;
		return error;
	}

	error = bus_dmamap_load(ring->jtag, ring->jmap, ring->jpool,
				NFE_JPOOL_SIZE, nfe_ring_dma_addr, &physaddr,
				BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not load jumbo DMA map\n");
		bus_dmamem_free(ring->jtag, ring->jpool, ring->jmap);
		bus_dma_tag_destroy(ring->jtag);
		ring->jtag = NULL;
		return error;
	}

	/* ..and split it into 9KB chunks */
	SLIST_INIT(&ring->jfreelist);

	buf = ring->jpool;
	for (i = 0; i < NFE_JPOOL_COUNT; i++) {
		jbuf = &ring->jbuf[i];

		jbuf->sc = sc;
		jbuf->ring = ring;
		jbuf->inuse = 0;
		jbuf->slot = i;
		jbuf->buf = buf;
		jbuf->physaddr = physaddr;

		SLIST_INSERT_HEAD(&ring->jfreelist, jbuf, jnext);

		buf += NFE_JBYTES;
		physaddr += NFE_JBYTES;
	}

	return 0;
}

static void
nfe_jpool_free(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	if (ring->jtag != NULL) {
		bus_dmamap_unload(ring->jtag, ring->jmap);
		bus_dmamem_free(ring->jtag, ring->jpool, ring->jmap);
		bus_dma_tag_destroy(ring->jtag);
	}
}

static int
nfe_alloc_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	int i, j, error, descsize;
	void **desc;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof(struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof(struct nfe_desc32);
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;

	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   NFE_TX_RING_COUNT * descsize, 1,
				   NFE_TX_RING_COUNT * descsize,
				   0, &ring->tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create TX desc DMA map\n");
		return error;
	}

	error = bus_dmamem_alloc(ring->tag, desc, BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &ring->map);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not allocate TX desc DMA memory\n");
		bus_dma_tag_destroy(ring->tag);
		ring->tag = NULL;
		return error;
	}

	error = bus_dmamap_load(ring->tag, ring->map, *desc,
				NFE_TX_RING_COUNT * descsize,
				nfe_ring_dma_addr, &ring->physaddr,
				BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not load TX desc DMA map\n");
		bus_dmamem_free(ring->tag, *desc, ring->map);
		bus_dma_tag_destroy(ring->tag);
		ring->tag = NULL;
		return error;
	}

	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   NFE_JBYTES * NFE_MAX_SCATTER,
				   NFE_MAX_SCATTER, NFE_JBYTES,
				   0, &ring->data_tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create TX buf DMA tag\n");
		return error;
	}

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		error = bus_dmamap_create(ring->data_tag, 0,
					  &ring->data[i].map);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not create %dth TX buf DMA map\n", i);
			goto fail;
		}
	}

	return 0;
fail:
	for (j = 0; j < i; ++j)
		bus_dmamap_destroy(ring->data_tag, ring->data[i].map);
	bus_dma_tag_destroy(ring->data_tag);
	ring->data_tag = NULL;
	return error;
}

static void
nfe_reset_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	int i;

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		struct nfe_tx_data *data = &ring->data[i];

		if (sc->sc_flags & NFE_40BIT_ADDR)
			ring->desc64[i].flags = 0;
		else
			ring->desc32[i].flags = 0;

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_tag, data->map,
					BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_tag, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}
	bus_dmamap_sync(ring->tag, ring->map, BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = 0;
}

static int
nfe_init_tx_ring(struct nfe_softc *sc __unused,
		 struct nfe_tx_ring *ring __unused)
{
	return 0;
}

static void
nfe_free_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	if (ring->data_tag != NULL) {
		struct nfe_tx_data *data;
		int i;

		for (i = 0; i < NFE_TX_RING_COUNT; ++i) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_unload(ring->data_tag, data->map);
				m_freem(data->m);
			}
			bus_dmamap_destroy(ring->data_tag, data->map);
		}

		bus_dma_tag_destroy(ring->data_tag);
	}

	if (ring->tag != NULL) {
		void *desc;

		if (sc->sc_flags & NFE_40BIT_ADDR)
			desc = ring->desc64;
		else
			desc = ring->desc32;

		bus_dmamap_unload(ring->tag, ring->map);
		bus_dmamem_free(ring->tag, desc, ring->map);
		bus_dma_tag_destroy(ring->tag);
	}
}

static int
nfe_ifmedia_upd(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return 0;
}

static void
nfe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

static void
nfe_setmulti(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint8_t addr[ETHER_ADDR_LEN], mask[ETHER_ADDR_LEN];
	uint32_t filter = NFE_RXFILTER_MAGIC;
	int i;

	if ((ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
		bzero(addr, ETHER_ADDR_LEN);
		bzero(mask, ETHER_ADDR_LEN);
		goto done;
	}

	bcopy(etherbroadcastaddr, addr, ETHER_ADDR_LEN);
	bcopy(etherbroadcastaddr, mask, ETHER_ADDR_LEN);

	LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		caddr_t maddr;

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		maddr = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			addr[i] &= maddr[i];
			mask[i] &= ~maddr[i];
		}
	}

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		mask[i] |= addr[i];

done:
	addr[0] |= 0x01;	/* make sure multicast bit is set */

	NFE_WRITE(sc, NFE_MULTIADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
	NFE_WRITE(sc, NFE_MULTIADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MULTIMASK_HI,
	    mask[3] << 24 | mask[2] << 16 | mask[1] << 8 | mask[0]);
	NFE_WRITE(sc, NFE_MULTIMASK_LO,
	    mask[5] <<  8 | mask[4]);

	filter |= (ifp->if_flags & IFF_PROMISC) ? NFE_PROMISC : NFE_U2M;
	NFE_WRITE(sc, NFE_RXFILTER, filter);
}

static void
nfe_get_macaddr(struct nfe_softc *sc, uint8_t *addr)
{
	uint32_t tmp;

	tmp = NFE_READ(sc, NFE_MACADDR_LO);
	addr[0] = (tmp >> 8) & 0xff;
	addr[1] = (tmp & 0xff);

	tmp = NFE_READ(sc, NFE_MACADDR_HI);
	addr[2] = (tmp >> 24) & 0xff;
	addr[3] = (tmp >> 16) & 0xff;
	addr[4] = (tmp >>  8) & 0xff;
	addr[5] = (tmp & 0xff);
}

static void
nfe_set_macaddr(struct nfe_softc *sc, const uint8_t *addr)
{
	NFE_WRITE(sc, NFE_MACADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MACADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
}

static void
nfe_tick(void *arg)
{
	struct nfe_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	lwkt_serialize_enter(ifp->if_serializer);

	mii_tick(mii);
	callout_reset(&sc->sc_tick_ch, hz, nfe_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
nfe_ring_dma_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	if (error)
		return;

	KASSERT(nseg == 1, ("too many segments, should be 1\n"));

	*((uint32_t *)arg) = seg->ds_addr;
}

static void
nfe_buf_dma_addr(void *arg, bus_dma_segment_t *segs, int nsegs,
		 bus_size_t mapsz __unused, int error)
{
	struct nfe_dma_ctx *ctx = arg;
	int i;

	if (error)
		return;

	KASSERT(nsegs <= ctx->nsegs,
		("too many segments(%d), should be <= %d\n",
		 nsegs, ctx->nsegs));

	ctx->nsegs = nsegs;
	for (i = 0; i < nsegs; ++i)
		ctx->segs[i] = segs[i];
}

static int
nfe_newbuf_std(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
	       int wait)
{
	struct nfe_rx_data *data = &ring->data[idx];
	struct nfe_dma_ctx ctx;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct mbuf *m;
	int error;

	m = m_getcl(wait ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return ENOBUFS;
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	ctx.nsegs = 1;
	ctx.segs = &seg;
	error = bus_dmamap_load_mbuf(ring->data_tag, ring->data_tmpmap,
				     m, nfe_buf_dma_addr, &ctx,
				     wait ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if_printf(&sc->arpcom.ac_if, "could map RX mbuf %d\n", error);
		return error;
	}

	/* Unload originally mapped mbuf */
	bus_dmamap_unload(ring->data_tag, data->map);

	/* Swap this DMA map with tmp DMA map */
	map = data->map;
	data->map = ring->data_tmpmap;
	ring->data_tmpmap = map;

	/* Caller is assumed to have collected the old mbuf */
	data->m = m;

	nfe_set_paddr_rxdesc(sc, ring, idx, seg.ds_addr);

	bus_dmamap_sync(ring->data_tag, data->map, BUS_DMASYNC_PREREAD);
	return 0;
}

static int
nfe_newbuf_jumbo(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
		 int wait)
{
	struct nfe_rx_data *data = &ring->data[idx];
	struct nfe_jbuf *jbuf;
	struct mbuf *m;

	MGETHDR(m, wait ? MB_WAIT : MB_DONTWAIT, MT_DATA);
	if (m == NULL)
		return ENOBUFS;

	jbuf = nfe_jalloc(sc);
	if (jbuf == NULL) {
		m_freem(m);
		if_printf(&sc->arpcom.ac_if, "jumbo allocation failed "
		    "-- packet dropped!\n");
		return ENOBUFS;
	}

	m->m_ext.ext_arg = jbuf;
	m->m_ext.ext_buf = jbuf->buf;
	m->m_ext.ext_free = nfe_jfree;
	m->m_ext.ext_ref = nfe_jref;
	m->m_ext.ext_size = NFE_JBYTES;

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT;
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	/* Caller is assumed to have collected the old mbuf */
	data->m = m;

	nfe_set_paddr_rxdesc(sc, ring, idx, jbuf->physaddr);

	bus_dmamap_sync(ring->jtag, ring->jmap, BUS_DMASYNC_PREREAD);
	return 0;
}

static void
nfe_set_paddr_rxdesc(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
		     bus_addr_t physaddr)
{
	if (sc->sc_flags & NFE_40BIT_ADDR) {
		struct nfe_desc64 *desc64 = &ring->desc64[idx];

#if defined(__LP64__)
		desc64->physaddr[0] = htole32(physaddr >> 32);
#endif
		desc64->physaddr[1] = htole32(physaddr & 0xffffffff);
	} else {
		struct nfe_desc32 *desc32 = &ring->desc32[idx];

		desc32->physaddr = htole32(physaddr);
	}
}

static void
nfe_set_ready_rxdesc(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx)
{
	if (sc->sc_flags & NFE_40BIT_ADDR) {
		struct nfe_desc64 *desc64 = &ring->desc64[idx];

		desc64->length = htole16(ring->bufsz);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		struct nfe_desc32 *desc32 = &ring->desc32[idx];

		desc32->length = htole16(ring->bufsz);
		desc32->flags = htole16(NFE_RX_READY);
	}
}
