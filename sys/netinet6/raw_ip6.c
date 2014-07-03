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
 * $FreeBSD: src/sys/netinet6/raw_ip6.c,v 1.7.2.7 2003/01/24 05:11:35 sam Exp $
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)raw_ip.c	8.2 (Berkeley) 1/4/94
 */

#include "opt_ipsec.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/jail.h>
#include <sys/protosw.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip6_mroute.h>
#include <netinet/icmp6.h>
#include <netinet/in_pcb.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6protosw.h>
#ifdef ENABLE_DEFAULT_SCOPE
#include <netinet6/scope6_var.h>
#endif
#include <netinet6/raw_ip6.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netinet6/ipsec6.h>
#endif /*IPSEC*/

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/ipsec6.h>
#endif /* FAST_IPSEC */

#include <machine/stdarg.h>

#define	satosin6(sa)	((struct sockaddr_in6 *)(sa))
#define	ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))

/*
 * Raw interface to IP6 protocol.
 */

extern struct	inpcbinfo ripcbinfo;
extern u_long	rip_sendspace;
extern u_long	rip_recvspace;

struct rip6stat rip6stat;

/*
 * Setup generic address and protocol structures
 * for raw_input routine, then pass them along with
 * mbuf chain.
 */
int
rip6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct inpcb *in6p;
	struct inpcb *last = NULL;
	struct mbuf *opts = NULL;
	struct sockaddr_in6 rip6src;

	rip6stat.rip6s_ipackets++;

	if (faithprefix_p != NULL && (*faithprefix_p)(&ip6->ip6_dst)) {
		/* XXX send icmp6 host/port unreach? */
		m_freem(m);
		return IPPROTO_DONE;
	}

	init_sin6(&rip6src, m); /* general init */

	LIST_FOREACH(in6p, &ripcbinfo.pcblisthead, inp_list) {
		if (in6p->in6p_flags & INP_PLACEMARKER)
			continue;
		if (!(in6p->in6p_vflag & INP_IPV6))
			continue;
		if (in6p->in6p_ip6_nxt &&
		    in6p->in6p_ip6_nxt != proto)
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_laddr) &&
		    !IN6_ARE_ADDR_EQUAL(&in6p->in6p_laddr, &ip6->ip6_dst))
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_faddr) &&
		    !IN6_ARE_ADDR_EQUAL(&in6p->in6p_faddr, &ip6->ip6_src))
			continue;
		if (in6p->in6p_cksum != -1) {
			rip6stat.rip6s_isum++;
			if (in6_cksum(m, ip6->ip6_nxt, *offp,
			    m->m_pkthdr.len - *offp)) {
				rip6stat.rip6s_badsum++;
				continue;
			}
		}
		if (last) {
			struct mbuf *n = m_copy(m, 0, (int)M_COPYALL);

#ifdef IPSEC
			/*
			 * Check AH/ESP integrity.
			 */
			if (n && ipsec6_in_reject_so(n, last->inp_socket)) {
				m_freem(n);
				ipsec6stat.in_polvio++;
				/* do not inject data into pcb */
			} else
#endif /*IPSEC*/
#ifdef FAST_IPSEC
			/*
			 * Check AH/ESP integrity.
			 */
			if (n && ipsec6_in_reject(n, last)) {
				m_freem(n);
				/* do not inject data into pcb */
			} else
#endif /*FAST_IPSEC*/
			if (n) {
				struct socket *so;

				so = last->in6p_socket;
				if ((last->in6p_flags & IN6P_CONTROLOPTS) ||
				    (so->so_options & SO_TIMESTAMP)) {
					ip6_savecontrol(last, &opts, ip6, n);
				}
				/* strip intermediate headers */
				m_adj(n, *offp);
				lwkt_gettoken(&so->so_rcv.ssb_token);
				if (ssb_appendaddr(&so->so_rcv,
						(struct sockaddr *)&rip6src,
						 n, opts) == 0) {
					m_freem(n);
					if (opts)
						m_freem(opts);
					rip6stat.rip6s_fullsock++;
				} else {
					sorwakeup(so);
				}
				lwkt_reltoken(&so->so_rcv.ssb_token);
				opts = NULL;
			}
		}
		last = in6p;
	}
