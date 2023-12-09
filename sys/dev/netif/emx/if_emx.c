/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 2001-2008, Intel Corporation
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
 *
 *
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include "opt_ifpoll.h"
#include "opt_emx.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
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
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include <dev/netif/ig_hal/e1000_api.h>
#include <dev/netif/ig_hal/e1000_82571.h>
#include <dev/netif/ig_hal/e1000_dragonfly.h>
#include <dev/netif/emx/if_emx.h>

#define DEBUG_HW 0

#ifdef EMX_RSS_DEBUG
#define EMX_RSS_DPRINTF(sc, lvl, fmt, ...) \
do { \
	if (sc->rss_debug >= lvl) \
		if_printf(&sc->arpcom.ac_if, fmt, __VA_ARGS__); \
} while (0)
#else	/* !EMX_RSS_DEBUG */
#define EMX_RSS_DPRINTF(sc, lvl, fmt, ...)	((void)0)
#endif	/* EMX_RSS_DEBUG */

#define EMX_NAME	"Intel(R) PRO/1000 "

#define EMX_DEVICE(id)	\
	{ EMX_VENDOR_ID, E1000_DEV_ID_##id, EMX_NAME #id }
#define EMX_DEVICE_NULL	{ 0, 0, NULL }

static const struct emx_device {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} emx_devices[] = {
	EMX_DEVICE(82571EB_COPPER),
	EMX_DEVICE(82571EB_FIBER),
	EMX_DEVICE(82571EB_SERDES),
	EMX_DEVICE(82571EB_SERDES_DUAL),
	EMX_DEVICE(82571EB_SERDES_QUAD),
	EMX_DEVICE(82571EB_QUAD_COPPER),
	EMX_DEVICE(82571EB_QUAD_COPPER_BP),
	EMX_DEVICE(82571EB_QUAD_COPPER_LP),
	EMX_DEVICE(82571EB_QUAD_FIBER),
	EMX_DEVICE(82571PT_QUAD_COPPER),

	EMX_DEVICE(82572EI_COPPER),
	EMX_DEVICE(82572EI_FIBER),
	EMX_DEVICE(82572EI_SERDES),
	EMX_DEVICE(82572EI),

	EMX_DEVICE(82573E),
	EMX_DEVICE(82573E_IAMT),
	EMX_DEVICE(82573L),

	EMX_DEVICE(80003ES2LAN_COPPER_SPT),
	EMX_DEVICE(80003ES2LAN_SERDES_SPT),
	EMX_DEVICE(80003ES2LAN_COPPER_DPT),
	EMX_DEVICE(80003ES2LAN_SERDES_DPT),

	EMX_DEVICE(82574L),
	EMX_DEVICE(82574LA),

	EMX_DEVICE(PCH_LPT_I217_LM),
	EMX_DEVICE(PCH_LPT_I217_V),
	EMX_DEVICE(PCH_LPTLP_I218_LM),
	EMX_DEVICE(PCH_LPTLP_I218_V),
	EMX_DEVICE(PCH_I218_LM2),
	EMX_DEVICE(PCH_I218_V2),
	EMX_DEVICE(PCH_I218_LM3),
	EMX_DEVICE(PCH_I218_V3),
	EMX_DEVICE(PCH_SPT_I219_LM),
	EMX_DEVICE(PCH_SPT_I219_V),
	EMX_DEVICE(PCH_SPT_I219_LM2),
	EMX_DEVICE(PCH_SPT_I219_V2),
	EMX_DEVICE(PCH_LBG_I219_LM3),
	EMX_DEVICE(PCH_SPT_I219_LM4),
	EMX_DEVICE(PCH_SPT_I219_V4),
	EMX_DEVICE(PCH_SPT_I219_LM5),
	EMX_DEVICE(PCH_SPT_I219_V5),
	EMX_DEVICE(PCH_CNP_I219_LM6),
	EMX_DEVICE(PCH_CNP_I219_V6),
	EMX_DEVICE(PCH_CNP_I219_LM7),
	EMX_DEVICE(PCH_CNP_I219_V7),
	EMX_DEVICE(PCH_ICP_I219_LM8),
	EMX_DEVICE(PCH_ICP_I219_V8),
	EMX_DEVICE(PCH_ICP_I219_LM9),
	EMX_DEVICE(PCH_ICP_I219_V9),
	EMX_DEVICE(PCH_CMP_I219_LM10),
	EMX_DEVICE(PCH_CMP_I219_V10),
	EMX_DEVICE(PCH_CMP_I219_LM11),
	EMX_DEVICE(PCH_CMP_I219_V11),
	EMX_DEVICE(PCH_CMP_I219_LM12),
	EMX_DEVICE(PCH_CMP_I219_V12),
	EMX_DEVICE(PCH_TGP_I219_LM13),
	EMX_DEVICE(PCH_TGP_I219_V13),
	EMX_DEVICE(PCH_TGP_I219_LM14),
	EMX_DEVICE(PCH_TGP_I219_V14),
	EMX_DEVICE(PCH_TGP_I219_LM15),
	EMX_DEVICE(PCH_TGP_I219_V15),
	EMX_DEVICE(PCH_ADP_I219_LM16),
	EMX_DEVICE(PCH_ADP_I219_V16),
	EMX_DEVICE(PCH_ADP_I219_LM17),
	EMX_DEVICE(PCH_ADP_I219_V17),
	EMX_DEVICE(PCH_MTP_I219_LM18),
	EMX_DEVICE(PCH_MTP_I219_V18),
	EMX_DEVICE(PCH_MTP_I219_LM19),
	EMX_DEVICE(PCH_MTP_I219_V19),

	/* required last entry */
	EMX_DEVICE_NULL
};

static int	emx_probe(device_t);
static int	emx_attach(device_t);
static int	emx_detach(device_t);
static int	emx_shutdown(device_t);
static int	emx_suspend(device_t);
static int	emx_resume(device_t);

static void	emx_init(void *);
static void	emx_stop(struct emx_softc *);
static int	emx_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	emx_start(struct ifnet *, struct ifaltq_subque *);
#ifdef IFPOLL_ENABLE
static void	emx_npoll(struct ifnet *, struct ifpoll_info *);
static void	emx_npoll_status(struct ifnet *);
static void	emx_npoll_tx(struct ifnet *, void *, int);
static void	emx_npoll_rx(struct ifnet *, void *, int);
#endif
static void	emx_watchdog(struct ifaltq_subque *);
static void	emx_media_status(struct ifnet *, struct ifmediareq *);
static int	emx_media_change(struct ifnet *);
static void	emx_timer(void *);
static void	emx_serialize(struct ifnet *, enum ifnet_serialize);
static void	emx_deserialize(struct ifnet *, enum ifnet_serialize);
static int	emx_tryserialize(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
static void	emx_serialize_assert(struct ifnet *, enum ifnet_serialize,
		    boolean_t);
#endif

static void	emx_intr(void *);
static void	emx_intr_mask(void *);
static void	emx_intr_body(struct emx_softc *, boolean_t);
static void	emx_rxeof(struct emx_rxdata *, int);
static void	emx_txeof(struct emx_txdata *);
static void	emx_tx_collect(struct emx_txdata *, boolean_t);
static void	emx_txgc_timer(void *);
static void	emx_tx_purge(struct emx_softc *);
static void	emx_enable_intr(struct emx_softc *);
static void	emx_disable_intr(struct emx_softc *);

static int	emx_dma_alloc(struct emx_softc *);
static void	emx_dma_free(struct emx_softc *);
static void	emx_init_tx_ring(struct emx_txdata *);
static int	emx_init_rx_ring(struct emx_rxdata *);
static void	emx_free_tx_ring(struct emx_txdata *);
static void	emx_free_rx_ring(struct emx_rxdata *);
static int	emx_create_tx_ring(struct emx_txdata *);
static int	emx_create_rx_ring(struct emx_rxdata *);
static void	emx_destroy_tx_ring(struct emx_txdata *, int);
static void	emx_destroy_rx_ring(struct emx_rxdata *, int);
static int	emx_newbuf(struct emx_rxdata *, int, int);
static int	emx_encap(struct emx_txdata *, struct mbuf **, int *, int *);
static int	emx_txcsum(struct emx_txdata *, struct mbuf *,
		    uint32_t *, uint32_t *);
static int	emx_tso_pullup(struct emx_txdata *, struct mbuf **);
static int	emx_tso_setup(struct emx_txdata *, struct mbuf *,
		    uint32_t *, uint32_t *);
static int	emx_get_txring_inuse(const struct emx_softc *, boolean_t);

static int 	emx_is_valid_eaddr(const uint8_t *);
static int	emx_reset(struct emx_softc *);
static void	emx_setup_ifp(struct emx_softc *);
static void	emx_init_tx_unit(struct emx_softc *);
static void	emx_init_rx_unit(struct emx_softc *);
static void	emx_update_stats(struct emx_softc *);
static void	emx_set_promisc(struct emx_softc *);
static void	emx_disable_promisc(struct emx_softc *);
static void	emx_set_multi(struct emx_softc *);
static void	emx_update_link_status(struct emx_softc *);
static void	emx_smartspeed(struct emx_softc *);
static void	emx_set_itr(struct emx_softc *, uint32_t);
static void	emx_disable_aspm(struct emx_softc *);
static void	emx_flush_tx_ring(struct emx_softc *);
static void	emx_flush_rx_ring(struct emx_softc *);
static void	emx_flush_txrx_ring(struct emx_softc *);

static void	emx_print_debug_info(struct emx_softc *);
static void	emx_print_nvm_info(struct emx_softc *);
static void	emx_print_hw_stats(struct emx_softc *);

static int	emx_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int	emx_sysctl_debug_info(SYSCTL_HANDLER_ARGS);
static int	emx_sysctl_int_throttle(SYSCTL_HANDLER_ARGS);
static int	emx_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS);
static int	emx_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS);
static void	emx_add_sysctl(struct emx_softc *);

static void	emx_serialize_skipmain(struct emx_softc *);
static void	emx_deserialize_skipmain(struct emx_softc *);

/* Management and WOL Support */
static void	emx_get_mgmt(struct emx_softc *);
static void	emx_rel_mgmt(struct emx_softc *);
static void	emx_get_hw_control(struct emx_softc *);
static void	emx_rel_hw_control(struct emx_softc *);
static void	emx_enable_wol(device_t);

static device_method_t emx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		emx_probe),
	DEVMETHOD(device_attach,	emx_attach),
	DEVMETHOD(device_detach,	emx_detach),
	DEVMETHOD(device_shutdown,	emx_shutdown),
	DEVMETHOD(device_suspend,	emx_suspend),
	DEVMETHOD(device_resume,	emx_resume),
	DEVMETHOD_END
};

static driver_t emx_driver = {
	"emx",
	emx_methods,
	sizeof(struct emx_softc),
};

static devclass_t emx_devclass;

DECLARE_DUMMY_MODULE(if_emx);
MODULE_DEPEND(emx, ig_hal, 1, 1, 1);
DRIVER_MODULE(if_emx, pci, emx_driver, emx_devclass, NULL, NULL);

/*
 * Tunables
 */
static int	emx_int_throttle_ceil = EMX_DEFAULT_ITR;
static int	emx_rxd = EMX_DEFAULT_RXD;
static int	emx_txd = EMX_DEFAULT_TXD;
static int	emx_smart_pwr_down = 0;
static int	emx_rxr = 0;
static int	emx_txr = 1;

/* Controls whether promiscuous also shows bad packets */
static int	emx_debug_sbp = 0;

static int	emx_82573_workaround = 1;
static int	emx_msi_enable = 1;

static char	emx_flowctrl[IFM_ETH_FC_STRLEN] = IFM_ETH_FC_NONE;

TUNABLE_INT("hw.emx.int_throttle_ceil", &emx_int_throttle_ceil);
TUNABLE_INT("hw.emx.rxd", &emx_rxd);
TUNABLE_INT("hw.emx.rxr", &emx_rxr);
TUNABLE_INT("hw.emx.txd", &emx_txd);
TUNABLE_INT("hw.emx.txr", &emx_txr);
TUNABLE_INT("hw.emx.smart_pwr_down", &emx_smart_pwr_down);
TUNABLE_INT("hw.emx.sbp", &emx_debug_sbp);
TUNABLE_INT("hw.emx.82573_workaround", &emx_82573_workaround);
TUNABLE_INT("hw.emx.msi.enable", &emx_msi_enable);
TUNABLE_STR("hw.emx.flow_ctrl", emx_flowctrl, sizeof(emx_flowctrl));

/* Global used in WOL setup with multiport cards */
static int	emx_global_quad_port_a = 0;

/* Set this to one to display debug statistics */
static int	emx_display_debug_stats = 0;

#if !defined(KTR_IF_EMX)
#define KTR_IF_EMX	KTR_ALL
#endif
KTR_INFO_MASTER(if_emx);
KTR_INFO(KTR_IF_EMX, if_emx, intr_beg, 0, "intr begin");
KTR_INFO(KTR_IF_EMX, if_emx, intr_end, 1, "intr end");
KTR_INFO(KTR_IF_EMX, if_emx, pkt_receive, 4, "rx packet");
KTR_INFO(KTR_IF_EMX, if_emx, pkt_txqueue, 5, "tx packet");
KTR_INFO(KTR_IF_EMX, if_emx, pkt_txclean, 6, "tx clean");
#define logif(name)	KTR_LOG(if_emx_ ## name)

static __inline void
emx_setup_rxdesc(emx_rxdesc_t *rxd, const struct emx_rxbuf *rxbuf)
{
	rxd->rxd_bufaddr = htole64(rxbuf->paddr);
	/* DD bit must be cleared */
	rxd->rxd_staterr = 0;
}

static __inline void
emx_free_txbuf(struct emx_txdata *tdata, struct emx_txbuf *tx_buffer)
{

	KKASSERT(tx_buffer->m_head != NULL);
	KKASSERT(tdata->tx_nmbuf > 0);
	tdata->tx_nmbuf--;

	bus_dmamap_unload(tdata->txtag, tx_buffer->map);
	m_freem(tx_buffer->m_head);
	tx_buffer->m_head = NULL;
}

static __inline void
emx_tx_intr(struct emx_txdata *tdata)
{

	emx_txeof(tdata);
	if (!ifsq_is_empty(tdata->ifsq))
		ifsq_devstart(tdata->ifsq);
}

static __inline void
emx_try_txgc(struct emx_txdata *tdata, int16_t dec)
{

	if (tdata->tx_running > 0) {
		tdata->tx_running -= dec;
		if (tdata->tx_running <= 0 && tdata->tx_nmbuf &&
		    tdata->num_tx_desc_avail < tdata->num_tx_desc &&
		    tdata->num_tx_desc_avail + tdata->tx_intr_nsegs >
		    tdata->num_tx_desc)
			emx_tx_collect(tdata, TRUE);
	}
}

static void
emx_txgc_timer(void *xtdata)
{
	struct emx_txdata *tdata = xtdata;
	struct ifnet *ifp = &tdata->sc->arpcom.ac_if;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP))
		return;

	if (!lwkt_serialize_try(&tdata->tx_serialize))
		goto done;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP)) {
		lwkt_serialize_exit(&tdata->tx_serialize);
		return;
	}
	emx_try_txgc(tdata, EMX_TX_RUNNING_DEC);

	lwkt_serialize_exit(&tdata->tx_serialize);
done:
	callout_reset(&tdata->tx_gc_timer, 1, emx_txgc_timer, tdata);
}

static __inline void
emx_rxcsum(uint32_t staterr, struct mbuf *mp)
{
	/* Ignore Checksum bit is set */
	if (staterr & E1000_RXD_STAT_IXSM)
		return;

	if ((staterr & (E1000_RXD_STAT_IPCS | E1000_RXDEXT_STATERR_IPE)) ==
	    E1000_RXD_STAT_IPCS)
		mp->m_pkthdr.csum_flags |= CSUM_IP_CHECKED | CSUM_IP_VALID;

	if ((staterr & (E1000_RXD_STAT_TCPCS | E1000_RXDEXT_STATERR_TCPE)) ==
	    E1000_RXD_STAT_TCPCS) {
		mp->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
					   CSUM_PSEUDO_HDR |
					   CSUM_FRAG_NOT_CHECKED;
		mp->m_pkthdr.csum_data = htons(0xffff);
	}
}

static __inline struct pktinfo *
emx_rssinfo(struct mbuf *m, struct pktinfo *pi,
	    uint32_t mrq, uint32_t hash, uint32_t staterr)
{
	switch (mrq & EMX_RXDMRQ_RSSTYPE_MASK) {
	case EMX_RXDMRQ_IPV4_TCP:
		pi->pi_netisr = NETISR_IP;
		pi->pi_flags = 0;
		pi->pi_l3proto = IPPROTO_TCP;
		break;

	case EMX_RXDMRQ_IPV6_TCP:
		pi->pi_netisr = NETISR_IPV6;
		pi->pi_flags = 0;
		pi->pi_l3proto = IPPROTO_TCP;
		break;

	case EMX_RXDMRQ_IPV4:
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
emx_probe(device_t dev)
{
	const struct emx_device *d;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = emx_devices; d->desc != NULL; ++d) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			device_set_async_attach(dev, TRUE);
			return 0;
		}
	}
	return ENXIO;
}

