/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	From: @(#)if.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/if_var.h,v 1.18.2.16 2003/04/15 18:11:19 fjoe Exp $
 * $DragonFly: src/sys/net/if_var.h,v 1.71 2008/11/22 04:00:53 sephe Exp $
 */

#ifndef	_NET_IF_VAR_H_
#define	_NET_IF_VAR_H_

#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif
#ifndef _NET_IF_H_
#include <net/if.h>
#endif

/*
 * Structures defining a network interface, providing a packet
 * transport mechanism (ala level 0 of the PUP protocols).
 *
 * Each interface accepts output datagrams of a specified maximum
 * length, and provides higher level routines with input datagrams
 * received from its medium.
 *
 * Output occurs when the routine if_output is called, with four parameters:
 *	ifp->if_output(ifp, m, dst, rt)
 * Here m is the mbuf chain to be sent and dst is the destination address.
 * The output routine encapsulates the supplied datagram if necessary,
 * and then transmits it on its medium.
 *
 * On input, each interface unwraps the data received by it, and either
 * places it on the input queue of a internetwork datagram routine
 * and posts the associated software interrupt, or passes the datagram to
 * the routine if_input. It is called with the mbuf chain as parameter:
 *	ifp->if_input(ifp, m)
 * The input routine removes the protocol dependent header if necessary.
 *
 * Routines exist for locating interfaces by their addresses
 * or for locating a interface on a certain network, as well as more general
 * routing and gateway routines maintaining information used to locate
 * interfaces.  These routines live in the files if.c and route.c
 */

/*
 * Forward structure declarations for function prototypes [sic].
 */
struct	mbuf;
struct	mbuf_chain;
struct	proc;
struct	rtentry;
struct	rt_addrinfo;
struct	socket;
struct	ether_header;
struct	ucred;
struct	lwkt_serialize;
struct	ifaddr_container;
struct	ifaddr;
struct	lwkt_port;
struct	lwkt_msg;
union	netmsg;
struct	pktinfo;
struct	ifpoll_info;

#include <sys/queue.h>		/* get TAILQ macros */

#include <net/altq/if_altq.h>

#ifdef _KERNEL
#include <sys/eventhandler.h>
#include <sys/mbuf.h>
#include <sys/systm.h>		/* XXX */
#include <sys/thread2.h>
#endif /* _KERNEL */

#define IF_DUNIT_NONE   -1

TAILQ_HEAD(ifnethead, ifnet);	/* we use TAILQs so that the order of */
TAILQ_HEAD(ifaddrhead, ifaddr_container); /* instantiation is preserved in the list */
TAILQ_HEAD(ifprefixhead, ifprefix);
TAILQ_HEAD(ifmultihead, ifmultiaddr);

/*
 * Structure defining a queue for a network interface.
 */
struct	ifqueue {
	struct	mbuf *ifq_head;
	struct	mbuf *ifq_tail;
	int	ifq_len;
	int	ifq_maxlen;
	int	ifq_drops;
};

/*
 * Note of DEVICE_POLLING
 * 1) Any file(*.c) that depends on DEVICE_POLLING supports in this
 *    file should include opt_polling.h at its beginning.
 * 2) When struct changes, which are conditioned by DEVICE_POLLING,
 *    are to be introduced, please keep the struct's size and layout
 *    same, no matter whether DEVICE_POLLING is defined or not.
 *    See ifnet.if_poll and ifnet.if_poll_unused for example.
 */

#ifdef DEVICE_POLLING
enum poll_cmd { POLL_ONLY, POLL_AND_CHECK_STATUS, POLL_DEREGISTER,
		POLL_REGISTER };
#endif

enum ifnet_serialize {
	IFNET_SERIALIZE_ALL,
	IFNET_SERIALIZE_MAIN,
	IFNET_SERIALIZE_TX_BASE = 0x10000000,
	IFNET_SERIALIZE_RX_BASE = 0x20000000
};
#define IFNET_SERIALIZE_TX	IFNET_SERIALIZE_TX_BASE
#define IFNET_SERIALIZE_RX(i)	(IFNET_SERIALIZE_RX_BASE + (i))

/*
 * Structure defining a network interface.
 *
 * (Would like to call this struct ``if'', but C isn't PL/1.)
 */

/*
 * NB: For FreeBSD, it is assumed that each NIC driver's softc starts with  
 * one of these structures, typically held within an arpcom structure.   
 * 
 *	struct <foo>_softc {
 *		struct arpcom {
 *			struct  ifnet ac_if;
 *			...
 *		} <arpcom> ;
 *		...   
 *	};
 *
 * The assumption is used in a number of places, including many
 * files in sys/net, device drivers, and sys/dev/mii.c:miibus_attach().
 * 
 * Unfortunately devices' softc are opaque, so we depend on this layout
 * to locate the struct ifnet from the softc in the generic code.
 *
 * MPSAFE NOTES: 
 *
 * ifnet is protected by calling if_serialize, if_tryserialize and
 * if_deserialize serialize functions with the ifnet_serialize parameter.
 * ifnet.if_snd is protected by its own spinlock.  Callers of if_ioctl,
 * if_watchdog, if_init, if_resolvemulti, and if_poll should call the
 * ifnet serialize functions with IFNET_SERIALIZE_ALL.  Callers of if_start
 * sould call the ifnet serialize functions with IFNET_SERIALIZE_TX.
 *
 * FIXME: Device drivers usually use the same serializer for their interrupt
 * FIXME: but this is not required.
 *
 * Caller of if_output must not serialize ifnet by calling ifnet serialize
 * functions; if_output will call the ifnet serialize functions based on
 * its own needs.  Caller of if_input does not necessarily hold the related
 * serializer.
 *
 * If a device driver installs the same serializer for its interrupt
 * as for ifnet, then the driver only really needs to worry about further
 * serialization in timeout based entry points.  All other entry points
 * will already be serialized.  Older ISA drivers still using the old
 * interrupt infrastructure will have to obtain and release the serializer
 * in their interrupt routine themselves.
 */
struct ifnet {
	void	*if_softc;		/* pointer to driver state */
	void	*if_l2com;		/* pointer to protocol bits */
	TAILQ_ENTRY(ifnet) if_link;	/* all struct ifnets are chained */
	char	if_xname[IFNAMSIZ];	/* external name (name + unit) */
	const char *if_dname;		/* driver name */
	int	if_dunit;		/* unit or IF_DUNIT_NONE */
	void	*if_vlantrunks;		/* vlan trunks */
	struct	ifaddrhead *if_addrheads; /* array[NCPU] of TAILQs of addresses per if */
	int	if_pcount;		/* number of promiscuous listeners */
	void	*if_carp;		/* carp interfaces */
	struct	bpf_if *if_bpf;		/* packet filter structure */
	u_short	if_index;		/* numeric abbreviation for this if  */
	short	if_timer;		/* time 'til if_watchdog called */
	int	if_flags;		/* up/down, broadcast, etc. */
	int	if_capabilities;	/* interface capabilities */
	int	if_capenable;		/* enabled features */
	void	*if_linkmib;		/* link-type-specific MIB data */
	size_t	if_linkmiblen;		/* length of above data */
	struct	if_data if_data;
	struct	ifmultihead if_multiaddrs; /* multicast addresses configured */
	int	if_amcount;		/* number of all-multicast requests */
/* procedure handles */
	int	(*if_output)		/* output routine (enqueue) */
		(struct ifnet *, struct mbuf *, struct sockaddr *,
		     struct rtentry *);
	void	(*if_input)		/* input routine from hardware driver */
		(struct ifnet *, struct mbuf *);
	void	(*if_start)		/* initiate output routine */
		(struct ifnet *);
	int	(*if_ioctl)		/* ioctl routine */
		(struct ifnet *, u_long, caddr_t, struct ucred *);
	void	(*if_watchdog)		/* timer routine */
		(struct ifnet *);
	void	(*if_init)		/* Init routine */
		(void *);
	int	(*if_resolvemulti)	/* validate/resolve multicast */
		(struct ifnet *, struct sockaddr **, struct sockaddr *);
	int	(*if_start_cpuid)	/* cpuid to run if_start */
		(struct ifnet *);
	TAILQ_HEAD(, ifg_list) if_groups; /* linked list of groups per if */
					/* protected by if_addr_mtx */
#ifdef DEVICE_POLLING
	void	(*if_poll)		/* IFF_POLLING support */
		(struct ifnet *, enum poll_cmd, int);
	int	if_poll_cpuid;
#else
	/* Place holders */
	void	(*if_poll_unused)(void);
	int	if_poll_cpuid_used;
#endif
	void	(*if_serialize)
		(struct ifnet *, enum ifnet_serialize);
	void	(*if_deserialize)
		(struct ifnet *, enum ifnet_serialize);
	int	(*if_tryserialize)
		(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
	void	(*if_serialize_assert)
		(struct ifnet *, enum ifnet_serialize, boolean_t);
#else
	/* Place holders */
	void	(*if_serialize_unused)(void);
#endif
#ifdef IFPOLL_ENABLE
	void	(*if_qpoll)
		(struct ifnet *, struct ifpoll_info *);
#else
	/* Place holders */
	void	(*if_qpoll_unused)(void);
#endif
	struct	ifaltq if_snd;		/* output queue (includes altq) */
	struct	ifprefixhead if_prefixhead; /* list of prefixes per if */
	const uint8_t	*if_broadcastaddr;
	void	*if_bridge;		/* bridge glue */
	void	*if_afdata[AF_MAX];
	struct ifaddr	*if_lladdr;
	struct lwkt_serialize *if_serializer;	/* serializer or MP lock */
	struct lwkt_serialize if_default_serializer; /* if not supplied */
	int	if_cpuid;
	struct netmsg_base *if_start_nmsg; /* percpu msgs to sched if_start */
	void	*if_pf_kif; /* pf interface abstraction */
 	union {
 		void *carp_s;		/* carp structure (used by !carp ifs) */
 		struct ifnet *carp_d;	/* ptr to carpdev (used by carp ifs) */
 	} if_carp_ptr;
	#define if_carp		if_carp_ptr.carp_s
	#define if_carpdev	if_carp_ptr.carp_d
};
typedef void if_init_f_t (void *);

#define	if_mtu		if_data.ifi_mtu
#define	if_type		if_data.ifi_type
#define if_physical	if_data.ifi_physical
#define	if_addrlen	if_data.ifi_addrlen
#define	if_hdrlen	if_data.ifi_hdrlen
#define	if_metric	if_data.ifi_metric
#define	if_link_state	if_data.ifi_link_state
#define	if_baudrate	if_data.ifi_baudrate
#define	if_hwassist	if_data.ifi_hwassist
#define	if_ipackets	if_data.ifi_ipackets
#define	if_ierrors	if_data.ifi_ierrors
#define	if_opackets	if_data.ifi_opackets
#define	if_oerrors	if_data.ifi_oerrors
#define	if_collisions	if_data.ifi_collisions
#define	if_ibytes	if_data.ifi_ibytes
#define	if_obytes	if_data.ifi_obytes
#define	if_imcasts	if_data.ifi_imcasts
#define	if_omcasts	if_data.ifi_omcasts
#define	if_iqdrops	if_data.ifi_iqdrops
#define	if_noproto	if_data.ifi_noproto
#define	if_lastchange	if_data.ifi_lastchange
#define if_recvquota	if_data.ifi_recvquota
#define	if_xmitquota	if_data.ifi_xmitquota
#define if_rawoutput(if, m, sa) if_output(if, m, sa, NULL)

/* for compatibility with other BSDs */
#define	if_list		if_link


/*
 * Output queues (ifp->if_snd) and slow device input queues (*ifp->if_slowq)
 * are queues of messages stored on ifqueue structures
 * (defined above).  Entries are added to and deleted from these structures
 * by these macros, which should be called with ipl raised to splimp().
 */
#define	IF_QFULL(ifq)		((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define	IF_DROP(ifq)		((ifq)->ifq_drops++)
#define	IF_QLEN(ifq)		((ifq)->ifq_len)
#define	IF_QEMPTY(ifq)		(IF_QLEN(ifq) == 0)
#define	IF_ENQUEUE(ifq, m) do {						\
	(m)->m_nextpkt = 0;						\
	if ((ifq)->ifq_tail == 0)					\
		(ifq)->ifq_head = m;					\
	else								\
		(ifq)->ifq_tail->m_nextpkt = m;				\
	(ifq)->ifq_tail = m;						\
	(ifq)->ifq_len++;						\
} while (0)
#define	IF_PREPEND(ifq, m) do {						\
	(m)->m_nextpkt = (ifq)->ifq_head;				\
	if ((ifq)->ifq_tail == 0)					\
		(ifq)->ifq_tail = (m);					\
	(ifq)->ifq_head = (m);						\
	(ifq)->ifq_len++;						\
} while (0)
#define	IF_DEQUEUE(ifq, m) do {						\
	(m) = (ifq)->ifq_head;						\
	if (m) {							\
		if (((ifq)->ifq_head = (m)->m_nextpkt) == 0)		\
			(ifq)->ifq_tail = 0;				\
		(m)->m_nextpkt = 0;					\
		(ifq)->ifq_len--;					\
	}								\
} while (0)

