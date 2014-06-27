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

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)ip_input.c	8.2 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/netinet/ip_input.c,v 1.130.2.52 2003/03/07 07:01:28 silby Exp $
 */

#define	_IP_VHL

#include "opt_bootp.h"
#include "opt_ipdn.h"
#include "opt_ipdivert.h"
#include "opt_ipstealth.h"
#include "opt_ipsec.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/mpipe.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/globaldata.h>
#include <sys/thread.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/in_cksum.h>
#include <sys/lock.h>

#include <sys/mplock2.h>

#include <machine/stdarg.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/pfil.h>
#include <net/route.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_divert.h>
#include <netinet/ip_flow.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <net/netmsg2.h>

#include <sys/socketvar.h>

#include <net/ipfw/ip_fw.h>
#include <net/dummynet/ip_dummynet.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netproto/key/key.h>
#endif

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/key.h>
#endif

int rsvp_on = 0;
static int ip_rsvp_on;
struct socket *ip_rsvpd;

int ipforwarding = 0;
SYSCTL_INT(_net_inet_ip, IPCTL_FORWARDING, forwarding, CTLFLAG_RW,
    &ipforwarding, 0, "Enable IP forwarding between interfaces");

static int ipsendredirects = 1; /* XXX */
SYSCTL_INT(_net_inet_ip, IPCTL_SENDREDIRECTS, redirect, CTLFLAG_RW,
    &ipsendredirects, 0, "Enable sending IP redirects");

int ip_defttl = IPDEFTTL;
SYSCTL_INT(_net_inet_ip, IPCTL_DEFTTL, ttl, CTLFLAG_RW,
    &ip_defttl, 0, "Maximum TTL on IP packets");

static int ip_dosourceroute = 0;
SYSCTL_INT(_net_inet_ip, IPCTL_SOURCEROUTE, sourceroute, CTLFLAG_RW,
    &ip_dosourceroute, 0, "Enable forwarding source routed IP packets");

static int ip_acceptsourceroute = 0;
SYSCTL_INT(_net_inet_ip, IPCTL_ACCEPTSOURCEROUTE, accept_sourceroute,
    CTLFLAG_RW, &ip_acceptsourceroute, 0,
    "Enable accepting source routed IP packets");

static int ip_keepfaith = 0;
SYSCTL_INT(_net_inet_ip, IPCTL_KEEPFAITH, keepfaith, CTLFLAG_RW,
    &ip_keepfaith, 0,
    "Enable packet capture for FAITH IPv4->IPv6 translator daemon");

static int nipq = 0;	/* total # of reass queues */
static int maxnipq;
SYSCTL_INT(_net_inet_ip, OID_AUTO, maxfragpackets, CTLFLAG_RW,
    &maxnipq, 0,
    "Maximum number of IPv4 fragment reassembly queue entries");

static int maxfragsperpacket;
SYSCTL_INT(_net_inet_ip, OID_AUTO, maxfragsperpacket, CTLFLAG_RW,
    &maxfragsperpacket, 0,
    "Maximum number of IPv4 fragments allowed per packet");

static int ip_sendsourcequench = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, sendsourcequench, CTLFLAG_RW,
    &ip_sendsourcequench, 0,
    "Enable the transmission of source quench packets");

int ip_do_randomid = 1;
SYSCTL_INT(_net_inet_ip, OID_AUTO, random_id, CTLFLAG_RW,
    &ip_do_randomid, 0,
    "Assign random ip_id values");	
/*
 * XXX - Setting ip_checkinterface mostly implements the receive side of
 * the Strong ES model described in RFC 1122, but since the routing table
 * and transmit implementation do not implement the Strong ES model,
 * setting this to 1 results in an odd hybrid.
 *
 * XXX - ip_checkinterface currently must be disabled if you use ipnat
 * to translate the destination address to another local interface.
 *
 * XXX - ip_checkinterface must be disabled if you add IP aliases
 * to the loopback interface instead of the interface where the
 * packets for those addresses are received.
 */
static int ip_checkinterface = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, check_interface, CTLFLAG_RW,
    &ip_checkinterface, 0, "Verify packet arrives on correct interface");

static u_long ip_hash_count = 0;
SYSCTL_ULONG(_net_inet_ip, OID_AUTO, hash_count, CTLFLAG_RD,
    &ip_hash_count, 0, "Number of packets hashed by IP");

#ifdef RSS_DEBUG
static u_long ip_rehash_count = 0;
SYSCTL_ULONG(_net_inet_ip, OID_AUTO, rehash_count, CTLFLAG_RD,
    &ip_rehash_count, 0, "Number of packets rehashed by IP");

static u_long ip_dispatch_fast = 0;
SYSCTL_ULONG(_net_inet_ip, OID_AUTO, dispatch_fast_count, CTLFLAG_RD,
    &ip_dispatch_fast, 0, "Number of packets handled on current CPU");

static u_long ip_dispatch_slow = 0;
SYSCTL_ULONG(_net_inet_ip, OID_AUTO, dispatch_slow_count, CTLFLAG_RD,
    &ip_dispatch_slow, 0, "Number of packets messaged to another CPU");
#endif

static struct lwkt_token ipq_token = LWKT_TOKEN_INITIALIZER(ipq_token);

#ifdef DIAGNOSTIC
static int ipprintfs = 0;
#endif

extern	struct domain inetdomain;
extern	struct protosw inetsw[];
u_char	ip_protox[IPPROTO_MAX];
struct	in_ifaddrhead in_ifaddrheads[MAXCPU];	/* first inet address */
struct	in_ifaddrhashhead *in_ifaddrhashtbls[MAXCPU];
						/* inet addr hash table */
u_long	in_ifaddrhmask;				/* mask for hash table */

static struct mbuf *ipforward_mtemp[MAXCPU];

struct ip_stats ipstats_percpu[MAXCPU] __cachealign;

static int
sysctl_ipstats(SYSCTL_HANDLER_ARGS)
{
	int cpu, error = 0;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		if ((error = SYSCTL_OUT(req, &ipstats_percpu[cpu],
					sizeof(struct ip_stats))))
			break;
		if ((error = SYSCTL_IN(req, &ipstats_percpu[cpu],
				       sizeof(struct ip_stats))))
			break;
	}

	return (error);
}
SYSCTL_PROC(_net_inet_ip, IPCTL_STATS, stats, (CTLTYPE_OPAQUE | CTLFLAG_RW),
    0, 0, sysctl_ipstats, "S,ip_stats", "IP statistics");

/* Packet reassembly stuff */
#define	IPREASS_NHASH_LOG2	6
#define	IPREASS_NHASH		(1 << IPREASS_NHASH_LOG2)
#define	IPREASS_HMASK		(IPREASS_NHASH - 1)
#define	IPREASS_HASH(x,y)						\
    (((((x) & 0xF) | ((((x) >> 8) & 0xF) << 4)) ^ (y)) & IPREASS_HMASK)

static TAILQ_HEAD(ipqhead, ipq) ipq[IPREASS_NHASH];

#ifdef IPCTL_DEFMTU
SYSCTL_INT(_net_inet_ip, IPCTL_DEFMTU, mtu, CTLFLAG_RW,
    &ip_mtu, 0, "Default MTU");
#endif

#ifdef IPSTEALTH
static int ipstealth = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, stealth, CTLFLAG_RW, &ipstealth, 0, "");
#else
static const int ipstealth = 0;
#endif

struct mbuf *(*ip_divert_p)(struct mbuf *, int, int);

struct pfil_head inet_pfil_hook;

/*
 * struct ip_srcrt_opt is used to store packet state while it travels
 * through the stack.
 *
 * XXX Note that the code even makes assumptions on the size and
 * alignment of fields inside struct ip_srcrt so e.g. adding some
 * fields will break the code.  This needs to be fixed.
 *
 * We need to save the IP options in case a protocol wants to respond
 * to an incoming packet over the same route if the packet got here
 * using IP source routing.  This allows connection establishment and
 * maintenance when the remote end is on a network that is not known
 * to us.
 */
struct ip_srcrt {
	struct	in_addr dst;			/* final destination */
	char	nop;				/* one NOP to align */
	char	srcopt[IPOPT_OFFSET + 1];	/* OPTVAL, OLEN and OFFSET */
	struct	in_addr route[MAX_IPOPTLEN/sizeof(struct in_addr)];
};

struct ip_srcrt_opt {
	int		ip_nhops;
	struct ip_srcrt	ip_srcrt;
};

static MALLOC_DEFINE(M_IPQ, "ipq", "IP Fragment Management");
static struct malloc_pipe ipq_mpipe;

