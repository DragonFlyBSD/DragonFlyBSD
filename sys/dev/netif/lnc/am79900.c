/*	$NetBSD: am79900.c,v 1.17 2005/12/24 20:27:29 perry Exp $	*/
/*	$FreeBSD: src/sys/dev/le/am79900.c,v 1.3 2006/05/16 21:04:01 marius Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
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

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

/*-
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_var.h>
#include <net/ifq_var.h>

#include <dev/netif/lnc/lancereg.h>
#include <dev/netif/lnc/lancevar.h>
#include <dev/netif/lnc/am79900reg.h>
#include <dev/netif/lnc/am79900var.h>

static void	am79900_meminit(struct lance_softc *);
static void	am79900_rint(struct lance_softc *);
static void	am79900_tint(struct lance_softc *);
static void	am79900_start_locked(struct lance_softc *sc);

#ifdef LEDEBUG
static void	am79900_recv_print(struct lance_softc *, int);
static void	am79900_xmit_print(struct lance_softc *, int);
#endif

int
am79900_config(struct am79900_softc *sc, const char* name, int unit)
{
	int mem;

	sc->lsc.sc_meminit = am79900_meminit;
	sc->lsc.sc_start_locked = am79900_start_locked;

	lance_config(&sc->lsc, name, unit);

	mem = 0;
	sc->lsc.sc_initaddr = mem;
	mem += sizeof(struct leinit);
	sc->lsc.sc_rmdaddr = mem;
	mem += sizeof(struct lermd) * sc->lsc.sc_nrbuf;
	sc->lsc.sc_tmdaddr = mem;
	mem += sizeof(struct letmd) * sc->lsc.sc_ntbuf;
	sc->lsc.sc_rbufaddr = mem;
	mem += LEBLEN * sc->lsc.sc_nrbuf;
	sc->lsc.sc_tbufaddr = mem;
	mem += LEBLEN * sc->lsc.sc_ntbuf;

	if (mem > sc->lsc.sc_memsize)
		panic("%s: memsize", __func__);

	return (0);
}

void
am79900_detach(struct am79900_softc *sc)
{

	ether_ifdetach(sc->lsc.ifp);
}

/*
 * Set up the initialization block and the descriptor rings.
 */
static void
am79900_meminit(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct leinit init;
	struct lermd rmd;
	struct letmd tmd;
	u_long a;
	int bix;

	if (ifp->if_flags & IFF_PROMISC)
		init.init_mode = LE_HTOLE32(LE_MODE_NORMAL | LE_MODE_PROM);
	else
		init.init_mode = LE_HTOLE32(LE_MODE_NORMAL);

	init.init_mode |= LE_HTOLE32(((ffs(sc->sc_ntbuf) - 1) << 28) |
	    ((ffs(sc->sc_nrbuf) - 1) << 20));

	init.init_padr[0] = LE_HTOLE32(sc->sc_enaddr[0] |
	    (sc->sc_enaddr[1] << 8) | (sc->sc_enaddr[2] << 16) |
	    (sc->sc_enaddr[3] << 24));
	init.init_padr[1] = LE_HTOLE32(sc->sc_enaddr[4] |
	    (sc->sc_enaddr[5] << 8));
	lance_setladrf(sc, init.init_ladrf);

	sc->sc_last_rd = 0;
	sc->sc_first_td = sc->sc_last_td = sc->sc_no_td = 0;

	a = sc->sc_addr + LE_RMDADDR(sc, 0);
	init.init_rdra = LE_HTOLE32(a);

	a = sc->sc_addr + LE_TMDADDR(sc, 0);
	init.init_tdra = LE_HTOLE32(a);

	(*sc->sc_copytodesc)(sc, &init, LE_INITADDR(sc), sizeof(init));

	/*
	 * Set up receive ring descriptors.
	 */
	for (bix = 0; bix < sc->sc_nrbuf; bix++) {
		a = sc->sc_addr + LE_RBUFADDR(sc, bix);
		rmd.rmd0 = LE_HTOLE32(a);
		rmd.rmd1 = LE_HTOLE32(LE_R1_OWN | LE_R1_ONES |
		    (-LEBLEN & 0xfff));
		rmd.rmd2 = 0;
		rmd.rmd3 = 0;
		(*sc->sc_copytodesc)(sc, &rmd, LE_RMDADDR(sc, bix),
		    sizeof(rmd));
	}

	/*
	 * Set up transmit ring descriptors.
	 */
	for (bix = 0; bix < sc->sc_ntbuf; bix++) {
		a = sc->sc_addr + LE_TBUFADDR(sc, bix);
		tmd.tmd0 = LE_HTOLE32(a);
		tmd.tmd1 = LE_HTOLE32(LE_T1_ONES);
		tmd.tmd2 = 0;
		tmd.tmd3 = 0;
		(*sc->sc_copytodesc)(sc, &tmd, LE_TMDADDR(sc, bix),
		    sizeof(tmd));
	}
}