#define	IF_POLL(ifq, m)		((m) = (ifq)->ifq_head)

#define IF_DRAIN(ifq) do {						\
	struct mbuf *m;							\
	while (1) {							\
		IF_DEQUEUE(ifq, m);					\
		if (m == NULL)						\
			break;						\
		m_freem(m);						\
	}								\
} while (0)

#ifdef _KERNEL

/* interface link layer address change event */
typedef void (*iflladdr_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(iflladdr_event, iflladdr_event_handler_t);

#ifdef INVARIANTS
#define ASSERT_IFNET_SERIALIZED_ALL(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_ALL, TRUE)
#define ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_ALL, FALSE)

#define ASSERT_IFNET_SERIALIZED_TX(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_TX, TRUE)
#define ASSERT_IFNET_NOT_SERIALIZED_TX(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_TX, FALSE)

#define ASSERT_IFNET_SERIALIZED_MAIN(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_MAIN, TRUE)
#define ASSERT_IFNET_NOT_SERIALIZED_MAIN(ifp) \
	(ifp)->if_serialize_assert((ifp), IFNET_SERIALIZE_MAIN, FALSE)
#else
#define ASSERT_IFNET_SERIALIZED_ALL(ifp)	((void)0)
#define ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp)	((void)0)
#define ASSERT_IFNET_SERIALIZED_TX(ifp)		((void)0)
#define ASSERT_IFNET_NOT_SERIALIZED_TX(ifp)	((void)0)
#define ASSERT_IFNET_SERIALIZED_MAIN(ifp)	((void)0)
#define ASSERT_IFNET_NOT_SERIALIZED_MAIN(ifp)	((void)0)
#endif

static __inline void
ifnet_serialize_all(struct ifnet *_ifp)
{
	_ifp->if_serialize(_ifp, IFNET_SERIALIZE_ALL);
}

static __inline void
ifnet_deserialize_all(struct ifnet *_ifp)
{
	_ifp->if_deserialize(_ifp, IFNET_SERIALIZE_ALL);
}

static __inline int
ifnet_tryserialize_all(struct ifnet *_ifp)
{
	return _ifp->if_tryserialize(_ifp, IFNET_SERIALIZE_ALL);
}

static __inline void
ifnet_serialize_tx(struct ifnet *_ifp)
{
	_ifp->if_serialize(_ifp, IFNET_SERIALIZE_TX);
}

static __inline void
ifnet_deserialize_tx(struct ifnet *_ifp)
{
	_ifp->if_deserialize(_ifp, IFNET_SERIALIZE_TX);
}

static __inline int
ifnet_tryserialize_tx(struct ifnet *_ifp)
{
	return _ifp->if_tryserialize(_ifp, IFNET_SERIALIZE_TX);
}

static __inline void
ifnet_serialize_main(struct ifnet *_ifp)
{
	_ifp->if_serialize(_ifp, IFNET_SERIALIZE_MAIN);
}

static __inline void
ifnet_deserialize_main(struct ifnet *_ifp)
{
	_ifp->if_deserialize(_ifp, IFNET_SERIALIZE_MAIN);
}

static __inline int
ifnet_tryserialize_main(struct ifnet *_ifp)
{
	return _ifp->if_tryserialize(_ifp, IFNET_SERIALIZE_MAIN);
}

/*
 * DEPRECATED - should not be used by any new driver.  This code uses the
 * old queueing interface and if_start ABI and does not use the ifp's
 * serializer.
 */
#define IF_HANDOFF(ifq, m, ifp)			if_handoff(ifq, m, ifp, 0)
#define IF_HANDOFF_ADJ(ifq, m, ifp, adj)	if_handoff(ifq, m, ifp, adj)

static __inline int
if_handoff(struct ifqueue *_ifq, struct mbuf *_m, struct ifnet *_ifp,
	   int _adjust)
{
	int _need_if_start = 0;

	crit_enter(); 

	if (IF_QFULL(_ifq)) {
		IF_DROP(_ifq);
		crit_exit();
		m_freem(_m);
		return (0);
	}
	if (_ifp != NULL) {
		_ifp->if_obytes += _m->m_pkthdr.len + _adjust;
		if (_m->m_flags & M_MCAST)
			_ifp->if_omcasts++;
		_need_if_start = !(_ifp->if_flags & IFF_OACTIVE);
	}
	IF_ENQUEUE(_ifq, _m);
	if (_need_if_start) {
		(*_ifp->if_start)(_ifp);
	}
	crit_exit();
	return (1);
}

/*
 * 72 was chosen below because it is the size of a TCP/IP
 * header (40) + the minimum mss (32).
 */
#define	IF_MINMTU	72
#define	IF_MAXMTU	65535

#endif /* _KERNEL */

struct in_ifaddr;

struct in_ifaddr_container {
	struct in_ifaddr	*ia;
	LIST_ENTRY(in_ifaddr_container) ia_hash;
				/* entry in bucket of inet addresses */
	TAILQ_ENTRY(in_ifaddr_container) ia_link;
				/* list of internet addresses */
	struct ifaddr_container	*ia_ifac; /* parent ifaddr_container */
};

struct ifaddr_container {
#define IFA_CONTAINER_MAGIC	0x19810219
#define IFA_CONTAINER_DEAD	0xc0dedead
	uint32_t		ifa_magic;  /* IFA_CONTAINER_MAGIC */
	struct ifaddr		*ifa;
	TAILQ_ENTRY(ifaddr_container)	ifa_link;   /* queue macro glue */
	u_int			ifa_refcnt; /* references to this structure */
	uint16_t		ifa_listmask;	/* IFA_LIST_ */
	uint16_t		ifa_prflags;	/* protocol specific flags */

	/*
	 * Protocol specific states
	 */
	union {
		struct in_ifaddr_container u_in_ifac;
	} ifa_proto_u;
};

#define IFA_LIST_IFADDRHEAD	0x01	/* on ifnet.if_addrheads[cpuid] */
#define IFA_LIST_IN_IFADDRHEAD	0x02	/* on in_ifaddrheads[cpuid] */
#define IFA_LIST_IN_IFADDRHASH	0x04	/* on in_ifaddrhashtbls[cpuid] */

#define IFA_PRF_FLAG0		0x01
#define IFA_PRF_FLAG1		0x02
#define IFA_PRF_FLAG2		0x04
#define IFA_PRF_FLAG3		0x08

/*
 * The ifaddr structure contains information about one address
 * of an interface.  They are maintained by the different address families,
 * are allocated and attached when an address is set, and are linked
 * together so all addresses for an interface can be located.
 */
struct ifaddr {
	struct	sockaddr *ifa_addr;	/* address of interface */
	struct	sockaddr *ifa_dstaddr;	/* other end of p-to-p link */
#define	ifa_broadaddr	ifa_dstaddr	/* broadcast address interface */
	struct	sockaddr *ifa_netmask;	/* used to determine subnet */
	struct	if_data if_data;	/* not all members are meaningful */
	struct	ifnet *ifa_ifp;		/* back-pointer to interface */
	void	*ifa_link_pad;
	struct ifaddr_container *ifa_containers;
	void	(*ifa_rtrequest)	/* check or clean routes (+ or -)'d */
		(int, struct rtentry *, struct rt_addrinfo *);
	u_short	ifa_flags;		/* mostly rt_flags for cloning */
	int	ifa_ncnt;		/* # of valid ifaddr_container */
	int	ifa_metric;		/* cost of going out this interface */
#ifdef notdef
	struct	rtentry *ifa_rt;	/* XXXX for ROUTETOIF ????? */
#endif
	int (*ifa_claim_addr)		/* check if an addr goes to this if */
		(struct ifaddr *, struct sockaddr *);

};
#define	IFA_ROUTE	RTF_UP		/* route installed */

/* for compatibility with other BSDs */
#define	ifa_list	ifa_link

/*
 * The prefix structure contains information about one prefix
 * of an interface.  They are maintained by the different address families,
 * are allocated and attached when an prefix or an address is set,
 * and are linked together so all prefixes for an interface can be located.
 */
struct ifprefix {
	struct	sockaddr *ifpr_prefix;	/* prefix of interface */
	struct	ifnet *ifpr_ifp;	/* back-pointer to interface */
	TAILQ_ENTRY(ifprefix) ifpr_list; /* queue macro glue */
	u_char	ifpr_plen;		/* prefix length in bits */
	u_char	ifpr_type;		/* protocol dependent prefix type */
};

/*
 * Multicast address structure.  This is analogous to the ifaddr
 * structure except that it keeps track of multicast addresses.
 * Also, the reference count here is a count of requests for this
 * address, not a count of pointers to this structure.
 */
struct ifmultiaddr {
	TAILQ_ENTRY(ifmultiaddr) ifma_link; /* queue macro glue */
	struct	sockaddr *ifma_addr;	/* address this membership is for */
	struct	sockaddr *ifma_lladdr;	/* link-layer translation, if any */
	struct	ifnet *ifma_ifp;	/* back-pointer to interface */
	u_int	ifma_refcount;		/* reference count */
	void	*ifma_protospec;	/* protocol-specific state, if any */
};

#ifdef _KERNEL

enum ifaddr_event {
	IFADDR_EVENT_ADD,
	IFADDR_EVENT_DELETE,
	IFADDR_EVENT_CHANGE
};

/* interface address change event */
typedef void (*ifaddr_event_handler_t)(void *, struct ifnet *,
	enum ifaddr_event, struct ifaddr *);
EVENTHANDLER_DECLARE(ifaddr_event, ifaddr_event_handler_t);
/* new interface attach event */
typedef void (*ifnet_attach_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_attach_event, ifnet_attach_event_handler_t);
/* interface detach event */
typedef void (*ifnet_detach_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_detach_event, ifnet_detach_event_handler_t);

/*
 * interface groups
 */
struct ifg_group {
	char				 ifg_group[IFNAMSIZ];
	u_int				 ifg_refcnt;
	void				*ifg_pf_kif;
	int				 ifg_carp_demoted;
	TAILQ_HEAD(, ifg_member)	 ifg_members;
	TAILQ_ENTRY(ifg_group)		 ifg_next;
};

struct ifg_member {
	TAILQ_ENTRY(ifg_member)	 ifgm_next;
	struct ifnet		*ifgm_ifp;
};

struct ifg_list {
	struct ifg_group	*ifgl_group;
	TAILQ_ENTRY(ifg_list)	 ifgl_next;
};

/* group attach event */
typedef void (*group_attach_event_handler_t)(void *, struct ifg_group *);
EVENTHANDLER_DECLARE(group_attach_event, group_attach_event_handler_t);
/* group detach event */
typedef void (*group_detach_event_handler_t)(void *, struct ifg_group *);
EVENTHANDLER_DECLARE(group_detach_event, group_detach_event_handler_t);
/* group change event */
typedef void (*group_change_event_handler_t)(void *, const char *);
EVENTHANDLER_DECLARE(group_change_event, group_change_event_handler_t);


#ifdef INVARIANTS
#define ASSERT_IFAC_VALID(ifac)	do { \
	KKASSERT((ifac)->ifa_magic == IFA_CONTAINER_MAGIC); \
	KKASSERT((ifac)->ifa_refcnt > 0); \
} while (0)
#else
#define ASSERT_IFAC_VALID(ifac)	((void)0)
#endif