static void		save_rte(struct mbuf *, u_char *, struct in_addr);
static int		ip_dooptions(struct mbuf *m, int, struct sockaddr_in *);
static void		ip_freef(struct ipqhead *, struct ipq *);
static void		ip_input_handler(netmsg_t);

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void
ip_init(void)
{
	struct protosw *pr;
	int i;
	int cpu;

	/*
	 * Make sure we can handle a reasonable number of fragments but
	 * cap it at 4000 (XXX).
	 */
	mpipe_init(&ipq_mpipe, M_IPQ, sizeof(struct ipq),
		    IFQ_MAXLEN, 4000, 0, NULL, NULL, NULL);
	for (i = 0; i < ncpus; ++i) {
		TAILQ_INIT(&in_ifaddrheads[i]);
		in_ifaddrhashtbls[i] =
			hashinit(INADDR_NHASH, M_IFADDR, &in_ifaddrhmask);
	}
	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == NULL)
		panic("ip_init");
	for (i = 0; i < IPPROTO_MAX; i++)
		ip_protox[i] = pr - inetsw;
	for (pr = inetdomain.dom_protosw;
	     pr < inetdomain.dom_protoswNPROTOSW; pr++) {
		if (pr->pr_domain->dom_family == PF_INET && pr->pr_protocol) {
			if (pr->pr_protocol != IPPROTO_RAW)
				ip_protox[pr->pr_protocol] = pr - inetsw;
		}
	}

	inet_pfil_hook.ph_type = PFIL_TYPE_AF;
	inet_pfil_hook.ph_af = AF_INET;
	if ((i = pfil_head_register(&inet_pfil_hook)) != 0) {
		kprintf("%s: WARNING: unable to register pfil hook, "
			"error %d\n", __func__, i);
	}

	for (i = 0; i < IPREASS_NHASH; i++)
		TAILQ_INIT(&ipq[i]);

	maxnipq = nmbclusters / 32;
	maxfragsperpacket = 16;

	ip_id = time_second & 0xffff;	/* time_second survives reboots */

	for (cpu = 0; cpu < ncpus; ++cpu) {
		/*
		 * Initialize IP statistics counters for each CPU.
		 */
		bzero(&ipstats_percpu[cpu], sizeof(struct ip_stats));

		/*
		 * Preallocate mbuf template for forwarding
		 */
		MGETHDR(ipforward_mtemp[cpu], MB_WAIT, MT_DATA);
	}

	netisr_register(NETISR_IP, ip_input_handler, ip_hashfn_in);
	netisr_register_hashcheck(NETISR_IP, ip_hashcheck);
}

/* Do transport protocol processing. */
static void
transport_processing_oncpu(struct mbuf *m, int hlen, struct ip *ip)
{
	const struct protosw *pr = &inetsw[ip_protox[ip->ip_p]];

	/*
	 * Switch out to protocol's input routine.
	 */
	PR_GET_MPLOCK(pr);
	pr->pr_input(&m, &hlen, ip->ip_p);
	PR_REL_MPLOCK(pr);
}

static void
transport_processing_handler(netmsg_t msg)
{
	struct netmsg_packet *pmsg = &msg->packet;
	struct ip *ip;
	int hlen;

	ip = mtod(pmsg->nm_packet, struct ip *);
	hlen = pmsg->base.lmsg.u.ms_result;

	transport_processing_oncpu(pmsg->nm_packet, hlen, ip);
	/* msg was embedded in the mbuf, do not reply! */
}

static void
ip_input_handler(netmsg_t msg)
{
	ip_input(msg->packet.nm_packet);
	/* msg was embedded in the mbuf, do not reply! */
}

/*
 * IP input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void
ip_input(struct mbuf *m)
{
	struct ip *ip;
	struct in_ifaddr *ia = NULL;
	struct in_ifaddr_container *iac;
	int hlen, checkif;
	u_short sum;
	struct in_addr pkt_dst;
	boolean_t using_srcrt = FALSE;		/* forward (by PFIL_HOOKS) */
	struct in_addr odst;			/* original dst address(NAT) */
	struct m_tag *mtag;
	struct sockaddr_in *next_hop = NULL;
	lwkt_port_t port;
#ifdef FAST_IPSEC
	struct tdb_ident *tdbi;
	struct secpolicy *sp;
	int error;
#endif

	M_ASSERTPKTHDR(m);

	/*
	 * This routine is called from numerous places which may not have
	 * characterized the packet.
	 */
	ip = mtod(m, struct ip *);
	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
	    ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK)) {
		/*
		 * Force hash recalculation for fragments and multicast
		 * packets; hardware may not do it correctly.
		 * XXX add flag to indicate the hash is from hardware
		 */
		m->m_flags &= ~M_HASH;
	}
	if ((m->m_flags & M_HASH) == 0) {
		ip_hashfn(&m, 0, IP_MPORT_IN);
		if (m == NULL)
			return;
		KKASSERT(m->m_flags & M_HASH);

		if (&curthread->td_msgport !=
		    netisr_hashport(m->m_pkthdr.hash)) {
			netisr_queue(NETISR_IP, m);
			/* Requeued to other netisr msgport; done */
			return;
		}

		/* mbuf could have been changed */
		ip = mtod(m, struct ip *);
	}

	/*
	 * Pull out certain tags
	 */
	if (m->m_pkthdr.fw_flags & IPFORWARD_MBUF_TAGGED) {
		/* Next hop */
		mtag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
		KKASSERT(mtag != NULL);
		next_hop = m_tag_data(mtag);
	}

	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		/* dummynet already filtered us */
		ip = mtod(m, struct ip *);
		hlen = IP_VHL_HL(ip->ip_vhl) << 2;
		goto iphack;
	}

	ipstat.ips_total++;

	/* length checks already done in ip_hashfn() */
	KASSERT(m->m_len >= sizeof(struct ip), ("IP header not in one mbuf"));

	if (IP_VHL_V(ip->ip_vhl) != IPVERSION) {
		ipstat.ips_badvers++;
		goto bad;
	}

	hlen = IP_VHL_HL(ip->ip_vhl) << 2;
	/* length checks already done in ip_hashfn() */
	KASSERT(hlen >= sizeof(struct ip), ("IP header len too small"));
	KASSERT(m->m_len >= hlen, ("complete IP header not in one mbuf"));

	/* 127/8 must not appear on wire - RFC1122 */
	if ((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
		if (!(m->m_pkthdr.rcvif->if_flags & IFF_LOOPBACK)) {
			ipstat.ips_badaddr++;
			goto bad;
		}
	}

	if (m->m_pkthdr.csum_flags & CSUM_IP_CHECKED) {
		sum = !(m->m_pkthdr.csum_flags & CSUM_IP_VALID);
	} else {
		if (hlen == sizeof(struct ip))
			sum = in_cksum_hdr(ip);
		else
			sum = in_cksum(m, hlen);
	}
	if (sum != 0) {
		ipstat.ips_badsum++;
		goto bad;
	}

#ifdef ALTQ
	if (altq_input != NULL && (*altq_input)(m, AF_INET) == 0) {
		/* packet is dropped by traffic conditioner */
		return;
	}
#endif
	/*
	 * Convert fields to host representation.
	 */
	ip->ip_len = ntohs(ip->ip_len);
	ip->ip_off = ntohs(ip->ip_off);

	/* length checks already done in ip_hashfn() */
	KASSERT(ip->ip_len >= hlen, ("total length less then header length"));
	KASSERT(m->m_pkthdr.len >= ip->ip_len, ("mbuf too short"));

	/*
	 * Trim mbufs if longer than the IP header would have us expect.
	 */
	if (m->m_pkthdr.len > ip->ip_len) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = ip->ip_len;
			m->m_pkthdr.len = ip->ip_len;
		} else {
			m_adj(m, ip->ip_len - m->m_pkthdr.len);
		}
	}
#if defined(IPSEC) && !defined(IPSEC_FILTERGIF)
	/*
	 * Bypass packet filtering for packets from a tunnel (gif).
	 */
	if (ipsec_gethist(m, NULL))
		goto pass;
#endif

	/*
	 * IpHack's section.
	 * Right now when no processing on packet has done
	 * and it is still fresh out of network we do our black
	 * deals with it.
	 * - Firewall: deny/allow/divert
	 * - Xlate: translate packet's addr/port (NAT).
	 * - Pipe: pass pkt through dummynet.
	 * - Wrap: fake packet's addr/port <unimpl.>
	 * - Encapsulate: put it in another IP and send out. <unimp.>
	 */

