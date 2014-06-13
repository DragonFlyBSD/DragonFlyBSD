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
 * $FreeBSD: src/sys/netinet/ip_divert.c,v 1.42.2.6 2003/01/23 21:06:45 sam Exp $
 */

#define	_IP_VHL

#include "opt_inet.h"
#include "opt_ipdivert.h"
#include "opt_ipsec.h"

#ifndef INET
#error "IPDIVERT requires INET."
#endif

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/socketvar.h>
#include <sys/socketvar2.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/in_cksum.h>
#include <sys/lock.h>
#include <sys/msgport.h>

#include <net/if.h>
#include <net/route.h>

#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_divert.h>

/*
 * Divert sockets
 */

/*
 * Allocate enough space to hold a full IP packet
 */
#define	DIVSNDQ		(65536 + 100)
#define	DIVRCVQ		(65536 + 100)

#define DIV_IS_OUTPUT(sin)	((sin) == NULL || (sin)->sin_addr.s_addr == 0)

#define DIV_OUTPUT	0x10000
#define DIV_INPUT	0x20000

/*
 * Divert sockets work in conjunction with ipfw, see the divert(4)
 * manpage for features.
 * Internally, packets selected by ipfw in ip_input() or ip_output(),
 * and never diverted before, are passed to the input queue of the
 * divert socket with a given 'divert_port' number (as specified in
 * the matching ipfw rule), and they are tagged with a 16 bit cookie
 * (representing the rule number of the matching ipfw rule), which
 * is passed to process reading from the socket.
 *
 * Packets written to the divert socket are again tagged with a cookie
 * (usually the same as above) and a destination address.
 * If the destination address is INADDR_ANY then the packet is
 * treated as outgoing and sent to ip_output(), otherwise it is
 * treated as incoming and sent to ip_input().
 * In both cases, the packet is tagged with the cookie.
 *
 * On reinjection, processing in ip_input() and ip_output()
 * will be exactly the same as for the original packet, except that
 * ipfw processing will start at the rule number after the one
 * written in the cookie (so, tagging a packet with a cookie of 0
 * will cause it to be effectively considered as a standard packet).
 */

/* Internal variables */
static struct inpcbinfo divcbinfo;
static struct inpcbportinfo divcbportinfo;

static u_long	div_sendspace = DIVSNDQ;	/* XXX sysctl ? */
static u_long	div_recvspace = DIVRCVQ;	/* XXX sysctl ? */

static struct mbuf *ip_divert(struct mbuf *, int, int);

static struct lwkt_token div_token = LWKT_TOKEN_INITIALIZER(div_token);

/*
 * Initialize divert connection block queue.
 */
void
div_init(void)
{
	in_pcbinfo_init(&divcbinfo);
	in_pcbportinfo_init(&divcbportinfo, 1, FALSE, 0);
	/*
	 * XXX We don't use the hash list for divert IP, but it's easier
	 * to allocate a one entry hash list than it is to check all
	 * over the place for hashbase == NULL.
	 */
	divcbinfo.hashbase = hashinit(1, M_PCB, &divcbinfo.hashmask);
	divcbinfo.portinfo = &divcbportinfo;
	divcbinfo.wildcardhashbase = hashinit(1, M_PCB,
					      &divcbinfo.wildcardhashmask);
	divcbinfo.ipi_size = sizeof(struct inpcb);
	ip_divert_p = ip_divert;
}

/*
 * IPPROTO_DIVERT is not a real IP protocol; don't allow any packets
 * with that protocol number to enter the system from the outside.
 */
int
div_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;

	ipstat.ips_noproto++;
	m_freem(m);
	return(IPPROTO_DONE);
}

/*
 * Divert a packet by passing it up to the divert socket at port 'port'.
 *
 * Setup generic address and protocol structures for div_input routine,
 * then pass them along with mbuf chain.
 */
