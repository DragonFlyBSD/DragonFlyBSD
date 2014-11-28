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

#ifndef _IF_BGEVAR_H_
#define _IF_BGEVAR_H_

/*
 * Tigon general information block. This resides in host memory
 * and contains the status counters, ring control blocks and
 * producer pointers.
 */

struct bge_gib {
	struct bge_stats	bge_stats;
	struct bge_rcb		bge_tx_rcb[16];
	struct bge_rcb		bge_std_rx_rcb;
	struct bge_rcb		bge_jumbo_rx_rcb;
	struct bge_rcb		bge_mini_rx_rcb;
	struct bge_rcb		bge_return_rcb;
};

#define BGE_MIN_FRAMELEN	60
#define BGE_MAX_FRAMELEN	1536
#define BGE_JUMBO_FRAMELEN	9018
#define BGE_JUMBO_MTU		(BGE_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)

#define BGE_TIMEOUT		5000
#define BGE_FIRMWARE_TIMEOUT	100000
#define BGE_TXCONS_UNSET	0xFFFF	/* impossible value */

/*
 * Other utility macros.
 */
#define BGE_INC(x, y)		(x) = ((x) + 1) % (y)

/*
 * BAR0 MAC register access macros.  The Tigon always uses memory mapped
 * register accesses and all registers must be accessed with 32 bit
 * operations.
 */

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->bge_btag, sc->bge_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->bge_btag, sc->bge_bhandle, reg)

#define BGE_SETBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) | x))

#define BGE_CLRBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) & ~x))

/* BAR2 APE register access macros. */
#define	APE_WRITE_4(sc, reg, val)	\
	bus_write_4(sc->bge_res2, reg, val)

#define	APE_READ_4(sc, reg)		\
	bus_read_4(sc->bge_res2, reg)

#define	APE_SETBIT(sc, reg, x)	\
	APE_WRITE_4(sc, reg, (APE_READ_4(sc, reg) | (x)))
#define	APE_CLRBIT(sc, reg, x)	\
	APE_WRITE_4(sc, reg, (APE_READ_4(sc, reg) & ~(x)))

#define BGE_MEMWIN_READ(sc, x, val)				\
do {								\
	pci_write_config(sc->bge_dev, BGE_PCI_MEMWIN_BASEADDR,	\
	    (0xFFFF0000 & x), 4);				\
	val = CSR_READ_4(sc, BGE_MEMWIN_START + (x & 0xFFFF));	\
} while(0)

#define BGE_MEMWIN_WRITE(sc, x, val)				\
do {								\
	pci_write_config(sc->bge_dev, BGE_PCI_MEMWIN_BASEADDR,	\
	    (0xFFFF0000 & x), 4);				\
	CSR_WRITE_4(sc, BGE_MEMWIN_START + (x & 0xFFFF), val);	\
} while(0)

#define RCB_WRITE_4(sc, rcb, offset, val)			\
	bus_space_write_4(sc->bge_btag, sc->bge_bhandle,	\
			  rcb + offsetof(struct bge_rcb, offset), val)

/*
 * Memory management stuff. Note: the SSLOTS, MSLOTS and JSLOTS
 * values are tuneable. They control the actual amount of buffers
 * allocated for the standard, mini and jumbo receive rings.
 */

#define BGE_SSLOTS	256
#define BGE_MSLOTS	256
#define BGE_JSLOTS	384

#define BGE_JRAWLEN (BGE_JUMBO_FRAMELEN + ETHER_ALIGN)
#define BGE_JLEN (BGE_JRAWLEN + \
	(sizeof(uint64_t) - BGE_JRAWLEN % sizeof(uint64_t)))
#define BGE_JPAGESZ PAGE_SIZE
#define BGE_RESID (BGE_JPAGESZ - (BGE_JLEN * BGE_JSLOTS) % BGE_JPAGESZ)
#define BGE_JMEM ((BGE_JLEN * BGE_JSLOTS) + BGE_RESID)

struct bge_softc;

struct bge_jslot {
	struct bge_softc	*bge_sc;
	void			*bge_buf;
	bus_addr_t		bge_paddr;
	int			bge_inuse;
	int			bge_slot;
	SLIST_ENTRY(bge_jslot)	jslot_link;
};

/*
 * Ring structures. Most of these reside in host memory and we tell
 * the NIC where they are via the ring control blocks. The exceptions
 * are the tx and command rings, which live in NIC memory and which
 * we access via the shared memory window.
 */
