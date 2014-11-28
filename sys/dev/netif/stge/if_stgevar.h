/*	$NetBSD: if_stgereg.h,v 1.3 2003/02/10 21:10:07 christos Exp $	*/
/*	$FreeBSD: src/sys/dev/stge/if_stgereg.h,v 1.1 2006/07/25 00:37:09 yongari Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * Transmit descriptor list size.
 */
#define	STGE_TX_RING_CNT	256
#define	STGE_TX_LOWAT		(STGE_TX_RING_CNT/32)
#define	STGE_TX_HIWAT		(STGE_TX_RING_CNT - STGE_TX_LOWAT)

/*
 * Receive descriptor list size.
 */
#define	STGE_RX_RING_CNT	256

#define	STGE_MAXTXSEGS		STGE_NTXFRAGS
#define STGE_MAXSGSIZE		PAGE_SIZE

#define STGE_JUMBO_FRAMELEN	9022
#define STGE_JUMBO_MTU	\
	(STGE_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN)

struct stge_txdesc {
	struct mbuf *tx_m;		/* head of our mbuf chain */
	bus_dmamap_t tx_dmamap;		/* our DMA map */
	STAILQ_ENTRY(stge_txdesc) tx_q;
};

STAILQ_HEAD(stge_txdq, stge_txdesc);

struct stge_rxdesc {
	struct mbuf *rx_m;
	bus_dmamap_t rx_dmamap;
};

#define	STGE_ADDR_LO(x)		((u_int64_t) (x) & 0xffffffff)
#define	STGE_ADDR_HI(x)		((u_int64_t) (x) >> 32)

#define	STGE_RING_ALIGN		8

struct stge_chain_data{
	bus_dma_tag_t		stge_parent_tag;
	bus_dma_tag_t		stge_tx_tag;
	struct stge_txdesc	stge_txdesc[STGE_TX_RING_CNT];
	struct stge_txdq	stge_txfreeq;
	struct stge_txdq	stge_txbusyq;
	bus_dma_tag_t		stge_rx_tag;
	struct stge_rxdesc	stge_rxdesc[STGE_RX_RING_CNT];
	bus_dma_tag_t		stge_tx_ring_tag;
	bus_dmamap_t		stge_tx_ring_map;
	bus_dma_tag_t		stge_rx_ring_tag;
	bus_dmamap_t		stge_rx_ring_map;
	bus_dmamap_t		stge_rx_sparemap;

	int			stge_tx_prod;
	int			stge_tx_cons;
	int			stge_tx_cnt;
	int			stge_rx_cons;
	int			stge_rxlen;
	struct mbuf		*stge_rxhead;
	struct mbuf		*stge_rxtail;
};

struct stge_ring_data {
	struct stge_tfd		*stge_tx_ring;
	bus_addr_t		stge_tx_ring_paddr;
	struct stge_rfd		*stge_rx_ring;
	bus_addr_t		stge_rx_ring_paddr;
};

#define STGE_TX_RING_ADDR(sc, i)	\
    ((sc)->sc_rdata.stge_tx_ring_paddr + sizeof(struct stge_tfd) * (i))
#define STGE_RX_RING_ADDR(sc, i)	\
    ((sc)->sc_rdata.stge_rx_ring_paddr + sizeof(struct stge_rfd) * (i))

#define STGE_TX_RING_SZ		\
    (sizeof(struct stge_tfd) * STGE_TX_RING_CNT)
#define STGE_RX_RING_SZ		\
    (sizeof(struct stge_rfd) * STGE_RX_RING_CNT)

/*
 * Software state per device.
 */
struct stge_softc {
	struct arpcom		arpcom;
	device_t		sc_dev;
	device_t		sc_miibus;

	bus_space_handle_t	sc_bhandle;	/* bus space handle */
	bus_space_tag_t		sc_btag;	/* bus space tag */
	int			sc_res_rid;
	int			sc_res_type;
	struct resource		*sc_res;

	int			sc_irq_rid;
	struct resource		*sc_irq;
	void			*sc_ih;		/* interrupt cookie */

	int			sc_rev;		/* silicon revision */

	struct callout		sc_tick_ch;	/* tick callout */

	struct ifpoll_compat	sc_npoll;
	struct stge_chain_data	sc_cdata;
	struct stge_ring_data	sc_rdata;
	int			sc_if_flags;
	int			sc_if_framesize;
	int			sc_txthresh;	/* Tx threshold */
	uint32_t		sc_usefiber:1;	/* if we're fiber */
	uint32_t		sc_stge1023:1;	/* are we a 1023 */
	uint32_t		sc_DMACtrl;	/* prototype DMACtrl reg. */
	uint32_t		sc_MACCtrl;	/* prototype MacCtrl reg. */
	uint16_t		sc_IntEnable;	/* prototype IntEnable reg. */
	uint16_t		sc_led;		/* LED conf. from EEPROM */
	uint8_t			sc_PhyCtrl;	/* prototype PhyCtrl reg. */
	int			sc_suspended;
	int			sc_detach;

	int			sc_rxint_nframe;
	int			sc_rxint_dmawait;
	int			sc_nerr;
};

#define	STGE_MAXERR	5

#define	STGE_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->sc_cdata.stge_rxhead = NULL;				\
	(_sc)->sc_cdata.stge_rxtail = NULL;				\
	(_sc)->sc_cdata.stge_rxlen = 0;					\
} while (/*CONSTCOND*/0)

#define STGE_TIMEOUT 1000

struct stge_mii_frame {
	uint8_t	mii_stdelim;
	uint8_t	mii_opcode;
	uint8_t	mii_phyaddr;
	uint8_t	mii_regaddr;
	uint8_t	mii_turnaround;
	uint16_t mii_data;
};
