/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2001
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
 * $FreeBSD: src/sys/dev/bge/if_bgereg.h,v 1.1.2.16 2004/09/23 20:11:18 ps Exp $
 */

#ifndef _IF_BNXVAR_H_
#define _IF_BNXVAR_H_

/*
 * Tigon general information block. This resides in host memory
 * and contains the status counters, ring control blocks and
 * producer pointers.
 */

struct bnx_gib {
	struct bge_stats	bnx_stats;
	struct bge_rcb		bnx_tx_rcb[16];
	struct bge_rcb		bnx_std_rx_rcb;
	struct bge_rcb		bnx_jumbo_rx_rcb;
	struct bge_rcb		bnx_mini_rx_rcb;
	struct bge_rcb		bnx_return_rcb;
};

#define BNX_MIN_FRAMELEN	60
#define BNX_MAX_FRAMELEN	1536
#define BNX_JUMBO_FRAMELEN	9018
#define BNX_JUMBO_MTU		(BNX_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)

#define BNX_TIMEOUT		5000
#define BNX_FIRMWARE_TIMEOUT	100000
#define BNX_TXCONS_UNSET	0xFFFF	/* impossible value */

/*
 * Other utility macros.
 */
#define BNX_INC(x, y)		(x) = ((x) + 1) % (y)

/*
 * BAR0 MAC register access macros. The Tigon always uses memory mapped
 * register accesses and all registers must be accessed with 32 bit
 * operations.
 */

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->bnx_btag, sc->bnx_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->bnx_btag, sc->bnx_bhandle, reg)

#define BNX_SETBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) | x))

#define BNX_CLRBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) & ~x))

/* BAR2 APE register access macros. */
#define	APE_WRITE_4(sc, reg, val)	\
	bus_write_4(sc->bnx_res2, reg, val)

#define	APE_READ_4(sc, reg)		\
	bus_read_4(sc->bnx_res2, reg)

#define	APE_SETBIT(sc, reg, x)	\
	APE_WRITE_4(sc, reg, (APE_READ_4(sc, reg) | (x)))
#define	APE_CLRBIT(sc, reg, x)	\
	APE_WRITE_4(sc, reg, (APE_READ_4(sc, reg) & ~(x)))

#define BNX_MEMWIN_READ(sc, x, val)				\
do {								\
	pci_write_config(sc->bnx_dev, BGE_PCI_MEMWIN_BASEADDR,	\
	    (0xFFFF0000 & x), 4);				\
	val = CSR_READ_4(sc, BGE_MEMWIN_START + (x & 0xFFFF));	\
} while(0)

#define BNX_MEMWIN_WRITE(sc, x, val)				\
do {								\
	pci_write_config(sc->bnx_dev, BGE_PCI_MEMWIN_BASEADDR,	\
	    (0xFFFF0000 & x), 4);				\
	CSR_WRITE_4(sc, BGE_MEMWIN_START + (x & 0xFFFF), val);	\
} while(0)

#define RCB_WRITE_4(sc, rcb, offset, val)			\
	bus_space_write_4(sc->bnx_btag, sc->bnx_bhandle,	\
			  rcb + offsetof(struct bge_rcb, offset), val)

/*
 * Memory management stuff. Note: the SSLOTS, MSLOTS and JSLOTS
 * values are tuneable. They control the actual amount of buffers
 * allocated for the standard, mini and jumbo receive rings.
 */

#define BNX_SSLOTS	256
#define BNX_MSLOTS	256
#define BNX_JSLOTS	384

#define BNX_JRAWLEN (BNX_JUMBO_FRAMELEN + ETHER_ALIGN)
#define BNX_JLEN (BNX_JRAWLEN + \
	(sizeof(uint64_t) - BNX_JRAWLEN % sizeof(uint64_t)))
#define BNX_JPAGESZ PAGE_SIZE
#define BNX_RESID (BNX_JPAGESZ - (BNX_JLEN * BNX_JSLOTS) % BNX_JPAGESZ)
#define BNX_JMEM ((BNX_JLEN * BNX_JSLOTS) + BNX_RESID)

struct bnx_softc;
struct bnx_tx_ring;

struct bnx_jslot {
	struct bnx_softc	*bnx_sc;
	void			*bnx_buf;
	bus_addr_t		bnx_paddr;
	int			bnx_inuse;
	int			bnx_slot;
	SLIST_ENTRY(bnx_jslot)	jslot_link;
};

