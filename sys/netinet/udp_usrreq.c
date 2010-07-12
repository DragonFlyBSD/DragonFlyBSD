/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
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
 *	@(#)udp_usrreq.c	8.6 (Berkeley) 5/23/95
 * $FreeBSD: src/sys/netinet/udp_usrreq.c,v 1.64.2.18 2003/01/24 05:11:34 sam Exp $
 * $DragonFly: src/sys/netinet/udp_usrreq.c,v 1.47 2008/11/11 10:46:58 sephe Exp $
 */

#include "opt_ipsec.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/thread2.h>
#include <sys/in_cksum.h>

#include <machine/stdarg.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet6/ip6_var.h>
#endif
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#endif

#ifdef IPSEC
#include <netinet6/ipsec.h>
#endif

int	udp_mpsafe_proto = 0;
TUNABLE_INT("net.inet.udp.mpsafe_proto", &udp_mpsafe_proto);

int	udp_mpsafe_thread = NETMSG_SERVICE_ADAPTIVE;
TUNABLE_INT("net.inet.udp.mpsafe_thread", &udp_mpsafe_thread);
SYSCTL_INT(_net_inet_udp, OID_AUTO, mpsafe_thread, CTLFLAG_RW,
	   &udp_mpsafe_thread, 0,
	   "0:BGL, 1:Adaptive BGL, 2:No BGL(experimental)");

/*
 * UDP protocol implementation.
 * Per RFC 768, August, 1980.
 */
#ifndef	COMPAT_42
static int	udpcksum = 1;
#else
static int	udpcksum = 0;		/* XXX */
#endif
SYSCTL_INT(_net_inet_udp, UDPCTL_CHECKSUM, checksum, CTLFLAG_RW,
		&udpcksum, 0, "");

int	log_in_vain = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, log_in_vain, CTLFLAG_RW,
    &log_in_vain, 0, "Log all incoming UDP packets");

static int	blackhole = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, blackhole, CTLFLAG_RW,
	&blackhole, 0, "Do not send port unreachables for refused connects");

static int	strict_mcast_mship = 1;
SYSCTL_INT(_net_inet_udp, OID_AUTO, strict_mcast_mship, CTLFLAG_RW,
	&strict_mcast_mship, 0, "Only send multicast to member sockets");

struct	inpcbinfo udbinfo;

#ifndef UDBHASHSIZE
#define UDBHASHSIZE 16
#endif

struct	udpstat udpstat;	/* from udp_var.h */
SYSCTL_STRUCT(_net_inet_udp, UDPCTL_STATS, stats, CTLFLAG_RW,
    &udpstat, udpstat, "UDP statistics (struct udpstat, netinet/udp_var.h)");

static struct	sockaddr_in udp_in = { sizeof udp_in, AF_INET };
#ifdef INET6
struct udp_in6 {
	struct sockaddr_in6	uin6_sin;
	u_char			uin6_init_done : 1;
} udp_in6 = {
	{ sizeof udp_in6.uin6_sin, AF_INET6 },
	0
};
struct udp_ip6 {
	struct ip6_hdr		uip6_ip6;
	u_char			uip6_init_done : 1;
} udp_ip6;
#endif /* INET6 */

static void udp_append (struct inpcb *last, struct ip *ip,
			    struct mbuf *n, int off);
#ifdef INET6
static void ip_2_ip6_hdr (struct ip6_hdr *ip6, struct ip *ip);
#endif

static int udp_connect_oncpu(struct socket *so, struct thread *td,
			struct sockaddr_in *sin, struct sockaddr_in *if_sin);
static int udp_detach (struct socket *so);
static	int udp_output (struct inpcb *, struct mbuf *, struct sockaddr *,
			    struct mbuf *, struct thread *);

void
udp_init(void)
{
	in_pcbinfo_init(&udbinfo);
	udbinfo.hashbase = hashinit(UDBHASHSIZE, M_PCB, &udbinfo.hashmask);
	udbinfo.porthashbase = hashinit(UDBHASHSIZE, M_PCB,
					&udbinfo.porthashmask);
	udbinfo.wildcardhashbase = hashinit(UDBHASHSIZE, M_PCB,
					    &udbinfo.wildcardhashmask);
	udbinfo.ipi_size = sizeof(struct inpcb);
	udp_thread_init();
}

