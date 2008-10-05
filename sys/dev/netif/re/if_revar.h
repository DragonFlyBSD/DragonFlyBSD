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
 * $DragonFly: src/sys/dev/netif/re/if_revar.h,v 1.12 2008/10/05 04:54:51 sephe Exp $
 */

#define RE_RX_DESC_CNT_DEF	64
#define RE_TX_DESC_CNT_DEF	64
#define RE_RX_DESC_CNT_MAX	1024
#define RE_TX_DESC_CNT_MAX	1024

#define RE_RX_LIST_SZ(sc)	((sc)->re_rx_desc_cnt * sizeof(struct re_desc))
#define RE_TX_LIST_SZ(sc)	((sc)->re_tx_desc_cnt * sizeof(struct re_desc))
#define RE_RING_ALIGN		256
#define RE_IFQ_MAXLEN		512
#define RE_MAXSEGS		16
#define RE_TXDESC_SPARE		4

#define RE_RXDESC_INC(sc, x)	(x = (x + 1) % (sc)->re_rx_desc_cnt)
#define RE_TXDESC_INC(sc, x)	(x = (x + 1) % (sc)->re_tx_desc_cnt)
#define RE_OWN(x)		(le32toh((x)->re_cmdstat) & RE_RDESC_STAT_OWN)
#define RE_RXBYTES(x)		(le32toh((x)->re_cmdstat) & sc->re_rxlenmask)
#define RE_PKTSZ(x)		((x)/* >> 3*/)

#define RE_ADDR_LO(y)		((uint64_t) (y) & 0xFFFFFFFF)
#define RE_ADDR_HI(y)		((uint64_t) (y) >> 32)

#define RE_JUMBO_FRAMELEN	7440
#define RE_JUMBO_MTU		(RE_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define RE_FRAMELEN_2K		2048
#define RE_FRAMELEN(mtu)	(mtu + ETHER_HDR_LEN + ETHER_CRC_LEN)
#define RE_SWCSUM_LIM_8169	2038

#define	RE_TIMEOUT		1000

struct re_type {
	uint16_t		re_vid;
	uint16_t		re_did;
	int			re_basetype;
	const char		*re_name;
};

struct re_hwrev {
	uint32_t		re_rev;
	int			re_type;	/* RE_{8139CPLUS,8169} */
	uint32_t		re_flags;	/* see RE_F_ */
	int			re_swcsum_lim;
	int			re_maxmtu;
};

#define RE_8139CPLUS		3
#define RE_8169			4

struct re_dmaload_arg {
	int			re_nsegs;
	bus_dma_segment_t	*re_segs;
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
	bus_dma_tag_t		re_mtag;	/* mbuf mapping tag */
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
};

struct re_softc {
	struct arpcom		arpcom;		/* interface info */
#ifdef RE_DIAG
	device_t		re_dev;
#endif
	bus_space_handle_t	re_bhandle;	/* bus space handle */
	bus_space_tag_t		re_btag;	/* bus space tag */
	struct resource		*re_res;
	struct resource		*re_irq;
	void			*re_intrhand;
	device_t		re_miibus;
	bus_dma_tag_t		re_parent_tag;
	bus_dma_tag_t		re_tag;
	uint8_t			re_type;
	int			re_eecmd_read;
	uint8_t			re_stats_no_timeout;
	int			re_txthresh;
	uint32_t		re_hwrev;
	struct re_list_data	re_ldata;
	struct callout		re_timer;
	struct mbuf		*re_head;
	struct mbuf		*re_tail;
	int			re_drop_rxfrag;
	uint32_t		re_flags;	/* see RE_F_ */
	uint32_t		re_rxlenmask;
	int			re_txstart;
	int			re_testmode;
	int			suspended;	/* 0 = normal  1 = suspended */
	int			re_link;
	int			re_eewidth;
	int			re_swcsum_lim;
	int			re_maxmtu;
	int			re_rx_desc_cnt;
	int			re_tx_desc_cnt;
#ifdef DEVICE_POLLING
	int			rxcycles;
#endif

	struct sysctl_ctx_list	re_sysctl_ctx;
	struct sysctl_oid	*re_sysctl_tree;
	uint16_t		re_intrs;
	uint16_t		re_tx_ack;

#ifndef BURN_BRIDGES
	uint32_t		saved_maps[5];	/* pci data */
	uint32_t		saved_biosaddr;
	uint8_t			saved_intline;
	uint8_t			saved_cachelnsz;
	uint8_t			saved_lattimer;
#endif
};

#define RE_F_HASMPC		0x1
#define RE_F_PCIE		0x2

#define RE_TX_MODERATION_IS_ENABLED(sc)			\
	((sc)->re_tx_ack == RE_ISR_TIMEOUT_EXPIRED)

#define RE_DISABLE_TX_MODERATION(sc) do {		\
	(sc)->re_tx_ack = RE_ISR_TX_OK;			\
	(sc)->re_intrs = RE_INTRS | RE_ISR_TX_OK;	\
} while (0)

#define RE_ENABLE_TX_MODERATION(sc) do {		\
	(sc)->re_tx_ack = RE_ISR_TIMEOUT_EXPIRED;	\
	(sc)->re_intrs = RE_INTRS;			\
} while (0)

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
