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
 *	@(#)ip_icmp.c	8.2 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/netinet/ip_icmp.c,v 1.39.2.19 2003/01/24 05:11:34 sam Exp $
 */

#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketops.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/in_cksum.h>

#include <machine/stdarg.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>
#include <net/route.h>

#define _IP_VHL
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/icmp_var.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netproto/key/key.h>
#endif

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/key.h>
#define	IPSEC
#endif

/*
 * ICMP routines: error generation, receive packet processing, and
 * routines to turnaround packets back to the originator, and
 * host table maintenance routines.
 */

struct icmpstat icmpstat;
SYSCTL_STRUCT(_net_inet_icmp, ICMPCTL_STATS, stats, CTLFLAG_RW,
	&icmpstat, icmpstat, "ICMP statistics");

static int	icmpmaskrepl = 0;
SYSCTL_INT(_net_inet_icmp, ICMPCTL_MASKREPL, maskrepl, CTLFLAG_RW,
	&icmpmaskrepl, 0, "Allow replies to netmask requests");

static int	drop_redirect = 0;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, drop_redirect, CTLFLAG_RW,
	&drop_redirect, 0, "Ignore ICMP redirects");

static int	log_redirect = 0;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, log_redirect, CTLFLAG_RW,
	&log_redirect, 0, "Enable output about ICMP redirects");

static int	discard_sourcequench = 1;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, discard_sourcequench, CTLFLAG_RW,
	&discard_sourcequench, 0, "Discard ICMP Source Quench");

#ifdef ICMP_BANDLIM

/*
 * ICMP error-response bandwidth limiting sysctl.  If not enabled, sysctl
 *      variable content is -1 and read-only.
 */

static int      icmplim = 200;
SYSCTL_INT(_net_inet_icmp, ICMPCTL_ICMPLIM, icmplim, CTLFLAG_RW,
	&icmplim, 0, "ICMP bandwidth limit");
#else

static int      icmplim = -1;
SYSCTL_INT(_net_inet_icmp, ICMPCTL_ICMPLIM, icmplim, CTLFLAG_RD,
	&icmplim, 0, "ICMP bandwidth limit");
	
#endif

static int	icmplim_output = 0;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, icmplim_output, CTLFLAG_RW,
	&icmplim_output, 0, "Enable output about ICMP bandwidth limits");

/*
 * ICMP broadcast echo sysctl
 */

static int	icmpbmcastecho = 0;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, bmcastecho, CTLFLAG_RW,
    &icmpbmcastecho, 0, "");

static char	icmp_reply_src[IFNAMSIZ];
SYSCTL_STRING(_net_inet_icmp, OID_AUTO, reply_src, CTLFLAG_RW,
	icmp_reply_src, IFNAMSIZ, "icmp reply source for non-local packets.");

static int	icmp_rfi;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, reply_from_interface, CTLFLAG_RW,
	&icmp_rfi, 0, "ICMP reply from incoming interface for "
	"non-local packets");

#ifdef ICMPPRINTFS
static int	icmpprintfs = 0;
SYSCTL_INT(_net_inet_icmp, OID_AUTO, debug_prints, CTLFLAG_RW,
	&icmpprintfs, 0, "extra ICMP debug prints");
#endif

static void	icmp_reflect (struct mbuf *);
static void	icmp_send (struct mbuf *, struct mbuf *, struct route *);

extern	struct protosw inetsw[];

/*
 * Generate an error packet of type error
 * in response to bad packet ip.
 */