/*
 * Check multicast packets to make sure they are only sent to sockets with
 * multicast memberships for the packet's destination address and arrival
 * interface.  Multicast packets to multicast-unaware sockets are also
 * disallowed.
 *
 * Returns 0 if the packet is acceptable, -1 if it is not.
 */
static __inline int
check_multicast_membership(struct ip *ip, struct inpcb *inp, struct mbuf *m)
{
	int mshipno;
	struct ip_moptions *mopt;

	if (strict_mcast_mship == 0 ||
	    !IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		return (0);
	}
	mopt = inp->inp_moptions;
	if (mopt == NULL)
		return (-1);
	for (mshipno = 0; mshipno <= mopt->imo_num_memberships; ++mshipno) {
		struct in_multi *maddr = mopt->imo_membership[mshipno];

		if (ip->ip_dst.s_addr == maddr->inm_addr.s_addr &&
		    m->m_pkthdr.rcvif == maddr->inm_ifp) {
			return (0);
		}
	}
	return (-1);
}

void
udp_input(struct mbuf *m, ...)
{
	int iphlen;
	struct ip *ip;
	struct udphdr *uh;
	struct inpcb *inp;
	struct mbuf *opts = NULL;
	int len, off, proto;
	struct ip save_ip;
	struct sockaddr *append_sa;
	__va_list ap;

	__va_start(ap, m);
	off = __va_arg(ap, int);
	proto = __va_arg(ap, int);
	__va_end(ap);

	iphlen = off;
	udpstat.udps_ipackets++;

	/*
	 * Strip IP options, if any; should skip this,
	 * make available to user, and use on returned packets,
	 * but we don't yet have a way to check the checksum
	 * with options still present.
	 */
	if (iphlen > sizeof(struct ip)) {
		ip_stripoptions(m);
		iphlen = sizeof(struct ip);
	}

	/*
	 * IP and UDP headers are together in first mbuf.
	 * Already checked and pulled up in ip_demux().
	 */
	KASSERT(m->m_len >= iphlen + sizeof(struct udphdr),
	    ("UDP header not in one mbuf"));

	ip = mtod(m, struct ip *);
	uh = (struct udphdr *)((caddr_t)ip + iphlen);

	/* destination port of 0 is illegal, based on RFC768. */
	if (uh->uh_dport == 0)
		goto bad;

	/*
	 * Make mbuf data length reflect UDP length.
	 * If not enough data to reflect UDP length, drop.
	 */
	len = ntohs((u_short)uh->uh_ulen);
	if (ip->ip_len != len) {
		if (len > ip->ip_len || len < sizeof(struct udphdr)) {
			udpstat.udps_badlen++;
			goto bad;
		}
		m_adj(m, len - ip->ip_len);
		/* ip->ip_len = len; */
	}
	/*
	 * Save a copy of the IP header in case we want restore it
	 * for sending an ICMP error message in response.
	 */
	save_ip = *ip;

	/*
	 * Checksum extended UDP header and data.
	 */
	if (uh->uh_sum) {
		if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR)
				uh->uh_sum = m->m_pkthdr.csum_data;
			else
				uh->uh_sum = in_pseudo(ip->ip_src.s_addr,
				    ip->ip_dst.s_addr, htonl((u_short)len +
				    m->m_pkthdr.csum_data + IPPROTO_UDP));
			uh->uh_sum ^= 0xffff;
		} else {
			char b[9];

			bcopy(((struct ipovly *)ip)->ih_x1, b, 9);
			bzero(((struct ipovly *)ip)->ih_x1, 9);
			((struct ipovly *)ip)->ih_len = uh->uh_ulen;
			uh->uh_sum = in_cksum(m, len + sizeof(struct ip));
			bcopy(b, ((struct ipovly *)ip)->ih_x1, 9);
		}
		if (uh->uh_sum) {
			udpstat.udps_badsum++;
			m_freem(m);
			return;
		}
	} else
		udpstat.udps_nosum++;

	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
	    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif)) {
		struct inpcb *last;

		/*
		 * Deliver a multicast or broadcast datagram to *all* sockets
		 * for which the local and remote addresses and ports match
		 * those of the incoming datagram.  This allows more than
		 * one process to receive multi/broadcasts on the same port.
		 * (This really ought to be done for unicast datagrams as
		 * well, but that would cause problems with existing
		 * applications that open both address-specific sockets and
		 * a wildcard socket listening to the same port -- they would
		 * end up receiving duplicates of every unicast datagram.
		 * Those applications open the multiple sockets to overcome an
		 * inadequacy of the UDP socket interface, but for backwards
		 * compatibility we avoid the problem here rather than
		 * fixing the interface.  Maybe 4.5BSD will remedy this?)
		 */

		/*
		 * Construct sockaddr format source address.
		 */
		udp_in.sin_port = uh->uh_sport;
		udp_in.sin_addr = ip->ip_src;
		/*
		 * Locate pcb(s) for datagram.
		 * (Algorithm copied from raw_intr().)
		 */
		last = NULL;
#ifdef INET6
		udp_in6.uin6_init_done = udp_ip6.uip6_init_done = 0;
#endif
		LIST_FOREACH(inp, &udbinfo.pcblisthead, inp_list) {
			if (inp->inp_flags & INP_PLACEMARKER)
				continue;
#ifdef INET6
			if (!(inp->inp_vflag & INP_IPV4))
				continue;
#endif
			if (inp->inp_lport != uh->uh_dport)
				continue;
			if (inp->inp_laddr.s_addr != INADDR_ANY) {
				if (inp->inp_laddr.s_addr !=
				    ip->ip_dst.s_addr)
					continue;
			}
			if (inp->inp_faddr.s_addr != INADDR_ANY) {
				if (inp->inp_faddr.s_addr !=
				    ip->ip_src.s_addr ||
				    inp->inp_fport != uh->uh_sport)
					continue;
			}

			if (check_multicast_membership(ip, inp, m) < 0)
				continue;

			if (last != NULL) {
				struct mbuf *n;

#ifdef IPSEC
				/* check AH/ESP integrity. */
				if (ipsec4_in_reject_so(m, last->inp_socket))
					ipsecstat.in_polvio++;
					/* do not inject data to pcb */
				else
#endif /*IPSEC*/
#ifdef FAST_IPSEC
				/* check AH/ESP integrity. */
				if (ipsec4_in_reject(m, last))
					;
				else
#endif /*FAST_IPSEC*/
				if ((n = m_copypacket(m, MB_DONTWAIT)) != NULL)
					udp_append(last, ip, n,
						   iphlen +
						   sizeof(struct udphdr));
			}
			last = inp;
			/*
			 * Don't look for additional matches if this one does
			 * not have either the SO_REUSEPORT or SO_REUSEADDR
			 * socket options set.  This heuristic avoids searching
			 * through all pcbs in the common case of a non-shared
			 * port.  It * assumes that an application will never
			 * clear these options after setting them.
			 */
			if (!(last->inp_socket->so_options &
			    (SO_REUSEPORT | SO_REUSEADDR)))
				break;
		}

		if (last == NULL) {
			/*
			 * No matching pcb found; discard datagram.
			 * (No need to send an ICMP Port Unreachable
			 * for a broadcast or multicast datgram.)
			 */
			udpstat.udps_noportbcast++;
			goto bad;
		}
#ifdef IPSEC
		/* check AH/ESP integrity. */
		if (ipsec4_in_reject_so(m, last->inp_socket)) {
			ipsecstat.in_polvio++;
			goto bad;
		}
#endif /*IPSEC*/
#ifdef FAST_IPSEC
		/* check AH/ESP integrity. */
		if (ipsec4_in_reject(m, last))
			goto bad;
#endif /*FAST_IPSEC*/
		udp_append(last, ip, m, iphlen + sizeof(struct udphdr));
		return;
	}
	/*
	 * Locate pcb for datagram.
	 */
	inp = in_pcblookup_hash(&udbinfo, ip->ip_src, uh->uh_sport,
	    ip->ip_dst, uh->uh_dport, 1, m->m_pkthdr.rcvif);
	if (inp == NULL) {
		if (log_in_vain) {
			char buf[sizeof "aaa.bbb.ccc.ddd"];

			strcpy(buf, inet_ntoa(ip->ip_dst));
			log(LOG_INFO,
			    "Connection attempt to UDP %s:%d from %s:%d\n",
			    buf, ntohs(uh->uh_dport), inet_ntoa(ip->ip_src),
			    ntohs(uh->uh_sport));
		}
		udpstat.udps_noport++;
		if (m->m_flags & (M_BCAST | M_MCAST)) {
			udpstat.udps_noportbcast++;
			goto bad;
		}
		if (blackhole)
			goto bad;
#ifdef ICMP_BANDLIM
		if (badport_bandlim(BANDLIM_ICMP_UNREACH) < 0)
			goto bad;
#endif
		*ip = save_ip;
		ip->ip_len += iphlen;
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_PORT, 0, 0);
		return;
	}
