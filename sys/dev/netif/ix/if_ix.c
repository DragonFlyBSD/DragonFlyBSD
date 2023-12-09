/*
 * Copyright (c) 2001-2017, Intel Corporation
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
#include "opt_ix.h"

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
#include <sys/taskqueue.h>

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

#include <dev/netif/ix/ixgbe_common.h>
#include <dev/netif/ix/ixgbe_api.h>
#include <dev/netif/ix/if_ix.h>

#define IX_IFM_DEFAULT		(IFM_ETHER | IFM_AUTO)

#ifdef IX_RSS_DEBUG
#define IX_RSS_DPRINTF(sc, lvl, fmt, ...) \
do { \
	if (sc->rss_debug >= lvl) \
		if_printf(&sc->arpcom.ac_if, fmt, __VA_ARGS__); \
} while (0)
#else	/* !IX_RSS_DEBUG */
#define IX_RSS_DPRINTF(sc, lvl, fmt, ...)	((void)0)
#endif	/* IX_RSS_DEBUG */

#define IX_NAME			"Intel(R) PRO/10GbE "
#define IX_DEVICE(id) \
	{ IXGBE_INTEL_VENDOR_ID, IXGBE_DEV_ID_##id, IX_NAME #id }
#define IX_DEVICE_NULL		{ 0, 0, NULL }

static struct ix_device {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} ix_devices[] = {
	IX_DEVICE(82598AF_DUAL_PORT),
	IX_DEVICE(82598AF_SINGLE_PORT),
	IX_DEVICE(82598EB_CX4),
	IX_DEVICE(82598AT),
	IX_DEVICE(82598AT2),
	IX_DEVICE(82598),
	IX_DEVICE(82598_DA_DUAL_PORT),
	IX_DEVICE(82598_CX4_DUAL_PORT),
	IX_DEVICE(82598EB_XF_LR),
	IX_DEVICE(82598_SR_DUAL_PORT_EM),
	IX_DEVICE(82598EB_SFP_LOM),
	IX_DEVICE(82599_KX4),
	IX_DEVICE(82599_KX4_MEZZ),
	IX_DEVICE(82599_SFP),
	IX_DEVICE(82599_XAUI_LOM),
	IX_DEVICE(82599_CX4),
	IX_DEVICE(82599_T3_LOM),
	IX_DEVICE(82599_COMBO_BACKPLANE),
	IX_DEVICE(82599_BACKPLANE_FCOE),
	IX_DEVICE(82599_SFP_SF2),
	IX_DEVICE(82599_SFP_FCOE),
	IX_DEVICE(82599EN_SFP),
	IX_DEVICE(82599_SFP_SF_QP),
	IX_DEVICE(82599_QSFP_SF_QP),
	IX_DEVICE(X540T),
	IX_DEVICE(X540T1),
	IX_DEVICE(X550T),
	IX_DEVICE(X550T1),
	IX_DEVICE(X550EM_X_KR),
	IX_DEVICE(X550EM_X_KX4),
	IX_DEVICE(X550EM_X_10G_T),
	IX_DEVICE(X550EM_X_1G_T),
	IX_DEVICE(X550EM_X_SFP),
	IX_DEVICE(X550EM_A_KR),
	IX_DEVICE(X550EM_A_KR_L),
	IX_DEVICE(X550EM_A_SFP),
	IX_DEVICE(X550EM_A_SFP_N),
	IX_DEVICE(X550EM_A_SGMII),
	IX_DEVICE(X550EM_A_SGMII_L),
	IX_DEVICE(X550EM_A_10G_T),
	IX_DEVICE(X550EM_A_1G_T),
	IX_DEVICE(X550EM_A_1G_T_L),
#if 0
	IX_DEVICE(X540_BYPASS),
	IX_DEVICE(82599_BYPASS),
#endif

	/* required last entry */
	IX_DEVICE_NULL
};

static int	ix_probe(device_t);
static int	ix_attach(device_t);
static int	ix_detach(device_t);
static int	ix_shutdown(device_t);

static void	ix_serialize(struct ifnet *, enum ifnet_serialize);
static void	ix_deserialize(struct ifnet *, enum ifnet_serialize);
static int	ix_tryserialize(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
static void	ix_serialize_assert(struct ifnet *, enum ifnet_serialize,
		    boolean_t);
#endif
static void	ix_start(struct ifnet *, struct ifaltq_subque *);
static void	ix_watchdog(struct ifaltq_subque *);
static int	ix_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	ix_init(void *);
static void	ix_stop(struct ix_softc *);
static void	ix_media_status(struct ifnet *, struct ifmediareq *);
static int	ix_media_change(struct ifnet *);
static void	ix_timer(void *);
static void	ix_fw_timer(void *);
#ifdef IFPOLL_ENABLE
static void	ix_npoll(struct ifnet *, struct ifpoll_info *);
static void	ix_npoll_rx(struct ifnet *, void *, int);
static void	ix_npoll_rx_direct(struct ifnet *, void *, int);
static void	ix_npoll_tx(struct ifnet *, void *, int);
static void	ix_npoll_status(struct ifnet *);
#endif

static void	ix_add_sysctl(struct ix_softc *);
static void	ix_add_intr_rate_sysctl(struct ix_softc *, int,
		    const char *, int (*)(SYSCTL_HANDLER_ARGS), const char *);
static int	ix_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_tx_nmbuf(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_rx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_txd(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_rxd(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_intr_rate(SYSCTL_HANDLER_ARGS, int);
static int	ix_sysctl_rxtx_intr_rate(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_rx_intr_rate(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_tx_intr_rate(SYSCTL_HANDLER_ARGS);
static int	ix_sysctl_sts_intr_rate(SYSCTL_HANDLER_ARGS);
#if 0
static void     ix_add_hw_stats(struct ix_softc *);
#endif

static void	ix_watchdog_reset(struct ix_softc *);
static void	ix_watchdog_task(void *, int);
static void	ix_sync_netisr(struct ix_softc *, int);
static void	ix_slot_info(struct ix_softc *);
static int	ix_alloc_rings(struct ix_softc *);
static void	ix_free_rings(struct ix_softc *);
static void	ix_setup_ifp(struct ix_softc *);
static void	ix_setup_serialize(struct ix_softc *);
static void	ix_setup_caps(struct ix_softc *);
static void	ix_set_ring_inuse(struct ix_softc *, boolean_t);
static int	ix_get_timer_cpuid(const struct ix_softc *, boolean_t);
static void	ix_update_stats(struct ix_softc *);
static void	ix_detect_fanfail(struct ix_softc *, uint32_t, boolean_t);

static void	ix_set_promisc(struct ix_softc *);
static void	ix_set_multi(struct ix_softc *);
static void	ix_set_vlan(struct ix_softc *);
static uint8_t	*ix_mc_array_itr(struct ixgbe_hw *, uint8_t **, uint32_t *);
static enum ixgbe_fc_mode ix_ifmedia2fc(int);
static const char *ix_ifmedia2str(int);
static const char *ix_fc2str(enum ixgbe_fc_mode);

static void	ix_get_txring_cnt(const struct ix_softc *, int *, int *);
static int	ix_get_txring_inuse(const struct ix_softc *, boolean_t);
static void	ix_init_tx_ring(struct ix_tx_ring *);
static void	ix_free_tx_ring(struct ix_tx_ring *);
static int	ix_create_tx_ring(struct ix_tx_ring *);
static void	ix_destroy_tx_ring(struct ix_tx_ring *, int);
static void	ix_init_tx_unit(struct ix_softc *);
static int	ix_encap(struct ix_tx_ring *, struct mbuf **,
		    uint16_t *, int *);
static int	ix_tx_ctx_setup(struct ix_tx_ring *,
		    const struct mbuf *, uint32_t *, uint32_t *);
static int	ix_tso_ctx_setup(struct ix_tx_ring *,
		    const struct mbuf *, uint32_t *, uint32_t *);
static void	ix_txeof(struct ix_tx_ring *, int);
static void	ix_txgc(struct ix_tx_ring *);
static void	ix_txgc_timer(void *);

static void	ix_get_rxring_cnt(const struct ix_softc *, int *, int *);
static int	ix_get_rxring_inuse(const struct ix_softc *, boolean_t);
static int	ix_init_rx_ring(struct ix_rx_ring *);
static void	ix_free_rx_ring(struct ix_rx_ring *);
static int	ix_create_rx_ring(struct ix_rx_ring *);
static void	ix_destroy_rx_ring(struct ix_rx_ring *, int);
static void	ix_init_rx_unit(struct ix_softc *, boolean_t);
#if 0
static void	ix_setup_hw_rsc(struct ix_rx_ring *);
#endif
static int	ix_newbuf(struct ix_rx_ring *, int, boolean_t);
static void	ix_rxeof(struct ix_rx_ring *, int);
static void	ix_rx_discard(struct ix_rx_ring *, int, boolean_t);
static void	ix_enable_rx_drop(struct ix_softc *);
static void	ix_disable_rx_drop(struct ix_softc *);

static void	ix_config_gpie(struct ix_softc *);
static void	ix_alloc_msix(struct ix_softc *);
static void	ix_free_msix(struct ix_softc *, boolean_t);
static void	ix_setup_msix_eims(const struct ix_softc *, int,
		    uint32_t *, uint32_t *);
static int	ix_alloc_intr(struct ix_softc *);
static void	ix_free_intr(struct ix_softc *);
static int	ix_setup_intr(struct ix_softc *);
static void	ix_teardown_intr(struct ix_softc *, int);
static void	ix_enable_intr(struct ix_softc *);
static void	ix_disable_intr(struct ix_softc *);
static void	ix_set_ivar(struct ix_softc *, uint8_t, uint8_t, int8_t);
static void	ix_set_eitr(struct ix_softc *, int, int);
static void	ix_intr_status(struct ix_softc *, uint32_t);
static void	ix_intr_82598(void *);
static void	ix_intr(void *);
static void	ix_msix_rxtx(void *);
static void	ix_msix_rx(void *);
static void	ix_msix_tx(void *);
static void	ix_msix_status(void *);

static void	ix_config_link(struct ix_softc *);
static boolean_t ix_sfp_probe(struct ix_softc *);
static boolean_t ix_is_sfp(struct ixgbe_hw *);
static void	ix_update_link_status(struct ix_softc *);
static void	ix_handle_link(struct ix_softc *);
static void	ix_handle_mod(struct ix_softc *);
static void	ix_handle_msf(struct ix_softc *);
static void	ix_handle_phy(struct ix_softc *);
static int	ix_powerdown(struct ix_softc *);
static void	ix_config_flowctrl(struct ix_softc *);
static void	ix_config_dmac(struct ix_softc *);
static void	ix_init_media(struct ix_softc *);

static void	ix_serialize_skipmain(struct ix_softc *);
static void	ix_deserialize_skipmain(struct ix_softc *);

static device_method_t ix_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ix_probe),
	DEVMETHOD(device_attach,	ix_attach),
	DEVMETHOD(device_detach,	ix_detach),
	DEVMETHOD(device_shutdown,	ix_shutdown),
	DEVMETHOD_END
};

static driver_t ix_driver = {
	"ix",
	ix_methods,
	sizeof(struct ix_softc)
};

static devclass_t ix_devclass;

DECLARE_DUMMY_MODULE(if_ix);
DRIVER_MODULE(if_ix, pci, ix_driver, ix_devclass, NULL, NULL);

static int	ix_msi_enable = 1;
static int	ix_msix_enable = 1;
static int	ix_rxr = 0;
static int	ix_txr = 0;
static int	ix_txd = IX_PERF_TXD;
static int	ix_rxd = IX_PERF_RXD;
static int	ix_unsupported_sfp = 0;
static int	ix_direct_input = 1;

static char	ix_flowctrl[IFM_ETH_FC_STRLEN] = IFM_ETH_FC_NONE;

TUNABLE_INT("hw.ix.msi.enable", &ix_msi_enable);
TUNABLE_INT("hw.ix.msix.enable", &ix_msix_enable);
TUNABLE_INT("hw.ix.rxr", &ix_rxr);
TUNABLE_INT("hw.ix.txr", &ix_txr);
TUNABLE_INT("hw.ix.txd", &ix_txd);
TUNABLE_INT("hw.ix.rxd", &ix_rxd);
TUNABLE_INT("hw.ix.unsupported_sfp", &ix_unsupported_sfp);
TUNABLE_STR("hw.ix.flow_ctrl", ix_flowctrl, sizeof(ix_flowctrl));
TUNABLE_INT("hw.ix.direct_input", &ix_direct_input);

/*
 * Smart speed setting, default to on.  This only works
 * as a compile option right now as its during attach,
 * set this to 'ixgbe_smart_speed_off' to disable.
 */
static const enum ixgbe_smart_speed ix_smart_speed =
    ixgbe_smart_speed_on;

static __inline void
ix_try_txgc(struct ix_tx_ring *txr, int8_t dec)
{

	if (txr->tx_running > 0) {
		txr->tx_running -= dec;
		if (txr->tx_running <= 0 && txr->tx_nmbuf &&
		    txr->tx_avail < txr->tx_ndesc &&
		    txr->tx_avail + txr->tx_intr_nsegs > txr->tx_ndesc)
			ix_txgc(txr);
	}
}

static void
ix_txgc_timer(void *xtxr)
{
	struct ix_tx_ring *txr = xtxr;
	struct ifnet *ifp = &txr->tx_sc->arpcom.ac_if;

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
	ix_try_txgc(txr, IX_TX_RUNNING_DEC);

	lwkt_serialize_exit(&txr->tx_serialize);
done:
	callout_reset(&txr->tx_gc_timer, 1, ix_txgc_timer, txr);
}

static __inline void
ix_tx_intr(struct ix_tx_ring *txr, int hdr)
{

	ix_txeof(txr, hdr);
	if (!ifsq_is_empty(txr->tx_ifsq))
		ifsq_devstart(txr->tx_ifsq);
}

static __inline void
ix_free_txbuf(struct ix_tx_ring *txr, struct ix_tx_buf *txbuf)
{

	KKASSERT(txbuf->m_head != NULL);
	KKASSERT(txr->tx_nmbuf > 0);
	txr->tx_nmbuf--;

	bus_dmamap_unload(txr->tx_tag, txbuf->map);
	m_freem(txbuf->m_head);
	txbuf->m_head = NULL;
}

static int
ix_probe(device_t dev)
{
	const struct ix_device *d;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = ix_devices; d->desc != NULL; ++d) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			return 0;
		}
	}
	return ENXIO;
}

static void
ix_get_rxring_cnt(const struct ix_softc *sc, int *ring_cnt, int *ring_cntmax)
{

	switch (sc->hw.mac.type) {
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		*ring_cntmax = IX_MAX_RXRING_X550;
		break;

	default:
		*ring_cntmax = IX_MAX_RXRING;
		break;
	}
	*ring_cnt = device_getenv_int(sc->dev, "rxr", ix_rxr);
}

static void
ix_get_txring_cnt(const struct ix_softc *sc, int *ring_cnt, int *ring_cntmax)
{

	switch (sc->hw.mac.type) {
	case ixgbe_mac_82598EB:
		*ring_cntmax = IX_MAX_TXRING_82598;
		break;

	case ixgbe_mac_82599EB:
		*ring_cntmax = IX_MAX_TXRING_82599;
		break;

	case ixgbe_mac_X540:
		*ring_cntmax = IX_MAX_TXRING_X540;
		break;

	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		*ring_cntmax = IX_MAX_TXRING_X550;
		break;

	default:
		*ring_cntmax = IX_MAX_TXRING;
		break;
	}
	*ring_cnt = device_getenv_int(sc->dev, "txr", ix_txr);
}

static int
ix_attach(device_t dev)
{
	struct ix_softc *sc = device_get_softc(dev);
	struct ixgbe_hw *hw;
	int error, ring_cnt, ring_cntmax;
	uint32_t ctrl_ext;
	char flowctrl[IFM_ETH_FC_STRLEN];

	sc->dev = dev;
	hw = &sc->hw;
	hw->back = sc;

	if_initname(&sc->arpcom.ac_if, device_get_name(dev),
	    device_get_unit(dev));
	ifmedia_init(&sc->media, IFM_IMASK | IFM_ETH_FCMASK,
	    ix_media_change, ix_media_status);

	/* Save frame size */
	sc->max_frame_size = ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN;

	sc->direct_input = ix_direct_input;
	TASK_INIT(&sc->wdog_task, 0, ix_watchdog_task, sc);

	callout_init_mp(&sc->fw_timer);
	callout_init_mp(&sc->timer);
	lwkt_serialize_init(&sc->main_serialize);

	/*
	 * Save off the information about this board
	 */
	hw->vendor_id = pci_get_vendor(dev);
	hw->device_id = pci_get_device(dev);
	hw->revision_id = pci_read_config(dev, PCIR_REVID, 1);
	hw->subsystem_vendor_id = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	hw->subsystem_device_id = pci_read_config(dev, PCIR_SUBDEV_0, 2);

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate IO memory
	 */
	sc->mem_rid = PCIR_BAR(0);
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mem_rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Unable to allocate bus resource: memory\n");
		error = ENXIO;
		goto failed;
	}

	sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->mem_res);
	sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->mem_res);

	sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;

	/* Let hardware know driver is loaded */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_DRV_LOAD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);

	/*
	 * Initialize the shared code
	 */
	if (ixgbe_init_shared_code(hw)) {
		device_printf(dev, "Unable to initialize the shared code\n");
		error = ENXIO;
		goto failed;
	}

	if (hw->mbx.ops.init_params)
		hw->mbx.ops.init_params(hw);

	hw->allow_unsupported_sfp = ix_unsupported_sfp;

	/* Pick up the 82599 settings */
	if (hw->mac.type != ixgbe_mac_82598EB)
		hw->phy.smart_speed = ix_smart_speed;

	/* Setup hardware capabilities */
	ix_setup_caps(sc);

	/* Allocate multicast array memory. */
	sc->mta = kmalloc(sizeof(*sc->mta) * IX_MAX_MCASTADDR,
	    M_DEVBUF, M_WAITOK);

	/* Save initial wake up filter configuration; WOL is disabled. */
	sc->wufc = IXGBE_READ_REG(hw, IXGBE_WUFC);

	/* Verify adapter fan is still functional (if applicable) */
	if (sc->caps & IX_CAP_DETECT_FANFAIL)
		ix_detect_fanfail(sc, IXGBE_READ_REG(hw, IXGBE_ESDP), FALSE);

	/* Ensure SW/FW semaphore is free */
	ixgbe_init_swfw_semaphore(hw);

#ifdef notyet
	/* Enable EEE power saving */
	if (sc->caps & IX_CAP_EEE)
		hw->mac.ops.setup_eee(hw, true);
#endif

	/*
	 * Configure total supported RX/TX ring count
	 */
	ix_get_rxring_cnt(sc, &ring_cnt, &ring_cntmax);
	sc->rx_rmap = if_ringmap_alloc(dev, ring_cnt, ring_cntmax);
	ix_get_txring_cnt(sc, &ring_cnt, &ring_cntmax);
	sc->tx_rmap = if_ringmap_alloc(dev, ring_cnt, ring_cntmax);
	if_ringmap_match(dev, sc->rx_rmap, sc->tx_rmap);

	sc->rx_ring_cnt = if_ringmap_count(sc->rx_rmap);
	sc->rx_ring_inuse = sc->rx_ring_cnt;
	sc->tx_ring_cnt = if_ringmap_count(sc->tx_rmap);
	sc->tx_ring_inuse = sc->tx_ring_cnt;

	/* Allocate TX/RX rings */
	error = ix_alloc_rings(sc);
	if (error)
		goto failed;

	/* Allocate interrupt */
	error = ix_alloc_intr(sc);
	if (error)
		goto failed;

	/* Setup serializes */
	ix_setup_serialize(sc);

	hw->phy.reset_if_overtemp = TRUE;
	error = ixgbe_reset_hw(hw);
	hw->phy.reset_if_overtemp = FALSE;
	if (error == IXGBE_ERR_SFP_NOT_PRESENT) {
		/*
		 * No optics in this port; ask timer routine
		 * to probe for later insertion.
		 */
		sc->sfp_probe = TRUE;
		error = 0;
	} else if (error == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		device_printf(dev, "Unsupported SFP+ module detected!\n");
		error = EIO;
		goto failed;
	} else if (error) {
		device_printf(dev, "Hardware initialization failed\n");
		error = EIO;
		goto failed;
	}

	/* Make sure we have a good EEPROM before we read from it */
	if (ixgbe_validate_eeprom_checksum(&sc->hw, NULL) < 0) {
		device_printf(dev, "The EEPROM Checksum Is Not Valid\n");
		error = EIO;
		goto failed;
	}

	error = ixgbe_start_hw(hw);
	if (error == IXGBE_ERR_EEPROM_VERSION) {
		device_printf(dev, "Pre-production device detected\n");
	} else if (error == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		device_printf(dev, "Unsupported SFP+ Module\n");
		error = EIO;
		goto failed;
	} else if (error == IXGBE_ERR_SFP_NOT_PRESENT) {
		device_printf(dev, "No SFP+ Module found\n");
	}

	/* Enable the optics for 82599 SFP+ fiber */
	ixgbe_enable_tx_laser(hw);

	/* Enable power to the phy. */
	ixgbe_set_phy_power(hw, TRUE);

	sc->ifm_media = IX_IFM_DEFAULT;
	/* Get default flow control settings */
	device_getenv_string(dev, "flow_ctrl", flowctrl, sizeof(flowctrl),
	    ix_flowctrl);
	sc->ifm_media |= ifmedia_str2ethfc(flowctrl);
	sc->advspeed = IXGBE_LINK_SPEED_UNKNOWN;

	/* Setup OS specific network interface */
	ix_setup_ifp(sc);

	/* Add sysctl tree */
	ix_add_sysctl(sc);

	error = ix_setup_intr(sc);
	if (error) {
		ether_ifdetach(&sc->arpcom.ac_if);
		goto failed;
	}

	/* Initialize statistics */
	ix_update_stats(sc);

	/* Check PCIE slot type/speed/width */
	ix_slot_info(sc);

	if (sc->caps & IX_CAP_FW_RECOVERY) {
		device_printf(dev, "start fw timer\n");
		callout_reset_bycpu(&sc->fw_timer, hz,
		    ix_fw_timer, sc, ix_get_timer_cpuid(sc, FALSE));
	}

	return 0;
