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

#ifndef _IF_IGB_H_
#define _IF_IGB_H_

/* Tunables */

/*
 * Max ring count
 */
#define IGB_MAX_RING_I210	4
#define IGB_MAX_RING_I211	2
#define IGB_MAX_RING_I350	8
#define IGB_MAX_RING_I354	8
#define IGB_MAX_RING_82580	8
#define IGB_MAX_RING_82576	16
#define IGB_MAX_RING_82575	4
#define IGB_MIN_RING		1
#define IGB_MIN_RING_RSS	2

/*
 * Max TX/RX interrupt bits
 */
#define IGB_MAX_TXRXINT_I210	4
#define IGB_MAX_TXRXINT_I211	4
#define IGB_MAX_TXRXINT_I350	8
#define IGB_MAX_TXRXINT_I354	8
#define IGB_MAX_TXRXINT_82580	8
#define IGB_MAX_TXRXINT_82576	16
#define IGB_MAX_TXRXINT_82575	4	/* XXX not used */
#define IGB_MIN_TXRXINT		2	/* XXX VF? */

/*
 * Max IVAR count
 */
#define IGB_MAX_IVAR_I210	4
#define IGB_MAX_IVAR_I211	4
#define IGB_MAX_IVAR_I350	4
#define IGB_MAX_IVAR_I354	4
#define IGB_MAX_IVAR_82580	4
#define IGB_MAX_IVAR_82576	8
#define IGB_MAX_IVAR_VF		1

/*
 * Default number of segments received before writing to RX related registers
 */
#define IGB_DEF_RXWREG_NSEGS	32

/*
 * Default number of segments sent before writing to TX related registers
 */
#define IGB_DEF_TXWREG_NSEGS	8

/*
 * IGB_TXD: Maximum number of Transmit Descriptors
 *
 *   This value is the number of transmit descriptors allocated by the driver.
 *   Increasing this value allows the driver to queue more transmits. Each
 *   descriptor is 16 bytes.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define IGB_MIN_TXD		256
#define IGB_DEFAULT_TXD		1024
#define IGB_MAX_TXD		4096

/*
 * IGB_RXD: Maximum number of Transmit Descriptors
 *
 *   This value is the number of receive descriptors allocated by the driver.
 *   Increasing this value allows the driver to buffer more incoming packets.
 *   Each descriptor is 16 bytes.  A receive buffer is also allocated for each
 *   descriptor. The maximum MTU size is 16110.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define IGB_MIN_RXD		256
#define IGB_DEFAULT_RXD		512
#define IGB_MAX_RXD		4096

/*
 * This parameter controls when the driver calls the routine to reclaim
 * transmit descriptors. Cleaning earlier seems a win.
 */
#define IGB_TX_CLEANUP_THRESHOLD(sc)	((sc)->num_tx_desc / 2)

/*
 * This parameter controls whether or not autonegotation is enabled.
 *              0 - Disable autonegotiation
 *              1 - Enable  autonegotiation
 */
#define DO_AUTO_NEG		1

/*
 * This parameter control whether or not the driver will wait for
 * autonegotiation to complete.
 *              1 - Wait for autonegotiation to complete
 *              0 - Don't wait for autonegotiation to complete
 */
#define WAIT_FOR_AUTO_NEG_DEFAULT	0

/* Tunables -- End */

#define AUTONEG_ADV_DEFAULT	(ADVERTISE_10_HALF | ADVERTISE_10_FULL | \
				 ADVERTISE_100_HALF | ADVERTISE_100_FULL | \
				 ADVERTISE_1000_FULL)

#define AUTO_ALL_MODES			0

/* PHY master/slave setting */
#define IGB_MASTER_SLAVE		e1000_ms_hw_default

/*
 * Micellaneous constants
 */
#define IGB_VENDOR_ID			0x8086

#define IGB_JUMBO_PBA			0x00000028
#define IGB_DEFAULT_PBA			0x00000030
#define IGB_SMARTSPEED_DOWNSHIFT	3
#define IGB_SMARTSPEED_MAX		15
#define IGB_MAX_LOOP			10

#define IGB_RX_PTHRESH			((hw->mac.type == e1000_i354) ? 12 : \
					  ((hw->mac.type <= e1000_82576) ? 16 : 8))
#define IGB_RX_HTHRESH			8
#define IGB_RX_WTHRESH			((hw->mac.type == e1000_82576 && \
					  sc->msix_mem_res) ? 1 : 4)

