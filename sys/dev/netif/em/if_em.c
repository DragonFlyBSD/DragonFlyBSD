/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 2001-2015, Intel Corporation
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
 *
 */
/*
 * SERIALIZATION API RULES:
 *
 * - We must call lwkt_serialize_handler_enable() prior to enabling the
 *   hardware interrupt and lwkt_serialize_handler_disable() after disabling
 *   the hardware interrupt in order to avoid handler execution races from
 *   scheduled interrupt threads.
 */

#include "opt_ifpoll.h"

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
#include <net/if_poll.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include <dev/netif/ig_hal/e1000_api.h>
#include <dev/netif/ig_hal/e1000_82571.h>
#include <dev/netif/ig_hal/e1000_dragonfly.h>
#include <dev/netif/em/if_em.h>

#define DEBUG_HW 0

#define EM_NAME	"Intel(R) PRO/1000 Network Connection "
#define EM_VER	" 7.6.2"

#define _EM_DEVICE(id, ret)	\
	{ EM_VENDOR_ID, E1000_DEV_ID_##id, ret, EM_NAME #id EM_VER }
#define EM_EMX_DEVICE(id)	_EM_DEVICE(id, -100)
#define EM_DEVICE(id)		_EM_DEVICE(id, 0)
#define EM_DEVICE_NULL	{ 0, 0, 0, NULL }

static const struct em_vendor_info em_vendor_info_array[] = {
	EM_DEVICE(82540EM),
	EM_DEVICE(82540EM_LOM),
	EM_DEVICE(82540EP),
	EM_DEVICE(82540EP_LOM),
	EM_DEVICE(82540EP_LP),

	EM_DEVICE(82541EI),
	EM_DEVICE(82541ER),
	EM_DEVICE(82541ER_LOM),
	EM_DEVICE(82541EI_MOBILE),
	EM_DEVICE(82541GI),
	EM_DEVICE(82541GI_LF),
	EM_DEVICE(82541GI_MOBILE),

	EM_DEVICE(82542),

	EM_DEVICE(82543GC_FIBER),
	EM_DEVICE(82543GC_COPPER),

	EM_DEVICE(82544EI_COPPER),
	EM_DEVICE(82544EI_FIBER),
	EM_DEVICE(82544GC_COPPER),
	EM_DEVICE(82544GC_LOM),

	EM_DEVICE(82545EM_COPPER),
	EM_DEVICE(82545EM_FIBER),
	EM_DEVICE(82545GM_COPPER),
	EM_DEVICE(82545GM_FIBER),
	EM_DEVICE(82545GM_SERDES),

	EM_DEVICE(82546EB_COPPER),
	EM_DEVICE(82546EB_FIBER),
	EM_DEVICE(82546EB_QUAD_COPPER),
	EM_DEVICE(82546GB_COPPER),
	EM_DEVICE(82546GB_FIBER),
	EM_DEVICE(82546GB_SERDES),
	EM_DEVICE(82546GB_PCIE),
	EM_DEVICE(82546GB_QUAD_COPPER),
	EM_DEVICE(82546GB_QUAD_COPPER_KSP3),

	EM_DEVICE(82547EI),
	EM_DEVICE(82547EI_MOBILE),
	EM_DEVICE(82547GI),

	EM_EMX_DEVICE(82571EB_COPPER),
	EM_EMX_DEVICE(82571EB_FIBER),
	EM_EMX_DEVICE(82571EB_SERDES),
	EM_EMX_DEVICE(82571EB_SERDES_DUAL),
	EM_EMX_DEVICE(82571EB_SERDES_QUAD),
	EM_EMX_DEVICE(82571EB_QUAD_COPPER),
	EM_EMX_DEVICE(82571EB_QUAD_COPPER_BP),
	EM_EMX_DEVICE(82571EB_QUAD_COPPER_LP),
	EM_EMX_DEVICE(82571EB_QUAD_FIBER),
	EM_EMX_DEVICE(82571PT_QUAD_COPPER),

	EM_EMX_DEVICE(82572EI_COPPER),
	EM_EMX_DEVICE(82572EI_FIBER),
	EM_EMX_DEVICE(82572EI_SERDES),
	EM_EMX_DEVICE(82572EI),

	EM_EMX_DEVICE(82573E),
	EM_EMX_DEVICE(82573E_IAMT),
	EM_EMX_DEVICE(82573L),

	EM_DEVICE(82583V),

	EM_EMX_DEVICE(80003ES2LAN_COPPER_SPT),
	EM_EMX_DEVICE(80003ES2LAN_SERDES_SPT),
	EM_EMX_DEVICE(80003ES2LAN_COPPER_DPT),
	EM_EMX_DEVICE(80003ES2LAN_SERDES_DPT),

	EM_DEVICE(ICH8_IGP_M_AMT),
	EM_DEVICE(ICH8_IGP_AMT),
	EM_DEVICE(ICH8_IGP_C),
	EM_DEVICE(ICH8_IFE),
	EM_DEVICE(ICH8_IFE_GT),
	EM_DEVICE(ICH8_IFE_G),
	EM_DEVICE(ICH8_IGP_M),
	EM_DEVICE(ICH8_82567V_3),

	EM_DEVICE(ICH9_IGP_M_AMT),
	EM_DEVICE(ICH9_IGP_AMT),
	EM_DEVICE(ICH9_IGP_C),
	EM_DEVICE(ICH9_IGP_M),
	EM_DEVICE(ICH9_IGP_M_V),
	EM_DEVICE(ICH9_IFE),
	EM_DEVICE(ICH9_IFE_GT),
	EM_DEVICE(ICH9_IFE_G),
	EM_DEVICE(ICH9_BM),

	EM_EMX_DEVICE(82574L),
	EM_EMX_DEVICE(82574LA),

	EM_DEVICE(ICH10_R_BM_LM),
	EM_DEVICE(ICH10_R_BM_LF),
	EM_DEVICE(ICH10_R_BM_V),
	EM_DEVICE(ICH10_D_BM_LM),
	EM_DEVICE(ICH10_D_BM_LF),
	EM_DEVICE(ICH10_D_BM_V),

	EM_DEVICE(PCH_M_HV_LM),
	EM_DEVICE(PCH_M_HV_LC),
	EM_DEVICE(PCH_D_HV_DM),
	EM_DEVICE(PCH_D_HV_DC),

	EM_DEVICE(PCH2_LV_LM),
	EM_DEVICE(PCH2_LV_V),

	EM_EMX_DEVICE(PCH_LPT_I217_LM),
	EM_EMX_DEVICE(PCH_LPT_I217_V),
	EM_EMX_DEVICE(PCH_LPTLP_I218_LM),
	EM_EMX_DEVICE(PCH_LPTLP_I218_V),
	EM_EMX_DEVICE(PCH_I218_LM2),
	EM_EMX_DEVICE(PCH_I218_V2),
	EM_EMX_DEVICE(PCH_I218_LM3),
	EM_EMX_DEVICE(PCH_I218_V3),
	EM_EMX_DEVICE(PCH_SPT_I219_LM),
	EM_EMX_DEVICE(PCH_SPT_I219_V),
	EM_EMX_DEVICE(PCH_SPT_I219_LM2),
	EM_EMX_DEVICE(PCH_SPT_I219_V2),
	EM_EMX_DEVICE(PCH_LBG_I219_LM3),
	EM_EMX_DEVICE(PCH_SPT_I219_LM4),
	EM_EMX_DEVICE(PCH_SPT_I219_V4),
	EM_EMX_DEVICE(PCH_SPT_I219_LM5),
	EM_EMX_DEVICE(PCH_SPT_I219_V5),
	EM_EMX_DEVICE(PCH_CNP_I219_LM6),
	EM_EMX_DEVICE(PCH_CNP_I219_V6),
	EM_EMX_DEVICE(PCH_CNP_I219_LM7),
	EM_EMX_DEVICE(PCH_CNP_I219_V7),
	EM_EMX_DEVICE(PCH_ICP_I219_LM8),
	EM_EMX_DEVICE(PCH_ICP_I219_V8),
	EM_EMX_DEVICE(PCH_ICP_I219_LM9),
	EM_EMX_DEVICE(PCH_ICP_I219_V9),
	EM_EMX_DEVICE(PCH_CMP_I219_LM10),
	EM_EMX_DEVICE(PCH_CMP_I219_V10),
	EM_EMX_DEVICE(PCH_CMP_I219_LM11),
	EM_EMX_DEVICE(PCH_CMP_I219_V11),
	EM_EMX_DEVICE(PCH_CMP_I219_LM12),
	EM_EMX_DEVICE(PCH_CMP_I219_V12),
	EM_EMX_DEVICE(PCH_TGP_I219_LM13),
	EM_EMX_DEVICE(PCH_TGP_I219_V13),
	EM_EMX_DEVICE(PCH_TGP_I219_LM14),
	EM_EMX_DEVICE(PCH_TGP_I219_V14),
	EM_EMX_DEVICE(PCH_TGP_I219_LM15),
	EM_EMX_DEVICE(PCH_TGP_I219_V15),
	EM_EMX_DEVICE(PCH_ADP_I219_LM16),
	EM_EMX_DEVICE(PCH_ADP_I219_V16),
	EM_EMX_DEVICE(PCH_ADP_I219_LM17),
	EM_EMX_DEVICE(PCH_ADP_I219_V17),
	EM_EMX_DEVICE(PCH_MTP_I219_LM18),
	EM_EMX_DEVICE(PCH_MTP_I219_V18),
	EM_EMX_DEVICE(PCH_MTP_I219_LM19),
	EM_EMX_DEVICE(PCH_MTP_I219_V19),

	/* required last entry */
	EM_DEVICE_NULL
};

static int	em_probe(device_t);
static int	em_attach(device_t);
static int	em_detach(device_t);
static int	em_shutdown(device_t);
static int	em_suspend(device_t);
static int	em_resume(device_t);

static void	em_init(void *);
static void	em_stop(struct adapter *);
static int	em_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	em_start(struct ifnet *, struct ifaltq_subque *);
#ifdef IFPOLL_ENABLE
static void	em_npoll(struct ifnet *, struct ifpoll_info *);
static void	em_npoll_compat(struct ifnet *, void *, int);
#endif
static void	em_watchdog(struct ifnet *);
static void	em_media_status(struct ifnet *, struct ifmediareq *);
static int	em_media_change(struct ifnet *);
static void	em_timer(void *);

static void	em_intr(void *);
static void	em_intr_mask(void *);
static void	em_intr_body(struct adapter *, boolean_t);
static void	em_rxeof(struct adapter *, int);
static void	em_txeof(struct adapter *);
static void	em_tx_collect(struct adapter *, boolean_t);
static void	em_tx_purge(struct adapter *);
static void	em_txgc_timer(void *);
static void	em_enable_intr(struct adapter *);
static void	em_disable_intr(struct adapter *);

static int	em_dma_malloc(struct adapter *, bus_size_t,
		    struct em_dma_alloc *);
static void	em_dma_free(struct adapter *, struct em_dma_alloc *);
static void	em_init_tx_ring(struct adapter *);
static int	em_init_rx_ring(struct adapter *);
static int	em_create_tx_ring(struct adapter *);
static int	em_create_rx_ring(struct adapter *);
static void	em_destroy_tx_ring(struct adapter *, int);
static void	em_destroy_rx_ring(struct adapter *, int);
static int	em_newbuf(struct adapter *, int, int);
static int	em_encap(struct adapter *, struct mbuf **, int *, int *);
static void	em_rxcsum(struct adapter *, struct e1000_rx_desc *,
		    struct mbuf *);
static int	em_txcsum(struct adapter *, struct mbuf *,
		    uint32_t *, uint32_t *);
static int	em_tso_pullup(struct adapter *, struct mbuf **);
static int	em_tso_setup(struct adapter *, struct mbuf *,
		    uint32_t *, uint32_t *);

static int	em_get_hw_info(struct adapter *);
static int 	em_is_valid_eaddr(const uint8_t *);
static int	em_alloc_pci_res(struct adapter *);
static void	em_free_pci_res(struct adapter *);
static int	em_reset(struct adapter *);
static void	em_setup_ifp(struct adapter *);
static void	em_init_tx_unit(struct adapter *);
static void	em_init_rx_unit(struct adapter *);
static void	em_update_stats(struct adapter *);
static void	em_set_promisc(struct adapter *);
static void	em_disable_promisc(struct adapter *);
static void	em_set_multi(struct adapter *);
static void	em_update_link_status(struct adapter *);
static void	em_smartspeed(struct adapter *);
static void	em_set_itr(struct adapter *, uint32_t);
static void	em_disable_aspm(struct adapter *);
static void	em_flush_tx_ring(struct adapter *);
static void	em_flush_rx_ring(struct adapter *);
static void	em_flush_txrx_ring(struct adapter *);

/* Hardware workarounds */
static int	em_82547_fifo_workaround(struct adapter *, int);
static void	em_82547_update_fifo_head(struct adapter *, int);
static int	em_82547_tx_fifo_reset(struct adapter *);
static void	em_82547_move_tail(void *);
static void	em_82547_move_tail_serialized(struct adapter *);
static uint32_t	em_82544_fill_desc(bus_addr_t, uint32_t, PDESC_ARRAY);

static void	em_print_debug_info(struct adapter *);
static void	em_print_nvm_info(struct adapter *);
static void	em_print_hw_stats(struct adapter *);

static int	em_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int	em_sysctl_debug_info(SYSCTL_HANDLER_ARGS);
static int	em_sysctl_int_throttle(SYSCTL_HANDLER_ARGS);
static int	em_sysctl_int_tx_nsegs(SYSCTL_HANDLER_ARGS);
static void	em_add_sysctl(struct adapter *adapter);

/* Management and WOL Support */
static void	em_get_mgmt(struct adapter *);
static void	em_rel_mgmt(struct adapter *);
static void	em_get_hw_control(struct adapter *);
static void	em_rel_hw_control(struct adapter *);
static void	em_enable_wol(device_t);

static device_method_t em_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		em_probe),
	DEVMETHOD(device_attach,	em_attach),
	DEVMETHOD(device_detach,	em_detach),
	DEVMETHOD(device_shutdown,	em_shutdown),
	DEVMETHOD(device_suspend,	em_suspend),
	DEVMETHOD(device_resume,	em_resume),
	DEVMETHOD_END
};

static driver_t em_driver = {
	"em",
	em_methods,
	sizeof(struct adapter),
};

static devclass_t em_devclass;

DECLARE_DUMMY_MODULE(if_em);
MODULE_DEPEND(em, ig_hal, 1, 1, 1);
DRIVER_MODULE(if_em, pci, em_driver, em_devclass, NULL, NULL);

/*
 * Tunables
 */
static int	em_int_throttle_ceil = EM_DEFAULT_ITR;
static int	em_rxd = EM_DEFAULT_RXD;
static int	em_txd = EM_DEFAULT_TXD;
static int	em_smart_pwr_down = 0;

/* Controls whether promiscuous also shows bad packets */
static int	em_debug_sbp = FALSE;

static int	em_82573_workaround = 1;
static int	em_msi_enable = 1;

static char	em_flowctrl[IFM_ETH_FC_STRLEN] = IFM_ETH_FC_NONE;

TUNABLE_INT("hw.em.int_throttle_ceil", &em_int_throttle_ceil);
TUNABLE_INT("hw.em.rxd", &em_rxd);
TUNABLE_INT("hw.em.txd", &em_txd);
TUNABLE_INT("hw.em.smart_pwr_down", &em_smart_pwr_down);
TUNABLE_INT("hw.em.sbp", &em_debug_sbp);
TUNABLE_INT("hw.em.82573_workaround", &em_82573_workaround);
TUNABLE_INT("hw.em.msi.enable", &em_msi_enable);
TUNABLE_STR("hw.em.flow_ctrl", em_flowctrl, sizeof(em_flowctrl));

/* Global used in WOL setup with multiport cards */
static int	em_global_quad_port_a = 0;

/* Set this to one to display debug statistics */
static int	em_display_debug_stats = 0;

#if !defined(KTR_IF_EM)
#define KTR_IF_EM	KTR_ALL
#endif
KTR_INFO_MASTER(if_em);
KTR_INFO(KTR_IF_EM, if_em, intr_beg, 0, "intr begin");
KTR_INFO(KTR_IF_EM, if_em, intr_end, 1, "intr end");
KTR_INFO(KTR_IF_EM, if_em, pkt_receive, 4, "rx packet");
KTR_INFO(KTR_IF_EM, if_em, pkt_txqueue, 5, "tx packet");
KTR_INFO(KTR_IF_EM, if_em, pkt_txclean, 6, "tx clean");
#define logif(name)	KTR_LOG(if_em_ ## name)

static __inline void
em_tx_intr(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	em_txeof(adapter);
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static __inline void
em_free_txbuffer(struct adapter *adapter, struct em_buffer *tx_buffer)
{

	KKASSERT(tx_buffer->m_head != NULL);
	KKASSERT(adapter->tx_nmbuf > 0);
	adapter->tx_nmbuf--;

	bus_dmamap_unload(adapter->txtag, tx_buffer->map);
	m_freem(tx_buffer->m_head);
	tx_buffer->m_head = NULL;
}

static __inline void
em_try_txgc(struct adapter *adapter, int dec)
{

	if (adapter->tx_running > 0) {
		adapter->tx_running -= dec;
		if (adapter->tx_running <= 0 && adapter->tx_nmbuf &&
		    adapter->num_tx_desc_avail < adapter->num_tx_desc &&
		    adapter->num_tx_desc_avail + adapter->tx_int_nsegs >
		    adapter->num_tx_desc)
			em_tx_collect(adapter, TRUE);
	}
}

static void
em_txgc_timer(void *xadapter)
{
	struct adapter *adapter = xadapter;
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP))
		return;

	if (!lwkt_serialize_try(ifp->if_serializer))
		goto done;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP | IFF_NPOLLING)) !=
	    (IFF_RUNNING | IFF_UP)) {
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}
	em_try_txgc(adapter, EM_TX_RUNNING_DEC);

	lwkt_serialize_exit(ifp->if_serializer);
done:
	callout_reset(&adapter->tx_gc_timer, 1, em_txgc_timer, adapter);
}

static int
em_probe(device_t dev)
{
	const struct em_vendor_info *ent;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (ent = em_vendor_info_array; ent->desc != NULL; ++ent) {
		if (vid == ent->vendor_id && did == ent->device_id) {
			device_set_desc(dev, ent->desc);
			device_set_async_attach(dev, TRUE);
			return (ent->ret);
		}
	}
	return (ENXIO);
}

