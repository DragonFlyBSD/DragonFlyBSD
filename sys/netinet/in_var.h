/*
 * Copyright (c) 1985, 1986, 1993
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
 *	@(#)in_var.h	8.2 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/netinet/in_var.h,v 1.33.2.3 2001/12/14 20:09:34 jlemon Exp $
 * $DragonFly: src/sys/netinet/in_var.h,v 1.16 2008/10/26 07:11:28 sephe Exp $
 */

#ifndef _NETINET_IN_VAR_H_
#define _NETINET_IN_VAR_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_FNV_HASH_H_
#include <sys/fnv_hash.h>
#endif
#ifndef _NET_IF_VAR_H_
#include <net/if_var.h>
#endif
#ifndef _NETINET_IN_H_
#include <netinet/in.h>
#endif

/*
 * Interface address, Internet version.  One of these structures
 * is allocated for each Internet address on an interface.
 * The ifaddr structure contains the protocol-independent part
 * of the structure and is assumed to be first.
 */
struct in_ifaddr {
	struct	ifaddr ia_ifa;		/* protocol-independent info */
#define	ia_ifp		ia_ifa.ifa_ifp
#define ia_flags	ia_ifa.ifa_flags
					/* ia_{,sub}net{,mask} in host order */
	u_long	ia_net;			/* network number of interface */
	u_long	ia_netmask;		/* mask of net part */
	u_long	ia_subnet;		/* subnet number, including net */
	u_long	ia_subnetmask;		/* mask of subnet part */
	struct	in_addr ia_netbroadcast; /* to recognize net broadcasts */
	void	*ia_pad1[2];
	void	*ia_pad2[2];
	struct	sockaddr_in ia_addr;	/* reserve space for interface name */
	struct	sockaddr_in ia_dstaddr; /* reserve space for broadcast addr */
#define	ia_broadaddr	ia_dstaddr
	struct	sockaddr_in ia_sockmask; /* reserve space for general netmask */
};

struct	in_aliasreq {
	char	ifra_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	struct	sockaddr_in ifra_addr;
	struct	sockaddr_in ifra_broadaddr;
#define ifra_dstaddr ifra_broadaddr
	struct	sockaddr_in ifra_mask;
};
/*
 * Given a pointer to an in_ifaddr (ifaddr),
 * return a pointer to the addr as a sockaddr_in.
 */
#define IA_SIN(ia)    (&(((struct in_ifaddr *)(ia))->ia_addr))
#define IA_DSTSIN(ia) (&(((struct in_ifaddr *)(ia))->ia_dstaddr))

