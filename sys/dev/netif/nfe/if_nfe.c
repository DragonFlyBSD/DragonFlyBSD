/*	$OpenBSD: if_nfe.c,v 1.63 2006/06/17 18:00:43 brad Exp $	*/

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

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/ifq_var.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "miibus_if.h"

#include <dev/netif/nfe/if_nfereg.h>
#include <dev/netif/nfe/if_nfevar.h>

#define NFE_CSUM
#define NFE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

static int	nfe_probe(device_t);
static int	nfe_attach(device_t);
static int	nfe_detach(device_t);
static void	nfe_shutdown(device_t);
static int	nfe_resume(device_t);
static int	nfe_suspend(device_t);

static int	nfe_miibus_readreg(device_t, int, int);
static void	nfe_miibus_writereg(device_t, int, int, int);
static void	nfe_miibus_statchg(device_t);

#ifdef IFPOLL_ENABLE
static void	nfe_npoll(struct ifnet *, struct ifpoll_info *);
static void	nfe_npoll_compat(struct ifnet *, void *, int);
static void	nfe_disable_intrs(struct nfe_softc *);
#endif
static void	nfe_intr(void *);
static int	nfe_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static int	nfe_rxeof(struct nfe_softc *);
static int	nfe_txeof(struct nfe_softc *, int);
static int	nfe_encap(struct nfe_softc *, struct nfe_tx_ring *,
			  struct mbuf *);
static void	nfe_start(struct ifnet *, struct ifaltq_subque *);
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
static void	nfe_powerup(device_t);
static void	nfe_mac_reset(struct nfe_softc *);
static void	nfe_tick(void *);
static void	nfe_set_paddr_rxdesc(struct nfe_softc *, struct nfe_rx_ring *,
				     int, bus_addr_t);
static void	nfe_set_ready_rxdesc(struct nfe_softc *, struct nfe_rx_ring *,
				     int);
static int	nfe_newbuf_std(struct nfe_softc *, struct nfe_rx_ring *, int,
			       int);
static int	nfe_newbuf_jumbo(struct nfe_softc *, struct nfe_rx_ring *, int,
				 int);
static void	nfe_enable_intrs(struct nfe_softc *);

static int	nfe_sysctl_imtime(SYSCTL_HANDLER_ARGS);

#define NFE_DEBUG
#ifdef NFE_DEBUG

static int	nfe_debug = 0;
static int	nfe_rx_ring_count = NFE_RX_RING_DEF_COUNT;
static int	nfe_tx_ring_count = NFE_TX_RING_DEF_COUNT;
/*
 * hw timer simulated interrupt moderation @4000Hz.  Negative values
 * disable the timer when the discrete interrupt rate falls below
 * the moderation rate.
 *
 * XXX 8000Hz might be better but if the interrupt is shared it can
 *     blow out the cpu.
 */
static int	nfe_imtime = -250;	/* uS */

TUNABLE_INT("hw.nfe.rx_ring_count", &nfe_rx_ring_count);
TUNABLE_INT("hw.nfe.tx_ring_count", &nfe_tx_ring_count);
TUNABLE_INT("hw.nfe.imtimer", &nfe_imtime);
TUNABLE_INT("hw.nfe.debug", &nfe_debug);

#define DPRINTF(sc, fmt, ...) do {		\
	if ((sc)->sc_debug) {			\
		if_printf(&(sc)->arpcom.ac_if,	\
			  fmt, __VA_ARGS__);	\
	}					\
} while (0)

#define DPRINTFN(sc, lv, fmt, ...) do {		\
	if ((sc)->sc_debug >= (lv)) {		\
		if_printf(&(sc)->arpcom.ac_if,	\
			  fmt, __VA_ARGS__);	\
	}					\
} while (0)

#else	/* !NFE_DEBUG */

#define DPRINTF(sc, fmt, ...)
#define DPRINTFN(sc, lv, fmt, ...)

#endif	/* NFE_DEBUG */

static const struct nfe_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} nfe_devices[] = {
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_LAN,
	  "NVIDIA nForce Fast Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_LAN,
	  "NVIDIA nForce2 Fast Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN1,
	  "NVIDIA nForce3 Gigabit Ethernet" },

	/* XXX TGEN the next chip can also be found in the nForce2 Ultra 400Gb
	   chipset, and possibly also the 400R; it might be both nForce2- and
	   nForce3-based boards can use the same MCPs (= southbridges) */
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
	  "NVIDIA MCP55 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN1,
	  "NVIDIA MCP61 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN2,
	  "NVIDIA MCP61 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN3,
	  "NVIDIA MCP61 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN4,
	  "NVIDIA MCP61 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN1,
	  "NVIDIA MCP65 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN2,
	  "NVIDIA MCP65 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN3,
	  "NVIDIA MCP65 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN4,
	  "NVIDIA MCP65 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN1,
	  "NVIDIA MCP67 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN2,
	  "NVIDIA MCP67 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN3,
	  "NVIDIA MCP67 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN4,
	  "NVIDIA MCP67 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN1,
	  "NVIDIA MCP73 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN2,
	  "NVIDIA MCP73 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN3,
	  "NVIDIA MCP73 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN4,
	  "NVIDIA MCP73 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN1,
	  "NVIDIA MCP77 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN2,
	  "NVIDIA MCP77 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN3,
	  "NVIDIA MCP77 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN4,
	  "NVIDIA MCP77 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN1,
	  "NVIDIA MCP79 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN2,
	  "NVIDIA MCP79 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN3,
	  "NVIDIA MCP79 Gigabit Ethernet" },

	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN4,
	  "NVIDIA MCP79 Gigabit Ethernet" },

	{ 0, 0, NULL }
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

	DEVMETHOD_END
};