static int
em_attach(device_t dev)
{
	struct adapter *adapter = device_get_softc(dev);
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	int tsize, rsize;
	int error = 0;
	int cap;
	uint16_t eeprom_data, device_id, apme_mask;
	driver_intr_t *intr_func;
	char flowctrl[IFM_ETH_FC_STRLEN];

	adapter->dev = adapter->osdep.dev = dev;

	/*
	 * Some versions of I219 only have PCI AF.
	 */
	if (pci_is_pcie(dev) || pci_find_extcap(dev, PCIY_PCIAF, &cap) == 0)
		adapter->flags |= EM_FLAG_GEN2;

	callout_init_mp(&adapter->timer);
	callout_init_mp(&adapter->tx_fifo_timer);
	callout_init_mp(&adapter->tx_gc_timer);

	ifmedia_init(&adapter->media, IFM_IMASK | IFM_ETH_FCMASK,
	    em_media_change, em_media_status);

	/* Determine hardware and mac info */
	error = em_get_hw_info(adapter);
	if (error) {
		device_printf(dev, "Identify hardware failed\n");
		goto fail;
	}

	/* Setup PCI resources */
	error = em_alloc_pci_res(adapter);
	if (error) {
		device_printf(dev, "Allocation of PCI resources failed\n");
		goto fail;
	}

	/*
	 * For ICH8 and family we need to map the flash memory,
	 * and this must happen after the MAC is identified.
	 *
	 * (SPT does not map the flash with a separate BAR)
	 */
	if (adapter->hw.mac.type == e1000_ich8lan ||
	    adapter->hw.mac.type == e1000_ich9lan ||
	    adapter->hw.mac.type == e1000_ich10lan ||
	    adapter->hw.mac.type == e1000_pchlan ||
	    adapter->hw.mac.type == e1000_pch2lan ||
	    adapter->hw.mac.type == e1000_pch_lpt) {
		adapter->flash_rid = EM_BAR_FLASH;

		adapter->flash = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					&adapter->flash_rid, RF_ACTIVE);
		if (adapter->flash == NULL) {
			device_printf(dev, "Mapping of Flash failed\n");
			error = ENXIO;
			goto fail;
		}
		adapter->osdep.flash_bus_space_tag =
		    rman_get_bustag(adapter->flash);
		adapter->osdep.flash_bus_space_handle =
		    rman_get_bushandle(adapter->flash);

		/*
		 * This is used in the shared code
		 * XXX this goof is actually not used.
		 */
		adapter->hw.flash_address = (uint8_t *)adapter->flash;
	} else if (adapter->hw.mac.type >= e1000_pch_spt) {
		/*
		 * In the new SPT device flash is not a seperate BAR,
		 * rather it is also in BAR0, so use the same tag and
		 * an offset handle for the FLASH read/write macros
		 * in the shared code.
		 */
		adapter->osdep.flash_bus_space_tag =
		    adapter->osdep.mem_bus_space_tag;
		adapter->osdep.flash_bus_space_handle =
		    adapter->osdep.mem_bus_space_handle + E1000_FLASH_BASE_ADDR;
	}

	switch (adapter->hw.mac.type) {
	case e1000_82571:
	case e1000_82572:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		/*
		 * Pullup extra 4bytes into the first data segment for
		 * TSO, see:
		 * 82571/82572 specification update errata #7
		 *
		 * Same applies to I217 (and maybe I218 and I219).
		 *
		 * NOTE:
		 * 4bytes instead of 2bytes, which are mentioned in the
		 * errata, are pulled; mainly to keep rest of the data
		 * properly aligned.
		 */
		adapter->flags |= EM_FLAG_TSO_PULLEX;
		/* FALL THROUGH */

	default:
		if (adapter->flags & EM_FLAG_GEN2)
			adapter->flags |= EM_FLAG_TSO;
		break;
	}

	/* Do Shared Code initialization */
	if (e1000_setup_init_funcs(&adapter->hw, TRUE)) {
		device_printf(dev, "Setup of Shared code failed\n");
		error = ENXIO;
		goto fail;
	}

	e1000_get_bus_info(&adapter->hw);

	/*
	 * Validate number of transmit and receive descriptors.  It
	 * must not exceed hardware maximum, and must be multiple
	 * of E1000_DBA_ALIGN.
	 */
	if ((em_txd * sizeof(struct e1000_tx_desc)) % EM_DBA_ALIGN != 0 ||
	    (adapter->hw.mac.type >= e1000_82544 && em_txd > EM_MAX_TXD) ||
	    (adapter->hw.mac.type < e1000_82544 && em_txd > EM_MAX_TXD_82543) ||
	    em_txd < EM_MIN_TXD) {
		if (adapter->hw.mac.type < e1000_82544)
			adapter->num_tx_desc = EM_MAX_TXD_82543;
		else
			adapter->num_tx_desc = EM_DEFAULT_TXD;
		device_printf(dev, "Using %d TX descriptors instead of %d!\n",
		    adapter->num_tx_desc, em_txd);
	} else {
		adapter->num_tx_desc = em_txd;
	}
	if ((em_rxd * sizeof(struct e1000_rx_desc)) % EM_DBA_ALIGN != 0 ||
	    (adapter->hw.mac.type >= e1000_82544 && em_rxd > EM_MAX_RXD) ||
	    (adapter->hw.mac.type < e1000_82544 && em_rxd > EM_MAX_RXD_82543) ||
	    em_rxd < EM_MIN_RXD) {
		if (adapter->hw.mac.type < e1000_82544)
			adapter->num_rx_desc = EM_MAX_RXD_82543;
		else
			adapter->num_rx_desc = EM_DEFAULT_RXD;
		device_printf(dev, "Using %d RX descriptors instead of %d!\n",
		    adapter->num_rx_desc, em_rxd);
	} else {
		adapter->num_rx_desc = em_rxd;
	}

	adapter->hw.mac.autoneg = DO_AUTO_NEG;
	adapter->hw.phy.autoneg_wait_to_complete = FALSE;
	adapter->hw.phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;
	adapter->rx_buffer_len = MCLBYTES;

	/*
	 * Interrupt throttle rate
	 */
	if (em_int_throttle_ceil == 0) {
		adapter->int_throttle_ceil = 0;
	} else {
		int throttle = em_int_throttle_ceil;

		if (throttle < 0)
			throttle = EM_DEFAULT_ITR;

		/* Recalculate the tunable value to get the exact frequency. */
		throttle = 1000000000 / 256 / throttle;

		/* Upper 16bits of ITR is reserved and should be zero */
		if (throttle & 0xffff0000)
			throttle = 1000000000 / 256 / EM_DEFAULT_ITR;

		adapter->int_throttle_ceil = 1000000000 / 256 / throttle;
	}

	e1000_init_script_state_82541(&adapter->hw, TRUE);
	e1000_set_tbi_compatibility_82543(&adapter->hw, TRUE);

	/* Copper options */
	if (adapter->hw.phy.media_type == e1000_media_type_copper) {
		adapter->hw.phy.mdix = AUTO_ALL_MODES;
		adapter->hw.phy.disable_polarity_correction = FALSE;
		adapter->hw.phy.ms_type = EM_MASTER_SLAVE;
	}

	/* Set the frame limits assuming standard ethernet sized frames. */
	adapter->hw.mac.max_frame_size =
	    ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN;
	adapter->min_frame_size = ETH_ZLEN + ETHER_CRC_LEN;

	/* This controls when hardware reports transmit completion status. */
	adapter->hw.mac.report_tx_early = 1;

	/*
	 * Create top level busdma tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT,
			0, &adapter->parent_dtag);
	if (error) {
		device_printf(dev, "could not create top level DMA tag\n");
		goto fail;
	}

	/*
	 * Allocate Transmit Descriptor ring
	 */
	tsize = roundup2(adapter->num_tx_desc * sizeof(struct e1000_tx_desc),
			 EM_DBA_ALIGN);
	error = em_dma_malloc(adapter, tsize, &adapter->txdma);
	if (error) {
		device_printf(dev, "Unable to allocate tx_desc memory\n");
		goto fail;
	}
	adapter->tx_desc_base = adapter->txdma.dma_vaddr;

	/*
	 * Allocate Receive Descriptor ring
	 */
	rsize = roundup2(adapter->num_rx_desc * sizeof(struct e1000_rx_desc),
			 EM_DBA_ALIGN);
	error = em_dma_malloc(adapter, rsize, &adapter->rxdma);
	if (error) {
		device_printf(dev, "Unable to allocate rx_desc memory\n");
		goto fail;
	}
	adapter->rx_desc_base = adapter->rxdma.dma_vaddr;

	/* Allocate multicast array memory. */
	adapter->mta = kmalloc(ETH_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES,
	    M_DEVBUF, M_WAITOK);

	/* Indicate SOL/IDER usage */
	if (e1000_check_reset_block(&adapter->hw)) {
		device_printf(dev,
		    "PHY reset is blocked due to SOL/IDER session.\n");
	}

	/* Disable EEE */
	adapter->hw.dev_spec.ich8lan.eee_disable = 1;

	/*
	 * Start from a known state, this is important in reading the
	 * nvm and mac from that.
	 */
	e1000_reset_hw(&adapter->hw);

	/* Make sure we have a good EEPROM before we read from it */
	if (e1000_validate_nvm_checksum(&adapter->hw) < 0) {
		/*
		 * Some PCI-E parts fail the first check due to
		 * the link being in sleep state, call it again,
		 * if it fails a second time its a real issue.
		 */
		if (e1000_validate_nvm_checksum(&adapter->hw) < 0) {
			device_printf(dev,
			    "The EEPROM Checksum Is Not Valid\n");
			error = EIO;
			goto fail;
		}
	}

	/* Copy the permanent MAC address out of the EEPROM */
	if (e1000_read_mac_addr(&adapter->hw) < 0) {
		device_printf(dev, "EEPROM read error while reading MAC"
		    " address\n");
		error = EIO;
		goto fail;
	}
	if (!em_is_valid_eaddr(adapter->hw.mac.addr)) {
		device_printf(dev, "Invalid MAC address\n");
		error = EIO;
		goto fail;
	}

	/* Disable ULP support */
	e1000_disable_ulp_lpt_lp(&adapter->hw, TRUE);

	/* Allocate transmit descriptors and buffers */
	error = em_create_tx_ring(adapter);
	if (error) {
		device_printf(dev, "Could not setup transmit structures\n");
		goto fail;
	}

	/* Allocate receive descriptors and buffers */
	error = em_create_rx_ring(adapter);
	if (error) {
		device_printf(dev, "Could not setup receive structures\n");
		goto fail;
	}

	/* Manually turn off all interrupts */
	E1000_WRITE_REG(&adapter->hw, E1000_IMC, 0xffffffff);

	/* Determine if we have to control management hardware */
	if (e1000_enable_mng_pass_thru(&adapter->hw))
		adapter->flags |= EM_FLAG_HAS_MGMT;

	/*
	 * Setup Wake-on-Lan
	 */
	apme_mask = EM_EEPROM_APME;
	eeprom_data = 0;
	switch (adapter->hw.mac.type) {
	case e1000_82542:
	case e1000_82543:
		break;

	case e1000_82573:
	case e1000_82583:
		adapter->flags |= EM_FLAG_HAS_AMT;
		/* FALL THROUGH */

	case e1000_82546:
	case e1000_82546_rev_3:
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		if (adapter->hw.bus.func == 1) {
			e1000_read_nvm(&adapter->hw,
			    NVM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);
		} else {
			e1000_read_nvm(&adapter->hw,
			    NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		}
		break;

	case e1000_ich8lan:
	case e1000_ich9lan:
	case e1000_ich10lan:
	case e1000_pchlan:
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		apme_mask = E1000_WUC_APME;
		adapter->flags |= EM_FLAG_HAS_AMT;
		eeprom_data = E1000_READ_REG(&adapter->hw, E1000_WUC);
		break;

	default:
		e1000_read_nvm(&adapter->hw,
		    NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		break;
	}
	if (eeprom_data & apme_mask)
		adapter->wol = E1000_WUFC_MAG | E1000_WUFC_MC;

	/*
         * We have the eeprom settings, now apply the special cases
         * where the eeprom may be wrong or the board won't support
         * wake on lan on a particular port
	 */
	device_id = pci_get_device(dev);
        switch (device_id) {
	case E1000_DEV_ID_82546GB_PCIE:
		adapter->wol = 0;
		break;

	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546GB_FIBER:
	case E1000_DEV_ID_82571EB_FIBER:
		/*
		 * Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting
		 */
		if (E1000_READ_REG(&adapter->hw, E1000_STATUS) &
		    E1000_STATUS_FUNC_1)
			adapter->wol = 0;
		break;

	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
	case E1000_DEV_ID_82571EB_QUAD_COPPER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_COPPER_LP:
                /* if quad port adapter, disable WoL on all but port A */
		if (em_global_quad_port_a != 0)
			adapter->wol = 0;
		/* Reset for multiple quad port adapters */
		if (++em_global_quad_port_a == 4)
			em_global_quad_port_a = 0;
                break;
	}

	/* XXX disable wol */
	adapter->wol = 0;

	/* Setup flow control. */
	device_getenv_string(dev, "flow_ctrl", flowctrl, sizeof(flowctrl),
	    em_flowctrl);
	adapter->ifm_flowctrl = ifmedia_str2ethfc(flowctrl);
	if (adapter->hw.mac.type == e1000_pchlan) {
		/* Only PAUSE reception is supported on PCH */
		adapter->ifm_flowctrl &= ~IFM_ETH_TXPAUSE;
	}

	/* Setup OS specific network interface */
	em_setup_ifp(adapter);

	/* Add sysctl tree, must after em_setup_ifp() */
	em_add_sysctl(adapter);

#ifdef IFPOLL_ENABLE
	/* Polling setup */
	ifpoll_compat_setup(&adapter->npoll,
	    device_get_sysctl_ctx(dev), device_get_sysctl_tree(dev),
	    device_get_unit(dev), ifp->if_serializer);
#endif

	/* Reset the hardware */
	error = em_reset(adapter);
	if (error) {
		/*
		 * Some 82573 parts fail the first reset, call it again,
		 * if it fails a second time its a real issue.
		 */
		error = em_reset(adapter);
		if (error) {
			device_printf(dev, "Unable to reset the hardware\n");
			ether_ifdetach(ifp);
			goto fail;
		}
	}

	/* Initialize statistics */
	em_update_stats(adapter);

	adapter->hw.mac.get_link_status = 1;
	em_update_link_status(adapter);

	/* Do we need workaround for 82544 PCI-X adapter? */
	if (adapter->hw.bus.type == e1000_bus_type_pcix &&
	    adapter->hw.mac.type == e1000_82544)
		adapter->pcix_82544 = TRUE;
	else
		adapter->pcix_82544 = FALSE;

	if (adapter->pcix_82544) {
		/*
		 * 82544 on PCI-X may split one TX segment
		 * into two TX descs, so we double its number
		 * of spare TX desc here.
		 */
		adapter->spare_tx_desc = 2 * EM_TX_SPARE;
	} else {
		adapter->spare_tx_desc = EM_TX_SPARE;
	}
	if (adapter->flags & EM_FLAG_TSO)
		adapter->spare_tx_desc = EM_TX_SPARE_TSO;
	adapter->tx_wreg_nsegs = EM_DEFAULT_TXWREG;

	/*
	 * Keep following relationship between spare_tx_desc, oact_tx_desc
	 * and tx_int_nsegs:
	 * (spare_tx_desc + EM_TX_RESERVED) <=
	 * oact_tx_desc <= EM_TX_OACTIVE_MAX <= tx_int_nsegs
	 */
	adapter->oact_tx_desc = adapter->num_tx_desc / 8;
	if (adapter->oact_tx_desc > EM_TX_OACTIVE_MAX)
		adapter->oact_tx_desc = EM_TX_OACTIVE_MAX;
	if (adapter->oact_tx_desc < adapter->spare_tx_desc + EM_TX_RESERVED)
		adapter->oact_tx_desc = adapter->spare_tx_desc + EM_TX_RESERVED;

	adapter->tx_int_nsegs = adapter->num_tx_desc / 16;
	if (adapter->tx_int_nsegs < adapter->oact_tx_desc)
		adapter->tx_int_nsegs = adapter->oact_tx_desc;

	/* Non-AMT based hardware can now take control from firmware */
	if ((adapter->flags & (EM_FLAG_HAS_MGMT | EM_FLAG_HAS_AMT)) ==
	    EM_FLAG_HAS_MGMT && adapter->hw.mac.type >= e1000_82571)
		em_get_hw_control(adapter);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(adapter->intr_res));

	/*
	 * Missing Interrupt Following ICR read:
	 *
	 * 82571/82572 specification update errata #76
	 * 82573 specification update errata #31
	 * 82574 specification update errata #12
	 * 82583 specification update errata #4
	 */
	intr_func = em_intr;
	if ((adapter->flags & EM_FLAG_SHARED_INTR) &&
	    (adapter->hw.mac.type == e1000_82571 ||
	     adapter->hw.mac.type == e1000_82572 ||
	     adapter->hw.mac.type == e1000_82573 ||
	     adapter->hw.mac.type == e1000_82574 ||
	     adapter->hw.mac.type == e1000_82583))
		intr_func = em_intr_mask;

	error = bus_setup_intr(dev, adapter->intr_res, INTR_MPSAFE,
			       intr_func, adapter, &adapter->intr_tag,
			       ifp->if_serializer);
	if (error) {
		device_printf(dev, "Failed to register interrupt handler");
		ether_ifdetach(ifp);
		goto fail;
	}
	return (0);
fail:
	em_detach(dev);
	return (error);
}

