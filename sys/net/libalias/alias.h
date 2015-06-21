/* lint -save -library Flexelint comment for external headers */

/*-
 * Copyright (c) 2001 Charles Mott <cm@linktel.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netinet/libalias/alias.h,v 1.34.6.1 2008/11/25 02:59:29 kensmith Exp $
 */

/*
 * Alias.h defines the outside world interfaces for the packet aliasing
 * software.
 *
 * This software is placed into the public domain with no restrictions on its
 * distribution.
 */

#ifndef _ALIAS_H_
#define	_ALIAS_H_

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#define LIBALIAS_BUF_SIZE 128
#ifdef	_KERNEL
/*
 * The kernel version of libalias does not support these features.
 */
#define	NO_FW_PUNCH
#define	NO_USE_SOCKETS

MALLOC_DECLARE(M_ALIAS);

#endif

/*
 * The external interface to libalias, the packet aliasing engine.
 *
 * There are two sets of functions:
 *
 * PacketAlias*() the old API which doesn't take an instance pointer
 * and therefore can only have one packet engine at a time.
 *
 * LibAlias*() the new API which takes as first argument a pointer to
 * the instance of the packet aliasing engine.
 *
 * The functions otherwise correspond to each other one for one, except
 * for the LibAliasUnaliasOut()/PacketUnaliasOut() function which were
 * were misnamed in the old API.
 */

/*
 * The instance structure
 */
struct libalias;

/* Data Structures

	The fundamental data structure used in this program is
	"struct alias_link".  Whenever a TCP connection is made,
	a UDP datagram is sent out, or an ICMP echo request is made,
	a link record is made (if it has not already been created).
	The link record is identified by the source address/port
	and the destination address/port. In the case of an ICMP
	echo request, the source port is treated as being equivalent
	with the 16-bit ID number of the ICMP packet.

	The link record also can store some auxiliary data.  For
	TCP connections that have had sequence and acknowledgment
	modifications, data space is available to track these changes.
	A state field is used to keep track in changes to the TCP
	connection state.  ID numbers of fragments can also be
	stored in the auxiliary space.  Pointers to unresolved
	fragments can also be stored.

	The link records support two independent chainings.  Lookup
	tables for input and out tables hold the initial pointers
	the link chains.  On input, the lookup table indexes on alias
	port and link type.  On output, the lookup table indexes on
	source address, destination address, source port, destination
	port and link type.
*/

struct ack_data_record {	/* used to save changes to ACK/sequence
				 * numbers */
	u_long		ack_old;
	u_long		ack_new;
	int		delta;
	int		active;
};

struct tcp_state {		/* Information about TCP connection		*/
	int		in;	/* State for outside -> inside			 */
	int		out;	/* State for inside  -> outside			*/
	int		index;	/* Index to ACK data array				 */
	int		ack_modified;	/* Indicates whether ACK and
					 * sequence numbers */
	/* been modified						   */
};

#define N_LINK_TCP_DATA   3	/* Number of distinct ACK number changes
				 * saved for a modified TCP stream */
struct tcp_dat {
	struct tcp_state state;
	struct ack_data_record ack[N_LINK_TCP_DATA];
	int		fwhole;	/* Which firewall record is used for this
				 * hole? */
};

struct server {			/* LSNAT server pool (circular list) */
	struct in_addr	addr;
	u_short		port;
	struct server  *next;
};

struct alias_link {		/* Main data structure */
	struct libalias *la;
	struct in_addr	src_addr;	/* Address and port information		*/
	struct in_addr	dst_addr;
	struct in_addr	alias_addr;
	struct in_addr	proxy_addr;
	u_short		src_port;
	u_short		dst_port;
	u_short		alias_port;
	u_short		proxy_port;
	struct server  *server;

	int		link_type;	/* Type of link: TCP, UDP, ICMP,
					 * proto, frag */

/* values for link_type */
#define LINK_ICMP					 IPPROTO_ICMP
#define LINK_UDP					  IPPROTO_UDP
#define LINK_TCP					  IPPROTO_TCP
#define LINK_FRAGMENT_ID			  (IPPROTO_MAX + 1)
#define LINK_FRAGMENT_PTR			 (IPPROTO_MAX + 2)
#define LINK_ADDR					 (IPPROTO_MAX + 3)
#define LINK_PPTP					 (IPPROTO_MAX + 4)

	int		flags;	/* indicates special characteristics   */
	int		pflags;	/* protocol-specific flags */

/* flag bits */
#define LINK_UNKNOWN_DEST_PORT	 0x01
#define LINK_UNKNOWN_DEST_ADDR	 0x02
#define LINK_PERMANENT			 0x04
#define LINK_PARTIALLY_SPECIFIED   0x03	/* logical-or of first two bits */
#define LINK_UNFIREWALLED		  0x08