static driver_t nfe_driver = {
	"nfe",
	nfe_methods,
	sizeof(struct nfe_softc)
};

static devclass_t	nfe_devclass;

DECLARE_DUMMY_MODULE(if_nfe);
MODULE_DEPEND(if_nfe, miibus, 1, 1, 1);
DRIVER_MODULE(if_nfe, pci, nfe_driver, nfe_devclass, NULL, NULL);
DRIVER_MODULE(miibus, nfe, miibus_driver, miibus_devclass, NULL, NULL);

/*
 * NOTE: NFE_WORDALIGN support is guesswork right now.
 */
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
			case PCI_PRODUCT_NVIDIA_NFORCE_LAN:
			case PCI_PRODUCT_NVIDIA_NFORCE2_LAN:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN1:
				sc->sc_caps = NFE_NO_PWRCTL |
					      NFE_FIX_EADDR;
				break;
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN2:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN3:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN4:
			case PCI_PRODUCT_NVIDIA_NFORCE3_LAN5:
				sc->sc_caps = NFE_JUMBO_SUP |
					      NFE_HW_CSUM |
					      NFE_NO_PWRCTL |
					      NFE_FIX_EADDR;
				break;
			case PCI_PRODUCT_NVIDIA_MCP51_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP51_LAN2:
				sc->sc_caps = NFE_FIX_EADDR;
				/* FALL THROUGH */
			case PCI_PRODUCT_NVIDIA_MCP61_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP61_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP61_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP61_LAN4:
			case PCI_PRODUCT_NVIDIA_MCP67_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP67_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP67_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP67_LAN4:
			case PCI_PRODUCT_NVIDIA_MCP73_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP73_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP73_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP73_LAN4:
				sc->sc_caps |= NFE_40BIT_ADDR;
				break;
			case PCI_PRODUCT_NVIDIA_CK804_LAN1:
			case PCI_PRODUCT_NVIDIA_CK804_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP04_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP04_LAN2:
				sc->sc_caps = NFE_JUMBO_SUP |
					      NFE_40BIT_ADDR |
					      NFE_HW_CSUM |
					      NFE_NO_PWRCTL |
					      NFE_FIX_EADDR;
				break;
			case PCI_PRODUCT_NVIDIA_MCP65_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP65_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP65_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP65_LAN4:
				sc->sc_caps = NFE_JUMBO_SUP |
					      NFE_40BIT_ADDR;
				break;
			case PCI_PRODUCT_NVIDIA_MCP55_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP55_LAN2:
				sc->sc_caps = NFE_JUMBO_SUP |
					      NFE_40BIT_ADDR |
					      NFE_HW_CSUM |
					      NFE_HW_VLAN |
					      NFE_FIX_EADDR;
				break;
			case PCI_PRODUCT_NVIDIA_MCP77_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP77_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP77_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP77_LAN4:
			case PCI_PRODUCT_NVIDIA_MCP79_LAN1:
			case PCI_PRODUCT_NVIDIA_MCP79_LAN2:
			case PCI_PRODUCT_NVIDIA_MCP79_LAN3:
			case PCI_PRODUCT_NVIDIA_MCP79_LAN4:
				sc->sc_caps = NFE_40BIT_ADDR |
					      NFE_HW_CSUM |
					      NFE_WORDALIGN;
				break;
			}

			device_set_desc(dev, n->desc);
			device_set_async_attach(dev, TRUE);
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
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t lowaddr;
	int error;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	lwkt_serialize_init(&sc->sc_jbuf_serializer);

	/*
	 * Initialize sysctl variables
	 */
	sc->sc_rx_ring_count = nfe_rx_ring_count;
	sc->sc_tx_ring_count = nfe_tx_ring_count;
	sc->sc_debug = nfe_debug;
	if (nfe_imtime < 0) {
		sc->sc_flags |= NFE_F_DYN_IM;
		sc->sc_imtime = -nfe_imtime;
	} else {
		sc->sc_imtime = nfe_imtime;
	}
	sc->sc_irq_enable = NFE_IRQ_ENABLE(sc);

	sc->sc_mem_rid = PCIR_BAR(0);

	if (sc->sc_caps & NFE_40BIT_ADDR)
		sc->rxtxctl_desc = NFE_RXTX_DESC_V3;
	else if (sc->sc_caps & NFE_JUMBO_SUP)
		sc->rxtxctl_desc = NFE_RXTX_DESC_V2;

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
		device_printf(dev, "could not allocate io memory\n");
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

	/* Disable WOL */
	NFE_WRITE(sc, NFE_WOL_CTL, 0);

	if ((sc->sc_caps & NFE_NO_PWRCTL) == 0)
		nfe_powerup(dev);

	nfe_get_macaddr(sc, eaddr);

	/*
	 * Allocate top level DMA tag
	 */
	if (sc->sc_caps & NFE_40BIT_ADDR)
		lowaddr = NFE_BUS_SPACE_MAXADDR;
	else
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			lowaddr,		/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
			0,			/* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			0,			/* flags */
			&sc->sc_dtag);
	if (error) {
		device_printf(dev, "could not allocate parent dma tag\n");
		goto fail;
	}

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

	/*
	 * Create sysctl tree
	 */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
			OID_AUTO, "imtimer", CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, nfe_sysctl_imtime, "I",
			"Interrupt moderation time (usec).  "
			"0 to disable interrupt moderation.");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "rx_ring_count", CTLFLAG_RD, &sc->sc_rx_ring_count,
		       0, "RX ring count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "tx_ring_count", CTLFLAG_RD, &sc->sc_tx_ring_count,
		       0, "TX ring count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "debug", CTLFLAG_RW, &sc->sc_debug,
		       0, "control debugging printfs");

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
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = nfe_npoll;
#endif
	ifp->if_watchdog = nfe_watchdog;
	ifp->if_init = nfe_init;
	ifp->if_nmbclusters = sc->sc_rx_ring_count;
	ifq_set_maxlen(&ifp->if_snd, sc->sc_tx_ring_count);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	if (sc->sc_caps & NFE_HW_VLAN)
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;