failed:
	ix_detach(dev);
	return error;
}

static int
ix_detach(device_t dev)
{
	struct ix_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ix_sync_netisr(sc, IFF_UP);
		taskqueue_drain(taskqueue_thread[0], &sc->wdog_task);

		ifnet_serialize_all(ifp);

		ix_powerdown(sc);
		ix_teardown_intr(sc, sc->intr_cnt);

		ifnet_deserialize_all(ifp);

		callout_terminate(&sc->timer);
		ether_ifdetach(ifp);
	}
	callout_terminate(&sc->fw_timer);

	if (sc->mem_res != NULL) {
		uint32_t ctrl_ext;

		/* Let hardware know driver is unloading */
		ctrl_ext = IXGBE_READ_REG(&sc->hw, IXGBE_CTRL_EXT);
		ctrl_ext &= ~IXGBE_CTRL_EXT_DRV_LOAD;
		IXGBE_WRITE_REG(&sc->hw, IXGBE_CTRL_EXT, ctrl_ext);
	}

	ifmedia_removeall(&sc->media);
	bus_generic_detach(dev);

	ix_free_intr(sc);

	if (sc->msix_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->msix_mem_rid,
		    sc->msix_mem_res);
	}
	if (sc->mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
		    sc->mem_res);
	}

	ix_free_rings(sc);

	if (sc->mta != NULL)
		kfree(sc->mta, M_DEVBUF);
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
ix_shutdown(device_t dev)
{
	struct ix_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ix_sync_netisr(sc, IFF_UP);
	taskqueue_drain(taskqueue_thread[0], &sc->wdog_task);

	ifnet_serialize_all(ifp);
	ix_powerdown(sc);
	ifnet_deserialize_all(ifp);

	return 0;
}

static void
ix_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct ix_softc *sc = ifp->if_softc;
	struct ix_tx_ring *txr = ifsq_get_priv(ifsq);
	int idx = -1;
	uint16_t nsegs;

	KKASSERT(txr->tx_ifsq == ifsq);
	ASSERT_SERIALIZED(&txr->tx_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	if (!sc->link_active || (txr->tx_flags & IX_TXFLAG_ENABLED) == 0) {
		ifsq_purge(ifsq);
		return;
	}

	while (!ifsq_is_empty(ifsq)) {
		struct mbuf *m_head;

		if (txr->tx_avail <= IX_MAX_SCATTER + IX_TX_RESERVED) {
			ifsq_set_oactive(ifsq);
			ifsq_watchdog_set_count(&txr->tx_watchdog, 5);
			break;
		}

		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		if (ix_encap(txr, &m_head, &nsegs, &idx)) {
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

		if (nsegs >= txr->tx_wreg_nsegs) {
			IXGBE_WRITE_REG(&sc->hw, IXGBE_TDT(txr->tx_idx), idx);
			nsegs = 0;
			idx = -1;
		}

		ETHER_BPF_MTAP(ifp, m_head);
	}
	if (idx >= 0)
		IXGBE_WRITE_REG(&sc->hw, IXGBE_TDT(txr->tx_idx), idx);
	txr->tx_running = IX_TX_RUNNING;
}

static int
ix_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct ix_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0, mask, reinit;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > IX_MAX_MTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
			sc->max_frame_size = ifp->if_mtu + IX_MTU_HDR;
			ix_init(sc);
		}
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					ix_set_promisc(sc);
			} else {
				ix_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			ix_stop(sc);
		}
		sc->if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING) {
			ix_disable_intr(sc);
			ix_set_multi(sc);
#ifdef IFPOLL_ENABLE
			if ((ifp->if_flags & IFF_NPOLLING) == 0)
#endif
				ix_enable_intr(sc);
		}
		break;

	case SIOCSIFMEDIA:
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
				ifp->if_hwassist |= CSUM_OFFLOAD;
			else
				ifp->if_hwassist &= ~CSUM_OFFLOAD;
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
			ix_init(sc);
		break;

#if 0
	case SIOCGI2C:
	{
		struct ixgbe_i2c_req	i2c;
		error = copyin(ifr->ifr_data, &i2c, sizeof(i2c));
		if (error)
			break;
		if ((i2c.dev_addr != 0xA0) || (i2c.dev_addr != 0xA2)){
			error = EINVAL;
			break;
		}
		hw->phy.ops.read_i2c_byte(hw, i2c.offset,
		    i2c.dev_addr, i2c.data);
		error = copyout(&i2c, ifr->ifr_data, sizeof(i2c));
		break;
	}
#endif

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return error;
}

#define IXGBE_MHADD_MFS_SHIFT 16

static void
ix_init(void *xsc)
{
	struct ix_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t rxctrl;
	int i, error;
	boolean_t polling;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	ix_stop(sc);

	if (sc->flags & IX_FLAG_FW_RECOVERY)
		return;

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	/* Configure # of used RX/TX rings */
	ix_set_ring_inuse(sc, polling);
	ifq_set_subq_divisor(&ifp->if_snd, sc->tx_ring_inuse);

	/* Get the latest mac address, User can use a LAA */
	bcopy(IF_LLADDR(ifp), hw->mac.addr, IXGBE_ETH_LENGTH_OF_ADDRESS);
	ixgbe_set_rar(hw, 0, hw->mac.addr, 0, 1);
	hw->addr_ctrl.rar_used_count = 1;

	/* Prepare transmit descriptors and buffers */
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ix_init_tx_ring(&sc->tx_rings[i]);

	ixgbe_init_hw(hw);
	ix_init_tx_unit(sc);

	/* Setup Multicast table */
	ix_set_multi(sc);

	/* Prepare receive descriptors and buffers */
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		error = ix_init_rx_ring(&sc->rx_rings[i]);
		if (error) {
			if_printf(ifp, "Could not initialize RX ring%d\n", i);
			ix_stop(sc);
			return;
		}
	}

	/* Configure RX settings */
	ix_init_rx_unit(sc, polling);

	/* Enable SDP & MSI-X interrupts based on adapter */
	ix_config_gpie(sc);

	/* Set MTU size */
	if (ifp->if_mtu > ETHERMTU) {
		uint32_t mhadd;

		/* aka IXGBE_MAXFRS on 82599 and newer */
		mhadd = IXGBE_READ_REG(hw, IXGBE_MHADD);
		mhadd &= ~IXGBE_MHADD_MFS_MASK;
		mhadd |= sc->max_frame_size << IXGBE_MHADD_MFS_SHIFT;
		IXGBE_WRITE_REG(hw, IXGBE_MHADD, mhadd);
	}

	/*
	 * Enable TX rings
	 */
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		uint32_t txdctl;

		txdctl = IXGBE_READ_REG(hw, IXGBE_TXDCTL(i));
		txdctl |= IXGBE_TXDCTL_ENABLE;

		/*
		 * Set WTHRESH to 0, since TX head write-back is used
		 */
		txdctl &= ~(0x7f << 16);

		/*
		 * When the internal queue falls below PTHRESH (32),
		 * start prefetching as long as there are at least
		 * HTHRESH (1) buffers ready. The values are taken
		 * from the Intel linux driver 3.8.21.
		 * Prefetching enables tx line rate even with 1 queue.
		 */
		txdctl |= (32 << 0) | (1 << 8);
		IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(i), txdctl);
	}

	/*
	 * Enable RX rings
	 */
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		uint32_t rxdctl;
		int k;

		rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
		if (hw->mac.type == ixgbe_mac_82598EB) {
			/*
			 * PTHRESH = 21
			 * HTHRESH = 4
			 * WTHRESH = 8
			 */
			rxdctl &= ~0x3FFFFF;
			rxdctl |= 0x080420;
		}
		rxdctl |= IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(i), rxdctl);
		for (k = 0; k < 10; ++k) {
			if (IXGBE_READ_REG(hw, IXGBE_RXDCTL(i)) &
			    IXGBE_RXDCTL_ENABLE)
				break;
			else
				msec_delay(1);
		}
		wmb();
		IXGBE_WRITE_REG(hw, IXGBE_RDT(i),
		    sc->rx_rings[0].rx_ndesc - 1);
	}

	/* Enable Receive engine */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	if (hw->mac.type == ixgbe_mac_82598EB)
		rxctrl |= IXGBE_RXCTRL_DMBYPS;
	rxctrl |= IXGBE_RXCTRL_RXEN;
	ixgbe_enable_rx_dma(hw, rxctrl);

	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		const struct ix_tx_ring *txr = &sc->tx_rings[i];

		if (txr->tx_intr_vec >= 0) {
			ix_set_ivar(sc, i, txr->tx_intr_vec, 1);
		} else if (!polling) {
			/*
			 * Unconfigured TX interrupt vector could only
			 * happen for MSI-X.
			 */
			KASSERT(sc->intr_type == PCI_INTR_TYPE_MSIX,
			    ("TX intr vector is not set"));
			if (bootverbose)
				if_printf(ifp, "IVAR skips TX ring %d\n", i);
		}
	}
	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		const struct ix_rx_ring *rxr = &sc->rx_rings[i];

		if (polling && rxr->rx_intr_vec < 0)
			continue;

		KKASSERT(rxr->rx_intr_vec >= 0);
		ix_set_ivar(sc, i, rxr->rx_intr_vec, 0);
		if (rxr->rx_txr != NULL) {
			/*
			 * Piggyback the TX ring interrupt onto the RX
			 * ring interrupt vector.
			 */
			KASSERT(rxr->rx_txr->tx_intr_vec < 0,
			    ("piggybacked TX ring configured intr vector"));
			ix_set_ivar(sc, rxr->rx_txr->tx_idx,
			    rxr->rx_intr_vec, 1);
			if (bootverbose) {
				if_printf(ifp, "IVAR RX ring %d piggybacks "
				    "TX ring %u\n", i, rxr->rx_txr->tx_idx);
			}
		}
	}
	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		/* Set up status MSI-X vector; it is using fixed entry 1 */
		ix_set_ivar(sc, 1, sc->sts_msix_vec, -1);

		/* Set up auto-mask for TX and RX rings */
		if (hw->mac.type == ixgbe_mac_82598EB) {
			IXGBE_WRITE_REG(hw, IXGBE_EIAM, IXGBE_EIMS_RTX_QUEUE);
		} else {
			IXGBE_WRITE_REG(hw, IXGBE_EIAM_EX(0), 0xFFFFFFFF);
			IXGBE_WRITE_REG(hw, IXGBE_EIAM_EX(1), 0xFFFFFFFF);
		}
	} else {
		IXGBE_WRITE_REG(hw, IXGBE_EIAM, IXGBE_EIMS_RTX_QUEUE);
	}
	for (i = 0; i < sc->intr_cnt; ++i)
		ix_set_eitr(sc, i, sc->intr_data[i].intr_rate);

	/*
	 * Check on any SFP devices that need to be kick-started
	 */
	if (hw->phy.type == ixgbe_phy_none) {
		error = hw->phy.ops.identify(hw);
		if (error == IXGBE_ERR_SFP_NOT_SUPPORTED) {
			if_printf(ifp,
			    "Unsupported SFP+ module type was detected.\n");
			/* XXX stop */
			return;
		}
	}

	/* Config/Enable Link */
	ix_config_link(sc);

	/* Hardware Packet Buffer & Flow Control setup */
	ix_config_flowctrl(sc);

	/* Initialize the FC settings */
	ixgbe_start_hw(hw);

	/* Set up VLAN support and filter */
	ix_set_vlan(sc);

	/* Setup DMA Coalescing */
	ix_config_dmac(sc);

	/*
	 * Only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (polling)
		ix_disable_intr(sc);
	else
		ix_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		ifsq_clr_oactive(txr->tx_ifsq);
		ifsq_watchdog_start(&txr->tx_watchdog);

		if (!polling) {
			callout_reset_bycpu(&txr->tx_gc_timer, 1,
			    ix_txgc_timer, txr, txr->tx_intr_cpuid);
		}
	}

	sc->timer_cpuid = ix_get_timer_cpuid(sc, polling);
	callout_reset_bycpu(&sc->timer, hz, ix_timer, sc, sc->timer_cpuid);
}

static void
ix_intr(void *xsc)
{
	struct ix_softc *sc = xsc;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t eicr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);
	if (eicr == 0) {
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, sc->intr_mask);
		return;
	}

	if (eicr & IX_RX0_INTR_MASK) {
		struct ix_rx_ring *rxr = &sc->rx_rings[0];

		lwkt_serialize_enter(&rxr->rx_serialize);
		ix_rxeof(rxr, -1);
		lwkt_serialize_exit(&rxr->rx_serialize);
	}
	if (eicr & IX_RX1_INTR_MASK) {
		struct ix_rx_ring *rxr;

		KKASSERT(sc->rx_ring_inuse == IX_MIN_RXRING_RSS);
		rxr = &sc->rx_rings[1];

		lwkt_serialize_enter(&rxr->rx_serialize);
		ix_rxeof(rxr, -1);
		lwkt_serialize_exit(&rxr->rx_serialize);
	}

	if (eicr & IX_TX_INTR_MASK) {
		struct ix_tx_ring *txr = &sc->tx_rings[0];

		lwkt_serialize_enter(&txr->tx_serialize);
		ix_tx_intr(txr, *(txr->tx_hdr));
		lwkt_serialize_exit(&txr->tx_serialize);
	}

	if (__predict_false(eicr & IX_EICR_STATUS))
		ix_intr_status(sc, eicr);

	IXGBE_WRITE_REG(hw, IXGBE_EIMS, sc->intr_mask);
}

static void
ix_intr_82598(void *xsc)
{
	struct ix_softc *sc = xsc;

	ASSERT_SERIALIZED(&sc->main_serialize);

	/* Software workaround for 82598 errata #26 */
	IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, IXGBE_IRQ_CLEAR_MASK);

	ix_intr(sc);
}

static void
ix_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ix_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;
	int layer;
	boolean_t link_active;

	if (sc->flags & IX_FLAG_FW_RECOVERY) {
		link_active = FALSE;
	} else {
		ix_update_link_status(sc);
		link_active = sc->link_active;
	}

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!link_active) {
		if (IFM_SUBTYPE(ifm->ifm_media) != IFM_AUTO)
			ifmr->ifm_active |= ifm->ifm_media;
		else
			ifmr->ifm_active |= IFM_NONE;
		return;
	}
	ifmr->ifm_status |= IFM_ACTIVE;

	layer = sc->phy_layer;

	if ((layer & IXGBE_PHYSICAL_LAYER_10GBASE_T) ||
	    (layer & IXGBE_PHYSICAL_LAYER_1000BASE_T) ||
	    (layer & IXGBE_PHYSICAL_LAYER_100BASE_TX) ||
	    (layer & IXGBE_PHYSICAL_LAYER_10BASE_T)) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_T | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_T | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_100_FULL:
			ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_10_FULL:
			ifmr->ifm_active |= IFM_10_T | IFM_FDX;
			break;
		}
	} else if ((layer & IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU) ||
	    (layer & IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA)) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_TWINAX | IFM_FDX;
			break;
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LR) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_LR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_LX | IFM_FDX;
			break;
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LRM) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_LRM | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_LX | IFM_FDX;
			break;
		}
	} else if ((layer & IXGBE_PHYSICAL_LAYER_10GBASE_SR) ||
	    (layer & IXGBE_PHYSICAL_LAYER_1000BASE_SX)) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_SX | IFM_FDX;
			break;
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_CX4) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_CX4 | IFM_FDX;
			break;
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KR) {
		/*
		 * XXX: These need to use the proper media types once
		 * they're added.
		 */
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_2_5GB_FULL:
			ifmr->ifm_active |= IFM_2500_SX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_CX | IFM_FDX;
			break;
		}
	} else if ((layer & IXGBE_PHYSICAL_LAYER_10GBASE_KX4) ||
	    (layer & IXGBE_PHYSICAL_LAYER_2500BASE_KX) ||
	    (layer & IXGBE_PHYSICAL_LAYER_1000BASE_KX)) {
		/*
		 * XXX: These need to use the proper media types once
		 * they're added.
		 */
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_CX4 | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_2_5GB_FULL:
			ifmr->ifm_active |= IFM_2500_SX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_CX | IFM_FDX;
			break;
		}
	}

	/* If nothing is recognized... */
	if (IFM_SUBTYPE(ifmr->ifm_active) == 0)
		ifmr->ifm_active |= IFM_NONE;

	if (sc->ifm_media & IFM_ETH_FORCEPAUSE)
		ifmr->ifm_active |= (sc->ifm_media & IFM_ETH_FCMASK);

	switch (sc->hw.fc.current_mode) {
	case ixgbe_fc_full:
		ifmr->ifm_active |= IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE;
		break;
	case ixgbe_fc_rx_pause:
		ifmr->ifm_active |= IFM_ETH_RXPAUSE;
		break;
	case ixgbe_fc_tx_pause:
		ifmr->ifm_active |= IFM_ETH_TXPAUSE;
		break;
	default:
		break;
	}
}

static int
ix_media_change(struct ifnet *ifp)
{
	struct ix_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;
	struct ixgbe_hw *hw = &sc->hw;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (hw->phy.media_type == ixgbe_media_type_backplane ||
	    hw->mac.ops.setup_link == NULL) {
		if ((ifm->ifm_media ^ sc->ifm_media) & IFM_ETH_FCMASK) {
			/* Only flow control setting changes are allowed */
			return (EOPNOTSUPP);
		}
	}

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->advspeed = IXGBE_LINK_SPEED_UNKNOWN;
		break;

	case IFM_10G_T:
	case IFM_10G_LRM:
	case IFM_10G_SR:	/* XXX also KR */
	case IFM_10G_LR:
	case IFM_10G_CX4:	/* XXX also KX4 */
	case IFM_10G_TWINAX:
		sc->advspeed = IXGBE_LINK_SPEED_10GB_FULL;
		break;

	case IFM_1000_T:
	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_CX:	/* XXX is KX */
		sc->advspeed = IXGBE_LINK_SPEED_1GB_FULL;
		break;

	case IFM_100_TX:
		sc->advspeed = IXGBE_LINK_SPEED_100_FULL;
		break;

	default:
		if (bootverbose) {
			if_printf(ifp, "Invalid media type %d!\n",
			    ifm->ifm_media);
		}
		return EINVAL;
	}
	sc->ifm_media = ifm->ifm_media;

#if 0
	if (hw->mac.ops.setup_link != NULL) {
		hw->mac.autotry_restart = TRUE;
		hw->mac.ops.setup_link(hw, sc->advspeed, TRUE);
	}
#else
	if (ifp->if_flags & IFF_RUNNING)
		ix_init(sc);
#endif
	return 0;
}

static __inline int
ix_tso_pullup(struct mbuf **mp)
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
	return 0;
}