void
icmp_error(struct mbuf *n, int type, int code, n_long dest, int destmtu)
{
	struct ip *oip = mtod(n, struct ip *), *nip;
	unsigned oiplen = IP_VHL_HL(oip->ip_vhl) << 2;
	struct icmp *icp;
	struct mbuf *m;
	unsigned icmplen;

#ifdef ICMPPRINTFS
	if (icmpprintfs)
		kprintf("icmp_error(%p, %d, %d)\n", oip, type, code);
#endif
	if (type != ICMP_REDIRECT)
		icmpstat.icps_error++;
	/*
	 * Don't send error if the original packet was encrypted.
	 * Don't send error if not the first fragment of message.
	 * Don't error if the old packet protocol was ICMP
	 * error message, only known informational types.
	 */
	if (n->m_flags & M_DECRYPTED)
		goto freeit;
	if (oip->ip_off &~ (IP_MF|IP_DF))
		goto freeit;
	if (oip->ip_p == IPPROTO_ICMP && type != ICMP_REDIRECT &&
	  n->m_len >= oiplen + ICMP_MINLEN &&
	  !ICMP_INFOTYPE(((struct icmp *)((caddr_t)oip + oiplen))->icmp_type)) {
		icmpstat.icps_oldicmp++;
		goto freeit;
	}
	/* Don't send error in response to a multicast or broadcast packet */
	if (n->m_flags & (M_BCAST|M_MCAST))
		goto freeit;
	/*
	 * First, formulate icmp message
	 */
	m = m_gethdr(M_NOWAIT, MT_HEADER);
	if (m == NULL)
		goto freeit;
	icmplen = min(oiplen + 8, oip->ip_len);
	if (icmplen < sizeof(struct ip))
		panic("icmp_error: bad length");
	m->m_len = icmplen + ICMP_MINLEN;
	MH_ALIGN(m, m->m_len);
	icp = mtod(m, struct icmp *);
	if ((u_int)type > ICMP_MAXTYPE)
		panic("icmp_error");
	icmpstat.icps_outhist[type]++;
	icp->icmp_type = type;
	if (type == ICMP_REDIRECT)
		icp->icmp_gwaddr.s_addr = dest;
	else {
		icp->icmp_void = 0;
		/*
		 * The following assignments assume an overlay with the
		 * zeroed icmp_void field.
		 */
		if (type == ICMP_PARAMPROB) {
			icp->icmp_pptr = code;
			code = 0;
		} else if (type == ICMP_UNREACH &&
			code == ICMP_UNREACH_NEEDFRAG && destmtu) {
			icp->icmp_nextmtu = htons(destmtu);
		}
	}

	icp->icmp_code = code;
	m_copydata(n, 0, icmplen, (caddr_t)&icp->icmp_ip);
	nip = &icp->icmp_ip;

	/*
	 * Convert fields to network representation.
	 */
	nip->ip_len = htons(nip->ip_len);
	nip->ip_off = htons(nip->ip_off);

	/*
	 * Now, copy old ip header (without options)
	 * in front of icmp message.
	 */
	if (m->m_data - sizeof(struct ip) < m->m_pktdat)
		panic("icmp len");
	m->m_data -= sizeof(struct ip);
	m->m_len += sizeof(struct ip);
	m->m_pkthdr.len = m->m_len;
	m->m_pkthdr.rcvif = n->m_pkthdr.rcvif;
	nip = mtod(m, struct ip *);
	bcopy(oip, nip, sizeof(struct ip));
	nip->ip_len = m->m_len;
	nip->ip_vhl = IP_VHL_BORING;
	nip->ip_p = IPPROTO_ICMP;
	nip->ip_tos = 0;
	m->m_pkthdr.fw_flags |= n->m_pkthdr.fw_flags & FW_MBUF_GENERATED;
	icmp_reflect(m);

freeit:
	m_freem(n);
}

static void
icmp_ctlinput_done_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;
	struct mbuf *m = msg->m;
	int hlen = msg->hlen;

	rip_input(&m, &hlen, msg->proto);
}

static void
icmp_ctlinput_done(struct mbuf *m)
{
	struct netmsg_ctlinput *msg = &m->m_hdr.mh_ctlmsg;

	netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
	    icmp_ctlinput_done_handler);
	lwkt_sendmsg(netisr_cpuport(0), &msg->base.lmsg);
}

static void
icmp_mtudisc(struct mbuf *m, int hlen)
{
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct rtentry *rt;
	struct icmp *icp;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));

	icp = mtodoff(m, struct icmp *, hlen);
	icmpsrc.sin_addr = icp->icmp_ip.ip_dst;

	/*
	 * MTU discovery:
	 * If we got a needfrag and there is a host route to the original
	 * destination, and the MTU is not locked, then set the MTU in the
	 * route to the suggested new value (if given) and then notify as
	 * usual.  The ULPs will notice that the MTU has changed and adapt
	 * accordingly.  If no new MTU was suggested, then we guess a new
	 * one less than the current value.  If the new MTU is unreasonably
	 * small (arbitrarily set at 296), then we reset the MTU to the
	 * interface value and enable the lock bit, indicating that we are
	 * no longer doing MTU discovery.
	 */
	rt = rtpurelookup((struct sockaddr *)&icmpsrc);
	if (rt != NULL && (rt->rt_flags & RTF_HOST) &&
	    !(rt->rt_rmx.rmx_locks & RTV_MTU)) {
#ifdef DEBUG_MTUDISC
		char src_buf[INET_ADDRSTRLEN];
#endif
		int mtu;

		mtu = ntohs(icp->icmp_nextmtu);
		if (!mtu)
			mtu = ip_next_mtu(rt->rt_rmx.rmx_mtu, 1);
#ifdef DEBUG_MTUDISC
		kprintf("MTU for %s reduced to %d\n",
		    inet_ntop(AF_INET, &icmpsrc.sin_addr,
			src_buf, INET_ADDRSTRLEN), mtu);
#endif
		if (mtu < 296) {
			/* rt->rt_rmx.rmx_mtu = rt->rt_ifp->if_mtu; */
			rt->rt_rmx.rmx_locks |= RTV_MTU;
		} else if (rt->rt_rmx.rmx_mtu > mtu) {
			rt->rt_rmx.rmx_mtu = mtu;
		}
	}
	if (rt != NULL)
		--rt->rt_refcnt;

	/*
	 * XXX if the packet contains [IPv4 AH TCP], we can't make a
	 * notification to TCP layer.
	 */
	so_pr_ctlinput_direct(&inetsw[ip_protox[icp->icmp_ip.ip_p]],
	    PRC_MSGSIZE, (struct sockaddr *)&icmpsrc, &icp->icmp_ip);
}