#ifdef NFE_CSUM
	if (sc->sc_caps & NFE_HW_CSUM) {
		ifp->if_capabilities |= IFCAP_HWCSUM;
		ifp->if_hwassist = NFE_CSUM_FEATURES;
	}
#else
	sc->sc_caps &= ~NFE_HW_CSUM;
#endif
	ifp->if_capenable = ifp->if_capabilities;

	callout_init(&sc->sc_tick_ch);

	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->sc_npoll, ctx, (struct sysctl_oid *)tree,
	    device_get_unit(dev), ifp->if_serializer);
#endif

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
	if (sc->sc_dtag != NULL)
		bus_dma_tag_destroy(sc->sc_dtag);

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
	if (ifp->if_flags & IFF_UP)
		nfe_init(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static void
nfe_miibus_statchg(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	struct mii_data *mii = device_get_softc(sc->sc_miibus);
	uint32_t phy, seed, misc = NFE_MISC1_MAGIC, link = NFE_MEDIA_SET;

	ASSERT_SERIALIZED(sc->arpcom.ac_if.if_serializer);

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

#ifdef IFPOLL_ENABLE

static void
nfe_npoll_compat(struct ifnet *ifp, void *arg __unused, int count __unused)
{
	struct nfe_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	nfe_rxeof(sc);
	nfe_txeof(sc, 1);
}

static void
nfe_disable_intrs(struct nfe_softc *sc)
{
	/* Disable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, 0);
	sc->sc_flags &= ~NFE_F_IRQ_TIMER;
	sc->sc_npoll.ifpc_stcount = 0;
}

static void
nfe_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct nfe_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->sc_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = nfe_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING)
			nfe_disable_intrs(sc);
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			nfe_enable_intrs(sc);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));
	}
}

#endif	/* IFPOLL_ENABLE */

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

	if (sc->sc_rate_second != time_uptime) {
		/*
		 * Calculate sc_rate_avg - interrupts per second.
		 */
		sc->sc_rate_second = time_uptime;
		if (sc->sc_rate_avg < sc->sc_rate_acc)
			sc->sc_rate_avg = sc->sc_rate_acc;
		else
			sc->sc_rate_avg = (sc->sc_rate_avg * 3 +
					   sc->sc_rate_acc) / 4;
		sc->sc_rate_acc = 0;
	} else if (sc->sc_rate_avg < sc->sc_rate_acc) {
		/*
		 * Don't wait for a tick to roll over if we are taking
		 * a lot of interrupts.
		 */
		sc->sc_rate_avg = sc->sc_rate_acc;
	}

	DPRINTFN(sc, 5, "%s: interrupt register %x\n", __func__, r);

	if (r & NFE_IRQ_LINK) {
		NFE_READ(sc, NFE_PHY_STATUS);
		NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
		DPRINTF(sc, "link state changed %s\n", "");
	}

	if (ifp->if_flags & IFF_RUNNING) {
		int ret;
		int rate;

		/* check Rx ring */
		ret = nfe_rxeof(sc);

		/* check Tx ring */
		ret |= nfe_txeof(sc, 1);

		/* update the rate accumulator */
		if (ret)
			++sc->sc_rate_acc;

		if (sc->sc_flags & NFE_F_DYN_IM) {
			rate = 1000000 / sc->sc_imtime;
			if ((sc->sc_flags & NFE_F_IRQ_TIMER) == 0 &&
			    sc->sc_rate_avg > rate) {
				/*
				 * Use the hardware timer to reduce the
				 * interrupt rate if the discrete interrupt
				 * rate has exceeded our threshold.
				 */
				NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_IMTIMER);
				sc->sc_flags |= NFE_F_IRQ_TIMER;
			} else if ((sc->sc_flags & NFE_F_IRQ_TIMER) &&
				   sc->sc_rate_avg <= rate) {
				/*
				 * Use discrete TX/RX interrupts if the rate
				 * has fallen below our threshold.
				 */
				NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_NOIMTIMER);
				sc->sc_flags &= ~NFE_F_IRQ_TIMER;

				/*
				 * Recollect, mainly to avoid the possible race
				 * introduced by changing interrupt masks.
				 */
				nfe_rxeof(sc);
				nfe_txeof(sc, 1);
			}
		}
	}
}

