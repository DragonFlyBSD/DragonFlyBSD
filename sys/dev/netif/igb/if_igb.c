/*
 * Copyright (c) 2001-2011, Intel Corporation 
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
#include <net/toeplitz.h>
#include <net/toeplitz2.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>
#include <net/if_poll.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include <dev/netif/ig_hal/e1000_api.h>
#include <dev/netif/ig_hal/e1000_82575.h>
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
	IGB_DEVICE(I210_FIBER),
	IGB_DEVICE(I210_SERDES),
	IGB_DEVICE(I210_SGMII),
	IGB_DEVICE(I211_COPPER),

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
static int	igb_sysctl_intr_rate(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_msix_rate(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_rx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static void	igb_set_ring_inuse(struct igb_softc *, boolean_t);
static int	igb_get_rxring_inuse(const struct igb_softc *, boolean_t);
static int	igb_get_txring_inuse(const struct igb_softc *, boolean_t);
static void	igb_set_timer_cpuid(struct igb_softc *, boolean_t);
#ifdef IFPOLL_ENABLE
static int	igb_sysctl_npoll_rxoff(SYSCTL_HANDLER_ARGS);
static int	igb_sysctl_npoll_txoff(SYSCTL_HANDLER_ARGS);
#endif

static void	igb_vf_init_stats(struct igb_softc *);
static void	igb_reset(struct igb_softc *);
static void	igb_update_stats_counters(struct igb_softc *);
static void	igb_update_vf_stats_counters(struct igb_softc *);
static void	igb_update_link_status(struct igb_softc *);
static void	igb_init_tx_unit(struct igb_softc *);
static void	igb_init_rx_unit(struct igb_softc *);

static void	igb_set_vlan(struct igb_softc *);
static void	igb_set_multi(struct igb_softc *);
static void	igb_set_promisc(struct igb_softc *);
static void	igb_disable_promisc(struct igb_softc *);

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
static void	igb_setup_serializer(struct igb_softc *);

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
static void	igb_txeof(struct igb_tx_ring *);
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
static void	igb_teardown_intr(struct igb_softc *);
static void	igb_msix_try_alloc(struct igb_softc *);
static void	igb_msix_rx_conf(struct igb_softc *, int, int *, int);
static void	igb_msix_tx_conf(struct igb_softc *, int, int *, int);
static void	igb_msix_free(struct igb_softc *, boolean_t);
static int	igb_msix_setup(struct igb_softc *);
static void	igb_msix_teardown(struct igb_softc *, int);
static void	igb_msix_rx(void *);
static void	igb_msix_tx(void *);
static void	igb_msix_status(void *);
static void	igb_msix_rxtx(void *);

/* Management and WOL Support */
static void	igb_get_mgmt(struct igb_softc *);
static void	igb_rel_mgmt(struct igb_softc *);
static void	igb_get_hw_control(struct igb_softc *);
static void	igb_rel_hw_control(struct igb_softc *);
static void	igb_enable_wol(device_t);

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
static int	igb_fc_setting = e1000_fc_full;

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
TUNABLE_INT("hw.igb.fc_setting", &igb_fc_setting);

/* i350 specific */
TUNABLE_INT("hw.igb.eee_disabled", &igb_eee_disabled);
TUNABLE_INT("hw.igb.dma_coalesce", &igb_dma_coalesce);

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

	m->m_flags |= M_HASH;
	m->m_pkthdr.hash = toeplitz_hash(hash);
	return pi;
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
	int error = 0, ring_max;
#ifdef IFPOLL_ENABLE
	int offset, offset_def;
#endif

#ifdef notyet
	/* SYSCTL stuff */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "nvm", CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
	    igb_sysctl_nvm_info, "I", "NVM Information");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "flow_control", CTLTYPE_INT|CTLFLAG_RW,
	    adapter, 0, igb_set_flowcntl, "I", "Flow Control");
#endif

	callout_init_mp(&sc->timer);
	lwkt_serialize_init(&sc->main_serialize);

	if_initname(&sc->arpcom.ac_if, device_get_name(dev),
	    device_get_unit(dev));
	sc->dev = sc->osdep.dev = dev;

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
	switch (sc->hw.mac.type) {
	case e1000_82575:
		ring_max = IGB_MAX_RING_82575;
		break;

	case e1000_82576:
		ring_max = IGB_MAX_RING_82576;
		break;

	case e1000_82580:
		ring_max = IGB_MAX_RING_82580;
		break;

	case e1000_i350:
		ring_max = IGB_MAX_RING_I350;
		break;

	case e1000_i210:
		ring_max = IGB_MAX_RING_I210;
		break;

	case e1000_i211:
		ring_max = IGB_MAX_RING_I211;
		break;

	default:
		ring_max = IGB_MIN_RING;
		break;
	}

	sc->rx_ring_cnt = device_getenv_int(dev, "rxr", igb_rxr);
	sc->rx_ring_cnt = if_ring_count2(sc->rx_ring_cnt, ring_max);
#ifdef IGB_RSS_DEBUG
	sc->rx_ring_cnt = device_getenv_int(dev, "rxr_debug", sc->rx_ring_cnt);
#endif
	sc->rx_ring_inuse = sc->rx_ring_cnt;

	sc->tx_ring_cnt = device_getenv_int(dev, "txr", igb_txr);
	sc->tx_ring_cnt = if_ring_count2(sc->tx_ring_cnt, ring_max);
#ifdef IGB_TSS_DEBUG
	sc->tx_ring_cnt = device_getenv_int(dev, "txr_debug", sc->tx_ring_cnt);
#endif
	sc->tx_ring_inuse = sc->tx_ring_cnt;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

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

#ifdef IFPOLL_ENABLE
	/*
	 * NPOLLING RX CPU offset
	 */
	if (sc->rx_ring_cnt == ncpus2) {
		offset = 0;
	} else {
		offset_def = (sc->rx_ring_cnt * device_get_unit(dev)) % ncpus2;
		offset = device_getenv_int(dev, "npoll.rxoff", offset_def);
		if (offset >= ncpus2 ||
		    offset % sc->rx_ring_cnt != 0) {
			device_printf(dev, "invalid npoll.rxoff %d, use %d\n",
			    offset, offset_def);
			offset = offset_def;
		}
	}
	sc->rx_npoll_off = offset;

	/*
	 * NPOLLING TX CPU offset
	 */
	if (sc->tx_ring_cnt == ncpus2) {
		offset = 0;
	} else {
		offset_def = (sc->tx_ring_cnt * device_get_unit(dev)) % ncpus2;
		offset = device_getenv_int(dev, "npoll.txoff", offset_def);
		if (offset >= ncpus2 ||
		    offset % sc->tx_ring_cnt != 0) {
			device_printf(dev, "invalid npoll.txoff %d, use %d\n",
			    offset, offset_def);
			offset = offset_def;
		}
	}
	sc->tx_npoll_off = offset;
