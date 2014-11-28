/*-
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/jme/if_jmevar.h,v 1.1 2008/05/27 01:42:01 yongari Exp $
 */

#ifndef	_IF_JMEVAR_H
#define	_IF_JMEVAR_H

#include <sys/queue.h>
#include <sys/callout.h>
#include <sys/taskqueue.h>

/*
 * JMC250 supports upto JME_NDESC_MAX descriptors and the number of
 * descriptors should be multiple of JME_NDESC_ALIGN.
 */
#define	JME_TX_DESC_CNT_DEF	512
#define	JME_RX_DESC_CNT_DEF	512

#define JME_NDESC_ALIGN		16
#define JME_NDESC_MAX		1024

#define JME_NRXRING_1		1
#define JME_NRXRING_2		2
#define JME_NRXRING_4		4

#define JME_NRXRING_MIN		JME_NRXRING_1
#define JME_NRXRING_MAX		JME_NRXRING_4

/* RX rings + TX ring + status */
#define JME_NSERIALIZE		(JME_NRXRING_MAX + 1 + 1)

/* RX rings + TX ring + status */
#define JME_MSIXCNT(nrx)	((nrx) + 1 + 1)
#define JME_NMSIX		JME_MSIXCNT(JME_NRXRING_MAX)

/*
 * Tx/Rx descriptor queue base should be 16bytes aligned and
 * should not cross 4G bytes boundary on the 64bits address
 * mode.
 */
#define	JME_TX_RING_ALIGN	__VM_CACHELINE_SIZE
#define	JME_RX_RING_ALIGN	__VM_CACHELINE_SIZE
#define	JME_MAXSEGSIZE		4096
#define	JME_TSO_MAXSIZE		(IP_MAXPACKET + sizeof(struct ether_vlan_header))
#define	JME_MAXTXSEGS		40
#define	JME_RX_BUF_ALIGN	sizeof(uint64_t)
#define	JME_SSB_ALIGN		__VM_CACHELINE_SIZE

#if (BUS_SPACE_MAXADDR != BUS_SPACE_MAXADDR_32BIT)
#define JME_RING_BOUNDARY	0x100000000ULL
#else
#define JME_RING_BOUNDARY	0
#endif

#define	JME_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	JME_ADDR_HI(x)		((uint64_t) (x) >> 32)

/* Water mark to kick reclaiming Tx buffers. */
#define	JME_TX_DESC_HIWAT(tdata)	\
	((tdata)->jme_tx_desc_cnt - (((tdata)->jme_tx_desc_cnt * 3) / 10))

/*
 * JMC250 can send 9K jumbo frame on Tx path and can receive
 * 65535 bytes.
 */
#define JME_JUMBO_FRAMELEN	9216
#define JME_JUMBO_MTU							\
	(JME_JUMBO_FRAMELEN - sizeof(struct ether_vlan_header) -	\
	 ETHER_HDR_LEN - ETHER_CRC_LEN)
#define	JME_MAX_MTU							\
	(ETHER_MAX_LEN + sizeof(struct ether_vlan_header) -		\
	 ETHER_HDR_LEN - ETHER_CRC_LEN)
/*
 * JMC250 can't handle Tx checksum offload/TSO if frame length
 * is larger than its FIFO size(2K). It's also good idea to not
 * use jumbo frame if hardware is running at half-duplex media.
 * Because the jumbo frame may not fit into the Tx FIFO,
 * collisions make hardware fetch frame from host memory with
 * DMA again which in turn slows down Tx performance
 * significantly.
 */
#define	JME_TX_FIFO_SIZE	2000
/*
 * JMC250 has just 4K Rx FIFO. To support jumbo frame that is
 * larger than 4K bytes in length, Rx FIFO threshold should be
 * adjusted to minimize Rx FIFO overrun.
 */
#define	JME_RX_FIFO_SIZE	4000

#define	JME_DESC_INC(x, y)	((x) = ((x) + 1) % (y))
#define JME_DESC_ADD(x, d, y)	((x) = ((x) + (d)) % (y))

struct jme_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	int			tx_ndesc;
	struct jme_desc		*tx_desc;
};

struct jme_rxdesc {
	struct mbuf 		*rx_m;
	bus_addr_t		rx_paddr;
	bus_dmamap_t		rx_dmamap;
	struct jme_desc		*rx_desc;
};

struct jme_softc;

/*
 * RX ring/descs
 */
struct jme_rxdata {
	struct lwkt_serialize	jme_rx_serialize;
	struct jme_softc	*jme_sc;

	uint32_t		jme_rx_coal;
	uint32_t		jme_rx_comp;
	uint32_t		jme_rx_empty;
	int			jme_rx_idx;

	bus_dma_tag_t		jme_rx_tag;	/* RX mbuf tag */
	bus_dmamap_t		jme_rx_sparemap;
	struct jme_rxdesc	*jme_rxdesc;

	struct jme_desc		*jme_rx_ring;
	int			jme_rx_cons;
	int			jme_rx_desc_cnt;

	int			jme_rxlen;
	struct mbuf		*jme_rxhead;
	struct mbuf		*jme_rxtail;

	u_long			jme_rx_pkt;
	u_long			jme_rx_emp;

	bus_addr_t		jme_rx_ring_paddr;
	bus_dma_tag_t		jme_rx_ring_tag;
	bus_dmamap_t		jme_rx_ring_map;
} __cachealign;

struct jme_txdata {
	struct lwkt_serialize	jme_tx_serialize;
	struct jme_softc	*jme_sc;

	bus_dma_tag_t		jme_tx_tag;	/* TX mbuf tag */
	struct jme_txdesc	*jme_txdesc;