static int
em_detach(device_t dev)
{
	struct adapter *adapter = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &adapter->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);

		em_stop(adapter);

		e1000_phy_hw_reset(&adapter->hw);

		em_rel_mgmt(adapter);
		em_rel_hw_control(adapter);

		if (adapter->wol) {
			E1000_WRITE_REG(&adapter->hw, E1000_WUC,
					E1000_WUC_PME_EN);
			E1000_WRITE_REG(&adapter->hw, E1000_WUFC, adapter->wol);
			em_enable_wol(dev);
		}

		bus_teardown_intr(dev, adapter->intr_res, adapter->intr_tag);

		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	} else if (adapter->memory != NULL) {
		em_rel_hw_control(adapter);
	}

	ifmedia_removeall(&adapter->media);
	bus_generic_detach(dev);

	em_free_pci_res(adapter);

	em_destroy_tx_ring(adapter, adapter->num_tx_desc);
	em_destroy_rx_ring(adapter, adapter->num_rx_desc);

	/* Free Transmit Descriptor ring */
	if (adapter->tx_desc_base)
		em_dma_free(adapter, &adapter->txdma);

	/* Free Receive Descriptor ring */
	if (adapter->rx_desc_base)
		em_dma_free(adapter, &adapter->rxdma);

	/* Free top level busdma tag */
	if (adapter->parent_dtag != NULL)
		bus_dma_tag_destroy(adapter->parent_dtag);

	if (adapter->mta != NULL)
		kfree(adapter->mta, M_DEVBUF);

	return (0);
}

static int
em_shutdown(device_t dev)
{
	return em_suspend(dev);
}

static int
em_suspend(device_t dev)
{
	struct adapter *adapter = device_get_softc(dev);
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	em_stop(adapter);

	em_rel_mgmt(adapter);
	em_rel_hw_control(adapter);

	if (adapter->wol) {
		E1000_WRITE_REG(&adapter->hw, E1000_WUC, E1000_WUC_PME_EN);
		E1000_WRITE_REG(&adapter->hw, E1000_WUFC, adapter->wol);
		em_enable_wol(dev);
	}

	lwkt_serialize_exit(ifp->if_serializer);

	return bus_generic_suspend(dev);
}

static int
em_resume(device_t dev)
{
	struct adapter *adapter = device_get_softc(dev);
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (adapter->hw.mac.type == e1000_pch2lan)
		e1000_resume_workarounds_pchlan(&adapter->hw);

	em_init(adapter);
	em_get_mgmt(adapter);
	if_devstart(ifp);

	lwkt_serialize_exit(ifp->if_serializer);

	return bus_generic_resume(dev);
}

static void
em_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct adapter *adapter = ifp->if_softc;
	struct mbuf *m_head;
	int idx = -1, nsegs = 0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	if (!adapter->link_active) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	while (!ifq_is_empty(&ifp->if_snd)) {
		/* Now do we at least have a minimal? */
		if (EM_IS_OACTIVE(adapter)) {
			em_tx_collect(adapter, FALSE);
			if (EM_IS_OACTIVE(adapter)) {
				ifq_set_oactive(&ifp->if_snd);
				adapter->no_tx_desc_avail1++;
				break;
			}
		}

		logif(pkt_txqueue);
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (em_encap(adapter, &m_head, &nsegs, &idx)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			em_tx_collect(adapter, FALSE);
			continue;
		}

		/*
		 * TX interrupt are aggressively aggregated, so increasing
		 * opackets at TX interrupt time will make the opackets
		 * statistics vastly inaccurate; we do the opackets increment
		 * now.
		 */
		IFNET_STAT_INC(ifp, opackets, 1);

		if (nsegs >= adapter->tx_wreg_nsegs && idx >= 0) {
			E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), idx);
			nsegs = 0;
			idx = -1;
		}

		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, m_head);

		/* Set timeout in case hardware has problems transmitting. */
		ifp->if_timer = EM_TX_TIMEOUT;
	}
	if (idx >= 0)
		E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), idx);
	adapter->tx_running = EM_TX_RUNNING;
}

static int
em_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct adapter *adapter = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	uint16_t eeprom_data = 0;
	int max_frame_size, mask, reinit;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (command) {
	case SIOCSIFMTU:
		switch (adapter->hw.mac.type) {
		case e1000_82573:
			/*
			 * 82573 only supports jumbo frames
			 * if ASPM is disabled.
			 */
			e1000_read_nvm(&adapter->hw,
			    NVM_INIT_3GIO_3, 1, &eeprom_data);
			if (eeprom_data & NVM_WORD1A_ASPM_MASK) {
				max_frame_size = ETHER_MAX_LEN;
				break;
			}
			/* FALL THROUGH */

		/* Limit Jumbo Frame size */
		case e1000_82571:
		case e1000_82572:
		case e1000_ich9lan:
		case e1000_ich10lan:
		case e1000_pch2lan:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
		case e1000_82574:
		case e1000_82583:
		case e1000_80003es2lan:
			max_frame_size = 9234;
			break;

		case e1000_pchlan:
			max_frame_size = 4096;
			break;

		/* Adapters that do not support jumbo frames */
		case e1000_82542:
		case e1000_ich8lan:
			max_frame_size = ETHER_MAX_LEN;
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
		adapter->hw.mac.max_frame_size =
		    ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

		if (ifp->if_flags & IFF_RUNNING)
			em_init(adapter);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING)) {
				if ((ifp->if_flags ^ adapter->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					em_disable_promisc(adapter);
					em_set_promisc(adapter);
				}
			} else {
				em_init(adapter);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			em_stop(adapter);
		}
		adapter->if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING) {
			em_disable_intr(adapter);
			em_set_multi(adapter);
			if (adapter->hw.mac.type == e1000_82542 &&
			    adapter->hw.revision_id == E1000_REVISION_2)
				em_init_rx_unit(adapter);
#ifdef IFPOLL_ENABLE
			if (!(ifp->if_flags & IFF_NPOLLING))
#endif
				em_enable_intr(adapter);
		}
		break;

	case SIOCSIFMEDIA:
		/* Check SOL/IDER usage */
		if (e1000_check_reset_block(&adapter->hw)) {
			device_printf(adapter->dev, "Media change is"
			    " blocked due to SOL/IDER session.\n");
			break;
		}
		/* FALL THROUGH */

	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &adapter->media, command);
		break;

	case SIOCSIFCAP:
		reinit = 0;
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_RXCSUM) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reinit = 1;
		}
		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= EM_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~EM_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= IFCAP_TSO;
			if (ifp->if_capenable & IFCAP_TSO)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if (mask & IFCAP_VLAN_HWTAGGING) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			reinit = 1;
		}
		if (reinit && (ifp->if_flags & IFF_RUNNING))
			em_init(adapter);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static void
em_watchdog(struct ifnet *ifp)
{
	struct adapter *adapter = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * The timer is set to 5 every time start queues a packet.
	 * Then txeof keeps resetting it as long as it cleans at
	 * least one descriptor.
	 * Finally, anytime all descriptors are clean the timer is
	 * set to 0.
	 */

	if (E1000_READ_REG(&adapter->hw, E1000_TDT(0)) ==
	    E1000_READ_REG(&adapter->hw, E1000_TDH(0))) {
		/*
		 * If we reach here, all TX jobs are completed and
		 * the TX engine should have been idled for some time.
		 * We don't need to call if_devstart() here.
		 */
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		return;
	}

	/*
	 * If we are in this routine because of pause frames, then
	 * don't reset the hardware.
	 */
	if (E1000_READ_REG(&adapter->hw, E1000_STATUS) &
	    E1000_STATUS_TXOFF) {
		ifp->if_timer = EM_TX_TIMEOUT;
		return;
	}

	if (e1000_check_for_link(&adapter->hw) == 0)
		if_printf(ifp, "watchdog timeout -- resetting\n");

	IFNET_STAT_INC(ifp, oerrors, 1);
	adapter->watchdog_events++;

	em_init(adapter);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
em_init(void *xsc)
{
	struct adapter *adapter = xsc;
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	device_t dev = adapter->dev;

	ASSERT_SERIALIZED(ifp->if_serializer);

	em_stop(adapter);

	/* Get the latest mac address, User can use a LAA */
        bcopy(IF_LLADDR(ifp), adapter->hw.mac.addr, ETHER_ADDR_LEN);

	/* Put the address into the Receive Address Array */
	e1000_rar_set(&adapter->hw, adapter->hw.mac.addr, 0);

	/*
	 * With the 82571 adapter, RAR[0] may be overwritten
	 * when the other port is reset, we make a duplicate
	 * in RAR[14] for that eventuality, this assures
	 * the interface continues to function.
	 */
	if (adapter->hw.mac.type == e1000_82571) {
		e1000_set_laa_state_82571(&adapter->hw, TRUE);
		e1000_rar_set(&adapter->hw, adapter->hw.mac.addr,
		    E1000_RAR_ENTRIES - 1);
	}

	/* Reset the hardware */
	if (em_reset(adapter)) {
		device_printf(dev, "Unable to reset the hardware\n");
		/* XXX em_stop()? */
		return;
	}
	em_update_link_status(adapter);

	/* Setup VLAN support, basic and offload if available */
	E1000_WRITE_REG(&adapter->hw, E1000_VET, ETHERTYPE_VLAN);

	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) {
		uint32_t ctrl;

		ctrl = E1000_READ_REG(&adapter->hw, E1000_CTRL);
		ctrl |= E1000_CTRL_VME;
		E1000_WRITE_REG(&adapter->hw, E1000_CTRL, ctrl);
	}

	/* Configure for OS presence */
	em_get_mgmt(adapter);

	/* Prepare transmit descriptors and buffers */
	em_init_tx_ring(adapter);
	em_init_tx_unit(adapter);

	/* Setup Multicast table */
	em_set_multi(adapter);

	/* Prepare receive descriptors and buffers */
	if (em_init_rx_ring(adapter)) {
		device_printf(dev, "Could not setup receive structures\n");
		em_stop(adapter);
		return;
	}
	em_init_rx_unit(adapter);

	/* Don't lose promiscuous settings */
	em_set_promisc(adapter);

	/* Reset hardware counters */
	e1000_clear_hw_cntrs_base_generic(&adapter->hw);

	/* MSI/X configuration for 82574 */
	if (adapter->hw.mac.type == e1000_82574) {
		int tmp;

		tmp = E1000_READ_REG(&adapter->hw, E1000_CTRL_EXT);
		tmp |= E1000_CTRL_EXT_PBA_CLR;
		E1000_WRITE_REG(&adapter->hw, E1000_CTRL_EXT, tmp);
		/*
		 * XXX MSIX
		 * Set the IVAR - interrupt vector routing.
		 * Each nibble represents a vector, high bit
		 * is enable, other 3 bits are the MSIX table
		 * entry, we map RXQ0 to 0, TXQ0 to 1, and
		 * Link (other) to 2, hence the magic number.
		 */
		E1000_WRITE_REG(&adapter->hw, E1000_IVAR, 0x800A0908);
	}

#ifdef IFPOLL_ENABLE
	/*
	 * Only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (ifp->if_flags & IFF_NPOLLING)
		em_disable_intr(adapter);
	else
#endif /* IFPOLL_ENABLE */
		em_enable_intr(adapter);

	/* AMT based hardware can now take control from firmware */
	if ((adapter->flags & (EM_FLAG_HAS_MGMT | EM_FLAG_HAS_AMT)) ==
	    (EM_FLAG_HAS_MGMT | EM_FLAG_HAS_AMT) &&
	    adapter->hw.mac.type >= e1000_82571)
		em_get_hw_control(adapter);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

#ifdef IFPOLL_ENABLE
	if ((ifp->if_flags & IFF_NPOLLING) == 0)
#endif
	{
		callout_reset_bycpu(&adapter->tx_gc_timer, 1,
		    em_txgc_timer, adapter,
		    rman_get_cpuid(adapter->intr_res));
	}
	callout_reset(&adapter->timer, hz, em_timer, adapter);
}

#ifdef IFPOLL_ENABLE

static void
em_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct adapter *adapter = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (adapter->npoll.ifpc_stcount-- == 0) {
		uint32_t reg_icr;

		adapter->npoll.ifpc_stcount = adapter->npoll.ifpc_stfrac;

		reg_icr = E1000_READ_REG(&adapter->hw, E1000_ICR);
		if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
			callout_stop(&adapter->timer);
			adapter->hw.mac.get_link_status = 1;
			em_update_link_status(adapter);
			callout_reset(&adapter->timer, hz, em_timer, adapter);
		}
	}

	em_rxeof(adapter, count);

	em_tx_intr(adapter);
	em_try_txgc(adapter, 1);
}

static void
em_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct adapter *adapter = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = adapter->npoll.ifpc_cpuid;

                info->ifpi_rx[cpuid].poll_func = em_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(adapter->intr_res));
	}
	if (ifp->if_flags & IFF_RUNNING)
		em_init(adapter);
}

#endif /* IFPOLL_ENABLE */

static void
em_intr(void *xsc)
{
	em_intr_body(xsc, TRUE);
}

static void
em_intr_body(struct adapter *adapter, boolean_t chk_asserted)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	uint32_t reg_icr;

	logif(intr_beg);
	ASSERT_SERIALIZED(ifp->if_serializer);

	reg_icr = E1000_READ_REG(&adapter->hw, E1000_ICR);

	if (chk_asserted &&
	    ((adapter->hw.mac.type >= e1000_82571 &&
	      (reg_icr & E1000_ICR_INT_ASSERTED) == 0) ||
	     reg_icr == 0)) {
		logif(intr_end);
		return;
	}

	/*
	 * XXX: some laptops trigger several spurious interrupts
	 * on em(4) when in the resume cycle. The ICR register
	 * reports all-ones value in this case. Processing such
	 * interrupts would lead to a freeze. I don't know why.
	 */
	if (reg_icr == 0xffffffff) {
		logif(intr_end);
		return;
	}

	if (ifp->if_flags & IFF_RUNNING) {
		if (reg_icr &
		    (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO))
			em_rxeof(adapter, -1);
		if (reg_icr & E1000_ICR_TXDW)
			em_tx_intr(adapter);
	}

	/* Link status change */
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		callout_stop(&adapter->timer);
		adapter->hw.mac.get_link_status = 1;
		em_update_link_status(adapter);

		/* Deal with TX cruft when link lost */
		em_tx_purge(adapter);

		callout_reset(&adapter->timer, hz, em_timer, adapter);
	}

	if (reg_icr & E1000_ICR_RXO)
		adapter->rx_overruns++;

	logif(intr_end);
}

static void
em_intr_mask(void *xsc)
{
	struct adapter *adapter = xsc;

	E1000_WRITE_REG(&adapter->hw, E1000_IMC, 0xffffffff);
	/*
	 * NOTE:
	 * ICR.INT_ASSERTED bit will never be set if IMS is 0,
	 * so don't check it.
	 */
	em_intr_body(adapter, FALSE);
	E1000_WRITE_REG(&adapter->hw, E1000_IMS, IMS_ENABLE_MASK);
}

static void
em_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct adapter *adapter = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	em_update_link_status(adapter);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!adapter->link_active) {
		if (adapter->hw.mac.autoneg)
			ifmr->ifm_active |= IFM_NONE;
		else
			ifmr->ifm_active = adapter->media.ifm_media;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;
	if (adapter->ifm_flowctrl & IFM_ETH_FORCEPAUSE)
		ifmr->ifm_active |= adapter->ifm_flowctrl;

	if (adapter->hw.phy.media_type == e1000_media_type_fiber ||
	    adapter->hw.phy.media_type == e1000_media_type_internal_serdes) {
		u_char fiber_type = IFM_1000_SX;

		if (adapter->hw.mac.type == e1000_82545)
			fiber_type = IFM_1000_LX;
		ifmr->ifm_active |= fiber_type | IFM_FDX;
	} else {
		switch (adapter->link_speed) {
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
		if (adapter->link_duplex == FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
	}
	if (ifmr->ifm_active & IFM_FDX) {
		ifmr->ifm_active |=
		    e1000_fc2ifmedia(adapter->hw.fc.current_mode);
	}
}

static int
em_media_change(struct ifnet *ifp)
{
	struct adapter *adapter = ifp->if_softc;
	struct ifmedia *ifm = &adapter->media;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (adapter->hw.mac.type == e1000_pchlan &&
	    (IFM_OPTIONS(ifm->ifm_media) & IFM_ETH_TXPAUSE)) {
		if (bootverbose)
			if_printf(ifp, "TX PAUSE is not supported on PCH\n");
		return EINVAL;
	}

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		adapter->hw.mac.autoneg = DO_AUTO_NEG;
		adapter->hw.phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;
		break;

	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_T:
		adapter->hw.mac.autoneg = DO_AUTO_NEG;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;

	case IFM_100_TX:
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_FDX) {
			adapter->hw.mac.forced_speed_duplex = ADVERTISE_100_FULL;
		} else {
			if (IFM_OPTIONS(ifm->ifm_media) &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
				if (bootverbose) {
					if_printf(ifp, "Flow control is not "
					    "allowed for half-duplex\n");
				}
				return EINVAL;
			}
			adapter->hw.mac.forced_speed_duplex = ADVERTISE_100_HALF;
		}
		adapter->hw.mac.autoneg = FALSE;
		adapter->hw.phy.autoneg_advertised = 0;
		break;

	case IFM_10_T:
		if (IFM_OPTIONS(ifm->ifm_media) & IFM_FDX) {
			adapter->hw.mac.forced_speed_duplex = ADVERTISE_10_FULL;
		} else {
			if (IFM_OPTIONS(ifm->ifm_media) &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) {
				if (bootverbose) {
					if_printf(ifp, "Flow control is not "
					    "allowed for half-duplex\n");
				}
				return EINVAL;
			}
			adapter->hw.mac.forced_speed_duplex = ADVERTISE_10_HALF;
		}
		adapter->hw.mac.autoneg = FALSE;
		adapter->hw.phy.autoneg_advertised = 0;
		break;

	default:
		if (bootverbose) {
			if_printf(ifp, "Unsupported media type %d\n",
			    IFM_SUBTYPE(ifm->ifm_media));
		}
		return EINVAL;
	}
	adapter->ifm_flowctrl = ifm->ifm_media & IFM_ETH_FCMASK;

	if (ifp->if_flags & IFF_RUNNING)
		em_init(adapter);

	return (0);
}