static int
emx_attach(device_t dev)
{
	struct emx_softc *sc = device_get_softc(dev);
	int error = 0, i, throttle, msi_enable;
	int tx_ring_max, ring_cnt;
	u_int intr_flags;
	uint16_t eeprom_data, device_id, apme_mask;
	driver_intr_t *intr_func;
	char flowctrl[IFM_ETH_FC_STRLEN];

	/*
	 * Setup RX rings
	 */
	for (i = 0; i < EMX_NRX_RING; ++i) {
		sc->rx_data[i].sc = sc;
		sc->rx_data[i].idx = i;
	}

	/*
	 * Setup TX ring
	 */
	for (i = 0; i < EMX_NTX_RING; ++i) {
		sc->tx_data[i].sc = sc;
		sc->tx_data[i].idx = i;
		callout_init_mp(&sc->tx_data[i].tx_gc_timer);
	}

	/*
	 * Initialize serializers
	 */
	lwkt_serialize_init(&sc->main_serialize);
	for (i = 0; i < EMX_NTX_RING; ++i)
		lwkt_serialize_init(&sc->tx_data[i].tx_serialize);
	for (i = 0; i < EMX_NRX_RING; ++i)
		lwkt_serialize_init(&sc->rx_data[i].rx_serialize);

	/*
	 * Initialize serializer array
	 */
	i = 0;

	KKASSERT(i < EMX_NSERIALIZE);
	sc->serializes[i++] = &sc->main_serialize;

	KKASSERT(i < EMX_NSERIALIZE);
	sc->serializes[i++] = &sc->tx_data[0].tx_serialize;
	KKASSERT(i < EMX_NSERIALIZE);
	sc->serializes[i++] = &sc->tx_data[1].tx_serialize;

	KKASSERT(i < EMX_NSERIALIZE);
	sc->serializes[i++] = &sc->rx_data[0].rx_serialize;
	KKASSERT(i < EMX_NSERIALIZE);
	sc->serializes[i++] = &sc->rx_data[1].rx_serialize;

	KKASSERT(i == EMX_NSERIALIZE);

	ifmedia_init(&sc->media, IFM_IMASK | IFM_ETH_FCMASK,
	    emx_media_change, emx_media_status);
	callout_init_mp(&sc->timer);

	sc->dev = sc->osdep.dev = dev;

	/*
	 * Determine hardware and mac type
	 */
	sc->hw.vendor_id = pci_get_vendor(dev);
	sc->hw.device_id = pci_get_device(dev);
	sc->hw.revision_id = pci_get_revid(dev);
	sc->hw.subsystem_vendor_id = pci_get_subvendor(dev);
	sc->hw.subsystem_device_id = pci_get_subdevice(dev);

	if (e1000_set_mac_type(&sc->hw))
		return ENXIO;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate IO memory
	 */
	sc->memory_rid = EMX_BAR_MEM;
	sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					    &sc->memory_rid, RF_ACTIVE);
	if (sc->memory == NULL) {
		device_printf(dev, "Unable to allocate bus resource: memory\n");
		error = ENXIO;
		goto fail;
	}
	sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->memory);
	sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->memory);

	/* XXX This is quite goofy, it is not actually used */
	sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;

	/*
	 * Don't enable MSI-X on 82574, see:
	 * 82574 specification update errata #15
	 *
	 * Don't enable MSI on 82571/82572, see:
	 * 82571/82572 specification update errata #63
	 */
	msi_enable = emx_msi_enable;
	if (msi_enable &&
	    (sc->hw.mac.type == e1000_82571 ||
	     sc->hw.mac.type == e1000_82572))
		msi_enable = 0;
again:
	/*
	 * Allocate interrupt
	 */
	sc->intr_type = pci_alloc_1intr(dev, msi_enable,
	    &sc->intr_rid, &intr_flags);

	if (sc->intr_type == PCI_INTR_TYPE_LEGACY) {
		int unshared;

		unshared = device_getenv_int(dev, "irq.unshared", 0);
		if (!unshared) {
			sc->flags |= EMX_FLAG_SHARED_INTR;
			if (bootverbose)
				device_printf(dev, "IRQ shared\n");
		} else {
			intr_flags &= ~RF_SHAREABLE;
			if (bootverbose)
				device_printf(dev, "IRQ unshared\n");
		}
	}

	sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->intr_rid,
	    intr_flags);
	if (sc->intr_res == NULL) {
		device_printf(dev, "Unable to allocate bus resource: %s\n",
		    sc->intr_type == PCI_INTR_TYPE_MSI ? "MSI" : "legacy intr");
		if (!msi_enable) {
			/* Retry with MSI. */
			msi_enable = 1;
			sc->flags &= ~EMX_FLAG_SHARED_INTR;
			goto again;
		}
		error = ENXIO;
		goto fail;
	}

	/* Save PCI command register for Shared Code */
	sc->hw.bus.pci_cmd_word = pci_read_config(dev, PCIR_COMMAND, 2);
	sc->hw.back = &sc->osdep;

	/*
	 * For I217/I218, we need to map the flash memory and this
	 * must happen after the MAC is identified.
	 */
	if (sc->hw.mac.type == e1000_pch_lpt) {
		sc->flash_rid = EMX_BAR_FLASH;

		sc->flash = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &sc->flash_rid, RF_ACTIVE);
		if (sc->flash == NULL) {
			device_printf(dev, "Mapping of Flash failed\n");
			error = ENXIO;
			goto fail;
		}
		sc->osdep.flash_bus_space_tag = rman_get_bustag(sc->flash);
		sc->osdep.flash_bus_space_handle =
		    rman_get_bushandle(sc->flash);

		/*
		 * This is used in the shared code
		 * XXX this goof is actually not used.
		 */
		sc->hw.flash_address = (uint8_t *)sc->flash;
	} else if (sc->hw.mac.type >= e1000_pch_spt) {
		/*
		 * In the new SPT device flash is not a seperate BAR,
		 * rather it is also in BAR0, so use the same tag and
		 * an offset handle for the FLASH read/write macros
		 * in the shared code.
		 */
		sc->osdep.flash_bus_space_tag = sc->osdep.mem_bus_space_tag;
		sc->osdep.flash_bus_space_handle =
		    sc->osdep.mem_bus_space_handle + E1000_FLASH_BASE_ADDR;
	}

	/* Do Shared Code initialization */
	if (e1000_setup_init_funcs(&sc->hw, TRUE)) {
		device_printf(dev, "Setup of Shared code failed\n");
		error = ENXIO;
		goto fail;
	}
	e1000_get_bus_info(&sc->hw);

	sc->hw.mac.autoneg = EMX_DO_AUTO_NEG;
	sc->hw.phy.autoneg_wait_to_complete = FALSE;
	sc->hw.phy.autoneg_advertised = EMX_AUTONEG_ADV_DEFAULT;

	/*
	 * Interrupt throttle rate
	 */
	throttle = device_getenv_int(dev, "int_throttle_ceil",
	    emx_int_throttle_ceil);
	if (throttle == 0) {
		sc->int_throttle_ceil = 0;
	} else {
		if (throttle < 0)
			throttle = EMX_DEFAULT_ITR;

		/* Recalculate the tunable value to get the exact frequency. */
		throttle = 1000000000 / 256 / throttle;

		/* Upper 16bits of ITR is reserved and should be zero */
		if (throttle & 0xffff0000)
			throttle = 1000000000 / 256 / EMX_DEFAULT_ITR;

		sc->int_throttle_ceil = 1000000000 / 256 / throttle;
	}

	e1000_init_script_state_82541(&sc->hw, TRUE);
	e1000_set_tbi_compatibility_82543(&sc->hw, TRUE);

	/* Copper options */
	if (sc->hw.phy.media_type == e1000_media_type_copper) {
		sc->hw.phy.mdix = EMX_AUTO_ALL_MODES;
		sc->hw.phy.disable_polarity_correction = FALSE;
		sc->hw.phy.ms_type = EMX_MASTER_SLAVE;
	}

	/* Set the frame limits assuming standard ethernet sized frames. */
	sc->hw.mac.max_frame_size = ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN;

	/* This controls when hardware reports transmit completion status. */
	sc->hw.mac.report_tx_early = 1;

	/*
	 * Calculate # of RX/TX rings
	 */
	ring_cnt = device_getenv_int(dev, "rxr", emx_rxr);
	sc->rx_rmap = if_ringmap_alloc(dev, ring_cnt, EMX_NRX_RING);

	tx_ring_max = 1;
	if (sc->hw.mac.type == e1000_82571 ||
	    sc->hw.mac.type == e1000_82572 ||
	    sc->hw.mac.type == e1000_80003es2lan ||
	    sc->hw.mac.type == e1000_pch_lpt ||
	    sc->hw.mac.type == e1000_pch_spt ||
	    sc->hw.mac.type == e1000_pch_cnp ||
	    sc->hw.mac.type == e1000_82574)
		tx_ring_max = EMX_NTX_RING;
	ring_cnt = device_getenv_int(dev, "txr", emx_txr);
	sc->tx_rmap = if_ringmap_alloc(dev, ring_cnt, tx_ring_max);

	if_ringmap_match(dev, sc->rx_rmap, sc->tx_rmap);
	sc->rx_ring_cnt = if_ringmap_count(sc->rx_rmap);
	sc->tx_ring_cnt = if_ringmap_count(sc->tx_rmap);

	/* Allocate RX/TX rings' busdma(9) stuffs */
	error = emx_dma_alloc(sc);
	if (error)
		goto fail;

	/* Allocate multicast array memory. */
	sc->mta = kmalloc(ETH_ADDR_LEN * EMX_MCAST_ADDR_MAX,
	    M_DEVBUF, M_WAITOK);

	/* Indicate SOL/IDER usage */
	if (e1000_check_reset_block(&sc->hw)) {
		device_printf(dev,
		    "PHY reset is blocked due to SOL/IDER session.\n");
	}

	/* Disable EEE on I217/I218 */
	sc->hw.dev_spec.ich8lan.eee_disable = 1;

	/*
	 * Start from a known state, this is important in reading the
	 * nvm and mac from that.
	 */
	e1000_reset_hw(&sc->hw);

	/* Make sure we have a good EEPROM before we read from it */
	if (e1000_validate_nvm_checksum(&sc->hw) < 0) {
		/*
		 * Some PCI-E parts fail the first check due to
		 * the link being in sleep state, call it again,
		 * if it fails a second time its a real issue.
		 */
		if (e1000_validate_nvm_checksum(&sc->hw) < 0) {
			device_printf(dev,
			    "The EEPROM Checksum Is Not Valid\n");
			error = EIO;
			goto fail;
		}
	}

	/* Copy the permanent MAC address out of the EEPROM */
	if (e1000_read_mac_addr(&sc->hw) < 0) {
		device_printf(dev, "EEPROM read error while reading MAC"
		    " address\n");
		error = EIO;
		goto fail;
	}
	if (!emx_is_valid_eaddr(sc->hw.mac.addr)) {
		device_printf(dev, "Invalid MAC address\n");
		error = EIO;
		goto fail;
	}

	/* Disable ULP support */
	e1000_disable_ulp_lpt_lp(&sc->hw, TRUE);

	/* Determine if we have to control management hardware */
	if (e1000_enable_mng_pass_thru(&sc->hw))
		sc->flags |= EMX_FLAG_HAS_MGMT;

	/*
	 * Setup Wake-on-Lan
	 */
	apme_mask = EMX_EEPROM_APME;
	eeprom_data = 0;
	switch (sc->hw.mac.type) {
	case e1000_82573:
		sc->flags |= EMX_FLAG_HAS_AMT;
		/* FALL THROUGH */

	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		if (sc->hw.bus.func == 1) {
			e1000_read_nvm(&sc->hw,
			    NVM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);
		} else {
			e1000_read_nvm(&sc->hw,
			    NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		}
		break;

	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		apme_mask = E1000_WUC_APME;
		sc->flags |= EMX_FLAG_HAS_AMT;
		eeprom_data = E1000_READ_REG(&sc->hw, E1000_WUC);
		break;

	default:
		e1000_read_nvm(&sc->hw,
		    NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		break;
	}
	if (eeprom_data & apme_mask)
		sc->wol = E1000_WUFC_MAG | E1000_WUFC_MC;

	/*
         * We have the eeprom settings, now apply the special cases
         * where the eeprom may be wrong or the board won't support
         * wake on lan on a particular port
	 */
	device_id = pci_get_device(dev);
        switch (device_id) {
	case E1000_DEV_ID_82571EB_FIBER:
		/*
		 * Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting
		 */
		if (E1000_READ_REG(&sc->hw, E1000_STATUS) &
		    E1000_STATUS_FUNC_1)
			sc->wol = 0;
		break;

	case E1000_DEV_ID_82571EB_QUAD_COPPER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_COPPER_LP:
                /* if quad port sc, disable WoL on all but port A */
		if (emx_global_quad_port_a != 0)
			sc->wol = 0;
		/* Reset for multiple quad port adapters */
		if (++emx_global_quad_port_a == 4)
			emx_global_quad_port_a = 0;
                break;
	}

	/* XXX disable wol */
	sc->wol = 0;

	/* Initialized #of TX rings to use. */
	sc->tx_ring_inuse = emx_get_txring_inuse(sc, FALSE);

	/* Setup flow control. */
	device_getenv_string(dev, "flow_ctrl", flowctrl, sizeof(flowctrl),
	    emx_flowctrl);
	sc->ifm_flowctrl = ifmedia_str2ethfc(flowctrl);

	/* Setup OS specific network interface */
	emx_setup_ifp(sc);

	/* Add sysctl tree, must after em_setup_ifp() */
	emx_add_sysctl(sc);

	/* Reset the hardware */
	error = emx_reset(sc);
	if (error) {
		/*
		 * Some 82573 parts fail the first reset, call it again,
		 * if it fails a second time its a real issue.
		 */
		error = emx_reset(sc);
		if (error) {
			device_printf(dev, "Unable to reset the hardware\n");
			ether_ifdetach(&sc->arpcom.ac_if);
			goto fail;
		}
	}

	/* Initialize statistics */
	emx_update_stats(sc);

	sc->hw.mac.get_link_status = 1;
	emx_update_link_status(sc);

	/* Non-AMT based hardware can now take control from firmware */
	if ((sc->flags & (EMX_FLAG_HAS_MGMT | EMX_FLAG_HAS_AMT)) ==
	    EMX_FLAG_HAS_MGMT)
		emx_get_hw_control(sc);

	/*
	 * Missing Interrupt Following ICR read:
	 *
	 * 82571/82572 specification update errata #76
	 * 82573 specification update errata #31
	 * 82574 specification update errata #12
	 */
	intr_func = emx_intr;
	if ((sc->flags & EMX_FLAG_SHARED_INTR) &&
	    (sc->hw.mac.type == e1000_82571 ||
	     sc->hw.mac.type == e1000_82572 ||
	     sc->hw.mac.type == e1000_82573 ||
	     sc->hw.mac.type == e1000_82574))
		intr_func = emx_intr_mask;

	error = bus_setup_intr(dev, sc->intr_res, INTR_MPSAFE, intr_func, sc,
			       &sc->intr_tag, &sc->main_serialize);
	if (error) {
		device_printf(dev, "Failed to register interrupt handler");
		ether_ifdetach(&sc->arpcom.ac_if);
		goto fail;
	}
	return (0);
fail:
	emx_detach(dev);
	return (error);
}

static int
emx_detach(device_t dev)
{
	struct emx_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ifnet_serialize_all(ifp);

		emx_stop(sc);

		e1000_phy_hw_reset(&sc->hw);

		emx_rel_mgmt(sc);
		emx_rel_hw_control(sc);

		if (sc->wol) {
			E1000_WRITE_REG(&sc->hw, E1000_WUC, E1000_WUC_PME_EN);
			E1000_WRITE_REG(&sc->hw, E1000_WUFC, sc->wol);
			emx_enable_wol(dev);
		}

		bus_teardown_intr(dev, sc->intr_res, sc->intr_tag);

		ifnet_deserialize_all(ifp);

		ether_ifdetach(ifp);
	} else if (sc->memory != NULL) {
		emx_rel_hw_control(sc);
	}

	ifmedia_removeall(&sc->media);
	bus_generic_detach(dev);

	if (sc->intr_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->intr_rid,
				     sc->intr_res);
	}

	if (sc->intr_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->memory != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->memory_rid,
				     sc->memory);
	}

	if (sc->flash != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->flash_rid,
		    sc->flash);
	}

	emx_dma_free(sc);

	if (sc->mta != NULL)
		kfree(sc->mta, M_DEVBUF);

	if (sc->rx_rmap != NULL)
		if_ringmap_free(sc->rx_rmap);
	if (sc->tx_rmap != NULL)
		if_ringmap_free(sc->tx_rmap);

	return (0);
}

static int
emx_shutdown(device_t dev)
{
	return emx_suspend(dev);
}

static int
emx_suspend(device_t dev)
{
	struct emx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);

	emx_stop(sc);

	emx_rel_mgmt(sc);
	emx_rel_hw_control(sc);

	if (sc->wol) {
		E1000_WRITE_REG(&sc->hw, E1000_WUC, E1000_WUC_PME_EN);
		E1000_WRITE_REG(&sc->hw, E1000_WUFC, sc->wol);
		emx_enable_wol(dev);
	}

	ifnet_deserialize_all(ifp);

	return bus_generic_suspend(dev);
}

static int
emx_resume(device_t dev)
{
	struct emx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ifnet_serialize_all(ifp);

	emx_init(sc);
	emx_get_mgmt(sc);
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ifsq_devstart_sched(sc->tx_data[i].ifsq);

	ifnet_deserialize_all(ifp);

	return bus_generic_resume(dev);
}

static void
emx_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct emx_softc *sc = ifp->if_softc;
	struct emx_txdata *tdata = ifsq_get_priv(ifsq);
	struct mbuf *m_head;
	int idx = -1, nsegs = 0;

	KKASSERT(tdata->ifsq == ifsq);
	ASSERT_SERIALIZED(&tdata->tx_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	if (!sc->link_active || (tdata->tx_flags & EMX_TXFLAG_ENABLED) == 0) {
		ifsq_purge(ifsq);
		return;
	}

	while (!ifsq_is_empty(ifsq)) {
		/* Now do we at least have a minimal? */
		if (EMX_IS_OACTIVE(tdata)) {
			emx_tx_collect(tdata, FALSE);
			if (EMX_IS_OACTIVE(tdata)) {
				ifsq_set_oactive(ifsq);
				break;
			}
		}

		logif(pkt_txqueue);
		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		if (emx_encap(tdata, &m_head, &nsegs, &idx)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			emx_tx_collect(tdata, FALSE);
			continue;
		}

		/*
		 * TX interrupt are aggressively aggregated, so increasing
		 * opackets at TX interrupt time will make the opackets
		 * statistics vastly inaccurate; we do the opackets increment
		 * now.
		 */
		IFNET_STAT_INC(ifp, opackets, 1);

		if (nsegs >= tdata->tx_wreg_nsegs) {
			E1000_WRITE_REG(&sc->hw, E1000_TDT(tdata->idx), idx);
			nsegs = 0;
			idx = -1;
		}

		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, m_head);

		/* Set timeout in case hardware has problems transmitting. */
		ifsq_watchdog_set_count(&tdata->tx_watchdog, EMX_TX_TIMEOUT);
	}
	if (idx >= 0)
		E1000_WRITE_REG(&sc->hw, E1000_TDT(tdata->idx), idx);
	tdata->tx_running = EMX_TX_RUNNING;
}

