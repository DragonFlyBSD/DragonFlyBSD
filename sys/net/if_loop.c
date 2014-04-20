/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)if_loop.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/if_loop.c,v 1.47.2.9 2004/02/08 08:40:24 silby Exp $
 */

/*
 * Loopback interface driver for protocol testing and timing.
 */
#include "use_loop.h"

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <sys/mplock2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/bpf.h>
#include <net/bpfdesc.h>

#ifdef	INET
#include <netinet/in.h>
#include <netinet/in_var.h>
#endif

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#endif

static void	loopattach(void *);
static int	looutput(struct ifnet *, struct mbuf *, struct sockaddr *,
			 struct rtentry *);
static int	loioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	lortrequest(int, struct rtentry *);
#ifdef ALTQ
static void	lo_altqstart(struct ifnet *, struct ifaltq_subque *);
#endif
PSEUDO_SET(loopattach, if_loop);

#ifdef TINY_LOMTU
#define	LOMTU	(1024+512)
#elif defined(LARGE_LOMTU)
#define LOMTU	131072
#else
#define LOMTU	16384
#endif

#define LO_CSUM_FEATURES	(CSUM_IP | CSUM_UDP | CSUM_TCP)

struct	ifnet loif[NLOOP];

/* ARGSUSED */
static void
loopattach(void *dummy)
{
	struct ifnet *ifp;
	int i;

	for (i = 0, ifp = loif; i < NLOOP; i++, ifp++) {
		if_initname(ifp, "lo", i);
		ifp->if_mtu = LOMTU;
		ifp->if_flags = IFF_LOOPBACK | IFF_MULTICAST;
		ifp->if_capabilities = IFCAP_HWCSUM;
		ifp->if_hwassist = LO_CSUM_FEATURES;
		ifp->if_capenable = ifp->if_capabilities;
		ifp->if_ioctl = loioctl;
		ifp->if_output = looutput;
		ifp->if_type = IFT_LOOP;
		ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
		ifq_set_ready(&ifp->if_snd);
#ifdef ALTQ
	        ifp->if_start = lo_altqstart;
#endif
		if_attach(ifp, NULL);
		bpfattach(ifp, DLT_NULL, sizeof(u_int));
	}
}

static int
looutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	 struct rtentry *rt)
{
	M_ASSERTPKTHDR(m);

	if (rt && rt->rt_flags & (RTF_REJECT|RTF_BLACKHOLE)) {
		m_freem(m);
		return (rt->rt_flags & RTF_BLACKHOLE ? 0 :
		        rt->rt_flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
	}

	IFNET_STAT_INC(ifp, opackets, 1);
	IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len);
#if 1	/* XXX */
	switch (dst->sa_family) {
	case AF_INET:
	case AF_INET6:
		break;
	default:
		kprintf("looutput: af=%d unexpected\n", dst->sa_family);
		m_freem(m);
		return (EAFNOSUPPORT);
	}
#endif

	if (ifp->if_capenable & IFCAP_RXCSUM) {
		int csum_flags = 0;

		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= (CSUM_IP_CHECKED | CSUM_IP_VALID);
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
			csum_flags |= (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);

		m->m_pkthdr.csum_flags |= csum_flags;
		if (csum_flags & CSUM_DATA_VALID)
			m->m_pkthdr.csum_data = 0xffff;
	}
	return (if_simloop(ifp, m, dst->sa_family, 0));
}

/*
 * if_simloop()
 *
 * This function is to support software emulation of hardware loopback,
 * i.e., for interfaces with the IFF_SIMPLEX attribute. Since they can't
 * hear their own broadcasts, we create a copy of the packet that we
 * would normally receive via a hardware loopback.
 *
 * This function expects the packet to include the media header of length hlen.
 */
