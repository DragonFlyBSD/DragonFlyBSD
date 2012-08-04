/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/serialize.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp_var.h>

#include "pktgen.h"

#define CDEV_NAME	"pktg"
#define CDEV_MAJOR	151

struct pktgen {
	uint32_t		pktg_flags;	/* PKTG_F_ */
	int			pktg_refcnt;

	uint64_t		pktg_tx_cnt;
	uint64_t		pktg_err_cnt;
	struct timeval		pktg_start;
	struct timeval		pktg_end;

	struct callout		pktg_stop;
	int			pktg_duration;
	int			pktg_cpuid;
	void			(*pktg_thread)(void *);

	int			pktg_datalen;
	int			pktg_yield;
	struct ifnet		*pktg_ifp;

	in_addr_t		pktg_saddr;	/* host byte order */
	in_addr_t		pktg_daddr;	/* host byte order */
	u_short			pktg_sport;	/* host byte order */
	u_short			pktg_dport;	/* host byte order */

	int			pktg_nsaddr;
	int			pktg_ndaddr;
	int			pktg_nsport;
	int			pktg_ndport;

	uint8_t			pktg_dst_lladdr[ETHER_ADDR_LEN];
};

#define PKTG_F_CONFIG	0x1
#define PKTG_F_STOP	0x2
#define PKTG_F_RUNNING	0x4

static int 		pktgen_modevent(module_t, int, void *);
static int		pktgen_config(struct pktgen *,
				      const struct pktgen_conf *);
static int		pktgen_start(struct pktgen *, int);
static void		pktgen_thread_exit(struct pktgen *, uint64_t, uint64_t);
static void		pktgen_stop_cb(void *);
static void		pktgen_udp_thread(void *);
static void		pktgen_udp_thread1(void *);

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
	callout_init(&pktg->pktg_stop);

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

	KKASSERT(pktg->pktg_refcnt > 0);
	if (--pktg->pktg_refcnt == 0)
		kfree(pktg, M_PKTGEN);
	dev->si_drv1 = NULL;

	KKASSERT(pktgen_refcnt > 0);
	pktgen_refcnt--;

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
		error = pktgen_start(pktg, minor(dev));
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
	const struct sockaddr *sa;
	struct ifnet *ifp;
	int yield, nsaddr, ndaddr, nsport, ndport, thread1;

	if (pktg->pktg_flags & PKTG_F_RUNNING)
		return EBUSY;

	if (conf->pc_cpuid < 0 || conf->pc_cpuid >= ncpus)
		return EINVAL;
	if (conf->pc_datalen <= 0)
		return EINVAL;
	if (conf->pc_duration <= 0)
		return EINVAL;

	yield = conf->pc_yield;
	if (yield <= 0)
		yield = PKTGEN_YIELD_DEFAULT;

	if (conf->pc_nsaddr <= 0 && conf->pc_ndaddr <= 0 &&
	    conf->pc_nsport <= 0 && conf->pc_ndport <= 0)
		thread1 = 0;
	else
		thread1 = 1;

	nsaddr = conf->pc_nsaddr;
	if (nsaddr <= 0)
		nsaddr = 1;
	ndaddr = conf->pc_ndaddr;
	if (ndaddr <= 0)
		ndaddr = 1;

	nsport = conf->pc_nsport;
	if (nsport <= 0)
		nsport = 1;
	ndport = conf->pc_ndport;
	if (ndport <= 0)
		ndport = 1;

	ifp = ifunit(conf->pc_ifname);
	if (ifp == NULL)
		return ENXIO;

	sa = &conf->pc_dst_lladdr;
	if (sa->sa_family != AF_LINK)
		return EPROTONOSUPPORT;
	if (sa->sa_len != ETHER_ADDR_LEN)
		return EPROTONOSUPPORT;
	if (ETHER_IS_MULTICAST(sa->sa_data) ||
	    bcmp(sa->sa_data, ifp->if_broadcastaddr, ifp->if_addrlen) == 0)
		return EADDRNOTAVAIL;

	sin = &conf->pc_src;
	if (sin->sin_family != AF_INET)
		return EPROTONOSUPPORT;
	if (sin->sin_port == 0)
		return EINVAL;

	sin = &conf->pc_dst;
	if (sin->sin_family != AF_INET)
		return EPROTONOSUPPORT;
	if (sin->sin_port == 0)
		return EINVAL;

	/* Accept the config */
	pktg->pktg_flags |= PKTG_F_CONFIG;
	pktg->pktg_refcnt++;
	pktgen_refcnt++;

	pktg->pktg_duration = conf->pc_duration;
	pktg->pktg_cpuid = conf->pc_cpuid;
	pktg->pktg_ifp = ifp;
	pktg->pktg_datalen = conf->pc_datalen;
	pktg->pktg_yield = yield;
	bcopy(sa->sa_data, pktg->pktg_dst_lladdr, ETHER_ADDR_LEN);

	pktg->pktg_saddr = ntohl(conf->pc_src.sin_addr.s_addr);
	pktg->pktg_daddr = ntohl(conf->pc_dst.sin_addr.s_addr);
	pktg->pktg_nsaddr = nsaddr;
	pktg->pktg_ndaddr = ndaddr;

	pktg->pktg_sport = ntohs(conf->pc_src.sin_port);
	pktg->pktg_dport = ntohs(conf->pc_dst.sin_port);
	pktg->pktg_nsport = nsport;
	pktg->pktg_ndport = ndport;

	pktg->pktg_thread = thread1 ? pktgen_udp_thread1 : pktgen_udp_thread;

	return 0;
}