static int
ix_encap(struct ix_tx_ring *txr, struct mbuf **m_headp,
    uint16_t *segs_used, int *idx)
{
	uint32_t olinfo_status = 0, cmd_type_len, cmd_rs = 0;
	int i, j, error, nsegs, first, maxsegs;
	struct mbuf *m_head = *m_headp;
	bus_dma_segment_t segs[IX_MAX_SCATTER];
	bus_dmamap_t map;
	struct ix_tx_buf *txbuf;
	union ixgbe_adv_tx_desc *txd = NULL;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		error = ix_tso_pullup(m_headp);
		if (__predict_false(error))
			return error;
		m_head = *m_headp;
	}

	/* Basic descriptor defines */
	cmd_type_len = (IXGBE_ADVTXD_DTYP_DATA |
	    IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT);

	if (m_head->m_flags & M_VLANTAG)
		cmd_type_len |= IXGBE_ADVTXD_DCMD_VLE;

	/*
	 * Important to capture the first descriptor
	 * used because it will contain the index of
	 * the one we tell the hardware to report back
	 */
	first = txr->tx_next_avail;
	txbuf = &txr->tx_buf[first];
	map = txbuf->map;

	/*
	 * Map the packet for DMA.
	 */
	maxsegs = txr->tx_avail - IX_TX_RESERVED;
	if (maxsegs > IX_MAX_SCATTER)
		maxsegs = IX_MAX_SCATTER;

	error = bus_dmamap_load_mbuf_defrag(txr->tx_tag, map, m_headp,
	    segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (__predict_false(error)) {
		m_freem(*m_headp);
		*m_headp = NULL;
		return error;
	}
	bus_dmamap_sync(txr->tx_tag, map, BUS_DMASYNC_PREWRITE);

	m_head = *m_headp;

	/*
	 * Set up the appropriate offload context if requested,
	 * this may consume one TX descriptor.
	 */
	if (ix_tx_ctx_setup(txr, m_head, &cmd_type_len, &olinfo_status)) {
		(*segs_used)++;
		txr->tx_nsegs++;
	}

	*segs_used += nsegs;
	txr->tx_nsegs += nsegs;
	if (txr->tx_nsegs >= txr->tx_intr_nsegs) {
		/*
		 * Report Status (RS) is turned on every intr_nsegs
		 * descriptors (roughly).
		 */
		txr->tx_nsegs = 0;
		cmd_rs = IXGBE_TXD_CMD_RS;
	}

	i = txr->tx_next_avail;
	for (j = 0; j < nsegs; j++) {
		bus_size_t seglen;
		bus_addr_t segaddr;

		txbuf = &txr->tx_buf[i];
		txd = &txr->tx_base[i];
		seglen = segs[j].ds_len;
		segaddr = htole64(segs[j].ds_addr);

		txd->read.buffer_addr = segaddr;
		txd->read.cmd_type_len = htole32(IXGBE_TXD_CMD_IFCS |
		    cmd_type_len |seglen);
		txd->read.olinfo_status = htole32(olinfo_status);

		if (++i == txr->tx_ndesc)
			i = 0;
	}
	txd->read.cmd_type_len |= htole32(IXGBE_TXD_CMD_EOP | cmd_rs);

	txr->tx_avail -= nsegs;
	txr->tx_next_avail = i;
	txr->tx_nmbuf++;

	txbuf->m_head = m_head;
	txr->tx_buf[first].map = txbuf->map;
	txbuf->map = map;

	/*
	 * Defer TDT updating, until enough descrptors are setup
	 */
	*idx = i;

	return 0;
}

static void
ix_set_promisc(struct ix_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg_rctl;
	int mcnt = 0;

	reg_rctl = IXGBE_READ_REG(&sc->hw, IXGBE_FCTRL);
	reg_rctl &= ~IXGBE_FCTRL_UPE;
	if (ifp->if_flags & IFF_ALLMULTI) {
		mcnt = IX_MAX_MCASTADDR;
	} else {
		struct ifmultiaddr *ifma;

		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			if (mcnt == IX_MAX_MCASTADDR)
				break;
			mcnt++;
		}
	}
	if (mcnt < IX_MAX_MCASTADDR)
		reg_rctl &= ~IXGBE_FCTRL_MPE;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_FCTRL, reg_rctl);

	if (ifp->if_flags & IFF_PROMISC) {
		reg_rctl |= IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE;
		IXGBE_WRITE_REG(&sc->hw, IXGBE_FCTRL, reg_rctl);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg_rctl |= IXGBE_FCTRL_MPE;
		reg_rctl &= ~IXGBE_FCTRL_UPE;
		IXGBE_WRITE_REG(&sc->hw, IXGBE_FCTRL, reg_rctl);
	}
}

static void
ix_set_multi(struct ix_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t fctrl;
	struct ix_mc_addr *mta;
	int mcnt = 0;

	mta = sc->mta;
	bzero(mta, sizeof(*mta) * IX_MAX_MCASTADDR);

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (mcnt == IX_MAX_MCASTADDR)
			break;
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    mta[mcnt].addr, IXGBE_ETH_LENGTH_OF_ADDRESS);
		mcnt++;
	}

	fctrl = IXGBE_READ_REG(&sc->hw, IXGBE_FCTRL);
	fctrl |= (IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	if (ifp->if_flags & IFF_PROMISC) {
		fctrl |= IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE;
	} else if (mcnt >= IX_MAX_MCASTADDR || (ifp->if_flags & IFF_ALLMULTI)) {
		fctrl |= IXGBE_FCTRL_MPE;
		fctrl &= ~IXGBE_FCTRL_UPE;
	} else {
		fctrl &= ~(IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	}
	IXGBE_WRITE_REG(&sc->hw, IXGBE_FCTRL, fctrl);

	if (mcnt < IX_MAX_MCASTADDR) {
		ixgbe_update_mc_addr_list(&sc->hw,
		    (uint8_t *)mta, mcnt, ix_mc_array_itr, TRUE);
	}
}

/*
 * This is an iterator function now needed by the multicast
 * shared code. It simply feeds the shared code routine the
 * addresses in the array of ix_set_multi() one by one.
 */
static uint8_t *
ix_mc_array_itr(struct ixgbe_hw *hw, uint8_t **update_ptr, uint32_t *vmdq)
{
	struct ix_mc_addr *mta = (struct ix_mc_addr *)*update_ptr;

	*vmdq = mta->vmdq;
	*update_ptr = (uint8_t *)(mta + 1);

	return (mta->addr);
}

static void
ix_timer(void *arg)
{
	struct ix_softc *sc = arg;

	lwkt_serialize_enter(&sc->main_serialize);

	if ((sc->arpcom.ac_if.if_flags & IFF_RUNNING) == 0) {
		lwkt_serialize_exit(&sc->main_serialize);
		return;
	}

	/* Check for pluggable optics */
	if (sc->sfp_probe) {
		if (!ix_sfp_probe(sc))
			goto done; /* Nothing to do */
	}

	ix_update_link_status(sc);
	ix_update_stats(sc);

done:
	callout_reset_bycpu(&sc->timer, hz, ix_timer, sc, sc->timer_cpuid);
	lwkt_serialize_exit(&sc->main_serialize);
}

static void
ix_update_link_status(struct ix_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (sc->link_up) {
		if (sc->link_active == FALSE) {
			if (bootverbose) {
				if_printf(ifp, "Link is up %d Gbps %s\n",
				    sc->link_speed == 128 ? 10 : 1,
				    "Full Duplex");
			}

			/*
			 * Update any Flow Control changes
			 */
			ixgbe_fc_enable(&sc->hw);
			/* MUST after ixgbe_fc_enable() */
			if (sc->rx_ring_inuse > 1) {
				switch (sc->hw.fc.current_mode) {
				case ixgbe_fc_rx_pause:
				case ixgbe_fc_tx_pause:
				case ixgbe_fc_full:
					ix_disable_rx_drop(sc);
					break;

				case ixgbe_fc_none:
					ix_enable_rx_drop(sc);
					break;

				default:
					break;
				}
			}

			/* Update DMA coalescing config */
			ix_config_dmac(sc);

			sc->link_active = TRUE;

			ifp->if_link_state = LINK_STATE_UP;
			if_link_state_change(ifp);
		}
	} else { /* Link down */
		if (sc->link_active == TRUE) {
			if (bootverbose)
				if_printf(ifp, "Link is Down\n");
			ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(ifp);

			sc->link_active = FALSE;
		}
	}
}

static void
ix_stop(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	ix_disable_intr(sc);
	callout_stop(&sc->timer);

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		ifsq_clr_oactive(txr->tx_ifsq);
		ifsq_watchdog_stop(&txr->tx_watchdog);
		txr->tx_flags &= ~IX_TXFLAG_ENABLED;

		txr->tx_running = 0;
		callout_stop(&txr->tx_gc_timer);
	}

	ixgbe_reset_hw(hw);
	hw->adapter_stopped = FALSE;
	ixgbe_stop_adapter(hw);
	if (hw->mac.type == ixgbe_mac_82599EB)
		ixgbe_stop_mac_link_on_d3_82599(hw);
	/* Turn off the laser - noop with no optics */
	ixgbe_disable_tx_laser(hw);

	/* Update the stack */
	sc->link_up = FALSE;
	ix_update_link_status(sc);

	/* Reprogram the RAR[0] in case user changed it. */
	ixgbe_set_rar(hw, 0, hw->mac.addr, 0, IXGBE_RAH_AV);

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		ix_free_tx_ring(&sc->tx_rings[i]);

	for (i = 0; i < sc->rx_ring_cnt; ++i)
		ix_free_rx_ring(&sc->rx_rings[i]);
}

static void
ix_setup_ifp(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ifp->if_baudrate = IF_Gbps(10UL);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = ix_init;
	ifp->if_ioctl = ix_ioctl;
	ifp->if_start = ix_start;
	ifp->if_serialize = ix_serialize;
	ifp->if_deserialize = ix_deserialize;
	ifp->if_tryserialize = ix_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = ix_serialize_assert;
#endif
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = ix_npoll;
#endif

	/* Increase TSO burst length */
	ifp->if_tsolen = (8 * ETHERMTU);

	ifp->if_nmbclusters = sc->rx_ring_cnt * sc->rx_rings[0].rx_ndesc;
	ifp->if_nmbjclusters = ifp->if_nmbclusters;

	ifq_set_maxlen(&ifp->if_snd, sc->tx_rings[0].tx_ndesc - 2);
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->tx_ring_cnt);

	ifp->if_mapsubq = ifq_mapsubq_modulo;
	ifq_set_subq_divisor(&ifp->if_snd, 1);

	ether_ifattach(ifp, hw->mac.addr, NULL);

	ifp->if_capabilities =
	    IFCAP_HWCSUM | IFCAP_TSO | IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	if (IX_ENABLE_HWRSS(sc))
		ifp->if_capabilities |= IFCAP_RSS;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_hwassist = CSUM_OFFLOAD | CSUM_TSO;

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	/* Setup TX rings and subqueues */
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		ifsq_set_cpuid(ifsq, txr->tx_intr_cpuid);
		ifsq_set_priv(ifsq, txr);
		ifsq_set_hw_serialize(ifsq, &txr->tx_serialize);
		txr->tx_ifsq = ifsq;

		ifsq_watchdog_init(&txr->tx_watchdog, ifsq, ix_watchdog, 0);
	}

	/* Specify the media types supported by this adapter */
	sc->phy_layer = ixgbe_get_supported_physical_layer(hw);
	ix_init_media(sc);
}

static boolean_t
ix_is_sfp(struct ixgbe_hw *hw)
{
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		if (hw->phy.type == ixgbe_phy_nl)
			return TRUE;
		return FALSE;

	case ixgbe_mac_82599EB:
		switch (hw->mac.ops.get_media_type(hw)) {
		case ixgbe_media_type_fiber:
		case ixgbe_media_type_fiber_qsfp:
			return TRUE;
		default:
			return FALSE;
		}

	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber)
			return TRUE;
		return FALSE;

	default:
		return FALSE;
	}
}

static void
ix_config_link(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	boolean_t sfp;

	sfp = ix_is_sfp(hw);
	if (sfp) {
		if (hw->phy.multispeed_fiber)
			ixgbe_enable_tx_laser(hw);
		ix_handle_mod(sc);
	} else {
		uint32_t autoneg, err = 0;

		if (hw->mac.ops.check_link != NULL) {
			err = ixgbe_check_link(hw, &sc->link_speed,
			    &sc->link_up, FALSE);
			if (err)
				return;
		}

		if (sc->advspeed != IXGBE_LINK_SPEED_UNKNOWN)
			autoneg = sc->advspeed;
		else
			autoneg = hw->phy.autoneg_advertised;
		if (!autoneg && hw->mac.ops.get_link_capabilities != NULL) {
			bool negotiate;

			err = hw->mac.ops.get_link_capabilities(hw,
			    &autoneg, &negotiate);
			if (err)
				return;
		}

		if (hw->mac.ops.setup_link != NULL) {
			err = hw->mac.ops.setup_link(hw,
			    autoneg, sc->link_up);
			if (err)
				return;
		}
	}
}

static int
ix_alloc_rings(struct ix_softc *sc)
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
	sc->tx_rings = kmalloc(sizeof(struct ix_tx_ring) * sc->tx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		txr->tx_sc = sc;
		txr->tx_idx = i;
		txr->tx_intr_vec = -1;
		txr->tx_intr_cpuid = -1;
		lwkt_serialize_init(&txr->tx_serialize);
		callout_init_mp(&txr->tx_gc_timer);

		error = ix_create_tx_ring(txr);
		if (error)
			return error;
	}

	/*
	 * Allocate RX descriptor rings and buffers
	 */ 
	sc->rx_rings = kmalloc(sizeof(struct ix_rx_ring) * sc->rx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct ix_rx_ring *rxr = &sc->rx_rings[i];

		rxr->rx_sc = sc;
		rxr->rx_idx = i;
		rxr->rx_intr_vec = -1;
		lwkt_serialize_init(&rxr->rx_serialize);

		error = ix_create_rx_ring(rxr);
		if (error)
			return error;
	}

	return 0;
}

static int
ix_create_tx_ring(struct ix_tx_ring *txr)
{
	int error, i, tsize, ntxd;

	/*
	 * Validate number of transmit descriptors.  It must not exceed
	 * hardware maximum, and must be multiple of IX_DBA_ALIGN.
	 */
	ntxd = device_getenv_int(txr->tx_sc->dev, "txd", ix_txd);
	if (((ntxd * sizeof(union ixgbe_adv_tx_desc)) % IX_DBA_ALIGN) != 0 ||
	    ntxd < IX_MIN_TXD || ntxd > IX_MAX_TXD) {
		device_printf(txr->tx_sc->dev,
		    "Using %d TX descriptors instead of %d!\n",
		    IX_DEF_TXD, ntxd);
		txr->tx_ndesc = IX_DEF_TXD;
	} else {
		txr->tx_ndesc = ntxd;
	}

	/*
	 * Allocate TX head write-back buffer
	 */
	txr->tx_hdr = bus_dmamem_coherent_any(txr->tx_sc->parent_tag,
	    __VM_CACHELINE_SIZE, __VM_CACHELINE_SIZE, BUS_DMA_WAITOK,
	    &txr->tx_hdr_dtag, &txr->tx_hdr_map, &txr->tx_hdr_paddr);
	if (txr->tx_hdr == NULL) {
		device_printf(txr->tx_sc->dev,
		    "Unable to allocate TX head write-back buffer\n");
		return ENOMEM;
	}

	/*
	 * Allocate TX descriptor ring
	 */
	tsize = roundup2(txr->tx_ndesc * sizeof(union ixgbe_adv_tx_desc),
	    IX_DBA_ALIGN);
	txr->tx_base = bus_dmamem_coherent_any(txr->tx_sc->parent_tag,
	    IX_DBA_ALIGN, tsize, BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &txr->tx_base_dtag, &txr->tx_base_map, &txr->tx_base_paddr);
	if (txr->tx_base == NULL) {
		device_printf(txr->tx_sc->dev,
		    "Unable to allocate TX Descriptor memory\n");
		return ENOMEM;
	}

	tsize = __VM_CACHELINE_ALIGN(sizeof(struct ix_tx_buf) * txr->tx_ndesc);
	txr->tx_buf = kmalloc(tsize, M_DEVBUF,
			      M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create DMA tag for TX buffers
	 */
	error = bus_dma_tag_create(txr->tx_sc->parent_tag,
	    1, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    IX_TSO_SIZE,	/* maxsize */
	    IX_MAX_SCATTER,	/* nsegments */
	    PAGE_SIZE,		/* maxsegsize */
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
	    BUS_DMA_ONEBPAGE,	/* flags */
	    &txr->tx_tag);
	if (error) {
		device_printf(txr->tx_sc->dev,
		    "Unable to allocate TX DMA tag\n");
		kfree(txr->tx_buf, M_DEVBUF);
		txr->tx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for TX buffers
	 */
	for (i = 0; i < txr->tx_ndesc; ++i) {
		struct ix_tx_buf *txbuf = &txr->tx_buf[i];

		error = bus_dmamap_create(txr->tx_tag,
		    BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE, &txbuf->map);
		if (error) {
			device_printf(txr->tx_sc->dev,
			    "Unable to create TX DMA map\n");
			ix_destroy_tx_ring(txr, i);
			return error;
		}
	}

	/*
	 * Initialize various watermark
	 */
	txr->tx_wreg_nsegs = IX_DEF_TXWREG_NSEGS;
	txr->tx_intr_nsegs = txr->tx_ndesc / 16;

	return 0;
}

static void
ix_destroy_tx_ring(struct ix_tx_ring *txr, int ndesc)
{
	int i;

	if (txr->tx_hdr != NULL) {
		bus_dmamap_unload(txr->tx_hdr_dtag, txr->tx_hdr_map);
		bus_dmamem_free(txr->tx_hdr_dtag,
		    __DEVOLATILE(void *, txr->tx_hdr), txr->tx_hdr_map);
		bus_dma_tag_destroy(txr->tx_hdr_dtag);
		txr->tx_hdr = NULL;
	}

	if (txr->tx_base != NULL) {
		bus_dmamap_unload(txr->tx_base_dtag, txr->tx_base_map);
		bus_dmamem_free(txr->tx_base_dtag, txr->tx_base,
		    txr->tx_base_map);
		bus_dma_tag_destroy(txr->tx_base_dtag);
		txr->tx_base = NULL;
	}

	if (txr->tx_buf == NULL)
		return;

	for (i = 0; i < ndesc; ++i) {
		struct ix_tx_buf *txbuf = &txr->tx_buf[i];

		KKASSERT(txbuf->m_head == NULL);
		bus_dmamap_destroy(txr->tx_tag, txbuf->map);
	}
	bus_dma_tag_destroy(txr->tx_tag);

	kfree(txr->tx_buf, M_DEVBUF);
	txr->tx_buf = NULL;
}

static void
ix_init_tx_ring(struct ix_tx_ring *txr)
{
	/* Clear the old ring contents */
	bzero(txr->tx_base, sizeof(union ixgbe_adv_tx_desc) * txr->tx_ndesc);

	/* Clear TX head write-back buffer */
	*(txr->tx_hdr) = 0;

	/* Reset indices */
	txr->tx_next_avail = 0;
	txr->tx_next_clean = 0;
	txr->tx_nsegs = 0;
	txr->tx_nmbuf = 0;
	txr->tx_running = 0;

	/* Set number of descriptors available */
	txr->tx_avail = txr->tx_ndesc;

	/* Enable this TX ring */
	txr->tx_flags |= IX_TXFLAG_ENABLED;
}

static void
ix_init_tx_unit(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;
	int i;

	/*
	 * Setup the Base and Length of the Tx Descriptor Ring
	 */
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];
		uint64_t tdba = txr->tx_base_paddr;
		uint64_t hdr_paddr = txr->tx_hdr_paddr;
		uint32_t txctrl;

		IXGBE_WRITE_REG(hw, IXGBE_TDBAL(i), (uint32_t)tdba);
		IXGBE_WRITE_REG(hw, IXGBE_TDBAH(i), (uint32_t)(tdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDLEN(i),
		    txr->tx_ndesc * sizeof(union ixgbe_adv_tx_desc));

		/* Setup the HW Tx Head and Tail descriptor pointers */
		IXGBE_WRITE_REG(hw, IXGBE_TDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TDT(i), 0);

		/* Disable TX head write-back relax ordering */
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(i));
			break;
		default:
			txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL_82599(i));
			break;
		}
		txctrl &= ~IXGBE_DCA_TXCTRL_DESC_WRO_EN;
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL(i), txctrl);
			break;
		default:
			IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL_82599(i), txctrl);
			break;
		}

		/* Enable TX head write-back */
		IXGBE_WRITE_REG(hw, IXGBE_TDWBAH(i),
		    (uint32_t)(hdr_paddr >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDWBAL(i),
		    ((uint32_t)hdr_paddr) | IXGBE_TDWBAL_HEAD_WB_ENABLE);
	}

	if (hw->mac.type != ixgbe_mac_82598EB) {
		uint32_t dmatxctl, rttdcs;

		dmatxctl = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
		dmatxctl |= IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, dmatxctl);

		/* Disable arbiter to set MTQC */
		rttdcs = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
		rttdcs |= IXGBE_RTTDCS_ARBDIS;
		IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);

		IXGBE_WRITE_REG(hw, IXGBE_MTQC, IXGBE_MTQC_64Q_1PB);

		/* Reenable aribter */
		rttdcs &= ~IXGBE_RTTDCS_ARBDIS;
		IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);
	}
}

