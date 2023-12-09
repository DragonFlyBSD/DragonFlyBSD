/*
 * Copyright (c) 2004
 *	Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 1997, 1998-2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/re/if_re.c,v 1.25 2004/06/09 14:34:01 naddy Exp $
 */

/*
 * RealTek 8169S/8110S/8168/8111/8101E/8125 PCI NIC driver
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Networking Software Engineer
 * Wind River Systems
 */

/*
 * This driver is designed to support RealTek's next generation of
 * 10/100 and 10/100/1000 PCI ethernet controllers. There are currently
 * seven devices in this family: the the RTL8169, the RTL8169S, RTL8110S,
 * the RTL8168, the RTL8111 and the RTL8101E.
 *
 * The 8169 is a 64-bit 10/100/1000 gigabit ethernet MAC:
 *
 *	o Descriptor based DMA mechanism.  Each descriptor represents
 *	  a single packet fragment. Data buffers may be aligned on
 *	  any byte boundary.
 *
 *	o 64-bit DMA.
 *
 *	o TCP/IP checksum offload for both RX and TX.
 *
 *	o High and normal priority transmit DMA rings.
 *
 *	o VLAN tag insertion and extraction.
 *
 *	o TCP large send (segmentation offload).
 *
 *	o 1000Mbps mode.
 *
 *	o Jumbo frames.
 *
 * 	o GMII and TBI ports/registers for interfacing with copper
 *	  or fiber PHYs.
 *
 *      o RX and TX DMA rings can have up to 1024 descriptors.
 *
 * The 8169 does not have a built-in PHY.  Most reference boards use a
 * Marvell 88E1000 'Alaska' copper gigE PHY.  8169/8110 is _no longer_
 * supported.
 *
 * The 8169S/8110S 10/100/1000 devices have built-in copper gigE PHYs
 * (the 'S' stands for 'single-chip').  These devices have the same
 * programming API as the older 8169, but also have some vendor-specific
 * registers for the on-board PHY.  The 8110S is a LAN-on-motherboard
 * part designed to be pin-compatible with the RealTek 8100 10/100 chip.
 * 8125 supports 10/100/1000/2500.
 * 
 * This driver takes advantage of the RX and TX checksum offload and
 * VLAN tag insertion/extraction features.  It also implements
 * interrupt moderation using the timer interrupt registers, which
 * significantly reduces interrupt load.
 */

#define _IP_VHL

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/in_cksum.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <netinet/ip.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/re/if_rereg.h>
#include <dev/netif/re/if_revar.h>
#include <dev/netif/re/re.h>
#include <dev/netif/re/re_dragonfly.h>

/*
 * Various supported device vendors/types and their names.
 */
static const struct re_type {
	uint16_t	re_vid;
	uint16_t	re_did;
	const char	*re_name;
} re_devs[] = {
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE528T,
	  "D-Link DGE-528(T) Gigabit Ethernet Adapter" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8101E,
	  "RealTek 810x PCIe 10/100baseTX" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168,
	  "RealTek 8111/8168 PCIe Gigabit Ethernet" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168_1,
	  "RealTek 8168 PCIe Gigabit Ethernet" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8125,
	  "RealTek 8125 PCIe Gigabit Ethernet" },

#ifdef notyet
	/*
	 * This driver now only supports built-in PHYs.
	 */
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169,
	  "RealTek 8110/8169 Gigabit Ethernet" },
#endif

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169SC,
	  "RealTek 8169SC/8110SC Single-chip Gigabit Ethernet" },

	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CG_LAPCIGT,
	  "Corega CG-LAPCIGT Gigabit Ethernet" },

	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_EG1032,
	  "Linksys EG1032 Gigabit Ethernet" },

	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_997902,
	  "US Robotics 997902 Gigabit Ethernet" },

	{ PCI_VENDOR_TTTECH, PCI_PRODUCT_TTTECH_MC322,
	  "TTTech MC322 Gigabit Ethernet" },

	{ 0, 0, NULL }
};

static int	re_probe(device_t);
static int	re_attach(device_t);
static int	re_detach(device_t);
static int	re_suspend(device_t);
static int	re_resume(device_t);
static void	re_shutdown(device_t);

static int	re_allocmem(device_t);
static void	re_freemem(device_t);
static void	re_freebufmem(struct re_softc *, int, int);
static int	re_encap(struct re_softc *, struct mbuf **, int *);
static int	re_newbuf_std(struct re_softc *, int, int);
#ifdef RE_JUMBO
static int	re_newbuf_jumbo(struct re_softc *, int, int);
#endif
static void	re_setup_rxdesc(struct re_softc *, int);
static int	re_rx_list_init(struct re_softc *);
static int	re_tx_list_init(struct re_softc *);
static int	re_rxeof(struct re_softc *);
static int	re_txeof(struct re_softc *);
static int	re_tx_collect(struct re_softc *);
static void	re_intr(void *);
static void	re_tick(void *);
static void	re_tick_serialized(void *);
static void	re_disable_aspm(device_t);
static void	re_link_up(struct re_softc *);
static void	re_link_down(struct re_softc *);

static void	re_start_xmit(struct re_softc *);
static void	re_write_imr(struct re_softc *, uint32_t);
static void	re_write_isr(struct re_softc *, uint32_t);
static uint32_t	re_read_isr(struct re_softc *);
static void	re_start_xmit_8125(struct re_softc *);
static void	re_write_imr_8125(struct re_softc *, uint32_t);
static void	re_write_isr_8125(struct re_softc *, uint32_t);
static uint32_t	re_read_isr_8125(struct re_softc *);

static void	re_start(struct ifnet *, struct ifaltq_subque *);
static int	re_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	re_init(void *);
static void	re_stop(struct re_softc *, boolean_t);
static void	re_watchdog(struct ifnet *);

static void	re_setup_hw_im(struct re_softc *);
static void	re_setup_sim_im(struct re_softc *);
static void	re_disable_hw_im(struct re_softc *);
static void	re_disable_sim_im(struct re_softc *);
static void	re_config_imtype(struct re_softc *, int);
static void	re_setup_intr(struct re_softc *, int, int);

static int	re_sysctl_hwtime(SYSCTL_HANDLER_ARGS, int *);
static int	re_sysctl_rxtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_txtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_simtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_imtype(SYSCTL_HANDLER_ARGS);

static int	re_jpool_alloc(struct re_softc *);
static void	re_jpool_free(struct re_softc *);
#ifdef RE_JUMBO
static struct re_jbuf *re_jbuf_alloc(struct re_softc *);
static void	re_jbuf_free(void *);
static void	re_jbuf_ref(void *);
#endif

#ifdef IFPOLL_ENABLE
static void	re_npoll(struct ifnet *, struct ifpoll_info *);
static void	re_npoll_compat(struct ifnet *, void *, int);
#endif

static device_method_t re_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		re_probe),
	DEVMETHOD(device_attach,	re_attach),
	DEVMETHOD(device_detach,	re_detach),
	DEVMETHOD(device_suspend,	re_suspend),
	DEVMETHOD(device_resume,	re_resume),
	DEVMETHOD(device_shutdown,	re_shutdown),
	DEVMETHOD_END
};

static driver_t re_driver = {
	"re",
	re_methods,
	sizeof(struct re_softc)
};

static devclass_t re_devclass;

DECLARE_DUMMY_MODULE(if_re);
DRIVER_MODULE(if_re, pci, re_driver, re_devclass, NULL, NULL);
DRIVER_MODULE(if_re, cardbus, re_driver, re_devclass, NULL, NULL);

static int	re_rx_desc_count = RE_RX_DESC_CNT_DEF;
static int	re_tx_desc_count = RE_TX_DESC_CNT_DEF;
static int	re_msi_enable = 1;

TUNABLE_INT("hw.re.rx_desc_count", &re_rx_desc_count);
TUNABLE_INT("hw.re.tx_desc_count", &re_tx_desc_count);
TUNABLE_INT("hw.re.msi.enable", &re_msi_enable);

static __inline void
re_free_rxchain(struct re_softc *sc)
{
	if (sc->re_head != NULL) {
		m_freem(sc->re_head);
		sc->re_head = sc->re_tail = NULL;
	}
}

static int
re_probe(device_t dev)
{
	const struct re_type *t;
	uint16_t vendor, product;

	vendor = pci_get_vendor(dev);
	product = pci_get_device(dev);

	/*
	 * Only attach to rev.3 of the Linksys EG1032 adapter.
	 * Rev.2 is supported by sk(4).
	 */
	if (vendor == PCI_VENDOR_LINKSYS &&
	    product == PCI_PRODUCT_LINKSYS_EG1032 &&
	    pci_get_subdevice(dev) != PCI_SUBDEVICE_LINKSYS_EG1032_REV3)
		return ENXIO;

	for (t = re_devs; t->re_name != NULL; t++) {
		if (product == t->re_did && vendor == t->re_vid)
			break;
	}
	if (t->re_name == NULL)
		return ENXIO;

	device_set_desc(dev, t->re_name);
	return 0;
}