#ifdef IPSEC
	if (ipsec4_in_reject_so(m, inp->inp_socket)) {
		ipsecstat.in_polvio++;
		goto bad;
	}
#endif /*IPSEC*/
#ifdef FAST_IPSEC
	if (ipsec4_in_reject(m, inp))
		goto bad;
#endif /*FAST_IPSEC*/
	/*
	 * Check the minimum TTL for socket.
	 */
	if (ip->ip_ttl < inp->inp_ip_minttl)
		goto bad;

	/*
	 * Construct sockaddr format source address.
	 * Stuff source address and datagram in user buffer.
	 */
	udp_in.sin_port = uh->uh_sport;
	udp_in.sin_addr = ip->ip_src;
	if ((inp->inp_flags & INP_CONTROLOPTS) ||
	    (inp->inp_socket->so_options & SO_TIMESTAMP)) {
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6) {
			int savedflags;

			ip_2_ip6_hdr(&udp_ip6.uip6_ip6, ip);
			savedflags = inp->inp_flags;
			inp->inp_flags &= ~INP_UNMAPPABLEOPTS;
			ip6_savecontrol(inp, &opts, &udp_ip6.uip6_ip6, m);
			inp->inp_flags = savedflags;
		} else
#endif
		ip_savecontrol(inp, &opts, ip, m);
	}
	m_adj(m, iphlen + sizeof(struct udphdr));