static int
ix_tx_ctx_setup(struct ix_tx_ring *txr, const struct mbuf *mp,
    uint32_t *cmd_type_len, uint32_t *olinfo_status)
{
	struct ixgbe_adv_tx_context_desc *TXD;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0;
	int ehdrlen, ip_hlen = 0, ctxd;
	boolean_t offload = TRUE;

	/* First check if TSO is to be used */
	if (mp->m_pkthdr.csum_flags & CSUM_TSO) {
		return ix_tso_ctx_setup(txr, mp,
		    cmd_type_len, olinfo_status);
	}

	if ((mp->m_pkthdr.csum_flags & CSUM_OFFLOAD) == 0)
		offload = FALSE;

	/* Indicate the whole packet as payload when not doing TSO */
	*olinfo_status |= mp->m_pkthdr.len << IXGBE_ADVTXD_PAYLEN_SHIFT;

	/*
	 * In advanced descriptors the vlan tag must be placed into the
	 * context descriptor.  Hence we need to make one even if not
	 * doing checksum offloads.
	 */
	if (mp->m_flags & M_VLANTAG) {
		vlan_macip_lens |= htole16(mp->m_pkthdr.ether_vlantag) <<
		    IXGBE_ADVTXD_VLAN_SHIFT;
	} else if (!offload) {
		/* No TX descriptor is consumed */
		return 0;
	}

	/* Set the ether header length */
	ehdrlen = mp->m_pkthdr.csum_lhlen;
	KASSERT(ehdrlen > 0, ("invalid ether hlen"));
	vlan_macip_lens |= ehdrlen << IXGBE_ADVTXD_MACLEN_SHIFT;

	if (mp->m_pkthdr.csum_flags & CSUM_IP) {
		*olinfo_status |= IXGBE_TXD_POPTS_IXSM << 8;
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
		ip_hlen = mp->m_pkthdr.csum_iphlen;
		KASSERT(ip_hlen > 0, ("invalid ip hlen"));
	}
	vlan_macip_lens |= ip_hlen;

	type_tucmd_mlhl |= IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;
	if (mp->m_pkthdr.csum_flags & CSUM_TCP)
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
	else if (mp->m_pkthdr.csum_flags & CSUM_UDP)
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_UDP;

	if (mp->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
		*olinfo_status |= IXGBE_TXD_POPTS_TXSM << 8;

	/* Now ready a context descriptor */
	ctxd = txr->tx_next_avail;
	TXD = (struct ixgbe_adv_tx_context_desc *)&txr->tx_base[ctxd];

	/* Now copy bits into descriptor */
	TXD->vlan_macip_lens = htole32(vlan_macip_lens);
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	TXD->seqnum_seed = htole32(0);
	TXD->mss_l4len_idx = htole32(0);

	/* We've consumed the first desc, adjust counters */
	if (++ctxd == txr->tx_ndesc)
		ctxd = 0;
	txr->tx_next_avail = ctxd;
	--txr->tx_avail;

	/* One TX descriptor is consumed */
	return 1;
}

static int
ix_tso_ctx_setup(struct ix_tx_ring *txr, const struct mbuf *mp,
    uint32_t *cmd_type_len, uint32_t *olinfo_status)
{
	struct ixgbe_adv_tx_context_desc *TXD;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0;
	uint32_t mss_l4len_idx = 0, paylen;
	int ctxd, ehdrlen, ip_hlen, tcp_hlen;

	ehdrlen = mp->m_pkthdr.csum_lhlen;
	KASSERT(ehdrlen > 0, ("invalid ether hlen"));

	ip_hlen = mp->m_pkthdr.csum_iphlen;
	KASSERT(ip_hlen > 0, ("invalid ip hlen"));

	tcp_hlen = mp->m_pkthdr.csum_thlen;
	KASSERT(tcp_hlen > 0, ("invalid tcp hlen"));

	ctxd = txr->tx_next_avail;
	TXD = (struct ixgbe_adv_tx_context_desc *) &txr->tx_base[ctxd];

	if (mp->m_flags & M_VLANTAG) {
		vlan_macip_lens |= htole16(mp->m_pkthdr.ether_vlantag) <<
		    IXGBE_ADVTXD_VLAN_SHIFT;
	}
	vlan_macip_lens |= ehdrlen << IXGBE_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= ip_hlen;
	TXD->vlan_macip_lens = htole32(vlan_macip_lens);

	/* ADV DTYPE TUCMD */
	type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
	type_tucmd_mlhl |= IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;
	type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);

	/* MSS L4LEN IDX */
	mss_l4len_idx |= (mp->m_pkthdr.tso_segsz << IXGBE_ADVTXD_MSS_SHIFT);
	mss_l4len_idx |= (tcp_hlen << IXGBE_ADVTXD_L4LEN_SHIFT);
	TXD->mss_l4len_idx = htole32(mss_l4len_idx);

	TXD->seqnum_seed = htole32(0);

	if (++ctxd == txr->tx_ndesc)
		ctxd = 0;

	txr->tx_avail--;
	txr->tx_next_avail = ctxd;

	*cmd_type_len |= IXGBE_ADVTXD_DCMD_TSE;

	/* This is used in the transmit desc in encap */
	paylen = mp->m_pkthdr.len - ehdrlen - ip_hlen - tcp_hlen;

	*olinfo_status |= IXGBE_TXD_POPTS_IXSM << 8;
	*olinfo_status |= IXGBE_TXD_POPTS_TXSM << 8;
	*olinfo_status |= paylen << IXGBE_ADVTXD_PAYLEN_SHIFT;

	/* One TX descriptor is consumed */
	return 1;
}

static void
ix_txeof(struct ix_tx_ring *txr, int hdr)
{
	int first, avail;

	if (txr->tx_avail == txr->tx_ndesc)
		return;

	first = txr->tx_next_clean;
	if (first == hdr)
		return;

	avail = txr->tx_avail;
	while (first != hdr) {
		struct ix_tx_buf *txbuf = &txr->tx_buf[first];

		KKASSERT(avail < txr->tx_ndesc);
		++avail;

		if (txbuf->m_head != NULL)
			ix_free_txbuf(txr, txbuf);
		if (++first == txr->tx_ndesc)
			first = 0;
	}
	txr->tx_next_clean = first;
	txr->tx_avail = avail;

	if (txr->tx_avail > IX_MAX_SCATTER + IX_TX_RESERVED) {
		ifsq_clr_oactive(txr->tx_ifsq);
		ifsq_watchdog_set_count(&txr->tx_watchdog, 0);
	}
	txr->tx_running = IX_TX_RUNNING;
}

static void
ix_txgc(struct ix_tx_ring *txr)
{
	int first, hdr;
#ifdef INVARIANTS
	int avail;
#endif

	if (txr->tx_avail == txr->tx_ndesc)
		return;

	hdr = IXGBE_READ_REG(&txr->tx_sc->hw, IXGBE_TDH(txr->tx_idx));
	first = txr->tx_next_clean;
	if (first == hdr)
		goto done;
	txr->tx_gc++;

#ifdef INVARIANTS
	avail = txr->tx_avail;
#endif
	while (first != hdr) {
		struct ix_tx_buf *txbuf = &txr->tx_buf[first];

#ifdef INVARIANTS
		KKASSERT(avail < txr->tx_ndesc);
		++avail;
#endif
		if (txbuf->m_head != NULL)
			ix_free_txbuf(txr, txbuf);
		if (++first == txr->tx_ndesc)
			first = 0;
	}
done:
	if (txr->tx_nmbuf)
		txr->tx_running = IX_TX_RUNNING;
}

static int
ix_create_rx_ring(struct ix_rx_ring *rxr)
{
	int i, rsize, error, nrxd;

	/*
	 * Validate number of receive descriptors.  It must not exceed
	 * hardware maximum, and must be multiple of IX_DBA_ALIGN.
	 */
	nrxd = device_getenv_int(rxr->rx_sc->dev, "rxd", ix_rxd);
	if (((nrxd * sizeof(union ixgbe_adv_rx_desc)) % IX_DBA_ALIGN) != 0 ||
	    nrxd < IX_MIN_RXD || nrxd > IX_MAX_RXD) {
		device_printf(rxr->rx_sc->dev,
		    "Using %d RX descriptors instead of %d!\n",
		    IX_DEF_RXD, nrxd);
		rxr->rx_ndesc = IX_DEF_RXD;
	} else {
		rxr->rx_ndesc = nrxd;
	}

	/*
	 * Allocate RX descriptor ring
	 */
	rsize = roundup2(rxr->rx_ndesc * sizeof(union ixgbe_adv_rx_desc),
	    IX_DBA_ALIGN);
	rxr->rx_base = bus_dmamem_coherent_any(rxr->rx_sc->parent_tag,
	    IX_DBA_ALIGN, rsize, BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &rxr->rx_base_dtag, &rxr->rx_base_map, &rxr->rx_base_paddr);
	if (rxr->rx_base == NULL) {
		device_printf(rxr->rx_sc->dev,
		    "Unable to allocate TX Descriptor memory\n");
		return ENOMEM;
	}

	rsize = __VM_CACHELINE_ALIGN(sizeof(struct ix_rx_buf) * rxr->rx_ndesc);
	rxr->rx_buf = kmalloc(rsize, M_DEVBUF,
			      M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create DMA tag for RX buffers
	 */
	error = bus_dma_tag_create(rxr->rx_sc->parent_tag,
	    1, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    PAGE_SIZE,		/* maxsize */
	    1,			/* nsegments */
	    PAGE_SIZE,		/* maxsegsize */
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, /* flags */
	    &rxr->rx_tag);
	if (error) {
		device_printf(rxr->rx_sc->dev,
		    "Unable to create RX DMA tag\n");
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
		device_printf(rxr->rx_sc->dev,
		    "Unable to create spare RX DMA map\n");
		bus_dma_tag_destroy(rxr->rx_tag);
		kfree(rxr->rx_buf, M_DEVBUF);
		rxr->rx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for RX buffers
	 */
	for (i = 0; i < rxr->rx_ndesc; ++i) {
		struct ix_rx_buf *rxbuf = &rxr->rx_buf[i];

		error = bus_dmamap_create(rxr->rx_tag,
		    BUS_DMA_WAITOK, &rxbuf->map);
		if (error) {
			device_printf(rxr->rx_sc->dev,
			    "Unable to create RX dma map\n");
			ix_destroy_rx_ring(rxr, i);
			return error;
		}
	}

	/*
	 * Initialize various watermark
	 */
	rxr->rx_wreg_nsegs = IX_DEF_RXWREG_NSEGS;

	return 0;
}

static void
ix_destroy_rx_ring(struct ix_rx_ring *rxr, int ndesc)
{
	int i;

	if (rxr->rx_base != NULL) {
		bus_dmamap_unload(rxr->rx_base_dtag, rxr->rx_base_map);
		bus_dmamem_free(rxr->rx_base_dtag, rxr->rx_base,
		    rxr->rx_base_map);
		bus_dma_tag_destroy(rxr->rx_base_dtag);
		rxr->rx_base = NULL;
	}

	if (rxr->rx_buf == NULL)
		return;

	for (i = 0; i < ndesc; ++i) {
		struct ix_rx_buf *rxbuf = &rxr->rx_buf[i];

		KKASSERT(rxbuf->m_head == NULL);
		bus_dmamap_destroy(rxr->rx_tag, rxbuf->map);
	}
	bus_dmamap_destroy(rxr->rx_tag, rxr->rx_sparemap);
	bus_dma_tag_destroy(rxr->rx_tag);

	kfree(rxr->rx_buf, M_DEVBUF);
	rxr->rx_buf = NULL;
}

/*
** Used to detect a descriptor that has
** been merged by Hardware RSC.
*/
static __inline uint32_t
ix_rsc_count(union ixgbe_adv_rx_desc *rx)
{
	return (le32toh(rx->wb.lower.lo_dword.data) &
	    IXGBE_RXDADV_RSCCNT_MASK) >> IXGBE_RXDADV_RSCCNT_SHIFT;
}

#if 0
/*********************************************************************
 *
 *  Initialize Hardware RSC (LRO) feature on 82599
 *  for an RX ring, this is toggled by the LRO capability
 *  even though it is transparent to the stack.
 *
 *  NOTE: since this HW feature only works with IPV4 and 
 *        our testing has shown soft LRO to be as effective
 *        I have decided to disable this by default.
 *
 **********************************************************************/
static void
ix_setup_hw_rsc(struct ix_rx_ring *rxr)
{
	struct	ix_softc 	*sc = rxr->rx_sc;
	struct	ixgbe_hw	*hw = &sc->hw;
	uint32_t			rscctrl, rdrxctl;

#if 0
	/* If turning LRO/RSC off we need to disable it */
	if ((sc->arpcom.ac_if.if_capenable & IFCAP_LRO) == 0) {
		rscctrl = IXGBE_READ_REG(hw, IXGBE_RSCCTL(rxr->me));
		rscctrl &= ~IXGBE_RSCCTL_RSCEN;
		return;
	}
#endif

	rdrxctl = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	rdrxctl &= ~IXGBE_RDRXCTL_RSCFRSTSIZE;
	rdrxctl |= IXGBE_RDRXCTL_CRCSTRIP;
	rdrxctl |= IXGBE_RDRXCTL_RSCACKC;
	IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, rdrxctl);

	rscctrl = IXGBE_READ_REG(hw, IXGBE_RSCCTL(rxr->me));
	rscctrl |= IXGBE_RSCCTL_RSCEN;
	/*
	** Limit the total number of descriptors that
	** can be combined, so it does not exceed 64K
	*/
	if (rxr->mbuf_sz == MCLBYTES)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_16;
	else if (rxr->mbuf_sz == MJUMPAGESIZE)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_8;
	else if (rxr->mbuf_sz == MJUM9BYTES)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_4;
	else  /* Using 16K cluster */
		rscctrl |= IXGBE_RSCCTL_MAXDESC_1;

	IXGBE_WRITE_REG(hw, IXGBE_RSCCTL(rxr->me), rscctrl);

	/* Enable TCP header recognition */
	IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(0),
	    (IXGBE_READ_REG(hw, IXGBE_PSRTYPE(0)) |
	    IXGBE_PSRTYPE_TCPHDR));

	/* Disable RSC for ACK packets */
	IXGBE_WRITE_REG(hw, IXGBE_RSCDBU,
	    (IXGBE_RSCDBU_RSCACKDIS | IXGBE_READ_REG(hw, IXGBE_RSCDBU)));

	rxr->hw_rsc = TRUE;
}
#endif

static int
ix_init_rx_ring(struct ix_rx_ring *rxr)
{
	int i;

	/* Clear the ring contents */
	bzero(rxr->rx_base, rxr->rx_ndesc * sizeof(union ixgbe_adv_rx_desc));

	/* XXX we need JUMPAGESIZE for RSC too */
	if (rxr->rx_sc->max_frame_size <= MCLBYTES)
		rxr->rx_mbuf_sz = MCLBYTES;
	else
		rxr->rx_mbuf_sz = MJUMPAGESIZE;

	/* Now replenish the mbufs */
	for (i = 0; i < rxr->rx_ndesc; ++i) {
		int error;

		error = ix_newbuf(rxr, i, TRUE);
		if (error)
			return error;
	}

	/* Setup our descriptor indices */
	rxr->rx_next_check = 0;
	rxr->rx_flags &= ~IX_RXRING_FLAG_DISC;

#if 0
	/*
	** Now set up the LRO interface:
	*/
	if (ixgbe_rsc_enable)
		ix_setup_hw_rsc(rxr);
#endif

	return 0;
}

#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT 2

#define BSIZEPKT_ROUNDUP ((1<<IXGBE_SRRCTL_BSIZEPKT_SHIFT)-1)
	
static void
ix_init_rx_unit(struct ix_softc *sc, boolean_t polling)
{
	struct ixgbe_hw	*hw = &sc->hw;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t bufsz, fctrl, rxcsum, hlreg;
	int i;

	/*
	 * Make sure receives are disabled while setting up the descriptor ring
	 */
	ixgbe_disable_rx(hw);

	/* Enable broadcasts */
	fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	fctrl |= IXGBE_FCTRL_BAM;
	if (hw->mac.type == ixgbe_mac_82598EB) {
		fctrl |= IXGBE_FCTRL_DPF;
		fctrl |= IXGBE_FCTRL_PMCF;
	}
	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);

	/* Set for Jumbo Frames? */
	hlreg = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	if (ifp->if_mtu > ETHERMTU)
		hlreg |= IXGBE_HLREG0_JUMBOEN;
	else
		hlreg &= ~IXGBE_HLREG0_JUMBOEN;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg);

	KKASSERT(sc->rx_rings[0].rx_mbuf_sz >= MCLBYTES);
	bufsz = (sc->rx_rings[0].rx_mbuf_sz + BSIZEPKT_ROUNDUP) >>
	    IXGBE_SRRCTL_BSIZEPKT_SHIFT;

	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		struct ix_rx_ring *rxr = &sc->rx_rings[i];
		uint64_t rdba = rxr->rx_base_paddr;
		uint32_t srrctl;

		/* Setup the Base and Length of the Rx Descriptor Ring */
		IXGBE_WRITE_REG(hw, IXGBE_RDBAL(i), (uint32_t)rdba);
		IXGBE_WRITE_REG(hw, IXGBE_RDBAH(i), (uint32_t)(rdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_RDLEN(i),
		    rxr->rx_ndesc * sizeof(union ixgbe_adv_rx_desc));

		/*
		 * Set up the SRRCTL register
		 */
		srrctl = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));

		srrctl &= ~IXGBE_SRRCTL_BSIZEHDR_MASK;
		srrctl &= ~IXGBE_SRRCTL_BSIZEPKT_MASK;
		srrctl |= bufsz;
		srrctl |= IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
		if (sc->rx_ring_inuse > 1) {
			/* See the commend near ix_enable_rx_drop() */
			if (sc->ifm_media &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
				srrctl &= ~IXGBE_SRRCTL_DROP_EN;
				if (i == 0 && bootverbose) {
					if_printf(ifp, "flow control %s, "
					    "disable RX drop\n",
					    ix_ifmedia2str(sc->ifm_media));
				}
			} else {
				srrctl |= IXGBE_SRRCTL_DROP_EN;
				if (i == 0 && bootverbose) {
					if_printf(ifp, "flow control %s, "
					    "enable RX drop\n",
					    ix_ifmedia2str(sc->ifm_media));
				}
			}
		}
		IXGBE_WRITE_REG(hw, IXGBE_SRRCTL(i), srrctl);

		/* Setup the HW Rx Head and Tail Descriptor Pointers */
		IXGBE_WRITE_REG(hw, IXGBE_RDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RDT(i), 0);
	}

	if (sc->hw.mac.type != ixgbe_mac_82598EB)
		IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(0), 0);

	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);

	/*
	 * Setup RSS
	 */
	if (sc->rx_ring_inuse > 1) {
		uint8_t key[IX_NRSSRK * IX_RSSRK_SIZE];
		const struct if_ringmap *rm;
		int j, r, nreta, table_nent;

		/*
		 * NOTE:
		 * When we reach here, RSS has already been disabled
		 * in ix_stop(), so we could safely configure RSS key
		 * and redirect table.
		 */

		/*
		 * Configure RSS key
		 */
		toeplitz_get_key(key, sizeof(key));
		for (i = 0; i < IX_NRSSRK; ++i) {
			uint32_t rssrk;

			rssrk = IX_RSSRK_VAL(key, i);
			IX_RSS_DPRINTF(sc, 1, "rssrk%d 0x%08x\n",
			    i, rssrk);

			IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), rssrk);
		}

		/*
		 * Configure RSS redirect table.
		 */

		/* Table size will differ based on MAC */
		switch (hw->mac.type) {
		case ixgbe_mac_X550:
		case ixgbe_mac_X550EM_x:
		case ixgbe_mac_X550EM_a:
			nreta = IX_NRETA_X550;
			break;
		default:
			nreta = IX_NRETA;
			break;
		}

		table_nent = nreta * IX_RETA_SIZE;
		KASSERT(table_nent <= IX_RDRTABLE_SIZE,
		    ("invalid RETA count %d", nreta));
		if (polling)
			rm = sc->rx_rmap;
		else
			rm = sc->rx_rmap_intr;
		if_ringmap_rdrtable(rm, sc->rdr_table, table_nent);

		r = 0;
		for (j = 0; j < nreta; ++j) {
			uint32_t reta = 0;

			for (i = 0; i < IX_RETA_SIZE; ++i) {
				uint32_t q;

				q = sc->rdr_table[r];
				KASSERT(q < sc->rx_ring_inuse,
				    ("invalid RX ring index %d", q));
				reta |= q << (8 * i);
				++r;
			}
			IX_RSS_DPRINTF(sc, 1, "reta 0x%08x\n", reta);
			if (j < IX_NRETA) {
				IXGBE_WRITE_REG(hw, IXGBE_RETA(j), reta);
			} else {
				IXGBE_WRITE_REG(hw, IXGBE_ERETA(j - IX_NRETA),
				    reta);
			}
		}

		/*
		 * Enable multiple receive queues.
		 * Enable IPv4 RSS standard hash functions.
		 */
		IXGBE_WRITE_REG(hw, IXGBE_MRQC,
		    IXGBE_MRQC_RSSEN |
		    IXGBE_MRQC_RSS_FIELD_IPV4 |
		    IXGBE_MRQC_RSS_FIELD_IPV4_TCP);

		/*
		 * NOTE:
		 * PCSD must be enabled to enable multiple
		 * receive queues.
		 */
		rxcsum |= IXGBE_RXCSUM_PCSD;
	}

	if (ifp->if_capenable & IFCAP_RXCSUM)
		rxcsum |= IXGBE_RXCSUM_PCSD;

	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
}

