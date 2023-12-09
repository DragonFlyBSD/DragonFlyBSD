/*
 * Copyright (c) 2001-2013, Intel Corporation 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 *  1. Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 * 
 *  2. Redistributions in binary form must reproduce the above copyright 
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 * 
 *  3. Neither the name of the Intel Corporation nor the names of its 
 *     contributors may be used to endorse or promote products derived from 
 *     this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_ifpoll.h"
#include "opt_igb.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/serialize2.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/if_ringmap.h>
#include <net/toeplitz.h>
#include <net/toeplitz2.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>
#include <net/if_poll.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include <dev/netif/ig_hal/e1000_api.h>
#include <dev/netif/ig_hal/e1000_82575.h>
#include <dev/netif/ig_hal/e1000_dragonfly.h>
#include <dev/netif/igb/if_igb.h>

#ifdef IGB_RSS_DEBUG
#define IGB_RSS_DPRINTF(sc, lvl, fmt, ...) \
do { \
	if (sc->rss_debug >= lvl) \
		if_printf(&sc->arpcom.ac_if, fmt, __VA_ARGS__); \
} while (0)
#else	/* !IGB_RSS_DEBUG */
#define IGB_RSS_DPRINTF(sc, lvl, fmt, ...)	((void)0)
#endif	/* IGB_RSS_DEBUG */

#define IGB_NAME	"Intel(R) PRO/1000 "
#define IGB_DEVICE(id)	\
	{ IGB_VENDOR_ID, E1000_DEV_ID_##id, IGB_NAME #id }
#define IGB_DEVICE_NULL	{ 0, 0, NULL }

static struct igb_device {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} igb_devices[] = {
	IGB_DEVICE(82575EB_COPPER),
	IGB_DEVICE(82575EB_FIBER_SERDES),
	IGB_DEVICE(82575GB_QUAD_COPPER),
	IGB_DEVICE(82576),
	IGB_DEVICE(82576_NS),
	IGB_DEVICE(82576_NS_SERDES),
	IGB_DEVICE(82576_FIBER),
	IGB_DEVICE(82576_SERDES),
	IGB_DEVICE(82576_SERDES_QUAD),
	IGB_DEVICE(82576_QUAD_COPPER),
	IGB_DEVICE(82576_QUAD_COPPER_ET2),
	IGB_DEVICE(82576_VF),
	IGB_DEVICE(82580_COPPER),
	IGB_DEVICE(82580_FIBER),
	IGB_DEVICE(82580_SERDES),
	IGB_DEVICE(82580_SGMII),
	IGB_DEVICE(82580_COPPER_DUAL),
	IGB_DEVICE(82580_QUAD_FIBER),
	IGB_DEVICE(DH89XXCC_SERDES),
	IGB_DEVICE(DH89XXCC_SGMII),
	IGB_DEVICE(DH89XXCC_SFP),
	IGB_DEVICE(DH89XXCC_BACKPLANE),
	IGB_DEVICE(I350_COPPER),
	IGB_DEVICE(I350_FIBER),
	IGB_DEVICE(I350_SERDES),
	IGB_DEVICE(I350_SGMII),
	IGB_DEVICE(I350_VF),
	IGB_DEVICE(I210_COPPER),
	IGB_DEVICE(I210_COPPER_IT),
	IGB_DEVICE(I210_COPPER_OEM1),
	IGB_DEVICE(I210_COPPER_FLASHLESS),
	IGB_DEVICE(I210_SERDES_FLASHLESS),
	IGB_DEVICE(I210_FIBER),
	IGB_DEVICE(I210_SERDES),
	IGB_DEVICE(I210_SGMII),
	IGB_DEVICE(I211_COPPER),
	IGB_DEVICE(I354_BACKPLANE_1GBPS),
	IGB_DEVICE(I354_BACKPLANE_2_5GBPS),
	IGB_DEVICE(I354_SGMII),

	/* required last entry */
	IGB_DEVICE_NULL
};

static int	igb_probe(device_t);
static int	igb_attach(device_t);
static int	igb_detach(device_t);
static int	igb_shutdown(device_t);
static int	igb_suspend(device_t);
static int	igb_resume(device_t);

static boolean_t igb_is_valid_ether_addr(const uint8_t *);
static void	igb_setup_ifp(struct igb_softc *);
static boolean_t igb_txcsum_ctx(struct igb_tx_ring *, struct mbuf *);
static int	igb_tso_pullup(struct igb_tx_ring *, struct mbuf **);
static void	igb_tso_ctx(struct igb_tx_ring *, struct mbuf *, uint32_t *);
static void	igb_add_sysctl(struct igb_softc *);
static void	igb_add_intr_rate_sysctl(struct igb_softc *, int,
		    const char *, const char *);
static int	igb_sysctl_intr_rate(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_rx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static void	igb_set_ring_inuse(struct igb_softc *, boolean_t);
static int	igb_get_rxring_inuse(const struct igb_softc *, boolean_t);
static int	igb_get_txring_inuse(const struct igb_softc *, boolean_t);
static void	igb_set_timer_cpuid(struct igb_softc *, boolean_t);

static void	igb_vf_init_stats(struct igb_softc *);
static void	igb_reset(struct igb_softc *, boolean_t);
static void	igb_update_stats_counters(struct igb_softc *);
static void	igb_update_vf_stats_counters(struct igb_softc *);
static void	igb_update_link_status(struct igb_softc *);
static void	igb_init_tx_unit(struct igb_softc *);
static void	igb_init_rx_unit(struct igb_softc *, boolean_t);
static void	igb_init_dmac(struct igb_softc *, uint32_t);
static void	igb_reg_dump(struct igb_softc *);
static int	igb_sysctl_reg_dump(SYSCTL_HANDLER_ARGS);

static void	igb_set_vlan(struct igb_softc *);
static void	igb_set_multi(struct igb_softc *);
static void	igb_set_promisc(struct igb_softc *);
static void	igb_disable_promisc(struct igb_softc *);

static int	igb_get_ring_max(const struct igb_softc *);
static void	igb_get_rxring_cnt(const struct igb_softc *, int *, int *);
static void	igb_get_txring_cnt(const struct igb_softc *, int *, int *);
static int	igb_alloc_rings(struct igb_softc *);
static void	igb_free_rings(struct igb_softc *);
static int	igb_create_tx_ring(struct igb_tx_ring *);
static int	igb_create_rx_ring(struct igb_rx_ring *);
static void	igb_free_tx_ring(struct igb_tx_ring *);
static void	igb_free_rx_ring(struct igb_rx_ring *);
static void	igb_destroy_tx_ring(struct igb_tx_ring *, int);
static void	igb_destroy_rx_ring(struct igb_rx_ring *, int);
static void	igb_init_tx_ring(struct igb_tx_ring *);
static int	igb_init_rx_ring(struct igb_rx_ring *);
static int	igb_newbuf(struct igb_rx_ring *, int, boolean_t);
static int	igb_encap(struct igb_tx_ring *, struct mbuf **, int *, int *);
static void	igb_rx_refresh(struct igb_rx_ring *, int);
static void	igb_setup_serialize(struct igb_softc *);

static void	igb_stop(struct igb_softc *);
static void	igb_init(void *);
static int	igb_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	igb_media_status(struct ifnet *, struct ifmediareq *);
static int	igb_media_change(struct ifnet *);
static void	igb_timer(void *);
static void	igb_watchdog(struct ifaltq_subque *);
static void	igb_start(struct ifnet *, struct ifaltq_subque *);
#ifdef IFPOLL_ENABLE
static void	igb_npoll(struct ifnet *, struct ifpoll_info *);
static void	igb_npoll_rx(struct ifnet *, void *, int);
static void	igb_npoll_tx(struct ifnet *, void *, int);
static void	igb_npoll_status(struct ifnet *);
#endif
static void	igb_serialize(struct ifnet *, enum ifnet_serialize);
static void	igb_deserialize(struct ifnet *, enum ifnet_serialize);
static int	igb_tryserialize(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
static void	igb_serialize_assert(struct ifnet *, enum ifnet_serialize,
		    boolean_t);
#endif

static void	igb_intr(void *);
static void	igb_intr_shared(void *);
static void	igb_rxeof(struct igb_rx_ring *, int);
static void	igb_txeof(struct igb_tx_ring *, int);
static void	igb_txgc(struct igb_tx_ring *);
static void	igb_txgc_timer(void *);
static void	igb_set_eitr(struct igb_softc *, int, int);
static void	igb_enable_intr(struct igb_softc *);
static void	igb_disable_intr(struct igb_softc *);
static void	igb_init_unshared_intr(struct igb_softc *);
static void	igb_init_intr(struct igb_softc *);
static int	igb_setup_intr(struct igb_softc *);
static void	igb_set_txintr_mask(struct igb_tx_ring *, int *, int);
static void	igb_set_rxintr_mask(struct igb_rx_ring *, int *, int);
static void	igb_set_intr_mask(struct igb_softc *);
static int	igb_alloc_intr(struct igb_softc *);
static void	igb_free_intr(struct igb_softc *);
static void	igb_teardown_intr(struct igb_softc *, int);
static void	igb_alloc_msix(struct igb_softc *);
static void	igb_free_msix(struct igb_softc *, boolean_t);
static void	igb_msix_rx(void *);
static void	igb_msix_tx(void *);
static void	igb_msix_status(void *);
static void	igb_msix_rxtx(void *);

/* Management and WOL Support */
static void	igb_get_mgmt(struct igb_softc *);
static void	igb_rel_mgmt(struct igb_softc *);
static void	igb_get_hw_control(struct igb_softc *);
static void	igb_rel_hw_control(struct igb_softc *);
static void	igb_enable_wol(struct igb_softc *);
static int	igb_enable_phy_wol(struct igb_softc *);

static device_method_t igb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		igb_probe),
	DEVMETHOD(device_attach,	igb_attach),
	DEVMETHOD(device_detach,	igb_detach),
	DEVMETHOD(device_shutdown,	igb_shutdown),
	DEVMETHOD(device_suspend,	igb_suspend),
	DEVMETHOD(device_resume,	igb_resume),
	DEVMETHOD_END
};

static driver_t igb_driver = {
	"igb",
	igb_methods,
	sizeof(struct igb_softc),
};

static devclass_t igb_devclass;

DECLARE_DUMMY_MODULE(if_igb);
MODULE_DEPEND(igb, ig_hal, 1, 1, 1);
DRIVER_MODULE(if_igb, pci, igb_driver, igb_devclass, NULL, NULL);

static int	igb_rxd = IGB_DEFAULT_RXD;
static int	igb_txd = IGB_DEFAULT_TXD;
static int	igb_rxr = 0;
static int	igb_txr = 0;
static int	igb_msi_enable = 1;
static int	igb_msix_enable = 1;
static int	igb_eee_disabled = 1;	/* Energy Efficient Ethernet */

static char	igb_flowctrl[IFM_ETH_FC_STRLEN] = IFM_ETH_FC_NONE;

/*
 * DMA Coalescing, only for i350 - default to off,
 * this feature is for power savings
 */
static int	igb_dma_coalesce = 0;

TUNABLE_INT("hw.igb.rxd", &igb_rxd);
TUNABLE_INT("hw.igb.txd", &igb_txd);
TUNABLE_INT("hw.igb.rxr", &igb_rxr);
TUNABLE_INT("hw.igb.txr", &igb_txr);
TUNABLE_INT("hw.igb.msi.enable", &igb_msi_enable);
TUNABLE_INT("hw.igb.msix.enable", &igb_msix_enable);
TUNABLE_STR("hw.igb.flow_ctrl", igb_flowctrl, sizeof(igb_flowctrl));

/* i350 specific */
TUNABLE_INT("hw.igb.eee_disabled", &igb_eee_disabled);
TUNABLE_INT("hw.igb.dma_coalesce", &igb_dma_coalesce);

static __inline void
igb_tx_intr(struct igb_tx_ring *txr, int hdr)
{

	igb_txeof(txr, hdr);
	if (!ifsq_is_empty(txr->ifsq))
		ifsq_devstart(txr->ifsq);
}

static __inline void
igb_try_txgc(struct igb_tx_ring *txr, int16_t dec)
{

	if (txr->tx_running > 0) {
		txr->tx_running -= dec;
		if (txr->tx_running <= 0 && txr->tx_nmbuf &&
		    txr->tx_avail < txr->num_tx_desc &&
		    txr->tx_avail + txr->intr_nsegs > txr->num_tx_desc)
			igb_txgc(txr);
	}
}

static void
igb_txgc_timer(void *xtxr)
{
	struct igb_tx_ring *txr = xtxr;
	struct ifnet *ifp = &txr->sc->arpcom.ac_if;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP))
		return;

	if (!lwkt_serialize_try(&txr->tx_serialize))
		goto done;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP)) {
		lwkt_serialize_exit(&txr->tx_serialize);
		return;
	}
	igb_try_txgc(txr, IGB_TX_RUNNING_DEC);

	lwkt_serialize_exit(&txr->tx_serialize);
done:
	callout_reset(&txr->tx_gc_timer, 1, igb_txgc_timer, txr);
}

static __inline void
igb_free_txbuf(struct igb_tx_ring *txr, struct igb_tx_buf *txbuf)
{

	KKASSERT(txbuf->m_head != NULL);
	KKASSERT(txr->tx_nmbuf > 0);
	txr->tx_nmbuf--;

	bus_dmamap_unload(txr->tx_tag, txbuf->map);
	m_freem(txbuf->m_head);
	txbuf->m_head = NULL;
}

static __inline void
igb_rxcsum(uint32_t staterr, struct mbuf *mp)
{
	/* Ignore Checksum bit is set */
	if (staterr & E1000_RXD_STAT_IXSM)
		return;

	if ((staterr & (E1000_RXD_STAT_IPCS | E1000_RXDEXT_STATERR_IPE)) ==
	    E1000_RXD_STAT_IPCS)
		mp->m_pkthdr.csum_flags |= CSUM_IP_CHECKED | CSUM_IP_VALID;

	if (staterr & (E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS)) {
		if ((staterr & E1000_RXDEXT_STATERR_TCPE) == 0) {
			mp->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
			    CSUM_PSEUDO_HDR | CSUM_FRAG_NOT_CHECKED;
			mp->m_pkthdr.csum_data = htons(0xffff);
		}
	}
}

static __inline struct pktinfo *
igb_rssinfo(struct mbuf *m, struct pktinfo *pi,
    uint32_t hash, uint32_t hashtype, uint32_t staterr)
{
	switch (hashtype) {
	case E1000_RXDADV_RSSTYPE_IPV4_TCP:
		pi->pi_netisr = NETISR_IP;
		pi->pi_flags = 0;
		pi->pi_l3proto = IPPROTO_TCP;
		break;

	case E1000_RXDADV_RSSTYPE_IPV4:
		if (staterr & E1000_RXD_STAT_IXSM)
			return NULL;

		if ((staterr &
		     (E1000_RXD_STAT_TCPCS | E1000_RXDEXT_STATERR_TCPE)) ==
		    E1000_RXD_STAT_TCPCS) {
			pi->pi_netisr = NETISR_IP;
			pi->pi_flags = 0;
			pi->pi_l3proto = IPPROTO_UDP;
			break;
		}
		/* FALL THROUGH */
	default:
		return NULL;
	}

	m_sethash(m, toeplitz_hash(hash));
	return pi;
}

static int
igb_get_ring_max(const struct igb_softc *sc)
{

	switch (sc->hw.mac.type) {
	case e1000_82575:
		return (IGB_MAX_RING_82575);

	case e1000_82576:
		return (IGB_MAX_RING_82576);

	case e1000_82580:
		return (IGB_MAX_RING_82580);

	case e1000_i350:
		return (IGB_MAX_RING_I350);

	case e1000_i354:
		return (IGB_MAX_RING_I354);

	case e1000_i210:
		return (IGB_MAX_RING_I210);

	case e1000_i211:
		return (IGB_MAX_RING_I211);

	default:
		return (IGB_MIN_RING);
	}
}

static void
igb_get_rxring_cnt(const struct igb_softc *sc, int *ring_cnt, int *ring_max)
{

	*ring_max = igb_get_ring_max(sc);
	*ring_cnt = device_getenv_int(sc->dev, "rxr", igb_rxr);
}

static void
igb_get_txring_cnt(const struct igb_softc *sc, int *ring_cnt, int *ring_max)
{

	*ring_max = igb_get_ring_max(sc);
	*ring_cnt = device_getenv_int(sc->dev, "txr", igb_txr);
}

static int
igb_probe(device_t dev)
{
	const struct igb_device *d;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = igb_devices; d->desc != NULL; ++d) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
igb_attach(device_t dev)
{
	struct igb_softc *sc = device_get_softc(dev);
	uint16_t eeprom_data;
	int error = 0, ring_max, ring_cnt;
	char flowctrl[IFM_ETH_FC_STRLEN];

#ifdef notyet
	/* SYSCTL stuff */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "nvm", CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
	    igb_sysctl_nvm_info, "I", "NVM Information");
#endif

	ifmedia_init(&sc->media, IFM_IMASK | IFM_ETH_FCMASK,
	    igb_media_change, igb_media_status);
	callout_init_mp(&sc->timer);
	lwkt_serialize_init(&sc->main_serialize);

	if_initname(&sc->arpcom.ac_if, device_get_name(dev),
	    device_get_unit(dev));
	sc->dev = sc->osdep.dev = dev;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Determine hardware and mac type
	 */
	sc->hw.vendor_id = pci_get_vendor(dev);
	sc->hw.device_id = pci_get_device(dev);
	sc->hw.revision_id = pci_read_config(dev, PCIR_REVID, 1);
	sc->hw.subsystem_vendor_id = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	sc->hw.subsystem_device_id = pci_read_config(dev, PCIR_SUBDEV_0, 2);

	if (e1000_set_mac_type(&sc->hw))
		return ENXIO;

	/* Are we a VF device? */
	if (sc->hw.mac.type == e1000_vfadapt ||
	    sc->hw.mac.type == e1000_vfadapt_i350)
		sc->vf_ifp = 1;
	else
		sc->vf_ifp = 0;

	/*
	 * Configure total supported RX/TX ring count
	 */
	igb_get_rxring_cnt(sc, &ring_cnt, &ring_max);
	sc->rx_rmap = if_ringmap_alloc(dev, ring_cnt, ring_max);
	igb_get_txring_cnt(sc, &ring_cnt, &ring_max);
	sc->tx_rmap = if_ringmap_alloc(dev, ring_cnt, ring_max);
	if_ringmap_match(dev, sc->rx_rmap, sc->tx_rmap);

	sc->rx_ring_cnt = if_ringmap_count(sc->rx_rmap);
	sc->rx_ring_inuse = sc->rx_ring_cnt;
	sc->tx_ring_cnt = if_ringmap_count(sc->tx_rmap);
	sc->tx_ring_inuse = sc->tx_ring_cnt;

	/* Setup flow control. */
	device_getenv_string(dev, "flow_ctrl", flowctrl, sizeof(flowctrl),
	    igb_flowctrl);
	sc->ifm_flowctrl = ifmedia_str2ethfc(flowctrl);

	/*
	 * Allocate IO memory
	 */
	sc->mem_rid = PCIR_BAR(0);
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Unable to allocate bus resource: memory\n");
		error = ENXIO;
		goto failed;
	}
	sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->mem_res);
	sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->mem_res);

	sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;

	/* Save PCI command register for Shared Code */
	sc->hw.bus.pci_cmd_word = pci_read_config(dev, PCIR_COMMAND, 2);
	sc->hw.back = &sc->osdep;

	/* Do Shared Code initialization */
	if (e1000_setup_init_funcs(&sc->hw, TRUE)) {
		device_printf(dev, "Setup of Shared code failed\n");
		error = ENXIO;
		goto failed;
	}

	e1000_get_bus_info(&sc->hw);

	sc->hw.mac.autoneg = DO_AUTO_NEG;
	sc->hw.phy.autoneg_wait_to_complete = FALSE;
	sc->hw.phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;

	/* Copper options */
	if (sc->hw.phy.media_type == e1000_media_type_copper) {
		sc->hw.phy.mdix = AUTO_ALL_MODES;
		sc->hw.phy.disable_polarity_correction = FALSE;
		sc->hw.phy.ms_type = IGB_MASTER_SLAVE;
	}

	/* Set the frame limits assuming  standard ethernet sized frames. */
	sc->max_frame_size = ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN;

	/* Allocate RX/TX rings */
	error = igb_alloc_rings(sc);
	if (error)
		goto failed;

	/* Allocate interrupt */
	error = igb_alloc_intr(sc);
	if (error)
		goto failed;

	/* Setup serializes */
	igb_setup_serialize(sc);

	/* Allocate the appropriate stats memory */
	if (sc->vf_ifp) {
		sc->stats = kmalloc(sizeof(struct e1000_vf_stats), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		igb_vf_init_stats(sc);
	} else {
		sc->stats = kmalloc(sizeof(struct e1000_hw_stats), M_DEVBUF,
		    M_WAITOK | M_ZERO);
	}

	/* Allocate multicast array memory. */
	sc->mta = kmalloc(ETHER_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES,
	    M_DEVBUF, M_WAITOK);

	/* Some adapter-specific advanced features */
	if (sc->hw.mac.type >= e1000_i350) {
#ifdef notyet
		igb_set_sysctl_value(adapter, "dma_coalesce",
		    "configure dma coalesce",
		    &adapter->dma_coalesce, igb_dma_coalesce);
		igb_set_sysctl_value(adapter, "eee_disabled",
		    "enable Energy Efficient Ethernet",
		    &adapter->hw.dev_spec._82575.eee_disable,
		    igb_eee_disabled);
#else
		sc->dma_coalesce = igb_dma_coalesce;
		sc->hw.dev_spec._82575.eee_disable = igb_eee_disabled;
#endif
		if (sc->hw.phy.media_type == e1000_media_type_copper) {
                        if (sc->hw.mac.type == e1000_i354)
				e1000_set_eee_i354(&sc->hw, TRUE, TRUE);
			else
				e1000_set_eee_i350(&sc->hw, TRUE, TRUE);
		}
	}

	/*
	 * Start from a known state, this is important in reading the nvm and
	 * mac from that.
	 */
	e1000_reset_hw(&sc->hw);

	/* Make sure we have a good EEPROM before we read from it */
	if (sc->hw.mac.type != e1000_i210 && sc->hw.mac.type != e1000_i211 &&
	    e1000_validate_nvm_checksum(&sc->hw) < 0) {
		/*
		 * Some PCI-E parts fail the first check due to
		 * the link being in sleep state, call it again,
		 * if it fails a second time its a real issue.
		 */
		if (e1000_validate_nvm_checksum(&sc->hw) < 0) {
			device_printf(dev,
			    "The EEPROM Checksum Is Not Valid\n");
			error = EIO;
			goto failed;
		}
	}

	/* Copy the permanent MAC address out of the EEPROM */
	if (e1000_read_mac_addr(&sc->hw) < 0) {
		device_printf(dev, "EEPROM read error while reading MAC"
		    " address\n");
		error = EIO;
		goto failed;
	}
	if (!igb_is_valid_ether_addr(sc->hw.mac.addr)) {
		device_printf(dev, "Invalid MAC address\n");
		error = EIO;
		goto failed;
	}

	/* Setup OS specific network interface */
	igb_setup_ifp(sc);

	/* Add sysctl tree, must after igb_setup_ifp() */
	igb_add_sysctl(sc);

	/* Now get a good starting state */
	igb_reset(sc, FALSE);

	/* Initialize statistics */
	igb_update_stats_counters(sc);

	sc->hw.mac.get_link_status = 1;
	igb_update_link_status(sc);

	/* Indicate SOL/IDER usage */
	if (e1000_check_reset_block(&sc->hw)) {
		device_printf(dev,
		    "PHY reset is blocked due to SOL/IDER session.\n");
	}

	/* Determine if we have to control management hardware */
	if (e1000_enable_mng_pass_thru(&sc->hw))
		sc->flags |= IGB_FLAG_HAS_MGMT;

	/*
	 * Setup Wake-on-Lan
	 */
	/* APME bit in EEPROM is mapped to WUC.APME */
	eeprom_data = E1000_READ_REG(&sc->hw, E1000_WUC) & E1000_WUC_APME;
	if (eeprom_data) {
		/* XXX E1000_WUFC_MC always be cleared from E1000_WUC. */
		sc->wol = E1000_WUFC_MAG | E1000_WUFC_MC;
		device_printf(dev, "has WOL\n");
	}

#ifdef notyet
	/* Register for VLAN events */
	adapter->vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
	     igb_register_vlan, adapter, EVENTHANDLER_PRI_FIRST);
	adapter->vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
	     igb_unregister_vlan, adapter, EVENTHANDLER_PRI_FIRST);
