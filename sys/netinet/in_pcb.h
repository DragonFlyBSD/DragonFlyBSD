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
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)in_pcb.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/in_pcb.h,v 1.32.2.7 2003/01/24 05:11:34 sam Exp $
 */

#ifndef _NETINET_IN_PCB_H_
#define _NETINET_IN_PCB_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_SYSCTL_H_
#include <sys/sysctl.h>
#endif
#ifndef _NETINET_IN_H_
#include <netinet/in.h>
#endif
#ifndef _NET_ROUTE_H_
#include <net/route.h>
#endif

#define	in6pcb		inpcb	/* for KAME src sync over BSD*'s */
#define	in6p_sp		inp_sp	/* for KAME src sync over BSD*'s */
struct inpcbpolicy;

/*
 * Common structure pcb for internet protocol implementation.
 * Here are stored pointers to local and foreign host table
 * entries, local and foreign socket numbers, and pointers
 * up (to a socket structure) and down (to a protocol-specific)
 * control block.
 */
LIST_HEAD(inpcbhead, inpcb);
LIST_HEAD(inpcbporthead, inpcbport);
typedef	u_quad_t	inp_gen_t;

struct inpcontainer {
	struct inpcb			*ic_inp;
	LIST_ENTRY(inpcontainer)	ic_list;
};
LIST_HEAD(inpcontainerhead, inpcontainer);

/*
 * PCB with AF_INET6 null bind'ed laddr can receive AF_INET input packet.
 * So, AF_INET6 null laddr is also used as AF_INET null laddr,
 * by utilize following structure. (At last, same as INRIA)
 */
struct in_addr_4in6 {
	u_int32_t	ia46_pad32[3];
	struct	in_addr	ia46_addr4;
};

union in_dependaddr {
	struct in_addr_4in6 id46_addr;
	struct in6_addr	id6_addr;
};

/*
 * NOTE: ipv6 addrs should be 64-bit aligned, per RFC 2553.
 * in_conninfo has some extra padding to accomplish this.
 */
struct in_endpoints {
	u_int16_t	ie_fport;		/* foreign port */
	u_int16_t	ie_lport;		/* local port */
	/* protocol dependent part, local and foreign addr */
	union in_dependaddr ie_dependfaddr;	/* foreign host table entry */
	union in_dependaddr ie_dependladdr;	/* local host table entry */
#define	ie_faddr	ie_dependfaddr.id46_addr.ia46_addr4
#define	ie_laddr	ie_dependladdr.id46_addr.ia46_addr4
#define	ie6_faddr	ie_dependfaddr.id6_addr
#define	ie6_laddr	ie_dependladdr.id6_addr
};

struct inp_localgroup {
	LIST_ENTRY(inp_localgroup) il_list;
	uint16_t	il_lport;
	u_char		il_vflag;
	u_char		il_pad;
	uint32_t	il_pad2;
	union in_dependaddr il_dependladdr;
#define il_laddr	il_dependladdr.id46_addr.ia46_addr4
#define il6_laddr	il_dependladdr.id6_addr
	int		il_inpsiz;	/* size of il_inp[] */
	int		il_inpcnt;	/* # of elem in il_inp[] */
	struct inpcb	*il_inp[];
};
LIST_HEAD(inp_localgrphead, inp_localgroup);

/*
 * XXX
 * At some point struct route should possibly change to:
 *   struct rtentry *rt
 *   struct in_endpoints *ie;
 */
struct in_conninfo {
	u_int8_t	inc_flags;
	u_int8_t	inc_len;
	u_int16_t	inc_pad;	/* XXX alignment for in_endpoints */
	/* protocol dependent part; cached route */
	struct	in_endpoints inc_ie;
	union {
		/* placeholder for routing entry */
		struct	route inc4_route;
		struct	route_in6 inc6_route;
	} inc_dependroute;
};
#define inc_isipv6	inc_flags	/* temp compatibility */
#define	inc_fport	inc_ie.ie_fport
#define	inc_lport	inc_ie.ie_lport
#define	inc_faddr	inc_ie.ie_faddr
#define	inc_laddr	inc_ie.ie_laddr
#define	inc_route	inc_dependroute.inc4_route
#define	inc6_faddr	inc_ie.ie6_faddr
#define	inc6_laddr	inc_ie.ie6_laddr
#define	inc6_route	inc_dependroute.inc6_route

/*
 * NB: the zone allocator is type-stable EXCEPT FOR THE FIRST TWO LONGS
 * of the structure.  Therefore, it is important that the members in
 * that position not contain any information which is required to be
 * stable.
 */
