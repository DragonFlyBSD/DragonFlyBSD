/*	$FreeBSD: src/sys/contrib/pf/net/if_pflog.c,v 1.9 2004/06/22 20:13:24 brooks Exp $	*/
/*	$OpenBSD: if_pflog.c,v 1.11 2003/12/31 11:18:25 cedric Exp $	*/
/*	$DragonFly: src/sys/net/pf/if_pflog.c,v 1.1 2004/09/19 22:32:47 joerg Exp $ */

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
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/in_cksum.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sockio.h>
#include <vm/vm_zone.h>

#include <net/if.h>
#include <net/if_types.h>
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
#define DPRINTF(x)    do { if (pflogdebug) printf x ; } while (0)
#else
#define DPRINTF(x)
#endif


static void	pflog_clone_destroy(struct ifnet *);
static int	pflog_clone_create(struct if_clone *, int);
int	pflogoutput(struct ifnet *, struct mbuf *, struct sockaddr *,
	    	       struct rtentry *);
int	pflogioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
void	pflogrtrequest(int, struct rtentry *, struct sockaddr *);
void	pflogstart(struct ifnet *);

static MALLOC_DEFINE(M_PFLOG, PFLOGNAME, "Packet Filter Logging Interface");
static LIST_HEAD(pflog_list, pflog_softc) pflog_list;
struct if_clone pflog_cloner = IF_CLONE_INITIALIZER("pflog", pflog_clone_create,
	    pflog_clone_destroy, 1, 1);

static void
pflog_clone_destroy(struct ifnet *ifp)
{
	struct pflog_softc *sc;

	sc = ifp->if_softc;

	/*
	 * Do we really need this?
	 */
	IF_DRAIN(&ifp->if_snd);

	bpfdetach(ifp);
	if_detach(ifp);
	LIST_REMOVE(sc, sc_next);
	free(sc, M_PFLOG);
}

static int
pflog_clone_create(struct if_clone *ifc, int unit)
{
	struct pflog_softc *sc;

	MALLOC(sc, struct pflog_softc *, sizeof(*sc), M_PFLOG, M_WAITOK|M_ZERO);

	if_initname(&sc->sc_if, ifc->ifc_name, unit);
        sc->sc_if.if_mtu = PFLOGMTU;
        sc->sc_if.if_ioctl = pflogioctl;
        sc->sc_if.if_output = pflogoutput;
        sc->sc_if.if_start = pflogstart;
        sc->sc_if.if_type = IFT_PFLOG;
        sc->sc_if.if_snd.ifq_maxlen = ifqmaxlen;
        sc->sc_if.if_hdrlen = PFLOG_HDRLEN;
        sc->sc_if.if_softc = sc;
        if_attach(&sc->sc_if);

        LIST_INSERT_HEAD(&pflog_list, sc, sc_next);
	bpfattach(&sc->sc_if, DLT_PFLOG, PFLOG_HDRLEN);

        return (0);
}

/*
 * Start output on the pflog interface.
 */
void
pflogstart(struct ifnet *ifp)
{
	int s;

	s = splimp();
	IF_DROP(&ifp->if_snd);
	IF_DRAIN(&ifp->if_snd);
	splx(s);
}

int
pflogoutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt)
{
	m_freem(m);
	return (0);
}

/* ARGSUSED */
void
pflogrtrequest(int cmd, struct rtentry *rt, struct sockaddr *sa)
{
	if (rt)
		rt->rt_rmx.rmx_mtu = PFLOGMTU;
}

/* ARGSUSED */
int
pflogioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	switch (cmd) {
	case SIOCSIFADDR:
	case SIOCAIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

int
pflog_packet(struct pfi_kif *kif, struct mbuf *m, sa_family_t af, u_int8_t dir,
    u_int8_t reason, struct pf_rule *rm, struct pf_rule *am,
    struct pf_ruleset *ruleset)
{
	struct ifnet *ifn;
	struct pfloghdr hdr;
	struct mbuf m1;

	if (kif == NULL || m == NULL || rm == NULL)
		return (-1);

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
		if (ruleset != NULL)
			memcpy(hdr.ruleset, ruleset->name,
			    sizeof(hdr.ruleset));

			
	}
	hdr.dir = dir;

#ifdef INET
	if (af == AF_INET && dir == PF_OUT) {
		struct ip *ip;

		ip = mtod(m, struct ip *);
		ip->ip_sum = 0;
		ip->ip_sum = in_cksum(m, ip->ip_hl << 2);
	}
#endif /* INET */

	m1.m_next = m;
	m1.m_len = PFLOG_HDRLEN;
	m1.m_data = (char *) &hdr;

	KASSERT((!LIST_EMPTY(&pflog_list)), ("pflog: no interface"));
	ifn = &LIST_FIRST(&pflog_list)->sc_if;

	BPF_MTAP(ifn, &m1);

	return (0);
}

static int
pflog_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&pflog_list);
		if_clone_attach(&pflog_cloner);
		break;

	case MOD_UNLOAD:
		if_clone_detach(&pflog_cloner);
		while (!LIST_EMPTY(&pflog_list))
			pflog_clone_destroy(
				&LIST_FIRST(&pflog_list)->sc_if);
		break;

	default:
		error = EINVAL;
		break;
	}

	return error;
}

static moduledata_t pflog_mod = {
	"pflog",
	pflog_modevent,
	0
};

#define PFLOG_MODVER 1

DECLARE_MODULE(pflog, pflog_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(pflog, PFLOG_MODVER);