static int
pktgen_start(struct pktgen *pktg, int m)
{
	if ((pktg->pktg_flags & PKTG_F_CONFIG) == 0)
		return EINVAL;
	if (pktg->pktg_flags & PKTG_F_RUNNING)
		return EBUSY;

	pktg->pktg_flags |= PKTG_F_RUNNING;

	lwkt_create(pktg->pktg_thread, pktg, NULL, NULL, 0,
		    pktg->pktg_cpuid, "pktgen %d", m);
	return 0;
}

static void
pktgen_stop_cb(void *arg)
{
	struct pktgen *pktg = arg;

	pktg->pktg_flags |= PKTG_F_STOP;
}

static void
pktgen_udp_thread1(void *arg)
{
	struct pktgen *pktg = arg;
	struct ifnet *ifp = pktg->pktg_ifp;
	struct ip *ip;
	struct udpiphdr *ui;
	struct ether_header *eh;
	struct mbuf *m;
	u_short ulen, psum;
	int len, ip_len;
	int sw_csum, csum_flags;
	int loop, r, error;
	uint64_t err_cnt, cnt;
	in_addr_t saddr, daddr;
	u_short sport, dport;

	callout_reset(&pktg->pktg_stop, pktg->pktg_duration * hz,
		      pktgen_stop_cb, pktg);

	cnt = err_cnt = 0;
	r = loop = 0;

	ip_len = pktg->pktg_datalen + sizeof(*ui);
	len = ip_len + ETHER_HDR_LEN;

	psum = htons((u_short)pktg->pktg_datalen + sizeof(struct udphdr)
		     + IPPROTO_UDP);
	ulen = htons(pktg->pktg_datalen + sizeof(struct udphdr));

	sw_csum = (CSUM_UDP | CSUM_IP) & ~ifp->if_hwassist;
	csum_flags = (CSUM_UDP | CSUM_IP) & ifp->if_hwassist;

	saddr = pktg->pktg_saddr;
	daddr = pktg->pktg_daddr;
	sport = pktg->pktg_sport;
	dport = pktg->pktg_dport;

	microtime(&pktg->pktg_start);
	while ((pktg->pktg_flags & PKTG_F_STOP) == 0) {
		m = m_getl(len, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
		m->m_len = m->m_pkthdr.len = len;

		m_adj(m, ETHER_HDR_LEN);

		ui = mtod(m, struct udpiphdr *);
		ui->ui_pr = IPPROTO_UDP;
		ui->ui_src.s_addr = htonl(saddr);
		ui->ui_dst.s_addr = htonl(daddr);
		ui->ui_sport = htons(sport);
		ui->ui_dport = htons(dport);
		ui->ui_ulen = ulen;
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr,
				       ui->ui_dst.s_addr, psum);
		m->m_pkthdr.csum_flags = (CSUM_IP | CSUM_UDP);
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
		m->m_pkthdr.csum_iphlen = sizeof(struct ip);
		m->m_pkthdr.csum_thlen = sizeof(struct udphdr);
		m->m_pkthdr.csum_lhlen = sizeof(struct ether_header);

		ip = (struct ip *)ui;
		ip->ip_len = ip_len;
		ip->ip_ttl = 64;	/* XXX */
		ip->ip_tos = 0;		/* XXX */
		ip->ip_vhl = IP_VHL_BORING;
		ip->ip_off = 0;
		ip->ip_id = ip_newid();

		if (sw_csum & CSUM_DELAY_DATA)
			in_delayed_cksum(m);
		m->m_pkthdr.csum_flags = csum_flags;

		ip->ip_len = htons(ip->ip_len);
		ip->ip_sum = 0;
		if (sw_csum & CSUM_DELAY_IP)
			ip->ip_sum = in_cksum_hdr(ip);

		M_PREPEND(m, ETHER_HDR_LEN, MB_WAIT);
		eh = mtod(m, struct ether_header *);
		bcopy(pktg->pktg_dst_lladdr, eh->ether_dhost, ETHER_ADDR_LEN);
		bcopy(IF_LLADDR(ifp), eh->ether_shost, ETHER_ADDR_LEN);
		eh->ether_type = htons(ETHERTYPE_IP);

		ifnet_serialize_tx(ifp);
		error = ifq_handoff(ifp, m, NULL);
		ifnet_deserialize_tx(ifp);

		loop++;
		if (error) {
			err_cnt++;
			loop = 0;
			lwkt_yield();
		} else {
			cnt++;
			if (loop == pktg->pktg_yield) {
				loop = 0;
				lwkt_yield();
			}

			r++;
			saddr = pktg->pktg_saddr + (r % pktg->pktg_nsaddr);
			daddr = pktg->pktg_daddr + (r % pktg->pktg_ndaddr);
			sport = pktg->pktg_sport + (r % pktg->pktg_nsport);
			dport = pktg->pktg_dport + (r % pktg->pktg_ndport);
		}
	}
	microtime(&pktg->pktg_end);

	pktgen_thread_exit(pktg, cnt, err_cnt);
}

