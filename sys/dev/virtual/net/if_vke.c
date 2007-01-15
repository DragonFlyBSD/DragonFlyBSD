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
 * $DragonFly: src/sys/dev/virtual/net/if_vke.c,v 1.4 2007/01/15 05:27:28 dillon Exp $
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/md_var.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>

#include <netinet/in_var.h>

#include <sys/stat.h>
#include <sys/ioccom.h>
#include <net/tap/if_tap.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define VKE_DEVNAME		"vke"

struct vke_softc {
	struct arpcom		arpcom;
	int			sc_fd;
	int			sc_unit;

	struct kqueue_info	*sc_kqueue;

	void			*sc_txbuf;
	struct mbuf		*sc_rx_mbuf;

	struct sysctl_ctx_list	sc_sysctl_ctx;
	struct sysctl_oid	*sc_sysctl_tree;

	int			sc_tap_unit;	/* unit of backend tap(4) */
	in_addr_t		sc_addr;	/* address */
	in_addr_t		sc_mask;	/* netmask */
};

static void	vke_start(struct ifnet *);
static void	vke_init(void *);
static int	vke_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static int	vke_attach(const struct vknetif_info *, int);
static void	vke_intr(void *, struct intrframe *);
static int	vke_stop(struct vke_softc *);
static int	vke_rxeof(struct vke_softc *);
static int	vke_init_addr(struct ifnet *, in_addr_t, in_addr_t);

static void
vke_sysinit(void *arg __unused)
{
	int i, unit;

	KASSERT(NetifNum <= VKNETIF_MAX, ("too many netifs: %d\n", NetifNum));

	unit = 0;
	for (i = 0; i < NetifNum; ++i) {
		if (vke_attach(&NetifInfo[i], unit) == 0)
			++unit;
	}
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

	if (sc->sc_addr != 0) {
		in_addr_t addr, mask;

		addr = sc->sc_addr;
		mask = sc->sc_mask;

		/*
		 * Make sure vkernel assigned
		 * address will not be added
		 * again.
		 */
		sc->sc_addr = 0;
		sc->sc_mask = 0;

		vke_init_addr(ifp, addr, mask);
	}

	ifp->if_start(ifp);
}

static void
vke_start(struct ifnet *ifp)
{
	struct vke_softc *sc = ifp->if_softc;
	struct mbuf *m;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	if (sc->sc_kqueue == NULL) {
		sc->sc_kqueue = kqueue_add(sc->sc_fd, vke_intr, sc);
	}

	while ((m = ifq_dequeue(&ifp->if_snd, NULL)) != NULL) {
		m_copydata(m, 0, m->m_pkthdr.len, sc->sc_txbuf);
		write(sc->sc_fd, sc->sc_txbuf, m->m_pkthdr.len);
		m_freem(m);
		ifp->if_opackets++;
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
	case SIOCGIFSTATUS: {
		struct ifstat *ifs = (struct ifstat *)data;
		int len;

		len = strlen(ifs->ascii);
		if (len < sizeof(ifs->ascii)) {
			ksnprintf(ifs->ascii + len, sizeof(ifs->ascii) - len,
				  "\tBacked by tap%d\n", sc->sc_tap_unit);
		}
		break;
	}
	case SIOCSIFADDR:
		if (((struct ifaddr *)data)->ifa_addr->sa_family == AF_INET) {
			/*
			 * If we are explicitly requested to change address,
			 * we should invalidate address/netmask passed in
			 * from vkernel command line.
			 */
			sc->sc_addr = 0;
			sc->sc_mask = 0;
		}
		/* FALL THROUGH */
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
	if (sc->sc_kqueue) {
		kqueue_del(sc->sc_kqueue);
		sc->sc_kqueue = NULL;
	}

	if (sc->sc_rx_mbuf != NULL) {
		m_freem(sc->sc_rx_mbuf);
		sc->sc_rx_mbuf = NULL;
	}
	return 0;
}

static void
vke_intr(void *xsc, struct intrframe *frame __unused)
{
	struct vke_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto back;

	vke_rxeof(sc);

	ifp->if_start(ifp);

back:
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
vke_rxeof(struct vke_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;

	ASSERT_SERIALIZED(ifp->if_serializer);

	for (;;) {
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

static int
vke_attach(const struct vknetif_info *info, int unit)
{
	struct vke_softc *sc;
	struct ifnet *ifp;
	struct tapinfo tapinfo;
	uint8_t enaddr[ETHER_ADDR_LEN];
	int fd;

	KKASSERT(info->tap_fd >= 0);
	fd = info->tap_fd;

	if (ioctl(fd, TAPGIFINFO, &tapinfo) < 0) {
		kprintf(VKE_DEVNAME "%d: ioctl(TAPGIFINFO) failed: %s\n",
			unit, strerror(errno));
		return ENXIO;
	}

	if (ioctl(fd, SIOCGIFADDR, enaddr) < 0) {
		kprintf(VKE_DEVNAME "%d: ioctl(SIOCGIFADDR) failed: %s\n",
			unit, strerror(errno));
		return ENXIO;
	}
	enaddr[1] += 1;

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_txbuf = kmalloc(MCLBYTES, M_DEVBUF, M_WAITOK);
	sc->sc_fd = fd;
	sc->sc_unit = unit;
	sc->sc_tap_unit = info->tap_unit;
	sc->sc_addr = info->netif_addr;
	sc->sc_mask = info->netif_mask;

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, VKE_DEVNAME, sc->sc_unit);

	/* NB: after if_initname() */
	sysctl_ctx_init(&sc->sc_sysctl_ctx);
	sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
					     SYSCTL_STATIC_CHILDREN(_hw),
					     OID_AUTO, ifp->if_xname,
					     CTLFLAG_RD, 0, "");
	if (sc->sc_sysctl_tree == NULL) {
		kprintf(VKE_DEVNAME "%d: can't add sysctl node\n", unit);
	} else {
		SYSCTL_ADD_INT(&sc->sc_sysctl_ctx,
			       SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			       OID_AUTO, "tap_unit",
			       CTLFLAG_RW, &sc->sc_tap_unit, 0,
			       "Backend tap(4) unit");
	}

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

	if (bootverbose && sc->sc_addr != 0) {
		if_printf(ifp, "pre-configured "
			  "address 0x%08x, netmask 0x%08x\n",
			  ntohl(sc->sc_addr), ntohl(sc->sc_mask));
	}

	return 0;
}

static int
vke_init_addr(struct ifnet *ifp, in_addr_t addr, in_addr_t mask)
{
	struct ifaliasreq ifra;
	struct sockaddr_in *sin;
	int ret;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (bootverbose) {
		if_printf(ifp, "add pre-configured "
			  "address 0x%08x, netmask 0x%08x\n",
			  ntohl(addr), ntohl(mask));
	}

	bzero(&ifra, sizeof(ifra));

	/* NB: no need to set ifaliasreq.ifra_name */

	sin = (struct sockaddr_in *)&ifra.ifra_addr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr.s_addr = addr;

	if (mask != 0) {
		sin = (struct sockaddr_in *)&ifra.ifra_mask;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr.s_addr = mask;
	}

	/*
	 * Temporarily release serializer, in_control() will hold
	 * it again before calling ifnet.if_ioctl().
	 */
	lwkt_serialize_exit(ifp->if_serializer);
	ret = in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);
	lwkt_serialize_enter(ifp->if_serializer);

	return ret;
}