static int
re_allocmem(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	bus_dmamem_t dmem;
	int error, i;

	/*
	 * Allocate list data
	 */
	sc->re_ldata.re_tx_mbuf =
	kmalloc(sc->re_tx_desc_cnt * sizeof(struct mbuf *),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_mbuf =
	kmalloc(sc->re_rx_desc_cnt * sizeof(struct mbuf *),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_paddr =
	kmalloc(sc->re_rx_desc_cnt * sizeof(bus_addr_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_tx_dmamap =
	kmalloc(sc->re_tx_desc_cnt * sizeof(bus_dmamap_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_dmamap =
	kmalloc(sc->re_rx_desc_cnt * sizeof(bus_dmamap_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
			0,			/* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			0,			/* flags */
			&sc->re_parent_tag);
	if (error) {
		device_printf(dev, "could not allocate parent dma tag\n");
		return error;
	}

	/* Allocate TX descriptor list. */
	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RING_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RE_TX_LIST_SZ(sc), BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&dmem);
	if (error) {
		device_printf(dev, "could not allocate TX ring\n");
		return error;
	}
	sc->re_ldata.re_tx_list_tag = dmem.dmem_tag;
	sc->re_ldata.re_tx_list_map = dmem.dmem_map;
	sc->re_ldata.re_tx_list = dmem.dmem_addr;
	sc->re_ldata.re_tx_list_addr = dmem.dmem_busaddr;

	/* Allocate RX descriptor list. */
	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RING_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RE_RX_LIST_SZ(sc), BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&dmem);
	if (error) {
		device_printf(dev, "could not allocate RX ring\n");
		return error;
	}
	sc->re_ldata.re_rx_list_tag = dmem.dmem_tag;
	sc->re_ldata.re_rx_list_map = dmem.dmem_map;
	sc->re_ldata.re_rx_list = dmem.dmem_addr;
	sc->re_ldata.re_rx_list_addr = dmem.dmem_busaddr;

	/* Allocate maps for TX mbufs. */
	error = bus_dma_tag_create(sc->re_parent_tag,
			1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RE_FRAMELEN_MAX, RE_MAXSEGS, MCLBYTES,
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
			&sc->re_ldata.re_tx_mtag);
	if (error) {
		device_printf(dev, "could not allocate TX buf dma tag\n");
		return(error);
	}

	/* Create DMA maps for TX buffers */
	for (i = 0; i < sc->re_tx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->re_ldata.re_tx_mtag,
				BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
				&sc->re_ldata.re_tx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for TX buf\n");
			re_freebufmem(sc, i, 0);
			return(error);
		}
	}

	/* Allocate maps for RX mbufs. */
	error = bus_dma_tag_create(sc->re_parent_tag,
			RE_RXBUF_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			MCLBYTES, 1, MCLBYTES,
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ALIGNED,
			&sc->re_ldata.re_rx_mtag);
	if (error) {
		device_printf(dev, "could not allocate RX buf dma tag\n");
		return(error);
	}

	/* Create spare DMA map for RX */
	error = bus_dmamap_create(sc->re_ldata.re_rx_mtag, BUS_DMA_WAITOK,
			&sc->re_ldata.re_rx_spare);
	if (error) {
		device_printf(dev, "can't create spare DMA map for RX\n");
		bus_dma_tag_destroy(sc->re_ldata.re_rx_mtag);
		sc->re_ldata.re_rx_mtag = NULL;
		return error;
	}

	/* Create DMA maps for RX buffers */
	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->re_ldata.re_rx_mtag,
				BUS_DMA_WAITOK, &sc->re_ldata.re_rx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for RX buf\n");
			re_freebufmem(sc, sc->re_tx_desc_cnt, i);
			return(error);
		}
	}

	/* Create jumbo buffer pool for RX if required */
	if (sc->re_caps & RE_C_CONTIGRX) {
		error = re_jpool_alloc(sc);
		if (error) {
			re_jpool_free(sc);
#ifdef RE_JUMBO
			/* Disable jumbo frame support */
			sc->re_maxmtu = ETHERMTU;
#endif
		}
	}
	return(0);
}

static void
re_freebufmem(struct re_softc *sc, int tx_cnt, int rx_cnt)
{
	int i;

	/* Destroy all the RX and TX buffer maps */
	if (sc->re_ldata.re_tx_mtag) {
		for (i = 0; i < tx_cnt; i++) {
			bus_dmamap_destroy(sc->re_ldata.re_tx_mtag,
					   sc->re_ldata.re_tx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->re_ldata.re_tx_mtag);
		sc->re_ldata.re_tx_mtag = NULL;
	}

	if (sc->re_ldata.re_rx_mtag) {
		for (i = 0; i < rx_cnt; i++) {
			bus_dmamap_destroy(sc->re_ldata.re_rx_mtag,
					   sc->re_ldata.re_rx_dmamap[i]);
		}
		bus_dmamap_destroy(sc->re_ldata.re_rx_mtag,
				   sc->re_ldata.re_rx_spare);
		bus_dma_tag_destroy(sc->re_ldata.re_rx_mtag);
		sc->re_ldata.re_rx_mtag = NULL;
	}
}

static void
re_freemem(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);

	/* Unload and free the RX DMA ring memory and map */
	if (sc->re_ldata.re_rx_list_tag) {
		bus_dmamap_unload(sc->re_ldata.re_rx_list_tag,
				  sc->re_ldata.re_rx_list_map);
		bus_dmamem_free(sc->re_ldata.re_rx_list_tag,
				sc->re_ldata.re_rx_list,
				sc->re_ldata.re_rx_list_map);
		bus_dma_tag_destroy(sc->re_ldata.re_rx_list_tag);
	}

	/* Unload and free the TX DMA ring memory and map */
	if (sc->re_ldata.re_tx_list_tag) {
		bus_dmamap_unload(sc->re_ldata.re_tx_list_tag,
				  sc->re_ldata.re_tx_list_map);
		bus_dmamem_free(sc->re_ldata.re_tx_list_tag,
				sc->re_ldata.re_tx_list,
				sc->re_ldata.re_tx_list_map);
		bus_dma_tag_destroy(sc->re_ldata.re_tx_list_tag);
	}

	/* Free RX/TX buf DMA stuffs */
	re_freebufmem(sc, sc->re_tx_desc_cnt, sc->re_rx_desc_cnt);

	/* Unload and free the stats buffer and map */
	if (sc->re_ldata.re_stag) {
		bus_dmamap_unload(sc->re_ldata.re_stag, sc->re_ldata.re_smap);
		bus_dmamem_free(sc->re_ldata.re_stag,
				sc->re_ldata.re_stats,
				sc->re_ldata.re_smap);
		bus_dma_tag_destroy(sc->re_ldata.re_stag);
	}

	if (sc->re_caps & RE_C_CONTIGRX)
		re_jpool_free(sc);

	if (sc->re_parent_tag)
		bus_dma_tag_destroy(sc->re_parent_tag);

	if (sc->re_ldata.re_tx_mbuf != NULL)
		kfree(sc->re_ldata.re_tx_mbuf, M_DEVBUF);
	if (sc->re_ldata.re_rx_mbuf != NULL)
		kfree(sc->re_ldata.re_rx_mbuf, M_DEVBUF);
	if (sc->re_ldata.re_rx_paddr != NULL)
		kfree(sc->re_ldata.re_rx_paddr, M_DEVBUF);
	if (sc->re_ldata.re_tx_dmamap != NULL)
		kfree(sc->re_ldata.re_tx_dmamap, M_DEVBUF);
	if (sc->re_ldata.re_rx_dmamap != NULL)
		kfree(sc->re_ldata.re_rx_dmamap, M_DEVBUF);
}

static boolean_t
re_is_faste(struct re_softc *sc)
{
	if (pci_get_vendor(sc->dev) == PCI_VENDOR_REALTEK) {
		switch (sc->re_device_id) {
		case PCI_PRODUCT_REALTEK_RT8169:
		case PCI_PRODUCT_REALTEK_RT8169SC:
		case PCI_PRODUCT_REALTEK_RT8168:
		case PCI_PRODUCT_REALTEK_RT8168_1:
		case PCI_PRODUCT_REALTEK_RT8125:
			return FALSE;
		default:
			return TRUE;
		}
	} else {
		return FALSE;
	}
}

static bool
re_is_2500e(const struct re_softc *sc)
{
	if (pci_get_vendor(sc->dev) == PCI_VENDOR_REALTEK) {
		switch (sc->re_device_id) {
		case PCI_PRODUCT_REALTEK_RT8125:
			return true;

		default:
			return false;
		}
	}
	return false;
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
re_attach(device_t dev)
{
	struct re_softc	*sc = device_get_softc(dev);
	struct ifnet *ifp;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int error = 0, qlen, msi_enable;
	u_int irq_flags;

	callout_init_mp(&sc->re_timer);
	sc->dev = dev;
	sc->re_device_id = pci_get_device(dev);
	sc->re_unit = device_get_unit(dev);
	ifmedia_init(&sc->media, IFM_IMASK, rtl_ifmedia_upd, rtl_ifmedia_sts);

	if (pci_get_vendor(dev) == PCI_VENDOR_REALTEK &&
	    sc->re_device_id == PCI_PRODUCT_REALTEK_RT8125) {
		sc->re_start_xmit = re_start_xmit_8125;
		sc->re_write_imr = re_write_imr_8125;
		sc->re_write_isr = re_write_isr_8125;
		sc->re_read_isr = re_read_isr_8125;
	} else {
		sc->re_start_xmit = re_start_xmit;
		sc->re_write_imr = re_write_imr;
		sc->re_write_isr = re_write_isr;
		sc->re_read_isr = re_read_isr;
	}

	sc->re_caps = RE_C_HWIM;

	sc->re_rx_desc_cnt = re_rx_desc_count;
	if (sc->re_rx_desc_cnt > RE_RX_DESC_CNT_MAX)
		sc->re_rx_desc_cnt = RE_RX_DESC_CNT_MAX;

	sc->re_tx_desc_cnt = re_tx_desc_count;
	if (sc->re_tx_desc_cnt > RE_TX_DESC_CNT_MAX)
		sc->re_tx_desc_cnt = RE_TX_DESC_CNT_MAX;

	qlen = RE_IFQ_MAXLEN;
	if (sc->re_tx_desc_cnt > qlen)
		qlen = sc->re_tx_desc_cnt;

	sc->re_rxbuf_size = MCLBYTES;
	sc->re_newbuf = re_newbuf_std;

	/*
	 * Hardware interrupt moderation settings.
	 * XXX does not seem correct, undocumented.
	 */
	sc->re_tx_time = 5;		/* 125us */
	sc->re_rx_time = 2;		/* 50us */

	/* Simulated interrupt moderation setting. */
	sc->re_sim_time = 150;		/* 150us */

	/* Use simulated interrupt moderation by default. */
	sc->re_imtype = RE_IMTYPE_SIM;
	re_config_imtype(sc, sc->re_imtype);

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "rx_desc_count", CTLFLAG_RD, &sc->re_rx_desc_cnt,
		       0, "RX desc count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "tx_desc_count", CTLFLAG_RD, &sc->re_tx_desc_cnt,
		       0, "TX desc count");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "sim_time",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, re_sysctl_simtime, "I",
			"Simulated interrupt moderation time (usec).");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "imtype",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, re_sysctl_imtype, "I",
			"Interrupt moderation type -- "
			"0:disable, 1:simulated, "
			"2:hardware(if supported)");
	if (sc->re_caps & RE_C_HWIM) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
				OID_AUTO, "hw_rxtime",
				CTLTYPE_INT | CTLFLAG_RW,
				sc, 0, re_sysctl_rxtime, "I",
				"Hardware interrupt moderation time "
				"(unit: 25usec).");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
				OID_AUTO, "hw_txtime",
				CTLTYPE_INT | CTLFLAG_RW,
				sc, 0, re_sysctl_txtime, "I",
				"Hardware interrupt moderation time "
				"(unit: 25usec).");
	}

