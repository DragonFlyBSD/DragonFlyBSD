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
 *	@(#)if.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/if.h,v 1.58.2.9 2002/08/30 14:23:38 sobomax Exp $
 */

#ifndef _NET_IF_H_
#define	_NET_IF_H_

#include <sys/cdefs.h>

#if __BSD_VISIBLE
/*
 * <net/if.h> does not depend on <sys/time.h> on most other systems.  This
 * helps userland compatibility.  (struct timeval ifi_lastchange)
 */
#include <sys/socket.h>
#ifndef _KERNEL
#include <sys/time.h>
#endif /* !_KERNEL */
#include <sys/types.h>
#endif /* __BSD_VISIBLE */

/*
 * Length of interface external name, including terminating '\0'.
 * Note: this is the same size as a generic device's external name.
 */
#define		IF_NAMESIZE	16
#if __BSD_VISIBLE
#define		IFNAMSIZ	IF_NAMESIZE
#define		IF_MAXUNIT	0x7fff		/* if_unit is 15bits */

/*
 * Structure describing information about an interface
 * which may be of interest to management entities.
 */
struct if_data {
	/* generic interface information */
	u_char	ifi_type;		/* ethernet, tokenring, etc */
	u_char	ifi_physical;		/* e.g., AUI, Thinnet, 10base-T, etc */
	u_char	ifi_addrlen;		/* media address length */
	u_char	ifi_hdrlen;		/* media header length */
	u_char	ifi_recvquota;		/* polling quota for receive intrs */
	u_char	ifi_xmitquota;		/* polling quota for xmit intrs */
	u_long	ifi_mtu;		/* maximum transmission unit */
	u_long	ifi_metric;		/* routing metric (external only) */
	u_long	ifi_link_state;		/* current link state */
	uint64_t ifi_baudrate;		/* linespeed */
	/* volatile statistics */
	u_long	ifi_ipackets;		/* packets received on interface */
	u_long	ifi_ierrors;		/* input errors on interface */
	u_long	ifi_opackets;		/* packets sent on interface */
	u_long	ifi_oerrors;		/* output errors on interface */
	u_long	ifi_collisions;		/* collisions on csma interfaces */
	u_long	ifi_ibytes;		/* total number of octets received */
	u_long	ifi_obytes;		/* total number of octets sent */
	u_long	ifi_imcasts;		/* packets received via multicast */
	u_long	ifi_omcasts;		/* packets sent via multicast */
	u_long	ifi_iqdrops;		/* dropped on input, this interface */
	u_long	ifi_noproto;		/* destined for unsupported protocol */
	u_long	ifi_hwassist;		/* HW offload capabilities */
	u_long	ifi_oqdrops;		/* dropped on input, this interface */
	struct	timeval ifi_lastchange;	/* time of last administrative change */
};

#define	IFF_UP		0x1		/* interface is up */
#define	IFF_BROADCAST	0x2		/* broadcast address valid */
#define	IFF_DEBUG	0x4		/* turn on debugging */
#define	IFF_LOOPBACK	0x8		/* is a loopback net */
#define	IFF_POINTOPOINT	0x10		/* interface is point-to-point link */
#define	IFF_SMART	0x20		/* interface manages own routes */
#define	IFF_RUNNING	0x40		/* resources allocated */
#define	IFF_NOARP	0x80		/* no address resolution protocol */
#define	IFF_PROMISC	0x100		/* receive all packets */
#define	IFF_ALLMULTI	0x200		/* receive all multicast packets */
#define	IFF_OACTIVE_COMPAT 0x400	/* was transmission in progress */
#define	IFF_SIMPLEX	0x800		/* can't hear own transmissions */
#define	IFF_LINK0	0x1000		/* per link layer defined bit */
#define	IFF_LINK1	0x2000		/* per link layer defined bit */
#define	IFF_LINK2	0x4000		/* per link layer defined bit */
#define	IFF_ALTPHYS	IFF_LINK2	/* use alternate physical connection */
#define	IFF_MULTICAST	0x8000		/* supports multicast */
#define	IFF_POLLING_COMPAT 0x10000	/* was interface is in polling mode */
#define	IFF_PPROMISC	0x20000		/* user-requested promisc mode */
#define	IFF_MONITOR	0x40000		/* user-requested monitor mode */
#define	IFF_STATICARP	0x80000		/* static ARP */
#define	IFF_NPOLLING	0x100000	/* interface is in polling mode */
#define	IFF_IDIRECT	0x200000	/* direct input */
#define	IFF_ISBRIDGE	0x400000	/* this is actually a bridge(4) */
#define	IFF_ANNOUNCE	0x800000	/* proxy interface's routes */

/* flags set internally only: */
#define	IFF_CANTCHANGE \
	(IFF_BROADCAST|IFF_POINTOPOINT|IFF_RUNNING|IFF_OACTIVE_COMPAT|\
	 IFF_SIMPLEX|IFF_MULTICAST|IFF_ALLMULTI|IFF_SMART|IFF_POLLING_COMPAT|\
	 IFF_NPOLLING|IFF_IDIRECT|IFF_ISBRIDGE)

#ifndef _KERNEL
/*
 * User space compat
 */
#define	IFF_OACTIVE	IFF_OACTIVE_COMPAT
#define	IFF_POLLING	IFF_POLLING_COMPAT
#endif

/*
 * Values for if_link_state.
 */
#define	LINK_STATE_UNKNOWN	0	/* link invalid/unknown */
#define	LINK_STATE_DOWN		1	/* link is down */
#define	LINK_STATE_UP		2	/* link is up */
#define	LINK_STATE_IS_UP(_s)	((_s) >= LINK_STATE_UP)

/*
 * Some convenience macros used for setting ifi_baudrate.
 * XXX 1000 vs. 1024? --thorpej@netbsd.org
 */
#define	IF_Kbps(x)	((x) * 1000)		/* kilobits/sec. */
#define	IF_Mbps(x)	(IF_Kbps((x) * 1000))	/* megabits/sec. */
#define	IF_Gbps(x)	(IF_Mbps((x) * 1000))	/* gigabits/sec. */

/* Capabilities that interfaces can advertise. */
#define	IFCAP_RXCSUM		0x00001 /* can offload checksum on RX */
#define	IFCAP_TXCSUM		0x00002 /* can offload checksum on TX */
#define	IFCAP_NETCONS		0x00004 /* can be a network console */
#define	IFCAP_VLAN_MTU		0x00008	/* VLAN-compatible MTU */
#define	IFCAP_VLAN_HWTAGGING	0x00010	/* hardware VLAN tag support */
#define	IFCAP_JUMBO_MTU		0x00020	/* 9000 byte MTU support */
#define	IFCAP_RSS		0x00040	/* Receive Side Scaling for IPv4 */
#define	IFCAP_VLAN_HWCSUM	0x00080	/* can do IFCAP_HWCSUM on VLANs */
#define	IFCAP_TSO4		0x00100	/* can offload TCP segmentation */
#define	IFCAP_TSO		IFCAP_TSO4
#define	IFCAP_TSO6		0x00200	/* can offload TCP6 segmentation */

#define	IFCAP_HWCSUM	(IFCAP_RXCSUM | IFCAP_TXCSUM)


#define	IFQ_MAXLEN	250
#define	IFNET_SLOWHZ	1		/* granularity is 1 second */

/*
 * Message format for use in obtaining information about interfaces
 * from getkerninfo and the routing socket
 */
struct if_msghdr {
	u_short	ifm_msglen;	/* to skip over non-understood messages */
	u_char	ifm_version;	/* future binary compatibility */
	u_char	ifm_type;	/* message type */
	u_short	ifm_index;	/* index for associated ifp */
	int	ifm_flags;	/* value of if_flags */
	int	ifm_addrs;	/* like rtm_addrs */
	struct	if_data ifm_data;/* statistics and other data about if */
};

/*
 * Message format for use in obtaining information about interface addresses
 * from getkerninfo and the routing socket
 */
struct ifa_msghdr {
	u_short	ifam_msglen;	/* to skip over non-understood messages */
	u_char	ifam_version;	/* future binary compatibility */
	u_char	ifam_type;	/* message type */
	u_short	ifam_index;	/* index for associated ifp */
	int	ifam_flags;	/* value of ifa_flags */
	int	ifam_addrs;	/* like rtm_addrs */
	int	ifam_addrflags;	/* family specific address flags */
	int	ifam_metric;	/* value of ifa_metric */
};

/*
 * Message format for use in obtaining information about multicast addresses
 * from the routing socket
 */
struct ifma_msghdr {
	u_short	ifmam_msglen;	/* to skip over non-understood messages */
	u_char	ifmam_version;	/* future binary compatibility */
	u_char	ifmam_type;	/* message type */
	u_short	ifmam_index;	/* index for associated ifp */
	int	ifmam_flags;	/* value of ifa_flags */
	int	ifmam_addrs;	/* like rtm_addrs */
};

/*
 * Message format announcing the arrival or departure of a network interface.
 */
struct if_announcemsghdr {
	u_short	ifan_msglen;	/* to skip over non-understood messages */
	u_char	ifan_version;	/* future binary compatibility */
	u_char	ifan_type;	/* message type */
	u_short	ifan_index;	/* index for associated ifp */
	char	ifan_name[IFNAMSIZ]; /* if name, e.g. "en0" */
	u_short	ifan_what;	/* what type of announcement */
};

#define	IFAN_ARRIVAL	0	/* interface arrival */
#define	IFAN_DEPARTURE	1	/* interface departure */

/*
 * Buffer with length to be used in SIOCGIFDESCR/SIOCSIFDESCR requests
 */
struct ifreq_buffer {
	size_t	 length;
	void	*buffer;
};

/*
 * Interface request structure used for socket
 * ioctl's.  All interface ioctl's must have parameter
 * definitions which begin with ifr_name.  The
 * remainder may be interface specific.
 */
struct	ifreq {
	char	ifr_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		struct	ifreq_buffer ifru_buffer;
		short	ifru_flags[2];
		short	ifru_index;
		int	ifru_metric;
		int	ifru_mtu;
		int	ifru_phys;
		int	ifru_media;
		void	*ifru_data;
		int	ifru_cap[2];
		int	ifru_pollcpu;
		int	ifru_tsolen;
	} ifr_ifru;
#define	ifr_addr	ifr_ifru.ifru_addr	/* address */
#define	ifr_dstaddr	ifr_ifru.ifru_dstaddr	/* other end of p-to-p link */
#define	ifr_broadaddr	ifr_ifru.ifru_broadaddr	/* broadcast address */
#define	ifr_buffer	ifr_ifru.ifru_buffer	/* user supplied buffer with its length */
#define	ifr_flags	ifr_ifru.ifru_flags[0]	/* flags (low 16 bits) */
#define	ifr_flagshigh	ifr_ifru.ifru_flags[1]	/* flags (high 16 bits) */
#define	ifr_metric	ifr_ifru.ifru_metric	/* metric */
#define	ifr_mtu		ifr_ifru.ifru_mtu	/* mtu */
#define	ifr_phys	ifr_ifru.ifru_phys	/* physical wire */
#define	ifr_media	ifr_ifru.ifru_media	/* physical media */
#define	ifr_data	ifr_ifru.ifru_data	/* for use by interface */
#define	ifr_reqcap	ifr_ifru.ifru_cap[0]	/* requested capabilities */
#define	ifr_curcap	ifr_ifru.ifru_cap[1]	/* current capabilities */
#define	ifr_index	ifr_ifru.ifru_index	/* interface index */
#define	ifr_pollcpu	ifr_ifru.ifru_pollcpu	/* deprecated */
#define	ifr_tsolen	ifr_ifru.ifru_tsolen	/* max TSO length */
};

