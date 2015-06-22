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
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 * $FreeBSD: src/sys/netinet/in.h,v 1.48.2.10 2003/08/24 08:24:38 hsu Exp $
 */

#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

#include <sys/types.h>

#ifndef _KERNEL
#include <machine/endian.h>
#ifndef htonl
#define	htonl(x)	__htonl(x)
#endif
#ifndef htons
#define	htons(x)	__htons(x)
#endif
#ifndef ntohl
#define	ntohl(x)	__ntohl(x)
#endif
#ifndef ntohs
#define	ntohs(x)	__ntohs(x)
#endif
#endif

/*
 * Constants and structures defined by the internet system,
 * Per RFC 790, September 1981, and numerous additions.
 */

/*
 * Protocols (RFC 1700)
 */
#define	IPPROTO_IP		0		/* dummy for IP */
#define	IPPROTO_HOPOPTS		0		/* IP6 hop-by-hop options */
#define	IPPROTO_ICMP		1		/* control message protocol */
#define	IPPROTO_IGMP		2		/* group mgmt protocol */
#define	IPPROTO_GGP		3		/* gateway^2 (deprecated) */
#define IPPROTO_IPV4		4		/* IPv4 encapsulation */
#define IPPROTO_IPIP		IPPROTO_IPV4	/* for compatibility */
#define	IPPROTO_TCP		6		/* tcp */
#define	IPPROTO_ST		7		/* Stream protocol II */
#define	IPPROTO_EGP		8		/* exterior gateway protocol */
#define	IPPROTO_PIGP		9		/* private interior gateway */
#define	IPPROTO_RCCMON		10		/* BBN RCC Monitoring */
#define	IPPROTO_NVPII		11		/* network voice protocol*/
#define	IPPROTO_PUP		12		/* pup */
#define	IPPROTO_ARGUS		13		/* Argus */
#define	IPPROTO_EMCON		14		/* EMCON */
#define	IPPROTO_XNET		15		/* Cross Net Debugger */
#define	IPPROTO_CHAOS		16		/* Chaos*/
#define	IPPROTO_UDP		17		/* user datagram protocol */
#define	IPPROTO_MUX		18		/* Multiplexing */
#define	IPPROTO_MEAS		19		/* DCN Measurement Subsystems */
#define	IPPROTO_HMP		20		/* Host Monitoring */
#define	IPPROTO_PRM		21		/* Packet Radio Measurement */
#define	IPPROTO_IDP		22		/* xns idp */
#define	IPPROTO_TRUNK1		23		/* Trunk-1 */
#define	IPPROTO_TRUNK2		24		/* Trunk-2 */
#define	IPPROTO_LEAF1		25		/* Leaf-1 */
#define	IPPROTO_LEAF2		26		/* Leaf-2 */
#define	IPPROTO_RDP		27		/* Reliable Data */
#define	IPPROTO_IRTP		28		/* Reliable Transaction */
#define	IPPROTO_TP		29		/* tp-4 w/ class negotiation */
#define	IPPROTO_BLT		30		/* Bulk Data Transfer */
#define	IPPROTO_NSP		31		/* Network Services */
#define	IPPROTO_INP		32		/* Merit Internodal */
#define	IPPROTO_SEP		33		/* Sequential Exchange */
#define	IPPROTO_3PC		34		/* Third Party Connect */
#define	IPPROTO_IDPR		35		/* InterDomain Policy Routing */
#define	IPPROTO_XTP		36		/* XTP */
#define	IPPROTO_DDP		37		/* Datagram Delivery */
#define	IPPROTO_CMTP		38		/* Control Message Transport */
#define	IPPROTO_TPXX		39		/* TP++ Transport */
#define	IPPROTO_IL		40		/* IL transport protocol */
#define	IPPROTO_IPV6		41		/* IP6 header */
#define	IPPROTO_SDRP		42		/* Source Demand Routing */
#define	IPPROTO_ROUTING		43		/* IP6 routing header */
#define	IPPROTO_FRAGMENT	44		/* IP6 fragmentation header */
#define	IPPROTO_IDRP		45		/* InterDomain Routing*/
#define	IPPROTO_RSVP		46		/* resource reservation */
#define	IPPROTO_GRE		47		/* General Routing Encap. */
#define	IPPROTO_MHRP		48		/* Mobile Host Routing */
#define	IPPROTO_BHA		49		/* BHA */
#define	IPPROTO_ESP		50		/* IP6 Encap Sec. Payload */
#define	IPPROTO_AH		51		/* IP6 Auth Header */
#define	IPPROTO_INLSP		52		/* Integ. Net Layer Security */
#define	IPPROTO_SWIPE		53		/* IP with encryption */
#define	IPPROTO_NHRP		54		/* Next Hop Resolution */
#define	IPPROTO_MOBILE		55		/* IP Mobility */
#define	IPPROTO_TLSP		56		/* Transport Layer Security */
#define	IPPROTO_SKIP		57		/* SKIP */
#define	IPPROTO_ICMPV6		58		/* ICMP6 */
#define	IPPROTO_NONE		59		/* IP6 no next header */
#define	IPPROTO_DSTOPTS		60		/* IP6 destination option */
#define	IPPROTO_AHIP		61		/* any host internal protocol */
#define	IPPROTO_CFTP		62		/* CFTP */
#define	IPPROTO_HELLO		63		/* "hello" routing protocol */
#define	IPPROTO_SATEXPAK	64		/* SATNET/Backroom EXPAK */
#define	IPPROTO_KRYPTOLAN	65		/* Kryptolan */
#define	IPPROTO_RVD		66		/* Remote Virtual Disk */
#define	IPPROTO_IPPC		67		/* Pluribus Packet Core */
#define	IPPROTO_ADFS		68		/* Any distributed FS */
#define	IPPROTO_SATMON		69		/* Satnet Monitoring */
#define	IPPROTO_VISA		70		/* VISA Protocol */
#define	IPPROTO_IPCV		71		/* Packet Core Utility */
#define	IPPROTO_CPNX		72		/* Comp. Prot. Net. Executive */
#define	IPPROTO_CPHB		73		/* Comp. Prot. HeartBeat */
#define	IPPROTO_WSN		74		/* Wang Span Network */
#define	IPPROTO_PVP		75		/* Packet Video Protocol */
#define	IPPROTO_BRSATMON	76		/* BackRoom SATNET Monitoring */
#define	IPPROTO_ND		77		/* Sun net disk proto (temp.) */
#define	IPPROTO_WBMON		78		/* WIDEBAND Monitoring */
#define	IPPROTO_WBEXPAK		79		/* WIDEBAND EXPAK */
#define	IPPROTO_EON		80		/* ISO cnlp */
#define	IPPROTO_VMTP		81		/* VMTP */
#define	IPPROTO_SVMTP		82		/* Secure VMTP */
#define	IPPROTO_VINES		83		/* Banyon VINES */
#define	IPPROTO_TTP		84		/* TTP */
#define	IPPROTO_IGP		85		/* NSFNET-IGP */
#define	IPPROTO_DGP		86		/* dissimilar gateway prot. */
#define	IPPROTO_TCF		87		/* TCF */
#define	IPPROTO_IGRP		88		/* Cisco/GXS IGRP */
#define	IPPROTO_OSPFIGP		89		/* OSPFIGP */
#define	IPPROTO_SRPC		90		/* Strite RPC protocol */
#define	IPPROTO_LARP		91		/* Locus Address Resoloution */
#define	IPPROTO_MTP		92		/* Multicast Transport */
#define	IPPROTO_AX25		93		/* AX.25 Frames */
#define	IPPROTO_IPEIP		94		/* IP encapsulated in IP */
#define	IPPROTO_MICP		95		/* Mobile Int.ing control */
#define	IPPROTO_SCCSP		96		/* Semaphore Comm. security */
#define	IPPROTO_ETHERIP		97		/* Ethernet IP encapsulation */
#define	IPPROTO_ENCAP		98		/* encapsulation header */
#define	IPPROTO_APES		99		/* any private encr. scheme */
#define	IPPROTO_GMTP		100		/* GMTP*/
#define	IPPROTO_IPCOMP		108		/* payload compression (IPComp) */
/* 101-254: Partly Unassigned */
#define	IPPROTO_PIM		103		/* Protocol Independent Mcast */
#define	IPPROTO_CARP		112		/* CARP */
#define	IPPROTO_PGM		113		/* PGM */
#define	IPPROTO_PFSYNC		240		/* PFSYNC */
/* 255: Reserved */
/* BSD Private, local use, namespace incursion */
#define	IPPROTO_DIVERT		254		/* divert pseudo-protocol */
#define	IPPROTO_RAW		255		/* raw IP packet */
#define	IPPROTO_MAX		256

