/*	$OpenBSD: if_nfevar.h,v 1.11 2006/02/19 13:57:02 damien Exp $	*/

/*
 * Copyright (c) 2005 Jonathan Gray <jsg@openbsd.org>
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

struct nfe_tx_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

struct nfe_tx_ring {
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_addr_t		physaddr;
	struct nfe_desc32	*desc32;
	struct nfe_desc64	*desc64;

	bus_dma_tag_t		data_tag;
	struct nfe_tx_data	*data;
	int			queued;
	int			cur;
	int			next;
};

struct nfe_softc;

struct nfe_jbuf {
	struct nfe_softc	*sc;
	struct nfe_rx_ring	*ring;
	int			inuse;
	int			slot;
	caddr_t			buf;
	bus_addr_t		physaddr;
	SLIST_ENTRY(nfe_jbuf)	jnext;
};

struct nfe_rx_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

struct nfe_rx_ring {
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_addr_t		physaddr;
	struct nfe_desc32	*desc32;
	struct nfe_desc64	*desc64;

	bus_dma_tag_t		jtag;
	bus_dmamap_t		jmap;
	caddr_t			jpool;
	struct nfe_jbuf		*jbuf;
	SLIST_HEAD(, nfe_jbuf)	jfreelist;

	bus_dma_tag_t		data_tag;
	bus_dmamap_t		data_tmpmap;
	struct nfe_rx_data	*data;
	int			bufsz;
	int			cur;
	int			next;
};

struct nfe_softc {
	struct arpcom		arpcom;

	int			sc_mem_rid;
	time_t			sc_rate_second;
	int			sc_rate_acc;
	int			sc_rate_avg;
	struct resource		*sc_mem_res;
	bus_space_handle_t	sc_memh;
	bus_space_tag_t		sc_memt;

	int			sc_irq_rid;
	struct resource		*sc_irq_res;
	void			*sc_ih;

	device_t		sc_miibus;
	struct callout		sc_tick_ch;

	int			sc_if_flags;
	uint32_t		sc_caps;	/* hardware capabilities */
#define NFE_JUMBO_SUP	0x01
#define NFE_40BIT_ADDR	0x02
#define NFE_HW_CSUM	0x04
#define NFE_HW_VLAN	0x08
#define NFE_FIX_EADDR	0x10
#define NFE_NO_PWRCTL	0x20
#define NFE_WORDALIGN	0x40	/* word alignment DMA */

	uint32_t		sc_flags;
#define NFE_F_USE_JUMBO	0x01	/* use jumbo frame */
#define NFE_F_DYN_IM	0x02	/* enable dynamic interrupt moderation */
#define NFE_F_IRQ_TIMER	0x04	/* hardware timer irq is used */

	uint32_t		rxtxctl_desc;
	uint32_t		rxtxctl;
	uint8_t			mii_phyaddr;

	struct ifpoll_compat	sc_npoll;
	bus_dma_tag_t		sc_dtag;
	struct nfe_tx_ring	txq;
	struct nfe_rx_ring	rxq;

	uint32_t		sc_irq_enable;
	int			sc_tx_spare;
	int			sc_imtime;
	int			sc_rx_ring_count;
	int			sc_tx_ring_count;
	int			sc_debug;

	struct lwkt_serialize	sc_jbuf_serializer;
};

#define NFE_IRQ_ENABLE(sc)	\
	((sc)->sc_imtime == 0 ? NFE_IRQ_NOIMTIMER : \
	 (((sc)->sc_flags & NFE_F_DYN_IM) ? NFE_IRQ_NOIMTIMER: NFE_IRQ_IMTIMER))

#define NFE_ADDR_HI(addr)	((uint64_t) (addr) >> 32)
#define NFE_ADDR_LO(addr)	((uint64_t) (addr) & 0xffffffff)