#ifndef BURN_BRIDGES
	/*
	 * Handle power management nonsense.
	 */

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t membase, irq;

		/* Save important PCI config data. */
		membase = pci_read_config(dev, RE_PCI_LOMEM, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, RE_PCI_LOMEM, membase, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif
	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	if (pci_is_pcie(dev)) {
		sc->re_res_rid = PCIR_BAR(2);
		sc->re_res_type = SYS_RES_MEMORY;
	} else {
		sc->re_res_rid = PCIR_BAR(0);
		sc->re_res_type = SYS_RES_IOPORT;
	}
	sc->re_res = bus_alloc_resource_any(dev, sc->re_res_type,
	    &sc->re_res_rid, RF_ACTIVE);
	if (sc->re_res == NULL) {
		device_printf(dev, "couldn't map IO\n");
		error = ENXIO;
		goto fail;
	}

	sc->re_btag = rman_get_bustag(sc->re_res);
	sc->re_bhandle = rman_get_bushandle(sc->re_res);

	error = rtl_check_mac_version(sc);
	if (error) {
		device_printf(dev, "check mac version failed\n");
		goto fail;
	}

	rtl_init_software_variable(sc);
	if (pci_is_pcie(dev))
		sc->re_if_flags |= RL_FLAG_PCIE;
	else
		sc->re_if_flags &= ~RL_FLAG_PCIE;
	device_printf(dev, "MAC version 0x%08x, MACFG %u%s%s%s\n",
	    (CSR_READ_4(sc, RE_TXCFG) & 0xFCF00000), sc->re_type,
	    sc->re_coalesce_tx_pkt ? ", software TX defrag" : "",
	    sc->re_pad_runt ? ", pad runt" : "",
	    sc->re_hw_enable_msi_msix ? ", support MSI" : "");

	/*
	 * Allocate interrupt
	 */
	if (pci_is_pcie(dev) && sc->re_hw_enable_msi_msix)
		msi_enable = re_msi_enable;
	else
		msi_enable = 0;
	sc->re_irq_type = pci_alloc_1intr(dev, msi_enable,
	    &sc->re_irq_rid, &irq_flags);

	sc->re_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->re_irq_rid,
					    irq_flags);
	if (sc->re_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Disable ASPM */
	re_disable_aspm(dev);

	rtl_exit_oob(sc);
	rtl_hw_init(sc);

	/* Reset the adapter. */
	rtl_reset(sc);

	rtl_get_hw_mac_address(sc, eaddr);
	if (sc->re_type == MACFG_3)	/* Change PCI Latency time*/
		pci_write_config(dev, PCIR_LATTIMER, 0x40, 1);

	/* Allocate DMA stuffs */
	error = re_allocmem(dev);
	if (error)
		goto fail;

	if (pci_is_pcie(dev)) {
		sc->re_bus_speed = 125;
	} else {
		uint8_t cfg2;

		cfg2 = CSR_READ_1(sc, RE_CFG2);
		switch (cfg2 & RE_CFG2_PCICLK_MASK) {
		case RE_CFG2_PCICLK_33MHZ:
			sc->re_bus_speed = 33;
			break;
		case RE_CFG2_PCICLK_66MHZ:
			sc->re_bus_speed = 66;
			break;
		default:
			device_printf(dev, "unknown bus speed, assume 33MHz\n");
			sc->re_bus_speed = 33;
			break;
		}
	}
	device_printf(dev, "bus speed %dMHz\n", sc->re_bus_speed);

	/* Enable hardware checksum if available. */
	sc->re_tx_cstag = 1;
	sc->re_rx_cstag = 1;

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = re_ioctl;
	ifp->if_start = re_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = re_npoll;
#endif
	ifp->if_watchdog = re_watchdog;
	ifp->if_init = re_init;
	if (re_is_faste(sc))
		ifp->if_baudrate = IF_Mbps(100ULL);
	else if (re_is_2500e(sc))
		ifp->if_baudrate = IF_Mbps(2500ULL);
	else
		ifp->if_baudrate = IF_Mbps(1000ULL);
	ifp->if_nmbclusters = sc->re_rx_desc_cnt;
	ifq_set_maxlen(&ifp->if_snd, qlen);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING |
	    IFCAP_RXCSUM | IFCAP_TXCSUM;
	ifp->if_capenable = ifp->if_capabilities;
	/* NOTE: if_hwassist will be setup after the interface is up. */

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->re_irq));

	rtl_phy_power_up(sc);
	rtl_hw_phy_config(sc);
	rtl_clrwol(sc);

	/* TODO: jumbo frame */
	CSR_WRITE_2(sc, RE_RxMaxSize, sc->re_rxbuf_size);

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->re_npoll, ctx, (struct sysctl_oid *)tree,
	    device_get_unit(dev), ifp->if_serializer);
#endif

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->re_irq, INTR_MPSAFE | INTR_HIFREQ,
	    re_intr, sc, &sc->re_intrhand, ifp->if_serializer);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);
	if (!re_is_faste(sc)) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX,
		    0, NULL);
	}
	if (re_is_2500e(sc)) {
#ifndef IFM_2500_T
		ifmedia_add(&sc->media, IFM_ETHER | IFM_2500_SX | IFM_FDX,
		    0, NULL);
#else
		ifmedia_add(&sc->media, IFM_ETHER | IFM_2500_T | IFM_FDX,
		    0, NULL);
#endif
	}
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
	rtl_ifmedia_upd(ifp);