	int		timestamp;	/* Time link was last accessed		 */
	int		expire_time;	/* Expire time for link				*/
#ifndef	NO_USE_SOCKETS
	int		sockfd;	/* socket descriptor				   */
#endif
			LIST_ENTRY	(alias_link) list_out;	/* Linked list of
								 * pointers for	 */
			LIST_ENTRY	(alias_link) list_in;	/* input and output
								 * lookup tables  */

	union {			/* Auxiliary data					  */
		char		   *frag_ptr;
		struct in_addr	frag_addr;
		struct tcp_dat *tcp;
	}		data;
};

/* OLD API */

/* Initialization and control functions. */
void		PacketAliasInit(void);
void		PacketAliasSetAddress(struct in_addr _addr);
void		PacketAliasSetFWBase(unsigned int _base, unsigned int _num);
void		PacketAliasSetSkinnyPort(unsigned int _port);
unsigned int
		PacketAliasSetMode(unsigned int _flags, unsigned int _mask);
void		PacketAliasUninit(void);

/* Packet Handling functions. */
int		PacketAliasIn(char *_ptr, int _maxpacketsize);
int		PacketAliasOut(char *_ptr, int _maxpacketsize);
int		PacketUnaliasOut(char *_ptr, int _maxpacketsize);

/* Port and address redirection functions. */


int
PacketAliasAddServer(struct alias_link *_lnk,
	struct in_addr _addr, unsigned short _port);
struct alias_link *
PacketAliasRedirectAddr(struct in_addr _src_addr,
	struct in_addr _alias_addr);
int		PacketAliasRedirectDynamic(struct alias_link *_lnk);
void		PacketAliasRedirectDelete(struct alias_link *_lnk);
struct alias_link *
PacketAliasRedirectPort(struct in_addr _src_addr,
	unsigned short _src_port, struct in_addr _dst_addr,
	unsigned short _dst_port, struct in_addr _alias_addr,
	unsigned short _alias_port, unsigned char _proto);
struct alias_link *
PacketAliasRedirectProto(struct in_addr _src_addr,
	struct in_addr _dst_addr, struct in_addr _alias_addr,
	unsigned char _proto);

/* Fragment Handling functions. */
void		PacketAliasFragmentIn(char *_ptr, char *_ptr_fragment);
char		   *PacketAliasGetFragment(char *_ptr);
int		PacketAliasSaveFragment(char *_ptr);

/* Miscellaneous functions. */
int		PacketAliasCheckNewLink(void);
unsigned short
		PacketAliasInternetChecksum(unsigned short *_ptr, int _nbytes);
void		PacketAliasSetTarget(struct in_addr _target_addr);

/* Transparent proxying routines. */
int		PacketAliasProxyRule(const char *_cmd);

/* NEW API */

/* Initialization and control functions. */
struct libalias *LibAliasInit(struct libalias *);
void		LibAliasSetAddress(struct libalias *, struct in_addr _addr);
void		LibAliasSetFWBase(struct libalias *, unsigned int _base, unsigned int _num);
void		LibAliasSetSkinnyPort(struct libalias *, unsigned int _port);
unsigned int
		LibAliasSetMode(struct libalias *, unsigned int _flags, unsigned int _mask);
void		LibAliasUninit(struct libalias *);

/* Packet Handling functions. */
int		LibAliasIn (struct libalias *, char *_ptr, int _maxpacketsize);
int		LibAliasOut(struct libalias *, char *_ptr, int _maxpacketsize);
int		LibAliasOutTry(struct libalias *, char *_ptr, int _maxpacketsize, int _create);
int		LibAliasUnaliasOut(struct libalias *, char *_ptr, int _maxpacketsize);

/* Port and address redirection functions. */

int
LibAliasAddServer(struct libalias *, struct alias_link *_lnk,
	struct in_addr _addr, unsigned short _port);
struct alias_link *
LibAliasRedirectAddr(struct libalias *, struct in_addr _src_addr,
	struct in_addr _alias_addr);
int		LibAliasRedirectDynamic(struct libalias *, struct alias_link *_lnk);
void		LibAliasRedirectDelete(struct libalias *, struct alias_link *_lnk);
struct alias_link *
LibAliasRedirectPort(struct libalias *, struct in_addr _src_addr,
	unsigned short _src_port, struct in_addr _dst_addr,
	unsigned short _dst_port, struct in_addr _alias_addr,
	unsigned short _alias_port, unsigned char _proto);
struct alias_link *
LibAliasRedirectProto(struct libalias *, struct in_addr _src_addr,
	struct in_addr _dst_addr, struct in_addr _alias_addr,
	unsigned char _proto);