#define	_SIZEOF_ADDR_IFREQ(ifr) \
	((ifr).ifr_addr.sa_len > sizeof(struct sockaddr) ? \
	 (sizeof(struct ifreq) - sizeof(struct sockaddr) + \
	  (ifr).ifr_addr.sa_len) : sizeof(struct ifreq))

struct ifaliasreq {
	char	ifra_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	struct	sockaddr ifra_addr;
	struct	sockaddr ifra_broadaddr;
	struct	sockaddr ifra_mask;
};

struct ifmediareq {
	char	ifm_name[IFNAMSIZ];	/* if name, e.g. "en0" */
	int	ifm_current;		/* current media options */
	int	ifm_mask;		/* don't care mask */
	int	ifm_status;		/* media status */
	int	ifm_active;		/* active options */
	int	ifm_count;		/* # entries in ifm_ulist array */
	int	*ifm_ulist;		/* media words */
};

struct  ifdrv {
	char		 ifd_name[IFNAMSIZ];     /* if name, e.g. "en0" */
	unsigned long	 ifd_cmd;
	size_t		 ifd_len;
	void		*ifd_data;
};

/*
 * Structure used to retrieve aux status data from interfaces.
 * Kernel suppliers to this interface should respect the formatting
 * needed by ifconfig(8): each line starts with a TAB and ends with
 * a newline.  The canonical example to copy and paste is in if_tun.c.
 */

