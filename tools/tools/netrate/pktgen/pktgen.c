/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/tools/tools/netrate/pktgen/pktgen.c,v 1.4 2008/04/02 14:18:55 sephe Exp $
 */

#define _IP_VHL

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/in_cksum.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/serialize.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/udp_var.h>

#include "pktgen.h"

#define CDEV_NAME	"pktg"

#define PKTGEN_BUFSZ	2048

#ifndef PKTGEN_DEVCNT
#define PKTGEN_DEVCNT	4
#endif

struct pktgen;

struct netmsg_pktgen {
	struct netmsg_base	np_base;
	struct pktgen		*np_pktg;
	struct ifaltq_subque	*np_ifsq;
};

struct pktgen_buf {
	struct netmsg_base	pb_nmsg;	/* MUST BE THE FIRST */
	void			*pb_buf;
	volatile int		pb_done;
	int			pb_inuse;
	struct ifnet		*pb_ifp;
	struct ifaltq_subque	*pb_ifsq;
	int			pb_len;
	int			pb_cpuid;
	struct pktgen		*pb_pktg;
	LIST_ENTRY(pktgen_buf)	pb_link;
};

struct pktgen_pcpu {
	struct callout		pktg_stop;
	LIST_HEAD(, pktgen_buf)	pktg_buflist;
};

struct pktgen {
	uint32_t		pktg_flags;	/* PKTG_F_ */
	int			pktg_refcnt;

	int			pktg_duration;

	int			pktg_datalen;
	struct ifnet		*pktg_ifp;

	int			pktg_pktenq;

	struct sockaddr_in	pktg_src;
	int			pktg_ndst;
	struct sockaddr_in	*pktg_dst;
	uint8_t			pktg_dst_lladdr[ETHER_ADDR_LEN];

	struct pktgen_pcpu	pktg_pcpu[MAXCPU];
};

#define PKTG_F_CONFIG		0x1
#define PKTG_F_RUNNING		0x4
#define PKTG_F_SWITCH_SRCDST	0x8

static int 		pktgen_modevent(module_t, int, void *);

static void		pktgen_buf_free(void *);
static void		pktgen_buf_ref(void *);
static void		pktgen_buf_send(netmsg_t);

static int		pktgen_config(struct pktgen *,
			    const struct pktgen_conf *);
static int		pktgen_start(struct pktgen *, int);
static void		pktgen_free(struct pktgen *);
static void		pktgen_ref(struct pktgen *);
static void		pktgen_pcpu_stop_cb(void *);
static void		pktgen_mbuf(struct pktgen_buf *, struct mbuf *);
static void		pktgen_start_ifsq(struct pktgen *,
			    struct ifaltq_subque *);
static void		pktgen_start_ifsq_handler(netmsg_t);

static d_open_t		pktgen_open;
static d_close_t	pktgen_close;
static d_ioctl_t	pktgen_ioctl;

static struct dev_ops	pktgen_ops = {
	{ CDEV_NAME, 0, D_MPSAFE },
	.d_open =	pktgen_open,
	.d_close =	pktgen_close,
	.d_ioctl =	pktgen_ioctl,
};

static volatile int		pktgen_refcnt;
static struct lwkt_token pktgen_tok = LWKT_TOKEN_INITIALIZER(pktgen_token);

MALLOC_DECLARE(M_PKTGEN);
MALLOC_DEFINE(M_PKTGEN, CDEV_NAME, "Packet generator");

DEV_MODULE(pktgen, pktgen_modevent, NULL);