static int
em_encap(struct adapter *adapter, struct mbuf **m_headp,
    int *segs_used, int *idx)
{
	bus_dma_segment_t segs[EM_MAX_SCATTER];
	bus_dmamap_t map;
	struct em_buffer *tx_buffer, *tx_buffer_mapped;
	struct e1000_tx_desc *ctxd = NULL;
	struct mbuf *m_head = *m_headp;
	uint32_t txd_upper, txd_lower, txd_used, cmd = 0;
	int maxsegs, nsegs, i, j, first, last = 0, error;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		error = em_tso_pullup(adapter, m_headp);
		if (error)
			return error;
		m_head = *m_headp;
	}

	txd_upper = txd_lower = 0;
	txd_used = 0;

	/*
	 * Capture the first descriptor index, this descriptor
	 * will have the index of the EOP which is the only one
	 * that now gets a DONE bit writeback.
	 */
	first = adapter->next_avail_tx_desc;
	tx_buffer = &adapter->tx_buffer_area[first];
	tx_buffer_mapped = tx_buffer;
	map = tx_buffer->map;

	maxsegs = adapter->num_tx_desc_avail - EM_TX_RESERVED;
	KASSERT(maxsegs >= adapter->spare_tx_desc,
		("not enough spare TX desc"));
	if (adapter->pcix_82544) {
		/* Half it; see the comment in em_attach() */
		maxsegs >>= 1;
	}
	if (maxsegs > EM_MAX_SCATTER)
		maxsegs = EM_MAX_SCATTER;

	error = bus_dmamap_load_mbuf_defrag(adapter->txtag, map, m_headp,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		if (error == ENOBUFS)
			adapter->mbuf_alloc_failed++;
		else
			adapter->no_tx_dma_setup++;

		m_freem(*m_headp);
		*m_headp = NULL;
		return error;
	}
        bus_dmamap_sync(adapter->txtag, map, BUS_DMASYNC_PREWRITE);

	m_head = *m_headp;
	adapter->tx_nsegs += nsegs;
	*segs_used += nsegs;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		/* TSO will consume one TX desc */
		i = em_tso_setup(adapter, m_head, &txd_upper, &txd_lower);
		adapter->tx_nsegs += i;
		*segs_used += i;
	} else if (m_head->m_pkthdr.csum_flags & EM_CSUM_FEATURES) {
		/* TX csum offloading will consume one TX desc */
		i = em_txcsum(adapter, m_head, &txd_upper, &txd_lower);
		adapter->tx_nsegs += i;
		*segs_used += i;
	}

        /* Handle VLAN tag */
	if (m_head->m_flags & M_VLANTAG) {
		/* Set the vlan id. */
		txd_upper |= (htole16(m_head->m_pkthdr.ether_vlantag) << 16);
		/* Tell hardware to add tag */
		txd_lower |= htole32(E1000_TXD_CMD_VLE);
	}

	i = adapter->next_avail_tx_desc;

	/* Set up our transmit descriptors */
	for (j = 0; j < nsegs; j++) {
		/* If adapter is 82544 and on PCIX bus */
		if(adapter->pcix_82544) {
			DESC_ARRAY desc_array;
			uint32_t array_elements, counter;

			/*
			 * Check the Address and Length combination and
			 * split the data accordingly
			 */
			array_elements = em_82544_fill_desc(segs[j].ds_addr,
						segs[j].ds_len, &desc_array);
			for (counter = 0; counter < array_elements; counter++) {
				KKASSERT(txd_used < adapter->num_tx_desc_avail);

				tx_buffer = &adapter->tx_buffer_area[i];
				ctxd = &adapter->tx_desc_base[i];

				ctxd->buffer_addr = htole64(
				    desc_array.descriptor[counter].address);
				ctxd->lower.data = htole32(
				    E1000_TXD_CMD_IFCS | txd_lower |
				    desc_array.descriptor[counter].length);
				ctxd->upper.data = htole32(txd_upper);

				last = i;
				if (++i == adapter->num_tx_desc)
					i = 0;

				txd_used++;
                        }
		} else {
			tx_buffer = &adapter->tx_buffer_area[i];
			ctxd = &adapter->tx_desc_base[i];

			ctxd->buffer_addr = htole64(segs[j].ds_addr);
			ctxd->lower.data = htole32(E1000_TXD_CMD_IFCS |
						   txd_lower | segs[j].ds_len);
			ctxd->upper.data = htole32(txd_upper);

			last = i;
			if (++i == adapter->num_tx_desc)
				i = 0;
		}
	}

	adapter->next_avail_tx_desc = i;
	if (adapter->pcix_82544) {
		KKASSERT(adapter->num_tx_desc_avail > txd_used);
		adapter->num_tx_desc_avail -= txd_used;
	} else {
		KKASSERT(adapter->num_tx_desc_avail > nsegs);
		adapter->num_tx_desc_avail -= nsegs;
	}
	adapter->tx_nmbuf++;

	tx_buffer->m_head = m_head;
	tx_buffer_mapped->map = tx_buffer->map;
	tx_buffer->map = map;

	if (adapter->tx_nsegs >= adapter->tx_int_nsegs) {
		adapter->tx_nsegs = 0;

		/*
		 * Report Status (RS) is turned on
		 * every tx_int_nsegs descriptors.
		 */
		cmd = E1000_TXD_CMD_RS;

		/*
		 * Keep track of the descriptor, which will
		 * be written back by hardware.
		 */
		adapter->tx_dd[adapter->tx_dd_tail] = last;
		EM_INC_TXDD_IDX(adapter->tx_dd_tail);
		KKASSERT(adapter->tx_dd_tail != adapter->tx_dd_head);
	}

	/*
	 * Last Descriptor of Packet needs End Of Packet (EOP)
	 */
	ctxd->lower.data |= htole32(E1000_TXD_CMD_EOP | cmd);

	if (adapter->hw.mac.type == e1000_82547) {
		/*
		 * Advance the Transmit Descriptor Tail (TDT), this tells the
		 * E1000 that this frame is available to transmit.
		 */
		if (adapter->link_duplex == HALF_DUPLEX) {
			em_82547_move_tail_serialized(adapter);
		} else {
			E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), i);
			em_82547_update_fifo_head(adapter,
			    m_head->m_pkthdr.len);
		}
	} else {
		/*
		 * Defer TDT updating, until enough descriptors are setup
		 */
		*idx = i;
	}
	return (0);
}

/*
 * 82547 workaround to avoid controller hang in half-duplex environment.
 * The workaround is to avoid queuing a large packet that would span
 * the internal Tx FIFO ring boundary.  We need to reset the FIFO pointers
 * in this case.  We do that only when FIFO is quiescent.
 */
static void
em_82547_move_tail_serialized(struct adapter *adapter)
{
	struct e1000_tx_desc *tx_desc;
	uint16_t hw_tdt, sw_tdt, length = 0;
	bool eop = 0;

	ASSERT_SERIALIZED(adapter->arpcom.ac_if.if_serializer);

	hw_tdt = E1000_READ_REG(&adapter->hw, E1000_TDT(0));
	sw_tdt = adapter->next_avail_tx_desc;

	while (hw_tdt != sw_tdt) {
		tx_desc = &adapter->tx_desc_base[hw_tdt];
		length += tx_desc->lower.flags.length;
		eop = tx_desc->lower.data & E1000_TXD_CMD_EOP;
		if (++hw_tdt == adapter->num_tx_desc)
			hw_tdt = 0;

		if (eop) {
			if (em_82547_fifo_workaround(adapter, length)) {
				adapter->tx_fifo_wrk_cnt++;
				callout_reset(&adapter->tx_fifo_timer, 1,
					em_82547_move_tail, adapter);
				break;
			}
			E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), hw_tdt);
			em_82547_update_fifo_head(adapter, length);
			length = 0;
		}
	}
}

static void
em_82547_move_tail(void *xsc)
{
	struct adapter *adapter = xsc;
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	em_82547_move_tail_serialized(adapter);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
em_82547_fifo_workaround(struct adapter *adapter, int len)
{	
	int fifo_space, fifo_pkt_len;

	fifo_pkt_len = roundup2(len + EM_FIFO_HDR, EM_FIFO_HDR);

	if (adapter->link_duplex == HALF_DUPLEX) {
		fifo_space = adapter->tx_fifo_size - adapter->tx_fifo_head;

		if (fifo_pkt_len >= (EM_82547_PKT_THRESH + fifo_space)) {
			if (em_82547_tx_fifo_reset(adapter))
				return (0);
			else
				return (1);
		}
	}
	return (0);
}

static void
em_82547_update_fifo_head(struct adapter *adapter, int len)
{
	int fifo_pkt_len = roundup2(len + EM_FIFO_HDR, EM_FIFO_HDR);

	/* tx_fifo_head is always 16 byte aligned */
	adapter->tx_fifo_head += fifo_pkt_len;
	if (adapter->tx_fifo_head >= adapter->tx_fifo_size)
		adapter->tx_fifo_head -= adapter->tx_fifo_size;
}

static int
em_82547_tx_fifo_reset(struct adapter *adapter)
{
	uint32_t tctl;

	if ((E1000_READ_REG(&adapter->hw, E1000_TDT(0)) ==
	     E1000_READ_REG(&adapter->hw, E1000_TDH(0))) &&
	    (E1000_READ_REG(&adapter->hw, E1000_TDFT) == 
	     E1000_READ_REG(&adapter->hw, E1000_TDFH)) &&
	    (E1000_READ_REG(&adapter->hw, E1000_TDFTS) ==
	     E1000_READ_REG(&adapter->hw, E1000_TDFHS)) &&
	    (E1000_READ_REG(&adapter->hw, E1000_TDFPC) == 0)) {
		/* Disable TX unit */
		tctl = E1000_READ_REG(&adapter->hw, E1000_TCTL);
		E1000_WRITE_REG(&adapter->hw, E1000_TCTL,
		    tctl & ~E1000_TCTL_EN);

		/* Reset FIFO pointers */
		E1000_WRITE_REG(&adapter->hw, E1000_TDFT,
		    adapter->tx_head_addr);
		E1000_WRITE_REG(&adapter->hw, E1000_TDFH,
		    adapter->tx_head_addr);
		E1000_WRITE_REG(&adapter->hw, E1000_TDFTS,
		    adapter->tx_head_addr);
		E1000_WRITE_REG(&adapter->hw, E1000_TDFHS,
		    adapter->tx_head_addr);

		/* Re-enable TX unit */
		E1000_WRITE_REG(&adapter->hw, E1000_TCTL, tctl);
		E1000_WRITE_FLUSH(&adapter->hw);

		adapter->tx_fifo_head = 0;
		adapter->tx_fifo_reset_cnt++;

		return (TRUE);
	} else {
		return (FALSE);
	}
}

static void
em_set_promisc(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	uint32_t reg_rctl;

	reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);

	if (ifp->if_flags & IFF_PROMISC) {
		reg_rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		/* Turn this on if you want to see bad packets */
		if (em_debug_sbp)
			reg_rctl |= E1000_RCTL_SBP;
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg_rctl |= E1000_RCTL_MPE;
		reg_rctl &= ~E1000_RCTL_UPE;
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
	}
}

static void
em_disable_promisc(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	uint32_t reg_rctl;
	int mcnt = 0;

	reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
	reg_rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_SBP);

	if (ifp->if_flags & IFF_ALLMULTI) {
		mcnt = MAX_NUM_MULTICAST_ADDRESSES;
	} else {
		const struct ifmultiaddr *ifma;

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
		reg_rctl &= ~E1000_RCTL_MPE;

	E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
}

static void
em_set_multi(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t reg_rctl = 0;
	uint8_t *mta;
	int mcnt = 0;

	mta = adapter->mta;
	bzero(mta, ETH_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES);

	if (adapter->hw.mac.type == e1000_82542 && 
	    adapter->hw.revision_id == E1000_REVISION_2) {
		reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
		if (adapter->hw.bus.pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			e1000_pci_clear_mwi(&adapter->hw);
		reg_rctl |= E1000_RCTL_RST;
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
		msec_delay(5);
	}

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == MAX_NUM_MULTICAST_ADDRESSES)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &mta[mcnt * ETHER_ADDR_LEN], ETHER_ADDR_LEN);
		mcnt++;
	}

	if (mcnt >= MAX_NUM_MULTICAST_ADDRESSES) {
		reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
		reg_rctl |= E1000_RCTL_MPE;
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
	} else {
		e1000_update_mc_addr_list(&adapter->hw, mta, mcnt);
	}

	if (adapter->hw.mac.type == e1000_82542 && 
	    adapter->hw.revision_id == E1000_REVISION_2) {
		reg_rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
		reg_rctl &= ~E1000_RCTL_RST;
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, reg_rctl);
		msec_delay(5);
		if (adapter->hw.bus.pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			e1000_pci_set_mwi(&adapter->hw);
	}
}

/*
 * This routine checks for link status and updates statistics.
 */
static void
em_timer(void *xsc)
{
	struct adapter *adapter = xsc;
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	em_update_link_status(adapter);
	em_update_stats(adapter);

	/* Reset LAA into RAR[0] on 82571 */
	if (e1000_get_laa_state_82571(&adapter->hw) == TRUE)
		e1000_rar_set(&adapter->hw, adapter->hw.mac.addr, 0);

	if (em_display_debug_stats && (ifp->if_flags & IFF_RUNNING))
		em_print_hw_stats(adapter);

	em_smartspeed(adapter);

	callout_reset(&adapter->timer, hz, em_timer, adapter);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
em_update_link_status(struct adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	device_t dev = adapter->dev;
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
		link_check =
			E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU;
		break;

	case e1000_media_type_internal_serdes:
		e1000_check_for_link(hw);
		link_check = adapter->hw.mac.serdes_has_link;
		break;

	case e1000_media_type_unknown:
	default:
		break;
	}

	/* Now check for a transition */
	if (link_check && adapter->link_active == 0) {
		e1000_get_speed_and_duplex(hw, &adapter->link_speed,
		    &adapter->link_duplex);

		/*
		 * Check if we should enable/disable SPEED_MODE bit on
		 * 82571/82572
		 */
		if (adapter->link_speed != SPEED_1000 &&
		    (hw->mac.type == e1000_82571 ||
		     hw->mac.type == e1000_82572)) {
			int tarc0;

			tarc0 = E1000_READ_REG(hw, E1000_TARC(0));
			tarc0 &= ~TARC_SPEED_MODE_BIT;
			E1000_WRITE_REG(hw, E1000_TARC(0), tarc0);
		}
		if (bootverbose) {
			char flowctrl[IFM_ETH_FC_STRLEN];

			e1000_fc2str(hw->fc.current_mode, flowctrl,
			    sizeof(flowctrl));
			device_printf(dev, "Link is up %d Mbps %s, "
			    "Flow control: %s\n",
			    adapter->link_speed,
			    (adapter->link_duplex == FULL_DUPLEX) ?
			    "Full Duplex" : "Half Duplex",
			    flowctrl);
		}
		if (adapter->ifm_flowctrl & IFM_ETH_FORCEPAUSE)
			e1000_force_flowctrl(hw, adapter->ifm_flowctrl);
		adapter->link_active = 1;
		adapter->smartspeed = 0;
		ifp->if_baudrate = adapter->link_speed * 1000000;
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
	} else if (!link_check && adapter->link_active == 1) {
		ifp->if_baudrate = adapter->link_speed = 0;
		adapter->link_duplex = 0;
		if (bootverbose)
			device_printf(dev, "Link is Down\n");
		adapter->link_active = 0;
#if 0
		/* Link down, disable watchdog */
		if->if_timer = 0;
#endif
		ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(ifp);
	}
}

static void
em_stop(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	em_disable_intr(adapter);

	callout_stop(&adapter->timer);
	callout_stop(&adapter->tx_fifo_timer);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	adapter->tx_running = 0;
	callout_stop(&adapter->tx_gc_timer);

	/* I219 needs some special flushing to avoid hangs */
	if (adapter->hw.mac.type >= e1000_pch_spt)
		em_flush_txrx_ring(adapter);

	e1000_reset_hw(&adapter->hw);
	if (adapter->hw.mac.type >= e1000_82544)
		E1000_WRITE_REG(&adapter->hw, E1000_WUC, 0);

	for (i = 0; i < adapter->num_tx_desc; i++) {
		struct em_buffer *tx_buffer = &adapter->tx_buffer_area[i];

		if (tx_buffer->m_head != NULL)
			em_free_txbuffer(adapter, tx_buffer);
	}

	for (i = 0; i < adapter->num_rx_desc; i++) {
		struct em_buffer *rx_buffer = &adapter->rx_buffer_area[i];

		if (rx_buffer->m_head != NULL) {
			bus_dmamap_unload(adapter->rxtag, rx_buffer->map);
			m_freem(rx_buffer->m_head);
			rx_buffer->m_head = NULL;
		}
	}

	if (adapter->fmp != NULL)
		m_freem(adapter->fmp);
	adapter->fmp = NULL;
	adapter->lmp = NULL;

	adapter->csum_flags = 0;
	adapter->csum_lhlen = 0;
	adapter->csum_iphlen = 0;
	adapter->csum_thlen = 0;
	adapter->csum_mss = 0;
	adapter->csum_pktlen = 0;

	adapter->tx_dd_head = 0;
	adapter->tx_dd_tail = 0;
	adapter->tx_nsegs = 0;
}

static int
em_get_hw_info(struct adapter *adapter)
{
	device_t dev = adapter->dev;

	/* Save off the information about this board */
	adapter->hw.vendor_id = pci_get_vendor(dev);
	adapter->hw.device_id = pci_get_device(dev);
	adapter->hw.revision_id = pci_get_revid(dev);
	adapter->hw.subsystem_vendor_id = pci_get_subvendor(dev);
	adapter->hw.subsystem_device_id = pci_get_subdevice(dev);

	/* Do Shared Code Init and Setup */
	if (e1000_set_mac_type(&adapter->hw))
		return ENXIO;
	return 0;
}

