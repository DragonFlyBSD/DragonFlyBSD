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
 *	@(#)udp_usrreq.c	8.6 (Berkeley) 5/23/95
 * $FreeBSD: src/sys/netinet/udp_usrreq.c,v 1.64.2.18 2003/01/24 05:11:34 sam Exp $
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
#include <sys/in_cksum.h>
#include <sys/ktr.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/serialize.h>

#include <machine/stdarg.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

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

#define MSGF_UDP_SEND		MSGF_PROTO1

#define INP_DIRECT_DETACH	INP_FLAG_PROTO2

#define UDP_KTR_STRING		"inp=%p"
#define UDP_KTR_ARGS		struct inpcb *inp

#ifndef KTR_UDP
#define KTR_UDP			KTR_ALL
#endif

KTR_INFO_MASTER(udp);
KTR_INFO(KTR_UDP, udp, send_beg, 0, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, send_end, 1, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, send_ipout, 2, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, redisp_ipout_beg, 3, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, redisp_ipout_end, 4, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, send_redisp, 5, UDP_KTR_STRING, UDP_KTR_ARGS);
KTR_INFO(KTR_UDP, udp, send_inswildcard, 6, UDP_KTR_STRING, UDP_KTR_ARGS);

#define logudp(name, inp)	KTR_LOG(udp_##name, inp)

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
    &udpcksum, 0, "Enable checksumming of UDP packets");

int	log_in_vain = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, log_in_vain, CTLFLAG_RW,
    &log_in_vain, 0, "Log all incoming UDP packets");

static int	blackhole = 0;
SYSCTL_INT(_net_inet_udp, OID_AUTO, blackhole, CTLFLAG_RW,
	&blackhole, 0, "Do not send port unreachables for refused connects");

static int	strict_mcast_mship = 1;
SYSCTL_INT(_net_inet_udp, OID_AUTO, strict_mcast_mship, CTLFLAG_RW,
	&strict_mcast_mship, 0, "Only send multicast to member sockets");

int	udp_sosend_async = 1;
SYSCTL_INT(_net_inet_udp, OID_AUTO, sosend_async, CTLFLAG_RW,
	&udp_sosend_async, 0, "UDP asynchronized pru_send");

int	udp_sosend_prepend = 1;
SYSCTL_INT(_net_inet_udp, OID_AUTO, sosend_prepend, CTLFLAG_RW,
	&udp_sosend_prepend, 0,
	"Prepend enough space for proto and link header in pru_send");

static int udp_reuseport_ext = 1;
SYSCTL_INT(_net_inet_udp, OID_AUTO, reuseport_ext, CTLFLAG_RW,
	&udp_reuseport_ext, 0, "SO_REUSEPORT extension");

struct	inpcbinfo udbinfo[MAXCPU];

#ifndef UDBHASHSIZE
#define UDBHASHSIZE 16
#endif

struct	udpstat udpstat_percpu[MAXCPU] __cachealign;

static void udp_append(struct inpcb *last, struct ip *ip,
    struct mbuf *n, int off, struct sockaddr_in *udp_in);

static int udp_connect_oncpu(struct inpcb *inp, struct sockaddr_in *sin,
    struct sockaddr_in *if_sin);

static boolean_t udp_inswildcardhash(struct inpcb *inp,
    struct netmsg_base *msg, int error);
static void udp_remwildcardhash(struct inpcb *inp);

void
udp_init(void)
{
	struct inpcbportinfo *portinfo;
	int cpu;

	portinfo = kmalloc_cachealign(sizeof(*portinfo) * ncpus2, M_PCB,
	    M_WAITOK);

	for (cpu = 0; cpu < ncpus2; cpu++) {
		struct inpcbinfo *uicb = &udbinfo[cpu];

		/*
		 * NOTE:
		 * UDP pcb list, wildcard hash table and localgroup hash
		 * table are shared.
		 */
		in_pcbinfo_init(uicb, cpu, TRUE);
		uicb->hashbase = hashinit(UDBHASHSIZE, M_PCB, &uicb->hashmask);

		in_pcbportinfo_init(&portinfo[cpu], UDBHASHSIZE, TRUE, cpu);
		uicb->portinfo = portinfo;
		uicb->portinfo_mask = ncpus2_mask;

		uicb->wildcardhashbase = hashinit(UDBHASHSIZE, M_PCB,
		    &uicb->wildcardhashmask);
		uicb->localgrphashbase = hashinit(UDBHASHSIZE, M_PCB,
		    &uicb->localgrphashmask);

		uicb->ipi_size = sizeof(struct inpcb);
	}

	/*
	 * Initialize UDP statistics counters for each CPU.
	 */
	for (cpu = 0; cpu < ncpus; ++cpu)
		bzero(&udpstat_percpu[cpu], sizeof(struct udpstat));
}

static int
sysctl_udpstat(SYSCTL_HANDLER_ARGS)
{
	int cpu, error = 0;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		if ((error = SYSCTL_OUT(req, &udpstat_percpu[cpu],
					sizeof(struct udpstat))))
			break;
		if ((error = SYSCTL_IN(req, &udpstat_percpu[cpu],
				       sizeof(struct udpstat))))
			break;
	}

	return (error);
}
SYSCTL_PROC(_net_inet_udp, UDPCTL_STATS, stats, (CTLTYPE_OPAQUE | CTLFLAG_RW),
    0, 0, sysctl_udpstat, "S,udpstat", "UDP statistics");