static __inline void
ix_rx_refresh(struct ix_rx_ring *rxr, int i)
{
	if (--i < 0)
		i = rxr->rx_ndesc - 1;
	IXGBE_WRITE_REG(&rxr->rx_sc->hw, IXGBE_RDT(rxr->rx_idx), i);
}

static __inline void
ix_rxcsum(uint32_t staterr, struct mbuf *mp, uint32_t ptype)
{
	if ((ptype &
	     (IXGBE_RXDADV_PKTTYPE_IPV4 | IXGBE_RXDADV_PKTTYPE_IPV4_EX)) == 0) {
		/* Not IPv4 */
		return;
	}

	if ((staterr & (IXGBE_RXD_STAT_IPCS | IXGBE_RXDADV_ERR_IPE)) ==
	    IXGBE_RXD_STAT_IPCS)
		mp->m_pkthdr.csum_flags |= CSUM_IP_CHECKED | CSUM_IP_VALID;

	if ((ptype &
	     (IXGBE_RXDADV_PKTTYPE_TCP | IXGBE_RXDADV_PKTTYPE_UDP)) == 0) {
		/*
		 * - Neither TCP nor UDP
		 * - IPv4 fragment
		 */
		return;
	}

	if ((staterr & (IXGBE_RXD_STAT_L4CS | IXGBE_RXDADV_ERR_TCPE)) ==
	    IXGBE_RXD_STAT_L4CS) {
		mp->m_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR |
		    CSUM_FRAG_NOT_CHECKED;
		mp->m_pkthdr.csum_data = htons(0xffff);
	}
}

static __inline struct pktinfo *
ix_rssinfo(struct mbuf *m, struct pktinfo *pi,
    uint32_t hash, uint32_t hashtype, uint32_t ptype)
{
	switch (hashtype) {
	case IXGBE_RXDADV_RSSTYPE_IPV4_TCP:
		pi->pi_netisr = NETISR_IP;
		pi->pi_flags = 0;
		pi->pi_l3proto = IPPROTO_TCP;
		break;

	case IXGBE_RXDADV_RSSTYPE_IPV4:
		if ((ptype & IXGBE_RXDADV_PKTTYPE_UDP) == 0) {
			/* Not UDP or is fragment */
			return NULL;
		}
		pi->pi_netisr = NETISR_IP;
		pi->pi_flags = 0;
		pi->pi_l3proto = IPPROTO_UDP;
		break;

	default:
		return NULL;
	}

	m_sethash(m, toeplitz_hash(hash));
	return pi;
}

static __inline void
ix_setup_rxdesc(union ixgbe_adv_rx_desc *rxd, const struct ix_rx_buf *rxbuf)
{
	rxd->read.pkt_addr = htole64(rxbuf->paddr);
	rxd->wb.upper.status_error = 0;
}

static void
ix_rx_discard(struct ix_rx_ring *rxr, int i, boolean_t eop)
{
	struct ix_rx_buf *rxbuf = &rxr->rx_buf[i];

	/*
	 * XXX discard may not be correct
	 */
	if (eop) {
		IFNET_STAT_INC(&rxr->rx_sc->arpcom.ac_if, ierrors, 1);
		rxr->rx_flags &= ~IX_RXRING_FLAG_DISC;
	} else {
		rxr->rx_flags |= IX_RXRING_FLAG_DISC;
	}
	if (rxbuf->fmp != NULL) {
		m_freem(rxbuf->fmp);
		rxbuf->fmp = NULL;
		rxbuf->lmp = NULL;
	}
	ix_setup_rxdesc(&rxr->rx_base[i], rxbuf);
}

static void
ix_rxeof(struct ix_rx_ring *rxr, int count)
{
	struct ifnet *ifp = &rxr->rx_sc->arpcom.ac_if;
	int i, nsegs = 0, cpuid = mycpuid;

	i = rxr->rx_next_check;
	while (count != 0) {
		struct ix_rx_buf *rxbuf, *nbuf = NULL;
		union ixgbe_adv_rx_desc	*cur;
		struct mbuf *sendmp = NULL, *mp;
		struct pktinfo *pi = NULL, pi0;
		uint32_t rsc = 0, ptype, staterr, hash, hashtype;
		uint16_t len;
		boolean_t eop;

		cur = &rxr->rx_base[i];
		staterr = le32toh(cur->wb.upper.status_error);

		if ((staterr & IXGBE_RXD_STAT_DD) == 0)
			break;
		++nsegs;

		rxbuf = &rxr->rx_buf[i];
		mp = rxbuf->m_head;

		len = le16toh(cur->wb.upper.length);
		ptype = le32toh(cur->wb.lower.lo_dword.data) &
		    IXGBE_RXDADV_PKTTYPE_MASK;
		hash = le32toh(cur->wb.lower.hi_dword.rss);
		hashtype = le32toh(cur->wb.lower.lo_dword.data) &
		    IXGBE_RXDADV_RSSTYPE_MASK;

		eop = ((staterr & IXGBE_RXD_STAT_EOP) != 0);
		if (eop)
			--count;

		/*
		 * Make sure bad packets are discarded
		 */
		if ((staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) ||
		    (rxr->rx_flags & IX_RXRING_FLAG_DISC)) {
			ix_rx_discard(rxr, i, eop);
			goto next_desc;
		}

		bus_dmamap_sync(rxr->rx_tag, rxbuf->map, BUS_DMASYNC_POSTREAD);
		if (ix_newbuf(rxr, i, FALSE) != 0) {
			ix_rx_discard(rxr, i, eop);
			goto next_desc;
		}

		/*
		 * On 82599 which supports a hardware LRO, packets
		 * need not be fragmented across sequential descriptors,
		 * rather the next descriptor is indicated in bits
		 * of the descriptor.  This also means that we might
		 * proceses more than one packet at a time, something
		 * that has never been true before, it required
		 * eliminating global chain pointers in favor of what
		 * we are doing here.
		 */
		if (!eop) {
			int nextp;

			/*
			 * Figure out the next descriptor
			 * of this frame.
			 */
			if (rxr->rx_flags & IX_RXRING_FLAG_LRO)
				rsc = ix_rsc_count(cur);
			if (rsc) { /* Get hardware index */
				nextp = ((staterr &
				    IXGBE_RXDADV_NEXTP_MASK) >>
				    IXGBE_RXDADV_NEXTP_SHIFT);
			} else { /* Just sequential */
				nextp = i + 1;
				if (nextp == rxr->rx_ndesc)
					nextp = 0;
			}
			nbuf = &rxr->rx_buf[nextp];
			prefetch(nbuf);
		}
		mp->m_len = len;

		/*
		 * Rather than using the fmp/lmp global pointers
		 * we now keep the head of a packet chain in the
		 * buffer struct and pass this along from one
		 * descriptor to the next, until we get EOP.
		 */
		if (rxbuf->fmp == NULL) {
			mp->m_pkthdr.len = len;
			rxbuf->fmp = mp;
			rxbuf->lmp = mp;
		} else {
			rxbuf->fmp->m_pkthdr.len += len;
			rxbuf->lmp->m_next = mp;
			rxbuf->lmp = mp;
		}

		if (nbuf != NULL) {
			/*
			 * Not the last fragment of this frame,
			 * pass this fragment list on
			 */
			nbuf->fmp = rxbuf->fmp;
			nbuf->lmp = rxbuf->lmp;
		} else {
			/*
			 * Send this frame
			 */
			sendmp = rxbuf->fmp;

			sendmp->m_pkthdr.rcvif = ifp;
			IFNET_STAT_INC(ifp, ipackets, 1);
#ifdef IX_RSS_DEBUG
			rxr->rx_pkts++;
#endif

			/* Process vlan info */
			if (staterr & IXGBE_RXD_STAT_VP) {
				sendmp->m_pkthdr.ether_vlantag =
				    le16toh(cur->wb.upper.vlan);
				sendmp->m_flags |= M_VLANTAG;
			}
			if (ifp->if_capenable & IFCAP_RXCSUM)
				ix_rxcsum(staterr, sendmp, ptype);
			if (ifp->if_capenable & IFCAP_RSS) {
				pi = ix_rssinfo(sendmp, &pi0,
				    hash, hashtype, ptype);
			}
		}
		rxbuf->fmp = NULL;
		rxbuf->lmp = NULL;
next_desc:
		/* Advance our pointers to the next descriptor. */
		if (++i == rxr->rx_ndesc)
			i = 0;

		if (sendmp != NULL)
			ifp->if_input(ifp, sendmp, pi, cpuid);

		if (nsegs >= rxr->rx_wreg_nsegs) {
			ix_rx_refresh(rxr, i);
			nsegs = 0;
		}
	}
	rxr->rx_next_check = i;

	if (nsegs > 0)
		ix_rx_refresh(rxr, i);
}

static void
ix_set_vlan(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t ctrl;

	if ((sc->arpcom.ac_if.if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
		return;

	if (hw->mac.type == ixgbe_mac_82598EB) {
		ctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
		ctrl |= IXGBE_VLNCTRL_VME;
		IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, ctrl);
	} else {
		int i;

		/*
		 * On 82599 and later chips the VLAN enable is
		 * per queue in RXDCTL
		 */
		for (i = 0; i < sc->rx_ring_inuse; ++i) {
			ctrl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
			ctrl |= IXGBE_RXDCTL_VME;
			IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(i), ctrl);
		}
	}
}

static void
ix_enable_intr(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t fwsm;
	int i;

	for (i = 0; i < sc->intr_cnt; ++i)
		lwkt_serialize_handler_enable(sc->intr_data[i].intr_serialize);

	sc->intr_mask = (IXGBE_EIMS_ENABLE_MASK & ~IXGBE_EIMS_RTX_QUEUE);

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		sc->intr_mask |= IXGBE_EIMS_ECC;
		/* Temperature sensor on some adapters */
		sc->intr_mask |= IXGBE_EIMS_GPI_SDP0;
		/* SFP+ (RX_LOS_N & MOD_ABS_N) */
		sc->intr_mask |= IXGBE_EIMS_GPI_SDP1;
		sc->intr_mask |= IXGBE_EIMS_GPI_SDP2;
		break;

	case ixgbe_mac_X540:
		sc->intr_mask |= IXGBE_EIMS_ECC;
		/* Detect if Thermal Sensor is enabled */
		fwsm = IXGBE_READ_REG(hw, IXGBE_FWSM);
		if (fwsm & IXGBE_FWSM_TS_ENABLED)
			sc->intr_mask |= IXGBE_EIMS_TS;
		break;

	case ixgbe_mac_X550:
		sc->intr_mask |= IXGBE_EIMS_ECC;
		/* MAC thermal sensor is automatically enabled */
		sc->intr_mask |= IXGBE_EIMS_TS;
		break;

	case ixgbe_mac_X550EM_a:
	case ixgbe_mac_X550EM_x:
		sc->intr_mask |= IXGBE_EIMS_ECC;
		/* Some devices use SDP0 for important information */
		if (hw->device_id == IXGBE_DEV_ID_X550EM_X_SFP ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP_N ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_X_10G_T)
			sc->intr_mask |= IXGBE_EIMS_GPI_SDP0_BY_MAC(hw);
		if (hw->phy.type == ixgbe_phy_x550em_ext_t)
			sc->intr_mask |= IXGBE_EICR_GPI_SDP0_X540;
		break;

	default:
		break;
	}

	/* Enable Fan Failure detection */
	if (sc->caps & IX_CAP_DETECT_FANFAIL)
		sc->intr_mask |= IXGBE_EIMS_GPI_SDP1;

	/* With MSI-X we use auto clear for RX and TX rings */
	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		/*
		 * There are no EIAC1/EIAC2 for newer chips; the related
		 * bits for TX and RX rings > 16 are always auto clear.
		 *
		 * XXX which bits?  There are _no_ documented EICR1 and
		 * EICR2 at all; only EICR.
		 */
		IXGBE_WRITE_REG(hw, IXGBE_EIAC, IXGBE_EIMS_RTX_QUEUE);
	} else {
		sc->intr_mask |= IX_TX_INTR_MASK | IX_RX0_INTR_MASK;

		KKASSERT(sc->rx_ring_inuse <= IX_MIN_RXRING_RSS);
		if (sc->rx_ring_inuse == IX_MIN_RXRING_RSS)
			sc->intr_mask |= IX_RX1_INTR_MASK;
	}

	IXGBE_WRITE_REG(hw, IXGBE_EIMS, sc->intr_mask);

	/*
	 * Enable RX and TX rings for MSI-X
	 */
	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		for (i = 0; i < sc->tx_ring_inuse; ++i) {
			const struct ix_tx_ring *txr = &sc->tx_rings[i];

			if (txr->tx_intr_vec >= 0) {
				IXGBE_WRITE_REG(hw, txr->tx_eims,
				    txr->tx_eims_val);
			}
		}
		for (i = 0; i < sc->rx_ring_inuse; ++i) {
			const struct ix_rx_ring *rxr = &sc->rx_rings[i];

			KKASSERT(rxr->rx_intr_vec >= 0);
			IXGBE_WRITE_REG(hw, rxr->rx_eims, rxr->rx_eims_val);
		}
	}

	IXGBE_WRITE_FLUSH(hw);
}

static void
ix_disable_intr(struct ix_softc *sc)
{
	int i;

	if (sc->intr_type == PCI_INTR_TYPE_MSIX)
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAC, 0);

	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, ~0);
	} else {
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, 0xFFFF0000);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(0), ~0);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(1), ~0);
	}
	IXGBE_WRITE_FLUSH(&sc->hw);

	for (i = 0; i < sc->intr_cnt; ++i)
		lwkt_serialize_handler_disable(sc->intr_data[i].intr_serialize);
}

static void
ix_slot_info(struct ix_softc *sc)
{
	device_t dev = sc->dev;
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t offset;
	uint16_t link;
	boolean_t bus_info_valid = TRUE;

	/* Some devices are behind an internal bridge */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_SFP_SF_QP:
	case IXGBE_DEV_ID_82599_QSFP_SF_QP:
		goto get_parent_info;
	default:
		break;
	}

	ixgbe_get_bus_info(hw);

	/*
	 * Some devices don't use PCI-E, but there is no need
	 * to display "Unknown" for bus speed and width.
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		return;
	default:
		goto display;
	}

get_parent_info:
	/*
	 * For the Quad port adapter we need to parse back up
	 * the PCI tree to find the speed of the expansion slot
	 * into which this adapter is plugged.  A bit more work.
	 */
	dev = device_get_parent(device_get_parent(dev));
#ifdef IXGBE_DEBUG
	device_printf(dev, "parent pcib = %x,%x,%x\n", pci_get_bus(dev),
	    pci_get_slot(dev), pci_get_function(dev));
#endif
	dev = device_get_parent(device_get_parent(dev));
#ifdef IXGBE_DEBUG
	device_printf(dev, "slot pcib = %x,%x,%x\n", pci_get_bus(dev),
	    pci_get_slot(dev), pci_get_function(dev));
#endif
	/* Now get the PCI Express Capabilities offset */
	offset = pci_get_pciecap_ptr(dev);
	if (offset == 0) {
		/*
		 * Hmm...can't get PCI-Express capabilities.
		 * Falling back to default method.
		 */
		bus_info_valid = FALSE;
		ixgbe_get_bus_info(hw);
		goto display;
	}
	/* ...and read the Link Status Register */
	link = pci_read_config(dev, offset + PCIER_LINKSTAT, 2);
	ixgbe_set_pci_config_data_generic(hw, link);

display:
	device_printf(dev, "PCI Express Bus: Speed %s %s\n",
	    hw->bus.speed == ixgbe_bus_speed_8000 ? "8.0GT/s" :
	    hw->bus.speed == ixgbe_bus_speed_5000 ? "5.0GT/s" :
	    hw->bus.speed == ixgbe_bus_speed_2500 ? "2.5GT/s" : "Unknown",
	    hw->bus.width == ixgbe_bus_width_pcie_x8 ? "Width x8" :
	    hw->bus.width == ixgbe_bus_width_pcie_x4 ? "Width x4" :
	    hw->bus.width == ixgbe_bus_width_pcie_x1 ? "Width x1" : "Unknown");

	if (bus_info_valid) {
		if (hw->device_id != IXGBE_DEV_ID_82599_SFP_SF_QP &&
		    hw->bus.width <= ixgbe_bus_width_pcie_x4 &&
		    hw->bus.speed == ixgbe_bus_speed_2500) {
			device_printf(dev, "PCI-Express bandwidth available "
			    "for this card is not sufficient for optimal "
			    "performance.\n");
			device_printf(dev, "For optimal performance a "
			    "x8 PCIE, or x4 PCIE Gen2 slot is required.\n");
		}
		if (hw->device_id == IXGBE_DEV_ID_82599_SFP_SF_QP &&
		    hw->bus.width <= ixgbe_bus_width_pcie_x8 &&
		    hw->bus.speed < ixgbe_bus_speed_8000) {
			device_printf(dev, "PCI-Express bandwidth available "
			    "for this card is not sufficient for optimal "
			    "performance.\n");
			device_printf(dev, "For optimal performance a "
			    "x8 PCIE Gen3 slot is required.\n");
		}
	} else {
		device_printf(dev, "Unable to determine slot speed/width.  "
		    "The speed/width reported are that of the internal "
		    "switch.\n");
	}
}

/*
 * TODO comment is incorrect
 *
 * Setup the correct IVAR register for a particular MSIX interrupt
 * - entry is the register array entry
 * - vector is the MSIX vector for this queue
 * - type is RX/TX/MISC
 */
static void
ix_set_ivar(struct ix_softc *sc, uint8_t entry, uint8_t vector,
    int8_t type)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t ivar, index;

	vector |= IXGBE_IVAR_ALLOC_VAL;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		if (type == -1)
			entry = IXGBE_IVAR_OTHER_CAUSES_INDEX;
		else
			entry += (type * 64);
		index = (entry >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar &= ~(0xFF << (8 * (entry & 0x3)));
		ivar |= (vector << (8 * (entry & 0x3)));
		IXGBE_WRITE_REG(hw, IXGBE_IVAR(index), ivar);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_a:
	case ixgbe_mac_X550EM_x:
		if (type == -1) { /* MISC IVAR */
			index = (entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR_MISC);
			ivar &= ~(0xFF << index);
			ivar |= (vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, ivar);
		} else {	/* RX/TX IVARS */
			index = (16 * (entry & 1)) + (8 * type);
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(entry >> 1));
			ivar &= ~(0xFF << index);
			ivar |= (vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(entry >> 1), ivar);
		}
		/* FALL THROUGH */
	default:
		break;
	}
}

static boolean_t
ix_sfp_probe(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;

	if (hw->phy.type == ixgbe_phy_nl &&
	    hw->phy.sfp_type == ixgbe_sfp_type_not_present) {
		int32_t ret;

		ret = hw->phy.ops.identify_sfp(hw);
		if (ret)
			return FALSE;

		ret = hw->phy.ops.reset(hw);
		sc->sfp_probe = FALSE;
		if (ret == IXGBE_ERR_SFP_NOT_SUPPORTED) {
			if_printf(&sc->arpcom.ac_if,
			     "Unsupported SFP+ module detected!  "
			     "Reload driver with supported module.\n");
			return FALSE;
		}
		if_printf(&sc->arpcom.ac_if, "SFP+ module detected!\n");

		/* We now have supported optics */
		return TRUE;
	}
	return FALSE;
}

static void
ix_handle_link(struct ix_softc *sc)
{
	ixgbe_check_link(&sc->hw, &sc->link_speed, &sc->link_up, 0);
	ix_update_link_status(sc);
}

/*
 * Handling SFP module
 */
