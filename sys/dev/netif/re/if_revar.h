/*
 * Copyright (c) 2004
 *	Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 1997, 1998-2003
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/pci/if_rlreg.h,v 1.42 2004/05/24 19:39:23 jhb Exp $
 */

#define RE_RX_DESC_CNT_8139CP	64
#define RE_TX_DESC_CNT_8139CP	64

#define RE_RX_DESC_CNT_DEF	256
#define RE_TX_DESC_CNT_DEF	256
#define RE_RX_DESC_CNT_MAX	1024
#define RE_TX_DESC_CNT_MAX	1024

#define RE_RX_LIST_SZ(sc)	((sc)->re_rx_desc_cnt * sizeof(struct re_desc))
#define RE_TX_LIST_SZ(sc)	((sc)->re_tx_desc_cnt * sizeof(struct re_desc))
#define RE_RING_ALIGN		256
#define RE_IFQ_MAXLEN		512
#define RE_MAXSEGS		16
#define RE_TXDESC_SPARE		5
#define RE_JBUF_COUNT(sc)	(((sc)->re_rx_desc_cnt * 3) / 2)

#define RE_RXDESC_INC(sc, x)	(x = (x + 1) % (sc)->re_rx_desc_cnt)
#define RE_TXDESC_INC(sc, x)	(x = (x + 1) % (sc)->re_tx_desc_cnt)
#define RE_OWN(x)		(le32toh((x)->re_cmdstat) & RE_RDESC_STAT_OWN)
#define RE_RXBYTES(x)		(le32toh((x)->re_cmdstat) & sc->re_rxlenmask)
#define RE_PKTSZ(x)		((x)/* >> 3*/)

#define RE_ADDR_LO(y)		((uint64_t) (y) & 0xFFFFFFFF)
#define RE_ADDR_HI(y)		((uint64_t) (y) >> 32)

#define RE_MTU_6K		(6 * 1024)
#define RE_MTU_9K		(9 * 1024)

#define RE_ETHER_EXTRA		(ETHER_HDR_LEN + ETHER_CRC_LEN + EVL_ENCAPLEN)
#define RE_FRAMELEN(mtu)	((mtu) + RE_ETHER_EXTRA)

#define RE_FRAMELEN_6K		RE_FRAMELEN(RE_MTU_6K)
#define RE_FRAMELEN_9K		RE_FRAMELEN(RE_MTU_9K)
#define RE_FRAMELEN_MAX		RE_FRAMELEN_9K

#define RE_RXBUF_ALIGN		8
#define RE_JBUF_SIZE		roundup2(RE_FRAMELEN_MAX, RE_RXBUF_ALIGN)

#define	RE_TIMEOUT		1000

struct re_hwrev {
	uint32_t		re_hwrev;
	int			re_maxmtu;
	uint32_t		re_caps;	/* see RE_C_ */
};

struct re_softc;
struct re_jbuf {
	struct re_softc 	*re_sc;
	int			re_inuse;
	int			re_slot;
	caddr_t			re_buf;
	bus_addr_t		re_paddr;
	SLIST_ENTRY(re_jbuf)	re_link;
};

struct re_list_data {
	struct mbuf		**re_tx_mbuf;
	struct mbuf		**re_rx_mbuf;
	bus_addr_t		*re_rx_paddr;
	int			re_tx_prodidx;
	int			re_rx_prodidx;
	int			re_tx_considx;
	int			re_tx_free;
	bus_dmamap_t		*re_tx_dmamap;
	bus_dmamap_t		*re_rx_dmamap;
	bus_dmamap_t		re_rx_spare;
	bus_dma_tag_t		re_rx_mtag;	/* RX mbuf mapping tag */
	bus_dma_tag_t		re_tx_mtag;	/* TX mbuf mapping tag */
	bus_dma_tag_t		re_stag;	/* stats mapping tag */
	bus_dmamap_t		re_smap;	/* stats map */
	struct re_stats		*re_stats;
	bus_addr_t		re_stats_addr;
	bus_dma_tag_t		re_rx_list_tag;
	bus_dmamap_t		re_rx_list_map;
	struct re_desc		*re_rx_list;
	bus_addr_t		re_rx_list_addr;
	bus_dma_tag_t		re_tx_list_tag;
	bus_dmamap_t		re_tx_list_map;
	struct re_desc		*re_tx_list;
	bus_addr_t		re_tx_list_addr;