iphack:
	/*
	 * If we've been forwarded from the output side, then
	 * skip the firewall a second time
	 */
	if (next_hop != NULL)
		goto ours;

	/* No pfil hooks */
	if (!pfil_has_hooks(&inet_pfil_hook)) {
		if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
			/*
			 * Strip dummynet tags from stranded packets
			 */
			mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
			KKASSERT(mtag != NULL);
			m_tag_delete(m, mtag);
			m->m_pkthdr.fw_flags &= ~DUMMYNET_MBUF_TAGGED;
		}
		goto pass;
	}

	/*
	 * Run through list of hooks for input packets.
	 *
	 * NOTE!  If the packet is rewritten pf/ipfw/whoever must
	 *	  clear M_HASH.
	 */
	odst = ip->ip_dst;
	if (pfil_run_hooks(&inet_pfil_hook, &m, m->m_pkthdr.rcvif, PFIL_IN))
		return;
	if (m == NULL)	/* consumed by filter */
		return;
	ip = mtod(m, struct ip *);
	hlen = IP_VHL_HL(ip->ip_vhl) << 2;
	using_srcrt = (odst.s_addr != ip->ip_dst.s_addr);

	if (m->m_pkthdr.fw_flags & IPFORWARD_MBUF_TAGGED) {
		mtag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
		KKASSERT(mtag != NULL);
		next_hop = m_tag_data(mtag);
	}
	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		ip_dn_queue(m);
		return;
	}
	if (m->m_pkthdr.fw_flags & FW_MBUF_REDISPATCH) {
		m->m_pkthdr.fw_flags &= ~FW_MBUF_REDISPATCH;
	}
pass:
	/*
	 * Process options and, if not destined for us,
	 * ship it on.  ip_dooptions returns 1 when an
	 * error was detected (causing an icmp message
	 * to be sent and the original packet to be freed).
	 */
	if (hlen > sizeof(struct ip) && ip_dooptions(m, 0, next_hop))
		return;

	/* greedy RSVP, snatches any PATH packet of the RSVP protocol and no
	 * matter if it is destined to another node, or whether it is
	 * a multicast one, RSVP wants it! and prevents it from being forwarded
	 * anywhere else. Also checks if the rsvp daemon is running before
	 * grabbing the packet.
	 */
	if (rsvp_on && ip->ip_p == IPPROTO_RSVP)
		goto ours;

	/*
	 * Check our list of addresses, to see if the packet is for us.
	 * If we don't have any addresses, assume any unicast packet
	 * we receive might be for us (and let the upper layers deal
	 * with it).
	 */
	if (TAILQ_EMPTY(&in_ifaddrheads[mycpuid]) &&
	    !(m->m_flags & (M_MCAST | M_BCAST)))
		goto ours;

	/*
	 * Cache the destination address of the packet; this may be
	 * changed by use of 'ipfw fwd'.
	 */
	pkt_dst = next_hop ? next_hop->sin_addr : ip->ip_dst;

	/*
	 * Enable a consistency check between the destination address
	 * and the arrival interface for a unicast packet (the RFC 1122
	 * strong ES model) if IP forwarding is disabled and the packet
	 * is not locally generated and the packet is not subject to
	 * 'ipfw fwd'.
	 *
	 * XXX - Checking also should be disabled if the destination
	 * address is ipnat'ed to a different interface.
	 *
	 * XXX - Checking is incompatible with IP aliases added
	 * to the loopback interface instead of the interface where
	 * the packets are received.
	 */
	checkif = ip_checkinterface &&
		  !ipforwarding &&
		  m->m_pkthdr.rcvif != NULL &&
		  !(m->m_pkthdr.rcvif->if_flags & IFF_LOOPBACK) &&
		  next_hop == NULL;

	/*
	 * Check for exact addresses in the hash bucket.
	 */
	LIST_FOREACH(iac, INADDR_HASH(pkt_dst.s_addr), ia_hash) {
		ia = iac->ia;

		/*
		 * If the address matches, verify that the packet
		 * arrived via the correct interface if checking is
		 * enabled.
		 */
		if (IA_SIN(ia)->sin_addr.s_addr == pkt_dst.s_addr &&
		    (!checkif || ia->ia_ifp == m->m_pkthdr.rcvif))
			goto ours;
	}
	ia = NULL;

	/*
	 * Check for broadcast addresses.
	 *
	 * Only accept broadcast packets that arrive via the matching
	 * interface.  Reception of forwarded directed broadcasts would
	 * be handled via ip_forward() and ether_output() with the loopback
	 * into the stack for SIMPLEX interfaces handled by ether_output().
	 */
	if (m->m_pkthdr.rcvif != NULL &&
	    m->m_pkthdr.rcvif->if_flags & IFF_BROADCAST) {
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &m->m_pkthdr.rcvif->if_addrheads[mycpuid],
			      ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr == NULL) /* shutdown/startup race */
				continue;
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			if (satosin(&ia->ia_broadaddr)->sin_addr.s_addr ==
								pkt_dst.s_addr)
				goto ours;
			if (ia->ia_netbroadcast.s_addr == pkt_dst.s_addr)
				goto ours;
#ifdef BOOTP_COMPAT
			if (IA_SIN(ia)->sin_addr.s_addr == INADDR_ANY)
				goto ours;
#endif
		}
	}
	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		struct in_multi *inm;

		if (ip_mrouter != NULL) {
			/* XXX Multicast routing is not MPSAFE yet */
			get_mplock();

			/*
			 * If we are acting as a multicast router, all
			 * incoming multicast packets are passed to the
			 * kernel-level multicast forwarding function.
			 * The packet is returned (relatively) intact; if
			 * ip_mforward() returns a non-zero value, the packet
			 * must be discarded, else it may be accepted below.
			 */
			if (ip_mforward != NULL &&
			    ip_mforward(ip, m->m_pkthdr.rcvif, m, NULL) != 0) {
				rel_mplock();
				ipstat.ips_cantforward++;
				m_freem(m);
				return;
			}

			rel_mplock();

			/*
			 * The process-level routing daemon needs to receive
			 * all multicast IGMP packets, whether or not this
			 * host belongs to their destination groups.
			 */
			if (ip->ip_p == IPPROTO_IGMP)
				goto ours;
			ipstat.ips_forward++;
		}
		/*
		 * See if we belong to the destination multicast group on the
		 * arrival interface.
		 */
		inm = IN_LOOKUP_MULTI(&ip->ip_dst, m->m_pkthdr.rcvif);
		if (inm == NULL) {
			ipstat.ips_notmember++;
			m_freem(m);
			return;
		}
		goto ours;
	}
	if (ip->ip_dst.s_addr == INADDR_BROADCAST)
		goto ours;
	if (ip->ip_dst.s_addr == INADDR_ANY)
		goto ours;

	/*
	 * FAITH(Firewall Aided Internet Translator)
	 */
	if (m->m_pkthdr.rcvif && m->m_pkthdr.rcvif->if_type == IFT_FAITH) {
		if (ip_keepfaith) {
			if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_ICMP)
				goto ours;
		}
		m_freem(m);
		return;
	}

	/*
	 * Not for us; forward if possible and desirable.
	 */
	if (!ipforwarding) {
		ipstat.ips_cantforward++;
		m_freem(m);
	} else {
#ifdef IPSEC
		/*
		 * Enforce inbound IPsec SPD.
		 */
		if (ipsec4_in_reject(m, NULL)) {
			ipsecstat.in_polvio++;
			goto bad;
		}
#endif
#ifdef FAST_IPSEC
		mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
		crit_enter();
		if (mtag != NULL) {
			tdbi = (struct tdb_ident *)m_tag_data(mtag);
			sp = ipsec_getpolicy(tdbi, IPSEC_DIR_INBOUND);
		} else {
			sp = ipsec_getpolicybyaddr(m, IPSEC_DIR_INBOUND,
						   IP_FORWARDING, &error);
		}
		if (sp == NULL) {	/* NB: can happen if error */
			crit_exit();
			/*XXX error stat???*/
			DPRINTF(("ip_input: no SP for forwarding\n"));	/*XXX*/
			goto bad;
		}

		/*
		 * Check security policy against packet attributes.
		 */
		error = ipsec_in_reject(sp, m);
		KEY_FREESP(&sp);
		crit_exit();
		if (error) {
			ipstat.ips_cantforward++;
			goto bad;
		}
#endif
		ip_forward(m, using_srcrt, next_hop);
	}
	return;

ours:

	/*
	 * IPSTEALTH: Process non-routing options only
	 * if the packet is destined for us.
	 */
	if (ipstealth &&
	    hlen > sizeof(struct ip) &&
	    ip_dooptions(m, 1, next_hop))
		return;

	/* Count the packet in the ip address stats */
	if (ia != NULL) {
		IFA_STAT_INC(&ia->ia_ifa, ipackets, 1);
		IFA_STAT_INC(&ia->ia_ifa, ibytes, m->m_pkthdr.len);
	}

	/*
	 * If offset or IP_MF are set, must reassemble.
	 * Otherwise, nothing need be done.
	 * (We could look in the reassembly queue to see
	 * if the packet was previously fragmented,
	 * but it's not worth the time; just let them time out.)
	 */
	if (ip->ip_off & (IP_MF | IP_OFFMASK)) {
		/*
		 * Attempt reassembly; if it succeeds, proceed.  ip_reass()
		 * will return a different mbuf.
		 *
		 * NOTE: ip_reass() returns m with M_HASH cleared to force
		 *	 us to recharacterize the packet.
		 */
		m = ip_reass(m);
		if (m == NULL)
			return;
		ip = mtod(m, struct ip *);

		/* Get the header length of the reassembled packet */
		hlen = IP_VHL_HL(ip->ip_vhl) << 2;
	} else {
		ip->ip_len -= hlen;
	}

