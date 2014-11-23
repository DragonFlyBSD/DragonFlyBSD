/*	$FreeBSD: src/sys/netinet6/udp6_usrreq.c,v 1.6.2.13 2003/01/24 05:11:35 sam Exp $	*/
/*	$KAME: udp6_usrreq.c,v 1.27 2001/05/21 05:45:10 jinmei Exp $	*/

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

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/priv.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet/icmp6.h>
#include <netinet6/udp6_var.h>
#include <netinet6/ip6protosw.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netinet6/ipsec6.h>
#endif /* IPSEC */

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/ipsec6.h>
#endif /* FAST_IPSEC */

/*
 * UDP protocol inplementation.
 * Per RFC 768, August, 1980.
 */

extern	struct protosw inetsw[];
static	int in6_mcmatch (struct inpcb *, struct in6_addr *, struct ifnet *);

static int
in6_mcmatch(struct inpcb *in6p, struct in6_addr *ia6, struct ifnet *ifp)
{
	struct ip6_moptions *im6o = in6p->in6p_moptions;
	struct in6_multi_mship *imm;

	if (im6o == NULL)
		return 0;

	for (imm = im6o->im6o_memberships.lh_first; imm != NULL;
	     imm = imm->i6mm_chain.le_next) {
		if ((ifp == NULL ||
		     imm->i6mm_maddr->in6m_ifp == ifp) &&
		    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
				       ia6))
			return 1;
	}
	return 0;
}

