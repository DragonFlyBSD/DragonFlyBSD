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
 * $DragonFly: src/sys/net/netisr.h,v 1.12 2004/04/09 22:34:09 hsu Exp $
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

#ifdef _KERNEL

#include <sys/protosw.h>

struct netmsg;

typedef void (*netisr_fn_t)(struct netmsg *);

/*
 * Base class.  All net messages must start with the same fields.
 */
struct netmsg {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
};

struct netmsg_packet {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    struct mbuf		*nm_packet;
};

struct netmsg_pr_ctloutput {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    int			(*nm_prfn) (struct socket *, struct sockopt *);
    struct socket	*nm_so;
    struct sockopt	*nm_sopt;
};

struct netmsg_pr_timeout {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    void		(*nm_prfn) (void);
};

/*
 * for dispatching pr_ functions,
 * until they can be converted to message-passing
 */
void netmsg_pr_dispatcher(struct netmsg *);

#define CMD_NETMSG_NEWPKT		(MSG_CMD_NETMSG | 0x0001)
#define CMD_NETMSG_POLL			(MSG_CMD_NETMSG | 0x0002)

#define CMD_NETMSG_PRU_ABORT		(MSG_CMD_NETMSG | 0x0003)
#define CMD_NETMSG_PRU_ACCEPT		(MSG_CMD_NETMSG | 0x0004)
#define CMD_NETMSG_PRU_ATTACH		(MSG_CMD_NETMSG | 0x0005)
#define CMD_NETMSG_PRU_BIND		(MSG_CMD_NETMSG | 0x0006)
#define CMD_NETMSG_PRU_CONNECT		(MSG_CMD_NETMSG | 0x0007)
#define CMD_NETMSG_PRU_CONNECT2		(MSG_CMD_NETMSG | 0x0008)
#define CMD_NETMSG_PRU_CONTROL		(MSG_CMD_NETMSG | 0x0009)
#define CMD_NETMSG_PRU_DETACH		(MSG_CMD_NETMSG | 0x000a)
#define CMD_NETMSG_PRU_DISCONNECT	(MSG_CMD_NETMSG | 0x000b)
#define CMD_NETMSG_PRU_LISTEN		(MSG_CMD_NETMSG | 0x000c)
#define CMD_NETMSG_PRU_PEERADDR		(MSG_CMD_NETMSG | 0x000d)
#define CMD_NETMSG_PRU_RCVD		(MSG_CMD_NETMSG | 0x000e)
#define CMD_NETMSG_PRU_RCVOOB		(MSG_CMD_NETMSG | 0x000f)
#define CMD_NETMSG_PRU_SEND		(MSG_CMD_NETMSG | 0x0010)
#define CMD_NETMSG_PRU_SENSE		(MSG_CMD_NETMSG | 0x0011)
#define CMD_NETMSG_PRU_SHUTDOWN		(MSG_CMD_NETMSG | 0x0012)
#define CMD_NETMSG_PRU_SOCKADDR		(MSG_CMD_NETMSG | 0x0013)
#define CMD_NETMSG_PRU_SOSEND		(MSG_CMD_NETMSG | 0x0014)
#define CMD_NETMSG_PRU_SORECEIVE	(MSG_CMD_NETMSG | 0x0015)
#define CMD_NETMSG_PRU_SOPOLL		(MSG_CMD_NETMSG | 0x0016)

#define CMD_NETMSG_PR_CTLOUTPUT		(MSG_CMD_NETMSG | 0x0017)
#define CMD_NETMSG_PR_TIMEOUT		(MSG_CMD_NETMSG | 0x0018)

#define	CMD_NETMSG_ONCPU		(MSG_CMD_NETMSG | 0x0019)

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