void
udp_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct	inpcb *inp = so->so_pcb;

	if (sopt->sopt_level == IPPROTO_IP) {
		switch (sopt->sopt_name) {
		case IP_MULTICAST_IF:
		case IP_MULTICAST_VIF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
			if (&curthread->td_msgport != netisr_cpuport(0)) {
				/*
				 * This pr_ctloutput msg will be forwarded
				 * to netisr0 to run; we can't do direct
				 * detaching anymore.
				 */
				inp->inp_flags &= ~INP_DIRECT_DETACH;
			}
			break;
		}
	}
	return ip_ctloutput(msg);
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
check_multicast_membership(const struct ip *ip, const struct inpcb *inp,
    const struct mbuf *m)
{
	const struct ip_moptions *mopt;
	int mshipno;

	if (strict_mcast_mship == 0 ||
	    !IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		return (0);
	}

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("multicast input not in netisr0"));

	mopt = inp->inp_moptions;
	if (mopt == NULL)
		return (-1);
	for (mshipno = 0; mshipno < mopt->imo_num_memberships; ++mshipno) {
		const struct in_multi *maddr = mopt->imo_membership[mshipno];

		if (ip->ip_dst.s_addr == maddr->inm_addr.s_addr &&
		    m->m_pkthdr.rcvif == maddr->inm_ifp) {
			return (0);
		}
	}
	return (-1);
}

struct udp_mcast_arg {
	struct inpcb	*inp;
	struct inpcb	*last;
	struct ip	*ip;
	struct mbuf	*m;
	int		iphlen;
	struct sockaddr_in *udp_in;
};

static int
udp_mcast_input(struct udp_mcast_arg *arg)
{
	struct inpcb *inp = arg->inp;
	struct inpcb *last = arg->last;
	struct ip *ip = arg->ip;
	struct mbuf *m = arg->m;

	if (check_multicast_membership(ip, inp, m) < 0)
		return ERESTART; /* caller continue */

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
			    arg->iphlen + sizeof(struct udphdr),
			    arg->udp_in);
	}
	arg->last = last = inp;

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
		return EJUSTRETURN; /* caller stop */
	return 0;
}

int
udp_input(struct mbuf **mp, int *offp, int proto)
{
	struct sockaddr_in udp_in = { sizeof udp_in, AF_INET };
	int iphlen;
	struct ip *ip;
	struct udphdr *uh;
	struct inpcb *inp;
	struct mbuf *m;
	struct mbuf *opts = NULL;
	int len, off;
	struct ip save_ip;
	struct inpcbinfo *pcbinfo = &udbinfo[mycpuid];

	off = *offp;
	m = *mp;
	*mp = NULL;

	iphlen = off;
	udp_stat.udps_ipackets++;

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
			udp_stat.udps_badlen++;
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
			udp_stat.udps_badsum++;
			m_freem(m);
			return(IPPROTO_DONE);
		}
	} else
		udp_stat.udps_nosum++;

	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
	    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif)) {
	    	struct inpcbhead *connhead;
		struct inpcontainer *ic, *ic_marker;
		struct inpcontainerhead *ichead;
		struct udp_mcast_arg arg;
		struct inpcb *last;
		int error;

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
		arg.udp_in = &udp_in;
		/*
		 * Locate pcb(s) for datagram.
		 * (Algorithm copied from raw_intr().)
		 */
		last = NULL;
		arg.iphlen = iphlen;

		connhead = &pcbinfo->hashbase[
		    INP_PCBCONNHASH(ip->ip_src.s_addr, uh->uh_sport,
		    ip->ip_dst.s_addr, uh->uh_dport, pcbinfo->hashmask)];
		LIST_FOREACH(inp, connhead, inp_hash) {
#ifdef INET6
			if (!INP_ISIPV4(inp))
				continue;
#endif
			if (!in_hosteq(inp->inp_faddr, ip->ip_src) ||
			    !in_hosteq(inp->inp_laddr, ip->ip_dst) ||
			    inp->inp_fport != uh->uh_sport ||
			    inp->inp_lport != uh->uh_dport)
				continue;

			arg.inp = inp;
			arg.last = last;
			arg.ip = ip;
			arg.m = m;

			error = udp_mcast_input(&arg);
			if (error == ERESTART)
				continue;
			last = arg.last;

			if (error == EJUSTRETURN)
				goto done;
		}

		ichead = &pcbinfo->wildcardhashbase[
		    INP_PCBWILDCARDHASH(uh->uh_dport,
		    pcbinfo->wildcardhashmask)];
		ic_marker = in_pcbcontainer_marker(mycpuid);

		GET_PCBINFO_TOKEN(pcbinfo);
		LIST_INSERT_HEAD(ichead, ic_marker, ic_list);
		while ((ic = LIST_NEXT(ic_marker, ic_list)) != NULL) {
			LIST_REMOVE(ic_marker, ic_list);
			LIST_INSERT_AFTER(ic, ic_marker, ic_list);

			inp = ic->ic_inp;
			if (inp->inp_flags & INP_PLACEMARKER)
				continue;
#ifdef INET6
			if (!INP_ISIPV4(inp))
				continue;
#endif
			if (inp->inp_lport != uh->uh_dport)
				continue;
			if (inp->inp_laddr.s_addr != INADDR_ANY &&
			    inp->inp_laddr.s_addr != ip->ip_dst.s_addr)
				continue;

			arg.inp = inp;
			arg.last = last;
			arg.ip = ip;
			arg.m = m;

			error = udp_mcast_input(&arg);
			if (error == ERESTART)
				continue;
			last = arg.last;

			if (error == EJUSTRETURN)
				break;
		}
		LIST_REMOVE(ic_marker, ic_list);
		REL_PCBINFO_TOKEN(pcbinfo);
done:
		if (last == NULL) {
			/*
			 * No matching pcb found; discard datagram.
			 * (No need to send an ICMP Port Unreachable
			 * for a broadcast or multicast datgram.)
			 */
			udp_stat.udps_noportbcast++;
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
		udp_append(last, ip, m, iphlen + sizeof(struct udphdr),
		    &udp_in);
		return(IPPROTO_DONE);
	}
	/*
	 * Locate pcb for datagram.
	 */
	inp = in_pcblookup_pkthash(pcbinfo, ip->ip_src, uh->uh_sport,
	    ip->ip_dst, uh->uh_dport, TRUE, m->m_pkthdr.rcvif,
	    udp_reuseport_ext ? m : NULL);
	if (inp == NULL) {
		if (log_in_vain) {
			char buf[sizeof "aaa.bbb.ccc.ddd"];

			strcpy(buf, inet_ntoa(ip->ip_dst));
			log(LOG_INFO,
			    "Connection attempt to UDP %s:%d from %s:%d\n",
			    buf, ntohs(uh->uh_dport), inet_ntoa(ip->ip_src),
			    ntohs(uh->uh_sport));
		}
		udp_stat.udps_noport++;
		if (m->m_flags & (M_BCAST | M_MCAST)) {
			udp_stat.udps_noportbcast++;
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
		return(IPPROTO_DONE);
	}
	KASSERT(INP_ISIPV4(inp), ("not inet inpcb"));
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
	    (inp->inp_socket->so_options & SO_TIMESTAMP))
		ip_savecontrol(inp, &opts, ip, m);
	m_adj(m, iphlen + sizeof(struct udphdr));

	lwkt_gettoken(&inp->inp_socket->so_rcv.ssb_token);
	if (ssb_appendaddr(&inp->inp_socket->so_rcv,
	    (struct sockaddr *)&udp_in, m, opts) == 0) {
		lwkt_reltoken(&inp->inp_socket->so_rcv.ssb_token);
		udp_stat.udps_fullsock++;
		goto bad;
	}
	lwkt_reltoken(&inp->inp_socket->so_rcv.ssb_token);
	sorwakeup(inp->inp_socket);
	return(IPPROTO_DONE);
bad:
	m_freem(m);
	if (opts)
		m_freem(opts);
	return(IPPROTO_DONE);
}