struct	icmp6_filter;
struct	inpcbportinfo;

struct inpcb {
	LIST_ENTRY(inpcb) inp_hash; /* hash list */
	LIST_ENTRY(inpcb) inp_list; /* list for all PCBs of this proto */
	u_int32_t	inp_flow;
	int		inp_lgrpindex;	/* local group index */

	/* local and foreign ports, local and foreign addr */
	struct	in_conninfo inp_inc;

	void	*inp_ppcb;		/* pointer to per-protocol pcb */
	struct	inpcbinfo *inp_pcbinfo;	/* PCB list info */
	struct	socket *inp_socket;	/* back pointer to socket */
					/* list for this PCB's local port */
	int	inp_flags;		/* generic IP/datagram flags */

	struct	inpcbpolicy *inp_sp; /* for IPSEC */
	u_char	inp_vflag;
#define	INP_IPV4	0x1
#define	INP_IPV6	0x2
	u_char	inp_ip_ttl;		/* time to live proto */
	u_char	inp_ip_p;		/* protocol proto */
	u_char	inp_ip_minttl;		/* minimum TTL or drop */

	/* protocol dependent part; options */
	struct {
		u_char	inp4_ip_tos;		/* type of service proto */
		struct	mbuf *inp4_options;	/* IP options */
		struct	ip_moptions *inp4_moptions; /* IP multicast options */
	} inp_depend4;
#define inp_fport	inp_inc.inc_fport
#define inp_lport	inp_inc.inc_lport
#define	inp_faddr	inp_inc.inc_faddr
#define	inp_laddr	inp_inc.inc_laddr
#define	inp_route	inp_inc.inc_route
#define	inp_ip_tos	inp_depend4.inp4_ip_tos
#define	inp_options	inp_depend4.inp4_options
#define	inp_moptions	inp_depend4.inp4_moptions
	struct {
		/* IP options */
		struct	mbuf *inp6_options;
		/* IP6 options for outgoing packets */
		struct	ip6_pktopts *inp6_outputopts;
		/* IP multicast options */
		struct	ip6_moptions *inp6_moptions;
		/* ICMPv6 code type filter */
		struct	icmp6_filter *inp6_icmp6filt;
		/* IPV6_CHECKSUM setsockopt */
		int	inp6_cksum;
		u_short	inp6_ifindex;
		short	inp6_hops;
		u_int8_t	inp6_hlim;
	} inp_depend6;
	LIST_ENTRY(inpcb) inp_portlist;
	struct	inpcbportinfo *inp_portinfo;
	struct	inpcbport *inp_phd;	/* head of this list */
	inp_gen_t	inp_gencnt;	/* generation count of this instance */
#define	in6p_faddr	inp_inc.inc6_faddr
#define	in6p_laddr	inp_inc.inc6_laddr
#define	in6p_route	inp_inc.inc6_route
#define	in6p_ip6_hlim	inp_depend6.inp6_hlim
#define	in6p_hops	inp_depend6.inp6_hops	/* default hop limit */
#define	in6p_ip6_nxt	inp_ip_p
#define	in6p_flowinfo	inp_flow
#define	in6p_vflag	inp_vflag
#define	in6p_options	inp_depend6.inp6_options
#define	in6p_outputopts	inp_depend6.inp6_outputopts
#define	in6p_moptions	inp_depend6.inp6_moptions
#define	in6p_icmp6filt	inp_depend6.inp6_icmp6filt
#define	in6p_cksum	inp_depend6.inp6_cksum
#define	inp6_ifindex	inp_depend6.inp6_ifindex
#define	in6p_flags	inp_flags  /* for KAME src sync over BSD*'s */
#define	in6p_socket	inp_socket  /* for KAME src sync over BSD*'s */
#define	in6p_lport	inp_lport  /* for KAME src sync over BSD*'s */
#define	in6p_fport	inp_fport  /* for KAME src sync over BSD*'s */
#define	in6p_ppcb	inp_ppcb  /* for KAME src sync over BSD*'s */

	void	*inp_pf_sk;
};
/*
 * The range of the generation count, as used in this implementation,
 * is 9e19.  We would have to create 300 billion connections per
 * second for this number to roll over in a year.  This seems sufficiently
 * unlikely that we simply don't concern ourselves with that possibility.
 */

/*
 * Interface exported to userland by various protocols which use
 * inpcbs.  Hack alert -- only define if struct xsocket is in scope.
 */
#ifdef _SYS_SOCKETVAR_H_
struct	xinpcb {
	size_t	xi_len;		/* length of this structure */
	struct	inpcb xi_inp;
	struct	xsocket xi_socket;
	u_quad_t	xi_alignment_hack;
};
#endif /* _SYS_SOCKETVAR_H_ */