/*
 * Ring structures. Most of these reside in host memory and we tell
 * the NIC where they are via the ring control blocks. The exceptions
 * are the tx and command rings, which live in NIC memory and which
 * we access via the shared memory window.
 */
struct bnx_ring_data {
	struct bge_rx_bd	*bnx_rx_jumbo_ring;
	bus_addr_t		bnx_rx_jumbo_ring_paddr;
	void			*bnx_jumbo_buf;
	struct bnx_gib		bnx_info;
};

struct bnx_rx_buf {
	bus_dmamap_t		bnx_rx_dmamap;
	struct mbuf		*bnx_rx_mbuf;
	bus_addr_t		bnx_rx_paddr;
	int			bnx_rx_len;
	int			bnx_rx_refilled;
} __cachealign;

struct bnx_rx_std_ring {
	struct lwkt_serialize	bnx_rx_std_serialize;
	struct bnx_softc	*bnx_sc;

	uint16_t		bnx_rx_std_stop;
	uint16_t		bnx_rx_std;	/* current prod ring head */
	struct bge_rx_bd	*bnx_rx_std_ring;

	int			bnx_rx_std_refill __cachealign;
	int			bnx_rx_std_used;
	u_int			bnx_rx_std_running;
	struct thread		bnx_rx_std_ithread;

	struct bnx_rx_buf	bnx_rx_std_buf[BGE_STD_RX_RING_CNT];

	bus_dma_tag_t		bnx_rx_mtag;	/* RX mbuf DMA tag */

	bus_dma_tag_t		bnx_rx_std_ring_tag;
	bus_dmamap_t		bnx_rx_std_ring_map;
	bus_addr_t		bnx_rx_std_ring_paddr;
} __cachealign;

struct bnx_rx_ret_ring {
	struct lwkt_serialize	bnx_rx_ret_serialize;
	int			bnx_rx_mbx;
	uint32_t		bnx_saved_status_tag;
	volatile uint32_t	*bnx_hw_status_tag;
	int			bnx_msix_mbx;
	struct bnx_softc	*bnx_sc;
	struct bnx_rx_std_ring	*bnx_std;
	struct bnx_tx_ring	*bnx_txr;

	/* Shadow of bnx_rx_std_ring's bnx_rx_mtag */
	bus_dma_tag_t		bnx_rx_mtag;

	volatile uint16_t	*bnx_rx_considx;
	uint16_t		bnx_rx_saved_considx;
	uint16_t		bnx_rx_cnt;
	uint16_t		bnx_rx_cntmax;
	uint16_t		bnx_rx_mask;
	struct bge_rx_bd	*bnx_rx_ret_ring;
	bus_dmamap_t		bnx_rx_tmpmap;

	bus_dma_tag_t		bnx_rx_ret_ring_tag;
	bus_dmamap_t		bnx_rx_ret_ring_map;
	bus_addr_t		bnx_rx_ret_ring_paddr;

	u_long			bnx_rx_pkt;
	u_long			bnx_rx_force_sched;
} __cachealign;

/*
 * Mbuf pointers. We need these to keep track of the virtual addresses
 * of our mbuf chains since we can only convert from physical to virtual,
 * not the other way around.
 */
struct bnx_chain_data {
	bus_dma_tag_t		bnx_parent_tag;
	bus_dma_tag_t		bnx_rx_jumbo_ring_tag;
	bus_dma_tag_t		bnx_jumbo_tag;
	bus_dmamap_t		bnx_rx_jumbo_ring_map;
	bus_dmamap_t		bnx_jumbo_map;
	struct bnx_rx_buf	bnx_rx_jumbo_chain[BGE_JUMBO_RX_RING_CNT];
	/* Stick the jumbo mem management stuff here too. */
	struct bnx_jslot	bnx_jslots[BNX_JSLOTS];
};

struct bnx_tx_buf {
	bus_dmamap_t		bnx_tx_dmamap;
	struct mbuf		*bnx_tx_mbuf;
};