static void
am79900_rint(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct mbuf *m;
	struct lermd rmd;
	uint32_t rmd1;
	int bix, rp;
#if defined(__i386__)
	struct ether_header *eh;
#endif

	bix = sc->sc_last_rd;

	/* Process all buffers with valid data. */
	for (;;) {
		rp = LE_RMDADDR(sc, bix);
		(*sc->sc_copyfromdesc)(sc, &rmd, rp, sizeof(rmd));

		rmd1 = LE_LE32TOH(rmd.rmd1);
		if (rmd1 & LE_R1_OWN)
			break;

		m = NULL;
		if ((rmd1 & (LE_R1_ERR | LE_R1_STP | LE_R1_ENP)) !=
		    (LE_R1_STP | LE_R1_ENP)){
			if (rmd1 & LE_R1_ERR) {
#ifdef LEDEBUG
				if (rmd1 & LE_R1_ENP) {
					if ((rmd1 & LE_R1_OFLO) == 0) {
						if (rmd1 & LE_R1_FRAM)
							if_printf(ifp,
							    "framing error\n");
						if (rmd1 & LE_R1_CRC)
							if_printf(ifp,
							    "crc mismatch\n");
					}
				} else
					if (rmd1 & LE_R1_OFLO)
						if_printf(ifp, "overflow\n");
#endif
				if (rmd1 & LE_R1_BUFF)
					if_printf(ifp,
					    "receive buffer error\n");
			} else if ((rmd1 & (LE_R1_STP | LE_R1_ENP)) !=
			    (LE_R1_STP | LE_R1_ENP))
				if_printf(ifp, "dropping chained buffer\n");
		} else {
#ifdef LEDEBUG
			if (sc->sc_flags & LE_DEBUG)
				am79900_recv_print(sc, bix);
#endif
			/* Pull the packet off the interface. */
			m = lance_get(sc, LE_RBUFADDR(sc, bix),
			    (LE_LE32TOH(rmd.rmd2) & 0xfff) - ETHER_CRC_LEN);
		}

		rmd.rmd1 = LE_HTOLE32(LE_R1_OWN | LE_R1_ONES |
		    (-LEBLEN & 0xfff));
		rmd.rmd2 = 0;
		rmd.rmd3 = 0;
		(*sc->sc_copytodesc)(sc, &rmd, rp, sizeof(rmd));

		if (++bix == sc->sc_nrbuf)
			bix = 0;

		if (m != NULL) {
			IFNET_STAT_INC(ifp, ipackets, 1);

#ifdef __i386__
			/*
			 * The VMware LANCE does not present IFF_SIMPLEX
			 * behavior on multicast packets. Thus drop the
			 * packet if it is from ourselves.
			 */
			eh = mtod(m, struct ether_header *);
			if (memcmp(eh->ether_shost, sc->sc_enaddr,
				   ETHER_ADDR_LEN) == 0) {
				m_freem(m);
				continue;
			}
#endif

			/* Pass the packet up. */
			ifp->if_input(ifp, m, NULL, -1);
		} else
			IFNET_STAT_INC(ifp, ierrors, 1);
	}

	sc->sc_last_rd = bix;
}