static void
pktgen_udp_thread(void *arg)
{
	struct pktgen *pktg = arg;
	struct ifnet *ifp = pktg->pktg_ifp;
	struct ip *ip;
	struct udpiphdr *ui;
	struct ether_header *eh;
	struct mbuf *m;
	u_short ulen, sum;
	int len, ip_len;
	int sw_csum, csum_flags;
	int loop, error;
	uint64_t err_cnt, cnt;
	in_addr_t saddr, daddr;
	u_short sport, dport;

	callout_reset(&pktg->pktg_stop, pktg->pktg_duration * hz,
		      pktgen_stop_cb, pktg);

	cnt = err_cnt = 0;
	loop = 0;

	ip_len = pktg->pktg_datalen + sizeof(*ui);
	len = ip_len + ETHER_HDR_LEN;

	saddr = htonl(pktg->pktg_saddr);
	daddr = htonl(pktg->pktg_daddr);
	sport = htons(pktg->pktg_sport);
	dport = htons(pktg->pktg_dport);

	sum = in_pseudo(saddr, daddr,
		htons((u_short)pktg->pktg_datalen + sizeof(struct udphdr)
		+ IPPROTO_UDP));
	ulen = htons(pktg->pktg_datalen + sizeof(struct udphdr));

	sw_csum = (CSUM_UDP | CSUM_IP) & ~ifp->if_hwassist;
	csum_flags = (CSUM_UDP | CSUM_IP) & ifp->if_hwassist;

	microtime(&pktg->pktg_start);
	while ((pktg->pktg_flags & PKTG_F_STOP) == 0) {
		m = m_getl(len, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
		m->m_len = m->m_pkthdr.len = len;

		m_adj(m, ETHER_HDR_LEN);

		ui = mtod(m, struct udpiphdr *);
		ui->ui_pr = IPPROTO_UDP;
		ui->ui_src.s_addr = saddr;
		ui->ui_dst.s_addr = daddr;
		ui->ui_sport = sport;
		ui->ui_dport = dport;
		ui->ui_ulen = ulen;
		ui->ui_sum = sum;
		m->m_pkthdr.csum_flags = (CSUM_IP | CSUM_UDP);
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);

		ip = (struct ip *)ui;
		ip->ip_len = ip_len;
		ip->ip_ttl = 64;	/* XXX */
		ip->ip_tos = 0;		/* XXX */
		ip->ip_vhl = IP_VHL_BORING;
		ip->ip_off = 0;
		ip->ip_id = ip_newid();

		if (sw_csum & CSUM_DELAY_DATA)
			in_delayed_cksum(m);
		m->m_pkthdr.csum_flags = csum_flags;

		ip->ip_len = htons(ip->ip_len);
		ip->ip_sum = 0;
		if (sw_csum & CSUM_DELAY_IP)
			ip->ip_sum = in_cksum_hdr(ip);

		M_PREPEND(m, ETHER_HDR_LEN, MB_WAIT);
		eh = mtod(m, struct ether_header *);
		bcopy(pktg->pktg_dst_lladdr, eh->ether_dhost, ETHER_ADDR_LEN);
		bcopy(IF_LLADDR(ifp), eh->ether_shost, ETHER_ADDR_LEN);
		eh->ether_type = htons(ETHERTYPE_IP);

		ifnet_serialize_tx(ifp);
		error = ifq_handoff(ifp, m, NULL);
		ifnet_deserialize_tx(ifp);

		loop++;
		if (error) {
			err_cnt++;
			loop = 0;
			lwkt_yield();
		} else {
			cnt++;
			if (loop == pktg->pktg_yield) {
				loop = 0;
				lwkt_yield();
			}
		}
	}
	microtime(&pktg->pktg_end);

	pktgen_thread_exit(pktg, cnt, err_cnt);
}

static void
pktgen_thread_exit(struct pktgen *pktg, uint64_t tx_cnt, uint64_t err_cnt)
{
	struct timeval end;

	pktg->pktg_tx_cnt = tx_cnt;
	pktg->pktg_err_cnt = err_cnt;

	end = pktg->pktg_end;
	timevalsub(&end, &pktg->pktg_start);
	kprintf("cnt %ju, err %ju, time %ld.%06ld\n",
		(uintmax_t)pktg->pktg_tx_cnt,
		(uintmax_t)pktg->pktg_err_cnt, end.tv_sec, end.tv_usec);

	pktg->pktg_flags &= ~(PKTG_F_STOP | PKTG_F_CONFIG | PKTG_F_RUNNING);

	KKASSERT(pktg->pktg_refcnt > 0);
	if (--pktg->pktg_refcnt == 0)
		kfree(pktg, M_PKTGEN);	/* XXX */

	KKASSERT(pktgen_refcnt > 0);
	pktgen_refcnt--;

	lwkt_exit();
}