static void
div_packet(struct mbuf *m, int incoming, int port)
{
	struct sockaddr_in divsrc = { sizeof divsrc, AF_INET };
	struct inpcb *inp;
	struct socket *sa;
	struct m_tag *mtag;
	struct divert_info *divinfo;
	u_int16_t nport;

	/* Locate the divert info */
	mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL);
	divinfo = m_tag_data(mtag);
	divsrc.sin_port = divinfo->skipto;

	/*
	 * Record receive interface address, if any.
	 * But only for incoming packets.
	 */
	divsrc.sin_addr.s_addr = 0;
	if (incoming) {
		struct ifaddr_container *ifac;

		/* Find IP address for receive interface */
		TAILQ_FOREACH(ifac, &m->m_pkthdr.rcvif->if_addrheads[mycpuid],
			      ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr == NULL)
				continue;
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			divsrc.sin_addr =
			    ((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
			break;
		}
	}
	/*
	 * Record the incoming interface name whenever we have one.
	 */
	if (m->m_pkthdr.rcvif) {
		/*
		 * Hide the actual interface name in there in the
		 * sin_zero array. XXX This needs to be moved to a
		 * different sockaddr type for divert, e.g.
		 * sockaddr_div with multiple fields like
		 * sockaddr_dl. Presently we have only 7 bytes
		 * but that will do for now as most interfaces
		 * are 4 or less + 2 or less bytes for unit.
		 * There is probably a faster way of doing this,
		 * possibly taking it from the sockaddr_dl on the iface.
		 * This solves the problem of a P2P link and a LAN interface
		 * having the same address, which can result in the wrong
		 * interface being assigned to the packet when fed back
		 * into the divert socket. Theoretically if the daemon saves
		 * and re-uses the sockaddr_in as suggested in the man pages,
		 * this iface name will come along for the ride.
		 * (see div_output for the other half of this.)
		 */
		ksnprintf(divsrc.sin_zero, sizeof divsrc.sin_zero, "%s",
			  m->m_pkthdr.rcvif->if_xname);
	}

	/* Put packet on socket queue, if any */
	sa = NULL;
	nport = htons((u_int16_t)port);

	/*
	 * XXX
	 * Following loop to locate the inpcb is MPSAFE since the inpcb
	 * insertion/removal happens on the same CPU (CPU0), however,
	 * saving/testing the socket pointer is not MPSAFE.  So we still
	 * need to hold BGL here.
	 */
	lwkt_gettoken(&div_token);
	LIST_FOREACH(inp, &divcbinfo.pcblisthead, inp_list) {
		if (inp->inp_flags & INP_PLACEMARKER)
			continue;
		if (inp->inp_lport == nport)
			sa = inp->inp_socket;
	}
	if (sa) {
		lwkt_gettoken(&sa->so_rcv.ssb_token);
		if (ssb_appendaddr(&sa->so_rcv, (struct sockaddr *)&divsrc, m, NULL) == 0)
			m_freem(m);
		else
			sorwakeup(sa);
		lwkt_reltoken(&sa->so_rcv.ssb_token);
	} else {
		m_freem(m);
		ipstat.ips_noproto++;
		ipstat.ips_delivered--;
	}
	lwkt_reltoken(&div_token);
}

static void
div_packet_handler(netmsg_t msg)
{
	struct mbuf *m;
	int port, incoming = 0;

	m = msg->packet.nm_packet;

	port = msg->lmsg.u.ms_result32 & 0xffff;
	if (msg->lmsg.u.ms_result32 & DIV_INPUT)
		incoming = 1;
	div_packet(m, incoming, port);
	/* no reply, msg embedded in mbuf */
}

static void
divert_packet(struct mbuf *m, int incoming)
{
	struct m_tag *mtag;
	struct divert_info *divinfo;
	int port;

	M_ASSERTPKTHDR(m);

	/* Assure header */
	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL)
		return;

	mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL);
	KASSERT(mtag != NULL, ("%s no divert tag!", __func__));
	divinfo = m_tag_data(mtag);

	port = divinfo->port;
	KASSERT(port != 0, ("%s: port=0", __func__));

	if (mycpuid != 0) {
		struct netmsg_packet *nmp;

		nmp = &m->m_hdr.mh_netmsg;
		netmsg_init(&nmp->base, NULL, &netisr_apanic_rport,
			    0, div_packet_handler);
		nmp->nm_packet = m;

		nmp->base.lmsg.u.ms_result32 = port; /* port is 16bits */
		if (incoming)
			nmp->base.lmsg.u.ms_result32 |= DIV_INPUT;
		else
			nmp->base.lmsg.u.ms_result32 |= DIV_OUTPUT;

		lwkt_sendmsg(netisr_cpuport(0), &nmp->base.lmsg);
	} else {
		div_packet(m, incoming, port);
	}
}

/*
 * Deliver packet back into the IP processing machinery.
 *
 * If no address specified, or address is 0.0.0.0, send to ip_output();
 * otherwise, send to ip_input() and mark as having been received on
 * the interface with that address.
 */