int
udp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6;
	struct udphdr *uh;
	struct inpcb *in6p;
	struct  mbuf *opts = NULL;
	int off = *offp;
	int plen, ulen;
	struct sockaddr_in6 udp_in6;
	struct socket *so;
	struct inpcbinfo *pcbinfo = &udbinfo[0];

	IP6_EXTHDR_CHECK(m, off, sizeof(struct udphdr), IPPROTO_DONE);

	ip6 = mtod(m, struct ip6_hdr *);

	if (faithprefix_p != NULL && (*faithprefix_p)(&ip6->ip6_dst)) {
		/* XXX send icmp6 host/port unreach? */
		m_freem(m);
		return IPPROTO_DONE;
	}

	udp_stat.udps_ipackets++;

	plen = ntohs(ip6->ip6_plen) - off + sizeof(*ip6);
	uh = (struct udphdr *)((caddr_t)ip6 + off);
	ulen = ntohs((u_short)uh->uh_ulen);

	if (plen != ulen) {
		udp_stat.udps_badlen++;
		goto bad;
	}

	/*
	 * Checksum extended UDP header and data.
	 */
	if (uh->uh_sum == 0)
		udp_stat.udps_nosum++;
	else if (in6_cksum(m, IPPROTO_UDP, off, ulen) != 0) {
		udp_stat.udps_badsum++;
		goto bad;
	}

	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		struct	inpcb *last, *marker;

		/*
		 * Deliver a multicast datagram to all sockets
		 * for which the local and remote addresses and ports match
		 * those of the incoming datagram.  This allows more than
		 * one process to receive multicasts on the same port.
		 * (This really ought to be done for unicast datagrams as
		 * well, but that would cause problems with existing
		 * applications that open both address-specific sockets and
		 * a wildcard socket listening to the same port -- they would
		 * end up receiving duplicates of every unicast datagram.
		 * Those applications open the multiple sockets to overcome an
		 * inadequacy of the UDP socket interface, but for backwards
		 * compatibility we avoid the problem here rather than
		 * fixing the interface.  Maybe 4.5BSD will remedy this?)
		 */

		/*
		 * In a case that laddr should be set to the link-local
		 * address (this happens in RIPng), the multicast address
		 * specified in the received packet does not match with
		 * laddr. To cure this situation, the matching is relaxed
		 * if the receiving interface is the same as one specified
		 * in the socket and if the destination multicast address
		 * matches one of the multicast groups specified in the socket.
		 */

		/*
		 * Construct sockaddr format source address.
		 */
		init_sin6(&udp_in6, m); /* general init */
		udp_in6.sin6_port = uh->uh_sport;
		/*
		 * KAME note: traditionally we dropped udpiphdr from mbuf here.
		 * We need udphdr for IPsec processing so we do that later.
		 */

		/*
		 * Locate pcb(s) for datagram.
		 * (Algorithm copied from raw_intr().)
		 */
		last = NULL;

		marker = in_pcbmarker(mycpuid);

		GET_PCBINFO_TOKEN(pcbinfo);

		LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
		while ((in6p = LIST_NEXT(marker, inp_list)) != NULL) {
			LIST_REMOVE(marker, inp_list);
			LIST_INSERT_AFTER(in6p, marker, inp_list);

			if (in6p->inp_flags & INP_PLACEMARKER)
				continue;
			if (!(in6p->inp_vflag & INP_IPV6))
				continue;
			if (in6p->in6p_lport != uh->uh_dport)
				continue;
			if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_laddr)) {
				if (!IN6_ARE_ADDR_EQUAL(&in6p->in6p_laddr,
							&ip6->ip6_dst) &&
				    !in6_mcmatch(in6p, &ip6->ip6_dst,
						 m->m_pkthdr.rcvif))
					continue;
			}
			if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_faddr)) {
				if (!IN6_ARE_ADDR_EQUAL(&in6p->in6p_faddr,
							&ip6->ip6_src) ||
				   in6p->in6p_fport != uh->uh_sport)
					continue;
			}

			if (last != NULL) {
				struct mbuf *n;

#ifdef IPSEC
				/*
				 * Check AH/ESP integrity.
				 */
				if (ipsec6_in_reject_so(m, last->inp_socket))
					ipsec6stat.in_polvio++;
					/* do not inject data into pcb */
				else
#endif /* IPSEC */
#ifdef FAST_IPSEC
				/*
				 * Check AH/ESP integrity.
				 */
				if (ipsec6_in_reject(m, last))
					;
				else
#endif /* FAST_IPSEC */
				if ((n = m_copy(m, 0, M_COPYALL)) != NULL) {
					/*
					 * KAME NOTE: do not
					 * m_copy(m, offset, ...) above.
					 * ssb_appendaddr() expects M_PKTHDR,
					 * and m_copy() will copy M_PKTHDR
					 * only if offset is 0.
					 */
					so = last->in6p_socket;
					if ((last->in6p_flags & IN6P_CONTROLOPTS) ||
					    (so->so_options & SO_TIMESTAMP)) {
						ip6_savecontrol(last, &opts,
								ip6, n);
					}
					m_adj(n, off + sizeof(struct udphdr));
					lwkt_gettoken(&so->so_rcv.ssb_token);
					if (ssb_appendaddr(&so->so_rcv,
						    (struct sockaddr *)&udp_in6,
						    n, opts) == 0) {
						m_freem(n);
						if (opts)
							m_freem(opts);
						udp_stat.udps_fullsock++;
					} else {
						sorwakeup(so);
					}
					lwkt_reltoken(&so->so_rcv.ssb_token);
					opts = NULL;
				}
			}
			last = in6p;
			/*
			 * Don't look for additional matches if this one does
			 * not have either the SO_REUSEPORT or SO_REUSEADDR
			 * socket options set.  This heuristic avoids searching
			 * through all pcbs in the common case of a non-shared
			 * port.  It assumes that an application will never
			 * clear these options after setting them.
			 */
			if ((last->in6p_socket->so_options &
			     (SO_REUSEPORT | SO_REUSEADDR)) == 0)
				break;
		}
		LIST_REMOVE(marker, inp_list);

		REL_PCBINFO_TOKEN(pcbinfo);

		if (last == NULL) {
			/*
			 * No matching pcb found; discard datagram.
			 * (No need to send an ICMP Port Unreachable
			 * for a broadcast or multicast datgram.)
			 */
			udp_stat.udps_noport++;
			udp_stat.udps_noportmcast++;
			goto bad;
		}
#ifdef IPSEC
		/*
		 * Check AH/ESP integrity.
		 */
		if (ipsec6_in_reject_so(m, last->inp_socket)) {
			ipsec6stat.in_polvio++;
			goto bad;
		}
#endif /* IPSEC */
#ifdef FAST_IPSEC
		/*
		 * Check AH/ESP integrity.
		 */
		if (ipsec6_in_reject(m, last)) {
			goto bad;
		}
