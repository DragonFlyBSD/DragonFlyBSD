/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1997, 1998, 1999, 2000
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
 *
 * $FreeBSD: /c/ncvs/src/sys/pci/if_skreg.h,v 1.9 2000/04/22 02:16:37 wpaul Exp $
 * $NetBSD: if_skvar.h,v 1.6 2005/05/30 04:35:22 christos Exp $
 * $OpenBSD: if_skvar.h,v 1.2 2005/12/22 20:54:47 brad Exp $
 */

/*
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _IF_SKVAR_H_
#define _IF_SKVAR_H_

struct sk_jpool_entry {
	struct sk_if_softc	*sc_if;
	int			inuse;
	int			slot;
	void			*buf;
	bus_addr_t		paddr;
	SLIST_ENTRY(sk_jpool_entry) entry_next;
};

/*
 * Number of DMA segments in a TxCB. Note that this is carefully
 * chosen to make the total struct size an even power of two. It's
 * critical that no TxCB be split across a page boundary since
 * no attempt is made to allocate physically contiguous memory.
 */
#define SK_NTXSEG      30

struct sk_chain_data {
	bus_dma_tag_t		sk_buf_dtag;

	struct mbuf		*sk_tx_mbuf[SK_TX_RING_CNT];
	bus_dma_tag_t		sk_tx_dtag;
	bus_dmamap_t		sk_tx_dmap[SK_TX_RING_CNT];
	int			sk_tx_prod;
	int			sk_tx_cons;
	int			sk_tx_cnt;

	struct mbuf		*sk_rx_mbuf[SK_RX_RING_CNT];
	bus_dma_tag_t		sk_rx_dtag;
	bus_dmamap_t		sk_rx_dmap[SK_RX_RING_CNT];
	bus_dmamap_t		sk_rx_dmap_tmp;
	int			sk_rx_prod;
	int			sk_rx_cons;
	int			sk_rx_cnt;

	struct lwkt_serialize	sk_jpool_serializer;
	bus_dma_tag_t		sk_jpool_dtag;
	bus_dmamap_t		sk_jpool_dmap;
	void			*sk_jpool;
	struct sk_jpool_entry	sk_jpool_ent[SK_JSLOTS];
	SLIST_HEAD(, sk_jpool_entry) sk_jpool_free_ent;
};

struct sk_ring_data {
	bus_dma_tag_t		sk_ring_dtag;

	bus_dma_tag_t		sk_tx_ring_dtag;
	bus_dmamap_t		sk_tx_ring_dmap;
	bus_addr_t		sk_tx_ring_paddr;
	struct sk_tx_desc	*sk_tx_ring;

	bus_dma_tag_t		sk_rx_ring_dtag;
	bus_dmamap_t		sk_rx_ring_dmap;
	bus_addr_t		sk_rx_ring_paddr;
	struct sk_rx_desc	*sk_rx_ring;
};

struct sk_bcom_hack {
	int			reg;
	int			val;
};

#define SK_INC(x, y)	(x) = (x + 1) % y

/* Forward decl. */
struct sk_if_softc;

/* Softc for the GEnesis controller. */
struct sk_softc {
	device_t		sk_dev;
	int			sk_res_rid;
	struct resource		*sk_res;
	bus_space_handle_t	sk_bhandle;	/* bus space handle */
	bus_space_tag_t		sk_btag;	/* bus space tag */

	int			sk_irq_rid;
	struct resource		*sk_irq;
	void			*sk_intrhand;	/* irq handler handle */

	uint8_t			sk_coppertype;
	uint8_t			sk_pmd;		/* physical media type */
	uint8_t			sk_type;
	uint8_t			sk_rev;
	uint8_t			sk_macs;	/* # of MACs */
	uint32_t		sk_rboff;	/* RAMbuffer offset */
	uint32_t		sk_ramsize;	/* amount of RAM on NIC */
	uint32_t		sk_intrmask;
	uint32_t		sk_imtimer_ticks;
	int			sk_imtime;
	struct lwkt_serialize	sk_serializer;
	struct sk_if_softc	*sk_if[2];
	device_t		sk_devs[2];
};

/* Softc for each logical interface */
struct sk_if_softc {
	struct arpcom		arpcom;		/* interface info */
	device_t		sk_miibus;
	uint8_t			sk_port;	/* port # on controller */
	uint8_t			sk_xmac_rev;	/* XMAC chip rev (B2 or C1) */
	uint32_t		sk_rx_ramstart;
	uint32_t		sk_rx_ramend;
	uint32_t		sk_tx_ramstart;
	uint32_t		sk_tx_ramend;
	uint8_t			sk_phytype;
	int			sk_phyaddr;
	int			sk_cnt;
	int			sk_link;
	struct callout		sk_tick_timer;
	struct sk_chain_data	sk_cdata;
	struct sk_ring_data	sk_rdata;
	bus_dma_tag_t		sk_parent_dtag;
	struct sk_softc		*sk_softc;	/* parent controller */
	int			sk_tx_bmu;	/* TX BMU register */
	int			sk_if_flags;
	int			sk_use_jumbo;
};

#define SK_NDESC_RESERVE	2
#define SK_NDESC_SPARE		5

#define SK_IS_OACTIVE(sc_if) \
	((sc_if)->sk_cdata.sk_tx_cnt + SK_NDESC_RESERVE + SK_NDESC_SPARE > \
	 SK_TX_RING_CNT)

#endif /* !_IF_SKVAR_H_ */