int
if_simloop(struct ifnet *ifp, struct mbuf *m, int af, int hlen)
{
	int isr;

	KASSERT((m->m_flags & M_PKTHDR) != 0, ("if_simloop: no HDR"));
	m->m_pkthdr.rcvif = ifp;

	/* BPF write needs to be handled specially */
	if (af == AF_UNSPEC) {
		KASSERT(m->m_len >= sizeof(int), ("if_simloop: m_len"));
		af = *(mtod(m, int *));
		m->m_len -= sizeof(int);
		m->m_pkthdr.len -= sizeof(int);
		m->m_data += sizeof(int);
	}

	if (ifp->if_bpf) {
		bpf_gettoken();

		/* Re-check */
		if (ifp->if_bpf == NULL)
			goto rel;

		if (ifp->if_bpf->bif_dlt == DLT_NULL) {
			uint32_t bpf_af = (uint32_t)af;
			bpf_ptap(ifp->if_bpf, m, &bpf_af, 4);
		} else {
			bpf_mtap(ifp->if_bpf, m);
		}
rel:
		bpf_reltoken();
	}

	/* Strip away media header */
	if (hlen > 0)
		m_adj(m, hlen);
 
#ifdef ALTQ
	/*
	 * altq for loop is just for debugging.
	 * only used when called for loop interface (not for
	 * a simplex interface).
	 */
	if (ifq_is_enabled(&ifp->if_snd) && ifp->if_start == lo_altqstart) {
		struct altq_pktattr pktattr;
		int32_t *afp;

		/*
		 * if the queueing discipline needs packet classification,
		 * do it before prepending link headers.
		 */
		ifq_classify(&ifp->if_snd, m, af, &pktattr);

		M_PREPEND(m, sizeof(int32_t), MB_DONTWAIT);
		if (m == NULL)
			return(ENOBUFS);
		afp = mtod(m, int32_t *);
		*afp = (int32_t)af;

		return ifq_dispatch(ifp, m, &pktattr);
	}
#endif /* ALTQ */

	/* Deliver to upper layer protocol */
	switch (af) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		m->m_flags |= M_LOOP;
		isr = NETISR_IPV6;
		break;
#endif
	default:
		kprintf("if_simloop: can't handle af=%d\n", af);
		m_freem(m);
		return (EAFNOSUPPORT);
	}

	IFNET_STAT_INC(ifp, ipackets, 1);
	IFNET_STAT_INC(ifp, ibytes, m->m_pkthdr.len);
	netisr_queue(isr, m);
	return (0);
}

#ifdef ALTQ
static void
lo_altqstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct mbuf *m;
	int32_t af, *afp;
	int isr;
	
	while (1) {
		crit_enter();
		m = ifsq_dequeue(ifsq);
		crit_exit();
		if (m == NULL)
			return;

		afp = mtod(m, int32_t *);
		af = *afp;
		m_adj(m, sizeof(int32_t));

		switch (af) {
#ifdef INET
		case AF_INET:
			isr = NETISR_IP;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			m->m_flags |= M_LOOP;
			isr = NETISR_IPV6;
			break;
#endif
#ifdef ISO
		case AF_ISO:
			isr = NETISR_ISO;
			break;
#endif
		default:
			kprintf("lo_altqstart: can't handle af%d\n", af);
			m_freem(m);
			return;
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		IFNET_STAT_INC(ifp, ibytes, m->m_pkthdr.len);
		netisr_queue(isr, m);
	}
}
#endif /* ALTQ */

/* ARGSUSED */
static void
lortrequest(int cmd, struct rtentry *rt)
{
	if (rt) {
		rt->rt_rmx.rmx_mtu = rt->rt_ifp->if_mtu; /* for ISO */
		/*
		 * For optimal performance, the send and receive buffers
		 * should be at least twice the MTU plus a little more for
		 * overhead.
		 */
		rt->rt_rmx.rmx_recvpipe = rt->rt_rmx.rmx_sendpipe = 3 * LOMTU;
	}
}

/*
 * Process an ioctl request.
 */
/* ARGSUSED */
static int
loioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifaddr *ifa;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, mask;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP | IFF_RUNNING;
		ifa = (struct ifaddr *)data;
		ifa->ifa_rtrequest = lortrequest;
		/*
		 * Everything else is done at a higher level.
		 */
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifr == NULL) {
			error = EAFNOSUPPORT;		/* XXX */
			break;
		}
		switch (ifr->ifr_addr.sa_family) {

#ifdef INET
		case AF_INET:
			break;
#endif
#ifdef INET6
		case AF_INET6:
			break;
#endif

		default:
			error = EAFNOSUPPORT;
			break;
		}
		break;

	case SIOCSIFMTU:
		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSIFFLAGS:
		break;

	case SIOCSIFCAP:
		mask = (ifr->ifr_reqcap ^ ifp->if_capenable) & IFCAP_HWCSUM;
		if (mask) {
			ifp->if_capenable ^= mask;
			if (IFCAP_TXCSUM & ifp->if_capenable)
				ifp->if_hwassist = LO_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
		break;

	default:
		error = EINVAL;
	}
	return (error);
}