#endif /* FAST_IPSEC */
		if (last->in6p_flags & IN6P_CONTROLOPTS
		    || last->in6p_socket->so_options & SO_TIMESTAMP)
			ip6_savecontrol(last, &opts, ip6, m);

		m_adj(m, off + sizeof(struct udphdr));
		so = last->in6p_socket;
		lwkt_gettoken(&so->so_rcv.ssb_token);
		if (ssb_appendaddr(&so->so_rcv, (struct sockaddr *)&udp_in6,
				   m, opts) == 0) {
			udp_stat.udps_fullsock++;
			lwkt_reltoken(&so->so_rcv.ssb_token);
			goto bad;
		}
		sorwakeup(so);
		lwkt_reltoken(&so->so_rcv.ssb_token);
		return IPPROTO_DONE;
	}
	/*
	 * Locate pcb for datagram.
	 */
	in6p = in6_pcblookup_hash(pcbinfo, &ip6->ip6_src, uh->uh_sport,
				  &ip6->ip6_dst, uh->uh_dport, 1,
				  m->m_pkthdr.rcvif);
	if (in6p == NULL) {
		if (log_in_vain) {
			char buf[INET6_ADDRSTRLEN];

			strcpy(buf, ip6_sprintf(&ip6->ip6_dst));
			log(LOG_INFO,
			    "Connection attempt to UDP [%s]:%d from [%s]:%d\n",
			    buf, ntohs(uh->uh_dport),
			    ip6_sprintf(&ip6->ip6_src), ntohs(uh->uh_sport));
		}
		udp_stat.udps_noport++;
		if (m->m_flags & M_MCAST) {
			kprintf("UDP6: M_MCAST is set in a unicast packet.\n");
			udp_stat.udps_noportmcast++;
			goto bad;
		}
		icmp6_error(m, ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT, 0);
		return IPPROTO_DONE;
	}
#ifdef IPSEC
	/*
	 * Check AH/ESP integrity.
	 */
	if (ipsec6_in_reject_so(m, in6p->in6p_socket)) {
		ipsec6stat.in_polvio++;
		goto bad;
	}
#endif /* IPSEC */
#ifdef FAST_IPSEC
	/*
	 * Check AH/ESP integrity.
	 */
	if (ipsec6_in_reject(m, in6p)) {
		goto bad;
	}
#endif /* FAST_IPSEC */

	/*
	 * Construct sockaddr format source address.
	 * Stuff source address and datagram in user buffer.
	 */
	init_sin6(&udp_in6, m); /* general init */
	udp_in6.sin6_port = uh->uh_sport;
	if (in6p->in6p_flags & IN6P_CONTROLOPTS
	    || in6p->in6p_socket->so_options & SO_TIMESTAMP)
		ip6_savecontrol(in6p, &opts, ip6, m);
	m_adj(m, off + sizeof(struct udphdr));
	so = in6p->in6p_socket;
	lwkt_gettoken(&so->so_rcv.ssb_token);
	if (ssb_appendaddr(&so->so_rcv, (struct sockaddr *)&udp_in6,
			   m, opts) == 0) {
		udp_stat.udps_fullsock++;
		lwkt_reltoken(&so->so_rcv.ssb_token);
		goto bad;
	}
	sorwakeup(so);
	lwkt_reltoken(&so->so_rcv.ssb_token);
	return IPPROTO_DONE;
bad:
	if (m)
		m_freem(m);
	if (opts)
		m_freem(opts);
	return IPPROTO_DONE;
}

void
udp6_ctlinput(netmsg_t msg)
{
	int cmd = msg->ctlinput.nm_cmd;
	struct sockaddr *sa = msg->ctlinput.nm_arg;
	void *d = msg->ctlinput.nm_extra;
	struct udphdr uh;
	struct ip6_hdr *ip6;
	struct mbuf *m;
	int off = 0;
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	inp_notify_t notify = udp_notify;
	struct udp_portonly {
		u_int16_t uh_sport;
		u_int16_t uh_dport;
	} *uhp;

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
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		sa6_src = ip6cp->ip6c_src;
	} else {
		m = NULL;
		ip6 = NULL;
		sa6_src = &sa6_any;
	}

	if (ip6) {
		/*
		 * XXX: We assume that when IPV6 is non NULL,
		 * M and OFF are valid.
		 */

		/* check if we can safely examine src and dst ports */
		if (m->m_pkthdr.len < off + sizeof(*uhp))
			return;

		bzero(&uh, sizeof(uh));
		m_copydata(m, off, sizeof(*uhp), (caddr_t)&uh);

		in6_pcbnotify(&udbinfo[0], sa, uh.uh_dport,
			      (struct sockaddr *)ip6cp->ip6c_src, uh.uh_sport,
			      cmd, 0, notify);
	} else {
		in6_pcbnotify(&udbinfo[0], sa, 0,
			      (const struct sockaddr *)sa6_src, 0,
			      cmd, 0, notify);
	}