static int
emx_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct emx_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	uint16_t eeprom_data = 0;
	int max_frame_size, mask, reinit;
	int error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (command) {
	case SIOCSIFMTU:
		switch (sc->hw.mac.type) {
		case e1000_82573:
			/*
			 * 82573 only supports jumbo frames
			 * if ASPM is disabled.
			 */
			e1000_read_nvm(&sc->hw, NVM_INIT_3GIO_3, 1,
				       &eeprom_data);
			if (eeprom_data & NVM_WORD1A_ASPM_MASK) {
				max_frame_size = ETHER_MAX_LEN;
				break;
			}
			/* FALL THROUGH */

		/* Limit Jumbo Frame size */
		case e1000_82571:
		case e1000_82572:
		case e1000_82574:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
		case e1000_80003es2lan:
			max_frame_size = 9234;
			break;

		default:
			max_frame_size = MAX_JUMBO_FRAME_SIZE;
			break;
		}
		if (ifr->ifr_mtu > max_frame_size - ETHER_HDR_LEN -
		    ETHER_CRC_LEN) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		sc->hw.mac.max_frame_size = ifp->if_mtu + ETHER_HDR_LEN +
		    ETHER_CRC_LEN;

		if (ifp->if_flags & IFF_RUNNING)
			emx_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING)) {
				if ((ifp->if_flags ^ sc->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					emx_disable_promisc(sc);
					emx_set_promisc(sc);
				}
			} else {
				emx_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			emx_stop(sc);
		}
		sc->if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING) {
			emx_disable_intr(sc);
			emx_set_multi(sc);
#ifdef IFPOLL_ENABLE
			if (!(ifp->if_flags & IFF_NPOLLING))
#endif
				emx_enable_intr(sc);
		}
		break;

	case SIOCSIFMEDIA:
		/* Check SOL/IDER usage */
		if (e1000_check_reset_block(&sc->hw)) {
			device_printf(sc->dev, "Media change is"
			    " blocked due to SOL/IDER session.\n");
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
				ifp->if_hwassist |= EMX_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~EMX_CSUM_FEATURES;
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
			emx_init(sc);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static void
emx_watchdog(struct ifaltq_subque *ifsq)
{
	struct emx_txdata *tdata = ifsq_get_priv(ifsq);
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct emx_softc *sc = ifp->if_softc;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/*
	 * The timer is set to 5 every time start queues a packet.
	 * Then txeof keeps resetting it as long as it cleans at
	 * least one descriptor.
	 * Finally, anytime all descriptors are clean the timer is
	 * set to 0.
	 */

	if (E1000_READ_REG(&sc->hw, E1000_TDT(tdata->idx)) ==
	    E1000_READ_REG(&sc->hw, E1000_TDH(tdata->idx))) {
		/*
		 * If we reach here, all TX jobs are completed and
		 * the TX engine should have been idled for some time.
		 * We don't need to call ifsq_devstart_sched() here.
		 */
		ifsq_clr_oactive(ifsq);
		ifsq_watchdog_set_count(&tdata->tx_watchdog, 0);
		return;
	}

	/*
	 * If we are in this routine because of pause frames, then
	 * don't reset the hardware.
	 */
	if (E1000_READ_REG(&sc->hw, E1000_STATUS) & E1000_STATUS_TXOFF) {
		ifsq_watchdog_set_count(&tdata->tx_watchdog, EMX_TX_TIMEOUT);
		return;
	}

	if_printf(ifp, "TX %d watchdog timeout -- resetting\n", tdata->idx);

	IFNET_STAT_INC(ifp, oerrors, 1);

	emx_init(sc);
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		ifsq_devstart_sched(sc->tx_data[i].ifsq);
}

static void
emx_init(void *xsc)
{
	struct emx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	device_t dev = sc->dev;
	boolean_t polling;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	emx_stop(sc);

	/* Get the latest mac address, User can use a LAA */
        bcopy(IF_LLADDR(ifp), sc->hw.mac.addr, ETHER_ADDR_LEN);

	/* Put the address into the Receive Address Array */
	e1000_rar_set(&sc->hw, sc->hw.mac.addr, 0);

	/*
	 * With the 82571 sc, RAR[0] may be overwritten
	 * when the other port is reset, we make a duplicate
	 * in RAR[14] for that eventuality, this assures
	 * the interface continues to function.
	 */
	if (sc->hw.mac.type == e1000_82571) {
		e1000_set_laa_state_82571(&sc->hw, TRUE);
		e1000_rar_set(&sc->hw, sc->hw.mac.addr,
		    E1000_RAR_ENTRIES - 1);
	}

	/* Initialize the hardware */
	if (emx_reset(sc)) {
		device_printf(dev, "Unable to reset the hardware\n");
		/* XXX emx_stop()? */
		return;
	}
	emx_update_link_status(sc);

	/* Setup VLAN support, basic and offload if available */
	E1000_WRITE_REG(&sc->hw, E1000_VET, ETHERTYPE_VLAN);

	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) {
		uint32_t ctrl;

		ctrl = E1000_READ_REG(&sc->hw, E1000_CTRL);
		ctrl |= E1000_CTRL_VME;
		E1000_WRITE_REG(&sc->hw, E1000_CTRL, ctrl);
	}

	/* Configure for OS presence */
	emx_get_mgmt(sc);

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif
	sc->tx_ring_inuse = emx_get_txring_inuse(sc, polling);
	ifq_set_subq_divisor(&ifp->if_snd, sc->tx_ring_inuse);

	/* Prepare transmit descriptors and buffers */
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		emx_init_tx_ring(&sc->tx_data[i]);
	emx_init_tx_unit(sc);

	/* Setup Multicast table */
	emx_set_multi(sc);

	/* Prepare receive descriptors and buffers */
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		if (emx_init_rx_ring(&sc->rx_data[i])) {
			device_printf(dev,
			    "Could not setup receive structures\n");
			emx_stop(sc);
			return;
		}
	}
	emx_init_rx_unit(sc);

	/* Don't lose promiscuous settings */
	emx_set_promisc(sc);

	/* Reset hardware counters */
	e1000_clear_hw_cntrs_base_generic(&sc->hw);

	/* MSI/X configuration for 82574 */
	if (sc->hw.mac.type == e1000_82574) {
		int tmp;

		tmp = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
		tmp |= E1000_CTRL_EXT_PBA_CLR;
		E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT, tmp);
		/*
		 * XXX MSIX
		 * Set the IVAR - interrupt vector routing.
		 * Each nibble represents a vector, high bit
		 * is enable, other 3 bits are the MSIX table
		 * entry, we map RXQ0 to 0, TXQ0 to 1, and
		 * Link (other) to 2, hence the magic number.
		 */
		E1000_WRITE_REG(&sc->hw, E1000_IVAR, 0x800A0908);
	}

	/*
	 * Only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (polling)
		emx_disable_intr(sc);
	else
		emx_enable_intr(sc);

	/* AMT based hardware can now take control from firmware */
	if ((sc->flags & (EMX_FLAG_HAS_MGMT | EMX_FLAG_HAS_AMT)) ==
	    (EMX_FLAG_HAS_MGMT | EMX_FLAG_HAS_AMT))
		emx_get_hw_control(sc);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct emx_txdata *tdata = &sc->tx_data[i];

		ifsq_clr_oactive(tdata->ifsq);
		ifsq_watchdog_start(&tdata->tx_watchdog);
		if (!polling) {
			callout_reset_bycpu(&tdata->tx_gc_timer, 1,
			    emx_txgc_timer, tdata, ifsq_get_cpuid(tdata->ifsq));
		}
	}
	callout_reset(&sc->timer, hz, emx_timer, sc);
}

static void
emx_intr(void *xsc)
{
	emx_intr_body(xsc, TRUE);
}

static void
emx_intr_body(struct emx_softc *sc, boolean_t chk_asserted)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg_icr;

	logif(intr_beg);
	ASSERT_SERIALIZED(&sc->main_serialize);

	reg_icr = E1000_READ_REG(&sc->hw, E1000_ICR);

	if (chk_asserted && (reg_icr & E1000_ICR_INT_ASSERTED) == 0) {
		logif(intr_end);
		return;
	}

	/*
	 * XXX: some laptops trigger several spurious interrupts
	 * on emx(4) when in the resume cycle. The ICR register
	 * reports all-ones value in this case. Processing such
	 * interrupts would lead to a freeze. I don't know why.
	 */
	if (reg_icr == 0xffffffff) {
		logif(intr_end);
		return;
	}

	if (ifp->if_flags & IFF_RUNNING) {
		if (reg_icr &
		    (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO)) {
			int i;

			for (i = 0; i < sc->rx_ring_cnt; ++i) {
				lwkt_serialize_enter(
				&sc->rx_data[i].rx_serialize);
				emx_rxeof(&sc->rx_data[i], -1);
				lwkt_serialize_exit(
				&sc->rx_data[i].rx_serialize);
			}
		}
		if (reg_icr & E1000_ICR_TXDW) {
			struct emx_txdata *tdata = &sc->tx_data[0];

			lwkt_serialize_enter(&tdata->tx_serialize);
			emx_tx_intr(tdata);
			lwkt_serialize_exit(&tdata->tx_serialize);
		}
	}

	/* Link status change */
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		emx_serialize_skipmain(sc);

		callout_stop(&sc->timer);
		sc->hw.mac.get_link_status = 1;
		emx_update_link_status(sc);

		/* Deal with TX cruft when link lost */
		emx_tx_purge(sc);

		callout_reset(&sc->timer, hz, emx_timer, sc);

		emx_deserialize_skipmain(sc);
	}

	if (reg_icr & E1000_ICR_RXO)
		sc->rx_overruns++;

	logif(intr_end);
}

static void
emx_intr_mask(void *xsc)
{
	struct emx_softc *sc = xsc;

	E1000_WRITE_REG(&sc->hw, E1000_IMC, 0xffffffff);
	/*
	 * NOTE:
	 * ICR.INT_ASSERTED bit will never be set if IMS is 0,
	 * so don't check it.
	 */
	emx_intr_body(sc, FALSE);
	E1000_WRITE_REG(&sc->hw, E1000_IMS, IMS_ENABLE_MASK);
}

static void
emx_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct emx_softc *sc = ifp->if_softc;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	emx_update_link_status(sc);

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

	if (sc->hw.phy.media_type == e1000_media_type_fiber ||
	    sc->hw.phy.media_type == e1000_media_type_internal_serdes) {
		ifmr->ifm_active |= IFM_1000_SX | IFM_FDX;
	} else {
		switch (sc->link_speed) {
		case 10:
			ifmr->ifm_active |= IFM_10_T;
			break;
		case 100:
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
	if (ifmr->ifm_active & IFM_FDX)
		ifmr->ifm_active |= e1000_fc2ifmedia(sc->hw.fc.current_mode);
}

static int
emx_media_change(struct ifnet *ifp)
{
	struct emx_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->hw.mac.autoneg = EMX_DO_AUTO_NEG;
		sc->hw.phy.autoneg_advertised = EMX_AUTONEG_ADV_DEFAULT;
		break;

	case IFM_1000_SX:
	case IFM_1000_T:
		sc->hw.mac.autoneg = EMX_DO_AUTO_NEG;
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
		emx_init(sc);

	return (0);
}

static int
emx_encap(struct emx_txdata *tdata, struct mbuf **m_headp,
    int *segs_used, int *idx)
{
	bus_dma_segment_t segs[EMX_MAX_SCATTER];
	bus_dmamap_t map;
	struct emx_txbuf *tx_buffer, *tx_buffer_mapped;
	struct e1000_tx_desc *ctxd = NULL;
	struct mbuf *m_head = *m_headp;
	uint32_t txd_upper, txd_lower, cmd = 0;
	int maxsegs, nsegs, i, j, first, last = 0, error;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		error = emx_tso_pullup(tdata, m_headp);
		if (error)
			return error;
		m_head = *m_headp;
	}

	txd_upper = txd_lower = 0;

	/*
	 * Capture the first descriptor index, this descriptor
	 * will have the index of the EOP which is the only one
	 * that now gets a DONE bit writeback.
	 */
	first = tdata->next_avail_tx_desc;
	tx_buffer = &tdata->tx_buf[first];
	tx_buffer_mapped = tx_buffer;
	map = tx_buffer->map;

	maxsegs = tdata->num_tx_desc_avail - EMX_TX_RESERVED;
	KASSERT(maxsegs >= tdata->spare_tx_desc, ("not enough spare TX desc"));
	if (maxsegs > EMX_MAX_SCATTER)
		maxsegs = EMX_MAX_SCATTER;

	error = bus_dmamap_load_mbuf_defrag(tdata->txtag, map, m_headp,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(*m_headp);
		*m_headp = NULL;
		return error;
	}
        bus_dmamap_sync(tdata->txtag, map, BUS_DMASYNC_PREWRITE);

	m_head = *m_headp;
	tdata->tx_nsegs += nsegs;
	*segs_used += nsegs;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		/* TSO will consume one TX desc */
		i = emx_tso_setup(tdata, m_head, &txd_upper, &txd_lower);
		tdata->tx_nsegs += i;
		*segs_used += i;
	} else if (m_head->m_pkthdr.csum_flags & EMX_CSUM_FEATURES) {
		/* TX csum offloading will consume one TX desc */
		i = emx_txcsum(tdata, m_head, &txd_upper, &txd_lower);
		tdata->tx_nsegs += i;
		*segs_used += i;
	}

        /* Handle VLAN tag */
	if (m_head->m_flags & M_VLANTAG) {
		/* Set the vlan id. */
		txd_upper |= (htole16(m_head->m_pkthdr.ether_vlantag) << 16);
		/* Tell hardware to add tag */
		txd_lower |= htole32(E1000_TXD_CMD_VLE);
	}

	i = tdata->next_avail_tx_desc;

	/* Set up our transmit descriptors */
	for (j = 0; j < nsegs; j++) {
		tx_buffer = &tdata->tx_buf[i];
		ctxd = &tdata->tx_desc_base[i];

		ctxd->buffer_addr = htole64(segs[j].ds_addr);
		ctxd->lower.data = htole32(E1000_TXD_CMD_IFCS |
					   txd_lower | segs[j].ds_len);
		ctxd->upper.data = htole32(txd_upper);

		last = i;
		if (++i == tdata->num_tx_desc)
			i = 0;
	}

	tdata->next_avail_tx_desc = i;

	KKASSERT(tdata->num_tx_desc_avail > nsegs);
	tdata->num_tx_desc_avail -= nsegs;
	tdata->tx_nmbuf++;

	tx_buffer->m_head = m_head;
	tx_buffer_mapped->map = tx_buffer->map;
	tx_buffer->map = map;

	if (tdata->tx_nsegs >= tdata->tx_intr_nsegs) {
		tdata->tx_nsegs = 0;

		/*
		 * Report Status (RS) is turned on
		 * every tx_intr_nsegs descriptors.
		 */
		cmd = E1000_TXD_CMD_RS;

		/*
		 * Keep track of the descriptor, which will
		 * be written back by hardware.
		 */
		tdata->tx_dd[tdata->tx_dd_tail] = last;
		EMX_INC_TXDD_IDX(tdata->tx_dd_tail);
		KKASSERT(tdata->tx_dd_tail != tdata->tx_dd_head);
	}

	/*
	 * Last Descriptor of Packet needs End Of Packet (EOP)
	 */
	ctxd->lower.data |= htole32(E1000_TXD_CMD_EOP | cmd);

	/*
	 * Defer TDT updating, until enough descriptors are setup
	 */
	*idx = i;

#ifdef EMX_TSS_DEBUG
	tdata->tx_pkts++;
#endif

	return (0);
}

static void
emx_set_promisc(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg_rctl;

	reg_rctl = E1000_READ_REG(&sc->hw, E1000_RCTL);

	if (ifp->if_flags & IFF_PROMISC) {
		reg_rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		/* Turn this on if you want to see bad packets */
		if (emx_debug_sbp)
			reg_rctl |= E1000_RCTL_SBP;
		E1000_WRITE_REG(&sc->hw, E1000_RCTL, reg_rctl);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg_rctl |= E1000_RCTL_MPE;
		reg_rctl &= ~E1000_RCTL_UPE;
		E1000_WRITE_REG(&sc->hw, E1000_RCTL, reg_rctl);
	}
}

static void
emx_disable_promisc(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t reg_rctl;
	int mcnt = 0;

	reg_rctl = E1000_READ_REG(&sc->hw, E1000_RCTL);
	reg_rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_SBP);

	if (ifp->if_flags & IFF_ALLMULTI) {
		mcnt = EMX_MCAST_ADDR_MAX;
	} else {
		const struct ifmultiaddr *ifma;

		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			if (mcnt == EMX_MCAST_ADDR_MAX)
				break;
			mcnt++;
		}
	}
	/* Don't disable if in MAX groups */
	if (mcnt < EMX_MCAST_ADDR_MAX)
		reg_rctl &= ~E1000_RCTL_MPE;

	E1000_WRITE_REG(&sc->hw, E1000_RCTL, reg_rctl);
}

static void
emx_set_multi(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t reg_rctl = 0;
	uint8_t *mta;
	int mcnt = 0;

	mta = sc->mta;
	bzero(mta, ETH_ADDR_LEN * EMX_MCAST_ADDR_MAX);

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == EMX_MCAST_ADDR_MAX)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		      &mta[mcnt * ETHER_ADDR_LEN], ETHER_ADDR_LEN);
		mcnt++;
	}

	if (mcnt >= EMX_MCAST_ADDR_MAX) {
		reg_rctl = E1000_READ_REG(&sc->hw, E1000_RCTL);
		reg_rctl |= E1000_RCTL_MPE;
		E1000_WRITE_REG(&sc->hw, E1000_RCTL, reg_rctl);
	} else {
		e1000_update_mc_addr_list(&sc->hw, mta, mcnt);
	}
}

/*
 * This routine checks for link status and updates statistics.
 */
