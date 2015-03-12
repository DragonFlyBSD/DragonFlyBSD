/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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


#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/msgport.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <net/ipfw3/ip_fw.h>
#include <net/dummynet3/ip_dummynet.h>

static void	ip_dn_ether_output(netmsg_t);
static void	ip_dn_ether_demux(netmsg_t);
static void	ip_dn_ip_input(netmsg_t);
static void	ip_dn_ip_output(netmsg_t);

static void	ip_dn_sockopt_dispatch(netmsg_t);
static void	ip_dn_freepkt_dispatch(netmsg_t);
static void	ip_dn_dispatch(netmsg_t);

static void	ip_dn_freepkt(struct dn_pkt *);

static int	ip_dn_sockopt_flush(struct sockopt *);
static int	ip_dn_sockopt_get(struct sockopt *);
static int	ip_dn_sockopt_config(struct sockopt *);

ip_dn_io_t	*ip_dn_io_ptr;
int		ip_dn_cpu = 0;

TUNABLE_INT("net.inet.ip.dummynet.cpu", &ip_dn_cpu);

SYSCTL_NODE(_net_inet_ip, OID_AUTO, dummynet, CTLFLAG_RW, 0, "Dummynet");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, cpu, CTLFLAG_RD,
	   &ip_dn_cpu, 0, "CPU to run dummynet");

void
ip_dn_queue(struct mbuf *m)
{
	struct netmsg_packet *nmp;
	lwkt_port_t port;

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	nmp = &m->m_hdr.mh_netmsg;
	netmsg_init(&nmp->base, NULL, &netisr_apanic_rport,
			0, ip_dn_dispatch);
	nmp->nm_packet = m;

	port = netisr_cpuport(ip_dn_cpu);
	lwkt_sendmsg(port, &nmp->base.lmsg);
}

void
ip_dn_packet_free(struct dn_pkt *pkt)
{
	struct netmsg_packet *nmp;
	struct mbuf *m = pkt->dn_m;

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	nmp = &m->m_hdr.mh_netmsg;
	netmsg_init(&nmp->base, NULL, &netisr_apanic_rport,
			0, ip_dn_freepkt_dispatch);
	nmp->nm_packet = m;

	lwkt_sendmsg(pkt->msgport, &nmp->base.lmsg);
}

void
ip_dn_packet_redispatch(struct dn_pkt *pkt)
{
	static const netisr_fn_t dispatches[DN_TO_MAX] = {
	[DN_TO_IP_OUT]		= ip_dn_ip_output,
	[DN_TO_IP_IN]		= ip_dn_ip_input,
	[DN_TO_ETH_DEMUX]	= ip_dn_ether_demux,
	[DN_TO_ETH_OUT]		= ip_dn_ether_output
	};

	struct netmsg_packet *nmp;
	struct mbuf *m;
	netisr_fn_t dispatch;
	int dir;

	dir = (pkt->dn_flags & DN_FLAGS_DIR_MASK);
	KASSERT(dir < DN_TO_MAX,
		("unknown dummynet redispatch dir %d", dir));

	dispatch = dispatches[dir];
	KASSERT(dispatch != NULL,
		("unsupported dummynet redispatch dir %d", dir));

	m = pkt->dn_m;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	nmp = &m->m_hdr.mh_netmsg;
	netmsg_init(&nmp->base, NULL, &netisr_apanic_rport, 0, dispatch);
	nmp->nm_packet = m;

	lwkt_sendmsg(pkt->msgport, &nmp->base.lmsg);
}

int
ip_dn_sockopt(struct sockopt *sopt)
{
	int error = 0;

	/* Disallow sets in really-really secure mode. */
	if (sopt->sopt_dir == SOPT_SET) {
		if (securelevel >= 3)
			return EPERM;
	}

	switch (sopt->sopt_name) {
	case IP_DUMMYNET_GET:
		error = ip_dn_sockopt_get(sopt);
		break;

	case IP_DUMMYNET_FLUSH:
		error = ip_dn_sockopt_flush(sopt);
		break;

	case IP_DUMMYNET_DEL:
	case IP_DUMMYNET_CONFIGURE:
		error = ip_dn_sockopt_config(sopt);
		break;

	default:
		kprintf("%s -- unknown option %d\n", __func__, sopt->sopt_name);
		error = EINVAL;
		break;
	}
	return error;
}