out:
	lwkt_replymsg(&msg->ctlinput.base.lmsg, 0);
}

static int
udp6_getcred(SYSCTL_HANDLER_ARGS)
{
	struct sockaddr_in6 addrs[2];
	struct inpcb *inp;
	int error;

	error = priv_check(req->td, PRIV_ROOT);
	if (error)
		return (error);

	if (req->newlen != sizeof(addrs))
		return (EINVAL);
	if (req->oldlen != sizeof(struct ucred))
		return (EINVAL);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	crit_enter();
	inp = in6_pcblookup_hash(&udbinfo[0], &addrs[1].sin6_addr,
				 addrs[1].sin6_port,
				 &addrs[0].sin6_addr, addrs[0].sin6_port,
				 1, NULL);
	if (!inp || !inp->inp_socket) {
		error = ENOENT;
		goto out;
	}
	error = SYSCTL_OUT(req, inp->inp_socket->so_cred,
			   sizeof(struct ucred));

out:
	crit_exit();
	return (error);
}

SYSCTL_PROC(_net_inet6_udp6, OID_AUTO, getcred, CTLTYPE_OPAQUE|CTLFLAG_RW,
	    0, 0,
	    udp6_getcred, "S,ucred", "Get the ucred of a UDP6 connection");

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
udp6_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp) {
		soisdisconnected(so);

		in6_pcbdetach(inp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->abort.base.lmsg, error);
}

static void
udp6_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp != NULL) {
		error = EINVAL;
		goto out;
	}

	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		error = soreserve(so, udp_sendspace, udp_recvspace,
		    ai->sb_rlimit);
		if (error)
			goto out;
	}

	error = in_pcballoc(so, &udbinfo[0]);
	if (error)
		goto out;

	inp = (struct inpcb *)so->so_pcb;
	inp->inp_vflag |= INP_IPV6;
	if (!ip6_v6only)
		inp->inp_vflag |= INP_IPV4;
	inp->in6p_hops = -1;	/* use kernel default */
	inp->in6p_cksum = -1;	/* just to be sure */
	/*
	 * XXX: ugly!!
	 * IPv4 TTL initialization is necessary for an IPv6 socket as well,
	 * because the socket may be bound to an IPv6 wildcard address,
	 * which may match an IPv4-mapped IPv6 address.
	 */
	inp->inp_ip_ttl = ip_defttl;
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
udp6_bind(netmsg_t msg)
{
	struct socket *so =msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct thread *td = msg->bind.nm_td;
	struct sockaddr_in6 *sin6_p = (struct sockaddr_in6 *)nam;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}

	inp->inp_vflag &= ~INP_IPV4;
	inp->inp_vflag |= INP_IPV6;
	if (!(inp->inp_flags & IN6P_IPV6_V6ONLY)) {
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6_p->sin6_addr))
			inp->inp_vflag |= INP_IPV4;
		else if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
			struct sockaddr_in sin;

			in6_sin6_2_sin(&sin, sin6_p);
			inp->inp_vflag |= INP_IPV4;
			inp->inp_vflag &= ~INP_IPV6;
			crit_enter();
			error = in_pcbbind(inp, (struct sockaddr *)&sin, td);
			crit_exit();
			goto out;
		}
	}

	crit_enter();
	error = in6_pcbbind(inp, nam, td);
	crit_exit();
	if (error == 0) {
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6_p->sin6_addr))
			inp->inp_flags |= INP_WASBOUND_NOTANY;
		in_pcbinswildcardhash(inp);
	}