static void
emx_timer(void *xsc)
{
	struct emx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(&sc->main_serialize);

	emx_update_link_status(sc);
	emx_update_stats(sc);

	/* Reset LAA into RAR[0] on 82571 */
	if (e1000_get_laa_state_82571(&sc->hw) == TRUE)
		e1000_rar_set(&sc->hw, sc->hw.mac.addr, 0);

	if (emx_display_debug_stats && (ifp->if_flags & IFF_RUNNING))
		emx_print_hw_stats(sc);

	emx_smartspeed(sc);

	callout_reset(&sc->timer, hz, emx_timer, sc);

	lwkt_serialize_exit(&sc->main_serialize);
}

static void
emx_update_link_status(struct emx_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	device_t dev = sc->dev;
	uint32_t link_check = 0;

	/* Get the cached link value or read phy for real */
	switch (hw->phy.media_type) {
	case e1000_media_type_copper:
		if (hw->mac.get_link_status) {
			if (hw->mac.type >= e1000_pch_spt)
				msec_delay(50);
			/* Do the work to read phy */
			e1000_check_for_link(hw);
			link_check = !hw->mac.get_link_status;
			if (link_check) /* ESB2 fix */
				e1000_cfg_on_link_up(hw);
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
		link_check = sc->hw.mac.serdes_has_link;
		break;

	case e1000_media_type_unknown:
	default:
		break;
	}

	/* Now check for a transition */
	if (link_check && sc->link_active == 0) {
		e1000_get_speed_and_duplex(hw, &sc->link_speed,
		    &sc->link_duplex);

		/*
		 * Check if we should enable/disable SPEED_MODE bit on
		 * 82571EB/82572EI
		 */
		if (sc->link_speed != SPEED_1000 &&
		    (hw->mac.type == e1000_82571 ||
		     hw->mac.type == e1000_82572)) {
			int tarc0;

			tarc0 = E1000_READ_REG(hw, E1000_TARC(0));
			tarc0 &= ~EMX_TARC_SPEED_MODE;
			E1000_WRITE_REG(hw, E1000_TARC(0), tarc0);
		}
		if (bootverbose) {
			char flowctrl[IFM_ETH_FC_STRLEN];

			e1000_fc2str(hw->fc.current_mode, flowctrl,
			    sizeof(flowctrl));
			device_printf(dev, "Link is up %d Mbps %s, "
			    "Flow control: %s\n",
			    sc->link_speed,
			    (sc->link_duplex == FULL_DUPLEX) ?
			    "Full Duplex" : "Half Duplex",
			    flowctrl);
		}
		if (sc->ifm_flowctrl & IFM_ETH_FORCEPAUSE)
			e1000_force_flowctrl(hw, sc->ifm_flowctrl);
		sc->link_active = 1;
		sc->smartspeed = 0;
		ifp->if_baudrate = sc->link_speed * 1000000;
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
	} else if (!link_check && sc->link_active == 1) {
		ifp->if_baudrate = sc->link_speed = 0;
		sc->link_duplex = 0;
		if (bootverbose)
			device_printf(dev, "Link is Down\n");
		sc->link_active = 0;
		ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(ifp);
	}
}

static void
emx_stop(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	emx_disable_intr(sc);

	callout_stop(&sc->timer);

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct emx_txdata *tdata = &sc->tx_data[i];

		ifsq_clr_oactive(tdata->ifsq);
		ifsq_watchdog_stop(&tdata->tx_watchdog);
		tdata->tx_flags &= ~EMX_TXFLAG_ENABLED;

		tdata->tx_running = 0;
		callout_stop(&tdata->tx_gc_timer);
	}

	/* I219 needs some special flushing to avoid hangs */
	if (sc->hw.mac.type >= e1000_pch_spt)
		emx_flush_txrx_ring(sc);

	/*
	 * Disable multiple receive queues.
	 *
	 * NOTE:
	 * We should disable multiple receive queues before
	 * resetting the hardware.
	 */
	E1000_WRITE_REG(&sc->hw, E1000_MRQC, 0);

	e1000_reset_hw(&sc->hw);
	E1000_WRITE_REG(&sc->hw, E1000_WUC, 0);

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		emx_free_tx_ring(&sc->tx_data[i]);
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		emx_free_rx_ring(&sc->rx_data[i]);
}

static int
emx_reset(struct emx_softc *sc)
{
	device_t dev = sc->dev;
	uint16_t rx_buffer_size;
	uint32_t pba;

	/* Set up smart power down as default off on newer adapters. */
	if (!emx_smart_pwr_down &&
	    (sc->hw.mac.type == e1000_82571 ||
	     sc->hw.mac.type == e1000_82572)) {
		uint16_t phy_tmp = 0;

		/* Speed up time to link by disabling smart power down. */
		e1000_read_phy_reg(&sc->hw,
		    IGP02E1000_PHY_POWER_MGMT, &phy_tmp);
		phy_tmp &= ~IGP02E1000_PM_SPD;
		e1000_write_phy_reg(&sc->hw,
		    IGP02E1000_PHY_POWER_MGMT, phy_tmp);
	}

	/*
	 * Packet Buffer Allocation (PBA)
	 * Writing PBA sets the receive portion of the buffer
	 * the remainder is used for the transmit buffer.
	 */
	switch (sc->hw.mac.type) {
	/* Total Packet Buffer on these is 48K */
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		pba = E1000_PBA_32K; /* 32K for Rx, 16K for Tx */
		break;

	case e1000_82573: /* 82573: Total Packet Buffer is 32K */
		pba = E1000_PBA_12K; /* 12K for Rx, 20K for Tx */
		break;

	case e1000_82574:
		pba = E1000_PBA_20K; /* 20K for Rx, 20K for Tx */
		break;

	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
 		pba = E1000_PBA_26K;
 		break;

	default:
		/* Devices before 82547 had a Packet Buffer of 64K.   */
		if (sc->hw.mac.max_frame_size > 8192)
			pba = E1000_PBA_40K; /* 40K for Rx, 24K for Tx */
		else
			pba = E1000_PBA_48K; /* 48K for Rx, 16K for Tx */
	}
	E1000_WRITE_REG(&sc->hw, E1000_PBA, pba);

	/*
	 * These parameters control the automatic generation (Tx) and
	 * response (Rx) to Ethernet PAUSE frames.
	 * - High water mark should allow for at least two frames to be
	 *   received after sending an XOFF.
	 * - Low water mark works best when it is very near the high water mark.
	 *   This allows the receiver to restart by sending XON when it has
	 *   drained a bit. Here we use an arbitary value of 1500 which will
	 *   restart after one full frame is pulled from the buffer. There
	 *   could be several smaller frames in the buffer and if so they will
	 *   not trigger the XON until their total number reduces the buffer
	 *   by 1500.
	 * - The pause time is fairly large at 1000 x 512ns = 512 usec.
	 */
	rx_buffer_size = (E1000_READ_REG(&sc->hw, E1000_PBA) & 0xffff) << 10;

	sc->hw.fc.high_water = rx_buffer_size -
	    roundup2(sc->hw.mac.max_frame_size, 1024);
	sc->hw.fc.low_water = sc->hw.fc.high_water - 1500;

	sc->hw.fc.pause_time = EMX_FC_PAUSE_TIME;
	sc->hw.fc.send_xon = TRUE;
	sc->hw.fc.requested_mode = e1000_ifmedia2fc(sc->ifm_flowctrl);

	/*
	 * Device specific overrides/settings
	 */
	if (sc->hw.mac.type == e1000_pch_lpt ||
	    sc->hw.mac.type == e1000_pch_spt ||
	    sc->hw.mac.type == e1000_pch_cnp) {
		sc->hw.fc.high_water = 0x5C20;
		sc->hw.fc.low_water = 0x5048;
		sc->hw.fc.pause_time = 0x0650;
		sc->hw.fc.refresh_time = 0x0400;
		/* Jumbos need adjusted PBA */
		if (sc->arpcom.ac_if.if_mtu > ETHERMTU)
			E1000_WRITE_REG(&sc->hw, E1000_PBA, 12);
		else
			E1000_WRITE_REG(&sc->hw, E1000_PBA, 26);
	} else if (sc->hw.mac.type == e1000_80003es2lan) {
		sc->hw.fc.pause_time = 0xFFFF;
	}

	/* I219 needs some special flushing to avoid hangs */
	if (sc->hw.mac.type >= e1000_pch_spt)
		emx_flush_txrx_ring(sc);

	/* Issue a global reset */
	e1000_reset_hw(&sc->hw);
	E1000_WRITE_REG(&sc->hw, E1000_WUC, 0);
	emx_disable_aspm(sc);

	if (e1000_init_hw(&sc->hw) < 0) {
		device_printf(dev, "Hardware Initialization Failed\n");
		return (EIO);
	}

	E1000_WRITE_REG(&sc->hw, E1000_VET, ETHERTYPE_VLAN);
	e1000_get_phy_info(&sc->hw);
	e1000_check_for_link(&sc->hw);

	return (0);
}

static void
emx_setup_ifp(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	if_initname(ifp, device_get_name(sc->dev),
		    device_get_unit(sc->dev));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init =  emx_init;
	ifp->if_ioctl = emx_ioctl;
	ifp->if_start = emx_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = emx_npoll;
#endif
	ifp->if_serialize = emx_serialize;
	ifp->if_deserialize = emx_deserialize;
	ifp->if_tryserialize = emx_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = emx_serialize_assert;
#endif

	ifp->if_nmbclusters = sc->rx_ring_cnt * sc->rx_data[0].num_rx_desc;

	ifq_set_maxlen(&ifp->if_snd, sc->tx_data[0].num_tx_desc - 1);
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->tx_ring_cnt);

	ifp->if_mapsubq = ifq_mapsubq_modulo;
	ifq_set_subq_divisor(&ifp->if_snd, 1);

	ether_ifattach(ifp, sc->hw.mac.addr, NULL);

	ifp->if_capabilities = IFCAP_HWCSUM |
			       IFCAP_VLAN_HWTAGGING |
			       IFCAP_VLAN_MTU |
			       IFCAP_TSO;
	if (sc->rx_ring_cnt > 1)
		ifp->if_capabilities |= IFCAP_RSS;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_hwassist = EMX_CSUM_FEATURES | CSUM_TSO;

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct emx_txdata *tdata = &sc->tx_data[i];

		ifsq_set_cpuid(ifsq, rman_get_cpuid(sc->intr_res));
		ifsq_set_priv(ifsq, tdata);
		ifsq_set_hw_serialize(ifsq, &tdata->tx_serialize);
		tdata->ifsq = ifsq;

		ifsq_watchdog_init(&tdata->tx_watchdog, ifsq, emx_watchdog, 0);
	}

	/*
	 * Specify the media types supported by this sc and register
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

/*
 * Workaround for SmartSpeed on 82541 and 82547 controllers
 */
static void
emx_smartspeed(struct emx_softc *sc)
{
	uint16_t phy_tmp;

	if (sc->link_active || sc->hw.phy.type != e1000_phy_igp ||
	    sc->hw.mac.autoneg == 0 ||
	    (sc->hw.phy.autoneg_advertised & ADVERTISE_1000_FULL) == 0)
		return;

	if (sc->smartspeed == 0) {
		/*
		 * If Master/Slave config fault is asserted twice,
		 * we assume back-to-back
		 */
		e1000_read_phy_reg(&sc->hw, PHY_1000T_STATUS, &phy_tmp);
		if (!(phy_tmp & SR_1000T_MS_CONFIG_FAULT))
			return;
		e1000_read_phy_reg(&sc->hw, PHY_1000T_STATUS, &phy_tmp);
		if (phy_tmp & SR_1000T_MS_CONFIG_FAULT) {
			e1000_read_phy_reg(&sc->hw,
			    PHY_1000T_CTRL, &phy_tmp);
			if (phy_tmp & CR_1000T_MS_ENABLE) {
				phy_tmp &= ~CR_1000T_MS_ENABLE;
				e1000_write_phy_reg(&sc->hw,
				    PHY_1000T_CTRL, phy_tmp);
				sc->smartspeed++;
				if (sc->hw.mac.autoneg &&
				    !e1000_phy_setup_autoneg(&sc->hw) &&
				    !e1000_read_phy_reg(&sc->hw,
				     PHY_CONTROL, &phy_tmp)) {
					phy_tmp |= MII_CR_AUTO_NEG_EN |
						   MII_CR_RESTART_AUTO_NEG;
					e1000_write_phy_reg(&sc->hw,
					    PHY_CONTROL, phy_tmp);
				}
			}
		}
		return;
	} else if (sc->smartspeed == EMX_SMARTSPEED_DOWNSHIFT) {
		/* If still no link, perhaps using 2/3 pair cable */
		e1000_read_phy_reg(&sc->hw, PHY_1000T_CTRL, &phy_tmp);
		phy_tmp |= CR_1000T_MS_ENABLE;
		e1000_write_phy_reg(&sc->hw, PHY_1000T_CTRL, phy_tmp);
		if (sc->hw.mac.autoneg &&
		    !e1000_phy_setup_autoneg(&sc->hw) &&
		    !e1000_read_phy_reg(&sc->hw, PHY_CONTROL, &phy_tmp)) {
			phy_tmp |= MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG;
			e1000_write_phy_reg(&sc->hw, PHY_CONTROL, phy_tmp);
		}
	}

	/* Restart process after EMX_SMARTSPEED_MAX iterations */
	if (sc->smartspeed++ == EMX_SMARTSPEED_MAX)
		sc->smartspeed = 0;
}

