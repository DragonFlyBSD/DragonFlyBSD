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
 * $DragonFly: src/sys/netinet/ip_demux.c,v 1.30 2005/01/19 17:30:54 dillon Exp $
 */

/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 *
 * License terms: all terms for the DragonFly license above plus the following:
 *
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Jeffrey M. Hsu
 *	for the DragonFly Project.
 *
 *    This requirement may be waived with permission from Jeffrey Hsu.
 *    This requirement will sunset and may be removed on July 8 2005,
 *    after which the standard DragonFly license (as shown above) will
 *    apply.
 */

#include "opt_inet.h"

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

static struct thread tcp_thread[MAXCPU];
static struct thread udp_thread[MAXCPU];

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

boolean_t
ip_lengthcheck(struct mbuf **mp)
{
	struct mbuf *m = *mp;
	struct ip *ip;
	int iphlen, iplen;
	struct tcphdr *th;
	int thoff;				/* TCP data offset */

	/*
	 * The packet must be at least the size of an IP header
	 */
	if (m->m_pkthdr.len < sizeof(struct ip)) {
		ipstat.ips_tooshort++;
		m_free(m);
		return FALSE;
	}

	/*
	 * The first mbuf must entirely contain the IP header
	 */
	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL) {
		ipstat.ips_toosmall++;
		return FALSE;
	}
	ip = mtod(m, struct ip *);

	/*
	 * Extract the actual IP header length and do a bounds check.  The
	 * first mbuf must entirely contain the extended IP header.
	 */
	iphlen = ip->ip_hl << 2;
	iplen = ntohs(ip->ip_len);
	if (iphlen < sizeof(struct ip)) {	/* minimum header length */
		ipstat.ips_badhlen++;
		m_free(m);
		return FALSE;
	}
	if (m->m_len < iphlen) {
		m = m_pullup(m, iphlen);
		if (m == NULL) {
			ipstat.ips_badhlen++;
			return FALSE;
		}
		ip = mtod(m, struct ip *);
	}

	/*
	 * The TCP/IP or UDP/IP header must be entirely contained within
	 * the first fragment of a packet.  Packet filters will break if they
	 * aren't.
	 *
	 * Since the packet will be trimmed to ip_len we must also make sure
	 * the potentially trimmed down length is still sufficient to hold
	 * the header(s).
	 */
	if ((ntohs(ip->ip_off) & IP_OFFMASK) == 0) {
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			if (iplen < iphlen + sizeof(struct tcphdr)) {
				++tcpstat.tcps_rcvshort;
				m_free(m);
				return FALSE;
			}
			if (m->m_len < iphlen + sizeof(struct tcphdr)) {
				m = m_pullup(m, iphlen + sizeof(struct tcphdr));
				if (m == NULL) {
					tcpstat.tcps_rcvshort++;
					return FALSE;
				}
				ip = mtod(m, struct ip *);
			}
			break;
		case IPPROTO_UDP:
			if (iplen < iphlen + sizeof(struct udphdr)) {
				++udpstat.udps_hdrops;
				m_free(m);
				return FALSE;
			}
			if (m->m_len < iphlen + sizeof(struct udphdr)) {
				m = m_pullup(m, iphlen + sizeof(struct udphdr));
				if (m == NULL) {
					udpstat.udps_hdrops++;
					return FALSE;
				}
				ip = mtod(m, struct ip *);
			}
			break;
		default:
			if (iplen < iphlen) {
				++ipstat.ips_badlen;
				m_free(m);
				return FALSE;
			}
			break;
		}
	}

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		if (thoff < sizeof(struct tcphdr) ||
		    thoff > ntohs(ip->ip_len)) {
			tcpstat.tcps_rcvbadoff++;
			m_free(m);
			return FALSE;
		}
		if (m->m_len < iphlen + thoff) {
			m = m_pullup(m, iphlen + thoff);
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				return FALSE;
			}
		}
		break;
	}

	*mp = m;
	return TRUE;
}

/*
 * Map a packet to a protocol processing thread and return the thread's port.
 * If an error occurs, the passed mbuf will be freed, *mptr will be set
 * to NULL, and NULL will be returned.  If no error occurs, the passed mbuf
 * may be modified and a port pointer will be returned.
 */