/*
 * subroutine of udp_input(), mainly for source code readability.
 * caller must properly init udp_ip6 and udp_in6 beforehand.
 */
static void
udp_append(struct inpcb *last, struct ip *ip, struct mbuf *n, int off,
    struct sockaddr_in *udp_in)
{
	struct mbuf *opts = NULL;
	int ret;

	KASSERT(INP_ISIPV4(last), ("not inet inpcb"));

	if (last->inp_flags & INP_CONTROLOPTS ||
	    last->inp_socket->so_options & SO_TIMESTAMP)
		ip_savecontrol(last, &opts, ip, n);
	m_adj(n, off);

	lwkt_gettoken(&last->inp_socket->so_rcv.ssb_token);
	ret = ssb_appendaddr(&last->inp_socket->so_rcv,
	    (struct sockaddr *)udp_in, n, opts);
	lwkt_reltoken(&last->inp_socket->so_rcv.ssb_token);
	if (ret == 0) {
		m_freem(n);
		if (opts)
			m_freem(opts);
		udp_stat.udps_fullsock++;
	} else {
		sorwakeup(last->inp_socket);
	}
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
	struct netmsg_base base;
	inp_notify_t	nm_notify;
	struct in_addr	nm_faddr;
	int		nm_arg;
};

static void
udp_notifyall_oncpu(netmsg_t msg)
{
	struct netmsg_udp_notify *nm = (struct netmsg_udp_notify *)msg;
	int nextcpu, cpu = mycpuid;

	in_pcbnotifyall(&udbinfo[cpu], nm->nm_faddr, nm->nm_arg, nm->nm_notify);

	nextcpu = cpu + 1;
	if (nextcpu < ncpus2)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &nm->base.lmsg);
	else
		lwkt_replymsg(&nm->base.lmsg, 0);
}

inp_notify_t
udp_get_inpnotify(int cmd, const struct sockaddr *sa,
    struct ip **ip0, int *cpuid)
{
	struct in_addr faddr;
	struct ip *ip = *ip0;
	inp_notify_t notify = udp_notify;