static int
emx_create_tx_ring(struct emx_txdata *tdata)
{
	device_t dev = tdata->sc->dev;
	struct emx_txbuf *tx_buffer;
	int error, i, tsize, ntxd;

	/*
	 * Validate number of transmit descriptors.  It must not exceed
	 * hardware maximum, and must be multiple of E1000_DBA_ALIGN.
	 */
	ntxd = device_getenv_int(dev, "txd", emx_txd);
	if ((ntxd * sizeof(struct e1000_tx_desc)) % EMX_DBA_ALIGN != 0 ||
	    ntxd > EMX_MAX_TXD || ntxd < EMX_MIN_TXD) {
		device_printf(dev, "Using %d TX descriptors instead of %d!\n",
		    EMX_DEFAULT_TXD, ntxd);
		tdata->num_tx_desc = EMX_DEFAULT_TXD;
	} else {
		tdata->num_tx_desc = ntxd;
	}

	/*
	 * Allocate Transmit Descriptor ring
	 */
	tsize = roundup2(tdata->num_tx_desc * sizeof(struct e1000_tx_desc),
			 EMX_DBA_ALIGN);
	tdata->tx_desc_base = bus_dmamem_coherent_any(tdata->sc->parent_dtag,
				EMX_DBA_ALIGN, tsize, BUS_DMA_WAITOK,
				&tdata->tx_desc_dtag, &tdata->tx_desc_dmap,
				&tdata->tx_desc_paddr);
	if (tdata->tx_desc_base == NULL) {
		device_printf(dev, "Unable to allocate tx_desc memory\n");
		return ENOMEM;
	}

	tsize = __VM_CACHELINE_ALIGN(
	    sizeof(struct emx_txbuf) * tdata->num_tx_desc);
	tdata->tx_buf = kmalloc(tsize, M_DEVBUF,
				M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create DMA tags for tx buffers
	 */
	error = bus_dma_tag_create(tdata->sc->parent_dtag, /* parent */
			1, 0,			/* alignment, bounds */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			EMX_TSO_SIZE,		/* maxsize */
			EMX_MAX_SCATTER,	/* nsegments */
			EMX_MAX_SEGSIZE,	/* maxsegsize */
			BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
			BUS_DMA_ONEBPAGE,	/* flags */
			&tdata->txtag);
	if (error) {
		device_printf(dev, "Unable to allocate TX DMA tag\n");
		kfree(tdata->tx_buf, M_DEVBUF);
		tdata->tx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for tx buffers
	 */
	for (i = 0; i < tdata->num_tx_desc; i++) {
		tx_buffer = &tdata->tx_buf[i];

		error = bus_dmamap_create(tdata->txtag,
					  BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
					  &tx_buffer->map);
		if (error) {
			device_printf(dev, "Unable to create TX DMA map\n");
			emx_destroy_tx_ring(tdata, i);
			return error;
		}
	}

	/*
	 * Setup TX parameters
	 */
	tdata->spare_tx_desc = EMX_TX_SPARE;
	tdata->tx_wreg_nsegs = EMX_DEFAULT_TXWREG;

	/*
	 * Keep following relationship between spare_tx_desc, oact_tx_desc
	 * and tx_intr_nsegs:
	 * (spare_tx_desc + EMX_TX_RESERVED) <=
	 * oact_tx_desc <= EMX_TX_OACTIVE_MAX <= tx_intr_nsegs
	 */
	tdata->oact_tx_desc = tdata->num_tx_desc / 8;
	if (tdata->oact_tx_desc > EMX_TX_OACTIVE_MAX)
		tdata->oact_tx_desc = EMX_TX_OACTIVE_MAX;
	if (tdata->oact_tx_desc < tdata->spare_tx_desc + EMX_TX_RESERVED)
		tdata->oact_tx_desc = tdata->spare_tx_desc + EMX_TX_RESERVED;

	tdata->tx_intr_nsegs = tdata->num_tx_desc / 16;
	if (tdata->tx_intr_nsegs < tdata->oact_tx_desc)
		tdata->tx_intr_nsegs = tdata->oact_tx_desc;

	/*
	 * Pullup extra 4bytes into the first data segment for TSO, see:
	 * 82571/82572 specification update errata #7
	 *
	 * Same applies to I217 (and maybe I218 and I219).
	 *
	 * NOTE:
	 * 4bytes instead of 2bytes, which are mentioned in the errata,
	 * are pulled; mainly to keep rest of the data properly aligned.
	 */
	if (tdata->sc->hw.mac.type == e1000_82571 ||
	    tdata->sc->hw.mac.type == e1000_82572 ||
	    tdata->sc->hw.mac.type == e1000_pch_lpt ||
	    tdata->sc->hw.mac.type == e1000_pch_spt ||
	    tdata->sc->hw.mac.type == e1000_pch_cnp)
		tdata->tx_flags |= EMX_TXFLAG_TSO_PULLEX;

	return (0);
}

static void
emx_init_tx_ring(struct emx_txdata *tdata)
{
	/* Clear the old ring contents */
	bzero(tdata->tx_desc_base,
	      sizeof(struct e1000_tx_desc) * tdata->num_tx_desc);

	/* Reset state */
	tdata->next_avail_tx_desc = 0;
	tdata->next_tx_to_clean = 0;
	tdata->num_tx_desc_avail = tdata->num_tx_desc;
	tdata->tx_nmbuf = 0;
	tdata->tx_running = 0;

	tdata->tx_flags |= EMX_TXFLAG_ENABLED;
	if (tdata->sc->tx_ring_inuse > 1) {
		tdata->tx_flags |= EMX_TXFLAG_FORCECTX;
		if (bootverbose) {
			if_printf(&tdata->sc->arpcom.ac_if,
			    "TX %d force ctx setup\n", tdata->idx);
		}
	}
}

static void
emx_init_tx_unit(struct emx_softc *sc)
{
	uint32_t tctl, tarc, tipg = 0, txdctl;
	int i;

	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct emx_txdata *tdata = &sc->tx_data[i];
		uint64_t bus_addr;

		/* Setup the Base and Length of the Tx Descriptor Ring */
		bus_addr = tdata->tx_desc_paddr;
		E1000_WRITE_REG(&sc->hw, E1000_TDLEN(i),
		    tdata->num_tx_desc * sizeof(struct e1000_tx_desc));
		E1000_WRITE_REG(&sc->hw, E1000_TDBAH(i),
		    (uint32_t)(bus_addr >> 32));
		E1000_WRITE_REG(&sc->hw, E1000_TDBAL(i),
		    (uint32_t)bus_addr);
		/* Setup the HW Tx Head and Tail descriptor pointers */
		E1000_WRITE_REG(&sc->hw, E1000_TDT(i), 0);
		E1000_WRITE_REG(&sc->hw, E1000_TDH(i), 0);

		txdctl = 0x1f;		/* PTHRESH */
		txdctl |= 1 << 8;	/* HTHRESH */
		txdctl |= 1 << 16;	/* WTHRESH */
		txdctl |= 1 << 22;	/* Reserved bit 22 must always be 1 */
		txdctl |= E1000_TXDCTL_GRAN;
		txdctl |= 1 << 25;	/* LWTHRESH */

		E1000_WRITE_REG(&sc->hw, E1000_TXDCTL(i), txdctl);
	}

	/* Set the default values for the Tx Inter Packet Gap timer */
	switch (sc->hw.mac.type) {
	case e1000_80003es2lan:
		tipg = DEFAULT_82543_TIPG_IPGR1;
		tipg |= DEFAULT_80003ES2LAN_TIPG_IPGR2 <<
		    E1000_TIPG_IPGR2_SHIFT;
		break;

	default:
		if (sc->hw.phy.media_type == e1000_media_type_fiber ||
		    sc->hw.phy.media_type == e1000_media_type_internal_serdes)
			tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
		else
			tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
		tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		break;
	}

	E1000_WRITE_REG(&sc->hw, E1000_TIPG, tipg);

	/* NOTE: 0 is not allowed for TIDV */
	E1000_WRITE_REG(&sc->hw, E1000_TIDV, 1);
	E1000_WRITE_REG(&sc->hw, E1000_TADV, 0);

	/*
	 * Errata workaround (obtained from Linux).  This is necessary
	 * to make multiple TX queues work on 82574.
	 * XXX can't find it in any published errata though.
	 */
	txdctl = E1000_READ_REG(&sc->hw, E1000_TXDCTL(0));
	E1000_WRITE_REG(&sc->hw, E1000_TXDCTL(1), txdctl);

	if (sc->hw.mac.type == e1000_82571 ||
	    sc->hw.mac.type == e1000_82572) {
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(0));
		tarc |= EMX_TARC_SPEED_MODE;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(0), tarc);
	} else if (sc->hw.mac.type == e1000_80003es2lan) {
		/* errata: program both queues to unweighted RR */
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(0));
		tarc |= 1;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(0), tarc);
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(1));
		tarc |= 1;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(1), tarc);
	} else if (sc->hw.mac.type == e1000_82574) {
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(0));
		tarc |= EMX_TARC_ERRATA;
		if (sc->tx_ring_inuse > 1) {
			tarc |= (EMX_TARC_COMPENSATION_MODE | EMX_TARC_MQ_FIX);
			E1000_WRITE_REG(&sc->hw, E1000_TARC(0), tarc);
			E1000_WRITE_REG(&sc->hw, E1000_TARC(1), tarc);
		} else {
			E1000_WRITE_REG(&sc->hw, E1000_TARC(0), tarc);
		}
	}

	/* Program the Transmit Control Register */
	tctl = E1000_READ_REG(&sc->hw, E1000_TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC | E1000_TCTL_EN |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);
	tctl |= E1000_TCTL_MULR;

	/* This write will effectively turn on the transmit unit. */
	E1000_WRITE_REG(&sc->hw, E1000_TCTL, tctl);

	if (sc->hw.mac.type == e1000_82571 ||
	    sc->hw.mac.type == e1000_82572 ||
	    sc->hw.mac.type == e1000_80003es2lan) {
		/* Bit 28 of TARC1 must be cleared when MULR is enabled */
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(1));
		tarc &= ~(1 << 28);
		E1000_WRITE_REG(&sc->hw, E1000_TARC(1), tarc);
	} else if (sc->hw.mac.type >= e1000_pch_spt) {
		uint32_t reg;

		reg = E1000_READ_REG(&sc->hw, E1000_IOSFPC);
		reg |= E1000_RCTL_RDMTS_HEX;
		E1000_WRITE_REG(&sc->hw, E1000_IOSFPC, reg);
		reg = E1000_READ_REG(&sc->hw, E1000_TARC(0));
		reg |= E1000_TARC0_CB_MULTIQ_3_REQ;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(0), reg);
	}

	if (sc->tx_ring_inuse > 1) {
		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(0));
		tarc &= ~EMX_TARC_COUNT_MASK;
		tarc |= 1;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(0), tarc);

		tarc = E1000_READ_REG(&sc->hw, E1000_TARC(1));
		tarc &= ~EMX_TARC_COUNT_MASK;
		tarc |= 1;
		E1000_WRITE_REG(&sc->hw, E1000_TARC(1), tarc);
	}
}

static void
emx_destroy_tx_ring(struct emx_txdata *tdata, int ndesc)
{
	struct emx_txbuf *tx_buffer;
	int i;

	/* Free Transmit Descriptor ring */
	if (tdata->tx_desc_base) {
		bus_dmamap_unload(tdata->tx_desc_dtag, tdata->tx_desc_dmap);
		bus_dmamem_free(tdata->tx_desc_dtag, tdata->tx_desc_base,
				tdata->tx_desc_dmap);
		bus_dma_tag_destroy(tdata->tx_desc_dtag);

		tdata->tx_desc_base = NULL;
	}

	if (tdata->tx_buf == NULL)
		return;

	for (i = 0; i < ndesc; i++) {
		tx_buffer = &tdata->tx_buf[i];

		KKASSERT(tx_buffer->m_head == NULL);
		bus_dmamap_destroy(tdata->txtag, tx_buffer->map);
	}
	bus_dma_tag_destroy(tdata->txtag);

	kfree(tdata->tx_buf, M_DEVBUF);
	tdata->tx_buf = NULL;
}

/*
 * The offload context needs to be set when we transfer the first
 * packet of a particular protocol (TCP/UDP).  This routine has been
 * enhanced to deal with inserted VLAN headers.
 *
 * If the new packet's ether header length, ip header length and
 * csum offloading type are same as the previous packet, we should
 * avoid allocating a new csum context descriptor; mainly to take
 * advantage of the pipeline effect of the TX data read request.
 *
 * This function returns number of TX descrptors allocated for
 * csum context.
 */
static int
emx_txcsum(struct emx_txdata *tdata, struct mbuf *mp,
	   uint32_t *txd_upper, uint32_t *txd_lower)
{
	struct e1000_context_desc *TXD;
	int curr_txd, ehdrlen, csum_flags;
	uint32_t cmd, hdr_len, ip_hlen;

	csum_flags = mp->m_pkthdr.csum_flags & EMX_CSUM_FEATURES;
	ip_hlen = mp->m_pkthdr.csum_iphlen;
	ehdrlen = mp->m_pkthdr.csum_lhlen;

	if ((tdata->tx_flags & EMX_TXFLAG_FORCECTX) == 0 &&
	    tdata->csum_lhlen == ehdrlen && tdata->csum_iphlen == ip_hlen &&
	    tdata->csum_flags == csum_flags) {
		/*
		 * Same csum offload context as the previous packets;
		 * just return.
		 */
		*txd_upper = tdata->csum_txd_upper;
		*txd_lower = tdata->csum_txd_lower;
		return 0;
	}

	/*
	 * Setup a new csum offload context.
	 */

	curr_txd = tdata->next_avail_tx_desc;
	TXD = (struct e1000_context_desc *)&tdata->tx_desc_base[curr_txd];

	cmd = 0;

	/* Setup of IP header checksum. */
	if (csum_flags & CSUM_IP) {
		/*
		 * Start offset for header checksum calculation.
		 * End offset for header checksum calculation.
		 * Offset of place to put the checksum.
		 */
		TXD->lower_setup.ip_fields.ipcss = ehdrlen;
		TXD->lower_setup.ip_fields.ipcse =
		    htole16(ehdrlen + ip_hlen - 1);
		TXD->lower_setup.ip_fields.ipcso =
		    ehdrlen + offsetof(struct ip, ip_sum);
		cmd |= E1000_TXD_CMD_IP;
		*txd_upper |= E1000_TXD_POPTS_IXSM << 8;
	}
	hdr_len = ehdrlen + ip_hlen;

	if (csum_flags & CSUM_TCP) {
		/*
		 * Start offset for payload checksum calculation.
		 * End offset for payload checksum calculation.
		 * Offset of place to put the checksum.
		 */
		TXD->upper_setup.tcp_fields.tucss = hdr_len;
		TXD->upper_setup.tcp_fields.tucse = htole16(0);
		TXD->upper_setup.tcp_fields.tucso =
		    hdr_len + offsetof(struct tcphdr, th_sum);
		cmd |= E1000_TXD_CMD_TCP;
		*txd_upper |= E1000_TXD_POPTS_TXSM << 8;
	} else if (csum_flags & CSUM_UDP) {
		/*
		 * Start offset for header checksum calculation.
		 * End offset for header checksum calculation.
		 * Offset of place to put the checksum.
		 */
		TXD->upper_setup.tcp_fields.tucss = hdr_len;
		TXD->upper_setup.tcp_fields.tucse = htole16(0);
		TXD->upper_setup.tcp_fields.tucso =
		    hdr_len + offsetof(struct udphdr, uh_sum);
		*txd_upper |= E1000_TXD_POPTS_TXSM << 8;
	}

	*txd_lower = E1000_TXD_CMD_DEXT |	/* Extended descr type */
		     E1000_TXD_DTYP_D;		/* Data descr */

	/* Save the information for this csum offloading context */
	tdata->csum_lhlen = ehdrlen;
	tdata->csum_iphlen = ip_hlen;
	tdata->csum_flags = csum_flags;
	tdata->csum_txd_upper = *txd_upper;
	tdata->csum_txd_lower = *txd_lower;

	TXD->tcp_seg_setup.data = htole32(0);
	TXD->cmd_and_length =
	    htole32(E1000_TXD_CMD_IFCS | E1000_TXD_CMD_DEXT | cmd);

	if (++curr_txd == tdata->num_tx_desc)
		curr_txd = 0;

	KKASSERT(tdata->num_tx_desc_avail > 0);
	tdata->num_tx_desc_avail--;

	tdata->next_avail_tx_desc = curr_txd;
	return 1;
}

static void
emx_txeof(struct emx_txdata *tdata)
{
	struct emx_txbuf *tx_buffer;
	int first, num_avail;

	if (tdata->tx_dd_head == tdata->tx_dd_tail)
		return;

	if (tdata->num_tx_desc_avail == tdata->num_tx_desc)
		return;

	num_avail = tdata->num_tx_desc_avail;
	first = tdata->next_tx_to_clean;

	while (tdata->tx_dd_head != tdata->tx_dd_tail) {
		int dd_idx = tdata->tx_dd[tdata->tx_dd_head];
		struct e1000_tx_desc *tx_desc;

		tx_desc = &tdata->tx_desc_base[dd_idx];
		if (tx_desc->upper.fields.status & E1000_TXD_STAT_DD) {
			EMX_INC_TXDD_IDX(tdata->tx_dd_head);

			if (++dd_idx == tdata->num_tx_desc)
				dd_idx = 0;

			while (first != dd_idx) {
				logif(pkt_txclean);

				KKASSERT(num_avail < tdata->num_tx_desc);
				num_avail++;

				tx_buffer = &tdata->tx_buf[first];
				if (tx_buffer->m_head)
					emx_free_txbuf(tdata, tx_buffer);

				if (++first == tdata->num_tx_desc)
					first = 0;
			}
		} else {
			break;
		}
	}
	tdata->next_tx_to_clean = first;
	tdata->num_tx_desc_avail = num_avail;

	if (tdata->tx_dd_head == tdata->tx_dd_tail) {
		tdata->tx_dd_head = 0;
		tdata->tx_dd_tail = 0;
	}

	if (!EMX_IS_OACTIVE(tdata)) {
		ifsq_clr_oactive(tdata->ifsq);

		/* All clean, turn off the timer */
		if (tdata->num_tx_desc_avail == tdata->num_tx_desc)
			ifsq_watchdog_set_count(&tdata->tx_watchdog, 0);
	}
	tdata->tx_running = EMX_TX_RUNNING;
}

static void
emx_tx_collect(struct emx_txdata *tdata, boolean_t gc)
{
	struct emx_txbuf *tx_buffer;
	int tdh, first, num_avail, dd_idx = -1;

	if (tdata->num_tx_desc_avail == tdata->num_tx_desc)
		return;

	tdh = E1000_READ_REG(&tdata->sc->hw, E1000_TDH(tdata->idx));
	if (tdh == tdata->next_tx_to_clean) {
		if (gc && tdata->tx_nmbuf > 0)
			tdata->tx_running = EMX_TX_RUNNING;
		return;
	}
	if (gc)
		tdata->tx_gc++;

	if (tdata->tx_dd_head != tdata->tx_dd_tail)
		dd_idx = tdata->tx_dd[tdata->tx_dd_head];

	num_avail = tdata->num_tx_desc_avail;
	first = tdata->next_tx_to_clean;

	while (first != tdh) {
		logif(pkt_txclean);

		KKASSERT(num_avail < tdata->num_tx_desc);
		num_avail++;

		tx_buffer = &tdata->tx_buf[first];
		if (tx_buffer->m_head)
			emx_free_txbuf(tdata, tx_buffer);

		if (first == dd_idx) {
			EMX_INC_TXDD_IDX(tdata->tx_dd_head);
			if (tdata->tx_dd_head == tdata->tx_dd_tail) {
				tdata->tx_dd_head = 0;
				tdata->tx_dd_tail = 0;
				dd_idx = -1;
			} else {
				dd_idx = tdata->tx_dd[tdata->tx_dd_head];
			}
		}

		if (++first == tdata->num_tx_desc)
			first = 0;
	}
	tdata->next_tx_to_clean = first;
	tdata->num_tx_desc_avail = num_avail;

	if (!EMX_IS_OACTIVE(tdata)) {
		ifsq_clr_oactive(tdata->ifsq);

		/* All clean, turn off the timer */
		if (tdata->num_tx_desc_avail == tdata->num_tx_desc)
			ifsq_watchdog_set_count(&tdata->tx_watchdog, 0);
	}
	if (!gc || tdata->tx_nmbuf > 0)
		tdata->tx_running = EMX_TX_RUNNING;
}

/*
 * When Link is lost sometimes there is work still in the TX ring
 * which will result in a watchdog, rather than allow that do an
 * attempted cleanup and then reinit here.  Note that this has been
 * seens mostly with fiber adapters.
 */
static void
emx_tx_purge(struct emx_softc *sc)
{
	int i;

	if (sc->link_active)
		return;

	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct emx_txdata *tdata = &sc->tx_data[i];

		if (tdata->tx_watchdog.wd_timer) {
			emx_tx_collect(tdata, FALSE);
			if (tdata->tx_watchdog.wd_timer) {
				if_printf(&sc->arpcom.ac_if,
				    "Link lost, TX pending, reinit\n");
				emx_init(sc);
				return;
			}
		}
	}
}

static int
emx_newbuf(struct emx_rxdata *rdata, int i, int init)
{
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct emx_rxbuf *rx_buffer;
	int error, nseg;

	m = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		if (init) {
			if_printf(&rdata->sc->arpcom.ac_if,
				  "Unable to allocate RX mbuf\n");
		}
		return (ENOBUFS);
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (rdata->sc->hw.mac.max_frame_size <= MCLBYTES - ETHER_ALIGN)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(rdata->rxtag,
			rdata->rx_sparemap, m,
			&seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init) {
			if_printf(&rdata->sc->arpcom.ac_if,
				  "Unable to load RX mbuf\n");
		}
		return (error);
	}

	rx_buffer = &rdata->rx_buf[i];
	if (rx_buffer->m_head != NULL)
		bus_dmamap_unload(rdata->rxtag, rx_buffer->map);

	map = rx_buffer->map;
	rx_buffer->map = rdata->rx_sparemap;
	rdata->rx_sparemap = map;

	rx_buffer->m_head = m;
	rx_buffer->paddr = seg.ds_addr;

	emx_setup_rxdesc(&rdata->rx_desc[i], rx_buffer);
	return (0);
}

