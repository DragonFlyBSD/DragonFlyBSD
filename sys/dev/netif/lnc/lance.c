/*	$NetBSD: lance.c,v 1.34 2005/12/24 20:27:30 perry Exp $	*/
/*	$FreeBSD: src/sys/dev/le/lance.c,v 1.2 2006/05/16 21:04:01 marius Exp $	*/


/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>

#include <dev/netif/lnc/lancereg.h>
#include <dev/netif/lnc/lancevar.h>

devclass_t le_devclass;

static void lance_start(struct ifnet *, struct ifaltq_subque *);
static void lance_init(void *);
static void lance_watchdog(struct ifnet *);
static int lance_mediachange(struct ifnet *);
static void lance_mediastatus(struct ifnet *, struct ifmediareq *);
static int lance_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

void
lance_config(struct lance_softc *sc, const char* name, int unit)
{
	struct ifnet *ifp;
	int i, nbuf;

	ifp = sc->ifp = &sc->sc_if;

	/* Initialize ifnet structure. */
	ifp->if_softc = sc;
	if_initname(ifp, name, unit);
	ifp->if_start = lance_start;
	ifp->if_ioctl = lance_ioctl;
	ifp->if_watchdog = lance_watchdog;
	ifp->if_init = lance_init;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
#ifdef LANCE_REVC_BUG
	ifp->if_flags &= ~IFF_MULTICAST;
#endif
	ifp->if_baudrate = IF_Mbps(10);
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* Attach the interface. */
	ether_ifattach(ifp, sc->sc_enaddr, NULL);

	/* Claim 802.1q capability. */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable |= IFCAP_VLAN_MTU;

	/* Initialize ifmedia structures. */
	ifmedia_init(&sc->sc_media, 0, lance_mediachange, lance_mediastatus);
	if (sc->sc_supmedia != NULL) {
		for (i = 0; i < sc->sc_nsupmedia; i++)
			ifmedia_add(&sc->sc_media, sc->sc_supmedia[i], 0, NULL);
		ifmedia_set(&sc->sc_media, sc->sc_defaultmedia);
	} else {
		ifmedia_add(&sc->sc_media,
		    IFM_MAKEWORD(IFM_ETHER, IFM_MANUAL, 0, 0), 0, NULL);
		ifmedia_set(&sc->sc_media,
		    IFM_MAKEWORD(IFM_ETHER, IFM_MANUAL, 0, 0));
	}

	switch (sc->sc_memsize) {
	case 8192:
		sc->sc_nrbuf = 4;
		sc->sc_ntbuf = 1;
		break;
	case 16384:
		sc->sc_nrbuf = 8;
		sc->sc_ntbuf = 2;
		break;
	case 32768:
		sc->sc_nrbuf = 16;
		sc->sc_ntbuf = 4;
		break;
	case 65536:
		sc->sc_nrbuf = 32;
		sc->sc_ntbuf = 8;
		break;
	case 131072:
		sc->sc_nrbuf = 64;
		sc->sc_ntbuf = 16;
		break;
	case 262144:
		sc->sc_nrbuf = 128;
		sc->sc_ntbuf = 32;
		break;
	default:
		/* weird memory size; cope with it */
		nbuf = sc->sc_memsize / LEBLEN;
		sc->sc_ntbuf = nbuf / 5;
		sc->sc_nrbuf = nbuf - sc->sc_ntbuf;
	}

	if_printf(ifp, "%d receive buffers, %d transmit buffers\n",
	    sc->sc_nrbuf, sc->sc_ntbuf);

	/* Make sure the chip is stopped. */
	lance_stop(sc);
}

void
lance_suspend(struct lance_softc *sc)
{

	lwkt_serialize_enter(sc->ifp->if_serializer);
	lance_stop(sc);
	lwkt_serialize_exit(sc->ifp->if_serializer);
}

void
lance_resume(struct lance_softc *sc)
{

	lwkt_serialize_enter(sc->ifp->if_serializer);
	if (sc->ifp->if_flags & IFF_UP)
		lance_init_locked(sc);
	lwkt_serialize_exit(sc->ifp->if_serializer);
}

static void
lance_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct lance_softc *sc = ifp->if_softc;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	(*sc->sc_start_locked)(sc);
}

void
lance_stop(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_STOP);
}

