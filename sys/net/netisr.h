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
 */

#ifndef _NET_NETISR_H_
#define _NET_NETISR_H_

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
#define	NETISR_IP	2		/* same as AF_INET */
#define	NETISR_ARP	18		/* same as AF_LINK */
#define	NETISR_MPLS	21		/* MPLS */
#define	NETISR_USB	25		/* USB soft interrupt */
#define	NETISR_PPP	27		/* PPP soft interrupt */
#define	NETISR_IPV6	28		/* same as AF_INET6 */
#define	NETISR_NATM	29		/* same as AF_NATM */
#define	NETISR_NETGRAPH	30		/* same as AF_NETGRAPH */
#define	NETISR_BLUETOOTH 31

#define	NETISR_MAX	32

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_PROTOSW_H_
#include <sys/protosw.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif
#ifndef _NET_NETMSG_H_
#include <net/netmsg.h>
#endif

#endif

#ifdef _KERNEL

void netmsg_so_notify(netmsg_t);
void netmsg_so_notify_abort(netmsg_t);
void netmsg_so_notify_doabort(lwkt_msg_t);

#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Temporary pktinfo structure passed directly from the driver to
 * ether_input_pkt(), allows us to bypass numerous checks.
 */
struct pktinfo {
	int		pi_netisr;	/* netisr index, e.g. NETISR_IP */
	uint32_t	pi_flags;	/* PKTINFO_FLAG_ */
	int		pi_l3proto;	/* layer3 protocol number */
};

#define PKTINFO_FLAG_FRAG      0x1

/*
 * NETISR_xxx registrations
 */
struct netisr {
	netisr_fn_t	ni_handler;	/* packet handler function */
	netisr_hashck_t	ni_hashck;	/* hash check function */
	netisr_hashfn_t	ni_hashfn;	/* characterize pkt return hash */
	struct netmsg_base ni_netmsg;	/* for sched_netisr() (no-data) */
};

#endif

#ifdef _KERNEL

#define NETISR_ROLLUP_PRIO_TCP		200
#define NETISR_ROLLUP_PRIO_IFSTART	50

extern lwkt_port netisr_adone_rport;
extern lwkt_port netisr_afree_rport;
extern lwkt_port netisr_afree_free_so_rport;
extern lwkt_port netisr_apanic_rport;
extern lwkt_port netisr_sync_port;

void		netisr_register(int, netisr_fn_t, netisr_hashfn_t);
void		netisr_register_hashcheck(int, netisr_hashck_t);
void		netisr_register_rollup(netisr_ru_t ru_func, int ru_prio);

void		netisr_characterize(int num, struct mbuf **mp, int hoff);
void		netisr_hashcheck(int num, struct mbuf *m,
		    const struct pktinfo *pi);
int		netisr_queue(int, struct mbuf *);
int		netisr_handle(int, struct mbuf *);

struct netisr_barrier *netisr_barrier_create(void);
void		netisr_barrier_set(struct netisr_barrier *);
void		netisr_barrier_rem(struct netisr_barrier *);

void		netmsg_service_port_init(lwkt_port_t);
void		netmsg_service_sync(void);
void		netmsg_sync_handler(netmsg_t);
void		schednetisr(int);

#endif	/* _KERNEL */

#endif	/* _NET_NETISR_H_ */