struct inpcbport {
	LIST_ENTRY(inpcbport) phd_hash;
	struct inpcbhead phd_pcblist;
	u_short phd_port;
};

struct lwkt_token;

struct inpcbportinfo {
	struct  lwkt_token *porttoken;	/* if this inpcbportinfo is shared */
	struct	inpcbporthead *porthashbase;
	u_long	porthashmask;
	u_short	offset;
	u_short	lastport;
	u_short	lastlow;
	u_short	lasthi;
} __cachealign;

struct inpcbinfo {		/* XXX documentation, prefixes */
	struct	lwkt_token *infotoken;	/* if this inpcbinfo is shared */
	struct	inpcbhead *hashbase;
	u_long	hashmask;
	int	portinfo_mask;
	struct	inpcbportinfo *portinfo;
	struct 	inpcbport *portsave;	/* port allocation cache */
	struct	inpcontainerhead *wildcardhashbase;
	u_long	wildcardhashmask;
	struct	inp_localgrphead *localgrphashbase;
	u_long	localgrphashmask;
	struct	inpcbhead pcblisthead;	/* head of queue of active pcb's */
	size_t	ipi_size;	/* allocation size for pcbs */
	u_int	ipi_count;	/* number of pcbs in this list */
	int	cpu;		/* related protocol thread cpu */
	u_quad_t ipi_gencnt;	/* current generation count */
} __cachealign;


#define	INP_PCBCONNHASH(faddr, fport, laddr, lport, mask)		\
    (((faddr) ^ ((faddr) >> 16) ^ (laddr) ^ ntohs((lport) ^ (fport))) & (mask))

#define	INP_PCBPORTHASH(lport, mask)		(ntohs(lport) & (mask))

#define	INP_PCBWILDCARDHASH(lport, mask)	(ntohs(lport) & (mask))

#define	INP_PCBLOCALGRPHASH(lport, mask)	(ntohs(lport) & (mask))

/* flags in inp_flags: */
#define	INP_RECVOPTS		0x01	/* receive incoming IP options */
#define	INP_RECVRETOPTS		0x02	/* receive IP options for reply */
#define	INP_RECVDSTADDR		0x04	/* receive IP dst address */
#define	INP_HDRINCL		0x08	/* user supplies entire IP header */
#define	INP_HIGHPORT		0x10	/* user wants "high" port binding */
#define	INP_LOWPORT		0x20	/* user wants "low" port binding */
#define	INP_ANONPORT		0x40	/* port chosen for user */
#define	INP_RECVIF		0x80	/* receive incoming interface */
#define	INP_MTUDISC		0x100	/* user can do MTU discovery */
#define	INP_FAITH		0x200	/* accept FAITH'ed connections */
#define	INP_WILDCARD		0x400	/* wildcard match */
#define INP_FLAG_PROTO2		0x800	/* protocol specific */
#define	INP_CONNECTED		0x1000	/* exact match */
#define	INP_FLAG_PROTO1		0x2000	/* protocol specific */
#define INP_PLACEMARKER		0x4000	/* skip this pcb, its a placemarker */

#define IN6P_IPV6_V6ONLY	0x008000 /* restrict AF_INET6 socket for v6 */

#define	IN6P_PKTINFO		0x010000 /* receive IP6 dst and I/F */
#define	IN6P_HOPLIMIT		0x020000 /* receive hoplimit */
#define	IN6P_HOPOPTS		0x040000 /* receive hop-by-hop options */
#define	IN6P_DSTOPTS		0x080000 /* receive dst options after rthdr */
#define	IN6P_RTHDR		0x100000 /* receive routing header */
#define	IN6P_RTHDRDSTOPTS	0x200000 /* receive dstoptions before rthdr */
#define IN6P_AUTOFLOWLABEL	0x800000 /* attach flowlabel automatically */
/* 
 * RFC3542 Definition 
 */
#define	IN6P_TCLASS		0x400000 /* receive traffic class value */
#define	IN6P_RFC2292		0x40000000 /* used RFC2292 API on the socket */
#define	IN6P_MTU		0x80000000 /* receive path MTU */

/* 0x10000000 unused */
#define INP_ONLIST		0x20000000 /* on pcblist */
#define	INP_RECVTTL		0x80000000 /* receive incoming IP TTL */