static __inline void
_IFAREF(struct ifaddr *_ifa, int _cpu_id)
{
	struct ifaddr_container *_ifac = &_ifa->ifa_containers[_cpu_id];

	crit_enter();
	ASSERT_IFAC_VALID(_ifac);
	++_ifac->ifa_refcnt;
	crit_exit();
}

static __inline void
IFAREF(struct ifaddr *_ifa)
{
	_IFAREF(_ifa, mycpuid);
}

#include <sys/malloc.h>

MALLOC_DECLARE(M_IFADDR);
MALLOC_DECLARE(M_IFMADDR);
MALLOC_DECLARE(M_IFNET);

void	ifac_free(struct ifaddr_container *, int);

static __inline void
_IFAFREE(struct ifaddr *_ifa, int _cpu_id)
{
	struct ifaddr_container *_ifac = &_ifa->ifa_containers[_cpu_id];

	crit_enter();
	ASSERT_IFAC_VALID(_ifac);
	if (--_ifac->ifa_refcnt == 0)
		ifac_free(_ifac, _cpu_id);
	crit_exit();
}

static __inline void
IFAFREE(struct ifaddr *_ifa)
{
	_IFAFREE(_ifa, mycpuid);
}

struct lwkt_port *ifnet_portfn(int);
int	ifnet_domsg(struct lwkt_msg *, int);
void	ifnet_sendmsg(struct lwkt_msg *, int);
void	ifnet_forwardmsg(struct lwkt_msg *, int);
struct ifnet *ifnet_byindex(unsigned short);