static void
icmp_mtudisc_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;
	int nextcpu;

	icmp_mtudisc(msg->m, msg->hlen);

	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &msg->base.lmsg);
	else
		icmp_ctlinput_done(msg->m);
}

static boolean_t
icmp_mtudisc_start(struct mbuf *m, int hlen, int proto)
{
	struct netmsg_ctlinput *msg;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));

	icmp_mtudisc(m, hlen);

	if (ncpus == 1) {
		/* There is only one netisr; done */
		return FALSE;
	}

	msg = &m->m_hdr.mh_ctlmsg;
	netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
	    icmp_mtudisc_handler);
	msg->m = m;
	msg->cmd = PRC_MSGSIZE;
	msg->hlen = hlen;
	msg->proto = proto;

	lwkt_sendmsg(netisr_cpuport(1), &msg->base.lmsg);
	return TRUE;
}

static void
icmp_ctlinput(struct mbuf *m, int cmd, int hlen)
{
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct icmp *icp;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));

	icp = mtodoff(m, struct icmp *, hlen);
	icmpsrc.sin_addr = icp->icmp_ip.ip_dst;

	/*
	 * XXX if the packet contains [IPv4 AH TCP], we can't make a
	 * notification to TCP layer.
	 */
	so_pr_ctlinput_direct(&inetsw[ip_protox[icp->icmp_ip.ip_p]],
	    cmd, (struct sockaddr *)&icmpsrc, &icp->icmp_ip);
}

static void
icmp_ctlinput_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;

	icmp_ctlinput(msg->m, msg->cmd, msg->hlen);
	icmp_ctlinput_done(msg->m);
}

static void
icmp_ctlinput_start(struct mbuf *m, struct lwkt_port *port,
    int cmd, int hlen, int proto)
{
	struct netmsg_ctlinput *msg;

	KASSERT(&curthread->td_msgport != port,
	    ("send icmp ctlinput to the current netisr"));

	msg = &m->m_hdr.mh_ctlmsg;
	netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
	    icmp_ctlinput_handler);
	msg->m = m;
	msg->cmd = cmd;
	msg->hlen = hlen;
	msg->proto = proto;

	lwkt_sendmsg(port, &msg->base.lmsg);
}

static void
icmp_ctlinput_global_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;
	int nextcpu;

	icmp_ctlinput(msg->m, msg->cmd, msg->hlen);

	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &msg->base.lmsg);
	else
		icmp_ctlinput_done(msg->m);
}

static void
icmp_ctlinput_global_start(struct mbuf *m, int cmd, int hlen, int proto)
{
	struct netmsg_ctlinput *msg;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));
	KASSERT(ncpus > 1, ("there is only 1 cpu"));

	icmp_ctlinput(m, cmd, hlen);

	msg = &m->m_hdr.mh_ctlmsg;
	netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
	    icmp_ctlinput_global_handler);
	msg->m = m;
	msg->cmd = cmd;
	msg->hlen = hlen;
	msg->proto = proto;

	lwkt_sendmsg(netisr_cpuport(1), &msg->base.lmsg);
}

#define ICMP_RTREDIRECT_FLAGS	(RTF_GATEWAY | RTF_HOST)

static void
icmp_redirect(struct mbuf *m, int hlen, boolean_t prt)
{
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct sockaddr_in icmpdst = { sizeof(struct sockaddr_in), AF_INET };
	struct sockaddr_in icmpgw = { sizeof(struct sockaddr_in), AF_INET };
	struct icmp *icp;
	struct ip *ip;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));

	ip = mtod(m, struct ip *);
	icp = mtodoff(m, struct icmp *, hlen);

	/*
	 * Short circuit routing redirects to force immediate change
	 * in the kernel's routing tables.  The message is also handed
	 * to anyone listening on a raw socket (e.g. the routing daemon
	 * for use in updating its tables).
	 */
#ifdef ICMPPRINTFS
	if (icmpprintfs && prt) {
		char dst_buf[INET_ADDRSTRLEN], gw_buf[INET_ADDRSTRLEN];

		kprintf("redirect dst %s to %s\n",
		    inet_ntop(AF_INET, &icp->icmp_ip.ip_dst,
			dst_buf, INET_ADDRSTRLEN),
		    inet_ntop(AF_INET, &icp->icmp_gwaddr,
			gw_buf, INET_ADDRSTRLEN));
	}
#endif
	icmpgw.sin_addr = ip->ip_src;
	icmpdst.sin_addr = icp->icmp_gwaddr;
	icmpsrc.sin_addr = icp->icmp_ip.ip_dst;
	rtredirect_oncpu((struct sockaddr *)&icmpsrc,
	    (struct sockaddr *)&icmpdst, NULL, ICMP_RTREDIRECT_FLAGS,
	    (struct sockaddr *)&icmpgw);
	kpfctlinput_direct(PRC_REDIRECT_HOST, (struct sockaddr *)&icmpsrc);
}