/* last return value of *_input(), meaning "all job for this pkt is done".  */
#define	IPPROTO_DONE		257

/* Used by RSS: the layer3 protocol is unknown */
#define IPPROTO_UNKNOWN		258

/*
 * Local port number conventions:
 *
 * When a user does a bind(2) or connect(2) with a port number of zero,
 * a non-conflicting local port address is chosen.
 * The default range is IPPORT_RESERVED through
 * IPPORT_USERRESERVED, although that is settable by sysctl.
 *
 * A user may set the IPPROTO_IP option IP_PORTRANGE to change this
 * default assignment range.
 *
 * The value IP_PORTRANGE_DEFAULT causes the default behavior.
 *
 * The value IP_PORTRANGE_HIGH changes the range of candidate port numbers
 * into the "high" range.  These are reserved for client outbound connections
 * which do not want to be filtered by any firewalls.
 *
 * The value IP_PORTRANGE_LOW changes the range to the "low" are
 * that is (by convention) restricted to privileged processes.  This
 * convention is based on "vouchsafe" principles only.  It is only secure
 * if you trust the remote host to restrict these ports.
 *
 * The default range of ports and the high range can be changed by
 * sysctl(3).  (net.inet.ip.port{hi,low}{first,last}_auto)
 *
 * Changing those values has bad security implications if you are
 * using a a stateless firewall that is allowing packets outside of that
 * range in order to allow transparent outgoing connections.
 *
 * Such a firewall configuration will generally depend on the use of these
 * default values.  If you change them, you may find your Security
 * Administrator looking for you with a heavy object.
 *
 * For a slightly more orthodox text view on this:
 *   ftp://ftp.isi.edu/in-notes/iana/assignments/port-numbers
 *
 *   port numbers are divided into three ranges:
 *		0 -  1023 Well Known Ports
 *	     1024 - 49151 Registered Ports
 *	    49152 - 65535 Dynamic and/or Private Ports
 */

