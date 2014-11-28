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

#ifndef _IF_IX_H_
#define _IF_IX_H_

/* Tunables */

/*
 * MSI-X count
 */
#define IX_MAX_MSIX		64
#define IX_MAX_MSIX_82598	16

/*
 * RX ring count
 */
#define IX_MAX_RXRING		16
#define IX_MIN_RXRING_RSS	2

/*
 * TX ring count
 */
#define IX_MAX_TXRING_82598	32
#define IX_MAX_TXRING_82599	64
#define IX_MAX_TXRING_X540	64

/*
 * Default number of segments received before writing to RX related registers
 */
#define IX_DEF_RXWREG_NSEGS	32

/*
 * Default number of segments sent before writing to TX related registers
 */
#define IX_DEF_TXWREG_NSEGS	8

/*
 * TxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of transmit descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more transmits. Each descriptor is 16
 * bytes. Performance tests have show the 2K value to be optimal for top
 * performance.
 */
#define IX_DEF_TXD		1024
#define IX_PERF_TXD		2048
#define IX_MAX_TXD		4096
#define IX_MIN_TXD		64

/*
 * RxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of receive descriptors allocated for each RX queue. Increasing this
 * value allows the driver to buffer more incoming packets. Each descriptor
 * is 16 bytes.  A receive buffer is also allocated for each descriptor. 
 * 
 * Note: with 8 rings and a dual port card, it is possible to bump up 
 *	against the system mbuf pool limit, you can tune nmbclusters
 *	to adjust for this.
 */
#define IX_DEF_RXD		1024
#define IX_PERF_RXD		2048
#define IX_MAX_RXD		4096
#define IX_MIN_RXD		64

/* Alignment for rings */
#define IX_DBA_ALIGN		128

#define IX_MAX_FRAME_SIZE	0x3F00

/* Flow control constants */
#define IX_FC_PAUSE		0xFFFF
#define IX_FC_HI		0x20000
#define IX_FC_LO		0x10000

/*
 * RSS related registers
 */
#define IX_NRSSRK		10
#define IX_RSSRK_SIZE		4
#define IX_RSSRK_VAL(key, i)	(key[(i) * IX_RSSRK_SIZE] | \
				 key[(i) * IX_RSSRK_SIZE + 1] << 8 | \
				 key[(i) * IX_RSSRK_SIZE + 2] << 16 | \
				 key[(i) * IX_RSSRK_SIZE + 3] << 24)
#define IX_NRETA		32
#define IX_RETA_SIZE		4

/*
 * EITR
 */
#define IX_EITR_INTVL_MASK_82598 0xffff
#define IX_EITR_INTVL_MASK	0x0fff
#define IX_EITR_INTVL_RSVD_MASK	0x0007
#define IX_EITR_INTVL_MIN	IXGBE_MIN_EITR
#define IX_EITR_INTVL_MAX	IXGBE_MAX_EITR

/*
 * Used for optimizing small rx mbufs.  Effort is made to keep the copy
 * small and aligned for the CPU L1 cache.
 * 
 * MHLEN is typically 168 bytes, giving us 8-byte alignment.  Getting
 * 32 byte alignment needed for the fast bcopy results in 8 bytes being
 * wasted.  Getting 64 byte alignment, which _should_ be ideal for
 * modern Intel CPUs, results in 40 bytes wasted and a significant drop
 * in observed efficiency of the optimization, 97.9% -> 81.8%.
 */
#define IX_RX_COPY_LEN		160
#define IX_RX_COPY_ALIGN	(MHLEN - IX_RX_COPY_LEN)

#define IX_MAX_MCASTADDR	128

#define IX_MSIX_BAR_82598	3
#define IX_MSIX_BAR_82599	4

#define IX_TSO_SIZE		(IP_MAXPACKET + \
				 sizeof(struct ether_vlan_header))

/*
 * MUST be less than 38.  Though 82598 does not have this limit,
 * we don't want long TX chain.  33 should be large enough even
 * for 64K TSO (32 x 2K mbuf cluster and 1 x mbuf header).
 *
 * Reference:
 * - 82599 datasheet 7.2.1.1
 * - X540 datasheet 7.2.1.1
 */
#define IX_MAX_SCATTER		33
#define IX_TX_RESERVED		3	/* 1 for TX ctx, 2 reserved */

/* MSI and legacy interrupt */
#define IX_TX_INTR_VEC		0
#define IX_TX_INTR_MASK		(1 << IX_TX_INTR_VEC)
#define IX_RX0_INTR_VEC		1
#define IX_RX0_INTR_MASK	(1 << IX_RX0_INTR_VEC)
#define IX_RX1_INTR_VEC		2
#define IX_RX1_INTR_MASK	(1 << IX_RX1_INTR_VEC)

#define IX_INTR_RATE		8000
#define IX_MSIX_RX_RATE		8000
#define IX_MSIX_TX_RATE		6000

/* IOCTL define to gather SFP+ Diagnostic data */
#define SIOCGI2C		SIOCGIFGENERIC

/* TX checksum offload */
#define CSUM_OFFLOAD		(CSUM_IP|CSUM_TCP|CSUM_UDP)

#define IX_EICR_STATUS		(IXGBE_EICR_LSC | IXGBE_EICR_ECC | \
				 IXGBE_EICR_GPI_SDP1 | IXGBE_EICR_GPI_SDP2 | \
				 IXGBE_EICR_TS)