static void
icmp_redirect_done_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;
	struct mbuf *m = msg->m;
	int hlen = msg->hlen;
#ifdef IPSEC
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct icmp *icp = mtodoff(m, struct icmp *, hlen);;

	icmpsrc.sin_addr = icp->icmp_ip.ip_dst;
	key_sa_routechange((struct sockaddr *)&icmpsrc);
#endif
	rip_input(&m, &hlen, msg->proto);
}

static void
icmp_redirect_done(struct mbuf *m, int hlen, boolean_t dispatch_rip)
{
	struct rt_addrinfo rtinfo;
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct sockaddr_in icmpdst = { sizeof(struct sockaddr_in), AF_INET };
	struct sockaddr_in icmpgw = { sizeof(struct sockaddr_in), AF_INET };
	struct icmp *icp;
	struct ip *ip;

	ip = mtod(m, struct ip *);
	icp = mtodoff(m, struct icmp *, hlen);

	icmpgw.sin_addr = ip->ip_src;
	icmpdst.sin_addr = icp->icmp_gwaddr;
	icmpsrc.sin_addr = icp->icmp_ip.ip_dst;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_info[RTAX_DST] = (struct sockaddr *)&icmpsrc;
	rtinfo.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&icmpdst;
	rtinfo.rti_info[RTAX_NETMASK] = NULL;
	rtinfo.rti_info[RTAX_AUTHOR] = (struct sockaddr *)&icmpgw;
	rt_missmsg(RTM_REDIRECT, &rtinfo, ICMP_RTREDIRECT_FLAGS, 0);

	if (dispatch_rip) {
		struct netmsg_ctlinput *msg = &m->m_hdr.mh_ctlmsg;

		netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
		    icmp_redirect_done_handler);
		lwkt_sendmsg(netisr_cpuport(0), &msg->base.lmsg);
	}
}

static void
icmp_redirect_handler(netmsg_t nmsg)
{
	struct netmsg_ctlinput *msg = (struct netmsg_ctlinput *)nmsg;
	int nextcpu;

	icmp_redirect(msg->m, msg->hlen, FALSE);

	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &msg->base.lmsg);
	else
		icmp_redirect_done(msg->m, msg->hlen, TRUE);
}

static boolean_t
icmp_redirect_start(struct mbuf *m, int hlen, int proto)
{
	struct netmsg_ctlinput *msg;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));

	icmp_redirect(m, hlen, TRUE);

	if (ncpus == 1) {
		/* There is only one netisr; done */
		icmp_redirect_done(m, hlen, FALSE);
		return FALSE;
	}

	msg = &m->m_hdr.mh_ctlmsg;
	netmsg_init(&msg->base, NULL, &netisr_apanic_rport, 0,
	    icmp_redirect_handler);
	msg->m = m;
	msg->cmd = PRC_REDIRECT_HOST;
	msg->hlen = hlen;
	msg->proto = proto;

	lwkt_sendmsg(netisr_cpuport(1), &msg->base.lmsg);
	return TRUE;
}

/*
 * Process a received ICMP message.
 */
int
icmp_input(struct mbuf **mp, int *offp, int proto)
{
	struct sockaddr_in icmpsrc = { sizeof(struct sockaddr_in), AF_INET };
	struct sockaddr_in icmpdst = { sizeof(struct sockaddr_in), AF_INET };
	struct icmp *icp;
	struct in_ifaddr *ia;
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	int icmplen = ip->ip_len;
	int i, hlen;
	int code;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));

	*mp = NULL;
	hlen = *offp;

	/*
	 * Locate icmp structure in mbuf, and check
	 * that not corrupted and of at least minimum length.
	 */
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char src_buf[INET_ADDRSTRLEN], dst_buf[INET_ADDRSTRLEN];

		kprintf("icmp_input from %s to %s, len %d\n",
		    inet_ntop(AF_INET, &ip->ip_src, src_buf, INET_ADDRSTRLEN),
		    inet_ntop(AF_INET, &ip->ip_dst, dst_buf, INET_ADDRSTRLEN),
		    icmplen);
	}
#endif
	if (icmplen < ICMP_MINLEN) {
		icmpstat.icps_tooshort++;
		goto freeit;
	}
	i = hlen + min(icmplen, ICMP_ADVLENMIN);
	if (m->m_len < i && (m = m_pullup(m, i)) == NULL)  {
		icmpstat.icps_tooshort++;
		return(IPPROTO_DONE);
	}
	ip = mtod(m, struct ip *);

	if (in_cksum_skip(m, hlen + icmplen, hlen)) {
		icmpstat.icps_checksum++;
		goto freeit;
	}
	icp = (struct icmp *)((caddr_t)ip + hlen);

	if (m->m_pkthdr.rcvif && m->m_pkthdr.rcvif->if_type == IFT_FAITH) {
		/*
		 * Deliver very specific ICMP type only.
		 */
		switch (icp->icmp_type) {
		case ICMP_UNREACH:
		case ICMP_TIMXCEED:
			break;
		default:
			goto freeit;
		}
	}

