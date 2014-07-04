/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by the 3am Software Foundry ("3am").  It was developed by Matt Thomas.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netinet/ip_flow.c,v 1.9.2.2 2001/11/04 17:35:31 luigi Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>
#include <sys/in_cksum.h>

#include <machine/smp.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_flow.h>

#define	IPFLOW_TIMER		(5 * PR_SLOWHZ)
#define IPFLOW_HASHBITS		6	/* should not be a multiple of 8 */
#define	IPFLOW_HASHSIZE		(1 << IPFLOW_HASHBITS)
#define	IPFLOW_MAX		256

#define IPFLOW_RTENTRY_ISDOWN(rt) \
	(((rt)->rt_flags & RTF_UP) == 0 || \
	 ((rt)->rt_ifp->if_flags & IFF_UP) == 0)

struct netmsg_ipfaddr {
	struct netmsg_base base;
	struct in_addr	ipf_addr;
};

struct ipflow {
	LIST_ENTRY(ipflow) ipf_hash;	/* next ipflow in hash bucket */
	LIST_ENTRY(ipflow) ipf_list;	/* next ipflow in list */

	struct in_addr ipf_dst;		/* destination address */
	struct in_addr ipf_src;		/* source address */
	uint8_t ipf_tos;		/* type-of-service */

	uint8_t ipf_flags;		/* see IPFLOW_FLAG_ */
	uint8_t ipf_pad[2];		/* explicit pad */
	int ipf_refcnt;			/* reference count */

	struct route ipf_ro;		/* associated route entry */
	u_long ipf_uses;		/* number of uses in this period */

	int ipf_timer;			/* remaining lifetime of this entry */
	u_long ipf_dropped;		/* ENOBUFS returned by if_output */
	u_long ipf_errors;		/* other errors returned by if_output */
	u_long ipf_last_uses;		/* number of uses in last period */
};
LIST_HEAD(ipflowhead, ipflow);

#define IPFLOW_FLAG_ONLIST	0x1

#define ipflow_inuse		ipflow_inuse_pcpu[mycpuid]
#define ipflowtable		ipflowtable_pcpu[mycpuid]
#define ipflowlist		ipflowlist_pcpu[mycpuid]

static struct ipflowhead	ipflowtable_pcpu[MAXCPU][IPFLOW_HASHSIZE];
static struct ipflowhead	ipflowlist_pcpu[MAXCPU];
static int			ipflow_inuse_pcpu[MAXCPU];
static struct netmsg_base	ipflow_timo_netmsgs[MAXCPU];
static int			ipflow_active = 0;

#define IPFLOW_REFCNT_INIT	1

/* ipflow is alive and active */
#define IPFLOW_IS_ACTIVE(ipf)	((ipf)->ipf_refcnt > IPFLOW_REFCNT_INIT)
/* ipflow is alive but not active */
#define IPFLOW_NOT_ACTIVE(ipf)	((ipf)->ipf_refcnt == IPFLOW_REFCNT_INIT)

#define IPFLOW_REF(ipf) \
do { \
	KKASSERT((ipf)->ipf_refcnt > 0); \
	(ipf)->ipf_refcnt++; \
} while (0)

#define IPFLOW_FREE(ipf) \
do { \
	KKASSERT((ipf)->ipf_refcnt > 0); \
	(ipf)->ipf_refcnt--; \
	if ((ipf)->ipf_refcnt == 0) \
		ipflow_free((ipf)); \
} while (0)

#define IPFLOW_INSERT(bucket, ipf) \
do { \
	KKASSERT(((ipf)->ipf_flags & IPFLOW_FLAG_ONLIST) == 0); \
	(ipf)->ipf_flags |= IPFLOW_FLAG_ONLIST; \
	LIST_INSERT_HEAD((bucket), (ipf), ipf_hash); \
	LIST_INSERT_HEAD(&ipflowlist, (ipf), ipf_list); \
} while (0)

#define IPFLOW_REMOVE(ipf) \
do { \
	KKASSERT((ipf)->ipf_flags & IPFLOW_FLAG_ONLIST); \
	(ipf)->ipf_flags &= ~IPFLOW_FLAG_ONLIST; \
	LIST_REMOVE((ipf), ipf_hash); \
	LIST_REMOVE((ipf), ipf_list); \
} while (0)

SYSCTL_NODE(_net_inet_ip, OID_AUTO, ipflow, CTLFLAG_RW, 0, "ip flow");
SYSCTL_INT(_net_inet_ip, IPCTL_FASTFORWARDING, fastforwarding, CTLFLAG_RW,
	   &ipflow_active, 0, "Enable flow-based IP forwarding");

