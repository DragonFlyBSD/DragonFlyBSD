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

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp_var.h>

#include "pktgen.h"

#define CDEV_NAME	"pktg"
#define CDEV_MAJOR	151

#define PKTGEN_BUFSZ	2048

struct pktgen;

struct pktgen_buf {
	struct netmsg_base	pb_nmsg;	/* MUST BE THE FIRST */
	void			*pb_buf;
	volatile int		pb_done;
	volatile int		pb_inuse;
	struct ifnet		*pb_ifp;
	int			pb_len;
	int			pb_cpuid;
	struct pktgen		*pb_pktg;
	LIST_ENTRY(pktgen_buf)	pb_link;
};

struct pktgen {
	uint32_t		pktg_flags;	/* PKTG_F_ */
	int			pktg_refcnt;

	struct callout		pktg_stop;
	int			pktg_duration;

	int			pktg_datalen;
	struct ifnet		*pktg_ifp;

	struct sockaddr_in	pktg_src;
	int			pktg_ndst;
	struct sockaddr_in	*pktg_dst;
	uint8_t			pktg_dst_lladdr[ETHER_ADDR_LEN];

	LIST_HEAD(, pktgen_buf)	pktg_buflist;
};

#define PKTG_F_CONFIG	0x1
#define PKTG_F_RUNNING	0x4

static int 		pktgen_modevent(module_t, int, void *);

static void		pktgen_buf_free(void *);
static void		pktgen_buf_ref(void *);
static void		pktgen_buf_send(netmsg_t);

static int		pktgen_config(struct pktgen *,
			    const struct pktgen_conf *);
static int		pktgen_start(struct pktgen *);
static void		pktgen_free(struct pktgen *);
static void		pktgen_stop_cb(void *);
static void		pktgen_mbuf(struct pktgen_buf *, struct mbuf *);

static d_open_t		pktgen_open;
static d_close_t	pktgen_close;
static d_ioctl_t	pktgen_ioctl;

static struct dev_ops	pktgen_ops = {
	{ CDEV_NAME, CDEV_MAJOR, 0 },
	.d_open =	pktgen_open,
	.d_close =	pktgen_close,
	.d_ioctl =	pktgen_ioctl,
};

static int		pktgen_refcnt;
static struct lwkt_token pktgen_tok = LWKT_TOKEN_INITIALIZER(pktgen_token);

MALLOC_DECLARE(M_PKTGEN);
MALLOC_DEFINE(M_PKTGEN, CDEV_NAME, "Packet generator");

DEV_MODULE(pktgen, pktgen_modevent, NULL);

static int
pktgen_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		make_dev(&pktgen_ops, 0, UID_ROOT, GID_WHEEL, 0600,
		    CDEV_NAME"%d", 0);
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
	int error;

	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
	if (error)
		return error;

	lwkt_gettoken(&pktgen_tok);

	if (dev->si_drv1 != NULL) {
		lwkt_reltoken(&pktgen_tok);
		return EBUSY;
	}

	pktg = kmalloc(sizeof(*pktg), M_PKTGEN, M_ZERO | M_WAITOK);
	callout_init_mp(&pktg->pktg_stop);
	LIST_INIT(&pktg->pktg_buflist);

	dev->si_drv1 = pktg;
	pktg->pktg_refcnt = 1;

	pktgen_refcnt++;

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
	pktgen_free(pktg);

	lwkt_reltoken(&pktgen_tok);

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
		error = pktgen_start(pktg);
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
	int i, error;

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

	pktg->pktg_duration = conf->pc_duration;
	pktg->pktg_datalen = conf->pc_datalen;
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