static void
ip_dn_freepkt(struct dn_pkt *pkt)
{
	struct rtentry *rt = pkt->ro.ro_rt;

	/* Unreference route entry */
	if (rt != NULL) {
		if (rt->rt_refcnt <= 0) {	/* XXX assert? */
			kprintf("-- warning, refcnt now %ld, decreasing\n",
				rt->rt_refcnt);
		}
		RTFREE(rt);
	}

	/* Unreference packet private data */
	if (pkt->dn_unref_priv)
		pkt->dn_unref_priv(pkt->dn_priv);

	/* Free the parent mbuf, this will free 'pkt' as well */
	m_freem(pkt->dn_m);
}

static void
ip_dn_freepkt_dispatch(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	KASSERT(pkt->cpuid == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "target cpuid %d, mycpuid %d", __func__,
		 pkt->cpuid, mycpuid));

	ip_dn_freepkt(pkt);
}

static void
ip_dn_dispatch(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;

	KASSERT(ip_dn_cpu == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "dummynet cpuid %d, mycpuid %d", __func__,
		 ip_dn_cpu, mycpuid));

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	if (DUMMYNET_LOADED) {
		if (ip_dn_io_ptr(m) == 0)
			return;
	}

	/*
	 * ip_dn_io_ptr() failed or dummynet(4) is not loaded
	 */
	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	ip_dn_packet_free(pkt);
}

static void
ip_dn_ip_output(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	struct rtentry *rt;
	ip_dn_unref_priv_t unref_priv;
	void *priv;

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	KASSERT(pkt->cpuid == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "target cpuid %d, mycpuid %d", __func__,
		 pkt->cpuid, mycpuid));
	KASSERT((pkt->dn_flags & DN_FLAGS_DIR_MASK) == DN_TO_IP_OUT,
		("wrong direction %d, should be %d",
		 (pkt->dn_flags & DN_FLAGS_DIR_MASK), DN_TO_IP_OUT));

	priv = pkt->dn_priv;
	unref_priv = pkt->dn_unref_priv;
	rt = pkt->ro.ro_rt;

	if (rt != NULL && !(rt->rt_flags & RTF_UP)) {
		/*
		 * Recorded rtentry is gone, when the packet
		 * was on delay line.
		 */
		ip_dn_freepkt(pkt);
		return;
	}

	ip_output(pkt->dn_m, NULL, NULL, pkt->flags, NULL, NULL);
	/* 'rt' will be freed in ip_output */

	if (unref_priv)
		unref_priv(priv);
}

static void
ip_dn_ip_input(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	ip_dn_unref_priv_t unref_priv;
	void *priv;

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	KASSERT(pkt->cpuid == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "target cpuid %d, mycpuid %d", __func__,
		 pkt->cpuid, mycpuid));
	KASSERT(pkt->ro.ro_rt == NULL,
		("route entry is not NULL for ip_input"));
	KASSERT((pkt->dn_flags & DN_FLAGS_DIR_MASK) == DN_TO_IP_IN,
		("wrong direction %d, should be %d",
		 (pkt->dn_flags & DN_FLAGS_DIR_MASK), DN_TO_IP_IN));

	priv = pkt->dn_priv;
	unref_priv = pkt->dn_unref_priv;

	ip_input(m);

	if (unref_priv)
		unref_priv(priv);
}

static void
ip_dn_ether_demux(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	ip_dn_unref_priv_t unref_priv;
	void *priv;

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	KASSERT(pkt->cpuid == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "target cpuid %d, mycpuid %d", __func__,
		 pkt->cpuid, mycpuid));
	KASSERT(pkt->ro.ro_rt == NULL,
		("route entry is not NULL for ether_demux"));
	KASSERT((pkt->dn_flags & DN_FLAGS_DIR_MASK) == DN_TO_ETH_DEMUX,
		("wrong direction %d, should be %d",
		 (pkt->dn_flags & DN_FLAGS_DIR_MASK), DN_TO_ETH_DEMUX));

	priv = pkt->dn_priv;
	unref_priv = pkt->dn_unref_priv;

	/*
	 * Make sure that ether header is contiguous
	 */
	if (m->m_len < ETHER_HDR_LEN &&
		(m = m_pullup(m, ETHER_HDR_LEN)) == NULL) {
		kprintf("%s: pullup fail, dropping pkt\n", __func__);
		goto back;
	}
	ether_demux_oncpu(m->m_pkthdr.rcvif, m);
back:
	if (unref_priv)
		unref_priv(priv);
}