static __inline int
ifa_domsg(struct lwkt_msg *_lmsg, int _cpu)
{
	return ifnet_domsg(_lmsg, _cpu);
}

static __inline void
ifa_sendmsg(struct lwkt_msg *_lmsg, int _cpu)
{
	ifnet_sendmsg(_lmsg, _cpu);
}

static __inline void
ifa_forwardmsg(struct lwkt_msg *_lmsg, int _nextcpu)
{
	ifnet_forwardmsg(_lmsg, _nextcpu);
}

#define REINPUT_KEEPRCVIF	0x0001	/* ether_reinput_oncpu() */
#define REINPUT_RUNBPF 		0x0002	/* ether_reinput_oncpu() */


extern	struct ifnethead ifnet;
extern struct	ifnet	**ifindex2ifnet;
extern	int ifqmaxlen;
extern	struct ifnet loif[];
extern	int if_index;

void	ether_ifattach(struct ifnet *, uint8_t *, struct lwkt_serialize *);
void	ether_ifattach_bpf(struct ifnet *, uint8_t *, u_int, u_int,
			struct lwkt_serialize *);
void	ether_ifdetach(struct ifnet *);
void	ether_demux_oncpu(struct ifnet *, struct mbuf *);
void	ether_reinput_oncpu(struct ifnet *, struct mbuf *, int);
void	ether_input_chain(struct ifnet *, struct mbuf *,
			  const struct pktinfo *, struct mbuf_chain *);
