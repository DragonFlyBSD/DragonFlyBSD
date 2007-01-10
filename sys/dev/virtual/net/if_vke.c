/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/virtual/net/if_vke.c,v 1.1 2007/01/10 13:33:22 swildner Exp $
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <machine/md_var.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>

#include <sys/stat.h>
#include <sys/ioccom.h>
#include <net/tap/if_tap.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* XXX */
#define VKE_TX_THRESHOLD	32
#define VKE_RX_THRESHOLD	32

struct vke_softc {
	struct arpcom	arpcom;
	int		sc_fd;
	int		sc_unit;
	struct callout	sc_intr;
	void		*sc_txbuf;
	struct mbuf	*sc_rx_mbuf;
};

static void	vke_start(struct ifnet *);
static void	vke_init(void *);
static void	vke_intr(void *);
static int	vke_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static int	vke_stop(struct vke_softc *);
static int	vke_rxeof(struct vke_softc *);

static void
vke_sysinit(void *arg __unused)
{
	struct vke_softc *sc;
	struct ifnet *ifp;
	struct tapinfo tapinfo;
	uint8_t enaddr[ETHER_ADDR_LEN];

	if (NetifFd < 0)
		return;

	if (ioctl(NetifFd, TAPGIFINFO, &tapinfo) < 0) {
		kprintf("vke0: ioctl(TAPGIFINFO) failed: %s\n",
			strerror(errno));
		return;
	}

	if (ioctl(NetifFd, SIOCGIFADDR, enaddr) < 0) {
		kprintf("vke0: ioctl(SIOCGIFADDR) failed: %s\n",
			strerror(errno));
		return;
	}
	enaddr[1] += 1;

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_txbuf = kmalloc(MCLBYTES, M_DEVBUF, M_WAITOK);
	sc->sc_fd = NetifFd;
	sc->sc_unit = 0;

	callout_init(&sc->sc_intr);

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, "vke", sc->sc_unit);
	ifp->if_softc = sc;
	ifp->if_ioctl = vke_ioctl;
	ifp->if_start = vke_start;
	ifp->if_init = vke_init;
	ifp->if_mtu = tapinfo.mtu;
	ifp->if_baudrate = tapinfo.baudrate;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* TODO: if_media */

	ether_ifattach(ifp, enaddr, NULL);
}
SYSINIT(vke, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, vke_sysinit, NULL);

static void
vke_init(void *xsc)
{
	struct vke_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	vke_stop(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;
	callout_reset(&sc->sc_intr, 5, vke_intr, sc);

	ifp->if_start(ifp);
}

static void
vke_start(struct ifnet *ifp)
{
	struct vke_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	for (i = 0; i < VKE_TX_THRESHOLD; ++i) {
		m = ifq_dequeue(&ifp->if_snd, NULL);
		if (m != NULL) {
			m_copydata(m, 0, m->m_pkthdr.len, sc->sc_txbuf);
			write(sc->sc_fd, sc->sc_txbuf, m->m_pkthdr.len);
			m_freem(m);

			ifp->if_opackets++;
		} else {
			break;
		}
	}
}

static int
vke_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct vke_softc *sc = ifp->if_softc;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				vke_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vke_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = EOPNOTSUPP;
		/* TODO */
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static int
vke_stop(struct vke_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	callout_stop(&sc->sc_intr);

	return 0;
}

static void
vke_intr(void *xsc)
{
	struct vke_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto back;

	vke_rxeof(sc);

	ifp->if_start(ifp);
	callout_reset(&sc->sc_intr, 5, vke_intr, sc);

back:
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
vke_rxeof(struct vke_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	for (i = 0; i < VKE_RX_THRESHOLD; ++i) {
		int n;

		if (sc->sc_rx_mbuf != NULL) {
			m = sc->sc_rx_mbuf;
			sc->sc_rx_mbuf = NULL;
		} else {
			m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
			if (m == NULL)
				return ENOBUFS;
		}

		n = read(sc->sc_fd, mtod(m, void *), MCLBYTES);
		if (n < 0) {
			sc->sc_rx_mbuf = m;	/* We can use it next time */

			/* TODO: handle fatal error */
			break;
		}
		ifp->if_ipackets++;
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = n;
		ifp->if_input(ifp, m);
	}
	return 0;
}