struct bge_ring_data {
	struct bge_rx_bd	*bge_rx_std_ring;
	bus_addr_t		bge_rx_std_ring_paddr;
	struct bge_rx_bd	*bge_rx_jumbo_ring;
	bus_addr_t		bge_rx_jumbo_ring_paddr;
	struct bge_rx_bd	*bge_rx_return_ring;
	bus_addr_t		bge_rx_return_ring_paddr;
	struct bge_tx_bd	*bge_tx_ring;
	bus_addr_t		bge_tx_ring_paddr;
	struct bge_status_block	*bge_status_block;
	bus_addr_t		bge_status_block_paddr;
	struct bge_stats	*bge_stats;
	bus_addr_t		bge_stats_paddr;
	void			*bge_jumbo_buf;
	struct bge_gib		bge_info;
};

struct bge_rxchain {
	struct mbuf	*bge_mbuf;
	bus_addr_t	bge_paddr;
};

/*
 * Mbuf pointers. We need these to keep track of the virtual addresses
 * of our mbuf chains since we can only convert from physical to virtual,
 * not the other way around.
 */
struct bge_chain_data {
	bus_dma_tag_t		bge_parent_tag;
	bus_dma_tag_t		bge_rx_std_ring_tag;
	bus_dma_tag_t		bge_rx_jumbo_ring_tag;
	bus_dma_tag_t		bge_rx_return_ring_tag;
	bus_dma_tag_t		bge_tx_ring_tag;
	bus_dma_tag_t		bge_status_tag;
	bus_dma_tag_t		bge_stats_tag;
	bus_dma_tag_t		bge_jumbo_tag;
	bus_dma_tag_t		bge_tx_mtag;	/* TX mbuf DMA tag */
	bus_dma_tag_t		bge_rx_mtag;	/* RX mbuf DMA tag */
	bus_dmamap_t		bge_rx_tmpmap;
	bus_dmamap_t		bge_tx_dmamap[BGE_TX_RING_CNT];
	bus_dmamap_t		bge_rx_std_dmamap[BGE_STD_RX_RING_CNT];
	bus_dmamap_t		bge_rx_std_ring_map;
	bus_dmamap_t		bge_rx_jumbo_ring_map;
	bus_dmamap_t		bge_tx_ring_map;
	bus_dmamap_t		bge_rx_return_ring_map;
	bus_dmamap_t		bge_status_map;
	bus_dmamap_t		bge_stats_map;
	bus_dmamap_t		bge_jumbo_map;
	struct mbuf		*bge_tx_chain[BGE_TX_RING_CNT];
	struct bge_rxchain	bge_rx_std_chain[BGE_STD_RX_RING_CNT];
	struct bge_rxchain	bge_rx_jumbo_chain[BGE_JUMBO_RX_RING_CNT];
	/* Stick the jumbo mem management stuff here too. */
	struct bge_jslot	bge_jslots[BGE_JSLOTS];
};

struct bge_softc {
	struct arpcom		arpcom;		/* interface info */
	device_t		bge_dev;
	device_t		bge_miibus;
	bus_space_handle_t	bge_bhandle;
	bus_space_tag_t		bge_btag;
	void			*bge_intrhand;
	struct resource		*bge_irq;
	int			bge_irq_type;
	int			bge_irq_rid;
	struct resource		*bge_res;	/* MAC mapped I/O */
	struct resource		*bge_res2;	/* APE mapped I/O */
	struct ifmedia		bge_ifmedia;	/* TBI media info */
	int			bge_pcixcap;
	int			bge_pciecap;
	int			bge_msicap;
	uint32_t		bge_pci_miscctl;
	uint32_t		bge_status_tag;
	uint32_t		bge_flags;	/* BGE_FLAG_ */
#define BGE_FLAG_TBI		0x00000001
#define BGE_FLAG_JUMBO		0x00000002
#define BGE_FLAG_ONESHOT_MSI	0x00000004
#define BGE_FLAG_TSO		0x00000008
#define BGE_FLAG_MII_SERDES	0x00000010
#define	BGE_FLAG_CPMU		0x00000020
#define BGE_FLAG_APE		0x00000040
#define BGE_FLAG_PCIX		0x00000200
#define BGE_FLAG_PCIE		0x00000400
#define BGE_FLAG_5700_FAMILY	0x00001000
#define BGE_FLAG_5705_PLUS	0x00002000
#define BGE_FLAG_5714_FAMILY	0x00004000
#define BGE_FLAG_575X_PLUS	0x00008000
#define BGE_FLAG_5755_PLUS	0x00010000
#define BGE_FLAG_MAXADDR_40BIT	0x00020000
#define BGE_FLAG_RX_ALIGNBUG	0x00100000
#define BGE_FLAG_NO_EEPROM	0x10000000
#define BGE_FLAG_5788		0x20000000
#define BGE_FLAG_SHORTDMA	0x40000000
#define BGE_FLAG_STATUS_TAG	0x80000000