out:
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
udp6_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}

	if (!(inp->inp_flags & IN6P_IPV6_V6ONLY)) {
		struct sockaddr_in6 *sin6_p;

		sin6_p = (struct sockaddr_in6 *)nam;
		if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
			struct sockaddr_in sin;

			if (inp->inp_faddr.s_addr != INADDR_ANY) {
				error = EISCONN;
				goto out;
			}
			in6_sin6_2_sin(&sin, sin6_p);
			crit_enter();
			if (inp->inp_flags & INP_WILDCARD)
				in_pcbremwildcardhash(inp);
			error = in_pcbconnect(inp, (struct sockaddr *)&sin, td);
			crit_exit();
			if (error == 0) {
				inp->inp_vflag |= INP_IPV4;
				inp->inp_vflag &= ~INP_IPV6;
				soisconnected(so);
			}
			goto out;
		}
	}
	if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		error = EISCONN;
		goto out;
	}
	if (inp->inp_flags & INP_WILDCARD)
		in_pcbremwildcardhash(inp);
	if (!prison_remote_ip(td, nam)) {
		error = EAFNOSUPPORT; /* IPv4 only jail */
		goto out;
	}
	crit_enter();
	error = in6_pcbconnect(inp, nam, td);
	crit_exit();
	if (error == 0) {
		if (!ip6_v6only) { /* should be non mapped addr */
			inp->inp_vflag &= ~INP_IPV4;
			inp->inp_vflag |= INP_IPV6;
		}
		soisconnected(so);
	} else if (error == EAFNOSUPPORT) {	/* connection dissolved */
		/*
		 * Follow traditional BSD behavior and retain
		 * the local port binding.  But, fix the old misbehavior
		 * of overwriting any previously bound local address.
		 */
		if (!(inp->inp_flags & INP_WASBOUND_NOTANY))
			inp->in6p_laddr = kin6addr_any;
		in_pcbinswildcardhash(inp);
	}
out:
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
udp6_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp) {
		in6_pcbdetach(inp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

static void
udp6_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct inpcb *inp;
	int error;

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}

	if (inp->inp_vflag & INP_IPV4) {
		const struct pr_usrreqs *pru;

		pru = inetsw[ip_protox[IPPROTO_UDP]].pr_usrreqs;
		pru->pru_disconnect(msg);	/* XXX on right port? */
		return;
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr)) {
		error = ENOTCONN;
	} else {
		in6_pcbdisconnect(inp);
		soclrstate(so, SS_ISCONNECTED);		/* XXX */
		error = 0;
	}
out:
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
udp6_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *addr = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct thread *td = msg->send.nm_td;
	struct inpcb *inp;
	int error = 0;

	inp = so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto bad;
	}

	if (addr) {
		if (addr->sa_len != sizeof(struct sockaddr_in6)) {
			error = EINVAL;
			goto bad;
		}
		if (addr->sa_family != AF_INET6) {
			error = EAFNOSUPPORT;
			goto bad;
		}
	}

	if (!ip6_v6only) {
		int hasv4addr;
		struct sockaddr_in6 *sin6 = NULL;

		if (addr == NULL)
			hasv4addr = (inp->inp_vflag & INP_IPV4);
		else {
			sin6 = (struct sockaddr_in6 *)addr;
			hasv4addr = IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)
				? 1 : 0;
		}
		if (hasv4addr) {
			const struct pr_usrreqs *pru;

			if (sin6)
				in6_sin6_2_sin_in_sock(addr);
			pru = inetsw[ip_protox[IPPROTO_UDP]].pr_usrreqs;
			pru->pru_send(msg);
			/* msg invalid now */
			return;
		}
	}

	error = udp6_output(inp, m, addr, control, td);
	lwkt_replymsg(&msg->send.base.lmsg, error);
	return;
bad:
	m_freem(m);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

struct pr_usrreqs udp6_usrreqs = {
	.pru_abort = udp6_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = udp6_attach,
	.pru_bind = udp6_bind,
	.pru_connect = udp6_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in6_control_dispatch,
	.pru_detach = udp6_detach,
	.pru_disconnect = udp6_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = in6_mapped_peeraddr_dispatch,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = udp6_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = udp_shutdown,
	.pru_sockaddr = in6_mapped_sockaddr_dispatch,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