static MALLOC_DEFINE(M_IPFLOW, "ip_flow", "IP flow");

static void	ipflow_free(struct ipflow *);

static unsigned
ipflow_hash(struct in_addr dst, struct in_addr src, unsigned tos)
{
	unsigned hash = tos;
	int idx;

	for (idx = 0; idx < 32; idx += IPFLOW_HASHBITS)
		hash += (dst.s_addr >> (32 - idx)) + (src.s_addr >> idx);
	return hash & (IPFLOW_HASHSIZE-1);
}

static struct ipflow *
ipflow_lookup(const struct ip *ip)
{
	unsigned hash;
	struct ipflow *ipf;

	hash = ipflow_hash(ip->ip_dst, ip->ip_src, ip->ip_tos);
	LIST_FOREACH(ipf, &ipflowtable[hash], ipf_hash) {
		if (ip->ip_dst.s_addr == ipf->ipf_dst.s_addr &&
		    ip->ip_src.s_addr == ipf->ipf_src.s_addr &&
		    ip->ip_tos == ipf->ipf_tos)
			break;
	}
	return ipf;
}

int
ipflow_fastforward(struct mbuf *m)
{
	struct ip *ip;
	struct ipflow *ipf;
	struct rtentry *rt;
	struct sockaddr *dst;
	struct ifnet *ifp;
	int error, iplen;

	/*
	 * Are we forwarding packets?
	 */
	if (!ipforwarding || !ipflow_active)
		return 0;

	/*
	 * Was packet received as a link-level multicast or broadcast?
	 * If so, don't try to fast forward..
	 */
	if (m->m_flags & (M_BCAST | M_MCAST))
		return 0;

	/* length checks already done in ip_hashfn() */
	KASSERT(m->m_len >= sizeof(struct ip), ("IP header not in one mbuf"));
	ip = mtod(m, struct ip *);

	/*
	 * IP header with no option and valid version
	 */
	if (ip->ip_v != IPVERSION || ip->ip_hl != (sizeof(struct ip) >> 2))
		return 0;

	iplen = ntohs(ip->ip_len);
	/* length checks already done in ip_hashfn() */
	KASSERT(iplen >= sizeof(struct ip),
		("total length less then header length"));
	KASSERT(m->m_pkthdr.len >= iplen, ("mbuf too short"));

	/*
	 * Find a flow.
	 */
	ipf = ipflow_lookup(ip);
	if (ipf == NULL)
		return 0;

	/*
	 * Verify the IP header checksum.
	 */
	if (m->m_pkthdr.csum_flags & CSUM_IP_CHECKED) {
		if (!(m->m_pkthdr.csum_flags & CSUM_IP_VALID))
			return 0;
	} else {
		/* Must compute it ourselves. */
		if (in_cksum_hdr(ip) != 0)
			return 0;
	}

	/*
	 * Route and interface still up?
	 */
	rt = ipf->ipf_ro.ro_rt;
	if (IPFLOW_RTENTRY_ISDOWN(rt))
		return 0;
	ifp = rt->rt_ifp;

	/*
	 * Packet size OK?  TTL?
	 */
	if (m->m_pkthdr.len > ifp->if_mtu || ip->ip_ttl <= IPTTLDEC)
		return 0;

	/*
	 * Clear any in-bound checksum flags for this packet.
	 */
	m->m_pkthdr.csum_flags = 0;

	/*
	 * Everything checks out and so we can forward this packet.
	 * Modify the TTL and incrementally change the checksum.
	 * 
	 * This method of adding the checksum works on either endian CPU.
	 * If htons() is inlined, all the arithmetic is folded; otherwise
	 * the htons()s are combined by CSE due to the __const__ attribute.
	 *
	 * Don't bother using HW checksumming here -- the incremental
	 * update is pretty fast.
	 */
	ip->ip_ttl -= IPTTLDEC;
	if (ip->ip_sum >= (uint16_t)~htons(IPTTLDEC << 8))
		ip->ip_sum -= ~htons(IPTTLDEC << 8);
	else
		ip->ip_sum += htons(IPTTLDEC << 8);

	/*
	 * Trim the packet in case it's too long.. 
	 */
	if (m->m_pkthdr.len > iplen) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = iplen;
			m->m_pkthdr.len = iplen;
		} else {
			m_adj(m, iplen - m->m_pkthdr.len);
		}
	}

	/*
	 * Send the packet on its way.  All we can get back is ENOBUFS
	 */
	ipf->ipf_uses++;
	ipf->ipf_timer = IPFLOW_TIMER;

	if (rt->rt_flags & RTF_GATEWAY)
		dst = rt->rt_gateway;
	else
		dst = &ipf->ipf_ro.ro_dst;

	/*
	 * Reference count this ipflow, before the possible blocking
	 * ifnet.if_output(), so this ipflow will not be changed or
	 * reaped behind our back.
	 */
	IPFLOW_REF(ipf);

	error = ifp->if_output(ifp, m, dst, rt);
	if (error) {
		if (error == ENOBUFS)
			ipf->ipf_dropped++;
		else
			ipf->ipf_errors++;
	}

	IPFLOW_FREE(ipf);
	return 1;
}

