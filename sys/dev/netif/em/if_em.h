/*
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
 */

#ifndef _IF_EM_H_
#define _IF_EM_H_

/* Tunables */

/*
 * EM_TXD: Maximum number of Transmit Descriptors
 * Valid Range: 256 for 82542 and 82543-based adapters
 *              256-4096 for others
 * Default Value: 256
 *   This value is the number of transmit descriptors allocated by the driver.
 *   Increasing this value allows the driver to queue more transmits. Each
 *   descriptor is 16 bytes.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define EM_MIN_TXD			256
#define EM_MAX_TXD_82543		EM_MIN_TXD
#define EM_MAX_TXD			4096
#define EM_DEFAULT_TXD			512

/*
 * EM_RXD - Maximum number of receive Descriptors
 * Valid Range: 256 for 82542 and 82543-based adapters
 *              256-4096 for others
 * Default Value: 256
 *   This value is the number of receive descriptors allocated by the driver.
 *   Increasing this value allows the driver to buffer more incoming packets.
 *   Each descriptor is 16 bytes.  A receive buffer is also allocated for each
 *   descriptor. The maximum MTU size is 16110.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   desscriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct e1000_tx_desc)) % 128 == 0
 */
#define EM_MIN_RXD			256
#define EM_MAX_RXD_82543		EM_MIN_RXD
#define EM_MAX_RXD			4096
#define EM_DEFAULT_RXD			512

/*
 * EM_TIDV - Transmit Interrupt Delay Value
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value delays the generation of transmit interrupts in units of
 *   1.024 microseconds. Transmit interrupt reduction can improve CPU
 *   efficiency if properly tuned for specific network traffic. If the
 *   system is reporting dropped transmits, this value may be set too high
 *   causing the driver to run out of available transmit descriptors.
 *
 * NOTE:
 * It is not used.  In DragonFly the TX interrupt moderation is done by
 * conditionally setting RS bit in TX descriptors.  See the description
 * in struct adapter.
 */
#define EM_TIDV				64

/*
 * EM_TADV - Transmit Absolute Interrupt Delay Value
 * (Not valid for 82542/82543/82544)
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value, in units of 1.024 microseconds, limits the delay in which a
 *   transmit interrupt is generated. Useful only if EM_TIDV is non-zero,
 *   this value ensures that an interrupt is generated after the initial
 *   packet is sent on the wire within the set amount of time.  Proper tuning,
 *   along with EM_TIDV, may improve traffic throughput in specific
 *   network conditions.
 *
 * NOTE:
 * It is not used.  In DragonFly the TX interrupt moderation is done by
 * conditionally setting RS bit in TX descriptors.  See the description
 * in struct adapter.
 */
#define EM_TADV				64

/*
 * Receive Interrupt Delay Timer (Packet Timer)
 *
 * NOTE:
 * RDTR and RADV are deprecated; use ITR instead.  They are only used to
 * workaround hardware bug on certain 82573 based NICs.
 */
#define EM_RDTR_82573			32

/*
 * Receive Interrupt Absolute Delay Timer (Not valid for 82542/82543/82544)
 *
 * NOTE:
 * RDTR and RADV are deprecated; use ITR instead.  They are only used to
 * workaround hardware bug on certain 82573 based NICs.
 */
#define EM_RADV_82573			64

/*
 * This parameter controls the duration of transmit watchdog timer.
 */
#define EM_TX_TIMEOUT			5

/* One for TX csum offloading desc, the other 2 are reserved */
#define EM_TX_RESERVED			3

/* Large enough for 16K jumbo frame */
#define EM_TX_SPARE			8
/* Large enough for 64K jumbo frame */
#define EM_TX_SPARE_TSO			33

#define EM_TX_OACTIVE_MAX		64

/* Interrupt throttle rate */
#define EM_DEFAULT_ITR			6000

/* Number of segments sent before writing to TX related registers */
#define EM_DEFAULT_TXWREG		8

/*
 * This parameter controls whether or not autonegotation is enabled.
 *              0 - Disable autonegotiation
 *              1 - Enable  autonegotiation
 */
#define DO_AUTO_NEG			1

/*
 * This parameter control whether or not the driver will wait for
 * autonegotiation to complete.
 *              1 - Wait for autonegotiation to complete
 *              0 - Don't wait for autonegotiation to complete
 */
#define WAIT_FOR_AUTO_NEG_DEFAULT	0

/* Tunables -- End */

#define AUTONEG_ADV_DEFAULT		(ADVERTISE_10_HALF | \
					 ADVERTISE_10_FULL | \
					 ADVERTISE_100_HALF | \
					 ADVERTISE_100_FULL | \
					 ADVERTISE_1000_FULL)

#define AUTO_ALL_MODES			0

/* PHY master/slave setting */
#define EM_MASTER_SLAVE			e1000_ms_hw_default

/*
 * Micellaneous constants
 */
#define EM_VENDOR_ID			0x8086