#ifdef IPSEC
	/*
	 * enforce IPsec policy checking if we are seeing last header.
	 * note that we do not visit this with protocols with pcb layer
	 * code - like udp/tcp/raw ip.
	 */
	if ((inetsw[ip_protox[ip->ip_p]].pr_flags & PR_LASTHDR) &&
	    ipsec4_in_reject(m, NULL)) {
		ipsecstat.in_polvio++;
		goto bad;
	}
#endif
#if FAST_IPSEC
	/*
	 * enforce IPsec policy checking if we are seeing last header.
	 * note that we do not visit this with protocols with pcb layer
	 * code - like udp/tcp/raw ip.
	 */
	if (inetsw[ip_protox[ip->ip_p]].pr_flags & PR_LASTHDR) {
		/*
		 * Check if the packet has already had IPsec processing
		 * done.  If so, then just pass it along.  This tag gets
		 * set during AH, ESP, etc. input handling, before the
		 * packet is returned to the ip input queue for delivery.
		 */
		mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
		crit_enter();
		if (mtag != NULL) {
			tdbi = (struct tdb_ident *)m_tag_data(mtag);
			sp = ipsec_getpolicy(tdbi, IPSEC_DIR_INBOUND);
		} else {
			sp = ipsec_getpolicybyaddr(m, IPSEC_DIR_INBOUND,
						   IP_FORWARDING, &error);
		}
		if (sp != NULL) {
			/*
			 * Check security policy against packet attributes.
			 */
			error = ipsec_in_reject(sp, m);
			KEY_FREESP(&sp);
		} else {
			/* XXX error stat??? */
			error = EINVAL;
DPRINTF(("ip_input: no SP, packet discarded\n"));/*XXX*/
			crit_exit();
			goto bad;
		}
		crit_exit();
		if (error)
			goto bad;
	}
#endif /* FAST_IPSEC */

	/*
	 * We must forward the packet to the correct protocol thread if
	 * we are not already in it.
	 *
	 * NOTE: ip_len is now in host form.  ip_len is not adjusted
	 *	 further for protocol processing, instead we pass hlen
	 *	 to the protosw and let it deal with it.
	 */
	ipstat.ips_delivered++;

	if ((m->m_flags & M_HASH) == 0) {
#ifdef RSS_DEBUG
		atomic_add_long(&ip_rehash_count, 1);
#endif
		ip->ip_len = htons(ip->ip_len + hlen);
		ip->ip_off = htons(ip->ip_off);

		ip_hashfn(&m, 0, IP_MPORT_IN);
		if (m == NULL)
			return;

		ip = mtod(m, struct ip *);
		ip->ip_len = ntohs(ip->ip_len) - hlen;
		ip->ip_off = ntohs(ip->ip_off);
		KKASSERT(m->m_flags & M_HASH);
	}
	port = netisr_hashport(m->m_pkthdr.hash);

	if (port != &curthread->td_msgport) {
		struct netmsg_packet *pmsg;

#ifdef RSS_DEBUG
		atomic_add_long(&ip_dispatch_slow, 1);
#endif

		pmsg = &m->m_hdr.mh_netmsg;
		netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
			    0, transport_processing_handler);
		pmsg->nm_packet = m;
		pmsg->base.lmsg.u.ms_result = hlen;
		lwkt_sendmsg(port, &pmsg->base.lmsg);
	} else {
#ifdef RSS_DEBUG
		atomic_add_long(&ip_dispatch_fast, 1);
#endif
		transport_processing_oncpu(m, hlen, ip);
	}
	return;

bad:
	m_freem(m);
}

/*
 * Take incoming datagram fragment and try to reassemble it into
 * whole datagram.  If a chain for reassembly of this datagram already
 * exists, then it is given as fp; otherwise have to make a chain.
 */