#ifdef INET6
	if (inp->inp_vflag & INP_IPV6) {
		in6_sin_2_v4mapsin6(&udp_in, &udp_in6.uin6_sin);
		append_sa = (struct sockaddr *)&udp_in6;
	} else
#endif
		append_sa = (struct sockaddr *)&udp_in;
	if (ssb_appendaddr(&inp->inp_socket->so_rcv, append_sa, m, opts) == 0) {
		udpstat.udps_fullsock++;
		goto bad;
	}
	sorwakeup(inp->inp_socket);
	return;
bad:
	m_freem(m);
	if (opts)
		m_freem(opts);
	return;
}

#ifdef INET6
static void
ip_2_ip6_hdr(struct ip6_hdr *ip6, struct ip *ip)
{
	bzero(ip6, sizeof *ip6);

	ip6->ip6_vfc = IPV6_VERSION;
	ip6->ip6_plen = ip->ip_len;
	ip6->ip6_nxt = ip->ip_p;
	ip6->ip6_hlim = ip->ip_ttl;
	ip6->ip6_src.s6_addr32[2] = ip6->ip6_dst.s6_addr32[2] =
		IPV6_ADDR_INT32_SMP;
	ip6->ip6_src.s6_addr32[3] = ip->ip_src.s_addr;
	ip6->ip6_dst.s6_addr32[3] = ip->ip_dst.s_addr;
}
#endif

/*
 * subroutine of udp_input(), mainly for source code readability.
 * caller must properly init udp_ip6 and udp_in6 beforehand.
 */
static void
udp_append(struct inpcb *last, struct ip *ip, struct mbuf *n, int off)
{
	struct sockaddr *append_sa;
	struct mbuf *opts = NULL;

	if (last->inp_flags & INP_CONTROLOPTS ||
	    last->inp_socket->so_options & SO_TIMESTAMP) {
#ifdef INET6
		if (last->inp_vflag & INP_IPV6) {
			int savedflags;

			if (udp_ip6.uip6_init_done == 0) {
				ip_2_ip6_hdr(&udp_ip6.uip6_ip6, ip);
				udp_ip6.uip6_init_done = 1;
			}
			savedflags = last->inp_flags;
			last->inp_flags &= ~INP_UNMAPPABLEOPTS;
			ip6_savecontrol(last, &opts, &udp_ip6.uip6_ip6, n);
			last->inp_flags = savedflags;
		} else
#endif
		ip_savecontrol(last, &opts, ip, n);
	}
#ifdef INET6
	if (last->inp_vflag & INP_IPV6) {
		if (udp_in6.uin6_init_done == 0) {
			in6_sin_2_v4mapsin6(&udp_in, &udp_in6.uin6_sin);
			udp_in6.uin6_init_done = 1;
		}
		append_sa = (struct sockaddr *)&udp_in6.uin6_sin;
	} else
#endif
		append_sa = (struct sockaddr *)&udp_in;
	m_adj(n, off);
	if (ssb_appendaddr(&last->inp_socket->so_rcv, append_sa, n, opts) == 0) {
		m_freem(n);
		if (opts)
			m_freem(opts);
		udpstat.udps_fullsock++;
	} else
		sorwakeup(last->inp_socket);
}