static void
am79900_tint(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct letmd tmd;
	uint32_t tmd1, tmd2;
	int bix;

	bix = sc->sc_first_td;

	for (;;) {
		if (sc->sc_no_td <= 0)
			break;

		(*sc->sc_copyfromdesc)(sc, &tmd, LE_TMDADDR(sc, bix),
		    sizeof(tmd));

		tmd1 = LE_LE32TOH(tmd.tmd1);

#ifdef LEDEBUG
		if (sc->sc_flags & LE_DEBUG)
			if_printf(ifp, "trans tmd: "
			    "adr %08x, flags/blen %08x\n",
			    LE_LE32TOH(tmd.tmd0), tmd1);
#endif

		if (tmd1 & LE_T1_OWN)
			break;

		ifq_clr_oactive(&ifp->if_snd);

		if (tmd1 & LE_T1_ERR) {
			tmd2 = LE_LE32TOH(tmd.tmd2);
			if (tmd2 & LE_T2_BUFF)
				if_printf(ifp, "transmit buffer error\n");
			else if (tmd2 & LE_T2_UFLO)
				if_printf(ifp, "underflow\n");
			if (tmd2 & (LE_T2_BUFF | LE_T2_UFLO)) {
				lance_init_locked(sc);
				return;
			}
			if (tmd2 & LE_T2_LCAR) {
				if (sc->sc_flags & LE_CARRIER) {
					ifp->if_link_state = LINK_STATE_DOWN;
					if_link_state_change(ifp);
				}
				sc->sc_flags &= ~LE_CARRIER;
				if (sc->sc_nocarrier)
					(*sc->sc_nocarrier)(sc);
				else
					if_printf(ifp, "lost carrier\n");
			}
			if (tmd2 & LE_T2_LCOL)
				IFNET_STAT_INC(ifp, collisions, 1);
			if (tmd2 & LE_T2_RTRY) {
#ifdef LEDEBUG
				if_printf(ifp, "excessive collisions\n");
#endif
				IFNET_STAT_INC(ifp, collisions, 16);
			}
			IFNET_STAT_INC(ifp, oerrors, 1);
		} else {
			if (tmd1 & LE_T1_ONE)
				IFNET_STAT_INC(ifp, collisions, 1);
			else if (tmd1 & LE_T1_MORE)
				/* Real number is unknown. */
				IFNET_STAT_INC(ifp, collisions, 2);
			IFNET_STAT_INC(ifp, opackets, 1);
		}

		if (++bix == sc->sc_ntbuf)
			bix = 0;

		--sc->sc_no_td;
	}

	sc->sc_first_td = bix;

	ifp->if_timer = sc->sc_no_td > 0 ? 5 : 0;
}

/*
 * Controller interrupt
 */
void
am79900_intr(void *arg)
{
	struct lance_softc *sc = arg;
	struct ifnet *ifp = sc->ifp;
	uint16_t isr;

	if (sc->sc_hwintr && (*sc->sc_hwintr)(sc) == -1) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		lance_init_locked(sc);
		return;
	}

	isr = (*sc->sc_rdcsr)(sc, LE_CSR0);
#if defined(LEDEBUG) && LEDEBUG > 1
	if (sc->sc_flags & LE_DEBUG)
		if_printf(ifp, "%s: entering with isr=%04x\n", __func__, isr);
#endif
	if ((isr & LE_C0_INTR) == 0) {
		return;
	}

	/*
	 * Clear interrupt source flags and turn off interrupts. If we
	 * don't clear these flags before processing their sources we
	 * could completely miss some interrupt events as the NIC can
	 * change these flags while we're in this handler. We turn off
	 * interrupts so we don't get another RX interrupt while still
	 * processing the previous one in ifp->if_input() with the
	 * driver lock dropped.
	 */
	(*sc->sc_wrcsr)(sc, LE_CSR0, isr & ~(LE_C0_INEA | LE_C0_TDMD |
	    LE_C0_STOP | LE_C0_STRT | LE_C0_INIT));

	if (isr & LE_C0_ERR) {
		if (isr & LE_C0_BABL) {
#ifdef LEDEBUG
			if_printf(ifp, "babble\n");
#endif
			IFNET_STAT_INC(ifp, oerrors, 1);
		}
#if 0
		if (isr & LE_C0_CERR) {
			if_printf(ifp, "collision error\n");
			ifp->if_collisions++;
		}
#endif
		if (isr & LE_C0_MISS) {
#ifdef LEDEBUG
			if_printf(ifp, "missed packet\n");
#endif
			IFNET_STAT_INC(ifp, ierrors, 1);
		}
		if (isr & LE_C0_MERR) {
			if_printf(ifp, "memory error\n");
			lance_init_locked(sc);
			return;
		}
	}

	if ((isr & LE_C0_RXON) == 0) {
		if_printf(ifp, "receiver disabled\n");
		IFNET_STAT_INC(ifp, ierrors, 1);
		lance_init_locked(sc);
		return;
	}
	if ((isr & LE_C0_TXON) == 0) {
		if_printf(ifp, "transmitter disabled\n");
		IFNET_STAT_INC(ifp, oerrors, 1);
		lance_init_locked(sc);
		return;
	}

	/*
	 * Pretend we have carrier; if we don't this will be cleared shortly.
	 */
	if (!(sc->sc_flags & LE_CARRIER)) {
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
	}
	sc->sc_flags |= LE_CARRIER;

	if (isr & LE_C0_RINT)
		am79900_rint(sc);
	if (isr & LE_C0_TINT)
		am79900_tint(sc);

	/* Enable interrupts again. */
	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INEA);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Set up output on interface.
 * Get another datagram to send off of the interface queue, and map it to the
 * interface before starting the output.
 */
