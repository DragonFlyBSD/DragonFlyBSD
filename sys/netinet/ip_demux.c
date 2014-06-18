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
#include <net/netisr2.h>
#include <net/toeplitz2.h>

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

/*
 * Toeplitz hash functions - the idea is to match the hardware.
 */
static __inline int
INP_MPORT_HASH_UDP(in_addr_t faddr, in_addr_t laddr,
		   in_port_t fport, in_port_t lport)
{
	return toeplitz_hash(toeplitz_rawhash_addr(faddr, laddr));
}

static __inline int
INP_MPORT_HASH_TCP(in_addr_t faddr, in_addr_t laddr,
		   in_port_t fport, in_port_t lport)
{
	return toeplitz_hash(
	       toeplitz_rawhash_addrport(faddr, laddr, fport, lport));
}

/*
 * Map a network address to a processor.
 */
int
tcp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (netisr_hashcpu(INP_MPORT_HASH_TCP(faddr, laddr, fport, lport)));
}

/*
 * Not implemented yet, use protocol thread 0
 */
int
udp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
#ifdef notyet
	return (netisr_hashcpu(INP_MPORT_HASH_UDP(faddr, laddr, fport, lport)));
#else
	return 0;
#endif
}

int
udp_addrcpu_pkt(in_addr_t faddr, in_port_t fport, in_addr_t laddr,
    in_port_t lport)
{
	if (IN_MULTICAST(ntohl(faddr))) {
		/* XXX handle multicast on CPU0 for now */
		return 0;
	}
	return (netisr_hashcpu(INP_MPORT_HASH_UDP(faddr, laddr, fport, lport)));
}

/*
 * If the packet is a valid IP datagram, upon returning of this function
 * following things are promised:
 *
 * o  IP header (including any possible IP options) and any data preceding
 *    IP header (usually linker layer header) are in one mbuf (m_len).
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
ip_lengthcheck(struct mbuf **mp, int hoff)
{
	struct mbuf *m = *mp;
	struct ip *ip;
	int len, iphlen, iplen;
	struct tcphdr *th;
	int thoff;				/* TCP data offset */

	len = hoff + sizeof(struct ip);

	/* The packet must be at least the size of an IP header. */
	if (m->m_pkthdr.len < len) {
		ipstat.ips_tooshort++;
		goto fail;
	}

	/* The fixed IP header must reside completely in the first mbuf. */
	if (m->m_len < len) {
		m = m_pullup(m, len);
		if (m == NULL) {
			ipstat.ips_toosmall++;
			goto fail;
		}
	}

	ip = mtodoff(m, struct ip *, hoff);

	/* Bound check the packet's stated IP header length. */
	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(struct ip)) {	/* minimum header length */
		ipstat.ips_badhlen++;
		goto fail;
	}

	/* The full IP header must reside completely in the one mbuf. */
	if (m->m_len < hoff + iphlen) {
		m = m_pullup(m, hoff + iphlen);
		if (m == NULL) {
			ipstat.ips_badhlen++;
			goto fail;
		}
		ip = mtodoff(m, struct ip *, hoff);
	}

	iplen = ntohs(ip->ip_len);

	/*
	 * Check that the amount of data in the buffers is as
	 * at least much as the IP header would have us expect.
	 */
	if (m->m_pkthdr.len < hoff + iplen) {
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
		if (m->m_len < hoff + iphlen + sizeof(struct tcphdr)) {
			m = m_pullup(m, hoff + iphlen + sizeof(struct tcphdr));
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				goto fail;
			}
			ip = mtodoff(m, struct ip *, hoff);
		}
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		if (thoff < sizeof(struct tcphdr) ||
		    thoff + iphlen > ntohs(ip->ip_len)) {
			tcpstat.tcps_rcvbadoff++;
			goto fail;
		}
		if (m->m_len < hoff + iphlen + thoff) {
			m = m_pullup(m, hoff + iphlen + thoff);
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				goto fail;
			}
		}
		break;
	case IPPROTO_UDP:
		if (iplen < iphlen + sizeof(struct udphdr)) {
			++udp_stat.udps_hdrops;
			goto fail;
		}
		if (m->m_len < hoff + iphlen + sizeof(struct udphdr)) {
			m = m_pullup(m, hoff + iphlen + sizeof(struct udphdr));
			if (m == NULL) {
				udp_stat.udps_hdrops++;
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
 * Assign a protocol processing thread to a packet.  The IP header is at
 * offset (hoff) in the packet (i.e. the mac header might still be intact).
 *
 * This function can blow away the mbuf if the packet is malformed.
 */
void
ip_hashfn(struct mbuf **mptr, int hoff, int dir)
{
	struct ip *ip;
	int iphlen;
	struct tcphdr *th;
	struct udphdr *uh;
	struct mbuf *m;
	int hash;

	if (!ip_lengthcheck(mptr, hoff))
		return;

	m = *mptr;
	ip = mtodoff(m, struct ip *, hoff);
	iphlen = ip->ip_hl << 2;

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK)) {
		hash = 0;
		goto back;
	}

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		hash = INP_MPORT_HASH_TCP(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    th->th_sport, th->th_dport);
		break;

	case IPPROTO_UDP:
		if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
			/* XXX handle multicast on CPU0 for now */
			hash = 0;
			break;
		}
		uh = (struct udphdr *)((caddr_t)ip + iphlen);
		hash = INP_MPORT_HASH_UDP(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    uh->uh_sport, uh->uh_dport);
		break;

	default:
		hash = 0;
		break;
	}