	bus_dma_tag_t		re_jpool_tag;
	bus_dmamap_t		re_jpool_map;
	caddr_t			re_jpool;
	struct re_jbuf		*re_jbuf;
	struct lwkt_serialize	re_jbuf_serializer;
	SLIST_HEAD(, re_jbuf)	re_jbuf_free;
};

struct re_softc {
	struct arpcom		arpcom;		/* interface info */
	device_t		re_dev;
	bus_space_handle_t	re_bhandle;	/* bus space handle */
	bus_space_tag_t		re_btag;	/* bus space tag */
	int			re_res_rid;
	int			re_res_type;
	struct resource		*re_res;
	struct resource		*re_irq;
	void			*re_intrhand;
	device_t		re_miibus;
	bus_dma_tag_t		re_parent_tag;
	bus_dma_tag_t		re_tag;
	uint32_t		re_hwrev;
	struct ifpoll_compat	re_npoll;
	struct re_list_data	re_ldata;
	struct callout		re_timer;
	struct mbuf		*re_head;
	struct mbuf		*re_tail;
	uint32_t		re_caps;	/* see RE_C_ */
	uint32_t		re_rxlenmask;
	int			re_txstart;
	int			re_eewidth;
	int			re_ee_eaddr;
	int			re_maxmtu;
	int			re_rx_desc_cnt;
	int			re_tx_desc_cnt;
	int			re_bus_speed;
	int			rxcycles;
	int			re_rxbuf_size;
	int			(*re_newbuf)(struct re_softc *, int, int);
	int			re_irq_type;
	int			re_irq_rid;

	uint32_t		re_flags;	/* see RE_F_ */
	int			re_if_flags;	/* saved ifnet.if_flags */

	u_long			re_hwassist;
	uint16_t		re_intrs;
	uint16_t		re_tx_ack;
	uint16_t		re_rx_ack;
	int			re_tx_time;
	int			re_rx_time;
	int			re_sim_time;
	int			re_imtype;	/* see RE_IMTYPE_ */

	uint32_t		saved_maps[5];	/* pci data */
	uint32_t		saved_biosaddr;
	uint8_t			saved_intline;
	uint8_t			saved_cachelnsz;
	uint8_t			saved_lattimer;
};

#define RE_C_PCIE		0x1	/* PCI-E */
#define RE_C_PCI64		0x2	/* PCI64 */
#define RE_C_HWIM		0x4	/* hardware interrupt moderation */
#define RE_C_HWCSUM		0x8	/* hardware csum offload */
#define RE_C_8139CP		0x20	/* is 8139C+ */
#define RE_C_MAC2		0x40	/* MAC style 2 */
#define RE_C_PHYPMGT		0x80	/* PHY supports power mgmt */
#define RE_C_8169		0x100	/* is 8110/8169 */
#define RE_C_AUTOPAD		0x200	/* hardware auto-pad short frames */
#define RE_C_CONTIGRX		0x400	/* need contig buf to RX jumbo frames */
#define RE_C_STOP_RXTX		0x800	/* could stop RX/TX engine */
#define RE_C_FASTE		0x1000	/* 10/100 only NIC */
#define RE_C_EE_EADDR		0x2000	/* ethernet address in EEPROM */

#define RE_IS_8139CP(sc)	((sc)->re_caps & RE_C_8139CP)
#define RE_IS_8169(sc)		((sc)->re_caps & RE_C_8169)

/* Interrupt moderation types */
#define RE_IMTYPE_NONE		0
#define RE_IMTYPE_SIM		1	/* simulated */
#define RE_IMTYPE_HW		2	/* hardware based */

#define RE_F_TIMER_INTR		0x1
#define RE_F_USE_JPOOL		0x2
#define RE_F_DROP_RXFRAG	0x4
#define RE_F_LINKED		0x8
#define RE_F_SUSPENDED		0x10
#define RE_F_TESTMODE		0x20

/*
 * register space access macros
 */
#define CSR_WRITE_STREAM_4(sc, reg, val)	\
	bus_space_write_stream_4(sc->re_btag, sc->re_bhandle, reg, val)
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->re_btag, sc->re_bhandle, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->re_btag, sc->re_bhandle, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->re_btag, sc->re_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->re_btag, sc->re_bhandle, reg)
#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->re_btag, sc->re_bhandle, reg)
#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->re_btag, sc->re_bhandle, reg)

#define CSR_SETBIT_1(sc, reg, val)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | (val))
#define CSR_CLRBIT_1(sc, reg, val)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~(val))