#ifdef IPSEC
	/*
	 * Check AH/ESP integrity.
	 */
	if (last && ipsec6_in_reject_so(m, last->inp_socket)) {
		m_freem(m);
		ipsec6stat.in_polvio++;
		ip6stat.ip6s_delivered--;
		/* do not inject data into pcb */
	} else
#endif /*IPSEC*/
#ifdef FAST_IPSEC
	/*
	 * Check AH/ESP integrity.
	 */
	if (last && ipsec6_in_reject(m, last)) {
		m_freem(m);
		ip6stat.ip6s_delivered--;
		/* do not inject data into pcb */
	} else
#endif /*FAST_IPSEC*/
	if (last) {
		struct socket *so;

		so = last->in6p_socket;
		if ((last->in6p_flags & IN6P_CONTROLOPTS) ||
		    (so->so_options & SO_TIMESTAMP)) {
			ip6_savecontrol(last, &opts, ip6, m);
		}
		/* strip intermediate headers */
		m_adj(m, *offp);
		lwkt_gettoken(&so->so_rcv.ssb_token);
		if (ssb_appendaddr(&so->so_rcv, (struct sockaddr *)&rip6src,
				   m, opts) == 0) {
			m_freem(m);
			if (opts)
				m_freem(opts);
			rip6stat.rip6s_fullsock++;
		} else {
			sorwakeup(so);
		}
		lwkt_reltoken(&so->so_rcv.ssb_token);
	} else {
		rip6stat.rip6s_nosock++;
		if (m->m_flags & M_MCAST)
			rip6stat.rip6s_nosockmcast++;
		if (proto == IPPROTO_NONE)
			m_freem(m);
		else {
			char *prvnxtp = ip6_get_prevhdr(m, *offp); /* XXX */
			icmp6_error(m, ICMP6_PARAM_PROB,
				    ICMP6_PARAMPROB_NEXTHEADER,
				    prvnxtp - mtod(m, char *));
		}
		ip6stat.ip6s_delivered--;
	}
	return IPPROTO_DONE;
}

void
rip6_ctlinput(netmsg_t msg)
{
	int cmd = msg->ctlinput.nm_cmd;
	struct sockaddr *sa = msg->ctlinput.nm_arg;
	void *d = msg->ctlinput.nm_extra;
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	void (*notify) (struct inpcb *, int) = in6_rtchange;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		goto out;

	if ((unsigned)cmd >= PRC_NCMDS)
		goto out;
	if (PRC_IS_REDIRECT(cmd))
		notify = in6_rtchange, d = NULL;
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (inet6ctlerrmap[cmd] == 0)
		goto out;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		sa6_src = ip6cp->ip6c_src;
	} else {
		sa6_src = &sa6_any;
	}

	in6_pcbnotify(&ripcbinfo, sa, 0,
		      (const struct sockaddr *)sa6_src, 0, cmd, 0, notify);
out:
	lwkt_replymsg(&msg->ctlinput.base.lmsg, 0);
}

/*
 * Generate IPv6 header and pass packet to ip6_output.
 * Tack on options user may have setup with control call.
 */