/*
 * Notify a udp user of an asynchronous error;
 * just wake up so that he can collect error status.
 */
void
udp_notify(struct inpcb *inp, int error)
{
	inp->inp_socket->so_error = error;
	sorwakeup(inp->inp_socket);
	sowwakeup(inp->inp_socket);
}

struct netmsg_udp_notify {
	struct netmsg	nm_nmsg;
	void		(*nm_notify)(struct inpcb *, int);
	struct in_addr	nm_faddr;
	int		nm_arg;
};

static void
udp_notifyall_oncpu(struct netmsg *netmsg)
{
	struct netmsg_udp_notify *nmsg = (struct netmsg_udp_notify *)netmsg;
	int nextcpu;

	in_pcbnotifyall(&udbinfo.pcblisthead, nmsg->nm_faddr, nmsg->nm_arg,
			nmsg->nm_notify);

	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus2)
		lwkt_forwardmsg(udp_cport(nextcpu), &netmsg->nm_lmsg);
	else
		lwkt_replymsg(&netmsg->nm_lmsg, 0);
}

static void
udp_rtchange(struct inpcb *inp, int err)
{
#ifdef SMP
	/* XXX Nuke this, once UDP inpcbs are CPU localized */
	if (inp->inp_route.ro_rt && inp->inp_route.ro_rt->rt_cpuid == mycpuid) {
		rtfree(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = NULL;
		/*
		 * A new route can be allocated the next time
		 * output is attempted.
		 */
	}
#else
	in_rtchange(inp, err);
#endif
}

void
udp_ctlinput(int cmd, struct sockaddr *sa, void *vip)
{
	struct ip *ip = vip;
	struct udphdr *uh;
	void (*notify) (struct inpcb *, int) = udp_notify;
	struct in_addr faddr;
	struct inpcb *inp;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return;

	if (PRC_IS_REDIRECT(cmd)) {
		ip = NULL;
		notify = udp_rtchange;
	} else if (cmd == PRC_HOSTDEAD)
		ip = NULL;
	else if ((unsigned)cmd >= PRC_NCMDS || inetctlerrmap[cmd] == 0)
		return;
	if (ip) {
		crit_enter();
		uh = (struct udphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		inp = in_pcblookup_hash(&udbinfo, faddr, uh->uh_dport,
					ip->ip_src, uh->uh_sport, 0, NULL);
		if (inp != NULL && inp->inp_socket != NULL)
			(*notify)(inp, inetctlerrmap[cmd]);
		crit_exit();
	} else if (PRC_IS_REDIRECT(cmd)) {
		struct netmsg_udp_notify nmsg;

		KKASSERT(&curthread->td_msgport == cpu_portfn(0));
		netmsg_init(&nmsg.nm_nmsg, NULL, &curthread->td_msgport,
			    0, udp_notifyall_oncpu);
		nmsg.nm_faddr = faddr;
		nmsg.nm_arg = inetctlerrmap[cmd];
		nmsg.nm_notify = notify;

		lwkt_domsg(udp_cport(0), &nmsg.nm_nmsg.nm_lmsg, 0);
	} else {
		/*
		 * XXX We should forward msg upon PRC_HOSTHEAD and ip == NULL,
		 * once UDP inpcbs are CPU localized
		 */
		KKASSERT(&curthread->td_msgport == udp_cport(0));
		in_pcbnotifyall(&udbinfo.pcblisthead, faddr, inetctlerrmap[cmd],
				notify);
	}
}

SYSCTL_PROC(_net_inet_udp, UDPCTL_PCBLIST, pcblist, CTLFLAG_RD, &udbinfo, 0,
	    in_pcblist_global, "S,xinpcb", "List of active UDP sockets");

static int
udp_getcred(SYSCTL_HANDLER_ARGS)
{
	struct sockaddr_in addrs[2];
	struct inpcb *inp;
	int error;

	error = priv_check(req->td, PRIV_ROOT);
	if (error)
		return (error);
	error = SYSCTL_IN(req, addrs, sizeof addrs);
	if (error)
		return (error);
	crit_enter();
	inp = in_pcblookup_hash(&udbinfo, addrs[1].sin_addr, addrs[1].sin_port,
				addrs[0].sin_addr, addrs[0].sin_port, 1, NULL);
	if (inp == NULL || inp->inp_socket == NULL) {
		error = ENOENT;
		goto out;
	}
	error = SYSCTL_OUT(req, inp->inp_socket->so_cred, sizeof(struct ucred));
out:
	crit_exit();
	return (error);
}

SYSCTL_PROC(_net_inet_udp, OID_AUTO, getcred, CTLTYPE_OPAQUE|CTLFLAG_RW,
    0, 0, udp_getcred, "S,ucred", "Get the ucred of a UDP connection");

static int
udp_output(struct inpcb *inp, struct mbuf *m, struct sockaddr *dstaddr,
	   struct mbuf *control, struct thread *td)
{
	struct udpiphdr *ui;
	int len = m->m_pkthdr.len;
	struct sockaddr_in *sin;	/* really is initialized before use */
	int error = 0, lport_any = 0;

	if (len + sizeof(struct udpiphdr) > IP_MAXPACKET) {
		error = EMSGSIZE;
		goto release;
	}

	if (inp->inp_lport == 0) {	/* unbound socket */
		error = in_pcbbind(inp, NULL, td);
		if (error)
			goto release;
		in_pcbinswildcardhash(inp);
		lport_any = 1;
	}

	if (dstaddr != NULL) {		/* destination address specified */
		if (inp->inp_faddr.s_addr != INADDR_ANY) {
			/* already connected */
			error = EISCONN;
			goto release;
		}
		sin = (struct sockaddr_in *)dstaddr;
		if (!prison_remote_ip(td, (struct sockaddr *)&sin)) {
			error = EAFNOSUPPORT; /* IPv6 only jail */
			goto release;
		}
	} else {
		if (inp->inp_faddr.s_addr == INADDR_ANY) {
			/* no destination specified and not already connected */
			error = ENOTCONN;
			goto release;
		}
		sin = NULL;
	}

	/*
	 * Calculate data length and get a mbuf
	 * for UDP and IP headers.
	 */
	M_PREPEND(m, sizeof(struct udpiphdr), MB_DONTWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto release;
	}

	/*
	 * Fill in mbuf with extended UDP header
	 * and addresses and length put into network format.
	 */
	ui = mtod(m, struct udpiphdr *);
	bzero(ui->ui_x1, sizeof ui->ui_x1);	/* XXX still needed? */
	ui->ui_pr = IPPROTO_UDP;

	/*
	 * Set destination address.
	 */
	if (dstaddr != NULL) {			/* use specified destination */
		ui->ui_dst = sin->sin_addr;
		ui->ui_dport = sin->sin_port;
	} else {				/* use connected destination */
		ui->ui_dst = inp->inp_faddr;
		ui->ui_dport = inp->inp_fport;
	}

	/*
	 * Set source address.
	 */
	if (inp->inp_laddr.s_addr == INADDR_ANY) {
		struct sockaddr_in *if_sin;

		if (dstaddr == NULL) {	
			/*
			 * connect() had (or should have) failed because
			 * the interface had no IP address, but the
			 * application proceeded to call send() anyways.
			 */
			error = ENOTCONN;
			goto release;
		}

		/* Look up outgoing interface. */
		if ((error = in_pcbladdr(inp, dstaddr, &if_sin, td)))
			goto release;
		ui->ui_src = if_sin->sin_addr;	/* use address of interface */
	} else {
		ui->ui_src = inp->inp_laddr;	/* use non-null bound address */
	}
	ui->ui_sport = inp->inp_lport;
	KASSERT(inp->inp_lport != 0, ("inp lport should have been bound"));

	ui->ui_ulen = htons((u_short)len + sizeof(struct udphdr));

	/*
	 * Set up checksum and output datagram.
	 */
	if (udpcksum) {
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr, ui->ui_dst.s_addr,
		    htons((u_short)len + sizeof(struct udphdr) + IPPROTO_UDP));
		m->m_pkthdr.csum_flags = CSUM_UDP;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
	} else {
		ui->ui_sum = 0;
	}
	((struct ip *)ui)->ip_len = sizeof(struct udpiphdr) + len;
	((struct ip *)ui)->ip_ttl = inp->inp_ip_ttl;	/* XXX */
	((struct ip *)ui)->ip_tos = inp->inp_ip_tos;	/* XXX */
	udpstat.udps_opackets++;

	error = ip_output(m, inp->inp_options, &inp->inp_route,
	    (inp->inp_socket->so_options & (SO_DONTROUTE | SO_BROADCAST)) |
	    IP_DEBUGROUTE,
	    inp->inp_moptions, inp);

	/*
	 * If this is the first data gram sent on an unbound and unconnected
	 * UDP socket, lport will be changed in this function.  If target
	 * CPU after this lport changing is no longer the current CPU, then
	 * free the route entry allocated on the current CPU.
	 */
	if (lport_any) {
		if (udp_addrcpu(inp->inp_faddr.s_addr, inp->inp_fport,
		    inp->inp_laddr.s_addr, inp->inp_lport) != mycpuid) {
			struct route *ro = &inp->inp_route;

			if (ro->ro_rt != NULL)
				RTFREE(ro->ro_rt);
			bzero(ro, sizeof(*ro));
		}
	}
	return (error);

release:
	m_freem(m);
	return (error);
}

u_long	udp_sendspace = 9216;		/* really max datagram size */
					/* 40 1K datagrams */
SYSCTL_INT(_net_inet_udp, UDPCTL_MAXDGRAM, maxdgram, CTLFLAG_RW,
    &udp_sendspace, 0, "Maximum outgoing UDP datagram size");

u_long	udp_recvspace = 40 * (1024 +
#ifdef INET6
				      sizeof(struct sockaddr_in6)
#else
				      sizeof(struct sockaddr_in)
#endif
				      );
SYSCTL_INT(_net_inet_udp, UDPCTL_RECVSPACE, recvspace, CTLFLAG_RW,
    &udp_recvspace, 0, "Maximum incoming UDP datagram size");

static int
udp_abort(struct socket *so)
{
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;	/* ??? possible? panic instead? */
	soisdisconnected(so);
	crit_enter();
	in_pcbdetach(inp);
	crit_exit();
	return 0;
}

static int
udp_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp != NULL)
		return EINVAL;

	error = soreserve(so, udp_sendspace, udp_recvspace, ai->sb_rlimit);
	if (error)
		return error;
	crit_enter();
	error = in_pcballoc(so, &udbinfo);
	crit_exit();
	if (error)
		return error;
	so->so_port = udp_soport_attach(so);

	inp = (struct inpcb *)so->so_pcb;
	inp->inp_vflag |= INP_IPV4;
	inp->inp_ip_ttl = ip_defttl;
	return 0;
}