#endif

#ifdef notyet
	igb_add_hw_stats(adapter);
#endif

	/*
	 * Disable interrupt to prevent spurious interrupts (line based
	 * interrupt, MSI or even MSI-X), which had been observed on
	 * several types of LOMs, from being handled.
	 */
	igb_disable_intr(sc);

	error = igb_setup_intr(sc);
	if (error) {
		ether_ifdetach(&sc->arpcom.ac_if);
		goto failed;
	}
	return 0;

failed:
	igb_detach(dev);
	return error;
}

static int
igb_detach(device_t dev)
{
	struct igb_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ifnet_serialize_all(ifp);

		igb_stop(sc);

		e1000_phy_hw_reset(&sc->hw);

		/* Give control back to firmware */
		igb_rel_mgmt(sc);
		igb_rel_hw_control(sc);
		igb_enable_wol(sc);

		igb_teardown_intr(sc, sc->intr_cnt);

		ifnet_deserialize_all(ifp);

		ether_ifdetach(ifp);
	} else if (sc->mem_res != NULL) {
		igb_rel_hw_control(sc);
	}

	ifmedia_removeall(&sc->media);
	bus_generic_detach(dev);

	igb_free_intr(sc);

	if (sc->msix_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->msix_mem_rid,
		    sc->msix_mem_res);
	}
	if (sc->mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
		    sc->mem_res);
	}

	igb_free_rings(sc);

	if (sc->mta != NULL)
		kfree(sc->mta, M_DEVBUF);
	if (sc->stats != NULL)
		kfree(sc->stats, M_DEVBUF);
	if (sc->serializes != NULL)
		kfree(sc->serializes, M_DEVBUF);
	if (sc->rx_rmap != NULL)
		if_ringmap_free(sc->rx_rmap);
	if (sc->rx_rmap_intr != NULL)
		if_ringmap_free(sc->rx_rmap_intr);
	if (sc->tx_rmap != NULL)
		if_ringmap_free(sc->tx_rmap);
	if (sc->tx_rmap_intr != NULL)
		if_ringmap_free(sc->tx_rmap_intr);

	return 0;
}

static int
igb_shutdown(device_t dev)
{
	return igb_suspend(dev);
}

static int
igb_suspend(device_t dev)
{
	struct igb_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);

	igb_stop(sc);

	igb_rel_mgmt(sc);
	igb_rel_hw_control(sc);
	igb_enable_wol(sc);

	ifnet_deserialize_all(ifp);

	return bus_generic_suspend(dev);
}

static int
igb_resume(device_t dev)
{
	struct igb_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ifnet_serialize_all(ifp);

	igb_init(sc);
	igb_get_mgmt(sc);

	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ifsq_devstart_sched(sc->tx_rings[i].ifsq);

	ifnet_deserialize_all(ifp);

	return bus_generic_resume(dev);
}

static int
igb_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct igb_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int max_frame_size, mask, reinit;
	int error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (command) {
	case SIOCSIFMTU:
		max_frame_size = 9234;
		if (ifr->ifr_mtu > max_frame_size - ETHER_HDR_LEN -
		    ETHER_CRC_LEN) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		sc->max_frame_size = ifp->if_mtu + ETHER_HDR_LEN +
		    ETHER_CRC_LEN;

		if (ifp->if_flags & IFF_RUNNING)
			igb_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					igb_disable_promisc(sc);
					igb_set_promisc(sc);
				}
			} else {
				igb_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			igb_stop(sc);
		}
		sc->if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING) {
			igb_disable_intr(sc);
			igb_set_multi(sc);
#ifdef IFPOLL_ENABLE
			if (!(ifp->if_flags & IFF_NPOLLING))
#endif
				igb_enable_intr(sc);
		}
		break;

	case SIOCSIFMEDIA:
		/* Check SOL/IDER usage */
		if (e1000_check_reset_block(&sc->hw)) {
			if_printf(ifp, "Media change is "
			    "blocked due to SOL/IDER session.\n");
			break;
		}
		/* FALL THROUGH */

	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCSIFCAP:
		reinit = 0;
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_RXCSUM) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reinit = 1;
		}
		if (mask & IFCAP_VLAN_HWTAGGING) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			reinit = 1;
		}
		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= IGB_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~IGB_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= IFCAP_TSO;
			if (ifp->if_capenable & IFCAP_TSO)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if (mask & IFCAP_RSS)
			ifp->if_capenable ^= IFCAP_RSS;
		if (reinit && (ifp->if_flags & IFF_RUNNING))
			igb_init(sc);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return error;
}

static void
igb_init(void *xsc)
{
	struct igb_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	boolean_t polling;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	igb_stop(sc);

	/* Get the latest mac address, User can use a LAA */
	bcopy(IF_LLADDR(ifp), sc->hw.mac.addr, ETHER_ADDR_LEN);

	/* Put the address into the Receive Address Array */
	e1000_rar_set(&sc->hw, sc->hw.mac.addr, 0);

	igb_reset(sc, FALSE);
	igb_update_link_status(sc);

	E1000_WRITE_REG(&sc->hw, E1000_VET, ETHERTYPE_VLAN);

	/* Clear bad data from Rx FIFOs */
	e1000_rx_fifo_flush_82575(&sc->hw);

	/* Configure for OS presence */
	igb_get_mgmt(sc);

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	/* Configured used RX/TX rings */
	igb_set_ring_inuse(sc, polling);
	ifq_set_subq_divisor(&ifp->if_snd, sc->tx_ring_inuse);

	/* Initialize interrupt */
	igb_init_intr(sc);

	/* Prepare transmit descriptors and buffers */
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		igb_init_tx_ring(&sc->tx_rings[i]);
	igb_init_tx_unit(sc);

	/* Setup Multicast table */
	igb_set_multi(sc);

#if 0
	/*
	 * Figure out the desired mbuf pool
	 * for doing jumbo/packetsplit
	 */
	if (adapter->max_frame_size <= 2048)
		adapter->rx_mbuf_sz = MCLBYTES;
	else if (adapter->max_frame_size <= 4096)
		adapter->rx_mbuf_sz = MJUMPAGESIZE;
	else
		adapter->rx_mbuf_sz = MJUM9BYTES;
#endif

	/* Prepare receive descriptors and buffers */
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		int error;

		error = igb_init_rx_ring(&sc->rx_rings[i]);
		if (error) {
			if_printf(ifp, "Could not setup receive structures\n");
			igb_stop(sc);
			return;
		}
	}
	igb_init_rx_unit(sc, polling);

	/* Enable VLAN support */
	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		igb_set_vlan(sc);

	/* Don't lose promiscuous settings */
	igb_set_promisc(sc);

	/* Clear counters */
	e1000_clear_hw_cntrs_base_generic(&sc->hw);

	/* This clears any pending interrupts */
	E1000_READ_REG(&sc->hw, E1000_ICR);

	/*
	 * Only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (polling) {
		igb_disable_intr(sc);
	} else {
		igb_enable_intr(sc);
		E1000_WRITE_REG(&sc->hw, E1000_ICS, E1000_ICS_LSC);
	}

	/* Set Energy Efficient Ethernet */
	if (sc->hw.phy.media_type == e1000_media_type_copper) {
		if (sc->hw.mac.type == e1000_i354)
			e1000_set_eee_i354(&sc->hw, TRUE, TRUE);
		else
			e1000_set_eee_i350(&sc->hw, TRUE, TRUE);
	}

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		ifsq_clr_oactive(txr->ifsq);
		ifsq_watchdog_start(&txr->tx_watchdog);

		if (!polling) {
			callout_reset_bycpu(&txr->tx_gc_timer, 1,
			    igb_txgc_timer, txr, txr->tx_intr_cpuid);
                }
	}

	igb_set_timer_cpuid(sc, polling);
	callout_reset_bycpu(&sc->timer, hz, igb_timer, sc, sc->timer_cpuid);
}

static void
igb_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct igb_softc *sc = ifp->if_softc;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		sc->hw.mac.get_link_status = 1;
	igb_update_link_status(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!sc->link_active) {
		if (sc->hw.mac.autoneg)
			ifmr->ifm_active |= IFM_NONE;
		else
			ifmr->ifm_active |= sc->media.ifm_media;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;
	if (sc->ifm_flowctrl & IFM_ETH_FORCEPAUSE)
		ifmr->ifm_active |= sc->ifm_flowctrl;

	switch (sc->link_speed) {
	case 10:
		ifmr->ifm_active |= IFM_10_T;
		break;

	case 100:
		/*
		 * Support for 100Mb SFP - these are Fiber 
		 * but the media type appears as serdes
		 */
		if (sc->hw.phy.media_type == e1000_media_type_fiber ||
		    sc->hw.phy.media_type == e1000_media_type_internal_serdes)
			ifmr->ifm_active |= IFM_100_FX;
		else
			ifmr->ifm_active |= IFM_100_TX;
		break;

	case 1000:
		if (sc->hw.phy.media_type == e1000_media_type_fiber ||
		    sc->hw.phy.media_type == e1000_media_type_internal_serdes)
			ifmr->ifm_active |= IFM_1000_SX;
		else
			ifmr->ifm_active |= IFM_1000_T;
		break;

	case 2500:
		ifmr->ifm_active |= IFM_2500_SX;
		break;
	}

	if (sc->link_duplex == FULL_DUPLEX)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;

	if (sc->link_duplex == FULL_DUPLEX)
		ifmr->ifm_active |= e1000_fc2ifmedia(sc->hw.fc.current_mode);
}

static int
igb_media_change(struct ifnet *ifp)
{
	struct igb_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return EINVAL;

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->hw.mac.autoneg = DO_AUTO_NEG;
		sc->hw.phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;
		break;

	case IFM_1000_SX:
	case IFM_1000_T:
		sc->hw.mac.autoneg = DO_AUTO_NEG;
		sc->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;

	case IFM_100_TX:
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_FDX) {
			sc->hw.mac.forced_speed_duplex = ADVERTISE_100_FULL;
		} else {
			if (IFM_OPTIONS(ifm->ifm_media) &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
				if (bootverbose) {
					if_printf(ifp, "Flow control is not "
					    "allowed for half-duplex\n");
				}
				return EINVAL;
			}
			sc->hw.mac.forced_speed_duplex = ADVERTISE_100_HALF;
		}
		sc->hw.mac.autoneg = FALSE;
		sc->hw.phy.autoneg_advertised = 0;
		break;

	case IFM_10_T:
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_FDX) {
			sc->hw.mac.forced_speed_duplex = ADVERTISE_10_FULL;
		} else {
			if (IFM_OPTIONS(ifm->ifm_media) &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
				if (bootverbose) {
					if_printf(ifp, "Flow control is not "
					    "allowed for half-duplex\n");
				}
				return EINVAL;
			}
			sc->hw.mac.forced_speed_duplex = ADVERTISE_10_HALF;
		}
		sc->hw.mac.autoneg = FALSE;
		sc->hw.phy.autoneg_advertised = 0;
		break;

	default:
		if (bootverbose) {
			if_printf(ifp, "Unsupported media type %d\n",
			    IFM_SUBTYPE(ifm->ifm_media));
		}
		return EINVAL;
	}
	sc->ifm_flowctrl = ifm->ifm_media & IFM_ETH_FCMASK;

	if (ifp->if_flags & IFF_RUNNING)
		igb_init(sc);

	return 0;
}

static void
igb_set_promisc(struct igb_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct e1000_hw *hw = &sc->hw;
	uint32_t reg;

	if (sc->vf_ifp) {
		e1000_promisc_set_vf(hw, e1000_promisc_enabled);
		return;
	}

	reg = E1000_READ_REG(hw, E1000_RCTL);
	if (ifp->if_flags & IFF_PROMISC) {
		reg |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		E1000_WRITE_REG(hw, E1000_RCTL, reg);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg |= E1000_RCTL_MPE;
		reg &= ~E1000_RCTL_UPE;
		E1000_WRITE_REG(hw, E1000_RCTL, reg);
	}
}

static void
igb_disable_promisc(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg;
	int mcnt = 0;

	if (sc->vf_ifp) {
		e1000_promisc_set_vf(hw, e1000_promisc_disabled);
		return;
	}
	reg = E1000_READ_REG(hw, E1000_RCTL);
	reg &= ~E1000_RCTL_UPE;
	if (ifp->if_flags & IFF_ALLMULTI) {
		mcnt = MAX_NUM_MULTICAST_ADDRESSES;
	} else {
		struct  ifmultiaddr *ifma;
		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			if (mcnt == MAX_NUM_MULTICAST_ADDRESSES)
				break;
			mcnt++;
		}
	}
	/* Don't disable if in MAX groups */
	if (mcnt < MAX_NUM_MULTICAST_ADDRESSES)
		reg &= ~E1000_RCTL_MPE;
	E1000_WRITE_REG(hw, E1000_RCTL, reg);
}

static void
igb_set_multi(struct igb_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t reg_rctl = 0;
	uint8_t *mta;
	int mcnt = 0;

	mta = sc->mta;
	bzero(mta, ETH_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES);

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == MAX_NUM_MULTICAST_ADDRESSES)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &mta[mcnt * ETH_ADDR_LEN], ETH_ADDR_LEN);
		mcnt++;
	}

	if (mcnt >= MAX_NUM_MULTICAST_ADDRESSES) {
		reg_rctl = E1000_READ_REG(&sc->hw, E1000_RCTL);
		reg_rctl |= E1000_RCTL_MPE;
		E1000_WRITE_REG(&sc->hw, E1000_RCTL, reg_rctl);
	} else {
		e1000_update_mc_addr_list(&sc->hw, mta, mcnt);
	}
}

static void
igb_timer(void *xsc)
{
	struct igb_softc *sc = xsc;

	lwkt_serialize_enter(&sc->main_serialize);

	igb_update_link_status(sc);
	igb_update_stats_counters(sc);

	callout_reset_bycpu(&sc->timer, hz, igb_timer, sc, sc->timer_cpuid);

	lwkt_serialize_exit(&sc->main_serialize);
}

static void
igb_update_link_status(struct igb_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct e1000_hw *hw = &sc->hw;
	uint32_t link_check, thstat, ctrl;

	link_check = thstat = ctrl = 0;

	/* Get the cached link value or read for real */
	switch (hw->phy.media_type) {
	case e1000_media_type_copper:
		if (hw->mac.get_link_status) {
			/* Do the work to read phy */
			e1000_check_for_link(hw);
			link_check = !hw->mac.get_link_status;
		} else {
			link_check = TRUE;
		}
		break;

	case e1000_media_type_fiber:
		e1000_check_for_link(hw);
		link_check = E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU;
		break;

	case e1000_media_type_internal_serdes:
		e1000_check_for_link(hw);
		link_check = hw->mac.serdes_has_link;
		break;

	/* VF device is type_unknown */
	case e1000_media_type_unknown:
		e1000_check_for_link(hw);
		link_check = !hw->mac.get_link_status;
		/* Fall thru */
	default:
		break;
	}

	/* Check for thermal downshift or shutdown */
	if (hw->mac.type == e1000_i350) {
		thstat = E1000_READ_REG(hw, E1000_THSTAT);
		ctrl = E1000_READ_REG(hw, E1000_CTRL_EXT);
	}

	/* Now we check if a transition has happened */
	if (link_check && sc->link_active == 0) {
		e1000_get_speed_and_duplex(hw, 
		    &sc->link_speed, &sc->link_duplex);
		if (bootverbose) {
			char flowctrl[IFM_ETH_FC_STRLEN];

			/* Get the flow control for display */
			e1000_fc2str(hw->fc.current_mode, flowctrl,
			    sizeof(flowctrl));

			if_printf(ifp, "Link is up %d Mbps %s, "
			    "Flow control: %s\n",
			    sc->link_speed,
			    sc->link_duplex == FULL_DUPLEX ?
			    "Full Duplex" : "Half Duplex",
			    flowctrl);
		}
		if (sc->ifm_flowctrl & IFM_ETH_FORCEPAUSE)
			e1000_force_flowctrl(hw, sc->ifm_flowctrl);
		sc->link_active = 1;

		ifp->if_baudrate = sc->link_speed * 1000000;
		if ((ctrl & E1000_CTRL_EXT_LINK_MODE_GMII) &&
		    (thstat & E1000_THSTAT_LINK_THROTTLE))
			if_printf(ifp, "Link: thermal downshift\n");
		/* Delay Link Up for Phy update */
		if ((hw->mac.type == e1000_i210 ||
		     hw->mac.type == e1000_i211) &&
		    hw->phy.id == I210_I_PHY_ID)
			msec_delay(IGB_I210_LINK_DELAY);
		/*
		 * Reset if the media type changed.
		 * Support AutoMediaDetect for Marvell M88 PHY in i354.
		 */
		if (hw->dev_spec._82575.media_changed) {
			hw->dev_spec._82575.media_changed = FALSE;
			igb_reset(sc, TRUE);
		}
		/* This can sleep */
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
	} else if (!link_check && sc->link_active == 1) {
		ifp->if_baudrate = sc->link_speed = 0;
		sc->link_duplex = 0;
		if (bootverbose)
			if_printf(ifp, "Link is Down\n");
		if ((ctrl & E1000_CTRL_EXT_LINK_MODE_GMII) &&
		    (thstat & E1000_THSTAT_PWR_DOWN))
			if_printf(ifp, "Link: thermal shutdown\n");
		sc->link_active = 0;
		/* This can sleep */
		ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(ifp);
	}
}

static void
igb_stop(struct igb_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	igb_disable_intr(sc);

	callout_stop(&sc->timer);

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		ifsq_clr_oactive(txr->ifsq);
		ifsq_watchdog_stop(&txr->tx_watchdog);
		txr->tx_flags &= ~IGB_TXFLAG_ENABLED;

		txr->tx_running = 0;
		callout_stop(&txr->tx_gc_timer);
	}

	e1000_reset_hw(&sc->hw);
	E1000_WRITE_REG(&sc->hw, E1000_WUC, 0);

	e1000_led_off(&sc->hw);
	e1000_cleanup_led(&sc->hw);

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		igb_free_tx_ring(&sc->tx_rings[i]);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		igb_free_rx_ring(&sc->rx_rings[i]);
}