#ifdef ICMPPRINTFS
	if (icmpprintfs)
		kprintf("icmp_input, type %d code %d\n", icp->icmp_type,
		    icp->icmp_code);
#endif

	/*
	 * Message type specific processing.
	 */
	if (icp->icmp_type > ICMP_MAXTYPE)
		goto raw;
	icmpstat.icps_inhist[icp->icmp_type]++;
	code = icp->icmp_code;
	switch (icp->icmp_type) {

	case ICMP_UNREACH:
		switch (code) {
			case ICMP_UNREACH_NET:
			case ICMP_UNREACH_HOST:
			case ICMP_UNREACH_SRCFAIL:
			case ICMP_UNREACH_NET_UNKNOWN:
			case ICMP_UNREACH_HOST_UNKNOWN:
			case ICMP_UNREACH_ISOLATED:
			case ICMP_UNREACH_TOSNET:
			case ICMP_UNREACH_TOSHOST:
			case ICMP_UNREACH_HOST_PRECEDENCE:
			case ICMP_UNREACH_PRECEDENCE_CUTOFF:
				code = PRC_UNREACH_NET;
				break;

			case ICMP_UNREACH_NEEDFRAG:
				code = PRC_MSGSIZE;
				break;

			/*
			 * RFC 1122, Sections 3.2.2.1 and 4.2.3.9.
			 * Treat subcodes 2,3 as immediate RST
			 */
			case ICMP_UNREACH_PROTOCOL:
			case ICMP_UNREACH_PORT:
				code = PRC_UNREACH_PORT;
				break;

			case ICMP_UNREACH_NET_PROHIB:
			case ICMP_UNREACH_HOST_PROHIB:
			case ICMP_UNREACH_FILTER_PROHIB:
				code = PRC_UNREACH_ADMIN_PROHIB;
				break;

			default:
				goto badcode;
		}
		goto deliver;

	case ICMP_TIMXCEED:
		if (code > 1)
			goto badcode;
		code += PRC_TIMXCEED_INTRANS;
		goto deliver;

	case ICMP_PARAMPROB:
		if (code > 1)
			goto badcode;
		code = PRC_PARAMPROB;
		goto deliver;

	case ICMP_SOURCEQUENCH:
		if (code)
			goto badcode;
		if (discard_sourcequench)
			break;
		code = PRC_QUENCH;
deliver:
		/*
		 * Problem with datagram; advise higher level routines.
		 */
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    IP_VHL_HL(icp->icmp_ip.ip_vhl) < (sizeof(struct ip) >> 2)) {
			icmpstat.icps_badlen++;
			goto freeit;
		}
		/* Discard ICMP's in response to multicast packets */
		if (IN_MULTICAST(ntohl(icp->icmp_ip.ip_dst.s_addr)))
			goto badcode;
#ifdef ICMPPRINTFS
		if (icmpprintfs)
			kprintf("deliver to protocol %d\n", icp->icmp_ip.ip_p);
#endif
		icmpsrc.sin_addr = icp->icmp_ip.ip_dst;

		/*
		 * MTU discovery
		 */
		if (code == PRC_MSGSIZE) {
			/* Run MTU discovery in all netisrs */
			if (icmp_mtudisc_start(m, hlen, proto)) {
				/* Forwarded; done */
				return IPPROTO_DONE;
			}
			/* Move on; run rip_input() directly */
		} else {
			struct protosw *pr;
			struct lwkt_port *port;
			int cpu;

			pr = &inetsw[ip_protox[icp->icmp_ip.ip_p]];
			port = so_pr_ctlport(pr, code,
			    (struct sockaddr *)&icmpsrc, &icp->icmp_ip, &cpu);
			if (port != NULL) {
				if (cpu == ncpus) {
					if (ncpus > 1) {
						/*
						 * Run pr_ctlinput in all
						 * netisrs
						 */
						icmp_ctlinput_global_start(m,
						    code, hlen, proto);
						return IPPROTO_DONE;
					}
					/*
					 * There is only one netisr; run
					 * pr_ctlinput directly.
					 */
				} else if (cpu != mycpuid) {
					/*
					 * Send to the target netisr to run
					 * pr_ctlinput.
					 */
					icmp_ctlinput_start(m, port,
					    code, hlen, proto);
					return IPPROTO_DONE;
				}

				/*
				 * The target netisr is this netisr.
				 *
				 * XXX if the packet contains [IPv4 AH TCP],
				 * we can't make a notification to TCP layer.
				 */
				so_pr_ctlinput_direct(pr, code,
				    (struct sockaddr *)&icmpsrc, &icp->icmp_ip);
			}
			/* Move on; run rip_input() directly */
		}
		break;