struct mbuf *
ip_reass(struct mbuf *m)
{
	struct ip *ip = mtod(m, struct ip *);
	struct mbuf *p = NULL, *q, *nq;
	struct mbuf *n;
	struct ipq *fp = NULL;
	struct ipqhead *head;
	int hlen = IP_VHL_HL(ip->ip_vhl) << 2;
	int i, next;
	u_short sum;

	/* If maxnipq is 0, never accept fragments. */
	if (maxnipq == 0) {
		ipstat.ips_fragments++;
		ipstat.ips_fragdropped++;
		m_freem(m);
		return NULL;
	}

	sum = IPREASS_HASH(ip->ip_src.s_addr, ip->ip_id);
	/*
	 * Look for queue of fragments of this datagram.
	 */
	lwkt_gettoken(&ipq_token);
	head = &ipq[sum];
	TAILQ_FOREACH(fp, head, ipq_list) {
		if (ip->ip_id == fp->ipq_id &&
		    ip->ip_src.s_addr == fp->ipq_src.s_addr &&
		    ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
		    ip->ip_p == fp->ipq_p)
			goto found;
	}

	fp = NULL;

	/*
	 * Enforce upper bound on number of fragmented packets
	 * for which we attempt reassembly;
	 * If maxnipq is -1, accept all fragments without limitation.
	 */
	if (nipq > maxnipq && maxnipq > 0) {
		/*
		 * drop something from the tail of the current queue
		 * before proceeding further
		 */
		struct ipq *q = TAILQ_LAST(head, ipqhead);
		if (q == NULL) {
			/*
			 * The current queue is empty,
			 * so drop from one of the others.
			 */
			for (i = 0; i < IPREASS_NHASH; i++) {
				struct ipq *r = TAILQ_LAST(&ipq[i], ipqhead);
				if (r) {
					ipstat.ips_fragtimeout += r->ipq_nfrags;
					ip_freef(&ipq[i], r);
					break;
				}
			}
		} else {
			ipstat.ips_fragtimeout += q->ipq_nfrags;
			ip_freef(head, q);
		}
	}
found:
	/*
	 * Adjust ip_len to not reflect header,
	 * convert offset of this to bytes.
	 */
	ip->ip_len -= hlen;
	if (ip->ip_off & IP_MF) {
		/*
		 * Make sure that fragments have a data length
		 * that's a non-zero multiple of 8 bytes.
		 */
		if (ip->ip_len == 0 || (ip->ip_len & 0x7) != 0) {
			ipstat.ips_toosmall++; /* XXX */
			m_freem(m);
			goto done;
		}
		m->m_flags |= M_FRAG;
	} else {
		m->m_flags &= ~M_FRAG;
	}
	ip->ip_off <<= 3;

	ipstat.ips_fragments++;
	m->m_pkthdr.header = ip;

	/*
	 * If the hardware has not done csum over this fragment
	 * then csum_data is not valid at all.
	 */
	if ((m->m_pkthdr.csum_flags & (CSUM_FRAG_NOT_CHECKED | CSUM_DATA_VALID))
	    == (CSUM_FRAG_NOT_CHECKED | CSUM_DATA_VALID)) {
		m->m_pkthdr.csum_data = 0;
		m->m_pkthdr.csum_flags &= ~(CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
	}

	/*
	 * Presence of header sizes in mbufs
	 * would confuse code below.
	 */
	m->m_data += hlen;
	m->m_len -= hlen;

	/*
	 * If first fragment to arrive, create a reassembly queue.
	 */
	if (fp == NULL) {
		if ((fp = mpipe_alloc_nowait(&ipq_mpipe)) == NULL)
			goto dropfrag;
		TAILQ_INSERT_HEAD(head, fp, ipq_list);
		nipq++;
		fp->ipq_nfrags = 1;
		fp->ipq_ttl = IPFRAGTTL;
		fp->ipq_p = ip->ip_p;
		fp->ipq_id = ip->ip_id;
		fp->ipq_src = ip->ip_src;
		fp->ipq_dst = ip->ip_dst;
		fp->ipq_frags = m;
		m->m_nextpkt = NULL;
		goto inserted;
	} else {
		fp->ipq_nfrags++;
	}

#define	GETIP(m)	((struct ip*)((m)->m_pkthdr.header))

	/*
	 * Find a segment which begins after this one does.
	 */
	for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt) {
		if (GETIP(q)->ip_off > ip->ip_off)
			break;
	}

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us, otherwise
	 * stick new segment in the proper place.
	 *
	 * If some of the data is dropped from the the preceding
	 * segment, then it's checksum is invalidated.
	 */
	if (p) {
		i = GETIP(p)->ip_off + GETIP(p)->ip_len - ip->ip_off;
		if (i > 0) {
			if (i >= ip->ip_len)
				goto dropfrag;
			m_adj(m, i);
			m->m_pkthdr.csum_flags = 0;
			ip->ip_off += i;
			ip->ip_len -= i;
		}
		m->m_nextpkt = p->m_nextpkt;
		p->m_nextpkt = m;
	} else {
		m->m_nextpkt = fp->ipq_frags;
		fp->ipq_frags = m;
	}

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	for (; q != NULL && ip->ip_off + ip->ip_len > GETIP(q)->ip_off;
	     q = nq) {
		i = (ip->ip_off + ip->ip_len) - GETIP(q)->ip_off;
		if (i < GETIP(q)->ip_len) {
			GETIP(q)->ip_len -= i;
			GETIP(q)->ip_off += i;
			m_adj(q, i);
			q->m_pkthdr.csum_flags = 0;
			break;
		}
		nq = q->m_nextpkt;
		m->m_nextpkt = nq;
		ipstat.ips_fragdropped++;
		fp->ipq_nfrags--;
		q->m_nextpkt = NULL;
		m_freem(q);
	}

inserted:
	/*
	 * Check for complete reassembly and perform frag per packet
	 * limiting.
	 *
	 * Frag limiting is performed here so that the nth frag has
	 * a chance to complete the packet before we drop the packet.
	 * As a result, n+1 frags are actually allowed per packet, but
	 * only n will ever be stored. (n = maxfragsperpacket.)
	 *
	 */
	next = 0;
	for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt) {
		if (GETIP(q)->ip_off != next) {
			if (fp->ipq_nfrags > maxfragsperpacket) {
				ipstat.ips_fragdropped += fp->ipq_nfrags;
				ip_freef(head, fp);
			}
			goto done;
		}
		next += GETIP(q)->ip_len;
	}
	/* Make sure the last packet didn't have the IP_MF flag */
	if (p->m_flags & M_FRAG) {
		if (fp->ipq_nfrags > maxfragsperpacket) {
			ipstat.ips_fragdropped += fp->ipq_nfrags;
			ip_freef(head, fp);
		}
		goto done;
	}

	/*
	 * Reassembly is complete.  Make sure the packet is a sane size.
	 */
	q = fp->ipq_frags;
	ip = GETIP(q);
	if (next + (IP_VHL_HL(ip->ip_vhl) << 2) > IP_MAXPACKET) {
		ipstat.ips_toolong++;
		ipstat.ips_fragdropped += fp->ipq_nfrags;
		ip_freef(head, fp);
		goto done;
	}

	/*
	 * Concatenate fragments.
	 */
	m = q;
	n = m->m_next;
	m->m_next = NULL;
	m_cat(m, n);
	nq = q->m_nextpkt;
	q->m_nextpkt = NULL;
	for (q = nq; q != NULL; q = nq) {
		nq = q->m_nextpkt;
		q->m_nextpkt = NULL;
		m->m_pkthdr.csum_flags &= q->m_pkthdr.csum_flags;
		m->m_pkthdr.csum_data += q->m_pkthdr.csum_data;
		m_cat(m, q);
	}

	/*
	 * Clean up the 1's complement checksum.  Carry over 16 bits must
	 * be added back.  This assumes no more then 65535 packet fragments
	 * were reassembled.  A second carry can also occur (but not a third).
	 */
	m->m_pkthdr.csum_data = (m->m_pkthdr.csum_data & 0xffff) +
				(m->m_pkthdr.csum_data >> 16);
	if (m->m_pkthdr.csum_data > 0xFFFF)
		m->m_pkthdr.csum_data -= 0xFFFF;

	/*
	 * Create header for new ip packet by
	 * modifying header of first packet;
	 * dequeue and discard fragment reassembly header.
	 * Make header visible.
	 */
	ip->ip_len = next;
	ip->ip_src = fp->ipq_src;
	ip->ip_dst = fp->ipq_dst;
	TAILQ_REMOVE(head, fp, ipq_list);
	nipq--;
	mpipe_free(&ipq_mpipe, fp);
	m->m_len += (IP_VHL_HL(ip->ip_vhl) << 2);
	m->m_data -= (IP_VHL_HL(ip->ip_vhl) << 2);
	/* some debugging cruft by sklower, below, will go away soon */
	if (m->m_flags & M_PKTHDR) { /* XXX this should be done elsewhere */
		int plen = 0;

		for (n = m; n; n = n->m_next)
			plen += n->m_len;
		m->m_pkthdr.len = plen;
	}

	/*
	 * Reassembly complete, return the next protocol.
	 *
	 * Be sure to clear M_HASH to force the packet
	 * to be re-characterized.
	 *
	 * Clear M_FRAG, we are no longer a fragment.
	 */
	m->m_flags &= ~(M_HASH | M_FRAG);

	ipstat.ips_reassembled++;
	lwkt_reltoken(&ipq_token);
	return (m);

dropfrag:
	ipstat.ips_fragdropped++;
	if (fp != NULL)
		fp->ipq_nfrags--;
	m_freem(m);
done:
	lwkt_reltoken(&ipq_token);
	return (NULL);

#undef GETIP
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 *
 * Called with ipq_token held.
 */
