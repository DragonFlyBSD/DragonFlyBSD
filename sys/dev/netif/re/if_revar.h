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
 * $DragonFly: src/sys/dev/netif/re/if_revar.h,v 1.3 2007/01/15 12:53:26 sephe Exp $
 */

struct re_chain_data {
	uint16_t		cur_rx;
	caddr_t			re_rx_buf;
	caddr_t			re_rx_buf_ptr;
	bus_dmamap_t		re_rx_dmamap;

	struct mbuf		*re_tx_chain[RE_TX_LIST_CNT];
	bus_dmamap_t		re_tx_dmamap[RE_TX_LIST_CNT];
	uint8_t			last_tx;
	uint8_t			cur_tx;
};

#define RE_INC(x)		(x = (x + 1) % RE_TX_LIST_CNT)
#define RE_CUR_TXADDR(x)	((x->re_cdata.cur_tx * 4) + RE_TXADDR0)
#define RE_CUR_TXSTAT(x)	((x->re_cdata.cur_tx * 4) + RE_TXSTAT0)
#define RE_CUR_TXMBUF(x)	(x->re_cdata.re_tx_chain[x->re_cdata.cur_tx])
#define RE_CUR_DMAMAP(x)	(x->re_cdata.re_tx_dmamap[x->re_cdata.cur_tx])
#define RE_LAST_TXADDR(x)	((x->re_cdata.last_tx * 4) + RE_TXADDR0)
#define RE_LAST_TXSTAT(x)	((x->re_cdata.last_tx * 4) + RE_TXSTAT0)
#define RE_LAST_TXMBUF(x)	(x->re_cdata.re_tx_chain[x->re_cdata.last_tx])
#define RE_LAST_DMAMAP(x)	(x->re_cdata.re_tx_dmamap[x->re_cdata.last_tx])

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
	const char		*re_desc;
};

#define RE_8139CPLUS		3
#define RE_8169			4

struct re_softc;

struct re_dmaload_arg {
	struct re_softc		*sc;
	int			re_idx;
	int			re_maxsegs;
	uint32_t		re_flags;
	struct re_desc		*re_ring;
};

struct re_list_data {
	struct mbuf		*re_tx_mbuf[RE_TX_DESC_CNT];
	struct mbuf		*re_rx_mbuf[RE_TX_DESC_CNT];
	int			re_tx_prodidx;
	int			re_rx_prodidx;
	int			re_tx_considx;
	int			re_tx_free;
	bus_dmamap_t		re_tx_dmamap[RE_TX_DESC_CNT];
	bus_dmamap_t		re_rx_dmamap[RE_RX_DESC_CNT];
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
	struct re_chain_data	re_cdata;
	struct re_list_data	re_ldata;
	struct callout		re_timer;
	struct mbuf		*re_head;
	struct mbuf		*re_tail;
	uint32_t		re_flags;	/* see RE_F_ */
	uint32_t		re_rxlenmask;
	int			re_txstart;
	int			re_testmode;
	int			suspended;	/* 0 = normal  1 = suspended */
	int			re_link;
	int			re_eewidth;
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