#endif

	/* Allocate interrupt */
	error = igb_alloc_intr(sc);
	if (error)
		goto failed;

	/* Setup serializers */
	igb_setup_serializer(sc);

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
		if (sc->hw.phy.media_type == e1000_media_type_copper)
			e1000_set_eee_i350(&sc->hw);
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
	igb_reset(sc);

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
	if (eeprom_data)
		sc->wol = E1000_WUFC_MAG;
	/* XXX disable WOL */
	sc->wol = 0; 

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

		if (sc->wol) {
			E1000_WRITE_REG(&sc->hw, E1000_WUC, E1000_WUC_PME_EN);
			E1000_WRITE_REG(&sc->hw, E1000_WUFC, sc->wol);
			igb_enable_wol(dev);
		}

		igb_teardown_intr(sc);

		ifnet_deserialize_all(ifp);

		ether_ifdetach(ifp);
	} else if (sc->mem_res != NULL) {
		igb_rel_hw_control(sc);
	}
	bus_generic_detach(dev);

	if (sc->sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sysctl_ctx);

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

	if (sc->wol) {
		E1000_WRITE_REG(&sc->hw, E1000_WUC, E1000_WUC_PME_EN);
		E1000_WRITE_REG(&sc->hw, E1000_WUFC, sc->wol);
		igb_enable_wol(dev);
	}

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

	igb_reset(sc);
	igb_update_link_status(sc);

	E1000_WRITE_REG(&sc->hw, E1000_VET, ETHERTYPE_VLAN);

	/* Configure for OS presence */
	igb_get_mgmt(sc);

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	/* Configured used RX/TX rings */
	igb_set_ring_inuse(sc, polling);
	ifq_set_subq_mask(&ifp->if_snd, sc->tx_ring_inuse - 1);

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
	igb_init_rx_unit(sc);

	/* Enable VLAN support */
	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		igb_set_vlan(sc);

	/* Don't lose promiscuous settings */
	igb_set_promisc(sc);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		ifsq_clr_oactive(sc->tx_rings[i].ifsq);
		ifsq_watchdog_start(&sc->tx_rings[i].tx_watchdog);
	}

	igb_set_timer_cpuid(sc, polling);
	callout_reset_bycpu(&sc->timer, hz, igb_timer, sc, sc->timer_cpuid);
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
	if (sc->hw.phy.media_type == e1000_media_type_copper)
		e1000_set_eee_i350(&sc->hw);
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

	if (!sc->link_active)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;

	switch (sc->link_speed) {
	case 10:
		ifmr->ifm_active |= IFM_10_T;
		break;

	case 100:
		/*
		 * Support for 100Mb SFP - these are Fiber 
		 * but the media type appears as serdes
		 */
		if (sc->hw.phy.media_type == e1000_media_type_internal_serdes)
			ifmr->ifm_active |= IFM_100_FX;
		else
			ifmr->ifm_active |= IFM_100_TX;
		break;

	case 1000:
		ifmr->ifm_active |= IFM_1000_T;
		break;
	}

	if (sc->link_duplex == FULL_DUPLEX)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;
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

	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_T:
		sc->hw.mac.autoneg = DO_AUTO_NEG;
		sc->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;

	case IFM_100_TX:
		sc->hw.mac.autoneg = FALSE;
		sc->hw.phy.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.mac.forced_speed_duplex = ADVERTISE_100_FULL;
		else
			sc->hw.mac.forced_speed_duplex = ADVERTISE_100_HALF;
		break;

	case IFM_10_T:
		sc->hw.mac.autoneg = FALSE;
		sc->hw.phy.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.mac.forced_speed_duplex = ADVERTISE_10_FULL;
		else
			sc->hw.mac.forced_speed_duplex = ADVERTISE_10_HALF;
		break;

	default:
		if_printf(ifp, "Unsupported media type\n");
		break;
	}

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
	uint32_t reg;

	if (sc->vf_ifp) {
		e1000_promisc_set_vf(hw, e1000_promisc_disabled);
		return;
	}
	reg = E1000_READ_REG(hw, E1000_RCTL);
	reg &= ~E1000_RCTL_UPE;
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
			const char *flowctl;

			/* Get the flow control for display */
			switch (hw->fc.current_mode) {
			case e1000_fc_rx_pause:
				flowctl = "RX";
				break;

			case e1000_fc_tx_pause:
				flowctl = "TX";
				break;

			case e1000_fc_full:
				flowctl = "Full";
				break;

			default:
				flowctl = "None";
				break;
			}

			if_printf(ifp, "Link is up %d Mbps %s, "
			    "Flow control: %s\n",
			    sc->link_speed,
			    sc->link_duplex == FULL_DUPLEX ?
			    "Full Duplex" : "Half Duplex",
			    flowctl);
		}
		sc->link_active = 1;

		ifp->if_baudrate = sc->link_speed * 1000000;
		if ((ctrl & E1000_CTRL_EXT_LINK_MODE_GMII) &&
		    (thstat & E1000_THSTAT_LINK_THROTTLE))
			if_printf(ifp, "Link: thermal downshift\n");
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
		ifsq_clr_oactive(sc->tx_rings[i].ifsq);
		ifsq_watchdog_stop(&sc->tx_rings[i].tx_watchdog);
		sc->tx_rings[i].tx_flags &= ~IGB_TXFLAG_ENABLED;
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
igb_reset(struct igb_softc *sc)
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
	fc->requested_mode = e1000_fc_default;

	/* Issue a global reset */
	e1000_reset_hw(hw);
	E1000_WRITE_REG(hw, E1000_WUC, 0);

	if (e1000_init_hw(hw) < 0)
		if_printf(ifp, "Hardware Initialization Failed\n");

	/* Setup DMA Coalescing */
	if (hw->mac.type > e1000_82580 && hw->mac.type != e1000_i211) {
		uint32_t dmac;
		uint32_t reg;

		if (sc->dma_coalesce == 0) {
			/*
			 * Disabled
			 */
			reg = E1000_READ_REG(hw, E1000_DMACR);
			reg &= ~E1000_DMACR_DMAC_EN;
			E1000_WRITE_REG(hw, E1000_DMACR, reg);
			goto reset_out;
		}

		/* Set starting thresholds */
		E1000_WRITE_REG(hw, E1000_DMCTXTH, 0);
		E1000_WRITE_REG(hw, E1000_DMCRTRH, 0);

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
		reg = ((dmac << E1000_DMACR_DMACTHR_SHIFT)
		    & E1000_DMACR_DMACTHR_MASK);
		/* Transition to L0x or L1 if available.. */
		reg |= (E1000_DMACR_DMAC_EN | E1000_DMACR_DMAC_LX_MASK);
		/* timer = value in sc->dma_coalesce in 32usec intervals */
		reg |= (sc->dma_coalesce >> 5);
		E1000_WRITE_REG(hw, E1000_DMACR, reg);

		/* Set the interval before transition */
		reg = E1000_READ_REG(hw, E1000_DMCTLX);
		reg |= 0x80000004;
		E1000_WRITE_REG(hw, E1000_DMCTLX, reg);

		/* Free space in tx packet buffer to wake from DMA coal */
		E1000_WRITE_REG(hw, E1000_DMCTXTH,
		    (20480 - (2 * sc->max_frame_size)) >> 6);

		/* Make low power state decision controlled by DMA coal */
		reg = E1000_READ_REG(hw, E1000_PCIEMISC);
		reg &= ~E1000_PCIEMISC_LX_DECISION;
		E1000_WRITE_REG(hw, E1000_PCIEMISC, reg);
		if_printf(ifp, "DMA Coalescing enabled\n");
	} else if (hw->mac.type == e1000_82580) {
		uint32_t reg = E1000_READ_REG(hw, E1000_PCIEMISC);

		E1000_WRITE_REG(hw, E1000_DMACR, 0);
		E1000_WRITE_REG(hw, E1000_PCIEMISC,
		    reg & ~E1000_PCIEMISC_LX_DECISION);
	}