static int
udp_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;
	crit_enter();
	error = in_pcbbind(inp, nam, td);
	crit_exit();
	if (error == 0) {
		if (sin->sin_addr.s_addr != INADDR_ANY)
			inp->inp_flags |= INP_WASBOUND_NOTANY;
		in_pcbinswildcardhash(inp);
	}
	return error;
}

#ifdef SMP

struct netmsg_udp_connect {
	struct netmsg		nm_netmsg;
	struct socket		*nm_so;
	struct sockaddr_in	*nm_sin;
	struct sockaddr_in	*nm_ifsin;
	struct thread		*nm_td;
};

static void
udp_connect_handler(netmsg_t netmsg)
{
	struct netmsg_udp_connect *msg = (void *)netmsg;
	int error;

	error = udp_connect_oncpu(msg->nm_so, msg->nm_td,
				  msg->nm_sin, msg->nm_ifsin);
	lwkt_replymsg(&msg->nm_netmsg.nm_lmsg, error);
}

#endif

static int
udp_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct sockaddr_in *if_sin;
	lwkt_port_t port;
	int error;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;
	if (inp->inp_faddr.s_addr != INADDR_ANY)
		return EISCONN;
	error = 0;

	/*
	 * Bind if we have to
	 */
	if (td->td_proc && td->td_proc->p_ucred->cr_prison != NULL &&
	    inp->inp_laddr.s_addr == INADDR_ANY) {
		error = in_pcbbind(inp, NULL, td);
		if (error)
			return (error);
	}

	/*
	 * Calculate the correct protocol processing thread.  The connect
	 * operation must run there.
	 */
	error = in_pcbladdr(inp, nam, &if_sin, td);
	if (error)
		return(error);
	if (!prison_remote_ip(td, nam))
		return(EAFNOSUPPORT); /* IPv6 only jail */

	port = udp_addrport(sin->sin_addr.s_addr, sin->sin_port,
			    inp->inp_laddr.s_addr, inp->inp_lport);
