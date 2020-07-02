/*	$OpenBSD: if_pflog.c,v 1.27 2007/12/20 02:53:02 brad Exp $	*/
/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and 
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece, 
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by John Ioannidis, Angelos D. Keromytis
 * and Niels Provos.
 * Copyright (c) 2001, Angelos D. Keromytis, Niels Provos.
 *
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "use_bpf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/in_cksum.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sockio.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/route.h>
#include <net/bpf.h>

#ifdef	INET
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#endif

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/nd6.h>
#endif /* INET6 */

#include <net/pf/pfvar.h>
#include <net/pf/if_pflog.h>

#define	PFLOGNAME	"pflog"

#define PFLOGMTU	(32768 + MHLEN + MLEN)

#ifdef PFLOGDEBUG
#define DPRINTF(x)	do { if (pflogdebug) kprintf x ; } while (0)
#else
#define DPRINTF(x)
#endif

static void	pflogattach(void);
static int	pflogoutput(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);
static int	pflogioctl(struct ifnet *, u_long, caddr_t __unused,
		    struct ucred * __unused);
static void	pflogstart(struct ifnet *, struct ifaltq_subque *);
static int	pflog_clone_create(struct if_clone *, int, caddr_t, caddr_t);
static int	pflog_clone_destroy(struct ifnet *);

static struct if_clone pflog_cloner = IF_CLONE_INITIALIZER(
    PFLOGNAME, pflog_clone_create, pflog_clone_destroy, 1, 1);

static LIST_HEAD(, pflog_softc) pflogif_list;
static struct ifnet *pflogifs[PFLOGIFS_MAX]; /* for fast access */

static void
pflogattach(void)
{
	int	i;
	LIST_INIT(&pflogif_list);
	for (i = 0; i < PFLOGIFS_MAX; i++)
		pflogifs[i] = NULL;
	if_clone_attach(&pflog_cloner);
}

static int
pflog_clone_create(struct if_clone *ifc, int unit,
		   caddr_t params __unused, caddr_t data __unused)
{
	struct ifnet *ifp;
	struct pflog_softc *pflogif;

	lwkt_gettoken(&pf_token);

	if (unit >= PFLOGIFS_MAX) {
		lwkt_reltoken(&pf_token);
		return (EINVAL);
	}

	pflogif = kmalloc(sizeof(*pflogif), M_DEVBUF, M_WAITOK | M_ZERO);
	pflogif->sc_unit = unit;
	lwkt_reltoken(&pf_token);

	ifp = &pflogif->sc_if;
	if_initname(ifp, ifc->ifc_name, unit);
	ifp->if_softc = pflogif;
	ifp->if_mtu = PFLOGMTU;
	ifp->if_ioctl = pflogioctl;
	ifp->if_output = pflogoutput;
	ifp->if_start = pflogstart;
	ifp->if_type = IFT_PFLOG;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifp->if_hdrlen = PFLOG_HDRLEN;

	if_attach(ifp, NULL);
#if NBPF > 0
	bpfattach(&pflogif->sc_if, DLT_PFLOG, PFLOG_HDRLEN);
#endif

	lwkt_gettoken(&pf_token);
	crit_enter();
	LIST_INSERT_HEAD(&pflogif_list, pflogif, sc_list);
	pflogifs[unit] = ifp;
	crit_exit();
	lwkt_reltoken(&pf_token);

	return (0);
}

static int
pflog_clone_destroy(struct ifnet *ifp)
{
	struct pflog_softc	*pflogif = ifp->if_softc;

	lwkt_gettoken(&pf_token);

	crit_enter();
	pflogifs[pflogif->sc_unit] = NULL;
	LIST_REMOVE(pflogif, sc_list);
	crit_exit();

	lwkt_reltoken(&pf_token);
#if NBPF > 0
	bpfdetach(ifp);
#endif
	if_detach(ifp);
	lwkt_gettoken(&pf_token);
	kfree(pflogif, M_DEVBUF);
	lwkt_reltoken(&pf_token);

	return 0;
}

/*
 * Start output on the pflog interface.
 */
static void
pflogstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct mbuf *m;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	for (;;) {
		m = ifsq_dequeue(ifsq);
		if (m == NULL)
			return;
		else
			m_freem(m);
	}
}

static int
pflogoutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	    struct rtentry *rt)
{
	m_freem(m);
	return (0);
}