struct bnx_tx_ring {
	struct lwkt_serialize	bnx_tx_serialize;
	volatile uint32_t	*bnx_hw_status_tag;
	uint32_t		bnx_saved_status_tag;
	struct bnx_softc	*bnx_sc;
	struct ifaltq_subque	*bnx_ifsq;
	volatile uint16_t	*bnx_tx_considx;
	uint16_t		bnx_tx_flags;
#define BNX_TX_FLAG_SHORTDMA		0x0001
#define BNX_TX_FLAG_FORCE_DEFRAG	0x0002
	uint16_t		bnx_tx_saved_considx;
	int			bnx_tx_cnt;
	uint32_t		bnx_tx_prodidx;
	int			bnx_tx_wreg;
	int			bnx_tx_mbx;
	struct ifsubq_watchdog	bnx_tx_watchdog;

	struct bge_tx_bd	*bnx_tx_ring;

	bus_dma_tag_t		bnx_tx_mtag;	/* TX mbuf DMA tag */
	struct bnx_tx_buf	bnx_tx_buf[BGE_TX_RING_CNT];

	bus_dma_tag_t		bnx_tx_ring_tag;
	bus_dmamap_t		bnx_tx_ring_map;
	bus_addr_t		bnx_tx_ring_paddr;
	int			bnx_tx_cpuid;

	u_long			bnx_tx_pkt;
} __cachealign;

struct bnx_intr_data {
	struct bnx_softc	*bnx_sc;
	struct bnx_rx_ret_ring	*bnx_ret;
	struct bnx_tx_ring	*bnx_txr;

	int			bnx_intr_cpuid;
	struct lwkt_serialize	*bnx_intr_serialize;
	struct callout		bnx_intr_timer;
	void			(*bnx_intr_check)(void *);
	uint16_t		bnx_rx_check_considx;
	uint16_t		bnx_tx_check_considx;
	boolean_t		bnx_intr_maylose;

	void			*bnx_intr_arg;
	driver_intr_t		*bnx_intr_func;
	void			*bnx_intr_hand;
	struct resource		*bnx_intr_res;
	int			bnx_intr_rid;
	int			bnx_intr_mbx;
	const uint32_t		*bnx_saved_status_tag;

	const char		*bnx_intr_desc;
	char			bnx_intr_desc0[64];

	bus_dma_tag_t		bnx_status_tag;
	bus_dmamap_t		bnx_status_map;
	struct bge_status_block	*bnx_status_block;
	bus_addr_t		bnx_status_block_paddr;
} __cachealign;

#define BNX_RX_RING_MAX		4
#define BNX_TX_RING_MAX		4
#define BNX_INTR_MAX		5

struct bnx_softc {
	struct arpcom		arpcom;		/* interface info */
	device_t		bnx_dev;
	device_t		bnx_miibus;
	bus_space_handle_t	bnx_bhandle;
	bus_space_tag_t		bnx_btag;
	struct resource		*bnx_res;	/* MAC mapped I/O */
	struct resource		*bnx_res2;	/* APE mapped I/O */
	struct ifmedia		bnx_ifmedia;	/* TBI media info */
	int			bnx_pciecap;
	uint32_t		bnx_flags;	/* BNX_FLAG_ */
#define BNX_FLAG_TBI		0x00000001
#define BNX_FLAG_JUMBO		0x00000002
#define BNX_FLAG_APE		0x00000004
#define BNX_FLAG_5717_PLUS	0x00000008
#define BNX_FLAG_MII_SERDES	0x00000010
#define BNX_FLAG_CPMU		0x00000020
#define BNX_FLAG_57765_PLUS	0x00000040
#define BNX_FLAG_57765_FAMILY	0x00000080
#define BNX_FLAG_STATUSTAG_BUG	0x00000100
#define BNX_FLAG_TSO		0x00000200
#define BNX_FLAG_NO_EEPROM	0x10000000
#define BNX_FLAG_RXTX_BUNDLE	0x20000000
#define BNX_FLAG_STD_THREAD	0x40000000
#define BNX_FLAG_STATUS_HASTAG	0x80000000

	uint32_t		bnx_mfw_flags;	/* Management F/W flags */
#define	BNX_MFW_ON_RXCPU	0x00000001
#define	BNX_MFW_ON_APE		0x00000002
#define	BNX_MFW_TYPE_NCSI	0x00000004
#define	BNX_MFW_TYPE_DASH	0x00000008
	int			bnx_phy_ape_lock;
	int			bnx_func_addr;

	uint32_t		bnx_chipid;
	uint32_t		bnx_asicrev;
	uint32_t		bnx_chiprev;
	struct bnx_ring_data	bnx_ldata;	/* rings */
	struct bnx_chain_data	bnx_cdata;	/* mbufs */