/*
 * Ports < IPPORT_RESERVED are reserved for
 * privileged processes (e.g. root).  (IP_PORTRANGE_LOW)
 * Ports > IPPORT_USERRESERVED are reserved
 * for servers, not necessarily privileged.  (IP_PORTRANGE_DEFAULT)
 */
#define	IPPORT_RESERVED		1024
#define	IPPORT_USERRESERVED	5000

/*
 * Default local port range to use by setting IP_PORTRANGE_HIGH
 */
#define	IPPORT_HIFIRSTAUTO	49152
#define	IPPORT_HILASTAUTO	65535

/*
 * Scanning for a free reserved port return a value below IPPORT_RESERVED,
 * but higher than IPPORT_RESERVEDSTART.  Traditionally the start value was
 * 512, but that conflicts with some well-known-services that firewalls may
 * have a fit if we use.
 */
#define IPPORT_RESERVEDSTART	600

#define	IPPORT_MAX		65535

#ifndef _SA_FAMILY_T_DECLARED
typedef	__uint8_t	sa_family_t;
#define	_SA_FAMILY_T_DECLARED
#endif

#ifndef _STRUCT_IN_ADDR_DECLARED
#define _STRUCT_IN_ADDR_DECLARED
/*
 * Internet address (a structure for historical reasons)
 */