#define EM_BAR_MEM			PCIR_BAR(0)
#define EM_BAR_FLASH			PCIR_BAR(1)

#define EM_JUMBO_PBA			0x00000028
#define EM_DEFAULT_PBA			0x00000030
#define EM_SMARTSPEED_DOWNSHIFT		3
#define EM_SMARTSPEED_MAX		15
#define EM_MAX_INTR			10

#define MAX_NUM_MULTICAST_ADDRESSES	128
#define PCI_ANY_ID			(~0U)
#define EM_FC_PAUSE_TIME		1000
#define EM_EEPROM_APME			0x400;

/*
 * TDBA/RDBA should be aligned on 16 byte boundary. But TDLEN/RDLEN should be
 * multiple of 128 bytes. So we align TDBA/RDBA on 128 byte boundary. This will
 * also optimize cache line size effect. H/W supports up to cache line size 128.
 */
#define EM_DBA_ALIGN			128

#define SPEED_MODE_BIT			(1 << 21) /* On PCI-E MACs only */

/* PCI Config defines */
#define EM_BAR_TYPE(v)			((v) & EM_BAR_TYPE_MASK)
#define EM_BAR_TYPE_MASK		0x00000001
#define EM_BAR_TYPE_MMEM		0x00000000
#define EM_BAR_TYPE_IO			0x00000001
#define EM_BAR_MEM_TYPE(v)		((v) & EM_BAR_MEM_TYPE_MASK)
#define EM_BAR_MEM_TYPE_MASK		0x00000006
#define EM_BAR_MEM_TYPE_32BIT		0x00000000
#define EM_BAR_MEM_TYPE_64BIT		0x00000004

#define EM_MAX_SCATTER			64
#define EM_TSO_SIZE			(IP_MAXPACKET + \
					 sizeof(struct ether_vlan_header))
#define EM_MSIX_MASK			0x01F00000 /* For 82574 use */
#define ETH_ZLEN			60

#define EM_CSUM_FEATURES		(CSUM_IP | CSUM_TCP | CSUM_UDP)

/*
 * 82574 has a nonstandard address for EIAC
 * and since its only used in MSIX, and in
 * the em driver only 82574 uses MSIX we can
 * solve it just using this define.
 */
#define EM_EIAC				0x000DC

/* Used in for 82547 10Mb Half workaround */
#define EM_PBA_BYTES_SHIFT		0xA
#define EM_TX_HEAD_ADDR_SHIFT		7
#define EM_PBA_TX_MASK			0xFFFF0000
#define EM_FIFO_HDR			0x10
#define EM_82547_PKT_THRESH		0x3e0

/*
 * Bus dma allocation structure used by
 * em_dma_malloc and em_dma_free.
 */
struct em_dma_alloc {
	bus_addr_t		dma_paddr;
	void			*dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
};

/* Our adapter structure */
struct adapter {
	struct arpcom		arpcom;
	struct e1000_hw		hw;
	int			flags;
#define EM_FLAG_SHARED_INTR	0x0001
#define EM_FLAG_HAS_MGMT	0x0002
#define EM_FLAG_HAS_AMT		0x0004
#define EM_FLAG_HW_CTRL		0x0008
#define EM_FLAG_TSO		0x0010
#define EM_FLAG_TSO_PULLEX	0x0020

	/* DragonFly operating-system-specific structures. */
	struct e1000_osdep	osdep;
	device_t		dev;

	bus_dma_tag_t		parent_dtag;

	struct resource		*memory;
	int			memory_rid;
	struct resource		*flash;
	int			flash_rid;

	struct resource		*ioport;
	int			io_rid;

	struct resource		*intr_res;
	void			*intr_tag;
	int			intr_rid;
	int			intr_type;

	struct ifmedia		media;
	struct callout		timer;
	struct callout		tx_fifo_timer;
	int			if_flags;
	int			min_frame_size;

	/* WOL register value */
	int			wol;

	/* Multicast array memory */
	uint8_t			*mta;

	/* Info about the board itself */
	uint8_t			link_active;
	uint16_t		link_speed;
	uint16_t		link_duplex;
	uint32_t		smartspeed;
	int			int_throttle_ceil;

	/* Polling */
	struct ifpoll_compat	npoll;

	/*
	 * Transmit definitions
	 *
	 * We have an array of num_tx_desc descriptors (handled
	 * by the controller) paired with an array of tx_buffers
	 * (at tx_buffer_area).
	 * The index of the next available descriptor is next_avail_tx_desc.
	 * The number of remaining tx_desc is num_tx_desc_avail.
	 */
	struct em_dma_alloc	txdma;		/* bus_dma glue for tx desc */
	struct e1000_tx_desc	*tx_desc_base;
	struct em_buffer	*tx_buffer_area;
	uint32_t		next_avail_tx_desc;
	uint32_t		next_tx_to_clean;
	int			num_tx_desc_avail;
	int			num_tx_desc;
	bus_dma_tag_t		txtag;		/* dma tag for tx */
	int			spare_tx_desc;
	int			oact_tx_desc;