static void
ip_dn_ether_output(netmsg_t nmsg)
{
	struct netmsg_packet *nmp;
	struct mbuf *m;
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	ip_dn_unref_priv_t unref_priv;
	void *priv;

	nmp = &nmsg->packet;
	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED,
		("mbuf is not tagged for dummynet!"));

	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	KKASSERT(mtag != NULL);

	pkt = m_tag_data(mtag);
	KASSERT(pkt->cpuid == mycpuid,
		("%s: dummynet packet was delivered to wrong cpu! "
		 "target cpuid %d, mycpuid %d", __func__,
		 pkt->cpuid, mycpuid));
	KASSERT(pkt->ro.ro_rt == NULL,
		("route entry is not NULL for ether_output_frame"));
	KASSERT((pkt->dn_flags & DN_FLAGS_DIR_MASK) == DN_TO_ETH_OUT,
		("wrong direction %d, should be %d",
		 (pkt->dn_flags & DN_FLAGS_DIR_MASK), DN_TO_ETH_OUT));

	priv = pkt->dn_priv;
	unref_priv = pkt->dn_unref_priv;

	ether_output_frame(pkt->ifp, m);

	if (unref_priv)
		unref_priv(priv);
}

static void
ip_dn_sockopt_dispatch(netmsg_t nmsg)
{
	lwkt_msg *msg = &nmsg->lmsg;
	struct dn_sopt *dn_sopt = msg->u.ms_resultp;
	int error;

	KASSERT(ip_dn_cpu == mycpuid,
		("%s: dummynet sockopt is done on wrong cpu! "
		 "dummynet cpuid %d, mycpuid %d", __func__,
		 ip_dn_cpu, mycpuid));

	if (DUMMYNET_LOADED)
		error = ip_dn_ctl_ptr(dn_sopt);
	else
		error = ENOPROTOOPT;
	lwkt_replymsg(msg, error);
}

static int
ip_dn_sockopt_flush(struct sockopt *sopt)
{
	struct dn_sopt dn_sopt;
	struct netmsg_base smsg;

	bzero(&dn_sopt, sizeof(dn_sopt));
	dn_sopt.dn_sopt_name = sopt->sopt_name;

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ip_dn_sockopt_dispatch);
	smsg.lmsg.u.ms_resultp = &dn_sopt;
	lwkt_domsg(netisr_cpuport(ip_dn_cpu), &smsg.lmsg, 0);

	return smsg.lmsg.ms_error;
}

static int
ip_dn_sockopt_get(struct sockopt *sopt)
{
	struct dn_sopt dn_sopt;
	struct netmsg_base smsg;
	int error;

	bzero(&dn_sopt, sizeof(dn_sopt));
	dn_sopt.dn_sopt_name = sopt->sopt_name;

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ip_dn_sockopt_dispatch);
	smsg.lmsg.u.ms_resultp = &dn_sopt;
	lwkt_domsg(netisr_cpuport(ip_dn_cpu), &smsg.lmsg, 0);

	error = smsg.lmsg.ms_error;
	if (error) {
		KKASSERT(dn_sopt.dn_sopt_arg == NULL);
		KKASSERT(dn_sopt.dn_sopt_arglen == 0);
		return error;
	}

	soopt_from_kbuf(sopt, dn_sopt.dn_sopt_arg, dn_sopt.dn_sopt_arglen);
	kfree(dn_sopt.dn_sopt_arg, M_TEMP);
	return 0;
}

static int
ip_dn_sockopt_config(struct sockopt *sopt)
{
	struct dn_ioc_pipe tmp_ioc_pipe;
	struct dn_sopt dn_sopt;
	struct netmsg_base smsg;
	int error;

	error = soopt_to_kbuf(sopt, &tmp_ioc_pipe, sizeof tmp_ioc_pipe,
				  sizeof tmp_ioc_pipe);
	if (error)
		return error;

	bzero(&dn_sopt, sizeof(dn_sopt));
	dn_sopt.dn_sopt_name = sopt->sopt_name;
	dn_sopt.dn_sopt_arg = &tmp_ioc_pipe;
	dn_sopt.dn_sopt_arglen = sizeof(tmp_ioc_pipe);

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ip_dn_sockopt_dispatch);
	smsg.lmsg.u.ms_resultp = &dn_sopt;
	lwkt_domsg(netisr_cpuport(ip_dn_cpu), &smsg.lmsg, 0);

	return smsg.lmsg.ms_error;
}