static void
ix_handle_mod(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t err;

	if (sc->hw.need_crosstalk_fix) {
		uint32_t cage_full = 0;

		switch (hw->mac.type) {
		case ixgbe_mac_82599EB:
			cage_full = IXGBE_READ_REG(hw, IXGBE_ESDP) &
			    IXGBE_ESDP_SDP2;
			break;

		case ixgbe_mac_X550EM_x:
		case ixgbe_mac_X550EM_a:
			cage_full = IXGBE_READ_REG(hw, IXGBE_ESDP) &
			    IXGBE_ESDP_SDP0;
			break;

		default:
			break;
		}

		if (!cage_full)
			return;
	}

	err = hw->phy.ops.identify_sfp(hw);
	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		if_printf(&sc->arpcom.ac_if,
		    "Unsupported SFP+ module type was detected.\n");
		return;
	}

	if (hw->mac.type == ixgbe_mac_82598EB)
		err = hw->phy.ops.reset(hw);
	else
		err = hw->mac.ops.setup_sfp(hw);
	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		if_printf(&sc->arpcom.ac_if,
		    "Setup failure - unsupported SFP+ module type.\n");
		return;
	}
	ix_handle_msf(sc);
}

/*
 * Handling MSF (multispeed fiber)
 */
static void
ix_handle_msf(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t autoneg;

	sc->phy_layer = ixgbe_get_supported_physical_layer(hw);
	ix_init_media(sc);

	if (sc->advspeed != IXGBE_LINK_SPEED_UNKNOWN)
		autoneg = sc->advspeed;
	else
		autoneg = hw->phy.autoneg_advertised;
	if (!autoneg && hw->mac.ops.get_link_capabilities != NULL) {
		bool negotiate;

		hw->mac.ops.get_link_capabilities(hw, &autoneg, &negotiate);
	}
	if (hw->mac.ops.setup_link != NULL)
		hw->mac.ops.setup_link(hw, autoneg, TRUE);
}

static void
ix_handle_phy(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int error;

	error = hw->phy.ops.handle_lasi(hw);
	if (error == IXGBE_ERR_OVERTEMP) {
		if_printf(&sc->arpcom.ac_if,
		    "CRITICAL: EXTERNAL PHY OVER TEMP!!  "
		    "PHY will downshift to lower power state!\n");
	} else if (error) {
		if_printf(&sc->arpcom.ac_if,
		    "Error handling LASI interrupt: %d\n", error);
	}
}

static void
ix_update_stats(struct ix_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw *hw = &sc->hw;
	struct ixgbe_hw_stats *stats = &sc->stats;
	uint32_t missed_rx = 0, bprc, lxon, lxoff, total;
	uint64_t total_missed_rx = 0;
	int i;

	stats->crcerrs += IXGBE_READ_REG(hw, IXGBE_CRCERRS);
	stats->illerrc += IXGBE_READ_REG(hw, IXGBE_ILLERRC);
	stats->errbc += IXGBE_READ_REG(hw, IXGBE_ERRBC);
	stats->mspdc += IXGBE_READ_REG(hw, IXGBE_MSPDC);
	stats->mpc[0] += IXGBE_READ_REG(hw, IXGBE_MPC(0));

	for (i = 0; i < 16; i++) {
		stats->qprc[i] += IXGBE_READ_REG(hw, IXGBE_QPRC(i));
		stats->qptc[i] += IXGBE_READ_REG(hw, IXGBE_QPTC(i));
		stats->qprdc[i] += IXGBE_READ_REG(hw, IXGBE_QPRDC(i));
	}
	stats->mlfc += IXGBE_READ_REG(hw, IXGBE_MLFC);
	stats->mrfc += IXGBE_READ_REG(hw, IXGBE_MRFC);
	stats->rlec += IXGBE_READ_REG(hw, IXGBE_RLEC);

	/* Hardware workaround, gprc counts missed packets */
	stats->gprc += IXGBE_READ_REG(hw, IXGBE_GPRC);
	stats->gprc -= missed_rx;

	if (hw->mac.type != ixgbe_mac_82598EB) {
		stats->gorc += IXGBE_READ_REG(hw, IXGBE_GORCL) +
		    ((uint64_t)IXGBE_READ_REG(hw, IXGBE_GORCH) << 32);
		stats->gotc += IXGBE_READ_REG(hw, IXGBE_GOTCL) +
		    ((uint64_t)IXGBE_READ_REG(hw, IXGBE_GOTCH) << 32);
		stats->tor += IXGBE_READ_REG(hw, IXGBE_TORL) +
		    ((uint64_t)IXGBE_READ_REG(hw, IXGBE_TORH) << 32);
		stats->lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXCNT);
		stats->lxoffrxc += IXGBE_READ_REG(hw, IXGBE_LXOFFRXCNT);
	} else {
		stats->lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXC);
		stats->lxoffrxc += IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
		/* 82598 only has a counter in the high register */
		stats->gorc += IXGBE_READ_REG(hw, IXGBE_GORCH);
		stats->gotc += IXGBE_READ_REG(hw, IXGBE_GOTCH);
		stats->tor += IXGBE_READ_REG(hw, IXGBE_TORH);
	}

	/*
	 * Workaround: mprc hardware is incorrectly counting
	 * broadcasts, so for now we subtract those.
	 */
	bprc = IXGBE_READ_REG(hw, IXGBE_BPRC);
	stats->bprc += bprc;
	stats->mprc += IXGBE_READ_REG(hw, IXGBE_MPRC);
	if (hw->mac.type == ixgbe_mac_82598EB)
		stats->mprc -= bprc;

	stats->prc64 += IXGBE_READ_REG(hw, IXGBE_PRC64);
	stats->prc127 += IXGBE_READ_REG(hw, IXGBE_PRC127);
	stats->prc255 += IXGBE_READ_REG(hw, IXGBE_PRC255);
	stats->prc511 += IXGBE_READ_REG(hw, IXGBE_PRC511);
	stats->prc1023 += IXGBE_READ_REG(hw, IXGBE_PRC1023);
	stats->prc1522 += IXGBE_READ_REG(hw, IXGBE_PRC1522);

	lxon = IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	stats->lxontxc += lxon;
	lxoff = IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	stats->lxofftxc += lxoff;
	total = lxon + lxoff;

	stats->gptc += IXGBE_READ_REG(hw, IXGBE_GPTC);
	stats->mptc += IXGBE_READ_REG(hw, IXGBE_MPTC);
	stats->ptc64 += IXGBE_READ_REG(hw, IXGBE_PTC64);
	stats->gptc -= total;
	stats->mptc -= total;
	stats->ptc64 -= total;
	stats->gotc -= total * ETHER_MIN_LEN;

	stats->ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	stats->rfc += IXGBE_READ_REG(hw, IXGBE_RFC);
	stats->roc += IXGBE_READ_REG(hw, IXGBE_ROC);
	stats->rjc += IXGBE_READ_REG(hw, IXGBE_RJC);
	stats->mngprc += IXGBE_READ_REG(hw, IXGBE_MNGPRC);
	stats->mngpdc += IXGBE_READ_REG(hw, IXGBE_MNGPDC);
	stats->mngptc += IXGBE_READ_REG(hw, IXGBE_MNGPTC);
	stats->tpr += IXGBE_READ_REG(hw, IXGBE_TPR);
	stats->tpt += IXGBE_READ_REG(hw, IXGBE_TPT);
	stats->ptc127 += IXGBE_READ_REG(hw, IXGBE_PTC127);
	stats->ptc255 += IXGBE_READ_REG(hw, IXGBE_PTC255);
	stats->ptc511 += IXGBE_READ_REG(hw, IXGBE_PTC511);
	stats->ptc1023 += IXGBE_READ_REG(hw, IXGBE_PTC1023);
	stats->ptc1522 += IXGBE_READ_REG(hw, IXGBE_PTC1522);
	stats->bptc += IXGBE_READ_REG(hw, IXGBE_BPTC);
	stats->xec += IXGBE_READ_REG(hw, IXGBE_XEC);
	stats->fccrc += IXGBE_READ_REG(hw, IXGBE_FCCRC);
	stats->fclast += IXGBE_READ_REG(hw, IXGBE_FCLAST);
	/* Only read FCOE on 82599 */
	if (hw->mac.type != ixgbe_mac_82598EB) {
		stats->fcoerpdc += IXGBE_READ_REG(hw, IXGBE_FCOERPDC);
		stats->fcoeprc += IXGBE_READ_REG(hw, IXGBE_FCOEPRC);
		stats->fcoeptc += IXGBE_READ_REG(hw, IXGBE_FCOEPTC);
		stats->fcoedwrc += IXGBE_READ_REG(hw, IXGBE_FCOEDWRC);
		stats->fcoedwtc += IXGBE_READ_REG(hw, IXGBE_FCOEDWTC);
	}

	/* Rx Errors */
	IFNET_STAT_SET(ifp, iqdrops, total_missed_rx);
	IFNET_STAT_SET(ifp, ierrors, sc->stats.crcerrs + sc->stats.rlec);
}

#if 0
/*
 * Add sysctl variables, one per statistic, to the system.
 */
static void
ix_add_hw_stats(struct ix_softc *sc)
{

	device_t dev = sc->dev;

	struct ix_tx_ring *txr = sc->tx_rings;
	struct ix_rx_ring *rxr = sc->rx_rings;

	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	struct ixgbe_hw_stats *stats = &sc->stats;

	struct sysctl_oid *stat_node, *queue_node;
	struct sysctl_oid_list *stat_list, *queue_list;

#define QUEUE_NAME_LEN 32
	char namebuf[QUEUE_NAME_LEN];

	/* MAC stats get the own sub node */

	stat_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "mac_stats", 
				    CTLFLAG_RD, NULL, "MAC Statistics");
	stat_list = SYSCTL_CHILDREN(stat_node);

	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "crc_errs",
			CTLFLAG_RD, &stats->crcerrs,
			"CRC Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "ill_errs",
			CTLFLAG_RD, &stats->illerrc,
			"Illegal Byte Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "byte_errs",
			CTLFLAG_RD, &stats->errbc,
			"Byte Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "short_discards",
			CTLFLAG_RD, &stats->mspdc,
			"MAC Short Packets Discarded");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "local_faults",
			CTLFLAG_RD, &stats->mlfc,
			"MAC Local Faults");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "remote_faults",
			CTLFLAG_RD, &stats->mrfc,
			"MAC Remote Faults");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rec_len_errs",
			CTLFLAG_RD, &stats->rlec,
			"Receive Length Errors");

	/* Flow Control stats */
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "xon_txd",
			CTLFLAG_RD, &stats->lxontxc,
			"Link XON Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "xon_recvd",
			CTLFLAG_RD, &stats->lxonrxc,
			"Link XON Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "xoff_txd",
			CTLFLAG_RD, &stats->lxofftxc,
			"Link XOFF Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "xoff_recvd",
			CTLFLAG_RD, &stats->lxoffrxc,
			"Link XOFF Received");

	/* Packet Reception Stats */
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "total_octets_rcvd",
			CTLFLAG_RD, &stats->tor, 
			"Total Octets Received"); 
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "good_octets_rcvd",
			CTLFLAG_RD, &stats->gorc, 
			"Good Octets Received"); 
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "total_pkts_rcvd",
			CTLFLAG_RD, &stats->tpr,
			"Total Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "good_pkts_rcvd",
			CTLFLAG_RD, &stats->gprc,
			"Good Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mcast_pkts_rcvd",
			CTLFLAG_RD, &stats->mprc,
			"Multicast Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "bcast_pkts_rcvd",
			CTLFLAG_RD, &stats->bprc,
			"Broadcast Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_64",
			CTLFLAG_RD, &stats->prc64,
			"64 byte frames received ");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_65_127",
			CTLFLAG_RD, &stats->prc127,
			"65-127 byte frames received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_128_255",
			CTLFLAG_RD, &stats->prc255,
			"128-255 byte frames received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_256_511",
			CTLFLAG_RD, &stats->prc511,
			"256-511 byte frames received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_512_1023",
			CTLFLAG_RD, &stats->prc1023,
			"512-1023 byte frames received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frames_1024_1522",
			CTLFLAG_RD, &stats->prc1522,
			"1023-1522 byte frames received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "recv_undersized",
			CTLFLAG_RD, &stats->ruc,
			"Receive Undersized");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "recv_fragmented",
			CTLFLAG_RD, &stats->rfc,
			"Fragmented Packets Received ");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "recv_oversized",
			CTLFLAG_RD, &stats->roc,
			"Oversized Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "recv_jabberd",
			CTLFLAG_RD, &stats->rjc,
			"Received Jabber");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "management_pkts_rcvd",
			CTLFLAG_RD, &stats->mngprc,
			"Management Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "management_pkts_drpd",
			CTLFLAG_RD, &stats->mngptc,
			"Management Packets Dropped");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "checksum_errs",
			CTLFLAG_RD, &stats->xec,
			"Checksum Errors");

	/* Packet Transmission Stats */
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "good_octets_txd",
			CTLFLAG_RD, &stats->gotc, 
			"Good Octets Transmitted"); 
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "total_pkts_txd",
			CTLFLAG_RD, &stats->tpt,
			"Total Packets Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "good_pkts_txd",
			CTLFLAG_RD, &stats->gptc,
			"Good Packets Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "bcast_pkts_txd",
			CTLFLAG_RD, &stats->bptc,
			"Broadcast Packets Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mcast_pkts_txd",
			CTLFLAG_RD, &stats->mptc,
			"Multicast Packets Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "management_pkts_txd",
			CTLFLAG_RD, &stats->mngptc,
			"Management Packets Transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_64",
			CTLFLAG_RD, &stats->ptc64,
			"64 byte frames transmitted ");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_65_127",
			CTLFLAG_RD, &stats->ptc127,
			"65-127 byte frames transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_128_255",
			CTLFLAG_RD, &stats->ptc255,
			"128-255 byte frames transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_256_511",
			CTLFLAG_RD, &stats->ptc511,
			"256-511 byte frames transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_512_1023",
			CTLFLAG_RD, &stats->ptc1023,
			"512-1023 byte frames transmitted");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_frames_1024_1522",
			CTLFLAG_RD, &stats->ptc1522,
			"1024-1522 byte frames transmitted");
}
#endif

/*
 * Enable the hardware to drop packets when the buffer is full.
 * This is useful when multiple RX rings are used, so that no
 * single RX ring being full stalls the entire RX engine.  We
 * only enable this when multiple RX rings are used and when
 * flow control is disabled.
 */
static void
ix_enable_rx_drop(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int i;

	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if,
		    "flow control %s, enable RX drop\n",
		    ix_fc2str(sc->hw.fc.current_mode));
	}

	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		uint32_t srrctl = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));

		srrctl |= IXGBE_SRRCTL_DROP_EN;
		IXGBE_WRITE_REG(hw, IXGBE_SRRCTL(i), srrctl);
	}
}

static void
ix_disable_rx_drop(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int i;

	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if,
		    "flow control %s, disable RX drop\n",
		    ix_fc2str(sc->hw.fc.current_mode));
	}

	for (i = 0; i < sc->rx_ring_inuse; ++i) {
		uint32_t srrctl = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));

		srrctl &= ~IXGBE_SRRCTL_DROP_EN;
		IXGBE_WRITE_REG(hw, IXGBE_SRRCTL(i), srrctl);
	}
}

static void
ix_setup_serialize(struct ix_softc *sc)
{
	int i = 0, j;

	/* Main + RX + TX */
	sc->nserialize = 1 + sc->rx_ring_cnt + sc->tx_ring_cnt;
	sc->serializes =
	    kmalloc(sc->nserialize * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializes
	 *
	 * NOTE: Order is critical
	 */

	KKASSERT(i < sc->nserialize);
	sc->serializes[i++] = &sc->main_serialize;

	for (j = 0; j < sc->rx_ring_cnt; ++j) {
		KKASSERT(i < sc->nserialize);
		sc->serializes[i++] = &sc->rx_rings[j].rx_serialize;
	}

	for (j = 0; j < sc->tx_ring_cnt; ++j) {
		KKASSERT(i < sc->nserialize);
		sc->serializes[i++] = &sc->tx_rings[j].tx_serialize;
	}

	KKASSERT(i == sc->nserialize);
}

static int
ix_alloc_intr(struct ix_softc *sc)
{
	struct ix_intr_data *intr;
	struct ix_tx_ring *txr;
	u_int intr_flags;
	int i;

	ix_alloc_msix(sc);
	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		ix_set_ring_inuse(sc, FALSE);
		goto done;
	}

	/*
	 * Reset some settings changed by ix_alloc_msix().
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
		txr->tx_intr_vec = -1;
		txr->tx_intr_cpuid = -1;
	}
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct ix_rx_ring *rxr = &sc->rx_rings[i];

		rxr->rx_intr_vec = -1;
		rxr->rx_txr = NULL;
	}

	sc->intr_cnt = 1;
	sc->intr_data = kmalloc(sizeof(struct ix_intr_data), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	intr = &sc->intr_data[0];

	/*
	 * Allocate MSI/legacy interrupt resource
	 */
	if (sc->caps & IX_CAP_LEGACY_INTR) {
		sc->intr_type = pci_alloc_1intr(sc->dev, ix_msi_enable,
		    &intr->intr_rid, &intr_flags);
	} else {
		int cpu;

		/*
		 * Only MSI is supported.
		 */
		cpu = device_getenv_int(sc->dev, "msi.cpu", -1);
		if (cpu >= ncpus)
			cpu = ncpus - 1;

		if (pci_alloc_msi(sc->dev, &intr->intr_rid, 1, cpu) == 0) {
			sc->intr_type = PCI_INTR_TYPE_MSI;
			intr_flags = RF_ACTIVE;
		} else {
			sc->intr_type = PCI_INTR_TYPE_LEGACY;
			device_printf(sc->dev, "Unable to allocate MSI\n");
			return ENXIO;
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
	if (sc->hw.mac.type == ixgbe_mac_82598EB)
		intr->intr_func = ix_intr_82598;
	else
		intr->intr_func = ix_intr;
	intr->intr_funcarg = sc;
	intr->intr_rate = IX_INTR_RATE;
	intr->intr_use = IX_INTR_USE_RXTX;

	sc->tx_rings[0].tx_intr_vec = IX_TX_INTR_VEC;
	sc->tx_rings[0].tx_intr_cpuid = intr->intr_cpuid;

	sc->rx_rings[0].rx_intr_vec = IX_RX0_INTR_VEC;

	ix_set_ring_inuse(sc, FALSE);

	KKASSERT(sc->rx_ring_inuse <= IX_MIN_RXRING_RSS);
	if (sc->rx_ring_inuse == IX_MIN_RXRING_RSS) {
		sc->rx_rings[1].rx_intr_vec = IX_RX1_INTR_VEC;

		/*
		 * Allocate RX ring map for RSS setup.
		 */
		sc->rx_rmap_intr = if_ringmap_alloc(sc->dev,
		    IX_MIN_RXRING_RSS, IX_MIN_RXRING_RSS);
		KASSERT(if_ringmap_count(sc->rx_rmap_intr) ==
		    sc->rx_ring_inuse, ("RX ring inuse mismatch"));
	}
done:
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		txr = &sc->tx_rings[i];
		if (txr->tx_intr_cpuid < 0)
			txr->tx_intr_cpuid = 0;
	}
	return 0;
}

static void
ix_free_intr(struct ix_softc *sc)
{
	if (sc->intr_data == NULL)
		return;

	if (sc->intr_type != PCI_INTR_TYPE_MSIX) {
		struct ix_intr_data *intr = &sc->intr_data[0];

		KKASSERT(sc->intr_cnt == 1);
		if (intr->intr_res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    intr->intr_rid, intr->intr_res);
		}
		if (sc->intr_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->dev);

		kfree(sc->intr_data, M_DEVBUF);
	} else {
		ix_free_msix(sc, TRUE);
	}
}

static void
ix_set_ring_inuse(struct ix_softc *sc, boolean_t polling)
{
	sc->rx_ring_inuse = ix_get_rxring_inuse(sc, polling);
	sc->tx_ring_inuse = ix_get_txring_inuse(sc, polling);
	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if,
		    "RX rings %d/%d, TX rings %d/%d\n",
		    sc->rx_ring_inuse, sc->rx_ring_cnt,
		    sc->tx_ring_inuse, sc->tx_ring_cnt);
	}
}

static int
ix_get_rxring_inuse(const struct ix_softc *sc, boolean_t polling)
{
	if (!IX_ENABLE_HWRSS(sc))
		return 1;

	if (polling)
		return sc->rx_ring_cnt;
	else if (sc->intr_type != PCI_INTR_TYPE_MSIX)
		return IX_MIN_RXRING_RSS;
	else
		return sc->rx_ring_msix;
}

static int
ix_get_txring_inuse(const struct ix_softc *sc, boolean_t polling)
{
	if (!IX_ENABLE_HWTSS(sc))
		return 1;

	if (polling)
		return sc->tx_ring_cnt;
	else if (sc->intr_type != PCI_INTR_TYPE_MSIX)
		return 1;
	else
		return sc->tx_ring_msix;
}