lwkt_port_t
ip_mport(struct mbuf **mptr)
{
	struct ip *ip;
	int iphlen;
	struct tcphdr *th;
	struct udphdr *uh;
	struct mbuf *m;
	int thoff;				/* TCP data offset */
	lwkt_port_t port;
	int cpu;

	if (!ip_lengthcheck(mptr)) {
		*mptr = NULL;
		return (NULL);
	}

	m = *mptr;
	ip = mtod(m, struct ip *);
	iphlen = ip->ip_hl << 2;

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK))
		return (&netisr_cpu[0].td_msgport);

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		cpu = INP_MPORT_HASH(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    th->th_sport, th->th_dport);
		port = &tcp_thread[cpu].td_msgport;
		break;
	case IPPROTO_UDP:
		uh = (struct udphdr *)((caddr_t)ip + iphlen);

		if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
		    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif)) {
			cpu = 0;
		} else {
			cpu = INP_MPORT_HASH(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, uh->uh_sport, uh->uh_dport);
		}
		port = &udp_thread[cpu].td_msgport;
		break;
	default:
		port = &netisr_cpu[0].td_msgport;
		break;
	}

	return (port);
}

/*
 * Map a TCP socket to a protocol processing thread.
 */
lwkt_port_t
tcp_soport(struct socket *so, struct sockaddr *nam, int req)
{
	struct inpcb *inp;

	/* The following processing all take place on Protocol Thread 0. */
	if (req == PRU_BIND || req == PRU_CONNECT || req == PRU_ATTACH ||
	    req == PRU_LISTEN)
		return (&tcp_thread[0].td_msgport);

	inp = sotoinpcb(so);
	if (!inp)		/* connection reset by peer */
		return (&tcp_thread[0].td_msgport);

	/*
	 * Already bound and connected or listening.  For TCP connections,
	 * the (faddr, fport, laddr, lport) association cannot change now.
	 *
	 * Note: T/TCP code needs some reorganization to fit into
	 * this model.  XXX JH
	 *
	 * Rely on type-stable memory and check in protocol handler
	 * to fix race condition here w/ deallocation of inp.  XXX JH
	 */
	return (&tcp_thread[INP_MPORT_HASH(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport)].td_msgport);
}

lwkt_port_t
tcp_addrport(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (&tcp_thread[tcp_addrcpu(faddr, fport,
					laddr, lport)].td_msgport);
}

/*
 * Map a UDP socket to a protocol processing thread.
 */
lwkt_port_t
udp_soport(struct socket *so, struct sockaddr *nam, int req)
{
	struct inpcb *inp;

	/*
	 * The following processing all take place on Protocol Thread 0:
	 *   only bind() and connect() have a non-null nam parameter
	 *   attach() has a null socket parameter
	 *   Fast and slow timeouts pass in two NULLs
	 */
	if (nam != NULL || so == NULL)
		return (&udp_thread[0].td_msgport);

	inp = sotoinpcb(so);

	if (IN_MULTICAST(ntohl(inp->inp_laddr.s_addr)))
		return (&udp_thread[0].td_msgport);

	/*
	 * Rely on type-stable memory and check in protocol handler
	 * to fix race condition here w/ deallocation of inp.  XXX JH
	 */

	return (&udp_thread[INP_MPORT_HASH(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport)].td_msgport);
}

/*
 * Map a network address to a processor.
 */
int
tcp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (INP_MPORT_HASH(faddr, laddr, fport, lport));
}

int
udp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	if (IN_MULTICAST(ntohl(laddr)))
		return (0);
	else
		return (INP_MPORT_HASH(faddr, laddr, fport, lport));
}

/*
 * Return LWKT port for cpu.
 */
lwkt_port_t
tcp_cport(int cpu)
{
	return (&tcp_thread[cpu].td_msgport);
}

void
tcp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(tcpmsg_service_loop, NULL, NULL,
			&tcp_thread[cpu], 0, cpu, "tcp_thread %d", cpu);
		netmsg_service_port_init(&tcp_thread[cpu].td_msgport);
	}
}

void
udp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, NULL, NULL,
			&udp_thread[cpu], 0, cpu, "udp_thread %d", cpu);
		netmsg_service_port_init(&udp_thread[cpu].td_msgport);
	}
}