badcode:
		icmpstat.icps_badcode++;
		break;

	case ICMP_ECHO:
		if (!icmpbmcastecho
		    && (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			icmpstat.icps_bmcastecho++;
			break;
		}
		icp->icmp_type = ICMP_ECHOREPLY;
#ifdef ICMP_BANDLIM
		if (badport_bandlim(BANDLIM_ICMP_ECHO) < 0)
			goto freeit;
		else
#endif
			goto reflect;

	case ICMP_TSTAMP:
		if (!icmpbmcastecho
		    && (m->m_flags & (M_MCAST | M_BCAST)) != 0) {
			icmpstat.icps_bmcasttstamp++;
			break;
		}
		if (icmplen < ICMP_TSLEN) {
			icmpstat.icps_badlen++;
			break;
		}
		icp->icmp_type = ICMP_TSTAMPREPLY;
		icp->icmp_rtime = iptime();
		icp->icmp_ttime = icp->icmp_rtime;	/* bogus, do later! */
#ifdef ICMP_BANDLIM
		if (badport_bandlim(BANDLIM_ICMP_TSTAMP) < 0)
			goto freeit;
		else
#endif
			goto reflect;

	case ICMP_MASKREQ:
		if (icmpmaskrepl == 0)
			break;
		/*
		 * We are not able to respond with all ones broadcast
		 * unless we receive it over a point-to-point interface.
		 */
		if (icmplen < ICMP_MASKLEN)
			break;
		switch (ip->ip_dst.s_addr) {

		case INADDR_BROADCAST:
		case INADDR_ANY:
			icmpdst.sin_addr = ip->ip_src;
			break;

		default:
			icmpdst.sin_addr = ip->ip_dst;
		}
		ia = (struct in_ifaddr *)ifaof_ifpforaddr(
			    (struct sockaddr *)&icmpdst, m->m_pkthdr.rcvif);
		if (ia == NULL)
			break;
		if (ia->ia_ifp == 0)
			break;
		icp->icmp_type = ICMP_MASKREPLY;
		icp->icmp_mask = ia->ia_sockmask.sin_addr.s_addr;
		if (ip->ip_src.s_addr == 0) {
			if (ia->ia_ifp->if_flags & IFF_BROADCAST)
			    ip->ip_src = satosin(&ia->ia_broadaddr)->sin_addr;
			else if (ia->ia_ifp->if_flags & IFF_POINTOPOINT)
			    ip->ip_src = satosin(&ia->ia_dstaddr)->sin_addr;
		}
reflect:
		ip->ip_len += hlen;	/* since ip_input deducts this */
		icmpstat.icps_reflect++;
		icmpstat.icps_outhist[icp->icmp_type]++;
		icmp_reflect(m);
		return(IPPROTO_DONE);

	case ICMP_REDIRECT:
		if (log_redirect) {
			char src_buf[INET_ADDRSTRLEN];
			char dst_buf[INET_ADDRSTRLEN];
			char gwy_buf[INET_ADDRSTRLEN];

			kprintf("icmp redirect from %s: %s => %s\n",
			    inet_ntop(AF_INET, &ip->ip_src,
			        src_buf, INET_ADDRSTRLEN),
			    inet_ntop(AF_INET, &icp->icmp_ip.ip_dst,
			        dst_buf, INET_ADDRSTRLEN),
			    inet_ntop(AF_INET, &icp->icmp_gwaddr,
			        gwy_buf, INET_ADDRSTRLEN));
		}
		if (drop_redirect)
			break;
		if (code > 3)
			goto badcode;
		if (icmplen < ICMP_ADVLENMIN || icmplen < ICMP_ADVLEN(icp) ||
		    IP_VHL_HL(icp->icmp_ip.ip_vhl) < (sizeof(struct ip) >> 2)) {
			icmpstat.icps_badlen++;
			break;
		}
#ifdef ICMPPRINTFS
		if (icmpprintfs) {
			char dst_buf[INET_ADDRSTRLEN], gw_buf[INET_ADDRSTRLEN];

			kprintf("redirect dst %s to %s\n",
			    inet_ntop(AF_INET, &icp->icmp_ip.ip_dst,
			        dst_buf, INET_ADDRSTRLEN),
			    inet_ntop(AF_INET, &icp->icmp_gwaddr,
			        gw_buf, INET_ADDRSTRLEN));
		}
#endif
		icmpsrc.sin_addr = icp->icmp_ip.ip_dst;

		/* Run redirect in all netisrs */
		if (icmp_redirect_start(m, hlen, proto)) {
			/* Forwarded; done */
			return IPPROTO_DONE;
		}
		/* Move on; run rip_input() directly */
#ifdef IPSEC
		key_sa_routechange((struct sockaddr *)&icmpsrc);
#endif
		break;

	/*
	 * No kernel processing for the following;
	 * just fall through to send to raw listener.
	 */
	case ICMP_ECHOREPLY:
	case ICMP_ROUTERADVERT:
	case ICMP_ROUTERSOLICIT:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQREPLY:
	case ICMP_MASKREPLY:
	default:
		break;
	}

raw:
	*mp = m;
	rip_input(mp, offp, proto);
	return(IPPROTO_DONE);

freeit:
	m_freem(m);
	return(IPPROTO_DONE);
}

/*
 * Reflect the ip packet back to the source
 */