void	ether_input_chain_init(struct mbuf_chain *);
void	ether_input_dispatch(struct mbuf_chain *);
int	ether_output_frame(struct ifnet *, struct mbuf *);
int	ether_ioctl(struct ifnet *, int, caddr_t);
struct ifnet *ether_bridge_interface(struct ifnet *ifp);
uint32_t	ether_crc32_le(const uint8_t *, size_t);
uint32_t	ether_crc32_be(const uint8_t *, size_t);

int	if_addmulti(struct ifnet *, struct sockaddr *, struct ifmultiaddr **);
int	if_allmulti(struct ifnet *, int);
void	if_attach(struct ifnet *, struct lwkt_serialize *);
int	if_delmulti(struct ifnet *, struct sockaddr *);
void	if_delallmulti(struct ifnet *ifp);
void	if_purgeaddrs_nolink(struct ifnet *);
void	if_detach(struct ifnet *);
void	if_down(struct ifnet *);
void	if_link_state_change(struct ifnet *);
void	if_initname(struct ifnet *, const char *, int);
int	if_getanyethermac(uint16_t *, int);
int	if_printf(struct ifnet *, const char *, ...) __printflike(2, 3);
struct ifnet *if_alloc(uint8_t);
void	if_free(struct ifnet *);
void	if_route(struct ifnet *, int flag, int fam);
int	if_setlladdr(struct ifnet *, const u_char *, int);
void	if_unroute(struct ifnet *, int flag, int fam);
void	if_up(struct ifnet *);
/*void	ifinit(void);*/ /* declared in systm.h for main() */
int	ifioctl(struct socket *, u_long, caddr_t, struct ucred *);
int	ifpromisc(struct ifnet *, int);
struct	ifnet *ifunit(const char *);
struct	ifnet *if_withname(struct sockaddr *);