#ifdef SMP
	if (port != &curthread->td_msgport) {
		struct netmsg_udp_connect msg;
		struct route *ro = &inp->inp_route;

		/*
		 * in_pcbladdr() may have allocated a route entry for us
		 * on the current CPU, but we need a route entry on the
		 * inpcb's owner CPU, so free it here.
		 */
		if (ro->ro_rt != NULL)
			RTFREE(ro->ro_rt);
		bzero(ro, sizeof(*ro));

		/*
		 * NOTE: We haven't set so->so_port yet do not pass so
		 *       to netmsg_init() or it will be improperly forwarded.
		 */
		netmsg_init(&msg.nm_netmsg, NULL, &curthread->td_msgport,
			    0, udp_connect_handler);
		msg.nm_so = so;
		msg.nm_sin = sin;
		msg.nm_ifsin = if_sin;
		msg.nm_td = td;
		error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	} else {
		error = udp_connect_oncpu(so, td, sin, if_sin);
	}
#else
	error = udp_connect_oncpu(so, td, sin, if_sin);
#endif
	return (error);
}

static int
udp_connect_oncpu(struct socket *so, struct thread *td,
		  struct sockaddr_in *sin, struct sockaddr_in *if_sin)
{
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp->inp_flags & INP_WILDCARD)
		in_pcbremwildcardhash(inp);
	error = in_pcbconnect(inp, (struct sockaddr *)sin, td);

	if (error == 0) {
		/*
		 * No more errors can occur, finish adjusting the socket
		 * and change the processing port to reflect the connected
		 * socket.  Once set we can no longer safely mess with the
		 * socket.
		 */
		soisconnected(so);
		sosetport(so, &curthread->td_msgport);
	} else if (error == EAFNOSUPPORT) {	/* connection dissolved */
		/*
		 * Follow traditional BSD behavior and retain
		 * the local port binding.  But, fix the old misbehavior
		 * of overwriting any previously bound local address.
		 */
		if (!(inp->inp_flags & INP_WASBOUND_NOTANY))
			inp->inp_laddr.s_addr = INADDR_ANY;
		in_pcbinswildcardhash(inp);
	}
	return error;
}