static void
igb_reset(struct igb_softc *sc, boolean_t media_reset)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct e1000_hw *hw = &sc->hw;
	struct e1000_fc_info *fc = &hw->fc;
	uint32_t pba = 0;
	uint16_t hwm;

	/* Let the firmware know the OS is in control */
	igb_get_hw_control(sc);

	/*
	 * Packet Buffer Allocation (PBA)
	 * Writing PBA sets the receive portion of the buffer
	 * the remainder is used for the transmit buffer.
	 */
	switch (hw->mac.type) {
	case e1000_82575:
		pba = E1000_PBA_32K;
		break;

	case e1000_82576:
	case e1000_vfadapt:
		pba = E1000_READ_REG(hw, E1000_RXPBS);
		pba &= E1000_RXPBS_SIZE_MASK_82576;
		break;

	case e1000_82580:
	case e1000_i350:
	case e1000_i354:
	case e1000_vfadapt_i350:
		pba = E1000_READ_REG(hw, E1000_RXPBS);
		pba = e1000_rxpbs_adjust_82580(pba);
		break;

	case e1000_i210:
	case e1000_i211:
		pba = E1000_PBA_34K;
		break;

	default:
		break;
	}

	/* Special needs in case of Jumbo frames */
	if (hw->mac.type == e1000_82575 && ifp->if_mtu > ETHERMTU) {
		uint32_t tx_space, min_tx, min_rx;

		pba = E1000_READ_REG(hw, E1000_PBA);
		tx_space = pba >> 16;
		pba &= 0xffff;

		min_tx = (sc->max_frame_size +
		    sizeof(struct e1000_tx_desc) - ETHER_CRC_LEN) * 2;
		min_tx = roundup2(min_tx, 1024);
		min_tx >>= 10;
		min_rx = sc->max_frame_size;
		min_rx = roundup2(min_rx, 1024);
		min_rx >>= 10;
		if (tx_space < min_tx && (min_tx - tx_space) < pba) {
			pba = pba - (min_tx - tx_space);
			/*
			 * if short on rx space, rx wins
			 * and must trump tx adjustment
			 */
			if (pba < min_rx)
				pba = min_rx;
		}
		E1000_WRITE_REG(hw, E1000_PBA, pba);
	}

	/*
	 * These parameters control the automatic generation (Tx) and
	 * response (Rx) to Ethernet PAUSE frames.
	 * - High water mark should allow for at least two frames to be
	 *   received after sending an XOFF.
	 * - Low water mark works best when it is very near the high water mark.
	 *   This allows the receiver to restart by sending XON when it has
	 *   drained a bit.
	 */
	hwm = min(((pba << 10) * 9 / 10),
	    ((pba << 10) - 2 * sc->max_frame_size));

	if (hw->mac.type < e1000_82576) {
		fc->high_water = hwm & 0xFFF8; /* 8-byte granularity */
		fc->low_water = fc->high_water - 8;
	} else {
		fc->high_water = hwm & 0xFFF0; /* 16-byte granularity */
		fc->low_water = fc->high_water - 16;
	}
	fc->pause_time = IGB_FC_PAUSE_TIME;
	fc->send_xon = TRUE;
	fc->requested_mode = e1000_ifmedia2fc(sc->ifm_flowctrl);

	/* Issue a global reset */
	e1000_reset_hw(hw);
	E1000_WRITE_REG(hw, E1000_WUC, 0);

	/* Reset for AutoMediaDetect */
	if (media_reset) {
		e1000_setup_init_funcs(hw, TRUE);
		e1000_get_bus_info(hw);
	}

	if (e1000_init_hw(hw) < 0)
		if_printf(ifp, "Hardware Initialization Failed\n");

	/* Setup DMA Coalescing */
	igb_init_dmac(sc, pba);

	E1000_WRITE_REG(&sc->hw, E1000_VET, ETHERTYPE_VLAN);
	e1000_get_phy_info(hw);
	e1000_check_for_link(hw);
}

static void
igb_setup_ifp(struct igb_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = igb_init;
	ifp->if_ioctl = igb_ioctl;
	ifp->if_start = igb_start;
	ifp->if_serialize = igb_serialize;
	ifp->if_deserialize = igb_deserialize;
	ifp->if_tryserialize = igb_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = igb_serialize_assert;
#endif
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = igb_npoll;
#endif

	ifp->if_nmbclusters = sc->rx_ring_cnt * sc->rx_rings[0].num_rx_desc;

	ifq_set_maxlen(&ifp->if_snd, sc->tx_rings[0].num_tx_desc - 1);
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->tx_ring_cnt);

	ifp->if_mapsubq = ifq_mapsubq_modulo;
	ifq_set_subq_divisor(&ifp->if_snd, 1);

	ether_ifattach(ifp, sc->hw.mac.addr, NULL);

	ifp->if_capabilities =
	    IFCAP_HWCSUM | IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU | IFCAP_TSO;
	if (IGB_ENABLE_HWRSS(sc))
		ifp->if_capabilities |= IFCAP_RSS;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_hwassist = IGB_CSUM_FEATURES | CSUM_TSO;

	/*
	 * Tell the upper layer(s) we support long frames
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	/* Setup TX rings and subqueues */
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		ifsq_set_cpuid(ifsq, txr->tx_intr_cpuid);
		ifsq_set_priv(ifsq, txr);
		ifsq_set_hw_serialize(ifsq, &txr->tx_serialize);
		txr->ifsq = ifsq;

		ifsq_watchdog_init(&txr->tx_watchdog, ifsq, igb_watchdog, 0);
	}

	/*
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	if (sc->hw.phy.media_type == e1000_media_type_fiber ||
	    sc->hw.phy.media_type == e1000_media_type_internal_serdes) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_SX | IFM_FDX,
		    0, NULL);
	} else {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T, 0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX, 0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX | IFM_FDX,
		    0, NULL);
		if (sc->hw.phy.type != e1000_phy_ife) {
			ifmedia_add(&sc->media,
			    IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
		}
	}
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO | sc->ifm_flowctrl);
}

static void
igb_add_sysctl(struct igb_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	char node[32];
	int i;

	ctx = device_get_sysctl_ctx(sc->dev);
	tree = device_get_sysctl_tree(sc->dev);
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxr", CTLFLAG_RD, &sc->rx_ring_cnt, 0, "# of RX rings");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxr_inuse", CTLFLAG_RD, &sc->rx_ring_inuse, 0,
	    "# of RX rings used");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txr", CTLFLAG_RD, &sc->tx_ring_cnt, 0, "# of TX rings");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txr_inuse", CTLFLAG_RD, &sc->tx_ring_inuse, 0,
	    "# of TX rings used");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxd", CTLFLAG_RD, &sc->rx_rings[0].num_rx_desc, 0,
	    "# of RX descs");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txd", CTLFLAG_RD, &sc->tx_rings[0].num_tx_desc, 0,
	    "# of TX descs");

#define IGB_ADD_INTR_RATE_SYSCTL(sc, use, name) \
do { \
	igb_add_intr_rate_sysctl(sc, IGB_INTR_USE_##use, #name "_intr_rate", \
	    #use " interrupt rate"); \
} while (0)

	IGB_ADD_INTR_RATE_SYSCTL(sc, RXTX, rxtx);
	IGB_ADD_INTR_RATE_SYSCTL(sc, RX, rx);
	IGB_ADD_INTR_RATE_SYSCTL(sc, TX, tx);
	IGB_ADD_INTR_RATE_SYSCTL(sc, STATUS, sts);

#undef IGB_ADD_INTR_RATE_SYSCTL

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_intr_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_tx_intr_nsegs, "I",
	    "# of segments per TX interrupt");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_tx_wreg_nsegs, "I",
	    "# of segments sent before write to hardware register");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_rx_wreg_nsegs, "I",
	    "# of segments received before write to hardware register");

	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
		    OID_AUTO, "tx_msix_cpumap", CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->tx_rmap_intr, 0, if_ringmap_cpumap_sysctl, "I",
		    "TX MSI-X CPU map");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
		    OID_AUTO, "rx_msix_cpumap", CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->rx_rmap_intr, 0, if_ringmap_cpumap_sysctl, "I",
		    "RX MSI-X CPU map");
	}
#ifdef IFPOLL_ENABLE
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_poll_cpumap", CTLTYPE_OPAQUE | CTLFLAG_RD,
	    sc->tx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
	    "TX polling CPU map");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rx_poll_cpumap", CTLTYPE_OPAQUE | CTLFLAG_RD,
	    sc->rx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
	    "RX polling CPU map");
#endif

#ifdef IGB_RSS_DEBUG
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rss_debug", CTLFLAG_RW, &sc->rss_debug, 0,
	    "RSS debug level");
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "rx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx,
		    SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RW, &sc->rx_rings[i].rx_packets, "RXed packets");
	}
#endif
	for  (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

#ifdef IGB_TSS_DEBUG
		ksnprintf(node, sizeof(node), "tx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RW, &txr->tx_packets, "TXed packets");
#endif
		ksnprintf(node, sizeof(node), "tx%d_nmbuf", i);
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RD, &txr->tx_nmbuf, 0, "# of pending TX mbufs");

		ksnprintf(node, sizeof(node), "tx%d_gc", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RW, &txr->tx_gc, "# of TX desc GC");
	}

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "dumpreg", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_reg_dump, "I", "dump registers");
}

static int
igb_alloc_rings(struct igb_softc *sc)
{
	int error, i;

	/*
	 * Create top level busdma tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
	    &sc->parent_tag);
	if (error) {
		device_printf(sc->dev, "could not create top level DMA tag\n");
		return error;
	}

	/*
	 * Allocate TX descriptor rings and buffers
	 */
	sc->tx_rings = kmalloc(sizeof(struct igb_tx_ring) * sc->tx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		/* Set up some basics */
		txr->sc = sc;
		txr->me = i;
		txr->tx_intr_cpuid = -1;
		lwkt_serialize_init(&txr->tx_serialize);
		callout_init_mp(&txr->tx_gc_timer);

		error = igb_create_tx_ring(txr);
		if (error)
			return error;
	}

	/*
	 * Allocate RX descriptor rings and buffers
	 */ 
	sc->rx_rings = kmalloc(sizeof(struct igb_rx_ring) * sc->rx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];

		/* Set up some basics */
		rxr->sc = sc;
		rxr->me = i;
		lwkt_serialize_init(&rxr->rx_serialize);

		error = igb_create_rx_ring(rxr);
		if (error)
			return error;
	}

	return 0;
}

static void
igb_free_rings(struct igb_softc *sc)
{
	int i;

	if (sc->tx_rings != NULL) {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];

			igb_destroy_tx_ring(txr, txr->num_tx_desc);
		}
		kfree(sc->tx_rings, M_DEVBUF);
	}

	if (sc->rx_rings != NULL) {
		for (i = 0; i < sc->rx_ring_cnt; ++i) {
			struct igb_rx_ring *rxr = &sc->rx_rings[i];

			igb_destroy_rx_ring(rxr, rxr->num_rx_desc);
		}
		kfree(sc->rx_rings, M_DEVBUF);
	}
}

static int
igb_create_tx_ring(struct igb_tx_ring *txr)
{
	int tsize, error, i, ntxd;

	/*
	 * Validate number of transmit descriptors. It must not exceed
	 * hardware maximum, and must be multiple of IGB_DBA_ALIGN.
	 */
	ntxd = device_getenv_int(txr->sc->dev, "txd", igb_txd);
	if ((ntxd * sizeof(struct e1000_tx_desc)) % IGB_DBA_ALIGN != 0 ||
	    ntxd > IGB_MAX_TXD || ntxd < IGB_MIN_TXD) {
		device_printf(txr->sc->dev,
		    "Using %d TX descriptors instead of %d!\n",
		    IGB_DEFAULT_TXD, ntxd);
		txr->num_tx_desc = IGB_DEFAULT_TXD;
	} else {
		txr->num_tx_desc = ntxd;
	}

	/*
	 * Allocate TX descriptor ring
	 */
	tsize = roundup2(txr->num_tx_desc * sizeof(union e1000_adv_tx_desc),
	    IGB_DBA_ALIGN);
	txr->txdma.dma_vaddr = bus_dmamem_coherent_any(txr->sc->parent_tag,
	    IGB_DBA_ALIGN, tsize, BUS_DMA_WAITOK,
	    &txr->txdma.dma_tag, &txr->txdma.dma_map, &txr->txdma.dma_paddr);
	if (txr->txdma.dma_vaddr == NULL) {
		device_printf(txr->sc->dev,
		    "Unable to allocate TX Descriptor memory\n");
		return ENOMEM;
	}
	txr->tx_base = txr->txdma.dma_vaddr;
	bzero(txr->tx_base, tsize);

	tsize = __VM_CACHELINE_ALIGN(
	    sizeof(struct igb_tx_buf) * txr->num_tx_desc);
	txr->tx_buf = kmalloc(tsize, M_DEVBUF,
			      M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Allocate TX head write-back buffer
	 */
	txr->tx_hdr = bus_dmamem_coherent_any(txr->sc->parent_tag,
	    __VM_CACHELINE_SIZE, __VM_CACHELINE_SIZE, BUS_DMA_WAITOK,
	    &txr->tx_hdr_dtag, &txr->tx_hdr_dmap, &txr->tx_hdr_paddr);
	if (txr->tx_hdr == NULL) {
		device_printf(txr->sc->dev,
		    "Unable to allocate TX head write-back buffer\n");
		return ENOMEM;
	}

	/*
	 * Create DMA tag for TX buffers
	 */
	error = bus_dma_tag_create(txr->sc->parent_tag,
	    1, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    IGB_TSO_SIZE,	/* maxsize */
	    IGB_MAX_SCATTER,	/* nsegments */
	    PAGE_SIZE,		/* maxsegsize */
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
	    BUS_DMA_ONEBPAGE,	/* flags */
	    &txr->tx_tag);
	if (error) {
		device_printf(txr->sc->dev, "Unable to allocate TX DMA tag\n");
		kfree(txr->tx_buf, M_DEVBUF);
		txr->tx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for TX buffers
	 */
	for (i = 0; i < txr->num_tx_desc; ++i) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[i];

		error = bus_dmamap_create(txr->tx_tag,
		    BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE, &txbuf->map);
		if (error) {
			device_printf(txr->sc->dev,
			    "Unable to create TX DMA map\n");
			igb_destroy_tx_ring(txr, i);
			return error;
		}
	}

	if (txr->sc->hw.mac.type == e1000_82575)
		txr->tx_flags |= IGB_TXFLAG_TSO_IPLEN0;

	/*
	 * Initialize various watermark
	 */
	if (txr->sc->hw.mac.type == e1000_82575) {
		/*
		 * There no ways to GC pending TX mbufs in 'header
		 * write back' mode with reduced # of RS TX descs,
		 * since TDH does _not_ move for 82575.
		 */
		txr->intr_nsegs = 1;
	} else {
		txr->intr_nsegs = txr->num_tx_desc / 16;
	}
	txr->wreg_nsegs = IGB_DEF_TXWREG_NSEGS;

	return 0;
}

static void
igb_free_tx_ring(struct igb_tx_ring *txr)
{
	int i;

	for (i = 0; i < txr->num_tx_desc; ++i) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[i];

		if (txbuf->m_head != NULL)
			igb_free_txbuf(txr, txbuf);
	}
}

static void
igb_destroy_tx_ring(struct igb_tx_ring *txr, int ndesc)
{
	int i;

	if (txr->txdma.dma_vaddr != NULL) {
		bus_dmamap_unload(txr->txdma.dma_tag, txr->txdma.dma_map);
		bus_dmamem_free(txr->txdma.dma_tag, txr->txdma.dma_vaddr,
		    txr->txdma.dma_map);
		bus_dma_tag_destroy(txr->txdma.dma_tag);
		txr->txdma.dma_vaddr = NULL;
	}

	if (txr->tx_hdr != NULL) {
		bus_dmamap_unload(txr->tx_hdr_dtag, txr->tx_hdr_dmap);
		bus_dmamem_free(txr->tx_hdr_dtag, txr->tx_hdr,
		    txr->tx_hdr_dmap);
		bus_dma_tag_destroy(txr->tx_hdr_dtag);
		txr->tx_hdr = NULL;
	}

	if (txr->tx_buf == NULL)
		return;

	for (i = 0; i < ndesc; ++i) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[i];

		KKASSERT(txbuf->m_head == NULL);
		bus_dmamap_destroy(txr->tx_tag, txbuf->map);
	}
	bus_dma_tag_destroy(txr->tx_tag);

	kfree(txr->tx_buf, M_DEVBUF);
	txr->tx_buf = NULL;
}

static void
igb_init_tx_ring(struct igb_tx_ring *txr)
{
	/* Clear the old descriptor contents */
	bzero(txr->tx_base,
	    sizeof(union e1000_adv_tx_desc) * txr->num_tx_desc);

	/* Clear TX head write-back buffer */
	*(txr->tx_hdr) = 0;

	/* Reset indices */
	txr->next_avail_desc = 0;
	txr->next_to_clean = 0;
	txr->tx_nsegs = 0;
	txr->tx_running = 0;
	txr->tx_nmbuf = 0;

	/* Set number of descriptors available */
	txr->tx_avail = txr->num_tx_desc;

	/* Enable this TX ring */
	txr->tx_flags |= IGB_TXFLAG_ENABLED;
}

static void
igb_init_tx_unit(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	uint32_t tctl;
	int i;

	/* Setup the Tx Descriptor Rings */
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];
		uint64_t bus_addr = txr->txdma.dma_paddr;
		uint64_t hdr_paddr = txr->tx_hdr_paddr;
		uint32_t txdctl = 0;
		uint32_t dca_txctrl;

		E1000_WRITE_REG(hw, E1000_TDLEN(i),
		    txr->num_tx_desc * sizeof(struct e1000_tx_desc));
		E1000_WRITE_REG(hw, E1000_TDBAH(i),
		    (uint32_t)(bus_addr >> 32));
		E1000_WRITE_REG(hw, E1000_TDBAL(i),
		    (uint32_t)bus_addr);

		/* Setup the HW Tx Head and Tail descriptor pointers */
		E1000_WRITE_REG(hw, E1000_TDT(i), 0);
		E1000_WRITE_REG(hw, E1000_TDH(i), 0);

		dca_txctrl = E1000_READ_REG(hw, E1000_DCA_TXCTRL(i));
		dca_txctrl &= ~E1000_DCA_TXCTRL_TX_WB_RO_EN;
		E1000_WRITE_REG(hw, E1000_DCA_TXCTRL(i), dca_txctrl);

		/*
		 * Don't set WB_on_EITR:
		 * - 82575 does not have it
		 * - It almost has no effect on 82576, see:
		 *   82576 specification update errata #26
		 * - It causes unnecessary bus traffic
		 */
		E1000_WRITE_REG(hw, E1000_TDWBAH(i),
		    (uint32_t)(hdr_paddr >> 32));
		E1000_WRITE_REG(hw, E1000_TDWBAL(i),
		    ((uint32_t)hdr_paddr) | E1000_TX_HEAD_WB_ENABLE);

		/*
		 * WTHRESH is ignored by the hardware, since header
		 * write back mode is used.
		 */
		txdctl |= IGB_TX_PTHRESH;
		txdctl |= IGB_TX_HTHRESH << 8;
		txdctl |= IGB_TX_WTHRESH << 16;
		txdctl |= E1000_TXDCTL_QUEUE_ENABLE;
		E1000_WRITE_REG(hw, E1000_TXDCTL(i), txdctl);
	}

	if (sc->vf_ifp)
		return;

	e1000_config_collision_dist(hw);

	/* Program the Transmit Control Register */
	tctl = E1000_READ_REG(hw, E1000_TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= (E1000_TCTL_PSP | E1000_TCTL_RTLC | E1000_TCTL_EN |
	    (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT));

	/* This write will effectively turn on the transmit unit. */
	E1000_WRITE_REG(hw, E1000_TCTL, tctl);
}

static boolean_t
igb_txcsum_ctx(struct igb_tx_ring *txr, struct mbuf *mp)
{
	struct e1000_adv_tx_context_desc *TXD;
	uint32_t vlan_macip_lens, type_tucmd_mlhl, mss_l4len_idx;
	int ehdrlen, ctxd, ip_hlen = 0;
	boolean_t offload = TRUE;

	if ((mp->m_pkthdr.csum_flags & IGB_CSUM_FEATURES) == 0)
		offload = FALSE;

	vlan_macip_lens = type_tucmd_mlhl = mss_l4len_idx = 0;

	ctxd = txr->next_avail_desc;
	TXD = (struct e1000_adv_tx_context_desc *)&txr->tx_base[ctxd];

	/*
	 * In advanced descriptors the vlan tag must 
	 * be placed into the context descriptor, thus
	 * we need to be here just for that setup.
	 */
	if (mp->m_flags & M_VLANTAG) {
		uint16_t vlantag;

		vlantag = htole16(mp->m_pkthdr.ether_vlantag);
		vlan_macip_lens |= (vlantag << E1000_ADVTXD_VLAN_SHIFT);
	} else if (!offload) {
		return FALSE;
	}

	ehdrlen = mp->m_pkthdr.csum_lhlen;
	KASSERT(ehdrlen > 0, ("invalid ether hlen"));

	/* Set the ether header length */
	vlan_macip_lens |= ehdrlen << E1000_ADVTXD_MACLEN_SHIFT;
	if (mp->m_pkthdr.csum_flags & CSUM_IP) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV4;
		ip_hlen = mp->m_pkthdr.csum_iphlen;
		KASSERT(ip_hlen > 0, ("invalid ip hlen"));
	}
	vlan_macip_lens |= ip_hlen;

	type_tucmd_mlhl |= E1000_ADVTXD_DCMD_DEXT | E1000_ADVTXD_DTYP_CTXT;
	if (mp->m_pkthdr.csum_flags & CSUM_TCP)
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_TCP;
	else if (mp->m_pkthdr.csum_flags & CSUM_UDP)
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_UDP;

	/*
	 * 82575 needs the TX context index added; the queue
	 * index is used as TX context index here.
	 */
	if (txr->sc->hw.mac.type == e1000_82575)
		mss_l4len_idx = txr->me << 4;

	/* Now copy bits into descriptor */
	TXD->vlan_macip_lens = htole32(vlan_macip_lens);
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	TXD->seqnum_seed = htole32(0);
	TXD->mss_l4len_idx = htole32(mss_l4len_idx);

	/* We've consumed the first desc, adjust counters */
	if (++ctxd == txr->num_tx_desc)
		ctxd = 0;
	txr->next_avail_desc = ctxd;
	--txr->tx_avail;

	return offload;
}

