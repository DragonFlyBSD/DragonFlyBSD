/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)route.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/net/route.h,v 1.36.2.5 2002/02/01 11:48:01 ru Exp $
 * $DragonFly: src/sys/net/route.h,v 1.24 2008/09/11 11:23:29 sephe Exp $
 */

#ifndef _NET_ROUTE_H_
#define _NET_ROUTE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_SOCKET_H_
#include <sys/socket.h>
#endif

/*
 * Kernel resident routing tables.
 *
 * The routing tables are initialized when interface addresses
 * are set by making entries for all directly connected interfaces.
 */

/*
 * A route consists of a destination address and a reference
 * to a routing entry.  These are often held by protocols
 * in their control blocks, e.g. inpcb.
 */
struct route {
	struct	rtentry *ro_rt;
	struct	sockaddr ro_dst;
};

/*
 * These numbers are used by reliable protocols for determining
 * retransmission behavior and are included in the routing structure.
 */
struct rt_metrics {	/* grouped for locality of reference */
	u_long	rmx_locks;	/* Kernel must leave these values alone */
	u_long	rmx_mtu;	/* MTU for this path */
	u_long	rmx_pksent;	/* packets sent using this route */
	u_long	rmx_expire;	/* lifetime for route */

	u_long	rmx_sendpipe;	/* outbound delay-bandwidth product */
	u_long	rmx_ssthresh;	/* outbound gateway buffer limit */
	u_long	rmx_rtt;	/* estimated round trip time */
	u_long	rmx_rttvar;	/* estimated rtt variance */

	u_long	rmx_recvpipe;	/* inbound delay-bandwidth product */
	u_long	rmx_hopcount;	/* max hops expected */
	u_short rmx_mssopt;	/* peer's cached MSS */
	u_short	rmx_pad;	/* explicit pad */
	u_long	rmx_msl;	/* maximum segment lifetime, unit: ms */
	u_long	rmx_iwmaxsegs;	/* IW segments max */
	u_long	rmx_iwcapsegs;	/* IW segments */
};

/*
 * rmx_rtt and rmx_rttvar are stored as microseconds;
 * RTTTOPRHZ(rtt) converts to a value suitable for use
 * by a protocol slowtimo counter.
 */
#define	RTM_RTTUNIT	1000000	/* units for rtt, rttvar, as units per sec */
#define	RTTTOPRHZ(r)	((r) / (RTM_RTTUNIT / PR_SLOWHZ))

/*
 * XXX kernel function pointer `rt_output' is visible to applications.
 */
struct mbuf;

/*
 * We distinguish between routes to hosts and routes to networks,
 * preferring the former if available.  For each route we infer
 * the interface to use from the gateway address supplied when
 * the route was entered.  Routes that forward packets through
 * gateways are marked so that the output routines know to address the
 * gateway rather than the ultimate destination.
 */
#ifndef RNF_NORMAL
#include <net/radix.h>
#endif

struct rtentry {
	struct	radix_node rt_nodes[2];	/* tree glue, and other values */
#define	rt_key(r)	((struct sockaddr *)((r)->rt_nodes->rn_key))
#define	rt_mask(r)	((struct sockaddr *)((r)->rt_nodes->rn_mask))
	struct	sockaddr *rt_gateway;	/* value */
	long	rt_refcnt;		/* # held references */
	u_long	rt_flags;		/* up/down?, host/net */
	struct	ifnet *rt_ifp;		/* the answer: interface to use */
	struct	ifaddr *rt_ifa;		/* the answer: interface to use */
	struct	sockaddr *rt_genmask;	/* for generation of cloned routes */
	void	*rt_llinfo;		/* pointer to link level info cache */
	struct	rt_metrics rt_rmx;	/* metrics used by rx'ing protocols */
	struct	rtentry *rt_gwroute;	/* implied entry for gatewayed routes */
	int	(*rt_output) (struct ifnet *, struct mbuf *, struct sockaddr *,
			      struct rtentry *);
					/* output routine for this (rt,if) */
	struct	rtentry *rt_parent;	/* cloning parent of this route */
	int	rt_cpuid;		/* owner cpu */
	struct	sockaddr *rt_shim[3];	/* mpls label / operation array */
};

/*
 * Following structure necessary for 4.3 compatibility;
 * We should eventually move it to a compat file.
 */