static void
ipflow_addstats(struct ipflow *ipf)
{
	ipf->ipf_ro.ro_rt->rt_use += ipf->ipf_uses;
	ipstat.ips_cantforward += ipf->ipf_errors + ipf->ipf_dropped;
	ipstat.ips_total += ipf->ipf_uses;
	ipstat.ips_forward += ipf->ipf_uses;
	ipstat.ips_fastforward += ipf->ipf_uses;
}

static void
ipflow_free(struct ipflow *ipf)
{
	KKASSERT(ipf->ipf_refcnt == 0);
	KKASSERT((ipf->ipf_flags & IPFLOW_FLAG_ONLIST) == 0);

	KKASSERT(ipflow_inuse > 0);
	ipflow_inuse--;

	ipflow_addstats(ipf);
	RTFREE(ipf->ipf_ro.ro_rt);
	kfree(ipf, M_IPFLOW);
}

static void
ipflow_reset(struct ipflow *ipf)
{
	ipflow_addstats(ipf);
	RTFREE(ipf->ipf_ro.ro_rt);
	ipf->ipf_uses = ipf->ipf_last_uses = 0;
	ipf->ipf_errors = ipf->ipf_dropped = 0;
}

static struct ipflow *
ipflow_reap(void)
{
	struct ipflow *ipf, *maybe_ipf = NULL;

	LIST_FOREACH(ipf, &ipflowlist, ipf_list) {
		/*
		 * Skip actively used ipflow
		 */
		if (IPFLOW_IS_ACTIVE(ipf))
			continue;

		/*
		 * If this no longer points to a valid route
		 * reclaim it.
		 */
		if ((ipf->ipf_ro.ro_rt->rt_flags & RTF_UP) == 0)
			goto done;

		/*
		 * choose the one that's been least recently used
		 * or has had the least uses in the last 1.5
		 * intervals.
		 */
		if (maybe_ipf == NULL ||
		    ipf->ipf_timer < maybe_ipf->ipf_timer ||
		    (ipf->ipf_timer == maybe_ipf->ipf_timer &&
		     ipf->ipf_last_uses + ipf->ipf_uses <
		     maybe_ipf->ipf_last_uses + maybe_ipf->ipf_uses))
			maybe_ipf = ipf;
	}
	if (maybe_ipf == NULL)
		return NULL;

	ipf = maybe_ipf;
done:
	/*
	 * Remove the entry from the flow table and reset its states
	 */
	IPFLOW_REMOVE(ipf);
	ipflow_reset(ipf);
	return ipf;
}

static void
ipflow_timo_dispatch(netmsg_t nmsg)
{
	struct ipflow *ipf, *next_ipf;

	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);	/* reply ASAP */
	crit_exit();

	LIST_FOREACH_MUTABLE(ipf, &ipflowlist, ipf_list, next_ipf) {
		if (--ipf->ipf_timer == 0) {
			IPFLOW_REMOVE(ipf);
			IPFLOW_FREE(ipf);
		} else {
			ipf->ipf_last_uses = ipf->ipf_uses;
			ipf->ipf_ro.ro_rt->rt_use += ipf->ipf_uses;
			ipstat.ips_total += ipf->ipf_uses;
			ipstat.ips_forward += ipf->ipf_uses;
			ipstat.ips_fastforward += ipf->ipf_uses;
			ipf->ipf_uses = 0;
		}
	}
}

static void
ipflow_timo_ipi(void *arg __unused)
{
	struct lwkt_msg *msg = &ipflow_timo_netmsgs[mycpuid].lmsg;

	crit_enter();
	if (msg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(mycpuid), msg);
	crit_exit();
}

void
ipflow_slowtimo(void)
{
	cpumask_t mask;
	int i;

	CPUMASK_ASSZERO(mask);
	for (i = 0; i < ncpus; ++i) {
		if (ipflow_inuse_pcpu[i])
			CPUMASK_ORBIT(mask, i);
	}
	CPUMASK_ANDMASK(mask, smp_active_mask);
	if (CPUMASK_TESTNZERO(mask))
		lwkt_send_ipiq_mask(mask, ipflow_timo_ipi, NULL);
}