static void
am79900_start_locked(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct letmd tmd;
	struct mbuf *m;
	int bix, enq, len, rp;

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	bix = sc->sc_last_td;
	enq = 0;

	for (; sc->sc_no_td < sc->sc_ntbuf &&
	    !ifq_is_empty(&ifp->if_snd);) {
		rp = LE_TMDADDR(sc, bix);
		(*sc->sc_copyfromdesc)(sc, &tmd, rp, sizeof(tmd));

		if (LE_LE32TOH(tmd.tmd1) & LE_T1_OWN) {
			ifq_set_oactive(&ifp->if_snd);
			if_printf(ifp,
			    "missing buffer, no_td = %d, last_td = %d\n",
			    sc->sc_no_td, sc->sc_last_td);
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		/*
		 * If BPF is listening on this interface, let it see the packet
		 * before we commit it to the wire.
		 */
		BPF_MTAP(ifp, m);

		/*
		 * Copy the mbuf chain into the transmit buffer.
		 */
		len = lance_put(sc, LE_TBUFADDR(sc, bix), m);

#ifdef LEDEBUG
		if (len > ETHERMTU + ETHER_HDR_LEN)
			if_printf(ifp, "packet length %d\n", len);
#endif

		/*
		 * Init transmit registers, and set transmit start flag.
		 */
		tmd.tmd1 = LE_HTOLE32(LE_T1_OWN | LE_T1_STP | LE_T1_ENP |
		    LE_T1_ONES | (-len & 0xfff));
		tmd.tmd2 = 0;
		tmd.tmd3 = 0;

		(*sc->sc_copytodesc)(sc, &tmd, rp, sizeof(tmd));

#ifdef LEDEBUG
		if (sc->sc_flags & LE_DEBUG)
			am79900_xmit_print(sc, bix);
#endif

		(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INEA | LE_C0_TDMD);
		enq++;

		if (++bix == sc->sc_ntbuf)
			bix = 0;

		if (++sc->sc_no_td == sc->sc_ntbuf) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
	}

	sc->sc_last_td = bix;

	if (enq > 0)
		ifp->if_timer = 5;
}

#ifdef LEDEBUG
static void
am79900_recv_print(struct lance_softc *sc, int no)
{
	struct ifnet *ifp = sc->ifp;
	struct ether_header eh;
	struct lermd rmd;
	uint16_t len;

	(*sc->sc_copyfromdesc)(sc, &rmd, LE_RMDADDR(sc, no), sizeof(rmd));
	len = LE_LE32TOH(rmd.rmd2) & 0xfff;
	if_printf(ifp, "receive buffer %d, len = %d\n", no, len);
	if_printf(ifp, "status %04x\n", (*sc->sc_rdcsr)(sc, LE_CSR0));
	if_printf(ifp, "adr %08x, flags/blen %08x\n", LE_LE32TOH(rmd.rmd0),
	    LE_LE32TOH(rmd.rmd1));
	if (len - ETHER_CRC_LEN >= sizeof(eh)) {
		(*sc->sc_copyfrombuf)(sc, &eh, LE_RBUFADDR(sc, no), sizeof(eh));
		if_printf(ifp, "dst %s", ether_sprintf(eh.ether_dhost));
		kprintf(" src %s type %04x\n", ether_sprintf(eh.ether_shost),
		    ntohs(eh.ether_type));
	}
}

static void
am79900_xmit_print(struct lance_softc *sc, int no)
{
	struct ifnet *ifp = sc->ifp;
	struct ether_header eh;
	struct letmd tmd;
	uint16_t len;

	(*sc->sc_copyfromdesc)(sc, &tmd, LE_TMDADDR(sc, no), sizeof(tmd));
	len = -(LE_LE32TOH(tmd.tmd1) & 0xfff);
	if_printf(ifp, "transmit buffer %d, len = %d\n", no, len);
	if_printf(ifp, "status %04x\n", (*sc->sc_rdcsr)(sc, LE_CSR0));
	if_printf(ifp, "adr %08x, flags/blen %08x\n", LE_LE32TOH(tmd.tmd0),
	    LE_LE32TOH(tmd.tmd1));
	if (len >= sizeof(eh)) {
		(*sc->sc_copyfrombuf)(sc, &eh, LE_TBUFADDR(sc, no), sizeof(eh));
		if_printf(ifp, "dst %s", ether_sprintf(eh.ether_dhost));
		kprintf(" src %s type %04x\n", ether_sprintf(eh.ether_shost),
		    ntohs(eh.ether_type));
	}
}
#endif /* LEDEBUG */