static int
em_alloc_pci_res(struct adapter *adapter)
{
	device_t dev = adapter->dev;
	u_int intr_flags;
	int val, rid, msi_enable;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	adapter->memory_rid = EM_BAR_MEM;
	adapter->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
				&adapter->memory_rid, RF_ACTIVE);
	if (adapter->memory == NULL) {
		device_printf(dev, "Unable to allocate bus resource: memory\n");
		return (ENXIO);
	}
	adapter->osdep.mem_bus_space_tag =
	    rman_get_bustag(adapter->memory);
	adapter->osdep.mem_bus_space_handle =
	    rman_get_bushandle(adapter->memory);

	/* XXX This is quite goofy, it is not actually used */
	adapter->hw.hw_addr = (uint8_t *)&adapter->osdep.mem_bus_space_handle;

	/* Only older adapters use IO mapping */
	if (adapter->hw.mac.type > e1000_82543 &&
	    adapter->hw.mac.type < e1000_82571) {
		/* Figure our where our IO BAR is ? */
		for (rid = PCIR_BAR(0); rid < PCIR_CARDBUSCIS;) {
			val = pci_read_config(dev, rid, 4);
			if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
				adapter->io_rid = rid;
				break;
			}
			rid += 4;
			/* check for 64bit BAR */
			if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
				rid += 4;
		}
		if (rid >= PCIR_CARDBUSCIS) {
			device_printf(dev, "Unable to locate IO BAR\n");
			return (ENXIO);
		}
		adapter->ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
					&adapter->io_rid, RF_ACTIVE);
		if (adapter->ioport == NULL) {
			device_printf(dev, "Unable to allocate bus resource: "
			    "ioport\n");
			return (ENXIO);
		}
		adapter->hw.io_base = 0;
		adapter->osdep.io_bus_space_tag =
		    rman_get_bustag(adapter->ioport);
		adapter->osdep.io_bus_space_handle =
		    rman_get_bushandle(adapter->ioport);
	}

	/*
	 * Don't enable MSI-X on 82574, see:
	 * 82574 specification update errata #15
	 *
	 * Don't enable MSI on PCI/PCI-X chips, see:
	 * 82540 specification update errata #6
	 * 82545 specification update errata #4
	 *
	 * Don't enable MSI on 82571/82572, see:
	 * 82571/82572 specification update errata #63
	 */
	msi_enable = em_msi_enable;
	if (msi_enable &&
	    ((adapter->flags & EM_FLAG_GEN2) == 0 ||
	     adapter->hw.mac.type == e1000_82571 ||
	     adapter->hw.mac.type == e1000_82572))
		msi_enable = 0;
again:
	adapter->intr_type = pci_alloc_1intr(dev, msi_enable,
	    &adapter->intr_rid, &intr_flags);

	if (adapter->intr_type == PCI_INTR_TYPE_LEGACY) {
		int unshared;

		unshared = device_getenv_int(dev, "irq.unshared", 0);
		if (!unshared) {
			adapter->flags |= EM_FLAG_SHARED_INTR;
			if (bootverbose)
				device_printf(dev, "IRQ shared\n");
		} else {
			intr_flags &= ~RF_SHAREABLE;
			if (bootverbose)
				device_printf(dev, "IRQ unshared\n");
		}
	}

	adapter->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &adapter->intr_rid, intr_flags);
	if (adapter->intr_res == NULL) {
		device_printf(dev, "Unable to allocate bus resource: %s\n",
		    adapter->intr_type == PCI_INTR_TYPE_MSI ?
		    "MSI" : "legacy intr");
		if (!msi_enable) {
			/* Retry with MSI. */
			msi_enable = 1;
			adapter->flags &= ~EM_FLAG_SHARED_INTR;
			goto again;
		}
		return (ENXIO);
	}

	adapter->hw.bus.pci_cmd_word = pci_read_config(dev, PCIR_COMMAND, 2);
	adapter->hw.back = &adapter->osdep;
	return (0);
}

static void
em_free_pci_res(struct adapter *adapter)
{
	device_t dev = adapter->dev;

	if (adapter->intr_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    adapter->intr_rid, adapter->intr_res);
	}

	if (adapter->intr_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (adapter->memory != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    adapter->memory_rid, adapter->memory);
	}

	if (adapter->flash != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    adapter->flash_rid, adapter->flash);
	}

	if (adapter->ioport != NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT,
		    adapter->io_rid, adapter->ioport);
	}
}

static int
em_reset(struct adapter *adapter)
{
	device_t dev = adapter->dev;
	uint16_t rx_buffer_size;
	uint32_t pba;

	/* When hardware is reset, fifo_head is also reset */
	adapter->tx_fifo_head = 0;

	/* Set up smart power down as default off on newer adapters. */
	if (!em_smart_pwr_down &&
	    (adapter->hw.mac.type == e1000_82571 ||
	     adapter->hw.mac.type == e1000_82572)) {
		uint16_t phy_tmp = 0;

		/* Speed up time to link by disabling smart power down. */
		e1000_read_phy_reg(&adapter->hw,
		    IGP02E1000_PHY_POWER_MGMT, &phy_tmp);
		phy_tmp &= ~IGP02E1000_PM_SPD;
		e1000_write_phy_reg(&adapter->hw,
		    IGP02E1000_PHY_POWER_MGMT, phy_tmp);
	}

	/*
	 * Packet Buffer Allocation (PBA)
	 * Writing PBA sets the receive portion of the buffer
	 * the remainder is used for the transmit buffer.
	 *
	 * Devices before the 82547 had a Packet Buffer of 64K.
	 *   Default allocation: PBA=48K for Rx, leaving 16K for Tx.
	 * After the 82547 the buffer was reduced to 40K.
	 *   Default allocation: PBA=30K for Rx, leaving 10K for Tx.
	 *   Note: default does not leave enough room for Jumbo Frame >10k.
	 */
	switch (adapter->hw.mac.type) {
	case e1000_82547:
	case e1000_82547_rev_2: /* 82547: Total Packet Buffer is 40K */
		if (adapter->hw.mac.max_frame_size > 8192)
			pba = E1000_PBA_22K; /* 22K for Rx, 18K for Tx */
		else
			pba = E1000_PBA_30K; /* 30K for Rx, 10K for Tx */
		adapter->tx_fifo_head = 0;
		adapter->tx_head_addr = pba << EM_TX_HEAD_ADDR_SHIFT;
		adapter->tx_fifo_size =
		    (E1000_PBA_40K - pba) << EM_PBA_BYTES_SHIFT;
		break;

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
	case e1000_82583:
		pba = E1000_PBA_20K; /* 20K for Rx, 20K for Tx */
		break;

	case e1000_ich8lan:
		pba = E1000_PBA_8K;
		break;

	case e1000_ich9lan:
	case e1000_ich10lan:
#define E1000_PBA_10K	0x000A
		pba = E1000_PBA_10K;
		break;

	case e1000_pchlan:
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		pba = E1000_PBA_26K;
		break;

	default:
		/* Devices before 82547 had a Packet Buffer of 64K.   */
		if (adapter->hw.mac.max_frame_size > 8192)
			pba = E1000_PBA_40K; /* 40K for Rx, 24K for Tx */
		else
			pba = E1000_PBA_48K; /* 48K for Rx, 16K for Tx */
	}
	E1000_WRITE_REG(&adapter->hw, E1000_PBA, pba);

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
	rx_buffer_size =
		(E1000_READ_REG(&adapter->hw, E1000_PBA) & 0xffff) << 10;

	adapter->hw.fc.high_water = rx_buffer_size -
	    roundup2(adapter->hw.mac.max_frame_size, 1024);
	adapter->hw.fc.low_water = adapter->hw.fc.high_water - 1500;

	if (adapter->hw.mac.type == e1000_80003es2lan)
		adapter->hw.fc.pause_time = 0xFFFF;
	else
		adapter->hw.fc.pause_time = EM_FC_PAUSE_TIME;

	adapter->hw.fc.send_xon = TRUE;

	adapter->hw.fc.requested_mode = e1000_ifmedia2fc(adapter->ifm_flowctrl);

	/*
	 * Device specific overrides/settings
	 */
	switch (adapter->hw.mac.type) {
	case e1000_pchlan:
		KASSERT(adapter->hw.fc.requested_mode == e1000_fc_rx_pause ||
		    adapter->hw.fc.requested_mode == e1000_fc_none,
		    ("unsupported flow control on PCH %d",
		     adapter->hw.fc.requested_mode));
		adapter->hw.fc.pause_time = 0xFFFF; /* override */
		if (adapter->arpcom.ac_if.if_mtu > ETHERMTU) {
			adapter->hw.fc.high_water = 0x3500;
			adapter->hw.fc.low_water = 0x1500;
		} else {
			adapter->hw.fc.high_water = 0x5000;
			adapter->hw.fc.low_water = 0x3000;
		}
		adapter->hw.fc.refresh_time = 0x1000;
		break;

	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		adapter->hw.fc.high_water = 0x5C20;
		adapter->hw.fc.low_water = 0x5048;
		adapter->hw.fc.pause_time = 0x0650;
		adapter->hw.fc.refresh_time = 0x0400;
		/* Jumbos need adjusted PBA */
		if (adapter->arpcom.ac_if.if_mtu > ETHERMTU)
			E1000_WRITE_REG(&adapter->hw, E1000_PBA, 12);
		else
			E1000_WRITE_REG(&adapter->hw, E1000_PBA, 26);
		break;

	case e1000_ich9lan:
	case e1000_ich10lan:
		if (adapter->arpcom.ac_if.if_mtu > ETHERMTU) {
			adapter->hw.fc.high_water = 0x2800;
			adapter->hw.fc.low_water =
			    adapter->hw.fc.high_water - 8;
			break;
		}
		/* FALL THROUGH */
	default:
		if (adapter->hw.mac.type == e1000_80003es2lan)
			adapter->hw.fc.pause_time = 0xFFFF;
		break;
	}

	/* I219 needs some special flushing to avoid hangs */
	if (adapter->hw.mac.type >= e1000_pch_spt)
		em_flush_txrx_ring(adapter);

	/* Issue a global reset */
	e1000_reset_hw(&adapter->hw);
	if (adapter->hw.mac.type >= e1000_82544)
		E1000_WRITE_REG(&adapter->hw, E1000_WUC, 0);
	em_disable_aspm(adapter);

	if (e1000_init_hw(&adapter->hw) < 0) {
		device_printf(dev, "Hardware Initialization Failed\n");
		return (EIO);
	}

	E1000_WRITE_REG(&adapter->hw, E1000_VET, ETHERTYPE_VLAN);
	e1000_get_phy_info(&adapter->hw);
	e1000_check_for_link(&adapter->hw);

	return (0);
}

static void
em_setup_ifp(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	if_initname(ifp, device_get_name(adapter->dev),
		    device_get_unit(adapter->dev));
	ifp->if_softc = adapter;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init =  em_init;
	ifp->if_ioctl = em_ioctl;
	ifp->if_start = em_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = em_npoll;
#endif
	ifp->if_watchdog = em_watchdog;
	ifp->if_nmbclusters = adapter->num_rx_desc;
	ifq_set_maxlen(&ifp->if_snd, adapter->num_tx_desc - 1);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, adapter->hw.mac.addr, NULL);

	ifp->if_capabilities = IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	if (adapter->hw.mac.type >= e1000_82543)
		ifp->if_capabilities |= IFCAP_HWCSUM;
	if (adapter->flags & EM_FLAG_TSO)
		ifp->if_capabilities |= IFCAP_TSO;
	ifp->if_capenable = ifp->if_capabilities;

	if (ifp->if_capenable & IFCAP_TXCSUM)
		ifp->if_hwassist |= EM_CSUM_FEATURES;
	if (ifp->if_capenable & IFCAP_TSO)
		ifp->if_hwassist |= CSUM_TSO;

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	if (adapter->hw.phy.media_type == e1000_media_type_fiber ||
	    adapter->hw.phy.media_type == e1000_media_type_internal_serdes) {
		u_char fiber_type = IFM_1000_SX; /* default type */

		if (adapter->hw.mac.type == e1000_82545)
			fiber_type = IFM_1000_LX;
		ifmedia_add(&adapter->media, IFM_ETHER | fiber_type | IFM_FDX, 
			    0, NULL);
	} else {
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_10_T, 0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_10_T | IFM_FDX,
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_100_TX,
			    0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_100_TX | IFM_FDX,
			    0, NULL);
		if (adapter->hw.phy.type != e1000_phy_ife) {
			ifmedia_add(&adapter->media,
				IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
		}
	}
	ifmedia_add(&adapter->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&adapter->media, IFM_ETHER | IFM_AUTO |
	    adapter->ifm_flowctrl);
}


/*
 * Workaround for SmartSpeed on 82541 and 82547 controllers
 */
static void
em_smartspeed(struct adapter *adapter)
{
	uint16_t phy_tmp;

	if (adapter->link_active || adapter->hw.phy.type != e1000_phy_igp ||
	    adapter->hw.mac.autoneg == 0 ||
	    (adapter->hw.phy.autoneg_advertised & ADVERTISE_1000_FULL) == 0)
		return;

	if (adapter->smartspeed == 0) {
		/*
		 * If Master/Slave config fault is asserted twice,
		 * we assume back-to-back
		 */
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_tmp);
		if (!(phy_tmp & SR_1000T_MS_CONFIG_FAULT))
			return;
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_STATUS, &phy_tmp);
		if (phy_tmp & SR_1000T_MS_CONFIG_FAULT) {
			e1000_read_phy_reg(&adapter->hw,
			    PHY_1000T_CTRL, &phy_tmp);
			if (phy_tmp & CR_1000T_MS_ENABLE) {
				phy_tmp &= ~CR_1000T_MS_ENABLE;
				e1000_write_phy_reg(&adapter->hw,
				    PHY_1000T_CTRL, phy_tmp);
				adapter->smartspeed++;
				if (adapter->hw.mac.autoneg &&
				    !e1000_phy_setup_autoneg(&adapter->hw) &&
				    !e1000_read_phy_reg(&adapter->hw,
				     PHY_CONTROL, &phy_tmp)) {
					phy_tmp |= MII_CR_AUTO_NEG_EN |
						   MII_CR_RESTART_AUTO_NEG;
					e1000_write_phy_reg(&adapter->hw,
					    PHY_CONTROL, phy_tmp);
				}
			}
		}
		return;
	} else if (adapter->smartspeed == EM_SMARTSPEED_DOWNSHIFT) {
		/* If still no link, perhaps using 2/3 pair cable */
		e1000_read_phy_reg(&adapter->hw, PHY_1000T_CTRL, &phy_tmp);
		phy_tmp |= CR_1000T_MS_ENABLE;
		e1000_write_phy_reg(&adapter->hw, PHY_1000T_CTRL, phy_tmp);
		if (adapter->hw.mac.autoneg &&
		    !e1000_phy_setup_autoneg(&adapter->hw) &&
		    !e1000_read_phy_reg(&adapter->hw, PHY_CONTROL, &phy_tmp)) {
			phy_tmp |= MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG;
			e1000_write_phy_reg(&adapter->hw, PHY_CONTROL, phy_tmp);
		}
	}

	/* Restart process after EM_SMARTSPEED_MAX iterations */
	if (adapter->smartspeed++ == EM_SMARTSPEED_MAX)
		adapter->smartspeed = 0;
}

static int
em_dma_malloc(struct adapter *adapter, bus_size_t size,
	      struct em_dma_alloc *dma)
{
	dma->dma_vaddr = bus_dmamem_coherent_any(adapter->parent_dtag,
				EM_DBA_ALIGN, size, BUS_DMA_WAITOK,
				&dma->dma_tag, &dma->dma_map,
				&dma->dma_paddr);
	if (dma->dma_vaddr == NULL)
		return ENOMEM;
	else
		return 0;
}

static void
em_dma_free(struct adapter *adapter, struct em_dma_alloc *dma)
{
	if (dma->dma_tag == NULL)
		return;
	bus_dmamap_unload(dma->dma_tag, dma->dma_map);
	bus_dmamem_free(dma->dma_tag, dma->dma_vaddr, dma->dma_map);
	bus_dma_tag_destroy(dma->dma_tag);
}

