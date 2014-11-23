/*	$FreeBSD: src/sys/netinet6/in6_pcb.h,v 1.2.2.3 2001/08/13 16:26:17 ume Exp $	*/
/*	$KAME: in6_pcb.h,v 1.13 2001/02/06 09:16:53 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
 */

#ifndef _NETINET6_IN6_PCB_H_
#define	_NETINET6_IN6_PCB_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#ifndef _NETINET_IN_PCB_H_
#include <netinet/in_pcb.h>
#endif

#define	satosin6(sa)	((struct sockaddr_in6 *)(sa))
#define	sin6tosa(sin6)	((struct sockaddr *)(sin6))
#define	ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))

struct mbuf;
struct ifnet;
struct thread;
struct socket;
struct sockaddr;
struct inpcb;
struct in6pcb;
struct inpcbinfo;
struct inpcbportinfo;
struct inpcbhead;
struct ip6_moptions;
struct ip6_pktopts;
struct in6_addr;
struct route_in6;
struct sockaddr_in6;
union netmsg;

void	in6_pcbpurgeif0 (struct inpcbinfo *, struct ifnet *);
void	in6_losing (struct inpcb *);
int	in6_pcballoc (struct socket *, struct inpcbinfo *, struct thread *);
int	in6_pcbbind (struct inpcb *, struct sockaddr *, struct thread *);
int	in6_pcbconnect (struct inpcb *, struct sockaddr *, struct thread *);
void	in6_pcbdetach (struct inpcb *);
void	in6_pcbdisconnect (struct inpcb *);
int	in6_pcbladdr (struct inpcb *, struct sockaddr *,
			  struct in6_addr **, struct thread *);
struct	inpcb *
	in6_pcblookup_local (struct inpcbportinfo *, const struct in6_addr *,
			     u_int, int, struct ucred *);
struct	inpcb *
	in6_pcblookup_hash (struct inpcbinfo *,
				struct in6_addr *, u_int, struct in6_addr *,
				u_int, int, struct ifnet *);
void	in6_pcbnotify (struct inpcbinfo *, struct sockaddr *,
			   in_port_t, const struct sockaddr *, in_port_t,
			   int, int, inp_notify_t);
void	in6_rtchange (struct inpcb *, int);
void	in6_setpeeraddr_dispatch (union netmsg *);
void	in6_setsockaddr_dispatch (union netmsg *);
int	in6_setpeeraddr (struct socket *so, struct sockaddr **nam);
int	in6_setsockaddr (struct socket *so, struct sockaddr **nam);
void	in6_mapped_sockaddr_dispatch(union netmsg *msg);
int	in6_mapped_sockaddr (struct socket *so, struct sockaddr **nam);
int	in6_mapped_peeraddr (struct socket *so, struct sockaddr **nam);
void	in6_mapped_savefaddr (struct socket *so, const struct sockaddr *faddr);
void	in6_mapped_peeraddr_dispatch(netmsg_t msg);
struct	in6_addr *in6_selectsrc (struct sockaddr_in6 *,
				     struct ip6_pktopts *,
				     struct ip6_moptions *,
				     struct route_in6 *,
				     struct in6_addr *, int *, struct thread *);
int	in6_selecthlim (struct in6pcb *, struct ifnet *);
int	in6_pcbsetlport (struct in6_addr *, struct inpcb *, struct thread *);
void	init_sin6 (struct sockaddr_in6 *sin6, struct mbuf *m);

#endif /* _KERNEL */

#endif /* !_NETINET6_IN6_PCB_H_ */