static int
udp_detach(struct socket *so)
{
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;
	crit_enter();
	in_pcbdetach(inp);
	crit_exit();
	return 0;
}

static int
udp_disconnect(struct socket *so)
{
	struct route *ro;
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;
	if (inp->inp_faddr.s_addr == INADDR_ANY)
		return ENOTCONN;

	crit_enter();
	in_pcbdisconnect(inp);
	crit_exit();
	so->so_state &= ~SS_ISCONNECTED;		/* XXX */

	ro = &inp->inp_route;
	if (ro->ro_rt != NULL)
		RTFREE(ro->ro_rt);
	bzero(ro, sizeof(*ro));

	return 0;
}

static int
udp_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
	    struct mbuf *control, struct thread *td)
{
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL) {
		m_freem(m);
		return EINVAL;
	}
	return udp_output(inp, m, addr, control, td);
}

int
udp_shutdown(struct socket *so)
{
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		return EINVAL;
	socantsendmore(so);
	return 0;
}

struct pr_usrreqs udp_usrreqs = {
	.pru_abort = udp_abort,
	.pru_accept = pru_accept_notsupp,
	.pru_attach = udp_attach,
	.pru_bind = udp_bind,
	.pru_connect = udp_connect,
	.pru_connect2 = pru_connect2_notsupp,
	.pru_control = in_control,
	.pru_detach = udp_detach,
	.pru_disconnect = udp_disconnect,
	.pru_listen = pru_listen_notsupp,
	.pru_peeraddr = in_setpeeraddr,
	.pru_rcvd = pru_rcvd_notsupp,
	.pru_rcvoob = pru_rcvoob_notsupp,
	.pru_send = udp_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = udp_shutdown,
	.pru_sockaddr = in_setsockaddr,
	.pru_sosend = sosendudp,
	.pru_soreceive = soreceive
};