static int
div_output(struct socket *so, struct mbuf *m,
	struct sockaddr_in *sin, struct mbuf *control)
{
	int error = 0;
	struct m_tag *mtag;
	struct divert_info *divinfo;

	if (control)
		m_freem(control);		/* XXX */

	/*
	 * Prepare the tag for divert info. Note that a packet
	 * with a 0 tag in mh_data is effectively untagged,
	 * so we could optimize that case.
	 */
	mtag = m_tag_get(PACKET_TAG_IPFW_DIVERT, sizeof(*divinfo), MB_DONTWAIT);
	if (mtag == NULL) {
		error = ENOBUFS;
		goto cantsend;
	}
	m_tag_prepend(m, mtag);

	/* Loopback avoidance and state recovery */
	divinfo = m_tag_data(mtag);
	if (sin)
		divinfo->skipto = sin->sin_port;
	else
		divinfo->skipto = 0;

	/* Reinject packet into the system as incoming or outgoing */
	if (DIV_IS_OUTPUT(sin)) {
		struct ip *const ip = mtod(m, struct ip *);

		/* Don't allow packet length sizes that will crash */
		if ((u_short)ntohs(ip->ip_len) > m->m_pkthdr.len) {
			error = EINVAL;
			goto cantsend;
		}

		/* Convert fields to host order for ip_output() */
		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_off = ntohs(ip->ip_off);

		/* Send packet to output processing */
		ipstat.ips_rawout++;			/* XXX */
		error = ip_output(m, NULL, NULL,
			    (so->so_options & SO_DONTROUTE) |
			    IP_ALLOWBROADCAST | IP_RAWOUTPUT,
			    NULL, NULL);
	} else {
		ip_input(m);
	}
	return error;

cantsend:
	m_freem(m);
	return error;
}

static void
div_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	int proto = msg->attach.nm_proto;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct inpcb *inp;
	int error;

	inp  = so->so_pcb;
	if (inp)
		panic("div_attach");
	error = priv_check_cred(ai->p_ucred, PRIV_ROOT, NULL_CRED_OKAY);
	if (error)
		goto out;

	error = soreserve(so, div_sendspace, div_recvspace, ai->sb_rlimit);
	if (error)
		goto out;
	lwkt_gettoken(&div_token);
	sosetport(so, netisr_cpuport(0));
	error = in_pcballoc(so, &divcbinfo);
	if (error) {
		lwkt_reltoken(&div_token);
		goto out;
	}
	inp = (struct inpcb *)so->so_pcb;
	inp->inp_ip_p = proto;
	inp->inp_vflag |= INP_IPV4;
	inp->inp_flags |= INP_HDRINCL;
	/*
	 * The socket is always "connected" because
	 * we always know "where" to send the packet.
	 */
	sosetstate(so, SS_ISCONNECTED);
	lwkt_reltoken(&div_token);
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
div_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct inpcb *inp;

	inp = so->so_pcb;
	if (inp == NULL)
		panic("div_detach");
	in_pcbdetach(inp);
	lwkt_replymsg(&msg->detach.base.lmsg, 0);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
div_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;

	soisdisconnected(so);
	div_detach(msg);
	/* msg invalid now */
}

static void
div_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	int error;

	if (so->so_state & SS_ISCONNECTED) {
		soreference(so);
		div_abort(msg);
		/* msg invalid now */
		sofree(so);
		return;
	}
	error = ENOTCONN;
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
div_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	int error;

	/*
	 * in_pcbbind assumes that nam is a sockaddr_in
	 * and in_pcbbind requires a valid address. Since divert
	 * sockets don't we need to make sure the address is
	 * filled in properly.
	 * XXX -- divert should not be abusing in_pcbind
	 * and should probably have its own family.
	 */
	if (nam->sa_family != AF_INET) {
		error = EAFNOSUPPORT;
	} else {
		((struct sockaddr_in *)nam)->sin_addr.s_addr = INADDR_ANY;
		error = in_pcbbind(so->so_pcb, nam, msg->bind.nm_td);
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
div_shutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;

	socantsendmore(so);

	lwkt_replymsg(&msg->shutdown.base.lmsg, 0);
}

static void
div_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *nam = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	int error;

	/* Length check already done in ip_hashfn() */
	KASSERT(m->m_len >= sizeof(struct ip), ("IP header not in one mbuf"));

	/* Send packet */
	error = div_output(so, m, (struct sockaddr_in *)nam, control);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