void
ipflow_create(const struct route *ro, struct mbuf *m)
{
	const struct ip *const ip = mtod(m, struct ip *);
	struct ipflow *ipf;
	unsigned hash;

	/*
	 * Don't create cache entries for ICMP messages.
	 */
	if (!ipflow_active || ip->ip_p == IPPROTO_ICMP)
		return;

	/*
	 * See if an existing flow struct exists.  If so remove it from it's
	 * list and free the old route.  If not, try to malloc a new one
	 * (if we aren't at our limit).
	 */
	ipf = ipflow_lookup(ip);
	if (ipf == NULL) {
		if (ipflow_inuse == IPFLOW_MAX) {
			ipf = ipflow_reap();
			if (ipf == NULL)
				return;
		} else {
			ipf = kmalloc(sizeof(*ipf), M_IPFLOW,
				      M_NOWAIT | M_ZERO);
			if (ipf == NULL)
				return;
			ipf->ipf_refcnt = IPFLOW_REFCNT_INIT;

			ipflow_inuse++;
		}
	} else {
		if (IPFLOW_NOT_ACTIVE(ipf)) {
			IPFLOW_REMOVE(ipf);
			ipflow_reset(ipf);
		} else {
			/* This ipflow is being used; don't change it */
			KKASSERT(IPFLOW_IS_ACTIVE(ipf));
			return;
		}
	}
	/* This ipflow should not be actively used */
	KKASSERT(IPFLOW_NOT_ACTIVE(ipf));

	/*
	 * Fill in the updated information.
	 */
	ipf->ipf_ro = *ro;
	ro->ro_rt->rt_refcnt++;
	ipf->ipf_dst = ip->ip_dst;
	ipf->ipf_src = ip->ip_src;
	ipf->ipf_tos = ip->ip_tos;
	ipf->ipf_timer = IPFLOW_TIMER;

	/*
	 * Insert into the approriate bucket of the flow table.
	 */
	hash = ipflow_hash(ip->ip_dst, ip->ip_src, ip->ip_tos);
	IPFLOW_INSERT(&ipflowtable[hash], ipf);
}

void
ipflow_flush_oncpu(void)
{
	struct ipflow *ipf;

	while ((ipf = LIST_FIRST(&ipflowlist)) != NULL) {
		IPFLOW_REMOVE(ipf);
		IPFLOW_FREE(ipf);
	}
}

static void
ipflow_ifaddr_handler(netmsg_t nmsg)
{
	struct netmsg_ipfaddr *amsg = (struct netmsg_ipfaddr *)nmsg;
	struct ipflow *ipf, *next_ipf;

	LIST_FOREACH_MUTABLE(ipf, &ipflowlist, ipf_list, next_ipf) {
		if (ipf->ipf_dst.s_addr == amsg->ipf_addr.s_addr ||
		    ipf->ipf_src.s_addr == amsg->ipf_addr.s_addr) {
			IPFLOW_REMOVE(ipf);
			IPFLOW_FREE(ipf);
		}
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipflow_ifaddr(void *arg __unused, struct ifnet *ifp __unused,
	      enum ifaddr_event event, struct ifaddr *ifa)
{
	struct netmsg_ipfaddr amsg;

	if (ifa->ifa_addr->sa_family != AF_INET)
		return;

	/* Only add/change events need to be handled */
	switch (event) {
	case IFADDR_EVENT_ADD:
	case IFADDR_EVENT_CHANGE:
		break;

	case IFADDR_EVENT_DELETE:
		return;
	}

	netmsg_init(&amsg.base, NULL, &curthread->td_msgport,
		    MSGF_PRIORITY, ipflow_ifaddr_handler);
	amsg.ipf_addr = ifatoia(ifa)->ia_addr.sin_addr;

	ifnet_domsg(&amsg.base.lmsg, 0);
}

static void
ipflow_init(void)
{
	char oid_name[32];
	int i;

	for (i = 0; i < ncpus; ++i) {
		netmsg_init(&ipflow_timo_netmsgs[i], NULL, &netisr_adone_rport,
			    0, ipflow_timo_dispatch);

		ksnprintf(oid_name, sizeof(oid_name), "inuse%d", i);

		SYSCTL_ADD_INT(NULL,
		SYSCTL_STATIC_CHILDREN(_net_inet_ip_ipflow),
		OID_AUTO, oid_name, CTLFLAG_RD, &ipflow_inuse_pcpu[i], 0,
		"# of ip flow being used");
	}
	EVENTHANDLER_REGISTER(ifaddr_event, ipflow_ifaddr, NULL,
			      EVENTHANDLER_PRI_ANY);
}
SYSINIT(arp, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY, ipflow_init, 0);