static int
em_create_tx_ring(struct adapter *adapter)
{
	device_t dev = adapter->dev;
	struct em_buffer *tx_buffer;
	int error, i;

	adapter->tx_buffer_area =
		kmalloc(sizeof(struct em_buffer) * adapter->num_tx_desc,
			M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Create DMA tags for tx buffers
	 */
	error = bus_dma_tag_create(adapter->parent_dtag, /* parent */
			1, 0,			/* alignment, bounds */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			EM_TSO_SIZE,		/* maxsize */
			EM_MAX_SCATTER,		/* nsegments */
			PAGE_SIZE,		/* maxsegsize */
			BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW |
			BUS_DMA_ONEBPAGE,	/* flags */
			&adapter->txtag);
	if (error) {
		device_printf(dev, "Unable to allocate TX DMA tag\n");
		kfree(adapter->tx_buffer_area, M_DEVBUF);
		adapter->tx_buffer_area = NULL;
		return error;
	}

	/*
	 * Create DMA maps for tx buffers
	 */
	for (i = 0; i < adapter->num_tx_desc; i++) {
		tx_buffer = &adapter->tx_buffer_area[i];

		error = bus_dmamap_create(adapter->txtag,
					  BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
					  &tx_buffer->map);
		if (error) {
			device_printf(dev, "Unable to create TX DMA map\n");
			em_destroy_tx_ring(adapter, i);
			return error;
		}
	}
	return (0);
}

static void
em_init_tx_ring(struct adapter *adapter)
{
	/* Clear the old ring contents */
	bzero(adapter->tx_desc_base,
	    (sizeof(struct e1000_tx_desc)) * adapter->num_tx_desc);

	/* Reset state */
	adapter->next_avail_tx_desc = 0;
	adapter->next_tx_to_clean = 0;
	adapter->num_tx_desc_avail = adapter->num_tx_desc;
	adapter->tx_nmbuf = 0;
	adapter->tx_running = 0;
}

static void
em_init_tx_unit(struct adapter *adapter)
{
	uint32_t tctl, tarc, tipg = 0;
	uint64_t bus_addr;

	/* Setup the Base and Length of the Tx Descriptor Ring */
	bus_addr = adapter->txdma.dma_paddr;
	E1000_WRITE_REG(&adapter->hw, E1000_TDLEN(0),
	    adapter->num_tx_desc * sizeof(struct e1000_tx_desc));
	E1000_WRITE_REG(&adapter->hw, E1000_TDBAH(0),
	    (uint32_t)(bus_addr >> 32));
	E1000_WRITE_REG(&adapter->hw, E1000_TDBAL(0),
	    (uint32_t)bus_addr);
	/* Setup the HW Tx Head and Tail descriptor pointers */
	E1000_WRITE_REG(&adapter->hw, E1000_TDT(0), 0);
	E1000_WRITE_REG(&adapter->hw, E1000_TDH(0), 0);
	if (adapter->flags & EM_FLAG_GEN2) {
		uint32_t txdctl = 0;

		txdctl |= 0x1f;		/* PTHRESH */
		txdctl |= 1 << 8;	/* HTHRESH */
		txdctl |= 1 << 16;	/* WTHRESH */
		txdctl |= 1 << 22;	/* Reserved bit 22 must always be 1 */
		txdctl |= E1000_TXDCTL_GRAN;
		txdctl |= 1 << 25;	/* LWTHRESH */

		E1000_WRITE_REG(&adapter->hw, E1000_TXDCTL(0), txdctl);
	}

	/* Set the default values for the Tx Inter Packet Gap timer */
	switch (adapter->hw.mac.type) {
	case e1000_82542:
		tipg = DEFAULT_82542_TIPG_IPGT;
		tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		break;

	case e1000_80003es2lan:
		tipg = DEFAULT_82543_TIPG_IPGR1;
		tipg |= DEFAULT_80003ES2LAN_TIPG_IPGR2 <<
		    E1000_TIPG_IPGR2_SHIFT;
		break;

	default:
		if (adapter->hw.phy.media_type == e1000_media_type_fiber ||
		    adapter->hw.phy.media_type ==
		    e1000_media_type_internal_serdes)
			tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
		else
			tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
		tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
		tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		break;
	}

	E1000_WRITE_REG(&adapter->hw, E1000_TIPG, tipg);

	/* NOTE: 0 is not allowed for TIDV */
	E1000_WRITE_REG(&adapter->hw, E1000_TIDV, 1);
	if(adapter->hw.mac.type >= e1000_82540)
		E1000_WRITE_REG(&adapter->hw, E1000_TADV, 0);

	if (adapter->hw.mac.type == e1000_82571 ||
	    adapter->hw.mac.type == e1000_82572) {
		tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
		tarc |= TARC_SPEED_MODE_BIT;
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
	} else if (adapter->hw.mac.type == e1000_80003es2lan) {
		/* errata: program both queues to unweighted RR */
		tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
		tarc |= 1;
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
		tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(1));
		tarc |= 1;
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(1), tarc);
	} else if (adapter->hw.mac.type == e1000_82574) {
		tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
		tarc |= TARC_ERRATA_BIT;
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), tarc);
	}

	/* Program the Transmit Control Register */
	tctl = E1000_READ_REG(&adapter->hw, E1000_TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC | E1000_TCTL_EN |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	if (adapter->hw.mac.type >= e1000_82571)
		tctl |= E1000_TCTL_MULR;

	/* This write will effectively turn on the transmit unit. */
	E1000_WRITE_REG(&adapter->hw, E1000_TCTL, tctl);

	if (adapter->hw.mac.type == e1000_82571 ||
	    adapter->hw.mac.type == e1000_82572 ||
	    adapter->hw.mac.type == e1000_80003es2lan) {
		/* Bit 28 of TARC1 must be cleared when MULR is enabled */
		tarc = E1000_READ_REG(&adapter->hw, E1000_TARC(1));
		tarc &= ~(1 << 28);
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(1), tarc);
	} else if (adapter->hw.mac.type >= e1000_pch_spt) {
		uint32_t reg;

		reg = E1000_READ_REG(&adapter->hw, E1000_IOSFPC);
		reg |= E1000_RCTL_RDMTS_HEX;
		E1000_WRITE_REG(&adapter->hw, E1000_IOSFPC, reg);
		reg = E1000_READ_REG(&adapter->hw, E1000_TARC(0));
		reg |= E1000_TARC0_CB_MULTIQ_3_REQ;
		E1000_WRITE_REG(&adapter->hw, E1000_TARC(0), reg);
	}
}

static void
em_destroy_tx_ring(struct adapter *adapter, int ndesc)
{
	struct em_buffer *tx_buffer;
	int i;

	if (adapter->tx_buffer_area == NULL)
		return;

	for (i = 0; i < ndesc; i++) {
		tx_buffer = &adapter->tx_buffer_area[i];

		KKASSERT(tx_buffer->m_head == NULL);
		bus_dmamap_destroy(adapter->txtag, tx_buffer->map);
	}
	bus_dma_tag_destroy(adapter->txtag);

	kfree(adapter->tx_buffer_area, M_DEVBUF);
	adapter->tx_buffer_area = NULL;
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
em_txcsum(struct adapter *adapter, struct mbuf *mp,
	  uint32_t *txd_upper, uint32_t *txd_lower)
{
	struct e1000_context_desc *TXD;
	int curr_txd, ehdrlen, csum_flags;
	uint32_t cmd, hdr_len, ip_hlen;

	csum_flags = mp->m_pkthdr.csum_flags & EM_CSUM_FEATURES;
	ip_hlen = mp->m_pkthdr.csum_iphlen;
	ehdrlen = mp->m_pkthdr.csum_lhlen;

	if (adapter->csum_lhlen == ehdrlen &&
	    adapter->csum_iphlen == ip_hlen &&
	    adapter->csum_flags == csum_flags) {
		/*
		 * Same csum offload context as the previous packets;
		 * just return.
		 */
		*txd_upper = adapter->csum_txd_upper;
		*txd_lower = adapter->csum_txd_lower;
		return 0;
	}

	/*
	 * Setup a new csum offload context.
	 */

	curr_txd = adapter->next_avail_tx_desc;
	TXD = (struct e1000_context_desc *)&adapter->tx_desc_base[curr_txd];

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
	adapter->csum_lhlen = ehdrlen;
	adapter->csum_iphlen = ip_hlen;
	adapter->csum_flags = csum_flags;
	adapter->csum_txd_upper = *txd_upper;
	adapter->csum_txd_lower = *txd_lower;

	TXD->tcp_seg_setup.data = htole32(0);
	TXD->cmd_and_length =
	    htole32(E1000_TXD_CMD_IFCS | E1000_TXD_CMD_DEXT | cmd);

	if (++curr_txd == adapter->num_tx_desc)
		curr_txd = 0;

	KKASSERT(adapter->num_tx_desc_avail > 0);
	adapter->num_tx_desc_avail--;

	adapter->next_avail_tx_desc = curr_txd;
	return 1;
}

static void
em_txeof(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	struct em_buffer *tx_buffer;
	int first, num_avail;

	if (adapter->tx_dd_head == adapter->tx_dd_tail)
		return;

	if (adapter->num_tx_desc_avail == adapter->num_tx_desc)
		return;

	num_avail = adapter->num_tx_desc_avail;
	first = adapter->next_tx_to_clean;

	while (adapter->tx_dd_head != adapter->tx_dd_tail) {
		struct e1000_tx_desc *tx_desc;
		int dd_idx = adapter->tx_dd[adapter->tx_dd_head];

		tx_desc = &adapter->tx_desc_base[dd_idx];
		if (tx_desc->upper.fields.status & E1000_TXD_STAT_DD) {
			EM_INC_TXDD_IDX(adapter->tx_dd_head);

			if (++dd_idx == adapter->num_tx_desc)
				dd_idx = 0;

			while (first != dd_idx) {
				logif(pkt_txclean);

				KKASSERT(num_avail < adapter->num_tx_desc);
				num_avail++;

				tx_buffer = &adapter->tx_buffer_area[first];
				if (tx_buffer->m_head != NULL)
					em_free_txbuffer(adapter, tx_buffer);

				if (++first == adapter->num_tx_desc)
					first = 0;
			}
		} else {
			break;
		}
	}
	adapter->next_tx_to_clean = first;
	adapter->num_tx_desc_avail = num_avail;

	if (adapter->tx_dd_head == adapter->tx_dd_tail) {
		adapter->tx_dd_head = 0;
		adapter->tx_dd_tail = 0;
	}

	if (!EM_IS_OACTIVE(adapter)) {
		ifq_clr_oactive(&ifp->if_snd);

		/* All clean, turn off the timer */
		if (adapter->num_tx_desc_avail == adapter->num_tx_desc)
			ifp->if_timer = 0;
	}
	adapter->tx_running = EM_TX_RUNNING;
}

static void
em_tx_collect(struct adapter *adapter, boolean_t gc)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	struct em_buffer *tx_buffer;
	int tdh, first, num_avail, dd_idx = -1;

	if (adapter->num_tx_desc_avail == adapter->num_tx_desc)
		return;

	tdh = E1000_READ_REG(&adapter->hw, E1000_TDH(0));
	if (tdh == adapter->next_tx_to_clean) {
		if (gc && adapter->tx_nmbuf > 0)
			adapter->tx_running = EM_TX_RUNNING;
		return;
	}
	if (gc)
		adapter->tx_gc++;

	if (adapter->tx_dd_head != adapter->tx_dd_tail)
		dd_idx = adapter->tx_dd[adapter->tx_dd_head];

	num_avail = adapter->num_tx_desc_avail;
	first = adapter->next_tx_to_clean;

	while (first != tdh) {
		logif(pkt_txclean);

		KKASSERT(num_avail < adapter->num_tx_desc);
		num_avail++;

		tx_buffer = &adapter->tx_buffer_area[first];
		if (tx_buffer->m_head != NULL)
			em_free_txbuffer(adapter, tx_buffer);

		if (first == dd_idx) {
			EM_INC_TXDD_IDX(adapter->tx_dd_head);
			if (adapter->tx_dd_head == adapter->tx_dd_tail) {
				adapter->tx_dd_head = 0;
				adapter->tx_dd_tail = 0;
				dd_idx = -1;
			} else {
				dd_idx = adapter->tx_dd[adapter->tx_dd_head];
			}
		}

		if (++first == adapter->num_tx_desc)
			first = 0;
	}
	adapter->next_tx_to_clean = first;
	adapter->num_tx_desc_avail = num_avail;

	if (!EM_IS_OACTIVE(adapter)) {
		ifq_clr_oactive(&ifp->if_snd);

		/* All clean, turn off the timer */
		if (adapter->num_tx_desc_avail == adapter->num_tx_desc)
			ifp->if_timer = 0;
	}
	if (!gc || adapter->tx_nmbuf > 0)
		adapter->tx_running = EM_TX_RUNNING;
}

/*
 * When Link is lost sometimes there is work still in the TX ring
 * which will result in a watchdog, rather than allow that do an
 * attempted cleanup and then reinit here.  Note that this has been
 * seens mostly with fiber adapters.
 */
static void
em_tx_purge(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	if (!adapter->link_active && ifp->if_timer) {
		em_tx_collect(adapter, FALSE);
		if (ifp->if_timer) {
			if_printf(ifp, "Link lost, TX pending, reinit\n");
			ifp->if_timer = 0;
			em_init(adapter);
		}
	}
}

static int
em_newbuf(struct adapter *adapter, int i, int init)
{
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct em_buffer *rx_buffer;
	int error, nseg;

	m = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		adapter->mbuf_cluster_failed++;
		if (init) {
			if_printf(&adapter->arpcom.ac_if,
				  "Unable to allocate RX mbuf\n");
		}
		return (ENOBUFS);
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (adapter->hw.mac.max_frame_size <= MCLBYTES - ETHER_ALIGN)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(adapter->rxtag,
			adapter->rx_sparemap, m,
			&seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init) {
			if_printf(&adapter->arpcom.ac_if,
				  "Unable to load RX mbuf\n");
		}
		return (error);
	}

	rx_buffer = &adapter->rx_buffer_area[i];
	if (rx_buffer->m_head != NULL)
		bus_dmamap_unload(adapter->rxtag, rx_buffer->map);

	map = rx_buffer->map;
	rx_buffer->map = adapter->rx_sparemap;
	adapter->rx_sparemap = map;

	rx_buffer->m_head = m;

	adapter->rx_desc_base[i].buffer_addr = htole64(seg.ds_addr);
	return (0);
}

