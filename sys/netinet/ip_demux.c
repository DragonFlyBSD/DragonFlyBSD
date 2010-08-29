/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/sys/netinet/ip_demux.c,v 1.45 2008/11/11 10:46:58 sephe Exp $
 */

#include "opt_inet.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/thread.h>
#include <sys/sysctl.h>
#include <sys/globaldata.h>

#include <net/if.h>
#include <net/netisr.h>
#ifdef RSS
#include <net/toeplitz2.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

extern struct thread netisr_cpu[];
extern int udp_mpsafe_thread;

static struct thread tcp_thread[MAXCPU];
static struct thread udp_thread[MAXCPU];

#ifndef RSS

static __inline int
INP_MPORT_HASH(in_addr_t faddr, in_addr_t laddr,
	       in_port_t fport, in_port_t lport)
{
	/*
	 * Use low order bytes.
	 */

#if (BYTE_ORDER == LITTLE_ENDIAN)
	KASSERT(ncpus2 < 256, ("need different hash function"));  /* XXX JH */
	return (((faddr >> 24) ^ (fport >> 8) ^ (laddr >> 24) ^ (lport >> 8)) &
		ncpus2_mask);
#else
	return ((faddr ^ fport ^ laddr ^ lport) & ncpus2_mask);
#endif
}

#endif	/* !RSS */

static __inline int
INP_MPORT_HASH_UDP(in_addr_t faddr, in_addr_t laddr,
		   in_port_t fport, in_port_t lport)
{
#ifndef RSS
	return INP_MPORT_HASH(faddr, laddr, fport, lport);
#else
	return toeplitz_hash(toeplitz_rawhash_addr(faddr, laddr));
#endif
}

static __inline int
INP_MPORT_HASH_TCP(in_addr_t faddr, in_addr_t laddr,
		   in_port_t fport, in_port_t lport)
{
#ifndef RSS
	return INP_MPORT_HASH(faddr, laddr, fport, lport);
#else
	return toeplitz_hash(
	       toeplitz_rawhash_addrport(faddr, laddr, fport, lport));
#endif
}

/*
 * If the packet is a valid IP datagram, upon returning of this function
 * following things are promised:
 *
 * o  IP header (including any possible IP options) is in one mbuf (m_len).
 * o  IP header length is not less than the minimum (sizeof(struct ip)).
 * o  IP total length is not less than IP header length.
 * o  IP datagram resides completely in the mbuf chain,
 *    i.e. pkthdr.len >= IP total length.
 *
 * If the packet is a UDP datagram,
 * o  IP header (including any possible IP options) and UDP header are in
 *    one mbuf (m_len).
 * o  IP total length is not less than (IP header length + UDP header length).
 *
 * If the packet is a TCP segment,
 * o  IP header (including any possible IP options) and TCP header (including
 *    any possible TCP options) are in one mbuf (m_len).
 * o  TCP header length is not less than the minimum (sizeof(struct tcphdr)).
 * o  IP total length is not less than (IP header length + TCP header length).
 */
boolean_t
ip_lengthcheck(struct mbuf **mp)
{
	struct mbuf *m = *mp;
	struct ip *ip;
	int iphlen, iplen;
	struct tcphdr *th;
	int thoff;				/* TCP data offset */

	/* The packet must be at least the size of an IP header. */
	if (m->m_pkthdr.len < sizeof(struct ip)) {
		ipstat.ips_tooshort++;
		goto fail;
	}

	/* The fixed IP header must reside completely in the first mbuf. */
	if (m->m_len < sizeof(struct ip)) {
		m = m_pullup(m, sizeof(struct ip));
		if (m == NULL) {
			ipstat.ips_toosmall++;
			goto fail;
		}
	}

	ip = mtod(m, struct ip *);

	/* Bound check the packet's stated IP header length. */
	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(struct ip)) {	/* minimum header length */
		ipstat.ips_badhlen++;
		goto fail;
	}

	/* The full IP header must reside completely in the one mbuf. */
	if (m->m_len < iphlen) {
		m = m_pullup(m, iphlen);
		if (m == NULL) {
			ipstat.ips_badhlen++;
			goto fail;
		}
		ip = mtod(m, struct ip *);
	}

	iplen = ntohs(ip->ip_len);

	/*
	 * Check that the amount of data in the buffers is as
	 * at least much as the IP header would have us expect.
	 */
	if (m->m_pkthdr.len < iplen) {
		ipstat.ips_tooshort++;
		goto fail;
	}

	/*
	 * Fragments other than the first fragment don't have much
	 * length information.
	 */
	if (ntohs(ip->ip_off) & IP_OFFMASK)
		goto ipcheckonly;

	/*
	 * The TCP/IP or UDP/IP header must be entirely contained within
	 * the first fragment of a packet.  Packet filters will break if they
	 * aren't.
	 *
	 * Since the packet will be trimmed to ip_len we must also make sure
	 * the potentially trimmed down length is still sufficient to hold
	 * the header(s).
	 */
	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (iplen < iphlen + sizeof(struct tcphdr)) {
			++tcpstat.tcps_rcvshort;
			goto fail;
		}
		if (m->m_len < iphlen + sizeof(struct tcphdr)) {
			m = m_pullup(m, iphlen + sizeof(struct tcphdr));
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				goto fail;
			}
			ip = mtod(m, struct ip *);
		}
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		if (thoff < sizeof(struct tcphdr) ||
		    thoff + iphlen > ntohs(ip->ip_len)) {
			tcpstat.tcps_rcvbadoff++;
			goto fail;
		}
		if (m->m_len < iphlen + thoff) {
			m = m_pullup(m, iphlen + thoff);
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				goto fail;
			}
		}
		break;
	case IPPROTO_UDP:
		if (iplen < iphlen + sizeof(struct udphdr)) {
			++udpstat.udps_hdrops;
			goto fail;
		}
		if (m->m_len < iphlen + sizeof(struct udphdr)) {
			m = m_pullup(m, iphlen + sizeof(struct udphdr));
			if (m == NULL) {
				udpstat.udps_hdrops++;
				goto fail;
			}
		}
		break;
	default:
ipcheckonly:
		if (iplen < iphlen) {
			++ipstat.ips_badlen;
			goto fail;
		}
		break;
	}

	m->m_flags |= M_LENCHECKED;
	*mp = m;
	return TRUE;

fail:
	if (m != NULL)
		m_freem(m);
	*mp = NULL;
	return FALSE;
}

/*
 * Map a packet to a protocol processing thread and return the thread's port.
 * If an error occurs, the passed mbuf will be freed, *mptr will be set
 * to NULL, and NULL will be returned.  If no error occurs, the passed mbuf
 * may be modified and a port pointer will be returned.
 */
lwkt_port_t
ip_mport(struct mbuf **mptr, int dir)
{
	struct ip *ip;
	int iphlen;
	struct tcphdr *th;
	struct udphdr *uh;
	struct mbuf *m;
	int thoff;				/* TCP data offset */
	lwkt_port_t port;
	int cpu;

	if (!ip_lengthcheck(mptr))
		return (NULL);

	m = *mptr;
	ip = mtod(m, struct ip *);
	iphlen = ip->ip_hl << 2;

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK)) {
		cpu = 0;
		port = &netisr_cpu[cpu].td_msgport;
		goto back;
	}

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		cpu = INP_MPORT_HASH_TCP(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    th->th_sport, th->th_dport);
		port = &tcp_thread[cpu].td_msgport;
		break;

	case IPPROTO_UDP:
		uh = (struct udphdr *)((caddr_t)ip + iphlen);

#ifndef RSS
		if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
		    (dir == IP_MPORT_IN &&
		     in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif))) {
			cpu = 0;
		} else
#endif
		{
			cpu = INP_MPORT_HASH_UDP(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, uh->uh_sport, uh->uh_dport);
		}
		port = &udp_thread[cpu].td_msgport;
		break;

	default:
		cpu = 0;
		port = &netisr_cpu[cpu].td_msgport;
		break;
	}
back:
	m->m_flags |= M_HASH;
	m->m_pkthdr.hash = cpu;
	return (port);
}

lwkt_port_t
ip_mport_in(struct mbuf **mptr)
{
	return ip_mport(mptr, IP_MPORT_IN);
}

/*
 * Map a packet to a protocol processing thread and return the thread's port.
 * Unlike ip_mport(), the packet content is not accessed.  The packet info
 * (pi) and the hash of the packet (m_pkthdr.hash) is used instead.  NULL is
 * returned if the packet info does not contain enough information.
 *
 * Caller has already made sure that m_pkthdr.hash is valid, i.e. m_flags
 * has M_HASH set.
 */
lwkt_port_t
ip_mport_pktinfo(const struct pktinfo *pi, struct mbuf *m)
{
	lwkt_port_t port;

	KASSERT(m->m_pkthdr.hash < ncpus2,
		("invalid packet hash %#x\n", m->m_pkthdr.hash));

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (pi->pi_flags & PKTINFO_FLAG_FRAG) {
		m->m_pkthdr.hash = 0;
		return &netisr_cpu[0].td_msgport;
	}

	switch (pi->pi_l3proto) {
	case IPPROTO_TCP:
		port = &tcp_thread[m->m_pkthdr.hash].td_msgport;
		break;

	case IPPROTO_UDP:
		port = &udp_thread[m->m_pkthdr.hash].td_msgport;
		break;

	default:
		port = NULL;
		break;
	}
	return port;
}

/*
 * Initital port when creating the socket, generally before
 * binding or connect.
 */
lwkt_port_t
tcp_soport_attach(struct socket *so)
{
	return(&tcp_thread[0].td_msgport);
}