reset_out:
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

	ifq_set_maxlen(&ifp->if_snd, sc->tx_rings[0].num_tx_desc - 1);
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->tx_ring_cnt);

	ifp->if_mapsubq = ifq_mapsubq_mask;
	ifq_set_subq_mask(&ifp->if_snd, 0);

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

		ifsq_watchdog_init(&txr->tx_watchdog, ifsq, igb_watchdog);
	}

	/*
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	ifmedia_init(&sc->media, IFM_IMASK, igb_media_change, igb_media_status);
	if (sc->hw.phy.media_type == e1000_media_type_fiber ||
	    sc->hw.phy.media_type == e1000_media_type_internal_serdes) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_SX | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_SX, 0, NULL);
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
			ifmedia_add(&sc->media,
			    IFM_ETHER | IFM_1000_T, 0, NULL);
		}
	}
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
}

static void
igb_add_sysctl(struct igb_softc *sc)
{
	char node[32];
	int i;

	sysctl_ctx_init(&sc->sysctl_ctx);
	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO,
	    device_get_nameunit(sc->dev), CTLFLAG_RD, 0, "");
	if (sc->sysctl_tree == NULL) {
		device_printf(sc->dev, "can't add sysctl node\n");
		return;
	}

	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "rxr", CTLFLAG_RD, &sc->rx_ring_cnt, 0, "# of RX rings");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "rxr_inuse", CTLFLAG_RD, &sc->rx_ring_inuse, 0,
	    "# of RX rings used");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "txr", CTLFLAG_RD, &sc->tx_ring_cnt, 0, "# of TX rings");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "txr_inuse", CTLFLAG_RD, &sc->tx_ring_inuse, 0,
	    "# of TX rings used");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "rxd", CTLFLAG_RD, &sc->rx_rings[0].num_rx_desc, 0,
	    "# of RX descs");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "txd", CTLFLAG_RD, &sc->tx_rings[0].num_tx_desc, 0,
	    "# of TX descs");

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		SYSCTL_ADD_PROC(&sc->sysctl_ctx,
		    SYSCTL_CHILDREN(sc->sysctl_tree),
		    OID_AUTO, "intr_rate", CTLTYPE_INT | CTLFLAG_RW,
		    sc, 0, igb_sysctl_intr_rate, "I", "interrupt rate");
	} else {
		for (i = 0; i < sc->msix_cnt; ++i) {
			struct igb_msix_data *msix = &sc->msix_data[i];

			ksnprintf(node, sizeof(node), "msix%d_rate", i);
			SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			    SYSCTL_CHILDREN(sc->sysctl_tree),
			    OID_AUTO, node, CTLTYPE_INT | CTLFLAG_RW,
			    msix, 0, igb_sysctl_msix_rate, "I",
			    msix->msix_rate_desc);
		}
	}

	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "tx_intr_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_tx_intr_nsegs, "I",
	    "# of segments per TX interrupt");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "tx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_tx_wreg_nsegs, "I",
	    "# of segments sent before write to hardware register");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "rx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, igb_sysctl_rx_wreg_nsegs, "I",
	    "# of segments received before write to hardware register");

#ifdef IFPOLL_ENABLE
	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "npoll_rxoff", CTLTYPE_INT|CTLFLAG_RW,
	    sc, 0, igb_sysctl_npoll_rxoff, "I", "NPOLLING RX cpu offset");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "npoll_txoff", CTLTYPE_INT|CTLFLAG_RW,
	    sc, 0, igb_sysctl_npoll_txoff, "I", "NPOLLING TX cpu offset");
#endif

#ifdef IGB_RSS_DEBUG
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "rss_debug", CTLFLAG_RW, &sc->rss_debug, 0,
	    "RSS debug level");
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "rx%d_pkt", i);
		SYSCTL_ADD_ULONG(&sc->sysctl_ctx,
		    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, node,
		    CTLFLAG_RW, &sc->rx_rings[i].rx_packets, "RXed packets");
	}
#endif
#ifdef IGB_TSS_DEBUG
	for  (i = 0; i < sc->tx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "tx%d_pkt", i);
		SYSCTL_ADD_ULONG(&sc->sysctl_ctx,
		    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, node,
		    CTLFLAG_RW, &sc->tx_rings[i].tx_packets, "TXed packets");
	}
#endif
}

static int
igb_alloc_rings(struct igb_softc *sc)
{
	int error, i;

	/*
	 * Create top level busdma tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
	    &sc->parent_tag);
	if (error) {
		device_printf(sc->dev, "could not create top level DMA tag\n");
		return error;
	}

	/*
	 * Allocate TX descriptor rings and buffers
	 */
	sc->tx_rings = kmalloc_cachealign(
	    sizeof(struct igb_tx_ring) * sc->tx_ring_cnt,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];

		/* Set up some basics */
		txr->sc = sc;
		txr->me = i;
		lwkt_serialize_init(&txr->tx_serialize);

		error = igb_create_tx_ring(txr);
		if (error)
			return error;
	}

	/*
	 * Allocate RX descriptor rings and buffers
	 */ 
	sc->rx_rings = kmalloc_cachealign(
	    sizeof(struct igb_rx_ring) * sc->rx_ring_cnt,
	    M_DEVBUF, M_WAITOK | M_ZERO);
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
	txr->tx_buf = kmalloc_cachealign(tsize, M_DEVBUF, M_WAITOK | M_ZERO);

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
	    NULL, NULL,		/* filter, filterarg */
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
	txr->spare_desc = IGB_TX_SPARE;
	txr->intr_nsegs = txr->num_tx_desc / 16;
	txr->wreg_nsegs = IGB_DEF_TXWREG_NSEGS;
	txr->oact_hi_desc = txr->num_tx_desc / 2;
	txr->oact_lo_desc = txr->num_tx_desc / 8;
	if (txr->oact_lo_desc > IGB_TX_OACTIVE_MAX)
		txr->oact_lo_desc = IGB_TX_OACTIVE_MAX;
	if (txr->oact_lo_desc < txr->spare_desc + IGB_TX_RESERVED)
		txr->oact_lo_desc = txr->spare_desc + IGB_TX_RESERVED;

	return 0;
}