back:
	m->m_flags |= M_HASH;
	m->m_pkthdr.hash = hash;
}

void
ip_hashfn_in(struct mbuf **mptr, int hoff)
{
	ip_hashfn(mptr, hoff, IP_MPORT_IN);
}

/*
 * Verify and adjust the hash value of the packet.
 *
 * Unlike ip_hashfn(), the packet content is not accessed.  The packet info
 * (pi) and the hash of the packet (m_pkthdr.hash) is used instead.
 *
 * Caller has already made sure that m_pkthdr.hash is valid, i.e. m_flags
 * has M_HASH set.
 */
void
ip_hashcheck(struct mbuf *m, const struct pktinfo *pi)
{
	KASSERT((m->m_flags & M_HASH), ("no valid packet hash"));

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (pi->pi_flags & PKTINFO_FLAG_FRAG) {
		m->m_pkthdr.hash = 0;
		return;
	}

	switch (pi->pi_l3proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;

	default:
		/* Let software calculate the hash */
		m->m_flags &= ~M_HASH;
		break;
	}
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
		 * A new message will be allocated later to save necessary
		 * information and will be forwarded to all network protocol
		 * threads in the following way:
		 *
		 * (the the thread owns the msgport that we return here)
		 * netisr0 <--+
		 *    |       |
		 *    |       |
		 *    |       |
		 *    +-------+
		 *     sendmsg
		 *     [msg is kmalloc()ed]
		 *    
		 *
		 * Later on, when the msg is received by netisr0:
		 *
		 *         forwardmsg         forwardmsg
		 * netisr0 ---------> netisr1 ---------> netisrN
		 *                                       [msg is kfree()ed]
		 */
		return cpu0_ctlport(cmd, sa, vip);
	} else {
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		cpu = tcp_addrcpu(faddr.s_addr, th->th_dport,
				  ip->ip_src.s_addr, th->th_sport);
	}
	return(netisr_cpuport(cpu));
}

lwkt_port_t
tcp_addrport(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return(netisr_cpuport(tcp_addrcpu(faddr, fport, laddr, lport)));
}

lwkt_port_t
tcp_addrport0(void)
{
	return(netisr_cpuport(0));
}

lwkt_port_t
udp_addrport(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return(netisr_cpuport(udp_addrcpu(faddr, fport, laddr, lport)));
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
	if (ip == NULL || PRC_IS_REDIRECT(cmd) || cmd == PRC_HOSTDEAD) {
		/*
		 * See the comment in tcp_ctlport.
		 */
		return cpu0_ctlport(cmd, sa, vip);
	} else {
		uh = (struct udphdr *)((caddr_t)ip + (ip->ip_hl << 2));

		cpu = udp_addrcpu(faddr.s_addr, ip->ip_src.s_addr,
				  uh->uh_dport, uh->uh_sport);
	}
	return (netisr_cpuport(cpu));
}

struct lwkt_port *
tcp_initport(void)
{
	return netisr_cpuport(mycpuid & ncpus2_mask);
}