	faddr = ((const struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return NULL;

	if (PRC_IS_REDIRECT(cmd)) {
		ip = NULL;
		notify = in_rtchange;
	} else if (cmd == PRC_HOSTDEAD) {
		ip = NULL;
	} else if ((unsigned)cmd >= PRC_NCMDS || inetctlerrmap[cmd] == 0) {
		return NULL;
	}

	if (cpuid != NULL) {
		if (ip == NULL) {
			/* Go through all CPUs */
			*cpuid = ncpus;
		} else {
			const struct udphdr *uh;

			uh = (const struct udphdr *)
			    ((caddr_t)ip + (ip->ip_hl << 2));
			*cpuid = udp_addrcpu(faddr.s_addr, uh->uh_dport,
			    ip->ip_src.s_addr, uh->uh_sport);
		}
	}

	*ip0 = ip;
	return notify;
}

void
udp_ctlinput(netmsg_t msg)
{
	struct sockaddr *sa = msg->ctlinput.nm_arg;
	struct ip *ip = msg->ctlinput.nm_extra;
	int cmd = msg->ctlinput.nm_cmd, cpuid;
	inp_notify_t notify;
	struct in_addr faddr;

	notify = udp_get_inpnotify(cmd, sa, &ip, &cpuid);
	if (notify == NULL)
		goto done;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (ip) {
		const struct udphdr *uh;
		struct inpcb *inp;

		if (cpuid != mycpuid)
			goto done;

		uh = (const struct udphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		inp = in_pcblookup_hash(&udbinfo[mycpuid], faddr, uh->uh_dport,
					ip->ip_src, uh->uh_sport, 0, NULL);
		if (inp != NULL && inp->inp_socket != NULL)
			notify(inp, inetctlerrmap[cmd]);
	} else if (msg->ctlinput.nm_direct) {
		if (cpuid != ncpus && cpuid != mycpuid)
			goto done;
		if (mycpuid >= ncpus2)
			goto done;

		in_pcbnotifyall(&udbinfo[mycpuid], faddr, inetctlerrmap[cmd],
		    notify);
	} else {
		struct netmsg_udp_notify *nm;

		KKASSERT(&curthread->td_msgport == netisr_cpuport(0));
		nm = kmalloc(sizeof(*nm), M_LWKTMSG, M_INTWAIT);
		netmsg_init(&nm->base, NULL, &netisr_afree_rport,
			    0, udp_notifyall_oncpu);
		nm->nm_faddr = faddr;
		nm->nm_arg = inetctlerrmap[cmd];
		nm->nm_notify = notify;
		lwkt_sendmsg(netisr_cpuport(0), &nm->base.lmsg);
	}
done:
	lwkt_replymsg(&msg->lmsg, 0);
}

SYSCTL_PROC(_net_inet_udp, UDPCTL_PCBLIST, pcblist, CTLFLAG_RD, udbinfo, 0,
	    in_pcblist_global_ncpus2, "S,xinpcb", "List of active UDP sockets");

static int
udp_getcred(SYSCTL_HANDLER_ARGS)
{
	struct sockaddr_in addrs[2];
	struct ucred cred0, *cred = NULL;
	struct inpcb *inp;
	int error, cpu, origcpu;

	error = priv_check(req->td, PRIV_ROOT);
	if (error)
		return (error);
	error = SYSCTL_IN(req, addrs, sizeof addrs);
	if (error)
		return (error);

	origcpu = mycpuid;
	cpu = udp_addrcpu(addrs[1].sin_addr.s_addr, addrs[1].sin_port,
	    addrs[0].sin_addr.s_addr, addrs[0].sin_port);

	lwkt_migratecpu(cpu);

	inp = in_pcblookup_hash(&udbinfo[cpu],
	    addrs[1].sin_addr, addrs[1].sin_port,
	    addrs[0].sin_addr, addrs[0].sin_port, TRUE, NULL);
	if (inp == NULL || inp->inp_socket == NULL) {
		error = ENOENT;
	} else if (inp->inp_socket->so_cred != NULL) {
		cred0 = *(inp->inp_socket->so_cred);
		cred = &cred0;
	}

	lwkt_migratecpu(origcpu);

	if (error)
		return error;

	return SYSCTL_OUT(req, cred, sizeof(struct ucred));
}
SYSCTL_PROC(_net_inet_udp, OID_AUTO, getcred, CTLTYPE_OPAQUE|CTLFLAG_RW,
    0, 0, udp_getcred, "S,ucred", "Get the ucred of a UDP connection");

static void
udp_send_redispatch(netmsg_t msg)
{
	struct mbuf *m = msg->send.nm_m;
	int pru_flags = msg->send.nm_flags;
	struct inpcb *inp = msg->send.base.nm_so->so_pcb;
	struct mbuf *m_opt = msg->send.nm_control; /* XXX save ipopt */
	int flags = msg->send.nm_priv; /* ip_output flags */
	int error;

	logudp(redisp_ipout_beg, inp);

	/*
	 * - Don't use inp route cache.  It should only be used in the
	 *   inp owner netisr.
	 * - Access to inp_moptions should be safe, since multicast UDP
	 *   datagrams are redispatched to netisr0 and inp_moptions is
	 *   changed only in netisr0.
	 */
	error = ip_output(m, m_opt, NULL, flags, inp->inp_moptions, inp);
	if ((pru_flags & PRUS_NOREPLY) == 0)
		lwkt_replymsg(&msg->send.base.lmsg, error);

	if (m_opt != NULL) {
		/* Free saved ip options, if any */
		m_freem(m_opt);
	}

	logudp(redisp_ipout_end, inp);
}

static void
udp_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *dstaddr = msg->send.nm_addr;
	int pru_flags = msg->send.nm_flags;
	struct inpcb *inp = so->so_pcb;
	struct thread *td = msg->send.nm_td;
	int flags;

	struct udpiphdr *ui;
	int len = m->m_pkthdr.len;
	struct sockaddr_in *sin;	/* really is initialized before use */
	int error = 0, cpu;

	KKASSERT(msg->send.nm_control == NULL);

	logudp(send_beg, inp);

	if (inp == NULL) {
		error = EINVAL;
		goto release;
	}

	if (len + sizeof(struct udpiphdr) > IP_MAXPACKET) {
		error = EMSGSIZE;
		goto release;
	}

	if (inp->inp_lport == 0) {	/* unbound socket */
		boolean_t forwarded;

		error = in_pcbbind(inp, NULL, td);
		if (error)
			goto release;

		/*
		 * Need to call udp_send again, after this inpcb is
		 * inserted into wildcard hash table.
		 */
		msg->send.base.lmsg.ms_flags |= MSGF_UDP_SEND;
		forwarded = udp_inswildcardhash(inp, &msg->send.base, 0);
		if (forwarded) {
			/*
			 * The message is further forwarded, so we are
			 * done here.
			 */
			logudp(send_inswildcard, inp);
			return;
		}
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
	if (inp->inp_laddr.s_addr == INADDR_ANY ||
	    IN_MULTICAST(ntohl(inp->inp_laddr.s_addr))) {
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
		error = in_pcbladdr_find(inp, dstaddr, &if_sin, td, 1);
		if (error)
			goto release;
		ui->ui_src = if_sin->sin_addr;	/* use address of interface */
	} else {
		ui->ui_src = inp->inp_laddr;	/* use non-null bound address */
	}
	ui->ui_sport = inp->inp_lport;
	KASSERT(inp->inp_lport != 0, ("inp lport should have been bound"));

	/*
	 * Release the original thread, since it is no longer used
	 */
	if (pru_flags & PRUS_HELDTD) {
		lwkt_rele(td);
		pru_flags &= ~PRUS_HELDTD;
	}
	/*
	 * Free the dest address, since it is no longer needed
	 */
	if (pru_flags & PRUS_FREEADDR) {
		kfree(dstaddr, M_SONAME);
		pru_flags &= ~PRUS_FREEADDR;
	}

	ui->ui_ulen = htons((u_short)len + sizeof(struct udphdr));

	/*
	 * Set up checksum and output datagram.
	 */
	if (udpcksum) {
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr, ui->ui_dst.s_addr,
		    htons((u_short)len + sizeof(struct udphdr) + IPPROTO_UDP));
		m->m_pkthdr.csum_flags = CSUM_UDP;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
		m->m_pkthdr.csum_thlen = sizeof(struct udphdr);
	} else {
		ui->ui_sum = 0;
	}
	((struct ip *)ui)->ip_len = sizeof(struct udpiphdr) + len;
	((struct ip *)ui)->ip_ttl = inp->inp_ip_ttl;	/* XXX */
	((struct ip *)ui)->ip_tos = inp->inp_ip_tos;	/* XXX */
	udp_stat.udps_opackets++;

	flags = IP_DEBUGROUTE |
	    (inp->inp_socket->so_options & (SO_DONTROUTE | SO_BROADCAST));
	if (pru_flags & PRUS_DONTROUTE)
		flags |= SO_DONTROUTE;

	if (inp->inp_flags & INP_CONNECTED) {
		/*
		 * For connected socket, this datagram has already
		 * been in the correct netisr; no need to rehash.
		 */
		goto sendit;
	}

	cpu = udp_addrcpu(ui->ui_dst.s_addr, ui->ui_dport,
	    ui->ui_src.s_addr, ui->ui_sport);
	if (cpu != mycpuid) {
		struct mbuf *m_opt = NULL;
		struct netmsg_pru_send *smsg;
		struct lwkt_port *port = netisr_cpuport(cpu);

		/*
		 * Not on the CPU that matches this UDP datagram hash;
		 * redispatch to the correct CPU to do the ip_output().
		 */
		if (inp->inp_options != NULL) {
			/*
			 * If there are ip options, then save a copy,
			 * since accessing inp_options on other CPUs'
			 * is not safe.
			 *
			 * XXX optimize this?
			 */
			m_opt = m_copym(inp->inp_options, 0, M_COPYALL,
			    MB_WAIT);
		}
		if ((pru_flags & PRUS_NOREPLY) == 0) {
			/*
			 * Change some parts of the original netmsg and
			 * forward it to the target netisr.
			 *
			 * NOTE: so_port MUST NOT be checked in the target
			 * netisr.
			 */
			smsg = &msg->send;
			smsg->nm_priv = flags; /* ip_output flags */
			smsg->nm_m = m;
			smsg->nm_control = m_opt; /* XXX save ipopt */
			smsg->base.lmsg.ms_flags |= MSGF_IGNSOPORT;
			smsg->base.nm_dispatch = udp_send_redispatch;
			lwkt_forwardmsg(port, &smsg->base.lmsg);
		} else {
			/*
			 * Recreate the netmsg, since the original mbuf
			 * could have been changed.  And send it to the
			 * target netisr.
			 *
			 * NOTE: so_port MUST NOT be checked in the target
			 * netisr.
			 */
			smsg = &m->m_hdr.mh_sndmsg;
			netmsg_init(&smsg->base, so, &netisr_apanic_rport,
			    MSGF_IGNSOPORT, udp_send_redispatch);
			smsg->nm_priv = flags; /* ip_output flags */
			smsg->nm_flags = pru_flags;
			smsg->nm_m = m;
			smsg->nm_control = m_opt; /* XXX save ipopt */
			lwkt_sendmsg(port, &smsg->base.lmsg);
		}

		/* This UDP datagram is redispatched; done */
		logudp(send_redisp, inp);
		return;
	}

sendit:
	logudp(send_ipout, inp);
	error = ip_output(m, inp->inp_options, &inp->inp_route, flags,
	    inp->inp_moptions, inp);
	m = NULL;

release:
	if (m != NULL)
		m_freem(m);

	if (pru_flags & PRUS_HELDTD)
		lwkt_rele(td);
	if (pru_flags & PRUS_FREEADDR)
		kfree(dstaddr, M_SONAME);
	if ((pru_flags & PRUS_NOREPLY) == 0)
		lwkt_replymsg(&msg->send.base.lmsg, error);

	logudp(send_end, inp);
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

/*
 * This should never happen, since UDP socket does not support
 * connection acception (SO_ACCEPTCONN, i.e. listen(2)).
 */
static void
udp_abort(netmsg_t msg __unused)
{
	panic("udp_abort is called");
}

static void
udp_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp != NULL) {
		error = EINVAL;
		goto out;
	}
	error = soreserve(so, udp_sendspace, udp_recvspace, ai->sb_rlimit);
	if (error)
		goto out;

	error = in_pcballoc(so, &udbinfo[mycpuid]);
	if (error)
		goto out;

	inp = (struct inpcb *)so->so_pcb;
	inp->inp_flags |= INP_DIRECT_DETACH;
	inp->inp_ip_ttl = ip_defttl;
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
udp_inswildcard_replymsg(netmsg_t msg)
{
	lwkt_msg_t lmsg = &msg->lmsg;

	if (lmsg->ms_flags & MSGF_UDP_SEND) {
		udp_send(msg);
		/* msg is replied by udp_send() */
	} else {
		lwkt_replymsg(lmsg, lmsg->ms_error);
	}
}

static void
udp_soreuseport_dispatch(netmsg_t msg)
{
	/* This inpcb has already been in the wildcard hash. */
	in_pcblink_flags(msg->base.nm_so->so_pcb, &udbinfo[mycpuid], 0);
	udp_inswildcard_replymsg(msg);
}

static void
udp_sosetport(struct lwkt_msg *msg, lwkt_port_t port)
{
	sosetport(((struct netmsg_base *)msg)->nm_so, port);
}

static boolean_t
udp_inswildcardhash_oncpu(struct inpcb *inp, struct netmsg_base *msg)
{
	int cpu;

	KASSERT(inp->inp_pcbinfo == &udbinfo[mycpuid],
	    ("not on owner cpu"));

	in_pcbinswildcardhash(inp);
	for (cpu = 0; cpu < ncpus2; ++cpu) {
		if (cpu == mycpuid) {
			/*
			 * This inpcb has been inserted by the above
			 * in_pcbinswildcardhash().
			 */
			continue;
		}
		in_pcbinswildcardhash_oncpu(inp, &udbinfo[cpu]);
	}

	if (inp->inp_socket->so_options & SO_REUSEPORT) {
		/*
		 * For SO_REUSEPORT socket, redistribute it based on its
		 * local group index.
		 */
		cpu = inp->inp_lgrpindex & ncpus2_mask;
		if (cpu != mycpuid) {
			struct lwkt_port *port = netisr_cpuport(cpu);
			lwkt_msg_t lmsg = &msg->lmsg;

			/*
			 * We are moving the protocol processing port the
			 * socket is on, we have to unlink here and re-link
			 * on the target cpu (this inpcb is still left in
			 * the wildcard hash).
			 */
			in_pcbunlink_flags(inp, &udbinfo[mycpuid], 0);
			msg->nm_dispatch = udp_soreuseport_dispatch;

			/*
			 * See the related comment in tcp_usrreq.c
			 * tcp_connect()
			 */
			lwkt_setmsg_receipt(lmsg, udp_sosetport);
			lwkt_forwardmsg(port, lmsg);
			return TRUE; /* forwarded */
		}
	}
	return FALSE;
}

static void
udp_inswildcardhash_dispatch(netmsg_t msg)
{
	struct inpcb *inp = msg->base.nm_so->so_pcb;
	boolean_t forwarded;

	KASSERT(inp->inp_lport != 0, ("local port not set yet"));
	KASSERT((ntohs(inp->inp_lport) & ncpus2_mask) == mycpuid,
	    ("not target cpu"));

	in_pcblink(inp, &udbinfo[mycpuid]);

	forwarded = udp_inswildcardhash_oncpu(inp, &msg->base);
	if (forwarded) {
		/* The message is further forwarded, so we are done here. */
		return;
	}
	udp_inswildcard_replymsg(msg);
}

static boolean_t
udp_inswildcardhash(struct inpcb *inp, struct netmsg_base *msg, int error)
{
	lwkt_msg_t lmsg = &msg->lmsg;
	int cpu;

	ASSERT_INP_NOTINHASH(inp);

	/* This inpcb could no longer be directly detached */
	inp->inp_flags &= ~INP_DIRECT_DETACH;

	/*
	 * Always clear the route cache, so we don't need to
	 * worry about any owner CPU changes later.
	 */
	in_pcbresetroute(inp);

	KASSERT(inp->inp_lport != 0, ("local port not set yet"));
	cpu = ntohs(inp->inp_lport) & ncpus2_mask;

	lmsg->ms_error = error;
	if (cpu != mycpuid) {
		struct lwkt_port *port = netisr_cpuport(cpu);

		/*
		 * We are moving the protocol processing port the socket
		 * is on, we have to unlink here and re-link on the
		 * target cpu.
		 */
		in_pcbunlink(inp, &udbinfo[mycpuid]);
		msg->nm_dispatch = udp_inswildcardhash_dispatch;

		/* See the related comment in tcp_usrreq.c tcp_connect() */
		lwkt_setmsg_receipt(lmsg, udp_sosetport);
		lwkt_forwardmsg(port, lmsg);
		return TRUE; /* forwarded */
	}

	return udp_inswildcardhash_oncpu(inp, msg);
}

static void
udp_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp) {
		struct sockaddr *nam = msg->bind.nm_nam;
		struct thread *td = msg->bind.nm_td;

		error = in_pcbbind(inp, nam, td);
		if (error == 0) {
			struct sockaddr_in *sin = (struct sockaddr_in *)nam;
			boolean_t forwarded;

			if (sin->sin_addr.s_addr != INADDR_ANY)
				inp->inp_flags |= INP_WASBOUND_NOTANY;

			forwarded = udp_inswildcardhash(inp,
			    &msg->bind.base, 0);
			if (forwarded) {
				/*
				 * The message is further forwarded, so
				 * we are done here.
				 */
				return;
			}
		}
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
udp_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	struct inpcb *inp;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct sockaddr_in *if_sin;
	struct lwkt_port *port;
	int error;

	KKASSERT(msg->connect.nm_m == NULL);

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}