	uint32_t		bge_mfw_flags;	/* Management F/W flags */
#define	BGE_MFW_ON_RXCPU	0x00000001
#define	BGE_MFW_ON_APE		0x00000002
#define	BGE_MFW_TYPE_NCSI	0x00000004
#define	BGE_MFW_TYPE_DASH	0x00000008
	int			bge_phy_ape_lock;
	int			bge_func_addr;

	uint32_t		bge_chipid;
	uint8_t			bge_asf_mode;
#define ASF_ENABLE		0x01
#define ASF_NEW_HANDSHAKE	0x02
#define ASF_STACKUP		0x04
	uint8_t			bge_asf_count;
	uint32_t		bge_asicrev;
	uint32_t		bge_chiprev;
	struct ifpoll_compat	bge_npoll;	/* polling */
	struct bge_ring_data	bge_ldata;	/* rings */
	struct bge_chain_data	bge_cdata;	/* mbufs */
	uint16_t		bge_tx_saved_considx;
	uint16_t		bge_rx_saved_considx;
	uint16_t		bge_ev_saved_considx;
	uint16_t		bge_return_ring_cnt;
	uint16_t		bge_std;	/* current std ring head */
	uint16_t		bge_jumbo;	/* current jumo ring head */
	SLIST_HEAD(__bge_jfreehead, bge_jslot)	bge_jfree_listhead;
	struct lwkt_serialize	bge_jslot_serializer;
	uint32_t		bge_stat_ticks;
	uint32_t		bge_rx_coal_ticks;
	uint32_t		bge_tx_coal_ticks;
	uint32_t		bge_rx_coal_bds;
	uint32_t		bge_tx_coal_bds;
	uint32_t		bge_rx_coal_ticks_int;
	uint32_t		bge_tx_coal_ticks_int;
	uint32_t		bge_rx_coal_bds_int;
	uint32_t		bge_tx_coal_bds_int;
	uint32_t		bge_tx_prodidx;
	int			bge_tx_wreg;
	int			bge_rx_wreg;
	uint32_t		bge_tx_buf_ratio;
	uint32_t		bge_mi_mode;
	int			bge_force_defrag;
	int			bge_mbox_reorder;
	int			bge_if_flags;
	int			bge_txcnt;
	int			bge_txspare;
	int			bge_txrsvd;
	int			bge_link;
	int			bge_link_evt;
	struct callout		bge_stat_timer;

	int			bge_phyno;
	uint32_t		bge_coal_chg;
#define BGE_RX_COAL_TICKS_CHG		0x01
#define BGE_TX_COAL_TICKS_CHG		0x02
#define BGE_RX_COAL_BDS_CHG		0x04
#define BGE_TX_COAL_BDS_CHG		0x08
#define BGE_RX_COAL_TICKS_INT_CHG	0x10
#define BGE_TX_COAL_TICKS_INT_CHG	0x20
#define BGE_RX_COAL_BDS_INT_CHG		0x40
#define BGE_TX_COAL_BDS_INT_CHG		0x80

	void			(*bge_link_upd)(struct bge_softc *, uint32_t);
	uint32_t		bge_link_chg;
};

#define BGE_NSEG_NEW		40
#define BGE_NSEG_SPARE		5
#define BGE_NSEG_SPARE_TSO	33
#define BGE_NSEG_RSVD		16
#define BGE_NSEG_RSVD_TSO	4

/* RX coalesce ticks, unit: us */
#define BGE_RX_COAL_TICKS_MIN	0
#define BGE_RX_COAL_TICKS_DEF	160
#define BGE_RX_COAL_TICKS_MAX	1023

/* TX coalesce ticks, unit: us */
#define BGE_TX_COAL_TICKS_MIN	0
#define BGE_TX_COAL_TICKS_DEF	1023
#define BGE_TX_COAL_TICKS_MAX	1023

/* RX coalesce BDs */
#define BGE_RX_COAL_BDS_MIN	0
#define BGE_RX_COAL_BDS_DEF	80
#define BGE_RX_COAL_BDS_MAX	255

/* TX coalesce BDs */
#define BGE_TX_COAL_BDS_MIN	0
#define BGE_TX_COAL_BDS_DEF	128
#define BGE_TX_COAL_BDS_MAX	255

/* Number of segments sent before writing to TX related registers */
#define BGE_TX_WREG_NSEGS	16

#endif	/* !_IF_BGEVAR_H_ */