struct	ifg_group *if_creategroup(const char *);
int     if_addgroup(struct ifnet *, const char *);
int     if_delgroup(struct ifnet *, const char *);
int     if_getgroup(caddr_t, struct ifnet *);
int     if_getgroupmembers(caddr_t);

struct	ifaddr *ifa_ifwithaddr(struct sockaddr *);
struct	ifaddr *ifa_ifwithdstaddr(struct sockaddr *);
struct	ifaddr *ifa_ifwithnet(struct sockaddr *);
struct	ifaddr *ifa_ifwithroute(int, struct sockaddr *, struct sockaddr *);
struct	ifaddr *ifaof_ifpforaddr(struct sockaddr *, struct ifnet *);

typedef void *if_com_alloc_t(u_char type, struct ifnet *ifp);
typedef void if_com_free_t(void *com, u_char type);
void    if_register_com_alloc(u_char, if_com_alloc_t *a, if_com_free_t *);
void    if_deregister_com_alloc(u_char);

void	*ifa_create(int, int);
void	ifa_destroy(struct ifaddr *);
void	ifa_iflink(struct ifaddr *, struct ifnet *, int);
void	ifa_ifunlink(struct ifaddr *, struct ifnet *);

struct ifaddr *ifaddr_byindex(unsigned short);

struct	ifmultiaddr *ifmaof_ifpforaddr(struct sockaddr *, struct ifnet *);
int	if_simloop(struct ifnet *ifp, struct mbuf *m, int af, int hlen);
void	if_devstart(struct ifnet *ifp);

#define IF_LLSOCKADDR(ifp)						\
    ((struct sockaddr_dl *)(ifp)->if_lladdr->ifa_addr)
#define IF_LLADDR(ifp)	LLADDR(IF_LLSOCKADDR(ifp))

#ifdef DEVICE_POLLING
typedef	void poll_handler_t (struct ifnet *ifp, enum poll_cmd cmd, int count);
int	ether_poll_register(struct ifnet *);
int	ether_poll_deregister(struct ifnet *);
int	ether_pollcpu_register(struct ifnet *, int);
#endif /* DEVICE_POLLING */

#ifdef IFPOLL_ENABLE
int	ifpoll_register(struct ifnet *);
int	ifpoll_deregister(struct ifnet *);
#endif	/* IFPOLL_ENABLE */

#endif /* _KERNEL */

#endif /* !_NET_IF_VAR_H_ */