static int
pktgen_modevent(module_t mod, int type, void *data)
{
	int error = 0, i;

	switch (type) {
	case MOD_LOAD:
		for (i = 0; i < PKTGEN_DEVCNT; ++i) {
			make_dev(&pktgen_ops, 0, UID_ROOT, GID_WHEEL, 0600,
			    CDEV_NAME"%d", i);
		}
		break;

	case MOD_UNLOAD:
		if (pktgen_refcnt > 0)
			return EBUSY;
		dev_ops_remove_all(&pktgen_ops);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return error;
}

static int
pktgen_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct pktgen *pktg;
	int error, i;

	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
	if (error)
		return error;

	lwkt_gettoken(&pktgen_tok);

	if (dev->si_drv1 != NULL) {
		lwkt_reltoken(&pktgen_tok);
		return EBUSY;
	}

	pktg = kmalloc(sizeof(*pktg), M_PKTGEN, M_ZERO | M_WAITOK);
	for (i = 0; i < ncpus; ++i) {
		struct pktgen_pcpu *p = &pktg->pktg_pcpu[i];

		callout_init_mp(&p->pktg_stop);
		LIST_INIT(&p->pktg_buflist);
	}

	dev->si_drv1 = pktg;
	pktg->pktg_refcnt = 1;

	atomic_add_int(&pktgen_refcnt, 1);

	lwkt_reltoken(&pktgen_tok);
	return 0;
}

static int
pktgen_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct pktgen *pktg = dev->si_drv1;

	lwkt_gettoken(&pktgen_tok);
	dev->si_drv1 = NULL;
	lwkt_reltoken(&pktgen_tok);

	pktgen_free(pktg);

	return 0;
}

static int
pktgen_ioctl(struct dev_ioctl_args *ap __unused)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct pktgen *pktg = dev->si_drv1;
	int error;

	lwkt_gettoken(&pktgen_tok);

	switch (ap->a_cmd) {
	case PKTGENSTART:
		error = pktgen_start(pktg, 0);
		break;

	case PKTGENMQSTART:
		error = pktgen_start(pktg, 1);
		break;

	case PKTGENSCONF:
		error = pktgen_config(pktg, (const struct pktgen_conf *)data);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	lwkt_reltoken(&pktgen_tok);
	return error;
}

static int
pktgen_config(struct pktgen *pktg, const struct pktgen_conf *conf)
{
	const struct sockaddr_in *sin;
	struct sockaddr_in *dst = NULL;
	const struct sockaddr *sa;
	struct ifnet *ifp;
	size_t dst_size;
	int i, error, pktenq;

	if (pktg->pktg_flags & (PKTG_F_RUNNING | PKTG_F_CONFIG))
		return EBUSY;

	if (conf->pc_datalen <= 0 ||
	    conf->pc_datalen > ETHERMTU - sizeof(struct udpiphdr))
		return EINVAL;
	if (conf->pc_duration <= 0)
		return EINVAL;

	sin = &conf->pc_src;
	if (sin->sin_family != AF_INET)
		return EPROTONOSUPPORT;
	if (sin->sin_port == 0)
		return EINVAL;

	if (conf->pc_ndst <= 0)
		return EINVAL;
	dst_size = conf->pc_ndst * sizeof(struct sockaddr_in);

	dst = kmalloc(dst_size, M_PKTGEN, M_WAITOK | M_NULLOK);
	if (dst == NULL)
		return ENOMEM;

	error = copyin(conf->pc_dst, dst, dst_size);
	if (error)
		goto failed;

	for (i = 0; i < conf->pc_ndst; ++i) {
		sin = &dst[i];
		if (sin->sin_family != AF_INET) {
			error = EPROTONOSUPPORT;
			goto failed;
		}
		if (sin->sin_port == 0) {
			error = EINVAL;
			goto failed;
		}
	}

	ifp = ifunit(conf->pc_ifname);
	if (ifp == NULL) {
		error = ENXIO;
		goto failed;
	}

	pktenq = conf->pc_pktenq;
	if (pktenq < 0 || pktenq > ifp->if_snd.altq_maxlen) {
		error = ENOBUFS;
		goto failed;
	} else if (pktenq == 0) {
		pktenq = (ifp->if_snd.altq_maxlen * 3) / 4;
	}

	sa = &conf->pc_dst_lladdr;
	if (sa->sa_family != AF_LINK) {
		error = EPROTONOSUPPORT;
		goto failed;
	}
	if (sa->sa_len != ETHER_ADDR_LEN) {
		error = EPROTONOSUPPORT;
		goto failed;
	}
	if (ETHER_IS_MULTICAST(sa->sa_data) ||
	    bcmp(sa->sa_data, ifp->if_broadcastaddr, ifp->if_addrlen) == 0) {
		error = EADDRNOTAVAIL;
		goto failed;
	}

	/*
	 * Accept the config
	 */
	pktg->pktg_flags |= PKTG_F_CONFIG;

	if (conf->pc_flags & PKTGEN_FLAG_SWITCH_SRCDST)
		pktg->pktg_flags |= PKTG_F_SWITCH_SRCDST;
	pktg->pktg_duration = conf->pc_duration;
	pktg->pktg_datalen = conf->pc_datalen;
	pktg->pktg_pktenq = pktenq;
	pktg->pktg_ifp = ifp;
	pktg->pktg_src = conf->pc_src;
	pktg->pktg_ndst = conf->pc_ndst;
	KKASSERT(pktg->pktg_dst == NULL);
	pktg->pktg_dst = dst;
	bcopy(sa->sa_data, pktg->pktg_dst_lladdr, ETHER_ADDR_LEN);

	return 0;

failed:
	if (dst != NULL)
		kfree(dst, M_PKTGEN);
	return error;
}