static int
em_create_rx_ring(struct adapter *adapter)
{
	device_t dev = adapter->dev;
	struct em_buffer *rx_buffer;
	int i, error;

	adapter->rx_buffer_area =
		kmalloc(sizeof(struct em_buffer) * adapter->num_rx_desc,
			M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Create DMA tag for rx buffers
	 */
	error = bus_dma_tag_create(adapter->parent_dtag, /* parent */
			1, 0,			/* alignment, bounds */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MCLBYTES,		/* maxsize */
			1,			/* nsegments */
			MCLBYTES,		/* maxsegsize */
			BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, /* flags */
			&adapter->rxtag);
	if (error) {
		device_printf(dev, "Unable to allocate RX DMA tag\n");
		kfree(adapter->rx_buffer_area, M_DEVBUF);
		adapter->rx_buffer_area = NULL;
		return error;
	}

	/*
	 * Create spare DMA map for rx buffers
	 */
	error = bus_dmamap_create(adapter->rxtag, BUS_DMA_WAITOK,
				  &adapter->rx_sparemap);
	if (error) {
		device_printf(dev, "Unable to create spare RX DMA map\n");
		bus_dma_tag_destroy(adapter->rxtag);
		kfree(adapter->rx_buffer_area, M_DEVBUF);
		adapter->rx_buffer_area = NULL;
		return error;
	}

	/*
	 * Create DMA maps for rx buffers
	 */
	for (i = 0; i < adapter->num_rx_desc; i++) {
		rx_buffer = &adapter->rx_buffer_area[i];

		error = bus_dmamap_create(adapter->rxtag, BUS_DMA_WAITOK,
					  &rx_buffer->map);
		if (error) {
			device_printf(dev, "Unable to create RX DMA map\n");
			em_destroy_rx_ring(adapter, i);
			return error;
		}
	}
	return (0);
}

static int
em_init_rx_ring(struct adapter *adapter)
{
	int i, error;

	/* Reset descriptor ring */
	bzero(adapter->rx_desc_base,
	    (sizeof(struct e1000_rx_desc)) * adapter->num_rx_desc);

	/* Allocate new ones. */
	for (i = 0; i < adapter->num_rx_desc; i++) {
		error = em_newbuf(adapter, i, 1);
		if (error)
			return (error);
	}

	/* Setup our descriptor pointers */
	adapter->next_rx_desc_to_check = 0;

	return (0);
}

static void
em_init_rx_unit(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	uint64_t bus_addr;
	uint32_t rctl, rxcsum;

	/*
	 * Make sure receives are disabled while setting
	 * up the descriptor ring
	 */
	rctl = E1000_READ_REG(&adapter->hw, E1000_RCTL);
	/* Do not disable if ever enabled on this hardware */
	if (adapter->hw.mac.type != e1000_82574 &&
	    adapter->hw.mac.type != e1000_82583)
		E1000_WRITE_REG(&adapter->hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);

	if (adapter->hw.mac.type >= e1000_82540) {
		uint32_t itr;

		/*
		 * Set the interrupt throttling rate. Value is calculated
		 * as ITR = 1 / (INT_THROTTLE_CEIL * 256ns)
		 */
		if (adapter->int_throttle_ceil)
			itr = 1000000000 / 256 / adapter->int_throttle_ceil;
		else
			itr = 0;
		em_set_itr(adapter, itr);
	}

	/* Disable accelerated ackknowledge */
	if (adapter->hw.mac.type == e1000_82574) {
		uint32_t rfctl;

		rfctl = E1000_READ_REG(&adapter->hw, E1000_RFCTL);
		rfctl |= E1000_RFCTL_ACK_DIS;
		E1000_WRITE_REG(&adapter->hw, E1000_RFCTL, rfctl);
	}

	/* Receive Checksum Offload for IP and TCP/UDP */
	rxcsum = E1000_READ_REG(&adapter->hw, E1000_RXCSUM);
	if (ifp->if_capenable & IFCAP_RXCSUM)
		rxcsum |= (E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL);
	else
		rxcsum &= ~(E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL);
	E1000_WRITE_REG(&adapter->hw, E1000_RXCSUM, rxcsum);

	/*
	 * XXX TEMPORARY WORKAROUND: on some systems with 82573
	 * long latencies are observed, like Lenovo X60. This
	 * change eliminates the problem, but since having positive
	 * values in RDTR is a known source of problems on other
	 * platforms another solution is being sought.
	 */
	if (em_82573_workaround && adapter->hw.mac.type == e1000_82573) {
		E1000_WRITE_REG(&adapter->hw, E1000_RADV, EM_RADV_82573);
		E1000_WRITE_REG(&adapter->hw, E1000_RDTR, EM_RDTR_82573);
	}

	/*
	 * Setup the Base and Length of the Rx Descriptor Ring
	 */
	bus_addr = adapter->rxdma.dma_paddr;
	E1000_WRITE_REG(&adapter->hw, E1000_RDLEN(0),
	    adapter->num_rx_desc * sizeof(struct e1000_rx_desc));
	E1000_WRITE_REG(&adapter->hw, E1000_RDBAH(0),
	    (uint32_t)(bus_addr >> 32));
	E1000_WRITE_REG(&adapter->hw, E1000_RDBAL(0),
	    (uint32_t)bus_addr);

	/*
	 * Setup the HW Rx Head and Tail Descriptor Pointers
	 */
	E1000_WRITE_REG(&adapter->hw, E1000_RDH(0), 0);
	E1000_WRITE_REG(&adapter->hw, E1000_RDT(0), adapter->num_rx_desc - 1);

	/* Set PTHRESH for improved jumbo performance */
	if (ifp->if_mtu > ETHERMTU) {
		uint32_t rxdctl;

		if (adapter->hw.mac.type == e1000_ich9lan ||
		    adapter->hw.mac.type == e1000_pch2lan ||
		    adapter->hw.mac.type == e1000_ich10lan) {
			rxdctl = E1000_READ_REG(&adapter->hw, E1000_RXDCTL(0));
			E1000_WRITE_REG(&adapter->hw, E1000_RXDCTL(0),
			    rxdctl | 3);
		} else if (adapter->hw.mac.type == e1000_82574) {
			rxdctl = E1000_READ_REG(&adapter->hw, E1000_RXDCTL(0));
                	rxdctl |= 0x20;		/* PTHRESH */
                	rxdctl |= 4 << 8;	/* HTHRESH */
                	rxdctl |= 4 << 16;	/* WTHRESH */
			rxdctl |= 1 << 24;	/* Switch to granularity */
			E1000_WRITE_REG(&adapter->hw, E1000_RXDCTL(0), rxdctl);
		}
	}

	if (adapter->hw.mac.type >= e1000_pch2lan) {
		if (ifp->if_mtu > ETHERMTU)
			e1000_lv_jumbo_workaround_ich8lan(&adapter->hw, TRUE);
		else
			e1000_lv_jumbo_workaround_ich8lan(&adapter->hw, FALSE);
	}

	/* Setup the Receive Control Register */
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO |
		E1000_RCTL_RDMTS_HALF |
		(adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/* Make sure VLAN Filters are off */
	rctl &= ~E1000_RCTL_VFE;

	if (e1000_tbi_sbp_enabled_82543(&adapter->hw))
		rctl |= E1000_RCTL_SBP;
	else
		rctl &= ~E1000_RCTL_SBP;

	switch (adapter->rx_buffer_len) {
	default:
	case 2048:
		rctl |= E1000_RCTL_SZ_2048;
		break;

	case 4096:
		rctl |= E1000_RCTL_SZ_4096 |
		    E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;

	case 8192:
		rctl |= E1000_RCTL_SZ_8192 |
		    E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;

	case 16384:
		rctl |= E1000_RCTL_SZ_16384 |
		    E1000_RCTL_BSEX | E1000_RCTL_LPE;
		break;
	}

	if (ifp->if_mtu > ETHERMTU)
		rctl |= E1000_RCTL_LPE;
	else
		rctl &= ~E1000_RCTL_LPE;

	/* Enable Receives */
	E1000_WRITE_REG(&adapter->hw, E1000_RCTL, rctl);
}

static void
em_destroy_rx_ring(struct adapter *adapter, int ndesc)
{
	struct em_buffer *rx_buffer;
	int i;

	if (adapter->rx_buffer_area == NULL)
		return;

	for (i = 0; i < ndesc; i++) {
		rx_buffer = &adapter->rx_buffer_area[i];

		KKASSERT(rx_buffer->m_head == NULL);
		bus_dmamap_destroy(adapter->rxtag, rx_buffer->map);
	}
	bus_dmamap_destroy(adapter->rxtag, adapter->rx_sparemap);
	bus_dma_tag_destroy(adapter->rxtag);

	kfree(adapter->rx_buffer_area, M_DEVBUF);
	adapter->rx_buffer_area = NULL;
}

static void
em_rxeof(struct adapter *adapter, int count)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	uint8_t status, accept_frame = 0, eop = 0;
	uint16_t len, desc_len, prev_len_adj;
	struct e1000_rx_desc *current_desc;
	struct mbuf *mp;
	int i;

	i = adapter->next_rx_desc_to_check;
	current_desc = &adapter->rx_desc_base[i];

	if (!(current_desc->status & E1000_RXD_STAT_DD))
		return;

	while ((current_desc->status & E1000_RXD_STAT_DD) && count != 0) {
		struct mbuf *m = NULL;

		logif(pkt_receive);

		mp = adapter->rx_buffer_area[i].m_head;

		/*
		 * Can't defer bus_dmamap_sync(9) because TBI_ACCEPT
		 * needs to access the last received byte in the mbuf.
		 */
		bus_dmamap_sync(adapter->rxtag, adapter->rx_buffer_area[i].map,
				BUS_DMASYNC_POSTREAD);

		accept_frame = 1;
		prev_len_adj = 0;
		desc_len = le16toh(current_desc->length);
		status = current_desc->status;
		if (status & E1000_RXD_STAT_EOP) {
			count--;
			eop = 1;
			if (desc_len < ETHER_CRC_LEN) {
				len = 0;
				prev_len_adj = ETHER_CRC_LEN - desc_len;
			} else {
				len = desc_len - ETHER_CRC_LEN;
			}
		} else {
			eop = 0;
			len = desc_len;
		}

		if (current_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK) {
			uint8_t	last_byte;
			uint32_t pkt_len = desc_len;

			if (adapter->fmp != NULL)
				pkt_len += adapter->fmp->m_pkthdr.len;

			last_byte = *(mtod(mp, caddr_t) + desc_len - 1);
			if (TBI_ACCEPT(&adapter->hw, status,
			    current_desc->errors, pkt_len, last_byte,
			    adapter->min_frame_size,
			    adapter->hw.mac.max_frame_size)) {
				e1000_tbi_adjust_stats_82543(&adapter->hw,
				    &adapter->stats, pkt_len,
				    adapter->hw.mac.addr,
				    adapter->hw.mac.max_frame_size);
				if (len > 0)
					len--;
			} else {
				accept_frame = 0;
			}
		}

		if (accept_frame) {
			if (em_newbuf(adapter, i, 0) != 0) {
				IFNET_STAT_INC(ifp, iqdrops, 1);
				goto discard;
			}

			/* Assign correct length to the current fragment */
			mp->m_len = len;

			if (adapter->fmp == NULL) {
				mp->m_pkthdr.len = len;
				adapter->fmp = mp; /* Store the first mbuf */
				adapter->lmp = mp;
			} else {
				/*
				 * Chain mbuf's together
				 */

				/*
				 * Adjust length of previous mbuf in chain if
				 * we received less than 4 bytes in the last
				 * descriptor.
				 */
				if (prev_len_adj > 0) {
					adapter->lmp->m_len -= prev_len_adj;
					adapter->fmp->m_pkthdr.len -=
					    prev_len_adj;
				}
				adapter->lmp->m_next = mp;
				adapter->lmp = adapter->lmp->m_next;
				adapter->fmp->m_pkthdr.len += len;
			}

			if (eop) {
				adapter->fmp->m_pkthdr.rcvif = ifp;
				IFNET_STAT_INC(ifp, ipackets, 1);

				if (ifp->if_capenable & IFCAP_RXCSUM) {
					em_rxcsum(adapter, current_desc,
						  adapter->fmp);
				}

				if (status & E1000_RXD_STAT_VP) {
					adapter->fmp->m_pkthdr.ether_vlantag =
					    (le16toh(current_desc->special) &
					    E1000_RXD_SPC_VLAN_MASK);
					adapter->fmp->m_flags |= M_VLANTAG;
				}
				m = adapter->fmp;
				adapter->fmp = NULL;
				adapter->lmp = NULL;
			}
		} else {
			IFNET_STAT_INC(ifp, ierrors, 1);
discard:
#ifdef foo
			/* Reuse loaded DMA map and just update mbuf chain */
			mp = adapter->rx_buffer_area[i].m_head;
			mp->m_len = mp->m_pkthdr.len = MCLBYTES;
			mp->m_data = mp->m_ext.ext_buf;
			mp->m_next = NULL;
			if (adapter->hw.mac.max_frame_size <=
			    (MCLBYTES - ETHER_ALIGN))
				m_adj(mp, ETHER_ALIGN);
#endif
			if (adapter->fmp != NULL) {
				m_freem(adapter->fmp);
				adapter->fmp = NULL;
				adapter->lmp = NULL;
			}
			m = NULL;
		}

		/* Zero out the receive descriptors status. */
		current_desc->status = 0;

		if (m != NULL)
			ifp->if_input(ifp, m, NULL, -1);

		/* Advance our pointers to the next descriptor. */
		if (++i == adapter->num_rx_desc)
			i = 0;
		current_desc = &adapter->rx_desc_base[i];
	}
	adapter->next_rx_desc_to_check = i;

	/* Advance the E1000's Receive Queue #0  "Tail Pointer". */
	if (--i < 0)
		i = adapter->num_rx_desc - 1;
	E1000_WRITE_REG(&adapter->hw, E1000_RDT(0), i);
}

static void
em_rxcsum(struct adapter *adapter, struct e1000_rx_desc *rx_desc,
	  struct mbuf *mp)
{
	/* 82543 or newer only */
	if (adapter->hw.mac.type < e1000_82543 ||
	    /* Ignore Checksum bit is set */
	    (rx_desc->status & E1000_RXD_STAT_IXSM))
		return;

	if ((rx_desc->status & E1000_RXD_STAT_IPCS) &&
	    !(rx_desc->errors & E1000_RXD_ERR_IPE)) {
		/* IP Checksum Good */
		mp->m_pkthdr.csum_flags |= CSUM_IP_CHECKED | CSUM_IP_VALID;
	}

	if ((rx_desc->status & E1000_RXD_STAT_TCPCS) &&
	    !(rx_desc->errors & E1000_RXD_ERR_TCPE)) {
		mp->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
					   CSUM_PSEUDO_HDR |
					   CSUM_FRAG_NOT_CHECKED;
		mp->m_pkthdr.csum_data = htons(0xffff);
	}
}

static void
em_enable_intr(struct adapter *adapter)
{
	uint32_t ims_mask = IMS_ENABLE_MASK;

	lwkt_serialize_handler_enable(adapter->arpcom.ac_if.if_serializer);

#if 0
	/* XXX MSIX */
	if (adapter->hw.mac.type == e1000_82574) {
		E1000_WRITE_REG(&adapter->hw, EM_EIAC, EM_MSIX_MASK);
		ims_mask |= EM_MSIX_MASK;
        }
#endif
	E1000_WRITE_REG(&adapter->hw, E1000_IMS, ims_mask);
}

static void
em_disable_intr(struct adapter *adapter)
{
	uint32_t clear = 0xffffffff;

	/*
	 * The first version of 82542 had an errata where when link was forced
	 * it would stay up even up even if the cable was disconnected.
	 * Sequence errors were used to detect the disconnect and then the
	 * driver would unforce the link.  This code in the in the ISR.  For
	 * this to work correctly the Sequence error interrupt had to be
	 * enabled all the time.
	 */
	if (adapter->hw.mac.type == e1000_82542 &&
	    adapter->hw.revision_id == E1000_REVISION_2)
		clear &= ~E1000_ICR_RXSEQ;
	else if (adapter->hw.mac.type == e1000_82574)
		E1000_WRITE_REG(&adapter->hw, EM_EIAC, 0);

	E1000_WRITE_REG(&adapter->hw, E1000_IMC, clear);

	adapter->npoll.ifpc_stcount = 0;

	lwkt_serialize_handler_disable(adapter->arpcom.ac_if.if_serializer);
}

/*
 * Bit of a misnomer, what this really means is
 * to enable OS management of the system... aka
 * to disable special hardware management features 
 */
static void
em_get_mgmt(struct adapter *adapter)
{
	/* A shared code workaround */
#define E1000_82542_MANC2H E1000_MANC2H
	if (adapter->flags & EM_FLAG_HAS_MGMT) {
		int manc2h = E1000_READ_REG(&adapter->hw, E1000_MANC2H);
		int manc = E1000_READ_REG(&adapter->hw, E1000_MANC);

		/* disable hardware interception of ARP */
		manc &= ~(E1000_MANC_ARP_EN);

                /* enable receiving management packets to the host */
                if (adapter->hw.mac.type >= e1000_82571) {
			manc |= E1000_MANC_EN_MNG2HOST;
#define E1000_MNG2HOST_PORT_623 (1 << 5)
#define E1000_MNG2HOST_PORT_664 (1 << 6)
			manc2h |= E1000_MNG2HOST_PORT_623;
			manc2h |= E1000_MNG2HOST_PORT_664;
			E1000_WRITE_REG(&adapter->hw, E1000_MANC2H, manc2h);
		}

		E1000_WRITE_REG(&adapter->hw, E1000_MANC, manc);
	}
}

/*
 * Give control back to hardware management
 * controller if there is one.
 */
static void
em_rel_mgmt(struct adapter *adapter)
{
	if (adapter->flags & EM_FLAG_HAS_MGMT) {
		int manc = E1000_READ_REG(&adapter->hw, E1000_MANC);

		/* re-enable hardware interception of ARP */
		manc |= E1000_MANC_ARP_EN;

		if (adapter->hw.mac.type >= e1000_82571)
			manc &= ~E1000_MANC_EN_MNG2HOST;

		E1000_WRITE_REG(&adapter->hw, E1000_MANC, manc);
	}
}

/*
 * em_get_hw_control() sets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded.  For AMT version (only with 82573)
 * of the f/w this means that the network i/f is open.
 */
static void
em_get_hw_control(struct adapter *adapter)
{
	/* Let firmware know the driver has taken over */
	if (adapter->hw.mac.type == e1000_82573) {
		uint32_t swsm;

		swsm = E1000_READ_REG(&adapter->hw, E1000_SWSM);
		E1000_WRITE_REG(&adapter->hw, E1000_SWSM,
		    swsm | E1000_SWSM_DRV_LOAD);
	} else {
		uint32_t ctrl_ext;

		ctrl_ext = E1000_READ_REG(&adapter->hw, E1000_CTRL_EXT);
		E1000_WRITE_REG(&adapter->hw, E1000_CTRL_EXT,
		    ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
	}
	adapter->flags |= EM_FLAG_HW_CTRL;
}

/*
 * em_rel_hw_control() resets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.  For AMT version (only with 82573)
 * of the f/w this means that the network i/f is closed.
 */
static void
em_rel_hw_control(struct adapter *adapter)
{
	if ((adapter->flags & EM_FLAG_HW_CTRL) == 0)
		return;
	adapter->flags &= ~EM_FLAG_HW_CTRL;

	/* Let firmware taken over control of h/w */
	if (adapter->hw.mac.type == e1000_82573) {
		uint32_t swsm;

		swsm = E1000_READ_REG(&adapter->hw, E1000_SWSM);
		E1000_WRITE_REG(&adapter->hw, E1000_SWSM,
		    swsm & ~E1000_SWSM_DRV_LOAD);
	} else {
		uint32_t ctrl_ext;

		ctrl_ext = E1000_READ_REG(&adapter->hw, E1000_CTRL_EXT);
		E1000_WRITE_REG(&adapter->hw, E1000_CTRL_EXT,
		    ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
	}
}

static int
em_is_valid_eaddr(const uint8_t *addr)
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
em_enable_wol(device_t dev)
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


/*
 * 82544 Coexistence issue workaround.
 *    There are 2 issues.
 *       1. Transmit Hang issue.
 *    To detect this issue, following equation can be used...
 *	  SIZE[3:0] + ADDR[2:0] = SUM[3:0].
 *	  If SUM[3:0] is in between 1 to 4, we will have this issue.
 *
 *       2. DAC issue.
 *    To detect this issue, following equation can be used...
 *	  SIZE[3:0] + ADDR[2:0] = SUM[3:0].
 *	  If SUM[3:0] is in between 9 to c, we will have this issue.
 *
 *    WORKAROUND:
 *	  Make sure we do not have ending address
 *	  as 1,2,3,4(Hang) or 9,a,b,c (DAC)
 */
static uint32_t
em_82544_fill_desc(bus_addr_t address, uint32_t length, PDESC_ARRAY desc_array)
{
	uint32_t safe_terminator;

	/*
	 * Since issue is sensitive to length and address.
	 * Let us first check the address...
	 */
	if (length <= 4) {
		desc_array->descriptor[0].address = address;
		desc_array->descriptor[0].length = length;
		desc_array->elements = 1;
		return (desc_array->elements);
	}

	safe_terminator =
	(uint32_t)((((uint32_t)address & 0x7) + (length & 0xF)) & 0xF);

	/* If it does not fall between 0x1 to 0x4 and 0x9 to 0xC then return */
	if (safe_terminator == 0 ||
	    (safe_terminator > 4 && safe_terminator < 9) ||
	    (safe_terminator > 0xC && safe_terminator <= 0xF)) {
		desc_array->descriptor[0].address = address;
		desc_array->descriptor[0].length = length;
		desc_array->elements = 1;
		return (desc_array->elements);
	}

	desc_array->descriptor[0].address = address;
	desc_array->descriptor[0].length = length - 4;
	desc_array->descriptor[1].address = address + (length - 4);
	desc_array->descriptor[1].length = 4;
	desc_array->elements = 2;
	return (desc_array->elements);
}

static void
em_update_stats(struct adapter *adapter)
{
	struct ifnet *ifp = &adapter->arpcom.ac_if;

	if (adapter->hw.phy.media_type == e1000_media_type_copper ||
	    (E1000_READ_REG(&adapter->hw, E1000_STATUS) & E1000_STATUS_LU)) {
		adapter->stats.symerrs +=
			E1000_READ_REG(&adapter->hw, E1000_SYMERRS);
		adapter->stats.sec += E1000_READ_REG(&adapter->hw, E1000_SEC);
	}
	adapter->stats.crcerrs += E1000_READ_REG(&adapter->hw, E1000_CRCERRS);
	adapter->stats.mpc += E1000_READ_REG(&adapter->hw, E1000_MPC);
	adapter->stats.scc += E1000_READ_REG(&adapter->hw, E1000_SCC);
	adapter->stats.ecol += E1000_READ_REG(&adapter->hw, E1000_ECOL);

	adapter->stats.mcc += E1000_READ_REG(&adapter->hw, E1000_MCC);
	adapter->stats.latecol += E1000_READ_REG(&adapter->hw, E1000_LATECOL);
	adapter->stats.colc += E1000_READ_REG(&adapter->hw, E1000_COLC);
	adapter->stats.dc += E1000_READ_REG(&adapter->hw, E1000_DC);
	adapter->stats.rlec += E1000_READ_REG(&adapter->hw, E1000_RLEC);
	adapter->stats.xonrxc += E1000_READ_REG(&adapter->hw, E1000_XONRXC);
	adapter->stats.xontxc += E1000_READ_REG(&adapter->hw, E1000_XONTXC);
	adapter->stats.xoffrxc += E1000_READ_REG(&adapter->hw, E1000_XOFFRXC);
	adapter->stats.xofftxc += E1000_READ_REG(&adapter->hw, E1000_XOFFTXC);
	adapter->stats.fcruc += E1000_READ_REG(&adapter->hw, E1000_FCRUC);
	adapter->stats.prc64 += E1000_READ_REG(&adapter->hw, E1000_PRC64);
	adapter->stats.prc127 += E1000_READ_REG(&adapter->hw, E1000_PRC127);
	adapter->stats.prc255 += E1000_READ_REG(&adapter->hw, E1000_PRC255);
	adapter->stats.prc511 += E1000_READ_REG(&adapter->hw, E1000_PRC511);
	adapter->stats.prc1023 += E1000_READ_REG(&adapter->hw, E1000_PRC1023);
	adapter->stats.prc1522 += E1000_READ_REG(&adapter->hw, E1000_PRC1522);
	adapter->stats.gprc += E1000_READ_REG(&adapter->hw, E1000_GPRC);
	adapter->stats.bprc += E1000_READ_REG(&adapter->hw, E1000_BPRC);
	adapter->stats.mprc += E1000_READ_REG(&adapter->hw, E1000_MPRC);
	adapter->stats.gptc += E1000_READ_REG(&adapter->hw, E1000_GPTC);

	/* For the 64-bit byte counters the low dword must be read first. */
	/* Both registers clear on the read of the high dword */

	adapter->stats.gorc += E1000_READ_REG(&adapter->hw, E1000_GORCH);
	adapter->stats.gotc += E1000_READ_REG(&adapter->hw, E1000_GOTCH);

	adapter->stats.rnbc += E1000_READ_REG(&adapter->hw, E1000_RNBC);
	adapter->stats.ruc += E1000_READ_REG(&adapter->hw, E1000_RUC);
	adapter->stats.rfc += E1000_READ_REG(&adapter->hw, E1000_RFC);
	adapter->stats.roc += E1000_READ_REG(&adapter->hw, E1000_ROC);
	adapter->stats.rjc += E1000_READ_REG(&adapter->hw, E1000_RJC);

	adapter->stats.tor += E1000_READ_REG(&adapter->hw, E1000_TORH);
	adapter->stats.tot += E1000_READ_REG(&adapter->hw, E1000_TOTH);

	adapter->stats.tpr += E1000_READ_REG(&adapter->hw, E1000_TPR);
	adapter->stats.tpt += E1000_READ_REG(&adapter->hw, E1000_TPT);
	adapter->stats.ptc64 += E1000_READ_REG(&adapter->hw, E1000_PTC64);
	adapter->stats.ptc127 += E1000_READ_REG(&adapter->hw, E1000_PTC127);
	adapter->stats.ptc255 += E1000_READ_REG(&adapter->hw, E1000_PTC255);
	adapter->stats.ptc511 += E1000_READ_REG(&adapter->hw, E1000_PTC511);
	adapter->stats.ptc1023 += E1000_READ_REG(&adapter->hw, E1000_PTC1023);
	adapter->stats.ptc1522 += E1000_READ_REG(&adapter->hw, E1000_PTC1522);
	adapter->stats.mptc += E1000_READ_REG(&adapter->hw, E1000_MPTC);
	adapter->stats.bptc += E1000_READ_REG(&adapter->hw, E1000_BPTC);

	if (adapter->hw.mac.type >= e1000_82543) {
		adapter->stats.algnerrc += 
		E1000_READ_REG(&adapter->hw, E1000_ALGNERRC);
		adapter->stats.rxerrc += 
		E1000_READ_REG(&adapter->hw, E1000_RXERRC);
		adapter->stats.tncrs += 
		E1000_READ_REG(&adapter->hw, E1000_TNCRS);
		adapter->stats.cexterr += 
		E1000_READ_REG(&adapter->hw, E1000_CEXTERR);
		adapter->stats.tsctc += 
		E1000_READ_REG(&adapter->hw, E1000_TSCTC);
		adapter->stats.tsctfc += 
		E1000_READ_REG(&adapter->hw, E1000_TSCTFC);
	}

	IFNET_STAT_SET(ifp, collisions, adapter->stats.colc);

	/* Rx Errors */
	IFNET_STAT_SET(ifp, ierrors,
	    adapter->dropped_pkts + adapter->stats.rxerrc +
	    adapter->stats.crcerrs + adapter->stats.algnerrc +
	    adapter->stats.ruc + adapter->stats.roc +
	    adapter->stats.mpc + adapter->stats.cexterr);

	/* Tx Errors */
	IFNET_STAT_SET(ifp, oerrors,
	    adapter->stats.ecol + adapter->stats.latecol +
	    adapter->watchdog_events);
}

static void
em_print_debug_info(struct adapter *adapter)
{
	device_t dev = adapter->dev;
	uint8_t *hw_addr = adapter->hw.hw_addr;

	device_printf(dev, "Adapter hardware address = %p \n", hw_addr);
	device_printf(dev, "CTRL = 0x%x RCTL = 0x%x \n",
	    E1000_READ_REG(&adapter->hw, E1000_CTRL),
	    E1000_READ_REG(&adapter->hw, E1000_RCTL));
	device_printf(dev, "Packet buffer = Tx=%dk Rx=%dk \n",
	    ((E1000_READ_REG(&adapter->hw, E1000_PBA) & 0xffff0000) >> 16),\
	    (E1000_READ_REG(&adapter->hw, E1000_PBA) & 0xffff) );
	device_printf(dev, "Flow control watermarks high = %d low = %d\n",
	    adapter->hw.fc.high_water,
	    adapter->hw.fc.low_water);
	device_printf(dev, "tx_int_delay = %d, tx_abs_int_delay = %d\n",
	    E1000_READ_REG(&adapter->hw, E1000_TIDV),
	    E1000_READ_REG(&adapter->hw, E1000_TADV));
	device_printf(dev, "rx_int_delay = %d, rx_abs_int_delay = %d\n",
	    E1000_READ_REG(&adapter->hw, E1000_RDTR),
	    E1000_READ_REG(&adapter->hw, E1000_RADV));
	device_printf(dev, "fifo workaround = %lld, fifo_reset_count = %lld\n",
	    (long long)adapter->tx_fifo_wrk_cnt,
	    (long long)adapter->tx_fifo_reset_cnt);
	device_printf(dev, "hw tdh = %d, hw tdt = %d\n",
	    E1000_READ_REG(&adapter->hw, E1000_TDH(0)),
	    E1000_READ_REG(&adapter->hw, E1000_TDT(0)));
	device_printf(dev, "hw rdh = %d, hw rdt = %d\n",
	    E1000_READ_REG(&adapter->hw, E1000_RDH(0)),
	    E1000_READ_REG(&adapter->hw, E1000_RDT(0)));
	device_printf(dev, "Num Tx descriptors avail = %d\n",
	    adapter->num_tx_desc_avail);
	device_printf(dev, "Tx Descriptors not avail1 = %ld\n",
	    adapter->no_tx_desc_avail1);
	device_printf(dev, "Tx Descriptors not avail2 = %ld\n",
	    adapter->no_tx_desc_avail2);
	device_printf(dev, "Std mbuf failed = %ld\n",
	    adapter->mbuf_alloc_failed);
	device_printf(dev, "Std mbuf cluster failed = %ld\n",
	    adapter->mbuf_cluster_failed);
	device_printf(dev, "Driver dropped packets = %ld\n",
	    adapter->dropped_pkts);
	device_printf(dev, "Driver tx dma failure in encap = %ld\n",
	    adapter->no_tx_dma_setup);
}

static void
em_print_hw_stats(struct adapter *adapter)
{
	device_t dev = adapter->dev;

	device_printf(dev, "Excessive collisions = %lld\n",
	    (long long)adapter->stats.ecol);
#if (DEBUG_HW > 0)  /* Dont output these errors normally */
	device_printf(dev, "Symbol errors = %lld\n",
	    (long long)adapter->stats.symerrs);
#endif
	device_printf(dev, "Sequence errors = %lld\n",
	    (long long)adapter->stats.sec);
	device_printf(dev, "Defer count = %lld\n",
	    (long long)adapter->stats.dc);
	device_printf(dev, "Missed Packets = %lld\n",
	    (long long)adapter->stats.mpc);
	device_printf(dev, "Receive No Buffers = %lld\n",
	    (long long)adapter->stats.rnbc);
	/* RLEC is inaccurate on some hardware, calculate our own. */
	device_printf(dev, "Receive Length Errors = %lld\n",
	    ((long long)adapter->stats.roc + (long long)adapter->stats.ruc));
	device_printf(dev, "Receive errors = %lld\n",
	    (long long)adapter->stats.rxerrc);
	device_printf(dev, "Crc errors = %lld\n",
	    (long long)adapter->stats.crcerrs);
	device_printf(dev, "Alignment errors = %lld\n",
	    (long long)adapter->stats.algnerrc);
	device_printf(dev, "Collision/Carrier extension errors = %lld\n",
	    (long long)adapter->stats.cexterr);
	device_printf(dev, "RX overruns = %ld\n", adapter->rx_overruns);
	device_printf(dev, "watchdog timeouts = %ld\n",
	    adapter->watchdog_events);
	device_printf(dev, "XON Rcvd = %lld\n",
	    (long long)adapter->stats.xonrxc);
	device_printf(dev, "XON Xmtd = %lld\n",
	    (long long)adapter->stats.xontxc);
	device_printf(dev, "XOFF Rcvd = %lld\n",
	    (long long)adapter->stats.xoffrxc);
	device_printf(dev, "XOFF Xmtd = %lld\n",
	    (long long)adapter->stats.xofftxc);
	device_printf(dev, "Good Packets Rcvd = %lld\n",
	    (long long)adapter->stats.gprc);
	device_printf(dev, "Good Packets Xmtd = %lld\n",
	    (long long)adapter->stats.gptc);
}

static void
em_print_nvm_info(struct adapter *adapter)
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
		e1000_read_nvm(&adapter->hw, i, 1, &eeprom_data);
		kprintf("%04x ", eeprom_data);
	}
	kprintf("\n");
}

static int
em_sysctl_debug_info(SYSCTL_HANDLER_ARGS)
{
	struct adapter *adapter;
	struct ifnet *ifp;
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	adapter = (struct adapter *)arg1;
	ifp = &adapter->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (result == 1)
		em_print_debug_info(adapter);

	/*
	 * This value will cause a hex dump of the
	 * first 32 16-bit words of the EEPROM to
	 * the screen.
	 */
	if (result == 2)
		em_print_nvm_info(adapter);

	lwkt_serialize_exit(ifp->if_serializer);

	return (error);
}

static int
em_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	if (result == 1) {
		struct adapter *adapter = (struct adapter *)arg1;
		struct ifnet *ifp = &adapter->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		em_print_hw_stats(adapter);
		lwkt_serialize_exit(ifp->if_serializer);
	}
	return (error);
}