	struct lwkt_serialize	bnx_main_serialize;
	volatile uint32_t	*bnx_hw_status;
	volatile uint32_t	*bnx_hw_status_tag;
	uint32_t		bnx_saved_status_tag;
	int			bnx_link_evt;
	u_long			bnx_errors;
	u_long			bnx_norxbds;

	int			bnx_serialize_cnt;
	struct lwkt_serialize	**bnx_serialize;

	int			bnx_tx_ringcnt;
	struct bnx_tx_ring	*bnx_tx_ring;
	int			bnx_rx_retcnt;
	struct bnx_rx_ret_ring	*bnx_rx_ret_ring;
	struct bnx_rx_std_ring	bnx_rx_std_ring;

	uint16_t		bnx_jumbo;	/* current jumo ring head */
	SLIST_HEAD(__bnx_jfreehead, bnx_jslot)	bnx_jfree_listhead;
	struct lwkt_serialize	bnx_jslot_serializer;
	uint32_t		bnx_rx_coal_ticks;
	uint32_t		bnx_tx_coal_ticks;
	uint32_t		bnx_rx_coal_bds;
	uint32_t		bnx_rx_coal_bds_poll;
	uint32_t		bnx_tx_coal_bds;
	uint32_t		bnx_tx_coal_bds_poll;
	uint32_t		bnx_rx_coal_bds_int;
	uint32_t		bnx_tx_coal_bds_int;
	uint32_t		bnx_mi_mode;
	int			bnx_if_flags;
	int			bnx_link;
	int			bnx_tick_cpuid;
	struct callout		bnx_tick_timer;

	int			bnx_npoll_rxoff;
	int			bnx_npoll_txoff;

	int			bnx_msix_mem_rid;
	struct resource		*bnx_msix_mem_res;
	int			bnx_intr_type;
	int			bnx_intr_cnt;
	struct bnx_intr_data	bnx_intr_data[BNX_INTR_MAX];

	int			bnx_phyno;
	uint32_t		bnx_coal_chg;
#define BNX_RX_COAL_TICKS_CHG		0x01
#define BNX_TX_COAL_TICKS_CHG		0x02
#define BNX_RX_COAL_BDS_CHG		0x04
#define BNX_TX_COAL_BDS_CHG		0x08
#define BNX_RX_COAL_BDS_INT_CHG		0x40
#define BNX_TX_COAL_BDS_INT_CHG		0x80

	void			(*bnx_link_upd)(struct bnx_softc *, uint32_t);
	uint32_t		bnx_link_chg;

	int			bnx_rss_debug;
#define BNX_TSO_NSTATS		45
	u_long			bnx_tsosegs[BNX_TSO_NSTATS];
};

#define BNX_NSEG_NEW		40
#define BNX_NSEG_SPARE		33	/* enough for 64K TSO segment */
#define BNX_NSEG_RSVD		4

/* RX coalesce ticks, unit: us */
#define BNX_RX_COAL_TICKS_MIN	0
#define BNX_RX_COAL_TICKS_DEF	160
#define BNX_RX_COAL_TICKS_MAX	1023

/* TX coalesce ticks, unit: us */
#define BNX_TX_COAL_TICKS_MIN	0
#define BNX_TX_COAL_TICKS_DEF	1023
#define BNX_TX_COAL_TICKS_MAX	1023

/* RX coalesce BDs */
#define BNX_RX_COAL_BDS_MIN	0
#define BNX_RX_COAL_BDS_DEF	0
#define BNX_RX_COAL_BDS_INT_DEF	80
#define BNX_RX_COAL_BDS_MAX	255

/* TX coalesce BDs */
#define BNX_TX_COAL_BDS_MIN	0
#define BNX_TX_COAL_BDS_DEF	128
#define BNX_TX_COAL_BDS_POLL_DEF 64
#define BNX_TX_COAL_BDS_INT_DEF	64
#define BNX_TX_COAL_BDS_MAX	255

/* Number of segments sent before writing to TX related registers */
#define BNX_TX_WREG_NSEGS	8

/* Return ring descriptor count */
#define BNX_RETURN_RING_CNT	512

#define BNX_TX_RING_MAX		4

#define BNX_RSS_ENABLED(sc)	((sc)->bnx_rx_retcnt > 1)

#endif	/* !_IF_BNXVAR_H_ */