static void
icmp_reflect(struct mbuf *m)
{
	struct ip *ip = mtod(m, struct ip *);
	struct in_ifaddr *ia;
	struct in_ifaddr_container *iac;
	struct ifaddr_container *ifac;
	struct ifnet *ifp;
	struct in_addr t;
	struct mbuf *opts = NULL;
	int optlen = (IP_VHL_HL(ip->ip_vhl) << 2) - sizeof(struct ip);
	struct route *ro = NULL, rt;

	if (!in_canforward(ip->ip_src) &&
	    ((ntohl(ip->ip_src.s_addr) & IN_CLASSA_NET) !=
	     (IN_LOOPBACKNET << IN_CLASSA_NSHIFT))) {
		m_freem(m);	/* Bad return address */
		icmpstat.icps_badaddr++;
		goto done;	/* Ip_output() will check for broadcast */
	}
	t = ip->ip_dst;
	ip->ip_dst = ip->ip_src;

	ro = &rt;
	bzero(ro, sizeof *ro);

	/*
	 * If the incoming packet was addressed directly to us,
	 * use dst as the src for the reply.  Otherwise (broadcast
	 * or anonymous), use the address which corresponds
	 * to the incoming interface.
	 */
	ia = NULL;
	LIST_FOREACH(iac, INADDR_HASH(t.s_addr), ia_hash) {
		if (t.s_addr == IA_SIN(iac->ia)->sin_addr.s_addr) {
			ia = iac->ia;
			goto match;
		}
	}
	ifp = m->m_pkthdr.rcvif;
	if (ifp != NULL && (ifp->if_flags & IFF_BROADCAST)) {
		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			if (satosin(&ia->ia_broadaddr)->sin_addr.s_addr ==
			    t.s_addr)
				goto match;
		}
	}
	/*
	 * If the packet was transiting through us, use the address of
	 * the interface the packet came through in.  If that interface
	 * doesn't have a suitable IP address, the normal selection
	 * criteria apply.
	 */
	if (icmp_rfi && ifp != NULL) {
		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			goto match;
		}
	}
	/*
	 * If the incoming packet was not addressed directly to us, use
	 * designated interface for icmp replies specified by sysctl
	 * net.inet.icmp.reply_src (default not set). Otherwise continue
	 * with normal source selection.
	 */
	if (icmp_reply_src[0] != '\0' &&
	    (ifp = ifunit_netisr(icmp_reply_src))) {
		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			ia = ifatoia(ifa);
			goto match;
		}
	}
	/*
	 * If the packet was transiting through us, use the address of
	 * the interface that is the closest to the packet source.
	 * When we don't have a route back to the packet source, stop here
	 * and drop the packet.
	 */
	ia = ip_rtaddr(ip->ip_dst, ro);
	if (ia == NULL) {
		m_freem(m);
		icmpstat.icps_noroute++;
		goto done;
	}
match:
	t = IA_SIN(ia)->sin_addr;
	ip->ip_src = t;
	ip->ip_ttl = ip_defttl;

	if (optlen > 0) {
		u_char *cp;
		int opt, cnt;
		u_int len;

		/*
		 * Retrieve any source routing from the incoming packet;
		 * add on any record-route or timestamp options.
		 */
		cp = (u_char *) (ip + 1);
		if ((opts = ip_srcroute(m)) == NULL &&
		    (opts = m_gethdr(M_NOWAIT, MT_HEADER))) {
			opts->m_len = sizeof(struct in_addr);
			mtod(opts, struct in_addr *)->s_addr = 0;
		}
		if (opts) {
#ifdef ICMPPRINTFS
			if (icmpprintfs)
				kprintf("icmp_reflect optlen %d rt %d => ",
				       optlen, opts->m_len);
#endif
			for (cnt = optlen; cnt > 0; cnt -= len, cp += len) {
				opt = cp[IPOPT_OPTVAL];
				if (opt == IPOPT_EOL)
					break;
				if (opt == IPOPT_NOP)
					len = 1;
				else {
					if (cnt < IPOPT_OLEN + sizeof *cp)
						break;
					len = cp[IPOPT_OLEN];
					if (len < IPOPT_OLEN + sizeof *cp ||
					    len > cnt)
					break;
				}
				/*
				 * Should check for overflow, but it
				 * "can't happen".
				 */
				if (opt == IPOPT_RR || opt == IPOPT_TS ||
				    opt == IPOPT_SECURITY) {
					bcopy(cp,
					      mtod(opts, caddr_t) + opts->m_len,
					      len);
					opts->m_len += len;
				}
			}
			/* Terminate & pad, if necessary */
			cnt = opts->m_len % 4;
			if (cnt) {
				for (; cnt < 4; cnt++) {
					*(mtod(opts, caddr_t) + opts->m_len) =
					    IPOPT_EOL;
					opts->m_len++;
				}
			}
#ifdef ICMPPRINTFS
			if (icmpprintfs)
				kprintf("%d\n", opts->m_len);
#endif
		}
		/*
		 * Now strip out original options by copying rest of first
		 * mbuf's data back, and adjust the IP length.
		 */
		ip->ip_len -= optlen;
		ip->ip_vhl = IP_VHL_BORING;
		m->m_len -= optlen;
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len -= optlen;
		optlen += sizeof(struct ip);
		bcopy((caddr_t)ip + optlen, ip + 1,
		      m->m_len - sizeof(struct ip));
	}
	m->m_pkthdr.fw_flags &= FW_MBUF_GENERATED;
	m->m_flags &= ~(M_BCAST|M_MCAST);
	icmp_send(m, opts, ro);