static void
lance_init(void *xsc)
{
	struct lance_softc *sc = (struct lance_softc *)xsc;

	crit_enter();
	lance_init_locked(sc);
	crit_exit();
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
void
lance_init_locked(struct lance_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	u_long a;
	int timo;

	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_STOP);
	DELAY(100);

	/* Newer LANCE chips have a reset register. */
	if (sc->sc_hwreset)
		(*sc->sc_hwreset)(sc);

	/* Set the correct byte swapping mode, etc. */
	(*sc->sc_wrcsr)(sc, LE_CSR3, sc->sc_conf3);

	/*
	 * Update our private copy of the Ethernet address.
	 * We NEED the copy so we can ensure its alignment!
	 */
	memcpy(sc->sc_enaddr, IF_LLADDR(ifp), ETHER_ADDR_LEN);

	/* Set up LANCE init block. */
	(*sc->sc_meminit)(sc);

	/* Give LANCE the physical address of its init block. */
	a = sc->sc_addr + LE_INITADDR(sc);
	(*sc->sc_wrcsr)(sc, LE_CSR1, a & 0xffff);
	(*sc->sc_wrcsr)(sc, LE_CSR2, a >> 16);

	/* Try to initialize the LANCE. */
	DELAY(100);
	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INIT);

	/* Wait for initialization to finish. */
	for (timo = 100000; timo; timo--)
		if ((*sc->sc_rdcsr)(sc, LE_CSR0) & LE_C0_IDON)
			break;

	/* Set the current media. */
	if (sc->sc_mediachange)
		(*sc->sc_mediachange)(sc);

	if ((*sc->sc_rdcsr)(sc, LE_CSR0) & LE_C0_IDON) {
		/* Start the LANCE. */
		(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INEA | LE_C0_STRT);
		ifp->if_flags |= IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		if_devstart(ifp);
	} else
		if_printf(ifp, "controller failed to initialize\n");

	if (sc->sc_hwinit)
		(*sc->sc_hwinit)(sc);
}

/*
 * Routine to copy from mbuf chain to transmit buffer in
 * network buffer memory.
 */
int
lance_put(struct lance_softc *sc, int boff, struct mbuf *m)
{
	int tlen = 0;

	for (; m; m = m_free(m)) {
		if (m->m_len == 0)
			continue;
		(*sc->sc_copytobuf)(sc, mtod(m, caddr_t), boff, m->m_len);
		boff += m->m_len;
		tlen += m->m_len;
	}
	if (tlen < LEMINSIZE) {
		(*sc->sc_zerobuf)(sc, boff, LEMINSIZE - tlen);
		tlen = LEMINSIZE;
	}
	return (tlen);
}

/*
 * Pull data off an interface.
 * Len is length of data, with local net header stripped.
 * We copy the data into mbufs.  When full cluster sized units are present
 * we copy into clusters.
 */
struct mbuf *
lance_get(struct lance_softc *sc, int boff, int totlen)
{
	struct ifnet *ifp = sc->ifp;
	struct mbuf *m0 = NULL, *newm;
	struct mbuf *m = NULL;
	int mlen;

	if (totlen <= ETHER_HDR_LEN || totlen > LEBLEN - ETHER_CRC_LEN) {
#ifdef LEDEBUG
		if_printf(ifp, "invalid packet size %d; dropping\n", totlen);
#endif
		return (NULL);
	}

	do {
		newm = m_getl(totlen, M_NOWAIT, MT_DATA,
			      m0 == NULL ? M_PKTHDR : 0, &mlen);
		if (newm == NULL)
			goto bad;

		if (m0 == NULL) {
			caddr_t newdata;

			m0 = newm;
			m0->m_pkthdr.rcvif = ifp;
			m0->m_pkthdr.len = totlen;
			newdata = (caddr_t)
			    ALIGN(m0->m_data + ETHER_HDR_LEN) - ETHER_HDR_LEN;
			mlen -= newdata - m0->m_data;
			m0->m_data = newdata;
		} else {
			m->m_next = newm;
		}
		m = newm;

		m->m_len = min(totlen, mlen);
		(*sc->sc_copyfrombuf)(sc, mtod(m, caddr_t), boff, m->m_len);
		boff += m->m_len;
		totlen -= m->m_len;
	} while (totlen > 0);

	return (m0);

bad:
	m_freem(m0);
	return (NULL);
}

static void
lance_watchdog(struct ifnet *ifp)
{
	struct lance_softc *sc = ifp->if_softc;

	if_printf(ifp, "device timeout\n");
	IFNET_STAT_INC(ifp, oerrors, 1);
	lance_init_locked(sc);
}

static int
lance_mediachange(struct ifnet *ifp)
{
	struct lance_softc *sc = ifp->if_softc;
	int error;

	if (sc->sc_mediachange) {
		error = (*sc->sc_mediachange)(sc);
		return (error);
	}
	return (0);
}

static void
lance_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct lance_softc *sc = ifp->if_softc;

	if (!(ifp->if_flags & IFF_UP)) {
		return;
	}

	ifmr->ifm_status = IFM_AVALID;
	if (sc->sc_flags & LE_CARRIER)
		ifmr->ifm_status |= IFM_ACTIVE;

	if (sc->sc_mediastatus)
		(*sc->sc_mediastatus)(sc, ifmr);
}

/*
 * Process an ioctl request.
 */
static int
lance_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct lance_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	crit_enter();

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_PROMISC) {
			if (!(sc->sc_flags & LE_PROMISC)) {
				sc->sc_flags |= LE_PROMISC;
				lance_init_locked(sc);
			}
		} else if (sc->sc_flags & LE_PROMISC) {
			sc->sc_flags &= ~LE_PROMISC;
			lance_init_locked(sc);
		}

		if ((ifp->if_flags & IFF_ALLMULTI) &&
		    !(sc->sc_flags & LE_ALLMULTI)) {
			sc->sc_flags |= LE_ALLMULTI;
			lance_init_locked(sc);
		} else if (!(ifp->if_flags & IFF_ALLMULTI) &&
		    (sc->sc_flags & LE_ALLMULTI)) {
			sc->sc_flags &= ~LE_ALLMULTI;
			lance_init_locked(sc);
		}

		if (!(ifp->if_flags & IFF_UP) &&
		    ifp->if_flags & IFF_RUNNING) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			lance_stop(sc);
		} else if (ifp->if_flags & IFF_UP &&
		    !(ifp->if_flags & IFF_RUNNING)) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			lance_init_locked(sc);
		}