struct ortentry {
	u_long	rt_hash;		/* to speed lookups */
	struct	sockaddr rt_dst;	/* key */
	struct	sockaddr rt_gateway;	/* value */
	short	rt_flags;		/* up/down?, host/net */
	short	rt_refcnt;		/* # held references */
	u_long	rt_use;			/* raw # packets forwarded */
	struct	ifnet *rt_ifp;		/* the answer: interface to use */
};

#define rt_use rt_rmx.rmx_pksent

#define	RTF_UP		0x1		/* route usable */
#define	RTF_GATEWAY	0x2		/* destination is a gateway */
#define	RTF_HOST	0x4		/* host entry (net otherwise) */
#define	RTF_REJECT	0x8		/* host or net unreachable */
#define	RTF_DYNAMIC	0x10		/* created dynamically (by redirect) */
#define	RTF_MODIFIED	0x20		/* modified dynamically (by redirect) */
#define RTF_DONE	0x40		/* message confirmed */
/*			0x80		   unused, was RTF_DELCLONE */
#define RTF_CLONING	0x100		/* generate new routes on use */
#define RTF_XRESOLVE	0x200		/* external daemon resolves name */
#define RTF_LLINFO	0x400		/* generated by link layer (e.g. ARP) */
#define RTF_STATIC	0x800		/* manually added */
#define RTF_BLACKHOLE	0x1000		/* just discard pkts (during updates) */
#define RTF_PROTO2	0x4000		/* protocol specific routing flag */
#define RTF_PROTO1	0x8000		/* protocol specific routing flag */

#define RTF_PRCLONING	0x10000		/* protocol requires cloning */
#define RTF_WASCLONED	0x20000		/* route generated through cloning */
#define RTF_PROTO3	0x40000		/* protocol specific routing flag */
/*			0x80000		   unused */
#define RTF_PINNED	0x100000	/* future use */
#define	RTF_LOCAL	0x200000	/* route represents a local address */
#define	RTF_BROADCAST	0x400000	/* route represents a bcast address */
#define	RTF_MULTICAST	0x800000	/* route represents a mcast address */
#define	RTF_MPLSOPS	0x1000000	/* route uses mpls label operations */
					/* 0x2000000 and up unassigned */

/*
 * Routing statistics.
 */
struct	rtstatistics {
	u_long	rts_badredirect;	/* bogus redirect calls */
	u_long	rts_dynamic;		/* routes created by redirects */
	u_long	rts_newgateway;		/* routes modified by redirects */
	u_long	rts_unreach;		/* lookups which failed */
	u_long	rts_wildcard;		/* lookups satisfied by a wildcard */
};
/*
 * Structures for routing messages.
 */
struct rt_msghdr {
	u_short	rtm_msglen;	/* to skip over non-understood messages */
	u_char	rtm_version;	/* future binary compatibility */
	u_char	rtm_type;	/* message type */
	u_short	rtm_index;	/* index for associated ifp */
	int	rtm_flags;	/* flags, incl. kern & message, e.g. DONE */
	int	rtm_addrs;	/* bitmask identifying sockaddrs in msg */
	pid_t	rtm_pid;	/* identify sender */
	int	rtm_seq;	/* for sender to identify action */
	int	rtm_errno;	/* why failed */
	int	rtm_use;	/* from rtentry */
	u_long	rtm_inits;	/* which metrics we are initializing */
	struct	rt_metrics rtm_rmx; /* metrics themselves */
};

#define RTM_VERSION	6	/* Up the ante and ignore older versions */

/*
 * Message types.
 */
#define RTM_ADD		0x1	/* Add Route */
#define RTM_DELETE	0x2	/* Delete Route */
#define RTM_CHANGE	0x3	/* Change Metrics or flags */
#define RTM_GET		0x4	/* Report Metrics */
#define RTM_LOSING	0x5	/* Kernel Suspects Partitioning */
#define RTM_REDIRECT	0x6	/* Told to use different route */
#define RTM_MISS	0x7	/* Lookup failed on this address */
#define RTM_LOCK	0x8	/* fix specified metrics */
#define RTM_OLDADD	0x9	/* caused by SIOCADDRT */
#define RTM_OLDDEL	0xa	/* caused by SIOCDELRT */
#define RTM_RESOLVE	0xb	/* req to resolve dst to LL addr */
#define RTM_NEWADDR	0xc	/* address being added to iface */
#define RTM_DELADDR	0xd	/* address being removed from iface */
#define RTM_IFINFO	0xe	/* iface going up/down etc. */
#define	RTM_NEWMADDR	0xf	/* mcast group membership being added to if */
#define	RTM_DELMADDR	0x10	/* mcast group membership being deleted */
#define	RTM_IFANNOUNCE	0x11	/* iface arrival/departure */
#define	RTM_IEEE80211	0x12	/* IEEE80211 wireless event */