static int
emx_create_rx_ring(struct emx_rxdata *rdata)
{
	device_t dev = rdata->sc->dev;
	struct emx_rxbuf *rx_buffer;
	int i, error, rsize, nrxd;

	/*
	 * Validate number of receive descriptors.  It must not exceed
	 * hardware maximum, and must be multiple of E1000_DBA_ALIGN.
	 */
	nrxd = device_getenv_int(dev, "rxd", emx_rxd);
	if ((nrxd * sizeof(emx_rxdesc_t)) % EMX_DBA_ALIGN != 0 ||
	    nrxd > EMX_MAX_RXD || nrxd < EMX_MIN_RXD) {
		device_printf(dev, "Using %d RX descriptors instead of %d!\n",
		    EMX_DEFAULT_RXD, nrxd);
		rdata->num_rx_desc = EMX_DEFAULT_RXD;
	} else {
		rdata->num_rx_desc = nrxd;
	}

	/*
	 * Allocate Receive Descriptor ring
	 */
	rsize = roundup2(rdata->num_rx_desc * sizeof(emx_rxdesc_t),
			 EMX_DBA_ALIGN);
	rdata->rx_desc = bus_dmamem_coherent_any(rdata->sc->parent_dtag,
				EMX_DBA_ALIGN, rsize, BUS_DMA_WAITOK,
				&rdata->rx_desc_dtag, &rdata->rx_desc_dmap,
				&rdata->rx_desc_paddr);
	if (rdata->rx_desc == NULL) {
		device_printf(dev, "Unable to allocate rx_desc memory\n");
		return ENOMEM;
	}

	rsize = __VM_CACHELINE_ALIGN(
	    sizeof(struct emx_rxbuf) * rdata->num_rx_desc);
	rdata->rx_buf = kmalloc(rsize, M_DEVBUF,
				M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create DMA tag for rx buffers
	 */
	error = bus_dma_tag_create(rdata->sc->parent_dtag, /* parent */
			1, 0,			/* alignment, bounds */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MCLBYTES,		/* maxsize */
			1,			/* nsegments */
			MCLBYTES,		/* maxsegsize */
			BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, /* flags */
			&rdata->rxtag);
	if (error) {
		device_printf(dev, "Unable to allocate RX DMA tag\n");
		kfree(rdata->rx_buf, M_DEVBUF);
		rdata->rx_buf = NULL;
		return error;
	}

	/*
	 * Create spare DMA map for rx buffers
	 */
	error = bus_dmamap_create(rdata->rxtag, BUS_DMA_WAITOK,
				  &rdata->rx_sparemap);
	if (error) {
		device_printf(dev, "Unable to create spare RX DMA map\n");
		bus_dma_tag_destroy(rdata->rxtag);
		kfree(rdata->rx_buf, M_DEVBUF);
		rdata->rx_buf = NULL;
		return error;
	}

	/*
	 * Create DMA maps for rx buffers
	 */
	for (i = 0; i < rdata->num_rx_desc; i++) {
		rx_buffer = &rdata->rx_buf[i];

		error = bus_dmamap_create(rdata->rxtag, BUS_DMA_WAITOK,
					  &rx_buffer->map);
		if (error) {
			device_printf(dev, "Unable to create RX DMA map\n");
			emx_destroy_rx_ring(rdata, i);
			return error;
		}
	}
	return (0);
}

static void
emx_free_rx_ring(struct emx_rxdata *rdata)
{
	int i;

	for (i = 0; i < rdata->num_rx_desc; i++) {
		struct emx_rxbuf *rx_buffer = &rdata->rx_buf[i];

		if (rx_buffer->m_head != NULL) {
			bus_dmamap_unload(rdata->rxtag, rx_buffer->map);
			m_freem(rx_buffer->m_head);
			rx_buffer->m_head = NULL;
		}
	}

	if (rdata->fmp != NULL)
		m_freem(rdata->fmp);
	rdata->fmp = NULL;
	rdata->lmp = NULL;
}

static void
emx_free_tx_ring(struct emx_txdata *tdata)
{
	int i;

	for (i = 0; i < tdata->num_tx_desc; i++) {
		struct emx_txbuf *tx_buffer = &tdata->tx_buf[i];

		if (tx_buffer->m_head != NULL)
			emx_free_txbuf(tdata, tx_buffer);
	}

	tdata->tx_flags &= ~EMX_TXFLAG_FORCECTX;

	tdata->csum_flags = 0;
	tdata->csum_lhlen = 0;
	tdata->csum_iphlen = 0;
	tdata->csum_thlen = 0;
	tdata->csum_mss = 0;
	tdata->csum_pktlen = 0;

	tdata->tx_dd_head = 0;
	tdata->tx_dd_tail = 0;
	tdata->tx_nsegs = 0;
}

static int
emx_init_rx_ring(struct emx_rxdata *rdata)
{
	int i, error;

	/* Reset descriptor ring */
	bzero(rdata->rx_desc, sizeof(emx_rxdesc_t) * rdata->num_rx_desc);

	/* Allocate new ones. */
	for (i = 0; i < rdata->num_rx_desc; i++) {
		error = emx_newbuf(rdata, i, 1);
		if (error)
			return (error);
	}

	/* Setup our descriptor pointers */
	rdata->next_rx_desc_to_check = 0;

	return (0);
}

static void
emx_init_rx_unit(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint64_t bus_addr;
	uint32_t rctl, itr, rfctl, rxcsum;
	int i;

	/*
	 * Make sure receives are disabled while setting
	 * up the descriptor ring
	 */
	rctl = E1000_READ_REG(&sc->hw, E1000_RCTL);
	/* Do not disable if ever enabled on this hardware */
	if (sc->hw.mac.type != e1000_82574)
		E1000_WRITE_REG(&sc->hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);

	/*
	 * Set the interrupt throttling rate. Value is calculated
	 * as ITR = 1 / (INT_THROTTLE_CEIL * 256ns)
	 */
	if (sc->int_throttle_ceil)
		itr = 1000000000 / 256 / sc->int_throttle_ceil;
	else
		itr = 0;
	emx_set_itr(sc, itr);

	/* Use extended RX descriptor */
	rfctl = E1000_READ_REG(&sc->hw, E1000_RFCTL);
	rfctl |= E1000_RFCTL_EXTEN;
	/* Disable accelerated ackknowledge */
	if (sc->hw.mac.type == e1000_82574)
		rfctl |= E1000_RFCTL_ACK_DIS;
	E1000_WRITE_REG(&sc->hw, E1000_RFCTL, rfctl);

	/*
	 * Receive Checksum Offload for TCP and UDP
	 *
	 * Checksum offloading is also enabled if multiple receive
	 * queue is to be supported, since we need it to figure out
	 * packet type.
	 */
	rxcsum = E1000_READ_REG(&sc->hw, E1000_RXCSUM);
	if ((ifp->if_capenable & IFCAP_RXCSUM) ||
	    sc->rx_ring_cnt > 1) {
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

	/*
	 * Configure multiple receive queue (RSS)
	 */
	if (sc->rx_ring_cnt > 1) {
		uint8_t key[EMX_NRSSRK * EMX_RSSRK_SIZE];
		int r, j;

		KASSERT(sc->rx_ring_cnt == EMX_NRX_RING,
		    ("invalid number of RX ring (%d)", sc->rx_ring_cnt));

		/*
		 * NOTE:
		 * When we reach here, RSS has already been disabled
		 * in emx_stop(), so we could safely configure RSS key
		 * and redirect table.
		 */

		/*
		 * Configure RSS key
		 */
		toeplitz_get_key(key, sizeof(key));
		for (i = 0; i < EMX_NRSSRK; ++i) {
			uint32_t rssrk;

			rssrk = EMX_RSSRK_VAL(key, i);
			EMX_RSS_DPRINTF(sc, 1, "rssrk%d 0x%08x\n", i, rssrk);

			E1000_WRITE_REG(&sc->hw, E1000_RSSRK(i), rssrk);
		}

		/*
		 * Configure RSS redirect table.
		 */
		if_ringmap_rdrtable(sc->rx_rmap, sc->rdr_table,
		    EMX_RDRTABLE_SIZE);

		r = 0;
		for (j = 0; j < EMX_NRETA; ++j) {
			uint32_t reta = 0;

			for (i = 0; i < EMX_RETA_SIZE; ++i) {
				uint32_t q;

				q = sc->rdr_table[r] << EMX_RETA_RINGIDX_SHIFT;
				reta |= q << (8 * i);
				++r;
			}
			EMX_RSS_DPRINTF(sc, 1, "reta 0x%08x\n", reta);
			E1000_WRITE_REG(&sc->hw, E1000_RETA(j), reta);
		}

		/*
		 * Enable multiple receive queues.
		 * Enable IPv4 RSS standard hash functions.
		 * Disable RSS interrupt.
		 */
		E1000_WRITE_REG(&sc->hw, E1000_MRQC,
				E1000_MRQC_ENABLE_RSS_2Q |
				E1000_MRQC_RSS_FIELD_IPV4_TCP |
				E1000_MRQC_RSS_FIELD_IPV4);
	}

	/*
	 * XXX TEMPORARY WORKAROUND: on some systems with 82573
	 * long latencies are observed, like Lenovo X60. This
	 * change eliminates the problem, but since having positive
	 * values in RDTR is a known source of problems on other
	 * platforms another solution is being sought.
	 */
	if (emx_82573_workaround && sc->hw.mac.type == e1000_82573) {
		E1000_WRITE_REG(&sc->hw, E1000_RADV, EMX_RADV_82573);
		E1000_WRITE_REG(&sc->hw, E1000_RDTR, EMX_RDTR_82573);
	}

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct emx_rxdata *rdata = &sc->rx_data[i];

		/*
		 * Setup the Base and Length of the Rx Descriptor Ring
		 */
		bus_addr = rdata->rx_desc_paddr;
		E1000_WRITE_REG(&sc->hw, E1000_RDLEN(i),
		    rdata->num_rx_desc * sizeof(emx_rxdesc_t));
		E1000_WRITE_REG(&sc->hw, E1000_RDBAH(i),
		    (uint32_t)(bus_addr >> 32));
		E1000_WRITE_REG(&sc->hw, E1000_RDBAL(i),
		    (uint32_t)bus_addr);

		/*
		 * Setup the HW Rx Head and Tail Descriptor Pointers
		 */
		E1000_WRITE_REG(&sc->hw, E1000_RDH(i), 0);
		E1000_WRITE_REG(&sc->hw, E1000_RDT(i),
		    sc->rx_data[i].num_rx_desc - 1);
	}

	/* Set PTHRESH for improved jumbo performance */
	if (ifp->if_mtu > ETHERMTU && sc->hw.mac.type == e1000_82574) {
		uint32_t rxdctl;

		for (i = 0; i < sc->rx_ring_cnt; ++i) {
			rxdctl = E1000_READ_REG(&sc->hw, E1000_RXDCTL(i));
                	rxdctl |= 0x20;		/* PTHRESH */
                	rxdctl |= 4 << 8;	/* HTHRESH */
                	rxdctl |= 4 << 16;	/* WTHRESH */
			rxdctl |= 1 << 24;	/* Switch to granularity */
			E1000_WRITE_REG(&sc->hw, E1000_RXDCTL(i), rxdctl);
		}
	}

	if (sc->hw.mac.type >= e1000_pch2lan) {
		if (ifp->if_mtu > ETHERMTU)
			e1000_lv_jumbo_workaround_ich8lan(&sc->hw, TRUE);
		else
			e1000_lv_jumbo_workaround_ich8lan(&sc->hw, FALSE);
	}

	/* Setup the Receive Control Register */
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO |
		E1000_RCTL_RDMTS_HALF | E1000_RCTL_SECRC |
		(sc->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/* Make sure VLAN Filters are off */
	rctl &= ~E1000_RCTL_VFE;

	/* Don't store bad paket */
	rctl &= ~E1000_RCTL_SBP;

	/* MCLBYTES */
	rctl |= E1000_RCTL_SZ_2048;

	if (ifp->if_mtu > ETHERMTU)
		rctl |= E1000_RCTL_LPE;
	else
		rctl &= ~E1000_RCTL_LPE;

	/* Enable Receives */
	E1000_WRITE_REG(&sc->hw, E1000_RCTL, rctl);
}

static void
emx_destroy_rx_ring(struct emx_rxdata *rdata, int ndesc)
{
	struct emx_rxbuf *rx_buffer;
	int i;

	/* Free Receive Descriptor ring */
	if (rdata->rx_desc) {
		bus_dmamap_unload(rdata->rx_desc_dtag, rdata->rx_desc_dmap);
		bus_dmamem_free(rdata->rx_desc_dtag, rdata->rx_desc,
				rdata->rx_desc_dmap);
		bus_dma_tag_destroy(rdata->rx_desc_dtag);

		rdata->rx_desc = NULL;
	}

	if (rdata->rx_buf == NULL)
		return;

	for (i = 0; i < ndesc; i++) {
		rx_buffer = &rdata->rx_buf[i];

		KKASSERT(rx_buffer->m_head == NULL);
		bus_dmamap_destroy(rdata->rxtag, rx_buffer->map);
	}
	bus_dmamap_destroy(rdata->rxtag, rdata->rx_sparemap);
	bus_dma_tag_destroy(rdata->rxtag);

	kfree(rdata->rx_buf, M_DEVBUF);
	rdata->rx_buf = NULL;
}

static void
emx_rxeof(struct emx_rxdata *rdata, int count)
{
	struct ifnet *ifp = &rdata->sc->arpcom.ac_if;
	uint32_t staterr;
	emx_rxdesc_t *current_desc;
	struct mbuf *mp;
	int i, cpuid = mycpuid;

	i = rdata->next_rx_desc_to_check;
	current_desc = &rdata->rx_desc[i];
	staterr = le32toh(current_desc->rxd_staterr);

	if (!(staterr & E1000_RXD_STAT_DD))
		return;

	while ((staterr & E1000_RXD_STAT_DD) && count != 0) {
		struct pktinfo *pi = NULL, pi0;
		struct emx_rxbuf *rx_buf = &rdata->rx_buf[i];
		struct mbuf *m = NULL;
		int eop, len;

		logif(pkt_receive);

		mp = rx_buf->m_head;

		/*
		 * Can't defer bus_dmamap_sync(9) because TBI_ACCEPT
		 * needs to access the last received byte in the mbuf.
		 */
		bus_dmamap_sync(rdata->rxtag, rx_buf->map,
				BUS_DMASYNC_POSTREAD);

		len = le16toh(current_desc->rxd_length);
		if (staterr & E1000_RXD_STAT_EOP) {
			count--;
			eop = 1;
		} else {
			eop = 0;
		}

		if (!(staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK)) {
			uint16_t vlan = 0;
			uint32_t mrq, rss_hash;

			/*
			 * Save several necessary information,
			 * before emx_newbuf() destroy it.
			 */
			if ((staterr & E1000_RXD_STAT_VP) && eop)
				vlan = le16toh(current_desc->rxd_vlan);

			mrq = le32toh(current_desc->rxd_mrq);
			rss_hash = le32toh(current_desc->rxd_rss);

			EMX_RSS_DPRINTF(rdata->sc, 10,
			    "ring%d, mrq 0x%08x, rss_hash 0x%08x\n",
			    rdata->idx, mrq, rss_hash);

			if (emx_newbuf(rdata, i, 0) != 0) {
				IFNET_STAT_INC(ifp, iqdrops, 1);
				goto discard;
			}

			/* Assign correct length to the current fragment */
			mp->m_len = len;

			if (rdata->fmp == NULL) {
				mp->m_pkthdr.len = len;
				rdata->fmp = mp; /* Store the first mbuf */
				rdata->lmp = mp;
			} else {
				/*
				 * Chain mbuf's together
				 */
				rdata->lmp->m_next = mp;
				rdata->lmp = rdata->lmp->m_next;
				rdata->fmp->m_pkthdr.len += len;
			}

			if (eop) {
				rdata->fmp->m_pkthdr.rcvif = ifp;
				IFNET_STAT_INC(ifp, ipackets, 1);

				if (ifp->if_capenable & IFCAP_RXCSUM)
					emx_rxcsum(staterr, rdata->fmp);

				if (staterr & E1000_RXD_STAT_VP) {
					rdata->fmp->m_pkthdr.ether_vlantag =
					    vlan;
					rdata->fmp->m_flags |= M_VLANTAG;
				}
				m = rdata->fmp;
				rdata->fmp = NULL;
				rdata->lmp = NULL;

				if (ifp->if_capenable & IFCAP_RSS) {
					pi = emx_rssinfo(m, &pi0, mrq,
							 rss_hash, staterr);
				}
#ifdef EMX_RSS_DEBUG
				rdata->rx_pkts++;
#endif
			}
		} else {
			IFNET_STAT_INC(ifp, ierrors, 1);
discard:
			emx_setup_rxdesc(current_desc, rx_buf);
			if (rdata->fmp != NULL) {
				m_freem(rdata->fmp);
				rdata->fmp = NULL;
				rdata->lmp = NULL;
			}
			m = NULL;
		}

		if (m != NULL)
			ifp->if_input(ifp, m, pi, cpuid);

		/* Advance our pointers to the next descriptor. */
		if (++i == rdata->num_rx_desc)
			i = 0;

		current_desc = &rdata->rx_desc[i];
		staterr = le32toh(current_desc->rxd_staterr);
	}
	rdata->next_rx_desc_to_check = i;

	/* Advance the E1000's Receive Queue "Tail Pointer". */
	if (--i < 0)
		i = rdata->num_rx_desc - 1;
	E1000_WRITE_REG(&rdata->sc->hw, E1000_RDT(rdata->idx), i);
}

static void
emx_enable_intr(struct emx_softc *sc)
{
	uint32_t ims_mask = IMS_ENABLE_MASK;

	lwkt_serialize_handler_enable(&sc->main_serialize);

#if 0
	if (sc->hw.mac.type == e1000_82574) {
		E1000_WRITE_REG(hw, EMX_EIAC, EM_MSIX_MASK);
		ims_mask |= EM_MSIX_MASK;
	}
#endif
	E1000_WRITE_REG(&sc->hw, E1000_IMS, ims_mask);
}

static void
emx_disable_intr(struct emx_softc *sc)
{
	if (sc->hw.mac.type == e1000_82574)
		E1000_WRITE_REG(&sc->hw, EMX_EIAC, 0);
	E1000_WRITE_REG(&sc->hw, E1000_IMC, 0xffffffff);

	lwkt_serialize_handler_disable(&sc->main_serialize);
}

/*
 * Bit of a misnomer, what this really means is
 * to enable OS management of the system... aka
 * to disable special hardware management features 
 */
static void
emx_get_mgmt(struct emx_softc *sc)
{
	/* A shared code workaround */
	if (sc->flags & EMX_FLAG_HAS_MGMT) {
		int manc2h = E1000_READ_REG(&sc->hw, E1000_MANC2H);
		int manc = E1000_READ_REG(&sc->hw, E1000_MANC);

		/* disable hardware interception of ARP */
		manc &= ~(E1000_MANC_ARP_EN);

                /* enable receiving management packets to the host */
		manc |= E1000_MANC_EN_MNG2HOST;
#define E1000_MNG2HOST_PORT_623 (1 << 5)
#define E1000_MNG2HOST_PORT_664 (1 << 6)
		manc2h |= E1000_MNG2HOST_PORT_623;
		manc2h |= E1000_MNG2HOST_PORT_664;
		E1000_WRITE_REG(&sc->hw, E1000_MANC2H, manc2h);

		E1000_WRITE_REG(&sc->hw, E1000_MANC, manc);
	}
}

/*
 * Give control back to hardware management
 * controller if there is one.
 */
static void
emx_rel_mgmt(struct emx_softc *sc)
{
	if (sc->flags & EMX_FLAG_HAS_MGMT) {
		int manc = E1000_READ_REG(&sc->hw, E1000_MANC);

		/* re-enable hardware interception of ARP */
		manc |= E1000_MANC_ARP_EN;
		manc &= ~E1000_MANC_EN_MNG2HOST;

		E1000_WRITE_REG(&sc->hw, E1000_MANC, manc);
	}
}

/*
 * emx_get_hw_control() sets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded.  For AMT version (only with 82573)
 * of the f/w this means that the network i/f is open.
 */
static void
emx_get_hw_control(struct emx_softc *sc)
{
	/* Let firmware know the driver has taken over */
	if (sc->hw.mac.type == e1000_82573) {
		uint32_t swsm;

		swsm = E1000_READ_REG(&sc->hw, E1000_SWSM);
		E1000_WRITE_REG(&sc->hw, E1000_SWSM,
		    swsm | E1000_SWSM_DRV_LOAD);
	} else {
		uint32_t ctrl_ext;

		ctrl_ext = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
		E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT,
		    ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
	}
	sc->flags |= EMX_FLAG_HW_CTRL;
}

/*
 * emx_rel_hw_control() resets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.  For AMT version (only with 82573)
 * of the f/w this means that the network i/f is closed.
 */
static void
emx_rel_hw_control(struct emx_softc *sc)
{
	if ((sc->flags & EMX_FLAG_HW_CTRL) == 0)
		return;
	sc->flags &= ~EMX_FLAG_HW_CTRL;

	/* Let firmware taken over control of h/w */
	if (sc->hw.mac.type == e1000_82573) {
		uint32_t swsm;

		swsm = E1000_READ_REG(&sc->hw, E1000_SWSM);
		E1000_WRITE_REG(&sc->hw, E1000_SWSM,
		    swsm & ~E1000_SWSM_DRV_LOAD);
	} else {
		uint32_t ctrl_ext;

		ctrl_ext = E1000_READ_REG(&sc->hw, E1000_CTRL_EXT);
		E1000_WRITE_REG(&sc->hw, E1000_CTRL_EXT,
		    ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
	}
}

static int
emx_is_valid_eaddr(const uint8_t *addr)
{
	char zero_addr[ETHER_ADDR_LEN] = { 0, 0, 0, 0, 0, 0 };

	if ((addr[0] & 1) || !bcmp(addr, zero_addr, ETHER_ADDR_LEN))
		return (FALSE);

	return (TRUE);
}

/*
 * Enable PCI Wake On Lan capability
 */
static void
emx_enable_wol(device_t dev)
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
emx_update_stats(struct emx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (sc->hw.phy.media_type == e1000_media_type_copper ||
	    (E1000_READ_REG(&sc->hw, E1000_STATUS) & E1000_STATUS_LU)) {
		sc->stats.symerrs += E1000_READ_REG(&sc->hw, E1000_SYMERRS);
		sc->stats.sec += E1000_READ_REG(&sc->hw, E1000_SEC);
	}
	sc->stats.crcerrs += E1000_READ_REG(&sc->hw, E1000_CRCERRS);
	sc->stats.mpc += E1000_READ_REG(&sc->hw, E1000_MPC);
	sc->stats.scc += E1000_READ_REG(&sc->hw, E1000_SCC);
	sc->stats.ecol += E1000_READ_REG(&sc->hw, E1000_ECOL);

	sc->stats.mcc += E1000_READ_REG(&sc->hw, E1000_MCC);
	sc->stats.latecol += E1000_READ_REG(&sc->hw, E1000_LATECOL);
	sc->stats.colc += E1000_READ_REG(&sc->hw, E1000_COLC);
	sc->stats.dc += E1000_READ_REG(&sc->hw, E1000_DC);
	sc->stats.rlec += E1000_READ_REG(&sc->hw, E1000_RLEC);
	sc->stats.xonrxc += E1000_READ_REG(&sc->hw, E1000_XONRXC);
	sc->stats.xontxc += E1000_READ_REG(&sc->hw, E1000_XONTXC);
	sc->stats.xoffrxc += E1000_READ_REG(&sc->hw, E1000_XOFFRXC);
	sc->stats.xofftxc += E1000_READ_REG(&sc->hw, E1000_XOFFTXC);
	sc->stats.fcruc += E1000_READ_REG(&sc->hw, E1000_FCRUC);
	sc->stats.prc64 += E1000_READ_REG(&sc->hw, E1000_PRC64);
	sc->stats.prc127 += E1000_READ_REG(&sc->hw, E1000_PRC127);
	sc->stats.prc255 += E1000_READ_REG(&sc->hw, E1000_PRC255);
	sc->stats.prc511 += E1000_READ_REG(&sc->hw, E1000_PRC511);
	sc->stats.prc1023 += E1000_READ_REG(&sc->hw, E1000_PRC1023);
	sc->stats.prc1522 += E1000_READ_REG(&sc->hw, E1000_PRC1522);
	sc->stats.gprc += E1000_READ_REG(&sc->hw, E1000_GPRC);
	sc->stats.bprc += E1000_READ_REG(&sc->hw, E1000_BPRC);
	sc->stats.mprc += E1000_READ_REG(&sc->hw, E1000_MPRC);
	sc->stats.gptc += E1000_READ_REG(&sc->hw, E1000_GPTC);

	/* For the 64-bit byte counters the low dword must be read first. */
	/* Both registers clear on the read of the high dword */

	sc->stats.gorc += E1000_READ_REG(&sc->hw, E1000_GORCH);
	sc->stats.gotc += E1000_READ_REG(&sc->hw, E1000_GOTCH);

	sc->stats.rnbc += E1000_READ_REG(&sc->hw, E1000_RNBC);
	sc->stats.ruc += E1000_READ_REG(&sc->hw, E1000_RUC);
	sc->stats.rfc += E1000_READ_REG(&sc->hw, E1000_RFC);
	sc->stats.roc += E1000_READ_REG(&sc->hw, E1000_ROC);
	sc->stats.rjc += E1000_READ_REG(&sc->hw, E1000_RJC);

	sc->stats.tor += E1000_READ_REG(&sc->hw, E1000_TORH);
	sc->stats.tot += E1000_READ_REG(&sc->hw, E1000_TOTH);

	sc->stats.tpr += E1000_READ_REG(&sc->hw, E1000_TPR);
	sc->stats.tpt += E1000_READ_REG(&sc->hw, E1000_TPT);
	sc->stats.ptc64 += E1000_READ_REG(&sc->hw, E1000_PTC64);
	sc->stats.ptc127 += E1000_READ_REG(&sc->hw, E1000_PTC127);
	sc->stats.ptc255 += E1000_READ_REG(&sc->hw, E1000_PTC255);
	sc->stats.ptc511 += E1000_READ_REG(&sc->hw, E1000_PTC511);
	sc->stats.ptc1023 += E1000_READ_REG(&sc->hw, E1000_PTC1023);
	sc->stats.ptc1522 += E1000_READ_REG(&sc->hw, E1000_PTC1522);
	sc->stats.mptc += E1000_READ_REG(&sc->hw, E1000_MPTC);
	sc->stats.bptc += E1000_READ_REG(&sc->hw, E1000_BPTC);

	sc->stats.algnerrc += E1000_READ_REG(&sc->hw, E1000_ALGNERRC);
	sc->stats.rxerrc += E1000_READ_REG(&sc->hw, E1000_RXERRC);
	sc->stats.tncrs += E1000_READ_REG(&sc->hw, E1000_TNCRS);
	sc->stats.cexterr += E1000_READ_REG(&sc->hw, E1000_CEXTERR);
	sc->stats.tsctc += E1000_READ_REG(&sc->hw, E1000_TSCTC);
	sc->stats.tsctfc += E1000_READ_REG(&sc->hw, E1000_TSCTFC);

	IFNET_STAT_SET(ifp, collisions, sc->stats.colc);

	/* Rx Errors */
	IFNET_STAT_SET(ifp, ierrors,
	    sc->stats.rxerrc + sc->stats.crcerrs + sc->stats.algnerrc +
	    sc->stats.ruc + sc->stats.roc + sc->stats.mpc + sc->stats.cexterr);

	/* Tx Errors */
	IFNET_STAT_SET(ifp, oerrors, sc->stats.ecol + sc->stats.latecol);
}

static void
emx_print_debug_info(struct emx_softc *sc)
{
	device_t dev = sc->dev;
	uint8_t *hw_addr = sc->hw.hw_addr;
	int i;

	device_printf(dev, "Adapter hardware address = %p \n", hw_addr);
	device_printf(dev, "CTRL = 0x%x RCTL = 0x%x \n",
	    E1000_READ_REG(&sc->hw, E1000_CTRL),
	    E1000_READ_REG(&sc->hw, E1000_RCTL));
	device_printf(dev, "Packet buffer = Tx=%dk Rx=%dk \n",
	    ((E1000_READ_REG(&sc->hw, E1000_PBA) & 0xffff0000) >> 16),\
	    (E1000_READ_REG(&sc->hw, E1000_PBA) & 0xffff) );
	device_printf(dev, "Flow control watermarks high = %d low = %d\n",
	    sc->hw.fc.high_water, sc->hw.fc.low_water);
	device_printf(dev, "tx_int_delay = %d, tx_abs_int_delay = %d\n",
	    E1000_READ_REG(&sc->hw, E1000_TIDV),
	    E1000_READ_REG(&sc->hw, E1000_TADV));
	device_printf(dev, "rx_int_delay = %d, rx_abs_int_delay = %d\n",
	    E1000_READ_REG(&sc->hw, E1000_RDTR),
	    E1000_READ_REG(&sc->hw, E1000_RADV));

	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		device_printf(dev, "hw %d tdh = %d, hw tdt = %d\n", i,
		    E1000_READ_REG(&sc->hw, E1000_TDH(i)),
		    E1000_READ_REG(&sc->hw, E1000_TDT(i)));
	}
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		device_printf(dev, "hw %d rdh = %d, hw rdt = %d\n", i,
		    E1000_READ_REG(&sc->hw, E1000_RDH(i)),
		    E1000_READ_REG(&sc->hw, E1000_RDT(i)));
	}

	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		device_printf(dev, "TX %d Tx descriptors avail = %d\n", i,
		    sc->tx_data[i].num_tx_desc_avail);
		device_printf(dev, "TX %d TSO segments = %lu\n", i,
		    sc->tx_data[i].tso_segments);
		device_printf(dev, "TX %d TSO ctx reused = %lu\n", i,
		    sc->tx_data[i].tso_ctx_reused);
	}
}