#define IGB_TX_PTHRESH			((hw->mac.type == e1000_i354) ? 20 : 8)
#define IGB_TX_HTHRESH			1
#define IGB_TX_WTHRESH			16

#define MAX_NUM_MULTICAST_ADDRESSES	128
#define IGB_FC_PAUSE_TIME		0x0680

#define IGB_INTR_RATE			6000
#define IGB_MSIX_RX_RATE		6000
#define IGB_MSIX_TX_RATE		4000

/*
 * TDBA/RDBA should be aligned on 16 byte boundary. But TDLEN/RDLEN should be
 * multiple of 128 bytes. So we align TDBA/RDBA on 128 byte boundary. This will
 * also optimize cache line size effect. H/W supports up to cache line size 128.
 */
#define IGB_DBA_ALIGN			128

/* PCI Config defines */
#define IGB_MSIX_BAR			3
#define IGB_MSIX_BAR_ALT		4

#define IGB_VFTA_SIZE			128
#define IGB_TSO_SIZE			(IP_MAXPACKET + \
					 sizeof(struct ether_vlan_header))
#define IGB_HDR_BUF			128
#define IGB_TXPBSIZE			20408
#define IGB_PKTTYPE_MASK		0x0000FFF0

#define IGB_CSUM_FEATURES		(CSUM_IP | CSUM_TCP | CSUM_UDP)

/* One for TX csum offloading desc, the other 2 are reserved */
#define IGB_TX_RESERVED			3

/* Large enough for 64K TSO */
#define IGB_MAX_SCATTER			33

#define IGB_NRSSRK			10
#define IGB_RSSRK_SIZE			4
#define IGB_RSSRK_VAL(key, i)		(key[(i) * IGB_RSSRK_SIZE] | \
					 key[(i) * IGB_RSSRK_SIZE + 1] << 8 | \
					 key[(i) * IGB_RSSRK_SIZE + 2] << 16 | \
					 key[(i) * IGB_RSSRK_SIZE + 3] << 24)

#define IGB_NRETA			32
#define IGB_RETA_SIZE			4
#define IGB_RETA_SHIFT			0
#define IGB_RETA_SHIFT_82575		6

#define IGB_RDRTABLE_SIZE		(IGB_NRETA * IGB_RETA_SIZE)

#define IGB_EITR_INTVL_MASK		0x7ffc
#define IGB_EITR_INTVL_SHIFT		2

/* Disable DMA Coalesce Flush */
#define IGB_DMCTLX_DCFLUSH_DIS		0x80000000

struct igb_softc;

/*
 * Bus dma information structure
 */
struct igb_dma {
	bus_addr_t		dma_paddr;
	void			*dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
};

/*
 * Transmit ring: one per queue
 */
struct igb_tx_ring {
	struct lwkt_serialize	tx_serialize;
	struct igb_softc	*sc;
	struct ifaltq_subque	*ifsq;
	uint32_t		me;
	uint32_t		tx_flags;
#define IGB_TXFLAG_TSO_IPLEN0	0x1
#define IGB_TXFLAG_ENABLED	0x2
	struct e1000_tx_desc	*tx_base;
	int			num_tx_desc;
	uint32_t		next_avail_desc;
	uint32_t		next_to_clean;
	uint32_t		*tx_hdr;
	int			tx_avail;
	struct igb_tx_buf	*tx_buf;
	bus_dma_tag_t		tx_tag;
	int			tx_nsegs;
	int			intr_nsegs;
	int			wreg_nsegs;
	int			tx_intr_vec;
	uint32_t		tx_intr_mask;
	struct ifsubq_watchdog	tx_watchdog;

	/* Soft stats */
	u_long			tx_packets;

	struct igb_dma		txdma;
	bus_dma_tag_t		tx_hdr_dtag;
	bus_dmamap_t		tx_hdr_dmap;
	bus_addr_t		tx_hdr_paddr;
	int			tx_intr_cpuid;
} __cachealign;

/*
 * Receive ring: one per queue
 */
struct igb_rx_ring {
	struct lwkt_serialize	rx_serialize;
	struct igb_softc	*sc;
	uint32_t		me;
	union e1000_adv_rx_desc	*rx_base;
	boolean_t		discard;
	int			num_rx_desc;
	uint32_t		next_to_check;
	struct igb_rx_buf	*rx_buf;
	bus_dma_tag_t		rx_tag;
	bus_dmamap_t		rx_sparemap;
	int			rx_intr_vec;
	uint32_t		rx_intr_mask;