fail:
	if (error)
		re_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
re_detach(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		re_stop(sc, TRUE);
		bus_teardown_intr(dev, sc->re_irq, sc->re_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}
	ifmedia_removeall(&sc->media);

	if (sc->re_irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->re_irq_rid,
				     sc->re_irq);

	if (sc->re_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->re_res) {
		bus_release_resource(dev, sc->re_res_type, sc->re_res_rid,
		    sc->re_res);
	}
	rtl_cmac_unmap(sc);

	/* Free DMA stuffs */
	re_freemem(dev);

	return(0);
}

static void
re_setup_rxdesc(struct re_softc *sc, int idx)
{
	bus_addr_t paddr;
	uint32_t cmdstat;
	struct re_desc *d;

	paddr = sc->re_ldata.re_rx_paddr[idx];
	d = &sc->re_ldata.re_rx_list[idx];

	d->re_bufaddr_lo = htole32(RE_ADDR_LO(paddr));
	d->re_bufaddr_hi = htole32(RE_ADDR_HI(paddr));

	cmdstat = sc->re_rxbuf_size | RE_RDESC_CMD_OWN;
	if (idx == (sc->re_rx_desc_cnt - 1))
		cmdstat |= RE_RDESC_CMD_EOR;
	d->re_cmdstat = htole32(cmdstat);
}

static int
re_newbuf_std(struct re_softc *sc, int idx, int init)
{
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct mbuf *m;
	int error, nsegs;

	m = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		error = ENOBUFS;

		if (init) {
			if_printf(&sc->arpcom.ac_if, "m_getcl failed\n");
			return error;
		} else {
			goto back;
		}
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * NOTE:
	 * re(4) chips need address of the receive buffer to be 8-byte
	 * aligned, so don't call m_adj(m, ETHER_ALIGN) here.
	 */

	error = bus_dmamap_load_mbuf_segment(sc->re_ldata.re_rx_mtag,
			sc->re_ldata.re_rx_spare, m,
			&seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init) {
			if_printf(&sc->arpcom.ac_if, "can't load RX mbuf\n");
			return error;
		} else {
			goto back;
		}
	}

	if (!init) {
		bus_dmamap_sync(sc->re_ldata.re_rx_mtag,
				sc->re_ldata.re_rx_dmamap[idx],
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->re_ldata.re_rx_mtag,
				  sc->re_ldata.re_rx_dmamap[idx]);
	}
	sc->re_ldata.re_rx_mbuf[idx] = m;
	sc->re_ldata.re_rx_paddr[idx] = seg.ds_addr;

	map = sc->re_ldata.re_rx_dmamap[idx];
	sc->re_ldata.re_rx_dmamap[idx] = sc->re_ldata.re_rx_spare;
	sc->re_ldata.re_rx_spare = map;
back:
	re_setup_rxdesc(sc, idx);
	return error;
}

#ifdef RE_JUMBO
static int
re_newbuf_jumbo(struct re_softc *sc, int idx, int init)
{
	struct mbuf *m;
	struct re_jbuf *jbuf;
	int error = 0;

	MGETHDR(m, init ? M_WAITOK : M_NOWAIT, MT_DATA);
	if (m == NULL) {
		error = ENOBUFS;
		if (init) {
			if_printf(&sc->arpcom.ac_if, "MGETHDR failed\n");
			return error;
		} else {
			goto back;
		}
	}

	jbuf = re_jbuf_alloc(sc);
	if (jbuf == NULL) {
		m_freem(m);

		error = ENOBUFS;
		if (init) {
			if_printf(&sc->arpcom.ac_if, "jpool is empty\n");
			return error;
		} else {
			goto back;
		}
	}

	m->m_ext.ext_arg = jbuf;
	m->m_ext.ext_buf = jbuf->re_buf;
	m->m_ext.ext_free = re_jbuf_free;
	m->m_ext.ext_ref = re_jbuf_ref;
	m->m_ext.ext_size = sc->re_rxbuf_size;

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT;
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	/*
	 * NOTE:
	 * Some re(4) chips(e.g. RTL8101E) need address of the receive buffer
	 * to be 8-byte aligned, so don't call m_adj(m, ETHER_ALIGN) here.
	 */

	sc->re_ldata.re_rx_mbuf[idx] = m;
	sc->re_ldata.re_rx_paddr[idx] = jbuf->re_paddr;
back:
	re_setup_rxdesc(sc, idx);
	return error;
}
#endif	/* RE_JUMBO */

static int
re_tx_list_init(struct re_softc *sc)
{
	bzero(sc->re_ldata.re_tx_list, RE_TX_LIST_SZ(sc));

	sc->re_ldata.re_tx_prodidx = 0;
	sc->re_ldata.re_tx_considx = 0;
	sc->re_ldata.re_tx_free = sc->re_tx_desc_cnt;

	return(0);
}

static int
re_rx_list_init(struct re_softc *sc)
{
	int i, error;

	bzero(sc->re_ldata.re_rx_list, RE_RX_LIST_SZ(sc));

	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		error = sc->re_newbuf(sc, i, 1);
		if (error)
			return(error);
	}

	sc->re_ldata.re_rx_prodidx = 0;
	sc->re_head = sc->re_tail = NULL;

	return(0);
}

#define RE_IP4_PACKET	0x1
#define RE_TCP_PACKET	0x2
#define RE_UDP_PACKET	0x4

static __inline uint8_t
re_packet_type(struct re_softc *sc, uint32_t rxstat, uint32_t rxctrl)
{
	uint8_t packet_type = 0;

	if (sc->re_if_flags & RL_FLAG_DESCV2) {
		if (rxctrl & RE_RDESC_CTL_PROTOIP4)
			packet_type |= RE_IP4_PACKET;
	} else {
		if (rxstat & RE_RDESC_STAT_PROTOID)
			packet_type |= RE_IP4_PACKET;
	}
	if (RE_TCPPKT(rxstat))
		packet_type |= RE_TCP_PACKET;
	else if (RE_UDPPKT(rxstat))
		packet_type |= RE_UDP_PACKET;
	return packet_type;
}

/*
 * RX handler for C+ and 8169. For the gigE chips, we support
 * the reception of jumbo frames that have been fragmented
 * across multiple 2K mbuf cluster buffers.
 */
static int
re_rxeof(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;
	struct re_desc 	*cur_rx;
	uint32_t rxstat, rxctrl;
	int i, total_len, rx = 0;

	for (i = sc->re_ldata.re_rx_prodidx;
	     RE_OWN(&sc->re_ldata.re_rx_list[i]) == 0; RE_RXDESC_INC(sc, i)) {
		cur_rx = &sc->re_ldata.re_rx_list[i];
		m = sc->re_ldata.re_rx_mbuf[i];
		total_len = RE_RXBYTES(cur_rx);
		rxstat = le32toh(cur_rx->re_cmdstat);
		rxctrl = le32toh(cur_rx->re_control);

		rx = 1;

#ifdef INVARIANTS
		if (sc->re_flags & RE_F_USE_JPOOL)
			KKASSERT(rxstat & RE_RDESC_STAT_EOF);
#endif

		if ((rxstat & RE_RDESC_STAT_EOF) == 0) {
			if (sc->re_flags & RE_F_DROP_RXFRAG) {
				re_setup_rxdesc(sc, i);
				continue;
			}

			if (sc->re_newbuf(sc, i, 0)) {
				/* Drop upcoming fragments */
				sc->re_flags |= RE_F_DROP_RXFRAG;
				continue;
			}

			m->m_len = MCLBYTES;
			if (sc->re_head == NULL) {
				sc->re_head = sc->re_tail = m;
			} else {
				sc->re_tail->m_next = m;
				sc->re_tail = m;
			}
			continue;
		} else if (sc->re_flags & RE_F_DROP_RXFRAG) {
			/*
			 * Last fragment of a multi-fragment packet.
			 *
			 * Since error already happened, this fragment
			 * must be dropped as well as the fragment chain.
			 */
			re_setup_rxdesc(sc, i);
			re_free_rxchain(sc);
			sc->re_flags &= ~RE_F_DROP_RXFRAG;
			continue;
		}

		rxstat >>= 1;
		if (rxstat & RE_RDESC_STAT_RXERRSUM) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			/*
			 * If this is part of a multi-fragment packet,
			 * discard all the pieces.
			 */
			re_free_rxchain(sc);
			re_setup_rxdesc(sc, i);
			continue;
		}

		/*
		 * If allocating a replacement mbuf fails,
		 * reload the current one.
		 */

		if (sc->re_newbuf(sc, i, 0)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}

		if (sc->re_head != NULL) {
			m->m_len = total_len % MCLBYTES;
			/* 
			 * Special case: if there's 4 bytes or less
			 * in this buffer, the mbuf can be discarded:
			 * the last 4 bytes is the CRC, which we don't
			 * care about anyway.
			 */
			if (m->m_len <= ETHER_CRC_LEN) {
				sc->re_tail->m_len -=
				    (ETHER_CRC_LEN - m->m_len);
				m_freem(m);
			} else {
				m->m_len -= ETHER_CRC_LEN;
				sc->re_tail->m_next = m;
			}
			m = sc->re_head;
			sc->re_head = sc->re_tail = NULL;
			m->m_pkthdr.len = total_len - ETHER_CRC_LEN;
		} else {
			m->m_pkthdr.len = m->m_len =
			    (total_len - ETHER_CRC_LEN);
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		m->m_pkthdr.rcvif = ifp;

		/* Do RX checksumming if enabled */

		if (ifp->if_capenable & IFCAP_RXCSUM) {
			uint8_t packet_type;

			packet_type = re_packet_type(sc, rxstat, rxctrl);

			/* Check IP header checksum */
			if (packet_type & RE_IP4_PACKET) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((rxstat & RE_RDESC_STAT_IPSUMBAD) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}

			/* Check TCP/UDP checksum */
			if (((packet_type & RE_TCP_PACKET) &&
			     (rxstat & RE_RDESC_STAT_TCPSUMBAD) == 0) ||
			    ((packet_type & RE_UDP_PACKET) &&
			     (rxstat & RE_RDESC_STAT_UDPSUMBAD) == 0)) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID|CSUM_PSEUDO_HDR|
				    CSUM_FRAG_NOT_CHECKED;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		if (rxctrl & RE_RDESC_CTL_HASTAG) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag =
				be16toh((rxctrl & RE_RDESC_CTL_TAGDATA));
		}
		ifp->if_input(ifp, m, NULL, -1);
	}

	sc->re_ldata.re_rx_prodidx = i;

	return rx;
}

