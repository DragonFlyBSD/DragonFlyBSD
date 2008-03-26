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
 * $DragonFly: src/tools/tools/netrate/pktgen/pktgen.c,v 1.1 2008/03/26 13:53:14 sephe Exp $
 */

#define _IP_VHL

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioccom.h>
#include <sys/in_cksum.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
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

	struct callout		pktg_stop;
	int			pktg_duration;
	int			pktg_cpuid;

	int			pktg_datalen;
	int			pktg_yield;
	struct ifnet		*pktg_ifp;
	struct sockaddr_in	pktg_src;
	struct sockaddr_in	pktg_dst;
	uint8_t			pktg_dst_lladdr[ETHER_ADDR_LEN];
};

#define PKTG_F_CONFIG	0x1
#define PKTG_F_STOP	0x2
#define PKTG_F_RUNNING	0x4

static int 		pktgen_modevent(module_t, int, void *);
static int		pktgen_config(struct pktgen *,
				      const struct pktgen_conf *);
static int		pktgen_start(struct pktgen *, int);
static void		pktgen_stop_cb(void *);
static void		pktgen_thread(void *);

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

MALLOC_DECLARE(M_PKTGEN);
MALLOC_DEFINE(M_PKTGEN, CDEV_NAME, "Packet generator");

DEV_MODULE(pktgen, pktgen_modevent, NULL);

static int
pktgen_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		dev_ops_add(&pktgen_ops, 0, 0);
		break;

	case MOD_UNLOAD:
		if (pktgen_refcnt > 0)
			return EBUSY;
		dev_ops_remove(&pktgen_ops, 0, 0);
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

	error = suser_cred(ap->a_cred, 0);
	if (error)
		return error;

	get_mplock();

	if (dev->si_drv1 != NULL) {
		rel_mplock();
		return EBUSY;
	}

	pktg = kmalloc(sizeof(*pktg), M_PKTGEN, M_ZERO | M_WAITOK);
	callout_init(&pktg->pktg_stop);

	dev = make_dev(&pktgen_ops, minor(dev), UID_ROOT, GID_WHEEL, 0600,
		       CDEV_NAME "%d", lminor(dev));
	dev->si_drv1 = pktg;
	pktg->pktg_refcnt = 1;

	pktgen_refcnt++;

	rel_mplock();
	return 0;
}

static int
pktgen_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct pktgen *pktg = dev->si_drv1;

	get_mplock();

	KKASSERT(pktg->pktg_refcnt > 0);
	if (--pktg->pktg_refcnt == 0)
		kfree(pktg, M_PKTGEN);
	dev->si_drv1 = NULL;

	KKASSERT(pktgen_refcnt > 0);
	pktgen_refcnt--;

	rel_mplock();
	return 0;
}

static int
pktgen_ioctl(struct dev_ioctl_args *ap __unused)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct pktgen *pktg = dev->si_drv1;
	int error;

	get_mplock();

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

	rel_mplock();
	return error;
}

static int
pktgen_config(struct pktgen *pktg, const struct pktgen_conf *conf)
{
	const struct sockaddr_in *sin;
	const struct sockaddr *sa;
	struct ifnet *ifp;
	int yield;

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
	bcopy(&conf->pc_src, &pktg->pktg_src, sizeof(pktg->pktg_src));
	bcopy(&conf->pc_dst, &pktg->pktg_dst, sizeof(pktg->pktg_dst));

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

	lwkt_create(pktgen_thread, pktg, NULL, NULL, 0,
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
pktgen_thread(void *arg)
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
	int loop, error;
	uint64_t err_cnt, cnt;
	struct timeval start, end;

	rel_mplock();	/* Don't need MP lock */

	callout_reset(&pktg->pktg_stop, pktg->pktg_duration * hz,
		      pktgen_stop_cb, pktg);

	cnt = err_cnt = 0;
	loop = 0;

	ip_len = pktg->pktg_datalen + sizeof(*ui);
	len = ip_len + ETHER_HDR_LEN;

	psum = htons((u_short)pktg->pktg_datalen + sizeof(struct udphdr)
		     + IPPROTO_UDP);
	ulen = htons(pktg->pktg_datalen + sizeof(struct udphdr));

	sw_csum = (CSUM_UDP | CSUM_IP) & ~ifp->if_hwassist;
	csum_flags = (CSUM_UDP | CSUM_IP) & ifp->if_hwassist;

	microtime(&start);
	while ((pktg->pktg_flags & PKTG_F_STOP) == 0) {
		m = m_getl(len, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
		m->m_len = m->m_pkthdr.len = len;

		m_adj(m, ETHER_HDR_LEN);

		ui = mtod(m, struct udpiphdr *);
		ui->ui_pr = IPPROTO_UDP;
		ui->ui_dst = pktg->pktg_dst.sin_addr;
		ui->ui_dport = pktg->pktg_dst.sin_port;
		ui->ui_src = pktg->pktg_src.sin_addr;
		ui->ui_sport = pktg->pktg_src.sin_port;
		ui->ui_ulen = ulen;
		ui->ui_sum = in_pseudo(ui->ui_src.s_addr,
				       ui->ui_dst.s_addr, psum);
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

		lwkt_serialize_enter(ifp->if_serializer);
		error = ifq_handoff(ifp, m, NULL);
		lwkt_serialize_exit(ifp->if_serializer);

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
	microtime(&end);

	timevalsub(&end, &start);
	kprintf("cnt %llu, err %llu, time %ld.%ld\n", cnt, err_cnt,
		end.tv_sec, end.tv_usec);

	pktg->pktg_flags &= ~(PKTG_F_STOP | PKTG_F_CONFIG | PKTG_F_RUNNING);

	KKASSERT(pktg->pktg_refcnt > 0);
	if (--pktg->pktg_refcnt == 0)
		kfree(pktg, M_PKTGEN);	/* XXX */

	KKASSERT(pktgen_refcnt > 0);
	pktgen_refcnt--;

	lwkt_exit();
}