struct in_addr {
	in_addr_t s_addr;
};
#endif

/*
 * Definitions of bits in internet address integers.
 * On subnets, the decomposition of addresses to host and net parts
 * is done according to subnet mask, not the masks here.
 */
#define	IN_CLASSA(i)		(((u_int32_t)(i) & 0x80000000) == 0)
#define	IN_CLASSA_NET		0xff000000
#define	IN_CLASSA_NSHIFT	24
#define	IN_CLASSA_HOST		0x00ffffff
#define	IN_CLASSA_MAX		128

#define	IN_CLASSB(i)		(((u_int32_t)(i) & 0xc0000000) == 0x80000000)
#define	IN_CLASSB_NET		0xffff0000
#define	IN_CLASSB_NSHIFT	16
#define	IN_CLASSB_HOST		0x0000ffff
#define	IN_CLASSB_MAX		65536

#define	IN_CLASSC(i)		(((u_int32_t)(i) & 0xe0000000) == 0xc0000000)
#define	IN_CLASSC_NET		0xffffff00
#define	IN_CLASSC_NSHIFT	8
#define	IN_CLASSC_HOST		0x000000ff

#define	IN_CLASSD(i)		(((u_int32_t)(i) & 0xf0000000) == 0xe0000000)
#define	IN_CLASSD_NET		0xf0000000	/* These ones aren't really */
#define	IN_CLASSD_NSHIFT	28		/* net and host fields, but */
#define	IN_CLASSD_HOST		0x0fffffff	/* routing needn't know.    */
#define	IN_MULTICAST(i)		IN_CLASSD(i)

#define	IN_EXPERIMENTAL(i)	(((u_int32_t)(i) & 0xf0000000) == 0xf0000000)
#define	IN_BADCLASS(i)		(((u_int32_t)(i) & 0xf0000000) == 0xf0000000)

#define	INADDR_ANY		(u_int32_t)0x00000000
#define	INADDR_LOOPBACK		(u_int32_t)0x7f000001
#define	INADDR_BROADCAST	(u_int32_t)0xffffffff	/* must be masked */
#ifndef _KERNEL
#define	INADDR_NONE		0xffffffff		/* -1 return */
#endif

#define	INADDR_UNSPEC_GROUP	(u_int32_t)0xe0000000	/* 224.0.0.0 */
#define	INADDR_ALLHOSTS_GROUP	(u_int32_t)0xe0000001	/* 224.0.0.1 */
#define	INADDR_ALLRTRS_GROUP	(u_int32_t)0xe0000002	/* 224.0.0.2 */
#define	INADDR_CARP_GROUP       (u_int32_t)0xe0000012   /* 224.0.0.18 */
#define	INADDR_PFSYNC_GROUP	(u_int32_t)0xe00000f0	/* 224.0.0.240 */
#define	INADDR_MAX_LOCAL_GROUP	(u_int32_t)0xe00000ff	/* 224.0.0.255 */

#define	IN_LOOPBACKNET		127			/* official! */

/*
 * Socket address, internet style.
 */
struct sockaddr_in {
	u_char		sin_len;
	sa_family_t	sin_family;
	u_short		sin_port;
	struct in_addr	sin_addr;
	char		sin_zero[8];
};

#define	INET_ADDRSTRLEN		16

/*
 * Options for use with [gs]etsockopt at the IP level.
 * First word of comment is data type; bool is stored in int.
 */