#undef RE_IP4_PACKET
#undef RE_TCP_PACKET
#undef RE_UDP_PACKET

static int
re_tx_collect(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t txstat;
	int idx, tx = 0;

	for (idx = sc->re_ldata.re_tx_considx;
	     sc->re_ldata.re_tx_free < sc->re_tx_desc_cnt;
	     RE_TXDESC_INC(sc, idx)) {
		txstat = le32toh(sc->re_ldata.re_tx_list[idx].re_cmdstat);
		if (txstat & RE_TDESC_CMD_OWN)
			break;

		tx = 1;

		sc->re_ldata.re_tx_list[idx].re_bufaddr_lo = 0;

		/*
		 * We only stash mbufs in the last descriptor
		 * in a fragment chain, which also happens to
		 * be the only place where the TX status bits
		 * are valid.
		 *
		 * NOTE:
		 * On 8125, RE_TDESC_CMD_EOF is no longer left
		 * uncleared.
		 */
		if (sc->re_ldata.re_tx_mbuf[idx] != NULL) {
			bus_dmamap_unload(sc->re_ldata.re_tx_mtag,
			    sc->re_ldata.re_tx_dmamap[idx]);
			m_freem(sc->re_ldata.re_tx_mbuf[idx]);
			sc->re_ldata.re_tx_mbuf[idx] = NULL;
			if (txstat & (RE_TDESC_STAT_EXCESSCOL|
			    RE_TDESC_STAT_COLCNT))
				IFNET_STAT_INC(ifp, collisions, 1);
			if (txstat & RE_TDESC_STAT_TXERRSUM)
				IFNET_STAT_INC(ifp, oerrors, 1);
			else
				IFNET_STAT_INC(ifp, opackets, 1);
		}
		sc->re_ldata.re_tx_free++;
	}
	sc->re_ldata.re_tx_considx = idx;

	return tx;
}

static int
re_txeof(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int tx;

	tx = re_tx_collect(sc);

	/* There is enough free TX descs */
	if (sc->re_ldata.re_tx_free > RE_TXDESC_SPARE)
		ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Some chips will ignore a second TX request issued while an
	 * existing transmission is in progress. If the transmitter goes
	 * idle but there are still packets waiting to be sent, we need
	 * to restart the channel here to flush them out. This only seems
	 * to be required with the PCIe devices.
	 */
	if (sc->re_ldata.re_tx_free < sc->re_tx_desc_cnt)
		sc->re_start_xmit(sc);
	else
		ifp->if_timer = 0;

	return tx;
}

static void
re_tick(void *xsc)
{
	struct re_softc *sc = xsc;

	lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);
	re_tick_serialized(xsc);
	lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
}

static void
re_tick_serialized(void *xsc)
{
	struct re_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (rtl_link_ok(sc)) {
		if ((sc->re_flags & RE_F_LINKED) == 0)
			re_link_up(sc);
	} else if (sc->re_flags & RE_F_LINKED) {
		re_link_down(sc);
	}
	callout_reset(&sc->re_timer, hz, re_tick, sc);
}

#ifdef IFPOLL_ENABLE

static void
re_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->re_npoll.ifpc_stcount-- == 0) {
		uint32_t status;

		sc->re_npoll.ifpc_stcount = sc->re_npoll.ifpc_stfrac;

		status = sc->re_read_isr(sc);
		if (status)
			sc->re_write_isr(sc, status);

		/*
		 * XXX check behaviour on receiver stalls.
		 */

		if (status & RE_ISR_SYSTEM_ERR) {
			rtl_reset(sc);
			re_init(sc);
			/* Done! */
			return;
		}
	}

	sc->rxcycles = count;
	re_rxeof(sc);
	re_txeof(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
re_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->re_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = re_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING)
			re_setup_intr(sc, 0, RE_IMTYPE_NONE);
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			re_setup_intr(sc, 1, sc->re_imtype);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->re_irq));
	}
}
#endif /* IFPOLL_ENABLE */

static void
re_intr(void *arg)
{
	struct re_softc	*sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t status;
	int proc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->re_flags & RE_F_SUSPENDED) ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	/* Disable interrupts. */
	sc->re_write_imr(sc, 0);

	status = sc->re_read_isr(sc);