static void
pktgen_start_ifsq(struct pktgen *pktg, struct ifaltq_subque *ifsq)
{
	struct netmsg_pktgen *np;

	np = kmalloc(sizeof(*np), M_LWKTMSG, M_WAITOK);
	netmsg_init(&np->np_base, NULL, &netisr_afree_rport, 0,
	    pktgen_start_ifsq_handler);
	np->np_pktg = pktg;
	np->np_ifsq = ifsq;

	lwkt_sendmsg(netisr_cpuport(ifsq_get_cpuid(ifsq)), &np->np_base.lmsg);
}

static int
pktgen_start(struct pktgen *pktg, int mq)
{
	struct ifaltq *ifq;

	if ((pktg->pktg_flags & PKTG_F_CONFIG) == 0)
		return EINVAL;
	if (pktg->pktg_flags & PKTG_F_RUNNING)
		return EBUSY;
	pktg->pktg_flags |= PKTG_F_RUNNING;

	ifq = &pktg->pktg_ifp->if_snd;
	if (!mq) {
		pktgen_ref(pktg);
		pktgen_start_ifsq(pktg, ifq_get_subq_default(ifq));
	} else {
		int i;

		for (i = 0; i < ifq->altq_subq_cnt; ++i)
			pktgen_ref(pktg);
		for (i = 0; i < ifq->altq_subq_cnt; ++i)
			pktgen_start_ifsq(pktg, ifq_get_subq(ifq, i));
	}
	return 0;
}