#define	IP_OPTIONS		1    /* buf/ip_opts; set/get IP options */
#define	IP_HDRINCL		2    /* int; header is included with data */
#define	IP_TOS			3    /* int; IP type of service and preced. */
#define	IP_TTL			4    /* int; IP time to live */
#define	IP_RECVOPTS		5    /* bool; receive all IP opts w/dgram */
#define	IP_RECVRETOPTS		6    /* bool; receive IP opts for response */
#define	IP_RECVDSTADDR		7    /* bool; receive IP dst addr w/dgram */
#define	IP_RETOPTS		8    /* ip_opts; set/get IP options */
#define	IP_MULTICAST_IF		9    /* u_char; set/get IP multicast i/f  */
#define	IP_MULTICAST_TTL	10   /* u_char; set/get IP multicast ttl */
#define	IP_MULTICAST_LOOP	11   /* u_char; set/get IP multicast loopback */
#define	IP_ADD_MEMBERSHIP	12   /* ip_mreq; add an IP group membership */
#define	IP_DROP_MEMBERSHIP	13   /* ip_mreq; drop an IP group membership */
#define IP_MULTICAST_VIF	14   /* set/get IP mcast virt. iface */
#define IP_RSVP_ON		15   /* enable RSVP in kernel */
#define IP_RSVP_OFF		16   /* disable RSVP in kernel */
#define IP_RSVP_VIF_ON		17   /* set RSVP per-vif socket */
#define IP_RSVP_VIF_OFF		18   /* unset RSVP per-vif socket */
#define IP_PORTRANGE		19   /* int; range to choose for unspec port */
#define	IP_RECVIF		20   /* bool; receive reception if w/dgram */
/* for IPSEC */
#define	IP_IPSEC_POLICY		21   /* int; set/get security policy */
#define	IP_FAITH		22   /* bool; accept FAITH'ed connections */

#define IP_FW_X			49   /* ipfw2 firewall */
#define	IP_FW_ADD		50   /* add a firewall rule to chain */
#define	IP_FW_DEL		51   /* delete a firewall rule from chain */
#define	IP_FW_FLUSH		52   /* flush firewall rule chain */
#define	IP_FW_ZERO		53   /* clear single/all firewall counter(s) */
#define	IP_FW_GET		54   /* get entire firewall rule chain */
#define	IP_FW_RESETLOG		55   /* reset logging counters */

#define	IP_DUMMYNET_CONFIGURE	60   /* add/configure a dummynet pipe */
#define	IP_DUMMYNET_DEL		61   /* delete a dummynet pipe from chain */
#define	IP_DUMMYNET_FLUSH	62   /* flush dummynet */
#define	IP_DUMMYNET_GET		64   /* get entire dummynet pipes */

#define	IP_RECVTTL		65   /* bool; receive IP TTL w/dgram */
#define	IP_MINTTL		66   /* minimum TTL for packet or drop */

/*
 * Defaults and limits for options
 */
#define	IP_DEFAULT_MULTICAST_TTL  1	/* normally limit m'casts to 1 hop  */
#define	IP_DEFAULT_MULTICAST_LOOP 1	/* normally hear sends if a member  */
#define	IP_MAX_MEMBERSHIPS	20	/* per socket */

/*
 * Argument structure for IP_ADD_MEMBERSHIP and IP_DROP_MEMBERSHIP.
 */
struct ip_mreq {
	struct	in_addr imr_multiaddr;	/* IP multicast address of group */
	struct	in_addr imr_interface;	/* local IP address of interface */
};

/*
 * Argument for IP_PORTRANGE:
 * - which range to search when port is unspecified at bind() or connect()
 */
#define	IP_PORTRANGE_DEFAULT	0	/* default range */
#define	IP_PORTRANGE_HIGH	1	/* "high" - request firewall bypass */
#define	IP_PORTRANGE_LOW	2	/* "low" - vouchsafe security */

/*
 * Definitions for inet sysctl operations.
 *
 * Third level is protocol number.
 * Fourth level is desired variable within that protocol.
 */
#define	IPPROTO_MAXID	(IPPROTO_AH + 1)	/* don't list to IPPROTO_MAX */