SYSCTL_DECL(_net_inet_divert);
SYSCTL_PROC(_net_inet_divert, OID_AUTO, pcblist, CTLFLAG_RD, &divcbinfo, 0,
	    in_pcblist_global_cpu0, "S,xinpcb", "List of active divert sockets");

struct pr_usrreqs div_usrreqs = {
	.pru_abort = div_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = div_attach,
	.pru_bind = div_bind,
	.pru_connect = pr_generic_notsupp,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in_control_dispatch,
	.pru_detach = div_detach,
	.pru_disconnect = div_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = in_setpeeraddr_dispatch,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = div_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = div_shutdown,
	.pru_sockaddr = in_setsockaddr_dispatch,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

static struct mbuf *
ip_divert_out(struct mbuf *m, int tee)
{
	struct mbuf *clone = NULL;
	struct ip *ip = mtod(m, struct ip *);

	/* Clone packet if we're doing a 'tee' */
	if (tee)
		clone = m_dup(m, MB_DONTWAIT);

	/*
	 * XXX
	 * delayed checksums are not currently compatible
	 * with divert sockets.
	 */
	if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(m);
		m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}

	/* Restore packet header fields to original values */
	ip->ip_len = htons(ip->ip_len);
	ip->ip_off = htons(ip->ip_off);

	/* Deliver packet to divert input routine */
	divert_packet(m, 0);

	/* If 'tee', continue with original packet */
	return clone;
}

static struct mbuf *
ip_divert_in(struct mbuf *m, int tee)
{
	struct mbuf *clone = NULL;
	struct ip *ip = mtod(m, struct ip *);
	struct m_tag *mtag;

	if (ip->ip_off & (IP_MF | IP_OFFMASK)) {
		const struct divert_info *divinfo;
		u_short frag_off;
		int hlen;

		/*
		 * Only trust divert info in the fragment
		 * at offset 0.
		 */
		frag_off = ip->ip_off << 3;
		if (frag_off != 0) {
			mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL);
			m_tag_delete(m, mtag);
		}

		/*
		 * Attempt reassembly; if it succeeds, proceed.
		 * ip_reass() will return a different mbuf.
		 */
		m = ip_reass(m);
		if (m == NULL)
			return NULL;
		ip = mtod(m, struct ip *);

		/* Caller need to redispatch the packet, if it is for us */
		m->m_pkthdr.fw_flags |= FW_MBUF_REDISPATCH;

		/*
		 * Get the header length of the reassembled
		 * packet
		 */
		hlen = IP_VHL_HL(ip->ip_vhl) << 2;

		/*
		 * Restore original checksum before diverting
		 * packet
		 */
		ip->ip_len += hlen;
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);
		ip->ip_sum = 0;
		if (hlen == sizeof(struct ip))
			ip->ip_sum = in_cksum_hdr(ip);
		else
			ip->ip_sum = in_cksum(m, hlen);
		ip->ip_off = ntohs(ip->ip_off);
		ip->ip_len = ntohs(ip->ip_len);

		/*
		 * Only use the saved divert info
		 */
		mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL);
		if (mtag == NULL) {
			/* Wrongly configured ipfw */
			kprintf("ip_input no divert info\n");
			m_freem(m);
			return NULL;
		}
		divinfo = m_tag_data(mtag);
		tee = divinfo->tee;
	}

	/*
	 * Divert or tee packet to the divert protocol if
	 * required.
	 */

	/* Clone packet if we're doing a 'tee' */
	if (tee)
		clone = m_dup(m, MB_DONTWAIT);

	/*
	 * Restore packet header fields to original
	 * values
	 */
	ip->ip_len = htons(ip->ip_len);
	ip->ip_off = htons(ip->ip_off);

	/* Deliver packet to divert input routine */
	divert_packet(m, 1);

	/* Catch invalid reference */
	m = NULL;
	ip = NULL;

	ipstat.ips_delivered++;

	/* If 'tee', continue with original packet */
	if (clone != NULL) {
		/*
		 * Complete processing of the packet.
		 * XXX Better safe than sorry, remove the DIVERT tag.
		 */
		mtag = m_tag_find(clone, PACKET_TAG_IPFW_DIVERT, NULL);
		KKASSERT(mtag != NULL);
		m_tag_delete(clone, mtag);
	}
	return clone;
}

static struct mbuf *
ip_divert(struct mbuf *m, int tee, int incoming)
{
	struct mbuf *ret;

	if (incoming)
		ret = ip_divert_in(m, tee);
	else
		ret = ip_divert_out(m, tee);
	return ret;
}