#ifdef LEDEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_flags |= LE_DEBUG;
		else
			sc->sc_flags &= ~LE_DEBUG;
#endif
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		if (ifp->if_flags & IFF_RUNNING)
			lance_init_locked(sc);
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	crit_exit();

	return (error);
}

/*
 * Set up the logical address filter.
 */
void
lance_setladrf(struct lance_softc *sc, uint16_t *af)
{
	struct ifnet *ifp = sc->ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;

	/*
	 * Set up multicast address filter by passing all multicast addresses
	 * through a crc generator, and then using the high order 6 bits as an
	 * index into the 64 bit logical address filter.  The high order bit
	 * selects the word, while the rest of the bits select the bit within
	 * the word.
	 */

	if (ifp->if_flags & IFF_PROMISC || sc->sc_flags & LE_ALLMULTI) {
		af[0] = af[1] = af[2] = af[3] = 0xffff;
		return;
	}

	af[0] = af[1] = af[2] = af[3] = 0x0000;

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		crc = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);

		/* Just want the 6 most significant bits. */
		crc >>= 26;

		/* Set the corresponding bit in the filter. */
		af[crc >> 4] |= LE_HTOLE16(1 << (crc & 0xf));
	}
}

/*
 * Routines for accessing the transmit and receive buffers.
 * The various CPU and adapter configurations supported by this
 * driver require three different access methods for buffers
 * and descriptors:
 *	(1) contig (contiguous data; no padding),
 *	(2) gap2 (two bytes of data followed by two bytes of padding),
 *	(3) gap16 (16 bytes of data followed by 16 bytes of padding).
 */

/*
 * contig: contiguous data with no padding.
 *
 * Buffers may have any alignment.
 */

void
lance_copytobuf_contig(struct lance_softc *sc, void *from, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;

	/*
	 * Just call memcpy() to do the work.
	 */
	memcpy(buf + boff, from, len);
}

void
lance_copyfrombuf_contig(struct lance_softc *sc, void *to, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;

	/*
	 * Just call memcpy() to do the work.
	 */
	memcpy(to, buf + boff, len);
}

void
lance_zerobuf_contig(struct lance_softc *sc, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;

	/*
	 * Just let memset() do the work
	 */
	memset(buf + boff, 0, len);
}

#if 0
/*
 * Examples only; duplicate these and tweak (if necessary) in
 * machine-specific front-ends.
 */

/*
 * gap2: two bytes of data followed by two bytes of pad.
 *
 * Buffers must be 4-byte aligned.  The code doesn't worry about
 * doing an extra byte.
 */

static void
lance_copytobuf_gap2(struct lance_softc *sc, void *fromv, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t from = fromv;
	volatile uint16_t *bptr;

	if (boff & 0x1) {
		/* Handle unaligned first byte. */
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*bptr = (*from++ << 8) | (*bptr & 0xff);
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 1) {
		*bptr = (from[1] << 8) | (from[0] & 0xff);
		bptr += 2;
		from += 2;
		len -= 2;
	}
	if (len == 1)
		*bptr = (uint16_t)*from;
}

static void
lance_copyfrombuf_gap2(struct lance_softc *sc, void *tov, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t to = tov;
	volatile uint16_t *bptr;
	uint16_t tmp;

	if (boff & 0x1) {
		/* Handle unaligned first byte. */
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*to++ = (*bptr >> 8) & 0xff;
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 1) {
		tmp = *bptr;
		*to++ = tmp & 0xff;
		*to++ = (tmp >> 8) & 0xff;
		bptr += 2;
		len -= 2;
	}
	if (len == 1)
		*to = *bptr & 0xff;
}

static void
lance_zerobuf_gap2(struct lance_softc *sc, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	volatile uint16_t *bptr;

	if ((unsigned)boff & 0x1) {
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*bptr &= 0xff;
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 0) {
		*bptr = 0;
		bptr += 2;
		len -= 2;
	}
}

/*
 * gap16: 16 bytes of data followed by 16 bytes of pad.
 *
 * Buffers must be 32-byte aligned.
 */

static void
lance_copytobuf_gap16(struct lance_softc *sc, void *fromv, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t bptr, from = fromv;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memcpy(bptr + boff, from, xfer);
		from += xfer;
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}

static void
lance_copyfrombuf_gap16(struct lance_softc *sc, void *tov, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t bptr, to = tov;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memcpy(to, bptr + boff, xfer);
		to += xfer;
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}

static void
lance_zerobuf_gap16(struct lance_softc *sc, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t bptr;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memset(bptr + boff, 0, xfer);
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}
#endif /* Example only */