static void
ip_freef(struct ipqhead *fhp, struct ipq *fp)
{
	struct mbuf *q;

	/*
	 * Remove first to protect against blocking
	 */
	TAILQ_REMOVE(fhp, fp, ipq_list);

	/*
	 * Clean out at our leisure
	 */
	while (fp->ipq_frags) {
		q = fp->ipq_frags;
		fp->ipq_frags = q->m_nextpkt;
		q->m_nextpkt = NULL;
		m_freem(q);
	}
	mpipe_free(&ipq_mpipe, fp);
	nipq--;
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
ip_slowtimo(void)
{
	struct ipq *fp, *fp_temp;
	struct ipqhead *head;
	int i;

	lwkt_gettoken(&ipq_token);
	for (i = 0; i < IPREASS_NHASH; i++) {
		head = &ipq[i];
		TAILQ_FOREACH_MUTABLE(fp, head, ipq_list, fp_temp) {
			if (--fp->ipq_ttl == 0) {
				ipstat.ips_fragtimeout += fp->ipq_nfrags;
				ip_freef(head, fp);
			}
		}
	}
	/*
	 * If we are over the maximum number of fragments
	 * (due to the limit being lowered), drain off
	 * enough to get down to the new limit.
	 */
	if (maxnipq >= 0 && nipq > maxnipq) {
		for (i = 0; i < IPREASS_NHASH; i++) {
			head = &ipq[i];
			while (nipq > maxnipq && !TAILQ_EMPTY(head)) {
				ipstat.ips_fragdropped +=
				    TAILQ_FIRST(head)->ipq_nfrags;
				ip_freef(head, TAILQ_FIRST(head));
			}
		}
	}
	lwkt_reltoken(&ipq_token);
	ipflow_slowtimo();
}

/*
 * Drain off all datagram fragments.
 */
void
ip_drain(void)
{
	struct ipqhead *head;
	int i;

	lwkt_gettoken(&ipq_token);
	for (i = 0; i < IPREASS_NHASH; i++) {
		head = &ipq[i];
		while (!TAILQ_EMPTY(head)) {
			ipstat.ips_fragdropped += TAILQ_FIRST(head)->ipq_nfrags;
			ip_freef(head, TAILQ_FIRST(head));
		}
	}
	lwkt_reltoken(&ipq_token);
	in_rtqdrain();
}

/*
 * Do option processing on a datagram,
 * possibly discarding it if bad options are encountered,
 * or forwarding it if source-routed.
 * The pass argument is used when operating in the IPSTEALTH
 * mode to tell what options to process:
 * [LS]SRR (pass 0) or the others (pass 1).
 * The reason for as many as two passes is that when doing IPSTEALTH,
 * non-routing options should be processed only if the packet is for us.
 * Returns 1 if packet has been forwarded/freed,
 * 0 if the packet should be processed further.
 */
static int
ip_dooptions(struct mbuf *m, int pass, struct sockaddr_in *next_hop)
{
	struct sockaddr_in ipaddr = { sizeof ipaddr, AF_INET };
	struct ip *ip = mtod(m, struct ip *);
	u_char *cp;
	struct in_ifaddr *ia;
	int opt, optlen, cnt, off, code, type = ICMP_PARAMPROB;
	boolean_t forward = FALSE;
	struct in_addr *sin, dst;
	n_time ntime;

	dst = ip->ip_dst;
	cp = (u_char *)(ip + 1);
	cnt = (IP_VHL_HL(ip->ip_vhl) << 2) - sizeof(struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < IPOPT_OLEN + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
		}
		switch (opt) {

		default:
			break;

		/*
		 * Source routing with record.
		 * Find interface with current destination address.
		 * If none on this machine then drop if strictly routed,
		 * or do nothing if loosely routed.
		 * Record interface address and bring up next address
		 * component.  If strictly routed make sure next
		 * address is on directly accessible net.
		 */
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			if (ipstealth && pass > 0)
				break;
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			ipaddr.sin_addr = ip->ip_dst;
			ia = (struct in_ifaddr *)
				ifa_ifwithaddr((struct sockaddr *)&ipaddr);
			if (ia == NULL) {
				if (opt == IPOPT_SSRR) {
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				}
				if (!ip_dosourceroute)
					goto nosourcerouting;
				/*
				 * Loose routing, and not at next destination
				 * yet; nothing to do except forward.
				 */
				break;
			}
			off--;			/* 0 origin */
			if (off > optlen - (int)sizeof(struct in_addr)) {
				/*
				 * End of source route.  Should be for us.
				 */
				if (!ip_acceptsourceroute)
					goto nosourcerouting;
				save_rte(m, cp, ip->ip_src);
				break;
			}
			if (ipstealth)
				goto dropit;
			if (!ip_dosourceroute) {
				if (ipforwarding) {
					char buf[sizeof "aaa.bbb.ccc.ddd"];

					/*
					 * Acting as a router, so generate ICMP
					 */
nosourcerouting:
					strcpy(buf, inet_ntoa(ip->ip_dst));
					log(LOG_WARNING,
					    "attempted source route from %s to %s\n",
					    inet_ntoa(ip->ip_src), buf);
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				} else {
					/*
					 * Not acting as a router,
					 * so silently drop.
					 */
dropit:
					ipstat.ips_cantforward++;
					m_freem(m);
					return (1);
				}
			}

			/*
			 * locate outgoing interface
			 */
			memcpy(&ipaddr.sin_addr, cp + off,
			    sizeof ipaddr.sin_addr);

			if (opt == IPOPT_SSRR) {
#define	INA	struct in_ifaddr *
#define	SA	struct sockaddr *
				if ((ia = (INA)ifa_ifwithdstaddr((SA)&ipaddr))
									== NULL)
					ia = (INA)ifa_ifwithnet((SA)&ipaddr);
			} else {
				ia = ip_rtaddr(ipaddr.sin_addr, NULL);
			}
			if (ia == NULL) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_SRCFAIL;
				goto bad;
			}
			ip->ip_dst = ipaddr.sin_addr;
			memcpy(cp + off, &IA_SIN(ia)->sin_addr,
			    sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			/*
			 * Let ip_intr's mcast routing check handle mcast pkts
			 */
			forward = !IN_MULTICAST(ntohl(ip->ip_dst.s_addr));
			break;

		case IPOPT_RR:
			if (ipstealth && pass == 0)
				break;
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			/*
			 * If no space remains, ignore.
			 */
			off--;			/* 0 origin */
			if (off > optlen - (int)sizeof(struct in_addr))
				break;
			memcpy(&ipaddr.sin_addr, &ip->ip_dst,
			    sizeof ipaddr.sin_addr);
			/*
			 * locate outgoing interface; if we're the destination,
			 * use the incoming interface (should be same).
			 */
			if ((ia = (INA)ifa_ifwithaddr((SA)&ipaddr)) == NULL &&
			    (ia = ip_rtaddr(ipaddr.sin_addr, NULL)) == NULL) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_HOST;
				goto bad;
			}
			memcpy(cp + off, &IA_SIN(ia)->sin_addr,
			    sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			break;

		case IPOPT_TS:
			if (ipstealth && pass == 0)
				break;
			code = cp - (u_char *)ip;
			if (optlen < 4 || optlen > 40) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < 5) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if (off > optlen - (int)sizeof(int32_t)) {
				cp[IPOPT_OFFSET + 1] += (1 << 4);
				if ((cp[IPOPT_OFFSET + 1] & 0xf0) == 0) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				break;
			}
			off--;				/* 0 origin */
			sin = (struct in_addr *)(cp + off);
			switch (cp[IPOPT_OFFSET + 1] & 0x0f) {

			case IPOPT_TS_TSONLY:
				break;

			case IPOPT_TS_TSANDADDR:
				if (off + sizeof(n_time) +
				    sizeof(struct in_addr) > optlen) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				ipaddr.sin_addr = dst;
				ia = (INA)ifaof_ifpforaddr((SA)&ipaddr,
							    m->m_pkthdr.rcvif);
				if (ia == NULL)
					continue;
				memcpy(sin, &IA_SIN(ia)->sin_addr,
				    sizeof(struct in_addr));
				cp[IPOPT_OFFSET] += sizeof(struct in_addr);
				off += sizeof(struct in_addr);
				break;

			case IPOPT_TS_PRESPEC:
				if (off + sizeof(n_time) +
				    sizeof(struct in_addr) > optlen) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				memcpy(&ipaddr.sin_addr, sin,
				    sizeof(struct in_addr));
				if (ifa_ifwithaddr((SA)&ipaddr) == NULL)
					continue;
				cp[IPOPT_OFFSET] += sizeof(struct in_addr);
				off += sizeof(struct in_addr);
				break;

			default:
				code = &cp[IPOPT_OFFSET + 1] - (u_char *)ip;
				goto bad;
			}
			ntime = iptime();
			memcpy(cp + off, &ntime, sizeof(n_time));
			cp[IPOPT_OFFSET] += sizeof(n_time);
		}
	}
	if (forward && ipforwarding) {
		ip_forward(m, TRUE, next_hop);
		return (1);
	}
	return (0);
bad:
	icmp_error(m, type, code, 0, 0);
	ipstat.ips_badoptions++;
	return (1);
}

/*
 * Given address of next destination (final or next hop),
 * return internet address info of interface to be used to get there.
 */
struct in_ifaddr *
ip_rtaddr(struct in_addr dst, struct route *ro0)
{
	struct route sro, *ro;
	struct sockaddr_in *sin;
	struct in_ifaddr *ia;

	if (ro0 != NULL) {
		ro = ro0;
	} else {
		bzero(&sro, sizeof(sro));
		ro = &sro;
	}

	sin = (struct sockaddr_in *)&ro->ro_dst;

	if (ro->ro_rt == NULL || dst.s_addr != sin->sin_addr.s_addr) {
		if (ro->ro_rt != NULL) {
			RTFREE(ro->ro_rt);
			ro->ro_rt = NULL;
		}
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof *sin;
		sin->sin_addr = dst;
		rtalloc_ign(ro, RTF_PRCLONING);
	}

	if (ro->ro_rt == NULL)
		return (NULL);

	ia = ifatoia(ro->ro_rt->rt_ifa);

	if (ro == &sro)
		RTFREE(ro->ro_rt);
	return ia;
}

/*
 * Save incoming source route for use in replies,
 * to be picked up later by ip_srcroute if the receiver is interested.
 */
static void
save_rte(struct mbuf *m, u_char *option, struct in_addr dst)
{
	struct m_tag *mtag;
	struct ip_srcrt_opt *opt;
	unsigned olen;

	mtag = m_tag_get(PACKET_TAG_IPSRCRT, sizeof(*opt), MB_DONTWAIT);
	if (mtag == NULL)
		return;
	opt = m_tag_data(mtag);

	olen = option[IPOPT_OLEN];
#ifdef DIAGNOSTIC
	if (ipprintfs)
		kprintf("save_rte: olen %d\n", olen);
#endif
	if (olen > sizeof(opt->ip_srcrt) - (1 + sizeof(dst))) {
		m_tag_free(mtag);
		return;
	}
	bcopy(option, opt->ip_srcrt.srcopt, olen);
	opt->ip_nhops = (olen - IPOPT_OFFSET - 1) / sizeof(struct in_addr);
	opt->ip_srcrt.dst = dst;
	m_tag_prepend(m, mtag);
}

/*
 * Retrieve incoming source route for use in replies,
 * in the same form used by setsockopt.
 * The first hop is placed before the options, will be removed later.
 */