static int
nfe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0, mask, jumbo_cap;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFMTU:
		if ((sc->sc_caps & NFE_JUMBO_SUP) && sc->rxq.jbuf != NULL)
			jumbo_cap = 1;
		else
			jumbo_cap = 0;

		if ((jumbo_cap && ifr->ifr_mtu > NFE_JUMBO_MTU) ||
		    (!jumbo_cap && ifr->ifr_mtu > ETHERMTU)) {
			return EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_flags & IFF_RUNNING)
				nfe_init(sc);
		}
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
		mask = (ifr->ifr_reqcap ^ ifp->if_capenable) & IFCAP_HWCSUM;
		if (mask && (ifp->if_capabilities & IFCAP_HWCSUM)) {
			ifp->if_capenable ^= mask;
			if (IFCAP_TXCSUM & ifp->if_capenable)
				ifp->if_hwassist = NFE_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;

			if (ifp->if_flags & IFF_RUNNING)
				nfe_init(sc);
		}
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static int
nfe_rxeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nfe_rx_ring *ring = &sc->rxq;
	int reap;

	reap = 0;
	for (;;) {
		struct nfe_rx_data *data = &ring->data[ring->cur];
		struct mbuf *m;
		uint16_t flags;
		int len, error;

		if (sc->sc_caps & NFE_40BIT_ADDR) {
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

		if ((sc->sc_caps & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
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
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto skip;
		}

		m = data->m;

		if (sc->sc_flags & NFE_F_USE_JUMBO)
			error = nfe_newbuf_jumbo(sc, ring, ring->cur, 0);
		else
			error = nfe_newbuf_std(sc, ring, ring->cur, 0);
		if (error) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto skip;
		}

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = len;
		m->m_pkthdr.rcvif = ifp;

		if ((ifp->if_capenable & IFCAP_RXCSUM) &&
		    (flags & NFE_RX_CSUMOK)) {
			if (flags & NFE_RX_IP_CSUMOK_V2) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED |
							  CSUM_IP_VALID;
			}

			if (flags &
			    (NFE_RX_UDP_CSUMOK_V2 | NFE_RX_TCP_CSUMOK_V2)) {
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
							  CSUM_PSEUDO_HDR |
							  CSUM_FRAG_NOT_CHECKED;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		ifp->if_input(ifp, m, NULL, -1);
skip:
		nfe_set_ready_rxdesc(sc, ring, ring->cur);
		sc->rxq.cur = (sc->rxq.cur + 1) % sc->sc_rx_ring_count;
	}
	return reap;
}

static int
nfe_txeof(struct nfe_softc *sc, int start)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nfe_tx_ring *ring = &sc->txq;
	struct nfe_tx_data *data = NULL;

	while (ring->next != ring->cur) {
		uint16_t flags;

		if (sc->sc_caps & NFE_40BIT_ADDR)
			flags = le16toh(ring->desc64[ring->next].flags);
		else
			flags = le16toh(ring->desc32[ring->next].flags);

		if (flags & NFE_TX_VALID)
			break;

		data = &ring->data[ring->next];

		if ((sc->sc_caps & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_TX_LASTFRAG_V1) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V1) != 0) {
				if_printf(ifp, "tx v1 error 0x%pb%i\n",
					  NFE_V1_TXERR, flags);
				IFNET_STAT_INC(ifp, oerrors, 1);
			} else {
				IFNET_STAT_INC(ifp, opackets, 1);
			}
		} else {
			if (!(flags & NFE_TX_LASTFRAG_V2) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V2) != 0) {
				if_printf(ifp, "tx v2 error 0x%pb%i\n",
					  NFE_V2_TXERR, flags);
				IFNET_STAT_INC(ifp, oerrors, 1);
			} else {
				IFNET_STAT_INC(ifp, opackets, 1);
			}
		}

		if (data->m == NULL) {	/* should not get there */
			if_printf(ifp,
				  "last fragment bit w/o associated mbuf!\n");
			goto skip;
		}

		/* last fragment of the mbuf chain transmitted */
		bus_dmamap_unload(ring->data_tag, data->map);
		m_freem(data->m);
		data->m = NULL;
skip:
		ring->queued--;
		KKASSERT(ring->queued >= 0);
		ring->next = (ring->next + 1) % sc->sc_tx_ring_count;
	}

	if (sc->sc_tx_ring_count - ring->queued >=
	    sc->sc_tx_spare + NFE_NSEG_RSVD)
		ifq_clr_oactive(&ifp->if_snd);

	if (ring->queued == 0)
		ifp->if_timer = 0;

	if (start && !ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	if (data != NULL)
		return 1;
	else
		return 0;
}