static int
pktgen_start(struct pktgen *pktg)
{
	struct mbuf *m, *head = NULL, **next;
	struct ifnet *ifp;
	int cpuid, orig_cpuid, i, alloc_cnt, keep_cnt;

	u_short ulen, psum;
	int len, ip_len;

	if ((pktg->pktg_flags & PKTG_F_CONFIG) == 0)
		return EINVAL;
	if (pktg->pktg_flags & PKTG_F_RUNNING)
		return EBUSY;
	pktg->pktg_flags |= PKTG_F_RUNNING;

	ifp = pktg->pktg_ifp;

	orig_cpuid = mycpuid;
	cpuid = ifp->if_start_cpuid(ifp);
	if (cpuid != orig_cpuid)
		lwkt_migratecpu(cpuid);

	alloc_cnt = ifp->if_snd.ifq_maxlen * 2;
	keep_cnt = (ifp->if_snd.ifq_maxlen * 7) / 8;

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

		pktg->pktg_refcnt++;
		pktgen_refcnt++;

		pb = kmalloc(sizeof(*pb), M_PKTGEN, M_WAITOK | M_ZERO);
		pb->pb_ifp = ifp;
		pb->pb_inuse = 1;
		pb->pb_buf = kmalloc(PKTGEN_BUFSZ, M_PKTGEN, M_WAITOK);
		pb->pb_len = len;
		pb->pb_cpuid = cpuid;
		pb->pb_pktg = pktg;
		netmsg_init(&pb->pb_nmsg, NULL, &netisr_adone_rport, 0,
		    pktgen_buf_send);
		LIST_INSERT_HEAD(&pktg->pktg_buflist, pb, pb_link);

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
		ui->ui_src.s_addr = pktg->pktg_src.sin_addr.s_addr;
		ui->ui_dst.s_addr = dst->sin_addr.s_addr;
		ui->ui_sport = pktg->pktg_src.sin_port;
		ui->ui_dport = dst->sin_port;
		ui->ui_ulen = ulen;
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr, ui->ui_dst.s_addr,
		    psum);

		ip = (struct ip *)ui;
		ip->ip_len = ip_len;
		ip->ip_ttl = 64;	/* XXX */
		ip->ip_tos = 0;		/* XXX */
		ip->ip_vhl = IP_VHL_BORING;
		ip->ip_off = 0;
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

	callout_reset(&pktg->pktg_stop, pktg->pktg_duration * hz,
	    pktgen_stop_cb, pktg);

	if (cpuid != orig_cpuid)
		lwkt_migratecpu(orig_cpuid);

	return 0;
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

	KKASSERT(&curthread->td_msgport == netisr_portfn(pb->pb_cpuid));

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

			lwkt_gettoken(&pktgen_tok);

			pktg = pb->pb_pktg;
			LIST_REMOVE(pb, pb_link);
			kfree(pb->pb_buf, M_PKTGEN);
			kfree(pb, M_PKTGEN);

			pktgen_free(pktg);

			lwkt_reltoken(&pktgen_tok);
		}
		return;
	}

	if (&curthread->td_msgport != netisr_portfn(pb->pb_cpuid)) {
		KKASSERT(pb->pb_cpuid == mycpuid);
		crit_enter();
		KKASSERT(pb->pb_nmsg.lmsg.ms_flags & MSGF_DONE);
		lwkt_sendmsg(netisr_portfn(pb->pb_cpuid), &pb->pb_nmsg.lmsg);
		crit_exit();
		return;
	}

	MGETHDR(m, MB_WAIT, MT_DATA);
	pktgen_mbuf(pb, m);
	ifq_enqueue(&pb->pb_ifp->if_snd, m, NULL);
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
	if (--pktg->pktg_refcnt == 0) {
		if (pktg->pktg_dst != NULL)
			kfree(pktg->pktg_dst, M_PKTGEN);
		KKASSERT(LIST_EMPTY(&pktg->pktg_buflist));
		kfree(pktg, M_PKTGEN);
	}

	KKASSERT(pktgen_refcnt > 0);
	pktgen_refcnt--;
}

static void
pktgen_stop_cb(void *arg)
{
	struct pktgen *pktg = arg;
	struct pktgen_buf *pb;

	lwkt_gettoken(&pktgen_tok);
	LIST_FOREACH(pb, &pktg->pktg_buflist, pb_link)
		pb->pb_done = 1;
	lwkt_reltoken(&pktgen_tok);
}
