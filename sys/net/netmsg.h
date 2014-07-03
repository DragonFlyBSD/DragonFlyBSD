/*
 * Copyright (c) 2003 Jeffrey Hsu
 * All rights reserved.
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
 *	This product includes software developed by Jeffrey M. Hsu.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $DragonFly: src/sys/net/netmsg.h,v 1.10 2008/10/27 02:56:30 sephe Exp $
 */

#ifndef _NET_NETMSG_H_
#define _NET_NETMSG_H_

#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_PROTOSW_H_
#include <sys/protosw.h>
#endif

struct pktinfo;

typedef void (*netisr_fn_t)(netmsg_t);
typedef void (*netisr_ru_t)(void);
typedef void (*netisr_hashfn_t)(struct mbuf **, int);
typedef void (*netisr_hashck_t)(struct mbuf *, const struct pktinfo *);

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * The base netmsg prefixes all netmsgs and includes an embedded LWKT
 * message.
 */
struct netmsg_base {
	struct lwkt_msg		lmsg;
	netisr_fn_t		nm_dispatch;
	struct socket		*nm_so;
};

#define MSGF_IGNSOPORT		MSGF_USER0	/* don't check so_port */
#define MSGF_PROTO1		MSGF_USER1	/* protocol specific */

typedef struct netmsg_base *netmsg_base_t;

/*
 * NETISR messages
 *
 * NOTE:
 * - netmsg_packet is embedded in mbufs.
 * - netmsg_pru_send is embedded in mbufs.
 * - netmsg_inarp is embedded in mbufs.
 */
TAILQ_HEAD(notifymsglist, netmsg_so_notify);

struct netmsg_packet {
	struct netmsg_base	base;
	struct mbuf		*nm_packet;
	int			nm_nxt;
};

struct netmsg_inarp {
	struct netmsg_base	base;
	struct mbuf		*m;
	in_addr_t		saddr;
	in_addr_t		taddr;
	in_addr_t		myaddr;
};

struct netmsg_pr_timeout {
	struct netmsg_base	base;
};

struct netmsg_so_notify;
typedef __boolean_t (*msg_predicate_fn_t)(struct netmsg_so_notify *);

struct netmsg_so_notify {
	struct netmsg_base	base;
	msg_predicate_fn_t	nm_predicate;
	int			nm_fflags; /* flags e.g. FNONBLOCK */
	int			nm_etype;  /* receive or send event */
	TAILQ_ENTRY(netmsg_so_notify) nm_list;
};

struct netmsg_so_notify_abort {
	struct netmsg_base	base;
	struct netmsg_so_notify	*nm_notifymsg;
};

#define NM_REVENT	0x1		/* event on receive buffer */
#define NM_SEVENT	0x2		/* event on send buffer */

/*
 * User protocol requests messages.
 */
struct netmsg_pru_abort {
	struct netmsg_base	base;
};

struct netmsg_pru_accept {
	struct netmsg_base	base;
	struct sockaddr		**nm_nam;
};

struct netmsg_pru_attach {
	struct netmsg_base	base;
	int			nm_proto;
	struct pru_attach_info	*nm_ai;
};

struct netmsg_pru_bind {
	struct netmsg_base	base;
	struct sockaddr		*nm_nam;
	struct thread		*nm_td;
};

struct netmsg_pru_connect {
	struct netmsg_base	base;
	struct sockaddr		*nm_nam;
	struct thread		*nm_td;
	struct mbuf		*nm_m;		/* connect with send */
	int			nm_sndflags;	/* connect with send, PRUS_ */
	int			nm_flags;	/* message control */
};

#define PRUC_RECONNECT		0x0001		/* thread port change */
#define PRUC_NAMALLOC		0x0002		/* nm_nam allocated */
#define PRUC_PUSH		0x0004		/* call tcp_output */
#define PRUC_FALLBACK		0x0008		/* fallback to ipv4 */
#define PRUC_ASYNC		0x0010
#define PRUC_HELDTD		0x0020
#define PRUC_HASLADDR		0x0040

struct netmsg_pru_connect2 {
	struct netmsg_base	base;
	struct socket		*nm_so1;
	struct socket		*nm_so2;
};

struct netmsg_pru_control {
	struct netmsg_base	base;
	u_long			nm_cmd;
	caddr_t			nm_data;
	struct ifnet		*nm_ifp;
	struct thread		*nm_td;
};

struct netmsg_pru_detach {
	struct netmsg_base	base;
};