	/* Saved csum offloading context information */
	int			csum_flags;
	int			csum_lhlen;
	int			csum_iphlen;

	int			csum_thlen;	/* TSO */
	int			csum_mss;	/* TSO */
	int			csum_pktlen;	/* TSO */

	uint32_t		csum_txd_upper;
	uint32_t		csum_txd_lower;

	int			tx_wreg_nsegs;

	/*
	 * Variables used to reduce TX interrupt rate and
	 * number of device's TX ring write requests.
	 *
	 * tx_nsegs:
	 * Number of TX descriptors setup so far.
	 *
	 * tx_int_nsegs:
	 * Once tx_nsegs > tx_int_nsegs, RS bit will be set
	 * in the last TX descriptor of the packet, and
	 * tx_nsegs will be reset to 0.  So TX interrupt and
	 * TX ring write request should be generated roughly
	 * every tx_int_nsegs TX descriptors.
	 *
	 * tx_dd[]:
	 * Index of the TX descriptors which have RS bit set,
	 * i.e. DD bit will be set on this TX descriptor after
	 * the data of the TX descriptor are transfered to
	 * hardware's internal packet buffer.  Only the TX
	 * descriptors listed in tx_dd[] will be checked upon
	 * TX interrupt.  This array is used as circular ring.
	 *
	 * tx_dd_tail, tx_dd_head:
	 * Tail and head index of valid elements in tx_dd[].
	 * tx_dd_tail == tx_dd_head means there is no valid
	 * elements in tx_dd[].  tx_dd_tail points to the position
	 * which is one beyond the last valid element in tx_dd[].
	 * tx_dd_head points to the first valid element in
	 * tx_dd[].
	 */
	int			tx_int_nsegs;
	int			tx_nsegs;
	int			tx_dd_tail;
	int			tx_dd_head;
#define EM_TXDD_MAX	64
#define EM_TXDD_SAFE	48 /* must be less than EM_TXDD_MAX */
	int			tx_dd[EM_TXDD_MAX];

	/*
	 * Receive definitions
	 *
	 * we have an array of num_rx_desc rx_desc (handled by the
	 * controller), and paired with an array of rx_buffers
	 * (at rx_buffer_area).
	 * The next pair to check on receive is at offset next_rx_desc_to_check
	 */
	struct em_dma_alloc	rxdma;		/* bus_dma glue for rx desc */
	struct e1000_rx_desc	*rx_desc_base;
	uint32_t		next_rx_desc_to_check;
	uint32_t		rx_buffer_len;
	int			num_rx_desc;
	struct em_buffer	*rx_buffer_area;
	bus_dma_tag_t		rxtag;
	bus_dmamap_t		rx_sparemap;

	/*
	 * First/last mbuf pointers, for
	 * collecting multisegment RX packets.
	 */
	struct mbuf		*fmp;
	struct mbuf		*lmp;

	/* Misc stats maintained by the driver */
	unsigned long		dropped_pkts;
	unsigned long		mbuf_alloc_failed;
	unsigned long		mbuf_cluster_failed;
	unsigned long		no_tx_desc_avail1;
	unsigned long		no_tx_desc_avail2;
	unsigned long		no_tx_map_avail;
	unsigned long		no_tx_dma_setup;
	unsigned long		watchdog_events;
	unsigned long		rx_overruns;
	unsigned long		rx_irq;
	unsigned long		tx_irq;
	unsigned long		link_irq;

	/* 82547 workaround */
	uint32_t		tx_fifo_size;
	uint32_t		tx_fifo_head;
	uint32_t		tx_fifo_head_addr;
	uint64_t		tx_fifo_reset_cnt;
	uint64_t		tx_fifo_wrk_cnt;
	uint32_t		tx_head_addr;

        /* For 82544 PCIX Workaround */
	boolean_t		pcix_82544;

	struct e1000_hw_stats	stats;
};

struct em_vendor_info {
	uint16_t	vendor_id;
	uint16_t	device_id;
	int		ret;
	const char	*desc;
};

struct em_buffer {
	struct mbuf	*m_head;
	bus_dmamap_t	map;		/* bus_dma map for packet */
};

/* For 82544 PCIX  Workaround */
typedef struct _ADDRESS_LENGTH_PAIR {
	uint64_t	address;
	uint32_t	length;
} ADDRESS_LENGTH_PAIR, *PADDRESS_LENGTH_PAIR;

typedef struct _DESCRIPTOR_PAIR {
	ADDRESS_LENGTH_PAIR descriptor[4];
	uint32_t	elements;
} DESC_ARRAY, *PDESC_ARRAY;

#define EM_IS_OACTIVE(adapter) \
	((adapter)->num_tx_desc_avail <= (adapter)->oact_tx_desc)

#define EM_INC_TXDD_IDX(idx) \
do { \
	if (++(idx) == EM_TXDD_MAX) \
		(idx) = 0; \
} while (0)

#endif /* _IF_EM_H_ */