again:
	proc = 0;
	if (status)
		sc->re_write_isr(sc, status);
	if (status & sc->re_intrs) {
		if (status & RE_ISR_SYSTEM_ERR) {
			rtl_reset(sc);
			re_init(sc);
			/* Done! */
			return;
		}
		proc |= re_rxeof(sc);
		proc |= re_txeof(sc);
	}

	if (sc->re_imtype == RE_IMTYPE_SIM) {
		if ((sc->re_flags & RE_F_TIMER_INTR)) {
			if (!proc) {
				/*
				 * Nothing needs to be processed, fallback
				 * to use TX/RX interrupts.
				 *
				 * NOTE: This will re-enable interrupts.
				 */
				re_setup_intr(sc, 1, RE_IMTYPE_NONE);

				/*
				 * Recollect, mainly to avoid the possible
				 * race introduced by changing interrupt
				 * masks.
				 */
				re_rxeof(sc);
				re_txeof(sc);
			} else {
				/* Re-enable interrupts. */
				sc->re_write_imr(sc, sc->re_intrs);
				CSR_WRITE_4(sc, RE_TIMERCNT, 1); /* reload */
			}
		} else if (proc) {
			/*
			 * Assume that using simulated interrupt moderation
			 * (hardware timer based) could reduce the interript
			 * rate.
			 *
			 * NOTE: This will re-enable interrupts.
			 */
			re_setup_intr(sc, 1, RE_IMTYPE_SIM);
		} else {
			/* Re-enable interrupts. */
			sc->re_write_imr(sc, sc->re_intrs);
		}
	} else {
		status = sc->re_read_isr(sc);
		if (status & sc->re_intrs) {
			if (!ifq_is_empty(&ifp->if_snd))
				if_devstart(ifp);
			/* NOTE: Interrupts are still disabled. */
			goto again;
		}
		/* Re-enable interrupts. */
		sc->re_write_imr(sc, sc->re_intrs);
	}

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static int
re_encap(struct re_softc *sc, struct mbuf **m_head, int *idx0)
{
	struct mbuf *m = *m_head;
	bus_dma_segment_t segs[RE_MAXSEGS];
	bus_dmamap_t map;
	int error, maxsegs, idx, i, nsegs;
	struct re_desc *d, *tx_ring;
	uint32_t cmd_csum, ctl_csum, vlantag;

	KASSERT(sc->re_ldata.re_tx_free > RE_TXDESC_SPARE,
		("not enough free TX desc"));

	if (sc->re_coalesce_tx_pkt && m->m_pkthdr.len != m->m_len) {
		struct mbuf *m_new;

		m_new = m_defrag(m, M_NOWAIT);
		if (m_new == NULL) {
			error = ENOBUFS;
			goto back;
		} else {
			*m_head = m = m_new;
			if (m->m_pkthdr.len != m->m_len) {
				/* Still not configuous; give up. */
				error = ENOBUFS;
				goto back;
			}
		}
	}

	map = sc->re_ldata.re_tx_dmamap[*idx0];

	/*
	 * Set up checksum offload. Note: checksum offload bits must
	 * appear in all descriptors of a multi-descriptor transmit
	 * attempt. (This is according to testing done with an 8169
	 * chip. I'm not sure if this is a requirement or a bug.)
	 */
	cmd_csum = ctl_csum = 0;
	if (m->m_pkthdr.csum_flags & CSUM_IP) {
		cmd_csum |= RE_TDESC_CMD_IPCSUM;
		ctl_csum |= RE_TDESC_CTL_IPCSUM;
	}
	if (m->m_pkthdr.csum_flags & CSUM_TCP) {
		cmd_csum |= RE_TDESC_CMD_TCPCSUM;
		ctl_csum |= RE_TDESC_CTL_TCPCSUM;
	}
	if (m->m_pkthdr.csum_flags & CSUM_UDP) {
		cmd_csum |= RE_TDESC_CMD_UDPCSUM;
		ctl_csum |= RE_TDESC_CTL_UDPCSUM;
	}

	/* For version2 descriptor, csum flags are set on re_control */
	if (sc->re_if_flags & RL_FLAG_DESCV2)
		cmd_csum = 0;
	else
		ctl_csum = 0;

	if (sc->re_pad_runt) {
		/*
		 * With some of the RealTek chips, using the checksum offload
		 * support in conjunction with the autopadding feature results
		 * in the transmission of corrupt frames. For example, if we
		 * need to send a really small IP fragment that's less than 60
		 * bytes in size, and IP header checksumming is enabled, the
		 * resulting ethernet frame that appears on the wire will
		 * have garbled payload. To work around this, if TX checksum
		 * offload is enabled, we always manually pad short frames out
		 * to the minimum ethernet frame size.
		 *
		 * Note: this appears unnecessary for TCP, and doing it for TCP
		 * with PCIe adapters seems to result in bad checksums.
		 */
		if ((m->m_pkthdr.csum_flags &
		     (CSUM_DELAY_IP | CSUM_DELAY_DATA)) &&
		    (m->m_pkthdr.csum_flags & CSUM_TCP) == 0 &&
		    m->m_pkthdr.len < RE_MIN_FRAMELEN) {
			error = m_devpad(m, RE_MIN_FRAMELEN);
			if (error)
				goto back;
		}
	}

	vlantag = 0;
	if (m->m_flags & M_VLANTAG) {
		vlantag = htobe16(m->m_pkthdr.ether_vlantag) |
			  RE_TDESC_CTL_INSTAG;
	}

	maxsegs = sc->re_ldata.re_tx_free;
	if (maxsegs > RE_MAXSEGS)
		maxsegs = RE_MAXSEGS;

	error = bus_dmamap_load_mbuf_defrag(sc->re_ldata.re_tx_mtag, map,
			m_head, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;

	m = *m_head;
	bus_dmamap_sync(sc->re_ldata.re_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	/*
	 * Map the segment array into descriptors.  We also keep track
	 * of the end of the ring and set the end-of-ring bits as needed,
	 * and we set the ownership bits in all except the very first
	 * descriptor, whose ownership bits will be turned on later.
	 */
	tx_ring = sc->re_ldata.re_tx_list;
	idx = *idx0;
	i = 0;
	for (;;) {
		uint32_t cmdstat;

		d = &tx_ring[idx];

		KKASSERT(sc->re_ldata.re_tx_mbuf[idx] == NULL);

		d->re_bufaddr_lo = htole32(RE_ADDR_LO(segs[i].ds_addr));
		d->re_bufaddr_hi = htole32(RE_ADDR_HI(segs[i].ds_addr));

		cmdstat = segs[i].ds_len;
		if (i == 0) {
			cmdstat |= RE_TDESC_CMD_SOF;
		} else if (i != nsegs - 1) {
			/*
			 * Last descriptor's ownership will be transfered
			 * later.
			 */
			cmdstat |= RE_TDESC_CMD_OWN;
		}
		if (idx == (sc->re_tx_desc_cnt - 1))
			cmdstat |= RE_TDESC_CMD_EOR;

		d->re_control = htole32(ctl_csum | vlantag);
		d->re_cmdstat = htole32(cmdstat | cmd_csum);

		i++;
		if (i == nsegs)
			break;
		RE_TXDESC_INC(sc, idx);
	}
	d->re_cmdstat |= htole32(RE_TDESC_CMD_EOF);

	/* Transfer ownership of packet to the chip. */
	d->re_cmdstat |= htole32(RE_TDESC_CMD_OWN);
	if (*idx0 != idx)
		tx_ring[*idx0].re_cmdstat |= htole32(RE_TDESC_CMD_OWN);

	/*
	 * Insure that the map for this transmission
	 * is placed at the array index of the last descriptor
	 * in this chain.
	 */
	sc->re_ldata.re_tx_dmamap[*idx0] = sc->re_ldata.re_tx_dmamap[idx];
	sc->re_ldata.re_tx_dmamap[idx] = map;

	sc->re_ldata.re_tx_mbuf[idx] = m;
	sc->re_ldata.re_tx_free -= nsegs;

	RE_TXDESC_INC(sc, idx);
	*idx0 = idx;
back:
	if (error) {
		m_freem(*m_head);
		*m_head = NULL;
	}
	return error;
}

/*
 * Main transmit routine for C+ and gigE NICs.
 */

static void
re_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct re_softc	*sc = ifp->if_softc;
	struct mbuf *m_head;
	int idx, need_trans, oactive, error;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->re_flags & RE_F_LINKED) == 0) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	idx = sc->re_ldata.re_tx_prodidx;

	need_trans = 0;
	oactive = 0;
	for (;;) {
		if (sc->re_ldata.re_tx_free <= RE_TXDESC_SPARE) {
			if (!oactive) {
				if (re_tx_collect(sc)) {
					oactive = 1;
					continue;
				}
			}
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		error = re_encap(sc, &m_head, &idx);
		if (error) {
			/* m_head is freed by re_encap(), if we reach here */
			IFNET_STAT_INC(ifp, oerrors, 1);

			if (error == EFBIG && !oactive) {
				if (re_tx_collect(sc)) {
					oactive = 1;
					continue;
				}
			}
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		oactive = 0;
		need_trans = 1;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (!need_trans)
		return;

	sc->re_ldata.re_tx_prodidx = idx;

	/*
	 * RealTek put the TX poll request register in a different
	 * location on the 8169 gigE chip. I don't know why.
	 */
	sc->re_start_xmit(sc);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
re_link_up(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	rtl_link_on_patch(sc);
	re_stop(sc, FALSE);
	rtl_set_eaddr(sc);

	error = re_rx_list_init(sc);
	if (error) {
		re_stop(sc, TRUE);
		return;
	}
	error = re_tx_list_init(sc);
	if (error) {
		re_stop(sc, TRUE);
		return;
	}

	/*
	 * Load the addresses of the RX and TX lists into the chip.
	 */
	CSR_WRITE_4(sc, RE_RXLIST_ADDR_HI,
	    RE_ADDR_HI(sc->re_ldata.re_rx_list_addr));
	CSR_WRITE_4(sc, RE_RXLIST_ADDR_LO,
	    RE_ADDR_LO(sc->re_ldata.re_rx_list_addr));

	CSR_WRITE_4(sc, RE_TXLIST_ADDR_HI,
	    RE_ADDR_HI(sc->re_ldata.re_tx_list_addr));
	CSR_WRITE_4(sc, RE_TXLIST_ADDR_LO,
	    RE_ADDR_LO(sc->re_ldata.re_tx_list_addr));

	rtl_hw_start(sc);

#ifdef IFPOLL_ENABLE
	/*
	 * Disable interrupts if we are polling.
	 */
	if (ifp->if_flags & IFF_NPOLLING)
		re_setup_intr(sc, 0, RE_IMTYPE_NONE);
	else	/* otherwise ... */
#endif /* IFPOLL_ENABLE */
	/*
	 * Enable interrupts.
	 */
	re_setup_intr(sc, 1, sc->re_imtype);
	sc->re_write_isr(sc, sc->re_intrs);

	sc->re_flags |= RE_F_LINKED;
	ifp->if_link_state = LINK_STATE_UP;
	if_link_state_change(ifp);

	if (bootverbose)
		if_printf(ifp, "link UP\n");

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
re_link_down(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* NOTE: re_stop() will reset RE_F_LINKED. */
	ifp->if_link_state = LINK_STATE_DOWN;
	if_link_state_change(ifp);

	re_stop(sc, FALSE);
	rtl_ifmedia_upd(ifp);

	if (bootverbose)
		if_printf(ifp, "link DOWN\n");
}

static void
re_init(void *xsc)
{
	struct re_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	re_stop(sc, TRUE);
	if (rtl_link_ok(sc)) {
		if (bootverbose)
			if_printf(ifp, "link is UP in if_init\n");
		re_link_up(sc);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->re_timer, hz, re_tick, sc);
}

static int
re_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct re_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, mask;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch(command) {
	case SIOCSIFMTU:
#ifdef RE_JUMBO
		if (ifr->ifr_mtu > sc->re_maxmtu) {
			error = EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_flags & IFF_RUNNING)
				ifp->if_init(sc);
		}
#else
		error = EOPNOTSUPP;
#endif
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc->re_saved_ifflags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					rtl_set_rx_packet_filter(sc);
			} else {
				re_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			re_stop(sc, TRUE);
		}
		sc->re_saved_ifflags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		rtl_set_rx_packet_filter(sc);
		break;

	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCSIFCAP:
		mask = (ifr->ifr_reqcap ^ ifp->if_capenable) &
		       ifp->if_capabilities;
		ifp->if_capenable ^= mask;

		/* NOTE: re_init will setup if_hwassist. */
		ifp->if_hwassist = 0;

		/* Setup flags for the backend. */
		if (ifp->if_capenable & IFCAP_RXCSUM)
			sc->re_rx_cstag = 1;
		else
			sc->re_rx_cstag = 0;
		if (ifp->if_capenable & IFCAP_TXCSUM)
			sc->re_tx_cstag = 1;
		else
			sc->re_tx_cstag = 0;

		if (mask && (ifp->if_flags & IFF_RUNNING))
			re_init(sc);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

static void
re_watchdog(struct ifnet *ifp)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	IFNET_STAT_INC(ifp, oerrors, 1);

	re_txeof(sc);
	re_rxeof(sc);

	if (sc->re_ldata.re_tx_free != sc->re_tx_desc_cnt) {
		if_printf(ifp, "watchdog timeout, txd free %d\n",
		    sc->re_ldata.re_tx_free);
		rtl_reset(sc);
		re_init(sc);
	}
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
re_stop(struct re_softc *sc, boolean_t full_stop)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/* Stop the adapter. */
	rtl_stop(sc);

	ifp->if_timer = 0;
	if (full_stop) {
		callout_stop(&sc->re_timer);
		ifp->if_flags &= ~IFF_RUNNING;
	}
	ifq_clr_oactive(&ifp->if_snd);
	sc->re_flags &= ~(RE_F_TIMER_INTR | RE_F_DROP_RXFRAG | RE_F_LINKED);

	re_free_rxchain(sc);

	/* Free the TX list buffers. */
	for (i = 0; i < sc->re_tx_desc_cnt; i++) {
		if (sc->re_ldata.re_tx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->re_ldata.re_tx_mtag,
					  sc->re_ldata.re_tx_dmamap[i]);
			m_freem(sc->re_ldata.re_tx_mbuf[i]);
			sc->re_ldata.re_tx_mbuf[i] = NULL;
		}
	}

	/* Free the RX list buffers. */
	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		if (sc->re_ldata.re_rx_mbuf[i] != NULL) {
			if ((sc->re_flags & RE_F_USE_JPOOL) == 0) {
				bus_dmamap_unload(sc->re_ldata.re_rx_mtag,
						  sc->re_ldata.re_rx_dmamap[i]);
			}
			m_freem(sc->re_ldata.re_rx_mbuf[i]);
			sc->re_ldata.re_rx_mbuf[i] = NULL;
		}
	}
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
re_suspend(device_t dev)
{
#ifndef BURN_BRIDGES
	int i;
#endif
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	re_stop(sc, TRUE);

#ifndef BURN_BRIDGES
	for (i = 0; i < 5; i++)
		sc->saved_maps[i] = pci_read_config(dev, PCIR_MAPS + i * 4, 4);
	sc->saved_biosaddr = pci_read_config(dev, PCIR_BIOS, 4);
	sc->saved_intline = pci_read_config(dev, PCIR_INTLINE, 1);
	sc->saved_cachelnsz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	sc->saved_lattimer = pci_read_config(dev, PCIR_LATTIMER, 1);
#endif

	sc->re_flags |= RE_F_SUSPENDED;

	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
re_resume(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
#ifndef BURN_BRIDGES
	int i;
#endif

	lwkt_serialize_enter(ifp->if_serializer);

#ifndef BURN_BRIDGES
	/* better way to do this? */
	for (i = 0; i < 5; i++)
		pci_write_config(dev, PCIR_MAPS + i * 4, sc->saved_maps[i], 4);
	pci_write_config(dev, PCIR_BIOS, sc->saved_biosaddr, 4);
	pci_write_config(dev, PCIR_INTLINE, sc->saved_intline, 1);
	pci_write_config(dev, PCIR_CACHELNSZ, sc->saved_cachelnsz, 1);
	pci_write_config(dev, PCIR_LATTIMER, sc->saved_lattimer, 1);

	/* reenable busmastering */
	pci_enable_busmaster(dev);
	pci_enable_io(dev, SYS_RES_IOPORT);
#endif

	/* reinitialize interface if necessary */
	if (ifp->if_flags & IFF_UP)
		re_init(sc);

	sc->re_flags &= ~RE_F_SUSPENDED;

	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
re_shutdown(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	re_stop(sc, TRUE);
	rtl_hw_d3_para(sc);
	rtl_phy_power_down(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
re_sysctl_rxtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;

	return re_sysctl_hwtime(oidp, arg1, arg2, req, &sc->re_rx_time);
}

static int
re_sysctl_txtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;

	return re_sysctl_hwtime(oidp, arg1, arg2, req, &sc->re_tx_time);
}

static int
re_sysctl_hwtime(SYSCTL_HANDLER_ARGS, int *hwtime)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = *hwtime;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (v != *hwtime) {
		*hwtime = v;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING && sc->re_imtype == RE_IMTYPE_HW)
			re_setup_hw_im(sc);
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
re_sysctl_simtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->re_sim_time;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (v != sc->re_sim_time) {
		sc->re_sim_time = v;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING && sc->re_imtype == RE_IMTYPE_SIM) {
#ifdef foo
			/*
			 * Following code causes various strange
			 * performance problems.  Hmm ...
			 */
			sc->re_write_imr(sc, 0);
			CSR_WRITE_4(sc, RE_TIMERINT, 0);
			CSR_READ_4(sc, RE_TIMERINT); /* flush */

			sc->re_write_imr(sc, sc->re_intrs);
			re_setup_sim_im(sc);
#else
			re_setup_intr(sc, 0, RE_IMTYPE_NONE);
			DELAY(10);
			re_setup_intr(sc, 1, RE_IMTYPE_SIM);
#endif
		}
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
re_sysctl_imtype(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->re_imtype;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v != RE_IMTYPE_HW && v != RE_IMTYPE_SIM && v != RE_IMTYPE_NONE) {
		error = EINVAL;
		goto back;
	}
	if (v == RE_IMTYPE_HW && (sc->re_caps & RE_C_HWIM) == 0) {
		/* Can't do hardware interrupt moderation */
		error = EOPNOTSUPP;
		goto back;
	}

	if (v != sc->re_imtype) {
		sc->re_imtype = v;
		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING)
			re_setup_intr(sc, 1, sc->re_imtype);
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
re_setup_hw_im(struct re_softc *sc)
{
	KKASSERT(sc->re_caps & RE_C_HWIM);

	/*
	 * Interrupt moderation
	 *
	 * 0xABCD
	 * A - unknown (maybe TX related)
	 * B - TX timer (unit: 25us)
	 * C - unknown (maybe RX related)
	 * D - RX timer (unit: 25us)
	 *
	 *
	 * re(4)'s interrupt moderation is actually controlled by
	 * two variables, like most other NICs (bge, bce etc.)
	 * o  timer
	 * o  number of packets [P]
	 *
	 * The logic relationship between these two variables is
	 * similar to other NICs too:
	 * if (timer expire || packets > [P])
	 *     Interrupt is delivered
	 *
	 * Currently we only know how to set 'timer', but not
	 * 'number of packets', which should be ~30, as far as I
	 * tested (sink ~900Kpps, interrupt rate is 30KHz)
	 */
	CSR_WRITE_2(sc, RE_IM,
		    RE_IM_RXTIME(sc->re_rx_time) |
		    RE_IM_TXTIME(sc->re_tx_time) |
		    RE_IM_MAGIC);
}

static void
re_disable_hw_im(struct re_softc *sc)
{
	if (sc->re_caps & RE_C_HWIM)
		CSR_WRITE_2(sc, RE_IM, 0);
}

static void
re_setup_sim_im(struct re_softc *sc)
{
	uint32_t ticks;

	if (sc->re_if_flags & RL_FLAG_PCIE) {
		ticks = sc->re_sim_time * sc->re_bus_speed;
	} else {
		/*
		 * Datasheet says tick decreases at bus speed,
		 * but it seems the clock runs a little bit
		 * faster, so we do some compensation here.
		 */
		ticks = (sc->re_sim_time * sc->re_bus_speed * 8) / 5;
	}
	CSR_WRITE_4(sc, RE_TIMERINT, ticks);

	CSR_WRITE_4(sc, RE_TIMERCNT, 1); /* reload */
	sc->re_flags |= RE_F_TIMER_INTR;
}

static void
re_disable_sim_im(struct re_softc *sc)
{
	CSR_WRITE_4(sc, RE_TIMERINT, 0);
	sc->re_flags &= ~RE_F_TIMER_INTR;
}

static void
re_config_imtype(struct re_softc *sc, int imtype)
{
	switch (imtype) {
	case RE_IMTYPE_HW:
		KKASSERT(sc->re_caps & RE_C_HWIM);
		/* FALL THROUGH */
	case RE_IMTYPE_NONE:
		sc->re_intrs = RE_INTRS;
		sc->re_rx_ack = RE_ISR_RX_OK | RE_ISR_FIFO_OFLOW |
				RE_ISR_RX_OVERRUN;
		sc->re_tx_ack = RE_ISR_TX_OK;
		break;

	case RE_IMTYPE_SIM:
		sc->re_intrs = RE_INTRS_TIMER;
		sc->re_rx_ack = RE_ISR_PCS_TIMEOUT;
		sc->re_tx_ack = RE_ISR_PCS_TIMEOUT;
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->arpcom.ac_if.if_xname, imtype);
	}
}

static void
re_setup_intr(struct re_softc *sc, int enable_intrs, int imtype)
{
	re_config_imtype(sc, imtype);

	if (enable_intrs)
		sc->re_write_imr(sc, sc->re_intrs);
	else
		sc->re_write_imr(sc, 0);

	sc->re_npoll.ifpc_stcount = 0;

	switch (imtype) {
	case RE_IMTYPE_NONE:
		re_disable_sim_im(sc);
		re_disable_hw_im(sc);
		break;

	case RE_IMTYPE_HW:
		KKASSERT(sc->re_caps & RE_C_HWIM);
		re_disable_sim_im(sc);
		re_setup_hw_im(sc);
		break;

	case RE_IMTYPE_SIM:
		re_disable_hw_im(sc);
		re_setup_sim_im(sc);
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->arpcom.ac_if.if_xname, imtype);
	}
}

static int
re_jpool_alloc(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;
	struct re_jbuf *jbuf;
	bus_addr_t paddr;
	bus_size_t jpool_size;
	bus_dmamem_t dmem;
	caddr_t buf;
	int i, error;

	lwkt_serialize_init(&ldata->re_jbuf_serializer);

	ldata->re_jbuf = kmalloc(sizeof(struct re_jbuf) * RE_JBUF_COUNT(sc),
				 M_DEVBUF, M_WAITOK | M_ZERO);

	jpool_size = RE_JBUF_COUNT(sc) * RE_JBUF_SIZE;

	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RXBUF_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			jpool_size, BUS_DMA_WAITOK, &dmem);
	if (error) {
		device_printf(sc->dev, "could not allocate jumbo memory\n");
		return error;
	}
	ldata->re_jpool_tag = dmem.dmem_tag;
	ldata->re_jpool_map = dmem.dmem_map;
	ldata->re_jpool = dmem.dmem_addr;
	paddr = dmem.dmem_busaddr;

	/* ..and split it into 9KB chunks */
	SLIST_INIT(&ldata->re_jbuf_free);

	buf = ldata->re_jpool;
	for (i = 0; i < RE_JBUF_COUNT(sc); i++) {
		jbuf = &ldata->re_jbuf[i];

		jbuf->re_sc = sc;
		jbuf->re_inuse = 0;
		jbuf->re_slot = i;
		jbuf->re_buf = buf;
		jbuf->re_paddr = paddr;

		SLIST_INSERT_HEAD(&ldata->re_jbuf_free, jbuf, re_link);

		buf += RE_JBUF_SIZE;
		paddr += RE_JBUF_SIZE;
	}
	return 0;
}