/*
 * This is used to map a socket to a message port for sendmsg() and friends.
 * It is not called for any other purpose.  In the case of TCP we just return
 * the port already installed in the socket.
 */
lwkt_port_t
tcp_soport(struct socket *so, struct sockaddr *nam,
	   struct mbuf **dummy __unused)
{
	return(so->so_port);
}

/*
 * Used to route icmp messages to the proper protocol thread for ctlinput
 * operation.
 */
lwkt_port_t
tcp_ctlport(int cmd, struct sockaddr *sa, void *vip)
{
	struct ip *ip = vip;
	struct tcphdr *th;
	struct in_addr faddr;
	int cpu;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return(NULL);
	if (ip == NULL || PRC_IS_REDIRECT(cmd) || cmd == PRC_HOSTDEAD) {
		/*
		 * Message will be forwarded to all TCP protocol threads
		 * in following way:
		 *
		 * netisr0 (the msgport we return here)
		 *    |
		 *    |
		 *    | domsg <----------------------------+
		 *    |                                    |
		 *    |                                    | replymsg
		 *    |                                    |
		 *    V   forwardmsg         forwardmsg    |
		 *  tcp0 ------------> tcp1 ------------> tcpN
		 */
		return cpu0_ctlport(cmd, sa, vip);
	} else {
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		cpu = tcp_addrcpu(faddr.s_addr, th->th_dport,
				  ip->ip_src.s_addr, th->th_sport);
	}
	return(&tcp_thread[cpu].td_msgport);
}

lwkt_port_t
tcp_addrport(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (&tcp_thread[tcp_addrcpu(faddr, fport,
					laddr, lport)].td_msgport);
}

lwkt_port_t
tcp_addrport0(void)
{
	return (&tcp_thread[0].td_msgport);
}

lwkt_port_t
udp_addrport(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (&udp_thread[udp_addrcpu(faddr, fport,
					laddr, lport)].td_msgport);
}

/*
 * Initital port when creating the socket, generally before
 * binding or connect.
 */
lwkt_port_t
udp_soport_attach(struct socket *so)
{
	return(&udp_thread[0].td_msgport);
}

/*
 * This is used to map a socket to a message port for sendmsg() and friends.
 * It is not called for any other purpose.
 *
 * In the case of UDP we just return the port already installed in the socket,
 * regardless of what (nam) is.
 */
lwkt_port_t
udp_soport(struct socket *so, struct sockaddr *nam,
	   struct mbuf **dummy __unused)
{
	return(so->so_port);
}

/*
 * Used to route icmp messages to the proper protocol thread for ctlinput
 * operation.
 */
lwkt_port_t
udp_ctlport(int cmd, struct sockaddr *sa, void *vip)
{
	struct ip *ip = vip;
	struct udphdr *uh;
	struct in_addr faddr;
	int cpu;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return(NULL);
	if (PRC_IS_REDIRECT(cmd)) {
		/*
		 * See the comment in tcp_ctlport; the only difference
		 * is that message is forwarded to UDP protocol theads.
		 */
		return cpu0_ctlport(cmd, sa, vip);
	} else if (ip == NULL || cmd == PRC_HOSTDEAD) {
		/*
		 * XXX
		 * Once UDP inpcbs are CPU localized, we should do
		 * the same forwarding as PRC_IS_REDIRECT(cmd)
		 */
		cpu = 0;
	} else {
		uh = (struct udphdr *)((caddr_t)ip + (ip->ip_hl << 2));

		cpu = INP_MPORT_HASH_UDP(faddr.s_addr, ip->ip_src.s_addr,
					 uh->uh_dport, uh->uh_sport);
	}
	return (&udp_thread[cpu].td_msgport);
}

/*
 * Map a network address to a processor.
 */
int
tcp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (INP_MPORT_HASH_TCP(faddr, laddr, fport, lport));
}

int
udp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
#ifndef RSS
	if (IN_MULTICAST(ntohl(laddr)))
		return (0);
	else
#endif
		return (INP_MPORT_HASH_UDP(faddr, laddr, fport, lport));
}

/*
 * Return LWKT port for cpu.
 */
lwkt_port_t
tcp_cport(int cpu)
{
	return (&tcp_thread[cpu].td_msgport);
}

lwkt_port_t
udp_cport(int cpu)
{
	return (&udp_thread[cpu].td_msgport);
}

void
tcp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(tcpmsg_service_loop, NULL, NULL,
			    &tcp_thread[cpu], TDF_NETWORK, cpu,
			    "tcp_thread %d", cpu);
		netmsg_service_port_init(&tcp_thread[cpu].td_msgport);
	}
}

void
udp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, &udp_mpsafe_thread, NULL,
			    &udp_thread[cpu], TDF_NETWORK, cpu,
			    "udp_thread %d", cpu);
		netmsg_service_port_init(&udp_thread[cpu].td_msgport);
	}
}