done:
	if (opts)
		m_free(opts);
	if (ro && ro->ro_rt)
		RTFREE(ro->ro_rt);
}

/*
 * Send an icmp packet back to the ip level,
 * after supplying a checksum.
 */
static void
icmp_send(struct mbuf *m, struct mbuf *opts, struct route *rt)
{
	struct ip *ip = mtod(m, struct ip *);
	struct icmp *icp;
	int hlen;

	hlen = IP_VHL_HL(ip->ip_vhl) << 2;
	m->m_data += hlen;
	m->m_len -= hlen;
	icp = mtod(m, struct icmp *);
	icp->icmp_cksum = 0;
	icp->icmp_cksum = in_cksum(m, ip->ip_len - hlen);
	m->m_data -= hlen;
	m->m_len += hlen;
	m->m_pkthdr.rcvif = NULL;
#ifdef ICMPPRINTFS
	if (icmpprintfs) {
		char dst_buf[INET_ADDRSTRLEN], src_buf[INET_ADDRSTRLEN];

		kprintf("icmp_send dst %s src %s\n",
		    inet_ntop(AF_INET, &ip->ip_dst, dst_buf, INET_ADDRSTRLEN),
		    inet_ntop(AF_INET, &ip->ip_src, src_buf, INET_ADDRSTRLEN));
	}
#endif
	ip_output(m, opts, rt, 0, NULL, NULL);
}

n_time
iptime(void)
{
	struct timeval atv;
	u_long t;

	getmicrotime(&atv);
	t = (atv.tv_sec % (24*60*60)) * 1000 + atv.tv_usec / 1000;
	return (htonl(t));
}

#if 1
/*
 * Return the next larger or smaller MTU plateau (table from RFC 1191)
 * given current value MTU.  If DIR is less than zero, a larger plateau
 * is returned; otherwise, a smaller value is returned.
 */
int
ip_next_mtu(int mtu, int dir)
{
	static int mtutab[] = {
		65535, 32000, 17914, 8166, 4352, 2002, 1492, 1006, 508, 296,
		68, 0
	};
	int i;

	for (i = 0; i < (sizeof mtutab) / (sizeof mtutab[0]); i++) {
		if (mtu >= mtutab[i])
			break;
	}

	if (dir < 0) {
		if (i == 0) {
			return 0;
		} else {
			return mtutab[i - 1];
		}
	} else {
		if (mtutab[i] == 0) {
			return 0;
		} else if(mtu > mtutab[i]) {
			return mtutab[i];
		} else {
			return mtutab[i + 1];
		}
	}
}
#endif

#ifdef ICMP_BANDLIM
/*
 * badport_bandlim() - check for ICMP bandwidth limit
 *
 *	Return 0 if it is ok to send an ICMP error response, -1 if we have
 *	hit our bandwidth limit and it is not ok.
 *
 *	If icmplim is <= 0, the feature is disabled and 0 is returned.
 *
 *	For now we separate the TCP and UDP subsystems w/ different 'which'
 *	values.  We may eventually remove this separation (and simplify the
 *	code further).
 *
 *	Note that the printing of the error message is delayed so we can
 *	properly print the icmp error rate that the system was trying to do
 *	(i.e. 22000/100 pps, etc...).  This can cause long delays in printing
 *	the 'final' error, but it doesn't make sense to solve the printing
 *	delay with more complex code.
 */
int
badport_bandlim(int which)
{
	static int lticks[BANDLIM_MAX + 1];
	static int lpackets[BANDLIM_MAX + 1];
	int dticks;
	const char *bandlimittype[] = {
		"Limiting icmp unreach response",
		"Limiting icmp ping response",
		"Limiting icmp tstamp response",
		"Limiting closed port RST response",
		"Limiting open port RST response"
		};

	/*
	 * Return ok status if feature disabled or argument out of
	 * ranage.
	 */

	if (icmplim <= 0 || which > BANDLIM_MAX || which < 0)
		return(0);
	dticks = ticks - lticks[which];

	/*
	 * reset stats when cumulative dt exceeds one second.
	 */

	if ((unsigned int)dticks > hz) {
		if (lpackets[which] > icmplim && icmplim_output) {
			kprintf("%s from %d to %d packets per second\n",
				bandlimittype[which],
				lpackets[which],
				icmplim
			);
		}
		lticks[which] = ticks;
		lpackets[which] = 0;
	}

	/*
	 * bump packet count
	 */

	if (++lpackets[which] > icmplim) {
		return(-1);
	}
	return(0);
}
#endif