static void
pktgen_start_ifsq_handler(netmsg_t nmsg)
{
	struct netmsg_pktgen *np = (struct netmsg_pktgen *)nmsg;
	struct pktgen *pktg = np->np_pktg;
	struct ifaltq_subque *ifsq = np->np_ifsq;

	struct mbuf *m, *head = NULL, **next;
	struct ifnet *ifp;
	struct pktgen_pcpu *p;
	int cpuid, i, alloc_cnt, keep_cnt;

	u_short ulen, psum;
	int len, ip_len;

	/* Reply ASAP */
	lwkt_replymsg(&np->np_base.lmsg, 0);

	ifp = pktg->pktg_ifp;

	cpuid = ifsq_get_cpuid(ifsq);
	KKASSERT(cpuid == mycpuid);

	p = &pktg->pktg_pcpu[cpuid];

	keep_cnt = pktg->pktg_pktenq;
	alloc_cnt = keep_cnt * 2;

	/*
	 * Prefault enough mbuf into mbuf objcache
	 */
	next = &head;
	for (i = 0; i < alloc_cnt; ++i) {
		MGETHDR(m, MB_WAIT, MT_DATA);
		*next = m;
		next = &m->m_nextpkt;
	}

	for (i = 0; i < alloc_cnt - keep_cnt; ++i) {
		m = head;
		head = m->m_nextpkt;
		m->m_nextpkt = NULL;
		m_freem(m);
	}
	KKASSERT(head != NULL);

	/*
	 * Setup the packets' data
	 */
	ip_len = pktg->pktg_datalen + sizeof(struct udpiphdr);
	len = ip_len + ETHER_HDR_LEN;

	psum = htons((u_short)pktg->pktg_datalen + sizeof(struct udphdr) +
	    IPPROTO_UDP);
	ulen = htons(pktg->pktg_datalen + sizeof(struct udphdr));

	m = head;
	i = 0;
	while (m != NULL) {
		struct mbuf *nextm;
		const struct sockaddr_in *dst;
		struct pktgen_buf *pb;
		struct ip *ip;
		struct udpiphdr *ui;
		struct ether_header *eh;

		pktgen_ref(pktg);

		pb = kmalloc(sizeof(*pb), M_PKTGEN, M_WAITOK | M_ZERO);
		pb->pb_ifp = ifp;
		pb->pb_ifsq = ifsq;
		pb->pb_inuse = 1;
		pb->pb_buf = kmalloc(PKTGEN_BUFSZ, M_PKTGEN, M_WAITOK);
		pb->pb_len = len;
		pb->pb_cpuid = cpuid;
		pb->pb_pktg = pktg;
		netmsg_init(&pb->pb_nmsg, NULL, &netisr_adone_rport, 0,
		    pktgen_buf_send);
		LIST_INSERT_HEAD(&p->pktg_buflist, pb, pb_link);

		dst = &pktg->pktg_dst[i % pktg->pktg_ndst];
		++i;

		m->m_ext.ext_arg = pb;
		m->m_ext.ext_buf = pb->pb_buf;
		m->m_ext.ext_free = pktgen_buf_free;
		m->m_ext.ext_ref = pktgen_buf_ref;
		m->m_ext.ext_size = PKTGEN_BUFSZ;

		m->m_data = m->m_ext.ext_buf;
		m->m_flags |= M_EXT;
		m->m_len = m->m_pkthdr.len = len;

		m->m_data += ETHER_HDR_LEN;
		m->m_len -= ETHER_HDR_LEN;
		m->m_pkthdr.len -= ETHER_HDR_LEN;

		ui = mtod(m, struct udpiphdr *);
		ui->ui_pr = IPPROTO_UDP;
		if (pktg->pktg_flags & PKTG_F_SWITCH_SRCDST) {
			ui->ui_src.s_addr = dst->sin_addr.s_addr;
			ui->ui_dst.s_addr = pktg->pktg_src.sin_addr.s_addr;
			ui->ui_sport = dst->sin_port;
			ui->ui_dport = pktg->pktg_src.sin_port;
		} else {
			ui->ui_src.s_addr = pktg->pktg_src.sin_addr.s_addr;
			ui->ui_dst.s_addr = dst->sin_addr.s_addr;
			ui->ui_sport = pktg->pktg_src.sin_port;
			ui->ui_dport = dst->sin_port;
		}
		ui->ui_ulen = ulen;
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr, ui->ui_dst.s_addr,
		    psum);
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);

		ip = (struct ip *)ui;
		ip->ip_len = ip_len;
		ip->ip_ttl = 64;	/* XXX */
		ip->ip_tos = 0;		/* XXX */
		ip->ip_vhl = IP_VHL_BORING;
		ip->ip_off = 0;
		ip->ip_sum = 0;
		ip->ip_id = ip_newid();

		in_delayed_cksum(m);

		ip->ip_len = htons(ip->ip_len);
		ip->ip_sum = in_cksum_hdr(ip);

		m->m_data -= ETHER_HDR_LEN;
		m->m_len += ETHER_HDR_LEN;
		m->m_pkthdr.len += ETHER_HDR_LEN;

		eh = mtod(m, struct ether_header *);
		bcopy(pktg->pktg_dst_lladdr, eh->ether_dhost, ETHER_ADDR_LEN);
		bcopy(IF_LLADDR(ifp), eh->ether_shost, ETHER_ADDR_LEN);
		eh->ether_type = htons(ETHERTYPE_IP);

		nextm = m->m_nextpkt;
		m->m_nextpkt = NULL;

		ifq_dispatch(ifp, m, NULL);

		m = nextm;
	}

	callout_reset(&p->pktg_stop, pktg->pktg_duration * hz,
	    pktgen_pcpu_stop_cb, p);

	pktgen_free(pktg);
}

