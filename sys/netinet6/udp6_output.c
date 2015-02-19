/*	$FreeBSD: src/sys/netinet6/udp6_output.c,v 1.1.2.6 2003/01/23 21:06:47 sam Exp $	*/
/*	$KAME: udp6_output.c,v 1.31 2001/05/21 16:39:15 jinmei Exp $	*/

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
 */

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
 *	@(#)udp_var.h	8.1 (Berkeley) 6/10/93
 */

#include "opt_ipsec.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/udp6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6protosw.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#ifdef INET6
#include <netinet6/ipsec6.h>
#endif
#endif /* IPSEC */

#include <net/net_osdep.h>

/*
 * UDP protocol inplementation.
 * Per RFC 768, August, 1980.
 */

#define in6pcb		inpcb
#define udp6stat	udp_stat
#define udp6s_opackets	udps_opackets

int
udp6_output(struct in6pcb *in6p, struct mbuf *m, struct sockaddr *addr6,
	    struct mbuf *control, struct thread *td)
{
	u_int32_t ulen = m->m_pkthdr.len;
	u_int32_t plen = sizeof(struct udphdr) + ulen;
	struct ip6_hdr *ip6;
	struct udphdr *udp6;
	struct in6_addr *laddr, *faddr;
	u_short fport;
	int error = 0;
	struct ip6_pktopts opt, *stickyopt = in6p->in6p_outputopts;
	int priv;
	int hlen = sizeof(struct ip6_hdr);
	struct sockaddr_in6 tmp;

	priv = !priv_check(td, PRIV_ROOT);	/* 1 if privileged, 0 if not */
	if (control) {
		if ((error = ip6_setpktoptions(control, &opt,
		    in6p->in6p_outputopts, 
		    IPPROTO_UDP, priv)) != 0)
			goto release;
		in6p->in6p_outputopts = &opt;
	}

	if (addr6) {
		/*
		 * IPv4 version of udp_output calls in_pcbconnect in this case,
		 * which needs splnet and affects performance.
		 * Since we saw no essential reason for calling in_pcbconnect,
		 * we get rid of such kind of logic, and call in6_selectsrc
		 * and in6_pcbsetlport in order to fill in the local address
		 * and the local port.
		 */
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr6;

		/* Caller should have rejected the v4-mapped address */
		KASSERT(!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr),
		    ("v4-mapped address"));

		if (sin6->sin6_port == 0) {
			error = EADDRNOTAVAIL;
			goto release;
		}

		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_faddr)) {
			/* how about ::ffff:0.0.0.0 case? */
			error = EISCONN;
			goto release;
		}
		if (!prison_remote_ip(td, addr6)) {
			error = EAFNOSUPPORT; /* IPv4 only jail */
			goto release;
		}

		/* protect *sin6 from overwrites */
		tmp = *sin6;
		sin6 = &tmp;

		faddr = &sin6->sin6_addr;
		fport = sin6->sin6_port; /* allow 0 port */

		/* KAME hack: embed scopeid */
		if (in6_embedscope(&sin6->sin6_addr, sin6, in6p, NULL) != 0) {
			error = EINVAL;
			goto release;
		}

		laddr = in6_selectsrc(sin6, in6p->in6p_outputopts,
				      in6p->in6p_moptions,
				      &in6p->in6p_route,
				      &in6p->in6p_laddr, &error, NULL);
		if (laddr == NULL) {
			if (error == 0)
				error = EADDRNOTAVAIL;
			goto release;
		}
		if (in6p->in6p_lport == 0 &&
		    (error = in6_pcbsetlport(laddr, in6p, td)) != 0)
			goto release;
	} else {
		if (IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_faddr)) {
			error = ENOTCONN;
			goto release;
		}

		/* Connection to v4-mapped address should have been rejected */
		KASSERT(!IN6_IS_ADDR_V4MAPPED(&in6p->in6p_faddr),
		    ("bound to v4-mapped address"));

		laddr = &in6p->in6p_laddr;
		faddr = &in6p->in6p_faddr;
		fport = in6p->in6p_fport;
	}

	/*
	 * Calculate data length and get a mbuf
	 * for UDP and IP6 headers.
	 */
	M_PREPEND(m, hlen + sizeof(struct udphdr), M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto release;
	}

	/*
	 * Stuff checksum and output datagram.
	 */
	udp6 = (struct udphdr *)(mtod(m, caddr_t) + hlen);
	udp6->uh_sport = in6p->in6p_lport; /* lport is always set in the PCB */
	udp6->uh_dport = fport;
	if (plen <= 0xffff)
		udp6->uh_ulen = htons((u_short)plen);
	else
		udp6->uh_ulen = 0;
	udp6->uh_sum = 0;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow	= in6p->in6p_flowinfo & IPV6_FLOWINFO_MASK;
	ip6->ip6_vfc 	&= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc 	|= IPV6_VERSION;
#if 0				/* ip6_plen will be filled in ip6_output. */
	ip6->ip6_plen	= htons((u_short)plen);
#endif
	ip6->ip6_nxt	= IPPROTO_UDP;
	ip6->ip6_hlim	= in6_selecthlim(in6p,
					 in6p->in6p_route.ro_rt ?
					 in6p->in6p_route.ro_rt->rt_ifp : NULL);
	ip6->ip6_src	= *laddr;
	ip6->ip6_dst	= *faddr;

	if ((udp6->uh_sum = in6_cksum(m, IPPROTO_UDP,
			sizeof(struct ip6_hdr), plen)) == 0) {
		udp6->uh_sum = 0xffff;
	}

	udp6stat.udp6s_opackets++;
	error = ip6_output(m, in6p->in6p_outputopts, &in6p->in6p_route, 0,
	    in6p->in6p_moptions, NULL, in6p);
	goto releaseopt;

release:
	m_freem(m);

releaseopt:
	if (control) {
		ip6_clearpktopts(in6p->in6p_outputopts, -1);
		in6p->in6p_outputopts = stickyopt;
		m_freem(control);
	}
	return (error);
}