#define	IFSTATMAX	800		/* 10 lines of text */
struct ifstat {
	char	ifs_name[IFNAMSIZ];	/* if name, e.g. "en0" */
	char	ascii[IFSTATMAX + 1];
};

/*
 * Structure used in SIOCGIFCONF request.
 * Used to retrieve interface configuration
 * for machine (useful for programs which
 * must know all networks accessible).
 */
struct	ifconf {
	int	ifc_len;		/* size of associated buffer */
	union {
		caddr_t	ifcu_buf;
		struct	ifreq *ifcu_req;
	} ifc_ifcu;
#define	ifc_buf	ifc_ifcu.ifcu_buf	/* buffer address */
#define	ifc_req	ifc_ifcu.ifcu_req	/* array of structures returned */
};

/*
 * interface groups
 */

#define	IFG_ALL		"all"		/* group contains all interfaces */
/* XXX: will we implement this? */
#define	IFG_EGRESS	"egress"	/* if(s) default route(s) point to */

struct ifg_req {
	union {
		char	ifgrqu_group[IFNAMSIZ];
		char	ifgrqu_member[IFNAMSIZ];
	} ifgrq_ifgrqu;
#define	ifgrq_group	ifgrq_ifgrqu.ifgrqu_group
#define	ifgrq_member	ifgrq_ifgrqu.ifgrqu_member
};