static void
igb_txeof(struct igb_tx_ring *txr, int hdr)
{
	int first, avail;

	if (txr->tx_avail == txr->num_tx_desc)
		return;

	first = txr->next_to_clean;
	if (first == hdr)
		return;

	avail = txr->tx_avail;
	while (first != hdr) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[first];

		KKASSERT(avail < txr->num_tx_desc);
		++avail;

		if (txbuf->m_head)
			igb_free_txbuf(txr, txbuf);

		if (++first == txr->num_tx_desc)
			first = 0;
	}
	txr->next_to_clean = first;
	txr->tx_avail = avail;

	/*
	 * If we have a minimum free, clear OACTIVE
	 * to tell the stack that it is OK to send packets.
	 */
	if (txr->tx_avail > IGB_MAX_SCATTER + IGB_TX_RESERVED) {
		ifsq_clr_oactive(txr->ifsq);

		/*
		 * We have enough TX descriptors, turn off
		 * the watchdog.  We allow small amount of
		 * packets (roughly intr_nsegs) pending on
		 * the transmit ring.
		 */
		ifsq_watchdog_set_count(&txr->tx_watchdog, 0);
	}
	txr->tx_running = IGB_TX_RUNNING;
}

static void
igb_txgc(struct igb_tx_ring *txr)
{
	int first, hdr;
#ifdef INVARIANTS
	int avail;
#endif

	if (txr->tx_avail == txr->num_tx_desc)
		return;

	hdr = E1000_READ_REG(&txr->sc->hw, E1000_TDH(txr->me)),
	first = txr->next_to_clean;
	if (first == hdr)
		goto done;
	txr->tx_gc++;

#ifdef INVARIANTS
	avail = txr->tx_avail;
#endif
	while (first != hdr) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[first];

#ifdef INVARIANTS
		KKASSERT(avail < txr->num_tx_desc);
		++avail;
#endif
		if (txbuf->m_head)
			igb_free_txbuf(txr, txbuf);

		if (++first == txr->num_tx_desc)
			first = 0;
	}
done:
	if (txr->tx_nmbuf)
		txr->tx_running = IGB_TX_RUNNING;
}

static int
igb_create_rx_ring(struct igb_rx_ring *rxr)
{
	int rsize, i, error, nrxd;

	/*
	 * Validate number of receive descriptors. It must not exceed
	 * hardware maximum, and must be multiple of IGB_DBA_ALIGN.
	 */
	nrxd = device_getenv_int(rxr->sc->dev, "rxd", igb_rxd);
	if ((nrxd * sizeof(struct e1000_rx_desc)) % IGB_DBA_ALIGN != 0 ||
	    nrxd > IGB_MAX_RXD || nrxd < IGB_MIN_RXD) {
		device_printf(rxr->sc->dev,
		    "Using %d RX descriptors instead of %d!\n",
		    IGB_DEFAULT_RXD, nrxd);
		rxr->num_rx_desc = IGB_DEFAULT_RXD;
	} else {
		rxr->num_rx_desc = nrxd;
	}

	/*
	 * Allocate RX descriptor ring
	 */
	rsize = roundup2(rxr->num_rx_desc * sizeof(union e1000_adv_rx_desc),
	    IGB_DBA_ALIGN);
	rxr->rxdma.dma_vaddr = bus_dmamem_coherent_any(rxr->sc->parent_tag,
	    IGB_DBA_ALIGN, rsize, BUS_DMA_WAITOK,
	    &rxr->rxdma.dma_tag, &rxr->rxdma.dma_map,
	    &rxr->rxdma.dma_paddr);
	if (rxr->rxdma.dma_vaddr == NULL) {
		device_printf(rxr->sc->dev,
		    "Unable to allocate RxDescriptor memory\n");
		return ENOMEM;
	}
	rxr->rx_base = rxr->rxdma.dma_vaddr;
	bzero(rxr->rx_base, rsize);

	rsize = __VM_CACHELINE_ALIGN(
	    sizeof(struct igb_rx_buf) * rxr->num_rx_desc);
	rxr->rx_buf = kmalloc(rsize, M_DEVBUF,
			      M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create DMA tag for RX buffers
	 */
	error = bus_dma_tag_create(rxr->sc->parent_tag,
	    1, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    MCLBYTES,		/* maxsize */
	    1,			/* nsegments */
	    MCLBYTES,		/* maxsegsize */
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, /* flags */
	    &rxr->rx_tag);
	if (error) {
		device_printf(rxr->sc->dev,
		    "Unable to create RX payload DMA tag\n");
		kfree(rxr->rx_buf, M_DEVBUF);
		rxr->rx_buf = NULL;
		return error;
	}

	/*
	 * Create spare DMA map for RX buffers
	 */
	error = bus_dmamap_create(rxr->rx_tag, BUS_DMA_WAITOK,
	    &rxr->rx_sparemap);
	if (error) {
		device_printf(rxr->sc->dev,
		    "Unable to create spare RX DMA maps\n");
		bus_dma_tag_destroy(rxr->rx_tag);
		kfree(rxr->rx_buf, M_DEVBUF);
		rxr->rx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for RX buffers
	 */
	for (i = 0; i < rxr->num_rx_desc; i++) {
		struct igb_rx_buf *rxbuf = &rxr->rx_buf[i];

		error = bus_dmamap_create(rxr->rx_tag,
		    BUS_DMA_WAITOK, &rxbuf->map);
		if (error) {
			device_printf(rxr->sc->dev,
			    "Unable to create RX DMA maps\n");
			igb_destroy_rx_ring(rxr, i);
			return error;
		}
	}

	/*
	 * Initialize various watermark
	 */
	rxr->wreg_nsegs = IGB_DEF_RXWREG_NSEGS;

	return 0;
}

static void
igb_free_rx_ring(struct igb_rx_ring *rxr)
{
	int i;

	for (i = 0; i < rxr->num_rx_desc; ++i) {
		struct igb_rx_buf *rxbuf = &rxr->rx_buf[i];

		if (rxbuf->m_head != NULL) {
			bus_dmamap_unload(rxr->rx_tag, rxbuf->map);
			m_freem(rxbuf->m_head);
			rxbuf->m_head = NULL;
		}
	}

	if (rxr->fmp != NULL)
		m_freem(rxr->fmp);
	rxr->fmp = NULL;
	rxr->lmp = NULL;
}

static void
igb_destroy_rx_ring(struct igb_rx_ring *rxr, int ndesc)
{
	int i;

	if (rxr->rxdma.dma_vaddr != NULL) {
		bus_dmamap_unload(rxr->rxdma.dma_tag, rxr->rxdma.dma_map);
		bus_dmamem_free(rxr->rxdma.dma_tag, rxr->rxdma.dma_vaddr,
		    rxr->rxdma.dma_map);
		bus_dma_tag_destroy(rxr->rxdma.dma_tag);
		rxr->rxdma.dma_vaddr = NULL;
	}

	if (rxr->rx_buf == NULL)
		return;

	for (i = 0; i < ndesc; ++i) {
		struct igb_rx_buf *rxbuf = &rxr->rx_buf[i];

		KKASSERT(rxbuf->m_head == NULL);
		bus_dmamap_destroy(rxr->rx_tag, rxbuf->map);
	}
	bus_dmamap_destroy(rxr->rx_tag, rxr->rx_sparemap);
	bus_dma_tag_destroy(rxr->rx_tag);

	kfree(rxr->rx_buf, M_DEVBUF);
	rxr->rx_buf = NULL;
}

static void
igb_setup_rxdesc(union e1000_adv_rx_desc *rxd, const struct igb_rx_buf *rxbuf)
{
	rxd->read.pkt_addr = htole64(rxbuf->paddr);
	rxd->wb.upper.status_error = 0;
}

static int
igb_newbuf(struct igb_rx_ring *rxr, int i, boolean_t wait)
{
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct igb_rx_buf *rxbuf;
	int error, nseg;

	m = m_getcl(wait ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		if (wait) {
			if_printf(&rxr->sc->arpcom.ac_if,
			    "Unable to allocate RX mbuf\n");
		}
		return ENOBUFS;
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (rxr->sc->max_frame_size <= MCLBYTES - ETHER_ALIGN)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(rxr->rx_tag,
	    rxr->rx_sparemap, m, &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (wait) {
			if_printf(&rxr->sc->arpcom.ac_if,
			    "Unable to load RX mbuf\n");
		}
		return error;
	}

	rxbuf = &rxr->rx_buf[i];
	if (rxbuf->m_head != NULL)
		bus_dmamap_unload(rxr->rx_tag, rxbuf->map);

	map = rxbuf->map;
	rxbuf->map = rxr->rx_sparemap;
	rxr->rx_sparemap = map;

	rxbuf->m_head = m;
	rxbuf->paddr = seg.ds_addr;

	igb_setup_rxdesc(&rxr->rx_base[i], rxbuf);
	return 0;
}

static int
igb_init_rx_ring(struct igb_rx_ring *rxr)
{
	int i;

	/* Clear the ring contents */
	bzero(rxr->rx_base,
	    rxr->num_rx_desc * sizeof(union e1000_adv_rx_desc));

	/* Now replenish the ring mbufs */
	for (i = 0; i < rxr->num_rx_desc; ++i) {
		int error;

		error = igb_newbuf(rxr, i, TRUE);
		if (error)
			return error;
	}

	/* Setup our descriptor indices */
	rxr->next_to_check = 0;

	rxr->fmp = NULL;
	rxr->lmp = NULL;
	rxr->discard = FALSE;

	return 0;
}

static void
igb_init_rx_unit(struct igb_softc *sc, boolean_t polling)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct e1000_hw *hw = &sc->hw;
	uint32_t rctl, rxcsum, srrctl = 0;
	int i;

	/*
	 * Make sure receives are disabled while setting
	 * up the descriptor ring
	 */
	rctl = E1000_READ_REG(hw, E1000_RCTL);
	E1000_WRITE_REG(hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);

#if 0
	/*
	** Set up for header split
	*/
	if (igb_header_split) {
		/* Use a standard mbuf for the header */
		srrctl |= IGB_HDR_BUF << E1000_SRRCTL_BSIZEHDRSIZE_SHIFT;
		srrctl |= E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
	} else
#endif
		srrctl |= E1000_SRRCTL_DESCTYPE_ADV_ONEBUF;

	/*
	** Set up for jumbo frames
	*/
	if (ifp->if_mtu > ETHERMTU) {
		rctl |= E1000_RCTL_LPE;
#if 0
		if (adapter->rx_mbuf_sz == MJUMPAGESIZE) {
			srrctl |= 4096 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
			rctl |= E1000_RCTL_SZ_4096 | E1000_RCTL_BSEX;
		} else if (adapter->rx_mbuf_sz > MJUMPAGESIZE) {
			srrctl |= 8192 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
			rctl |= E1000_RCTL_SZ_8192 | E1000_RCTL_BSEX;
		}
		/* Set maximum packet len */
		psize = adapter->max_frame_size;
		/* are we on a vlan? */
		if (adapter->ifp->if_vlantrunk != NULL)
			psize += VLAN_TAG_SIZE;
		E1000_WRITE_REG(&adapter->hw, E1000_RLPML, psize);
#else
		srrctl |= 2048 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
		rctl |= E1000_RCTL_SZ_2048;
#endif
	} else {
		rctl &= ~E1000_RCTL_LPE;
		srrctl |= 2048 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
		rctl |= E1000_RCTL_SZ_2048;
	}

	/*
	 * If TX flow control is disabled and more the 1 RX rings
	 * are enabled, enable DROP.
	 *
	 * This drops frames rather than hanging the RX MAC for all
	 * RX rings.
	 */
	if (sc->rx_ring_inuse > 1 &&
	    (sc->ifm_flowctrl & IFM_ETH_TXPAUSE) == 0) {
		srrctl |= E1000_SRRCTL_DROP_EN;
		if (bootverbose)
			if_printf(ifp, "enable RX drop\n");
	}

	/* Setup the Base and Length of the Rx Descriptor Rings */
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];
		uint64_t bus_addr = rxr->rxdma.dma_paddr;
		uint32_t rxdctl;

		E1000_WRITE_REG(hw, E1000_RDLEN(i),
		    rxr->num_rx_desc * sizeof(struct e1000_rx_desc));
		E1000_WRITE_REG(hw, E1000_RDBAH(i),
		    (uint32_t)(bus_addr >> 32));
		E1000_WRITE_REG(hw, E1000_RDBAL(i),
		    (uint32_t)bus_addr);
		E1000_WRITE_REG(hw, E1000_SRRCTL(i), srrctl);
		/* Enable this Queue */
		rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(i));
		rxdctl |= E1000_RXDCTL_QUEUE_ENABLE;
		rxdctl &= 0xFFF00000;
		rxdctl |= IGB_RX_PTHRESH;
		rxdctl |= IGB_RX_HTHRESH << 8;
		/*
		 * Don't set WTHRESH to a value above 1 on 82576, see:
		 * 82576 specification update errata #26
		 */
		rxdctl |= IGB_RX_WTHRESH << 16;
		E1000_WRITE_REG(hw, E1000_RXDCTL(i), rxdctl);
	}

	rxcsum = E1000_READ_REG(&sc->hw, E1000_RXCSUM);
	rxcsum &= ~(E1000_RXCSUM_PCSS_MASK | E1000_RXCSUM_IPPCSE);

	/*
	 * Receive Checksum Offload for TCP and UDP
	 *
	 * Checksum offloading is also enabled if multiple receive
	 * queue is to be supported, since we need it to figure out
	 * fragments.
	 */
	if ((ifp->if_capenable & IFCAP_RXCSUM) || IGB_ENABLE_HWRSS(sc)) {
		/*
		 * NOTE:
		 * PCSD must be enabled to enable multiple
		 * receive queues.
		 */
		rxcsum |= E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL |
		    E1000_RXCSUM_PCSD;
	} else {
		rxcsum &= ~(E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL |
		    E1000_RXCSUM_PCSD);
	}
	E1000_WRITE_REG(&sc->hw, E1000_RXCSUM, rxcsum);

	if (sc->rx_ring_inuse > 1) {
		uint8_t key[IGB_NRSSRK * IGB_RSSRK_SIZE];
		const struct if_ringmap *rm;
		uint32_t reta_shift;
		int j, r;

		/*
		 * NOTE:
		 * When we reach here, RSS has already been disabled
		 * in igb_stop(), so we could safely configure RSS key
		 * and redirect table.
		 */

		/*
		 * Configure RSS key
		 */
		toeplitz_get_key(key, sizeof(key));
		for (i = 0; i < IGB_NRSSRK; ++i) {
			uint32_t rssrk;

			rssrk = IGB_RSSRK_VAL(key, i);
			IGB_RSS_DPRINTF(sc, 1, "rssrk%d 0x%08x\n", i, rssrk);

			E1000_WRITE_REG(hw, E1000_RSSRK(i), rssrk);
		}

		/*
		 * Configure RSS redirect table
		 */
		if (polling)
			rm = sc->rx_rmap;
		else
			rm = sc->rx_rmap_intr;
		if_ringmap_rdrtable(rm, sc->rdr_table, IGB_RDRTABLE_SIZE);

		reta_shift = IGB_RETA_SHIFT;
		if (hw->mac.type == e1000_82575)
			reta_shift = IGB_RETA_SHIFT_82575;

		r = 0;
		for (j = 0; j < IGB_NRETA; ++j) {
			uint32_t reta = 0;

			for (i = 0; i < IGB_RETA_SIZE; ++i) {
				uint32_t q;

				q = sc->rdr_table[r] << reta_shift;
				reta |= q << (8 * i);
				++r;
			}
			IGB_RSS_DPRINTF(sc, 1, "reta 0x%08x\n", reta);
			E1000_WRITE_REG(hw, E1000_RETA(j), reta);
		}

		/*
		 * Enable multiple receive queues.
		 * Enable IPv4 RSS standard hash functions.
		 * Disable RSS interrupt on 82575
		 */
		E1000_WRITE_REG(&sc->hw, E1000_MRQC,
				E1000_MRQC_ENABLE_RSS_4Q |
				E1000_MRQC_RSS_FIELD_IPV4_TCP |
				E1000_MRQC_RSS_FIELD_IPV4);
	}

	/* Setup the Receive Control Register */
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO |
	    E1000_RCTL_RDMTS_HALF |
	    (hw->mac.mc_filter_type << E1000_RCTL_MO_SHIFT);
	/* Strip CRC bytes. */
	rctl |= E1000_RCTL_SECRC;
	/* Make sure VLAN Filters are off */
	rctl &= ~E1000_RCTL_VFE;
	/* Don't store bad packets */
	rctl &= ~E1000_RCTL_SBP;

	/* Enable Receives */
	E1000_WRITE_REG(hw, E1000_RCTL, rctl);

	/*
	 * Setup the HW Rx Head and Tail Descriptor Pointers
	 *   - needs to be after enable
	 */
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];

		E1000_WRITE_REG(hw, E1000_RDH(i), rxr->next_to_check);
		E1000_WRITE_REG(hw, E1000_RDT(i), rxr->num_rx_desc - 1);
	}
}

static void
igb_rx_refresh(struct igb_rx_ring *rxr, int i)
{
	if (--i < 0)
		i = rxr->num_rx_desc - 1;
	E1000_WRITE_REG(&rxr->sc->hw, E1000_RDT(rxr->me), i);
}

static void
igb_rxeof(struct igb_rx_ring *rxr, int count)
{
	struct ifnet *ifp = &rxr->sc->arpcom.ac_if;
	union e1000_adv_rx_desc	*cur;
	uint32_t staterr;
	int i, ncoll = 0, cpuid = mycpuid;

	i = rxr->next_to_check;
	cur = &rxr->rx_base[i];
	staterr = le32toh(cur->wb.upper.status_error);

	if ((staterr & E1000_RXD_STAT_DD) == 0)
		return;

	while ((staterr & E1000_RXD_STAT_DD) && count != 0) {
		struct pktinfo *pi = NULL, pi0;
		struct igb_rx_buf *rxbuf = &rxr->rx_buf[i];
		struct mbuf *m = NULL;
		boolean_t eop;

		eop = (staterr & E1000_RXD_STAT_EOP) ? TRUE : FALSE;
		if (eop)
			--count;

		++ncoll;
		if ((staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) == 0 &&
		    !rxr->discard) {
			struct mbuf *mp = rxbuf->m_head;
			uint32_t hash, hashtype;
			uint16_t vlan;
			int len;

			len = le16toh(cur->wb.upper.length);
			if ((rxr->sc->hw.mac.type == e1000_i350 ||
			     rxr->sc->hw.mac.type == e1000_i354) &&
			    (staterr & E1000_RXDEXT_STATERR_LB))
				vlan = be16toh(cur->wb.upper.vlan);
			else
				vlan = le16toh(cur->wb.upper.vlan);

			hash = le32toh(cur->wb.lower.hi_dword.rss);
			hashtype = le32toh(cur->wb.lower.lo_dword.data) &
			    E1000_RXDADV_RSSTYPE_MASK;

			IGB_RSS_DPRINTF(rxr->sc, 10,
			    "ring%d, hash 0x%08x, hashtype %u\n",
			    rxr->me, hash, hashtype);

			bus_dmamap_sync(rxr->rx_tag, rxbuf->map,
			    BUS_DMASYNC_POSTREAD);

			if (igb_newbuf(rxr, i, FALSE) != 0) {
				IFNET_STAT_INC(ifp, iqdrops, 1);
				goto discard;
			}

			mp->m_len = len;
			if (rxr->fmp == NULL) {
				mp->m_pkthdr.len = len;
				rxr->fmp = mp;
				rxr->lmp = mp;
			} else {
				rxr->lmp->m_next = mp;
				rxr->lmp = rxr->lmp->m_next;
				rxr->fmp->m_pkthdr.len += len;
			}

			if (eop) {
				m = rxr->fmp;
				rxr->fmp = NULL;
				rxr->lmp = NULL;

				m->m_pkthdr.rcvif = ifp;
				IFNET_STAT_INC(ifp, ipackets, 1);

				if (ifp->if_capenable & IFCAP_RXCSUM)
					igb_rxcsum(staterr, m);

				if (staterr & E1000_RXD_STAT_VP) {
					m->m_pkthdr.ether_vlantag = vlan;
					m->m_flags |= M_VLANTAG;
				}

				if (ifp->if_capenable & IFCAP_RSS) {
					pi = igb_rssinfo(m, &pi0,
					    hash, hashtype, staterr);
				}
#ifdef IGB_RSS_DEBUG
				rxr->rx_packets++;
#endif
			}
		} else {
			IFNET_STAT_INC(ifp, ierrors, 1);
discard:
			igb_setup_rxdesc(cur, rxbuf);
			if (!eop)
				rxr->discard = TRUE;
			else
				rxr->discard = FALSE;
			if (rxr->fmp != NULL) {
				m_freem(rxr->fmp);
				rxr->fmp = NULL;
				rxr->lmp = NULL;
			}
			m = NULL;
		}

		if (m != NULL)
			ifp->if_input(ifp, m, pi, cpuid);

		/* Advance our pointers to the next descriptor. */
		if (++i == rxr->num_rx_desc)
			i = 0;

		if (ncoll >= rxr->wreg_nsegs) {
			igb_rx_refresh(rxr, i);
			ncoll = 0;
		}

		cur = &rxr->rx_base[i];
		staterr = le32toh(cur->wb.upper.status_error);
	}
	rxr->next_to_check = i;

	if (ncoll > 0)
		igb_rx_refresh(rxr, i);
}