static int
nfe_encap(struct nfe_softc *sc, struct nfe_tx_ring *ring, struct mbuf *m0)
{
	bus_dma_segment_t segs[NFE_MAX_SCATTER];
	struct nfe_tx_data *data, *data_map;
	bus_dmamap_t map;
	struct nfe_desc64 *desc64 = NULL;
	struct nfe_desc32 *desc32 = NULL;
	uint16_t flags = 0;
	uint32_t vtag = 0;
	int error, i, j, maxsegs, nsegs;

	data = &ring->data[ring->cur];
	map = data->map;
	data_map = data;	/* Remember who owns the DMA map */

	maxsegs = (sc->sc_tx_ring_count - ring->queued) - NFE_NSEG_RSVD;
	if (maxsegs > NFE_MAX_SCATTER)
		maxsegs = NFE_MAX_SCATTER;
	KASSERT(maxsegs >= sc->sc_tx_spare,
		("not enough segments %d,%d", maxsegs, sc->sc_tx_spare));

	error = bus_dmamap_load_mbuf_defrag(ring->data_tag, map, &m0,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;
	bus_dmamap_sync(ring->data_tag, map, BUS_DMASYNC_PREWRITE);

	error = 0;

	/* setup h/w VLAN tagging */
	if (m0->m_flags & M_VLANTAG)
		vtag = m0->m_pkthdr.ether_vlantag;

	if (sc->arpcom.ac_if.if_capenable & IFCAP_TXCSUM) {
		if (m0->m_pkthdr.csum_flags & CSUM_IP)
			flags |= NFE_TX_IP_CSUM;
		if (m0->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
			flags |= NFE_TX_TCP_CSUM;
	}

	/*
	 * XXX urm. somebody is unaware of how hardware works.  You
	 * absolutely CANNOT set NFE_TX_VALID on the next descriptor in
	 * the ring until the entire chain is actually *VALID*.  Otherwise
	 * the hardware may encounter a partially initialized chain that
	 * is marked as being ready to go when it in fact is not ready to
	 * go.
	 */

	for (i = 0; i < nsegs; i++) {
		j = (ring->cur + i) % sc->sc_tx_ring_count;
		data = &ring->data[j];

		if (sc->sc_caps & NFE_40BIT_ADDR) {
			desc64 = &ring->desc64[j];
			desc64->physaddr[0] =
			    htole32(NFE_ADDR_HI(segs[i].ds_addr));
			desc64->physaddr[1] =
			    htole32(NFE_ADDR_LO(segs[i].ds_addr));
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
		KKASSERT(ring->queued <= sc->sc_tx_ring_count);
	}

	/* the whole mbuf chain has been DMA mapped, fix last descriptor */
	if (sc->sc_caps & NFE_40BIT_ADDR) {
		desc64->flags |= htole16(NFE_TX_LASTFRAG_V2);
	} else {
		if (sc->sc_caps & NFE_JUMBO_SUP)
			flags = NFE_TX_LASTFRAG_V2;
		else
			flags = NFE_TX_LASTFRAG_V1;
		desc32->flags |= htole16(flags);
	}

	/*
	 * Set NFE_TX_VALID backwards so the hardware doesn't see the
	 * whole mess until the first descriptor in the map is flagged.
	 */
	for (i = nsegs - 1; i >= 0; --i) {
		j = (ring->cur + i) % sc->sc_tx_ring_count;
		if (sc->sc_caps & NFE_40BIT_ADDR) {
			desc64 = &ring->desc64[j];
			desc64->flags |= htole16(NFE_TX_VALID);
		} else {
			desc32 = &ring->desc32[j];
			desc32->flags |= htole16(NFE_TX_VALID);
		}
	}
	ring->cur = (ring->cur + nsegs) % sc->sc_tx_ring_count;

	/* Exchange DMA map */
	data_map->map = data->map;
	data->map = map;
	data->m = m0;
back:
	if (error)
		m_freem(m0);
	return error;
}

static void
nfe_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct nfe_tx_ring *ring = &sc->txq;
	int count = 0, oactive = 0;
	struct mbuf *m0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		int error;

		if (sc->sc_tx_ring_count - ring->queued <
		    sc->sc_tx_spare + NFE_NSEG_RSVD) {
			if (oactive) {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}

			nfe_txeof(sc, 0);
			oactive = 1;
			continue;
		}

		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;

		ETHER_BPF_MTAP(ifp, m0);

		error = nfe_encap(sc, ring, m0);
		if (error) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (error == EFBIG) {
				if (oactive) {
					ifq_set_oactive(&ifp->if_snd);
					break;
				}
				nfe_txeof(sc, 0);
				oactive = 1;
			}
			continue;
		} else {
			oactive = 0;
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

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING) {
		if_printf(ifp, "watchdog timeout - lost interrupt recovered\n");
		nfe_txeof(sc, 1);
		return;
	}

	if_printf(ifp, "watchdog timeout\n");

	nfe_init(ifp->if_softc);

	IFNET_STAT_INC(ifp, oerrors, 1);
}

static void
nfe_init(void *xsc)
{
	struct nfe_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t tmp;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	nfe_stop(sc);

	if ((sc->sc_caps & NFE_NO_PWRCTL) == 0)
		nfe_mac_reset(sc);

	/*
	 * NOTE:
	 * Switching between jumbo frames and normal frames should
	 * be done _after_ nfe_stop() but _before_ nfe_init_rx_ring().
	 */
	if (ifp->if_mtu > ETHERMTU) {
		sc->sc_flags |= NFE_F_USE_JUMBO;
		sc->rxq.bufsz = NFE_JBYTES;
		sc->sc_tx_spare = NFE_NSEG_SPARE_JUMBO;
		if (bootverbose)
			if_printf(ifp, "use jumbo frames\n");
	} else {
		sc->sc_flags &= ~NFE_F_USE_JUMBO;
		sc->rxq.bufsz = MCLBYTES;
		sc->sc_tx_spare = NFE_NSEG_SPARE;
		if (bootverbose)
			if_printf(ifp, "use non-jumbo frames\n");
	}

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

	NFE_WRITE(sc, NFE_TX_POLL, 0);
	NFE_WRITE(sc, NFE_STATUS, 0);

	sc->rxtxctl = NFE_RXTX_BIT2 | sc->rxtxctl_desc;

	if (ifp->if_capenable & IFCAP_RXCSUM)
		sc->rxtxctl |= NFE_RXTX_RXCSUM;

	/*
	 * Although the adapter is capable of stripping VLAN tags from received
	 * frames (NFE_RXTX_VTAG_STRIP), we do not enable this functionality on
	 * purpose.  This will be done in software by our network stack.
	 */
	if (sc->sc_caps & NFE_HW_VLAN)
		sc->rxtxctl |= NFE_RXTX_VTAG_INSERT;

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);

	if (sc->sc_caps & NFE_HW_VLAN)
		NFE_WRITE(sc, NFE_VTAG_CTL, NFE_VTAG_ENABLE);

	NFE_WRITE(sc, NFE_SETUP_R6, 0);

	/* set MAC address */
	nfe_set_macaddr(sc, sc->arpcom.ac_enaddr);

	/* tell MAC where rings are in memory */
	if (sc->sc_caps & NFE_40BIT_ADDR) {
		NFE_WRITE(sc, NFE_RX_RING_ADDR_HI,
			  NFE_ADDR_HI(sc->rxq.physaddr));
	}
	NFE_WRITE(sc, NFE_RX_RING_ADDR_LO, NFE_ADDR_LO(sc->rxq.physaddr));

	if (sc->sc_caps & NFE_40BIT_ADDR) {
		NFE_WRITE(sc, NFE_TX_RING_ADDR_HI,
			  NFE_ADDR_HI(sc->txq.physaddr));
	}
	NFE_WRITE(sc, NFE_TX_RING_ADDR_LO, NFE_ADDR_LO(sc->txq.physaddr));

	NFE_WRITE(sc, NFE_RING_SIZE,
	    (sc->sc_rx_ring_count - 1) << 16 |
	    (sc->sc_tx_ring_count - 1));

	NFE_WRITE(sc, NFE_RXBUFSZ, sc->rxq.bufsz);

	/* force MAC to wakeup */
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_WAKEUP);
	DELAY(10);
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_VALID);

	NFE_WRITE(sc, NFE_SETUP_R1, NFE_R1_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R6, NFE_R6_MAGIC);

	/* update MAC knowledge of PHY; generates a NFE_IRQ_LINK interrupt */
	NFE_WRITE(sc, NFE_STATUS, sc->mii_phyaddr << 24 | NFE_STATUS_MAGIC);

	NFE_WRITE(sc, NFE_SETUP_R4, NFE_R4_MAGIC);

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