	if (msg->connect.nm_flags & PRUC_RECONNECT) {
		msg->connect.nm_flags &= ~PRUC_RECONNECT;
		in_pcblink(inp, &udbinfo[mycpuid]);
	}

	if (inp->inp_faddr.s_addr != INADDR_ANY) {
		error = EISCONN;
		goto out;
	}
	error = 0;

	/*
	 * Bind if we have to
	 */
	if (inp->inp_lport == 0) {
		error = in_pcbbind(inp, NULL, td);
		if (error)
			goto out;
	}

	/*
	 * Calculate the correct protocol processing thread.  The connect
	 * operation must run there.
	 */
	error = in_pcbladdr(inp, nam, &if_sin, td);
	if (error)
		goto out;
	if (!prison_remote_ip(td, nam)) {
		error = EAFNOSUPPORT; /* IPv6 only jail */
		goto out;
	}

	port = udp_addrport(sin->sin_addr.s_addr, sin->sin_port,
	    inp->inp_laddr.s_addr != INADDR_ANY ?
	    inp->inp_laddr.s_addr : if_sin->sin_addr.s_addr, inp->inp_lport);
	if (port != &curthread->td_msgport) {
		lwkt_msg_t lmsg = &msg->connect.base.lmsg;
		int nm_flags = PRUC_RECONNECT;

		/*
		 * in_pcbladdr() may have allocated a route entry for us
		 * on the current CPU, but we need a route entry on the
		 * inpcb's owner CPU, so free it here.
		 */
		in_pcbresetroute(inp);

		if (inp->inp_flags & INP_WILDCARD) {
			/*
			 * Remove this inpcb from the wildcard hash before
			 * the socket's msgport changes.
			 */
			udp_remwildcardhash(inp);
		}

		/*
		 * We are moving the protocol processing port the socket
		 * is on, we have to unlink here and re-link on the
		 * target cpu.
		 */
		in_pcbunlink(inp, &udbinfo[mycpuid]);
		msg->connect.nm_flags |= nm_flags;

		/* See the related comment in tcp_usrreq.c tcp_connect() */
		lwkt_setmsg_receipt(lmsg, udp_sosetport);
		lwkt_forwardmsg(port, lmsg);
		/* msg invalid now */
		return;
	}
	error = udp_connect_oncpu(inp, sin, if_sin);
out:
	if (error && inp != NULL && inp->inp_lport != 0 &&
	    (inp->inp_flags & INP_WILDCARD) == 0) {
		boolean_t forwarded;

		/* Connect failed; put it to wildcard hash. */
		forwarded = udp_inswildcardhash(inp, &msg->connect.base,
		    error);
		if (forwarded) {
			/*
			 * The message is further forwarded, so we are done
			 * here.
			 */
			return;
		}
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
udp_remwildcardhash(struct inpcb *inp)
{
	int cpu;

	KASSERT(inp->inp_pcbinfo == &udbinfo[mycpuid],
	    ("not on owner cpu"));

	for (cpu = 0; cpu < ncpus2; ++cpu) {
		if (cpu == mycpuid) {
			/*
			 * This inpcb will be removed by the later
			 * in_pcbremwildcardhash().
			 */
			continue;
		}
		in_pcbremwildcardhash_oncpu(inp, &udbinfo[cpu]);
	}
	in_pcbremwildcardhash(inp);
}

static int
udp_connect_oncpu(struct inpcb *inp, struct sockaddr_in *sin,
    struct sockaddr_in *if_sin)
{
	struct socket *so = inp->inp_socket;
	struct inpcb *oinp;

	oinp = in_pcblookup_hash(inp->inp_pcbinfo,
	    sin->sin_addr, sin->sin_port,
	    inp->inp_laddr.s_addr != INADDR_ANY ?
	    inp->inp_laddr : if_sin->sin_addr, inp->inp_lport, FALSE, NULL);
	if (oinp != NULL)
		return EADDRINUSE;

	/*
	 * No more errors can occur, finish adjusting the socket
	 * and change the processing port to reflect the connected
	 * socket.  Once set we can no longer safely mess with the
	 * socket.
	 */

	if (inp->inp_flags & INP_WILDCARD)
		udp_remwildcardhash(inp);

	if (inp->inp_laddr.s_addr == INADDR_ANY)
		inp->inp_laddr = if_sin->sin_addr;
	inp->inp_faddr = sin->sin_addr;
	inp->inp_fport = sin->sin_port;
	in_pcbinsconnhash(inp);

	soisconnected(so);

	return 0;
}

static void
udp_detach2(struct socket *so)
{
	in_pcbdetach(so->so_pcb);
	sodiscard(so);
	sofree(so);
}

static void
udp_detach_final_dispatch(netmsg_t msg)
{
	udp_detach2(msg->base.nm_so);
}

static void
udp_detach_oncpu_dispatch(netmsg_t msg)
{
	struct netmsg_base *clomsg = &msg->base;
	struct socket *so = clomsg->nm_so;
	struct inpcb *inp = so->so_pcb;
	struct thread *td = curthread;
	int nextcpu, cpuid = mycpuid;

	KASSERT(td->td_type == TD_TYPE_NETISR, ("not in netisr"));

	if (inp->inp_flags & INP_WILDCARD) {
		/*
		 * This inp will be removed on the inp's
		 * owner CPU later, so don't do it now.
		 */
		if (&td->td_msgport != so->so_port)
			in_pcbremwildcardhash_oncpu(inp, &udbinfo[cpuid]);
	}

	if (cpuid == 0) {
		/*
		 * Free and clear multicast socket option,
		 * which is only accessed in netisr0.
		 */
		ip_freemoptions(inp->inp_moptions);
		inp->inp_moptions = NULL;
	}

	nextcpu = cpuid + 1;
	if (nextcpu < ncpus2) {
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &clomsg->lmsg);
	} else {
		/*
		 * No one could see this inpcb now; destroy this
		 * inpcb in its owner netisr.
		 */
		netmsg_init(clomsg, so, &netisr_apanic_rport, 0,
		    udp_detach_final_dispatch);
		lwkt_sendmsg(so->so_port, &clomsg->lmsg);
	}
}

static void
udp_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct netmsg_base *clomsg;
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL) {
		lwkt_replymsg(&msg->detach.base.lmsg, EINVAL);
		return;
	}

	/*
	 * Reply EJUSTRETURN ASAP, we will call sodiscard() and
	 * sofree() later.
	 */
	lwkt_replymsg(&msg->detach.base.lmsg, EJUSTRETURN);

	if (ncpus2 == 1) {
		/* Only one CPU, detach the inpcb directly. */
		udp_detach2(so);
		return;
	}

	/*
	 * Remove this inpcb from the inpcb list first, so that
	 * no one could find this inpcb from the inpcb list.
	 */
	in_pcbofflist(inp);

	if (inp->inp_flags & INP_DIRECT_DETACH) {
		/*
		 * Direct detaching is allowed
		 */
		KASSERT((inp->inp_flags & INP_WILDCARD) == 0,
		    ("in the wildcardhash"));
		KASSERT(inp->inp_moptions == NULL, ("has mcast options"));
		udp_detach2(so);
		return;
	}

	/*
	 * Go through netisrs which process UDP to make sure
	 * no one could find this inpcb anymore.
	 */
	clomsg = &so->so_clomsg;
	netmsg_init(clomsg, so, &netisr_apanic_rport, MSGF_IGNSOPORT,
	    udp_detach_oncpu_dispatch);
	lwkt_sendmsg(netisr_cpuport(0), &clomsg->lmsg);
}