static void
re_jpool_free(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;

	if (ldata->re_jpool_tag != NULL) {
		bus_dmamap_unload(ldata->re_jpool_tag, ldata->re_jpool_map);
		bus_dmamem_free(ldata->re_jpool_tag, ldata->re_jpool,
				ldata->re_jpool_map);
		bus_dma_tag_destroy(ldata->re_jpool_tag);
		ldata->re_jpool_tag = NULL;
	}

	if (ldata->re_jbuf != NULL) {
		kfree(ldata->re_jbuf, M_DEVBUF);
		ldata->re_jbuf = NULL;
	}
}

#ifdef RE_JUMBO
static struct re_jbuf *
re_jbuf_alloc(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;
	struct re_jbuf *jbuf;

	lwkt_serialize_enter(&ldata->re_jbuf_serializer);

	jbuf = SLIST_FIRST(&ldata->re_jbuf_free);
	if (jbuf != NULL) {
		SLIST_REMOVE_HEAD(&ldata->re_jbuf_free, re_link);
		jbuf->re_inuse = 1;
	}

	lwkt_serialize_exit(&ldata->re_jbuf_serializer);

	return jbuf;
}

static void
re_jbuf_free(void *arg)
{
	struct re_jbuf *jbuf = arg;
	struct re_softc *sc = jbuf->re_sc;
	struct re_list_data *ldata = &sc->re_ldata;

	if (&ldata->re_jbuf[jbuf->re_slot] != jbuf) {
		panic("%s: free wrong jumbo buffer",
		      sc->arpcom.ac_if.if_xname);
	} else if (jbuf->re_inuse == 0) {
		panic("%s: jumbo buffer already freed",
		      sc->arpcom.ac_if.if_xname);
	}

	lwkt_serialize_enter(&ldata->re_jbuf_serializer);
	atomic_subtract_int(&jbuf->re_inuse, 1);
	if (jbuf->re_inuse == 0)
		SLIST_INSERT_HEAD(&ldata->re_jbuf_free, jbuf, re_link);
	lwkt_serialize_exit(&ldata->re_jbuf_serializer);
}