/* Fragment Handling functions. */
void		LibAliasFragmentIn(struct libalias *, char *_ptr, char *_ptr_fragment);
char		   *LibAliasGetFragment(struct libalias *, char *_ptr);
int		LibAliasSaveFragment(struct libalias *, char *_ptr);

/* Miscellaneous functions. */
int		LibAliasCheckNewLink(struct libalias *);
unsigned short
		LibAliasInternetChecksum(struct libalias *, unsigned short *_ptr, int _nbytes);
void		LibAliasSetTarget(struct libalias *, struct in_addr _target_addr);

/* Transparent proxying routines. */
int		LibAliasProxyRule(struct libalias *, const char *_cmd);

/* Module handling API */
int			 LibAliasLoadModule(char *);
int			 LibAliasUnLoadAllModule(void);
int			 LibAliasRefreshModules(void);

/* Mbuf helper function. */
struct mbuf	*m_megapullup(struct mbuf *, int);

/*
 * Mode flags and other constants.
 */


/* Mode flags, set using PacketAliasSetMode() */

/*
 * If PKT_ALIAS_LOG is set, a message will be printed to /var/log/alias.log
 * every time a link is created or deleted.  This is useful for debugging.
 */
#define	PKT_ALIAS_LOG			0x01

/*
 * If PKT_ALIAS_DENY_INCOMING is set, then incoming connections (e.g. to ftp,
 * telnet or web servers will be prevented by the aliasing mechanism.
 */
#define	PKT_ALIAS_DENY_INCOMING		0x02

/*
 * If PKT_ALIAS_SAME_PORTS is set, packets will be attempted sent from the
 * same port as they originated on.  This allows e.g. rsh to work *99% of the
 * time*, but _not_ 100% (it will be slightly flakey instead of not working
 * at all).  This mode bit is set by PacketAliasInit(), so it is a default
 * mode of operation.
 */
#define	PKT_ALIAS_SAME_PORTS		0x04

/*
 * If PKT_ALIAS_USE_SOCKETS is set, then when partially specified links (e.g.
 * destination port and/or address is zero), the packet aliasing engine will
 * attempt to allocate a socket for the aliasing port it chooses.  This will
 * avoid interference with the host machine.  Fully specified links do not
 * require this.  This bit is set after a call to PacketAliasInit(), so it is
 * a default mode of operation.
 */
#ifndef	NO_USE_SOCKETS
#define	PKT_ALIAS_USE_SOCKETS		0x08
#endif
/*-
 * If PKT_ALIAS_UNREGISTERED_ONLY is set, then only packets with
 * unregistered source addresses will be aliased.  Private
 * addresses are those in the following ranges:
 *
 *		10.0.0.0	 ->   10.255.255.255
 *		172.16.0.0   ->   172.31.255.255
 *		192.168.0.0  ->   192.168.255.255
 */
#define	PKT_ALIAS_UNREGISTERED_ONLY	0x10

/*
 * If PKT_ALIAS_RESET_ON_ADDR_CHANGE is set, then the table of dynamic
 * aliasing links will be reset whenever PacketAliasSetAddress() changes the
 * default aliasing address.  If the default aliasing address is left
 * unchanged by this function call, then the table of dynamic aliasing links
 * will be left intact.  This bit is set after a call to PacketAliasInit().
 */
#define	PKT_ALIAS_RESET_ON_ADDR_CHANGE	0x20

#ifndef NO_FW_PUNCH
/*
 * If PKT_ALIAS_PUNCH_FW is set, active FTP and IRC DCC connections will
 * create a 'hole' in the firewall to allow the transfers to work.  The
 * ipfw rule number that the hole is created with is controlled by
 * PacketAliasSetFWBase().  The hole will be attached to that
 * particular alias_link, so when the link goes away the hole is deleted.
 */
#define	PKT_ALIAS_PUNCH_FW		0x100
#endif

/*
 * If PKT_ALIAS_PROXY_ONLY is set, then NAT will be disabled and only
 * transparent proxying is performed.
 */
#define	PKT_ALIAS_PROXY_ONLY		0x40

/*
 * If PKT_ALIAS_REVERSE is set, the actions of PacketAliasIn() and
 * PacketAliasOut() are reversed.
 */
#define	PKT_ALIAS_REVERSE		0x80

/* Function return codes. */
#define	PKT_ALIAS_ERROR			-1
#define	PKT_ALIAS_OK			1
#define	PKT_ALIAS_IGNORED		2
#define	PKT_ALIAS_UNRESOLVED_FRAGMENT	3
#define	PKT_ALIAS_FOUND_HEADER_FRAGMENT	4



#endif				/* !_ALIAS_H_ */

/* lint -restore */