/* This is used to get SFP+ module data */
struct ix_i2c_req {
	uint8_t		dev_addr;
	uint8_t		offset;
	uint8_t		len;
	uint8_t		data[8];
};

struct ix_tx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};

struct ix_rx_buf {
	struct mbuf	*m_head;
	struct mbuf	*fmp;
	struct mbuf	*lmp;
	bus_dmamap_t	map;
	bus_addr_t	paddr;
	u_int		flags;
#define IX_RX_COPY	0x1
};

struct ix_softc;

struct ix_tx_ring {
	struct lwkt_serialize	tx_serialize;
	struct ifaltq_subque	*tx_ifsq;
	struct ix_softc		*tx_sc;
	volatile uint32_t	*tx_hdr;
	union ixgbe_adv_tx_desc	*tx_base;
	struct ix_tx_buf	*tx_buf;
	bus_dma_tag_t		tx_tag;
	uint16_t		tx_flags;
#define IX_TXFLAG_ENABLED	0x1
	uint16_t		tx_pad;
	uint32_t		tx_idx;
	uint16_t		tx_avail;
	uint16_t		tx_next_avail;
	uint16_t		tx_next_clean;
	uint16_t		tx_ndesc;
	uint16_t		tx_wreg_nsegs;
	uint16_t		tx_intr_nsegs;
	uint16_t		tx_nsegs;
	int16_t			tx_intr_vec;
	int			tx_intr_cpuid;
	uint32_t		tx_eims;
	uint32_t		tx_eims_val;
	struct ifsubq_watchdog	tx_watchdog;

	bus_dma_tag_t		tx_base_dtag;
	bus_dmamap_t		tx_base_map;
	bus_addr_t		tx_base_paddr;

	bus_dma_tag_t		tx_hdr_dtag;
	bus_dmamap_t		tx_hdr_map;
	bus_addr_t		tx_hdr_paddr;
} __cachealign;

struct ix_rx_ring {
	struct lwkt_serialize	rx_serialize;
	struct ix_softc		*rx_sc;
	union ixgbe_adv_rx_desc	*rx_base;
	struct ix_rx_buf	*rx_buf;
	bus_dma_tag_t		rx_tag;
	bus_dmamap_t		rx_sparemap;
	uint32_t		rx_idx;
	uint16_t		rx_flags;
#define IX_RXRING_FLAG_LRO	0x01
#define IX_RXRING_FLAG_DISC	0x02
	uint16_t 		rx_next_check;
	uint16_t		rx_ndesc;
	uint16_t		rx_mbuf_sz;
	uint16_t		rx_wreg_nsegs;
	int16_t			rx_intr_vec;
	uint32_t		rx_eims;
	uint32_t		rx_eims_val;
	struct ix_tx_ring	*rx_txr;	/* piggybacked TX ring */

#ifdef IX_RSS_DEBUG
	u_long			rx_pkts;
#endif

	bus_dma_tag_t		rx_base_dtag;
	bus_dmamap_t		rx_base_map;
	bus_addr_t		rx_base_paddr;
} __cachealign;

struct ix_intr_data {
	struct lwkt_serialize	*intr_serialize;
	driver_intr_t		*intr_func;
	void			*intr_hand;
	struct resource		*intr_res;
	void			*intr_funcarg;
	int			intr_rid;
	int			intr_cpuid;
	int			intr_rate;
	int			intr_use;
#define IX_INTR_USE_RXTX	0
#define IX_INTR_USE_STATUS	1
#define IX_INTR_USE_RX		2
#define IX_INTR_USE_TX		3
	const char		*intr_desc;
	char			intr_desc0[64];
};

struct ix_softc {
	struct arpcom		arpcom;

	struct ixgbe_hw		hw;
	struct ixgbe_osdep	osdep;

	struct lwkt_serialize	main_serialize;
	uint32_t		intr_mask;

	boolean_t		link_active;

	int			rx_ring_inuse;
	int			tx_ring_inuse;

	struct ix_rx_ring	*rx_rings;
	struct ix_tx_ring	*tx_rings;

	struct callout		timer;
	int			timer_cpuid;

	uint32_t		optics;
	uint32_t		fc;		/* local flow ctrl setting */
	uint32_t		link_speed;
	bool			link_up;
	boolean_t		sfp_probe;	/* plyggable optics */

	struct ixgbe_hw_stats 	stats;

	int			rx_ring_cnt;
	int			rx_ring_msix;

	int			tx_ring_cnt;
	int			tx_ring_msix;

	int			intr_type;
	int			intr_cnt;
	struct ix_intr_data	*intr_data;

	device_t		dev;
	bus_dma_tag_t		parent_tag;
	struct ifmedia		media;

	struct resource		*mem_res;
	int			mem_rid;

	struct resource 	*msix_mem_res;
	int			msix_mem_rid;

	int			nserialize;
	struct lwkt_serialize	**serializes;

	uint8_t			*mta;		/* Multicast array memory */

	int			if_flags;
	int			advspeed;	/* advertised link speeds */
	uint16_t		max_frame_size;
	int16_t			sts_msix_vec;	/* status MSI-X vector */

	int			rx_npoll_off;
	int			tx_npoll_off;

#ifdef IX_RSS_DEBUG
	int			rss_debug;
#endif
};

#define IX_ENABLE_HWRSS(sc)	((sc)->rx_ring_cnt > 1)
#define IX_ENABLE_HWTSS(sc)	((sc)->tx_ring_cnt > 1)

#endif /* _IF_IX_H_ */