	/*
	 * First/last mbuf pointers, for
	 * collecting multisegment RX packets.
	 */
	struct mbuf		*fmp;
	struct mbuf		*lmp;
	int			wreg_nsegs;

	struct igb_tx_ring	*rx_txr;	/* piggybacked TX ring */

	/* Soft stats */
	u_long			rx_packets;

	struct igb_dma		rxdma;
} __cachealign;

struct igb_intr_data {
	struct lwkt_serialize	*intr_serialize;
	driver_intr_t		*intr_func;
	void			*intr_hand;
	struct resource		*intr_res;
	void			*intr_funcarg;
	int			intr_rid;
	int			intr_cpuid;
	int			intr_rate;
	int			intr_use;
#define IGB_INTR_USE_RXTX	0
#define IGB_INTR_USE_STATUS	1
#define IGB_INTR_USE_RX		2
#define IGB_INTR_USE_TX		3
	const char		*intr_desc;
	char			intr_desc0[64];
};

struct igb_softc {
	struct arpcom		arpcom;
	struct e1000_hw		hw;

	struct e1000_osdep	osdep;
	device_t		dev;
	uint32_t		flags;
#define IGB_FLAG_SHARED_INTR	0x1
#define IGB_FLAG_HAS_MGMT	0x2

	bus_dma_tag_t		parent_tag;

	int			mem_rid;
	struct resource 	*mem_res;

	struct ifmedia		media;
	struct callout		timer;
	int			timer_cpuid;

	int			if_flags;
	int			max_frame_size;
	int			pause_frames;
	uint16_t		vf_ifp;	/* a VF interface */

	/* Management and WOL features */
	int			wol;

	/* Info about the interface */
	uint8_t			link_active;
	uint16_t		link_speed;
	uint16_t		link_duplex;
	uint32_t		smartspeed;
	uint32_t		dma_coalesce;

	/* Multicast array pointer */
	uint8_t			*mta;

	int			serialize_cnt;
	struct lwkt_serialize	**serializes;
	struct lwkt_serialize	main_serialize;

	int			intr_type;
	uint32_t		intr_mask;
	int			sts_msix_vec;
	uint32_t		sts_intr_mask;

	/*
	 * Transmit rings
	 */
	int			tx_ring_cnt;
	int			tx_ring_msix;
	int			tx_ring_inuse;
	struct igb_tx_ring	*tx_rings;

	/*
	 * Receive rings
	 */
	int			rss_debug;
	int			rx_ring_cnt;
	int			rx_ring_msix;
	int			rx_ring_inuse;
	struct igb_rx_ring	*rx_rings;

	int			ifm_flowctrl;

	/* Misc stats maintained by the driver */
	u_long			dropped_pkts;
	u_long			mbuf_defrag_failed;
	u_long			no_tx_dma_setup;
	u_long			watchdog_events;
	u_long			rx_overruns;
	u_long			device_control;
	u_long			rx_control;
	u_long			int_mask;
	u_long			eint_mask;
	u_long			packet_buf_alloc_rx;
	u_long			packet_buf_alloc_tx;

	void 			*stats;

	int			msix_mem_rid;
	struct resource 	*msix_mem_res;

	int			intr_cnt;
	struct igb_intr_data	*intr_data;

	struct if_ringmap	*rx_rmap;
	struct if_ringmap	*rx_rmap_intr;
	struct if_ringmap	*tx_rmap;
	struct if_ringmap	*tx_rmap_intr;

	int			rdr_table[IGB_RDRTABLE_SIZE];
};

#define IGB_ENABLE_HWRSS(sc)	((sc)->rx_ring_cnt > 1)
#define IGB_ENABLE_HWTSS(sc)	((sc)->tx_ring_cnt > 1)

struct igb_tx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;		/* bus_dma map for packet */
};

struct igb_rx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;	/* bus_dma map for packet */
	bus_addr_t	paddr;
};

#define UPDATE_VF_REG(reg, last, cur)		\
{						\
	uint32_t new = E1000_READ_REG(hw, reg);	\
	if (new < last)				\
		cur += 0x100000000LL;		\
	last = new;				\
	cur &= 0xFFFFFFFF00000000LL;		\
	cur |= new;				\
}

#define IGB_I210_LINK_DELAY	1000	/* unit: ms */

#endif /* _IF_IGB_H_ */