static void
em_add_sysctl(struct adapter *adapter)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(adapter->dev);
	tree = device_get_sysctl_tree(adapter->dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "debug", CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
	    em_sysctl_debug_info, "I", "Debug Information");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "stats", CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
	    em_sysctl_stats, "I", "Statistics");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "rxd", CTLFLAG_RD,
	    &adapter->num_rx_desc, 0, NULL);
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "txd", CTLFLAG_RD,
	    &adapter->num_tx_desc, 0, NULL);

	if (adapter->hw.mac.type >= e1000_82540) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
		    OID_AUTO, "int_throttle_ceil",
		    CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
		    em_sysctl_int_throttle, "I",
		    "interrupt throttling rate");
	}
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "int_tx_nsegs",
	    CTLTYPE_INT|CTLFLAG_RW, adapter, 0,
	    em_sysctl_int_tx_nsegs, "I",
	    "# segments per TX interrupt");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "wreg_tx_nsegs", CTLFLAG_RW,
	    &adapter->tx_wreg_nsegs, 0,
	    "# segments before write to hardware register");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_nmbuf",
	    CTLFLAG_RD, &adapter->tx_nmbuf, 0, "# of pending TX mbufs");
	SYSCTL_ADD_ULONG(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_gc",
	    CTLFLAG_RW, &adapter->tx_gc, "# of TX GC");
}

static int
em_sysctl_int_throttle(SYSCTL_HANDLER_ARGS)
{
	struct adapter *adapter = (void *)arg1;
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	int error, throttle;

	throttle = adapter->int_throttle_ceil;
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

	lwkt_serialize_enter(ifp->if_serializer);

	if (throttle)
		adapter->int_throttle_ceil = 1000000000 / 256 / throttle;
	else
		adapter->int_throttle_ceil = 0;

	if (ifp->if_flags & IFF_RUNNING)
		em_set_itr(adapter, throttle);

	lwkt_serialize_exit(ifp->if_serializer);

	if (bootverbose) {
		if_printf(ifp, "Interrupt moderation set to %d/sec\n",
			  adapter->int_throttle_ceil);
	}
	return 0;
}

static int
em_sysctl_int_tx_nsegs(SYSCTL_HANDLER_ARGS)
{
	struct adapter *adapter = (void *)arg1;
	struct ifnet *ifp = &adapter->arpcom.ac_if;
	int error, segs;

	segs = adapter->tx_int_nsegs;
	error = sysctl_handle_int(oidp, &segs, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (segs <= 0)
		return EINVAL;

	lwkt_serialize_enter(ifp->if_serializer);

	/*
	 * Don't allow int_tx_nsegs to become:
	 * o  Less the oact_tx_desc
	 * o  Too large that no TX desc will cause TX interrupt to
	 *    be generated (OACTIVE will never recover)
	 * o  Too small that will cause tx_dd[] overflow
	 */
	if (segs < adapter->oact_tx_desc ||
	    segs >= adapter->num_tx_desc - adapter->oact_tx_desc ||
	    segs < adapter->num_tx_desc / EM_TXDD_SAFE) {
		error = EINVAL;
	} else {
		error = 0;
		adapter->tx_int_nsegs = segs;
	}

	lwkt_serialize_exit(ifp->if_serializer);

	return error;
}

static void
em_set_itr(struct adapter *adapter, uint32_t itr)
{
	E1000_WRITE_REG(&adapter->hw, E1000_ITR, itr);
	if (adapter->hw.mac.type == e1000_82574) {
		int i;

		/*
		 * When using MSIX interrupts we need to
		 * throttle using the EITR register
		 */
		for (i = 0; i < 4; ++i) {
			E1000_WRITE_REG(&adapter->hw,
			    E1000_EITR_82574(i), itr);
		}
	}
}

static void
em_disable_aspm(struct adapter *adapter)
{
	uint16_t link_cap, link_ctrl, disable;
	uint8_t pcie_ptr, reg;
	device_t dev = adapter->dev;

	switch (adapter->hw.mac.type) {
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
	case e1000_82583:
		/*
		 * 82574 specification update errata #20
		 * 82583 specification update errata #9
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

	if (bootverbose) {
		if_printf(&adapter->arpcom.ac_if,
		    "disable ASPM %#02x\n", disable);
	}

	reg = pcie_ptr + PCIER_LINKCTRL;
	link_ctrl = pci_read_config(dev, reg, 2);
	link_ctrl &= ~disable;
	pci_write_config(dev, reg, link_ctrl, 2);
}

static int
em_tso_pullup(struct adapter *adapter, struct mbuf **mp)
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

	if (adapter->flags & EM_FLAG_TSO_PULLEX)
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
em_tso_setup(struct adapter *adapter, struct mbuf *mp,
    uint32_t *txd_upper, uint32_t *txd_lower)
{
	struct e1000_context_desc *TXD;
	int hoff, iphlen, thoff, hlen;
	int mss, pktlen, curr_txd;

	iphlen = mp->m_pkthdr.csum_iphlen;
	thoff = mp->m_pkthdr.csum_thlen;
	hoff = mp->m_pkthdr.csum_lhlen;
	mss = mp->m_pkthdr.tso_segsz;
	pktlen = mp->m_pkthdr.len;

	if (adapter->csum_flags == CSUM_TSO &&
	    adapter->csum_iphlen == iphlen &&
	    adapter->csum_lhlen == hoff &&
	    adapter->csum_thlen == thoff &&
	    adapter->csum_mss == mss &&
	    adapter->csum_pktlen == pktlen) {
		*txd_upper = adapter->csum_txd_upper;
		*txd_lower = adapter->csum_txd_lower;
		return 0;
	}
	hlen = hoff + iphlen + thoff;

	/*
	 * Setup a new TSO context.
	 */

	curr_txd = adapter->next_avail_tx_desc;
	TXD = (struct e1000_context_desc *)&adapter->tx_desc_base[curr_txd];

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
	adapter->csum_flags = CSUM_TSO;
	adapter->csum_lhlen = hoff;
	adapter->csum_iphlen = iphlen;
	adapter->csum_thlen = thoff;
	adapter->csum_mss = mss;
	adapter->csum_pktlen = pktlen;
	adapter->csum_txd_upper = *txd_upper;
	adapter->csum_txd_lower = *txd_lower;

	if (++curr_txd == adapter->num_tx_desc)
		curr_txd = 0;

	KKASSERT(adapter->num_tx_desc_avail > 0);
	adapter->num_tx_desc_avail--;

	adapter->next_avail_tx_desc = curr_txd;
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
em_flush_tx_ring(struct adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_desc *txd;
	uint32_t tctl;

	tctl = E1000_READ_REG(hw, E1000_TCTL);
	E1000_WRITE_REG(hw, E1000_TCTL, tctl | E1000_TCTL_EN);

	txd = &adapter->tx_desc_base[adapter->next_avail_tx_desc++];
	if (adapter->next_avail_tx_desc == adapter->num_tx_desc)
		adapter->next_avail_tx_desc = 0;

	/* Just use the ring as a dummy buffer addr */
	txd->buffer_addr = adapter->txdma.dma_paddr;
	txd->lower.data = htole32(E1000_TXD_CMD_IFCS | 512);
	txd->upper.data = 0;

	E1000_WRITE_REG(hw, E1000_TDT(0), adapter->next_avail_tx_desc);
	usec_delay(250);
}

/*
 * Remove all descriptors from the RX ring.
 *
 * Mark all descriptors in the RX ring as consumed and disable the RX ring.
 */
static void
em_flush_rx_ring(struct adapter *adapter)
{
	struct e1000_hw	*hw = &adapter->hw;
	uint32_t rctl, rxdctl;

	rctl = E1000_READ_REG(hw, E1000_RCTL);
	E1000_WRITE_REG(hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
	E1000_WRITE_FLUSH(hw);
	usec_delay(150);

	rxdctl = E1000_READ_REG(hw, E1000_RXDCTL(0));
	/* Zero the lower 14 bits (prefetch and host thresholds) */
	rxdctl &= 0xffffc000;
	/*
	 * Update thresholds: prefetch threshold to 31, host threshold to 1
	 * and make sure the granularity is "descriptors" and not "cache
	 * lines".
	 */
	rxdctl |= (0x1F | (1 << 8) | E1000_RXDCTL_THRESH_UNIT_DESC);
	E1000_WRITE_REG(hw, E1000_RXDCTL(0), rxdctl);

	/* Momentarily enable the RX ring for the changes to take effect */
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
em_flush_txrx_ring(struct adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	device_t dev = adapter->dev;
	uint16_t hang_state;
	uint32_t fext_nvm11;

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
	hang_state = pci_read_config(dev, PCICFG_DESC_RING_STATUS, 2);
	if ((hang_state & FLUSH_DESC_REQUIRED) &&
	    E1000_READ_REG(hw, E1000_TDLEN(0)))
		em_flush_tx_ring(adapter);

	/*
	 * Recheck, maybe the fault is caused by the RX ring.
	 */
	hang_state = pci_read_config(dev, PCICFG_DESC_RING_STATUS, 2);
	if (hang_state & FLUSH_DESC_REQUIRED)
		em_flush_rx_ring(adapter);
}