static void
igb_free_tx_ring(struct igb_tx_ring *txr)
{
	int i;

	for (i = 0; i < txr->num_tx_desc; ++i) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[i];

		if (txbuf->m_head != NULL) {
			bus_dmamap_unload(txr->tx_tag, txbuf->map);
			m_freem(txbuf->m_head);
			txbuf->m_head = NULL;
		}
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
igb_txeof(struct igb_tx_ring *txr)
{
	struct ifnet *ifp = &txr->sc->arpcom.ac_if;
	int first, hdr, avail;

	if (txr->tx_avail == txr->num_tx_desc)
		return;

	first = txr->next_to_clean;
	hdr = *(txr->tx_hdr);

	if (first == hdr)
		return;

	avail = txr->tx_avail;
	while (first != hdr) {
		struct igb_tx_buf *txbuf = &txr->tx_buf[first];

		++avail;
		if (txbuf->m_head) {
			bus_dmamap_unload(txr->tx_tag, txbuf->map);
			m_freem(txbuf->m_head);
			txbuf->m_head = NULL;
			IFNET_STAT_INC(ifp, opackets, 1);
		}
		if (++first == txr->num_tx_desc)
			first = 0;
	}
	txr->next_to_clean = first;
	txr->tx_avail = avail;

	/*
	 * If we have a minimum free, clear OACTIVE
	 * to tell the stack that it is OK to send packets.
	 */
	if (IGB_IS_NOT_OACTIVE(txr)) {
		ifsq_clr_oactive(txr->ifsq);

		/*
		 * We have enough TX descriptors, turn off
		 * the watchdog.  We allow small amount of
		 * packets (roughly intr_nsegs) pending on
		 * the transmit ring.
		 */
		txr->tx_watchdog.wd_timer = 0;
	}
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
	rxr->rx_buf = kmalloc_cachealign(rsize, M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Create DMA tag for RX buffers
	 */
	error = bus_dma_tag_create(rxr->sc->parent_tag,
	    1, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    NULL, NULL,		/* filter, filterarg */
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

	m = m_getcl(wait ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
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
igb_init_rx_unit(struct igb_softc *sc)
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

	if (IGB_ENABLE_HWRSS(sc)) {
		uint8_t key[IGB_NRSSRK * IGB_RSSRK_SIZE];
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
		 * Configure RSS redirect table in following fashion:
	 	 * (hash & ring_cnt_mask) == rdr_table[(hash & rdr_table_mask)]
		 */
		reta_shift = IGB_RETA_SHIFT;
		if (hw->mac.type == e1000_82575)
			reta_shift = IGB_RETA_SHIFT_82575;

		r = 0;
		for (j = 0; j < IGB_NRETA; ++j) {
			uint32_t reta = 0;

			for (i = 0; i < IGB_RETA_SIZE; ++i) {
				uint32_t q;

				q = (r % sc->rx_ring_inuse) << reta_shift;
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
	int i, ncoll = 0;

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
			if (rxr->sc->hw.mac.type == e1000_i350 &&
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
			ether_input_pkt(ifp, m, pi);

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
	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		lwkt_serialize_handler_enable(&sc->main_serialize);
	} else {
		int i;

		for (i = 0; i < sc->msix_cnt; ++i) {
			lwkt_serialize_handler_enable(
			    sc->msix_data[i].msix_serialize);
		}
	}

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
	if ((sc->flags & IGB_FLAG_SHARED_INTR) == 0) {
		E1000_WRITE_REG(&sc->hw, E1000_EIMC, 0xffffffff);
		E1000_WRITE_REG(&sc->hw, E1000_EIAC, 0);
	}
	E1000_WRITE_REG(&sc->hw, E1000_IMC, 0xffffffff);
	E1000_WRITE_FLUSH(&sc->hw);

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		lwkt_serialize_handler_disable(&sc->main_serialize);
	} else {
		int i;

		for (i = 0; i < sc->msix_cnt; ++i) {
			lwkt_serialize_handler_disable(
			    sc->msix_data[i].msix_serialize);
		}
	}
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

static int
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
igb_enable_wol(device_t dev)
{
	uint16_t cap, status;
	uint8_t id;

	/* First find the capabilities pointer*/
	cap = pci_read_config(dev, PCIR_CAP_PTR, 2);

	/* Read the PM Capabilities */
	id = pci_read_config(dev, cap, 1);
	if (id != PCIY_PMG)     /* Something wrong */
		return;

	/*
	 * OK, we have the power capabilities,
	 * so now get the status register
	 */
	cap += PCIR_POWER_STATUS;
	status = pci_read_config(dev, cap, 2);
	status |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(dev, cap, status, 2);
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

	stats->tor += E1000_READ_REG(hw, E1000_TORH);
	stats->tot += E1000_READ_REG(hw, E1000_TOTH);

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

	igb_txeof(txr);
	if (!ifsq_is_empty(txr->ifsq))
		ifsq_devstart(txr->ifsq);
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
		int off;

		info->ifpi_status.status_func = igb_npoll_status;
		info->ifpi_status.serializer = &sc->main_serialize;

		txr_cnt = igb_get_txring_inuse(sc, TRUE);
		off = sc->tx_npoll_off;
		for (i = 0; i < txr_cnt; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];
			int idx = i + off;

			KKASSERT(idx < ncpus2);
			info->ifpi_tx[idx].poll_func = igb_npoll_tx;
			info->ifpi_tx[idx].arg = txr;
			info->ifpi_tx[idx].serializer = &txr->tx_serialize;
			ifsq_set_cpuid(txr->ifsq, idx);
		}

		rxr_cnt = igb_get_rxring_inuse(sc, TRUE);
		off = sc->rx_npoll_off;
		for (i = 0; i < rxr_cnt; ++i) {
			struct igb_rx_ring *rxr = &sc->rx_rings[i];
			int idx = i + off;

			KKASSERT(idx < ncpus2);
			info->ifpi_rx[idx].poll_func = igb_npoll_rx;
			info->ifpi_rx[idx].arg = rxr;
			info->ifpi_rx[idx].serializer = &rxr->rx_serialize;
		}

		if (ifp->if_flags & IFF_RUNNING) {
			if (rxr_cnt == sc->rx_ring_inuse &&
			    txr_cnt == sc->tx_ring_inuse) {
				igb_set_timer_cpuid(sc, TRUE);
				igb_disable_intr(sc);
			} else {
				igb_init(sc);
			}
		}
	} else {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];

			ifsq_set_cpuid(txr->ifsq, txr->tx_intr_cpuid);
		}

		if (ifp->if_flags & IFF_RUNNING) {
			txr_cnt = igb_get_txring_inuse(sc, FALSE);
			rxr_cnt = igb_get_rxring_inuse(sc, FALSE);

			if (rxr_cnt == sc->rx_ring_inuse &&
			    txr_cnt == sc->tx_ring_inuse) {
				igb_set_timer_cpuid(sc, FALSE);
				igb_enable_intr(sc);
			} else {
				igb_init(sc);
			}
		}
	}
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
			igb_txeof(txr);
			if (!ifsq_is_empty(txr->ifsq))
				ifsq_devstart(txr->ifsq);
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
			igb_txeof(txr);
			if (!ifsq_is_empty(txr->ifsq))
				ifsq_devstart(txr->ifsq);
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
	KASSERT(maxsegs >= txr->spare_desc, ("not enough spare TX desc\n"));
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

	if (!IGB_IS_NOT_OACTIVE(txr))
		igb_txeof(txr);

	while (!ifsq_is_empty(ifsq)) {
		if (IGB_IS_OACTIVE(txr)) {
			ifsq_set_oactive(ifsq);
			/* Set watchdog on */
			txr->tx_watchdog.wd_timer = 5;
			break;
		}

		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		if (igb_encap(txr, &m_head, &nsegs, &idx)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			continue;
		}

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
		txr->tx_watchdog.wd_timer = 5;
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

static int
igb_sysctl_intr_rate(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, intr_rate;

	intr_rate = sc->intr_rate;
	error = sysctl_handle_int(oidp, &intr_rate, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (intr_rate < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);

	sc->intr_rate = intr_rate;
	if (ifp->if_flags & IFF_RUNNING)
		igb_set_eitr(sc, 0, sc->intr_rate);

	if (bootverbose)
		if_printf(ifp, "interrupt rate set to %d/sec\n", sc->intr_rate);

	ifnet_deserialize_all(ifp);

	return 0;
}

static int
igb_sysctl_msix_rate(SYSCTL_HANDLER_ARGS)
{
	struct igb_msix_data *msix = (void *)arg1;
	struct igb_softc *sc = msix->msix_sc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, msix_rate;

	msix_rate = msix->msix_rate;
	error = sysctl_handle_int(oidp, &msix_rate, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (msix_rate < 0)
		return EINVAL;

	lwkt_serialize_enter(msix->msix_serialize);

	msix->msix_rate = msix_rate;
	if (ifp->if_flags & IFF_RUNNING)
		igb_set_eitr(sc, msix->msix_vector, msix->msix_rate);

	if (bootverbose) {
		if_printf(ifp, "%s set to %d/sec\n", msix->msix_rate_desc,
		    msix->msix_rate);
	}

	lwkt_serialize_exit(msix->msix_serialize);

	return 0;
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

	if (nsegs >= txr->num_tx_desc - txr->oact_lo_desc ||
	    nsegs >= txr->oact_hi_desc - IGB_MAX_SCATTER) {
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
		sc->rx_rings[i].wreg_nsegs =nsegs;
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
		sc->tx_rings[i].wreg_nsegs =nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

#ifdef IFPOLL_ENABLE

static int
igb_sysctl_npoll_rxoff(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, off;

	off = sc->rx_npoll_off;
	error = sysctl_handle_int(oidp, &off, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (off < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);
	if (off >= ncpus2 || off % sc->rx_ring_cnt != 0) {
		error = EINVAL;
	} else {
		error = 0;
		sc->rx_npoll_off = off;
	}
	ifnet_deserialize_all(ifp);

	return error;
}

static int
igb_sysctl_npoll_txoff(SYSCTL_HANDLER_ARGS)
{
	struct igb_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, off;

	off = sc->tx_npoll_off;
	error = sysctl_handle_int(oidp, &off, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (off < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);
	if (off >= ncpus2 || off % sc->tx_ring_cnt != 0) {
		error = EINVAL;
	} else {
		error = 0;
		sc->tx_npoll_off = off;
	}
	ifnet_deserialize_all(ifp);

	return error;
}

#endif	/* IFPOLL_ENABLE */

static void
igb_init_intr(struct igb_softc *sc)
{
	igb_set_intr_mask(sc);

	if ((sc->flags & IGB_FLAG_SHARED_INTR) == 0)
		igb_init_unshared_intr(sc);

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		igb_set_eitr(sc, 0, sc->intr_rate);
	} else {
		int i;

		for (i = 0; i < sc->msix_cnt; ++i)
			igb_set_eitr(sc, i, sc->msix_data[i].msix_rate);
	}
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
				(rxr->rx_intr_bit | E1000_IVAR_VALID) << 16;
			} else {
				ivar &= 0xffffff00;
				ivar |=
				(rxr->rx_intr_bit | E1000_IVAR_VALID);
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
				(txr->tx_intr_bit | E1000_IVAR_VALID) << 24;
			} else {
				ivar &= 0xffff00ff;
				ivar |=
				(txr->tx_intr_bit | E1000_IVAR_VALID) << 8;
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
			ivar = (sc->sts_intr_bit | E1000_IVAR_VALID) << 8;
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
				(rxr->rx_intr_bit | E1000_IVAR_VALID);
			} else {
				ivar &= 0xff00ffff;
				ivar |=
				(rxr->rx_intr_bit | E1000_IVAR_VALID) << 16;
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
				(txr->tx_intr_bit | E1000_IVAR_VALID) << 8;
			} else {
				ivar &= 0x00ffffff;
				ivar |=
				(txr->tx_intr_bit | E1000_IVAR_VALID) << 24;
			}
			E1000_WRITE_REG_ARRAY(hw, E1000_IVAR0, index, ivar);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
			ivar = (sc->sts_intr_bit | E1000_IVAR_VALID) << 8;
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
	int error;

	if (sc->intr_type == PCI_INTR_TYPE_MSIX)
		return igb_msix_setup(sc);

	error = bus_setup_intr(sc->dev, sc->intr_res, INTR_MPSAFE,
	    (sc->flags & IGB_FLAG_SHARED_INTR) ? igb_intr_shared : igb_intr,
	    sc, &sc->intr_tag, &sc->main_serialize);
	if (error) {
		device_printf(sc->dev, "Failed to register interrupt handler");
		return error;
	}
	return 0;
}

static void
igb_set_txintr_mask(struct igb_tx_ring *txr, int *intr_bit0, int intr_bitmax)
{
	if (txr->sc->hw.mac.type == e1000_82575) {
		txr->tx_intr_bit = 0;	/* unused */
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
		int intr_bit = *intr_bit0;

		txr->tx_intr_bit = intr_bit % intr_bitmax;
		txr->tx_intr_mask = 1 << txr->tx_intr_bit;

		*intr_bit0 = intr_bit + 1;
	}
}

static void
igb_set_rxintr_mask(struct igb_rx_ring *rxr, int *intr_bit0, int intr_bitmax)
{
	if (rxr->sc->hw.mac.type == e1000_82575) {
		rxr->rx_intr_bit = 0;	/* unused */
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
		int intr_bit = *intr_bit0;

		rxr->rx_intr_bit = intr_bit % intr_bitmax;
		rxr->rx_intr_mask = 1 << rxr->rx_intr_bit;

		*intr_bit0 = intr_bit + 1;
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
	int i, intr_bit, intr_bitmax;
	u_int intr_flags;

	igb_msix_try_alloc(sc);
	if (sc->intr_type == PCI_INTR_TYPE_MSIX)
		goto done;

	/*
	 * Allocate MSI/legacy interrupt resource
	 */
	sc->intr_type = pci_alloc_1intr(sc->dev, igb_msi_enable,
	    &sc->intr_rid, &intr_flags);

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

	sc->intr_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->intr_rid, intr_flags);
	if (sc->intr_res == NULL) {
		device_printf(sc->dev, "Unable to allocate bus resource: "
		    "interrupt\n");
		return ENXIO;
	}

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		sc->tx_rings[i].tx_intr_cpuid = rman_get_cpuid(sc->intr_res);

	/*
	 * Setup MSI/legacy interrupt mask
	 */
	switch (sc->hw.mac.type) {
	case e1000_82575:
		intr_bitmax = IGB_MAX_TXRXINT_82575;
		break;

	case e1000_82576:
		intr_bitmax = IGB_MAX_TXRXINT_82576;
		break;

	case e1000_82580:
		intr_bitmax = IGB_MAX_TXRXINT_82580;
		break;

	case e1000_i350:
		intr_bitmax = IGB_MAX_TXRXINT_I350;
		break;

	case e1000_i210:
		intr_bitmax = IGB_MAX_TXRXINT_I210;
		break;

	case e1000_i211:
		intr_bitmax = IGB_MAX_TXRXINT_I211;
		break;

	default:
		intr_bitmax = IGB_MIN_TXRXINT;
		break;
	}
	intr_bit = 0;
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		igb_set_txintr_mask(&sc->tx_rings[i], &intr_bit, intr_bitmax);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		igb_set_rxintr_mask(&sc->rx_rings[i], &intr_bit, intr_bitmax);
	sc->sts_intr_bit = 0;
	sc->sts_intr_mask = E1000_EICR_OTHER;

	/* Initialize interrupt rate */
	sc->intr_rate = IGB_INTR_RATE;
done:
	igb_set_ring_inuse(sc, FALSE);
	igb_set_intr_mask(sc);
	return 0;
}

static void
igb_free_intr(struct igb_softc *sc)
{
	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		if (sc->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ, sc->intr_rid,
			    sc->intr_res);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->dev);
	} else {
		igb_msix_free(sc, TRUE);
	}
}

static void
igb_teardown_intr(struct igb_softc *sc)
{
	if (sc->intr_type != PCI_INTR_TYPE_MSIX)
		bus_teardown_intr(sc->dev, sc->intr_res, sc->intr_tag);
	else
		igb_msix_teardown(sc, sc->msix_cnt);
}

static void
igb_msix_try_alloc(struct igb_softc *sc)
{
	int msix_enable, msix_cnt, msix_cnt2, alloc_cnt;
	int i, x, error;
	int offset, offset_def;
	struct igb_msix_data *msix;
	boolean_t aggregate, setup = FALSE;

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
		/* One MSI-X model does not make sense */
		return;
	}

	i = 0;
	while ((1 << (i + 1)) <= msix_cnt)
		++i;
	msix_cnt2 = 1 << i;

	if (bootverbose) {
		device_printf(sc->dev, "MSI-X count %d/%d\n",
		    msix_cnt2, msix_cnt);
	}

	KKASSERT(msix_cnt2 <= msix_cnt);
	if (msix_cnt == msix_cnt2) {
		/* We need at least one MSI-X for link status */
		msix_cnt2 >>= 1;
		if (msix_cnt2 <= 1) {
			/* One MSI-X for RX/TX does not make sense */
			device_printf(sc->dev, "not enough MSI-X for TX/RX, "
			    "MSI-X count %d/%d\n", msix_cnt2, msix_cnt);
			return;
		}
		KKASSERT(msix_cnt > msix_cnt2);

		if (bootverbose) {
			device_printf(sc->dev, "MSI-X count fixup %d/%d\n",
			    msix_cnt2, msix_cnt);
		}
	}

	sc->rx_ring_msix = sc->rx_ring_cnt;
	if (sc->rx_ring_msix > msix_cnt2)
		sc->rx_ring_msix = msix_cnt2;

	sc->tx_ring_msix = sc->tx_ring_cnt;
	if (sc->tx_ring_msix > msix_cnt2)
		sc->tx_ring_msix = msix_cnt2;

	if (msix_cnt >= sc->tx_ring_msix + sc->rx_ring_msix + 1) {
		/*
		 * Independent TX/RX MSI-X
		 */
		aggregate = FALSE;
		if (bootverbose)
			device_printf(sc->dev, "independent TX/RX MSI-X\n");
		alloc_cnt = sc->tx_ring_msix + sc->rx_ring_msix;
	} else {
		/*
		 * Aggregate TX/RX MSI-X
		 */
		aggregate = TRUE;
		if (bootverbose)
			device_printf(sc->dev, "aggregate TX/RX MSI-X\n");
		alloc_cnt = msix_cnt2;
		if (alloc_cnt > ncpus2)
			alloc_cnt = ncpus2;
		if (sc->rx_ring_msix > alloc_cnt)
			sc->rx_ring_msix = alloc_cnt;
		if (sc->tx_ring_msix > alloc_cnt)
			sc->tx_ring_msix = alloc_cnt;
	}
	++alloc_cnt;	/* For link status */

	if (bootverbose) {
		device_printf(sc->dev, "MSI-X alloc %d, "
		    "RX ring %d, TX ring %d\n", alloc_cnt,
		    sc->rx_ring_msix, sc->tx_ring_msix);
	}

	sc->msix_mem_rid = PCIR_BAR(IGB_MSIX_BAR);
	sc->msix_mem_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->msix_mem_rid, RF_ACTIVE);
	if (sc->msix_mem_res == NULL) {
		device_printf(sc->dev, "Unable to map MSI-X table\n");
		return;
	}

	sc->msix_cnt = alloc_cnt;
	sc->msix_data = kmalloc_cachealign(
	    sizeof(struct igb_msix_data) * sc->msix_cnt,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (x = 0; x < sc->msix_cnt; ++x) {
		msix = &sc->msix_data[x];

		lwkt_serialize_init(&msix->msix_serialize0);
		msix->msix_sc = sc;
		msix->msix_rid = -1;
		msix->msix_vector = x;
		msix->msix_mask = 1 << msix->msix_vector;
		msix->msix_rate = IGB_INTR_RATE;
	}

	x = 0;
	if (!aggregate) {
		/*
		 * RX rings
		 */
		if (sc->rx_ring_msix == ncpus2) {
			offset = 0;
		} else {
			offset_def = (sc->rx_ring_msix *
			    device_get_unit(sc->dev)) % ncpus2;

			offset = device_getenv_int(sc->dev,
			    "msix.rxoff", offset_def);
			if (offset >= ncpus2 ||
			    offset % sc->rx_ring_msix != 0) {
				device_printf(sc->dev,
				    "invalid msix.rxoff %d, use %d\n",
				    offset, offset_def);
				offset = offset_def;
			}
		}
		igb_msix_rx_conf(sc, 0, &x, offset);

		/*
		 * TX rings
		 */
		if (sc->tx_ring_msix == ncpus2) {
			offset = 0;
		} else {
			offset_def = (sc->tx_ring_msix *
			    device_get_unit(sc->dev)) % ncpus2;

			offset = device_getenv_int(sc->dev,
			    "msix.txoff", offset_def);
			if (offset >= ncpus2 ||
			    offset % sc->tx_ring_msix != 0) {
				device_printf(sc->dev,
				    "invalid msix.txoff %d, use %d\n",
				    offset, offset_def);
				offset = offset_def;
			}
		}
		igb_msix_tx_conf(sc, 0, &x, offset);
	} else {
		int ring_agg, ring_max;

		ring_agg = sc->rx_ring_msix;
		if (ring_agg > sc->tx_ring_msix)
			ring_agg = sc->tx_ring_msix;

		ring_max = sc->rx_ring_msix;
		if (ring_max < sc->tx_ring_msix)
			ring_max = sc->tx_ring_msix;

		if (ring_max == ncpus2) {
			offset = 0;
		} else {
			offset_def = (ring_max * device_get_unit(sc->dev)) %
			    ncpus2;

			offset = device_getenv_int(sc->dev, "msix.off",
			    offset_def);
			if (offset >= ncpus2 || offset % ring_max != 0) {
				device_printf(sc->dev,
				    "invalid msix.off %d, use %d\n",
				    offset, offset_def);
				offset = offset_def;
			}
		}

		for (i = 0; i < ring_agg; ++i) {
			struct igb_tx_ring *txr = &sc->tx_rings[i];
			struct igb_rx_ring *rxr = &sc->rx_rings[i];

			KKASSERT(x < sc->msix_cnt);
			msix = &sc->msix_data[x++];

			txr->tx_intr_bit = msix->msix_vector;
			txr->tx_intr_mask = msix->msix_mask;
			rxr->rx_intr_bit = msix->msix_vector;
			rxr->rx_intr_mask = msix->msix_mask;

			msix->msix_serialize = &msix->msix_serialize0;
			msix->msix_func = igb_msix_rxtx;
			msix->msix_arg = msix;
			msix->msix_rx = rxr;
			msix->msix_tx = txr;

			msix->msix_cpuid = i + offset;
			KKASSERT(msix->msix_cpuid < ncpus2);
			txr->tx_intr_cpuid = msix->msix_cpuid;

			ksnprintf(msix->msix_desc, sizeof(msix->msix_desc),
			    "%s rxtx%d", device_get_nameunit(sc->dev), i);
			msix->msix_rate = IGB_MSIX_RX_RATE;
			ksnprintf(msix->msix_rate_desc,
			    sizeof(msix->msix_rate_desc),
			    "RXTX%d interrupt rate", i);
		}

		if (ring_agg != ring_max) {
			if (ring_max == sc->tx_ring_msix)
				igb_msix_tx_conf(sc, i, &x, offset);
			else
				igb_msix_rx_conf(sc, i, &x, offset);
		}
	}

	/*
	 * Link status
	 */
	KKASSERT(x < sc->msix_cnt);
	msix = &sc->msix_data[x++];
	sc->sts_intr_bit = msix->msix_vector;
	sc->sts_intr_mask = msix->msix_mask;

	msix->msix_serialize = &sc->main_serialize;
	msix->msix_func = igb_msix_status;
	msix->msix_arg = sc;
	msix->msix_cpuid = 0;
	ksnprintf(msix->msix_desc, sizeof(msix->msix_desc), "%s sts",
	    device_get_nameunit(sc->dev));
	ksnprintf(msix->msix_rate_desc, sizeof(msix->msix_rate_desc),
	    "status interrupt rate");

	KKASSERT(x == sc->msix_cnt);

	error = pci_setup_msix(sc->dev);
	if (error) {
		device_printf(sc->dev, "Setup MSI-X failed\n");
		goto back;
	}
	setup = TRUE;

	for (i = 0; i < sc->msix_cnt; ++i) {
		msix = &sc->msix_data[i];

		error = pci_alloc_msix_vector(sc->dev, msix->msix_vector,
		    &msix->msix_rid, msix->msix_cpuid);
		if (error) {
			device_printf(sc->dev,
			    "Unable to allocate MSI-X %d on cpu%d\n",
			    msix->msix_vector, msix->msix_cpuid);
			goto back;
		}

		msix->msix_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
		    &msix->msix_rid, RF_ACTIVE);
		if (msix->msix_res == NULL) {
			device_printf(sc->dev,
			    "Unable to allocate MSI-X %d resource\n",
			    msix->msix_vector);
			error = ENOMEM;
			goto back;
		}
	}

	pci_enable_msix(sc->dev);
	sc->intr_type = PCI_INTR_TYPE_MSIX;
back:
	if (error)
		igb_msix_free(sc, setup);
}

static void
igb_msix_free(struct igb_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->msix_cnt > 1);

	for (i = 0; i < sc->msix_cnt; ++i) {
		struct igb_msix_data *msix = &sc->msix_data[i];

		if (msix->msix_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    msix->msix_rid, msix->msix_res);
		}
		if (msix->msix_rid >= 0)
			pci_release_msix_vector(sc->dev, msix->msix_rid);
	}
	if (setup)
		pci_teardown_msix(sc->dev);

	sc->msix_cnt = 0;
	kfree(sc->msix_data, M_DEVBUF);
	sc->msix_data = NULL;
}

static int
igb_msix_setup(struct igb_softc *sc)
{
	int i;

	for (i = 0; i < sc->msix_cnt; ++i) {
		struct igb_msix_data *msix = &sc->msix_data[i];
		int error;

		error = bus_setup_intr_descr(sc->dev, msix->msix_res,
		    INTR_MPSAFE, msix->msix_func, msix->msix_arg,
		    &msix->msix_handle, msix->msix_serialize, msix->msix_desc);
		if (error) {
			device_printf(sc->dev, "could not set up %s "
			    "interrupt handler.\n", msix->msix_desc);
			igb_msix_teardown(sc, i);
			return error;
		}
	}
	return 0;
}

static void
igb_msix_teardown(struct igb_softc *sc, int msix_cnt)
{
	int i;

	for (i = 0; i < msix_cnt; ++i) {
		struct igb_msix_data *msix = &sc->msix_data[i];

		bus_teardown_intr(sc->dev, msix->msix_res, msix->msix_handle);
	}
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

	igb_txeof(txr);
	if (!ifsq_is_empty(txr->ifsq))
		ifsq_devstart(txr->ifsq);

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
igb_setup_serializer(struct igb_softc *sc)
{
	const struct igb_msix_data *msix;
	int i, j;

	/*
	 * Allocate serializer array
	 */

	/* Main + TX + RX */
	sc->serialize_cnt = 1 + sc->tx_ring_cnt + sc->rx_ring_cnt;

	/* Aggregate TX/RX MSI-X */
	for (i = 0; i < sc->msix_cnt; ++i) {
		msix = &sc->msix_data[i];
		if (msix->msix_serialize == &msix->msix_serialize0)
			sc->serialize_cnt++;
	}

	sc->serializes =
	    kmalloc(sc->serialize_cnt * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializers
	 *
	 * NOTE: Order is critical
	 */

	i = 0;

	KKASSERT(i < sc->serialize_cnt);
	sc->serializes[i++] = &sc->main_serialize;

	for (j = 0; j < sc->msix_cnt; ++j) {
		msix = &sc->msix_data[j];
		if (msix->msix_serialize == &msix->msix_serialize0) {
			KKASSERT(i < sc->serialize_cnt);
			sc->serializes[i++] = msix->msix_serialize;
		}
	}

	for (j = 0; j < sc->tx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->tx_rings[j].tx_serialize;
	}

	for (j = 0; j < sc->rx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->rx_rings[j].rx_serialize;
	}

	KKASSERT(i == sc->serialize_cnt);
}

static void
igb_msix_rx_conf(struct igb_softc *sc, int i, int *x0, int offset)
{
	int x = *x0;

	for (; i < sc->rx_ring_msix; ++i) {
		struct igb_rx_ring *rxr = &sc->rx_rings[i];
		struct igb_msix_data *msix;

		KKASSERT(x < sc->msix_cnt);
		msix = &sc->msix_data[x++];

		rxr->rx_intr_bit = msix->msix_vector;
		rxr->rx_intr_mask = msix->msix_mask;

		msix->msix_serialize = &rxr->rx_serialize;
		msix->msix_func = igb_msix_rx;
		msix->msix_arg = rxr;

		msix->msix_cpuid = i + offset;
		KKASSERT(msix->msix_cpuid < ncpus2);

		ksnprintf(msix->msix_desc, sizeof(msix->msix_desc), "%s rx%d",
		    device_get_nameunit(sc->dev), i);

		msix->msix_rate = IGB_MSIX_RX_RATE;
		ksnprintf(msix->msix_rate_desc, sizeof(msix->msix_rate_desc),
		    "RX%d interrupt rate", i);
	}
	*x0 = x;
}

static void
igb_msix_tx_conf(struct igb_softc *sc, int i, int *x0, int offset)
{
	int x = *x0;

	for (; i < sc->tx_ring_msix; ++i) {
		struct igb_tx_ring *txr = &sc->tx_rings[i];
		struct igb_msix_data *msix;

		KKASSERT(x < sc->msix_cnt);
		msix = &sc->msix_data[x++];

		txr->tx_intr_bit = msix->msix_vector;
		txr->tx_intr_mask = msix->msix_mask;

		msix->msix_serialize = &txr->tx_serialize;
		msix->msix_func = igb_msix_tx;
		msix->msix_arg = txr;

		msix->msix_cpuid = i + offset;
		KKASSERT(msix->msix_cpuid < ncpus2);
		txr->tx_intr_cpuid = msix->msix_cpuid;

		ksnprintf(msix->msix_desc, sizeof(msix->msix_desc), "%s tx%d",
		    device_get_nameunit(sc->dev), i);

		msix->msix_rate = IGB_MSIX_TX_RATE;
		ksnprintf(msix->msix_rate_desc, sizeof(msix->msix_rate_desc),
		    "TX%d interrupt rate", i);
	}
	*x0 = x;
}

static void
igb_msix_rxtx(void *arg)
{
	struct igb_msix_data *msix = arg;
	struct igb_rx_ring *rxr = msix->msix_rx;
	struct igb_tx_ring *txr = msix->msix_tx;

	ASSERT_SERIALIZED(&msix->msix_serialize0);

	lwkt_serialize_enter(&rxr->rx_serialize);
	igb_rxeof(rxr, -1);
	lwkt_serialize_exit(&rxr->rx_serialize);

	lwkt_serialize_enter(&txr->tx_serialize);
	igb_txeof(txr);
	if (!ifsq_is_empty(txr->ifsq))
		ifsq_devstart(txr->ifsq);
	lwkt_serialize_exit(&txr->tx_serialize);

	E1000_WRITE_REG(&msix->msix_sc->hw, E1000_EIMS, msix->msix_mask);
}

static void
igb_set_timer_cpuid(struct igb_softc *sc, boolean_t polling)
{
	if (polling || sc->intr_type == PCI_INTR_TYPE_MSIX)
		sc->timer_cpuid = 0; /* XXX fixed */
	else
		sc->timer_cpuid = rman_get_cpuid(sc->intr_res);
}
