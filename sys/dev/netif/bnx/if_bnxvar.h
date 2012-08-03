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
 * $DragonFly: src/sys/dev/netif/bge/if_bgereg.h,v 1.25 2008/10/22 14:24:24 sephe Exp $
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
 * Register access macros. The Tigon always uses memory mapped register
 * accesses and all registers must be accessed with 32 bit operations.
 */

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->bnx_btag, sc->bnx_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->bnx_btag, sc->bnx_bhandle, reg)

#define BNX_SETBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) | x))

#define BNX_CLRBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, (CSR_READ_4(sc, reg) & ~x))

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
	struct bge_rx_bd	*bnx_rx_std_ring;
	bus_addr_t		bnx_rx_std_ring_paddr;
	struct bge_rx_bd	*bnx_rx_jumbo_ring;
	bus_addr_t		bnx_rx_jumbo_ring_paddr;
	struct bge_rx_bd	*bnx_rx_return_ring;
	bus_addr_t		bnx_rx_return_ring_paddr;
	struct bge_tx_bd	*bnx_tx_ring;
	bus_addr_t		bnx_tx_ring_paddr;
	struct bge_status_block	*bnx_status_block;
	bus_addr_t		bnx_status_block_paddr;
	void			*bnx_jumbo_buf;
	struct bnx_gib		bnx_info;
};

struct bnx_rxchain {
	struct mbuf	*bnx_mbuf;
	bus_addr_t	bnx_paddr;
};

/*
 * Mbuf pointers. We need these to keep track of the virtual addresses
 * of our mbuf chains since we can only convert from physical to virtual,
 * not the other way around.
 */
struct bnx_chain_data {
	bus_dma_tag_t		bnx_parent_tag;
	bus_dma_tag_t		bnx_rx_std_ring_tag;
	bus_dma_tag_t		bnx_rx_jumbo_ring_tag;
	bus_dma_tag_t		bnx_rx_return_ring_tag;
	bus_dma_tag_t		bnx_tx_ring_tag;
	bus_dma_tag_t		bnx_status_tag;
	bus_dma_tag_t		bnx_jumbo_tag;
	bus_dma_tag_t		bnx_tx_mtag;	/* TX mbuf DMA tag */
	bus_dma_tag_t		bnx_rx_mtag;	/* RX mbuf DMA tag */
	bus_dmamap_t		bnx_rx_tmpmap;
	bus_dmamap_t		bnx_tx_dmamap[BGE_TX_RING_CNT];
	bus_dmamap_t		bnx_rx_std_dmamap[BGE_STD_RX_RING_CNT];
	bus_dmamap_t		bnx_rx_std_ring_map;
	bus_dmamap_t		bnx_rx_jumbo_ring_map;
	bus_dmamap_t		bnx_tx_ring_map;
	bus_dmamap_t		bnx_rx_return_ring_map;
	bus_dmamap_t		bnx_status_map;
	bus_dmamap_t		bnx_jumbo_map;
	struct mbuf		*bnx_tx_chain[BGE_TX_RING_CNT];
	struct bnx_rxchain	bnx_rx_std_chain[BGE_STD_RX_RING_CNT];
	struct bnx_rxchain	bnx_rx_jumbo_chain[BGE_JUMBO_RX_RING_CNT];
	/* Stick the jumbo mem management stuff here too. */
	struct bnx_jslot	bnx_jslots[BNX_JSLOTS];
};