static void
emx_print_hw_stats(struct emx_softc *sc)
{
	device_t dev = sc->dev;

	device_printf(dev, "Excessive collisions = %lld\n",
	    (long long)sc->stats.ecol);
#if (DEBUG_HW > 0)  /* Dont output these errors normally */
	device_printf(dev, "Symbol errors = %lld\n",
	    (long long)sc->stats.symerrs);
#endif
	device_printf(dev, "Sequence errors = %lld\n",
	    (long long)sc->stats.sec);
	device_printf(dev, "Defer count = %lld\n",
	    (long long)sc->stats.dc);
	device_printf(dev, "Missed Packets = %lld\n",
	    (long long)sc->stats.mpc);
	device_printf(dev, "Receive No Buffers = %lld\n",
	    (long long)sc->stats.rnbc);
	/* RLEC is inaccurate on some hardware, calculate our own. */
	device_printf(dev, "Receive Length Errors = %lld\n",
	    ((long long)sc->stats.roc + (long long)sc->stats.ruc));
	device_printf(dev, "Receive errors = %lld\n",
	    (long long)sc->stats.rxerrc);
	device_printf(dev, "Crc errors = %lld\n",
	    (long long)sc->stats.crcerrs);
	device_printf(dev, "Alignment errors = %lld\n",
	    (long long)sc->stats.algnerrc);
	device_printf(dev, "Collision/Carrier extension errors = %lld\n",
	    (long long)sc->stats.cexterr);
	device_printf(dev, "RX overruns = %ld\n", sc->rx_overruns);
	device_printf(dev, "XON Rcvd = %lld\n",
	    (long long)sc->stats.xonrxc);
	device_printf(dev, "XON Xmtd = %lld\n",
	    (long long)sc->stats.xontxc);
	device_printf(dev, "XOFF Rcvd = %lld\n",
	    (long long)sc->stats.xoffrxc);
	device_printf(dev, "XOFF Xmtd = %lld\n",
	    (long long)sc->stats.xofftxc);
	device_printf(dev, "Good Packets Rcvd = %lld\n",
	    (long long)sc->stats.gprc);
	device_printf(dev, "Good Packets Xmtd = %lld\n",
	    (long long)sc->stats.gptc);
}

static void
emx_print_nvm_info(struct emx_softc *sc)
{
	uint16_t eeprom_data;
	int i, j, row = 0;

	/* Its a bit crude, but it gets the job done */
	kprintf("\nInterface EEPROM Dump:\n");
	kprintf("Offset\n0x0000  ");
	for (i = 0, j = 0; i < 32; i++, j++) {
		if (j == 8) { /* Make the offset block */
			j = 0; ++row;
			kprintf("\n0x00%x0  ",row);
		}
		e1000_read_nvm(&sc->hw, i, 1, &eeprom_data);
		kprintf("%04x ", eeprom_data);
	}
	kprintf("\n");
}

static int
emx_sysctl_debug_info(SYSCTL_HANDLER_ARGS)
{
	struct emx_softc *sc;
	struct ifnet *ifp;
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	sc = (struct emx_softc *)arg1;
	ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);

	if (result == 1)
		emx_print_debug_info(sc);

	/*
	 * This value will cause a hex dump of the
	 * first 32 16-bit words of the EEPROM to
	 * the screen.
	 */
	if (result == 2)
		emx_print_nvm_info(sc);

	ifnet_deserialize_all(ifp);

	return (error);
}

static int
emx_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	if (result == 1) {
		struct emx_softc *sc = (struct emx_softc *)arg1;
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ifnet_serialize_all(ifp);
		emx_print_hw_stats(sc);
		ifnet_deserialize_all(ifp);
	}
	return (error);
}

static void
emx_add_sysctl(struct emx_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	char pkt_desc[32];
	int i;

	ctx = device_get_sysctl_ctx(sc->dev);
	tree = device_get_sysctl_tree(sc->dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
			OID_AUTO, "debug", CTLTYPE_INT|CTLFLAG_RW, sc, 0,
			emx_sysctl_debug_info, "I", "Debug Information");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
			OID_AUTO, "stats", CTLTYPE_INT|CTLFLAG_RW, sc, 0,
			emx_sysctl_stats, "I", "Statistics");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxd", CTLFLAG_RD, &sc->rx_data[0].num_rx_desc, 0,
	    "# of RX descs");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txd", CTLFLAG_RD, &sc->tx_data[0].num_tx_desc, 0,
	    "# of TX descs");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "int_throttle_ceil", CTLTYPE_INT|CTLFLAG_RW, sc, 0,
	    emx_sysctl_int_throttle, "I", "interrupt throttling rate");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_intr_nsegs", CTLTYPE_INT|CTLFLAG_RW, sc, 0,
	    emx_sysctl_tx_intr_nsegs, "I", "# segments per TX interrupt");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_wreg_nsegs", CTLTYPE_INT|CTLFLAG_RW, sc, 0,
	    emx_sysctl_tx_wreg_nsegs, "I",
	    "# segments sent before write to hardware register");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rx_ring_cnt", CTLFLAG_RD, &sc->rx_ring_cnt, 0,
	    "# of RX rings");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_ring_cnt", CTLFLAG_RD, &sc->tx_ring_cnt, 0,
	    "# of TX rings");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "tx_ring_inuse", CTLFLAG_RD, &sc->tx_ring_inuse, 0,
	    "# of TX rings used");

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

#ifdef EMX_RSS_DEBUG
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
		       OID_AUTO, "rss_debug", CTLFLAG_RW, &sc->rss_debug,
		       0, "RSS debug level");
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		ksnprintf(pkt_desc, sizeof(pkt_desc), "rx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    pkt_desc, CTLFLAG_RW, &sc->rx_data[i].rx_pkts,
		    "RXed packets");
	}
#endif
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
#ifdef EMX_TSS_DEBUG
		ksnprintf(pkt_desc, sizeof(pkt_desc), "tx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    pkt_desc, CTLFLAG_RW, &sc->tx_data[i].tx_pkts,
		    "TXed packets");
#endif

		ksnprintf(pkt_desc, sizeof(pkt_desc), "tx%d_nmbuf", i);
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    pkt_desc, CTLFLAG_RD, &sc->tx_data[i].tx_nmbuf, 0,
		    "# of pending TX mbufs");
		ksnprintf(pkt_desc, sizeof(pkt_desc), "tx%d_gc", i);
		SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    pkt_desc, CTLFLAG_RW, &sc->tx_data[i].tx_gc,
		    "# of TX desc GC");
	}
}