	struct jme_desc		*jme_tx_ring;

	int			jme_tx_wreg;
	int			jme_tx_prod;
	int			jme_tx_cons;
	int			jme_tx_cnt;
	int			jme_tx_desc_cnt;

	bus_addr_t		jme_tx_ring_paddr;
	bus_dma_tag_t		jme_tx_ring_tag;
	bus_dmamap_t		jme_tx_ring_map;
} __cachealign;

struct jme_chain_data {
	/*
	 * TX ring
	 */
	struct jme_txdata	jme_tx_data;

	/*
	 * RX rings
	 */
	int			jme_rx_ring_cnt;
	struct jme_rxdata	jme_rx_data[JME_NRXRING_MAX];

	/*
	 * Top level tags
	 */
	bus_dma_tag_t		jme_ring_tag;	/* parent ring tag */
	bus_dma_tag_t		jme_buffer_tag;	/* parent mbuf/ssb tag */

	/*
	 * Shadow status block (unused)
	 */
	struct jme_ssb		*jme_ssb_block;
	bus_addr_t		jme_ssb_block_paddr;
	bus_dma_tag_t		jme_ssb_tag;
	bus_dmamap_t		jme_ssb_map;
} __cachealign;

struct jme_msix_data {
	int			jme_msix_rid;
	int			jme_msix_cpuid;
	u_int			jme_msix_vector;
	uint32_t		jme_msix_intrs;
	struct resource		*jme_msix_res;
	void			*jme_msix_handle;
	struct lwkt_serialize	*jme_msix_serialize;
	char			jme_msix_desc[64];

	driver_intr_t		*jme_msix_func;
	void			*jme_msix_arg;
};

#define JME_TX_RING_SIZE(tdata)	\
    (sizeof(struct jme_desc) * (tdata)->jme_tx_desc_cnt)
#define JME_RX_RING_SIZE(rdata)	\
    (sizeof(struct jme_desc) * (rdata)->jme_rx_desc_cnt)
#define	JME_SSB_SIZE		sizeof(struct jme_ssb)

/*
 * Software state per device.
 */
struct jme_softc {
	struct arpcom		arpcom;
	device_t		jme_dev;

	int			jme_mem_rid;
	struct resource		*jme_mem_res;
	bus_space_tag_t		jme_mem_bt;
	bus_space_handle_t	jme_mem_bh;

	int			jme_irq_type;
	int			jme_irq_rid;
	struct resource		*jme_irq_res;
	void			*jme_irq_handle;
	struct jme_msix_data	jme_msix[JME_NMSIX];
	int			jme_msix_cnt;
	uint32_t		jme_msinum[JME_MSINUM_CNT];
	int			jme_tx_cpuid;

	int			jme_npoll_rxoff;
	int			jme_npoll_txoff;

	device_t		jme_miibus;
	int			jme_phyaddr;
	bus_addr_t		jme_lowaddr;

	uint32_t		jme_clksrc;
	uint32_t		jme_clksrc_1000;
	uint16_t		jme_phycom0;
	uint16_t		jme_phycom1;
	uint32_t		jme_tx_dma_size;
	uint32_t		jme_rx_dma_size;

	uint32_t		jme_caps;
#define	JME_CAP_FPGA		0x0001
#define	JME_CAP_PCIE		0x0002
#define	JME_CAP_PMCAP		0x0004
#define	JME_CAP_FASTETH		0x0008
#define	JME_CAP_JUMBO		0x0010
#define JME_CAP_PHYPWR		0x0020

	uint32_t		jme_workaround;
#define JME_WA_EXTFIFO		0x0001
#define JME_WA_HDX		0x0002

	boolean_t		jme_has_link;
	boolean_t		jme_in_tick;

	struct lwkt_serialize	jme_serialize;
	struct lwkt_serialize	*jme_serialize_arr[JME_NSERIALIZE];
	int			jme_serialize_cnt;

	struct callout		jme_tick_ch;
	struct jme_chain_data	jme_cdata;
	int			jme_if_flags;
	uint32_t		jme_txcsr;
	uint32_t		jme_rxcsr;

	/*
	 * Sysctl variables
	 */
	int			jme_tx_coal_to;
	int			jme_tx_coal_pkt;
	int			jme_rx_coal_to;
	int			jme_rx_coal_pkt;
	int			jme_rss_debug;
};

/* Register access macros. */
#define CSR_WRITE_4(_sc, reg, val)	\
	bus_space_write_4((_sc)->jme_mem_bt, (_sc)->jme_mem_bh, (reg), (val))
#define CSR_READ_4(_sc, reg)		\
	bus_space_read_4((_sc)->jme_mem_bt, (_sc)->jme_mem_bh, (reg))

#define	JME_MAXERR	5

#define	JME_RXCHAIN_RESET(rdata)	\
do {					\
	(rdata)->jme_rxhead = NULL;	\
	(rdata)->jme_rxtail = NULL;	\
	(rdata)->jme_rxlen = 0;		\
} while (0)

#define	JME_TX_TIMEOUT		5
#define JME_TIMEOUT		1000
#define JME_PHY_TIMEOUT		1000
#define JME_EEPROM_TIMEOUT	1000

#define JME_TXD_RSVD		1
/* Large enough to cooperate 64K TSO segment and one spare TX descriptor */
#define JME_TXD_SPARE		34

#define JME_TXWREG_NSEGS	16

#define JME_ENABLE_HWRSS(sc)	\
	((sc)->jme_cdata.jme_rx_ring_cnt > JME_NRXRING_MIN)

#endif