static void
igb_set_vlan(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	uint32_t reg;
#if 0
	struct ifnet *ifp = sc->arpcom.ac_if;
#endif

	if (sc->vf_ifp) {
		e1000_rlpml_set_vf(hw, sc->max_frame_size + VLAN_TAG_SIZE);
		return;
	}

	reg = E1000_READ_REG(hw, E1000_CTRL);
	reg |= E1000_CTRL_VME;
	E1000_WRITE_REG(hw, E1000_CTRL, reg);

#if 0
	/* Enable the Filter Table */
	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER) {
		reg = E1000_READ_REG(hw, E1000_RCTL);
		reg &= ~E1000_RCTL_CFIEN;
		reg |= E1000_RCTL_VFE;
		E1000_WRITE_REG(hw, E1000_RCTL, reg);
	}
#endif

	/* Update the frame size */
	E1000_WRITE_REG(&sc->hw, E1000_RLPML,
	    sc->max_frame_size + VLAN_TAG_SIZE);

#if 0
	/* Don't bother with table if no vlans */
	if ((adapter->num_vlans == 0) ||
	    ((ifp->if_capenable & IFCAP_VLAN_HWFILTER) == 0))
		return;
	/*
	** A soft reset zero's out the VFTA, so
	** we need to repopulate it now.
	*/
	for (int i = 0; i < IGB_VFTA_SIZE; i++)
		if (adapter->shadow_vfta[i] != 0) {
			if (adapter->vf_ifp)
				e1000_vfta_set_vf(hw,
				    adapter->shadow_vfta[i], TRUE);
			else
				E1000_WRITE_REG_ARRAY(hw, E1000_VFTA,
				 i, adapter->shadow_vfta[i]);
		}
#endif
}

static void
igb_enable_intr(struct igb_softc *sc)
{
	int i;

	for (i = 0; i < sc->intr_cnt; ++i)
		lwkt_serialize_handler_enable(sc->intr_data[i].intr_serialize);

	if ((sc->flags & IGB_FLAG_SHARED_INTR) == 0) {
		if (sc->intr_type == PCI_INTR_TYPE_MSIX)
			E1000_WRITE_REG(&sc->hw, E1000_EIAC, sc->intr_mask);
		else
			E1000_WRITE_REG(&sc->hw, E1000_EIAC, 0);
		E1000_WRITE_REG(&sc->hw, E1000_EIAM, sc->intr_mask);
		E1000_WRITE_REG(&sc->hw, E1000_EIMS, sc->intr_mask);
		E1000_WRITE_REG(&sc->hw, E1000_IMS, E1000_IMS_LSC);
	} else {
		E1000_WRITE_REG(&sc->hw, E1000_IMS, IMS_ENABLE_MASK);
	}
	E1000_WRITE_FLUSH(&sc->hw);
}

static void
igb_disable_intr(struct igb_softc *sc)
{
	int i;

	if ((sc->flags & IGB_FLAG_SHARED_INTR) == 0) {
		E1000_WRITE_REG(&sc->hw, E1000_EIMC, 0xffffffff);
		E1000_WRITE_REG(&sc->hw, E1000_EIAC, 0);
	}
	E1000_WRITE_REG(&sc->hw, E1000_IMC, 0xffffffff);
	E1000_WRITE_FLUSH(&sc->hw);

	for (i = 0; i < sc->intr_cnt; ++i)
		lwkt_serialize_handler_disable(sc->intr_data[i].intr_serialize);
}

/*
 * Bit of a misnomer, what this really means is
 * to enable OS management of the system... aka
 * to disable special hardware management features 
 */
static void
igb_get_mgmt(struct igb_softc *sc)
{
	if (sc->flags & IGB_FLAG_HAS_MGMT) {
		int manc2h = E1000_READ_REG(&sc->hw, E1000_MANC2H);
		int manc = E1000_READ_REG(&sc->hw, E1000_MANC);

		/* disable hardware interception of ARP */
		manc &= ~E1000_MANC_ARP_EN;

		/* enable receiving management packets to the host */
		manc |= E1000_MANC_EN_MNG2HOST;
		manc2h |= 1 << 5; /* Mng Port 623 */
		manc2h |= 1 << 6; /* Mng Port 664 */
		E1000_WRITE_REG(&sc->hw, E1000_MANC2H, manc2h);
		E1000_WRITE_REG(&sc->hw, E1000_MANC, manc);
	}
}

/*
 * Give control back to hardware management controller
 * if there is one.
 */
static void
igb_rel_mgmt(struct igb_softc *sc)
{
	if (sc->flags & IGB_FLAG_HAS_MGMT) {
		int manc = E1000_READ_REG(&sc->hw, E1000_MANC);

		/* Re-enable hardware interception of ARP */
		manc |= E1000_MANC_ARP_EN;
		manc &= ~E1000_MANC_EN_MNG2HOST;

		E1000_WRITE_REG(&sc->hw, E1000_MANC, manc);
	}
}

/*
 * Sets CTRL_EXT:DRV_LOAD bit.
 *
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded. 
 */
static void
igb_get_hw_control(struct igb_softc *sc)
{
	uint32_t ctrl_ext;

	if (sc->vf_ifp)
		return;

	/* Let firmware know the driver has taken over */
	ctrl_ext = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
	E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT,
	    ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
}

/*
 * Resets CTRL_EXT:DRV_LOAD bit.
 *
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.
 */
static void
igb_rel_hw_control(struct igb_softc *sc)
{
	uint32_t ctrl_ext;

	if (sc->vf_ifp)
		return;

	/* Let firmware taken over control of h/w */
	ctrl_ext = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
	E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT,
	    ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
}

static boolean_t
igb_is_valid_ether_addr(const uint8_t *addr)
{
	uint8_t zero_addr[ETHER_ADDR_LEN] = { 0, 0, 0, 0, 0, 0 };

	if ((addr[0] & 1) || !bcmp(addr, zero_addr, ETHER_ADDR_LEN))
		return FALSE;
	return TRUE;
}

/*
 * Enable PCI Wake On Lan capability
 */
static void
igb_enable_wol(struct igb_softc *sc)
{
	device_t dev = sc->dev;
	int error = 0;
	uint32_t pmc, ctrl;
	uint16_t status;

	if (pci_find_extcap(dev, PCIY_PMG, &pmc) != 0) {
		device_printf(dev, "no PMG\n");
		return;
	}

	/*
	 * Set the type of wakeup.
	 */
	sc->wol &= ~(E1000_WUFC_EX | E1000_WUFC_MC);
	if ((sc->wol & (E1000_WUFC_EX | E1000_WUFC_MAG | E1000_WUFC_MC)) == 0)
		goto pme;

	/*
	 * Advertise the wakeup capabilities.
	 */
	ctrl = E1000_READ_REG(&sc->hw, E1000_CTRL);
	ctrl |= (E1000_CTRL_SWDPIN2 | E1000_CTRL_SWDPIN3);
	E1000_WRITE_REG(&sc->hw, E1000_CTRL, ctrl);

	/*
	 * Keep the laser running on Fiber adapters.
	 */
	if (sc->hw.phy.media_type == e1000_media_type_fiber ||
	    sc->hw.phy.media_type == e1000_media_type_internal_serdes) {
		uint32_t ctrl_ext;

		ctrl_ext = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_SDP3_DATA;
		E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT, ctrl_ext);
	}

	error = igb_enable_phy_wol(sc);
	if (error)
		goto pme;

	/* XXX will this happen? ich/pch specific. */
	if (sc->hw.phy.type == e1000_phy_igp_3)
		e1000_igp3_phy_powerdown_workaround_ich8lan(&sc->hw);

pme:
	status = pci_read_config(dev, pmc + PCIR_POWER_STATUS, 2);
	status &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if (!error)
		status |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(dev, pmc + PCIR_POWER_STATUS, status, 2);
}

/*
 * WOL in the newer chipset interfaces (pchlan)
 * require thing to be copied into the phy
 */
static int
igb_enable_phy_wol(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	uint32_t mreg;
	uint16_t preg;
	int ret = 0, i;

	/* Copy MAC RARs to PHY RARs */
	e1000_copy_rx_addrs_to_phy_ich8lan(hw);

	/* Copy MAC MTA to PHY MTA */
	for (i = 0; i < hw->mac.mta_reg_count; i++) {
		mreg = E1000_READ_REG_ARRAY(hw, E1000_MTA, i);
		e1000_write_phy_reg(hw, BM_MTA(i), (uint16_t)(mreg & 0xFFFF));
		e1000_write_phy_reg(hw, BM_MTA(i) + 1,
		    (uint16_t)((mreg >> 16) & 0xFFFF));
	}

	/* Configure PHY Rx Control register */
	e1000_read_phy_reg(hw, BM_RCTL, &preg);
	mreg = E1000_READ_REG(hw, E1000_RCTL);
	if (mreg & E1000_RCTL_UPE)
		preg |= BM_RCTL_UPE;
	if (mreg & E1000_RCTL_MPE)
		preg |= BM_RCTL_MPE;
	preg &= ~(BM_RCTL_MO_MASK);
	if (mreg & E1000_RCTL_MO_3) {
		preg |= (((mreg & E1000_RCTL_MO_3) >> E1000_RCTL_MO_SHIFT)
				<< BM_RCTL_MO_SHIFT);
	}
	if (mreg & E1000_RCTL_BAM)
		preg |= BM_RCTL_BAM;
	if (mreg & E1000_RCTL_PMCF)
		preg |= BM_RCTL_PMCF;
	mreg = E1000_READ_REG(hw, E1000_CTRL);
	if (mreg & E1000_CTRL_RFCE)
		preg |= BM_RCTL_RFCE;
	e1000_write_phy_reg(&sc->hw, BM_RCTL, preg);

	/* Enable PHY wakeup in MAC register. */
	E1000_WRITE_REG(hw, E1000_WUC,
	    E1000_WUC_PHY_WAKE | E1000_WUC_PME_EN | E1000_WUC_APME);
	E1000_WRITE_REG(hw, E1000_WUFC, sc->wol);

	/* Configure and enable PHY wakeup in PHY registers */
	e1000_write_phy_reg(hw, BM_WUFC, sc->wol);
	e1000_write_phy_reg(hw, BM_WUC, E1000_WUC_PME_EN);
	/* Activate PHY wakeup */
	ret = hw->phy.ops.acquire(hw);
	if (ret) {
		if_printf(&sc->arpcom.ac_if, "Could not acquire PHY\n");
		return ret;
	}
	e1000_write_phy_reg_mdic(hw, IGP01E1000_PHY_PAGE_SELECT,
	                         (BM_WUC_ENABLE_PAGE << IGP_PAGE_SHIFT));
	ret = e1000_read_phy_reg_mdic(hw, BM_WUC_ENABLE_REG, &preg);
	if (ret) {
		if_printf(&sc->arpcom.ac_if, "Could not read PHY page 769\n");
		goto out;
	}
	preg |= BM_WUC_ENABLE_BIT | BM_WUC_HOST_WU_BIT;
	ret = e1000_write_phy_reg_mdic(hw, BM_WUC_ENABLE_REG, preg);
	if (ret) {
		if_printf(&sc->arpcom.ac_if,
		    "Could not set PHY Host Wakeup bit\n");
	}
out:
	hw->phy.ops.release(hw);
	return ret;
}

static void
igb_update_stats_counters(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	struct e1000_hw_stats *stats;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* 
	 * The virtual function adapter has only a
	 * small controlled set of stats, do only 
	 * those and return.
	 */
	if (sc->vf_ifp) {
		igb_update_vf_stats_counters(sc);
		return;
	}
	stats = sc->stats;

	if (sc->hw.phy.media_type == e1000_media_type_copper ||
	    (E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)) {
		stats->symerrs +=
		    E1000_READ_REG(hw,E1000_SYMERRS);
		stats->sec += E1000_READ_REG(hw, E1000_SEC);
	}

	stats->crcerrs += E1000_READ_REG(hw, E1000_CRCERRS);
	stats->mpc += E1000_READ_REG(hw, E1000_MPC);
	stats->scc += E1000_READ_REG(hw, E1000_SCC);
	stats->ecol += E1000_READ_REG(hw, E1000_ECOL);

	stats->mcc += E1000_READ_REG(hw, E1000_MCC);
	stats->latecol += E1000_READ_REG(hw, E1000_LATECOL);
	stats->colc += E1000_READ_REG(hw, E1000_COLC);
	stats->dc += E1000_READ_REG(hw, E1000_DC);
	stats->rlec += E1000_READ_REG(hw, E1000_RLEC);
	stats->xonrxc += E1000_READ_REG(hw, E1000_XONRXC);
	stats->xontxc += E1000_READ_REG(hw, E1000_XONTXC);

	/*
	 * For watchdog management we need to know if we have been
	 * paused during the last interval, so capture that here.
	 */ 
	sc->pause_frames = E1000_READ_REG(hw, E1000_XOFFRXC);
	stats->xoffrxc += sc->pause_frames;
	stats->xofftxc += E1000_READ_REG(hw, E1000_XOFFTXC);
	stats->fcruc += E1000_READ_REG(hw, E1000_FCRUC);
	stats->prc64 += E1000_READ_REG(hw, E1000_PRC64);
	stats->prc127 += E1000_READ_REG(hw, E1000_PRC127);
	stats->prc255 += E1000_READ_REG(hw, E1000_PRC255);
	stats->prc511 += E1000_READ_REG(hw, E1000_PRC511);
	stats->prc1023 += E1000_READ_REG(hw, E1000_PRC1023);
	stats->prc1522 += E1000_READ_REG(hw, E1000_PRC1522);
	stats->gprc += E1000_READ_REG(hw, E1000_GPRC);
	stats->bprc += E1000_READ_REG(hw, E1000_BPRC);
	stats->mprc += E1000_READ_REG(hw, E1000_MPRC);
	stats->gptc += E1000_READ_REG(hw, E1000_GPTC);

	/* For the 64-bit byte counters the low dword must be read first. */
	/* Both registers clear on the read of the high dword */

	stats->gorc += E1000_READ_REG(hw, E1000_GORCL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_GORCH) << 32);
	stats->gotc += E1000_READ_REG(hw, E1000_GOTCL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_GOTCH) << 32);

	stats->rnbc += E1000_READ_REG(hw, E1000_RNBC);
	stats->ruc += E1000_READ_REG(hw, E1000_RUC);
	stats->rfc += E1000_READ_REG(hw, E1000_RFC);
	stats->roc += E1000_READ_REG(hw, E1000_ROC);
	stats->rjc += E1000_READ_REG(hw, E1000_RJC);

	stats->mgprc += E1000_READ_REG(hw, E1000_MGTPRC);
	stats->mgpdc += E1000_READ_REG(hw, E1000_MGTPDC);
	stats->mgptc += E1000_READ_REG(hw, E1000_MGTPTC);

	stats->tor += E1000_READ_REG(hw, E1000_TORL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_TORH) << 32);
	stats->tot += E1000_READ_REG(hw, E1000_TOTL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_TOTH) << 32);

	stats->tpr += E1000_READ_REG(hw, E1000_TPR);
	stats->tpt += E1000_READ_REG(hw, E1000_TPT);
	stats->ptc64 += E1000_READ_REG(hw, E1000_PTC64);
	stats->ptc127 += E1000_READ_REG(hw, E1000_PTC127);
	stats->ptc255 += E1000_READ_REG(hw, E1000_PTC255);
	stats->ptc511 += E1000_READ_REG(hw, E1000_PTC511);
	stats->ptc1023 += E1000_READ_REG(hw, E1000_PTC1023);
	stats->ptc1522 += E1000_READ_REG(hw, E1000_PTC1522);
	stats->mptc += E1000_READ_REG(hw, E1000_MPTC);
	stats->bptc += E1000_READ_REG(hw, E1000_BPTC);

	/* Interrupt Counts */

	stats->iac += E1000_READ_REG(hw, E1000_IAC);
	stats->icrxptc += E1000_READ_REG(hw, E1000_ICRXPTC);
	stats->icrxatc += E1000_READ_REG(hw, E1000_ICRXATC);
	stats->ictxptc += E1000_READ_REG(hw, E1000_ICTXPTC);
	stats->ictxatc += E1000_READ_REG(hw, E1000_ICTXATC);
	stats->ictxqec += E1000_READ_REG(hw, E1000_ICTXQEC);
	stats->ictxqmtc += E1000_READ_REG(hw, E1000_ICTXQMTC);
	stats->icrxdmtc += E1000_READ_REG(hw, E1000_ICRXDMTC);
	stats->icrxoc += E1000_READ_REG(hw, E1000_ICRXOC);

	/* Host to Card Statistics */

	stats->cbtmpc += E1000_READ_REG(hw, E1000_CBTMPC);
	stats->htdpmc += E1000_READ_REG(hw, E1000_HTDPMC);
	stats->cbrdpc += E1000_READ_REG(hw, E1000_CBRDPC);
	stats->cbrmpc += E1000_READ_REG(hw, E1000_CBRMPC);
	stats->rpthc += E1000_READ_REG(hw, E1000_RPTHC);
	stats->hgptc += E1000_READ_REG(hw, E1000_HGPTC);
	stats->htcbdpc += E1000_READ_REG(hw, E1000_HTCBDPC);
	stats->hgorc += (E1000_READ_REG(hw, E1000_HGORCL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_HGORCH) << 32));
	stats->hgotc += (E1000_READ_REG(hw, E1000_HGOTCL) +
	    ((uint64_t)E1000_READ_REG(hw, E1000_HGOTCH) << 32));
	stats->lenerrs += E1000_READ_REG(hw, E1000_LENERRS);
	stats->scvpc += E1000_READ_REG(hw, E1000_SCVPC);
	stats->hrmpc += E1000_READ_REG(hw, E1000_HRMPC);

	stats->algnerrc += E1000_READ_REG(hw, E1000_ALGNERRC);
	stats->rxerrc += E1000_READ_REG(hw, E1000_RXERRC);
	stats->tncrs += E1000_READ_REG(hw, E1000_TNCRS);
	stats->cexterr += E1000_READ_REG(hw, E1000_CEXTERR);
	stats->tsctc += E1000_READ_REG(hw, E1000_TSCTC);
	stats->tsctfc += E1000_READ_REG(hw, E1000_TSCTFC);

	IFNET_STAT_SET(ifp, collisions, stats->colc);

	/* Rx Errors */
	IFNET_STAT_SET(ifp, ierrors,
	    stats->rxerrc + stats->crcerrs + stats->algnerrc +
	    stats->ruc + stats->roc + stats->mpc + stats->cexterr);

	/* Tx Errors */
	IFNET_STAT_SET(ifp, oerrors,
	    stats->ecol + stats->latecol + sc->watchdog_events);

	/* Driver specific counters */
	sc->device_control = E1000_READ_REG(hw, E1000_CTRL);
	sc->rx_control = E1000_READ_REG(hw, E1000_RCTL);
	sc->int_mask = E1000_READ_REG(hw, E1000_IMS);
	sc->eint_mask = E1000_READ_REG(hw, E1000_EIMS);
	sc->packet_buf_alloc_tx =
	    ((E1000_READ_REG(hw, E1000_PBA) & 0xffff0000) >> 16);
	sc->packet_buf_alloc_rx =
	    (E1000_READ_REG(hw, E1000_PBA) & 0xffff);
}

static void
igb_vf_init_stats(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	struct e1000_vf_stats *stats;

	stats = sc->stats;
	stats->last_gprc = E1000_READ_REG(hw, E1000_VFGPRC);
	stats->last_gorc = E1000_READ_REG(hw, E1000_VFGORC);
	stats->last_gptc = E1000_READ_REG(hw, E1000_VFGPTC);
	stats->last_gotc = E1000_READ_REG(hw, E1000_VFGOTC);
	stats->last_mprc = E1000_READ_REG(hw, E1000_VFMPRC);
}
 
static void
igb_update_vf_stats_counters(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	struct e1000_vf_stats *stats;

	if (sc->link_speed == 0)
		return;

	stats = sc->stats;
	UPDATE_VF_REG(E1000_VFGPRC, stats->last_gprc, stats->gprc);
	UPDATE_VF_REG(E1000_VFGORC, stats->last_gorc, stats->gorc);
	UPDATE_VF_REG(E1000_VFGPTC, stats->last_gptc, stats->gptc);
	UPDATE_VF_REG(E1000_VFGOTC, stats->last_gotc, stats->gotc);
	UPDATE_VF_REG(E1000_VFMPRC, stats->last_mprc, stats->mprc);
}

#ifdef IFPOLL_ENABLE

static void
igb_npoll_status(struct ifnet *ifp)
{
	struct igb_softc *sc = ifp->if_softc;
	uint32_t reg_icr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	reg_icr = E1000_READ_REG(&sc->hw, E1000_ICR);
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		sc->hw.mac.get_link_status = 1;
		igb_update_link_status(sc);
	}
}