struct mbuf *
ip_srcroute(struct mbuf *m0)
{
	struct in_addr *p, *q;
	struct mbuf *m;
	struct m_tag *mtag;
	struct ip_srcrt_opt *opt;

	if (m0 == NULL)
		return NULL;

	mtag = m_tag_find(m0, PACKET_TAG_IPSRCRT, NULL);
	if (mtag == NULL)
		return NULL;
	opt = m_tag_data(mtag);

	if (opt->ip_nhops == 0)
		return (NULL);
	m = m_get(MB_DONTWAIT, MT_HEADER);
	if (m == NULL)
		return (NULL);

#define	OPTSIZ	(sizeof(opt->ip_srcrt.nop) + sizeof(opt->ip_srcrt.srcopt))

	/* length is (nhops+1)*sizeof(addr) + sizeof(nop + srcrt header) */
	m->m_len = opt->ip_nhops * sizeof(struct in_addr) +
		   sizeof(struct in_addr) + OPTSIZ;
#ifdef DIAGNOSTIC
	if (ipprintfs) {
		kprintf("ip_srcroute: nhops %d mlen %d",
			opt->ip_nhops, m->m_len);
	}
#endif

	/*
	 * First save first hop for return route
	 */
	p = &opt->ip_srcrt.route[opt->ip_nhops - 1];
	*(mtod(m, struct in_addr *)) = *p--;
#ifdef DIAGNOSTIC
	if (ipprintfs)
		kprintf(" hops %x", ntohl(mtod(m, struct in_addr *)->s_addr));
#endif

	/*
	 * Copy option fields and padding (nop) to mbuf.
	 */
	opt->ip_srcrt.nop = IPOPT_NOP;
	opt->ip_srcrt.srcopt[IPOPT_OFFSET] = IPOPT_MINOFF;
	memcpy(mtod(m, caddr_t) + sizeof(struct in_addr), &opt->ip_srcrt.nop,
	    OPTSIZ);
	q = (struct in_addr *)(mtod(m, caddr_t) +
	    sizeof(struct in_addr) + OPTSIZ);
#undef OPTSIZ
	/*
	 * Record return path as an IP source route,
	 * reversing the path (pointers are now aligned).
	 */
	while (p >= opt->ip_srcrt.route) {
#ifdef DIAGNOSTIC
		if (ipprintfs)
			kprintf(" %x", ntohl(q->s_addr));
#endif
		*q++ = *p--;
	}
	/*
	 * Last hop goes to final destination.
	 */
	*q = opt->ip_srcrt.dst;
	m_tag_delete(m0, mtag);
#ifdef DIAGNOSTIC
	if (ipprintfs)
		kprintf(" %x\n", ntohl(q->s_addr));
#endif
	return (m);
}

/*
 * Strip out IP options.
 */
void
ip_stripoptions(struct mbuf *m)
{
	int datalen;
	struct ip *ip = mtod(m, struct ip *);
	caddr_t opts;
	int optlen;

	optlen = (IP_VHL_HL(ip->ip_vhl) << 2) - sizeof(struct ip);
	opts = (caddr_t)(ip + 1);
	datalen = m->m_len - (sizeof(struct ip) + optlen);
	bcopy(opts + optlen, opts, datalen);
	m->m_len -= optlen;
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len -= optlen;
	ip->ip_vhl = IP_MAKE_VHL(IPVERSION, sizeof(struct ip) >> 2);
}

u_char inetctlerrmap[PRC_NCMDS] = {
	0,		0,		0,		0,
	0,		EMSGSIZE,	EHOSTDOWN,	EHOSTUNREACH,
	EHOSTUNREACH,	EHOSTUNREACH,	ECONNREFUSED,	ECONNREFUSED,
	EMSGSIZE,	EHOSTUNREACH,	0,		0,
	0,		0,		0,		0,
	ENOPROTOOPT,	ECONNREFUSED
};

/*
 * Forward a packet.  If some error occurs return the sender
 * an icmp packet.  Note we can't always generate a meaningful
 * icmp message because icmp doesn't have a large enough repertoire
 * of codes and types.
 *
 * If not forwarding, just drop the packet.  This could be confusing
 * if ipforwarding was zero but some routing protocol was advancing
 * us as a gateway to somewhere.  However, we must let the routing
 * protocol deal with that.
 *
 * The using_srcrt parameter indicates whether the packet is being forwarded
 * via a source route.
 */
void
ip_forward(struct mbuf *m, boolean_t using_srcrt, struct sockaddr_in *next_hop)
{
	struct ip *ip = mtod(m, struct ip *);
	struct rtentry *rt;
	struct route fwd_ro;
	int error, type = 0, code = 0, destmtu = 0;
	struct mbuf *mcopy, *mtemp = NULL;
	n_long dest;
	struct in_addr pkt_dst;

	dest = INADDR_ANY;
	/*
	 * Cache the destination address of the packet; this may be
	 * changed by use of 'ipfw fwd'.
	 */
	pkt_dst = (next_hop != NULL) ? next_hop->sin_addr : ip->ip_dst;

#ifdef DIAGNOSTIC
	if (ipprintfs)
		kprintf("forward: src %x dst %x ttl %x\n",
		       ip->ip_src.s_addr, pkt_dst.s_addr, ip->ip_ttl);
#endif

	if (m->m_flags & (M_BCAST | M_MCAST) || !in_canforward(pkt_dst)) {
		ipstat.ips_cantforward++;
		m_freem(m);
		return;
	}
	if (!ipstealth && ip->ip_ttl <= IPTTLDEC) {
		icmp_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, dest, 0);
		return;
	}

	bzero(&fwd_ro, sizeof(fwd_ro));
	ip_rtaddr(pkt_dst, &fwd_ro);
	if (fwd_ro.ro_rt == NULL) {
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, dest, 0);
		return;
	}
	rt = fwd_ro.ro_rt;

	if (curthread->td_type == TD_TYPE_NETISR) {
		/*
		 * Save the IP header and at most 8 bytes of the payload,
		 * in case we need to generate an ICMP message to the src.
		 */
		mtemp = ipforward_mtemp[mycpuid];
		KASSERT((mtemp->m_flags & M_EXT) == 0 &&
		    mtemp->m_data == mtemp->m_pktdat &&
		    m_tag_first(mtemp) == NULL,
		    ("ip_forward invalid mtemp1"));

		if (!m_dup_pkthdr(mtemp, m, MB_DONTWAIT)) {
			/*
			 * It's probably ok if the pkthdr dup fails (because
			 * the deep copy of the tag chain failed), but for now
			 * be conservative and just discard the copy since
			 * code below may some day want the tags.
			 */
			mtemp = NULL;
		} else {
			mtemp->m_type = m->m_type;
			mtemp->m_len = imin((IP_VHL_HL(ip->ip_vhl) << 2) + 8,
			    (int)ip->ip_len);
			mtemp->m_pkthdr.len = mtemp->m_len;
			m_copydata(m, 0, mtemp->m_len, mtod(mtemp, caddr_t));
		}
	}

	if (!ipstealth)
		ip->ip_ttl -= IPTTLDEC;

	/*
	 * If forwarding packet using same interface that it came in on,
	 * perhaps should send a redirect to sender to shortcut a hop.
	 * Only send redirect if source is sending directly to us,
	 * and if packet was not source routed (or has any options).
	 * Also, don't send redirect if forwarding using a default route
	 * or a route modified by a redirect.
	 */
	if (rt->rt_ifp == m->m_pkthdr.rcvif &&
	    !(rt->rt_flags & (RTF_DYNAMIC | RTF_MODIFIED)) &&
	    satosin(rt_key(rt))->sin_addr.s_addr != INADDR_ANY &&
	    ipsendredirects && !using_srcrt && next_hop == NULL) {
		u_long src = ntohl(ip->ip_src.s_addr);
		struct in_ifaddr *rt_ifa = (struct in_ifaddr *)rt->rt_ifa;

		if (rt_ifa != NULL &&
		    (src & rt_ifa->ia_subnetmask) == rt_ifa->ia_subnet) {
			if (rt->rt_flags & RTF_GATEWAY)
				dest = satosin(rt->rt_gateway)->sin_addr.s_addr;
			else
				dest = pkt_dst.s_addr;
			/*
			 * Router requirements says to only send
			 * host redirects.
			 */
			type = ICMP_REDIRECT;
			code = ICMP_REDIRECT_HOST;
#ifdef DIAGNOSTIC
			if (ipprintfs)
				kprintf("redirect (%d) to %x\n", code, dest);
#endif
		}
	}

	error = ip_output(m, NULL, &fwd_ro, IP_FORWARDING, NULL, NULL);
	if (error == 0) {
		ipstat.ips_forward++;
		if (type == 0) {
			if (mtemp)
				ipflow_create(&fwd_ro, mtemp);
			goto done;
		} else {
			ipstat.ips_redirectsent++;
		}
	} else {
		ipstat.ips_cantforward++;
	}

	if (mtemp == NULL)
		goto done;

	/*
	 * Errors that do not require generating ICMP message
	 */
	switch (error) {
	case ENOBUFS:
		/*
		 * A router should not generate ICMP_SOURCEQUENCH as
		 * required in RFC1812 Requirements for IP Version 4 Routers.
		 * Source quench could be a big problem under DoS attacks,
		 * or if the underlying interface is rate-limited.
		 * Those who need source quench packets may re-enable them
		 * via the net.inet.ip.sendsourcequench sysctl.
		 */
		if (!ip_sendsourcequench)
			goto done;
		break;

	case EACCES:			/* ipfw denied packet */
		goto done;
	}

	KASSERT((mtemp->m_flags & M_EXT) == 0 &&
	    mtemp->m_data == mtemp->m_pktdat,
	    ("ip_forward invalid mtemp2"));
	mcopy = m_copym(mtemp, 0, mtemp->m_len, MB_DONTWAIT);
	if (mcopy == NULL)
		goto done;

	/*
	 * Send ICMP message.
	 */
	switch (error) {
	case 0:				/* forwarded, but need redirect */
		/* type, code set above */
		break;

	case ENETUNREACH:		/* shouldn't happen, checked above */
	case EHOSTUNREACH:
	case ENETDOWN:
	case EHOSTDOWN:
	default:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_HOST;
		break;

	case EMSGSIZE:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_NEEDFRAG;
#ifdef IPSEC
		/*
		 * If the packet is routed over IPsec tunnel, tell the
		 * originator the tunnel MTU.
		 *	tunnel MTU = if MTU - sizeof(IP) - ESP/AH hdrsiz
		 * XXX quickhack!!!
		 */
		if (fwd_ro.ro_rt != NULL) {
			struct secpolicy *sp = NULL;
			int ipsecerror;
			int ipsechdr;
			struct route *ro;

			sp = ipsec4_getpolicybyaddr(mcopy,
						    IPSEC_DIR_OUTBOUND,
						    IP_FORWARDING,
						    &ipsecerror);

			if (sp == NULL)
				destmtu = fwd_ro.ro_rt->rt_ifp->if_mtu;
			else {
				/* count IPsec header size */
				ipsechdr = ipsec4_hdrsiz(mcopy,
							 IPSEC_DIR_OUTBOUND,
							 NULL);

				/*
				 * find the correct route for outer IPv4
				 * header, compute tunnel MTU.
				 *
				 */
				if (sp->req != NULL && sp->req->sav != NULL &&
				    sp->req->sav->sah != NULL) {
					ro = &sp->req->sav->sah->sa_route;
					if (ro->ro_rt != NULL &&
					    ro->ro_rt->rt_ifp != NULL) {
						destmtu =
						    ro->ro_rt->rt_ifp->if_mtu;
						destmtu -= ipsechdr;
					}
				}

				key_freesp(sp);
			}
		}
#elif FAST_IPSEC
		/*
		 * If the packet is routed over IPsec tunnel, tell the
		 * originator the tunnel MTU.
		 *	tunnel MTU = if MTU - sizeof(IP) - ESP/AH hdrsiz
		 * XXX quickhack!!!
		 */
		if (fwd_ro.ro_rt != NULL) {
			struct secpolicy *sp = NULL;
			int ipsecerror;
			int ipsechdr;
			struct route *ro;

			sp = ipsec_getpolicybyaddr(mcopy,
						   IPSEC_DIR_OUTBOUND,
						   IP_FORWARDING,
						   &ipsecerror);

			if (sp == NULL)
				destmtu = fwd_ro.ro_rt->rt_ifp->if_mtu;
			else {
				/* count IPsec header size */
				ipsechdr = ipsec4_hdrsiz(mcopy,
							 IPSEC_DIR_OUTBOUND,
							 NULL);

				/*
				 * find the correct route for outer IPv4
				 * header, compute tunnel MTU.
				 */

				if (sp->req != NULL &&
				    sp->req->sav != NULL &&
				    sp->req->sav->sah != NULL) {
					ro = &sp->req->sav->sah->sa_route;
					if (ro->ro_rt != NULL &&
					    ro->ro_rt->rt_ifp != NULL) {
						destmtu =
						    ro->ro_rt->rt_ifp->if_mtu;
						destmtu -= ipsechdr;
					}
				}

				KEY_FREESP(&sp);
			}
		}
#else /* !IPSEC && !FAST_IPSEC */
		if (fwd_ro.ro_rt != NULL)
			destmtu = fwd_ro.ro_rt->rt_ifp->if_mtu;
#endif /*IPSEC*/
		ipstat.ips_cantfrag++;
		break;

	case ENOBUFS:
		type = ICMP_SOURCEQUENCH;
		code = 0;
		break;

	case EACCES:			/* ipfw denied packet */
		panic("ip_forward EACCES should not reach");
	}
	icmp_error(mcopy, type, code, dest, destmtu);
