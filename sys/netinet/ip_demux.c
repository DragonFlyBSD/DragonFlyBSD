/*
 * Copyright (c) 2003 Jeffrey Hsu
 * All rights reserved.
 *
 * $DragonFly: src/sys/netinet/ip_demux.c,v 1.5 2004/03/05 20:00:03 hsu Exp $
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/thread.h>
#include <sys/sysctl.h>

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

/*
 * XXX when we remove the MP lock changes to this must be master-synchronized
 */
static int      ip_mthread_enable = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, mthread_enable, CTLFLAG_RW,
    &ip_mthread_enable, 0, "");

static int
INP_MPORT_HASH(in_addr_t src, in_addr_t dst, int sport, int dport)
{
	/*
	 * Use low order bytes.
	 */

#if (BYTE_ORDER == LITTLE_ENDIAN)
	KASSERT(ncpus2 < 256, ("need different hash function"));  /* XXX JH */
	return (((src >> 24) ^ (sport >> 8) ^ (dst >> 24) ^ (dport >> 8)) &
		ncpus2_mask);
#else
	return ((src ^ sport ^ dst ^ dport) & ncpus2_mask);
#endif
}

lwkt_port_t
ip_mport(struct mbuf *m)
{
	struct ip *ip = mtod(m, struct ip *);
	int hlen;
	struct tcphdr *th;
	struct udphdr *uh;
	lwkt_port_t port;

	if (ip_mthread_enable == 0)
		return (&netisr_cpu[0].td_msgport);

	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL) {
		ipstat.ips_toosmall++;
		return (NULL);
	}

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK))
		return (&netisr_cpu[0].td_msgport);

	hlen = ip->ip_hl << 2;

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (m->m_len < sizeof(struct tcpiphdr) &&
		    (m = m_pullup(m, sizeof(struct tcpiphdr))) == NULL) {
			tcpstat.tcps_rcvshort++;
			return (NULL);
		}

		th = (struct tcphdr *)((caddr_t)ip + hlen);
		port = &tcp_thread[INP_MPORT_HASH(ip->ip_src.s_addr,
		    ip->ip_dst.s_addr, th->th_sport, th->th_dport)].td_msgport;
		break;
	case IPPROTO_UDP:
		if (m->m_len < hlen + sizeof(struct udphdr) &&
		    (m = m_pullup(m, hlen + sizeof(struct udphdr))) == NULL) {
			udpstat.udps_hdrops++;
			return (NULL);
		}

		uh = (struct udphdr *)((caddr_t)ip + hlen);
		port = &udp_thread[INP_MPORT_HASH(ip->ip_src.s_addr,
		    ip->ip_dst.s_addr, uh->uh_sport, uh->uh_dport)].td_msgport;
		break;
	default:
		port = &netisr_cpu[0].td_msgport;
		break;
	}
	KKASSERT(port->mp_putport != NULL);

	return (port);
}

lwkt_port_t
tcp_soport(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	return (&tcp_thread[INP_MPORT_HASH(inp->inp_laddr.s_addr,
	    inp->inp_faddr.s_addr, inp->inp_lport, inp->inp_fport)].td_msgport);
}

lwkt_port_t
udp_soport(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	return (&udp_thread[INP_MPORT_HASH(inp->inp_laddr.s_addr,
	    inp->inp_faddr.s_addr, inp->inp_lport, inp->inp_fport)].td_msgport);
}

void
tcp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, NULL, NULL, 
			&tcp_thread[cpu], 0, cpu, "tcp_thread %d", cpu);
	}
}

void
udp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, NULL, NULL,
			&udp_thread[cpu], 0, cpu, "udp_thread %d", cpu);
	}
}
