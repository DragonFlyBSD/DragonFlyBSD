/*
 * Copyright (c) 1980, 1986, 1989, 1993
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
 *	@(#)netisr.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/netisr.h,v 1.21.2.5 2002/02/09 23:02:39 luigi Exp $
 * $DragonFly: src/sys/net/netisr.h,v 1.16 2004/04/21 18:13:51 dillon Exp $
 */

#ifndef _NET_NETISR_H_
#define _NET_NETISR_H_

#include <sys/msgport.h>

/*
 * The networking code runs off software interrupts.
 *
 * You can switch into the network by doing splnet() and return by splx().
 * The software interrupt level for the network is higher than the software
 * level for the clock (so you can enter the network in routines called
 * at timeout time).
 */

/*
 * Each ``pup-level-1'' input queue has a bit in a ``netisr'' status
 * word which is used to de-multiplex a single software
 * interrupt used for scheduling the network code to calls
 * on the lowest level routine of each protocol.
 */
#define NETISR_RESERVED0 0		/* cannot be used */
#define	NETISR_POLL	1		/* polling callback */
#define	NETISR_IP	2		/* same as AF_INET */
#define	NETISR_NS	6		/* same as AF_NS */
#define	NETISR_AARP	15		/* Appletalk ARP */
#define	NETISR_ATALK2	16		/* Appletalk phase 2 */
#define	NETISR_ATALK1	17		/* Appletalk phase 1 */
#define	NETISR_ARP	18		/* same as AF_LINK */
#define	NETISR_IPX	23		/* same as AF_IPX */
#define	NETISR_USB	25		/* USB soft interrupt */
#define	NETISR_PPP	27		/* PPP soft interrupt */
#define	NETISR_IPV6	28		/* same as AF_INET6 */
#define	NETISR_NATM	29		/* same as AF_NATM */
#define	NETISR_NETGRAPH	30		/* same as AF_NETGRAPH */
#define	NETISR_POLLMORE	31		/* check if we need more polling */

#define	NETISR_MAX	32

TAILQ_HEAD(notifymsglist, netmsg_so_notify);

#ifdef _KERNEL

#include <sys/protosw.h>

struct netmsg;

typedef int (*netisr_fn_t)(struct netmsg *);
typedef boolean_t (*msg_predicate_fn_t)(struct netmsg *);

/*
 * Base class.  All net messages must start with the same fields.
 */
struct netmsg {
    struct lwkt_msg	nm_lmsg;
};

struct netmsg_packet {
    struct lwkt_msg	nm_lmsg;
    struct mbuf		*nm_packet;
};

struct netmsg_pr_ctloutput {
    struct lwkt_msg	nm_lmsg;
    int			(*nm_prfn) (struct socket *, struct sockopt *);
    struct socket	*nm_so;
    struct sockopt	*nm_sopt;
};

struct netmsg_pr_timeout {
    struct lwkt_msg	nm_lmsg;
    int			(*nm_prfn) (void);
};

struct netmsg_so_notify {
    struct lwkt_msg			nm_lmsg;
    msg_predicate_fn_t			nm_predicate;
    struct socket			*nm_so;
    int					nm_etype;  /* receive or send event */
    TAILQ_ENTRY(netmsg_so_notify)	nm_list;
};

#define NM_REVENT	0x1		/* event on receive buffer */
#define NM_SEVENT	0x2		/* event on send buffer */

/*
 * for dispatching pr_ functions,
 * until they can be converted to message-passing
 */
int netmsg_pru_abort(lwkt_msg_t);
int netmsg_pru_accept(lwkt_msg_t);
int netmsg_pru_attach(lwkt_msg_t);
int netmsg_pru_bind(lwkt_msg_t);
int netmsg_pru_connect(lwkt_msg_t);
int netmsg_pru_connect2(lwkt_msg_t);
int netmsg_pru_control(lwkt_msg_t);
int netmsg_pru_detach(lwkt_msg_t);
int netmsg_pru_disconnect(lwkt_msg_t);
int netmsg_pru_listen(lwkt_msg_t);
int netmsg_pru_peeraddr(lwkt_msg_t);
int netmsg_pru_rcvd(lwkt_msg_t);
int netmsg_pru_rcvoob(lwkt_msg_t);
int netmsg_pru_send(lwkt_msg_t);
int netmsg_pru_sense(lwkt_msg_t);
int netmsg_pru_shutdown(lwkt_msg_t);
int netmsg_pru_sockaddr(lwkt_msg_t);

int netmsg_pru_sopoll(lwkt_msg_t);

int netmsg_pr_ctloutput(lwkt_msg_t);
int netmsg_pr_timeout(lwkt_msg_t);

int netmsg_so_notify(lwkt_msg_t);
int netmsg_so_notify_abort(lwkt_msg_t);

typedef lwkt_port_t (*lwkt_portfn_t)(struct mbuf *);

struct netisr {
	lwkt_port	ni_port;		/* must be first */
	lwkt_portfn_t	ni_mport;
	netisr_fn_t	ni_handler;
};

extern lwkt_port netisr_afree_rport;

lwkt_port_t	cpu0_portfn(struct mbuf *m);
void		netisr_dispatch(int, struct mbuf *);
int		netisr_queue(int, struct mbuf *);
void		netisr_register(int, lwkt_portfn_t, netisr_fn_t);
int		netisr_unregister(int);
void		netmsg_service_loop(void *arg);
void		schednetisr(int);

#endif	/* KERNEL */

#endif	/* _NET_NETISR_H_ */