static void
pktgen_mbuf(struct pktgen_buf *pb, struct mbuf *m)
{
	m->m_ext.ext_arg = pb;
	m->m_ext.ext_buf = pb->pb_buf;
	m->m_ext.ext_free = pktgen_buf_free;
	m->m_ext.ext_ref = pktgen_buf_ref;
	m->m_ext.ext_size = PKTGEN_BUFSZ;

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT;
	m->m_len = m->m_pkthdr.len = pb->pb_len;
}

static void
pktgen_buf_send(netmsg_t msg)
{
	struct pktgen_buf *pb = (struct pktgen_buf *)msg;
	struct mbuf *m;

	KKASSERT(&curthread->td_msgport == netisr_cpuport(pb->pb_cpuid));

	crit_enter();
	lwkt_replymsg(&pb->pb_nmsg.lmsg, 0);
	crit_exit();

	MGETHDR(m, MB_WAIT, MT_DATA);
	pktgen_mbuf(pb, m);
	ifq_dispatch(pb->pb_ifp, m, NULL);
}

static void
pktgen_buf_free(void *arg)
{
	struct pktgen_buf *pb = arg;
	struct mbuf *m;

	KKASSERT(pb->pb_inuse > 0);
	if (pb->pb_done) {
		if (atomic_fetchadd_int(&pb->pb_inuse, -1) == 1) {
			struct pktgen *pktg;

			pktg = pb->pb_pktg;
			crit_enter();
			LIST_REMOVE(pb, pb_link);
			crit_exit();
			kfree(pb->pb_buf, M_PKTGEN);
			kfree(pb, M_PKTGEN);

			pktgen_free(pktg);
		}
		return;
	}

	if (&curthread->td_msgport != netisr_cpuport(pb->pb_cpuid)) {
		KKASSERT(pb->pb_cpuid == mycpuid);
		crit_enter();
		KKASSERT(pb->pb_nmsg.lmsg.ms_flags & MSGF_DONE);
		lwkt_sendmsg(netisr_cpuport(pb->pb_cpuid), &pb->pb_nmsg.lmsg);
		crit_exit();
		return;
	}

	MGETHDR(m, MB_WAIT, MT_DATA);
	pktgen_mbuf(pb, m);
	ifsq_enqueue(pb->pb_ifsq, m, NULL);
}

static void
pktgen_buf_ref(void *arg)
{
	struct pktgen_buf *pb = arg;

	panic("%s should never be called\n", __func__);

	KKASSERT(pb->pb_inuse > 0);
	atomic_add_int(&pb->pb_inuse, 1);
}

static void
pktgen_free(struct pktgen *pktg)
{
	KKASSERT(pktg->pktg_refcnt > 0);
	if (atomic_fetchadd_int(&pktg->pktg_refcnt, -1) == 1) {
		int i;

		if (pktg->pktg_dst != NULL)
			kfree(pktg->pktg_dst, M_PKTGEN);

		for (i = 0; i < ncpus; ++i)
			KKASSERT(LIST_EMPTY(&pktg->pktg_pcpu[i].pktg_buflist));
		kfree(pktg, M_PKTGEN);
	}

	KKASSERT(pktgen_refcnt > 0);
	atomic_subtract_int(&pktgen_refcnt, 1);
}

static void
pktgen_pcpu_stop_cb(void *arg)
{
	struct pktgen_pcpu *p = arg;
	struct pktgen_buf *pb;

	crit_enter();
	LIST_FOREACH(pb, &p->pktg_buflist, pb_link)
		pb->pb_done = 1;
	crit_exit();
}

static void
pktgen_ref(struct pktgen *pktg)
{
	atomic_add_int(&pktg->pktg_refcnt, 1);
	atomic_add_int(&pktgen_refcnt, 1);
}