struct bnx_softc {
	struct arpcom		arpcom;		/* interface info */
	device_t		bnx_dev;
	device_t		bnx_miibus;
	bus_space_handle_t	bnx_bhandle;
	bus_space_tag_t		bnx_btag;
	void			*bnx_intrhand;
	struct resource		*bnx_irq;
	int			bnx_irq_type;
	int			bnx_irq_rid;
	struct resource		*bnx_res;
	struct ifmedia		bnx_ifmedia;	/* TBI media info */
	int			bnx_pciecap;
	uint32_t		bnx_status_tag;
	uint32_t		bnx_flags;	/* BNX_FLAG_ */
#define BNX_FLAG_TBI		0x00000001
#define BNX_FLAG_JUMBO		0x00000002
#define BNX_FLAG_ONESHOT_MSI	0x00000004
#define BNX_FLAG_5717_PLUS	0x00000008
#define BNX_FLAG_MII_SERDES	0x00000010
#define BNX_FLAG_CPMU		0x00000020
#define BNX_FLAG_57765_PLUS	0x00000040
#define BNX_FLAG_57765_FAMILY	0x00000080
#define BNX_FLAG_STATUSTAG_BUG	0x00000100
#define BNX_FLAG_TSO		0x00000200
#define BNX_FLAG_NO_EEPROM	0x10000000
#define BNX_FLAG_SHORTDMA	0x40000000

	uint32_t		bnx_chipid;
	uint32_t		bnx_asicrev;
	uint32_t		bnx_chiprev;
	struct bnx_ring_data	bnx_ldata;	/* rings */
	struct bnx_chain_data	bnx_cdata;	/* mbufs */
	uint16_t		bnx_tx_saved_considx;
	uint16_t		bnx_rx_saved_considx;
	uint16_t		bnx_return_ring_cnt;
	uint16_t		bnx_std;	/* current std ring head */
	uint16_t		bnx_jumbo;	/* current jumo ring head */
	SLIST_HEAD(__bnx_jfreehead, bnx_jslot)	bnx_jfree_listhead;
	struct lwkt_serialize	bnx_jslot_serializer;
	uint32_t		bnx_rx_coal_ticks;
	uint32_t		bnx_tx_coal_ticks;
	uint32_t		bnx_rx_coal_bds;
	uint32_t		bnx_tx_coal_bds;
	uint32_t		bnx_rx_coal_bds_int;
	uint32_t		bnx_tx_coal_bds_int;
	uint32_t		bnx_tx_prodidx;
	uint32_t		bnx_tx_buf_ratio;
	uint32_t		bnx_mi_mode;
	int			bnx_force_defrag;
	int			bnx_if_flags;
	int			bnx_txcnt;
	int			bnx_link;
	int			bnx_link_evt;
	int			bnx_stat_cpuid;
	struct callout		bnx_stat_timer;

	uint16_t		bnx_rx_check_considx;
	uint16_t		bnx_tx_check_considx;
	boolean_t		bnx_intr_maylose;
	int			bnx_intr_cpuid;
	struct callout		bnx_intr_timer;

	struct sysctl_ctx_list	bnx_sysctl_ctx;
	struct sysctl_oid	*bnx_sysctl_tree;

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

#define BNX_TSO_NSTATS		45
	u_long			bnx_tsosegs[BNX_TSO_NSTATS];
};

#define BNX_NSEG_NEW		40
#define BNX_NSEG_SPARE		5
#define BNX_NSEG_RSVD		16

/* RX coalesce ticks, unit: us */
#define BNX_RX_COAL_TICKS_MIN	0
#define BNX_RX_COAL_TICKS_DEF	160
#define BNX_RX_COAL_TICKS_MAX	1023

/* TX coalesce ticks, unit: us */
#define BNX_TX_COAL_TICKS_MIN	0
#define BNX_TX_COAL_TICKS_DEF	1023
#define BNX_TX_COAL_TICKS_MAX	1023

/* RX coalesce BDs */
#define BNX_RX_COAL_BDS_MIN	1
#define BNX_RX_COAL_BDS_DEF	80
#define BNX_RX_COAL_BDS_MAX	255

/* TX coalesce BDs */
#define BNX_TX_COAL_BDS_MIN	1
#define BNX_TX_COAL_BDS_DEF	128
#define BNX_TX_COAL_BDS_MAX	255

#endif	/* !_IF_BNXVAR_H_ */