static void
udp_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct inpcb *inp;
	boolean_t forwarded;
	int error = 0;

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}
	if (inp->inp_faddr.s_addr == INADDR_ANY) {
		error = ENOTCONN;
		goto out;
	}

	soclrstate(so, SS_ISCONNECTED);		/* XXX */

	in_pcbdisconnect(inp);

	/*
	 * Follow traditional BSD behavior and retain the local port
	 * binding.  But, fix the old misbehavior of overwriting any
	 * previously bound local address.
	 */
	if (!(inp->inp_flags & INP_WASBOUND_NOTANY))
		inp->inp_laddr.s_addr = INADDR_ANY;

	if (so->so_state & SS_ISCLOSING) {
		/*
		 * If this socket is being closed, there is no need
		 * to put this socket back into wildcard hash table.
		 */
		error = 0;
		goto out;
	}

	forwarded = udp_inswildcardhash(inp, &msg->disconnect.base, 0);
	if (forwarded) {
		/*
		 * The message is further forwarded, so we are done
		 * here.
		 */
		return;
	}
out:
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

void
udp_shutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp) {
		socantsendmore(so);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->shutdown.base.lmsg, error);
}

struct pr_usrreqs udp_usrreqs = {
	.pru_abort = udp_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = udp_attach,
	.pru_bind = udp_bind,
	.pru_connect = udp_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in_control_dispatch,
	.pru_detach = udp_detach,
	.pru_disconnect = udp_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = in_setpeeraddr_dispatch,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = udp_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = udp_shutdown,
	.pru_sockaddr = in_setsockaddr_dispatch,
	.pru_sosend = sosendudp,
	.pru_soreceive = soreceive
};