int
rip6_output(struct mbuf *m, struct socket *so, ...)
{
	struct sockaddr_in6 *dstsock;
	struct mbuf *control;
	struct in6_addr *dst;
	struct ip6_hdr *ip6;
	struct inpcb *in6p;
	u_int	plen = m->m_pkthdr.len;
	int error = 0;
	struct ip6_pktopts opt, *optp = NULL;
	struct ifnet *oifp = NULL;
	int type = 0, code = 0;		/* for ICMPv6 output statistics only */
	int priv = 0;
	__va_list ap;

	__va_start(ap, so);
	dstsock = __va_arg(ap, struct sockaddr_in6 *);
	control = __va_arg(ap, struct mbuf *);
	__va_end(ap);

	in6p = so->so_pcb;

	priv = 0;
	if (so->so_cred->cr_uid == 0)
		priv = 1;
	dst = &dstsock->sin6_addr;
	if (control) {
		if ((error = ip6_setpktoptions(control, &opt,
		    in6p->in6p_outputopts, 
		    so->so_proto->pr_protocol, priv)) != 0)
			goto bad;
		optp = &opt;
	} else
		optp = in6p->in6p_outputopts;

	/*
	 * For an ICMPv6 packet, we should know its type and code
	 * to update statistics.
	 */
	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		struct icmp6_hdr *icmp6;
		if (m->m_len < sizeof(struct icmp6_hdr) &&
		    (m = m_pullup(m, sizeof(struct icmp6_hdr))) == NULL) {
			error = ENOBUFS;
			goto bad;
		}
		icmp6 = mtod(m, struct icmp6_hdr *);
		type = icmp6->icmp6_type;
		code = icmp6->icmp6_code;
	}

	M_PREPEND(m, sizeof(*ip6), MB_WAIT);
	ip6 = mtod(m, struct ip6_hdr *);

	/*
	 * Next header might not be ICMP6 but use its pseudo header anyway.
	 */
	ip6->ip6_dst = *dst;

	/*
	 * If the scope of the destination is link-local, embed the interface
	 * index in the address.
	 *
	 * XXX advanced-api value overrides sin6_scope_id
	 */
	if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_dst)) {
		struct in6_pktinfo *pi;

		/*
		 * XXX Boundary check is assumed to be already done in
		 * ip6_setpktoptions().
		 */
		if (optp && (pi = optp->ip6po_pktinfo) && pi->ipi6_ifindex) {
			ip6->ip6_dst.s6_addr16[1] = htons(pi->ipi6_ifindex);
			oifp = ifindex2ifnet[pi->ipi6_ifindex];
		} else if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) &&
			 in6p->in6p_moptions &&
			 in6p->in6p_moptions->im6o_multicast_ifp) {
			oifp = in6p->in6p_moptions->im6o_multicast_ifp;
			ip6->ip6_dst.s6_addr16[1] = htons(oifp->if_index);
		} else if (dstsock->sin6_scope_id) {
			/* boundary check */
			if (dstsock->sin6_scope_id < 0
			 || if_index < dstsock->sin6_scope_id) {
				error = ENXIO;  /* XXX EINVAL? */
				goto bad;
			}
			ip6->ip6_dst.s6_addr16[1]
				= htons(dstsock->sin6_scope_id & 0xffff);/*XXX*/
		}
	}

	/*
	 * Source address selection.
	 */
	{
		struct in6_addr *in6a;

		if ((in6a = in6_selectsrc(dstsock, optp,
					  in6p->in6p_moptions,
					  &in6p->in6p_route,
					  &in6p->in6p_laddr,
					  &error, NULL)) == NULL) {
			if (error == 0)
				error = EADDRNOTAVAIL;
			goto bad;
		}
		ip6->ip6_src = *in6a;
		if (in6p->in6p_route.ro_rt)
			oifp = ifindex2ifnet[in6p->in6p_route.ro_rt->rt_ifp->if_index];
	}
	ip6->ip6_flow = (ip6->ip6_flow & ~IPV6_FLOWINFO_MASK) |
		(in6p->in6p_flowinfo & IPV6_FLOWINFO_MASK);
	ip6->ip6_vfc = (ip6->ip6_vfc & ~IPV6_VERSION_MASK) |
		(IPV6_VERSION & IPV6_VERSION_MASK);
	/* ip6_plen will be filled in ip6_output, so not fill it here. */
	ip6->ip6_nxt = in6p->in6p_ip6_nxt;
	ip6->ip6_hlim = in6_selecthlim(in6p, oifp);

	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6 ||
	    in6p->in6p_cksum != -1) {
		struct mbuf *n;
		int off;
		u_int16_t *p;

		/* compute checksum */
		if (so->so_proto->pr_protocol == IPPROTO_ICMPV6)
			off = offsetof(struct icmp6_hdr, icmp6_cksum);
		else
			off = in6p->in6p_cksum;
		if (plen < off + 1) {
			error = EINVAL;
			goto bad;
		}
		off += sizeof(struct ip6_hdr);

		n = m;
		while (n && n->m_len <= off) {
			off -= n->m_len;
			n = n->m_next;
		}
		if (!n)
			goto bad;
		p = (u_int16_t *)(mtod(n, caddr_t) + off);
		*p = 0;
		*p = in6_cksum(m, ip6->ip6_nxt, sizeof(*ip6), plen);
	}

	error = ip6_output(m, optp, &in6p->in6p_route, 0,
			   in6p->in6p_moptions, &oifp, in6p);
	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		if (oifp)
			icmp6_ifoutstat_inc(oifp, type, code);
		icmp6stat.icp6s_outhist[type]++;
	} else
		rip6stat.rip6s_opackets++;

	goto freectl;