static void
igb_npoll_tx(struct ifnet *ifp, void *arg, int cycle __unused)
{
	struct igb_tx_ring *txr = arg;

	ASSERT_SERIALIZED(&txr->tx_serialize);
	igb_tx_intr(txr, *(txr->tx_hdr));
	igb_try_txgc(txr, 1);
}

static void
igb_npoll_rx(struct ifnet *ifp __unused, void *arg, int cycle)
{
	struct igb_rx_ring *rxr = arg;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	igb_rxeof(rxr, cycle);
}

static void
igb_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct igb_softc *sc = ifp->if_softc;
	int i, txr_cnt, rxr_cnt;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (info) {
		int cpu;

		info->ifpi_status.status_func = igb_npoll_status;
		info->ifpi_status.serializer = &sc->main_serialize;

		txr_cnt = igb_get_txring_inuse(sc, TRUE);
		for (i = 0; i < txr_cnt; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];

			cpu = if_ringmap_cpumap(sc->tx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_tx[cpu].poll_func = igb_npoll_tx;
			info->ifpi_tx[cpu].arg = txr;
			info->ifpi_tx[cpu].serializer = &txr->tx_serialize;
			ifsq_set_cpuid(txr->ifsq, cpu);
		}

		rxr_cnt = igb_get_rxring_inuse(sc, TRUE);
		for (i = 0; i < rxr_cnt; ++i) {
			struct igb_rx_ring *rxr = &sc->rx_rings[i];

			cpu = if_ringmap_cpumap(sc->rx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_rx[cpu].poll_func = igb_npoll_rx;
			info->ifpi_rx[cpu].arg = rxr;
			info->ifpi_rx[cpu].serializer = &rxr->rx_serialize;
		}
	} else {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];

			ifsq_set_cpuid(txr->ifsq, txr->tx_intr_cpuid);
		}
	}
	if (ifp->if_flags & IFF_RUNNING)
		igb_init(sc);
}

#endif /* IFPOLL_ENABLE */

static void
igb_intr(void *xsc)
{
	struct igb_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t eicr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	eicr = E1000_READ_REG(&sc->hw, E1000_EICR);

	if (eicr == 0)
		return;

	if (ifp->if_flags & IFF_RUNNING) {
		struct igb_tx_ring *txr = &sc->tx_rings[0];
		int i;

		for (i = 0; i < sc->rx_ring_inuse; ++i) {
			struct igb_rx_ring *rxr = &sc->rx_rings[i];

			if (eicr & rxr->rx_intr_mask) {
				lwkt_serialize_enter(&rxr->rx_serialize);
				igb_rxeof(rxr, -1);
				lwkt_serialize_exit(&rxr->rx_serialize);
			}
		}

		if (eicr & txr->tx_intr_mask) {
			lwkt_serialize_enter(&txr->tx_serialize);
			igb_tx_intr(txr, *(txr->tx_hdr));
			lwkt_serialize_exit(&txr->tx_serialize);
		}
	}

	if (eicr & E1000_EICR_OTHER) {
		uint32_t icr = E1000_READ_REG(&sc->hw, E1000_ICR);

		/* Link status change */
		if (icr & E1000_ICR_LSC) {
			sc->hw.mac.get_link_status = 1;
			igb_update_link_status(sc);
		}
	}

	/*
	 * Reading EICR has the side effect to clear interrupt mask,
	 * so all interrupts need to be enabled here.
	 */
	E1000_WRITE_REG(&sc->hw, E1000_EIMS, sc->intr_mask);
}

static void
igb_intr_shared(void *xsc)
{
	struct igb_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg_icr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	reg_icr = E1000_READ_REG(&sc->hw, E1000_ICR);

	/* Hot eject?  */
	if (reg_icr == 0xffffffff)
		return;

	/* Definitely not our interrupt.  */
	if (reg_icr == 0x0)
		return;

	if ((reg_icr & E1000_ICR_INT_ASSERTED) == 0)
		return;

	if (ifp->if_flags & IFF_RUNNING) {
		if (reg_icr &
		    (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO)) {
			int i;

			for (i = 0; i < sc->rx_ring_inuse; ++i) {
				struct igb_rx_ring *rxr = &sc->rx_rings[i];

				lwkt_serialize_enter(&rxr->rx_serialize);
				igb_rxeof(rxr, -1);
				lwkt_serialize_exit(&rxr->rx_serialize);
			}
		}

		if (reg_icr & E1000_ICR_TXDW) {
			struct igb_tx_ring *txr = &sc->tx_rings[0];

			lwkt_serialize_enter(&txr->tx_serialize);
			igb_tx_intr(txr, *(txr->tx_hdr));
			lwkt_serialize_exit(&txr->tx_serialize);
		}
	}

	/* Link status change */
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		sc->hw.mac.get_link_status = 1;
		igb_update_link_status(sc);
	}

	if (reg_icr & E1000_ICR_RXO)
		sc->rx_overruns++;
}

static int
igb_encap(struct igb_tx_ring *txr, struct mbuf **m_headp,
    int *segs_used, int *idx)
{
	bus_dma_segment_t segs[IGB_MAX_SCATTER];
	bus_dmamap_t map;
	struct igb_tx_buf *tx_buf, *tx_buf_mapped;
	union e1000_adv_tx_desc	*txd = NULL;
	struct mbuf *m_head = *m_headp;
	uint32_t olinfo_status = 0, cmd_type_len = 0, cmd_rs = 0;
	int maxsegs, nsegs, i, j, error;
	uint32_t hdrlen = 0;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		error = igb_tso_pullup(txr, m_headp);
		if (error)
			return error;
		m_head = *m_headp;
	}

	/* Set basic descriptor constants */
	cmd_type_len |= E1000_ADVTXD_DTYP_DATA;
	cmd_type_len |= E1000_ADVTXD_DCMD_IFCS | E1000_ADVTXD_DCMD_DEXT;
	if (m_head->m_flags & M_VLANTAG)
		cmd_type_len |= E1000_ADVTXD_DCMD_VLE;

	/*
	 * Map the packet for DMA.
	 */
	tx_buf = &txr->tx_buf[txr->next_avail_desc];
	tx_buf_mapped = tx_buf;
	map = tx_buf->map;

	maxsegs = txr->tx_avail - IGB_TX_RESERVED;
	if (maxsegs > IGB_MAX_SCATTER)
		maxsegs = IGB_MAX_SCATTER;

	error = bus_dmamap_load_mbuf_defrag(txr->tx_tag, map, m_headp,
	    segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		if (error == ENOBUFS)
			txr->sc->mbuf_defrag_failed++;
		else
			txr->sc->no_tx_dma_setup++;

		m_freem(*m_headp);
		*m_headp = NULL;
		return error;
	}
	bus_dmamap_sync(txr->tx_tag, map, BUS_DMASYNC_PREWRITE);

	m_head = *m_headp;

	/*
	 * Set up the TX context descriptor, if any hardware offloading is
	 * needed.  This includes CSUM, VLAN, and TSO.  It will consume one
	 * TX descriptor.
	 *
	 * Unlike these chips' predecessors (em/emx), TX context descriptor
	 * will _not_ interfere TX data fetching pipelining.
	 */
	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		igb_tso_ctx(txr, m_head, &hdrlen);
		cmd_type_len |= E1000_ADVTXD_DCMD_TSE;
		olinfo_status |= E1000_TXD_POPTS_IXSM << 8;
		olinfo_status |= E1000_TXD_POPTS_TXSM << 8;
		txr->tx_nsegs++;
		(*segs_used)++;
	} else if (igb_txcsum_ctx(txr, m_head)) {
		if (m_head->m_pkthdr.csum_flags & CSUM_IP)
			olinfo_status |= (E1000_TXD_POPTS_IXSM << 8);
		if (m_head->m_pkthdr.csum_flags & (CSUM_UDP | CSUM_TCP))
			olinfo_status |= (E1000_TXD_POPTS_TXSM << 8);
		txr->tx_nsegs++;
		(*segs_used)++;
	}

	*segs_used += nsegs;
	txr->tx_nsegs += nsegs;
	if (txr->tx_nsegs >= txr->intr_nsegs) {
		/*
		 * Report Status (RS) is turned on every intr_nsegs
		 * descriptors (roughly).
		 */
		txr->tx_nsegs = 0;
		cmd_rs = E1000_ADVTXD_DCMD_RS;
	}

	/* Calculate payload length */
	olinfo_status |= ((m_head->m_pkthdr.len - hdrlen)
	    << E1000_ADVTXD_PAYLEN_SHIFT);

	/*
	 * 82575 needs the TX context index added; the queue
	 * index is used as TX context index here.
	 */
	if (txr->sc->hw.mac.type == e1000_82575)
		olinfo_status |= txr->me << 4;

	/* Set up our transmit descriptors */
	i = txr->next_avail_desc;
	for (j = 0; j < nsegs; j++) {
		bus_size_t seg_len;
		bus_addr_t seg_addr;

		tx_buf = &txr->tx_buf[i];
		txd = (union e1000_adv_tx_desc *)&txr->tx_base[i];
		seg_addr = segs[j].ds_addr;
		seg_len = segs[j].ds_len;

		txd->read.buffer_addr = htole64(seg_addr);
		txd->read.cmd_type_len = htole32(cmd_type_len | seg_len);
		txd->read.olinfo_status = htole32(olinfo_status);
		if (++i == txr->num_tx_desc)
			i = 0;
		tx_buf->m_head = NULL;
	}

	KASSERT(txr->tx_avail > nsegs, ("invalid avail TX desc\n"));
	txr->next_avail_desc = i;
	txr->tx_avail -= nsegs;
	txr->tx_nmbuf++;

	tx_buf->m_head = m_head;
	tx_buf_mapped->map = tx_buf->map;
	tx_buf->map = map;

	/*
	 * Last Descriptor of Packet needs End Of Packet (EOP)
	 */
	txd->read.cmd_type_len |= htole32(E1000_ADVTXD_DCMD_EOP | cmd_rs);

	/*
	 * Defer TDT updating, until enough descrptors are setup
	 */
	*idx = i;
#ifdef IGB_TSS_DEBUG
	++txr->tx_packets;
#endif

	return 0;
}

static void
igb_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct igb_softc *sc = ifp->if_softc;
	struct igb_tx_ring *txr = ifsq_get_priv(ifsq);
	struct mbuf *m_head;
	int idx = -1, nsegs = 0;

	KKASSERT(txr->ifsq == ifsq);
	ASSERT_SERIALIZED(&txr->tx_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	if (!sc->link_active || (txr->tx_flags & IGB_TXFLAG_ENABLED) == 0) {
		ifsq_purge(ifsq);
		return;
	}

	while (!ifsq_is_empty(ifsq)) {
		if (txr->tx_avail <= IGB_MAX_SCATTER + IGB_TX_RESERVED) {
			ifsq_set_oactive(ifsq);
			/* Set watchdog on */
			ifsq_watchdog_set_count(&txr->tx_watchdog, 5);
			break;
		}

		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		if (igb_encap(txr, &m_head, &nsegs, &idx)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			continue;
		}

		/*
		 * TX interrupt are aggressively aggregated, so increasing
		 * opackets at TX interrupt time will make the opackets
		 * statistics vastly inaccurate; we do the opackets increment
		 * now.
		 */
		IFNET_STAT_INC(ifp, opackets, 1);

		if (nsegs >= txr->wreg_nsegs) {
			E1000_WRITE_REG(&txr->sc->hw, E1000_TDT(txr->me), idx);
			idx = -1;
			nsegs = 0;
		}

		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, m_head);
	}
	if (idx >= 0)
		E1000_WRITE_REG(&txr->sc->hw, E1000_TDT(txr->me), idx);
	txr->tx_running = IGB_TX_RUNNING;
}

static void
igb_watchdog(struct ifaltq_subque *ifsq)
{
	struct igb_tx_ring *txr = ifsq_get_priv(ifsq);
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct igb_softc *sc = ifp->if_softc;
	int i;

	KKASSERT(txr->ifsq == ifsq);
	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* 
	 * If flow control has paused us since last checking
	 * it invalidates the watchdog timing, so dont run it.
	 */
	if (sc->pause_frames) {
		sc->pause_frames = 0;
		ifsq_watchdog_set_count(&txr->tx_watchdog, 5);
		return;
	}

	if_printf(ifp, "Watchdog timeout -- resetting\n");
	if_printf(ifp, "Queue(%d) tdh = %d, hw tdt = %d\n", txr->me,
	    E1000_READ_REG(&sc->hw, E1000_TDH(txr->me)),
	    E1000_READ_REG(&sc->hw, E1000_TDT(txr->me)));
	if_printf(ifp, "TX(%d) desc avail = %d, "
	    "Next TX to Clean = %d\n",
	    txr->me, txr->tx_avail, txr->next_to_clean);

	IFNET_STAT_INC(ifp, oerrors, 1);
	sc->watchdog_events++;

	igb_init(sc);
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ifsq_devstart_sched(sc->tx_rings[i].ifsq);
}

static void
igb_set_eitr(struct igb_softc *sc, int idx, int rate)
{
	uint32_t eitr = 0;

	if (rate > 0) {
		if (sc->hw.mac.type == e1000_82575) {
			eitr = 1000000000 / 256 / rate;
			/*
			 * NOTE:
			 * Document is wrong on the 2 bits left shift
			 */
		} else {
			eitr = 1000000 / rate;
			eitr <<= IGB_EITR_INTVL_SHIFT;
		}

		if (eitr == 0) {
			/* Don't disable it */
			eitr = 1 << IGB_EITR_INTVL_SHIFT;
		} else if (eitr > IGB_EITR_INTVL_MASK) {
			/* Don't allow it to be too large */
			eitr = IGB_EITR_INTVL_MASK;
		}
	}
	if (sc->hw.mac.type == e1000_82575)
		eitr |= eitr << 16;
	else
		eitr |= E1000_EITR_CNT_IGNR;
	E1000_WRITE_REG(&sc->hw, E1000_EITR(idx), eitr);
}

static void
igb_add_intr_rate_sysctl(struct igb_softc *sc, int use,
    const char *name, const char *desc)
{
	int i;

	for (i = 0; i < sc->intr_cnt; ++i) {
		if (sc->intr_data[i].intr_use == use) {
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
			    OID_AUTO, name, CTLTYPE_INT | CTLFLAG_RW,
			    sc, use, igb_sysctl_intr_rate, "I", desc);
			break;
		}
	}
}

static int
igb_sysctl_intr_rate(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	int use = arg2;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, rate, i;
	struct igb_intr_data *intr;

	rate = 0;
	for (i = 0; i < sc->intr_cnt; ++i) {
		intr = &sc->intr_data[i];
		if (intr->intr_use == use) {
			rate = intr->intr_rate;
			break;
		}
	}

	error = sysctl_handle_int(oidp, &rate, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (rate <= 0)
		return EINVAL;

	ifnet_serialize_all(ifp);

	for (i = 0; i < sc->intr_cnt; ++i) {
		intr = &sc->intr_data[i];
		if (intr->intr_use == use && intr->intr_rate != rate) {
			intr->intr_rate = rate;
			if (ifp->if_flags & IFF_RUNNING)
				igb_set_eitr(sc, i, rate);
		}
	}

	ifnet_deserialize_all(ifp);

	return error;
}

static int
igb_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct igb_tx_ring *txr = &sc->tx_rings[0];
	int error, nsegs;

	nsegs = txr->intr_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (nsegs <= 0)
		return EINVAL;

	ifnet_serialize_all(ifp);

	if (nsegs >= txr->num_tx_desc - IGB_MAX_SCATTER - IGB_TX_RESERVED) {
		error = EINVAL;
	} else {
		int i;

		error = 0;
		for (i = 0; i < sc->tx_ring_cnt; ++i)
			sc->tx_rings[i].intr_nsegs = nsegs;
	}

	ifnet_deserialize_all(ifp);

	return error;
}

static int
igb_sysctl_rx_wreg_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, nsegs, i;

	nsegs = sc->rx_rings[0].wreg_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		sc->rx_rings[i].wreg_nsegs = nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
igb_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, nsegs, i;

	nsegs = sc->tx_rings[0].wreg_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		sc->tx_rings[i].wreg_nsegs = nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

static void
igb_init_intr(struct igb_softc *sc)
{
	int i;

	igb_set_intr_mask(sc);

	if ((sc->flags & IGB_FLAG_SHARED_INTR) == 0)
		igb_init_unshared_intr(sc);

	for (i = 0; i < sc->intr_cnt; ++i)
		igb_set_eitr(sc, i, sc->intr_data[i].intr_rate);
}

static void
igb_init_unshared_intr(struct igb_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	const struct igb_rx_ring *rxr;
	const struct igb_tx_ring *txr;
	uint32_t ivar, index;
	int i;

	/*
	 * Enable extended mode
	 */
	if (sc->hw.mac.type != e1000_82575) {
		uint32_t gpie;
		int ivar_max;

		gpie = E1000_GPIE_NSICR;
		if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
			gpie |= E1000_GPIE_MSIX_MODE |
			    E1000_GPIE_EIAME |
			    E1000_GPIE_PBA;
		}
		E1000_WRITE_REG(hw, E1000_GPIE, gpie);

		/*
		 * Clear IVARs
		 */
		switch (sc->hw.mac.type) {
		case e1000_82576:
			ivar_max = IGB_MAX_IVAR_82576;
			break;

		case e1000_82580:
			ivar_max = IGB_MAX_IVAR_82580;
			break;

		case e1000_i350:
			ivar_max = IGB_MAX_IVAR_I350;
			break;

		case e1000_i354:
			ivar_max = IGB_MAX_IVAR_I354;
			break;

		case e1000_vfadapt:
		case e1000_vfadapt_i350:
			ivar_max = IGB_MAX_IVAR_VF;
			break;

		case e1000_i210:
			ivar_max = IGB_MAX_IVAR_I210;
			break;

		case e1000_i211:
			ivar_max = IGB_MAX_IVAR_I211;
			break;

		default:
			panic("unknown mac type %d\n", sc->hw.mac.type);
		}
		for (i = 0; i < ivar_max; ++i)
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, i, 0);
		E1000_WRITE_REG(hw, E1000_IVAR_MISC, 0);
	} else {
		uint32_t tmp;

		KASSERT(sc->intr_type != PCI_INTR_TYPE_MSIX,
		    ("82575 w/ MSI-X"));
		tmp = E1000_READ_REG(hw, E1000_CTRL_EXT);
		tmp |= E1000_CTRL_EXT_IRCA;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, tmp);
	}

	/*
	 * Map TX/RX interrupts to EICR
	 */
	switch (sc->hw.mac.type) {
	case e1000_82580:
	case e1000_i350:
	case e1000_i354:
	case e1000_vfadapt:
	case e1000_vfadapt_i350:
	case e1000_i210:
	case e1000_i211:
		/* RX entries */
		for (i = 0; i < sc->rx_ring_inuse; ++i) {
			rxr = &sc->rx_rings[i];

			index = i >> 1;
			ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

			if (i & 1) {
				ivar &= 0xff00ffff;
				ivar |=
				(rxr->rx_intr_vec | E1000_IVAR_VALID) << 16;
			} else {
				ivar &= 0xffffff00;
				ivar |=
				(rxr->rx_intr_vec | E1000_IVAR_VALID);
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		/* TX entries */
		for (i = 0; i < sc->tx_ring_inuse; ++i) {
			txr = &sc->tx_rings[i];

			index = i >> 1;
			ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

			if (i & 1) {
				ivar &= 0x00ffffff;
				ivar |=
				(txr->tx_intr_vec | E1000_IVAR_VALID) << 24;
			} else {
				ivar &= 0xffff00ff;
				ivar |=
				(txr->tx_intr_vec | E1000_IVAR_VALID) << 8;
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
			ivar = (sc->sts_msix_vec | E1000_IVAR_VALID) << 8;
			E1000_WRITE_REG(hw, E1000_IVAR_MISC, ivar);
		}
		break;

	case e1000_82576:
		/* RX entries */
		for (i = 0; i < sc->rx_ring_inuse; ++i) {
			rxr = &sc->rx_rings[i];

			index = i & 0x7; /* Each IVAR has two entries */
			ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

			if (i < 8) {
				ivar &= 0xffffff00;
				ivar |=
				(rxr->rx_intr_vec | E1000_IVAR_VALID);
			} else {
				ivar &= 0xff00ffff;
				ivar |=
				(rxr->rx_intr_vec | E1000_IVAR_VALID) << 16;
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		/* TX entries */
		for (i = 0; i < sc->tx_ring_inuse; ++i) {
			txr = &sc->tx_rings[i];

			index = i & 0x7; /* Each IVAR has two entries */
			ivar = E1000_READ_REG_ARRAY(hw, E1000_IVAR0, index);

			if (i < 8) {
				ivar &= 0xffff00ff;
				ivar |=
				(txr->tx_intr_vec | E1000_IVAR_VALID) << 8;
			} else {
				ivar &= 0x00ffffff;
				ivar |=
				(txr->tx_intr_vec | E1000_IVAR_VALID) << 24;
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
			ivar = (sc->sts_msix_vec | E1000_IVAR_VALID) << 8;
			E1000_WRITE_REG(hw, E1000_IVAR_MISC, ivar);
		}
		break;

	case e1000_82575:
		/*
		 * Enable necessary interrupt bits.
		 *
		 * The name of the register is confusing; in addition to
		 * configuring the first vector of MSI-X, it also configures
		 * which bits of EICR could be set by the hardware even when
		 * MSI or line interrupt is used; it thus controls interrupt
		 * generation.  It MUST be configured explicitly; the default
		 * value mentioned in the datasheet is wrong: RX queue0 and
		 * TX queue0 are NOT enabled by default.
		 */
		E1000_WRITE_REG(&sc->hw, E1000_MSIXBM(0), sc->intr_mask);
		break;

	default:
		panic("unknown mac type %d\n", sc->hw.mac.type);
	}
}

static int
igb_setup_intr(struct igb_softc *sc)
{
	int i;

	for (i = 0; i < sc->intr_cnt; ++i) {
		struct igb_intr_data *intr = &sc->intr_data[i];
		int error;

		error = bus_setup_intr_descr(sc->dev, intr->intr_res,
		    INTR_MPSAFE, intr->intr_func, intr->intr_funcarg,
		    &intr->intr_hand, intr->intr_serialize, intr->intr_desc);
		if (error) {
			device_printf(sc->dev, "can't setup %dth intr\n", i);
			igb_teardown_intr(sc, i);
			return error;
		}
	}
	return 0;
}

static void
igb_set_txintr_mask(struct igb_tx_ring *txr, int *intr_vec0, int intr_vecmax)
{
	if (txr->sc->hw.mac.type == e1000_82575) {
		txr->tx_intr_vec = 0;	/* unused */
		switch (txr->me) {
		case 0:
			txr->tx_intr_mask = E1000_EICR_TX_QUEUE0;
			break;
		case 1:
			txr->tx_intr_mask = E1000_EICR_TX_QUEUE1;
			break;
		case 2:
			txr->tx_intr_mask = E1000_EICR_TX_QUEUE2;
			break;
		case 3:
			txr->tx_intr_mask = E1000_EICR_TX_QUEUE3;
			break;
		default:
			panic("unsupported # of TX ring, %d\n", txr->me);
		}
	} else {
		int intr_vec = *intr_vec0;

		txr->tx_intr_vec = intr_vec % intr_vecmax;
		txr->tx_intr_mask = 1 << txr->tx_intr_vec;

		*intr_vec0 = intr_vec + 1;
	}
}

static void
igb_set_rxintr_mask(struct igb_rx_ring *rxr, int *intr_vec0, int intr_vecmax)
{
	if (rxr->sc->hw.mac.type == e1000_82575) {
		rxr->rx_intr_vec = 0;	/* unused */
		switch (rxr->me) {
		case 0:
			rxr->rx_intr_mask = E1000_EICR_RX_QUEUE0;
			break;
		case 1:
			rxr->rx_intr_mask = E1000_EICR_RX_QUEUE1;
			break;
		case 2:
			rxr->rx_intr_mask = E1000_EICR_RX_QUEUE2;
			break;
		case 3:
			rxr->rx_intr_mask = E1000_EICR_RX_QUEUE3;
			break;
		default:
			panic("unsupported # of RX ring, %d\n", rxr->me);
		}
	} else {
		int intr_vec = *intr_vec0;

		rxr->rx_intr_vec = intr_vec % intr_vecmax;
		rxr->rx_intr_mask = 1 << rxr->rx_intr_vec;

		*intr_vec0 = intr_vec + 1;
	}
}

static void
igb_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct igb_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->serializes, sc->serialize_cnt, slz);
}

static void
igb_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct igb_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->serializes, sc->serialize_cnt, slz);
}

static int
igb_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct igb_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->serializes, sc->serialize_cnt,
	    slz);
}