static int
pflogioctl(struct ifnet *ifp, u_long cmd, caddr_t data __unused,
	   struct ucred *cr __unused)
{

	lwkt_gettoken(&pf_token);

	switch (cmd) {
	case SIOCSIFFLAGS:
		lwkt_reltoken(&pf_token);
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		lwkt_gettoken(&pf_token);
		break;
	default:
		lwkt_reltoken(&pf_token);
		return (EINVAL);
	}

	lwkt_reltoken(&pf_token);
	return (0);
}

int
pflog_packet(struct pfi_kif *kif, struct mbuf *m, sa_family_t af, u_int8_t dir,
	     u_int8_t reason, struct pf_rule *rm, struct pf_rule *am,
	     struct pf_ruleset *ruleset, struct pf_pdesc *pd)
{
	struct ifnet *ifn = NULL;
	struct pfloghdr hdr;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	if (kif == NULL || m == NULL || rm == NULL)
		return (-1);

	if ((ifn = pflogifs[rm->logif]) == NULL || !ifn->if_bpf)
		return (0);

	bzero(&hdr, sizeof(hdr));
	hdr.length = PFLOG_REAL_HDRLEN;
	hdr.af = af;
	hdr.action = rm->action;
	hdr.reason = reason;
	memcpy(hdr.ifname, kif->pfik_name, sizeof(hdr.ifname));

	if (am == NULL) {
		hdr.rulenr = htonl(rm->nr);
		hdr.subrulenr = -1;
	} else {
		hdr.rulenr = htonl(am->nr);
		hdr.subrulenr = htonl(rm->nr);
		if (ruleset != NULL && ruleset->anchor != NULL)
			strlcpy(hdr.ruleset, ruleset->anchor->name,
			    sizeof(hdr.ruleset));
	}
	if (rm->log & PF_LOG_SOCKET_LOOKUP && !pd->lookup.done)
		pd->lookup.done = pf_socket_lookup(dir, pd);
	if (pd->lookup.done > 0) {
		hdr.uid = pd->lookup.uid;
		hdr.pid = pd->lookup.pid;
	} else {
		hdr.uid = UID_MAX;
		hdr.pid = NO_PID;
	}
	hdr.rule_uid = rm->cuid;
	hdr.rule_pid = rm->cpid;
	hdr.dir = dir;

#ifdef INET
	if (af == AF_INET) {
		struct ip *ip;
		ip = mtod(m, struct ip *);	
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);

		if (dir == PF_OUT) {
			ip->ip_sum = 0;
			ip->ip_sum = in_cksum(m, ip->ip_hl << 2);
		}				
	}
#endif /* INET */

	IFNET_STAT_INC(ifn, opackets, 1);
	IFNET_STAT_INC(ifn, obytes, m->m_pkthdr.len);
	if (ifn->if_bpf) {
		bpf_gettoken();
		if (ifn->if_bpf) {
			bpf_mtap_hdr(ifn->if_bpf, (char *)&hdr, PFLOG_HDRLEN, m,
			    BPF_DIRECTION_OUT);
		}
		bpf_reltoken();
	}

#ifdef INET
	if (af == AF_INET) {
		struct ip *ip = mtod(m, struct ip *);

		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_off = ntohs(ip->ip_off);
	}
#endif /* INET */

	return (0);
}

static int
pflog_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	struct pflog_softc *pflogif, *tmp;

	lwkt_gettoken(&pf_token);

	switch (type) {
	case MOD_LOAD:
		lwkt_reltoken(&pf_token);
		pflogattach();
		lwkt_gettoken(&pf_token);
		break;

	case MOD_UNLOAD:
		lwkt_reltoken(&pf_token);
		if_clone_detach(&pflog_cloner);
		LIST_FOREACH_MUTABLE(pflogif, &pflogif_list, sc_list, tmp)
			pflog_clone_destroy(&pflogif->sc_if);
		lwkt_gettoken(&pf_token);
		break;

	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(&pf_token);
	return error;
}

static moduledata_t pflog_mod = {
	"pflog",
	pflog_modevent,
	0
};

DECLARE_MODULE(pflog, pflog_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(pflog, PFLOG_MODVER);
/* Do not run before pf is initialized. */
MODULE_DEPEND(pflog, pf, PF_MODVER, PF_MODVER, PF_MODVER);