/*
 * Bitmask values for rtm_inits and rmx_locks.
 */
#define RTV_MTU		0x1	/* init or lock _mtu */
#define RTV_HOPCOUNT	0x2	/* init or lock _hopcount */
#define RTV_EXPIRE	0x4	/* init or lock _expire */
#define RTV_RPIPE	0x8	/* init or lock _recvpipe */
#define RTV_SPIPE	0x10	/* init or lock _sendpipe */
#define RTV_SSTHRESH	0x20	/* init or lock _ssthresh */
#define RTV_RTT		0x40	/* init or lock _rtt */
#define RTV_RTTVAR	0x80	/* init or lock _rttvar */
#define RTV_MSL		0x100	/* init or lock _msl */
#define RTV_IWMAXSEGS	0x200	/* init or lock _iwmaxsegs */
#define RTV_IWCAPSEGS	0x400	/* init or lock _iwcapsegs */

/*
 * Bitmask values for rtm_addrs.
 */
#define RTA_DST		0x1	/* destination sockaddr present */
#define RTA_GATEWAY	0x2	/* gateway sockaddr present */
#define RTA_NETMASK	0x4	/* netmask sockaddr present */
#define RTA_GENMASK	0x8	/* cloning mask sockaddr present */
#define RTA_IFP		0x10	/* interface name sockaddr present */
#define RTA_IFA		0x20	/* interface addr sockaddr present */
#define RTA_AUTHOR	0x40	/* sockaddr for author of redirect */
#define RTA_BRD		0x80	/* for NEWADDR, broadcast or p-p dest addr */
#define RTA_MPLS1	0x100	/* mpls label and/or operation present */
#define RTA_MPLS2	0x200	/* mpls label and/or operation present */
#define RTA_MPLS3	0x400	/* mpls label and/or operation present */

/*
 * Index offsets for sockaddr array for alternate internal encoding.
 */
#define RTAX_DST	0	/* destination sockaddr present */
#define RTAX_GATEWAY	1	/* gateway sockaddr present */
#define RTAX_NETMASK	2	/* netmask sockaddr present */
#define RTAX_GENMASK	3	/* cloning mask sockaddr present */
#define RTAX_IFP	4	/* interface name sockaddr present */
#define RTAX_IFA	5	/* interface addr sockaddr present */
#define RTAX_AUTHOR	6	/* sockaddr for author of redirect */
#define RTAX_BRD	7	/* for NEWADDR, broadcast or p-p dest addr */
#define RTAX_MPLS1	8	/* mpls label and/or operation present */
#define RTAX_MPLS2	9	/* mpls label and/or operation present */
#define RTAX_MPLS3	10	/* mpls label and/or operation present */
#define RTAX_MAX	11	/* size of array to allocate */

struct rt_addrinfo {
	int		 rti_addrs;
	struct sockaddr	*rti_info[RTAX_MAX];
	int		 rti_flags;
	struct ifaddr	*rti_ifa;
	struct ifnet	*rti_ifp;
};

#ifdef _KERNEL

#define	rti_dst		rti_info[RTAX_DST]
#define	rti_gateway	rti_info[RTAX_GATEWAY]
#define	rti_netmask	rti_info[RTAX_NETMASK]
#define	rti_genmask	rti_info[RTAX_GENMASK]
#define	rti_ifpaddr	rti_info[RTAX_IFP]
#define	rti_ifaaddr	rti_info[RTAX_IFA]
#define	rti_author	rti_info[RTAX_AUTHOR]
#define	rti_bcastaddr	rti_info[RTAX_BRD]
#define	rti_mpls1	rti_info[RTAX_MPLS1]
#define	rti_mpls2	rti_info[RTAX_MPLS2]
#define	rti_mpls3	rti_info[RTAX_MPLS3]

extern struct radix_node_head *rt_tables[MAXCPU][AF_MAX+1];

struct ifmultiaddr;
struct proc;
struct ucred;