struct netmsg_pru_disconnect {
	struct netmsg_base	base;
};

struct netmsg_pru_listen {
	struct netmsg_base	base;
	struct thread		*nm_td;
	int			nm_flags;	/* PRUL_xxx */
};

#define PRUL_RELINK		0x1

struct netmsg_pru_peeraddr {
	struct netmsg_base	base;
	struct sockaddr		**nm_nam;
};

struct netmsg_pru_rcvd {
	struct netmsg_base	base;
	int			nm_flags;
	int			nm_pru_flags;	/* PRUR_xxx */
};

#define PRUR_ASYNC		0x1
#define PRUR_DEAD		0x2

struct netmsg_pru_rcvoob {
	struct netmsg_base	base;
	struct mbuf		*nm_m;
	int			nm_flags;
};

struct netmsg_pru_send {
	struct netmsg_base	base;
	int			nm_flags;	/* PRUS_xxx */
	int			nm_priv;	/* proto priv. */
	struct mbuf		*nm_m;
	struct sockaddr		*nm_addr;
	struct mbuf		*nm_control;
	struct thread		*nm_td;
};

#define PRUS_OOB		0x1
#define PRUS_EOF		0x2
#define PRUS_MORETOCOME		0x4
#define PRUS_NAMALLOC		0x8
#define PRUS_NOREPLY		0x10
#define PRUS_DONTROUTE		0x20
#define PRUS_FREEADDR		0x40
#define PRUS_HELDTD		0x80

struct netmsg_pru_sense {
	struct netmsg_base	base;
	struct stat		*nm_stat;
};

struct netmsg_pru_shutdown {
	struct netmsg_base	base;
};

struct netmsg_pru_sockaddr {
	struct netmsg_base	base;
	struct sockaddr		**nm_nam;
};

struct netmsg_pru_sosend {
	struct netmsg_base	base;
	struct sockaddr		*nm_addr;
	struct uio		*nm_uio;
	struct mbuf		*nm_top;
	struct mbuf		*nm_control;
	int			nm_flags;
	struct thread		*nm_td;
};

struct netmsg_pru_soreceive {
	struct netmsg_base	base;
	struct sockaddr		*nm_addr;
	struct sockaddr		**nm_paddr;
	struct uio		*nm_uio;
	struct sockbuf		*nm_sio;
	struct mbuf		**nm_controlp;
	int			*nm_flagsp;
};

struct netmsg_pr_ctloutput {
	struct netmsg_base	base;
	struct sockopt		*nm_sopt;
};

struct netmsg_pru_ctlinput {
	struct netmsg_base	base;
	int			nm_cmd;
	struct sockaddr		*nm_arg;
	void			*nm_extra;
};

/*
 * Union of all possible netmsgs.  Note that when a netmsg is sent the
 * actual allocated storage is likely only the size of the particular
 * class of message, and not sizeof(union netmsg).
 */
union netmsg {
	struct lwkt_msg			lmsg;		/* base embedded */
	struct netmsg_base		base;		/* base embedded */

	struct netmsg_packet		packet;		/* mbuf embedded */
	struct netmsg_pr_timeout	timeout;
	struct netmsg_so_notify		notify;
	struct netmsg_so_notify_abort	notify_abort;

	struct netmsg_pr_ctloutput	ctloutput;

	struct netmsg_pru_abort		abort;
	struct netmsg_pru_accept	accept;		/* synchronous */
	struct netmsg_pru_attach	attach;
	struct netmsg_pru_bind		bind;
	struct netmsg_pru_connect	connect;
	struct netmsg_pru_connect2	connect2;
	struct netmsg_pru_control	control;	/* synchronous */
	struct netmsg_pru_detach	detach;
	struct netmsg_pru_disconnect	disconnect;
	struct netmsg_pru_listen	listen;
	struct netmsg_pru_peeraddr	peeraddr;
	struct netmsg_pru_rcvd		rcvd;
	struct netmsg_pru_rcvoob	rcvoob;
	struct netmsg_pru_send		send;
	struct netmsg_pru_sense		sense;
	struct netmsg_pru_shutdown	shutdown;
	struct netmsg_pru_sockaddr	sockaddr;
	struct netmsg_pru_sosend	sosend;		/* synchronous */
	struct netmsg_pru_soreceive	soreceive;	/* synchronous */
	struct netmsg_pru_ctlinput	ctlinput;
};

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif	/* !_NET_NETMSG_H_ */