static void
re_jbuf_ref(void *arg)
{
	struct re_jbuf *jbuf = arg;
	struct re_softc *sc = jbuf->re_sc;
	struct re_list_data *ldata = &sc->re_ldata;

	if (&ldata->re_jbuf[jbuf->re_slot] != jbuf) {
		panic("%s: ref wrong jumbo buffer",
		      sc->arpcom.ac_if.if_xname);
	} else if (jbuf->re_inuse == 0) {
		panic("%s: jumbo buffer already freed",
		      sc->arpcom.ac_if.if_xname);
	}
	atomic_add_int(&jbuf->re_inuse, 1);
}
#endif	/* RE_JUMBO */

static void
re_disable_aspm(device_t dev)
{
	uint16_t link_cap, link_ctrl;
	uint8_t pcie_ptr, reg;

	pcie_ptr = pci_get_pciecap_ptr(dev);
	if (pcie_ptr == 0)
		return;

	link_cap = pci_read_config(dev, pcie_ptr + PCIER_LINKCAP, 2);
	if ((link_cap & PCIEM_LNKCAP_ASPM_MASK) == 0)
		return;

	if (bootverbose)
		device_printf(dev, "disable ASPM\n");

	reg = pcie_ptr + PCIER_LINKCTRL;
	link_ctrl = pci_read_config(dev, reg, 2);
	link_ctrl &= ~(PCIEM_LNKCTL_ASPM_L0S | PCIEM_LNKCTL_ASPM_L1);
	pci_write_config(dev, reg, link_ctrl, 2);
}

static void
re_start_xmit(struct re_softc *sc)
{
	CSR_WRITE_1(sc, RE_TPPOLL, RE_NPQ);
}

static void
re_write_imr(struct re_softc *sc, uint32_t val)
{
	CSR_WRITE_2(sc, RE_IMR, val);
}

static void
re_write_isr(struct re_softc *sc, uint32_t val)
{
	CSR_WRITE_2(sc, RE_ISR, val);
}

static uint32_t
re_read_isr(struct re_softc *sc)
{
	return CSR_READ_2(sc, RE_ISR);
}

static void
re_start_xmit_8125(struct re_softc *sc)
{
	CSR_WRITE_2(sc, RE_TPPOLL_8125, RE_NPQ_8125);
}

static void
re_write_imr_8125(struct re_softc *sc, uint32_t val)
{
	CSR_WRITE_4(sc, RE_IMR0_8125, val);
}

static void
re_write_isr_8125(struct re_softc *sc, uint32_t val)
{
	CSR_WRITE_4(sc, RE_ISR0_8125, val);
}

static uint32_t
re_read_isr_8125(struct re_softc *sc)
{
	return CSR_READ_4(sc, RE_ISR0_8125);
}