bad:
	if (m)
		m_freem(m);

freectl:
	if (optp == &opt && optp->ip6po_rthdr && optp->ip6po_route.ro_rt)
		RTFREE(optp->ip6po_route.ro_rt);
	if (control) {
		if (optp == &opt)
			ip6_clearpktopts(optp, -1);
		m_freem(control);
	}
	return (error);
}

/*
 * Raw IPv6 socket option processing.
 */
void
rip6_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	int error;

	if (sopt->sopt_level == IPPROTO_ICMPV6) {
		/*
		 * XXX: is it better to call icmp6_ctloutput() directly
		 * from protosw?
		 */
		icmp6_ctloutput(msg);
		/* msg invalid now */
		return;
	}
	if (sopt->sopt_level != IPPROTO_IPV6) {
		error = EINVAL;
		goto out;
	}

	error = 0;

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case MRT6_INIT:
		case MRT6_DONE:
		case MRT6_ADD_MIF:
		case MRT6_DEL_MIF:
		case MRT6_ADD_MFC:
		case MRT6_DEL_MFC:
		case MRT6_PIM:
			error = ip6_mrouter_get(so, sopt);
			break;
		case IPV6_CHECKSUM:
			error = ip6_raw_ctloutput(so, sopt);
			break;
		default:
			error = ip6_ctloutput(so, sopt);
			break;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
		case MRT6_INIT:
		case MRT6_DONE:
		case MRT6_ADD_MIF:
		case MRT6_DEL_MIF:
		case MRT6_ADD_MFC:
		case MRT6_DEL_MFC:
		case MRT6_PIM:
			error = ip6_mrouter_set(so, sopt);
			break;
		case IPV6_CHECKSUM:
			error = ip6_raw_ctloutput(so, sopt);
			break;
		default:
			error = ip6_ctloutput(so, sopt);
			break;
		}
		break;
	}
out:
	lwkt_replymsg(&msg->ctloutput.base.lmsg, error);
}

static void
rip6_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	int proto = msg->attach.nm_proto;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp)
		panic("rip6_attach");
	error = priv_check_cred(ai->p_ucred, PRIV_NETINET_RAW, NULL_CRED_OKAY);
	if (error)
		goto out;

	error = soreserve(so, rip_sendspace, rip_recvspace, ai->sb_rlimit);
	if (error)
		goto out;
	crit_enter();
	error = in_pcballoc(so, &ripcbinfo);
	crit_exit();
	if (error)
		goto out;
	inp = (struct inpcb *)so->so_pcb;
	inp->inp_vflag |= INP_IPV6;
	inp->in6p_ip6_nxt = (long)proto;
	inp->in6p_hops = -1;	/* use kernel default */
	inp->in6p_cksum = -1;
	inp->in6p_icmp6filt = kmalloc(sizeof(struct icmp6_filter), M_PCB,
				      M_NOWAIT);
	if (inp->in6p_icmp6filt != NULL)
		ICMP6_FILTER_SETPASSALL(inp->in6p_icmp6filt);
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
rip6_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		panic("rip6_detach");
	/* xxx: RSVP */
	if (so == ip6_mrouter)
		ip6_mrouter_done();
	if (inp->in6p_icmp6filt) {
		kfree(inp->in6p_icmp6filt, M_PCB);
		inp->in6p_icmp6filt = NULL;
	}
	in6_pcbdetach(inp);
	lwkt_replymsg(&msg->detach.base.lmsg, 0);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
rip6_abort(netmsg_t msg)
{
	soisdisconnected(msg->abort.base.nm_so);
	rip6_detach(msg);
	/* msg invalid now */
}

static void
rip6_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct inpcb *inp = so->so_pcb;

	if (so->so_state & SS_ISCONNECTED) {
		inp->in6p_faddr = kin6addr_any;
		soreference(so);
		rip6_abort(msg);
		/* msg invalid now */
		sofree(so);
		return;
	}
	lwkt_replymsg(&msg->disconnect.base.lmsg, ENOTCONN);
}

static void
rip6_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct inpcb *inp = so->so_pcb;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)nam;
	struct ifaddr *ia = NULL;
	int error;

	if (nam->sa_len != sizeof(*addr)) {
		error = EINVAL;
		goto out;
	}

	if (TAILQ_EMPTY(&ifnet) || addr->sin6_family != AF_INET6) {
		error = EADDRNOTAVAIL;
		goto out;
	}