#define IN_LNAOF(in, ifa) \
	((ntohl((in).s_addr) & ~((struct in_ifaddr *)(ifa)->ia_subnetmask))


#ifdef	_KERNEL
struct in_ifaddr_container;

extern	struct	in_addr zeroin_addr;
extern	u_char	inetctlerrmap[];

/*
 * Hash table for IP addresses.
 */
extern	LIST_HEAD(in_ifaddrhashhead, in_ifaddr_container) *in_ifaddrhashtbls[];
extern	TAILQ_HEAD(in_ifaddrhead, in_ifaddr_container) in_ifaddrheads[];
extern	u_long in_ifaddrhmask;			/* mask for hash table */

#define INADDR_NHASH_LOG2       9
#define INADDR_NHASH		(1 << INADDR_NHASH_LOG2)
#define INADDR_HASHVAL(x)	fnv_32_buf((&(x)), sizeof(x), FNV1_32_INIT)
#define INADDR_HASH(x) \
	(&in_ifaddrhashtbls[mycpuid][INADDR_HASHVAL(x) & in_ifaddrhmask])


/*
 * Function for finding the interface (ifnet structure) corresponding to one
 * of our IP addresses.
 */
static __inline struct ifnet *
INADDR_TO_IFP(const struct in_addr *_addr)
{
	struct in_ifaddr_container *_iac;

	LIST_FOREACH(_iac, INADDR_HASH(_addr->s_addr), ia_hash) {
		if (IA_SIN(_iac->ia)->sin_addr.s_addr == _addr->s_addr)
			return _iac->ia->ia_ifp;
	}
	return NULL;
}

/*
 * Function for finding the internet address structure (in_ifaddr) corresponding
 * to a given interface (ifnet structure).
 */
static __inline struct in_ifaddr *
IFP_TO_IA(const struct ifnet *_ifp)
{
	struct in_ifaddr_container *_iac;

	TAILQ_FOREACH(_iac, &in_ifaddrheads[mycpuid], ia_link)
		if (_iac->ia->ia_ifp == _ifp)
			return _iac->ia;
	return NULL;
}

#endif	/* _KERNEL */

/*
 * This information should be part of the ifnet structure but we don't wish
 * to change that - as it might break a number of things
 */

struct router_info {
	struct ifnet *rti_ifp;
	int    rti_type; /* type of router which is querier on this interface */
	int    rti_time; /* # of slow timeouts since last old query */
	struct router_info *rti_next;
};

/*
 * Internet multicast address structure.  There is one of these for each IP
 * multicast group to which this host belongs on a given network interface.
 * For every entry on the interface's if_multiaddrs list which represents
 * an IP multicast group, there is one of these structures.  They are also
 * kept on a system-wide list to make it easier to keep our legacy IGMP code
 * compatible with the rest of the world (see IN_FIRST_MULTI et al, below).
 */
struct in_multi {
	LIST_ENTRY(in_multi) inm_link;	/* queue macro glue */
	struct	in_addr inm_addr;	/* IP multicast address, convenience */
	struct	ifnet *inm_ifp;		/* back pointer to ifnet */
	struct	ifmultiaddr *inm_ifma;	/* back pointer to ifmultiaddr */
	u_int	inm_timer;		/* IGMP membership report timer */
	u_int	inm_state;		/*  state of the membership */
	struct	router_info *inm_rti;	/* router info*/
};

#ifdef _KERNEL

#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_inet_ip);
SYSCTL_DECL(_net_inet_raw);
#endif

extern LIST_HEAD(in_multihead, in_multi) in_multihead;

/*
 * Structure used by macros below to remember position when stepping through
 * all of the in_multi records.
 */
struct in_multistep {
	struct in_multi *i_inm;
};

/*
 * Look up the in_multi record for a given IP multicast address on a given
 * interface.  If no matching record is found, NULL is returned.
 */
static __inline struct in_multi *
IN_LOOKUP_MULTI(const struct in_addr *_addr, struct ifnet *_ifp)
{
	const struct ifmultiaddr *_ifma;
	struct in_multi *_inm = NULL;

	/* TODO: need ifnet_serialize_main */
	ifnet_serialize_all(_ifp);
	TAILQ_FOREACH(_ifma, &_ifp->if_multiaddrs, ifma_link) {
		if (_ifma->ifma_addr->sa_family == AF_INET &&
		    ((struct sockaddr_in *)_ifma->ifma_addr)->sin_addr.s_addr ==
		    _addr->s_addr) {
			_inm = _ifma->ifma_protospec;
			break;
		}
	}
	ifnet_deserialize_all(_ifp);

	return _inm;
}

/*
 * Macro to step through all of the in_multi records, one at a time.
 * The current position is remembered in "step", which the caller must
 * provide.  IN_FIRST_MULTI(), below, must be called to initialize "step"
 * and get the first record.  Both macros return a NULL "inm" when there
 * are no remaining records.
 */
#define IN_NEXT_MULTI(step, inm) \
	/* struct in_multistep  step; */ \
	/* struct in_multi *inm; */ \
do { \
	if (((inm) = (step).i_inm) != NULL) \
		(step).i_inm = LIST_NEXT((step).i_inm, inm_link); \
} while(0)

#define IN_FIRST_MULTI(step, inm) \
	/* struct in_multistep step; */ \
	/* struct in_multi *inm; */ \
do { \
	(step).i_inm = LIST_FIRST(&in_multihead); \
	IN_NEXT_MULTI((step), (inm)); \
} while(0)

struct	route;
struct  lwkt_serialize;
union	netmsg;

void	in_ifdetach(struct ifnet *ifp);
struct	in_multi *in_addmulti (struct in_addr *, struct ifnet *);
void	in_delmulti (struct in_multi *);
int	in_control (struct socket *, u_long, caddr_t, struct ifnet *,
			struct thread *);
void	in_control_dispatch(union netmsg *);
void	in_rtqdrain (void);
void	ip_input (struct mbuf *);
void	ip_forward (struct mbuf *, boolean_t, struct sockaddr_in *);
int	in_ifadown (struct ifaddr *ifa, int);
int	in_ifadown_force (struct ifaddr *ifa, int);
void	in_ifscrub (struct ifnet *, struct in_ifaddr *);
void	in_iaunlink (struct in_ifaddr *);
void	in_iahash_insert (struct in_ifaddr *);
void	in_iahash_remove (struct in_ifaddr *);

#endif /* _KERNEL */

/* INET6 stuff */
#include <netinet6/in6_var.h>

#endif /* _NETINET_IN_VAR_H_ */