#ifdef INVARIANTS

static void
igb_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct igb_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->serializes, sc->serialize_cnt,
	    slz, serialized);
}

#endif	/* INVARIANTS */

static void
igb_set_intr_mask(struct igb_softc *sc)
{
	int i;

	sc->intr_mask = sc->sts_intr_mask;
	for (i = 0; i < sc->rx_ring_inuse; ++i)
		sc->intr_mask |= sc->rx_rings[i].rx_intr_mask;
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		sc->intr_mask |= sc->tx_rings[i].tx_intr_mask;
	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if, "intr mask 0x%08x\n",
		    sc->intr_mask);
	}
}

static int
igb_alloc_intr(struct igb_softc *sc)
{
	struct igb_tx_ring *txr;
	struct igb_intr_data *intr;
	int i, intr_vec, intr_vecmax;
	u_int intr_flags;

	igb_alloc_msix(sc);
	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		igb_set_ring_inuse(sc, FALSE);
		goto done;
	}

	/*
	 * Reset some settings changed by igb_alloc_msix().
	 */
	if (sc->rx_rmap_intr != NULL) {
		if_ringmap_free(sc->rx_rmap_intr);
		sc->rx_rmap_intr = NULL;
	}
	if (sc->tx_rmap_intr != NULL) {
		if_ringmap_free(sc->tx_rmap_intr);
		sc->tx_rmap_intr = NULL;
	}
	if (sc->intr_data != NULL) {
		kfree(sc->intr_data, M_DEVBUF);
		sc->intr_data = NULL;
	}
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		txr = &sc->tx_rings[i];
		txr->tx_intr_vec = 0;
		txr->tx_intr_mask = 0;
		txr->tx_intr_cpuid = -1;
	}
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];

		rxr->rx_intr_vec = 0;
		rxr->rx_intr_mask = 0;
		rxr->rx_txr = NULL;
	}

	sc->intr_cnt = 1;
	sc->intr_data = kmalloc(sizeof(struct igb_intr_data), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	intr = &sc->intr_data[0];

	/*
	 * Allocate MSI/legacy interrupt resource
	 */
	sc->intr_type = pci_alloc_1intr(sc->dev, igb_msi_enable,
	    &intr->intr_rid, &intr_flags);

	if (sc->intr_type == PCI_INTR_TYPE_LEGACY) {
		int unshared;

		unshared = device_getenv_int(sc->dev, "irq.unshared", 0);
		if (!unshared) {
			sc->flags |= IGB_FLAG_SHARED_INTR;
			if (bootverbose)
				device_printf(sc->dev, "IRQ shared\n");
		} else {
			intr_flags &= ~RF_SHAREABLE;
			if (bootverbose)
				device_printf(sc->dev, "IRQ unshared\n");
		}
	}

	intr->intr_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &intr->intr_rid, intr_flags);
	if (intr->intr_res == NULL) {
		device_printf(sc->dev, "Unable to allocate bus resource: "
		    "interrupt\n");
		return ENXIO;
	}

	intr->intr_serialize = &sc->main_serialize;
	intr->intr_cpuid = rman_get_cpuid(intr->intr_res);
	intr->intr_func = (sc->flags & IGB_FLAG_SHARED_INTR) ?
	    igb_intr_shared : igb_intr;
	intr->intr_funcarg = sc;
	intr->intr_rate = IGB_INTR_RATE;
	intr->intr_use = IGB_INTR_USE_RXTX;

	sc->tx_rings[0].tx_intr_cpuid = intr->intr_cpuid;

	/*
	 * Setup MSI/legacy interrupt mask
	 */
	switch (sc->hw.mac.type) {
	case e1000_82575:
		intr_vecmax = IGB_MAX_TXRXINT_82575;
		break;

	case e1000_82576:
		intr_vecmax = IGB_MAX_TXRXINT_82576;
		break;

	case e1000_82580:
		intr_vecmax = IGB_MAX_TXRXINT_82580;
		break;

	case e1000_i350:
		intr_vecmax = IGB_MAX_TXRXINT_I350;
		break;

	case e1000_i354:
		intr_vecmax = IGB_MAX_TXRXINT_I354;
		break;

	case e1000_i210:
		intr_vecmax = IGB_MAX_TXRXINT_I210;
		break;

	case e1000_i211:
		intr_vecmax = IGB_MAX_TXRXINT_I211;
		break;

	default:
		intr_vecmax = IGB_MIN_TXRXINT;
		break;
	}
	intr_vec = 0;
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		igb_set_txintr_mask(&sc->tx_rings[i], &intr_vec, intr_vecmax);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		igb_set_rxintr_mask(&sc->rx_rings[i], &intr_vec, intr_vecmax);
	sc->sts_intr_mask = E1000_EICR_OTHER;

	igb_set_ring_inuse(sc, FALSE);
	KKASSERT(sc->rx_ring_inuse <= IGB_MIN_RING_RSS);
	if (sc->rx_ring_inuse == IGB_MIN_RING_RSS) {
		/*
		 * Allocate RX ring map for RSS setup.
		 */
		sc->rx_rmap_intr = if_ringmap_alloc(sc->dev,
		    IGB_MIN_RING_RSS, IGB_MIN_RING_RSS);
		KASSERT(if_ringmap_count(sc->rx_rmap_intr) ==
		    sc->rx_ring_inuse, ("RX ring inuse mismatch"));
	}
done:
	igb_set_intr_mask(sc);
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		txr = &sc->tx_rings[i];
		if (txr->tx_intr_cpuid < 0)
			txr->tx_intr_cpuid = 0;
	}
	return 0;
}

static void
igb_free_intr(struct igb_softc *sc)
{
	if (sc->intr_data == NULL)
		return;

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		struct igb_intr_data *intr = &sc->intr_data[0];

		KKASSERT(sc->intr_cnt == 1);
		if (intr->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    intr->intr_rid, intr->intr_res);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->dev);

		kfree(sc->intr_data, M_DEVBUF);
	} else {
		igb_free_msix(sc, TRUE);
	}
}

static void
igb_teardown_intr(struct igb_softc *sc, int intr_cnt)
{
	int i;

	if (sc->intr_data == NULL)
		return;

	for (i = 0; i < intr_cnt; ++i) {
		struct igb_intr_data *intr = &sc->intr_data[i];

		bus_teardown_intr(sc->dev, intr->intr_res, intr->intr_hand);
	}
}

static void
igb_alloc_msix(struct igb_softc *sc)
{
	int msix_enable, msix_cnt, msix_ring, alloc_cnt;
	int i, x, error;
	int ring_cnt, ring_cntmax;
	struct igb_intr_data *intr;
	boolean_t setup = FALSE;

	/*
	 * Don't enable MSI-X on 82575, see:
	 * 82575 specification update errata #25
	 */
	if (sc->hw.mac.type == e1000_82575)
		return;

	/* Don't enable MSI-X on VF */
	if (sc->vf_ifp)
		return;

	msix_enable = device_getenv_int(sc->dev, "msix.enable",
	    igb_msix_enable);
	if (!msix_enable)
		return;

	msix_cnt = pci_msix_count(sc->dev);
#ifdef IGB_MSIX_DEBUG
	msix_cnt = device_getenv_int(sc->dev, "msix.count", msix_cnt);
#endif
	if (msix_cnt <= 1) {
		/* One MSI-X model does not make sense. */
		return;
	}
	if (bootverbose)
		device_printf(sc->dev, "MSI-X count %d\n", msix_cnt);
	msix_ring = msix_cnt - 1; /* -1 for status */

	/*
	 * Configure # of RX/TX rings usable by MSI-X.
	 */
	igb_get_rxring_cnt(sc, &ring_cnt, &ring_cntmax);
	if (ring_cntmax > msix_ring)
		ring_cntmax = msix_ring;
	sc->rx_rmap_intr = if_ringmap_alloc(sc->dev, ring_cnt, ring_cntmax);

	igb_get_txring_cnt(sc, &ring_cnt, &ring_cntmax);
	if (ring_cntmax > msix_ring)
		ring_cntmax = msix_ring;
	sc->tx_rmap_intr = if_ringmap_alloc(sc->dev, ring_cnt, ring_cntmax);

	if_ringmap_match(sc->dev, sc->rx_rmap_intr, sc->tx_rmap_intr);
	sc->rx_ring_msix = if_ringmap_count(sc->rx_rmap_intr);
	KASSERT(sc->rx_ring_msix <= sc->rx_ring_cnt,
	    ("total RX ring count %d, MSI-X RX ring count %d",
	     sc->rx_ring_cnt, sc->rx_ring_msix));
	sc->tx_ring_msix = if_ringmap_count(sc->tx_rmap_intr);
	KASSERT(sc->tx_ring_msix <= sc->tx_ring_cnt,
	    ("total TX ring count %d, MSI-X TX ring count %d",
	     sc->tx_ring_cnt, sc->tx_ring_msix));

	/*
	 * Aggregate TX/RX MSI-X
	 */
	ring_cntmax = sc->rx_ring_msix;
	if (ring_cntmax < sc->tx_ring_msix)
		ring_cntmax = sc->tx_ring_msix;
	KASSERT(ring_cntmax <= msix_ring,
	    ("invalid ring count max %d, MSI-X count for rings %d",
	     ring_cntmax, msix_ring));

	alloc_cnt = ring_cntmax + 1; /* +1 for status */
	if (bootverbose) {
		device_printf(sc->dev, "MSI-X alloc %d, "
		    "RX ring %d, TX ring %d\n", alloc_cnt,
		    sc->rx_ring_msix, sc->tx_ring_msix);
	}

	sc->msix_mem_rid = PCIR_BAR(IGB_MSIX_BAR);
	sc->msix_mem_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->msix_mem_rid, RF_ACTIVE);
	if (sc->msix_mem_res == NULL) {
		sc->msix_mem_rid = PCIR_BAR(IGB_MSIX_BAR_ALT);
		sc->msix_mem_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
		    &sc->msix_mem_rid, RF_ACTIVE);
		if (sc->msix_mem_res == NULL) {
			device_printf(sc->dev, "Unable to map MSI-X table\n");
			return;
		}
	}

	sc->intr_cnt = alloc_cnt;
	sc->intr_data = kmalloc(sizeof(struct igb_intr_data) * sc->intr_cnt,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (x = 0; x < sc->intr_cnt; ++x) {
		intr = &sc->intr_data[x];
		intr->intr_rid = -1;
		intr->intr_rate = IGB_INTR_RATE;
	}

	x = 0;
	for (i = 0; i < sc->rx_ring_msix; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];
		struct igb_tx_ring *txr = NULL;
		int cpuid, j;

		KKASSERT(x < sc->intr_cnt);
		rxr->rx_intr_vec = x;
		rxr->rx_intr_mask = 1 << rxr->rx_intr_vec;

		cpuid = if_ringmap_cpumap(sc->rx_rmap_intr, i);

		/*
		 * Try finding TX ring to piggyback.
		 */
		for (j = 0; j < sc->tx_ring_msix; ++j) {
			if (cpuid ==
			    if_ringmap_cpumap(sc->tx_rmap_intr, j)) {
				txr = &sc->tx_rings[j];
				KKASSERT(txr->tx_intr_cpuid < 0);
				break;
			}
		}
		rxr->rx_txr = txr;

		intr = &sc->intr_data[x++];
		intr->intr_serialize = &rxr->rx_serialize;
		intr->intr_cpuid = cpuid;
		KKASSERT(intr->intr_cpuid < netisr_ncpus);
		intr->intr_funcarg = rxr;
		if (txr != NULL) {
			intr->intr_func = igb_msix_rxtx;
			intr->intr_use = IGB_INTR_USE_RXTX;
			ksnprintf(intr->intr_desc0, sizeof(intr->intr_desc0),
			    "%s rx%dtx%d", device_get_nameunit(sc->dev),
			    i, txr->me);

			txr->tx_intr_vec = rxr->rx_intr_vec;
			txr->tx_intr_mask = rxr->rx_intr_mask;
			txr->tx_intr_cpuid = intr->intr_cpuid;
		} else {
			intr->intr_func = igb_msix_rx;
			intr->intr_rate = IGB_MSIX_RX_RATE;
			intr->intr_use = IGB_INTR_USE_RX;

			ksnprintf(intr->intr_desc0, sizeof(intr->intr_desc0),
			    "%s rx%d", device_get_nameunit(sc->dev), i);
		}
		intr->intr_desc = intr->intr_desc0;
	}

	for (i = 0; i < sc->tx_ring_msix; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		if (txr->tx_intr_cpuid >= 0) {
			/* Piggybacked by RX ring. */
			continue;
		}

		KKASSERT(x < sc->intr_cnt);
		txr->tx_intr_vec = x;
		txr->tx_intr_mask = 1 << txr->tx_intr_vec;

		intr = &sc->intr_data[x++];
		intr->intr_serialize = &txr->tx_serialize;
		intr->intr_func = igb_msix_tx;
		intr->intr_funcarg = txr;
		intr->intr_rate = IGB_MSIX_TX_RATE;
		intr->intr_use = IGB_INTR_USE_TX;

		intr->intr_cpuid = if_ringmap_cpumap(sc->tx_rmap_intr, i);
		KKASSERT(intr->intr_cpuid < netisr_ncpus);
		txr->tx_intr_cpuid = intr->intr_cpuid;

		ksnprintf(intr->intr_desc0, sizeof(intr->intr_desc0), "%s tx%d",
		    device_get_nameunit(sc->dev), i);
		intr->intr_desc = intr->intr_desc0;
	}

	/*
	 * Link status
	 */
	KKASSERT(x < sc->intr_cnt);
	sc->sts_msix_vec = x;
	sc->sts_intr_mask = 1 << sc->sts_msix_vec;

	intr = &sc->intr_data[x++];
	intr->intr_serialize = &sc->main_serialize;
	intr->intr_func = igb_msix_status;
	intr->intr_funcarg = sc;
	intr->intr_cpuid = 0;
	intr->intr_use = IGB_INTR_USE_STATUS;

	ksnprintf(intr->intr_desc0, sizeof(intr->intr_desc0), "%s sts",
	    device_get_nameunit(sc->dev));
	intr->intr_desc = intr->intr_desc0;

	KKASSERT(x == sc->intr_cnt);

	error = pci_setup_msix(sc->dev);
	if (error) {
		device_printf(sc->dev, "Setup MSI-X failed\n");
		goto back;
	}
	setup = TRUE;

	for (i = 0; i < sc->intr_cnt; ++i) {
		intr = &sc->intr_data[i];

		error = pci_alloc_msix_vector(sc->dev, i, &intr->intr_rid,
		    intr->intr_cpuid);
		if (error) {
			device_printf(sc->dev,
			    "Unable to allocate MSI-X %d on cpu%d\n", i,
			    intr->intr_cpuid);
			goto back;
		}

		intr->intr_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
		    &intr->intr_rid, RF_ACTIVE);
		if (intr->intr_res == NULL) {
			device_printf(sc->dev,
			    "Unable to allocate MSI-X %d resource\n", i);
			error = ENOMEM;
			goto back;
		}
	}

	pci_enable_msix(sc->dev);
	sc->intr_type = PCI_INTR_TYPE_MSIX;
back:
	if (error)
		igb_free_msix(sc, setup);
}

static void
igb_free_msix(struct igb_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->intr_cnt > 1);

	for (i = 0; i < sc->intr_cnt; ++i) {
		struct igb_intr_data *intr = &sc->intr_data[i];

		if (intr->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    intr->intr_rid, intr->intr_res);
		}
		if (intr->intr_rid >= 0)
			pci_release_msix_vector(sc->dev, intr->intr_rid);
	}
	if (setup)
		pci_teardown_msix(sc->dev);

	sc->intr_cnt = 0;
	kfree(sc->intr_data, M_DEVBUF);
	sc->intr_data = NULL;
}

static void
igb_msix_rx(void *arg)
{
	struct igb_rx_ring *rxr = arg;

	ASSERT_SERIALIZED(&rxr->rx_serialize);
	igb_rxeof(rxr, -1);

	E1000_WRITE_REG(&rxr->sc->hw, E1000_EIMS, rxr->rx_intr_mask);
}

static void
igb_msix_tx(void *arg)
{
	struct igb_tx_ring *txr = arg;

	ASSERT_SERIALIZED(&txr->tx_serialize);

	igb_tx_intr(txr, *(txr->tx_hdr));
	E1000_WRITE_REG(&txr->sc->hw, E1000_EIMS, txr->tx_intr_mask);
}

static void
igb_msix_status(void *arg)
{
	struct igb_softc *sc = arg;
	uint32_t icr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	icr = E1000_READ_REG(&sc->hw, E1000_ICR);
	if (icr & E1000_ICR_LSC) {
		sc->hw.mac.get_link_status = 1;
		igb_update_link_status(sc);
	}

	E1000_WRITE_REG(&sc->hw, E1000_EIMS, sc->sts_intr_mask);
}

static void
igb_set_ring_inuse(struct igb_softc *sc, boolean_t polling)
{
	sc->rx_ring_inuse = igb_get_rxring_inuse(sc, polling);
	sc->tx_ring_inuse = igb_get_txring_inuse(sc, polling);
	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if, "RX rings %d/%d, TX rings %d/%d\n",
		    sc->rx_ring_inuse, sc->rx_ring_cnt,
		    sc->tx_ring_inuse, sc->tx_ring_cnt);
	}
}

static int
igb_get_rxring_inuse(const struct igb_softc *sc, boolean_t polling)
{
	if (!IGB_ENABLE_HWRSS(sc))
		return 1;

	if (polling)
		return sc->rx_ring_cnt;
	else if (sc->intr_type != PCI_INTR_TYPE_MSIX)
		return IGB_MIN_RING_RSS;
	else
		return sc->rx_ring_msix;
}

static int
igb_get_txring_inuse(const struct igb_softc *sc, boolean_t polling)
{
	if (!IGB_ENABLE_HWTSS(sc))
		return 1;

	if (polling)
		return sc->tx_ring_cnt;
	else if (sc->intr_type != PCI_INTR_TYPE_MSIX)
		return IGB_MIN_RING;
	else
		return sc->tx_ring_msix;
}