#define	INP_CONTROLOPTS		(INP_RECVOPTS|INP_RECVRETOPTS|INP_RECVDSTADDR|\
				 INP_RECVIF|INP_RECVTTL|\
				 IN6P_PKTINFO|IN6P_HOPLIMIT|IN6P_HOPOPTS|\
				 IN6P_DSTOPTS|IN6P_RTHDR|IN6P_RTHDRDSTOPTS|\
				 IN6P_TCLASS|IN6P_AUTOFLOWLABEL|IN6P_RFC2292|\
				 IN6P_MTU)
				 
#define	INP_UNMAPPABLEOPTS	(IN6P_HOPOPTS|IN6P_DSTOPTS|IN6P_RTHDR|\
				 IN6P_TCLASS|IN6P_AUTOFLOWLABEL)

 /* for KAME src sync over BSD*'s */
#define	IN6P_HIGHPORT		INP_HIGHPORT
#define	IN6P_LOWPORT		INP_LOWPORT
#define	IN6P_ANONPORT		INP_ANONPORT
#define	IN6P_RECVIF		INP_RECVIF
#define	IN6P_MTUDISC		INP_MTUDISC
#define	IN6P_FAITH		INP_FAITH
#define	IN6P_CONTROLOPTS INP_CONTROLOPTS
	/*
	 * socket AF version is {newer than,or include}
	 * actual datagram AF version
	 */

#define	INPLOOKUP_WILDCARD	1
#define	sotoinpcb(so)	((struct inpcb *)(so)->so_pcb)
#define	sotoin6pcb(so)	sotoinpcb(so) /* for KAME src sync over BSD*'s */

/* macros for handling bitmap of ports not to allocate dynamically */
#define	DP_MAPBITS	(sizeof(u_int32_t) * NBBY)
#define	DP_MAPSIZE	(howmany(65536, DP_MAPBITS))
#define	DP_SET(m, p)	((m)[(p) / DP_MAPBITS] |= (1 << ((p) % DP_MAPBITS)))
#define	DP_CLR(m, p)	((m)[(p) / DP_MAPBITS] &= ~(1 << ((p) % DP_MAPBITS)))
#define	DP_ISSET(m, p)	((m)[(p) / DP_MAPBITS] & (1 << ((p) % DP_MAPBITS)))

/* default values for baddynamicports [see ip_init()] */
#define	DEFBADDYNAMICPORTS_TCP	{ \
	587, 749, 750, 751, 871, 2049, \
	6000, 6001, 6002, 6003, 6004, 6005, 6006, 6007, 6008, 6009, 6010, \
	0 }
#define	DEFBADDYNAMICPORTS_UDP	{ 623, 664, 749, 750, 751, 2049, 0 }

struct baddynamicports {
	u_int32_t tcp[DP_MAPSIZE];
	u_int32_t udp[DP_MAPSIZE];
};


#define	INP_SOCKAF(so) so->so_proto->pr_domain->dom_family

#define	INP_CHECK_SOCKAF(so, af)	(INP_SOCKAF(so) == af)

#ifdef _KERNEL

#define GET_PORT_TOKEN(portinfo) \
do { \
	if ((portinfo)->porttoken) \
		lwkt_gettoken((portinfo)->porttoken); \
} while (0)

#define REL_PORT_TOKEN(portinfo) \
do { \
	if ((portinfo)->porttoken) \
		lwkt_reltoken((portinfo)->porttoken); \
} while (0)

#ifdef INVARIANTS
#define ASSERT_PORT_TOKEN_HELD(portinfo) \
do { \
	if ((portinfo)->porttoken) \
		ASSERT_LWKT_TOKEN_HELD((portinfo)->porttoken); \
} while (0)
#else	/* !INVARIANTS */
#define ASSERT_PORT_TOKEN_HELD(portinfo)
#endif	/* INVARIANTS */

#define GET_PCBINFO_TOKEN(pcbinfo) \
do { \
	if ((pcbinfo)->infotoken) \
		lwkt_gettoken((pcbinfo)->infotoken); \
} while (0)

#define REL_PCBINFO_TOKEN(pcbinfo) \
do { \
	if ((pcbinfo)->infotoken) \
		lwkt_reltoken((pcbinfo)->infotoken); \
} while (0)

#ifdef INVARIANTS
#define ASSERT_PCBINFO_TOKEN_HELD(pcbinfo) \
do { \
	if ((pcbinfo)->infotoken) \
		ASSERT_LWKT_TOKEN_HELD((pcbinfo)->infotoken); \
} while (0)
#else	/* !INVARIANTS */
#define ASSERT_PCBINFO_TOKEN_HELD(pcbinfo)
#endif	/* INVARIANTS */