#ifdef ENABLE_DEFAULT_SCOPE
	if (addr->sin6_scope_id == 0) {	/* not change if specified  */
		addr->sin6_scope_id = scope6_addr2default(&addr->sin6_addr);
	}
#endif
	if (!IN6_IS_ADDR_UNSPECIFIED(&addr->sin6_addr) &&
	    (ia = ifa_ifwithaddr((struct sockaddr *)addr)) == NULL) {
		error = EADDRNOTAVAIL;
		goto out;
	}
	if (ia &&
	    ((struct in6_ifaddr *)ia)->ia6_flags &
	    (IN6_IFF_ANYCAST|IN6_IFF_NOTREADY|
	     IN6_IFF_DETACHED|IN6_IFF_DEPRECATED)) {
		error = EADDRNOTAVAIL;
		goto out;
	}
	inp->in6p_laddr = addr->sin6_addr;
	error = 0;
out:
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
rip6_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct inpcb *inp = so->so_pcb;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)nam;
	struct in6_addr *in6a = NULL;
	int error = 0;
#ifdef ENABLE_DEFAULT_SCOPE
	struct sockaddr_in6 tmp;
#endif

	if (nam->sa_len != sizeof(*addr)) {
		error = EINVAL;
		goto out;
	}
	if (TAILQ_EMPTY(&ifnet)) {
		error = EADDRNOTAVAIL;
		goto out;
	}
	if (addr->sin6_family != AF_INET6) {
		error = EAFNOSUPPORT;
		goto out;
	}
#ifdef ENABLE_DEFAULT_SCOPE
	if (addr->sin6_scope_id == 0) {	/* not change if specified  */
		/* avoid overwrites */
		tmp = *addr;
		addr = &tmp;
		addr->sin6_scope_id = scope6_addr2default(&addr->sin6_addr);
	}
#endif
	/* Source address selection. XXX: need pcblookup? */
	in6a = in6_selectsrc(addr, inp->in6p_outputopts,
			     inp->in6p_moptions, &inp->in6p_route,
			     &inp->in6p_laddr, &error, NULL);
	if (in6a == NULL) {
		if (error == 0)
			error = EADDRNOTAVAIL;
	} else {
		inp->in6p_laddr = *in6a;
		inp->in6p_faddr = addr->sin6_addr;
		soisconnected(so);
		error = 0;
	}
out:
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
rip6_shutdown(netmsg_t msg)
{
	socantsendmore(msg->shutdown.base.nm_so);
	lwkt_replymsg(&msg->shutdown.base.lmsg, 0);
}

static void
rip6_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *nam = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct inpcb *inp = so->so_pcb;
	struct sockaddr_in6 tmp;
	struct sockaddr_in6 *dst;
	int error;

	/* always copy sockaddr to avoid overwrites */
	if (so->so_state & SS_ISCONNECTED) {
		if (nam) {
			m_freem(m);
			error = EISCONN;
			goto out;
		}
		/* XXX */
		bzero(&tmp, sizeof(tmp));
		tmp.sin6_family = AF_INET6;
		tmp.sin6_len = sizeof(struct sockaddr_in6);
		bcopy(&inp->in6p_faddr, &tmp.sin6_addr,
		      sizeof(struct in6_addr));
		dst = &tmp;
	} else {
		if (nam == NULL) {
			m_freem(m);
			error = ENOTCONN;
			goto out;
		}
		tmp = *(struct sockaddr_in6 *)nam;
		dst = &tmp;
	}
#ifdef ENABLE_DEFAULT_SCOPE
	if (dst->sin6_scope_id == 0) {	/* not change if specified  */
		dst->sin6_scope_id = scope6_addr2default(&dst->sin6_addr);
	}
#endif
	error = rip6_output(m, so, dst, control);
out:
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

struct pr_usrreqs rip6_usrreqs = {
	.pru_abort = rip6_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = rip6_attach,
	.pru_bind = rip6_bind,
	.pru_connect = rip6_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in6_control_dispatch,
	.pru_detach = rip6_detach,
	.pru_disconnect = rip6_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = in6_setpeeraddr_dispatch,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = rip6_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = rip6_shutdown,
	.pru_sockaddr = in6_setsockaddr_dispatch,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};