#define	CTL_IPPROTO_NAMES { \
	{ "ip", CTLTYPE_NODE }, \
	{ "icmp", CTLTYPE_NODE }, \
	{ "igmp", CTLTYPE_NODE }, \
	{ "ggp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "tcp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ "egp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pup", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "udp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "idp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "ipsec", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pim", CTLTYPE_NODE }, \
}

/*
 * Names for IP sysctl objects
 */
#define	IPCTL_FORWARDING	1	/* act as router */
#define	IPCTL_SENDREDIRECTS	2	/* may send redirects when forwarding */
#define	IPCTL_DEFTTL		3	/* default TTL */
#ifdef notyet
#define	IPCTL_DEFMTU		4	/* default MTU */
#endif
#define IPCTL_RTEXPIRE		5	/* cloned route expiration time */
#define IPCTL_RTMINEXPIRE	6	/* min value for expiration time */
#define IPCTL_RTMAXCACHE	7	/* trigger level for dynamic expire */
#define	IPCTL_SOURCEROUTE	8	/* may perform source routes */
#define	IPCTL_DIRECTEDBROADCAST	9	/* may re-broadcast received packets */
#define IPCTL_INTRQMAXLEN	10	/* max length of netisr queue */
#define	IPCTL_INTRQDROPS	11	/* number of netisr q drops */
#define	IPCTL_STATS		12	/* ipstat structure */
#define	IPCTL_ACCEPTSOURCEROUTE	13	/* may accept source routed packets */
#define	IPCTL_FASTFORWARDING	14	/* use fast IP forwarding code */
#define	IPCTL_KEEPFAITH		15	/* FAITH IPv4->IPv6 translater ctl */
#define	IPCTL_GIF_TTL		16	/* default TTL for gif encap packet */
#define	IPCTL_MAXID		17

#define	IPCTL_NAMES { \
	{ 0, 0 }, \
	{ "forwarding", CTLTYPE_INT }, \
	{ "redirect", CTLTYPE_INT }, \
	{ "ttl", CTLTYPE_INT }, \
	{ "mtu", CTLTYPE_INT }, \
	{ "rtexpire", CTLTYPE_INT }, \
	{ "rtminexpire", CTLTYPE_INT }, \
	{ "rtmaxcache", CTLTYPE_INT }, \
	{ "sourceroute", CTLTYPE_INT }, \
	{ "directed-broadcast", CTLTYPE_INT }, \
	{ "intr-queue-maxlen", CTLTYPE_INT }, \
	{ "intr-queue-drops", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
	{ "accept_sourceroute", CTLTYPE_INT }, \
	{ "fastforwarding", CTLTYPE_INT }, \
}

#ifdef _KERNEL
struct ifnet; struct mbuf;	/* forward declarations for Standard C */
#endif

/* INET6 stuff */
#define	__KAME_NETINET_IN_H_INCLUDED_
#include <netinet6/in6.h>
#undef __KAME_NETINET_IN_H_INCLUDED_

#ifdef _KERNEL

struct thread;

int	 in_broadcast (struct in_addr, struct ifnet *);
int	 in_canforward (struct in_addr);
int	 in_localaddr (struct in_addr);
char	*inet_ntoa (struct in_addr); /* in libkern */
char	*inet_ntop(int, const void * __restrict, char * __restrict,
	    socklen_t); /* in libkern */

int	prison_replace_wildcards (struct thread *td, struct sockaddr *ip);
int	prison_remote_ip (struct thread *td, struct sockaddr *ip);

#define in_hosteq(s, t)	((s).s_addr == (t).s_addr)
#define in_nullhost(x)	((x).s_addr == INADDR_ANY)

#define satosin(sa)	((struct sockaddr_in *)(sa))
#define sintosa(sin)	((struct sockaddr *)(sin))
#define ifatoia(ifa)	((struct in_ifaddr *)(ifa))

#endif

#endif