/*
 * Used to lookup groups for an interface
 */
struct ifgroupreq {
	char	ifgr_name[IFNAMSIZ];
	u_int	ifgr_len;
	union {
		char	ifgru_group[IFNAMSIZ];
		struct	ifg_req *ifgru_groups;
	} ifgr_ifgru;
#define	ifgr_group	ifgr_ifgru.ifgru_group
#define	ifgr_groups	ifgr_ifgru.ifgru_groups
};

/*
 * Structure for SIOC[AGD]LIFADDR
 */
struct if_laddrreq {
	char	iflr_name[IFNAMSIZ];
	u_int	flags;
#define	IFLR_PREFIX	0x8000  /* in: prefix given  out: kernel fills id */
	u_int	prefixlen;         /* in/out */
	struct	sockaddr_storage addr;   /* in/out */
	struct	sockaddr_storage dstaddr; /* out */
};
#endif /* __BSD_VISIBLE */

#ifndef _KERNEL
struct if_nameindex {
	unsigned int	 if_index;	/* 1, 2, ... */
	char		*if_name;	/* null terminated name: "lnc0", ... */
};

__BEGIN_DECLS
unsigned int	 if_nametoindex(const char *);
char		*if_indextoname(unsigned int, char *);
struct if_nameindex *if_nameindex(void);
void		 if_freenameindex(struct if_nameindex *);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_NET_IF_H_ */