static int
ix_setup_intr(struct ix_softc *sc)
{
	int i;

	for (i = 0; i < sc->intr_cnt; ++i) {
		struct ix_intr_data *intr = &sc->intr_data[i];
		int error;

		error = bus_setup_intr_descr(sc->dev, intr->intr_res,
		    INTR_MPSAFE, intr->intr_func, intr->intr_funcarg,
		    &intr->intr_hand, intr->intr_serialize, intr->intr_desc);
		if (error) {
			device_printf(sc->dev, "can't setup %dth intr\n", i);
			ix_teardown_intr(sc, i);
			return error;
		}
	}
	return 0;
}

static void
ix_teardown_intr(struct ix_softc *sc, int intr_cnt)
{
	int i;

	if (sc->intr_data == NULL)
		return;

	for (i = 0; i < intr_cnt; ++i) {
		struct ix_intr_data *intr = &sc->intr_data[i];

		bus_teardown_intr(sc->dev, intr->intr_res, intr->intr_hand);
	}
}

static void
ix_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct ix_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->serializes, sc->nserialize, slz);
}

static void
ix_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct ix_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->serializes, sc->nserialize, slz);
}

static int
ix_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct ix_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->serializes, sc->nserialize, slz);
}

static void
ix_serialize_skipmain(struct ix_softc *sc)
{
	lwkt_serialize_array_enter(sc->serializes, sc->nserialize, 1);
}

static void
ix_deserialize_skipmain(struct ix_softc *sc)
{
	lwkt_serialize_array_exit(sc->serializes, sc->nserialize, 1);
}

#ifdef INVARIANTS

static void
ix_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct ix_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->serializes, sc->nserialize, slz,
	    serialized);
}

#endif	/* INVARIANTS */

static void
ix_free_rings(struct ix_softc *sc)
{
	int i;

	if (sc->tx_rings != NULL) {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct ix_tx_ring *txr = &sc->tx_rings[i];

			ix_destroy_tx_ring(txr, txr->tx_ndesc);
		}
		kfree(sc->tx_rings, M_DEVBUF);
	}

	if (sc->rx_rings != NULL) {
		for (i =0; i < sc->rx_ring_cnt; ++i) {
			struct ix_rx_ring *rxr = &sc->rx_rings[i];

			ix_destroy_rx_ring(rxr, rxr->rx_ndesc);
		}
		kfree(sc->rx_rings, M_DEVBUF);
	}

	if (sc->parent_tag != NULL)
		bus_dma_tag_destroy(sc->parent_tag);
}

static void
ix_watchdog_reset(struct ix_softc *sc)
{
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(&sc->arpcom.ac_if);
	ix_init(sc);
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ifsq_devstart_sched(sc->tx_rings[i].tx_ifsq);
}

static void
ix_sync_netisr(struct ix_softc *sc, int flags)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);
	if (ifp->if_flags & IFF_RUNNING) {
		ifp->if_flags &= ~(IFF_RUNNING | flags);
	} else {
		ifnet_deserialize_all(ifp);
		return;
	}
	ifnet_deserialize_all(ifp);

	/* Make sure that polling stopped. */
	netmsg_service_sync();
}

static void
ix_watchdog_task(void *xsc, int pending __unused)
{
	struct ix_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ix_sync_netisr(sc, 0);

	ifnet_serialize_all(ifp);
	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		ix_watchdog_reset(sc);
	ifnet_deserialize_all(ifp);
}

static void
ix_watchdog(struct ifaltq_subque *ifsq)
{
	struct ix_tx_ring *txr = ifsq_get_priv(ifsq);
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct ix_softc *sc = ifp->if_softc;

	KKASSERT(txr->tx_ifsq == ifsq);
	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/*
	 * If the interface has been paused then don't do the watchdog check
	 */
	if (IXGBE_READ_REG(&sc->hw, IXGBE_TFCS) & IXGBE_TFCS_TXOFF) {
		ifsq_watchdog_set_count(&txr->tx_watchdog, 5);
		return;
	}

	if_printf(ifp, "Watchdog timeout -- resetting\n");
	if_printf(ifp, "Queue(%d) tdh = %d, hw tdt = %d\n", txr->tx_idx,
	    IXGBE_READ_REG(&sc->hw, IXGBE_TDH(txr->tx_idx)),
	    IXGBE_READ_REG(&sc->hw, IXGBE_TDT(txr->tx_idx)));
	if_printf(ifp, "TX(%d) desc avail = %d, next TX to Clean = %d\n",
	    txr->tx_idx, txr->tx_avail, txr->tx_next_clean);

	if ((ifp->if_flags & (IFF_IDIRECT | IFF_NPOLLING | IFF_RUNNING)) ==
	    (IFF_IDIRECT | IFF_NPOLLING | IFF_RUNNING))
		taskqueue_enqueue(taskqueue_thread[0], &sc->wdog_task);
	else
		ix_watchdog_reset(sc);
}

static void
ix_free_tx_ring(struct ix_tx_ring *txr)
{
	int i;

	for (i = 0; i < txr->tx_ndesc; ++i) {
		struct ix_tx_buf *txbuf = &txr->tx_buf[i];

		if (txbuf->m_head != NULL)
			ix_free_txbuf(txr, txbuf);
	}
}

static void
ix_free_rx_ring(struct ix_rx_ring *rxr)
{
	int i;

	for (i = 0; i < rxr->rx_ndesc; ++i) {
		struct ix_rx_buf *rxbuf = &rxr->rx_buf[i];

		if (rxbuf->fmp != NULL) {
			m_freem(rxbuf->fmp);
			rxbuf->fmp = NULL;
			rxbuf->lmp = NULL;
		} else {
			KKASSERT(rxbuf->lmp == NULL);
		}
		if (rxbuf->m_head != NULL) {
			bus_dmamap_unload(rxr->rx_tag, rxbuf->map);
			m_freem(rxbuf->m_head);
			rxbuf->m_head = NULL;
		}
	}
}

static int
ix_newbuf(struct ix_rx_ring *rxr, int i, boolean_t wait)
{
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct ix_rx_buf *rxbuf;
	int flags, error, nseg;

	flags = M_NOWAIT;
	if (__predict_false(wait))
		flags = M_WAITOK;

	m = m_getjcl(flags, MT_DATA, M_PKTHDR, rxr->rx_mbuf_sz);
	if (m == NULL) {
		if (wait) {
			if_printf(&rxr->rx_sc->arpcom.ac_if,
			    "Unable to allocate RX mbuf\n");
		}
		return ENOBUFS;
	}
	m->m_len = m->m_pkthdr.len = rxr->rx_mbuf_sz;

	error = bus_dmamap_load_mbuf_segment(rxr->rx_tag,
	    rxr->rx_sparemap, m, &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (wait) {
			if_printf(&rxr->rx_sc->arpcom.ac_if,
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

	ix_setup_rxdesc(&rxr->rx_base[i], rxbuf);
	return 0;
}

static void
ix_add_sysctl(struct ix_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->dev);
	char node[32];
	int i;

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
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxd", CTLTYPE_INT | CTLFLAG_RD,
	    sc, 0, ix_sysctl_rxd, "I",
	    "# of RX descs");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txd", CTLTYPE_INT | CTLFLAG_RD,
	    sc, 0, ix_sysctl_txd, "I",
	    "# of TX descs");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, ix_sysctl_tx_wreg_nsegs, "I",
	    "# of segments sent before write to hardware register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rx_wreg_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, ix_sysctl_rx_wreg_nsegs, "I",
	    "# of received segments sent before write to hardware register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_intr_nsegs", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, ix_sysctl_tx_intr_nsegs, "I",
	    "# of segments per TX interrupt");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "direct_input", CTLFLAG_RW, &sc->direct_input, 0,
	    "Enable direct input");
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

#define IX_ADD_INTR_RATE_SYSCTL(sc, use, name) \
do { \
	ix_add_intr_rate_sysctl(sc, IX_INTR_USE_##use, #name, \
	    ix_sysctl_##name, #use " interrupt rate"); \
} while (0)

	IX_ADD_INTR_RATE_SYSCTL(sc, RXTX, rxtx_intr_rate);
	IX_ADD_INTR_RATE_SYSCTL(sc, RX, rx_intr_rate);
	IX_ADD_INTR_RATE_SYSCTL(sc, TX, tx_intr_rate);
	IX_ADD_INTR_RATE_SYSCTL(sc, STATUS, sts_intr_rate);

#undef IX_ADD_INTR_RATE_SYSCTL

#ifdef IX_RSS_DEBUG
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rss_debug", CTLFLAG_RW, &sc->rss_debug, 0,
	    "RSS debug level");
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "rx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx,
		    SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RW, &sc->rx_rings[i].rx_pkts, "RXed packets");
	}
#endif
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		ksnprintf(node, sizeof(node), "tx%d_nmbuf", i);
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLTYPE_INT | CTLFLAG_RD, txr, 0, ix_sysctl_tx_nmbuf, "I",
		    "# of pending TX mbufs");

		ksnprintf(node, sizeof(node), "tx%d_gc", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, node,
		    CTLFLAG_RW, &txr->tx_gc, "# of TX desc GC");
	}

#if 0
	ix_add_hw_stats(sc);
#endif

}

static int
ix_sysctl_tx_nmbuf(SYSCTL_HANDLER_ARGS)
{
	struct ix_tx_ring *txr = (void *)arg1;
	int nmbuf;

	nmbuf = txr->tx_nmbuf;
	return (sysctl_handle_int(oidp, &nmbuf, 0, req));
}

static int
ix_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct ix_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, nsegs, i;

	nsegs = sc->tx_rings[0].tx_wreg_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (nsegs < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		sc->tx_rings[i].tx_wreg_nsegs = nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
ix_sysctl_rx_wreg_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct ix_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, nsegs, i;

	nsegs = sc->rx_rings[0].rx_wreg_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (nsegs < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		sc->rx_rings[i].rx_wreg_nsegs =nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
ix_sysctl_txd(SYSCTL_HANDLER_ARGS)
{
	struct ix_softc *sc = (void *)arg1;
	int txd;

	txd = sc->tx_rings[0].tx_ndesc;
	return sysctl_handle_int(oidp, &txd, 0, req);
}

static int
ix_sysctl_rxd(SYSCTL_HANDLER_ARGS)
{
	struct ix_softc *sc = (void *)arg1;
	int rxd;

	rxd = sc->rx_rings[0].rx_ndesc;
	return sysctl_handle_int(oidp, &rxd, 0, req);
}

static int
ix_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct ix_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ix_tx_ring *txr = &sc->tx_rings[0];
	int error, nsegs;

	nsegs = txr->tx_intr_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (nsegs < 0)
		return EINVAL;

	ifnet_serialize_all(ifp);

	if (nsegs >= txr->tx_ndesc - IX_MAX_SCATTER - IX_TX_RESERVED) {
		error = EINVAL;
	} else {
		int i;

		error = 0;
		for (i = 0; i < sc->tx_ring_cnt; ++i)
			sc->tx_rings[i].tx_intr_nsegs = nsegs;
	}

	ifnet_deserialize_all(ifp);

	return error;
}

static void
ix_set_eitr(struct ix_softc *sc, int idx, int rate)
{
	uint32_t eitr, eitr_intvl;

	eitr = IXGBE_READ_REG(&sc->hw, IXGBE_EITR(idx));
	eitr_intvl = 1000000000 / 256 / rate;

	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		eitr &= ~IX_EITR_INTVL_MASK_82598;
		if (eitr_intvl == 0)
			eitr_intvl = 1;
		else if (eitr_intvl > IX_EITR_INTVL_MASK_82598)
			eitr_intvl = IX_EITR_INTVL_MASK_82598;
	} else {
		eitr &= ~IX_EITR_INTVL_MASK;

		eitr_intvl &= ~IX_EITR_INTVL_RSVD_MASK;
		if (eitr_intvl == 0)
			eitr_intvl = IX_EITR_INTVL_MIN;
		else if (eitr_intvl > IX_EITR_INTVL_MAX)
			eitr_intvl = IX_EITR_INTVL_MAX;
	}
	eitr |= eitr_intvl;

	IXGBE_WRITE_REG(&sc->hw, IXGBE_EITR(idx), eitr);
}

static int
ix_sysctl_rxtx_intr_rate(SYSCTL_HANDLER_ARGS)
{
	return ix_sysctl_intr_rate(oidp, arg1, arg2, req, IX_INTR_USE_RXTX);
}

static int
ix_sysctl_rx_intr_rate(SYSCTL_HANDLER_ARGS)
{
	return ix_sysctl_intr_rate(oidp, arg1, arg2, req, IX_INTR_USE_RX);
}

static int
ix_sysctl_tx_intr_rate(SYSCTL_HANDLER_ARGS)
{
	return ix_sysctl_intr_rate(oidp, arg1, arg2, req, IX_INTR_USE_TX);
}

static int
ix_sysctl_sts_intr_rate(SYSCTL_HANDLER_ARGS)
{
	return ix_sysctl_intr_rate(oidp, arg1, arg2, req, IX_INTR_USE_STATUS);
}

static int
ix_sysctl_intr_rate(SYSCTL_HANDLER_ARGS, int use)
{
	struct ix_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, rate, i;

	rate = 0;
	for (i = 0; i < sc->intr_cnt; ++i) {
		if (sc->intr_data[i].intr_use == use) {
			rate = sc->intr_data[i].intr_rate;
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
		if (sc->intr_data[i].intr_use == use) {
			sc->intr_data[i].intr_rate = rate;
			if (ifp->if_flags & IFF_RUNNING)
				ix_set_eitr(sc, i, rate);
		}
	}

	ifnet_deserialize_all(ifp);

	return error;
}

static void
ix_add_intr_rate_sysctl(struct ix_softc *sc, int use,
    const char *name, int (*handler)(SYSCTL_HANDLER_ARGS), const char *desc)
{
	int i;

	for (i = 0; i < sc->intr_cnt; ++i) {
		if (sc->intr_data[i].intr_use == use) {
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
			    OID_AUTO, name, CTLTYPE_INT | CTLFLAG_RW,
			    sc, 0, handler, "I", desc);
			break;
		}
	}
}

static int
ix_get_timer_cpuid(const struct ix_softc *sc, boolean_t polling)
{
	if (polling || sc->intr_type == PCI_INTR_TYPE_MSIX)
		return 0; /* XXX fixed */
	else
		return rman_get_cpuid(sc->intr_data[0].intr_res);
}

static void
ix_alloc_msix(struct ix_softc *sc)
{
	int msix_enable, msix_cnt, msix_ring, alloc_cnt;
	struct ix_intr_data *intr;
	int i, x, error;
	int ring_cnt, ring_cntmax;
	boolean_t setup = FALSE;

	msix_enable = ix_msix_enable;
	/*
	 * Don't enable MSI-X on 82598 by default, see:
	 * 82598 specification update errata #38
	 */
	if (sc->hw.mac.type == ixgbe_mac_82598EB)
		msix_enable = 0;
	msix_enable = device_getenv_int(sc->dev, "msix.enable", msix_enable);
	if (!msix_enable)
		return;

	msix_cnt = pci_msix_count(sc->dev);
#ifdef IX_MSIX_DEBUG
	msix_cnt = device_getenv_int(sc->dev, "msix.count", msix_cnt);
#endif
	if (msix_cnt <= 1) {
		/* One MSI-X model does not make sense. */
		return;
	}

	/*
	 * Make sure that we don't break interrupt related registers
	 * (EIMS, etc) limitation.
	 */
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		if (msix_cnt > IX_MAX_MSIX_82598)
			msix_cnt = IX_MAX_MSIX_82598;
	} else {
		if (msix_cnt > IX_MAX_MSIX)
			msix_cnt = IX_MAX_MSIX;
	}
	if (bootverbose)
		device_printf(sc->dev, "MSI-X count %d\n", msix_cnt);
	msix_ring = msix_cnt - 1; /* -1 for status */

	/*
	 * Configure # of RX/TX rings usable by MSI-X.
	 */
	ix_get_rxring_cnt(sc, &ring_cnt, &ring_cntmax);
	if (ring_cntmax > msix_ring)
		ring_cntmax = msix_ring;
	sc->rx_rmap_intr = if_ringmap_alloc(sc->dev, ring_cnt, ring_cntmax);

	ix_get_txring_cnt(sc, &ring_cnt, &ring_cntmax);
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

	sc->msix_mem_rid = PCIR_BAR(IX_MSIX_BAR_82598);
	sc->msix_mem_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->msix_mem_rid, RF_ACTIVE);
	if (sc->msix_mem_res == NULL) {
		sc->msix_mem_rid = PCIR_BAR(IX_MSIX_BAR_82599);
		sc->msix_mem_res = bus_alloc_resource_any(sc->dev,
		    SYS_RES_MEMORY, &sc->msix_mem_rid, RF_ACTIVE);
		if (sc->msix_mem_res == NULL) {
			device_printf(sc->dev, "Unable to map MSI-X table\n");
			return;
		}
	}

	sc->intr_cnt = alloc_cnt;
	sc->intr_data = kmalloc(sizeof(struct ix_intr_data) * sc->intr_cnt,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (x = 0; x < sc->intr_cnt; ++x) {
		intr = &sc->intr_data[x];
		intr->intr_rid = -1;
		intr->intr_rate = IX_INTR_RATE;
	}

	x = 0;
	for (i = 0; i < sc->rx_ring_msix; ++i) {
		struct ix_rx_ring *rxr = &sc->rx_rings[i];
		struct ix_tx_ring *txr = NULL;
		int cpuid, j;

		KKASSERT(x < sc->intr_cnt);
		rxr->rx_intr_vec = x;
		ix_setup_msix_eims(sc, x,
		    &rxr->rx_eims, &rxr->rx_eims_val);

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
		if (txr != NULL) {
			ksnprintf(intr->intr_desc0,
			    sizeof(intr->intr_desc0), "%s rx%dtx%d",
			    device_get_nameunit(sc->dev), i, txr->tx_idx);
			intr->intr_use = IX_INTR_USE_RXTX;
			intr->intr_func = ix_msix_rxtx;
		} else {
			ksnprintf(intr->intr_desc0,
			    sizeof(intr->intr_desc0), "%s rx%d",
			    device_get_nameunit(sc->dev), i);
			intr->intr_rate = IX_MSIX_RX_RATE;
			intr->intr_use = IX_INTR_USE_RX;
			intr->intr_func = ix_msix_rx;
		}
		intr->intr_funcarg = rxr;
		intr->intr_cpuid = cpuid;
		KKASSERT(intr->intr_cpuid < netisr_ncpus);
		intr->intr_desc = intr->intr_desc0;

		if (txr != NULL) {
			txr->tx_intr_cpuid = intr->intr_cpuid;
			/* NOTE: Leave TX ring's intr_vec negative. */
		}
	}

	for (i = 0; i < sc->tx_ring_msix; ++i) {
		struct ix_tx_ring *txr = &sc->tx_rings[i];

		if (txr->tx_intr_cpuid >= 0) {
			/* Piggybacked by RX ring. */
			continue;
		}

		KKASSERT(x < sc->intr_cnt);
		txr->tx_intr_vec = x;
		ix_setup_msix_eims(sc, x, &txr->tx_eims, &txr->tx_eims_val);

		intr = &sc->intr_data[x++];
		intr->intr_serialize = &txr->tx_serialize;
		intr->intr_rate = IX_MSIX_TX_RATE;
		intr->intr_use = IX_INTR_USE_TX;
		intr->intr_func = ix_msix_tx;
		intr->intr_funcarg = txr;
		intr->intr_cpuid = if_ringmap_cpumap(sc->tx_rmap_intr, i);
		KKASSERT(intr->intr_cpuid < netisr_ncpus);
		ksnprintf(intr->intr_desc0, sizeof(intr->intr_desc0), "%s tx%d",
		    device_get_nameunit(sc->dev), i);
		intr->intr_desc = intr->intr_desc0;

		txr->tx_intr_cpuid = intr->intr_cpuid;
	}

	/*
	 * Status MSI-X
	 */
	KKASSERT(x < sc->intr_cnt);
	sc->sts_msix_vec = x;

	intr = &sc->intr_data[x++];

	intr->intr_serialize = &sc->main_serialize;
	intr->intr_func = ix_msix_status;
	intr->intr_funcarg = sc;
	intr->intr_cpuid = 0;
	intr->intr_use = IX_INTR_USE_STATUS;

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
		ix_free_msix(sc, setup);
}