#ifdef INVARIANTS
#define ASSERT_INP_NOTINHASH(inp) \
do { \
	KASSERT(!((inp)->inp_flags & INP_CONNECTED), \
	    ("already on connhash")); \
	KASSERT(!((inp)->inp_flags & INP_WILDCARD), \
	    ("already on wildcardhash")); \
} while (0)
#else	/* !INVARIANTS */
#define ASSERT_INP_NOTINHASH(inp)
#endif	/* INVARIANTS */

extern int	ipport_lowfirstauto;
extern int	ipport_lowlastauto;
extern int	ipport_firstauto;
extern int	ipport_lastauto;
extern int	ipport_hifirstauto;
extern int	ipport_hilastauto;

union netmsg;
struct xinpcb;

void	in_pcbportrange(u_short *, u_short *, u_short, u_short);
void	in_pcbpurgeif0 (struct inpcbinfo *, struct ifnet *);
void	in_losing (struct inpcb *);
void	in_rtchange (struct inpcb *, int);
void	in_pcbinfo_init (struct inpcbinfo *, int, boolean_t);
void	in_pcbportinfo_init (struct inpcbportinfo *, int, boolean_t, u_short);
int	in_pcballoc (struct socket *, struct inpcbinfo *);
void	in_pcbunlink (struct inpcb *, struct inpcbinfo *);
void	in_pcbunlink_flags (struct inpcb *, struct inpcbinfo *, int);
void	in_pcblink (struct inpcb *, struct inpcbinfo *);
void	in_pcblink_flags (struct inpcb *, struct inpcbinfo *, int);
void	in_pcbonlist (struct inpcb *);
void	in_pcbofflist (struct inpcb *);
int	in_pcbbind (struct inpcb *, struct sockaddr *, struct thread *);
int	in_pcbbind_remote(struct inpcb *, const struct sockaddr *,
	    struct thread *);
int	in_pcbconnect (struct inpcb *, struct sockaddr *, struct thread *);
void	in_pcbdetach (struct inpcb *);
void	in_pcbdisconnect (struct inpcb *);
void	in_pcbinswildcardhash(struct inpcb *inp);
void	in_pcbinswildcardhash_oncpu(struct inpcb *, struct inpcbinfo *);
void	in_pcbinsconnhash(struct inpcb *inp);
void	in_pcbinsporthash (struct inpcbportinfo *, struct inpcb *);
void	in_pcbinsporthash_lport (struct inpcb *);
int	in_pcbladdr (struct inpcb *, struct sockaddr *,
	    struct sockaddr_in **, struct thread *);
int	in_pcbladdr_find (struct inpcb *, struct sockaddr *,
	    struct sockaddr_in **, struct thread *, int);
struct inpcb *
	in_pcblookup_local (struct inpcbportinfo *, struct in_addr, u_int, int,
			    struct ucred *);
struct inpcb *
	in_pcblookup_hash (struct inpcbinfo *,
			       struct in_addr, u_int, struct in_addr, u_int,
			       boolean_t, struct ifnet *);
struct inpcb *
	in_pcblookup_pkthash (struct inpcbinfo *,
			       struct in_addr, u_int, struct in_addr, u_int,
			       boolean_t, struct ifnet *, const struct mbuf *);
void	in_pcbnotifyall (struct inpcbinfo *, struct in_addr,
	    int, void (*)(struct inpcb *, int));
int	in_setpeeraddr (struct socket *so, struct sockaddr **nam);
void	in_setpeeraddr_dispatch(union netmsg *);
int	in_setsockaddr (struct socket *so, struct sockaddr **nam);
void	in_setsockaddr_dispatch(netmsg_t msg);
int	in_baddynamic(u_int16_t, u_int16_t);
void	in_pcbremwildcardhash(struct inpcb *inp);
void	in_pcbremwildcardhash_oncpu(struct inpcb *, struct inpcbinfo *);
void	in_pcbremconnhash(struct inpcb *inp);
void	in_pcbremlists (struct inpcb *inp);
int	prison_xinpcb (struct thread *p, struct inpcb *inp);
void	in_savefaddr (struct socket *so, const struct sockaddr *faddr);
struct inpcb *
	in_pcblocalgroup_last(const struct inpcbinfo *, const struct inpcb *);
void	in_pcbglobalinit(void);
void	in_pcbresetroute(struct inpcb *);

int	in_pcblist_global(SYSCTL_HANDLER_ARGS);
int	in_pcblist_global_ncpus2(SYSCTL_HANDLER_ARGS);

struct inpcb *
	in_pcbmarker(int cpuid);
struct inpcontainer *
	in_pcbcontainer_marker(int cpuid);

#endif /* _KERNEL */

#endif /* !_NETINET_IN_PCB_H_ */