static int
igb_tso_pullup(struct igb_tx_ring *txr, struct mbuf **mp)
{
	int hoff, iphlen, thoff;
	struct mbuf *m;

	m = *mp;
	KASSERT(M_WRITABLE(m), ("TSO mbuf not writable"));

	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;
	hoff = m->m_pkthdr.csum_lhlen;

	KASSERT(iphlen > 0, ("invalid ip hlen"));
	KASSERT(thoff > 0, ("invalid tcp hlen"));
	KASSERT(hoff > 0, ("invalid ether hlen"));

	if (__predict_false(m->m_len < hoff + iphlen + thoff)) {
		m = m_pullup(m, hoff + iphlen + thoff);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		*mp = m;
	}
	if (txr->tx_flags & IGB_TXFLAG_TSO_IPLEN0) {
		struct ip *ip;

		ip = mtodoff(m, struct ip *, hoff);
		ip->ip_len = 0;
	}

	return 0;
}

static void
igb_tso_ctx(struct igb_tx_ring *txr, struct mbuf *m, uint32_t *hlen)
{
	struct e1000_adv_tx_context_desc *TXD;
	uint32_t vlan_macip_lens, type_tucmd_mlhl, mss_l4len_idx;
	int hoff, ctxd, iphlen, thoff;

	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;
	hoff = m->m_pkthdr.csum_lhlen;

	vlan_macip_lens = type_tucmd_mlhl = mss_l4len_idx = 0;

	ctxd = txr->next_avail_desc;
	TXD = (struct e1000_adv_tx_context_desc *)&txr->tx_base[ctxd];

	if (m->m_flags & M_VLANTAG) {
		uint16_t vlantag;

		vlantag = htole16(m->m_pkthdr.ether_vlantag);
		vlan_macip_lens |= (vlantag << E1000_ADVTXD_VLAN_SHIFT);
	}

	vlan_macip_lens |= (hoff << E1000_ADVTXD_MACLEN_SHIFT);
	vlan_macip_lens |= iphlen;

	type_tucmd_mlhl |= E1000_ADVTXD_DCMD_DEXT | E1000_ADVTXD_DTYP_CTXT;
	type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_TCP;
	type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV4;

	mss_l4len_idx |= (m->m_pkthdr.tso_segsz << E1000_ADVTXD_MSS_SHIFT);
	mss_l4len_idx |= (thoff << E1000_ADVTXD_L4LEN_SHIFT);

	/*
	 * 82575 needs the TX context index added; the queue
	 * index is used as TX context index here.
	 */
	if (txr->sc->hw.mac.type == e1000_82575)
		mss_l4len_idx |= txr->me << 4;

	TXD->vlan_macip_lens = htole32(vlan_macip_lens);
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	TXD->seqnum_seed = htole32(0);
	TXD->mss_l4len_idx = htole32(mss_l4len_idx);

	/* We've consumed the first desc, adjust counters */
	if (++ctxd == txr->num_tx_desc)
		ctxd = 0;
	txr->next_avail_desc = ctxd;
	--txr->tx_avail;

	*hlen = hoff + iphlen + thoff;
}

static void
igb_setup_serialize(struct igb_softc *sc)
{
	int i = 0, j;

	/* Main + RX + TX */
	sc->serialize_cnt = 1 + sc->rx_ring_cnt + sc->tx_ring_cnt;
	sc->serializes =
	    kmalloc(sc->serialize_cnt * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializes
	 *
	 * NOTE: Order is critical
	 */

	KKASSERT(i < sc->serialize_cnt);
	sc->serializes[i++] = &sc->main_serialize;

	for (j = 0; j < sc->rx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->rx_rings[j].rx_serialize;
	}

	for (j = 0; j < sc->tx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->tx_rings[j].tx_serialize;
	}

	KKASSERT(i == sc->serialize_cnt);
}

static void
igb_msix_rxtx(void *arg)
{
	struct igb_rx_ring *rxr = arg;
	struct igb_tx_ring *txr;
	int hdr;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	igb_rxeof(rxr, -1);

	/*
	 * NOTE:
	 * Since next_to_clean is only changed by igb_txeof(),
	 * which is called only in interrupt handler, the
	 * check w/o holding tx serializer is MPSAFE.
	 */
	txr = rxr->rx_txr;
	hdr = *(txr->tx_hdr);
	if (hdr != txr->next_to_clean) {
		lwkt_serialize_enter(&txr->tx_serialize);
		igb_tx_intr(txr, hdr);
		lwkt_serialize_exit(&txr->tx_serialize);
	}

	E1000_WRITE_REG(&rxr->sc->hw, E1000_EIMS, rxr->rx_intr_mask);
}

static void
igb_set_timer_cpuid(struct igb_softc *sc, boolean_t polling)
{
	if (polling || sc->intr_type == PCI_INTR_TYPE_MSIX)
		sc->timer_cpuid = 0; /* XXX fixed */
	else
		sc->timer_cpuid = rman_get_cpuid(sc->intr_data[0].intr_res);
}

static void
igb_init_dmac(struct igb_softc *sc, uint32_t pba)
{
	struct e1000_hw *hw = &sc->hw;
	uint32_t reg;

	if (hw->mac.type == e1000_i211)
		return;

	if (hw->mac.type > e1000_82580) {
		uint32_t dmac;
		uint16_t hwm;

		if (sc->dma_coalesce == 0) { /* Disabling it */
			reg = ~E1000_DMACR_DMAC_EN;
			E1000_WRITE_REG(hw, E1000_DMACR, reg);
			return;
		} else {
			if_printf(&sc->arpcom.ac_if,
			    "DMA Coalescing enabled\n");
		}

		/* Set starting threshold */
		E1000_WRITE_REG(hw, E1000_DMCTXTH, 0);

		hwm = 64 * pba - sc->max_frame_size / 16;
		if (hwm < 64 * (pba - 6))
			hwm = 64 * (pba - 6);
		reg = E1000_READ_REG(hw, E1000_FCRTC);
		reg &= ~E1000_FCRTC_RTH_COAL_MASK;
		reg |= ((hwm << E1000_FCRTC_RTH_COAL_SHIFT)
		    & E1000_FCRTC_RTH_COAL_MASK);
		E1000_WRITE_REG(hw, E1000_FCRTC, reg);

		dmac = pba - sc->max_frame_size / 512;
		if (dmac < pba - 10)
			dmac = pba - 10;
		reg = E1000_READ_REG(hw, E1000_DMACR);
		reg &= ~E1000_DMACR_DMACTHR_MASK;
		reg |= ((dmac << E1000_DMACR_DMACTHR_SHIFT)
		    & E1000_DMACR_DMACTHR_MASK);

		/* transition to L0x or L1 if available..*/
		reg |= (E1000_DMACR_DMAC_EN | E1000_DMACR_DMAC_LX_MASK);

		/*
		 * Check if status is 2.5Gb backplane connection
		 * before configuration of watchdog timer, which
		 * is in msec values in 12.8usec intervals watchdog
		 * timer = msec values in 32usec intervals for non
		 * 2.5Gb connection.
		 */
		if (hw->mac.type == e1000_i354) {
			int status = E1000_READ_REG(hw, E1000_STATUS);

			if ((status & E1000_STATUS_2P5_SKU) &&
			    !(status & E1000_STATUS_2P5_SKU_OVER))
				reg |= ((sc->dma_coalesce * 5) >> 6);
			else
				reg |= (sc->dma_coalesce >> 5);
		} else {
			reg |= (sc->dma_coalesce >> 5);
		}

		E1000_WRITE_REG(hw, E1000_DMACR, reg);

		E1000_WRITE_REG(hw, E1000_DMCRTRH, 0);

		/* Set the interval before transition */
		reg = E1000_READ_REG(hw, E1000_DMCTLX);
		if (hw->mac.type == e1000_i350)
			reg |= IGB_DMCTLX_DCFLUSH_DIS;
		/*
		 * In 2.5Gb connection, TTLX unit is 0.4 usec, which
		 * is 0x4*2 = 0xA.  But delay is still 4 usec.
		 */
		if (hw->mac.type == e1000_i354) {
			int status = E1000_READ_REG(hw, E1000_STATUS);

			if ((status & E1000_STATUS_2P5_SKU) &&
			    !(status & E1000_STATUS_2P5_SKU_OVER))
				reg |= 0xA;
			else
				reg |= 0x4;
		} else {
			reg |= 0x4;
		}
		E1000_WRITE_REG(hw, E1000_DMCTLX, reg);

		/* Free space in tx packet buffer to wake from DMA coal */
		E1000_WRITE_REG(hw, E1000_DMCTXTH,
		    (IGB_TXPBSIZE - (2 * sc->max_frame_size)) >> 6);

		/* Make low power state decision controlled by DMA coal */
		reg = E1000_READ_REG(hw, E1000_PCIEMISC);
		reg &= ~E1000_PCIEMISC_LX_DECISION;
		E1000_WRITE_REG(hw, E1000_PCIEMISC, reg);
	} else if (hw->mac.type == e1000_82580) {
		reg = E1000_READ_REG(hw, E1000_PCIEMISC);
		E1000_WRITE_REG(hw, E1000_PCIEMISC,
		    reg & ~E1000_PCIEMISC_LX_DECISION);
		E1000_WRITE_REG(hw, E1000_DMACR, 0);
	}
}

static void
igb_reg_dump(struct igb_softc *sc)
{
	device_t dev = sc->dev;
	int col = 0;

#define DUMPREG(regno)	\
	kprintf(" %13s=%08x", #regno + 6, E1000_READ_REG(&sc->hw, regno));\
	if (++col == 3) {		\
		kprintf("\n");		\
		col = 0;		\
	}				\

	device_printf(dev, "REGISTER DUMP\n");
	DUMPREG(E1000_CTRL);
	DUMPREG(E1000_STATUS);
	DUMPREG(E1000_EECD);
	DUMPREG(E1000_EERD);
	DUMPREG(E1000_CTRL_EXT);
	DUMPREG(E1000_FLA);
	DUMPREG(E1000_MDIC);
	DUMPREG(E1000_SCTL);
	DUMPREG(E1000_FCAL);
	DUMPREG(E1000_FCAH);
	DUMPREG(E1000_FCT);
	DUMPREG(E1000_CONNSW);
	DUMPREG(E1000_VET);
	DUMPREG(E1000_ICR);
	DUMPREG(E1000_ITR);
	DUMPREG(E1000_IMS);
	DUMPREG(E1000_IVAR);
	DUMPREG(E1000_SVCR);
	DUMPREG(E1000_SVT);
	DUMPREG(E1000_LPIC);
	DUMPREG(E1000_RCTL);
	DUMPREG(E1000_FCTTV);
	DUMPREG(E1000_TXCW);
	DUMPREG(E1000_RXCW);
	DUMPREG(E1000_EIMS);
	DUMPREG(E1000_EIAC);
	DUMPREG(E1000_EIAM);
	DUMPREG(E1000_GPIE);
	DUMPREG(E1000_IVAR0);
	DUMPREG(E1000_IVAR_MISC);
	DUMPREG(E1000_TCTL);
	DUMPREG(E1000_TCTL_EXT);
	DUMPREG(E1000_TIPG);
	DUMPREG(E1000_TBT);
	DUMPREG(E1000_AIT);
	DUMPREG(E1000_LEDCTL);
	DUMPREG(E1000_EXTCNF_CTRL);
	DUMPREG(E1000_EXTCNF_SIZE);
	DUMPREG(E1000_PHY_CTRL);
	DUMPREG(E1000_PBA);
	DUMPREG(E1000_PBS);
	DUMPREG(E1000_PBECCSTS);
	DUMPREG(E1000_EEMNGCTL);
	DUMPREG(E1000_EEARBC);
	DUMPREG(E1000_FLASHT);
	DUMPREG(E1000_EEARBC_I210);
	DUMPREG(E1000_EEWR);
	DUMPREG(E1000_FLSWCTL);
	DUMPREG(E1000_FLSWDATA);
	DUMPREG(E1000_FLSWCNT);
	DUMPREG(E1000_FLOP);
	DUMPREG(E1000_I2CCMD);
	DUMPREG(E1000_I2CPARAMS);
	DUMPREG(E1000_WDSTP);
	DUMPREG(E1000_SWDSTS);
	DUMPREG(E1000_FRTIMER);
	DUMPREG(E1000_TCPTIMER);
	DUMPREG(E1000_VPDDIAG);
	DUMPREG(E1000_IMS_V2);
	DUMPREG(E1000_IAM_V2);
	DUMPREG(E1000_ERT);
	DUMPREG(E1000_FCRTL);
	DUMPREG(E1000_FCRTH);
	DUMPREG(E1000_PSRCTL);
	DUMPREG(E1000_RDFH);
	DUMPREG(E1000_RDFT);
	DUMPREG(E1000_RDFHS);
	DUMPREG(E1000_RDFTS);
	DUMPREG(E1000_RDFPC);
	DUMPREG(E1000_PBRTH);
	DUMPREG(E1000_FCRTV);
	DUMPREG(E1000_RDPUMB);
	DUMPREG(E1000_RDPUAD);
	DUMPREG(E1000_RDPUWD);
	DUMPREG(E1000_RDPURD);
	DUMPREG(E1000_RDPUCTL);
	DUMPREG(E1000_PBDIAG);
	DUMPREG(E1000_RXPBS);
	DUMPREG(E1000_IRPBS);
	DUMPREG(E1000_PBRWAC);
	DUMPREG(E1000_RDTR);
	DUMPREG(E1000_RADV);
	DUMPREG(E1000_SRWR);
	DUMPREG(E1000_I210_FLMNGCTL);
	DUMPREG(E1000_I210_FLMNGDATA);
	DUMPREG(E1000_I210_FLMNGCNT);
	DUMPREG(E1000_I210_FLSWCTL);
	DUMPREG(E1000_I210_FLSWDATA);
	DUMPREG(E1000_I210_FLSWCNT);
	DUMPREG(E1000_I210_FLA);
	DUMPREG(E1000_INVM_SIZE);
	DUMPREG(E1000_I210_TQAVCTRL);
	DUMPREG(E1000_RSRPD);
	DUMPREG(E1000_RAID);
	DUMPREG(E1000_TXDMAC);
	DUMPREG(E1000_KABGTXD);
	DUMPREG(E1000_PBSLAC);
	DUMPREG(E1000_TXPBS);
	DUMPREG(E1000_ITPBS);
	DUMPREG(E1000_TDFH);
	DUMPREG(E1000_TDFT);
	DUMPREG(E1000_TDFHS);
	DUMPREG(E1000_TDFTS);
	DUMPREG(E1000_TDFPC);
	DUMPREG(E1000_TDPUMB);
	DUMPREG(E1000_TDPUAD);
	DUMPREG(E1000_TDPUWD);
	DUMPREG(E1000_TDPURD);
	DUMPREG(E1000_TDPUCTL);
	DUMPREG(E1000_DTXCTL);
	DUMPREG(E1000_DTXTCPFLGL);
	DUMPREG(E1000_DTXTCPFLGH);
	DUMPREG(E1000_DTXMXSZRQ);
	DUMPREG(E1000_TIDV);
	DUMPREG(E1000_TADV);
	DUMPREG(E1000_TSPMT);
	DUMPREG(E1000_VFGPRC);
	DUMPREG(E1000_VFGORC);
	DUMPREG(E1000_VFMPRC);
	DUMPREG(E1000_VFGPTC);
	DUMPREG(E1000_VFGOTC);
	DUMPREG(E1000_VFGOTLBC);
	DUMPREG(E1000_VFGPTLBC);
	DUMPREG(E1000_VFGORLBC);
	DUMPREG(E1000_VFGPRLBC);
	DUMPREG(E1000_LSECTXCAP);
	DUMPREG(E1000_LSECRXCAP);
	DUMPREG(E1000_LSECTXCTRL);
	DUMPREG(E1000_LSECRXCTRL);
	DUMPREG(E1000_LSECTXSCL);
	DUMPREG(E1000_LSECTXSCH);
	DUMPREG(E1000_LSECTXSA);
	DUMPREG(E1000_LSECTXPN0);
	DUMPREG(E1000_LSECTXPN1);
	DUMPREG(E1000_LSECRXSCL);
	DUMPREG(E1000_LSECRXSCH);
	DUMPREG(E1000_IPSCTRL);
	DUMPREG(E1000_IPSRXCMD);
	DUMPREG(E1000_IPSRXIDX);
	DUMPREG(E1000_IPSRXSALT);
	DUMPREG(E1000_IPSRXSPI);
	DUMPREG(E1000_IPSTXSALT);
	DUMPREG(E1000_IPSTXIDX);
	DUMPREG(E1000_PCS_CFG0);
	DUMPREG(E1000_PCS_LCTL);
	DUMPREG(E1000_PCS_LSTAT);
	DUMPREG(E1000_PCS_ANADV);
	DUMPREG(E1000_PCS_LPAB);
	DUMPREG(E1000_PCS_NPTX);
	DUMPREG(E1000_PCS_LPABNP);
	DUMPREG(E1000_RXCSUM);
	DUMPREG(E1000_RLPML);
	DUMPREG(E1000_RFCTL);
	DUMPREG(E1000_MTA);
	DUMPREG(E1000_RA);
	DUMPREG(E1000_RA2);
	DUMPREG(E1000_VFTA);
	DUMPREG(E1000_VT_CTL);
	DUMPREG(E1000_CIAA);
	DUMPREG(E1000_CIAD);
	DUMPREG(E1000_VFQA0);
	DUMPREG(E1000_VFQA1);
	DUMPREG(E1000_WUC);
	DUMPREG(E1000_WUFC);
	DUMPREG(E1000_WUS);
	DUMPREG(E1000_MANC);
	DUMPREG(E1000_IPAV);
	DUMPREG(E1000_IP4AT);
	DUMPREG(E1000_IP6AT);
	DUMPREG(E1000_WUPL);
	DUMPREG(E1000_WUPM);
	DUMPREG(E1000_PBACL);
	DUMPREG(E1000_FFLT);
	DUMPREG(E1000_HOST_IF);
	DUMPREG(E1000_HIBBA);
	DUMPREG(E1000_KMRNCTRLSTA);
	DUMPREG(E1000_MANC2H);
	DUMPREG(E1000_CCMCTL);
	DUMPREG(E1000_GIOCTL);
	DUMPREG(E1000_SCCTL);

#define E1000_WCS	0x558C
	DUMPREG(E1000_WCS);
#define E1000_GCR_EXT	0x586C
	DUMPREG(E1000_GCR_EXT);
	DUMPREG(E1000_GCR);
	DUMPREG(E1000_GCR2);
	DUMPREG(E1000_FACTPS);
	DUMPREG(E1000_DCA_ID);
	DUMPREG(E1000_DCA_CTRL);
	DUMPREG(E1000_UFUSE);
	DUMPREG(E1000_FFLT_DBG);
	DUMPREG(E1000_HICR);
	DUMPREG(E1000_FWSTS);
	DUMPREG(E1000_CPUVEC);
	DUMPREG(E1000_MRQC);
	DUMPREG(E1000_SWPBS);
	DUMPREG(E1000_MBVFICR);
	DUMPREG(E1000_MBVFIMR);
	DUMPREG(E1000_VFLRE);
	DUMPREG(E1000_VFRE);
	DUMPREG(E1000_VFTE);
	DUMPREG(E1000_QDE);
	DUMPREG(E1000_DTXSWC);
	DUMPREG(E1000_WVBR);
	DUMPREG(E1000_RPLOLR);
	DUMPREG(E1000_UTA);
	DUMPREG(E1000_IOVTCL);
	DUMPREG(E1000_VMRCTL);
	DUMPREG(E1000_VMRVLAN);
	DUMPREG(E1000_VMRVM);
	DUMPREG(E1000_LVMMC);
	DUMPREG(E1000_TXSWC);
	DUMPREG(E1000_SCCRL);
	DUMPREG(E1000_BSCTRH);
	DUMPREG(E1000_MSCTRH);
	DUMPREG(E1000_RXSTMPL);
	DUMPREG(E1000_RXSTMPH);
	DUMPREG(E1000_RXSATRL);
	DUMPREG(E1000_RXSATRH);
	DUMPREG(E1000_TXSTMPL);
	DUMPREG(E1000_TXSTMPH);
	DUMPREG(E1000_TIMINCA);
	DUMPREG(E1000_TIMADJL);
	DUMPREG(E1000_TIMADJH);
	DUMPREG(E1000_TSAUXC);
	DUMPREG(E1000_SYSSTMPL);
	DUMPREG(E1000_SYSSTMPH);
	DUMPREG(E1000_PLTSTMPL);
	DUMPREG(E1000_PLTSTMPH);
	DUMPREG(E1000_RXMTRL);
	DUMPREG(E1000_RXUDP);
	DUMPREG(E1000_SYSTIMR);
	DUMPREG(E1000_TSICR);
	DUMPREG(E1000_TSIM);
	DUMPREG(E1000_DMACR);
	DUMPREG(E1000_DMCTXTH);
	DUMPREG(E1000_DMCTLX);
	DUMPREG(E1000_DMCRTRH);
	DUMPREG(E1000_DMCCNT);
	DUMPREG(E1000_FCRTC);
	DUMPREG(E1000_PCIEMISC);
	DUMPREG(E1000_PCIEERRSTS);
	DUMPREG(E1000_IPCNFG);
	DUMPREG(E1000_LTRC);
	DUMPREG(E1000_EEER);
	DUMPREG(E1000_EEE_SU);
	DUMPREG(E1000_TLPIC);
	DUMPREG(E1000_RLPIC);
	if (++col != 1)
		kprintf("\n");
	kprintf("\n");
}

static int
igb_sysctl_reg_dump(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, dump = 0;

	error = sysctl_handle_int(oidp, &dump, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (dump <= 0)
		return EINVAL;

	ifnet_serialize_all(ifp);
	igb_reg_dump(sc);
	ifnet_deserialize_all(ifp);

	return error;
}