static int
emx_sysctl_int_throttle(SYSCTL_HANDLER_ARGS)
{
	struct emx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, throttle;

	throttle = sc->int_throttle_ceil;
	error = sysctl_handle_int(oidp, &throttle, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (throttle < 0 || throttle > 1000000000 / 256)
		return EINVAL;

	if (throttle) {
		/*
		 * Set the interrupt throttling rate in 256ns increments,
		 * recalculate sysctl value assignment to get exact frequency.
		 */
		throttle = 1000000000 / 256 / throttle;

		/* Upper 16bits of ITR is reserved and should be zero */
		if (throttle & 0xffff0000)
			return EINVAL;
	}

	ifnet_serialize_all(ifp);

	if (throttle)
		sc->int_throttle_ceil = 1000000000 / 256 / throttle;
	else
		sc->int_throttle_ceil = 0;

	if (ifp->if_flags & IFF_RUNNING)
		emx_set_itr(sc, throttle);

	ifnet_deserialize_all(ifp);

	if (bootverbose) {
		if_printf(ifp, "Interrupt moderation set to %d/sec\n",
			  sc->int_throttle_ceil);
	}
	return 0;
}

static int
emx_sysctl_tx_intr_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct emx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct emx_txdata *tdata = &sc->tx_data[0];
	int error, segs;

	segs = tdata->tx_intr_nsegs;
	error = sysctl_handle_int(oidp, &segs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (segs <= 0)
		return EINVAL;

	ifnet_serialize_all(ifp);

	/*
	 * Don't allow tx_intr_nsegs to become:
	 * o  Less the oact_tx_desc
	 * o  Too large that no TX desc will cause TX interrupt to
	 *    be generated (OACTIVE will never recover)
	 * o  Too small that will cause tx_dd[] overflow
	 */
	if (segs < tdata->oact_tx_desc ||
	    segs >= tdata->num_tx_desc - tdata->oact_tx_desc ||
	    segs < tdata->num_tx_desc / EMX_TXDD_SAFE) {
		error = EINVAL;
	} else {
		int i;

		error = 0;
		for (i = 0; i < sc->tx_ring_cnt; ++i)
			sc->tx_data[i].tx_intr_nsegs = segs;
	}

	ifnet_deserialize_all(ifp);

	return error;
}

static int
emx_sysctl_tx_wreg_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct emx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, nsegs, i;

	nsegs = sc->tx_data[0].tx_wreg_nsegs;
	error = sysctl_handle_int(oidp, &nsegs, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		sc->tx_data[i].tx_wreg_nsegs =nsegs;
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
emx_dma_alloc(struct emx_softc *sc)
{
	int error, i;

	/*
	 * Create top level busdma tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT,
			0, &sc->parent_dtag);
	if (error) {
		device_printf(sc->dev, "could not create top level DMA tag\n");
		return error;
	}

	/*
	 * Allocate transmit descriptors ring and buffers
	 */
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		error = emx_create_tx_ring(&sc->tx_data[i]);
		if (error) {
			device_printf(sc->dev,
			    "Could not setup transmit structures\n");
			return error;
		}
	}

	/*
	 * Allocate receive descriptors ring and buffers
	 */
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		error = emx_create_rx_ring(&sc->rx_data[i]);
		if (error) {
			device_printf(sc->dev,
			    "Could not setup receive structures\n");
			return error;
		}
	}
	return 0;
}

static void
emx_dma_free(struct emx_softc *sc)
{
	int i;

	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		emx_destroy_tx_ring(&sc->tx_data[i],
		    sc->tx_data[i].num_tx_desc);
	}

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		emx_destroy_rx_ring(&sc->rx_data[i],
		    sc->rx_data[i].num_rx_desc);
	}

	/* Free top level busdma tag */
	if (sc->parent_dtag != NULL)
		bus_dma_tag_destroy(sc->parent_dtag);
}

static void
emx_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct emx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->serializes, EMX_NSERIALIZE, slz);
}

static void
emx_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct emx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->serializes, EMX_NSERIALIZE, slz);
}

static int
emx_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct emx_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->serializes, EMX_NSERIALIZE, slz);
}

static void
emx_serialize_skipmain(struct emx_softc *sc)
{
	lwkt_serialize_array_enter(sc->serializes, EMX_NSERIALIZE, 1);
}

static void
emx_deserialize_skipmain(struct emx_softc *sc)
{
	lwkt_serialize_array_exit(sc->serializes, EMX_NSERIALIZE, 1);
}

#ifdef INVARIANTS

static void
emx_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct emx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->serializes, EMX_NSERIALIZE,
	    slz, serialized);
}

#endif	/* INVARIANTS */

#ifdef IFPOLL_ENABLE

static void
emx_npoll_status(struct ifnet *ifp)
{
	struct emx_softc *sc = ifp->if_softc;
	uint32_t reg_icr;

	ASSERT_SERIALIZED(&sc->main_serialize);

	reg_icr = E1000_READ_REG(&sc->hw, E1000_ICR);
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		callout_stop(&sc->timer);
		sc->hw.mac.get_link_status = 1;
		emx_update_link_status(sc);
		callout_reset(&sc->timer, hz, emx_timer, sc);
	}
}

static void
emx_npoll_tx(struct ifnet *ifp, void *arg, int cycle __unused)
{
	struct emx_txdata *tdata = arg;

	ASSERT_SERIALIZED(&tdata->tx_serialize);

	emx_tx_intr(tdata);
	emx_try_txgc(tdata, 1);
}

static void
emx_npoll_rx(struct ifnet *ifp __unused, void *arg, int cycle)
{
	struct emx_rxdata *rdata = arg;

	ASSERT_SERIALIZED(&rdata->rx_serialize);

	emx_rxeof(rdata, cycle);
}

static void
emx_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct emx_softc *sc = ifp->if_softc;
	int i, txr_cnt;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (info) {
		int cpu;

		info->ifpi_status.status_func = emx_npoll_status;
		info->ifpi_status.serializer = &sc->main_serialize;

		txr_cnt = emx_get_txring_inuse(sc, TRUE);
		for (i = 0; i < txr_cnt; ++i) {
			struct emx_txdata *tdata = &sc->tx_data[i];

			cpu = if_ringmap_cpumap(sc->tx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_tx[cpu].poll_func = emx_npoll_tx;
			info->ifpi_tx[cpu].arg = tdata;
			info->ifpi_tx[cpu].serializer = &tdata->tx_serialize;
			ifsq_set_cpuid(tdata->ifsq, cpu);
		}

		for (i = 0; i < sc->rx_ring_cnt; ++i) {
			struct emx_rxdata *rdata = &sc->rx_data[i];

			cpu = if_ringmap_cpumap(sc->rx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_rx[cpu].poll_func = emx_npoll_rx;
			info->ifpi_rx[cpu].arg = rdata;
			info->ifpi_rx[cpu].serializer = &rdata->rx_serialize;
		}
	} else {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct emx_txdata *tdata = &sc->tx_data[i];

			ifsq_set_cpuid(tdata->ifsq,
			    rman_get_cpuid(sc->intr_res));
		}
	}
	if (ifp->if_flags & IFF_RUNNING)
		emx_init(sc);
}

#endif	/* IFPOLL_ENABLE */

static void
emx_set_itr(struct emx_softc *sc, uint32_t itr)
{
	E1000_WRITE_REG(&sc->hw, E1000_ITR, itr);
	if (sc->hw.mac.type == e1000_82574) {
		int i;

		/*
		 * When using MSIX interrupts we need to
		 * throttle using the EITR register
		 */
		for (i = 0; i < 4; ++i)
			E1000_WRITE_REG(&sc->hw, E1000_EITR_82574(i), itr);
	}
}

/*
 * Disable the L0s, 82574L Errata #20
 */
static void
emx_disable_aspm(struct emx_softc *sc)
{
	uint16_t link_cap, link_ctrl, disable;
	uint8_t pcie_ptr, reg;
	device_t dev = sc->dev;

	switch (sc->hw.mac.type) {
	case e1000_82571:
	case e1000_82572:
	case e1000_82573:
		/*
		 * 82573 specification update
		 * errata #8 disable L0s
		 * errata #41 disable L1
		 *
		 * 82571/82572 specification update
		 # errata #13 disable L1
		 * errata #68 disable L0s
		 */
		disable = PCIEM_LNKCTL_ASPM_L0S | PCIEM_LNKCTL_ASPM_L1;
		break;

	case e1000_82574:
		/*
		 * 82574 specification update errata #20
		 *
		 * There is no need to disable L1
		 */
		disable = PCIEM_LNKCTL_ASPM_L0S;
		break;

	default:
		return;
	}

	pcie_ptr = pci_get_pciecap_ptr(dev);
	if (pcie_ptr == 0)
		return;

	link_cap = pci_read_config(dev, pcie_ptr + PCIER_LINKCAP, 2);
	if ((link_cap & PCIEM_LNKCAP_ASPM_MASK) == 0)
		return;

	if (bootverbose)
		if_printf(&sc->arpcom.ac_if, "disable ASPM %#02x\n", disable);

	reg = pcie_ptr + PCIER_LINKCTRL;
	link_ctrl = pci_read_config(dev, reg, 2);
	link_ctrl &= ~disable;
	pci_write_config(dev, reg, link_ctrl, 2);
}

static int
emx_tso_pullup(struct emx_txdata *tdata, struct mbuf **mp)
{
	int iphlen, hoff, thoff, ex = 0;
	struct mbuf *m;
	struct ip *ip;

	m = *mp;
	KASSERT(M_WRITABLE(m), ("TSO mbuf not writable"));

	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;
	hoff = m->m_pkthdr.csum_lhlen;

	KASSERT(iphlen > 0, ("invalid ip hlen"));
	KASSERT(thoff > 0, ("invalid tcp hlen"));
	KASSERT(hoff > 0, ("invalid ether hlen"));

	if (tdata->tx_flags & EMX_TXFLAG_TSO_PULLEX)
		ex = 4;

	if (m->m_len < hoff + iphlen + thoff + ex) {
		m = m_pullup(m, hoff + iphlen + thoff + ex);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		*mp = m;
	}
	ip = mtodoff(m, struct ip *, hoff);
	ip->ip_len = 0;

	return 0;
}

static int
emx_tso_setup(struct emx_txdata *tdata, struct mbuf *mp,
    uint32_t *txd_upper, uint32_t *txd_lower)
{
	struct e1000_context_desc *TXD;
	int hoff, iphlen, thoff, hlen;
	int mss, pktlen, curr_txd;

#ifdef EMX_TSO_DEBUG
	tdata->tso_segments++;
#endif

	iphlen = mp->m_pkthdr.csum_iphlen;
	thoff = mp->m_pkthdr.csum_thlen;
	hoff = mp->m_pkthdr.csum_lhlen;
	mss = mp->m_pkthdr.tso_segsz;
	pktlen = mp->m_pkthdr.len;

	if ((tdata->tx_flags & EMX_TXFLAG_FORCECTX) == 0 &&
	    tdata->csum_flags == CSUM_TSO &&
	    tdata->csum_iphlen == iphlen &&
	    tdata->csum_lhlen == hoff &&
	    tdata->csum_thlen == thoff &&
	    tdata->csum_mss == mss &&
	    tdata->csum_pktlen == pktlen) {
		*txd_upper = tdata->csum_txd_upper;
		*txd_lower = tdata->csum_txd_lower;
#ifdef EMX_TSO_DEBUG
		tdata->tso_ctx_reused++;
#endif
		return 0;
	}
	hlen = hoff + iphlen + thoff;

	/*
	 * Setup a new TSO context.
	 */

	curr_txd = tdata->next_avail_tx_desc;
	TXD = (struct e1000_context_desc *)&tdata->tx_desc_base[curr_txd];

	*txd_lower = E1000_TXD_CMD_DEXT |	/* Extended descr type */
		     E1000_TXD_DTYP_D |		/* Data descr type */
		     E1000_TXD_CMD_TSE;		/* Do TSE on this packet */

	/* IP and/or TCP header checksum calculation and insertion. */
	*txd_upper = (E1000_TXD_POPTS_IXSM | E1000_TXD_POPTS_TXSM) << 8;

	/*
	 * Start offset for header checksum calculation.
	 * End offset for header checksum calculation.
	 * Offset of place put the checksum.
	 */
	TXD->lower_setup.ip_fields.ipcss = hoff;
	TXD->lower_setup.ip_fields.ipcse = htole16(hoff + iphlen - 1);
	TXD->lower_setup.ip_fields.ipcso = hoff + offsetof(struct ip, ip_sum);

	/*
	 * Start offset for payload checksum calculation.
	 * End offset for payload checksum calculation.
	 * Offset of place to put the checksum.
	 */
	TXD->upper_setup.tcp_fields.tucss = hoff + iphlen;
	TXD->upper_setup.tcp_fields.tucse = 0;
	TXD->upper_setup.tcp_fields.tucso =
	    hoff + iphlen + offsetof(struct tcphdr, th_sum);

	/*
	 * Payload size per packet w/o any headers.
	 * Length of all headers up to payload.
	 */
	TXD->tcp_seg_setup.fields.mss = htole16(mss);
	TXD->tcp_seg_setup.fields.hdr_len = hlen;
	TXD->cmd_and_length = htole32(E1000_TXD_CMD_IFCS |
				E1000_TXD_CMD_DEXT |	/* Extended descr */
				E1000_TXD_CMD_TSE |	/* TSE context */
				E1000_TXD_CMD_IP |	/* Do IP csum */
				E1000_TXD_CMD_TCP |	/* Do TCP checksum */
				(pktlen - hlen));	/* Total len */

	/* Save the information for this TSO context */
	tdata->csum_flags = CSUM_TSO;
	tdata->csum_lhlen = hoff;
	tdata->csum_iphlen = iphlen;
	tdata->csum_thlen = thoff;
	tdata->csum_mss = mss;
	tdata->csum_pktlen = pktlen;
	tdata->csum_txd_upper = *txd_upper;
	tdata->csum_txd_lower = *txd_lower;

	if (++curr_txd == tdata->num_tx_desc)
		curr_txd = 0;

	KKASSERT(tdata->num_tx_desc_avail > 0);
	tdata->num_tx_desc_avail--;

	tdata->next_avail_tx_desc = curr_txd;
	return 1;
}

static int
emx_get_txring_inuse(const struct emx_softc *sc, boolean_t polling)
{
	if (polling)
		return sc->tx_ring_cnt;
	else
		return 1;
}

/*
 * Remove all descriptors from the TX ring.
 *
 * We want to clear all pending descriptors from the TX ring.  Zeroing
 * happens when the HW reads the regs.  We assign the ring itself as
 * the data of the next descriptor.  We don't care about the data we
 * are about to reset the HW.
 */
static void
emx_flush_tx_ring(struct emx_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	uint32_t tctl;
	int i;

	tctl = E1000_READ_REG(hw, E1000_TCTL);
	E1000_WRITE_REG(hw, E1000_TCTL, tctl | E1000_TCTL_EN);

	for (i = 0; i < sc->tx_ring_inuse; ++i) {
		struct emx_txdata *tdata = &sc->tx_data[i];
		struct e1000_tx_desc *txd;

		if (E1000_READ_REG(hw, E1000_TDLEN(i)) == 0)
			continue;

		txd = &tdata->tx_desc_base[tdata->next_avail_tx_desc++];
		if (tdata->next_avail_tx_desc == tdata->num_tx_desc)
			tdata->next_avail_tx_desc = 0;

		/* Just use the ring as a dummy buffer addr */
		txd->buffer_addr = tdata->tx_desc_paddr;
		txd->lower.data = htole32(E1000_TXD_CMD_IFCS | 512);
		txd->upper.data = 0;

		E1000_WRITE_REG(hw, E1000_TDT(i), tdata->next_avail_tx_desc);
		usec_delay(250);
	}
}

/*
 * Remove all descriptors from the RX rings.
 *
 * Mark all descriptors in the RX rings as consumed and disable the RX rings.
 */
static void
emx_flush_rx_ring(struct emx_softc *sc)
{
	struct e1000_hw	*hw = &sc->hw;
	uint32_t rctl;
	int i;

	rctl = E1000_READ_REG(hw, E1000_RCTL);
	E1000_WRITE_REG(hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
	E1000_WRITE_FLUSH(hw);
	usec_delay(150);

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		uint32_t rxdctl;

		rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(i));
		/* Zero the lower 14 bits (prefetch and host thresholds) */
		rxdctl &= 0xffffc000;
		/*
		 * Update thresholds: prefetch threshold to 31, host threshold
		 * to 1 and make sure the granularity is "descriptors" and not
		 * "cache lines".
		 */
		rxdctl |= (0x1F | (1 << 8) | E1000_RXDCTL_THRESH_UNIT_DESC);
		E1000_WRITE_REG(hw, E1000_RXDCTL(i), rxdctl);
	}

	/* Momentarily enable the RX rings for the changes to take effect */
	E1000_WRITE_REG(hw, E1000_RCTL, rctl | E1000_RCTL_EN);
	E1000_WRITE_FLUSH(hw);
	usec_delay(150);
	E1000_WRITE_REG(hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
}

/*
 * Remove all descriptors from the descriptor rings.
 *
 * In i219, the descriptor rings must be emptied before resetting the HW
 * or before changing the device state to D3 during runtime (runtime PM).
 *
 * Failure to do this will cause the HW to enter a unit hang state which
 * can only be released by PCI reset on the device.
 */
static void
emx_flush_txrx_ring(struct emx_softc *sc)
{
	struct e1000_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	uint16_t hang_state;
	uint32_t fext_nvm11, tdlen;
	int i;

	/*
	 * First, disable MULR fix in FEXTNVM11.
	 */
	fext_nvm11 = E1000_READ_REG(hw, E1000_FEXTNVM11);
	fext_nvm11 |= E1000_FEXTNVM11_DISABLE_MULR_FIX;
	E1000_WRITE_REG(hw, E1000_FEXTNVM11, fext_nvm11);

	/* 
	 * Do nothing if we're not in faulty state, or if the queue is
	 * empty.
	 */
	tdlen = 0;
	for (i = 0; i < sc->tx_ring_inuse; ++i)
		tdlen += E1000_READ_REG(hw, E1000_TDLEN(i));
	hang_state = pci_read_config(dev, EMX_PCICFG_DESC_RING_STATUS, 2);
	if ((hang_state & EMX_FLUSH_DESC_REQUIRED) && tdlen)
		emx_flush_tx_ring(sc);

	/*
	 * Recheck, maybe the fault is caused by the RX ring.
	 */
	hang_state = pci_read_config(dev, EMX_PCICFG_DESC_RING_STATUS, 2);
	if (hang_state & EMX_FLUSH_DESC_REQUIRED)
		emx_flush_rx_ring(sc);
}