void	 route_init (void);
void	 rt_dstmsg(int type, struct sockaddr *dst, int error);
int	 rt_getifa (struct rt_addrinfo *);
void	 rt_ieee80211msg(struct ifnet *, int, void *, size_t);
void	 rt_ifannouncemsg (struct ifnet *, int);
void	 rt_ifmsg (struct ifnet *);
int	 rt_llroute (struct sockaddr *dst, struct rtentry *rt0,
	    struct rtentry **drt);
void	 rt_missmsg (int, struct rt_addrinfo *, int, int);
void	 rt_newaddrmsg (int, struct ifaddr *, int, struct rtentry *);
void	 rt_newmaddrmsg (int, struct ifmultiaddr *);
void	 rt_rtmsg(int cmd, struct rtentry *rt, struct ifnet *ifp, int error);
int	 rt_setgate (struct rtentry *,
	    struct sockaddr *, struct sockaddr *, boolean_t);
void	 rtalloc (struct route *);
void	 rtalloc_ign (struct route *, u_long);

struct rtentry *
	 _rtlookup (struct sockaddr *, __boolean_t, u_long);
#define		RTL_REPORTMSG	TRUE
#define		RTL_DONTREPORT	FALSE

/* flags to ignore */
#define		RTL_DOCLONE	0UL
#define		RTL_DONTCLONE	(RTF_CLONING | RTF_PRCLONING)

/*
 * Look up a route with no cloning side-effects or miss reports generated.
 */
static __inline struct rtentry *
rtpurelookup(struct sockaddr *dst)
{
	return _rtlookup(dst, RTL_DONTREPORT, RTL_DONTCLONE);
}

/*
 * Do full route lookup with cloning and reporting on misses.
 */
static __inline struct rtentry *
rtlookup(struct sockaddr *dst)
{
	return _rtlookup(dst, RTL_REPORTMSG, RTL_DOCLONE);
}

typedef void (*rtrequest1_callback_func_t)(int, int, struct rt_addrinfo *,
				      struct rtentry *, void *);
typedef int (*rtsearch_callback_func_t)(int, struct rt_addrinfo *,
					struct rtentry *, void *, int);

void	 rtfree (struct rtentry *);
int	 rtinit (struct ifaddr *, int, int);
int	 rtchange (struct ifaddr *, struct ifaddr *);
int	 rtioctl (u_long, caddr_t, struct ucred *);
void	 rtredirect (struct sockaddr *, struct sockaddr *,
	    struct sockaddr *, int, struct sockaddr *);
int	 rtrequest (int, struct sockaddr *,
	    struct sockaddr *, struct sockaddr *, int, struct rtentry **);
int	 rtrequest_global (int, struct sockaddr *,
	    struct sockaddr *, struct sockaddr *, int);
int	 rtrequest1 (int, struct rt_addrinfo *, struct rtentry **);
int	 rtrequest1_global (int, struct rt_addrinfo *,
	    rtrequest1_callback_func_t, void *, boolean_t);

#define RTS_EXACTMATCH		TRUE
#define RTS_NOEXACTMATCH	FALSE

#define RTREQ_PRIO_HIGH		TRUE
#define RTREQ_PRIO_NORM		FALSE

int	 rtsearch_global(int, struct rt_addrinfo *,
	    rtsearch_callback_func_t, void *, boolean_t, boolean_t);

int	 rtmask_add_global(struct sockaddr *, boolean_t);

struct sockaddr *_rtmask_lookup(struct sockaddr *, boolean_t);

static __inline struct sockaddr *
rtmask_lookup(struct sockaddr *_mask)
{
	return _rtmask_lookup(_mask, FALSE);
}

static __inline struct sockaddr *
rtmask_purelookup(struct sockaddr *_mask)
{
	return _rtmask_lookup(_mask, TRUE);
}

void	rtfree_oncpu(struct rtentry *);
void	rtfree_remote(struct rtentry *);
void	rt_print(struct rt_addrinfo *, struct rtentry *);
void	rt_addrinfo_print(int cmd, struct rt_addrinfo *);
void	sockaddr_print(struct sockaddr *);

struct netmsg_base;
int	rt_domsg_global(struct netmsg_base *);

#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif

static __inline void
RTFREE(struct rtentry *rt)
{
	if (rt->rt_cpuid == mycpuid) {
		if (rt->rt_refcnt <= 1)
			rtfree_oncpu(rt);
		else
			--rt->rt_refcnt;
	} else {
		rtfree_remote(rt);
	}
}

int	in_inithead(void **, int);
#endif

#endif