done:
	if (mtemp != NULL)
		m_tag_delete_chain(mtemp);
	if (fwd_ro.ro_rt != NULL)
		RTFREE(fwd_ro.ro_rt);
}

void
ip_savecontrol(struct inpcb *inp, struct mbuf **mp, struct ip *ip,
	       struct mbuf *m)
{
	if (inp->inp_socket->so_options & SO_TIMESTAMP) {
		struct timeval tv;

		microtime(&tv);
		*mp = sbcreatecontrol((caddr_t) &tv, sizeof(tv),
		    SCM_TIMESTAMP, SOL_SOCKET);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVDSTADDR) {
		*mp = sbcreatecontrol((caddr_t) &ip->ip_dst,
		    sizeof(struct in_addr), IP_RECVDSTADDR, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVTTL) {
		*mp = sbcreatecontrol((caddr_t) &ip->ip_ttl,
		    sizeof(u_char), IP_RECVTTL, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#ifdef notyet
	/* XXX
	 * Moving these out of udp_input() made them even more broken
	 * than they already were.
	 */
	/* options were tossed already */
	if (inp->inp_flags & INP_RECVOPTS) {
		*mp = sbcreatecontrol((caddr_t) opts_deleted_above,
		    sizeof(struct in_addr), IP_RECVOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	/* ip_srcroute doesn't do what we want here, need to fix */
	if (inp->inp_flags & INP_RECVRETOPTS) {
		*mp = sbcreatecontrol((caddr_t) ip_srcroute(m),
		    sizeof(struct in_addr), IP_RECVRETOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#endif
	if (inp->inp_flags & INP_RECVIF) {
		struct ifnet *ifp;
		struct sdlbuf {
			struct sockaddr_dl sdl;
			u_char	pad[32];
		} sdlbuf;
		struct sockaddr_dl *sdp;
		struct sockaddr_dl *sdl2 = &sdlbuf.sdl;

		if (((ifp = m->m_pkthdr.rcvif)) &&
		    ((ifp->if_index != 0) && (ifp->if_index <= if_index))) {
			sdp = IF_LLSOCKADDR(ifp);
			/*
			 * Change our mind and don't try copy.
			 */
			if ((sdp->sdl_family != AF_LINK) ||
			    (sdp->sdl_len > sizeof(sdlbuf))) {
				goto makedummy;
			}
			bcopy(sdp, sdl2, sdp->sdl_len);
		} else {
makedummy:
			sdl2->sdl_len =
			    offsetof(struct sockaddr_dl, sdl_data[0]);
			sdl2->sdl_family = AF_LINK;
			sdl2->sdl_index = 0;
			sdl2->sdl_nlen = sdl2->sdl_alen = sdl2->sdl_slen = 0;
		}
		*mp = sbcreatecontrol((caddr_t) sdl2, sdl2->sdl_len,
			IP_RECVIF, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
}

/*
 * XXX these routines are called from the upper part of the kernel.
 *
 * They could also be moved to ip_mroute.c, since all the RSVP
 *  handling is done there already.
 */
int
ip_rsvp_init(struct socket *so)
{
	if (so->so_type != SOCK_RAW ||
	    so->so_proto->pr_protocol != IPPROTO_RSVP)
		return EOPNOTSUPP;

	if (ip_rsvpd != NULL)
		return EADDRINUSE;

	ip_rsvpd = so;
	/*
	 * This may seem silly, but we need to be sure we don't over-increment
	 * the RSVP counter, in case something slips up.
	 */
	if (!ip_rsvp_on) {
		ip_rsvp_on = 1;
		rsvp_on++;
	}

	return 0;
}

int
ip_rsvp_done(void)
{
	ip_rsvpd = NULL;
	/*
	 * This may seem silly, but we need to be sure we don't over-decrement
	 * the RSVP counter, in case something slips up.
	 */
	if (ip_rsvp_on) {
		ip_rsvp_on = 0;
		rsvp_on--;
	}
	return 0;
}

int
rsvp_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;

	*mp = NULL;

	if (rsvp_input_p) { /* call the real one if loaded */
		*mp = m;
		rsvp_input_p(mp, offp, proto);
		return(IPPROTO_DONE);
	}

	/* Can still get packets with rsvp_on = 0 if there is a local member
	 * of the group to which the RSVP packet is addressed.  But in this
	 * case we want to throw the packet away.
	 */

	if (!rsvp_on) {
		m_freem(m);
		return(IPPROTO_DONE);
	}

	if (ip_rsvpd != NULL) {
		*mp = m;
		rip_input(mp, offp, proto);
		return(IPPROTO_DONE);
	}
	/* Drop the packet */
	m_freem(m);
	return(IPPROTO_DONE);
}