#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		nfe_disable_intrs(sc);
	else
#endif
	nfe_enable_intrs(sc);

	callout_reset(&sc->sc_tick_ch, hz, nfe_tick, sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * If we had stuff in the tx ring before its all cleaned out now
	 * so we are not going to get an interrupt, jump-start any pending
	 * output.
	 */
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
nfe_stop(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t rxtxctl = sc->rxtxctl_desc | NFE_RXTX_BIT2;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->sc_tick_ch);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	sc->sc_flags &= ~NFE_F_IRQ_TIMER;

#define WAITMAX	50000

	/*
	 * Abort Tx
	 */
	NFE_WRITE(sc, NFE_TX_CTL, 0);
	for (i = 0; i < WAITMAX; ++i) {
		DELAY(100);
		if ((NFE_READ(sc, NFE_TX_STATUS) & NFE_TX_STATUS_BUSY) == 0)
			break;
	}
	if (i == WAITMAX)
		if_printf(ifp, "can't stop TX\n");
	DELAY(100);

	/*
	 * Disable Rx
	 */
	NFE_WRITE(sc, NFE_RX_CTL, 0);
	for (i = 0; i < WAITMAX; ++i) {
		DELAY(100);
		if ((NFE_READ(sc, NFE_RX_STATUS) & NFE_RX_STATUS_BUSY) == 0)
			break;
	}
	if (i == WAITMAX)
		if_printf(ifp, "can't stop RX\n");
	DELAY(100);

#undef WAITMAX

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, rxtxctl);

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
	bus_dmamem_t dmem;
	void **desc;

	if (sc->sc_caps & NFE_40BIT_ADDR) {
		desc = (void *)&ring->desc64;
		descsize = sizeof(struct nfe_desc64);
	} else {
		desc = (void *)&ring->desc32;
		descsize = sizeof(struct nfe_desc32);
	}

	ring->bufsz = MCLBYTES;
	ring->cur = ring->next = 0;

	error = bus_dmamem_coherent(sc->sc_dtag, PAGE_SIZE, 0,
				    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				    sc->sc_rx_ring_count * descsize,
				    BUS_DMA_WAITOK | BUS_DMA_ZERO, &dmem);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create RX desc ring\n");
		return error;
	}
	ring->tag = dmem.dmem_tag;
	ring->map = dmem.dmem_map;
	*desc = dmem.dmem_addr;
	ring->physaddr = dmem.dmem_busaddr;

	if (sc->sc_caps & NFE_JUMBO_SUP) {
		ring->jbuf =
		kmalloc(sizeof(struct nfe_jbuf) * NFE_JPOOL_COUNT(sc),
			M_DEVBUF, M_WAITOK | M_ZERO);

		error = nfe_jpool_alloc(sc, ring);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "could not allocate jumbo frames\n");
			kfree(ring->jbuf, M_DEVBUF);
			ring->jbuf = NULL;
			/* Allow jumbo frame allocation to fail */
		}
	}

	ring->data = kmalloc(sizeof(struct nfe_rx_data) * sc->sc_rx_ring_count,
			     M_DEVBUF, M_WAITOK | M_ZERO);

	error = bus_dma_tag_create(sc->sc_dtag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   MCLBYTES, 1, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,
				   &ring->data_tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create RX mbuf DMA tag\n");
		return error;
	}

	/* Create a spare RX mbuf DMA map */
	error = bus_dmamap_create(ring->data_tag, BUS_DMA_WAITOK,
				  &ring->data_tmpmap);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create spare RX mbuf DMA map\n");
		bus_dma_tag_destroy(ring->data_tag);
		ring->data_tag = NULL;
		return error;
	}

	for (i = 0; i < sc->sc_rx_ring_count; i++) {
		error = bus_dmamap_create(ring->data_tag, BUS_DMA_WAITOK,
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

	for (i = 0; i < sc->sc_rx_ring_count; i++) {
		struct nfe_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			if ((sc->sc_flags & NFE_F_USE_JUMBO) == 0)
				bus_dmamap_unload(ring->data_tag, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}

	ring->cur = ring->next = 0;
}

static int
nfe_init_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i;

	for (i = 0; i < sc->sc_rx_ring_count; ++i) {
		int error;

		/* XXX should use a function pointer */
		if (sc->sc_flags & NFE_F_USE_JUMBO)
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
	return 0;
}

static void
nfe_free_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	if (ring->data_tag != NULL) {
		struct nfe_rx_data *data;
		int i;

		for (i = 0; i < sc->sc_rx_ring_count; i++) {
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

	if (ring->jbuf != NULL)
		kfree(ring->jbuf, M_DEVBUF);
	if (ring->data != NULL)
		kfree(ring->data, M_DEVBUF);

	if (ring->tag != NULL) {
		void *desc;

		if (sc->sc_caps & NFE_40BIT_ADDR)
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
		panic("%s: free wrong jumbo buffer", __func__);
	else if (jbuf->inuse == 0)
		panic("%s: jumbo buffer already freed", __func__);

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
		panic("%s: ref wrong jumbo buffer", __func__);
	else if (jbuf->inuse == 0)
		panic("%s: jumbo buffer already freed", __func__);

	atomic_add_int(&jbuf->inuse, 1);
}

static int
nfe_jpool_alloc(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_jbuf *jbuf;
	bus_dmamem_t dmem;
	bus_addr_t physaddr;
	caddr_t buf;
	int i, error;

	/*
	 * Allocate a big chunk of DMA'able memory.
	 */
	error = bus_dmamem_coherent(sc->sc_dtag, PAGE_SIZE, 0,
				    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				    NFE_JPOOL_SIZE(sc),
				    BUS_DMA_WAITOK, &dmem);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create jumbo buffer\n");
		return error;
	}
	ring->jtag = dmem.dmem_tag;
	ring->jmap = dmem.dmem_map;
	ring->jpool = dmem.dmem_addr;
	physaddr = dmem.dmem_busaddr;

	/* ..and split it into 9KB chunks */
	SLIST_INIT(&ring->jfreelist);

	buf = ring->jpool;
	for (i = 0; i < NFE_JPOOL_COUNT(sc); i++) {
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
	bus_dmamem_t dmem;
	void **desc;

	if (sc->sc_caps & NFE_40BIT_ADDR) {
		desc = (void *)&ring->desc64;
		descsize = sizeof(struct nfe_desc64);
	} else {
		desc = (void *)&ring->desc32;
		descsize = sizeof(struct nfe_desc32);
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;

	error = bus_dmamem_coherent(sc->sc_dtag, PAGE_SIZE, 0,
				    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				    sc->sc_tx_ring_count * descsize,
				    BUS_DMA_WAITOK | BUS_DMA_ZERO, &dmem);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create TX desc ring\n");
		return error;
	}
	ring->tag = dmem.dmem_tag;
	ring->map = dmem.dmem_map;
	*desc = dmem.dmem_addr;
	ring->physaddr = dmem.dmem_busaddr;

	ring->data = kmalloc(sizeof(struct nfe_tx_data) * sc->sc_tx_ring_count,
			     M_DEVBUF, M_WAITOK | M_ZERO);

	error = bus_dma_tag_create(sc->sc_dtag, 1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			NFE_JBYTES, NFE_MAX_SCATTER, MCLBYTES,
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
			&ring->data_tag);
	if (error) {
		if_printf(&sc->arpcom.ac_if,
			  "could not create TX buf DMA tag\n");
		return error;
	}

	for (i = 0; i < sc->sc_tx_ring_count; i++) {
		error = bus_dmamap_create(ring->data_tag,
				BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
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

	for (i = 0; i < sc->sc_tx_ring_count; i++) {
		struct nfe_tx_data *data = &ring->data[i];

		if (sc->sc_caps & NFE_40BIT_ADDR)
			ring->desc64[i].flags = 0;
		else
			ring->desc32[i].flags = 0;

		if (data->m != NULL) {
			bus_dmamap_unload(ring->data_tag, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}

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

		for (i = 0; i < sc->sc_tx_ring_count; ++i) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_unload(ring->data_tag, data->map);
				m_freem(data->m);
			}
			bus_dmamap_destroy(ring->data_tag, data->map);
		}

		bus_dma_tag_destroy(ring->data_tag);
	}

	if (ring->data != NULL)
		kfree(ring->data, M_DEVBUF);

	if (ring->tag != NULL) {
		void *desc;

		if (sc->sc_caps & NFE_40BIT_ADDR)
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

	ASSERT_SERIALIZED(ifp->if_serializer);

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

	ASSERT_SERIALIZED(ifp->if_serializer);

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

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
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
	uint32_t lo, hi;

	lo = NFE_READ(sc, NFE_MACADDR_LO);
	hi = NFE_READ(sc, NFE_MACADDR_HI);
	if (sc->sc_caps & NFE_FIX_EADDR) {
		addr[0] = (lo >> 8) & 0xff;
		addr[1] = (lo & 0xff);

		addr[2] = (hi >> 24) & 0xff;
		addr[3] = (hi >> 16) & 0xff;
		addr[4] = (hi >>  8) & 0xff;
		addr[5] = (hi & 0xff);
	} else {
		addr[0] = (hi & 0xff);
		addr[1] = (hi >>  8) & 0xff;
		addr[2] = (hi >> 16) & 0xff;
		addr[3] = (hi >> 24) & 0xff;

		addr[4] = (lo & 0xff);
		addr[5] = (lo >>  8) & 0xff;
	}
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

static int
nfe_newbuf_std(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
	       int wait)
{
	struct nfe_rx_data *data = &ring->data[idx];
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct mbuf *m;
	int nsegs, error;

	m = m_getcl(wait ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return ENOBUFS;
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * Aligning the payload improves access times.
	 */
	if (sc->sc_caps & NFE_WORDALIGN)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(ring->data_tag, ring->data_tmpmap,
			m, &seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (wait) {
			if_printf(&sc->arpcom.ac_if,
				  "could map RX mbuf %d\n", error);
		}
		return error;
	}

	if (data->m != NULL) {
		/* Sync and unload originally mapped mbuf */
		bus_dmamap_sync(ring->data_tag, data->map,
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(ring->data_tag, data->map);
	}

	/* Swap this DMA map with tmp DMA map */
	map = data->map;
	data->map = ring->data_tmpmap;
	ring->data_tmpmap = map;

	/* Caller is assumed to have collected the old mbuf */
	data->m = m;

	nfe_set_paddr_rxdesc(sc, ring, idx, seg.ds_addr);
	return 0;
}

static int
nfe_newbuf_jumbo(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
		 int wait)
{
	struct nfe_rx_data *data = &ring->data[idx];
	struct nfe_jbuf *jbuf;
	struct mbuf *m;

	MGETHDR(m, wait ? M_WAITOK : M_NOWAIT, MT_DATA);
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

	/*
	 * Aligning the payload improves access times.
	 */
	if (sc->sc_caps & NFE_WORDALIGN)
		m_adj(m, ETHER_ALIGN);

	/* Caller is assumed to have collected the old mbuf */
	data->m = m;

	nfe_set_paddr_rxdesc(sc, ring, idx, jbuf->physaddr);
	return 0;
}

static void
nfe_set_paddr_rxdesc(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx,
		     bus_addr_t physaddr)
{
	if (sc->sc_caps & NFE_40BIT_ADDR) {
		struct nfe_desc64 *desc64 = &ring->desc64[idx];

		desc64->physaddr[0] = htole32(NFE_ADDR_HI(physaddr));
		desc64->physaddr[1] = htole32(NFE_ADDR_LO(physaddr));
	} else {
		struct nfe_desc32 *desc32 = &ring->desc32[idx];

		desc32->physaddr = htole32(physaddr);
	}
}

static void
nfe_set_ready_rxdesc(struct nfe_softc *sc, struct nfe_rx_ring *ring, int idx)
{
	if (sc->sc_caps & NFE_40BIT_ADDR) {
		struct nfe_desc64 *desc64 = &ring->desc64[idx];

		desc64->length = htole16(ring->bufsz);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		struct nfe_desc32 *desc32 = &ring->desc32[idx];

		desc32->length = htole16(ring->bufsz);
		desc32->flags = htole16(NFE_RX_READY);
	}
}

static int
nfe_sysctl_imtime(SYSCTL_HANDLER_ARGS)
{
	struct nfe_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t flags;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	flags = sc->sc_flags & ~NFE_F_DYN_IM;
	v = sc->sc_imtime;
	if (sc->sc_flags & NFE_F_DYN_IM)
		v = -v;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v < 0) {
		flags |= NFE_F_DYN_IM;
		v = -v;
	}

	if (v != sc->sc_imtime || (flags ^ sc->sc_flags)) {
		if (NFE_IMTIME(v) == 0)
			v = 0;
		sc->sc_imtime = v;
		sc->sc_flags = flags;
		sc->sc_irq_enable = NFE_IRQ_ENABLE(sc);

		if ((ifp->if_flags & (IFF_NPOLLING | IFF_RUNNING))
		    == IFF_RUNNING) {
			nfe_enable_intrs(sc);
		}
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
nfe_powerup(device_t dev)
{
	struct nfe_softc *sc = device_get_softc(dev);
	uint32_t pwr_state;
	uint16_t did;

	/*
	 * Bring MAC and PHY out of low power state
	 */

	pwr_state = NFE_READ(sc, NFE_PWR_STATE2) & ~NFE_PWRUP_MASK;

	did = pci_get_device(dev);
	if ((did == PCI_PRODUCT_NVIDIA_MCP51_LAN1 ||
	     did == PCI_PRODUCT_NVIDIA_MCP51_LAN2) &&
	    pci_get_revid(dev) >= 0xa3)
		pwr_state |= NFE_PWRUP_REV_A3;

	NFE_WRITE(sc, NFE_PWR_STATE2, pwr_state);
}

static void
nfe_mac_reset(struct nfe_softc *sc)
{
	uint32_t rxtxctl = sc->rxtxctl_desc | NFE_RXTX_BIT2;
	uint32_t macaddr_hi, macaddr_lo, tx_poll;

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | rxtxctl);

	/* Save several registers for later restoration */
	macaddr_hi = NFE_READ(sc, NFE_MACADDR_HI);
	macaddr_lo = NFE_READ(sc, NFE_MACADDR_LO);
	tx_poll = NFE_READ(sc, NFE_TX_POLL);

	NFE_WRITE(sc, NFE_MAC_RESET, NFE_RESET_ASSERT);
	DELAY(100);

	NFE_WRITE(sc, NFE_MAC_RESET, 0);
	DELAY(100);

	/* Restore saved registers */
	NFE_WRITE(sc, NFE_MACADDR_HI, macaddr_hi);
	NFE_WRITE(sc, NFE_MACADDR_LO, macaddr_lo);
	NFE_WRITE(sc, NFE_TX_POLL, tx_poll);

	NFE_WRITE(sc, NFE_RXTX_CTL, rxtxctl);
}

static void
nfe_enable_intrs(struct nfe_softc *sc)
{
	/*
	 * NFE_IMTIMER generates a periodic interrupt via NFE_IRQ_TIMER.
	 * It is unclear how wide the timer is.  Base programming does
	 * not seem to effect NFE_IRQ_TX_DONE or NFE_IRQ_RX_DONE so
	 * we don't get any interrupt moderation.  TX moderation is
	 * possible by using the timer interrupt instead of TX_DONE.
	 *
	 * It is unclear whether there are other bits that can be
	 * set to make the NFE device actually do interrupt moderation
	 * on the RX side.
	 *
	 * For now set a 128uS interval as a placemark, but don't use
	 * the timer.
	 */
	if (sc->sc_imtime == 0)
		NFE_WRITE(sc, NFE_IMTIMER, NFE_IMTIME_DEFAULT);
	else
		NFE_WRITE(sc, NFE_IMTIMER, NFE_IMTIME(sc->sc_imtime));

	/* Enable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, sc->sc_irq_enable);

	if (sc->sc_irq_enable & NFE_IRQ_TIMER)
		sc->sc_flags |= NFE_F_IRQ_TIMER;
	else
		sc->sc_flags &= ~NFE_F_IRQ_TIMER;
}