static void
ix_free_msix(struct ix_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->intr_cnt > 1);

	for (i = 0; i < sc->intr_cnt; ++i) {
		struct ix_intr_data *intr = &sc->intr_data[i];

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
ix_msix_rx(void *xrxr)
{
	struct ix_rx_ring *rxr = xrxr;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	ix_rxeof(rxr, -1);
	IXGBE_WRITE_REG(&rxr->rx_sc->hw, rxr->rx_eims, rxr->rx_eims_val);
}

static void
ix_msix_tx(void *xtxr)
{
	struct ix_tx_ring *txr = xtxr;

	ASSERT_SERIALIZED(&txr->tx_serialize);

	ix_tx_intr(txr, *(txr->tx_hdr));
	IXGBE_WRITE_REG(&txr->tx_sc->hw, txr->tx_eims, txr->tx_eims_val);
}

static void
ix_msix_rxtx(void *xrxr)
{
	struct ix_rx_ring *rxr = xrxr;
	struct ix_tx_ring *txr;
	int hdr;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	ix_rxeof(rxr, -1);

	/*
	 * NOTE:
	 * Since tx_next_clean is only changed by ix_txeof(),
	 * which is called only in interrupt handler, the
	 * check w/o holding tx serializer is MPSAFE.
	 */
	txr = rxr->rx_txr;
	hdr = *(txr->tx_hdr);
	if (hdr != txr->tx_next_clean) {
		lwkt_serialize_enter(&txr->tx_serialize);
		ix_tx_intr(txr, hdr);
		lwkt_serialize_exit(&txr->tx_serialize);
	}

	IXGBE_WRITE_REG(&rxr->rx_sc->hw, rxr->rx_eims, rxr->rx_eims_val);
}

static void
ix_intr_status(struct ix_softc *sc, uint32_t eicr)
{
	struct ixgbe_hw *hw = &sc->hw;

	/* Link status change */
	if (eicr & IXGBE_EICR_LSC)
		ix_handle_link(sc);

	if (hw->mac.type != ixgbe_mac_82598EB) {
		if (eicr & IXGBE_EICR_ECC)
			if_printf(&sc->arpcom.ac_if, "ECC ERROR!!  REBOOT!!\n");

		/* Check for over temp condition */
		if (sc->caps & IX_CAP_TEMP_SENSOR) {
			int32_t retval;

			switch (sc->hw.mac.type) {
			case ixgbe_mac_X550EM_a:
				if ((eicr & IXGBE_EICR_GPI_SDP0_X550EM_a) == 0)
					break;
				retval = hw->phy.ops.check_overtemp(hw);
				if (retval != IXGBE_ERR_OVERTEMP)
					break;

				/* Disable more temp sensor interrupts. */
				IXGBE_WRITE_REG(hw, IXGBE_EIMC,
				    IXGBE_EICR_GPI_SDP0_X550EM_a);
				if_printf(&sc->arpcom.ac_if, "CRITICAL: "
				    "OVER TEMP!!  PHY IS SHUT DOWN!!  "
				    "SHUTDOWN!!\n");
				break;

			default:
				if ((eicr & IXGBE_EICR_TS) == 0)
					break;
				retval = hw->phy.ops.check_overtemp(hw);
				if (retval != IXGBE_ERR_OVERTEMP)
					break;

				/* Disable more temp sensor interrupts. */
				IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EICR_TS);
				if_printf(&sc->arpcom.ac_if, "CRITICAL: "
				    "OVER TEMP!!  PHY IS SHUT DOWN!!  "
				    "SHUTDOWN!!\n");
				break;
			}
		}
	}

	if (ix_is_sfp(hw)) {
		uint32_t eicr_mask;

		/* Pluggable optics-related interrupt */
		if (hw->mac.type >= ixgbe_mac_X540)
			eicr_mask = IXGBE_EICR_GPI_SDP0_X540;
		else
			eicr_mask = IXGBE_EICR_GPI_SDP2_BY_MAC(hw);

		if (eicr & eicr_mask)
			ix_handle_mod(sc);

		if (hw->mac.type == ixgbe_mac_82599EB &&
		    (eicr & IXGBE_EICR_GPI_SDP1_BY_MAC(hw)))
			ix_handle_msf(sc);
	}

	/* Check for fan failure */
	if (sc->caps & IX_CAP_DETECT_FANFAIL)
		ix_detect_fanfail(sc, eicr, TRUE);

	/* External PHY interrupt */
	if (hw->phy.type == ixgbe_phy_x550em_ext_t &&
	    (eicr & IXGBE_EICR_GPI_SDP0_X540))
		ix_handle_phy(sc);
}

static void
ix_msix_status(void *xsc)
{
	struct ix_softc *sc = xsc;
	uint32_t eicr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	eicr = IXGBE_READ_REG(&sc->hw, IXGBE_EICR);
	ix_intr_status(sc, eicr);

	IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMS, sc->intr_mask);
}

static void
ix_setup_msix_eims(const struct ix_softc *sc, int x,
    uint32_t *eims, uint32_t *eims_val)
{
	if (x < 32) {
		if (sc->hw.mac.type == ixgbe_mac_82598EB) {
			KASSERT(x < IX_MAX_MSIX_82598,
			    ("%s: invalid vector %d for 82598",
			     device_get_nameunit(sc->dev), x));
			*eims = IXGBE_EIMS;
		} else {
			*eims = IXGBE_EIMS_EX(0);
		}
		*eims_val = 1 << x;
	} else {
		KASSERT(x < IX_MAX_MSIX, ("%s: invalid vector %d",
		    device_get_nameunit(sc->dev), x));
		KASSERT(sc->hw.mac.type != ixgbe_mac_82598EB,
		    ("%s: invalid vector %d for 82598",
		     device_get_nameunit(sc->dev), x));
		*eims = IXGBE_EIMS_EX(1);
		*eims_val = 1 << (x - 32);
	}
}

#ifdef IFPOLL_ENABLE

static void
ix_npoll_status(struct ifnet *ifp)
{
	struct ix_softc *sc = ifp->if_softc;
	uint32_t eicr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	eicr = IXGBE_READ_REG(&sc->hw, IXGBE_EICR);
	ix_intr_status(sc, eicr);
}

static void
ix_npoll_tx(struct ifnet *ifp, void *arg, int cycle __unused)
{
	struct ix_tx_ring *txr = arg;

	ASSERT_SERIALIZED(&txr->tx_serialize);

	ix_tx_intr(txr, *(txr->tx_hdr));
	ix_try_txgc(txr, 1);
}

static void
ix_npoll_rx(struct ifnet *ifp __unused, void *arg, int cycle)
{
	struct ix_rx_ring *rxr = arg;

	ASSERT_SERIALIZED(&rxr->rx_serialize);
	ix_rxeof(rxr, cycle);
}

static void
ix_npoll_rx_direct(struct ifnet *ifp __unused, void *arg, int cycle)
{
	struct ix_rx_ring *rxr = arg;

	ASSERT_NOT_SERIALIZED(&rxr->rx_serialize);
	ix_rxeof(rxr, cycle);
}

static void
ix_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct ix_softc *sc = ifp->if_softc;
	int i, txr_cnt, rxr_cnt, idirect;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	idirect = sc->direct_input;
	cpu_ccfence();

	if (info) {
		int cpu;

		info->ifpi_status.status_func = ix_npoll_status;
		info->ifpi_status.serializer = &sc->main_serialize;

		txr_cnt = ix_get_txring_inuse(sc, TRUE);
		for (i = 0; i < txr_cnt; ++i) {
			struct ix_tx_ring *txr = &sc->tx_rings[i];

			cpu = if_ringmap_cpumap(sc->tx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_tx[cpu].poll_func = ix_npoll_tx;
			info->ifpi_tx[cpu].arg = txr;
			info->ifpi_tx[cpu].serializer = &txr->tx_serialize;
			ifsq_set_cpuid(txr->tx_ifsq, cpu);
		}

		rxr_cnt = ix_get_rxring_inuse(sc, TRUE);
		for (i = 0; i < rxr_cnt; ++i) {
			struct ix_rx_ring *rxr = &sc->rx_rings[i];

			cpu = if_ringmap_cpumap(sc->rx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_rx[cpu].arg = rxr;
			if (idirect) {
				info->ifpi_rx[cpu].poll_func =
				    ix_npoll_rx_direct;
				info->ifpi_rx[cpu].serializer = NULL;
			} else {
				info->ifpi_rx[cpu].poll_func = ix_npoll_rx;
				info->ifpi_rx[cpu].serializer =
				    &rxr->rx_serialize;
			}
		}
		if (idirect)
			ifp->if_flags |= IFF_IDIRECT;
	} else {
		ifp->if_flags &= ~IFF_IDIRECT;
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct ix_tx_ring *txr = &sc->tx_rings[i];

			ifsq_set_cpuid(txr->tx_ifsq, txr->tx_intr_cpuid);
		}
	}
	if (ifp->if_flags & IFF_RUNNING)
		ix_init(sc);
}

#endif /* IFPOLL_ENABLE */

static enum ixgbe_fc_mode
ix_ifmedia2fc(int ifm)
{
	int fc_opt = ifm & (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);

	switch (fc_opt) {
	case (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE):
		return ixgbe_fc_full;

	case IFM_ETH_RXPAUSE:
		return ixgbe_fc_rx_pause;

	case IFM_ETH_TXPAUSE:
		return ixgbe_fc_tx_pause;

	default:
		return ixgbe_fc_none;
	}
}

static const char *
ix_ifmedia2str(int ifm)
{
	int fc_opt = ifm & (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);

	switch (fc_opt) {
	case (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE):
		return IFM_ETH_FC_FULL;

	case IFM_ETH_RXPAUSE:
		return IFM_ETH_FC_RXPAUSE;

	case IFM_ETH_TXPAUSE:
		return IFM_ETH_FC_TXPAUSE;

	default:
		return IFM_ETH_FC_NONE;
	}
}

static const char *
ix_fc2str(enum ixgbe_fc_mode fc)
{
	switch (fc) {
	case ixgbe_fc_full:
		return IFM_ETH_FC_FULL;

	case ixgbe_fc_rx_pause:
		return IFM_ETH_FC_RXPAUSE;

	case ixgbe_fc_tx_pause:
		return IFM_ETH_FC_TXPAUSE;

	default:
		return IFM_ETH_FC_NONE;
	}
}

static int
ix_powerdown(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int error = 0;

	/* Limit power management flow to X550EM baseT */
	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_10G_T &&
	    hw->phy.ops.enter_lplu) {
		/* Turn off support for APM wakeup. (Using ACPI instead) */
		IXGBE_WRITE_REG(hw, IXGBE_GRC,
		    IXGBE_READ_REG(hw, IXGBE_GRC) & ~(uint32_t)2);

		/*
		 * Clear Wake Up Status register to prevent any previous wakeup
		 * events from waking us up immediately after we suspend.
		 */
		IXGBE_WRITE_REG(hw, IXGBE_WUS, 0xffffffff);

		/*
		 * Program the Wakeup Filter Control register with user filter
		 * settings
		 */
		IXGBE_WRITE_REG(hw, IXGBE_WUFC, sc->wufc);

		/* Enable wakeups and power management in Wakeup Control */
		IXGBE_WRITE_REG(hw, IXGBE_WUC,
		    IXGBE_WUC_WKEN | IXGBE_WUC_PME_EN);

		/* X550EM baseT adapters need a special LPLU flow */
		hw->phy.reset_disable = true;
		ix_stop(sc);
		error = hw->phy.ops.enter_lplu(hw);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
			    "Error entering LPLU: %d\n", error);
		}
		hw->phy.reset_disable = false;
	} else {
		/* Just stop for other adapters */
		ix_stop(sc);
	}
	return error;
}

static void
ix_config_flowctrl(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t rxpb, frame, size, tmp;

	frame = sc->max_frame_size;

	/* Calculate High Water */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_a:
	case ixgbe_mac_X550EM_x:
		tmp = IXGBE_DV_X540(frame, frame);
		break;
	default:
		tmp = IXGBE_DV(frame, frame);
		break;
	}
	size = IXGBE_BT2KB(tmp);
	rxpb = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) >> 10;
	hw->fc.high_water[0] = rxpb - size;

	/* Now calculate Low Water */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_a:
	case ixgbe_mac_X550EM_x:
		tmp = IXGBE_LOW_DV_X540(frame);
		break;
	default:
		tmp = IXGBE_LOW_DV(frame);
		break;
	}
	hw->fc.low_water[0] = IXGBE_BT2KB(tmp);

	hw->fc.requested_mode = ix_ifmedia2fc(sc->ifm_media);
	if (sc->ifm_media & IFM_ETH_FORCEPAUSE)
		hw->fc.disable_fc_autoneg = TRUE;
	else
		hw->fc.disable_fc_autoneg = FALSE;
	hw->fc.pause_time = IX_FC_PAUSE;
	hw->fc.send_xon = TRUE;
}

static void
ix_config_dmac(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	struct ixgbe_dmac_config *dcfg = &hw->mac.dmac_config;

	if (hw->mac.type < ixgbe_mac_X550 || !hw->mac.ops.dmac_config)
		return;

	if ((dcfg->watchdog_timer ^ sc->dmac) ||
	    (dcfg->link_speed ^ sc->link_speed)) {
		dcfg->watchdog_timer = sc->dmac;
		dcfg->fcoe_en = false;
		dcfg->link_speed = sc->link_speed;
		dcfg->num_tcs = 1;

		if (bootverbose) {
			if_printf(&sc->arpcom.ac_if, "dmac settings: "
			    "watchdog %d, link speed %d\n",
			    dcfg->watchdog_timer, dcfg->link_speed);
		}

		hw->mac.ops.dmac_config(hw);
	}
}

static void
ix_init_media(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t layer;

	ifmedia_removeall(&sc->media);

	layer = sc->phy_layer;

	/*
	 * Media types with matching DragonFlyBSD media defines
	 */
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_T) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_T | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_T) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_100BASE_TX) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_100_TX | IFM_FDX,
		    0, NULL);
		/* No half-duplex support */
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10BASE_T) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10_T | IFM_FDX,
		    0, NULL);
		/* No half-duplex support */
	}

	if ((layer & IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU) ||
	    (layer & IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA)) {
		ifmedia_add_nodup(&sc->media,
		    IFM_ETHER | IFM_10G_TWINAX | IFM_FDX, 0, NULL);
	}

	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LR) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_LR | IFM_FDX,
		    0, NULL);
		if (hw->phy.multispeed_fiber) {
			ifmedia_add_nodup(&sc->media,
			    IFM_ETHER | IFM_1000_LX | IFM_FDX, 0, NULL);
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LRM) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_LRM | IFM_FDX,
		    0, NULL);
		if (hw->phy.multispeed_fiber) {
			ifmedia_add_nodup(&sc->media,
			    IFM_ETHER | IFM_1000_LX | IFM_FDX, 0, NULL);
		}
	}

	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_SR) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_SR | IFM_FDX,
		    0, NULL);
		if (hw->phy.multispeed_fiber) {
			ifmedia_add_nodup(&sc->media,
			    IFM_ETHER | IFM_1000_SX | IFM_FDX, 0, NULL);
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_SX) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_1000_SX | IFM_FDX,
		    0, NULL);
	}

	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_CX4) {
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_CX4 | IFM_FDX,
		    0, NULL);
	}

	/*
	 * XXX Other (no matching DragonFlyBSD media type):
	 * To workaround this, we'll assign these completely
	 * inappropriate media types.
	 */
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KR) {
		if_printf(&sc->arpcom.ac_if, "Media supported: 10GbaseKR\n");
		if_printf(&sc->arpcom.ac_if, "10GbaseKR mapped to 10GbaseSR\n");
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_SR | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KX4) {
		if_printf(&sc->arpcom.ac_if, "Media supported: 10GbaseKX4\n");
		if_printf(&sc->arpcom.ac_if,
		    "10GbaseKX4 mapped to 10GbaseCX4\n");
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_10G_CX4 | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_KX) {
		if_printf(&sc->arpcom.ac_if, "Media supported: 1000baseKX\n");
		if_printf(&sc->arpcom.ac_if,
		    "1000baseKX mapped to 1000baseCX\n");
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_1000_CX | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_2500BASE_KX) {
		if_printf(&sc->arpcom.ac_if, "Media supported: 2500baseKX\n");
		if_printf(&sc->arpcom.ac_if,
		    "2500baseKX mapped to 2500baseSX\n");
		ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_2500_SX | IFM_FDX,
		    0, NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_BX) {
		if_printf(&sc->arpcom.ac_if,
		    "Media supported: 1000baseBX, ignored\n");
	}

	/* XXX we probably don't need this */
	if (hw->device_id == IXGBE_DEV_ID_82598AT) {
		ifmedia_add_nodup(&sc->media,
		    IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
	}

	ifmedia_add_nodup(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);

	if (ifmedia_tryset(&sc->media, sc->ifm_media)) {
		int flowctrl = (sc->ifm_media & IFM_ETH_FCMASK);

		sc->advspeed = IXGBE_LINK_SPEED_UNKNOWN;
		sc->ifm_media = IX_IFM_DEFAULT | flowctrl;
		ifmedia_set(&sc->media, sc->ifm_media);
	}
}

static void
ix_setup_caps(struct ix_softc *sc)
{

	sc->caps |= IX_CAP_LEGACY_INTR;

	switch (sc->hw.mac.type) {
	case ixgbe_mac_82598EB:
		if (sc->hw.device_id == IXGBE_DEV_ID_82598AT)
			sc->caps |= IX_CAP_DETECT_FANFAIL;
		break;

	case ixgbe_mac_X550:
		sc->caps |= IX_CAP_TEMP_SENSOR | IX_CAP_FW_RECOVERY;
		break;

	case ixgbe_mac_X550EM_x:
		if (sc->hw.device_id == IXGBE_DEV_ID_X550EM_X_KR)
			sc->caps |= IX_CAP_EEE;
		sc->caps |= IX_CAP_FW_RECOVERY;
		break;

	case ixgbe_mac_X550EM_a:
		sc->caps &= ~IX_CAP_LEGACY_INTR;
		if (sc->hw.device_id == IXGBE_DEV_ID_X550EM_A_1G_T ||
		    sc->hw.device_id == IXGBE_DEV_ID_X550EM_A_1G_T_L)
			sc->caps |= IX_CAP_TEMP_SENSOR | IX_CAP_EEE;
		sc->caps |= IX_CAP_FW_RECOVERY;
		break;

	case ixgbe_mac_82599EB:
		if (sc->hw.device_id == IXGBE_DEV_ID_82599_QSFP_SF_QP)
			sc->caps &= ~IX_CAP_LEGACY_INTR;
		break;

	default:
		break;
	}
}

static void
ix_detect_fanfail(struct ix_softc *sc, uint32_t reg, boolean_t intr)
{
	uint32_t mask;

	mask = intr ? IXGBE_EICR_GPI_SDP1_BY_MAC(&sc->hw) : IXGBE_ESDP_SDP1;
	if (reg & mask) {
		if_printf(&sc->arpcom.ac_if,
		    "CRITICAL: FAN FAILURE!!  REPLACE IMMEDIATELY!!\n");
	}
}

static void
ix_config_gpie(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t gpie;

	gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);

	if (sc->intr_type == PCI_INTR_TYPE_MSIX) {
		/* Enable Enhanced MSI-X mode */
		gpie |= IXGBE_GPIE_MSIX_MODE |
		    IXGBE_GPIE_EIAME |
		    IXGBE_GPIE_PBA_SUPPORT |
		    IXGBE_GPIE_OCD;
	}

	/* Fan Failure Interrupt */
	if (sc->caps & IX_CAP_DETECT_FANFAIL)
		gpie |= IXGBE_SDP1_GPIEN;

	/* Thermal Sensor Interrupt */
	if (sc->caps & IX_CAP_TEMP_SENSOR)
		gpie |= IXGBE_SDP0_GPIEN_X540;

	/* Link detection */
	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		gpie |= IXGBE_SDP1_GPIEN | IXGBE_SDP2_GPIEN;
		break;

	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		gpie |= IXGBE_SDP0_GPIEN_X540;
		break;

	default:
		break;
	}

	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
}

static void
ix_fw_timer(void *xsc)
{
	struct ix_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(&sc->main_serialize);

	if (ixgbe_fw_recovery_mode(&sc->hw)) {
		if ((sc->flags & IX_FLAG_FW_RECOVERY) == 0) {
			sc->flags |= IX_FLAG_FW_RECOVERY;
			if (ifp->if_flags & IFF_RUNNING) {
				if_printf(ifp,
				    "fw recovery mode entered, stop\n");
				ix_serialize_skipmain(sc);
				ix_stop(sc);
				ix_deserialize_skipmain(sc);
			} else {
				if_printf(ifp, "fw recovery mode entered\n");
			}
		}
	} else {
		if (sc->flags & IX_FLAG_FW_RECOVERY) {
			sc->flags &= ~IX_FLAG_FW_RECOVERY;
			if (ifp->if_flags & IFF_UP) {
				if_printf(ifp,
				    "fw recovery mode exited, reinit\n");
				ix_serialize_skipmain(sc);
				ix_init(sc);
				ix_deserialize_skipmain(sc);
			} else {
				if_printf(ifp, "fw recovery mode exited\n");
			}
		}
	}

	callout_reset_bycpu(&sc->fw_timer, hz, ix_fw_timer, sc,
	    ix_get_timer_cpuid(sc,
		(ifp->if_flags & IFF_NPOLLING) ? TRUE : FALSE));

	lwkt_serialize_exit(&sc->main_serialize);
}
